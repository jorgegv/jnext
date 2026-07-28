// Emulated ESP-01 AT engine unit tests (GH #25 — the AT model, and the
// optional threaded wrapper around it).
//
// HERMETIC BY CONSTRUCTION: every row drives `esp::AtEngine` against an
// in-memory `FakeTransport`. No socket is opened, no name is resolved, no
// listener is bound — the transport interface exists precisely so this suite
// never touches the network (esp_socket.h, "THE TEST SEAM").
//
// PORTABLE BY CONSTRUCTION TOO: nothing here names a jnext type. The suite
// ships with the module and builds against `esp01` alone. jnext's `UartDevice`
// adapter and the `Uart::tick` call site are tested in
// test/esp/esp_uart_adapter_test.cpp, on jnext's side of the line.
//
// BOTH DRIVE MODES ARE EXERCISED (group J). The core is passive and drivable
// inline; `ThreadedEsp` is optional. An optional mode that no row runs is a
// mode that rots, so both answer the same stimulus with the same bytes here.
//
// The rows assert EXACT BYTES, not semantics, because the bytes are the
// contract. Three guest parsers busy-wait on them with no timeout:
//   * `\r\nOK\r\n> ` (trailing space) for the CIPSEND prompt,
//   * `OK\r\n` framing, which `.ESPBAUD` compares EXACTLY,
//   * `+IPD,<len>:` unmultiplexed, read by nextsync's byte FSM.
// A row that only checked "contains OK" would pass against a response that
// hangs the emulated machine forever.
//
// The pacing rows matter as much as the parsing ones: nextsync reprograms the
// link to 1.152 Mbaud and a burst delivered per-frame would be 4.5x the
// 512-byte RX FIFO. They pin that delivery is one byte per
// `prescaler * frame_bits` and that idle time banks no credit.
//
// Run: ./build/test/esp_at_test

#include "esp01/esp_at.h"
#include "esp01/esp_log.h"
#include "esp01/esp_threaded.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace esp;

// ── Tiny test harness (matches esp_socket_test.cpp style) ─────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\r')      out += "\\r";
        else if (c == '\n') out += "\\n";
        else if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        }
    }
    return out;
}

/// Everything the module handed to the logging seam during a capture window,
/// concatenated. File-scope rather than captured by reference: `set_log_sink`
/// installs a `std::function` that outlives the window's scope by one
/// statement, and a sink referencing a dead local is a trap for whoever adds
/// the next row.
static std::string g_log;

static void check(const char* id, const std::string& desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc.c_str());
    }
}

static void check_eq(const char* id, const std::string& desc, const std::string& got,
                     const std::string& want) {
    ++g_total;
    if (got == want) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n        want \"%s\"\n        got  \"%s\"\n", id,
                    desc.c_str(), printable(want).c_str(), printable(got).c_str());
    }
}

// ── Fake transport ────────────────────────────────────────────────────────
//
// Scriptable, synchronous, in-memory. `poll()` is the only place a state
// transition happens, mirroring the real transport's contract that
// `begin_connect` never completes a connection.

class FakeTransport : public EspTransport {
public:
    // Script knobs.
    bool           refuse_begin      = false;                    ///< begin_connect returns false
    /// Model a host that silently black-holes SYN: poll() moves Resolving ->
    /// Connecting and then never progresses, exactly as a stateful firewall
    /// drop looks to the socket layer.
    bool           never_settles     = false;
    TransportState settle_state      = TransportState::Connected;///< where poll() lands from Resolving
    std::size_t    send_cap          = static_cast<std::size_t>(-1);  ///< bytes accepted per send()

    // Observations.
    std::string              sent;         ///< everything the engine handed us
    std::deque<std::uint8_t> inbox;        ///< what recv() will hand back
    std::string              last_host;
    std::uint16_t            last_port   = 0;
    int                      begin_calls = 0;
    int                      close_calls = 0;

    bool begin_connect(const std::string& host, std::uint16_t port) override {
        ++begin_calls;
        if (refuse_begin) return false;
        if (port == 0) return false;
        if (state_ == TransportState::Resolving || state_ == TransportState::Connecting ||
            state_ == TransportState::Connected)
            return false;
        last_host = host;
        last_port = port;
        state_    = TransportState::Resolving;
        return true;
    }

    void poll() override {
        if (never_settles) {
            if (state_ == TransportState::Resolving) state_ = TransportState::Connecting;
            return;  // and there it stays, forever
        }
        if (state_ == TransportState::Resolving) {
            state_ = settle_state;
            if (state_ == TransportState::Failed) error_ = "scripted failure";
        }
    }

    TransportState     state() const override { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }

    std::size_t send(const std::uint8_t* data, std::size_t len) override {
        if (state_ != TransportState::Connected) return 0;
        const std::size_t n = len < send_cap ? len : send_cap;
        sent.append(reinterpret_cast<const char*>(data), n);
        return n;
    }

    std::size_t recv(std::uint8_t* buf, std::size_t cap) override {
        std::size_t n = 0;
        while (n < cap && !inbox.empty()) {
            buf[n++] = inbox.front();
            inbox.pop_front();
        }
        return n;
    }

    void close() override {
        ++close_calls;
        if (state_ != TransportState::Idle) state_ = TransportState::Closed;
    }

    // Test-side stimulus.
    void queue_from_peer(const std::string& s) {
        for (unsigned char c : s) inbox.push_back(c);
    }
    void peer_closes() { state_ = TransportState::Closed; }

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

/// Counts `poll()` calls, atomically, and is DELIBERATELY declared so that it
/// outlives the wrapper that polls it — which is what lets a row observe
/// whether the worker thread is really gone after destruction.
class CountingPollTransport : public EspTransport {
public:
    std::atomic<int> polls{0};

    bool begin_connect(const std::string&, std::uint16_t) override { return false; }
    void poll() override { polls.fetch_add(1); }
    TransportState     state() const override { return TransportState::Idle; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override {}

private:
    std::string error_;
    IpAddress   peer_ = ipv4(192, 0, 2, 1);
};

/// Sleeps inside `poll()`, standing in for the transport's synchronous
/// `getaddrinfo` — the one place the worker holds the core for a long time.
class BlockingPollTransport : public EspTransport {
public:
    std::atomic<bool>         in_poll{false};
    std::atomic<int>          polls{0};
    std::chrono::milliseconds block_for{0};

    bool begin_connect(const std::string&, std::uint16_t) override { return false; }
    void poll() override {
        polls.fetch_add(1);   // counted on ENTRY, so `in_poll` and it agree
        const auto d = block_for;
        if (d.count() == 0) return;
        in_poll.store(true);
        std::this_thread::sleep_for(d);
        in_poll.store(false);
    }
    TransportState     state() const override { return TransportState::Idle; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override {}

private:
    std::string error_;
    IpAddress   peer_ = ipv4(192, 0, 2, 1);
};

/// Blocks inside `poll()` while a connect is outstanding — a deliberate
/// VIOLATION of `EspTransport::poll()`'s non-blocking contract, standing in for
/// the synchronous `getaddrinfo` the shipped transport still performs. Used to
/// prove that a badly-behaved transport cannot starve the guest-bound pacer.
class SlowResolveTransport : public EspTransport {
public:
    std::chrono::milliseconds block_for{500};
    std::atomic<bool>         in_poll{false};

    bool begin_connect(const std::string&, std::uint16_t) override {
        if (state_ != TransportState::Idle) return false;
        state_ = TransportState::Resolving;
        return true;
    }
    void poll() override {
        if (state_ != TransportState::Resolving) return;
        in_poll.store(true);
        std::this_thread::sleep_for(block_for);
        in_poll.store(false);
        state_ = TransportState::Connected;
    }
    TransportState     state() const override { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override { state_ = TransportState::Closed; }

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

/// HONOURS the non-blocking contract: a connect stays `Resolving` across many
/// polls, but no call ever blocks — the shape `fix/esp-async-dns` gives the
/// real transport, and the shape every third-party transport must have.
class AsyncResolveTransport : public EspTransport {
public:
    /// Polls to sit in `Resolving` before completing. Large enough that a
    /// resolve is reliably still outstanding when the wrapper is destroyed.
    int polls_before_connected = 1000000;

    bool begin_connect(const std::string&, std::uint16_t) override {
        if (state_ != TransportState::Idle) return false;
        state_ = TransportState::Resolving;
        return true;
    }
    void poll() override {
        if (state_ != TransportState::Resolving) return;
        if (++polls_ >= polls_before_connected) state_ = TransportState::Connected;
    }
    TransportState     state() const override { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override { state_ = TransportState::Closed; }

private:
    TransportState state_ = TransportState::Idle;
    int            polls_ = 0;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

/// Connects instantly but blocks inside `send()` — another deliberate contract
/// violation, and the only way to hold the engine lock (the LOCKED half of the
/// worker pass) long enough to observe whether `set_output` waits on it.
class SlowSendTransport : public EspTransport {
public:
    std::chrono::milliseconds send_delay{400};
    std::atomic<bool>         in_send{false};

    bool begin_connect(const std::string&, std::uint16_t) override {
        if (state_ != TransportState::Idle) return false;
        state_ = TransportState::Resolving;
        return true;
    }
    void poll() override {
        if (state_ == TransportState::Resolving) state_ = TransportState::Connected;
    }
    TransportState     state() const override { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t len) override {
        if (state_ != TransportState::Connected) return 0;
        in_send.store(true);
        std::this_thread::sleep_for(send_delay);
        in_send.store(false);
        return len;
    }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override { state_ = TransportState::Closed; }

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

// ── Rig ───────────────────────────────────────────────────────────────────

/// One byte time, in 28 MHz ticks, at the UART's 115200 8N1 default
/// (prescaler 243 x 10 frame bits). Any value works — the rows that care
/// about the rate vary it deliberately.
static constexpr std::uint32_t BYTE_TICKS = 243 * 10;

struct Rig {
    FakeTransport tr;
    AtEngine      eng{tr};
    std::string   guest;  ///< everything the engine has released toward the guest

    Rig() {
        eng.set_output([this](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
    }

    /// Guest transmits a string, byte by byte, exactly as `UartChannel`
    /// would deliver it.
    void send(const std::string& s) {
        for (unsigned char c : s) eng.receive(c);
    }
    void send(const std::vector<std::uint8_t>& v) {
        for (std::uint8_t b : v) eng.receive(b);
    }

    /// Release everything currently queued toward the guest, at
    /// `byte_ticks` per byte. Bounded so a stuck engine fails rather than
    /// spins.
    void drain(std::uint32_t byte_ticks = BYTE_TICKS) {
        for (int i = 0; i < 200000 && eng.wants_tick(); ++i) eng.tick(byte_ticks, byte_ticks);
    }

    /// poll() then drain() — the usual "let everything settle" step.
    void settle(std::uint32_t byte_ticks = BYTE_TICKS) {
        eng.poll();
        drain(byte_ticks);
    }

    std::string take() {
        std::string s;
        s.swap(guest);
        return s;
    }

    /// Bring the engine to a live connection, discarding the handshake bytes.
    void connect(const char* host = "example.test", int port = 2048) {
        send(std::string("AT+CIPSTART=\"TCP\",\"") + host + "\"," + std::to_string(port) + "\r\n");
        settle();
        take();
    }
};

// ─────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("\n=== ESP-01 AT engine tests (GH #25) ===\n\n");

    // No sink is installed, so the module is silent for every functional row
    // below and the suite's own output is the only thing on the console — the
    // seam's unbound default doing exactly what it promises. The TRACE group at
    // the end binds a capture sink, per window, and asserts what comes out.

    // ══ Group A — the command surface, byte-exact ═══════════════════════

    {   // nextsync's very first act, and its post-baud-switch probe.
        Rig r;
        r.send("\r\n");
        r.drain();
        check_eq("AT-01", "a bare CRLF is an empty command answered ERROR", r.take(),
                 "\r\nERROR\r\n");
    }
    {   Rig r; r.send("AT\r\n"); r.drain();
        check_eq("AT-02", "AT answers exactly \\r\\nOK\\r\\n", r.take(), "\r\nOK\r\n"); }
    {   Rig r; r.send("ATE0\r\n"); r.drain();
        check_eq("AT-03", "ATE0 answers OK", r.take(), "\r\nOK\r\n");
        check("AT-03b", "...and leaves echo off", !r.eng.echo_enabled()); }
    {   Rig r;
        r.send("ATE1\r\n"); r.drain();
        check_eq("AT-04",
                 "ATE1 is NOT echoed — echo was still off while its own bytes arrived",
                 r.take(), "\r\nOK\r\n");
        check("AT-04b", "...but echo is now really on", r.eng.echo_enabled());
        r.send("AT\r\n"); r.drain();
        check_eq("AT-04c", "so the NEXT line is echoed, terminator and all, before its reply",
                 r.take(), "AT\r\n\r\nOK\r\n");
        r.send("ATE0\r\n"); r.drain();
        check_eq("AT-04d", "and ATE0 still echoes itself before switching echo off", r.take(),
                 "ATE0\r\n\r\nOK\r\n"); }
    {   Rig r; r.send("AT+CIPMUX=0\r\n"); r.drain();
        check_eq("AT-05", "AT+CIPMUX=0 answers OK (the only supported mode)", r.take(),
                 "\r\nOK\r\n"); }
    {   Rig r; r.send("AT+CIPMUX=1\r\n"); r.drain();
        check_eq("AT-06",
                 "AT+CIPMUX=1 is REFUSED — accepting it would promise a +IPD form "
                 "nextsync cannot read and cannot ask back",
                 r.take(), "\r\nERROR\r\n"); }
    {   Rig r; r.send("AT+CIPCLOSE\r\n"); r.drain();
        check_eq("AT-07",
                 "AT+CIPCLOSE with nothing open answers ERROR (nextsync loops until it sees it)",
                 r.take(), "\r\nERROR\r\n"); }
    {   Rig r; r.send("AT+RST\r\n"); r.drain();
        check_eq("AT-08", "AT+RST answers OK then the two WIFI URCs, never 'ready'", r.take(),
                 "\r\nOK\r\n\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n"); }
    {   Rig r; r.send("AT+NONSENSE\r\n"); r.drain();
        check_eq("AT-09", "an unsupported command answers ERROR", r.take(), "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("\n"); r.drain();
        check_eq("AT-10", "a bare LF produces nothing — it is only ever the CR's partner",
                 r.take(), "");
        r.send("ATE1\r\n"); r.drain(); r.take();
        r.send("\n"); r.drain();
        check_eq("AT-10b", "...and is not echoed either, even with echo on", r.take(), ""); }
    {   Rig r; r.send("at+cipmux=0\r\n"); r.drain();
        check_eq("AT-11", "command names match case-insensitively", r.take(), "\r\nOK\r\n"); }
    {   // The line is built so that its first MAX_COMMAND_LEN characters are a
        // VALID AT+CIPSTART. A truncate-and-run implementation would therefore
        // open a connection the guest never asked for; refusing the whole line
        // is the only safe reading, and `begin_calls` is what proves which
        // happened — the reply is `ERROR` either way.
        const std::string head = "AT+CIPSTART=\"TCP\",\"";
        const std::string tail = "\",2048";
        const std::size_t hostlen = AtEngine::MAX_COMMAND_LEN - head.size() - tail.size();
        Rig r;
        r.send(head + std::string(hostlen, 'h') + tail + std::string(50, 'X') + "\r\n");
        r.settle();
        check_eq("AT-12", "an overlong line answers exactly one ERROR", r.take(),
                 "\r\nERROR\r\n");
        check("AT-12b",
              "...and is REFUSED WHOLE — its truncated prefix, a valid CIPSTART, is never run",
              r.tr.begin_calls == 0); }
    {   Rig r; r.send("AT+UART_CUR=1152000,8,1,0,0\r\n"); r.drain();
        check_eq("AT-13", "nextsync's baud switch is acknowledged", r.take(), "\r\nOK\r\n");
        check("AT-13b", "...and the requested baud is recorded for tracing",
              r.eng.requested_baud() == 1152000u); }
    {   Rig r;
        r.send("AT+UART_DEF=115200,8,1,0,0\r\n"); r.drain();
        check_eq("AT-14", "the _DEF form is accepted too", r.take(), "\r\nOK\r\n");
        r.send("AT+UART=2000000,8,1,0,0\r\n"); r.drain();
        check_eq("AT-14b", "as is the plain AT+UART form", r.take(), "\r\nOK\r\n");
        check("AT-14c", "syncfast's 2 Mbaud is recorded", r.eng.requested_baud() == 2000000u); }
    {   Rig r; r.send("AT+UART_CUR=fast,8,1,0,0\r\n"); r.drain();
        check_eq("AT-15", "a non-numeric baud answers ERROR, not a clamped number", r.take(),
                 "\r\nERROR\r\n"); }

    // ══ Group B — connect ═══════════════════════════════════════════════

    {   Rig r;
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        r.drain();
        check_eq("CON-01",
                 "AT+CIPSTART answers NOTHING until the transport settles — there is no "
                 "synchronous connect to answer from",
                 r.take(), "");
        check("CON-01b", "...and the engine reports it is waiting", r.eng.awaiting_connect());
        r.settle();
        check_eq("CON-02", "a settled connection answers OK", r.take(), "\r\nOK\r\n");
        check("CON-02b", "...the engine is connected", r.eng.connected());
        check("CON-02c", "...and the transport got the parsed host and port",
              r.tr.last_host == "example.test" && r.tr.last_port == 2048); }
    {   Rig r;
        r.tr.settle_state = TransportState::Failed;
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        r.settle();
        check_eq("CON-03",
                 "a failed connect answers ERROR only — never FAIL, never CLOSED for a "
                 "connection that never existed",
                 r.take(), "\r\nERROR\r\n");
        check("CON-03b", "...and the engine is not connected", !r.eng.connected()); }
    {   Rig r;  // NXtel's real form, with the keepalive 4th argument.
        r.send("AT+CIPSTART=\"TCP\",\"nx.nxtel.org\",23280,7200\r\n");
        r.settle();
        check_eq("CON-04", "NXtel's 4-argument CIPSTART (with keepalive) connects", r.take(),
                 "\r\nOK\r\n");
        check("CON-04b", "...with host and port parsed past the keepalive",
              r.tr.last_host == "nx.nxtel.org" && r.tr.last_port == 23280); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"example.test\",2048\r\n");
        r.settle();
        check_eq("CON-05", "UDP is refused — v1.0 is TCP only", r.take(), "\r\nERROR\r\n");
        check("CON-05b", "...and no connect was ever started", r.tr.begin_calls == 0); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPSTART=\"TCP\",\"other.test\",99\r\n");
        r.settle();
        check_eq("CON-06",
                 "a second CIPSTART while connected answers ERROR, not 'ALREADY CONNECTED'",
                 r.take(), "\r\nERROR\r\n");
        check("CON-06b",
              "...and is rejected by the ENGINE — the transport is never asked a second time",
              r.tr.begin_calls == 1); }
    {   Rig r;  // Type-ahead during a connect must not be lost or reordered.
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        r.send("AT\r\nAT+CIPMUX=0\r\n");
        r.drain();
        check_eq("CON-07", "guest input during a connect is deferred, not answered early",
                 r.take(), "");
        r.settle();
        check_eq("CON-07b", "...then replayed in order once the connect settles", r.take(),
                 "\r\nOK\r\n\r\nOK\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPCLOSE\r\n");
        r.drain();
        check_eq("CON-08", "closing a live connection reports CLOSED then OK", r.take(),
                 "\r\nCLOSED\r\n\r\nOK\r\n");
        check("CON-08b", "...and the transport was really closed", r.tr.close_calls > 0); }
    {   Rig r;
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",0\r\n");
        r.settle();
        check_eq("CON-09", "port 0 answers ERROR", r.take(), "\r\nERROR\r\n"); }
    {   // A well-formed keepalive followed by junk must not be waved through
        // just because the field before it parsed.
        Rig r;
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048,7200,XYZ\r\n");
        r.settle();
        check_eq("CON-04c", "trailing garbage after a VALID keepalive still answers ERROR",
                 r.take(), "\r\nERROR\r\n");
        check("CON-04d", "...and no connect was attempted", r.tr.begin_calls == 0); }
    {   // A host that silently black-holes SYN. Without a deadline the guest
        // gets NOTHING until the OS abandons the handshake (~127 s on Linux),
        // and NXtel's post-Connect wait has no timeout and runs under `di` —
        // so this is a guest freeze, not a slow connect.
        Rig r;
        r.tr.never_settles = true;
        r.eng.set_connect_timeout(std::chrono::milliseconds(0));
        r.send("AT+CIPSTART=\"TCP\",\"blackhole.test\",2048\r\n");
        r.eng.poll();
        r.drain();
        const std::string first = r.take();
        // Bounded: a working deadline fires on one of the first few polls.
        for (int i = 0; i < 10 && r.eng.awaiting_connect(); ++i) r.settle();
        check_eq("CON-11", "a connect that never completes is abandoned with ERROR",
                 first + r.take(), "\r\nERROR\r\n");
        check("CON-11b", "...and the engine stops waiting", !r.eng.awaiting_connect());
        check("CON-11c", "...having released the socket", r.tr.close_calls > 0);
        // The slot must be REUSABLE, not wedged.
        r.tr.never_settles = false;
        r.eng.set_connect_timeout(std::chrono::milliseconds(10000));
        r.send("AT+CIPSTART=\"TCP\",\"good.test\",80\r\n");
        r.settle();
        check_eq("CON-12", "...and the slot is reusable afterwards", r.take(), "\r\nOK\r\n");
        check("CON-12b", "...really connected", r.eng.connected()); }
    {   // The deadline must not fire on a connect that is merely in progress.
        Rig r;
        r.tr.never_settles = true;
        r.send("AT+CIPSTART=\"TCP\",\"slow.test\",2048\r\n");
        for (int i = 0; i < 20; ++i) r.settle();
        check_eq("CON-13", "a connect inside its deadline is still awaited, not refused",
                 r.take(), "");
        check("CON-13b", "...and remains pending", r.eng.awaiting_connect()); }
    {   Rig r;
        r.tr.refuse_begin = true;
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        r.drain();
        check_eq("CON-10", "a transport that refuses the request answers ERROR immediately",
                 r.take(), "\r\nERROR\r\n"); }

    // ══ Group C — the send path ═════════════════════════════════════════

    {   Rig r;
        r.connect();
        r.send("AT+CIPSEND=3\r\n");
        r.drain();
        check_eq("SEND-01",
                 "AT+CIPSEND answers \\r\\nOK\\r\\n> — TRAILING SPACE INCLUDED; three "
                 "parsers busy-wait on this with no timeout",
                 r.take(), "\r\nOK\r\n> ");
        check("SEND-01b", "...and 3 payload bytes are outstanding",
              r.eng.payload_outstanding() == 3); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPSEND=3\r\n");
        r.drain(); r.take();
        r.send("abc");
        r.drain();
        check_eq("SEND-02", "the completed payload answers \\r\\nSEND OK\\r\\n", r.take(),
                 "\r\nSEND OK\r\n");
        check("SEND-02b", "...and exactly the payload reached the peer", r.tr.sent == "abc"); }
    {   // NXtel's REAL byte accounting, from esp.asm:44-49:
        //   ESPSend("AT+CIPSEND=3") / ESPSendBytes(db 255,253,39,CR,LF)  -> 5 bytes
        // The trailing CR,LF is structural (ESPSendProc resumes with `jp (hl)`
        // after the inline block), NOT payload.
        Rig r;
        r.connect();
        r.send("AT+CIPSEND=3\r\n");
        r.drain(); r.take();
        r.send(std::vector<std::uint8_t>{255, 253, 39, '\r', '\n'});
        r.drain();
        check_eq("SEND-03",
                 "NXtel's 5 bytes after CIPSEND=3: 3 are payload and the trailing CRLF "
                 "becomes an empty command line",
                 r.take(), "\r\nSEND OK\r\n\r\nERROR\r\n");
        check("SEND-03b", "...and the peer got exactly the 3 IAC bytes, not 5",
              r.tr.sent == std::string("\xFF\xFD\x27", 3)); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPSENDEX=3\r\n");
        r.drain();
        check_eq("SEND-04", "AT+CIPSENDEX is a distinct command with the same prompt", r.take(),
                 "\r\nOK\r\n> ");
        r.send("xyz"); r.drain();
        check_eq("SEND-04b", "...and the same completion", r.take(), "\r\nSEND OK\r\n");
        check("SEND-04c", "...delivering the payload", r.tr.sent == "xyz"); }
    {   Rig r;
        r.send("AT+CIPSEND=1\r\n"); r.drain();
        check_eq("SEND-05", "AT+CIPSEND with no connection answers ERROR — and no prompt, "
                            "which would hang the guest waiting to send",
                 r.take(), "\r\nERROR\r\n"); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPSEND=0\r\n"); r.drain();
        check_eq("SEND-06", "a zero length answers ERROR", r.take(), "\r\nERROR\r\n");
        r.send("AT+CIPSEND=2049\r\n"); r.drain();
        check_eq("SEND-06b", "...as does one over the 2048-byte ceiling", r.take(),
                 "\r\nERROR\r\n");
        // The boundary itself: rejecting 2049 proves nothing about where the
        // limit is unless the largest LEGAL length is proven to be accepted.
        r.send("AT+CIPSEND=2048\r\n"); r.drain();
        check_eq("SEND-06c", "...but exactly 2048 IS accepted, prompt and all", r.take(),
                 "\r\nOK\r\n> ");
        check("SEND-06d", "...with the full payload outstanding",
              r.eng.payload_outstanding() == 2048); }
    {   // 8-bit clean is a stated goal: CSpect fails here and NXtel needs it.
        Rig r;
        r.connect();
        r.send("AT+CIPSEND=4\r\n"); r.drain(); r.take();
        r.send(std::vector<std::uint8_t>{0x00, 0x1B, 0x80, 0xFF});
        r.drain();
        check("SEND-07", "the send path is 8-bit clean, NUL and ESC included",
              r.tr.sent == std::string("\x00\x1B\x80\xFF", 4)); }
    {   Rig r;
        r.connect();
        r.tr.send_cap = 2;  // kernel accepts 2 bytes at a time
        r.send("AT+CIPSEND=6\r\n"); r.drain(); r.take();
        r.send("abcdef"); r.drain();
        check_eq("SEND-08", "a partial socket accept still answers SEND OK exactly once",
                 r.take(), "\r\nSEND OK\r\n");
        check("SEND-08b", "...with only what the kernel took so far delivered",
              r.tr.sent == "ab");
        r.settle(); r.settle(); r.settle();
        check("SEND-08c", "...and the remainder flushed by later polls", r.tr.sent == "abcdef"); }
    {   Rig r;
        r.connect();
        r.send("ATE1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSEND=2\r\n"); r.drain(); r.take();
        r.send("hi"); r.drain();
        check_eq("SEND-09", "payload bytes are never echoed, even with echo on", r.take(),
                 "\r\nSEND OK\r\n"); }

    // ══ Group D — +IPD framing ══════════════════════════════════════════

    {   Rig r;
        r.connect();
        r.tr.queue_from_peer("HELLO");
        r.settle();
        check_eq("IPD-01", "inbound data is framed as the unmultiplexed +IPD,<len>: form",
                 r.take(), "\r\n+IPD,5:HELLO"); }
    {   Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string("\x00\x1B\x80\xFF+", 5));
        r.settle();
        check_eq("IPD-02", "+IPD is 8-bit clean and <len> counts raw bytes",
                 r.take(), std::string("\r\n+IPD,5:\x00\x1B\x80\xFF+", 14)); }
    {   // A chunk is cut only when the guest-bound queue has DRAINED, which is
        // what keeps a busy peer producing few large chunks instead of many
        // tiny ones — nextsync budgets 5 chunks per server packet. Here four
        // bytes trickle in one poll at a time while an earlier chunk is still
        // going out: they must leave as ONE following chunk, not four.
        Rig r;
        r.connect();
        r.tr.queue_from_peer("AAAA");
        r.eng.poll();
        for (int i = 0; i < 4; ++i) {
            r.tr.queue_from_peer("B");
            r.eng.poll();
            r.eng.tick(BYTE_TICKS, BYTE_TICKS);
        }
        r.drain();
        check_eq("IPD-03",
                 "bytes trickling in while a chunk drains coalesce into ONE following chunk",
                 r.take(), "\r\n+IPD,4:AAAA\r\n+IPD,4:BBBB"); }
    {   // nextsync's FSM scans for the FIRST '+', so nothing between a sent
        // payload and its +IPD may contain one.
        Rig r;
        r.connect();
        r.send("AT+CIPSEND=2\r\n"); r.drain(); r.take();
        r.tr.queue_from_peer("PONG");
        r.send("hi");
        r.drain();
        r.settle();
        const std::string stream = r.take();
        const std::size_t plus   = stream.find('+');
        check_eq("IPD-04", "SEND OK then +IPD, with no stray '+' between them", stream,
                 "\r\nSEND OK\r\n\r\n+IPD,4:PONG");
        check("IPD-04b", "...the first '+' in the stream is the +IPD's own",
              plus != std::string::npos && stream.compare(plus, 5, "+IPD,") == 0); }
    {   Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string(3000, 'z'));
        r.settle();
        const std::string s = r.take();
        check("IPD-05", "a 3000-byte burst is split at the 2048-byte chunk ceiling",
              s.compare(0, 12, "\r\n+IPD,2048:") == 0);
        check("IPD-05b", "...and the remainder is a second chunk, not a dribble",
              s.find("\r\n+IPD,952:") == 2 + 10 + 2048);
        check("IPD-05c", "...totalling exactly the payload plus two headers",
              s.size() == 12 + 2048 + 11 + 952); }
    {   // The header must not straddle a delivery gap: after the leading '+'
        // every header byte gets one ~314 us window with no outer retry.
        Rig r;
        r.connect();
        r.tr.queue_from_peer("QQQQ");
        r.eng.poll();
        std::string  seen;
        bool         gap_after_plus = false;
        bool         started        = false;
        for (int i = 0; i < 64 && r.eng.wants_tick(); ++i) {
            const std::size_t before = r.guest.size();
            r.eng.tick(BYTE_TICKS, BYTE_TICKS);
            const std::size_t after = r.guest.size();
            if (started && after == before) gap_after_plus = true;
            if (!started && r.guest.find('+') != std::string::npos) started = true;
        }
        seen = r.take();
        check("IPD-07",
              "once the header starts, every byte-slot delivers a byte — no gap can open "
              "inside +IPD,<len>:",
              !gap_after_plus);
        check("IPD-07b", "...and the header arrived intact", seen == "\r\n+IPD,4:QQQQ"); }
    {   Rig r;
        r.connect();
        // Data and the close land in the SAME poll — the case in which an
        // eager CLOSED would overtake the peer's last bytes.
        r.tr.queue_from_peer("BYE");
        r.tr.peer_closes();
        r.settle();
        check_eq("IPD-08",
                 "a peer close is reported only AFTER its last bytes have been framed",
                 r.take(), "\r\n+IPD,3:BYE\r\nCLOSED\r\n"); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPM");   // a half-typed command line, so the wire is not quiet
        r.tr.queue_from_peer("DATA");
        r.eng.poll();
        r.drain();
        check_eq("IPD-09", "no +IPD is cut while a command line is half-received", r.take(), "");
        r.send("UX=0\r\n");
        r.drain();
        check_eq("IPD-09b", "...it follows the completed command's reply", r.take(),
                 "\r\nOK\r\n\r\n+IPD,4:DATA"); }
    {   Rig r;
        r.connect();
        r.send("AT+CIPSEND=4\r\n"); r.drain(); r.take();
        r.tr.queue_from_peer("LATE");
        r.eng.poll();
        r.send("ab");        // payload only half sent
        r.drain();
        check_eq("IPD-10", "no +IPD is cut between the '>' prompt and the payload's SEND OK",
                 r.take(), "");
        r.send("cd"); r.drain();
        check_eq("IPD-10b", "...it follows SEND OK", r.take(),
                 "\r\nSEND OK\r\n\r\n+IPD,4:LATE"); }

    // ══ Group E — pacing ════════════════════════════════════════════════

    {   Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string(100, 'p'));
        r.eng.poll();
        for (int i = 0; i < 10; ++i) r.eng.tick(BYTE_TICKS, BYTE_TICKS);
        check("PACE-01",
              "a burst is drip-fed one byte per byte-time, never dumped into the 512-byte FIFO",
              r.guest.size() == 10); }
    {   Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string(100, 'p'));
        r.eng.poll();
        r.eng.tick(BYTE_TICKS * 10, BYTE_TICKS);
        check("PACE-02", "a 10-byte-time span releases exactly 10 bytes",
              r.guest.size() == 10); }
    {   Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string(10, 'p'));
        r.eng.poll();
        r.eng.tick(BYTE_TICKS / 2, BYTE_TICKS);
        const bool none_yet = r.guest.empty();
        r.eng.tick(BYTE_TICKS / 2, BYTE_TICKS);
        check("PACE-03", "sub-byte spans accumulate rather than rounding up to a byte",
              none_yet && r.guest.size() == 1); }
    {   Rig r;
        r.connect();
        // Long idle with nothing queued, then data appears.
        for (int i = 0; i < 50; ++i) r.eng.tick(BYTE_TICKS, BYTE_TICKS);
        r.tr.queue_from_peer(std::string(100, 'p'));
        r.eng.poll();
        r.eng.tick(BYTE_TICKS, BYTE_TICKS);
        check("PACE-04",
              "idle time banks no credit — otherwise a quiet link would burst at unbounded "
              "speed the instant data arrived",
              r.guest.size() == 1); }
    {   // The rate follows the LIVE prescaler, which is what makes .ESPBAUD
        // and nextsync's 1.152 Mbaud switch work without any extra plumbing.
        Rig r;
        r.connect();
        r.tr.queue_from_peer(std::string(100, 'p'));
        r.eng.poll();
        r.eng.tick(BYTE_TICKS, BYTE_TICKS / 10);   // 10x faster link
        check("PACE-05", "a faster byte-time delivers proportionally more bytes",
              r.guest.size() == 10); }
    {   // The post-drain reset, pinned across a drain -> refill boundary that
        // is mediated by a DIRECT queue() (another AT reply), not by poll().
        // Without the reset the sub-byte remainder of the first burst survives
        // and the next burst's first byte leaves up to a whole byte-time early.
        Rig r;
        r.send("AT\r\n");                                   // 6 bytes queued
        r.eng.tick(BYTE_TICKS * 6 + (BYTE_TICKS - 1), BYTE_TICKS);
        check("PACE-07", "a span drains the whole reply", r.guest.size() == 6);
        r.send("AT\r\n");                                   // 6 more, via queue(), not poll()
        r.eng.tick(1, BYTE_TICKS);
        check("PACE-07b",
              "...and the leftover sub-byte credit does NOT survive into the next burst",
              r.guest.size() == 6);
        r.eng.tick(BYTE_TICKS - 1, BYTE_TICKS);
        check("PACE-07c", "...the next byte arrives a full byte-time after the refill",
              r.guest.size() == 7); }
    {   Rig r;
        r.connect();
        r.tr.queue_from_peer("x");
        r.eng.poll();
        r.eng.tick(1, 0);   // degenerate prescaler
        check("PACE-06", "a zero byte-time neither divides by zero nor hangs",
              r.guest.size() == 1); }

    // ══ Group F — the tick gate ═════════════════════════════════════════
    //
    // The gate itself is the module's; jnext's `UartDevice` mirror of it, and
    // the `Uart::tick` call site that consumes it, are tested where they live
    // — test/esp/esp_uart_adapter_test.cpp.

    {   Rig r;
        check("HOOK-01", "an idle engine lowers the tick gate", !r.eng.wants_tick());
        r.send("AT\r\n");
        check("HOOK-02", "queued output raises it", r.eng.wants_tick());
        r.drain();
        check("HOOK-02b", "...and draining lowers it again", !r.eng.wants_tick()); }

    // ══ Group G — static diagnostics (NXtel's Network Settings screen) ══

    {   Rig r; r.send("AT+CWJAP?\r\n"); r.drain();
        const std::string s = r.take();
        check("DIAG-01", "AT+CWJAP? carries NXtel's CWJAP:\" SSID anchor",
              s.find("+CWJAP:\"JNextWifiHost\"") != std::string::npos);
        check("DIAG-01b", "...and the \",\" anchor that precedes the AP MAC",
              s.find("\",\"" ) != std::string::npos);
        check("DIAG-01c", "...ending in an OK", s.size() >= 6 &&
              s.compare(s.size() - 6, 6, "\r\nOK\r\n") == 0); }
    {   Rig r; r.send("AT+CIFSR\r\n"); r.drain();
        const std::string s = r.take();
        check("DIAG-02", "AT+CIFSR carries the TAIP,\" and TAMAC,\" anchors",
              s.find("TAIP,\"") != std::string::npos &&
              s.find("TAMAC,\"") != std::string::npos); }
    {   Rig r; r.send("AT+CIPSTA?\r\n"); r.drain();
        const std::string s = r.take();
        check("DIAG-03", "AT+CIPSTA? carries the gateway:\" and netmask:\" anchors",
              s.find("gateway:\"") != std::string::npos &&
              s.find("netmask:\"") != std::string::npos); }
    {   Rig r; r.send("AT+GMR\r\n"); r.drain();
        const std::string s = r.take();
        const std::size_t at  = s.find("T version:");
        const std::size_t sdk = s.find("DK version:");
        check("DIAG-04", "AT+GMR carries both version anchors", at != std::string::npos &&
              sdk != std::string::npos);
        // NXtel prints from the anchor up to the next '('. If the '(' is on a
        // LATER line the field renders as several lines of garbage, so the
        // terminator must sit inside the anchor's own line.
        auto paren_on_same_line = [&s](std::size_t anchor) {
            if (anchor == std::string::npos) return false;
            const std::size_t eol   = s.find("\r\n", anchor);
            const std::size_t paren = s.find('(', anchor);
            return paren != std::string::npos && (eol == std::string::npos || paren < eol);
        };
        check("DIAG-04b",
              "...each terminated by a '(' on its OWN line, so neither field renders as garbage",
              paren_on_same_line(at) && paren_on_same_line(sdk)); }

    {   Rig r; r.send("AT+CIPDNS_CUR?\r\n"); r.drain();
        const std::string s = r.take();
        check("DIAG-05", "AT+CIPDNS_CUR? carries the +CIPDNS_CUR: anchor twice",
              s.find("+CIPDNS_CUR:") != std::string::npos &&
              s.find("+CIPDNS_CUR:", s.find("+CIPDNS_CUR:") + 1) != std::string::npos); }
    {   Rig r;
        bool all_ok = true;
        for (const char* cmd : {"AT+CWJAP?\r\n", "AT+CIFSR\r\n", "AT+CIPSTA?\r\n",
                                "AT+GMR\r\n", "AT+CIPDNS_CUR?\r\n"}) {
            r.send(cmd);
            r.drain();
            const std::string s = r.take();
            if (s.size() < 6 || s.compare(s.size() - 6, 6, "\r\nOK\r\n") != 0) all_ok = false;
        }
        check("DIAG-06",
              "every diagnostic reply terminates with the exact OK framing .ESPBAUD compares",
              all_ok); }
    {   check("DIAG-07",
              "the advertised SSID is the fixed synthetic literal, never a host network",
              std::string(AtEngine::SSID) == "JNextWifiHost"); }

    // ══ Group H — what must NEVER be emitted ════════════════════════════

    {   // One full session across every code path that could tempt a real
        // firmware into one of the forbidden URCs.
        Rig r;
        r.send("\r\nATE0\r\nAT+CIPCLOSE\r\nAT+CIPMUX=0\r\n");
        r.settle();
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        r.settle();
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");  // already connected
        r.settle();
        r.send("AT+CIPSEND=2\r\n");
        r.settle();
        r.tr.send_cap = 0;                                        // peer will take nothing
        r.send("hi");
        r.settle();
        r.tr.queue_from_peer("D");
        r.settle();
        r.send("AT+RST\r\n");
        r.settle();
        const std::string s = r.take();
        bool clean = true;
        for (const char* forbidden : {"busy p", "ALREADY CONNECTED", "SEND FAIL",
                                      "link is not valid", "no ip", "\r\nready\r\n"}) {
            if (s.find(forbidden) != std::string::npos) {
                clean = false;
                std::printf("        forbidden URC in stream: \"%s\"\n", forbidden);
            }
        }
        check("NEVER-01", "a full session emits none of the never-emit URCs", clean);
        check("NEVER-02",
              "...and AT+RST drops the connection without an unsolicited CLOSED",
              s.find("CLOSED") == std::string::npos); }

    // ══ Group I — the tracing contract ══════════════════════════════════
    //
    // The owner made tracing a first-class requirement with a hard default:
    // NOTHING on by default except connection open/close. These rows pin that
    // from the outside, by capturing what the engine hands to the MODULE'S OWN
    // logging seam (esp01/esp_log.h) — not to spdlog. That is what makes them
    // runnable by a consumer who binds a different sink, and it makes them a
    // test of the seam rather than of somebody else's logging library.
    {
        auto capture = [](LogLevel level, const std::function<void()>& work) {
            g_log.clear();
            set_log_sink([](LogLevel, const std::string& m) { g_log += m; });
            set_log_threshold(level);
            work();
            set_log_sink(nullptr);
            set_log_threshold(LogLevel::Info);
            return g_log;
        };

        // A whole session — commands, prompt, payload, +IPD, close — run at
        // the DEFAULT level. Only the two connection events may appear.
        const std::string quiet = capture(LogLevel::Info, [] {
            Rig r;
            r.send("ATE0\r\nAT+CIPMUX=0\r\n");
            r.settle();
            r.connect();
            r.send("AT+CIPSEND=2\r\n"); r.settle();
            r.send("hi");               r.settle();
            r.tr.queue_from_peer("yo"); r.settle();
            r.send("AT+CIPCLOSE\r\n");  r.settle();
        });
        check("TRACE-01", "at the default level a connection open is reported",
              quiet.find("opened") != std::string::npos);
        check("TRACE-02", "...and the close",
              quiet.find("closed by the guest") != std::string::npos);
        check("TRACE-03",
              "...and NOTHING else — no AT chatter, no prompt, no +IPD, no pacing",
              quiet.find("AT <-") == std::string::npos &&
                  quiet.find("AT ->") == std::string::npos &&
                  quiet.find("+IPD framing") == std::string::npos &&
                  quiet.find("paced") == std::string::npos);

        const std::string dbg = capture(LogLevel::Debug, [] {
            Rig r;
            r.connect();
            r.send("AT+CIPSEND=2\r\n"); r.settle();
            r.send("hi");               r.settle();
            r.tr.queue_from_peer("yo"); r.settle();
        });
        check("TRACE-04", "at debug every AT command received is traced",
              dbg.find("AT <- \"AT+CIPSEND=2\"") != std::string::npos);
        check("TRACE-05", "...every response emitted is traced, escaped so framing is visible",
              dbg.find("AT -> \"\\r\\nOK\\r\\n> \"") != std::string::npos);
        check("TRACE-06", "...the payload byte count is traced",
              dbg.find("payload complete: 2 byte(s)") != std::string::npos);
        check("TRACE-07", "...and the +IPD framing decision is traced",
              dbg.find("+IPD framing 2 byte(s)") != std::string::npos);
        check("TRACE-08", "...but per-byte pacing is not — that is trace level",
              dbg.find("paced") == std::string::npos);

        const std::string trc = capture(LogLevel::Trace, [] {
            Rig r;
            r.connect();
            r.tr.queue_from_peer("yo");
            r.settle();
        });
        check("TRACE-09", "at trace the RX pacing and queue state are visible",
              trc.find("paced") != std::string::npos &&
                  trc.find("ticks/byte") != std::string::npos);
    }

    // ══ Group J — BOTH DRIVE MODES ══════════════════════════════════════
    //
    // The threaded wrapper is OPTIONAL: the core must stay drivable inline, or
    // the wrapper is not optional, it is mandatory-with-extra-steps. So both
    // modes are exercised here, against the same fake transport and the same
    // expected bytes — an alternative mode nothing runs is an alternative mode
    // that rots.
    //
    // Determinism: no row sleeps for a guessed interval. `wait_idle` reports
    // when the worker has drained what it was given and completed a pass, and
    // every loop below is bounded so a stuck wrapper FAILS rather than hangs.

    {   // (a) Constructed but NEVER STARTED — the wrapper must behave exactly
        //     like the bare core, byte for byte. This is the block that proves
        //     "optional" is true, and it deliberately exercises `receive` and
        //     `poll` SEPARATELY: an earlier version called both before
        //     asserting, so either path alone satisfied it and mutations that
        //     broke one were masked by the other (both verified to survive).
        FakeTransport tr;
        ThreadedEsp   esp{tr};
        std::string   guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        check("MODE-01", "an unstarted wrapper is not running", !esp.running());

        // No poll() at all: with no worker, `receive` must reach the core
        // directly rather than parking the byte in the inbound queue.
        for (char c : std::string("AT\r\n")) esp.receive(static_cast<std::uint8_t>(c));
        for (int i = 0; i < 64 && esp.wants_tick(); ++i) esp.tick(BYTE_TICKS, BYTE_TICKS);
        check_eq("MODE-02", "driven INLINE, receive() alone answers as the bare core does",
                 guest, "\r\nOK\r\n");
        guest.clear();

        // And the WALL-CLOCK half must still be reachable inline: a connect's
        // reply is deferred until the transport settles, which only `poll()`
        // does. Without a working inline poll() this reply never comes.
        for (char c : std::string("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n"))
            esp.receive(static_cast<std::uint8_t>(c));
        for (int i = 0; i < 64 && esp.wants_tick(); ++i) esp.tick(BYTE_TICKS, BYTE_TICKS);
        check("MODE-02b", "...and a connect's reply is still deferred, not invented",
              guest.empty());
        esp.poll();
        for (int i = 0; i < 64 && esp.wants_tick(); ++i) esp.tick(BYTE_TICKS, BYTE_TICKS);
        check_eq("MODE-02c", "...until an inline poll() settles the transport", guest,
                 "\r\nOK\r\n"); }

    {   // (b) Started. Same stimulus, same bytes — only the thread differs.
        FakeTransport tr;
        ThreadedEsp   esp{tr};
        std::string   guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        esp.start();
        check("MODE-03", "start() brings the worker up", esp.running());
        for (char c : std::string("AT\r\n")) esp.receive(static_cast<std::uint8_t>(c));
        bool settled = esp.wait_idle(2000);
        for (int i = 0; i < 64 && esp.wants_tick(); ++i) esp.tick(BYTE_TICKS, BYTE_TICKS);
        check("MODE-04", "the worker drains guest input without being polled", settled);
        check_eq("MODE-05", "driven THREADED it answers with the identical bytes", guest,
                 "\r\nOK\r\n");
        esp.stop();
        check("MODE-06", "stop() joins and reports it", !esp.running());
        esp.stop();
        check("MODE-07", "...and stop() is idempotent", !esp.running()); }

    {   // (c) A full session on the worker: connect, prompt, payload, +IPD.
        //     The socket half is what actually runs on the other thread, so a
        //     row that only sent `AT` would not exercise it.
        FakeTransport tr;
        ThreadedEsp   esp{tr};
        std::string   guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        esp.start();

        auto pump = [&](const std::string& s) {
            for (unsigned char c : s) esp.receive(c);
            for (int i = 0; i < 200; ++i) {
                esp.wait_idle(50);
                for (int j = 0; j < 4096 && esp.wants_tick(); ++j)
                    esp.tick(BYTE_TICKS, BYTE_TICKS);
                if (!esp.wants_tick()) break;
            }
        };
        pump("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n");
        check_eq("MODE-08", "a connect completes on the worker thread", guest, "\r\nOK\r\n");
        guest.clear();

        pump("AT+CIPSEND=2\r\n");
        check_eq("MODE-09", "...the CIPSEND prompt still comes back byte-exact", guest,
                 "\r\nOK\r\n> ");
        guest.clear();

        pump("hi");
        check_eq("MODE-10", "...the payload is acknowledged", guest, "\r\nSEND OK\r\n");
        check("MODE-11", "...and really reached the transport", tr.sent == "hi");
        guest.clear();

        tr.queue_from_peer("yo");
        // No guest input this time: the worker must notice peer data on its own.
        for (int i = 0; i < 200 && guest.size() < 12; ++i) {
            esp.wait_idle(50);
            for (int j = 0; j < 4096 && esp.wants_tick(); ++j)
                esp.tick(BYTE_TICKS, BYTE_TICKS);
        }
        check_eq("MODE-12", "unsolicited peer data is framed and paced out unprompted", guest,
                 "\r\n+IPD,2:yo");
        esp.stop(); }

    {   // (d) THE DESTRUCTOR JOINS. This is the row the lifetime contract
        //     exists for: a wrapper that detached instead of joining would
        //     leave a thread running over a destroyed object, and under
        //     jnext's cold boot — placement-new at the same address — that is
        //     silent corruption of the newly booted machine, not a crash.
        //
        //     "It did not crash" is NOT a test of that, and neither is "the
        //     thread stopped eventually": a detach plus a stop flag usually
        //     does stop promptly, which is why the first version of this row
        //     SURVIVED replacing join() with detach(). What distinguishes them
        //     is WHEN the destructor returns, so the wrapper is destroyed
        //     while the worker is provably inside a long poll(): a join cannot
        //     return until that poll() has, a detach returns straight through
        //     it. `in_poll` right after destruction is therefore a direct
        //     observation of the join, needing no sleep and no guesswork.
        BlockingPollTransport tr;   // outlives the wrapper, deliberately
        tr.block_for = std::chrono::milliseconds(200);
        {
            ThreadedEsp esp{tr};
            esp.start();
            for (int i = 0; i < 2000 && !tr.in_poll.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check("MODE-13", "the worker really ran while the wrapper was alive",
                  tr.in_poll.load() && tr.polls.load() > 0);
            // Destroyed here, WITHOUT an explicit stop() and with the worker
            // mid-poll — the shape a forgetful owner produces.
        }
        check("MODE-14",
              "destroying a running wrapper JOINS: the destructor cannot return "
              "while the worker is still inside poll()",
              !tr.in_poll.load()); }

    {   // (e) `tick()` must not inherit a worker stall. The worker holds the
        //     core across the transport's SYNCHRONOUS DNS lookup, which can be
        //     seconds; a blocking lock in `tick()` would hand that stall to
        //     the emulation thread, which is the whole thing this class exists
        //     to avoid.
        //
        //     The bound is deliberately huge — the worker blocks for 300 ms
        //     and `tick()` is allowed 200 ms — because a try-lock `tick()`
        //     takes microseconds. Only a genuinely blocking implementation can
        //     exceed it, so this cannot flake under a loaded box; it just has
        //     to not be a blocking lock.
        BlockingPollTransport tr;
        ThreadedEsp           esp{tr};
        std::string           guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        tr.block_for = std::chrono::milliseconds(300);
        esp.start();
        // Wait until the worker is provably inside the blocking poll().
        for (int i = 0; i < 2000 && !tr.in_poll.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check("MODE-15", "the worker is inside a long poll()", tr.in_poll.load());

        const auto t0 = std::chrono::steady_clock::now();
        esp.tick(BYTE_TICKS, BYTE_TICKS);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check("MODE-16", "tick() returns immediately rather than waiting for it", ms < 200);
        tr.block_for = std::chrono::milliseconds(0);
        esp.stop(); }

    // ══ Group K — the worker must not starve the wire ═══════════════════
    //
    // Three properties, all of them found MISSING by review rather than by a
    // green test, and all three measured on the version that lacked them.
    // Their common cause was one line: the worker held the engine lock across
    // `EspTransport::poll()`.

    {   // STALL-01 — THE ONE THAT MATTERS. Bytes already queued for the guest
        //     must keep flowing at their paced rate while the transport is
        //     stalled. Measured on the version that held the lock across the
        //     transport poll: 11 827 226 `tick()` calls over 400 ms of a
        //     500 ms stall delivered ZERO of these 39 bytes.
        //
        //     This is not a latency nicety. The emulation thread is not
        //     blocked — that is the entire point of the wrapper — so real
        //     T-states elapse throughout, and the receivers on the far end of
        //     this wire have no retry.
        SlowResolveTransport tr;
        tr.block_for = std::chrono::milliseconds(500);
        ThreadedEsp esp{tr};
        std::string guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });

        // Both events land INLINE, before the worker exists, so the ordering
        // is deterministic rather than raced: AT+RST queues its 36-byte reply
        // into `out_`, then AT+CIPSTART puts the transport into Resolving.
        for (unsigned char c : std::string("AT+RST\r\n")) esp.receive(c);
        const std::string reset_reply = "\r\nOK\r\n\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n";
        for (unsigned char c : std::string("AT+CIPSTART=\"TCP\",\"example.test\",80\r\n"))
            esp.receive(c);
        check("STALL-01a", "nothing has been delivered yet — no tick() has run", guest.empty());

        esp.start();
        for (int i = 0; i < 5000 && !tr.in_poll.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check("STALL-01b", "the worker is provably stalled inside the transport poll",
              tr.in_poll.load());

        // Pace from THIS thread — standing in for the emulation thread, which
        // keeps executing Z80 instructions regardless of any ESP-side stall.
        for (int i = 0; i < 4096 && guest.size() < reset_reply.size(); ++i)
            esp.tick(BYTE_TICKS, BYTE_TICKS);
        const bool still_stalled = tr.in_poll.load();
        check_eq("STALL-01", "queued bytes reach the wire DURING a transport stall", guest,
                 reset_reply);
        check("STALL-01c", "...and they did so while the stall was still in progress",
              still_stalled);
        esp.stop(); }

    {   // STALL-02 — shutdown is bounded when the transport honours its
        //     contract. Measured on a CONTRACT-VIOLATING transport that blocks
        //     2000 ms in poll(): the destructor took 2007 ms, which in jnext
        //     is the emulator frozen for two seconds on Reset. That case
        //     cannot be fixed here — joining a thread parked in a syscall is
        //     unbounded for anyone — so it is a stated contract
        //     (`EspTransport::poll`) and the row pins the other side of it:
        //     given a transport that does NOT block, destruction while a
        //     connect is still outstanding must be prompt.
        //
        //     The poll interval is deliberately 2 SECONDS, far longer than the
        //     bound asserted. That is what makes the row discriminative: it
        //     fails unless shutdown actively wakes the worker, rather than
        //     waiting for its next scheduled pass.
        AsyncResolveTransport tr;
        auto esp = std::unique_ptr<ThreadedEsp>(
            new ThreadedEsp(tr, std::chrono::milliseconds(2000)));
        esp->start();
        for (unsigned char c : std::string("AT+CIPSTART=\"TCP\",\"example.test\",80\r\n"))
            esp->receive(c);
        // Wait for the WORKER to have picked the command up — the transport
        // reaching Resolving is proof, and it costs nothing, whereas
        // `wait_idle` would wait out the deliberately long poll interval and
        // swallow the very window this row is timing.
        for (int i = 0; i < 5000 && tr.state() != TransportState::Resolving; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check("STALL-02a", "the connect is outstanding on the worker at destruction",
              tr.state() == TransportState::Resolving);

        // Time the DESTRUCTOR and nothing else.
        const auto t0 = std::chrono::steady_clock::now();
        esp.reset();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check("STALL-02",
              "destroying the wrapper mid-connect completes promptly, not at the next "
              "scheduled pass",
              ms < 500); }

    {   // STALL-03 — installing a sink must never wait on the worker.
        //     `set_output` used to take the engine lock, which meant it
        //     inherited whatever the worker was doing. A slow `send()` is used
        //     to hold that lock: it is another deliberate contract violation,
        //     and it is the only way to make the LOCKED half of the worker
        //     pass long enough to observe the difference — with the transport
        //     poll now unlocked, a slow poll() no longer holds it at all.
        SlowSendTransport tr;
        tr.send_delay = std::chrono::milliseconds(400);
        ThreadedEsp esp{tr};
        esp.set_output([](std::uint8_t) {});
        for (unsigned char c : std::string("AT+CIPSTART=\"TCP\",\"example.test\",80\r\n"))
            esp.receive(c);
        esp.poll();                       // inline: settles the connect
        for (unsigned char c : std::string("AT+CIPSEND=2\r\n")) esp.receive(c);
        esp.start();
        for (unsigned char c : std::string("hi")) esp.receive(c);   // -> flush_outbound -> send()
        for (int i = 0; i < 5000 && !tr.in_send.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check("STALL-03a", "the worker is inside a slow send(), holding the engine lock",
              tr.in_send.load());

        const auto t0 = std::chrono::steady_clock::now();
        esp.set_output([](std::uint8_t) {});
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        check("STALL-03", "set_output() returns without waiting for the engine lock", ms < 150);
        esp.stop(); }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail,
                g_skip);
    return g_fail == 0 ? 0 : 1;
}
