// jnext::remote::dzrp - the DZRP CLIENT (GH #511, epic #486). The fake-transport unit suite of the
// client half: framing, the session, the profiles, the send gate, the wait hook, NTF_LOG, the probe.
//
// WHAT IS UNDER TEST. `encode_command` / `ResponseParser` (src/remote/dzrp/dzrp_frame.*),
// `DzrpClient` (dzrp_client.*), `MemoryLink` and the probe's logic (test/remote/dzrp_probe.*). The
// oracles are the DZRP 2.2.0 spec (DeZog `design/DeZogProtocol.md`), cross-checked against jnext's
// OWN `DzrpServer` (its `FrameParser` / `encode_response`, and a real server on a real 48K machine
// over the memory link), plus the plan's profile table
// (doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md section 3). The T80N core has no debugger: there is
// no VHDL counterpart.
//
//   DZC-FR-*    framing: both length conventions, the cap, the chunk timeout, the serial preamble.
//   DZC-SES-*   the session: CMD_INIT, sequence numbers, the window, timeouts, notifications, detach_link.
//   DZC-ID-*    the identity rule: which remote is which profile.
//   DZC-GATE-*  the send gate: nothing outside the profile's set, nothing to a running program.
//   DZC-WAIT-*  the wait hook that lets a client talk to a server on its own thread.
//   DZC-LOG-*   NTF_LOG's format string.
//   DZC-E2E-*   a session against a real DzrpServer.
//   DZC-PRB-*   the probe (the R0.5 checkpoint tool) over a memory link.
//
// No socket anywhere. EVERY WAIT IS BOUNDED: the client's clock is a fake one (`g_now`, optionally
// auto-advancing), every loop has an iteration cap.
//
// Run: ./build/test/dzrp_client_test

#include "remote/client_link.h"
#include "remote/dzrp/dzrp_client.h"
#include "remote/dzrp/dzrp_frame.h"
#include "remote/dzrp/dzrp_server.h"
#include "remote/dzrp/dzrp_session.h"
#include "remote/fake_transport.h"
#include "remote/memory_link.h"

#include "dzrp_probe.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"
#include "version.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::remote::FakeListener;
using jnext::remote::LinkKind;
using jnext::remote::LinkSpec;
using jnext::remote::LinkState;
using jnext::remote::MemoryLink;
using namespace jnext::remote::dzrp;
using SteadyClock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;

// -- Tiny test harness (matches dzrp_adapter_test) ----------------------------------------------

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ", detail.c_str());
    }
}

// -- The fake clock the client reads ------------------------------------------------------------

static SteadyClock::time_point g_now = SteadyClock::time_point{} + std::chrono::hours(1);
static long                    g_auto_ms = 1;   // each read advances the clock by this much: a wait that
                                                // nothing answers ends in a Timeout, never a hang
struct AutoMs {   // a row that needs another rate (0 = exact), restored at the end of its scope
    long old;
    explicit AutoMs(long v) : old(g_auto_ms) { g_auto_ms = v; }
    ~AutoMs() { g_auto_ms = old; }
};

static SteadyClock::time_point fake_now() {
    g_now += milliseconds(g_auto_ms);
    return g_now;
}
static void advance(long ms) { g_now += milliseconds(ms); }

static const char* const TOKEN = "0123456789abcdef";   // never this process's token

static ClientConfig cfg() {
    ClientConfig c;
    c.session_token = TOKEN;
    c.clock         = fake_now;
    return c;
}

// -- Log capture --------------------------------------------------------------------------------

struct LogTap {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
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
    std::string first(const std::string& needle) const {
        for (const auto& l : ring->last_formatted())
            if (l.find(needle) != std::string::npos) return l;
        return {};
    }
};

// -- Byte helpers -------------------------------------------------------------------------------

static Bytes pattern(std::size_t n, unsigned salt) {
    Bytes s(n);
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<std::uint8_t>((i * 7u + salt + (i >> 8)) & 0xFF);
    return s;
}

static Bytes le32b(std::uint32_t v) {
    return {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
            static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
}

static Bytes cat(Bytes a, const Bytes& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static Bytes str(const std::string& s) { return Bytes(s.begin(), s.end()); }

/// Feed `b` to `p` until it is consumed, ready or failed. Returns bytes taken.
template <class P>
static std::size_t feed_all(P& p, const Bytes& b, SteadyClock::time_point t = g_now) {
    std::size_t off = 0;
    while (off < b.size() && p.wanted() > 0) {
        const std::size_t k = p.feed(b.data() + off, b.size() - off, t);
        if (k == 0) break;
        off += k;
    }
    return off;
}

// -- A scripted DZRP peer -----------------------------------------------------------------------

static Bytes default_bitfield() {
    // jnext's own: 1-4, 6-11, 15-21, 23-28, 39-43, 50, 51 (the server's table, legacy excluded).
    Bytes b(7, 0);
    for (int id : {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28,
                   39, 40, 41, 42, 43, 50, 51})
        b[id / 8] = static_cast<std::uint8_t>(b[id / 8] | (1u << (id % 8)));
    return b;
}

static Bytes bitfield_of(std::initializer_list<int> ids) {
    Bytes b;
    for (int id : ids) {
        if (b.size() <= static_cast<std::size_t>(id / 8)) b.resize(static_cast<std::size_t>(id / 8) + 1, 0);
        b[id / 8] = static_cast<std::uint8_t>(b[id / 8] | (1u << (id % 8)));
    }
    return b;
}

struct Script {
    FakeListener                       lsn;
    std::unique_ptr<esp::EspTransport> t;
    FrameParser                        fp;
    std::vector<int>                   ids;    // every command received, in order, all connections
    std::vector<int>                   seqs;
    std::vector<Command>               cmds;
    std::vector<Command>               held;   // what a `hold` handler kept
    std::string                        name = "jnext v1.4.1";
    std::uint8_t                       ver[3] = {2, 2, 0};
    std::uint8_t                       machine = 4;
    std::uint8_t                       init_err = 0;
    bool                               raw_init = false;
    Bytes                              init_raw;
    Bytes                              bitfield = default_bitfield();
    /// First look at every command: true = handled (a reply written, or none on purpose).
    std::function<bool(Script&, const Command&)> custom;

    Script() { lsn.open(0); }

    void send(const Bytes& f) {
        if (t) t->send(f.data(), f.size());
    }
    void reply(std::uint8_t seq, const Bytes& payload = {}) { send(encode_response(seq, payload)); }
    void notify(const Bytes& payload) { send(encode_response(0, payload)); }   // payload[0] = id
    void close() {
        if (t) t->close();
    }
    void release(std::size_t i, const Bytes& payload = {}) {
        if (i < held.size()) reply(held[i].seq, payload);
    }

    Bytes init_reply() const {
        if (raw_init) return init_raw;
        Bytes p = {init_err, ver[0], ver[1], ver[2], machine};
        p.insert(p.end(), name.begin(), name.end());
        p.push_back(0);
        return p;
    }

    static Bytes realistic(const Command& c) {
        if (c.id == CMD_GET_REGISTERS) {
            Bytes p;
            const std::uint16_t words[12] = {0x8000, 0xFF00, 0x0044, 0, 0, 0, 0, 0x5C3A, 0, 0, 0, 0};
            for (std::uint16_t w : words) {
                p.push_back(static_cast<std::uint8_t>(w));
                p.push_back(static_cast<std::uint8_t>(w >> 8));
            }
            for (std::uint8_t b : {std::uint8_t{0x12}, std::uint8_t{0x3F}, std::uint8_t{1}, std::uint8_t{0},
                                   std::uint8_t{8}})
                p.push_back(b);
            for (int i = 0; i < 8; ++i) p.push_back(static_cast<std::uint8_t>(0x10 + i));
            return p;
        }
        if (c.id == CMD_READ_MEM && c.payload.size() >= 5) {
            const std::size_t n = static_cast<std::size_t>(c.payload[3] | (c.payload[4] << 8));
            return pattern(n, c.payload[1]);
        }
        if (c.id == CMD_GET_TBBLUE_REG) return {0x0A};
        return {};
    }

    void handle(const Command& c) {
        ids.push_back(c.id);
        seqs.push_back(c.seq);
        cmds.push_back(c);
        if (custom && custom(*this, c)) return;
        if (c.id == CMD_INIT) reply(c.seq, init_reply());
        else if (c.id == CMD_GET_SUPPORTED_COMMANDS) reply(c.seq, bitfield);
        else reply(c.seq, realistic(c));
    }

    /// One service pass: accept, read, answer. Also the client's wait hook.
    void service() {
        for (int round = 0; round < 64; ++round) {
            if (!t || t->state() != esp::TransportState::Connected) {
                auto n = lsn.accept();
                if (n) {
                    t = std::move(n);
                    fp.reset();
                } else if (!t) {
                    return;
                }
            }
            std::uint8_t buf[4096];
            const std::size_t n = t->recv(buf, sizeof(buf));
            if (n == 0) return;
            std::size_t off = 0;
            while (off < n) {
                const std::size_t k = fp.feed(buf + off, n - off, g_now);
                off += k;
                if (fp.ready()) handle(fp.take());
                else if (k == 0 || fp.failed()) break;
            }
        }
    }

    bool count_ids_outside(const std::vector<int>& allowed, int& n) const {
        n = 0;
        for (int id : ids)
            if (std::find(allowed.begin(), allowed.end(), id) == allowed.end()) ++n;
        return true;
    }
};

static std::unique_ptr<DzrpClient> make_client(Script& s, LinkKind kind = LinkKind::Tcp,
                                               ClientConfig c = cfg()) {
    auto cl = std::unique_ptr<DzrpClient>(
        new DzrpClient(std::unique_ptr<jnext::remote::ClientLink>(new MemoryLink(s.lsn, kind)), c));
    // A blocking request waits through this hook: the script answers from it.
    cl->set_wait_hook([&s] { s.service(); });
    return cl;
}

/// Start the client and run the handshake against the script. True when Ready.
static bool bring_up(DzrpClient& c, Script& s) {
    if (!c.start()) return false;
    for (int i = 0; i < 200; ++i) {
        c.step();
        s.service();
        if (c.state() == SessionState::Ready || c.state() == SessionState::Failed ||
            c.state() == SessionState::Closed)
            break;
    }
    return c.state() == SessionState::Ready;
}

/// Hold every command after the handshake (reply later with `release`).
static void hold(Script& s) {
    s.custom = [](Script& sc, const Command& c) {
        if (c.id == CMD_INIT || c.id == CMD_GET_SUPPORTED_COMMANDS) return false;
        sc.held.push_back(c);
        return true;
    };
}

/// Drive the client until `done` or an iteration cap.
static void spin(DzrpClient& c, Script& s, const std::function<bool()>& done, int cap = 100) {
    for (int i = 0; i < cap && !done(); ++i) {
        c.poll(0);
        s.service();
    }
}

// -- A real server on a real machine (the dzrp_adapter_test rig idiom) --------------------------

static SteadyClock::time_point server_clock() { return SteadyClock::time_point{} + std::chrono::hours(1); }

struct Rig {
    Emulator                    emu;
    std::unique_ptr<Debugger>   dbg;
    std::unique_ptr<DzrpServer> dzrp;
    FakeListener*               lsn = nullptr;

    Rig() {
        EmulatorConfig ec;
        ec.type = MachineType::ZX48K;
        emu.init(ec);
        emu.mmu().write(0x8000, 0x18);
        emu.mmu().write(0x8001, 0xFE);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = 0x8000;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);
        dbg  = std::make_unique<Debugger>(emu);
        dzrp = std::make_unique<DzrpServer>(*dbg, server_clock);
        auto l = std::make_unique<FakeListener>();
        lsn    = l.get();
        dzrp->server().open(std::move(l), "127.0.0.1", 0);
        dbg->add_service(dzrp->server());
    }
    ~Rig() {
        dzrp.reset();
        dbg.reset();
    }
    void pump() { dbg->pump(PumpBudget{}); }
};

static std::unique_ptr<DzrpClient> rig_client(Rig& rig, ClientConfig c = cfg(), LinkKind k = LinkKind::Tcp) {
    auto cl = std::unique_ptr<DzrpClient>(
        new DzrpClient(std::unique_ptr<jnext::remote::ClientLink>(new MemoryLink(*rig.lsn, k)), c));
    cl->set_wait_hook([&rig] { rig.pump(); });
    return cl;
}

static bool rig_up(DzrpClient& c, Rig& rig) {
    if (!c.start()) return false;
    for (int i = 0; i < 200; ++i) {
        c.step();
        rig.pump();
        if (c.state() == SessionState::Ready || c.state() == SessionState::Failed) break;
    }
    return c.state() == SessionState::Ready;
}

// ==============================================================================================
// DZC-FR - framing
// ==============================================================================================

static void framing_rows() {
    // FR-01: the command encoder against the SERVER's FrameParser, at the boundary lengths.
    {
        bool        all = true;
        std::string why;
        for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{65535}, std::size_t{65536},
                              static_cast<std::size_t>(MAX_PAYLOAD_BYTES)}) {
            const Bytes pl = pattern(n, 3);
            const Bytes f  = encode_command(7, 0x42, pl);
            FrameParser fp;
            const std::size_t taken = feed_all(fp, f);
            if (f.size() != COMMAND_HEADER_BYTES + n || !fp.ready() || taken != 6 + n || fp.wanted() != 0) {
                all = false;
                why = "n=" + std::to_string(n) + " size=" + std::to_string(f.size()) + " taken=" + std::to_string(taken);
                break;
            }
            const Command c = fp.take();
            if (c.seq != 7 || c.id != 0x42 || c.payload != pl) {
                all = false;
                why = "n=" + std::to_string(n) + " content";
                break;
            }
        }
        check("DZC-FR-01", "encode_command: the server's FrameParser yields the same seq, id and payload at 0, 1, 65535, 65536 and the cap, taking exactly 6+n bytes", all, why);
    }
    // FR-02
    {
        const Bytes over = encode_command(1, 3, nullptr, static_cast<std::size_t>(MAX_PAYLOAD_BYTES) + 1);
        const Bytes zero = encode_command(0, 3, Bytes{});
        const Bytes ok   = encode_command(1, 3, Bytes{});
        check("DZC-FR-02", "encode_command returns an empty frame for a payload over the cap and for seq 0, and a 6-byte frame for neither",
              over.empty() && zero.empty() && ok.size() == 6);
    }
    // FR-03: the server's responses through the client parser.
    {
        bool        all = true;
        std::string why;
        for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{65535}, std::size_t{65536},
                              static_cast<std::size_t>(MAX_PAYLOAD_BYTES) - 1}) {
            const Bytes pl = pattern(n, 9);
            const Bytes f  = encode_response(5, pl);
            ResponseParser rp;
            const std::size_t taken = feed_all(rp, f);
            if (!rp.ready() || taken != f.size()) {
                all = false;
                why = "n=" + std::to_string(n) + " not ready";
                break;
            }
            const Response r = rp.take();
            if (r.seq != 5 || r.payload != pl || r.notification()) {
                all = false;
                why = "n=" + std::to_string(n) + " content";
                break;
            }
        }
        check("DZC-FR-03", "ResponseParser yields the same seq and payload as the server's encode_response at 0, 1, 65535, 65536 and the largest reply (cap-1)", all, why);
    }
    // FR-04: the cap is on the length field, checked before any payload byte.
    {
        ResponseParser ok;
        Bytes          f = cat(le32b(MAX_PAYLOAD_BYTES), Bytes{1});
        f.resize(f.size() + (MAX_PAYLOAD_BYTES - 1), 0x5A);
        const std::size_t t1 = feed_all(ok, f);
        const bool        at_cap = ok.ready() && !ok.failed() && t1 == f.size() &&
                            ok.take().payload.size() == MAX_PAYLOAD_BYTES - 1;
        ResponseParser over;
        Bytes          g = cat(le32b(MAX_PAYLOAD_BYTES + 1), Bytes(100, 0x5A));
        const std::size_t t2 = over.feed(g.data(), g.size(), g_now);
        check("DZC-FR-04", "a response length field equal to the cap is accepted; cap+1 fails after exactly the 4 length bytes and no payload byte is taken",
              at_cap && over.failed() && t2 == 4 && over.received() == 4,
              "taken=" + std::to_string(t2));
    }
    // FR-05
    {
        ResponseParser rp;
        const Bytes    f = {0, 0, 0, 0, 1, 2, 3};
        feed_all(rp, f);
        check("DZC-FR-05", "a response length field of 0 is refused (no sequence byte)",
              rp.failed() && rp.error().find("no sequence byte") != std::string::npos, rp.error());
    }
    // FR-06
    {
        const Bytes f = encode_response(9, pattern(19, 4));   // length 20: seq + 19
        bool        all = f.size() == 24;
        std::string why;
        for (std::size_t cut = 1; cut < f.size() && all; ++cut) {
            ResponseParser rp;
            Bytes a(f.begin(), f.begin() + static_cast<std::ptrdiff_t>(cut));
            Bytes b(f.begin() + static_cast<std::ptrdiff_t>(cut), f.end());
            feed_all(rp, a);
            const bool early = rp.ready();
            feed_all(rp, b);
            if (early || !rp.ready() || rp.take().payload != pattern(19, 4)) {
                all = false;
                why = "cut=" + std::to_string(cut);
            }
        }
        check("DZC-FR-06", "a 20-byte-length response fed split at every point parses like a one-shot feed", all, why);
    }
    // FR-07
    {
        const Bytes a = encode_response(1, Bytes{1, 2, 3});
        const Bytes b = encode_response(2, Bytes{4});
        ResponseParser rp;
        const Bytes ab = cat(a, b);
        const std::size_t k = rp.feed(ab.data(), ab.size(), g_now);
        const bool first = rp.ready() && k == a.size() && rp.wanted() == 0;
        const Response r1 = rp.take();
        const std::size_t k2 = rp.feed(ab.data() + k, ab.size() - k, g_now);
        check("DZC-FR-07", "two frames in one buffer: the first feed stops exactly at the end of frame 1",
              first && r1.seq == 1 && k2 == b.size() && rp.ready() && rp.take().seq == 2);
    }
    // FR-08
    {
        const SteadyClock::time_point T = g_now;
        ResponseParser rp;
        const Bytes    f = encode_response(1, pattern(10, 1));
        rp.feed(f.data(), 6, T);
        const bool a = !rp.stalled(T + milliseconds(4999));
        rp.feed(f.data() + 6, 1, T + milliseconds(4000));
        const bool b = !rp.stalled(T + milliseconds(4000 + 4999));
        const bool c = rp.stalled(T + milliseconds(4000 + 5000));
        check("DZC-FR-08", "a partial frame is stalled 5000 ms after its LAST progress, not before, and not from the frame start",
              a && b && c, std::string(a ? "a" : "!a") + (b ? "b" : "!b") + (c ? "c" : "!c"));
    }
    // FR-09
    {
        ResponseParser rp;
        feed_all(rp, encode_response(0, Bytes{2, 'a', 'b'}));
        const bool n1 = rp.ready();
        const Response r = rp.take();
        ResponseParser bad;
        feed_all(bad, Bytes{1, 0, 0, 0, 0});
        check("DZC-FR-09", "seq 0 + id is a notification with payload[0] = id; seq 0 with no id byte is refused",
              n1 && r.notification() && r.payload.size() == 3 && r.payload[0] == 2 && bad.failed() &&
                  bad.error().find("notification without an id") != std::string::npos,
              bad.error());
    }
    // FR-10: the order of frames as DzrpServer::queue_pause_ntf encodes the notification.
    {
        const Bytes pause = {NTF_PAUSE, BREAK_BP, 0x00, 0x80, 0x0B, 'x', 0};
        Bytes stream = cat(cat(encode_response(1, Bytes{9}), encode_response(0, pause)), encode_response(2, Bytes{}));
        ResponseParser    rp;
        std::vector<Response> got;
        std::size_t off = 0;
        while (off < stream.size() && got.size() < 3) {
            const std::size_t k = rp.feed(stream.data() + off, stream.size() - off, g_now);
            off += k;
            if (rp.ready()) got.push_back(rp.take());
            else if (k == 0) break;
        }
        check("DZC-FR-10", "response, NTF_PAUSE, response parse as three frames in order",
              got.size() == 3 && got[0].seq == 1 && got[1].notification() && got[1].payload == pause &&
                  got[2].seq == 2 && off == stream.size());
    }
    // FR-11: the preamble.
    {
        bool        all = true;
        std::string why;
        const Bytes frame = encode_response(3, Bytes{7, 8});
        struct Case { Bytes junk; };
        const std::vector<Bytes> junks = {Bytes{}, Bytes(1, 0), Bytes(1000, 0), Bytes{0x12, 0x34, 0x56}};
        for (std::size_t i = 0; i < junks.size() && all; ++i) {
            ResponseParser rp(ResponseParser::Options{true});
            feed_all(rp, cat(cat(junks[i], Bytes{0xA5}), frame));
            if (!rp.ready() || rp.swallowed() != junks[i].size() || rp.take().payload != Bytes{7, 8}) {
                all = false;
                why = "case " + std::to_string(i);
            }
        }
        check("DZC-FR-11", "preamble: runs of 0, 1 and 1000 zero bytes and non-zero junk before 0xA5 are swallowed, the frame parses and swallowed() counts them",
              all, why);
    }
    // FR-12
    {
        ResponseParser rp(ResponseParser::Options{true});
        const Bytes    frame = encode_response(3, Bytes{7, 8});
        rp.feed(Bytes{0x00, 0xA5}.data(), 2, g_now);
        const bool hunting_over = !rp.ready();
        feed_all(rp, frame);
        check("DZC-FR-12", "preamble: an 0xA5 that ends one feed is not lost, the frame follows in the next feed",
              hunting_over && rp.ready() && rp.take().payload == Bytes{7, 8});
    }
    // FR-13
    {
        ResponseParser rp(ResponseParser::Options{true});
        const Bytes    frame = encode_response(0xA5, Bytes(164, 0xA5));   // length 165 = 0xA5
        feed_all(rp, cat(Bytes{0xA5}, frame));
        const bool ready = rp.ready();
        const Response r = rp.take();
        check("DZC-FR-13", "preamble: an 0xA5 inside a frame (length byte, seq, payload) is data",
              ready && r.seq == 0xA5 && r.payload == Bytes(164, 0xA5));
    }
    // FR-14
    {
        // Option OFF: two frames of length 0xA5, whose first byte is 0xA5, decode literally.
        const Bytes f = encode_response(1, Bytes(164, 0xA5));
        ResponseParser rp;
        const Bytes two = cat(f, f);
        std::vector<Response> got;
        std::size_t off = 0;
        while (off < two.size() && got.size() < 2) {
            const std::size_t k = rp.feed(two.data() + off, two.size() - off, g_now);
            off += k;
            if (rp.ready()) got.push_back(rp.take());
            else if (k == 0) break;
        }
        ResponseParser zero;
        feed_all(zero, Bytes{0, 0, 0, 0});
        check("DZC-FR-14", "preamble OFF: a leading 0xA5 is a length byte (two frames decode literally) and a leading 0x00 is a length byte too",
              got.size() == 2 && got[0].payload == Bytes(164, 0xA5) && got[1].payload == got[0].payload &&
                  zero.failed());
    }
    // FR-15: the production Server and a real DzrpServer.
    {
        Rig rig;
        MemoryLink link(*rig.lsn);
        link.open();
        const Bytes pl = pattern(300, 5);
        const Bytes f  = encode_command(7, CMD_LOOPBACK, pl);
        link.write(f.data(), f.size());
        ResponseParser rp;
        for (int i = 0; i < 16 && !rp.ready(); ++i) {
            rig.pump();
            std::uint8_t buf[1024];
            const std::size_t n = link.read(buf, sizeof(buf));
            if (n) feed_all(rp, Bytes(buf, buf + n));
        }
        const bool ready = rp.ready() && !rp.failed();
        const Response r = ready ? rp.take() : Response{};
        check("DZC-FR-15", "encode_command(CMD_LOOPBACK, 300 bytes) over a MemoryLink to a real DzrpServer: the reply parses with seq 7 and the same 300 bytes",
              ready && r.seq == 7 && r.payload == pl);
    }
    // FR-16
    {
        ResponseParser rp;
        feed_all(rp, Bytes{0, 0, 0, 0});
        const bool failed = rp.failed();
        const std::size_t taken = rp.feed(encode_response(1, Bytes{1}).data(), 6, g_now);
        const bool sticky = rp.failed() && taken == 0 && !rp.ready();
        rp.reset();
        feed_all(rp, encode_response(1, Bytes{1}));
        check("DZC-FR-16", "the parser error is sticky until reset(); after reset() a good frame parses",
              failed && sticky && !rp.failed() && rp.ready() && rp.take().payload == Bytes{1});
    }
    // FR-17: the serial stub prefixes EVERY frame: the hunt is re-armed after each one.
    {
        const Bytes f1 = encode_response(3, Bytes{7, 8});
        const Bytes f2 = encode_response(4, Bytes{9});
        const Bytes j1 = {0x00, 0x11, 0x00};
        const Bytes j2 = {0x00, 0x00, 0x22, 0x33, 0x00};
        const Bytes stream = cat(cat(cat(cat(j1, Bytes{0xA5}), f1), cat(j2, Bytes{0xA5})), f2);
        auto parse = [&](std::size_t step, std::size_t& swallowed) {
            ResponseParser rp(ResponseParser::Options{true});
            std::vector<Response> got;
            std::size_t off = 0;
            while (off < stream.size()) {
                const std::size_t n = std::min(step, stream.size() - off);
                std::size_t used = 0;
                while (used < n) {
                    const std::size_t k = rp.feed(stream.data() + off + used, n - used, g_now);
                    used += k;
                    if (rp.ready()) got.push_back(rp.take());
                    else if (k == 0) break;
                }
                off += n;
            }
            swallowed = rp.swallowed();
            return got;
        };
        std::size_t sw1 = 0, sw2 = 0;
        const auto one = parse(stream.size(), sw1);
        const auto split = parse(1, sw2);
        const std::size_t want = j1.size() + j2.size();
        check("DZC-FR-17", "preamble: two frames back to back, the second with its own junk, parse in one feed and split byte by byte, and swallowed() counts the junk of both",
              one.size() == 2 && split.size() == 2 && one[0].payload == Bytes{7, 8} && one[1].payload == Bytes{9} &&
                  split[1].seq == 4 && sw1 == want && sw2 == want,
              std::to_string(sw1) + "/" + std::to_string(sw2));
    }
}

// ==============================================================================================
// DZC-SES - the session
// ==============================================================================================

static bool link_closed(const DzrpClient& c) { return c.link().state() == LinkState::Closed; }

static void session_rows() {
    // SES-01: the CMD_INIT bytes.
    {
        Script s;
        auto   c = make_client(s);
        bring_up(*c, s);
        const std::string nm = std::string("jnext v") + JNEXT_VERSION_STRING + " (session " + TOKEN + ")";
        const Bytes want = cat(cat(Bytes{2, 2, 0}, str(nm)), Bytes{0});
        check("DZC-SES-01", "CMD_INIT payload is 02 02 00 + \"jnext v<version> (session <token>)\" + NUL, the configured token",
              !s.cmds.empty() && s.cmds[0].id == CMD_INIT && s.cmds[0].payload == want);
    }
    // SES-02
    {
        Script s;
        s.bitfield = {0xFE, 0xFF, 0xFF, 0x1F, 0x00, 0x1E, 0x0F};
        auto c = make_client(s);
        const bool up = bring_up(*c, s);
        bool agree = true;
        for (int i = 0; i < 256; ++i) {
            const bool want = i < 56 && i != 0 && ((s.bitfield[i / 8] >> (i % 8)) & 1);
            if (c->in_set(static_cast<std::uint8_t>(i)) != want) agree = false;
        }
        check("DZC-SES-02", "jnext 2.2.0 machine 4: the next command is CMD_GET_SUPPORTED_COMMANDS and the bitfield becomes the set bit for bit; Ready, profile Jnext",
              up && s.ids == std::vector<int>({CMD_INIT, CMD_GET_SUPPORTED_COMMANDS}) && agree &&
                  c->profile() == Profile::Jnext);
    }
    // SES-03
    {
        auto run = [](std::uint8_t a, std::uint8_t b, std::uint8_t d) {
            Script s;
            s.ver[0] = a;
            s.ver[1] = b;
            s.ver[2] = d;
            auto c = make_client(s);
            bring_up(*c, s);
            return c->state();
        };
        const SessionState v21 = run(2, 1, 0), v30 = run(3, 0, 0), v32 = run(3, 2, 0), v23 = run(2, 3, 0);
        check("DZC-SES-03", "version gate for jnext: 2.1.0, 3.0.0 and 3.2.0 fail, 2.3.0 is Ready",
              v21 == SessionState::Failed && v30 == SessionState::Failed && v32 == SessionState::Failed &&
                  v23 == SessionState::Ready);
    }
    // SES-04
    {
        bool all = true;
        std::string why;
        for (const char* n : {"CSpect v2.19", "ZEsarUX"}) {
            Script s;
            s.name = n;
            auto c = make_client(s);
            bring_up(*c, s);
            if (c->state() != SessionState::Failed || c->error().find(n) == std::string::npos) {
                all = false;
                why = std::string(n) + ": " + c->error();
            }
        }
        check("DZC-SES-04", "CSpect and ZEsarUX (2.2.0) are refused, and the error names the remote", all, why);
    }
    // SES-05
    {
        Script s;
        s.init_err = 1;
        s.name     = "something";
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-SES-05", "a CMD_INIT error byte 1 with a plain name fails with \"remote refused CMD_INIT (error 1)\"",
              c->state() == SessionState::Failed && c->error() == "remote refused CMD_INIT (error 1)", c->error());
    }
    // SES-06: a real DzrpServer refuses its own process's client; the client recognises it.
    {
        Rig rig;
        ClientConfig cc = cfg();
        cc.session_token = process_session_token();
        auto c = rig_client(rig, cc);
        rig_up(*c, rig);
        check("DZC-SES-06", "a client carrying this process's token against a real DzrpServer fails with \"this is this jnext's own DZRP server\"",
              c->state() == SessionState::Failed && c->error() == "this is this jnext's own DZRP server", c->error());
    }
    // SES-07
    {
        Script s;
        s.raw_init = true;
        s.init_raw = {0, 2, 2, 0};   // 4 bytes: one short
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-SES-07", "a CMD_INIT reply of 4 payload bytes is a protocol error and closes the link",
              c->state() == SessionState::Failed && c->failure() == Status::ProtocolError && link_closed(*c),
              c->error());
    }
    // SES-08
    {
        Script s;
        s.machine = 2;
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-SES-08", "jnext 2.2.0 with machine 2 (not ZXNEXT) is refused",
              c->state() == SessionState::Failed && c->failure() == Status::Refused, c->error());
    }
    // SES-09
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        bool ok = true;
        for (int i = 0; i < 16; ++i) ok = ok && c->request(CMD_GET_REGISTERS).status == Status::Ok;
        // The script's own ids: handshake (1, 24) then 16 requests = 18 commands.
        std::string why;
        bool seqs = s.seqs.size() == 18;
        for (std::size_t i = 0; i < s.seqs.size(); ++i)
            if (s.seqs[i] != static_cast<int>(i % 15) + 1) {
                seqs = false;
                why += " " + std::to_string(s.seqs[i]);
            }
        check("DZC-SES-09", "sequence numbers run 1..15 then 1 again, never 0 and never 16", ok && seqs, why);
    }
    // SES-10
    {
        bool a = false, b = false;
        {   // a wrong seq
            Script s;
            auto c = make_client(s);
            bring_up(*c, s);
            s.custom = [](Script& sc, const Command& cmd) {
                if (cmd.id == CMD_GET_REGISTERS) { sc.reply(9, {}); return true; }
                return false;
            };
            std::uint32_t t1 = 0, t2 = 0;
            c->submit(CMD_GET_REGISTERS, {}, t1);
            c->submit(CMD_GET_REGISTERS, {}, t2);
            spin(*c, s, [&] { return c->done(t1); });
            const Reply r1 = c->take(t1);
            const Reply r2 = c->take(t2);
            a = r1.status == Status::ProtocolError && r2.status == Status::Aborted &&
                c->state() == SessionState::Failed && link_closed(*c);
        }
        {   // 0x13 for seq 3: the whole byte is compared, not its low four bits
            Script s;
            auto c = make_client(s);
            bring_up(*c, s);
            s.custom = [](Script& sc, const Command& cmd) {
                if (cmd.id == CMD_GET_REGISTERS) { sc.reply(0x13, {}); return true; }
                return false;
            };
            std::uint32_t t1 = 0;
            c->submit(CMD_GET_REGISTERS, {}, t1);
            spin(*c, s, [&] { return c->done(t1); });
            b = c->take(t1).status == Status::ProtocolError && c->state() == SessionState::Failed;
        }
        check("DZC-SES-10", "a reply with the wrong seq, and 0x13 for seq 3, is a protocol error: the link closes, the oldest command is ProtocolError and the rest Aborted",
              a && b, std::string(a ? "" : "wrong-seq ") + (b ? "" : "0x13"));
    }
    // SES-11
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        s.reply(5);
        spin(*c, s, [&] { return c->state() != SessionState::Ready; });
        check("DZC-SES-11", "a response with nothing outstanding is a protocol error",
              c->state() == SessionState::Failed && c->failure() == Status::ProtocolError, c->error());
    }
    // SES-12
    {
        AutoMs exact(0);   // the boundaries below are exact: the clock moves only by advance()
        // (a) jnext: both commands go out at once; no reply -> Timeout at exactly the timeout.
        Script s;
        hold(s);
        auto c = make_client(s);
        bring_up(*c, s);
        std::uint32_t t1 = 0, t2 = 0;
        c->submit(CMD_GET_REGISTERS, {}, t1);
        c->submit(CMD_GET_REGISTERS, {}, t2);
        s.service();
        advance(1999);
        c->poll(0);
        const bool waiting = !c->done(t1) && c->state() == SessionState::Ready;
        advance(1);
        c->poll(0);
        const Reply r1 = c->take(t1), r2 = c->take(t2);
        const bool a = waiting && r1.status == Status::Timeout && r2.status == Status::Aborted &&
                       c->state() == SessionState::Failed && link_closed(*c);
        // (b) dezogif_ng, window 1: the second command's deadline starts when it is SENT.
        Script s2;
        s2.name = "dezogif v2.2.1";
        s2.ver[1] = 1;
        hold(s2);
        ClientConfig k = cfg();
        k.response_timeout = milliseconds(1000);
        auto d = make_client(s2, LinkKind::Tcp, k);
        bring_up(*d, s2);
        std::uint32_t ta = 0, tb = 0;
        d->submit(CMD_GET_REGISTERS, {}, ta);
        d->submit(CMD_GET_REGISTERS, {}, tb);
        s2.service();
        advance(900);
        s2.release(0);
        d->poll(0);            // A done at +900; B is sent now
        s2.service();
        advance(999);
        d->poll(0);            // B is 999 ms old: from submit it would be 1899
        const bool b_waiting = d->take(ta).status == Status::Ok && !d->done(tb) && d->state() == SessionState::Ready;
        advance(1);
        d->poll(0);
        const bool b = b_waiting && d->take(tb).status == Status::Timeout;
        check("DZC-SES-12", "no reply: still waiting at timeout-1 ms, Timeout at the timeout (state Failed, link closed, the next command Aborted), timed from SEND not from submit",
              a && b, std::string(a ? "" : "jnext ") + (b ? "" : "window1"));
    }
    // SES-13
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        const Reply cont = c->request(CMD_CONTINUE, {0, 0, 0, 0, 0});
        const bool was_running = c->running();
        hold(s);
        std::uint32_t t = 0;
        c->submit(CMD_GET_REGISTERS, {}, t);
        s.service();
        s.notify({NTF_PAUSE, BREAK_BP, 0x00, 0x80, 0x0B, 'x', 0});
        s.release(0);
        spin(*c, s, [&] { return c->done(t); });
        const Reply r = c->take(t);
        const auto  p = c->take_pause();
        check("DZC-SES-13", "an NTF_PAUSE between a command and its reply: the reply completes Ok; take_pause gives reason 2, addr 0x8000, bank 0x0B, text \"x\"; running() false",
              cont.status == Status::Ok && was_running && r.status == Status::Ok && p && p->reason == 2 &&
                  p->addr == 0x8000 && p->bank1 == 0x0B && p->text == "x" && !c->running());
    }
    // SES-14
    {
        bool short_ntf = false, no_nul = false, unknown = false;
        {
            Script s;
            auto c = make_client(s);
            bring_up(*c, s);
            s.notify({NTF_PAUSE, 2, 0, 0x80});
            spin(*c, s, [&] { return c->state() != SessionState::Ready; });
            short_ntf = c->state() == SessionState::Failed && c->failure() == Status::ProtocolError;
        }
        {
            Script s;
            auto c = make_client(s);
            bring_up(*c, s);
            s.notify({NTF_PAUSE, 2, 0x00, 0x80, 0x0B, 'a', 'b'});
            spin(*c, s, [&] { return false; }, 3);
            const auto p = c->take_pause();
            no_nul = p && p->text == "ab" && c->state() == SessionState::Ready;
        }
        {
            Script s;
            auto c = make_client(s);
            bring_up(*c, s);
            LogTap log;
            s.notify({9, 1, 2});
            c->poll(0);
            const Reply r = c->request(CMD_GET_REGISTERS);
            unknown = log.count("ignoring notification 9") == 1 && r.status == Status::Ok &&
                      c->state() == SessionState::Ready;
        }
        check("DZC-SES-14", "NTF_PAUSE of 4 payload bytes is a protocol error; without its NUL the text runs to the end; an unknown notification id is warned and ignored and the next reply still matches",
              short_ntf && no_nul && unknown,
              std::string(short_ntf ? "" : "short ") + (no_nul ? "" : "nonul ") + (unknown ? "" : "unknown"));
    }
    // SES-15
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        c->close();
        s.service();
        const bool closed = c->state() == SessionState::Closed && link_closed(*c);
        const bool close_cmd = s.ids.size() == 3 && s.ids[2] == CMD_CLOSE && s.cmds[2].payload.empty();
        Script s2;
        {
            auto d = make_client(s2);
            bring_up(*d, s2);
        }   // destroyed while Ready
        s2.service();
        check("DZC-SES-15", "close() on Ready sends CMD_CLOSE (id 2, no payload) then closes the link; the destructor on Ready writes CMD_CLOSE too",
              closed && close_cmd && s2.ids.size() == 3 && s2.ids[2] == CMD_CLOSE);
    }
    // SES-16
    {
        Script s;
        hold(s);
        auto c = make_client(s);
        bring_up(*c, s);
        for (int i = 0; i < 20; ++i) { std::uint32_t t; c->submit(CMD_GET_REGISTERS, {}, t); }
        s.service();
        const std::size_t first = s.held.size();
        s.release(0);
        c->poll(0);
        s.service();
        const std::size_t after = s.held.size();
        Script d;
        d.name = "dezogif v2.2.1";
        d.ver[1] = 1;
        hold(d);
        auto dc = make_client(d);
        bring_up(*dc, d);
        for (int i = 0; i < 20; ++i) { std::uint32_t t; dc->submit(CMD_GET_REGISTERS, {}, t); }
        d.service();
        check("DZC-SES-16", "pipelining: a jnext profile puts exactly 15 frames on the wire before any reply and each reply releases one more; a dezogif_ng profile puts 1",
              first == 15 && after == 16 && d.held.size() == 1,
              std::to_string(first) + "/" + std::to_string(after) + "/" + std::to_string(d.held.size()));
    }
    // SES-17
    {
        auto win = [](int w, bool dezogif) {
            Script s;
            if (dezogif) { s.name = "dezogif v2.2.1"; s.ver[1] = 1; }
            ClientConfig k = cfg();
            k.window = w;
            auto c = make_client(s, LinkKind::Tcp, k);
            bring_up(*c, s);
            return c->window();
        };
        check("DZC-SES-17", "window 0 takes the profile default (jnext 15, dezogif 1); 99 clamps to 15; -3 clamps to 1",
              win(0, false) == 15 && win(0, true) == 1 && win(99, true) == 15 && win(-3, false) == 1);
    }
    // SES-18
    {
        Script s;
        hold(s);
        auto c = make_client(s);
        bring_up(*c, s);
        std::uint32_t t = 0;
        c->submit(CMD_GET_REGISTERS, {}, t);
        s.service();
        s.close();
        spin(*c, s, [&] { return c->done(t); });
        check("DZC-SES-18", "the peer closing the link with a command outstanding: the command is LinkLost and the session Failed \"the remote closed the connection\"",
              c->take(t).status == Status::LinkLost && c->state() == SessionState::Failed &&
                  c->error() == "the remote closed the connection", c->error());
    }
    // SES-19
    {
        auto run = [](std::size_t n) {
            Script s;
            s.bitfield = Bytes(n, 0xFF);
            auto c = make_client(s);
            bring_up(*c, s);
            return std::make_pair(c->state(), c->failure());
        };
        const auto n0 = run(0), n33 = run(33), n32 = run(32);
        check("DZC-SES-19", "a CMD_GET_SUPPORTED_COMMANDS reply of 0 or 33 bytes is a protocol error; 32 is accepted",
              n0.first == SessionState::Failed && n0.second == Status::ProtocolError &&
                  n33.first == SessionState::Failed && n33.second == Status::ProtocolError &&
                  n32.first == SessionState::Ready);
    }
    // SES-20
    {
        Script s;
        s.bitfield = bitfield_of({1, 3});   // no CMD_CLOSE
        auto c = make_client(s);
        bring_up(*c, s);
        c->close();
        s.service();
        check("DZC-SES-20", "a bitfield without CMD_CLOSE: close() sends nothing after CMD_GET_SUPPORTED_COMMANDS and closes the link",
              s.ids.size() == 2 && c->state() == SessionState::Closed && link_closed(*c));
    }
}

// SES-21 (a row the plan did not list): detach_link, the one door past the gate, is closed while a
// command is in flight or queued, and otherwise hands over the LIVE link without a CMD_CLOSE.
static void detach_row() {
    Script s;
    hold(s);
    auto c = make_client(s);
    bring_up(*c, s);
    std::uint32_t t = 0;
    c->submit(CMD_GET_REGISTERS, {}, t);
    s.service();
    const bool refused = c->detach_link() == nullptr && c->state() == SessionState::Ready;
    s.release(0);
    spin(*c, s, [&] { return c->done(t); });
    c->take(t);
    auto link = c->detach_link();
    s.service();
    const std::size_t before = s.ids.size();
    if (link) {
        link->write(Bytes{0, 0, 0, 0, 1, 3}.data(), 6);   // a raw CMD_GET_REGISTERS, seq 1
        s.service();
    }
    check("DZC-SES-21", "detach_link refuses while a command is in flight; once idle it hands over the live link (state Closed, no CMD_CLOSE sent) which still carries bytes",
          refused && link && c->state() == SessionState::Closed && link->state() == LinkState::Open &&
              std::count(s.ids.begin(), s.ids.end(), static_cast<int>(CMD_CLOSE)) == 0 && s.ids.size() == before + 1);
}


// SES-22..25: teardown with a command queued behind the window, and the bit-0 rule.
static void as_ng(Script& s) {
    s.name = "dezogif v2.2.1";
    s.ver[1] = 1;
}

static void teardown_rows() {
    {   // timeout: the command behind the window ends Aborted and a wait on it returns
        AutoMs exact(0);
        Script s;
        as_ng(s);
        hold(s);
        auto c = make_client(s);
        bring_up(*c, s);
        std::uint32_t ta = 0, tb = 0;
        c->submit(CMD_GET_REGISTERS, {}, ta);
        c->submit(CMD_GET_REGISTERS, {}, tb);   // window 1: queued, never sent
        s.service();
        advance(2000);
        c->poll(0);
        const bool both = c->done(ta) && c->done(tb);
        const Status sa = c->take(ta).status, sb = c->take(tb).status;
        check("DZC-SES-22", "a timeout with a command queued behind the window: the oldest is Timeout, the queued one is resolved at once as Aborted, and nothing was sent for it",
              both && sa == Status::Timeout && sb == Status::Aborted && s.held.size() == 1,
              std::to_string(both) + " " + status_name(sa) + " " + status_name(sb));
    }
    {   // link lost
        Script s;
        as_ng(s);
        hold(s);
        auto c = make_client(s);
        bring_up(*c, s);
        std::uint32_t ta = 0, tb = 0;
        c->submit(CMD_GET_REGISTERS, {}, ta);
        c->submit(CMD_GET_REGISTERS, {}, tb);
        s.service();
        s.close();
        spin(*c, s, [&] { return c->done(tb); });
        const bool both = c->done(ta) && c->done(tb);
        const Status sa = c->take(ta).status, sb = c->take(tb).status;
        check("DZC-SES-23", "the link lost with a command queued behind the window: both the in-flight and the queued command end LinkLost",
              both && sa == Status::LinkLost && sb == Status::LinkLost, std::string(status_name(sa)) + " " + status_name(sb));
    }
    {   // protocol error
        Script s;
        as_ng(s);
        s.custom = [](Script& sc, const Command& cmd) {
            if (cmd.id == CMD_GET_REGISTERS) { sc.reply(9, {}); return true; }
            return false;
        };
        auto c = make_client(s);
        bring_up(*c, s);
        std::uint32_t ta = 0, tb = 0;
        c->submit(CMD_GET_REGISTERS, {}, ta);
        c->submit(CMD_GET_REGISTERS, {}, tb);
        spin(*c, s, [&] { return c->done(tb); });
        const bool both = c->done(ta) && c->done(tb);
        const Status sa = c->take(ta).status, sb = c->take(tb).status;
        check("DZC-SES-24", "a protocol error with a command queued behind the window: the oldest is ProtocolError and the queued one Aborted, both resolved",
              both && sa == Status::ProtocolError && sb == Status::Aborted && c->state() == SessionState::Failed,
              std::string(status_name(sa)) + " " + status_name(sb));
    }
    {   // bit 0 of the bitfield is not a command
        Script s;
        s.bitfield = {0x0F};   // bits 0..3
        auto c = make_client(s);
        const bool up = bring_up(*c, s);
        check("DZC-SES-25", "a bitfield with bit 0 set: command 0 is not in the set and the set counts only commands 1..3",
              up && !c->in_set(0) && c->in_set(1) && c->in_set(3) && c->command_set().count() == 3,
              std::to_string(c->command_set().count()));
    }
}

// ==============================================================================================
// DZC-ID - the identity rule
// ==============================================================================================

static std::vector<int> ids_of(const std::bitset<256>& b) {
    std::vector<int> v;
    for (int i = 0; i < 256; ++i)
        if (b.test(static_cast<std::size_t>(i))) v.push_back(i);
    return v;
}

static std::vector<int> range_ids(std::initializer_list<std::pair<int, int>> rs) {
    std::vector<int> v;
    for (auto r : rs)
        for (int i = r.first; i <= r.second; ++i) v.push_back(i);
    return v;
}

static void identity_rows() {
    {
        Script s;
        auto c = make_client(s, LinkKind::Tcp);
        bring_up(*c, s);
        check("DZC-ID-01", "\"jnext v...\" 2.2.0 over a TCP-kind link: Jnext, Normal breakpoints, window 15",
              c->profile() == Profile::Jnext && c->breakpoint_mode() == BreakpointMode::Normal && c->window() == 15);
    }
    {
        Script s;
        auto c = make_client(s, LinkKind::Serial);
        bring_up(*c, s);
        check("DZC-ID-02", "\"jnext v...\" 2.2.0 over a serial-kind link: Jnext too (any link)",
              c->profile() == Profile::Jnext && c->state() == SessionState::Ready);
    }
    {
        Script s;
        s.name = "dezogif v2.2.1";
        s.ver[1] = 1;
        auto c = make_client(s, LinkKind::Tcp);
        bring_up(*c, s);
        std::vector<int> want = range_ids({{1, 23}, {40, 41}});
        check("DZC-ID-03", "\"dezogif v2.2.1\" 2.1.0 over TCP: DezogifNg, Simple breakpoints, window 1, no CMD 24 sent, set {1..23, 40, 41}",
              c->profile() == Profile::DezogifNg && c->breakpoint_mode() == BreakpointMode::Simple &&
                  c->window() == 1 && s.ids == std::vector<int>({CMD_INIT}) && ids_of(c->command_set()) == want);
    }
    {
        Script s;
        s.name = "dezogif v2.2.1";
        s.ver[1] = 1;
        auto c = make_client(s, LinkKind::Serial);
        bring_up(*c, s);
        check("DZC-ID-10", "\"dezogif v...\" 2.1.0 over a serial-kind link is refused, naming the WiFi dezogif_ng stub, no CMD 24",
              c->state() == SessionState::Failed && c->failure() == Status::Refused &&
                  s.ids == std::vector<int>({CMD_INIT}) && c->error().find("WiFi dezogif_ng") != std::string::npos,
              c->error());
    }
    {
        Script s;
        s.name = "dezogif v2.3.0-rc2";
        s.bitfield = bitfield_of({1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 20, 21, 22, 23, 24, 40, 41});
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-ID-11", "\"dezogif v2.3.0-rc2\" 2.2.0 over TCP is refused, naming the WiFi dezogif_ng stub, no CMD 24 sent",
              c->state() == SessionState::Failed && c->failure() == Status::Refused &&
                  s.ids == std::vector<int>({CMD_INIT}) && c->error().find("WiFi dezogif_ng") != std::string::npos,
              c->error());
    }
    {
        Script s;
        s.name = "dezogif v2.3.0-rc2";
        auto c = make_client(s, LinkKind::Serial);
        bring_up(*c, s);
        check("DZC-ID-12", "\"dezogif v...\" 2.2.0 over a serial-kind link is refused too",
              c->state() == SessionState::Failed && c->failure() == Status::Refused);
    }
    {
        Script s;
        s.name = "dezogif v2.0.0";
        s.ver[1] = 0;
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-ID-06", "\"dezogif v2.0.0\" 2.0.0 is refused", c->state() == SessionState::Failed && c->failure() == Status::Refused);
    }
    {
        bool all = true;
        std::string why;
        for (const char* n : {"jnext", "jnextv1", "JNEXT v1.4.1", "Dezogif v2.2.1", "dezogifng v1", ""}) {
            Script s;
            s.name = n;
            auto c = make_client(s);
            bring_up(*c, s);
            if (c->state() != SessionState::Failed) {
                all = false;
                why += std::string("[") + n + "]";
            }
        }
        check("DZC-ID-07", "lookalike names (\"jnext\", \"jnextv1\", \"JNEXT v1.4.1\", \"Dezogif v2.2.1\", \"dezogifng v1\", empty) are refused", all, why);
    }
    {
        Script s;
        LogTap log;
        auto   c = make_client(s);
        bring_up(*c, s);
        const std::string line = log.first("connected to");
        check("DZC-ID-08", "the connect log line names the remote, the DZRP version, the link, the profile and the window",
              line.find("\"jnext v") != std::string::npos && line.find("DZRP 2.2.0") != std::string::npos &&
                  line.find("mem:mem") != std::string::npos && line.find("profile jnext") != std::string::npos &&
                  line.find("window 15") != std::string::npos,
              line);
    }
}

// ==============================================================================================
// DZC-GATE - the send gate
// ==============================================================================================

static void as_dezogif_ng(Script& s) {
    s.name = "dezogif v2.2.1";
    s.ver[1] = 1;
}

/// Refusals: nothing reaches the peer and one error line per refusal.
static bool refused_all(DzrpClient& c, Script& s, std::initializer_list<int> ids) {
    s.service();
    const std::size_t before = s.ids.size();
    LogTap log;
    bool ok = true;
    for (int id : ids) {
        std::uint32_t t = 0;
        if (c.submit(static_cast<std::uint8_t>(id), {}, t) != Status::Refused) ok = false;
    }
    s.service();
    return ok && s.ids.size() == before && log.count("refused to send") == static_cast<int>(ids.size());
}

static void gate_rows() {
    {
        Script s;
        as_dezogif_ng(s);
        auto c = make_client(s);
        bring_up(*c, s);
        check("DZC-GATE-01", "DezogifNg: CMD 24, 25 and 42 are Refused, nothing sent, one error line each",
              refused_all(*c, s, {24, 25, 42}));
    }
    {
        Script s;
        as_dezogif_ng(s);
        auto c = make_client(s);
        bring_up(*c, s);
        const Reply cont = c->request(CMD_CONTINUE, {0, 0, 0, 0, 0});
        const bool refused = refused_all(*c, s, {3, 8, 11});
        std::uint32_t t = 0;
        const Status pause = c->submit(CMD_PAUSE, {}, t);
        check("DZC-GATE-03", "DezogifNg after CMD_CONTINUE (running): CMD 3, 8 and 11 are Refused; CMD_PAUSE is sent",
              cont.status == Status::Ok && c->running() && refused && pause == Status::Ok);
    }
    {
        Script s;
        as_dezogif_ng(s);
        auto c = make_client(s);
        bring_up(*c, s);
        c->request(CMD_CONTINUE, {0, 0, 0, 0, 0});
        s.notify({NTF_PAUSE, BREAK_MANUAL, 0, 0, 0, 0});
        spin(*c, s, [&] { return !c->running(); });
        std::uint32_t t = 0;
        const Status st = c->submit(CMD_GET_REGISTERS, {}, t);
        check("DZC-GATE-05", "after an NTF_PAUSE, running() is false and CMD 3 is sent", !c->running() && st == Status::Ok);
    }
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        c->request(CMD_CONTINUE, {0, 0, 0, 0, 0});
        std::uint32_t t = 0;
        const Status st = c->submit(CMD_GET_REGISTERS, {}, t);
        check("DZC-GATE-06", "the jnext profile has no running rule: CMD 3 is sent while running", c->running() && st == Status::Ok);
    }
    {
        Rig rig;
        auto c = rig_client(rig);
        rig_up(*c, rig);
        std::uint32_t t = 0;
        LogTap log;
        const Status st = c->submit(13, {}, t);
        check("DZC-GATE-07", "jnext against a real server: CMD 13 (absent from its bitfield) is Refused with one error line",
              c->state() == SessionState::Ready && st == Status::Refused && log.count("refused to send") == 1);
    }
    {
        Script s;
        auto c = make_client(s);
        c->start();
        c->step();   // Initialising, CMD_INIT sent
        LogTap log;
        const Reply r = c->request(CMD_GET_REGISTERS);
        s.service();
        check("DZC-GATE-08", "before Ready: request(3) is NotReady and only CMD_INIT reached the peer",
              c->state() == SessionState::Initialising && r.status == Status::NotReady &&
                  s.ids == std::vector<int>({CMD_INIT}) && log.count("refused to send") == 1);
    }
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        s.service();
        const std::size_t before = s.ids.size();
        LogTap log;
        std::uint32_t t = 0;
        const Status st = c->submit(CMD_WRITE_MEM, Bytes(static_cast<std::size_t>(MAX_PAYLOAD_BYTES) + 1, 0), t);
        s.service();
        check("DZC-GATE-09", "a payload over the 16 MiB cap is Refused with one error line, nothing is sent and the session stays Ready",
              st == Status::Refused && s.ids.size() == before && c->state() == SessionState::Ready &&
                  log.count("refused to send") == 1);
    }
}

// ==============================================================================================
// DZC-WAIT - the wait hook
// ==============================================================================================

static void wait_rows() {
    {
        Rig rig;
        auto c = rig_client(rig);
        int hooks = 0;
        c->set_wait_hook([&] { ++hooks; rig.pump(); });
        rig_up(*c, rig);
        hooks = 0;
        const Reply r = c->request(CMD_GET_REGISTERS);
        check("DZC-WAIT-01", "hook = Debugger::pump: request(CMD_GET_REGISTERS) completes Ok, its PC is the rig's 0x8000 and the hook ran",
              r.status == Status::Ok && r.payload.size() >= 2 && r.payload[0] == 0x00 && r.payload[1] == 0x80 && hooks >= 1);
    }
    {
        Rig rig;
        auto c = rig_client(rig);
        rig_up(*c, rig);
        c->set_wait_hook(nullptr);
        AutoMs fast(250);
        const Reply r = c->request(CMD_GET_REGISTERS);
        check("DZC-WAIT-02", "no hook and nothing pumping the server: the same request ends in Timeout (an auto-advancing clock, no real wait)",
              r.status == Status::Timeout);
    }
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        int inner_calls = 0;
        Reply inner;
        c->set_wait_hook([&] {
            s.service();
            if (inner_calls++ == 0) inner = c->request(CMD_GET_REGISTERS);
        });
        LogTap log;
        const Reply outer = c->request(CMD_GET_REGISTERS);
        s.service();
        const auto regs = std::count(s.ids.begin(), s.ids.end(), static_cast<int>(CMD_GET_REGISTERS));
        check("DZC-WAIT-03", "a request made from inside the hook is Reentrant and writes nothing; the outer request completes Ok",
              inner.status == Status::Reentrant && outer.status == Status::Ok && regs == 1 &&
                  log.count("from inside the wait hook") == 1,
              "regs=" + std::to_string(regs));
    }
}

// ==============================================================================================
// DZC-LOG - NTF_LOG
// ==============================================================================================

static std::string fmt_log(const Bytes& p) { return format_ntf_log(p.data(), p.size()); }

static void log_rows() {
    {
        Script s;
        auto c = make_client(s);
        bring_up(*c, s);
        LogTap log;
        s.notify(cat(cat(Bytes{NTF_LOG}, str("Value of A=$u1 and BC=$h2")), Bytes{0, 0x12, 0xAB, 0x71}));
        c->poll(0);
        check("DZC-LOG-01", "the spec's example: \"Value of A=$u1 and BC=$h2\" + 12 AB 71 logs \"Value of A=18 and BC=71AB\" once at info",
              log.count("remote log: Value of A=18 and BC=71AB") == 1 &&
                  fmt_log(cat(cat(str("Value of A=$u1 and BC=$h2"), Bytes{0}), Bytes{0x12, 0xAB, 0x71})) ==
                      "Value of A=18 and BC=71AB");
    }
    {
        check("DZC-LOG-02", "$s1 0xFF is -1, $s2 FE FF is -2, $h1 0x0A is 0A",
              fmt_log(cat(str("$s1"), Bytes{0, 0xFF})) == "-1" && fmt_log(cat(str("$s2"), Bytes{0, 0xFE, 0xFF})) == "-2" &&
                  fmt_log(cat(str("$h1"), Bytes{0, 0x0A})) == "0A");
    }
    {
        Bytes no_nul = str("abc$u1");   // no NUL: the whole payload is the format string, and there is no data
        Bytes big(4000, 'x');
        big.push_back(0);
        const bool short_data = fmt_log(cat(str("a$u2b"), Bytes{0, 0x01})) == "a<?>b";
        const bool dollar_end = fmt_log(cat(str("x$"), Bytes{0})) == "x$";
        const bool bad_seq = fmt_log(cat(str("$x1"), Bytes{0, 5})) == "$x1";
        const bool nonul = fmt_log(no_nul) == "abc<?>";
        const bool huge = fmt_log(big).size() <= 1024;
        const bool empty = fmt_log(Bytes{}).empty();
        check("DZC-LOG-03", "malformed NTF_LOG: short data prints <?>, a trailing $ and an unknown sequence print literally, a missing NUL and an empty payload read nothing past the end, a 4000-byte string is cut to 1024",
              short_data && dollar_end && bad_seq && nonul && huge && empty,
              std::string(short_data ? "" : "short ") + (dollar_end ? "" : "dollar ") + (bad_seq ? "" : "seq ") +
                  (nonul ? "" : "nonul ") + (huge ? "" : "huge ") + (empty ? "" : "empty"));
    }
}

// ==============================================================================================
// DZC-E2E - a session against a real DzrpServer
// ==============================================================================================

static void e2e_rows() {
    {
        Rig rig;
        LogTap log;
        auto c = rig_client(rig);
        const bool up = rig_up(*c, rig);
        std::vector<int> want = range_ids({{1, 4}, {6, 11}, {15, 21}, {23, 28}, {39, 43}, {50, 51}});
        const bool set_ok = ids_of(c->command_set()) == want;
        c->close();
        for (int i = 0; i < 4; ++i) rig.pump();
        check("DZC-E2E-01", "a real DzrpServer over the memory link: Ready as profile Jnext, the set is the server's table (no 5, 12, 13, 14, 22), close() ends the session with exactly one ATTACH client and one DETACH client",
              up && c->profile() == Profile::Jnext && set_ok && log.count("ATTACH client") == 1 &&
                  log.count("DETACH client") == 1 && c->state() == SessionState::Closed);
    }
    {
        Rig rig;
        for (int i = 0; i < 15; ++i)
            for (int k = 0; k < 16; ++k)
                rig.emu.mmu().write(static_cast<std::uint16_t>(0x9000 + i * 0x40 + k), static_cast<std::uint8_t>(i * 16 + k + 1));
        auto c = rig_client(rig);
        rig_up(*c, rig);
        std::vector<std::pair<std::uint8_t, Bytes>> burst;
        for (int i = 0; i < 15; ++i) {
            const std::uint16_t a = static_cast<std::uint16_t>(0x9000 + i * 0x40);
            burst.push_back({CMD_READ_MEM, Bytes{0, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(a >> 8), 16, 0}});
        }
        const std::vector<Reply> rs = c->request_all(burst);
        bool all = rs.size() == 15;
        for (int i = 0; i < 15 && all; ++i) {
            Bytes want(16);
            for (int k = 0; k < 16; ++k) want[k] = rig.emu.mmu().peek(static_cast<std::uint16_t>(0x9000 + i * 0x40 + k));
            all = rs[i].status == Status::Ok && rs[i].payload == want;
        }
        check("DZC-E2E-02", "request_all of 15 CMD_READ_MEM (16 bytes each, distinct addresses): 15 Ok replies in order, each equal to the machine's bytes", all);
    }
}

// ==============================================================================================
// DZC-PRB - the probe over a memory link
// ==============================================================================================

/// A link that records the command ids written through it (to count what a server was sent).
struct TapLink final : jnext::remote::ClientLink {
    std::unique_ptr<jnext::remote::ClientLink> inner;
    std::vector<int>*                          ids;
    FrameParser                                fp;
    TapLink(std::unique_ptr<jnext::remote::ClientLink> l, std::vector<int>* out) : inner(std::move(l)), ids(out) {}
    bool open() override { return inner->open(); }
    void poll(int w) override { inner->poll(w); }
    LinkState state() const override { return inner->state(); }
    std::size_t read(std::uint8_t* b, std::size_t c) override { return inner->read(b, c); }
    bool write(const std::uint8_t* p, std::size_t n) override {
        std::size_t off = 0;
        while (off < n) {
            const std::size_t k = fp.feed(p + off, n - off, g_now);
            off += k;
            if (fp.ready()) ids->push_back(fp.take().id);
            else if (k == 0) break;
        }
        return inner->write(p, n);
    }
    void close() override { inner->close(); }
    LinkKind kind() const override { return inner->kind(); }
    std::string describe() const override { return inner->describe(); }
    const std::string& last_error() const override { return inner->last_error(); }
};

struct ProbeRun {
    std::string out;
    int         rc = -1;
    int         links = 0;
};

static ProbeRun run_probe_args(std::vector<std::string> args, ProbeEnv env) {
    ProbeRun r;
    auto     inner = env.make_link;
    env.make_link = [&](const LinkSpec& sp) {
        ++r.links;
        return inner(sp);
    };
    env.base.session_token = TOKEN;
    env.base.clock         = fake_now;
    std::vector<std::string> store = {"dzrp_client_probe"};
    store.insert(store.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : store) argv.push_back(&a[0]);
    r.out = run_probe_captured(static_cast<int>(argv.size()), argv.data(), env, r.rc);
    return r;
}

static int lines_starting(const std::string& text, const std::string& prefix) {
    int n = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t e = text.find('\n', at);
        if (e == std::string::npos) e = text.size();
        if (text.compare(at, prefix.size(), prefix) == 0) ++n;
        at = e + 1;
    }
    return n;
}

static bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

static ProbeEnv rig_env(Rig& rig, std::vector<int>* tap = nullptr) {
    ProbeEnv env;
    env.wait_hook = [&rig] { rig.pump(); advance(10); };
    env.make_link = [&rig, tap](const LinkSpec&) {
        std::unique_ptr<jnext::remote::ClientLink> l(new MemoryLink(*rig.lsn));
        if (tap) l.reset(new TapLink(std::move(l), tap));
        return l;
    };
    return env;
}

static ProbeEnv script_env(Script& s) {
    ProbeEnv env;
    env.wait_hook = [&s] { s.service(); advance(10); };
    env.make_link = [&s](const LinkSpec&) {
        return std::unique_ptr<jnext::remote::ClientLink>(new MemoryLink(s.lsn));
    };
    return env;
}

static void probe_rows() {
    {
        Rig rig;
        const ProbeRun r = run_probe_args({"tcp:1.2.3.4:5"}, rig_env(rig));
        char want[32];
        std::snprintf(want, sizeof(want), "NR 0x00 = 0x%02X", rig.dbg->nextreg_peek(0));
        check("DZC-PRB-01", "the default probe sequence against a real DzrpServer: exit 0, profile jnext, PC=8000, 10 CMD_READ_MEM and 10 CMD_GET_TBBLUE_REG lines (NR 0x00 = the machine's value), CMD_CLOSE and \"0 failed\"",
              r.rc == 0 && has(r.out, "profile jnext") && has(r.out, "PC=8000") &&
                  lines_starting(r.out, "CMD_READ_MEM") == 10 && lines_starting(r.out, "CMD_GET_TBBLUE_REG") == 10 &&
                  has(r.out, want) && lines_starting(r.out, "CMD_CLOSE") == 1 && has(r.out, "summary:") &&
                  has(r.out, " 0 failed"),
              "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
    {
        Rig rig;
        const ProbeRun a = run_probe_args({}, rig_env(rig));
        const ProbeRun b = run_probe_args({"tcp:1.2.3.4"}, rig_env(rig));
        const ProbeRun c = run_probe_args({"--bogus", "tcp:1.2.3.4:5"}, rig_env(rig));
        const ProbeRun d = run_probe_args({"--timeout-ms", "x", "tcp:1.2.3.4:5"}, rig_env(rig));
        const ProbeRun e = run_probe_args({"--send-unsupported", "3", "tcp:1.2.3.4:5"}, rig_env(rig));
        check("DZC-PRB-02", "usage errors exit 2 and connect to nothing (no address, a bad address, an unknown option, a bad number); --send-unsupported with an id inside the set exits 2",
              a.rc == 2 && a.links == 0 && b.rc == 2 && b.links == 0 && c.rc == 2 && c.links == 0 &&
                  d.rc == 2 && d.links == 0 && e.rc == 2,
              std::to_string(a.rc) + std::to_string(b.rc) + std::to_string(c.rc) + std::to_string(d.rc) + std::to_string(e.rc));
    }
    {
        Rig rig;
        std::vector<int> sent;
        const ProbeRun r = run_probe_args({"--send-unsupported", "13", "--reconnect-delay-ms", "0", "tcp:1.2.3.4:5"},
                                          rig_env(rig, &sent));
        check("DZC-PRB-03", "--send-unsupported 13 against jnext: the seq-only reply is reported, then the reconnect and CMD_INIT succeed, exit 0, and the server was sent exactly one id-13 frame",
              r.rc == 0 && has(r.out, "(seq only)") && has(r.out, "reconnect + CMD_INIT") &&
                  std::count(sent.begin(), sent.end(), 13) == 1 && r.links == 2,
              "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
    {
        Script s;
        as_dezogif_ng(s);
        s.custom = [](Script&, const Command& c) { return c.id >= 24 && c.id != 40 && c.id != 41; };   // silent
        const ProbeRun r = run_probe_args({"--send-unsupported", "24", "--timeout-ms", "100", "--reconnect-delay-ms", "0",
                                           "tcp:1.2.3.4:5"},
                                          script_env(s));
        int outside = 0;
        s.count_ids_outside(range_ids({{1, 23}, {40, 41}}), outside);
        check("DZC-PRB-04", "a scripted dezogif_ng that never answers CMD 24: the probe reports \"no reply within\", reconnects and sends CMD_INIT again, and across both connections the peer received exactly one command outside its set (the 24); exit 0",
              r.rc == 0 && has(r.out, "no reply within") && has(r.out, "reconnect + CMD_INIT") && outside == 1 &&
                  std::count(s.ids.begin(), s.ids.end(), static_cast<int>(CMD_INIT)) == 2,
              "rc=" + std::to_string(r.rc) + " outside=" + std::to_string(outside) + "\n" + r.out);
    }
    {
        Script s;
        s.custom = [](Script&, const Command& c) { return c.id == CMD_READ_MEM; };   // never answered
        const ProbeRun r = run_probe_args({"--timeout-ms", "100", "tcp:1.2.3.4:5"}, script_env(s));
        std::string line;
        std::size_t at = r.out.find("CMD_READ_MEM");
        if (at != std::string::npos) line = r.out.substr(at, r.out.find('\n', at) - at);
        check("DZC-PRB-05", "a scripted peer that never answers CMD_READ_MEM: that line says timeout and the probe exits 1",
              r.rc == 1 && has(line, "timeout"), "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
    {
        Script s;
        s.custom = [](Script& sc, const Command& c) {
            if (c.id != CMD_GET_REGISTERS) return false;
            Bytes p(29, 0);
            p[28] = 8;   // eight slots declared, none carried
            sc.reply(c.seq, p);
            return true;
        };
        const ProbeRun r = run_probe_args({"--timeout-ms", "100", "tcp:1.2.3.4:5"}, script_env(s));
        check("DZC-PRB-06", "a registers reply of 29 bytes that declares 8 slots is reported MALFORMED with its hex, the probe says it stopped at the malformed reply and exits 1",
              r.rc == 1 && has(r.out, "MALFORMED") && has(r.out, "malformed (29 bytes)") &&
                  has(r.out, "stopped at the malformed reply") && has(r.out, " 1 failed"),
              "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
    {
        Script s;
        s.name = "CSpect v2.19";
        const ProbeRun r = run_probe_args({"--timeout-ms", "100", "tcp:1.2.3.4:5"}, script_env(s));
        check("DZC-PRB-07", "a refused handshake counts as a failed command: exit 1 and a summary of \"1 commands, 1 failed\"",
              r.rc == 1 && has(r.out, "summary: 1 commands, 1 failed"), "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
    {
        Script s;
        s.bitfield = bitfield_of({1, 2, 3, 8, 24});   // no CMD_GET_TBBLUE_REG: the gate refuses it
        const ProbeRun r = run_probe_args({"--timeout-ms", "100", "tcp:1.2.3.4:5"}, script_env(s));
        check("DZC-PRB-08", "a step the gate refuses stops the probe with \"stopped at the first failed step\" (not the malformed-reply wording) and exit 1",
              r.rc == 1 && has(r.out, "stopped at the first failed step") && !has(r.out, "malformed reply") &&
                  has(r.out, "refused"), "rc=" + std::to_string(r.rc) + "\n" + r.out);
    }
}

int main() {
    std::printf("dzrp_client_test - the DZRP client over memory links (GH #511)\n");
    framing_rows();
    session_rows();
    detach_row();
    teardown_rows();
    identity_rows();
    gate_rows();
    wait_rows();
    log_rows();
    e2e_rows();
    probe_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
