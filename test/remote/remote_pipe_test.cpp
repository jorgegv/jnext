// jnext::remote::PipeChannel / DebugServers channel servers (GH #545).
//
// WHAT IS UNDER TEST. The page channel: `remote::PipeChannel` (src/remote/
// pipe_transport.*), the second `esp::EspListener` the production
// `remote::Server` runs over, fed by the HOST instead of a kernel; and the
// `DebugServers` wiring that serves DZRP, ZRCP and GDB over it
// (`EmulatorConfig::{dzrp,zrcp,gdb}_channel`, `DebugServers::channel()`).
//
//   XPT-PIPE-*  the channel's contract, through the real `Server` and a probe
//               `Protocol`: admission, ordering, close, redial, end report,
//               refusal and the inbound cap.
//   DBGS-CH-*   DebugServers over a real Emulator + Debugger: the protocol
//               bytes the independent native peers check (dzrp-peer.py
//               `sc_init_regs`, zrcp-peer.py's welcome from the ZEsarUX 12.0
//               fixture, a GDB `?`).
//   DBGS-BUD-01 the paused pump budget: {0,2,10} natively, {0,0,10} in a page.
//
// ORACLE. The `EspListener` / `EspTransport` contract (esp_socket.h) and the
// `Server` contract (transport.h) for the XPT rows; the bytes the native peers
// and the recorded ZEsarUX 12.0 welcome for the DBGS rows. No row sleeps: every
// pump loop has an iteration cap.
//
// Run: ./build/test/remote_pipe_test

#include "remote/pipe_transport.h"
#include "remote/transport.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "platform/debug_servers.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::dbg::ServiceStep;
using jnext::remote::PipeChannel;
using jnext::remote::PipeResult;
using jnext::remote::Protocol;
using jnext::remote::Server;
using jnext::remote::ServerConfig;

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

static std::string esc(const std::string& s) {
    std::string out;
    char        b[8];
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F && c != '\\') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(b, sizeof(b), "\\x%02X", c);
            out += b;
        }
    }
    return out;
}

static std::string str(const std::vector<std::uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

static PipeResult hsend(PipeChannel& ch, const std::string& s) {
    return ch.host_send(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

// ── A probe Protocol: records, replies "re:<bytes>", closes on a "q" ───────

struct Probe : Protocol {
    int         connects    = 0;
    int         disconnects = 0;
    std::string got;          // every byte offered to on_service
    bool        reply       = true;

    void on_connect(jnext::remote::Connection&) override { ++connects; }
    ServiceStep on_service(jnext::remote::Connection& c) override {
        std::uint8_t buf[256];
        const std::size_t n = c.read(buf, sizeof(buf));
        if (n == 0) return ServiceStep::Idle;
        const std::string chunk(reinterpret_cast<char*>(buf), n);
        got += chunk;
        if (chunk == "q") {
            c.write(std::string("bye"));
            c.close();
        } else if (reply) {
            c.write("re:" + chunk);
        }
        return ServiceStep::Serviced;
    }
    void on_notify(jnext::remote::Connection&) override {}
    void on_disconnect() override { ++disconnects; }
};

static ServerConfig cfg() {
    ServerConfig c;
    c.name = "probe";
    return c;
}

struct PipeRig {
    PipeChannel channel;
    Probe       probe;
    Server      srv;

    explicit PipeRig(std::size_t cap = PipeChannel::kInboundCap)
        : channel(cap), srv(cfg(), probe) {
        srv.open(channel.make_listener(), "channel", 0);
    }
    void pump(int n = 6) {
        for (int i = 0; i < n; ++i) srv.service_once(0);
    }
};

static void pipe_rows() {
    {
        PipeRig r;
        const bool before = r.srv.listening();
        r.channel.host_open();
        r.pump(1);
        check("XPT-PIPE-01", "host_open() then one pump: on_connect exactly once; the Server "
                             "is listening on port 0 (esp_socket.h EspListener::port)",
              r.probe.connects == 1 && before && r.srv.listening() && r.srv.port() == 0);
    }
    {
        PipeRig r;
        r.channel.host_open();
        hsend(r.channel, "abc");
        hsend(r.channel, "def");
        r.pump();
        check("XPT-PIPE-02", "bytes sent between host_open() and the accept are all delivered, "
                             "in order, after the accept",
              r.probe.connects == 1 && r.probe.got == "abcdef", esc(r.probe.got));
    }
    {
        PipeRig r;
        r.probe.reply = false;
        r.channel.host_open();
        r.pump(1);
        const std::string a("\x00\xFF" "ab", 4), b("\x80\x0A", 2), c("end", 3);
        hsend(r.channel, a);
        r.pump(1);
        hsend(r.channel, b);
        r.pump(1);
        hsend(r.channel, c);
        r.pump();
        check("XPT-PIPE-03", "three host_send()s reach the Protocol concatenated, byte-exact "
                             "(NUL, 0xFF, 0x80 and LF included)",
              r.probe.got == a + b + c, esc(r.probe.got));
    }
    {
        PipeRig r;
        r.channel.host_open();
        hsend(r.channel, "x");
        r.pump();
        const std::string first  = str(r.channel.take_output());
        const std::string second = str(r.channel.take_output());
        check("XPT-PIPE-04", "a Protocol write() comes out of take_output() once; a second "
                             "take is empty",
              first == "re:x" && second.empty(), esc(first) + "/" + esc(second));
    }
    {
        // More than one 16 KiB read chunk (Connection::pull), patterned: a premature end of
        // stream at the first chunk would lose everything past it.
        PipeRig r;
        r.probe.reply = false;
        r.channel.host_open();
        r.pump(1);
        std::string big(40000, '\0');
        for (std::size_t i = 0; i < big.size(); ++i)
            big[i] = static_cast<char>((i * 7u + (i >> 8)) & 0xFF);
        hsend(r.channel, big);
        r.channel.host_close();
        r.pump(400);   // the probe reads 256 bytes a pass
        const int after_first = r.probe.disconnects;
        r.pump();
        check("XPT-PIPE-05", "host_close(): 40000 bytes sent before it (over one 16 KiB read "
                             "chunk) are all offered to on_service byte-exact, then "
                             "on_disconnect exactly once",
              r.probe.got == big && after_first == 1 && r.probe.disconnects == 1,
              "got " + std::to_string(r.probe.got.size()) + " bytes d=" +
                  std::to_string(r.probe.disconnects));
    }
    {
        PipeRig r;
        r.channel.host_open();
        hsend(r.channel, "a");
        r.pump();  // "re:a" is now owed to the host and NOT taken
        const PipeResult c = r.channel.host_close();
        const PipeResult o = r.channel.host_open();
        hsend(r.channel, "b");
        r.pump();
        const std::string out = str(r.channel.take_output());
        check("XPT-PIPE-06", "host_close() + host_open() in one host turn: the second session "
                             "is admitted and served; the first one's unread output never "
                             "comes out of the second's take_output()",
              c == PipeResult::Ok && o == PipeResult::Ok && r.probe.connects == 2 &&
                  r.probe.disconnects == 1 && r.probe.got == "ab" && out == "re:b",
              esc(out) + " c=" + std::to_string(r.probe.connects));
    }
    {
        PipeRig r;
        r.channel.host_open();
        hsend(r.channel, "q");
        r.pump();
        const bool early  = r.channel.take_ended();   // bytes not taken yet
        const std::string out = str(r.channel.take_output());
        const bool first  = r.channel.take_ended();
        const bool second = r.channel.take_ended();
        check("XPT-PIPE-07", "adapter close(): take_output() returns the bytes written before "
                             "it, THEN take_ended() is true exactly once (false before the "
                             "take)",
              !early && out == "bye" && first && !second && r.probe.disconnects == 1,
              esc(out));
    }
    {
        PipeRig r(16);
        r.channel.host_open();
        hsend(r.channel, "q");
        r.pump();   // jnext ended it; the host has not been told
        // Two 10-byte sends on a 16-byte channel: both accepted only if they are dropped, not
        // queued on the dead session.
        const PipeResult late  = hsend(r.channel, "0123456789");
        const PipeResult late2 = hsend(r.channel, "0123456789");
        r.pump();
        r.channel.take_output();
        const bool ended = r.channel.take_ended();
        const PipeResult o = r.channel.host_open();
        r.probe.got.clear();
        hsend(r.channel, "new");
        r.pump();
        check("XPT-PIPE-08", "host_send() after jnext ended the session, before take_ended(): "
                             "accepted (two 10-byte sends on a 16-byte channel both Ok: dropped, not "
                             "queued), delivered nowhere, and not to the next session",
              late == PipeResult::Ok && late2 == PipeResult::Ok && ended && o == PipeResult::Ok && r.probe.connects == 2 &&
                  r.probe.got == "new",
              esc(r.probe.got));
    }
    {
        PipeRig r;
        r.channel.host_open();
        r.pump(1);
        const PipeResult again = r.channel.host_open();
        r.pump();
        check("XPT-PIPE-09", "host_open() while a session is open: refused, no second "
                             "on_connect",
              again == PipeResult::WrongState && r.probe.connects == 1);
    }
    {
        PipeRig r;
        const PipeResult s = hsend(r.channel, "x");
        const PipeResult c = r.channel.host_close();
        const bool       t = r.channel.take_ended();
        const PipeResult o = r.channel.host_open();
        check("XPT-PIPE-10", "host_send() / host_close() with no session: refused, state "
                             "unchanged (no end to report, a host_open() still works)",
              s == PipeResult::WrongState && c == PipeResult::WrongState && !t &&
                  o == PipeResult::Ok && r.channel.take_output().empty());
    }
    {
        PipeRig r(16);
        r.channel.host_open();
        const PipeResult a = hsend(r.channel, "0123456789");   // 10
        const PipeResult b = hsend(r.channel, "ABCDEFG");      // 7: past 16
        const PipeResult c = hsend(r.channel, "abcdef");       // 6: reaches 16 exactly
        r.pump();
        check("XPT-PIPE-11", "a host_send() past the inbound cap is refused WHOLE (Full); the "
                             "bytes before it are intact and the cap is exact",
              a == PipeResult::Ok && b == PipeResult::Full && c == PipeResult::Ok &&
                  r.probe.got == "0123456789abcdef",
              esc(r.probe.got));
    }
    {
        PipeRig r;
        r.channel.host_open();
        r.pump(1);
        r.srv.stop();
        r.channel.take_output();
        const bool ended = r.channel.take_ended();
        check("XPT-PIPE-12", "Server::stop() with a session: on_disconnect once, and the host "
                             "(the channel outlives the listener) sees take_ended()",
              r.probe.disconnects == 1 && ended);
    }
    {
        PipeRig r;
        r.srv.stop();
        const PipeResult o = r.channel.host_open();
        const PipeResult s = hsend(r.channel, "x");
        const PipeResult c = r.channel.host_close();
        check("XPT-PIPE-13", "after Server::stop() with no session, host_open(), host_send() and "
                             "host_close() each return NoChannel and take_output() is empty",
              o == PipeResult::NoChannel && s == PipeResult::NoChannel &&
                  c == PipeResult::NoChannel && r.channel.take_output().empty());
    }
}

// ── DBGS: DebugServers over a real machine ─────────────────────────────────

struct MachineRig {
    Emulator                   emu;
    std::unique_ptr<Debugger>  dbg;
    std::unique_ptr<DebugServers> servers;
    bool                       started = false;

    explicit MachineRig(const EmulatorConfig& c) {
        emu.init(c);
        dbg     = std::make_unique<Debugger>(emu);
        servers = std::make_unique<DebugServers>();
        started = servers->start(*dbg, c);
    }
    ~MachineRig() {
        servers.reset();  // before the Debugger it unregisters from
        dbg.reset();
    }
    void pump(int n = 8) {
        for (int i = 0; i < n; ++i) dbg->pump(PumpBudget{});
    }
};

// The welcome of fixture scene `session`: its first `<` lines, up to the first `>`.
static std::string fixture_welcome() {
    std::ifstream in(JNEXT_ZRCP_FIXTURE);
    std::string   line, out;
    bool          in_scene = false;
    while (std::getline(in, line)) {
        if (line.rfind("@scene session ", 0) == 0) {
            in_scene = true;
            continue;
        }
        if (!in_scene) continue;
        if (line.rfind(">", 0) == 0) break;
        if (line.rfind("< \"", 0) != 0) continue;
        const std::string body = line.substr(3, line.size() - 4);
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\\' && i + 1 < body.size()) {
                const char n = body[++i];
                out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
            } else {
                out += body[i];
            }
        }
    }
    return out;
}

static EmulatorConfig machine_cfg() {
    EmulatorConfig c;
    c.type = MachineType::ZXN_ISSUE2;
    return c;
}

static void servers_rows() {
    {
        EmulatorConfig c = machine_cfg();
        c.dzrp_channel   = true;
        MachineRig r(c);
        PipeChannel* ch = r.servers->channel(cli::DebugProtocol::Dzrp);
        // CMD_INIT, seq 1: length 5 counts the payload (2.2.0, "t\0").
        const std::string init("\x05\x00\x00\x00\x01\x01\x02\x02\x00t\x00", 11);
        PipeResult        o = PipeResult::NoChannel;
        std::string       out;
        if (ch) {
            o = ch->host_open();
            hsend(*ch, init);
            for (int i = 0; i < 8 && out.size() < 5; ++i) {
                r.pump(1);
                out += str(ch->take_output());
            }
        }
        // The frame: le32 length, seq, then error 0, DZRP 2.2.0, machine 4, "jnext v...".
        const std::string head("\x00\x02\x02\x00\x04", 5);
        const bool        frame = out.size() > 5 + 5 && out[4] == 1 &&
                           out.compare(5, 5, head) == 0 && out.compare(10, 7, "jnext v") == 0;
        check("DBGS-CH-01", "DebugServers::start with dzrp_channel: a DZRP CMD_INIT (2.2.0) "
                            "through channel(Dzrp) and the Debugger's pump answers error 0, "
                            "DZRP 2.2.0, machine type 4, a name starting \"jnext v\" "
                            "(dzrp-peer.py sc_init_regs)",
              r.started && ch && o == PipeResult::Ok && frame, esc(out));
    }
    {
        EmulatorConfig c = machine_cfg();
        c.dzrp_channel = c.zrcp_channel = c.gdb_channel = true;
        MachineRig   r(c);
        PipeChannel* d = r.servers->channel(cli::DebugProtocol::Dzrp);
        PipeChannel* z = r.servers->channel(cli::DebugProtocol::Zrcp);
        PipeChannel* g = r.servers->channel(cli::DebugProtocol::Gdb);
        const bool   distinct = d && z && g && d != z && d != g && z != g;
        std::string  welcome, gdb;
        if (z) {
            z->host_open();
            r.pump(2);
            welcome = str(z->take_output());
        }
        if (g) {
            g->host_open();
            hsend(*g, "$?#3f");
            r.pump(4);
            gdb = str(g->take_output());
        }
        const std::string want = fixture_welcome();
        EmulatorConfig    none = machine_cfg();
        MachineRig        r0(none);
        const bool        off = r0.started && !r0.servers->channel(cli::DebugProtocol::Dzrp) &&
                         !r0.servers->channel(cli::DebugProtocol::Zrcp) &&
                         !r0.servers->channel(cli::DebugProtocol::Gdb);
        check("DBGS-CH-02", "all three channels: channel() is non-null and distinct for each; "
                            "ZRCP gives the ZEsarUX 12.0 welcome byte-exact (fixture); GDB "
                            "`$?#3f` is answered with a stop reply; with no channel field set "
                            "all three channel() are null",
              r.started && distinct && !want.empty() && welcome == want &&
                  gdb.find("T05thread:1;") != std::string::npos && off,
              esc(welcome) + " | " + esc(gdb));
    }
    {
        jnext::dbg::ServiceHint attached;
        attached.remote_attached = true;
        const PumpBudget paused  = DebugServers::frame_loop_budget(true, attached);
        const PumpBudget running = DebugServers::frame_loop_budget(false, attached);
#ifdef __EMSCRIPTEN__
        const int want_drain = 0;   // a page: no byte can arrive while wasm runs
#else
        const int want_drain = 2;
#endif
        check("DBGS-BUD-01", "frame_loop_budget(paused, attached) is {0,2,10} natively and "
                             "{0,0,10} in a page; running is {0,0,0} in both",
              paused.max_wait_ms == 0 && paused.drain_ms == want_drain &&
                  paused.budget_ms == 10 && running.max_wait_ms == 0 && running.drain_ms == 0 &&
                  running.budget_ms == 0,
              std::to_string(paused.drain_ms));
    }
}

int main() {
    pipe_rows();
    servers_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail,
                g_skip);
    return g_fail == 0 ? 0 : 1;
}
