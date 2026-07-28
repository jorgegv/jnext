// jnext's ESP-01 security, visibility and emulator-wiring tests
// (GH #25, branch 4 of 6).
//
// WHAT THIS SUITE OWNS, and why it is not in the module's own two suites:
// everything here is about jnext's RELATIONSHIP with the ESP rather than about
// the ESP. `src/esp01` is a self-contained component with a proven standalone
// build, and it must not learn what a hostname allowlist, a Qt status bar or an
// `Emulator` is. So the allowlist, the connection-event log, the transport
// decorator that enforces one and reports the other, and the wiring into
// `Emulator` (including the replay gate and the lifetime ordering) are tested
// here, against the module's public seams only.
//
//   POL-*    the hostname allowlist, as a pure value type
//   ELOG-*   the connection-event log (bounded, thread-safe)
//   GATE-*   EspGatedTransport: refusal, forwarding, event emission
//   AT-*     the refusal really is what the GUEST sees, through the real engine
//   FAULT-*  a throwing transport is surfaced exactly once
//   WIRE-*   Emulator: off by default, attached when on, inert under replay,
//            preserved across a soft reset, and joined at destruction
//
// Run: ./build/test/esp_wiring_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/rzx.h"
#include "core/log.h"
#include "esp01/esp_at.h"
#include "esp01/esp_socket.h"
#include "esp01/esp_threaded.h"
#include "peripheral/esp_host_policy.h"
#include "peripheral/esp_uart_adapter.h"
#include "peripheral/uart.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace esp;

// ── Tiny harness (matches the style of the sibling ESP suites) ────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool ok) {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %-14s %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %-14s %s\n", id, desc);
    }
}

/// Same, plus what was actually observed — printed only on failure. The rows
/// that compare byte streams need it: "the reply was wrong" is not a
/// diagnosis, and re-deriving one by hand from a red row is the expensive way.
static void check(const char* id, const char* desc, bool ok, const std::string& detail) {
    check(id, desc, ok);
    if (!ok) std::printf("               observed: %s\n", detail.c_str());
}

/// printf into a std::string, for those detail strings. (Not `fmt` — that is
/// the fmtlib namespace spdlog pulls in.)
static std::string detail_fmt(const char* format, ...) {
    char    buf[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return std::string(buf);
}

/// CR and LF rendered as escapes, so a failing byte-stream row is readable on
/// one line and a stray terminator cannot hide in the output.
static std::string visible(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\r')      out += "\\r";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

// ── A transport the tests drive by hand ───────────────────────────────────

/// Records what it was asked to do and reports whatever state the test sets.
/// No socket, no DNS, no listener — every row below is hermetic.
class ScriptedTransport final : public EspTransport {
public:
    bool begin_connect(const std::string& host, std::uint16_t port) override {
        ++begin_calls;
        last_host = host;
        last_port = port;
        if (!accept) return false;
        state_ = TransportState::Connecting;
        return true;
    }
    void poll() override { ++poll_calls; }
    TransportState     state() const override      { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t len) override {
        sent += len;
        return len;
    }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override { closed = true; state_ = TransportState::Closed; }

    void set_state(TransportState s) { state_ = s; }
    void set_error(std::string e)    { error_ = std::move(e); }

    int           begin_calls = 0;
    int           poll_calls  = 0;
    std::size_t   sent        = 0;
    bool          closed      = false;
    bool          accept      = true;
    std::string   last_host;
    std::uint16_t last_port = 0;

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

/// Throws from every method that the wrapper's service pass calls. Exists to
/// prove `ThreadedEsp::pass_exceptions()` really does count, so FAULT-02's
/// one-shot latch is protecting something real rather than an integer nobody
/// ever increments.
class ThrowingTransport final : public EspTransport {
public:
    bool begin_connect(const std::string&, std::uint16_t) override { return false; }
    void poll() override { throw std::runtime_error("deliberate transport fault"); }
    TransportState     state() const override      { return TransportState::Idle; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override {}

private:
    std::string error_;
    IpAddress   peer_;
};

// One byte time at the UART's 115200 8N1 default (prescaler 243 x 10 bits).
static constexpr std::uint32_t BYTE_TICKS = 243 * 10;

namespace {

/// Everything the esp01 logger emitted while `body` ran, joined.
template <typename Fn>
std::string captured_log(const char* level, Fn body) {
    auto ring  = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
    auto saved = Log::esp01()->sinks();
    Log::esp01()->sinks() = {ring};
    Log::parse_levels(std::string("esp01=") + level);
    body();
    std::string all;
    for (const std::string& line : ring->last_formatted()) all += line;
    Log::parse_levels("esp01=off");
    Log::esp01()->sinks() = saved;
    return all;
}

/// Guest-visible bytes produced by a real AtEngine driven over a gated
/// transport — the path an actual AT+CIPSTART takes.
std::string engine_reply(EspHostPolicy policy, const std::string& command,
                         EspConnectionLog& log) {
    auto scripted = std::make_unique<ScriptedTransport>();
    EspGatedTransport gated{std::move(scripted), std::move(policy), log};
    AtEngine    engine{gated};
    std::string guest;
    engine.set_output([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
    for (unsigned char c : command) engine.receive(c);
    engine.poll();
    for (int i = 0; i < 200000 && engine.wants_tick(); ++i)
        engine.tick(BYTE_TICKS, BYTE_TICKS);
    return guest;
}

/// A config that builds an emulator with no SD image — legitimate for a
/// hermetic fixture (emulator_config.h says so), and all these rows need is
/// the UART and the ESP.
EmulatorConfig bare_config() {
    EmulatorConfig cfg;
    cfg.silent = true;
    return cfg;
}

// ── Driving UART 0 the way a Z80 program does (ESP-01..04) ───────────────
//
// Ports only: OUT 0x153B selects the channel, OUT 0x133B transmits, IN 0x133B
// reads the status and IN 0x143B receives. Nothing here reaches past the port
// dispatcher, so these rows exercise the same path a guest does.

void uart0_select(Emulator& emu) { emu.port().out(0x153B, 0x00); }

void uart0_send(Emulator& emu, const std::string& line) {
    uart0_select(emu);
    for (char c : line) emu.port().out(0x133B, static_cast<std::uint8_t>(c));
}

/// Run `frames` frames, harvesting everything that reaches the RX FIFO.
/// Guest-bound bytes are paced at the channel's baud (2430 master cycles per
/// byte at the 115200 reset default), so a reply cannot all be there at once —
/// draining across frames is how a real program would see it too.
std::string uart0_drain(Emulator& emu, int frames) {
    std::string out;
    for (int i = 0; i < frames; ++i) {
        emu.run_frame();
        uart0_select(emu);
        while (emu.port().in(0x133B) & 0x01)          // status b0 = RX available
            out += static_cast<char>(emu.port().in(0x143B));
    }
    return out;
}

void nr_write(Emulator& emu, std::uint8_t reg, std::uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

std::uint8_t nr_read(Emulator& emu, std::uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

}  // namespace

int main() {
    std::printf("=== jnext ESP-01 security / visibility / wiring ===\n\n");
    Log::parse_levels("esp01=off");

    // ─────────────────────────────────────────────────────────────────────
    // POL — the hostname allowlist
    // ─────────────────────────────────────────────────────────────────────
    {
        EspHostPolicy p;
        check("POL-01", "an empty allowlist allows any host (it is not deny-all)",
              p.unrestricted() && p.allows("example.test") && p.allows("10.0.0.1"));
    }
    {
        EspHostPolicy p;
        p.add("nx.nxtel.org");
        check("POL-02", "a listed host is allowed", p.allows("nx.nxtel.org"));
        check("POL-03", "an unlisted host is refused", !p.allows("evil.test"));
        check("POL-04", "matching is case-insensitive in both directions",
              p.allows("NX.NxTel.ORG"));
    }
    {
        EspHostPolicy p;
        p.add("  nx.nxtel.org\t");
        check("POL-05", "surrounding whitespace in an entry is ignored",
              p.allows("nx.nxtel.org"));
    }
    {
        EspHostPolicy p;
        p.add("nx.nxtel.org");
        check("POL-06", "an empty host name matches nothing", !p.allows(""));
    }
    {
        EspHostPolicy p;
        check("POL-07", "add() rejects a blank entry rather than dropping it silently",
              !p.add("") && p.allowed_hosts.empty());
        check("POL-08", "add() rejects a whitespace-only entry",
              !p.add("   \t ") && p.allowed_hosts.empty());
    }
    {
        EspHostPolicy p;
        p.add("Example.Test");
        p.add("example.test");
        check("POL-09", "add() de-duplicates case-insensitively",
              p.allowed_hosts.size() == 1);
        check("POL-10", "...keeping the spelling the user wrote first",
              p.allowed_hosts[0] == "Example.Test");
    }
    {
        EspHostPolicy p;
        p.add("192.168.1.42");
        check("POL-11", "a listed IP literal is allowed", p.allows("192.168.1.42"));
        check("POL-12", "an unlisted IP literal is refused", !p.allows("192.168.1.43"));
    }
    {
        // The absence of a pattern language is a DECISION (esp_host_policy.h),
        // so it is pinned: a later "improvement" that adds implicit subdomain
        // or glob matching to a security control has to break these rows.
        EspHostPolicy p;
        p.add("nxtel.org");
        check("POL-13", "a subdomain of a listed host is NOT implicitly allowed",
              !p.allows("nx.nxtel.org"));
        EspHostPolicy q;
        q.add("*.nxtel.org");
        check("POL-14", "a '*.' entry is a literal string, not a wildcard",
              !q.allows("nx.nxtel.org") && q.allows("*.nxtel.org"));
    }

    // ─────────────────────────────────────────────────────────────────────
    // ELOG — the connection-event log
    // ─────────────────────────────────────────────────────────────────────
    {
        EspConnectionLog log;
        check("ELOG-01", "a fresh log is empty and its sequence is zero",
              log.sequence() == 0 && log.snapshot().empty());

        log.push({EspEvent::Kind::Opened, "a.test", 1, {}});
        log.push({EspEvent::Kind::Closed, "a.test", 1, {}});
        check("ELOG-02", "push advances the sequence", log.sequence() == 2);

        const auto snap = log.snapshot();
        check("ELOG-03", "the snapshot is oldest-first",
              snap.size() == 2 && snap[0].kind == EspEvent::Kind::Opened &&
                  snap[1].kind == EspEvent::Kind::Closed);
    }
    {
        EspConnectionLog log;
        const std::size_t over = EspConnectionLog::CAPACITY + 10;
        for (std::size_t i = 0; i < over; ++i)
            log.push({EspEvent::Kind::Opened, "h", static_cast<std::uint16_t>(i), {}});
        const auto snap = log.snapshot();
        check("ELOG-04", "the ring is bounded, dropping the OLDEST",
              snap.size() == EspConnectionLog::CAPACITY &&
                  snap.back().port == static_cast<std::uint16_t>(over - 1));
        check("ELOG-05", "the sequence keeps counting past the ring capacity",
              log.sequence() == over);
    }
    {
        // The log is written from the ESP worker thread and read from the GUI
        // thread; a lost or torn push here is a lost security notification.
        EspConnectionLog log;
        constexpr int THREADS = 4, EACH = 500;
        std::vector<std::thread> workers;
        for (int t = 0; t < THREADS; ++t) {
            workers.emplace_back([&log] {
                for (int i = 0; i < EACH; ++i)
                    log.push({EspEvent::Kind::Opened, "h", 1, {}});
            });
        }
        std::atomic<bool> stop{false};
        std::thread reader([&log, &stop] {
            while (!stop.load()) { (void)log.snapshot(); (void)log.sequence(); }
        });
        for (auto& w : workers) w.join();
        stop.store(true);
        reader.join();
        check("ELOG-06", "concurrent pushes are all counted, with a reader running",
              log.sequence() == static_cast<std::uint64_t>(THREADS) * EACH);
    }
    {
        const EspEvent::Kind kinds[] = {
            EspEvent::Kind::Refused, EspEvent::Kind::Opened, EspEvent::Kind::Failed,
            EspEvent::Kind::Closed,  EspEvent::Kind::TransportFault};
        bool all_named = true;
        for (EspEvent::Kind k : kinds) {
            const char* t = esp_event_text(k);
            if (t == nullptr || *t == '\0' || std::string(t) == "unknown") all_named = false;
        }
        check("ELOG-07", "every event kind has its own text (the GUI shows it)", all_named);
    }

    // ─────────────────────────────────────────────────────────────────────
    // GATE — EspGatedTransport
    // ─────────────────────────────────────────────────────────────────────
    {
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspHostPolicy policy;
        policy.add("allowed.test");
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), policy, log};

        check("GATE-01", "an allowed host reaches the wrapped transport",
              gated.begin_connect("allowed.test", 2048) && tr->begin_calls == 1 &&
                  tr->last_host == "allowed.test" && tr->last_port == 2048);

        const bool accepted = gated.begin_connect("evil.test", 80);
        check("GATE-02", "a refused host NEVER reaches the wrapped transport",
              tr->begin_calls == 1);
        check("GATE-03", "a refusal returns false — the 'request rejected' contract",
              !accepted);
        check("GATE-04", "a refusal is recorded as an event naming host and port",
              log.sequence() == 1 && log.snapshot().back().kind == EspEvent::Kind::Refused &&
                  log.snapshot().back().host == "evil.test" &&
                  log.snapshot().back().port == 80);
        check("GATE-05", "a refusal is counted", gated.refusals() == 1);
    }
    {
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), EspHostPolicy{}, log};
        check("GATE-06", "an empty allowlist forwards every host",
              gated.begin_connect("anything.test", 1) && tr->begin_calls == 1 &&
                  gated.refusals() == 0);
    }
    {
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), EspHostPolicy{}, log};
        gated.begin_connect("peer.test", 23281);
        tr->set_state(TransportState::Connected);
        gated.poll();
        check("GATE-07", "Connecting -> Connected reports one Opened event",
              log.sequence() == 1 && log.snapshot().back().kind == EspEvent::Kind::Opened &&
                  log.snapshot().back().host == "peer.test");
        gated.poll();
        gated.poll();
        check("GATE-08", "...and not again while it stays connected", log.sequence() == 1);

        tr->set_state(TransportState::Closed);
        gated.poll();
        check("GATE-10", "Connected -> Closed reports a Closed event",
              log.sequence() == 2 && log.snapshot().back().kind == EspEvent::Kind::Closed);
    }
    {
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), EspHostPolicy{}, log};
        gated.begin_connect("peer.test", 23281);
        tr->set_error("name resolution failed");
        tr->set_state(TransportState::Failed);
        gated.poll();
        check("GATE-09", "a failure reports a Failed event carrying last_error()",
              log.sequence() == 1 && log.snapshot().back().kind == EspEvent::Kind::Failed &&
                  log.snapshot().back().detail == "name resolution failed");
    }
    {
        // An attempt that never connected must not report a connection the
        // user never had.
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), EspHostPolicy{}, log};
        gated.begin_connect("peer.test", 1);
        tr->set_state(TransportState::Closed);
        gated.poll();
        check("GATE-11", "a never-connected attempt that closes reports nothing",
              log.sequence() == 0);
    }
    {
        auto  raw = std::make_unique<ScriptedTransport>();
        auto* tr  = raw.get();
        EspConnectionLog log;
        EspGatedTransport gated{std::move(raw), EspHostPolicy{}, log};
        const std::uint8_t payload[3] = {1, 2, 3};
        std::uint8_t       buf[4]{};
        tr->set_error("boom");
        const bool forwarded =
            gated.send(payload, 3) == 3 && tr->sent == 3 && gated.recv(buf, 4) == 0 &&
            gated.state() == TransportState::Idle && gated.last_error() == "boom" &&
            gated.peer_address() == ipv4(192, 0, 2, 1);
        gated.poll();
        gated.close();
        check("GATE-12", "every other method forwards to the wrapped transport",
              forwarded && tr->poll_calls == 1 && tr->closed);
    }
    {
        // The engine ALREADY logs opened/failed/closed at info/warn. The
        // decorator must not print them a second time — but it MUST print the
        // refusal, because only it knows the allowlist exists.
        EspHostPolicy policy;
        policy.add("allowed.test");
        EspConnectionLog log;
        std::string refusal_log = captured_log("info", [&] {
            auto  raw = std::make_unique<ScriptedTransport>();
            auto* tr  = raw.get();
            EspGatedTransport gated{std::move(raw), policy, log};
            gated.begin_connect("evil.test", 80);
            gated.begin_connect("allowed.test", 80);
            tr->set_state(TransportState::Connected);
            gated.poll();
            tr->set_state(TransportState::Closed);
            gated.poll();
        });
        check("GATE-13", "a refusal is logged, saying WHY (the engine cannot)",
              refusal_log.find("REFUSED") != std::string::npos &&
                  refusal_log.find("evil.test:80") != std::string::npos &&
                  refusal_log.find("esp-allow") != std::string::npos);
        check("GATE-14", "...at warn, so it survives the default log level",
              refusal_log.find("warning") != std::string::npos ||
                  refusal_log.find("warn") != std::string::npos);
        check("GATE-15", "the decorator does NOT duplicate the engine's own lines",
              refusal_log.find("opened") == std::string::npos &&
                  refusal_log.find("closed") == std::string::npos);
    }

    // ─────────────────────────────────────────────────────────────────────
    // AT — the refusal is what the GUEST actually sees
    // ─────────────────────────────────────────────────────────────────────
    {
        EspHostPolicy policy;
        policy.add("allowed.test");
        EspConnectionLog log;
        const std::string refused =
            engine_reply(policy, "AT+CIPSTART=\"TCP\",\"evil.test\",80\r\n", log);
        check("AT-01", "a refused host answers ERROR on the wire, not silence",
              refused == "\r\nERROR\r\n");

        EspConnectionLog log2;
        const std::string allowed =
            engine_reply(policy, "AT+CIPSTART=\"TCP\",\"allowed.test\",80\r\n", log2);
        // The scripted transport reports Connecting forever, so the engine
        // defers its reply; what matters is that it did NOT refuse.
        check("AT-02", "an allowed host is not refused (the reply is deferred)",
              allowed.empty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // FAULT — a throwing transport is surfaced, exactly once
    // ─────────────────────────────────────────────────────────────────────
    {
        bool reported = false;
        check("FAULT-01", "a zero count reports nothing",
              !esp_note_transport_fault(0, reported) && !reported);
        check("FAULT-02", "the first non-zero count reports",
              esp_note_transport_fault(1, reported) && reported);
        check("FAULT-03", "later observations do not report again",
              !esp_note_transport_fault(2, reported) &&
                  !esp_note_transport_fault(99, reported));
    }
    {
        // Wires the latch above to the thing it is latching: a real wrapper
        // over a transport that throws really does raise the counter, and the
        // worker survives it (the process would abort otherwise).
        ThrowingTransport tr;
        ThreadedEsp       esp{tr};
        esp.start();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (esp.pass_exceptions() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool counted = esp.pass_exceptions() > 0;
        const bool alive   = esp.running();
        esp.stop();
        check("FAULT-04", "a throwing transport raises pass_exceptions and the worker lives",
              counted && alive);
    }

    // ─────────────────────────────────────────────────────────────────────
    // WIRE — Emulator
    // ─────────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        emu.init(bare_config());
        check("WIRE-01", "the ESP is OFF by default — nothing is attached to UART 0",
              !emu.esp_enabled() && emu.uart().device(0) == nullptr);
        check("WIRE-02", "...and there is no connection log, so the GUI shows no cell",
              emu.esp_events() == nullptr);
    }
    {
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        cfg.esp_allowed_hosts = {"nx.nxtel.org"};
        Emulator emu;
        emu.init(cfg);
        check("WIRE-03", "--esp attaches a device to UART 0 (the ESP's channel)",
              emu.esp_enabled() && emu.uart().device(0) != nullptr);
        check("WIRE-04", "...and publishes a connection log for the GUI",
              emu.esp_events() != nullptr);
        check("WIRE-05", "...and the allowlist in force is readable back",
              emu.esp_allowed_hosts().size() == 1 &&
                  emu.esp_allowed_hosts()[0] == "nx.nxtel.org");
        check("WIRE-06", "UART 1 (the Pi header) is left alone",
              emu.uart().device(1) == nullptr);
    }
    {
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        Emulator emu;
        emu.init(cfg);
        auto* adapter = static_cast<EspUartAdapter*>(emu.uart().device(0));

        emu.run_frame();
        check("WIRE-07", "a normal frame leaves the ESP live",
              adapter != nullptr && !adapter->inert());

        emu.set_replay_mode(true);
        emu.run_frame();
        check("WIRE-08", "a frame in replay mode makes the ESP inert",
              adapter->inert());

        emu.set_replay_mode(false);
        emu.run_frame();
        check("WIRE-09", "...and leaving replay mode makes it live again",
              !adapter->inert());
        check("WIRE-10", "the device stays ATTACHED while inert (no loopback)",
              emu.uart().device(0) == adapter);
    }
    {
        // A soft reset resets the Next-side UART state machine, not the module
        // on the far end of the cable (design doc §4.3). Rebuilding the ESP
        // there would drop a live connection on a reset it never sees.
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        Emulator emu;
        emu.init(cfg);
        auto* before = static_cast<EspUartAdapter*>(emu.uart().device(0));
        // A MARK, not just an address. A rebuilt adapter can land on the very
        // same heap block the old one was freed from, so comparing pointers
        // alone passes for the wrong reason — mutation-testing this row is
        // exactly how that was found. `inert` is state only THIS object
        // carries, and a fresh adapter always starts live.
        if (before) before->set_inert(true);
        emu.init(cfg, /*preserve_memory=*/true);
        auto* after = static_cast<EspUartAdapter*>(emu.uart().device(0));
        check("WIRE-11", "a soft reset does NOT rebuild the ESP",
              before != nullptr && after == before && after->inert());
    }
    {
        // THE RZX HALF OF THE GATE. `replay_mode_` (rewind fast-forward) and
        // `rzx_player_.is_playing()` are INDEPENDENT sources of the same
        // hazard, and WIRE-07..10 only exercise the first: a reviewer dropped
        // `|| rzx_player_.is_playing()` outright and the entire suite stayed
        // green. RZX playback re-executes recorded instructions, so a replayed
        // `AT+CIPSTART` opens a second real socket and a replayed `AT+CIPSEND`
        // delivers the payload to the peer twice.
        //
        // Driven through the player's own public API rather than a file: a
        // default `RzxRecording` with a few empty frames keeps `is_playing()`
        // true for several frames (`begin_frame()` retires one per frame and
        // stops when they run out) and installs no IN override, so the machine
        // runs exactly as it otherwise would.
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        Emulator emu;
        emu.init(cfg);
        auto* adapter = static_cast<EspUartAdapter*>(emu.uart().device(0));

        emu.run_frame();
        check("WIRE-13", "the ESP is live before RZX playback starts",
              adapter != nullptr && !adapter->inert());

        RzxRecording rec;
        rec.frames.resize(4);            // enough to outlive the frames below
        emu.rzx_player().start(std::move(rec));
        emu.run_frame();
        check("WIRE-14", "RZX playback makes the ESP inert",
              adapter->inert() && emu.rzx_player().is_playing());

        emu.run_frame();
        check("WIRE-15", "...and it stays inert for as long as playback runs",
              adapter->inert() && emu.rzx_player().is_playing());
        check("WIRE-16", "...with the device still ATTACHED, so no UART loopback",
              emu.uart().device(0) == adapter);

        // The discriminator for "the two terms are ORed, not swapped": with
        // replay_mode_ explicitly FALSE, RZX alone must still hold it inert.
        emu.set_replay_mode(false);
        emu.run_frame();
        check("WIRE-17", "RZX alone holds it inert, with replay_mode_ clear",
              adapter->inert() && emu.rzx_player().is_playing());

        emu.rzx_player().stop();
        emu.run_frame();
        check("WIRE-18", "stopping RZX playback makes the ESP live again",
              !adapter->inert() && !emu.rzx_player().is_playing());
    }
    {
        // The whole reason the ESP is owned by Emulator: ~Emulator() joins the
        // worker, so a cold boot's placement-new cannot race a live thread.
        // Also bounds it — an unbounded join is a frozen emulator on Reset.
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        auto emu = std::make_unique<Emulator>();
        emu->init(cfg);
        const auto t0 = std::chrono::steady_clock::now();
        emu.reset();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        check("WIRE-12", "destroying the Emulator joins the ESP worker promptly",
              ms < 1000);
    }

    // ─────────────────────────────────────────────────────────────────────
    // ESP-01..04 — the G39 rows, closed (GH #25 branch 5)
    // ─────────────────────────────────────────────────────────────────────
    //
    // These four IDs sat `missing`/`missing` in the traceability matrix and as
    // deliberate `// WONT` comments in test/uart/uart_test.cpp, from the era
    // when jnext had no ESP at all. They are NOT duplicates of DEV-01..05 in
    // uart_integration_test: those drive a hand-written `StubUartDevice`, and
    // a stub cannot tell you whether the thing `--esp` actually builds — a
    // `ThreadedEsp` wrapping a real `AtEngine` behind an `EspGatedTransport` —
    // is on the wire and answering. Every row below uses the REAL one, reached
    // through `Emulator::setup_esp` exactly as a user's `--esp` reaches it.
    //
    // No network: `AT` and `ATE1` never touch the transport, so nothing here
    // resolves or connects. The socket half is proved by the
    // `esp-loopback-func` regression row against a real TCP peer.
    {
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        Emulator emu;
        emu.init(cfg);

        uart0_send(emu, "AT\r\n");
        const std::string reply = uart0_drain(emu, 4);

        // Two halves, and the second is the one a wiring mistake trips: an
        // unattached channel loops the guest's own bytes back into its own RX
        // FIFO (uart.cpp deliver_tx_byte), so "AT\r\n" coming back would look
        // like traffic while proving the ESP is NOT there. Echo is off by
        // default (design doc simplification 2), so the ESP never sends it.
        check("ESP-01",
              "guest TX on UART 0 egresses to the REAL emulated ESP-01, which parses "
              "the AT line and answers — not to the channel's loopback "
              "[zxnext.vhd:1611-1612, :3381 UART 0 = ESP]",
              reply == "\r\nOK\r\n",
              detail_fmt("RX drained %zu bytes: '%s' (want '\\r\\nOK\\r\\n'; 'AT\\r\\n' "
                  "would mean loopback, empty would mean nothing is attached)",
                  reply.size(), visible(reply).c_str()));
    }
    {
        // ESP-02 — the same reply reaches the guest through the real RX FIFO
        // AND drives the real IM2 UART0_RX vector, under the NR 0xC6 request
        // mask `near_full OR (avail AND NOT nr_c6(1))` (zxnext.vhd:1941-1944).
        // Asserting BOTH directions of the mask is what makes it
        // discriminative: a delivery path that bypassed `Uart::inject_rx`
        // could still hand the guest the bytes, but could not reproduce the
        // mask.
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;

        Emulator emu;
        emu.init(cfg);
        nr_write(emu, 0xC6, 0x01);              // per-byte avail contributes
        uart0_send(emu, "AT\r\n");
        const std::string got_avail = uart0_drain(emu, 4);
        const std::uint8_t ca_avail = nr_read(emu, 0xCA);

        Emulator emu2;
        emu2.init(cfg);
        nr_write(emu2, 0xC6, 0x02);             // near-full only; avail suppressed
        uart0_send(emu2, "AT\r\n");
        const std::string got_masked = uart0_drain(emu2, 4);
        const std::uint8_t ca_masked = nr_read(emu2, 0xCA);

        check("ESP-02",
              "the ESP's reply lands in the UART 0 RX FIFO and raises the UART0_RX "
              "IM2 vector under the NR 0xC6 request mask — six bytes never reach the "
              "3/4 near-full mark, so bit 1 suppresses it [zxnext.vhd:1941-1944]",
              got_avail == "\r\nOK\r\n" && (ca_avail & 0x03) != 0 &&
                  got_masked == "\r\nOK\r\n" && (ca_masked & 0x03) == 0,
              detail_fmt("C6=0x01: rx='%s' NR_CA=0x%02X (want reply + bits set); "
                  "C6=0x02: rx='%s' NR_CA=0x%02X (want reply + bits clear)",
                  visible(got_avail).c_str(), ca_avail,
                  visible(got_masked).c_str(), ca_masked));
    }
    {
        // ESP-03 — RESTATED to what v1.0 actually built, and deliberately so.
        //
        // The row used to claim NR 0x02 bit 7 "resets the attached ESP-01, as
        // nextsync's recovery path does". The hardware line is real —
        // zxnext.vhd:5119 latches `nr_02_bus_reset <= nr_wr_dat(7)` and
        // zxnext.vhd:1579 drives `o_RESET_PERIPHERAL` from it, with
        // nextreg.txt:48 saying verbatim "Assert and hold reset to the
        // expansion bus and the esp wifi" — and jnext latches and reads the
        // bit back correctly. What jnext does NOT do is route it to the
        // device: that is a v1.1 extension point (design doc §4.2, §10), left
        // out on the stated grounds that it is a RECOVERY path, "degraded, not
        // blocking", and that the hook belongs on the NR 0x02 write and not on
        // `UartChannel::reset` (§4.3).
        //
        // So the row now pins the fact rather than the aspiration, and it is
        // NOT a tautology: `ATE1` leaves observable state inside the AT engine
        // (echo on), and a real hard reset would restore the power-on default
        // (echo off, design doc simplification 2). Wire the reset up and this
        // row goes red — which is exactly what should happen, because the
        // design record has to change with it.
        EmulatorConfig cfg = bare_config();
        cfg.esp_enabled = true;
        Emulator emu;
        emu.init(cfg);

        uart0_send(emu, "ATE1\r\n");             // echo ON: engine state to lose
        uart0_drain(emu, 4);

        nr_write(emu, 0x02, 0x80);               // assert the ESP / expbus reset
        const std::uint8_t nr02 = nr_read(emu, 0x02);
        uart0_drain(emu, 2);

        uart0_send(emu, "AT\r\n");
        const std::string after = uart0_drain(emu, 4);

        check("ESP-03",
              "NR 0x02 bit 7 (o_RESET_PERIPHERAL) latches and reads back, and in v1.0 "
              "drives NO device reset — the attached ESP keeps its state across it "
              "(nextsync's recovery path is a v1.1 extension point, design doc §4.2) "
              "[zxnext.vhd:5119, :1579; nextreg.txt:48]",
              (nr02 & 0x80) != 0 && after.find("AT") != std::string::npos,
              detail_fmt("NR 0x02 readback=0x%02X (want b7 set); reply after the reset='%s' "
                  "(want the ATE1 echo still on — '\\r\\nOK\\r\\n' alone would mean "
                  "the engine was reset)",
                  nr02, visible(after).c_str()));
    }
    {
        // ESP-04 — the converse, and the reason ESP-01's second half is worth
        // asserting: with nothing attached, UART 0 keeps the loopback the 116
        // pre-ESP UART rows observe. This is what `--esp` being default-off
        // means at the wire.
        Emulator emu;
        emu.init(bare_config());

        uart0_send(emu, "AT\r\n");
        const std::string reply = uart0_drain(emu, 4);

        check("ESP-04",
              "with no ESP backend attached, UART 0 keeps its loopback: the guest's "
              "own bytes come back and nothing answers them",
              emu.uart().device(0) == nullptr && reply == "AT\r\n",
              detail_fmt("device=%p rx='%s' (want null + 'AT\\r\\n')",
                  static_cast<const void*>(emu.uart().device(0)),
                  visible(reply).c_str()));
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail,
                g_skip);
    return g_fail == 0 ? 0 : 1;
}
