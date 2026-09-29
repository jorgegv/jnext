// jnext::remote — the shared debugger socket transport (GH #287, package T of
// epic #276).
//
// WHAT IS UNDER TEST. `remote::Server` (the listener/`Service` every protocol
// server runs over), the `Connection` an adapter reads and writes through, and
// the in-memory fake (`FakeListener`/`FakeTransport`/`FakePeer`) the adapter
// suites will push packets through. There is no protocol here: the adapter in
// this file is a newline-terminated line protocol that exists only to drive
// the transport, the same role `FakeService` plays in debugger_backend_test.
//
//   XPT-FAKE-*  the fake itself: bytes both ways, partial reads and writes,
//               peer close (and a send after it), reset, nothing blocks. The adapter suites lean on
//               it, so it is proved before anything is built on it.
//   XPT-SRV-*   the Server over the fake — every rule of the mechanism, with
//               no kernel in the way: accept only inside a pass, one command per
//               pass, buffering, one client per listener and the busy reply,
//               disconnect in all its forms, linger, the two bounds, the wait.
//   XPT-NET-*   the same Server over the REAL socket layer on 127.0.0.1 port 0:
//               the reported port is the bound one, the kernel's partial writes,
//               the wait against real readiness, bind failures.
//   XPT-PUMP-*  the real listener registered through `Debugger::add_service`
//               and driven by `pump()`: `remote_attached`, the SES-03 drain in
//               both arms, `pump(wait)`, `remove_service`, and a disconnect
//               releasing the client's pause (SES-01).
//
// EVERY WAIT IS BOUNDED. Each loop carries a deadline or an iteration cap, every
// fake is finite, and the timing rows use generous bounds because the host may
// be loaded: they assert "waited at least most of it" and "came back well
// before the next order of magnitude", never an exact figure.
//
// The test CLIENT for the real-socket rows is `esp::make_socket_transport` with
// loopback allowed — the same public seam, so this suite opens no socket of its
// own either.
//
// Run: ./build/test/remote_transport_test

#include "remote/fake_transport.h"
#include "remote/transport.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../row_id.h"

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::dbg::ServiceStep;
using jnext::remote::Connection;
using jnext::remote::FakeListener;
using jnext::remote::FakePeer;
using jnext::remote::Protocol;
using jnext::remote::Server;
using jnext::remote::ServerConfig;
using SteadyClock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// ── Tiny test harness (matches debugger_backend_test) ──────────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

static long ms_since(SteadyClock::time_point t0) {
    return static_cast<long>(
        std::chrono::duration_cast<milliseconds>(SteadyClock::now() - t0).count());
}

static void nap() { std::this_thread::sleep_for(milliseconds(1)); }

/// The payload byte at offset `i` of a `big N` reply: position-dependent, so a
/// dropped, duplicated or reordered chunk cannot pass for the right one.
static std::uint8_t pat(std::size_t i) {
    return static_cast<std::uint8_t>((i * 131u + 7u) ^ (i >> 9));
}

static bool is_pattern(const std::string& s, std::size_t n) {
    if (s.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (static_cast<std::uint8_t>(s[i]) != pat(i)) return false;
    return true;
}

// ── Log capture ────────────────────────────────────────────────────────────

/// Adds a ring sink to the `debugger` channel for its lifetime.
struct LogTap {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256);
    LogTap() { Log::debugger()->sinks().push_back(ring); }
    ~LogTap() {
        auto& s = Log::debugger()->sinks();
        s.erase(std::remove(s.begin(), s.end(), ring), s.end());
    }
    int count(const std::string& needle) const {
        int n = 0;
        for (const auto& l : ring->last_formatted())
            if (l.find(needle) != std::string::npos) ++n;
        return n;
    }
};

// ── The test adapter: a line protocol ──────────────────────────────────────
//
// A command is a line. `ping` -> `pong`; `big N` -> N pattern bytes; `bye` ->
// `bye` and the adapter closes; `pause` / `run` drive the Debugger; anything
// else is echoed. Every command's output is finite.

class LineProtocol : public Protocol {
public:
    std::string welcome;           // written on connect when non-empty
    std::string notify_text;       // written on every on_notify when non-empty
    bool        consume = true;    // false: on_service reads nothing (backpressure rows)

    int connects = 0, disconnects = 0, notifies = 0;
    std::vector<std::string> done;         // commands executed, in order
    std::vector<std::string> trail;        // "C", "S:<cmd>", "D" in call order
    std::size_t max_available  = 0;        // largest available() on_service saw
    // pending_output() as on_notify saw it — AFTER the pass's send, so it is
    // what the kernel did not take. Last value, and the largest.
    std::size_t notify_pending     = 0;
    std::size_t max_notify_pending = 0;

    Debugger* dbg = nullptr;               // pump rows only
    ClientId  cid = jnext::dbg::CLIENT_NONE;

    void on_connect(Connection& c) override {
        ++connects;
        trail.push_back("C");
        if (!welcome.empty()) c.write(welcome);
        if (dbg) cid = dbg->attach({"xpt-line", jnext::dbg::ClientKind::Test}).value;
    }

    ServiceStep on_service(Connection& c) override {
        max_available = std::max(max_available, c.available());
        if (consume) {
            std::uint8_t b[4096];
            std::size_t  n;
            while ((n = c.read(b, sizeof(b))) > 0)
                buf_.append(reinterpret_cast<const char*>(b), n);
        }
        const std::size_t nl = buf_.find('\n');
        if (nl == std::string::npos) return ServiceStep::Idle;
        const std::string cmd = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        execute(c, cmd);
        done.push_back(cmd);
        trail.push_back("S:" + cmd);
        return ServiceStep::Serviced;
    }

    void on_notify(Connection& c) override {
        ++notifies;
        notify_pending     = c.pending_output();
        max_notify_pending = std::max(max_notify_pending, notify_pending);
        if (!notify_text.empty()) c.write(notify_text);
    }

    void on_disconnect() override {
        ++disconnects;
        trail.push_back("D");
        buf_.clear();
        if (dbg && cid != jnext::dbg::CLIENT_NONE) {
            dbg->detach(cid);
            cid = jnext::dbg::CLIENT_NONE;
        }
    }

private:
    void execute(Connection& c, const std::string& cmd) {
        if (cmd == "ping") {
            c.write("pong\n");
        } else if (cmd.rfind("big ", 0) == 0) {
            const std::size_t n = std::stoul(cmd.substr(4));
            std::string       s(n, '\0');
            for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<char>(pat(i));
            c.write(s);
        } else if (cmd == "bye") {
            c.write("bye\n");
            c.close();
        } else if (cmd == "pause" && dbg) {
            dbg->pause(cid);
            c.write("paused\n");
        } else if (cmd == "run" && dbg) {
            dbg->run(cid);
            c.write("running\n");
        } else {
            c.write("echo " + cmd + "\n");
        }
    }
    std::string buf_;
};

static ServerConfig cfg_named(const char* name) {
    ServerConfig c;
    c.name = name;
    return c;
}

/// A Server over a FakeListener. `lsn` is the listener the Server now owns;
/// valid until `srv.stop()` or the Server's destruction.
struct FakeRig {
    LineProtocol            proto;
    std::unique_ptr<Server> srv;
    FakeListener*           lsn = nullptr;
    explicit FakeRig(ServerConfig cfg = cfg_named("xpt")) {
        srv      = std::unique_ptr<Server>(new Server(std::move(cfg), proto));
        auto l   = std::unique_ptr<FakeListener>(new FakeListener());
        lsn      = l.get();
        srv->open(std::move(l), "127.0.0.1", 0);
    }
    /// Pass the server until `pred` holds or `max_passes` ran; true if it held.
    bool until(const std::function<bool()>& pred, int max_passes = 64) {
        for (int i = 0; i < max_passes; ++i) {
            if (pred()) return true;
            srv->service_once(0);
            srv->flush_notifications();
        }
        return pred();
    }
};

// ── The real-socket client ─────────────────────────────────────────────────

struct Client {
    std::unique_ptr<esp::EspTransport> t;
    std::string                        rx;

    bool connect(std::uint16_t port) {
        esp::AddressPolicy pol;
        pol.deny_loopback = false;  // this suite's peer IS on loopback
        t = esp::make_socket_transport(pol);
        if (!t->begin_connect("127.0.0.1", port)) return false;
        const auto t0 = SteadyClock::now();
        while (ms_since(t0) < 3000) {
            t->poll();
            if (t->state() == esp::TransportState::Connected) return true;
            if (t->state() == esp::TransportState::Failed ||
                t->state() == esp::TransportState::Closed)
                return false;
            nap();
        }
        return false;
    }

    bool send_all(const std::string& s) {
        std::size_t off = 0;
        const auto  t0  = SteadyClock::now();
        while (off < s.size() && ms_since(t0) < 3000) {
            off += t->send(reinterpret_cast<const std::uint8_t*>(s.data()) + off,
                           s.size() - off);
            if (t->state() != esp::TransportState::Connected) return false;
            if (off < s.size()) nap();
        }
        return off == s.size();
    }

    /// Read what has arrived. Bounded: at most 1024 reads per call.
    void drain() {
        std::uint8_t b[65536];
        for (int i = 0; i < 1024; ++i) {
            const std::size_t n = t->recv(b, sizeof(b));
            if (n == 0) break;
            rx.append(reinterpret_cast<const char*>(b), n);
        }
    }

    bool eof() const {
        return t->state() == esp::TransportState::Closed ||
               t->state() == esp::TransportState::Failed;
    }
};

/// Pass the server and read the client until `pred` holds or `timeout_ms`.
static bool pump_until(Server& srv, Client& c, const std::function<bool()>& pred,
                       int timeout_ms = 3000) {
    const auto t0 = SteadyClock::now();
    while (ms_since(t0) < timeout_ms) {
        srv.service_once(0);
        srv.flush_notifications();
        c.drain();
        if (pred()) return true;
        nap();
    }
    return pred();
}

// ── XPT-FAKE — the fake transport itself ───────────────────────────────────

static void fake_rows() {
    FakeListener lsn;
    check("XPT-FAKE-01", "a client cannot reach a fake listener that is not open",
          lsn.connect() == nullptr);

    const bool opened0 = lsn.open(0);
    const std::uint16_t eph = lsn.port();
    const bool opened_n = lsn.open(1234);
    check("XPT-FAKE-02", "open(0) reports a chosen non-zero port, open(N) reports N",
          opened0 && eph == FakeListener::kEphemeralPort && eph != 0 && opened_n &&
              lsn.port() == 1234,
          "eph=" + std::to_string(eph) + " n=" + std::to_string(lsn.port()));

    auto peer = lsn.connect(esp::ipv4(10, 1, 2, 3));
    auto t    = lsn.accept();
    auto none = lsn.accept();
    check("XPT-FAKE-03", "accept() hands over a transport born Connected, from the "
                         "connecting address, once; an accepted transport refuses "
                         "begin_connect",
          peer && t && !none && t->state() == esp::TransportState::Connected &&
              t->peer_address() == esp::ipv4(10, 1, 2, 3) &&
              !t->begin_connect("127.0.0.1", 80));

    std::uint8_t buf[64];
    peer->send("hello");
    const std::size_t n1 = t->recv(buf, sizeof(buf));
    check("XPT-FAKE-04", "bytes the client sends arrive at the server, in order",
          n1 == 5 && std::string(reinterpret_cast<char*>(buf), n1) == "hello");

    const std::string w = "world";
    const std::size_t s1 = t->send(reinterpret_cast<const std::uint8_t*>(w.data()), w.size());
    check("XPT-FAKE-05", "bytes the server sends arrive at the client, in order",
          s1 == 5 && peer->take() == "world");

    peer->set_recv_chunk(3);
    peer->send("abcdefgh");
    std::string got;
    std::size_t reads = 0;
    for (int i = 0; i < 10; ++i) {
        const std::size_t n = t->recv(buf, sizeof(buf));
        if (n == 0) break;
        ++reads;
        got.append(reinterpret_cast<char*>(buf), n);
    }
    check("XPT-FAKE-06", "a recv chunk limit yields PARTIAL reads — 3+3+2, nothing lost",
          got == "abcdefgh" && reads == 3,
          "got=" + got + " reads=" + std::to_string(reads));
    peer->set_recv_chunk(static_cast<std::size_t>(-1));

    peer->set_window(4);
    const std::string six = "123456";
    const std::size_t p1 = t->send(reinterpret_cast<const std::uint8_t*>(six.data()), 6);
    const std::string first = peer->take();
    const std::size_t p2 =
        t->send(reinterpret_cast<const std::uint8_t*>(six.data()) + p1, 6 - p1);
    check("XPT-FAKE-07", "a window yields PARTIAL writes — 4 of 6 now, the rest once the "
                         "client has read",
          p1 == 4 && first == "1234" && p2 == 2 && peer->take() == "56");

    peer->set_window(0);
    const std::size_t r0 = t->recv(buf, sizeof(buf));
    const std::size_t w0 = t->send(reinterpret_cast<const std::uint8_t*>(six.data()), 6);
    check("XPT-FAKE-08", "nothing blocks: an empty recv and a full window both return 0 "
                         "at once and the connection stays up",
          r0 == 0 && w0 == 0 && t->state() == esp::TransportState::Connected);
    peer->set_window(std::size_t{64} << 20);

    peer->send("tail");
    peer->close();
    const std::size_t tail = t->recv(buf, sizeof(buf));
    const bool up_after_tail = t->state() == esp::TransportState::Connected;
    const std::size_t eof = t->recv(buf, sizeof(buf));
    check("XPT-FAKE-09", "a client close is seen AFTER the bytes in front of it: data "
                         "first, then 0 with the state Closed",
          tail == 4 && up_after_tail && eof == 0 &&
              t->state() == esp::TransportState::Closed);

    auto peer2 = lsn.connect();
    auto t2    = lsn.accept();
    const bool before = peer2->closed_by_server();
    t2->close();
    check("XPT-FAKE-10", "a server close is seen by the client",
          !before && peer2->closed_by_server() &&
              t2->state() == esp::TransportState::Closed);

    auto peer3 = lsn.connect();
    auto t3    = lsn.accept();
    peer3->reset();
    const std::size_t rr = t3->recv(buf, sizeof(buf));
    check("XPT-FAKE-11", "a reset fails the connection: recv returns 0, state Failed, "
                         "with a reason",
          rr == 0 && t3->state() == esp::TransportState::Failed && !t3->last_error().empty());

    lsn.refuse_next_open("address already in use");
    const bool refused = !lsn.open(5000);
    const std::string why = lsn.last_error();
    const bool again = lsn.open(5000);
    check("XPT-FAKE-12", "refuse_next_open() fails exactly one open(), with its reason",
          refused && why == "address already in use" && again && lsn.listening());

    auto queued = lsn.connect();
    lsn.fail("boom");
    check("XPT-FAKE-13", "fail() stops the listener, reports why, and closes the "
                         "connections still waiting to be accepted",
          !lsn.listening() && lsn.last_error() == "boom" && queued->closed_by_server() &&
              lsn.accept() == nullptr);

    FakeListener lsn2;
    lsn2.open(0);
    auto peer4 = lsn2.connect();
    auto t4    = lsn2.accept();
    peer4->close();
    const std::size_t late = t4->send(reinterpret_cast<const std::uint8_t*>(six.data()), 6);
    check("XPT-FAKE-14", "a server send to a client that has hung up takes nothing and "
                         "reports Closed — the reset a real peer's kernel answers with",
          late == 0 && t4->state() == esp::TransportState::Closed && peer4->pending() == 0);
}

// ── XPT-SRV — the Server over the fake ─────────────────────────────────────

static void server_rows() {
    {
        LogTap    log;
        FakeRig   r;
        check("XPT-SRV-01", "open() over a listener: listening, the listener's port, and "
                            "the log line names that port",
              r.srv->listening() && r.srv->port() == FakeListener::kEphemeralPort &&
                  log.count("xpt: listening on 127.0.0.1:49152") == 1);

        r.proto.welcome = "hi\n";
        auto p = r.lsn->connect();
        check("XPT-SRV-02", "a client that has connected is NOT admitted before a service "
                            "pass: no on_connect, peer_connected() false",
              r.proto.connects == 0 && !r.srv->peer_connected() && r.lsn->queued() == 1);

        const ServiceStep s0 = r.srv->service_once(0);
        check("XPT-SRV-03", "one service pass admits it: on_connect once, "
                            "peer_connected(), and the greeting is on the wire in the "
                            "same call",
              r.proto.connects == 1 && r.srv->peer_connected() && p->take() == "hi\n" &&
                  s0 == ServiceStep::Idle && log.count("xpt: client connected from 127.0.0.1") == 1);

        p->send("pi");
        const ServiceStep half = r.srv->service_once(0);
        const std::string none = p->take();
        p->send("ng\n");
        const ServiceStep whole = r.srv->service_once(0);
        check("XPT-SRV-04", "half a command is Idle with no reply; the rest of it is "
                            "Serviced and answered in the same call",
              half == ServiceStep::Idle && none.empty() && whole == ServiceStep::Serviced &&
                  p->take() == "pong\n");

        p->send("a\nb\n");
        const ServiceStep st1 = r.srv->service_once(0);
        const std::size_t after1 = r.proto.done.size();
        const ServiceStep st2 = r.srv->service_once(0);
        const ServiceStep st3 = r.srv->service_once(0);
        check("XPT-SRV-05", "AT MOST ONE command per service_once: two pipelined commands "
                            "take two calls, and a third call is Idle",
              st1 == ServiceStep::Serviced && after1 == 2 && st2 == ServiceStep::Serviced &&
                  r.proto.done.size() == 3 && st3 == ServiceStep::Idle &&
                  p->take() == "echo a\necho b\n",
              "done=" + std::to_string(r.proto.done.size()));

        p->set_window(7);
        p->send("big 1000\n");
        r.srv->service_once(0);
        r.srv->flush_notifications();
        const std::size_t first_chunk = p->pending();
        const std::size_t held        = r.proto.notify_pending;
        std::string       got;
        for (int i = 0; i < 400 && got.size() < 1000; ++i) {
            got += p->take();
            r.srv->service_once(0);
        }
        got += p->take();
        check("XPT-SRV-06", "PARTIAL WRITES: a 1000-byte reply to a client that takes 7 at "
                            "a time is held, not dropped, and arrives whole and in order",
              first_chunk == 7 && held == 993 && is_pattern(got, 1000),
              "first=" + std::to_string(first_chunk) + " held=" + std::to_string(held) +
                  " got=" + std::to_string(got.size()));
        p->set_window(std::size_t{64} << 20);

        p->set_recv_chunk(1);
        p->send("ping\n");
        const ServiceStep one = r.srv->service_once(0);
        check("XPT-SRV-07", "PARTIAL READS: a command arriving one byte per recv is still "
                            "assembled and answered in one pass",
              one == ServiceStep::Serviced && p->take() == "pong\n");
        p->set_recv_chunk(static_cast<std::size_t>(-1));

        p->close();
        r.srv->service_once(0);
        check("XPT-SRV-08", "a client that hangs up: on_disconnect once, "
                            "peer_connected() false, logged",
              r.proto.disconnects == 1 && !r.srv->peer_connected() &&
                  log.count("xpt: client 127.0.0.1 disconnected") == 1);

        auto p2 = r.lsn->connect();
        r.srv->service_once(0);
        p2->send("ping\n");
        r.srv->service_once(0);
        check("XPT-SRV-09", "and the listener accepts again: a new client is admitted "
                            "and served",
              r.proto.connects == 2 && r.srv->peer_connected() && p2->take() == "hi\n" + std::string("pong\n"));
    }
    {
        // A command followed at once by a hang-up must still be executed — and
        // the pump interleaves flush_notifications() between service calls, so
        // this does too: the flush must not retire the client early.
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        p->send("a\nb\nc\n");
        p->close();
        std::vector<ServiceStep> steps;
        for (int i = 0; i < 6; ++i) {
            steps.push_back(r.srv->service_once(0));
            r.srv->flush_notifications();
        }
        check("XPT-SRV-10", "EOF behind a pipelined tail: every command the client sent "
                            "before hanging up is executed, one per call",
              r.proto.done == std::vector<std::string>({"a", "b", "c"}) &&
                  steps[0] == ServiceStep::Serviced && steps[2] == ServiceStep::Serviced,
              "done=" + std::to_string(r.proto.done.size()));
        check("XPT-SRV-11", "and on_disconnect comes once, AFTER the tail, although "
                            "flush_notifications() ran between the commands",
              r.proto.trail ==
                  std::vector<std::string>({"C", "S:a", "S:b", "S:c", "D"}),
              "trail size=" + std::to_string(r.proto.trail.size()));
    }
    {
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        p->send("bye\n");
        const ServiceStep st = r.srv->service_once(0);
        check("XPT-SRV-12", "the adapter closes: its last reply is delivered, then the "
                            "socket closes, on_disconnect once, the step still Serviced",
              st == ServiceStep::Serviced && p->take() == "bye\n" && p->closed_by_server() &&
                  r.proto.disconnects == 1 && !r.srv->peer_connected());
    }
    {
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        p->set_window(0);                        // the client is not reading
        p->send("bye\n");
        r.srv->service_once(0);
        const bool kept = !p->closed_by_server() && r.proto.disconnects == 1;
        p->set_window(std::size_t{64} << 20);    // ...and now it reads
        r.srv->service_once(0);
        check("XPT-SRV-13", "LINGER: a reply the client was not yet reading when the "
                            "adapter closed is kept, delivered once it reads, then closed",
              kept && p->take() == "bye\n" && p->closed_by_server() &&
                  r.proto.disconnects == 1);
    }
    {
        LogTap       log;
        ServerConfig c = cfg_named("xpt");
        c.linger_ms    = 50;
        FakeRig      r(c);
        auto         p = r.lsn->connect();
        r.srv->service_once(0);
        p->set_window(0);                        // never reads again
        p->send("bye\n");
        r.srv->service_once(0);
        const auto t0 = SteadyClock::now();
        while (!p->closed_by_server() && ms_since(t0) < 3000) {
            r.srv->service_once(0);
            nap();
        }
        const long took = ms_since(t0);
        check("XPT-SRV-14", "the linger is BOUNDED: a client that never reads is closed "
                            "after linger_ms anyway, and the dropped bytes are logged",
              p->closed_by_server() && took >= 30 && took < 3000 &&
                  log.count("xpt: dropped 4 undelivered bytes to 127.0.0.1") == 1,
              "took=" + std::to_string(took) + "ms");
    }
    {
        LogTap       log;
        ServerConfig c = cfg_named("xpt");
        c.busy_reply   = "busy\n";
        FakeRig      r(c);
        auto         p1 = r.lsn->connect();
        r.srv->service_once(0);
        auto p2 = r.lsn->connect(esp::ipv4(127, 0, 0, 2));
        r.srv->service_once(0);
        check("XPT-SRV-15", "ONE CLIENT PER LISTENER: a second connection gets the "
                            "adapter's busy reply and is closed, never on_connect, and "
                            "is logged at warn",
              p2->take() == "busy\n" && p2->closed_by_server() && r.proto.connects == 1 &&
                  log.count("xpt: refused a connection from 127.0.0.2") == 1);
        p1->send("ping\n");
        r.srv->service_once(0);
        check("XPT-SRV-16", "and the first client is untouched: still connected, still "
                            "served",
              r.srv->peer_connected() && !p1->closed_by_server() && p1->take() == "pong\n" &&
                  r.proto.disconnects == 0);
    }
    {
        FakeRig r;                               // busy_reply empty
        auto    p1 = r.lsn->connect();
        r.srv->service_once(0);
        auto p2 = r.lsn->connect();
        p2->send("ping\n");
        r.srv->service_once(0);
        check("XPT-SRV-17", "with no busy reply the second connection is closed with "
                            "nothing sent — and its bytes are never executed",
              p2->closed_by_server() && p2->pending() == 0 && r.proto.done.empty() &&
                  !p1->closed_by_server());
    }
    {
        LogTap       log;
        ServerConfig c = cfg_named("xpt");
        c.max_output   = 100;
        FakeRig      r(c);
        auto         p = r.lsn->connect();
        r.srv->service_once(0);
        p->set_window(0);
        p->send("big 500\n");
        r.srv->service_once(0);
        check("XPT-SRV-18", "a client that stops reading is DISCONNECTED once max_output "
                            "bytes are owed — neither buffered without bound nor dropped "
                            "silently",
              r.proto.disconnects == 1 && !r.srv->peer_connected() && p->closed_by_server() &&
                  log.count("xpt: client 127.0.0.1 is not reading its replies") == 1);
    }
    {
        ServerConfig c = cfg_named("xpt");
        c.max_input    = 16;
        FakeRig      r(c);
        r.proto.consume = false;                 // the adapter reads nothing yet
        auto p          = r.lsn->connect();
        r.srv->service_once(0);
        const std::string line = std::string(99, 'x') + "\n";
        p->send(line);
        for (int i = 0; i < 4; ++i) r.srv->service_once(0);
        const std::size_t held = r.proto.max_available;
        r.proto.consume = true;
        r.until([&] { return !r.proto.done.empty(); });
        check("XPT-SRV-19", "BACKPRESSURE: an adapter that does not read makes the server "
                            "stop at max_input — and nothing is lost: once it reads, the "
                            "whole 100-byte command arrives",
              held == 16 && r.proto.done.size() == 1 && r.proto.done[0] == std::string(99, 'x'),
              "held=" + std::to_string(held));
    }
    {
        LogTap  log;
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        r.lsn->fail("listener exploded");
        for (int i = 0; i < 3; ++i) r.srv->service_once(0);
        p->send("ping\n");
        r.srv->service_once(0);
        check("XPT-SRV-20", "a LISTENER fault is logged once and stops listening; the "
                            "connected client carries on",
              !r.srv->listening() && r.srv->port() == 0 &&
                  log.count("xpt: listener on 127.0.0.1:49152 failed — listener exploded") == 1 &&
                  p->take() == "pong\n");
    }
    {
        LogTap       log;
        LineProtocol proto;
        Server       srv(cfg_named("xpt"), proto);
        auto         l = std::unique_ptr<FakeListener>(new FakeListener());
        l->refuse_next_open("address already in use");
        const bool ok = srv.open(std::move(l), "127.0.0.1", 7777);
        check("XPT-SRV-21", "a bind that fails is a clean, reported error: false, the "
                            "listener's reason, logged, nothing listening",
              !ok && srv.last_error() == "address already in use" && !srv.listening() &&
                  srv.port() == 0 &&
                  log.count("xpt: cannot listen on 127.0.0.1:7777 — address already in use") ==
                      1 &&
                  srv.service_once(0) == ServiceStep::Idle);
    }
    {
        LogTap  log;
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        r.srv->stop();
        const ServiceStep after = r.srv->service_once(0);
        check("XPT-SRV-22", "stop(): the client gets on_disconnect and a closed socket, "
                            "nothing listens, it is logged, and a later pass is harmless",
              r.proto.disconnects == 1 && p->closed_by_server() && !r.srv->listening() &&
                  !r.srv->peer_connected() && after == ServiceStep::Idle &&
                  log.count("xpt: stopped listening on 127.0.0.1:49152") == 1);
    }
    {
        LineProtocol proto;
        std::shared_ptr<FakePeer> p;
        {
            Server srv(cfg_named("xpt"), proto);
            auto   l  = std::unique_ptr<FakeListener>(new FakeListener());
            auto*  lp = l.get();
            srv.open(std::move(l), "127.0.0.1", 0);
            p = lp->connect();
            srv.service_once(0);
        }
        check("XPT-SRV-23", "destroying a Server with a client connected closes the socket "
                            "and makes NO protocol call (the adapter may be half-destroyed)",
              proto.connects == 1 && proto.disconnects == 0 && p->closed_by_server());
    }
    {
        FakeRig r;
        r.proto.notify_text = "note\n";
        r.srv->flush_notifications();
        const int without = r.proto.notifies;
        auto p = r.lsn->connect();
        r.srv->service_once(0);
        r.srv->flush_notifications();
        check("XPT-SRV-24", "flush_notifications() calls on_notify only with a client "
                            "connected, and what it writes goes out in the same call",
              without == 0 && r.proto.notifies == 1 && p->take() == "note\n");
    }
    {
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        p->reset();
        r.srv->service_once(0);
        check("XPT-SRV-25", "a connection that FAILS (a reset) ends the session like a "
                            "hang-up: on_disconnect once, peer_connected() false",
              r.proto.disconnects == 1 && !r.srv->peer_connected());
    }
    {
        FakeRig r;
        auto    p = r.lsn->connect();
        r.srv->service_once(0);
        const auto t0 = SteadyClock::now();
        for (int i = 0; i < 200; ++i) r.srv->service_once(0);
        const long took = ms_since(t0);
        check("XPT-SRV-26", "service_once(0) NEVER SLEEPS: 200 calls against a silent "
                            "client take well under 100 ms",
              took < 100, "took=" + std::to_string(took) + "ms");

        const auto        t1   = SteadyClock::now();
        const ServiceStep st   = r.srv->service_once(120);
        const long        took1 = ms_since(t1);
        check("XPT-SRV-27", "service_once(120) against a silent client WAITS — most of the "
                            "120 ms — and comes back Idle well within its bound",
              st == ServiceStep::Idle && took1 >= 100 && took1 < 2000,
              "took=" + std::to_string(took1) + "ms");

        p->send("ping\n");
        const auto        t2    = SteadyClock::now();
        const ServiceStep st2   = r.srv->service_once(5000);
        const long        took2 = ms_since(t2);
        check("XPT-SRV-28", "a command already there is answered AT ONCE by a long wait",
              st2 == ServiceStep::Serviced && took2 < 500 && p->take() == "pong\n",
              "took=" + std::to_string(took2) + "ms");

        p->close();
        const auto        t3    = SteadyClock::now();
        const ServiceStep st3   = r.srv->service_once(5000);
        const long        took3 = ms_since(t3);
        check("XPT-SRV-29", "and a long wait returns early when the client goes, so the "
                            "loop owner gets its thread back",
              st3 == ServiceStep::Idle && took3 < 1000 && r.proto.disconnects == 1,
              "took=" + std::to_string(took3) + "ms");
    }
    {
        // GH #12 — the REDIAL. A client that reconnects hangs up and dials again
        // in one breath, so both reach the same pass. The server used to accept
        // before it read, and refused the redial as "a second client" of a
        // session that had already ended (the dezogif_ng conformance suite,
        // which opens a connection per check, lost every other one).
        LogTap       log;
        ServerConfig c = cfg_named("xpt");
        c.busy_reply   = "busy\n";
        FakeRig      r(c);
        r.proto.welcome = "hi\n";
        auto p1 = r.lsn->connect();
        r.srv->service_once(0);
        p1->take();
        p1->close();
        auto p2 = r.lsn->connect(esp::ipv4(127, 0, 0, 2));
        const ServiceStep st = r.srv->service_once(0);
        check("XPT-SRV-30", "a hang-up and a redial in the SAME pass: the hang-up is seen "
                            "first, so the redial is admitted and greeted in that pass — "
                            "never refused as a second client",
              r.proto.trail == std::vector<std::string>({"C", "D", "C"}) &&
                  st == ServiceStep::Idle && !p2->closed_by_server() && p2->take() == "hi\n" &&
                  r.srv->peer_connected() &&
                  log.count("xpt: refused a connection from 127.0.0.2") == 0,
              "trail size=" + std::to_string(r.proto.trail.size()));
    }
    {
        // The other side of the same edge: the hung-up client still has commands
        // queued. Its session is not over — every command it sent is executed
        // first, one per pass (XPT-SRV-10) — and the redial WAITS for it in the
        // listener's queue instead of being refused by it.
        LogTap       log;
        ServerConfig c = cfg_named("xpt");
        c.busy_reply   = "busy\n";
        FakeRig      r(c);
        auto p1 = r.lsn->connect();
        r.srv->service_once(0);
        p1->send("a\nb\n");
        p1->close();
        auto p2 = r.lsn->connect(esp::ipv4(127, 0, 0, 2));
        const ServiceStep s1     = r.srv->service_once(0);
        const std::size_t queued = r.lsn->queued();
        for (int i = 0; i < 4; ++i) {
            r.srv->service_once(0);
            r.srv->flush_notifications();
        }
        check("XPT-SRV-31", "a redial behind a hang-up with commands still queued waits "
                            "for them: both run, then the old session ends, then the "
                            "redial is admitted — never refused",
              s1 == ServiceStep::Serviced && queued == 1 &&
                  r.proto.trail ==
                      std::vector<std::string>({"C", "S:a", "S:b", "D", "C"}) &&
                  !p2->closed_by_server() &&
                  log.count("xpt: refused a connection from 127.0.0.2") == 0,
              "trail size=" + std::to_string(r.proto.trail.size()));
    }
    {
        // The session before the redial RAN A COMMAND in the same pass — the
        // adapter's closing `bye` — so the redial's own command waits for the
        // next pass (at most one per pass), but its greeting does not: it goes
        // out in the pass that admitted it.
        FakeRig r;
        r.proto.welcome = "hi\n";
        auto p1 = r.lsn->connect();
        r.srv->service_once(0);
        p1->take();
        p1->send("bye\n");
        auto p2 = r.lsn->connect(esp::ipv4(127, 0, 0, 2));
        p2->send("ping\n");
        const ServiceStep s1   = r.srv->service_once(0);
        const std::string got1 = p2->take();
        const ServiceStep s2   = r.srv->service_once(0);
        check("XPT-SRV-32", "a redial in the pass the previous session's last command "
                            "closed it: greeted in that pass, its own command served in "
                            "the next — one command per pass",
              s1 == ServiceStep::Serviced && p1->take() == "bye\n" && p1->closed_by_server() &&
                  got1 == "hi\n" && s2 == ServiceStep::Serviced && p2->take() == "pong\n" &&
                  r.proto.trail ==
                      std::vector<std::string>({"C", "S:bye", "D", "C", "S:ping"}),
              "got1='" + got1 + "' trail size=" + std::to_string(r.proto.trail.size()));
    }
}

// ── XPT-NET — the real socket layer on loopback ────────────────────────────

static void net_rows() {
    LogTap       log;
    LineProtocol proto;
    ServerConfig c = cfg_named("xpt");
    c.busy_reply   = "busy\n";
    Server srv(c, proto);
    const bool ok = srv.open("127.0.0.1", 0);
    const std::uint16_t port = srv.port();
    check("XPT-NET-01", "open(\"127.0.0.1\", 0) binds an OS-chosen port, port() reports "
                        "THAT port, and the log line names it",
          ok && port != 0 && srv.listening() &&
              log.count("xpt: listening on 127.0.0.1:" + std::to_string(port)) == 1,
          "port=" + std::to_string(port) + " err=" + srv.last_error());
    if (!ok) return;  // every row below needs the listener; their absence fails the count

    Client a;
    const bool dialled = a.connect(port);
    const bool before  = srv.peer_connected() || proto.connects != 0;
    srv.service_once(0);
    check("XPT-NET-02", "the kernel completes the client's connect, but the ACCEPT happens "
                        "inside a service pass: not admitted before, admitted after",
          dialled && !before && srv.peer_connected() && proto.connects == 1);

    a.send_all("ping\n");
    const bool pong = pump_until(srv, a, [&] { return a.rx == "pong\n"; });
    check("XPT-NET-03", "bytes flow both ways over the real socket", pong, "rx=" + a.rx);
    a.rx.clear();

    constexpr std::size_t kBig = std::size_t{16} << 20;
    proto.max_notify_pending = 0;
    a.send_all("big " + std::to_string(kBig) + "\n");
    const bool whole = pump_until(srv, a, [&] { return a.rx.size() >= kBig; }, 20000);
    check("XPT-NET-04", "a 16 MiB reply is more than the kernel takes at once — the rest "
                        "is held — and it arrives whole and in order",
          whole && proto.max_notify_pending > 0 && is_pattern(a.rx, kBig),
          "rx=" + std::to_string(a.rx.size()) +
              " held=" + std::to_string(proto.max_notify_pending));
    a.rx.clear();

    {
        // A second server with nobody connected: the no-peer arm.
        LineProtocol idle_proto;
        Server       idle(cfg_named("idle"), idle_proto);
        idle.open("127.0.0.1", 0);
        const auto t0 = SteadyClock::now();
        for (int i = 0; i < 50; ++i) idle.service_once(0);
        const long t_nopeer = ms_since(t0);
        const auto t1       = SteadyClock::now();
        for (int i = 0; i < 50; ++i) srv.service_once(0);
        const long t_silent = ms_since(t1);
        check("XPT-NET-05", "service_once(0) never blocks: 50 calls with no client, and 50 "
                            "against a connected silent one, each well under 100 ms",
              idle.listening() && t_nopeer < 100 && t_silent < 100,
              "nopeer=" + std::to_string(t_nopeer) + "ms silent=" + std::to_string(t_silent) +
                  "ms");
    }

    const auto        tw   = SteadyClock::now();
    const ServiceStep sw   = srv.service_once(150);
    const long        took = ms_since(tw);
    check("XPT-NET-06", "service_once(150) against a silent client waits most of 150 ms "
                        "and returns Idle within its bound",
          sw == ServiceStep::Idle && took >= 120 && took < 3000,
          "took=" + std::to_string(took) + "ms");

    {
        Client* ap = &a;
        std::thread late([ap] {
            std::this_thread::sleep_for(milliseconds(150));
            ap->send_all("ping\n");
        });
        const auto        t0  = SteadyClock::now();
        const ServiceStep st  = srv.service_once(5000);
        const long        el  = ms_since(t0);
        late.join();
        const bool replied = pump_until(srv, a, [&] { return a.rx == "pong\n"; });
        check("XPT-NET-07", "a long wait RETURNS EARLY when a command arrives mid-wait — "
                            "it waited for it, then answered it, far inside the 5 s",
              st == ServiceStep::Serviced && el >= 100 && el < 2500 && replied,
              "el=" + std::to_string(el) + "ms");
        a.rx.clear();
    }

    {
        Client b;
        const bool dialled_b = b.connect(port);
        const bool refused   = pump_until(srv, b, [&] { return b.eof(); });
        a.send_all("ping\n");
        const bool first_ok = pump_until(srv, a, [&] { return a.rx == "pong\n"; });
        check("XPT-NET-08", "a second concurrent client gets the busy reply and is closed; "
                            "the first keeps being served",
              dialled_b && refused && b.rx == "busy\n" && first_ok && proto.connects == 1,
              "b.rx=" + b.rx);
        a.rx.clear();
    }

    a.t->close();
    const bool gone = pump_until(srv, a, [&] { return !srv.peer_connected(); });
    check("XPT-NET-09", "a client that disconnects: peer_connected() false, on_disconnect "
                        "once",
          gone && proto.disconnects == 1);

    Client d;
    const bool dialled_d = d.connect(port);
    d.send_all("ping\n");
    const bool again = pump_until(srv, d, [&] { return d.rx == "pong\n"; });
    check("XPT-NET-10", "and the listener accepts a new client afterwards",
          dialled_d && again && proto.connects == 2 && srv.peer_connected());

    {
        LineProtocol other;
        Server       clash(cfg_named("clash"), other);
        const bool   bound = clash.open("127.0.0.1", port);
        check("XPT-NET-11", "a port already in use is a clean, reported error: false, a "
                            "reason, an error line — and the server holding it is "
                            "untouched",
              !bound && !clash.last_error().empty() && !clash.listening() &&
                  log.count("clash: cannot listen on 127.0.0.1:" + std::to_string(port)) == 1 &&
                  srv.listening() && srv.port() == port,
              "err=" + clash.last_error());
    }
    {
        LineProtocol other;
        Server       bad(cfg_named("bad"), other);
        const bool   word = bad.open("not-an-address", 0);
        const std::string why = bad.last_error();
        const bool   name = bad.open("localhost", 0);
        check("XPT-NET-12", "an address that is not a numeric IP — a word, or a host NAME, "
                            "which is refused rather than resolved — is a clean error",
              !word && why.find("not a numeric IP address") != std::string::npos && !name &&
                  !bad.listening(),
              "why=" + why);
        const bool foreign = bad.open("192.0.2.1", 0);  // TEST-NET-1: never a local address
        check("XPT-NET-13", "an address that is not on this host is a clean bind error",
              !foreign && !bad.last_error().empty() && !bad.listening(),
              "err=" + bad.last_error());
    }
    srv.stop();
}

// ── XPT-PUMP — the real listener inside Debugger::pump() ───────────────────

static void pump_rows() {
    // A 48K machine parked on `JR $` — the backend test's idiom, no ROM needed.
    Emulator      emu;
    EmulatorConfig ecfg;
    ecfg.type = MachineType::ZX48K;
    emu.init(ecfg);
    emu.mmu().write(0x8000, 0x18);
    emu.mmu().write(0x8001, 0xFE);
    Z80Registers regs = emu.cpu().get_registers();
    regs.PC   = 0x8000;
    regs.SP   = 0xFF00;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    Debugger     dbg(emu);
    LineProtocol proto;
    proto.dbg = &dbg;
    Server srv(cfg_named("xpt"), proto);
    if (!srv.open("127.0.0.1", 0)) {
        check("XPT-PUMP-01", "the pump rows need a listener", false, srv.last_error());
        return;
    }
    dbg.add_service(srv);

    const bool none = !dbg.pump(PumpBudget{}).remote_attached;
    Client a;
    const bool dialled = a.connect(srv.port());
    const bool hint    = dbg.pump(PumpBudget{}).remote_attached;
    check("XPT-PUMP-01", "registered through add_service(), the listener feeds "
                         "ServiceHint::remote_attached: false with no client, true once "
                         "one has been accepted inside pump()",
          none && dialled && hint && proto.connects == 1 && dbg.attached());

    PumpBudget generous;
    generous.max_wait_ms = 0;
    generous.drain_ms    = 50;
    generous.budget_ms   = 2000;

    // RUNNING: five commands queued, one pump, exactly one executed.
    a.send_all("r1\nr2\nr3\nr4\nr5\n");
    std::this_thread::sleep_for(milliseconds(20));  // loopback: all five are in
    proto.done.clear();
    dbg.pump(generous);
    check("XPT-PUMP-02", "while RUNNING a pump services exactly ONE command, however "
                         "generous the budget and however many are queued",
          !dbg.state().paused && proto.done.size() == 1,
          "done=" + std::to_string(proto.done.size()));
    // Let the other four through before the paused rows.
    for (int i = 0; i < 20 && proto.done.size() < 5; ++i) dbg.pump(PumpBudget{});
    a.drain();
    a.rx.clear();

    // A command that PAUSES the machine, sent over the wire.
    a.send_all("pause\n");
    const auto t0 = SteadyClock::now();
    while (!dbg.state().paused && ms_since(t0) < 3000) {
        dbg.pump(PumpBudget{});
        nap();
    }
    a.drain();
    check("XPT-PUMP-03", "a command read from the socket drives the machine inside "
                         "pump(): `pause` pauses it",
          dbg.state().paused);

    // PAUSED: five commands queued, one pump drains them all.
    proto.done.clear();
    a.rx.clear();
    a.send_all("p1\np2\np3\np4\np5\n");
    std::this_thread::sleep_for(milliseconds(20));
    dbg.pump(generous);
    const std::size_t drained = proto.done.size();
    const bool replies = pump_until(srv, a, [&] {
        return a.rx == "echo p1\necho p2\necho p3\necho p4\necho p5\n";
    });
    check("XPT-PUMP-04", "while PAUSED a queued chain is DRAINED in one pump — all five "
                         "commands, all five replies",
          drained == 5 && replies, "drained=" + std::to_string(drained) + " rx=" + a.rx);
    a.rx.clear();

    {
        PumpBudget wait;
        wait.max_wait_ms = 300;
        const auto t1 = SteadyClock::now();
        dbg.pump(wait);
        const long took = ms_since(t1);
        check("XPT-PUMP-05", "pump(wait) while paused with a silent client WAITS — most of "
                             "max_wait_ms — and returns within its bound: the headless "
                             "poll() that replaces a busy spin",
              took >= 250 && took < 3000, "took=" + std::to_string(took) + "ms");
    }
    {
        Client* ap = &a;
        std::thread late([ap] {
            std::this_thread::sleep_for(milliseconds(150));
            ap->send_all("ping\n");
        });
        PumpBudget wait;
        wait.max_wait_ms = 5000;
        proto.done.clear();
        const auto t1 = SteadyClock::now();
        dbg.pump(wait);
        const long took = ms_since(t1);
        late.join();
        check("XPT-PUMP-06", "and returns EARLY when a command arrives during the wait, "
                             "having executed it",
              took >= 100 && took < 2500 && proto.done == std::vector<std::string>({"ping"}),
              "took=" + std::to_string(took) + "ms");
        a.drain();
        a.rx.clear();
    }
    {
        const auto t1 = SteadyClock::now();
        for (int i = 0; i < 50; ++i) dbg.pump(PumpBudget{});
        const long took = ms_since(t1);
        check("XPT-PUMP-07", "PumpBudget{} never blocks, paused with a silent client "
                             "attached: 50 pumps well under 250 ms",
              took < 250, "took=" + std::to_string(took) + "ms");
    }

    // The client drops its socket while its own pause holds the machine.
    a.t->close();
    const auto t2 = SteadyClock::now();
    bool       gone = false;
    while (ms_since(t2) < 3000) {
        if (!dbg.pump(PumpBudget{}).remote_attached) {
            gone = true;
            break;
        }
        nap();
    }
    check("XPT-PUMP-08", "a client that drops its socket: remote_attached goes false, and "
                         "the adapter's detach on on_disconnect releases the pause IT held "
                         "(SES-01 — a crashed client must not leave the machine hung)",
          gone && proto.disconnects == 1 && !dbg.state().paused);

    dbg.remove_service(srv);
    Client b;
    const bool dialled_b = b.connect(srv.port());
    b.send_all("ping\n");
    bool answered = false;
    for (int i = 0; i < 20; ++i) {
        dbg.pump(generous);
        b.drain();
        if (!b.rx.empty()) answered = true;
        nap();
    }
    check("XPT-PUMP-09", "remove_service() stops it: a client that connects afterwards is "
                         "neither accepted nor answered by pump(), and nothing is remote",
          dialled_b && !answered && proto.connects == 1 &&
              !dbg.pump(PumpBudget{}).remote_attached);
    srv.stop();
}

int main() {
    std::printf("remote_transport_test — the shared debugger socket transport (GH #287)\n");
    fake_rows();
    server_rows();
    net_rows();
    pump_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
