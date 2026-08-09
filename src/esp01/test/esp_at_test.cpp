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
#include <stdexcept>
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

    /// What `begin_connect` was last asked for. `Udp` also switches send/recv
    /// to DATAGRAM semantics below, because a fake that accepted the protocol
    /// and then behaved like a stream would let a byte-coalescing bug pass.
    Protocol       protocol          = Protocol::Tcp;

    // Observations.
    std::string              sent;         ///< everything the engine handed us
    std::deque<std::uint8_t> inbox;        ///< TCP: what recv() will hand back
    /// UDP: one entry per datagram, in and out.
    std::deque<std::vector<std::uint8_t>> datagram_inbox;
    std::vector<std::string>              sent_datagrams;
    std::string              last_host;
    std::uint16_t            last_port       = 0;
    Protocol                 last_protocol   = Protocol::Tcp;
    std::uint16_t            last_local_port = 0;
    int                      begin_calls = 0;
    int                      close_calls = 0;
    /// Where `close()` records itself a SECOND time, when a row points this
    /// somewhere (GH #240).
    ///
    /// The engine OWNS an accepted transport and deletes it the instant the
    /// slot is released, which on the `AT+CIPSTO` path happens in the very
    /// same `settle()` that emits the `CLOSED` a row wants to assert. Reading
    /// `close_calls` after that reads a freed object; a row-local int does
    /// not. Null by default, so no existing row's behaviour changes.
    int*                     close_tally = nullptr;

    bool begin_connect(const std::string& host, std::uint16_t port, Protocol proto,
                       std::uint16_t local_port) override {
        ++begin_calls;
        if (refuse_begin) return false;
        if (port == 0) return false;
        if (state_ == TransportState::Resolving || state_ == TransportState::Connecting ||
            state_ == TransportState::Connected)
            return false;
        last_host       = host;
        last_port       = port;
        last_protocol   = proto;
        last_local_port = local_port;
        protocol        = proto;
        state_          = TransportState::Resolving;
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
        if (protocol == Protocol::Udp) {
            // ALL OR NOTHING, per the interface: a datagram send never returns
            // a partial count. `send_cap` therefore refuses the whole datagram
            // rather than truncating it.
            if (len > send_cap) return 0;
            sent_datagrams.emplace_back(reinterpret_cast<const char*>(data), len);
            sent.append(reinterpret_cast<const char*>(data), len);
            return len;
        }
        const std::size_t n = len < send_cap ? len : send_cap;
        sent.append(reinterpret_cast<const char*>(data), n);
        return n;
    }

    std::size_t recv(std::uint8_t* buf, std::size_t cap) override {
        if (protocol == Protocol::Udp) {
            // At most ONE datagram per call, and an oversized one is truncated
            // with its remainder discarded — exactly what the kernel does.
            if (datagram_inbox.empty()) return 0;
            const std::vector<std::uint8_t> dg = std::move(datagram_inbox.front());
            datagram_inbox.pop_front();
            const std::size_t n = dg.size() < cap ? dg.size() : cap;
            for (std::size_t i = 0; i < n; ++i) buf[i] = dg[i];
            return n;
        }
        std::size_t n = 0;
        while (n < cap && !inbox.empty()) {
            buf[n++] = inbox.front();
            inbox.pop_front();
        }
        return n;
    }

    void close() override {
        ++close_calls;
        if (close_tally) ++*close_tally;
        if (state_ != TransportState::Idle) state_ = TransportState::Closed;
    }

    // Test-side stimulus.
    void queue_from_peer(const std::string& s) {
        for (unsigned char c : s) inbox.push_back(c);
    }
    /// One whole datagram from the peer. Separate from `queue_from_peer` on
    /// purpose: the boundary is the thing under test.
    void queue_datagram_from_peer(const std::string& s) {
        datagram_inbox.emplace_back(s.begin(), s.end());
    }
    void peer_closes() { state_ = TransportState::Closed; }

    /// Start life already connected, the way a transport handed over by a
    /// listener does (`SocketTransport::adopt_connected`). An inbound
    /// connection never passes through `begin_connect`, so a fake that could
    /// only reach `Connected` through it could not model one at all.
    void arrive_connected() { state_ = TransportState::Connected; }

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

// ── Fake listener (GH #210) ───────────────────────────────────────────────
//
// Scriptable and in-memory, exactly like `FakeTransport`: nothing binds, so the
// AT rows stay hermetic. The real bind/accept path — where SO_REUSEADDR, the
// non-blocking accept and the loopback default live — is proved against real
// sockets in `esp_socket_test`, which is the only suite in this module allowed
// to open one.

class FakeListener : public EspListener {
public:
    /// Model a bind that fails: a port already in use, or an address that is
    /// not local. The engine must turn it into `ERROR` and must not fall back.
    bool        refuse_open = false;
    std::string refuse_reason = "address already in use";

    int open_calls  = 0;
    int close_calls = 0;

    /// Connections the peer side is about to make, oldest first. `poll()` moves
    /// ONE at a time into `pending_`, which is the real listener's own
    /// one-at-a-time rule rather than a convenience.
    std::deque<std::unique_ptr<EspTransport>> arrivals;

    bool open(std::uint16_t port) override {
        ++open_calls;
        if (refuse_open) {
            last_error_ = refuse_reason;
            return false;
        }
        listening_ = true;
        port_      = port;
        last_error_.clear();
        return true;
    }

    void close() override {
        if (listening_) ++close_calls;
        listening_ = false;
        port_      = 0;
        pending_.reset();
    }

    bool               listening() const override  { return listening_; }
    std::uint16_t      port() const override       { return port_; }
    const std::string& last_error() const override { return last_error_; }

    void poll() override {
        if (!listening_ || pending_ || arrivals.empty()) return;
        pending_ = std::move(arrivals.front());
        arrivals.pop_front();
    }

    std::unique_ptr<EspTransport> accept() override { return std::move(pending_); }

private:
    bool                          listening_ = false;
    std::uint16_t                 port_      = 0;
    std::string                   last_error_;
    std::unique_ptr<EspTransport> pending_;
};

/// Queue one inbound connection on `lsn` and return a borrowed pointer to it,
/// so a row can drive the peer side (`queue_from_peer`) and read what the guest
/// sent it (`sent`) after the engine has taken ownership.
static FakeTransport* add_inbound(FakeListener& lsn) {
    auto peer = std::unique_ptr<FakeTransport>(new FakeTransport);
    peer->arrive_connected();
    FakeTransport* raw = peer.get();
    lsn.arrivals.push_back(std::move(peer));
    return raw;
}

/// Counts `poll()` calls, atomically, and is DELIBERATELY declared so that it
/// outlives the wrapper that polls it — which is what lets a row observe
/// whether the worker thread is really gone after destruction.
class CountingPollTransport : public EspTransport {
public:
    std::atomic<int> polls{0};

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
        return false;
    }
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

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
        return false;
    }
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
/// VIOLATION of `EspTransport::poll()`'s non-blocking contract. It stood in for
/// the shipped transport's synchronous `getaddrinfo`; that is now asynchronous,
/// so this fake no longer mirrors anything jnext ships and is KEPT
/// DELIBERATELY: it stands in for any third-party transport that blocks, and it
/// is the only thing that still discriminates here. Rewriting these rows to use
/// `make_socket_transport` would silently gut them.
class SlowResolveTransport : public EspTransport {
public:
    std::chrono::milliseconds block_for{500};
    std::atomic<bool>         in_poll{false};

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
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

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
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

/// THROWS out of its first `poll()`, then behaves. A deliberate violation of
/// the no-throw contract on `EspTransport` (esp_socket.h), and the only way to
/// prove the worker survives one: an exception escaping a std::thread entry
/// point is `std::terminate`, so without the wrapper's guard this fake aborts
/// the process. Throwing only ONCE is what lets the row then check the worker
/// carried on and completed the connect, rather than merely checking it did
/// not die.
class ThrowOnceTransport : public EspTransport {
public:
    std::atomic<bool> threw{false};

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
        if (state_ != TransportState::Idle) return false;
        state_ = TransportState::Resolving;
        return true;
    }
    void poll() override {
        if (!threw.exchange(true)) throw std::runtime_error("transport blew up");
        if (state_ == TransportState::Resolving) state_ = TransportState::Connected;
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

/// Connects instantly but blocks inside `send()` — another deliberate contract
/// violation, and the only way to hold the engine lock (the LOCKED half of the
/// worker pass) long enough to observe whether `set_output` waits on it.
class SlowSendTransport : public EspTransport {
public:
    std::chrono::milliseconds send_delay{400};
    std::atomic<bool>         in_send{false};

    bool begin_connect(const std::string&, std::uint16_t, Protocol,
                       std::uint16_t) override {
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
    /// Every rig gets one, because a server that cannot be asked for is not a
    /// server. It costs the pre-GH #210 rows nothing: an unopened listener is
    /// polled and accepted from on every pass and answers "nothing", so the
    /// bytes those rows assert are unchanged.
    FakeListener  lsn;
    AtEngine      eng{tr, &lsn};
    std::string   guest;  ///< everything the engine has released toward the guest

    /// What the engine believes the time is, once `freeze_clock()` has been
    /// called. Untouched — and unread — until then.
    std::chrono::steady_clock::time_point clock_now = std::chrono::steady_clock::now();

    Rig() {
        eng.set_output([this](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
    }

    /// Put the engine's clock under this row's control (GH #240).
    ///
    /// OPT-IN, and that is the point: `AT+CIPSTO`'s default is 180 SECONDS, so
    /// the only alternative to an injectable clock is a suite that waits three
    /// minutes per row — which is to say, a suite nobody runs and a feature
    /// nobody proves. Every OTHER row keeps running against the real
    /// `steady_clock`, so the pre-existing connect-deadline rows (CON-11..13)
    /// are untouched by this seam existing.
    void freeze_clock() { eng.set_clock([this] { return clock_now; }); }

    /// Move the frozen clock forward. Only meaningful after `freeze_clock()`.
    void advance(int seconds) { clock_now += std::chrono::seconds(seconds); }

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

    /// The same, over UDP — newt's `AT+CIPSTART="UDP","<server>",123`.
    void connect_udp(const char* host = "time.test", int port = 123) {
        send(std::string("AT+CIPSTART=\"UDP\",\"") + host + "\"," + std::to_string(port) + "\r\n");
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
        // v1.0 answered ERROR here, and this row asserted it. GH #210 gives the
        // command a consumer, so the row now pins the new answer — while the
        // reason the old one existed is pinned harder than before, one group
        // down: the DEFAULT is what nextsync depends on, and MUX-01 asserts it
        // is still 0.
        check_eq("AT-06",
                 "AT+CIPMUX=1 is accepted (GH #210) — it was refused until server mode "
                 "had a consumer",
                 r.take(), "\r\nOK\r\n"); }
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
    {   // Was "UDP is refused — v1.0 is TCP only" until GH #198 gave it a
        // consumer (newt). Same stimulus, opposite answer, so the row keeps its
        // id rather than leaving an orphan behind.
        Rig r;
        r.send("AT+CIPSTART=\"SSL\",\"example.test\",2048\r\n");
        r.settle();
        check_eq("CON-05", "SSL is still refused — it still has no consumer", r.take(),
                 "\r\nERROR\r\n");
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

    // ══ Group D2 — UDP (GH #198) ════════════════════════════════════════
    //
    // THE SPECIFICATION IS TWO DOCUMENTS, AND THEY AGREE. The wire forms come
    // from Espressif's own AT command set — `AT+CIPSTART=<"type">,<"remote
    // host">,<remote port>[,<local port>,<mode>]` answered `CONNECT` then `OK`,
    // `AT+CIPSEND=<length>` answered `OK` + `>` then `SEND OK`, and one
    // `+IPD,<len>:` per received datagram in single-connection mode. The
    // CONSUMER is newt (github.com/chris-y/newt, GPLv3), whose `sntp` command
    // issues exactly that sequence: `net_connect_udp` reads ONE line and
    // returns false unless it begins `CONNECT`; `net_send_data` sends
    // `AT+CIPSEND=48` and then the 48-byte NTP packet; `net_recv_data` scans
    // for `+IPD,`, reads the length up to `:`, and then reads exactly that many
    // bytes. Every row below asserts one of those, and NONE of them was written
    // by reading this engine's output back.
    //
    // Datagram boundaries are the thread running through the group. A lone
    // request/response — which is all SNTP is — would pass just as well against
    // a byte-coalescing implementation, so the rows that matter are the ones
    // with TWO messages in flight.

    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123\r\n");
        r.settle();
        const std::string reply = r.take();
        check_eq("UDP-01",
                 "AT+CIPSTART=\"UDP\" answers CONNECT then OK — newt reads ONE line and "
                 "demands it start with CONNECT",
                 reply, "CONNECT\r\n\r\nOK\r\n");
        // Stated as the property rather than as a substring of the literal
        // above, because it is the one that was got WRONG first time and the
        // one no amount of reading the code revealed: with a leading CRLF the
        // first line is EMPTY, `strncmp(buf, "CONNECT", 7)` fails, and newt
        // gives up without ever sending its request. `CONNECT` is a status
        // line, not a result code; the blank line belongs to the OK after it.
        check_eq("UDP-01c",
                 "...and the FIRST CRLF-terminated line is CONNECT itself — no leading CRLF, "
                 "or newt's one-line read sees an empty line and gives up",
                 reply.substr(0, reply.find('\n') + 1), "CONNECT\r\n");
        check("UDP-01b", "...and the engine is connected", r.eng.connected());
        check("UDP-02", "...over UDP, to the parsed host and port, with an OS-chosen local port",
              r.tr.last_protocol == Protocol::Udp && r.tr.last_host == "time.test" &&
                  r.tr.last_port == 123 && r.tr.last_local_port == 0); }
    {   Rig r;
        r.send("AT+CIPSTART=\"udp\",\"time.test\",123\r\n");
        r.settle();
        check_eq("UDP-03", "the protocol token is case-insensitive, like every command name",
                 r.take(), "CONNECT\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123,4567\r\n");
        r.settle();
        check_eq("UDP-04", "the optional <local port> is accepted", r.take(),
                 "CONNECT\r\n\r\nOK\r\n");
        check("UDP-04b", "...and reaches the transport, which is what binds it",
              r.tr.last_local_port == 4567); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123,4567,0\r\n");
        r.settle();
        check_eq("UDP-05", "<mode> 0 — the fixed peer every client uses — is accepted",
                 r.take(), "CONNECT\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123,4567,1\r\n");
        r.settle();
        check_eq("UDP-06",
                 "<mode> 1 (peer re-points once) is REFUSED, not accepted-and-ignored",
                 r.take(), "\r\nERROR\r\n");
        check("UDP-06b", "...and no connect was started", r.tr.begin_calls == 0); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123,4567,2\r\n");
        r.settle();
        check_eq("UDP-07", "<mode> 2 (peer re-points per datagram) is REFUSED too", r.take(),
                 "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTART=\"UDP\",\"time.test\",123,notaport\r\n");
        r.settle();
        check_eq("UDP-08", "an unparseable <local port> answers ERROR rather than binding 0",
                 r.take(), "\r\nERROR\r\n");
        check("UDP-08b", "...and no connect was started", r.tr.begin_calls == 0); }
    {   // newt's payload: AT+CIPSEND=48 then a 48-byte NTP packet.
        Rig r;
        r.connect_udp();
        r.send("AT+CIPSEND=48\r\n"); r.drain();
        check_eq("UDP-09", "AT+CIPSEND on a UDP link issues the same OK + '> ' prompt",
                 r.take(), "\r\nOK\r\n> ");
        std::string ntp(48, '\0');
        ntp[0] = static_cast<char>(0x23);  // LI 0, VN 4, mode 3 (client)
        r.send(ntp);
        r.settle();
        check_eq("UDP-09b", "...and the completed payload answers SEND OK", r.take(),
                 "\r\nSEND OK\r\n");
        check("UDP-09c", "...having emitted EXACTLY ONE datagram of exactly 48 bytes",
              r.tr.sent_datagrams.size() == 1 && r.tr.sent_datagrams[0] == ntp); }
    {   // TWO messages in flight, which is the case a byte queue gets wrong.
        // The socket refuses everything until the second is queued, so both are
        // still pending when it opens up.
        Rig r;
        r.connect_udp();
        r.tr.send_cap = 0;                       // nothing accepted yet
        r.send("AT+CIPSEND=3\r\nAAA"); r.settle(); r.take();
        r.send("AT+CIPSEND=3\r\nBBB"); r.settle(); r.take();
        r.tr.send_cap = static_cast<std::size_t>(-1);
        r.settle();
        check("UDP-10",
              "two queued datagrams leave as TWO datagrams, never concatenated into one",
              r.tr.sent_datagrams.size() == 2 && r.tr.sent_datagrams[0] == "AAA" &&
                  r.tr.sent_datagrams[1] == "BBB"); }
    {   Rig r;
        r.connect_udp();
        r.tr.queue_datagram_from_peer(std::string(48, 'N'));
        r.settle();
        check_eq("UDP-11", "one received datagram is one +IPD carrying its own length",
                 r.take(), "\r\n+IPD,48:" + std::string(48, 'N')); }
    {   Rig r;
        r.connect_udp();
        r.tr.queue_datagram_from_peer("ONE");
        r.tr.queue_datagram_from_peer("TWO");
        r.settle();
        check_eq("UDP-12",
                 "two datagrams are framed as two +IPDs — merging them would hand the guest "
                 "a message boundary that never existed",
                 r.take(), "\r\n+IPD,3:ONE\r\n+IPD,3:TWO"); }
    {   // THE newt BUG, REPRODUCED EXACTLY. `uart_tx_bin` is
        // `do {…} while (size--)`, so after AT+CIPSEND=3 it puts FOUR bytes on
        // the wire. The 4th is heap debris that lands in the command-line
        // buffer; while any non-empty line held a URC back, the +IPD newt was
        // waiting for was never framed and it died on its own 5 s timeout.
        Rig r;
        r.connect_udp();
        r.send("AT+CIPSEND=3\r\n"); r.settle(); r.take();
        r.send(std::string("XYZ") + '\x9E');   // 3 payload bytes + one stray
        r.tr.queue_datagram_from_peer("PONG");
        r.settle();
        check_eq("UDP-13",
                 "a stray byte left over from a guest that overran its own CIPSEND does NOT "
                 "hold the +IPD back — it cannot become an AT command",
                 r.take(), "\r\nSEND OK\r\n\r\n+IPD,4:PONG");
        check("UDP-13b", "...and only the declared 3 bytes were transmitted",
              r.tr.sent_datagrams.size() == 1 && r.tr.sent_datagrams[0] == "XYZ");
        r.send("AT+CIPCLOSE\r\n"); r.drain();
        check_eq("UDP-13c", "...while the stray byte still spoils the NEXT line, as it must",
                 r.take(), "\r\nERROR\r\n"); }
    {   // The other half of that narrowing: a genuinely half-typed command must
        // still hold a URC back (IPD-09 pins the TCP case; this is the boundary
        // itself). 'A' is where debris and a command in the making become
        // indistinguishable — the documented 1-in-256 residual.
        Rig r;
        r.connect_udp();
        r.send("AT");                       // two bytes of a real command
        r.tr.queue_datagram_from_peer("HELD");
        r.settle();
        check_eq("UDP-14", "a half-typed AT command still holds the +IPD back", r.take(), "");
        r.send("\r\n"); r.drain();
        check_eq("UDP-14b", "...and it follows the completed command's reply", r.take(),
                 "\r\nOK\r\n\r\n+IPD,4:HELD"); }
    {   Rig r;
        r.connect_udp();
        // The command line is opened FIRST so the datagram is buffered but not
        // yet framed when the close arrives — otherwise `poll()` would frame it
        // on the spot and the row would prove nothing about dropping.
        r.send("AT+CIPCLOS");
        r.tr.queue_datagram_from_peer("DROPPED");
        r.eng.poll();
        r.send("E\r\n"); r.drain();
        check_eq("UDP-15", "AT+CIPCLOSE on a UDP link reports CLOSED then OK", r.take(),
                 "\r\nCLOSED\r\n\r\nOK\r\n");
        check("UDP-15b", "...and drops the datagrams buffered for a connection that is gone",
              r.eng.pending_from_peer() == 0); }
    {   Rig r;
        r.connect_udp();
        r.send("AT+RST\r\n"); r.drain(); r.take();
        check("UDP-16", "AT+RST puts the slot back to the TCP power-on default",
              r.eng.protocol() == Protocol::Tcp && !r.eng.connected()); }
    {   Rig r;
        r.connect_udp();
        check("UDP-17", "a live UDP connection reports itself as UDP",
              r.eng.protocol() == Protocol::Udp);
        r.send("AT");                  // holds the framing back, as UDP-14 pins
        r.tr.queue_datagram_from_peer("SIZE");
        r.eng.poll();
        check("UDP-17b", "...and pending_from_peer counts buffered datagram bytes",
              r.eng.pending_from_peer() == 4); }

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

    // ══ Group G2 — losing and regaining the association (GH #246) ═══════
    //
    // The module is taken off its network by the HOST, never by the guest, and
    // exactly one reply changes: `AT+CIFSR`'s station address. These rows own
    // both halves of that sentence — what changes, and what must not.

    {   Rig r;
        check("ASSOC-01", "a fresh module is associated", r.eng.associated());
        r.send("AT+CIFSR\r\n"); r.drain();
        const std::string s = r.take();
        check("ASSOC-02", "...so AT+CIFSR reports the station address",
              s.find(std::string("STAIP,\"") + AtEngine::STA_IP + "\"") != std::string::npos); }
    {   Rig r;
        r.eng.set_associated(false);
        check("ASSOC-03", "set_associated(false) takes it off the network",
              !r.eng.associated());
        r.send("AT+CIFSR\r\n"); r.drain();
        const std::string s = r.take();
        // The EXACT line, not merely "the old address is absent": a reply that
        // dropped the STAIP line entirely would pass a negative assertion and
        // break every guest that parses the reply, which is the failure this
        // spelling exists to prevent.
        check("ASSOC-04", "AT+CIFSR reports STAIP 0.0.0.0 while unassociated",
              s.find("+CIFSR:STAIP,\"0.0.0.0\"\r\n") != std::string::npos);
        check("ASSOC-05", "...and the real address appears nowhere in the reply",
              s.find(AtEngine::STA_IP) == std::string::npos);
        check("ASSOC-06", "...while the STAMAC line is untouched — the MAC is the radio's own",
              s.find(std::string("STAMAC,\"") + AtEngine::STA_MAC + "\"") != std::string::npos);
        check("ASSOC-07", "...and the reply still ends in the exact OK framing",
              s.size() >= 6 && s.compare(s.size() - 6, 6, "\r\nOK\r\n") == 0); }
    {   Rig r;
        r.eng.set_associated(false);
        r.eng.set_associated(true);
        r.send("AT+CIFSR\r\n"); r.drain();
        const std::string s = r.take();
        check("ASSOC-08",
              "re-associating restores the SAME address — a short outage does not move it",
              s.find(std::string("STAIP,\"") + AtEngine::STA_IP + "\"") != std::string::npos); }
    {   // §16.3: the guest is not what took the network away, so nothing the
        // guest can send brings it back. AT+RST is the one command that resets
        // every other piece of module state, which makes it the row that
        // matters.
        Rig r;
        r.eng.set_associated(false);
        r.send("AT+RST\r\n"); r.settle(); r.take();
        check("ASSOC-09", "AT+RST does NOT restore the association", !r.eng.associated());
        r.send("AT+CIFSR\r\n"); r.drain();
        const std::string s = r.take();
        check("ASSOC-10", "...so AT+CIFSR still reports 0.0.0.0 after a reset",
              s.find("+CIFSR:STAIP,\"0.0.0.0\"\r\n") != std::string::npos); }
    {   // The deliberate NON-changes of §16.3, pinned so that a later
        // "consistency fix" has to argue with a failing row rather than with a
        // comment. Neither reply is read by any evidenced consumer, and what a
        // real module answers to them while unassociated has not been measured
        // on the firmware this emulates.
        Rig r;
        r.eng.set_associated(false);
        r.send("AT+CWJAP?\r\n"); r.drain();
        const std::string jap = r.take();
        check("ASSOC-11",
              "AT+CWJAP? deliberately still reports the joined AP while unassociated",
              jap.find(std::string("+CWJAP:\"") + AtEngine::SSID + "\"") != std::string::npos);
        r.send("AT+CIPSTA?\r\n"); r.drain();
        const std::string sta = r.take();
        check("ASSOC-12",
              "AT+CIPSTA? deliberately still reports the configured address too",
              sta.find(std::string("ip:\"") + AtEngine::STA_IP + "\"") != std::string::npos); }
    {   // §16.3: traffic is not modelled. A connection opened while the module
        // is off its network still opens, which is the bound the design states
        // and not an accident of where the flag is read.
        Rig r;
        r.eng.set_associated(false);
        r.connect();
        check("ASSOC-13", "a connection still opens while unassociated — traffic is not modelled",
              r.eng.connected()); }

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

    {   // (e) `tick()` must not inherit a worker stall. Any transport that
        //     blocks inside a call the worker makes under the core lock — a
        //     third-party one, since the shipped transport no longer blocks
        //     anywhere — would be handed straight to the emulation thread by a
        //     blocking lock in `tick()`, which is the whole thing this class
        //     exists to avoid.
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
        //
        //     KNOWN LIMIT OF THIS ROW'S SHAPE, recorded because the identical
        //     trap was found in esp_socket_test's ASYNC-06/07 during review and
        //     the lesson is worth more than either row: IT TIMES THE
        //     DESTRUCTOR, which is a call that merely FOLLOWS the operation
        //     that could stall, not one that contains it. It discriminates only
        //     because `AsyncResolveTransport` never blocks anywhere, so there
        //     is no earlier call for a stall to hide in. Give that fake a slow
        //     `poll()` and the stall relocates to `start()`/the first pass:
        //     this row goes quiet and still passes. TIME THE CALL THAT
        //     CONTAINS THE STALL. (ASYNC-11 in esp_socket_test is that fix
        //     applied on the other side: it times the `poll()` that starts a
        //     lookup, not a later one.)
        AsyncResolveTransport tr;
        auto esp = std::unique_ptr<ThreadedEsp>(
            new ThreadedEsp(tr, /*listener=*/nullptr, std::chrono::milliseconds(2000)));
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

    {   // STALL-04 — A THROWING TRANSPORT MUST NOT ABORT THE PROCESS.
        //     `EspTransport`'s methods run on the worker thread, and an
        //     exception escaping a std::thread entry point is `std::terminate`
        //     — a process abort, not an error anyone can handle. The wrapper
        //     has to lose the pass and keep going, which is the same policy
        //     the resolver thread uses in esp_socket.cpp.
        //
        //     The transport throws on its FIRST poll only, then behaves. That
        //     is what makes this a real row rather than a crash test: with the
        //     guard, the worker survives and the LATER passes still complete
        //     the connect, so the guest gets its `OK` — proving the worker was
        //     not merely alive but still working. Without the guard the suite
        //     dies with SIGABRT and the harness reports a crashed suite, which
        //     is loud but tells you less.
        //
        //     Deliberately not forked, unlike esp_socket_test's SIG rows: this
        //     suite is portable by construction (nothing here names a POSIX
        //     header) and buying a tidier failure mode with <sys/wait.h> would
        //     cost the property that a consumer can run it anywhere.
        ThrowOnceTransport tr;
        ThreadedEsp esp{tr};
        std::string guest;
        esp.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        esp.start();
        for (unsigned char c : std::string("AT+CIPSTART=\"TCP\",\"example.test\",80\r\n"))
            esp.receive(c);

        bool ok_seen = false;
        for (int i = 0; i < 3000 && !ok_seen; ++i) {
            for (int k = 0; k < 256 && esp.wants_tick(); ++k) esp.tick(1, 1);
            if (guest.find("OK") != std::string::npos) ok_seen = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("STALL-04a", "the worker recorded the exception rather than dying on it",
              esp.pass_exceptions() >= 1 && tr.threw.load());
        check("STALL-04",
              "a transport that throws on the worker costs one service pass, not the "
              "process: the connect still completes afterwards",
              ok_seen);
        esp.stop(); }

    // ══ Group K — multiplexing and server mode (GH #210) ═══════════════
    //
    // THE FIRST FOUR ROWS ARE THE ONES THAT MATTER, and they are first for that
    // reason. `AT+CIPMUX=1` exists now, but the power-on default does not
    // change and nextsync — which never sends the command and whose `+IPD`
    // reader silently CORRUPTS the multiplexed form rather than rejecting it —
    // must keep seeing exactly the bytes it saw before. MUX-10..13 pin both
    // wire forms against each other so that neither can drift into the other's
    // session.

    {   Rig r;
        check("MUX-01",
              "the power-on default is CIPMUX=0 — no command can correct a wrong "
              "default at run time, so this is the value nextsync depends on",
              !r.eng.cipmux()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.drain();
        check("MUX-02", "...and AT+CIPMUX=1 really changes it, rather than being humoured",
              r.eng.cipmux()); }

    {   // THE NEXTSYNC GUARD. A session that never mentioned CIPMUX gets the
        // unmultiplexed form, byte for byte, exactly as it did before GH #210.
        Rig r;
        r.connect();
        r.tr.queue_from_peer("hello");
        r.settle();
        check_eq("MUX-10",
                 "a CIPMUX=0 session still sees the unmultiplexed +IPD,<len>: — the one "
                 "thing GH #210 could have broken silently",
                 r.take(), "\r\n+IPD,5:hello"); }
    {   // The other side of the same guard, on the SAME outbound connection.
        Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.connect();
        r.tr.queue_from_peer("hello");
        r.settle();
        check_eq("MUX-11",
                 "a CIPMUX=1 session's outbound connection sees +IPD,<id>,<len>: with "
                 "id 0",
                 r.take(), "\r\n+IPD,0,5:hello"); }
    {   // And a connection opened BEFORE the mode changed keeps its framing.
        // The mode cannot in fact change under it (MUX-05 is why), so this is
        // the belt to that braces: the flag is per connection, not global.
        Rig r;
        r.connect();
        r.send("AT+CIPMUX=1\r\n"); r.settle();
        r.take();
        r.tr.queue_from_peer("hi");
        r.settle();
        check_eq("MUX-12",
                 "a connection opened under CIPMUX=0 keeps the unmultiplexed +IPD even "
                 "after the mode command is attempted",
                 r.take(), "\r\n+IPD,2:hi"); }
    {   Rig r;
        r.connect();
        r.tr.peer_closes();
        r.settle();
        check_eq("MUX-13",
                 "and its CLOSED stays unprefixed — NXtel matches a 5-byte 'OSED\\r' "
                 "window",
                 r.take(), "\r\nCLOSED\r\n"); }
    {   // THE GUEST-INITIATED CLOSE TAKES THE SAME DECISION, which it did not
        // before: `cmd_cipclose` emitted a bare `CLOSED` unconditionally. That
        // was unreachable-by-construction until CIPMUX=1 existed, and once it
        // did, a session framed `+IPD,0,<len>:` throughout would have been
        // handed an unprefixed `CLOSED` by this one path.
        Rig r;
        r.connect();
        r.send("AT+CIPCLOSE\r\n"); r.settle();
        check_eq("MUX-14",
                 "AT+CIPCLOSE on a CIPMUX=0 connection answers the v1.0 bytes exactly",
                 r.take(), "\r\nCLOSED\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.connect();
        r.send("AT+CIPCLOSE\r\n"); r.settle();
        check_eq("MUX-15",
                 "...and on a CIPMUX=1 connection it carries the id, like every other "
                 "CLOSED path",
                 r.take(), "\r\n0,CLOSED\r\n\r\nOK\r\n"); }

    {   Rig r; r.send("AT+CIPMUX=2\r\n"); r.drain();
        check_eq("MUX-03", "AT+CIPMUX=2 is not a mode — ERROR", r.take(), "\r\nERROR\r\n"); }
    {   Rig r; r.send("AT+CIPMUX=\r\n"); r.drain();
        check_eq("MUX-04", "AT+CIPMUX with no argument — ERROR", r.take(), "\r\nERROR\r\n"); }

    {   // Real firmware: "This mode can only be changed after all connections
        // are disconnected". The peer was promised one framing and cannot
        // renegotiate.
        Rig r;
        r.connect();
        r.send("AT+CIPMUX=1\r\n"); r.drain();
        check_eq("MUX-05", "AT+CIPMUX=1 is refused while a connection is open", r.take(),
                 "\r\nERROR\r\n");
        check("MUX-05b", "...and the mode really did not move", !r.eng.cipmux()); }
    {   // The deliberate deviation (simplification 8c): a request for the mode
        // already in force is a no-op, not a change — which is what keeps
        // NXtel's init `AT+CIPMUX=0` answering OK in every circumstance it
        // answered OK before.
        Rig r;
        r.connect();
        r.send("AT+CIPMUX=0\r\n"); r.drain();
        check_eq("MUX-06", "AT+CIPMUX=0 while a connection is open is a NO-OP, still OK",
                 r.take(), "\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        r.send("AT+CIPMUX=0\r\n"); r.drain();
        check_eq("MUX-07",
                 "AT+CIPMUX=0 is refused while the server is listening — a server is a "
                 "promise of multiplexed framing to whoever connects next",
                 r.take(), "\r\nERROR\r\n");
        check("MUX-07b", "...and the server is still up", r.eng.server_listening()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.send("AT+RST\r\n"); r.drain(); r.take();
        check("MUX-08", "AT+RST restores the CIPMUX=0 power-on default", !r.eng.cipmux()); }

    // ── AT+CIPSERVER ──────────────────────────────────────────────────────

    {   Rig r;
        r.send("AT+CIPSERVER=1,4000\r\n"); r.drain();
        check_eq("SRV-01",
                 "AT+CIPSERVER=1 without AT+CIPMUX=1 first is ERROR (ESP-AT: a server "
                 "can only be created when multiple connections are activated)",
                 r.take(), "\r\nERROR\r\n");
        check("SRV-01b", "...and nothing was bound", r.lsn.open_calls == 0); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=1,4000\r\n"); r.drain();
        check_eq("SRV-02", "AT+CIPSERVER=1,<port> answers OK", r.take(), "\r\nOK\r\n");
        check("SRV-02b", "...and the listener really bound that port",
              r.eng.server_listening() && r.eng.server_port() == 4000); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,0\r\n"); r.drain(); r.take();
        check("SRV-03",
              "port 0 is refused although the socket layer accepts it: it means 'let "
              "the OS choose', and a guest that named no port cannot be told which it got",
              !r.eng.server_listening()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=1\r\n"); r.drain();
        check_eq("SRV-04", "AT+CIPSERVER=1 with no port is ERROR", r.take(), "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=1,4000,7\r\n"); r.drain();
        check_eq("SRV-05", "trailing arguments are refused, not ignored", r.take(),
                 "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=2,4000\r\n"); r.drain();
        check_eq("SRV-06", "mode 2 does not exist — ERROR", r.take(), "\r\nERROR\r\n"); }
    {   // A BIND FAILURE MUST NEVER WIDEN ANYTHING. The engine has one answer
        // available and it is `ERROR`; there is no second port and no second
        // address to try, which is the whole consequence of design doc §13.4.
        Rig r;
        r.lsn.refuse_open = true;
        r.send("AT+CIPMUX=1\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=1,4000\r\n"); r.drain();
        check_eq("SRV-07", "a bind failure answers ERROR", r.take(), "\r\nERROR\r\n");
        check("SRV-07b", "...and leaves nothing listening", !r.eng.server_listening());
        check("SRV-07c", "...having tried exactly once — no retry, no fallback port",
              r.lsn.open_calls == 1); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=1,4001\r\n"); r.drain();
        check_eq("SRV-08", "a second AT+CIPSERVER=1 while one is running is ERROR",
                 r.take(), "\r\nERROR\r\n");
        check("SRV-08b", "...and the running server is untouched",
              r.eng.server_port() == 4000 && r.lsn.open_calls == 1); }
    {   // A consumer that built no listener. Indistinguishable from a failed
        // bind on purpose: neither tells the guest anything it can act on.
        FakeTransport tr;
        AtEngine      eng{tr};
        std::string   guest;
        eng.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        for (unsigned char c : std::string("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"))
            eng.receive(c);
        for (int i = 0; i < 200000 && eng.wants_tick(); ++i) eng.tick(1, 1);
        check_eq("SRV-09", "an engine built with NO listener answers ERROR to CIPSERVER",
                 guest, "\r\nOK\r\n\r\nERROR\r\n");
        check("SRV-09b", "...and reports no server", !eng.server_listening()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=0\r\n"); r.drain();
        check_eq("SRV-10", "AT+CIPSERVER=0 stops the server and answers OK", r.take(),
                 "\r\nOK\r\n");
        check("SRV-10b", "...and the port is released", !r.eng.server_listening() &&
              r.lsn.close_calls == 1); }
    {   // Deliberate deviation (simplification 8b): real firmware says OK. The
        // one evidenced sender of this line turns the server off at init and
        // has always been answered ERROR here, because v1.0 had no such command
        // at all — so ERROR is the spelling that changes nothing for it.
        Rig r;
        r.send("AT+CIPSERVER=0\r\n"); r.drain();
        check_eq("SRV-11", "AT+CIPSERVER=0 with no server running is ERROR", r.take(),
                 "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.drain(); r.take();
        r.send("AT+CIPSERVER=0,1\r\n"); r.drain();
        check_eq("SRV-12", "ESP-AT's <close_all> argument is refused, not ignored",
                 r.take(), "\r\nERROR\r\n");
        check("SRV-12b", "...and the server is still running", r.eng.server_listening()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.drain(); r.take();
        r.send("AT+RST\r\n"); r.drain(); r.take();
        check("SRV-13",
              "AT+RST closes the server — a listening port that outlived the module "
              "that opened it is how this leaks",
              !r.eng.server_listening() && r.lsn.close_calls == 1); }

    // ── Accepted connections ──────────────────────────────────────────────

    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle();
        // `[<conn_id>,]CONNECT` — "A network connection of which ID is
        // <conn_id> is established" (ESP-AT, AT Messages table).
        check_eq("SRV-14", "an accepted connection is announced as <id>,CONNECT", r.take(),
                 "\r\n1,CONNECT\r\n");
        check("SRV-14b", "...and occupies one inbound slot", r.eng.inbound_connections() == 1); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        peer->queue_from_peer("DZRP");
        r.settle();
        check_eq("SRV-15", "its inbound data is framed with the multiplexed +IPD",
                 r.take(), "\r\n+IPD,1,4:DZRP"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPSEND=1,5\r\n"); r.drain();
        check_eq("SRV-16", "AT+CIPSEND=<id>,<len> issues the same prompt, byte for byte",
                 r.take(), "\r\nOK\r\n> ");
        r.send("PONG!"); r.settle();
        check_eq("SRV-16b", "...and the payload is acknowledged", r.take(),
                 "\r\nSEND OK\r\n");
        check("SRV-16c", "...having reached THAT connection's transport", peer->sent == "PONG!");
        check("SRV-16d", "...and not the outbound one", r.tr.sent.empty()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPSEND=5\r\n"); r.drain();
        check_eq("SRV-17",
                 "the single-connection AT+CIPSEND=<len> form is ERROR under CIPMUX=1 — "
                 "the argument list is read from the MODE, never sniffed from the text",
                 r.take(), "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPSEND=2,5\r\n"); r.drain();
        check_eq("SRV-18", "AT+CIPSEND to a link id with no connection is ERROR", r.take(),
                 "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        peer->peer_closes();
        r.settle();
        // `[<conn_id>,]CLOSED` — "A network connection of which ID is
        // <conn_id> ends" (ESP-AT, AT Messages table).
        check_eq("SRV-19", "a peer close is announced as <id>,CLOSED", r.take(),
                 "\r\n1,CLOSED\r\n");
        check("SRV-19b", "...and the slot is free again", r.eng.inbound_connections() == 0); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* first = add_inbound(r.lsn);
        r.settle(); r.take();
        first->peer_closes();
        r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle();
        check_eq("SRV-20", "a released slot is reused, so the next peer is id 1 again",
                 r.take(), "\r\n1,CONNECT\r\n"); }
    {   // FOUR inbound slots, because slot 0 is the outbound one
        // (simplification 8a). The fifth peer is closed at once rather than
        // left open on its own side and invisible on ours.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        std::vector<FakeTransport*> peers;
        for (int i = 0; i < 5; ++i) peers.push_back(add_inbound(r.lsn));
        for (int i = 0; i < 8; ++i) r.settle();
        check_eq("SRV-21", "four peers are accepted as ids 1..4, in order", r.take(),
                 "\r\n1,CONNECT\r\n\r\n2,CONNECT\r\n\r\n3,CONNECT\r\n\r\n4,CONNECT\r\n");
        check("SRV-21b", "...and the fifth is closed rather than silently held",
              r.eng.inbound_connections() == 4 && peers[4]->close_calls == 1); }
    {   // The reason for 8a, asserted rather than asserted about: accepting a
        // peer must not cost the guest the connection slot it dials out on.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n"); r.settle();
        check_eq("SRV-22", "an inbound connection never takes slot 0 — AT+CIPSTART still "
                 "works while a peer is connected",
                 r.take(), "\r\nOK\r\n");
        check("SRV-22b", "...and both connections are live",
              r.eng.connected() && r.eng.inbound_connections() == 1); }
    {   // AT+CIPSERVER=0 stops ACCEPTING. Dropping a live debug session because
        // its listener was retired would be a surprise nothing asked for.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPSERVER=0\r\n"); r.settle(); r.take();
        peer->queue_from_peer("still here");
        r.settle();
        check_eq("SRV-23",
                 "an established inbound connection survives AT+CIPSERVER=0 and keeps "
                 "delivering",
                 r.take(), "\r\n+IPD,1,10:still here"); }

    {   // THE BORROWED TRANSPORT IS NEVER RELEASED. Slot 0's transport belongs
        // to the host and outlives the engine, so the paths that free an
        // ACCEPTED one — a CLOSED that retires a connection, and AT+RST, which
        // sweeps every slot — must leave it alone. Freeing it does not crash
        // here (the deleter refuses), it NULLS the slot, and the next
        // AT+CIPSTART dereferences it. Both routes are exercised because they
        // are separate call sites.
        Rig r;
        r.connect();
        r.tr.peer_closes();
        r.settle(); r.take();
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n"); r.settle();
        check_eq("SRV-24",
                 "slot 0's transport survives its own connection closing — a reconnect "
                 "after CLOSED still works",
                 r.take(), "\r\nOK\r\n"); }
    {   Rig r;
        r.connect();
        r.send("AT+RST\r\n"); r.settle(); r.take();
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n"); r.settle();
        check_eq("SRV-25", "...and survives AT+RST sweeping every slot", r.take(),
                 "\r\nOK\r\n"); }

    // ── AT+CIPCLOSE=<id> (GH #211) ────────────────────────────────────────
    //
    // GH #210 shipped without the argument form because DeZog closes from its
    // end. A peer that WEDGES rather than closing is the case that reopened it:
    // four wedged peers occupy all four inbound slots for the rest of the
    // session, and the guest had nothing to say about it.
    //
    // TWO PROPERTIES CARRY THE WHOLE GROUP. The named connection — and ONLY the
    // named one — goes, slot included; and the bare spelling is untouched,
    // because nextsync loops that one and never sends `AT+CIPMUX`.

    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=1\r\n"); r.settle();
        // `[<conn_id>,]CLOSED` then the result code, exactly as the bare
        // spelling frames its own close (MUX-14/MUX-15).
        check_eq("CLS-01", "AT+CIPCLOSE=<id> answers <id>,CLOSED then OK", r.take(),
                 "\r\n1,CLOSED\r\n\r\nOK\r\n");
        check("CLS-01b", "...and the slot is free again", r.eng.inbound_connections() == 0);
        check("CLS-01c", "...having really closed that peer's socket",
              peer->close_calls == 1);
        r.settle();
        check_eq("CLS-01d",
                 "...and the peer-close path does not then announce it a second time",
                 r.take(), ""); }

    {   // EVERY inbound slot, and the id in the notification is the slot's own.
        // Closed out of order on purpose: a bug that closed "the first live
        // one" would pass a 1,2,3,4 sweep.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        std::vector<FakeTransport*> peers;
        for (int i = 0; i < 4; ++i) peers.push_back(add_inbound(r.lsn));
        for (int i = 0; i < 8; ++i) r.settle();
        r.take();
        r.send("AT+CIPCLOSE=2\r\n"); r.settle();
        check_eq("CLS-02", "the notification carries the id that was asked for, not the "
                 "first live one", r.take(), "\r\n2,CLOSED\r\n\r\nOK\r\n");
        check("CLS-02b", "...and only THAT peer's socket was closed",
              peers[1]->close_calls == 1 && peers[0]->close_calls == 0 &&
              peers[2]->close_calls == 0 && peers[3]->close_calls == 0);
        check("CLS-02c", "...leaving the other three connected",
              r.eng.inbound_connections() == 3);
        r.send("AT+CIPCLOSE=4\r\n"); r.settle();
        check_eq("CLS-03", "the top inbound slot closes the same way", r.take(),
                 "\r\n4,CLOSED\r\n\r\nOK\r\n");
        r.send("AT+CIPCLOSE=3\r\n"); r.settle();
        check_eq("CLS-04", "...and so does the one between them", r.take(),
                 "\r\n3,CLOSED\r\n\r\nOK\r\n");
        r.send("AT+CIPCLOSE=1\r\n"); r.settle();
        check_eq("CLS-05", "...and the first", r.take(), "\r\n1,CLOSED\r\n\r\nOK\r\n");
        check("CLS-05b", "so four wedged peers can all be freed — the exhaustion this "
              "command exists for", r.eng.inbound_connections() == 0 &&
              peers[0]->close_calls == 1 && peers[1]->close_calls == 1 &&
              peers[2]->close_calls == 1 && peers[3]->close_calls == 1); }

    {   // THE SLOT IS REALLY BACK IN THE POOL, not merely marked not-open: the
        // next peer is given the same id.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=1\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle();
        check_eq("CLS-06", "a slot freed by AT+CIPCLOSE=<id> is reused, so the next peer "
                 "is id 1 again", r.take(), "\r\n1,CONNECT\r\n"); }

    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=2\r\n"); r.settle();
        // The bare spelling's answer for "nothing was open", for the same
        // reason: a guest told OK would believe it had freed a slot.
        check_eq("CLS-07", "AT+CIPCLOSE to a link id with no connection is ERROR", r.take(),
                 "\r\nERROR\r\n");
        peer->queue_from_peer("alive");
        r.settle();
        check_eq("CLS-07b", "...and the connection that DOES exist is untouched", r.take(),
                 "\r\n+IPD,1,5:alive"); }

    {   // ESP-AT reads id 5 as "close every connection". Refused here, and the
        // refusal is a DECISION rather than a consequence of the slot count:
        // a bulk close promises a choice about connections the guest did not
        // name — which order, and whether the outbound slot 0 is included —
        // and that is the promise AT+CIPSERVER=0,<close_all> is refused for
        // (SRV-12).
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=5\r\n"); r.settle();
        check_eq("CLS-08", "ESP-AT's close-all id 5 is refused, not honoured", r.take(),
                 "\r\nERROR\r\n");
        check("CLS-08b", "...and nothing was closed",
              r.eng.inbound_connections() == 1 && peer->close_calls == 0); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.send("AT+CIPCLOSE=9\r\n"); r.drain();
        check_eq("CLS-09", "an id past the connection ceiling is ERROR", r.take(),
                 "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.send("AT+CIPCLOSE=x\r\n"); r.drain();
        check_eq("CLS-10", "a non-numeric id is ERROR", r.take(), "\r\nERROR\r\n"); }
    {   // `AT+CIPCLOSE=` IS NOT THE BARE SPELLING. It names no connection, so
        // it closes none — falling back on the outbound one would close a
        // connection the guest did not ask about.
        Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.connect();
        r.send("AT+CIPCLOSE=\r\n"); r.settle();
        check_eq("CLS-11", "AT+CIPCLOSE= with no id is ERROR, not the no-argument form",
                 r.take(), "\r\nERROR\r\n");
        check("CLS-11b", "...and the outbound connection it would have closed is still up",
              r.eng.connected()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=1,1\r\n"); r.settle();
        check_eq("CLS-12", "trailing arguments are refused, not ignored", r.take(),
                 "\r\nERROR\r\n");
        check("CLS-12b", "...and the connection is still live",
              r.eng.inbound_connections() == 1); }

    {   // The mirror of SRV-17: the argument list is read from the MODE, never
        // sniffed from the text. Real firmware rejects the parameter in
        // single-connection mode too.
        Rig r;
        r.connect();
        r.send("AT+CIPCLOSE=0\r\n"); r.settle();
        check_eq("CLS-13", "the argument form is ERROR under CIPMUX=0", r.take(),
                 "\r\nERROR\r\n");
        check("CLS-13b", "...and the connection is untouched", r.eng.connected()); }

    {   // THE NO-ARGUMENT FORM IS UNCHANGED, which is the requirement the whole
        // group is built around: nextsync loops that exact spelling and never
        // sends AT+CIPMUX, so it must keep meaning "close the outbound
        // connection" even in a session that has four inbound ones.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.connect();
        r.send("AT+CIPCLOSE\r\n"); r.settle();
        check_eq("CLS-14", "the no-argument AT+CIPCLOSE still closes the OUTBOUND slot, "
                 "even with inbound connections present", r.take(),
                 "\r\n0,CLOSED\r\n\r\nOK\r\n");
        check("CLS-14b", "...and leaves the inbound connections alone",
              r.eng.inbound_connections() == 1 && peer->close_calls == 0 &&
              !r.eng.connected()); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\n"); r.settle(); r.take();
        r.connect();
        r.send("AT+CIPCLOSE=0\r\n"); r.settle();
        check_eq("CLS-15", "AT+CIPCLOSE=0 closes the outbound connection with the same "
                 "bytes the bare spelling emits", r.take(), "\r\n0,CLOSED\r\n\r\nOK\r\n");
        r.send("AT+CIPSTART=\"TCP\",\"example.test\",2048\r\n"); r.settle();
        check_eq("CLS-15b", "...and slot 0's BORROWED transport survives it — a reconnect "
                 "still works", r.take(), "\r\nOK\r\n"); }

    {   // The race the recovery path actually runs into: the peer drops while
        // the guest is already typing the close. The engine has not polled yet,
        // so the connection is still live to it and the close succeeds — and
        // the deferred peer-close notification must NOT then arrive as a
        // second CLOSED for a connection the guest has been told about once.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        peer->peer_closes();
        r.send("AT+CIPCLOSE=1\r\n"); r.settle(); r.settle();
        check_eq("CLS-16", "a guest close racing a peer drop emits exactly one CLOSED",
                 r.take(), "\r\n1,CLOSED\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        peer->peer_closes();
        r.settle();
        check_eq("CLS-17", "and once the peer close HAS been announced...", r.take(),
                 "\r\n1,CLOSED\r\n");
        r.send("AT+CIPCLOSE=1\r\n"); r.settle();
        check_eq("CLS-17b", "...closing the same id again is ERROR — the slot is already "
                 "back in the pool", r.take(), "\r\nERROR\r\n"); }

    {   // Buffered-but-unframed peer data dies with a guest-requested close.
        // Stated rather than hidden: the guest asked for the close, and this is
        // what the bare spelling has always done.
        //
        // THE COMMAND IS SENT WITHOUT ITS TERMINATOR FIRST, and that is the
        // whole row. A command line in flight makes the wire non-quiet, so the
        // settle below reads the peer's bytes into the CONNECTION's buffer and
        // frames nothing — which is the only state in which "discarded on
        // close" means anything. Delivering the close in one go instead would
        // pass against an engine that never buffered the data at all.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        peer->queue_from_peer("unread");
        r.send("AT+CIPCLOSE=1"); r.settle();
        check_eq("CLS-18", "a command line in flight holds the +IPD back, so the peer's "
                 "bytes really are buffered when the close arrives", r.take(), "");
        r.send("\r\n"); r.settle(); r.settle();
        check_eq("CLS-18b", "...and they are discarded with the connection — no +IPD "
                 "follows the CLOSED", r.take(), "\r\n1,CLOSED\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.send("AT+CIPCLOSE=1\r\n"); r.settle(); r.take();
        r.send("AT+CIPSEND=1,4\r\n"); r.settle();
        check_eq("CLS-19", "AT+CIPSEND to a closed id is ERROR — the slot is gone, not "
                 "merely idle", r.take(), "\r\nERROR\r\n"); }

    {   // THE FAILURE THE COMMAND EXISTS FOR, end to end. Four peers wedge
        // rather than close, a fifth is turned away — which on real hardware is
        // a power-cycle-level failure — and one AT+CIPCLOSE=<id> puts the
        // module back in service. This is the row that proves the slot is
        // returned to the POOL and not merely marked not-open.
        Rig r;
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        for (int i = 0; i < 4; ++i) add_inbound(r.lsn);
        for (int i = 0; i < 8; ++i) r.settle();
        r.take();
        FakeTransport* turned_away = add_inbound(r.lsn);
        for (int i = 0; i < 2; ++i) r.settle();
        check_eq("CLS-20", "with all four slots wedged, a fifth peer is announced to "
                 "nobody", r.take(), "");
        check("CLS-20b", "...and dropped at once", turned_away->close_calls == 1);
        r.send("AT+CIPCLOSE=2\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        for (int i = 0; i < 2; ++i) r.settle();
        check_eq("CLS-21", "...and one AT+CIPCLOSE=<id> puts the module back in service, "
                 "the next peer landing in the freed slot", r.take(),
                 "\r\n2,CONNECT\r\n"); }

    // ── AT+CIPSTO — the server idle timeout (GH #240) ─────────────────────
    //
    // UNUSUALLY FOR THIS SUITE, THE ORACLE IS A MEASUREMENT. There is no VHDL
    // for a thing on the far end of a UART cable, so the authority is a real
    // Ai-Thinker ESP-01 (AT 1.2.0.0 / SDK 1.5.4.1) probed on 2026-08-08 — it
    // answers `+CIPSTO:180` and drops a silent server-accepted client after
    // 182.5 s and 181.8 s on two runs — plus the ESP8266 AT Instruction Set
    // v1.5.4 §5.17 for the range and the "0 means never" rule.
    //
    // THE CLOCK IS FROZEN, NOT WAITED ON. `Rig::freeze_clock()` exists for
    // exactly these rows: the default window is three minutes, so a row that
    // proved the timeout by sleeping would be a row that never runs. What that
    // buys is also what it costs — see the note above STO-11 for what these
    // rows do NOT prove.

    {   Rig r;
        r.send("AT+CIPSTO?\r\n"); r.drain();
        check_eq("STO-01", "AT+CIPSTO? answers the default a real module reports", r.take(),
                 "\r\n+CIPSTO:180\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTO=10\r\n"); r.drain();
        check_eq("STO-02", "an in-range AT+CIPSTO=<time> answers OK", r.take(), "\r\nOK\r\n");
        r.send("AT+CIPSTO?\r\n"); r.drain();
        check_eq("STO-02b", "...and the query reads back what was set", r.take(),
                 "\r\n+CIPSTO:10\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTO=0\r\n"); r.drain();
        check_eq("STO-03", "0 — \"it will never timeout\" — is a legal setting, not a "
                 "refusal", r.take(), "\r\nOK\r\n");
        r.send("AT+CIPSTO?\r\n"); r.drain();
        check_eq("STO-03b", "...and reads back as 0", r.take(), "\r\n+CIPSTO:0\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTO=7200\r\n"); r.drain();
        check_eq("STO-04", "the top of the documented 0~7200 range is INCLUSIVE", r.take(),
                 "\r\nOK\r\n");
        check("STO-04b", "...and really took", r.eng.server_timeout() == 7200); }
    {   Rig r;
        r.send("AT+CIPSTO=7201\r\n"); r.drain();
        check_eq("STO-05", "one past the range is ERROR", r.take(), "\r\nERROR\r\n");
        r.send("AT+CIPSTO?\r\n"); r.drain();
        check_eq("STO-05b", "...and a refused value changes nothing", r.take(),
                 "\r\n+CIPSTO:180\r\n\r\nOK\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTO=-1\r\n"); r.drain();
        check_eq("STO-06", "a negative time is ERROR", r.take(), "\r\nERROR\r\n");
        check("STO-06b", "...and did not wrap into a huge unsigned window",
              r.eng.server_timeout() == 180); }
    {   Rig r;
        r.send("AT+CIPSTO=\r\n"); r.drain();
        check_eq("STO-07", "AT+CIPSTO= with no time is ERROR, not a reset to 0", r.take(),
                 "\r\nERROR\r\n");
        check("STO-07b", "...and 0 is emphatically not what it meant",
              r.eng.server_timeout() == 180); }
    {   Rig r;
        r.send("AT+CIPSTO=abc\r\n"); r.drain();
        check_eq("STO-08", "a non-numeric time is ERROR", r.take(), "\r\nERROR\r\n"); }
    {   Rig r;
        r.send("AT+CIPSTO=10,20\r\n"); r.drain();
        check_eq("STO-09", "a trailing argument is refused, not ignored", r.take(),
                 "\r\nERROR\r\n");
        check("STO-09b", "...and nothing was taken from the part that did parse",
              r.eng.server_timeout() == 180); }
    {   // v1.5.4 lists the commands that write to flash and AT+CIPSTO is NOT
        // among them — which is why a module that had been running for weeks
        // still answered 180. A restart forgets it.
        Rig r;
        r.send("AT+CIPSTO=7200\r\n"); r.drain(); r.take();
        r.send("AT+RST\r\n"); r.drain(); r.take();
        r.send("AT+CIPSTO?\r\n"); r.drain();
        check_eq("STO-10", "the value does not survive AT+RST — the command does not "
                 "persist to flash", r.take(), "\r\n+CIPSTO:180\r\n\r\nOK\r\n"); }

    {   // THE ARM THAT MATTERS, and the limit of what it proves. These rows
        // drive the engine's own notion of time, so they prove the ENGINE
        // closes an idle inbound connection at the window it was given and
        // tells the guest in the bytes below. They do NOT prove the window a
        // real ESP-01 uses — that took a hardware probe, is recorded in the
        // issue, and is what fixed the default at 180 (STO-01/STO-12).
        Rig r;
        r.freeze_clock();
        r.send("AT+CIPSTO=30\r\nAT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        int            closes = 0;
        FakeTransport* peer   = add_inbound(r.lsn);
        peer->close_tally     = &closes;  // outlives the transport — see close_tally
        r.settle(); r.take();
        r.advance(29);
        r.settle();
        check_eq("STO-11", "a client one second short of the window is left alone", r.take(),
                 "");
        check("STO-11b", "...and is still connected",
              r.eng.inbound_connections() == 1 && closes == 0);
        r.advance(1);
        r.settle();
        // `[<id>,]CLOSED` and nothing else: the module hung up, the guest did
        // not ask it to, so this is a URC and no `OK` follows. That the guest
        // sees this spelling at all is INFERRED for AT 1.2.0.0 — v1.5.4's
        // CIPSTO entry does not say (esp_at.h simplification 9d).
        check_eq("STO-11c", "an idle client is dropped at the window, announced as "
                 "<id>,CLOSED with no OK", r.take(), "\r\n1,CLOSED\r\n");
        check("STO-11d", "...having really closed the socket", closes == 1);
        check("STO-11e", "...and returned the slot to the pool",
              r.eng.inbound_connections() == 0); }
    {   // The DEFAULT is enforced, not merely reported. A module that answered
        // `+CIPSTO:180` and then never timed anything out would pass STO-01.
        Rig r;
        r.freeze_clock();
        r.send("AT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        add_inbound(r.lsn);
        r.settle(); r.take();
        r.advance(179);
        r.settle();
        check_eq("STO-12", "the 180 s default really governs, with no AT+CIPSTO sent at all",
                 r.take(), "");
        r.advance(1);
        r.settle();
        check_eq("STO-12b", "...and fires at 180", r.take(), "\r\n1,CLOSED\r\n"); }
    {   // "If AT+CIPSTO=0, it will never timeout" (v1.5.4 §5.17).
        //
        // THE ACCEPT PASS IS ASSERTED RATHER THAN DISCARDED, deliberately: a
        // zero window read as "expired the instant it was armed" closes the
        // connection in the SAME settle that announces it, so a row that
        // take()s the CONNECT away and only looks afterwards sees silence and
        // passes. Reading both URCs out of one take() is what makes 0 mean
        // never rather than always.
        Rig r;
        r.freeze_clock();
        r.send("AT+CIPSTO=0\r\nAT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        int            closes = 0;
        FakeTransport* peer   = add_inbound(r.lsn);
        peer->close_tally     = &closes;
        r.settle();
        check_eq("STO-13", "AT+CIPSTO=0 does not close the client the moment it arrives",
                 r.take(), "\r\n1,CONNECT\r\n");
        r.advance(100000);  // well past the 7200 s ceiling of the setting form
        r.settle(); r.settle();
        check_eq("STO-13b", "...nor 100 000 seconds later — 0 really is never", r.take(), "");
        check("STO-13c", "...and the socket was left alone",
              r.eng.inbound_connections() == 1 && closes == 0); }
    {   // CLIENT SILENCE IS WHAT ARMS IT — the requirement's wording, and the
        // whole reason the refresh lives in `drain_socket`. Nothing here says
        // anything about the OTHER direction: whether server-initiated traffic
        // restarts the timer is what v1.5.4 does not state and what this
        // module deliberately does not model (esp_at.h simplification 9a).
        Rig r;
        r.freeze_clock();
        r.send("AT+CIPSTO=30\r\nAT+CIPMUX=1\r\nAT+CIPSERVER=1,4000\r\n"); r.settle(); r.take();
        FakeTransport* peer = add_inbound(r.lsn);
        r.settle(); r.take();
        r.advance(20);
        peer->queue_from_peer("x");
        r.settle();
        check_eq("STO-14", "the client speaks at 20 s and is heard", r.take(),
                 "\r\n+IPD,1,1:x");
        r.advance(20);
        r.settle();
        check_eq("STO-14b", "...which restarts the window: 40 s after connecting, but 20 s "
                 "after speaking, it is still up", r.take(), "");
        check("STO-14c", "...and really still connected", r.eng.inbound_connections() == 1);
        r.advance(11);
        r.settle();
        check_eq("STO-14d", "...and it is the SILENCE that is measured — 31 s after the last "
                 "byte it goes", r.take(), "\r\n1,CLOSED\r\n"); }
    {   // AT+CIPSTO is the TCP *SERVER* timeout. Slot 0 is the guest's own
        // outbound connection and no server ever accepted into it
        // (simplification 8a), so a silent outbound peer stays connected
        // exactly as it did before this feature existed.
        Rig r;
        r.freeze_clock();
        r.connect();
        r.send("AT+CIPSTO=10\r\n"); r.settle(); r.take();
        r.advance(1000);
        r.settle(); r.settle();
        check_eq("STO-15", "the OUTBOUND connection is not subject to the server timeout",
                 r.take(), "");
        check("STO-15b", "...and is still live", r.eng.connected() && r.tr.close_calls == 0); }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail,
                g_skip);
    return g_fail == 0 ? 0 : 1;
}
