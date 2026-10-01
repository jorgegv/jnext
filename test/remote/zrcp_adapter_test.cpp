// jnext::remote::zrcp — the ZEsarUX remote command protocol adapter (GH #280,
// epic #276 package Z). The fake-transport unit suite of zrcp-frontend.md
// §6.2 ("unit tier").
//
// WHAT IS UNDER TEST. `ZrcpServer` (src/remote/zrcp/zrcp_server.*) and its
// formatters (src/remote/zrcp/zrcp_format.*), run inside the PRODUCTION
// `remote::Server` over T's in-memory `FakeListener` / `FakePeer`, on a real
// `Emulator` + `Debugger`, driven through `Debugger::pump()` exactly as a loop
// owner drives it. Only the kernel is replaced. Every row asserts BYTES ON THE
// WIRE and, where a command touches the machine, MACHINE STATE — the #203
// shape: a stop is proved by the machine being stopped, where it should be.
//
//   ZRCP-FR-*    framing (WP-1): the welcome, the prompt, the blank line,
//                unknown / alias / extra arguments, set-cr, CRLF, one command
//                per pass, split delivery, the overlong line.
//   ZRCP-TAB-*   the command table: the census against ZEsarUX 12.0's `ls`,
//                the unsupported and declined replies, help, ls.
//   ZRCP-SES-*   the session: attach on connect, quit, hang-up, the second
//                client, a pause that is not this client's.
//   ZRCP-INFO-*  the information commands and the debug-settings byte.
//   ZRCP-FMT-*   the formatters against the [T] bytes and DeZog's offsets.
//   ZRCP-REG-*, ZRCP-MEM-*, ZRCP-DIS-*, ZRCP-PG-*, ZRCP-STK-*, ZRCP-TIME-*,
//   ZRCP-TBB-*, ZRCP-PORT-*  WP-2's inspection commands on a live machine.
//   ZRCP-CTL-*, ZRCP-RUN-*, ZRCP-RST-*  WP-3: cpu-step mode, the steps, the
//                run state machine (§4.3), resets (§4.6), NMI.
//
// THE ORACLE. The byte strings quoted as [T1]..[T5] are verbatim replies of
// ZEsarUX 12.0 recorded by the design's socket client (zrcp-frontend.md, head
// of file); the register-line offsets are DeZog 3.7.4's
// `decodezesaruxdata.ts`; the command census is ZEsarUX 12.0's own `ls`.
//
// EVERY WAIT IS BOUNDED. No row sleeps: `run n`'s slices run on a fake clock,
// every pump loop has an iteration cap, and T's fake is finite.
//
// Run: ./build/test/zrcp_adapter_test

#include "remote/dzrp/dzrp_server.h"
#include "remote/fake_transport.h"
#include "remote/transport.h"
#include "remote/zrcp/zrcp_condition.h"
#include "remote/zrcp/zrcp_format.h"
#include "remote/zrcp/zrcp_server.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/rzx.h"
#include "debug/debugger.h"
#include "version.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include "../row_id.h"

using jnext::dbg::ClientKind;
using jnext::dbg::Debugger;
using jnext::dbg::PauseReason;
using jnext::dbg::PumpBudget;
using jnext::dbg::Result;
using jnext::remote::FakeListener;
using jnext::remote::FakePeer;
using namespace jnext::remote::zrcp;
using SteadyClock = std::chrono::steady_clock;

// ── Tiny test harness (matches dzrp_adapter_test) ──────────────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " — ",
                    detail.c_str());
    }
}

/// A byte string as a C literal would spell it, for a FAIL line.
static std::string esc(const std::string& s, std::size_t max = 240) {
    std::string out;
    for (std::size_t i = 0; i < s.size() && i < max; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 32 || c > 126) {
            char b[8];
            std::snprintf(b, sizeof(b), "\\x%02X", c);
            out += b;
        } else out.push_back(static_cast<char>(c));
    }
    if (s.size() > max) out += "...";
    return out;
}

static bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
static bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

// ── The fake clock `run n` slices by ───────────────────────────────────────

static SteadyClock::time_point g_now = SteadyClock::time_point{} + std::chrono::hours(1);
/// When set, every read of the clock advances it 1 ms, so a 2 ms slice ends
/// after a couple of steps — a parked `run n` without any real timing.
static bool g_clock_ticks = false;
static SteadyClock::time_point fake_clock() {
    if (g_clock_ticks) g_now += std::chrono::milliseconds(1);
    return g_now;
}

// ── The rig: a real machine, the backend, the adapter over T's fake ────────

constexpr std::uint16_t PROG = 0x8000;

struct Rig {
    Emulator                    emu;
    std::unique_ptr<Debugger>   dbg;
    std::unique_ptr<ZrcpServer> zrcp;
    FakeListener*               lsn = nullptr;

    explicit Rig(MachineType type = MachineType::ZX48K) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        // Parked on `JR $` at 0x8000, interrupts off — the backend suite's
        // idiom: frames run, nothing moves.
        load({0x18, 0xFE});
        dbg  = std::make_unique<Debugger>(emu);
        zrcp = std::make_unique<ZrcpServer>(*dbg, fake_clock);
        auto l = std::make_unique<FakeListener>();
        lsn    = l.get();
        zrcp->server().open(std::move(l), "127.0.0.1", 0);
        dbg->add_service(zrcp->server());
    }

    ~Rig() {
        zrcp.reset();  // before the Debugger it unregisters from
        dbg.reset();
    }

    /// A program at PROG, PC there, SP 0xFF00, interrupts off.
    void load(const std::vector<std::uint8_t>& code, std::uint16_t at = PROG) {
        for (std::size_t i = 0; i < code.size(); ++i)
            emu.mmu().write(static_cast<std::uint16_t>(at + i), code[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = at;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);
    }

    std::uint16_t pc() { return emu.cpu().get_registers().PC; }

    /// One loop-owner tick: the frame batch (nothing if paused), then the pump
    /// with T's budget for the state it finds (debug_servers.cpp's rule).
    void tick() {
        if (!dbg->state().paused) emu.run_frame();
        pump();
    }
    void pump() {
        if (dbg->state().paused)
            dbg->pump(PumpBudget{0, 2, 10});
        else
            dbg->pump(PumpBudget{});
    }
};

/// One ZRCP client.
struct Zc {
    Rig&                      rig;
    std::shared_ptr<FakePeer> p;
    std::string               rx;
    std::string               welcome;

    explicit Zc(Rig& r) : rig(r) {
        p = rig.lsn->connect();
        rig.pump();
        welcome = p->take();
    }

    static bool has_prompt(const std::string& s) {
        return ends_with(s, PROMPT) || ends_with(s, PROMPT_STEP);
    }

    /// Pump (no frames) until a prompt ends what arrived, or `max` pumps.
    std::string wait(int max = 16) {
        for (int i = 0; i < max && !has_prompt(rx); ++i) {
            rig.pump();
            rx += p->take();
        }
        std::string out;
        out.swap(rx);
        return out;
    }

    /// Tick (frames + pump) until a prompt, or `max` ticks.
    std::string wait_ticks(int max = 16) {
        for (int i = 0; i < max && !has_prompt(rx); ++i) {
            rig.tick();
            rx += p->take();
        }
        std::string out;
        out.swap(rx);
        return out;
    }

    /// Send one line and wait for its reply.
    std::string cmd(const std::string& line, int max = 16) {
        p->send(line + "\n");
        return wait(max);
    }

    /// Send, pump once, and return exactly what came back (no waiting).
    std::string send_once(const std::string& bytes) {
        p->send(bytes);
        rig.pump();
        return p->take();
    }
};

static std::string reply_of(const std::string& body, bool step = false) {
    return body + "\n" + (step ? PROMPT_STEP : PROMPT);
}

// ===========================================================================
// ZRCP-FR — framing (WP-1)
// ===========================================================================

static void framing_rows() {
    {
        Rig rig;
        Zc  c(rig);
        check("ZRCP-FR-01", "on connect the client is greeted with ZEsarUX's welcome and the "
                            "prompt, byte for byte ([T1]), and attached to the backend",
              c.welcome ==
                      "Welcome to ZEsarUX remote command protocol (ZRCP)\nWrite help for "
                      "available commands\n\ncommand> " &&
                  rig.dbg->attached(),
              esc(c.welcome));

        check("ZRCP-FR-02", "a blank line is the empty command: \"\\ncommand> \" ([T5])",
              c.cmd("") == "\ncommand> ");

        check("ZRCP-FR-03", "an unknown command answers \"Unknown command\" ([T1])",
              c.cmd("foo-unknown-command") == "Unknown command\ncommand> ");

        const std::string lead  = c.cmd("   get-registers   ");
        const std::string upper = c.cmd("GET-REGISTERS");
        check("ZRCP-FR-04", "names are exact: leading whitespace and upper case are unknown "
                            "commands ([T5])",
              lead == "Unknown command\ncommand> " && upper == "Unknown command\ncommand> ",
              esc(lead) + " / " + esc(upper));

        const std::string gr    = c.cmd("gr");
        const std::string full  = c.cmd("get-registers");
        const std::string extra = c.cmd("get-registers foo bar");
        check("ZRCP-FR-05", "the alias gr answers as get-registers, and extra arguments are "
                            "ignored ([T5])",
              starts_with(gr, "PC=8000 ") && gr == full && extra == full, esc(gr));

        const std::string crlf = c.cmd("get-version\r");
        check("ZRCP-FR-06", "a telnet CRLF line is one command: the name ends at the CR",
              crlf == reply_of(std::string("12.0-jnext-") + JNEXT_VERSION_STRING), esc(crlf));

        const std::string cr_on = c.cmd("set-cr");
        const std::string after = c.cmd("get-version");
        const std::string two   = c.cmd("disassemble 0 2");
        check("ZRCP-FR-07", "set-cr: every later line feed of this session is preceded by a "
                            "carriage return, prompt included",
              cr_on == "\r\ncommand> " &&
                  after == std::string("12.0-jnext-") + JNEXT_VERSION_STRING + "\r\ncommand> " &&
                  two.find('\n') != std::string::npos &&
                  std::count(two.begin(), two.end(), '\n') ==
                      std::count(two.begin(), two.end(), '\r'),
              esc(after) + " / " + esc(two));
    }
    {
        // ONE COMMAND PER PASS: a running machine's pump (PumpBudget{}) asks
        // the service once, so two pipelined commands take two pumps.
        Rig rig;
        Zc  c(rig);
        const std::string first  = c.send_once("about\nget-version\n");
        rig.pump();
        const std::string second = c.p->take();
        check("ZRCP-FR-08", "two commands in one send are answered in order, one per service "
                            "pass (the running budget asks once per pump)",
              first == "jnext ZRCP remote command protocol\ncommand> " &&
                  second == reply_of(std::string("12.0-jnext-") + JNEXT_VERSION_STRING),
              esc(first) + " / " + esc(second));
    }
    {
        // SPLIT DELIVERY: one byte per pump; the command runs once, complete.
        Rig rig;
        Zc  c(rig);
        const std::string line  = "about\n";
        int               early = 0;
        for (std::size_t i = 0; i + 1 < line.size(); ++i)
            if (!c.send_once(line.substr(i, 1)).empty()) ++early;
        const std::string done = c.send_once("\n");
        check("ZRCP-FR-09", "a command delivered one byte per pump is answered once, when its "
                            "newline arrives",
              early == 0 && done == "jnext ZRCP remote command protocol\ncommand> ", esc(done));
    }
    {
        // AN OVERLONG LINE is refused, the rest of it up to the newline
        // discarded, and the session goes on.
        Rig rig;
        Zc  c(rig);
        c.p->send(std::string(600 * 1024, 'x'));
        const std::string refused = c.wait(64);
        c.p->send(std::string(1000, 'y') + "\n");
        const std::string after = c.cmd("about");
        check("ZRCP-FR-10", "a line past 512 KiB with no newline is answered \"Error. Command "
                            "line too long\", discarded to its newline, and the next command is "
                            "served",
              refused == "Error. Command line too long\ncommand> " &&
                  after == "jnext ZRCP remote command protocol\ncommand> " &&
                  !c.p->closed_by_server(),
              esc(refused) + " / " + esc(after));
    }
}

// ===========================================================================
// ZRCP-TAB — the command table (WP-1)
// ===========================================================================

/// ZEsarUX 12.0's `ls` ([T1]): all 125 command names.
static const char* const kZesaruxLs[] = {
    "about", "assemble", "ayplayer", "clear-membreakpoints", "close-all-menus",
    "cpu-code-coverage", "cpu-history", "cpu-panic", "cpu-step", "cpu-step-over",
    "cpu-transaction-log", "debug-analyze-command", "disable-breakpoint",
    "disable-breakpoints", "disassemble", "dump-nested-functions", "dump-scanline-buffer",
    "enable-breakpoint", "enable-breakpoints", "enter-cpu-step",
    "esxdoshandler-get-open-files", "evaluate", "exit-cpu-step", "exit-emulator",
    "extended-stack", "find-label", "generate-nmi", "get-audio-buffer-info",
    "get-breakpoints", "get-breakpointsactions", "get-breakpoints-optimized",
    "get-buildnumber", "get-cpu-core-name", "get-cpu-frequency", "get-cpu-turbo-speed",
    "get-crc32", "get-current-machine", "get-current-memory-zone", "get-debug-settings",
    "get-io-ports", "get-membreakpoints", "get-machines", "get-memory-pages",
    "get-memory-zones", "get-ocr", "get-os", "get-paging-state", "get-registers",
    "get-snapshot", "get-stack-backtrace", "get-text-overlay", "get-tstates",
    "get-tstates-partial", "get-ui-io-ports", "get-version", "get-video-driver",
    "get-visualmem-written-dump", "get-visualmem-read-dump", "get-visualmem-opcode-dump",
    "hard-reset-cpu", "help", "hexdump", "hexdump-internal", "ifrom-press-button",
    "kartusho-press-button", "load-binary", "load-source-code", "ls", "mmc-reload", "noop",
    "open-menu", "print-error", "print-footer", "put-snapshot", "qdos-get-open-files", "quit",
    "read-memory", "realtape-open", "reset-cpu", "reset-tstates-partial", "run",
    "save-binary", "save-binary-internal", "save-screen", "send-keys-ascii",
    "send-keys-event", "send-keys-string", "set-breakpoint", "set-breakpointaction",
    "set-cr", "set-debug-settings", "set-ui-io-ports", "set-machine", "set-membreakpoint",
    "set-memory-zone", "set-register", "set-text-brightness", "set-verbose-level",
    "set-window-zoom", "smartload", "snapshot-load", "snapshot-save",
    "snapshot-inram-get-index", "snapshot-inram-load", "speech-empty-fifo", "speech-send",
    "tbblue-get-clipwindow", "tbblue-set-clipwindow", "tbblue-get-palette",
    "tbblue-get-pattern", "tbblue-get-register", "tbblue-get-sprite", "tbblue-set-palette",
    "tbblue-set-pattern", "tbblue-set-register", "tbblue-set-sprite", "tsconf-get-af-port",
    "tsconf-set-af-port", "view-basic", "write-memory", "write-memory-raw", "write-port",
    "zeng-is-master", "zeng-online", "zxevo-get-nvram",
};

/// Served by the design (§2) and not yet by this build. Empty since WP-5: all
/// of §2's 67 are served. Kept so a row the table loses is still reported.
static const char* const kPendingWp45[] = {""};

static void table_rows() {
    const auto table = ZrcpServer::command_table();
    std::set<std::string> ls(std::begin(kZesaruxLs), std::end(kZesaruxLs));
    std::set<std::string> pending;
    for (const char* n : kPendingWp45)
        if (*n) pending.insert(n);
    std::set<std::string> rows;
    int served = 0, declined = 0, unsupported = 0;
    std::string stray;
    for (const auto& t : table) {
        rows.insert(t.name);
        if (!ls.count(t.name)) stray += std::string(" ") + t.name;
        served += t.cls == CommandClass::Served;
        declined += t.cls == CommandClass::Declined;
        unsupported += t.cls == CommandClass::Unsupported;
    }
    std::string missing;
    for (const auto& n : ls)
        if (!rows.count(n) && !pending.count(n)) missing += " " + n;
    std::string both;
    for (const auto& n : pending)
        if (rows.count(n) || !ls.count(n)) both += " " + n;
    check("ZRCP-TAB-01", "census against ZEsarUX 12.0's ls (125 names): every name is a table "
                         "row, no row is foreign, none pending; 67 served, 1 declined "
                         "(exit-emulator), 57 unsupported — §2's 67 / 1 / 57",
              ls.size() == 125 && pending.empty() && missing.empty() && stray.empty() &&
                  both.empty() && served == 67 && declined == 1 && unsupported == 57,
              "missing:" + missing + " stray:" + stray + " both:" + both + " served=" +
                  std::to_string(served) + " unsupported=" + std::to_string(unsupported));

    Rig rig;
    Zc  c(rig);
    const std::string u     = c.cmd("get-io-ports");
    const std::string ua    = c.cmd("a 8000H NOP");
    const std::string pend  = c.cmd("set-breakpointpasscount 1 2");
    check("ZRCP-TAB-02", "an unsupported ZEsarUX command — by name or by ZEsarUX's alias — "
                         "answers \"Error. Unsupported command in jnext: <name>\"; a name "
                         "ZEsarUX 12.0 lacks (set-breakpointpasscount) is unknown, as there",
              u == "Error. Unsupported command in jnext: get-io-ports\ncommand> " &&
                  ua == "Error. Unsupported command in jnext: assemble\ncommand> " &&
                  pend == "Unknown command\ncommand> ",
              esc(u) + " / " + esc(ua) + " / " + esc(pend));

    const std::string d = c.cmd("exit-emulator");
    const std::string alive = c.cmd("noop");
    check("ZRCP-TAB-03", "the declined exit-emulator answers the error and ends nothing: the "
                         "session serves on and nothing asked the process to exit",
              d == "Error. Unsupported command in jnext: exit-emulator\ncommand> " &&
                  alive == "\ncommand> " && !c.p->closed_by_server(),
              esc(d));

    const std::string help = c.cmd("help");
    check("ZRCP-TAB-04", "help lists the served and declined commands with a description and "
                         "no unsupported one",
              starts_with(help, "Available commands:\n") &&
                  help.find("\nget-registers ") != std::string::npos &&
                  help.find("\nexit-emulator ") != std::string::npos &&
                  help.find("Declined in jnext") != std::string::npos &&
                  help.find("\nget-io-ports") == std::string::npos &&
                  help.find("\nassemble") == std::string::npos &&
                  ends_with(help, "\ncommand> "),
              esc(help, 400));

    const std::string hgr  = c.cmd("help get-registers");
    const std::string hsr  = c.cmd("help set-register");
    const std::string hun  = c.cmd("help get-io-ports");
    const std::string hno  = c.cmd("help nothing-at-all");
    check("ZRCP-TAB-05", "help <cmd>: \"Syntax: name|alias params\\n\\nDescription\\n…\" as "
                         "ZEsarUX's, the divergences stated (MMU, IM); an unsupported one says "
                         "so; an unknown one has no help",
              starts_with(hgr, "Syntax: get-registers|gr\n\nDescription\n") &&
                  hgr.find("8000H+k") != std::string::npos &&
                  starts_with(hsr, "Syntax: set-register|sr register=value\n\nDescription\n") &&
                  hsr.find("IM") != std::string::npos &&
                  hun == "Error. Unsupported command in jnext: get-io-ports\ncommand> " &&
                  hno == "No help for that command\ncommand> ",
              esc(hgr) + " / " + esc(hsr));

    const std::string ls_out = c.cmd("ls");
    // Four columns, each the longest listed name + 2 (remote_simple_help).
    const std::size_t nl = ls_out.find('\n');
    const std::string row1 = nl == std::string::npos ? "" : ls_out.substr(0, nl);
    check("ZRCP-TAB-06", "ls: four columns of the served and declined names, each as wide as "
                         "the longest + 2, no unsupported name",
              starts_with(row1, "about") && row1.find("close-all-menus") != std::string::npos &&
                  ls_out.find("get-io-ports") == std::string::npos &&
                  ls_out.find("exit-emulator") != std::string::npos &&
                  row1.size() == 4 * (std::string("get-breakpointsactions").size() + 2),
              esc(row1));
}

// ===========================================================================
// ZRCP-SES — the session (WP-1)
// ===========================================================================

static void session_rows() {
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const bool paused = rig.dbg->state().paused;
        c.p->send("quit\n");
        rig.pump();
        const std::string bye = c.p->take();
        for (int i = 0; i < 4; ++i) rig.pump();
        check("ZRCP-SES-01", "quit: \"Sayonara baby\\n\", no prompt, the socket closed, the "
                             "client detached and the pause it made released (SES-01)",
              paused && bye == "Sayonara baby\n" && c.p->closed_by_server() &&
                  !rig.dbg->attached() && !rig.dbg->state().paused,
              esc(bye));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.p->close();
        for (int i = 0; i < 4; ++i) rig.pump();
        check("ZRCP-SES-02", "a hang-up detaches the client and releases its pause, so a "
                             "crashed client cannot leave the machine hung (§4.5)",
              !rig.dbg->attached() && !rig.dbg->state().paused);
    }
    {
        // A PAUSE THAT IS NOT THIS CLIENT'S survives it: enter-cpu-step does
        // not re-pause a paused machine (D's CMD_INIT lesson), so the detach
        // has nothing of this client's to release.
        Rig        rig;
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->pause(other);
        {
            Zc c(rig);
            c.cmd("enter-cpu-step");
            c.p->close();
            for (int i = 0; i < 4; ++i) rig.pump();
        }
        check("ZRCP-SES-03", "enter-cpu-step on a machine another client paused leaves that "
                             "pause its owner's: the ZRCP client's departure does not resume it",
              rig.dbg->state().paused && rig.dbg->state().pause_reason.by == other);
        rig.dbg->detach(other);
    }
    {
        // GH #280 N1 (owner decision 2026-10-01, "the pause should belong to
        // the one remaining"), through the real adapter path: a ZRCP client
        // that STEPS a machine another client paused owns the Step stop, and
        // its hang-up hands the pause back instead of releasing it.
        Rig        rig;
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->pause(other);
        bool zrcp_owned = false;
        {
            Zc c(rig);
            c.cmd("enter-cpu-step");
            c.cmd("cpu-step");
            zrcp_owned = rig.dbg->state().pause_reason.by != other;
            c.p->close();
            for (int i = 0; i < 4; ++i) rig.pump();
        }
        const auto st = rig.dbg->state();
        check("ZRCP-SES-09", "another client pauses, the ZRCP client cpu-steps (its own "
                             "Step stop) and hangs up: still paused, the other client's "
                             "again",
              zrcp_owned && st.paused && st.pause_reason.by == other,
              "paused=" + std::to_string(st.paused) + " by=" +
                  std::to_string(st.pause_reason.by));
        rig.dbg->detach(other);
    }
    {
        // The ZRCP client's OWN pause, another client attached and idle: the
        // hang-up passes it on; that client's own detach, the last, releases it.
        Rig        rig;
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        {
            Zc c(rig);
            c.cmd("enter-cpu-step");
            c.p->close();
            for (int i = 0; i < 4; ++i) rig.pump();
        }
        const auto st = rig.dbg->state();
        rig.dbg->detach(other);
        check("ZRCP-SES-10", "enter-cpu-step pauses a running machine, the ZRCP client "
                             "hangs up with another client attached: still paused, now "
                             "the other client's; its later detach releases it",
              st.paused && st.pause_reason.by == other && !rig.dbg->state().paused,
              "paused=" + std::to_string(st.paused) + " by=" +
                  std::to_string(st.pause_reason.by));
    }
    {
        Rig rig;
        Zc  a(rig);
        auto second = rig.lsn->connect();
        for (int i = 0; i < 3; ++i) rig.pump();
        const std::string busy = second->take();
        const std::string still = a.cmd("about");
        check("ZRCP-SES-04", "a second client is told \"Error. Another ZRCP client is "
                             "connected\\n\" and closed; the first serves on (§5.5)",
              busy == "Error. Another ZRCP client is connected\n" &&
                  second->closed_by_server() &&
                  still == "jnext ZRCP remote command protocol\ncommand> ",
              esc(busy));
    }
}

// ===========================================================================
// ZRCP-INFO — information (WP-1)
// ===========================================================================

static void info_rows() {
    Rig rig;
    Zc  c(rig);
    const std::string about = c.cmd("about");
    const std::string ver   = c.cmd("get-version");
    const std::string build = c.cmd("get-buildnumber");
    const std::string core  = c.cmd("get-cpu-core-name");
    const std::string os    = c.cmd("get-os");
    const std::string noop  = c.cmd("noop");
    const std::string menus = c.cmd("close-all-menus");
#if defined(_WIN32)
    const std::string want_os = "Windows";
#elif defined(__APPLE__)
    const std::string want_os = "macOS";
#else
    const std::string want_os = "GNU/Linux";
#endif
    check("ZRCP-INFO-01", "about / get-version (12.0-jnext-<ver>: semver 12.0.0, >= DeZog's "
                          "10.3, < 12.1) / get-buildnumber / get-cpu-core-name / get-os / noop / "
                          "close-all-menus answer exactly",
              about == "jnext ZRCP remote command protocol\ncommand> " &&
                  ver == reply_of(std::string("12.0-jnext-") + JNEXT_VERSION_STRING) &&
                  build == reply_of(JNEXT_VERSION_STRING) &&
                  core == "jnext-fuse-z80\ncommand> " && os == reply_of(want_os) &&
                  noop == "\ncommand> " && menus == "\ncommand> ",
              esc(ver) + " / " + esc(os));

    const std::string d0   = c.cmd("get-debug-settings");
    const std::string set3 = c.cmd("set-debug-settings 3");
    const std::string d3   = c.cmd("gds");
    const std::string b5   = c.cmd("set-debug-settings 32");
    const std::string d3b  = c.cmd("get-debug-settings");
    const std::string none = c.cmd("set-debug-settings");
    check("ZRCP-INFO-02", "the debug-settings byte starts at ZEsarUX's 1, stores and echoes; "
                          "bit 5 (step over interrupt) is declined and changes nothing; no "
                          "value is ZEsarUX's error",
              d0 == "1\ncommand> " && set3 == "\ncommand> " && d3 == "3\ncommand> " &&
                  b5 == "Error. Unsupported in jnext: step-over-interrupt (bit 5)\ncommand> " &&
                  d3b == "3\ncommand> " && none == "ERROR. No parameter set\ncommand> ",
              esc(b5));
}

// ===========================================================================
// ZRCP-FMT — the formatters, against [T] bytes and DeZog's offsets (WP-2)
// ===========================================================================

/// Eight slots as a Next at reset shows them: ROM image `rom` in slots 0-1,
/// RAM pages 10 11 4 5 0 1 above.
static std::array<jnext::dbg::SlotInfo, 8> next_slots(int rom) {
    std::array<jnext::dbg::SlotInfo, 8> s{};
    const std::uint8_t ram[6] = {10, 11, 4, 5, 0, 1};
    for (int i = 0; i < 2; ++i) {
        s[static_cast<std::size_t>(i)].is_rom         = true;
        s[static_cast<std::size_t>(i)].nr_page        = 0xFF;
        s[static_cast<std::size_t>(i)].effective_page = static_cast<std::uint8_t>(rom * 2 + i);
    }
    for (int i = 0; i < 6; ++i) {
        s[static_cast<std::size_t>(i + 2)].nr_page        = ram[i];
        s[static_cast<std::size_t>(i + 2)].effective_page = ram[i];
    }
    return s;
}

/// DeZog 3.7.4's decoder (`decodezesaruxdata.ts`): the label's first
/// `indexOf`, then a fixed width.
static unsigned dezog_field(const std::string& line, const std::string& label, int width) {
    const std::size_t at = line.find(label);
    if (at == std::string::npos || at + label.size() + width > line.size()) return 0x10000u;
    return static_cast<unsigned>(std::stoul(line.substr(at + label.size(), width), nullptr, 16));
}

static void format_rows() {
    // [T1]'s get-registers, register for register.
    Z80Registers r{};
    r.PC = 0x0136; r.SP = 0xFFDD; r.AF = 0x03BE; r.BC = 0x4F9D; r.HL = 0x03DA; r.DE = 0x0000;
    r.IX = 0xFFFF; r.IY = 0x16A0; r.AF2 = 0xFFFF; r.BC2 = 0xFFFF; r.HL2 = 0xFFFF;
    r.DE2 = 0xFFFF; r.I = 0x00; r.R = 0x59; r.MEMPTR = 0x0136; r.IM = 1; r.IFF1 = 0; r.IFF2 = 0;
    const std::string line = register_line(r, next_slots(3), MachineType::ZXN_ISSUE2);
    check("ZRCP-FMT-01", "the register line is [T1]'s byte for byte — lower-case hex, HL before "
                         "DE, two spaces before F=, IM1, IFF--, VPS: 0 — except the two ROM "
                         "slots, 8002 8003 where ZEsarUX 12.0 prints 0000 (§2.3.1)",
              line == "PC=0136 SP=ffdd AF=03be BC=4f9d HL=03da DE=0000 IX=ffff IY=16a0 "
                      "AF'=ffff BC'=ffff HL'=ffff DE'=ffff I=00 R=59  F=S-5H3PN- "
                      "F'=SZ5H3PNC MEMPTR=0136 IM1 IFF-- VPS: 0 "
                      "MMU=80028003000a000b0004000500000001",
              esc(line));

    Z80Registers v{};
    v.PC = 0x1234; v.SP = 0x5678; v.AF = 0x9ABC; v.BC = 0xDEF0; v.DE = 0x1357; v.HL = 0x2468;
    v.IX = 0xA1B2; v.IY = 0xC3D4; v.AF2 = 0xE5F6; v.BC2 = 0x0718; v.DE2 = 0x293A;
    v.HL2 = 0x4B5C; v.I = 0x6D; v.R = 0x7E; v.IM = 2; v.IFF1 = 1; v.IFF2 = 1; v.MEMPTR = 0x8F9E;
    const std::string vl = register_line(v, next_slots(0), MachineType::ZXN_ISSUE2);
    const bool widths =
        dezog_field(vl, "PC=", 4) == 0x1234 && dezog_field(vl, "SP=", 4) == 0x5678 &&
        dezog_field(vl, "AF=", 4) == 0x9ABC && dezog_field(vl, "BC=", 4) == 0xDEF0 &&
        dezog_field(vl, "HL=", 4) == 0x2468 && dezog_field(vl, "DE=", 4) == 0x1357 &&
        dezog_field(vl, "IX=", 4) == 0xA1B2 && dezog_field(vl, "IY=", 4) == 0xC3D4 &&
        dezog_field(vl, "AF'=", 4) == 0xE5F6 && dezog_field(vl, "BC'=", 4) == 0x0718 &&
        dezog_field(vl, "HL'=", 4) == 0x4B5C && dezog_field(vl, "DE'=", 4) == 0x293A &&
        dezog_field(vl, "I=", 2) == 0x6D && dezog_field(vl, "R=", 2) == 0x7E &&
        vl.find(" IM2 ") != std::string::npos && vl.find(" IFF12 ") != std::string::npos &&
        vl.find(" MEMPTR=8f9e ") != std::string::npos;
    check("ZRCP-FMT-02", "every field DeZog reads is at its label + the fixed width "
                         "(decodezesaruxdata.ts: 4 hex for 16-bit, 2 for I/R, IM + one digit), "
                         "with distinct values so a swapped field shows; MEMPTR is its own",
              widths, esc(vl));

    // The MMU= projection DeZog decodes (value >= 0x8000 -> ROM 0xFC + (v & 3)).
    const std::string m0 = register_line(v, next_slots(0), MachineType::ZXN_ISSUE2);
    const std::string m1 = register_line(v, next_slots(1), MachineType::ZXN_ISSUE2);
    const std::string m2 = register_line(v, next_slots(2), MachineType::ZXN_ISSUE2);
    check("ZRCP-FMT-03", "MMU=: ROM 0 and 2 -> 8000 8001 (DeZog ROM0), ROM 1 and 3 -> 8002 "
                         "8003 (ROM1); a RAM slot is its 8K page; never the 00ff sentinel",
              ends_with(m0, "MMU=80008001000a000b0004000500000001") &&
                  ends_with(m1, "MMU=80028003000a000b0004000500000001") &&
                  ends_with(m2, "MMU=80008001000a000b0004000500000001") &&
                  m0.find("00ff") == std::string::npos,
              esc(m1));

    check("ZRCP-FMT-04", "F=: SZ5H3PNC with - for each clear bit",
              flags_string(0xFF) == "SZ5H3PNC" && flags_string(0x00) == "--------" &&
                  flags_string(0xBE) == "S-5H3PN-" && flags_string(0x01) == "-------C");

    const std::string dl = disasm_line(0x0083, "CALL $1C5E");
    check("ZRCP-FMT-05", "a disassembly line is \"  0083 CALL 1C5E\" ([T5]): jnext's $ "
                         "dropped, the mnemonic at column 7 where DeZog reads it",
              dl == "  0083 CALL 1C5E" && dl.substr(7, 4) == "CALL" &&
                  strip_dollar("LD ($5C5D),HL") == "LD (5C5D),HL" &&
                  strip_dollar("DB $ED,$FF") == "DB ED,FF",
              esc(dl));

    // [T1] `hexdump 0 32` over [T1]'s ROM bytes.
    const std::uint8_t rom[32] = {0xF3, 0xED, 0x56, 0xC3, 0x80, 0x00, 0xFF, 0xFF,
                                  0xED, 0x4D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xED, 0x4D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                  0xED, 0x4D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const std::string hd = hexdump(0, rom, 32);
    const std::string hp = hexdump(0xFFFE, rom, 3);
    check("ZRCP-FMT-06", "hexdump is [T1]'s byte for byte; a short last line is padded to the "
                         "ASCII column",
              hd == "  0000H F3 ED 56 C3 80 00 FF FF ED 4D FF FF FF FF FF FF  |..V......M......|\n"
                    "  0010H ED 4D FF FF FF FF FF FF ED 4D FF FF FF FF FF FF  |.M.......M......|\n" &&
                  hp == "  FFFEH F3 ED 56 " + std::string(40, ' ') + "|..V|\n",
              esc(hp));

    check("ZRCP-FMT-07", "get-crc32's CRC-32 of [T1]'s first 16 ROM bytes is [T1]'s cc66e252",
              crc32_ieee(rom, 16) == 0xCC66E252u);

    const std::string pages = memory_pages(next_slots(0), false, MachineType::ZXN_ISSUE2);
    const std::string verb  = memory_pages(next_slots(0), true, MachineType::ZXN_ISSUE2);
    check("ZRCP-FMT-08", "get-memory-pages is [T1]'s \"RO RO A10 A11 A4 A5 A0 A1 \"; verbose is "
                         "[T3]'s Segment blocks",
              pages == "RO RO A10 A11 A4 A5 A0 A1 " &&
                  starts_with(verb, "Segment 1\nLong name: ROM\nShort name: RO\nStart: 0H\n"
                                    "End: 1FFFH\n\nSegment 2\n") &&
                  verb.find("Segment 3\nLong name: RAM 10\nShort name: A10\nStart: 4000H\n"
                            "End: 5FFFH\n\n") != std::string::npos &&
                  ends_with(verb, "Segment 8\nLong name: RAM 1\nShort name: A1\nStart: E000H\n"
                                  "End: FFFFH\n\n"),
              esc(verb, 400));

    std::uint32_t n = 0;
    const bool nums =
        parse_number("38h", n) && n == 0x38 && parse_number("0FFH", n) && n == 0xFF &&
        parse_number("32768", n) && n == 32768 && parse_number("4294967295", n) &&
        n == 0xFFFFFFFFu && !parse_number("0x38", n) && !parse_number("", n) &&
        !parse_number("H", n) && !parse_number("12G", n) && !parse_number("-1", n) &&
        !parse_number("4294967296", n) && !parse_number("FF", n) && !parse_number("ff", n) &&
        !parse_number("1a", n);
    check("ZRCP-FMT-09", "numbers: decimal or H-suffixed hex; 0x38, a bare H, a sign, a stray "
                         "letter, hex digits with no H (either case) and a value past 32 bits "
                         "are refused, never read as 0 (§1.1)",
              nums);

    check("ZRCP-FMT-10", "get-current-machine names the four machine types as DeZog matches "
                         "them (\"zx spectrum next\", \"48k\", \"128k\")",
              machine_name(MachineType::ZXN_ISSUE2) == "ZX Spectrum Next" &&
                  machine_name(MachineType::ZX48K) == "ZX Spectrum 48k" &&
                  machine_name(MachineType::ZX128K) == "ZX Spectrum 128k" &&
                  machine_name(MachineType::ZX_PLUS3) == "ZX Spectrum +3");
}

// ===========================================================================
// WP-2 — inspection on a live machine
// ===========================================================================

/// An RZX playback in force: the backend refuses every mutation while one
/// runs (D's idiom). No frame is run while it is on.
struct RzxOn {
    Emulator& emu;
    explicit RzxOn(Emulator& e) : emu(e) { emu.rzx_player().start(RzxRecording{}); }
    ~RzxOn() { emu.rzx_player().stop(); }
};

static std::string hex_of(const std::vector<std::uint8_t>& b) {
    std::string out;
    char        buf[4];
    for (std::uint8_t x : b) {
        std::snprintf(buf, sizeof(buf), "%02X", x);
        out += buf;
    }
    return out;
}

static std::vector<std::uint8_t> peek(Rig& rig, std::uint16_t addr, std::size_t n) {
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i)
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), static_cast<std::uint16_t>(addr + i), 1,
                      &out[i]);
    return out;
}

static void register_rows() {
    {
        Rig rig;
        Zc  c(rig);
        const std::string gr   = c.cmd("get-registers");
        const std::string want = register_line(rig.dbg->registers(), rig.dbg->mmu_slots(), rig.dbg->machine().type);
        check("ZRCP-REG-01", "get-registers is the live register line — PC=8000 on the rig, and "
                             "on the 48K machine ZEsarUX's legacy MMU=: ROM 0, banks 5 2 0, then "
                             "four 0000",
              gr == reply_of(want) && starts_with(gr, "PC=8000 SP=ff00 ") &&
                  gr.find("MMU=80000005000200000000000000000000\n") != std::string::npos,
              esc(gr));

        const std::string pc = c.cmd("set-register PC=9000H");
        check("ZRCP-REG-02", "set-register PC=9000H answers the register line with PC=9000 and "
                             "the machine's PC is 0x9000",
              starts_with(pc, "PC=9000 ") && rig.pc() == 0x9000 &&
                  pc == reply_of(register_line(rig.dbg->registers(), rig.dbg->mmu_slots(), rig.dbg->machine().type)),
              esc(pc));

        Z80Registers r = rig.emu.cpu().get_registers();
        r.AF2 = 0xFFFF; r.HL2 = 0xFFFF; r.AF = 0xFFFF;
        rig.emu.cpu().set_registers(r);
        c.cmd("set-register A'=12H");
        c.cmd("set-register HL'=1234H");
        c.cmd("set-register IFF1=1");
        c.cmd("set-register I=3FH");
        c.cmd("set-register R=7");
        const std::string f = c.cmd("set-register F=1");
        const Z80Registers m = rig.emu.cpu().get_registers();
        check("ZRCP-REG-03", "[T4]'s set-register sequence: A'=12H -> AF'=12ff, HL'=1234H, "
                             "IFF1=1 -> IFF1-, I=3FH, R=7 -> R=07, F=1 -> F=-------C, each in the "
                             "machine",
              f.find("AF'=12ff") != std::string::npos && f.find("HL'=1234") != std::string::npos &&
                  f.find(" IFF1- ") != std::string::npos && f.find(" I=3f R=07 ") != std::string::npos &&
                  f.find(" F=-------C ") != std::string::npos && m.AF2 == 0x12FF &&
                  m.HL2 == 0x1234 && m.IFF1 == 1 && m.IFF2 == 0 && m.I == 0x3F && m.R == 7 &&
                  (m.AF & 0xFF) == 0x01,
              esc(f));

        const std::string im2  = c.cmd("set-register IM=2");
        const std::string im3  = c.cmd("set-register IM=3");
        const std::string xx   = c.cmd("set-register XX=1");
        const std::string hex0 = c.cmd("set-register PC=0x10");
        const std::string noeq = c.cmd("set-register PC");
        const std::string low  = c.cmd("set-register pc=4660");
        check("ZRCP-REG-04", "IM=2 is set (divergence: ZEsarUX 12.0 refuses it); IM=3, an unknown "
                             "name, 0x10 and a missing = answer \"Error changing register\"; names "
                             "are case-insensitive",
              im2.find(" IM2 ") != std::string::npos &&
                  im3 == "Error changing register\ncommand> " &&
                  xx == "Error changing register\ncommand> " &&
                  hex0 == "Error changing register\ncommand> " &&
                  noeq == "Error changing register\ncommand> " && starts_with(low, "PC=1234 ") &&
                  rig.emu.cpu().get_registers().IM == 2 && rig.pc() == 0x1234,
              esc(im3));
        {
            RzxOn rzx(rig.emu);
            const std::string refused = c.cmd("set-register PC=100");
            check("ZRCP-REG-05", "under an RZX the backend refuses the write: \"Error changing "
                                 "register\" and PC unchanged",
                  refused == "Error changing register\ncommand> " && rig.pc() == 0x1234,
                  esc(refused));
        }
    }
}

static void memory_rows() {
    Rig rig;
    Zc  c(rig);
    for (int i = 0; i < 4; ++i) rig.emu.mmu().write(static_cast<std::uint16_t>(0x9000 + i),
                                                    static_cast<std::uint8_t>(0xA0 + i));
    rig.emu.mmu().write(0xFFFF, 0x5A);
    const std::string four = c.cmd("read-memory 36864 4");
    const std::string one  = c.cmd("read-memory 9000H");
    const std::string wrap = c.cmd("read-memory 65535 4");
    const std::string want_wrap = "5A" + hex_of(peek(rig, 0, 3));
    check("ZRCP-MEM-01", "read-memory: upper-case hex, two digits a byte, one line; address only "
                         "= 1 byte; past FFFFH it wraps to 0000H ([T1])",
          four == "A0A1A2A3\ncommand> " && one == "A0\ncommand> " &&
              wrap == reply_of(want_wrap),
          esc(four) + " / " + esc(wrap));

    const std::string all  = c.cmd("read-memory");
    const std::string zero = c.cmd("read-memory 0 0");
    const std::string big  = c.cmd("read-memory 0 2000000");
    const std::string bad  = c.cmd("read-memory 0x10");
    check("ZRCP-MEM-02", "bare read-memory and length 0 are the whole 64 KB (131072 digits, as "
                         "ZEsarUX's zone rule); a length past 1 MiB and 0x10 are refused",
          all.size() == 131072 + std::string("\ncommand> ").size() &&
              starts_with(all, hex_of(peek(rig, 0, 4))) && zero == all &&
              big == "Error. Length too large (max 1048576)\ncommand> " &&
              bad == "Error. Invalid address: 0x10\ncommand> ",
          esc(big) + " / " + esc(bad));

    const std::string wm    = c.cmd("write-memory 32768 65 66 67");
    const auto        after = peek(rig, 0x8000, 3);
    const auto        rom0  = peek(rig, 0, 1);
    const std::string wrom  = c.cmd("write-memory 0 1");
    const std::string badv  = c.cmd("write-memory 32768 256 1");
    const std::string none  = c.cmd("write-memory");
    check("ZRCP-MEM-03", "write-memory writes the bytes (empty reply); a byte into ROM is "
                         "ignored with the same empty reply ([T4]); 256 is refused before "
                         "anything is written; no parameter is ZEsarUX's error",
          wm == "\ncommand> " && after == std::vector<std::uint8_t>({0x41, 0x42, 0x43}) &&
              wrom == "\ncommand> " && peek(rig, 0, 1) == rom0 &&
              badv == "Error. Invalid byte value: 256\ncommand> " &&
              peek(rig, 0x8000, 1)[0] == 0x41 && none == "ERROR. No parameters set\ncommand> ",
          esc(badv));

    const std::string raw  = c.cmd("write-memory-raw 32768 0102ff");
    const auto        rw   = peek(rig, 0x8000, 3);
    const std::string odd  = c.cmd("write-memory-raw 32768 010");
    const std::string junk = c.cmd("write-memory-raw 32768 zz");
    std::string       rzx_r;
    {
        RzxOn rzx(rig.emu);
        rzx_r = c.cmd("write-memory-raw 32768 7777");
    }
    check("ZRCP-MEM-04", "write-memory-raw writes hex pairs; an odd digit count or a non-hex "
                         "digit is refused and writes nothing; under an RZX the refusal is said",
          raw == "\ncommand> " && rw == std::vector<std::uint8_t>({0x01, 0x02, 0xFF}) &&
              odd == "Error. Invalid hexadecimal byte string\ncommand> " &&
              junk == "Error. Invalid hexadecimal byte string\ncommand> " &&
              rzx_r == "Error. write-memory-raw refused: refused_rzx\ncommand> " &&
              peek(rig, 0x8000, 3) == rw,
          esc(odd) + " / " + esc(rzx_r));

    const auto        b20  = peek(rig, 0x9000, 20);
    const std::string hd   = c.cmd("hexdump 9000H 20");
    const std::string hd1  = c.cmd("hexdump 0");
    const auto        b16  = peek(rig, 0x9000, 16);
    char              crc[16];
    std::snprintf(crc, sizeof(crc), "%08x", crc32_ieee(b16.data(), b16.size()));
    const std::string cr   = c.cmd("get-crc32 36864 16");
    const std::string cr0  = c.cmd("get-crc32 0 0");
    const std::string cr1  = c.cmd("get-crc32 0");
    check("ZRCP-MEM-05", "hexdump and get-crc32 are the formatters over the live CPU view; "
                         "ZEsarUX's parameter errors",
          hd == reply_of(hexdump(0x9000, b20.data(), b20.size())) &&
              hd1 == "ERROR. Needs two parameters\ncommand> " && cr == reply_of(crc) &&
              cr0 == "ERROR. Length must be >0\ncommand> " &&
              cr1 == "ERROR. Needs two parameters\ncommand> ",
          esc(hd) + " / " + esc(cr));
}

static void disasm_rows() {
    Rig rig;
    rig.load({0x01, 0x00, 0xFF, 0x00, 0xCD, 0x5E, 0x1C, 0xC7});
    rig.emu.mmu().write(0xFFFE, 0x00);
    rig.emu.mmu().write(0xFFFF, 0x00);
    Zc c(rig);
    const std::string pc    = c.cmd("disassemble");
    const std::string four  = c.cmd("d 8000H 4");
    const std::string wrap  = c.cmd("disassemble 0FFFEH 3");
    check("ZRCP-DIS-01", "disassemble: from PC by default, one line; n lines; ZEsarUX's operand "
                         "spelling with no $ and the mnemonic at column 7 ([T1]); past FFFFH it "
                         "wraps to 0000H ([T4])",
          pc == "  8000 LD BC,FF00\ncommand> " &&
              four == "  8000 LD BC,FF00\n  8003 NOP\n  8004 CALL 1C5E\n  8007 RST 00\ncommand> " &&
              starts_with(wrap, "  FFFE NOP\n  FFFF NOP\n  0000 ") &&
              four.find('$') == std::string::npos,
          esc(four) + " / " + esc(wrap));
}

static void pages_stack_time_rows() {
    {
        Rig rig;
        Zc  c(rig);
        const std::string gmp = c.cmd("get-memory-pages");
        const std::string v   = c.cmd("gmp verbose");
        check("ZRCP-PG-01", "get-memory-pages on the 48K machine is ZEsarUX's two fixed "
                             "segments \"ROM RAM \"; verbose is the segment list",
              gmp == "ROM RAM \ncommand> " &&
                  v == reply_of(memory_pages(rig.dbg->mmu_slots(), true, rig.dbg->machine().type)),
              esc(gmp));
    }
    {
        Rig rig;
        const std::uint8_t stack[] = {0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A, 0xF0, 0xDE, 0x11, 0x22};
        for (std::size_t i = 0; i < sizeof(stack); ++i)
            rig.emu.mmu().write(static_cast<std::uint16_t>(0xFF00 + i), stack[i]);
        Zc c(rig);
        const std::string three = c.cmd("get-stack-backtrace 3");
        const std::string five  = c.cmd("get-stack-backtrace");
        const std::string zero  = c.cmd("get-stack-backtrace 0");
        c.cmd("set-register SP=65534");
        rig.emu.mmu().write(0xFFFE, 0xCD);
        rig.emu.mmu().write(0xFFFF, 0xAB);
        const auto        rom  = peek(rig, 0, 2);
        const std::string wrap = c.cmd("get-stack-backtrace 2");
        char              w2[16];
        std::snprintf(w2, sizeof(w2), "%04XH ", rom[0] | (rom[1] << 8));
        check("ZRCP-STK-01", "get-stack-backtrace: little-endian words from SP, %04XH each + a "
                              "space, 5 by default, 0 refused; past FFFFH it wraps",
              three == "1234H 5678H 9ABCH \ncommand> " &&
                  five == "1234H 5678H 9ABCH DEF0H 2211H \ncommand> " &&
                  zero == "ERROR. Items must be >0\ncommand> " &&
                  wrap == reply_of(std::string("ABCDH ") + w2),
              esc(five) + " / " + esc(wrap));
    }
    {
        Rig rig;
        rig.load({0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const auto        t  = rig.dbg->time();
        const auto        mi = rig.dbg->machine();
        const std::string ts = c.cmd("get-tstates");
        const std::string p0 = c.cmd("get-tstates-partial");
        c.cmd("reset-tstates-partial");
        c.cmd("cpu-step");
        const std::string p1 = c.cmd("get-tstates-partial");
        check("ZRCP-TIME-01", "get-tstates is the in-frame count in CPU T-states; "
                              "get-tstates-partial is nine zero-padded digits and counts exactly "
                              "the 4 T-states of the NOP stepped since reset-tstates-partial",
              ts == reply_of(std::to_string(t.cycle_in_frame /
                                            static_cast<std::uint64_t>(mi.cpu_divisor)),
                             true) &&
                  p0.size() >= 9 + std::string("\ncommand@cpu-step> ").size() &&
                  p1 == "000000004\ncommand@cpu-step> ",
              esc(ts) + " / " + esc(p1));

        const std::string hz = c.cmd("get-cpu-frequency");
        const double want = static_cast<double>(mi.master_cycles_per_frame) * mi.fps /
                            mi.cpu_divisor;
        const long long v = std::atoll(hz.c_str());
        check("ZRCP-TIME-02", "get-cpu-frequency is the live CPU clock in decimal Hz: master "
                              "cycles per second over the divisor, ~3.5 MHz on the 48K machine",
              hz == reply_of(std::to_string(std::llround(want)), true) && v > 3400000 &&
                  v < 3600000,
              esc(hz));
    }
    {
        std::string got;
        for (MachineType t : {MachineType::ZX48K, MachineType::ZX128K, MachineType::ZX_PLUS3,
                              MachineType::ZXN_ISSUE2}) {
            Rig rig(t);
            Zc  c(rig);
            got += c.cmd("gcm");
        }
        check("ZRCP-MACH-01", "get-current-machine on the four machines",
              got == "ZX Spectrum 48k\ncommand> ZX Spectrum 128k\ncommand> ZX Spectrum "
                     "+3\ncommand> ZX Spectrum Next\ncommand> ",
              esc(got));
    }
}

static void tbblue_rows() {
    Rig rig(MachineType::ZXN_ISSUE2);
    Zc  c(rig);
    char nr7[8];
    std::snprintf(nr7, sizeof(nr7), "%02XH", rig.dbg->nextreg_peek(0x07));
    const std::string r7  = c.cmd("tbblue-get-register 7");
    const std::string r7h = c.cmd("tbblue-get-register 7H");
    const std::string big = c.cmd("tbblue-get-register 256");
    const std::string no  = c.cmd("tbblue-get-register");
    check("ZRCP-TBB-01", "tbblue-get-register is the register through its read path as %02XH "
                         "([T1]); 256 is out of range; no index is ZEsarUX's error",
          r7 == reply_of(nr7) && r7h == r7 && big == "ERROR. Out of range\ncommand> " &&
              no == "ERROR. No parameter set\ncommand> ",
          esc(r7));

    const std::string set = c.cmd("tbblue-set-register 80 5");
    const std::string gmp = c.cmd("get-memory-pages");
    const std::string oor = c.cmd("tbblue-set-register 80 256");
    const std::string one = c.cmd("tbblue-set-register 80");
    check("ZRCP-TBB-02", "tbblue-set-register 80 5 runs NR 0x50's handler: slot 0 is RAM page 5 "
                         "(get-memory-pages says A5); 256 and a missing value are ZEsarUX's "
                         "errors",
          set == "\ncommand> " && starts_with(gmp, "A5 ") &&
              rig.dbg->mmu_slots()[0].nr_page == 5 && !rig.dbg->mmu_slots()[0].is_rom &&
              oor == "ERROR. Out of range\ncommand> " &&
              one == "ERROR. Needs two parameters\ncommand> ",
          esc(gmp));

    const std::string s4  = c.cmd("tbblue-set-sprite 0 10 20 30 40");
    const std::string g4  = c.cmd("tbblue-get-sprite 0");
    const std::string s5  = c.cmd("tbblue-set-sprite 1 1 2 3 64 5");
    const std::string g5  = c.cmd("tbblue-get-sprite 1");
    const std::string gw  = c.cmd("tbblue-get-sprite 127 2");
    const std::string g128 = c.cmd("tbblue-get-sprite 128");
    const std::string s6  = c.cmd("tbblue-set-sprite 2 1 2 3 4 5 6");
    const auto        a0  = rig.dbg->sprite_attr_raw(0).value;
    check("ZRCP-TBB-03", "sprites: [T4]'s set 0 10 20 30 40 then get = \"0A 14 1E 28 \\n\\n\"; a "
                         "fifth byte is shown when attribute 3 bit 6 is set; 127 2 wraps to 0; "
                         "128 and six values are refused",
          s4 == "\ncommand> " && g4 == "0A 14 1E 28 \n\ncommand> " && a0[0] == 10 &&
              a0[3] == 40 && s5 == "\ncommand> " && g5 == "01 02 03 40 05 \n\ncommand> " &&
              starts_with(gw.substr(gw.find('\n') + 1), "0A 14 1E 28 \n") &&
              g128 == "ERROR. Out of range\ncommand> " &&
              s6 == "Error. At most 5 attribute bytes\ncommand> ",
          esc(g4) + " / " + esc(g5) + " / " + esc(gw));

    const std::string sp  = c.cmd("tbblue-set-pattern 1 1 2 3");
    const std::string g8  = c.cmd("tbblue-get-pattern 1 8");
    const std::string g4b = c.cmd("tbblue-get-pattern 2 4 1");
    const std::string bpp = c.cmd("tbblue-get-pattern 0 5");
    const std::string o8  = c.cmd("tbblue-get-pattern 64 8");
    const std::string ok4 = c.cmd("tbblue-get-pattern 127 4");
    const std::string o4  = c.cmd("tbblue-get-pattern 128 4");
    const auto        ram = rig.dbg->pattern_ram();
    check("ZRCP-TBB-04", "patterns: set-pattern writes pattern RAM at index*256; an 8-bpp get is "
                         "256 values, a 4-bpp get 128 bytes from index*128 (so 4-bpp 2 is 8-bpp "
                         "1's first half); bpp 5, 8-bpp 64 and 4-bpp 128 are refused",
          sp == "\ncommand> " && ram.data[256] == 1 && ram.data[258] == 3 &&
              starts_with(g8, "01 02 03 ") && g8.size() == 256 * 3 + 2 + 9 &&
              starts_with(g4b, "01 02 03 ") && g4b.size() == 128 * 3 + 2 + 9 &&
              bpp == "ERROR. Invalid value for bpp: 5\ncommand> " &&
              o8 == "ERROR. Out of range\ncommand> " && ok4.size() == 128 * 3 + 2 + 9 &&
              o4 == "ERROR. Out of range\ncommand> ",
          esc(g8, 40) + " / " + esc(bpp));

    const std::string spal = c.cmd("tbblue-set-palette sprite first 255 1FFH");
    const std::string gpal = c.cmd("tbblue-get-palette sprite first 255 1");
    const auto        pal  = rig.dbg->palette(jnext::dbg::PaletteId::SpriteFirst);
    char              w0[8];
    std::snprintf(w0, sizeof(w0), "%03X ", pal[0] & 0x1FF);
    const std::string wrap = c.cmd("tbblue-get-palette sprite first 255 2");
    const std::string unk  = c.cmd("tbblue-get-palette tilemap first 0");
    const std::string few  = c.cmd("tbblue-get-palette sprite first");
    const std::string ten  = c.cmd("tbblue-set-palette ula second 0 200H");
    check("ZRCP-TBB-05", "palettes: 9-bit %03X values; set writes the bank; 255 2 wraps to entry "
                         "0; an unknown palette, two parameters and a 10-bit value are refused",
          spal == "\ncommand> " && gpal == "1FF \ncommand> " && pal[255] == 0x1FF &&
              wrap == reply_of(std::string("1FF ") + w0) &&
              unk == "ERROR. Unknown palette\ncommand> " &&
              few == "ERROR. Needs three parameter minimum\ncommand> " &&
              ten == "Error. Invalid 9-bit colour: 200H\ncommand> ",
          esc(gpal) + " / " + esc(wrap));

    const std::string csp = c.cmd("tbblue-get-clipwindow sprite");
    // A guest write first advances Layer 2's rotating index to Y1, so only a
    // set that resets the index (NR 0x1C) lands X1 X2 Y1 Y2 where they belong.
    c.cmd("tbblue-set-register 24 9");
    const std::string set2 = c.cmd("tbblue-set-clipwindow layer2 1 2 3 4");
    const std::string cl2 = c.cmd("tbblue-get-clipwindow layer2");
    const auto        w   = rig.dbg->clip_window(jnext::dbg::ClipLayer::Layer2);
    const std::string cun = c.cmd("tbblue-get-clipwindow copper");
    const std::string c0  = c.cmd("tbblue-get-clipwindow");
    const std::string c4  = c.cmd("tbblue-set-clipwindow ula 1 2 3");
    check("ZRCP-TBB-06", "clip windows: the sprite window at reset is [T1]'s \"0 255 0 191 \"; "
                         "set layer2 1 2 3 4 goes through NR 0x1C + NR 0x18 into the live "
                         "window even with the index advanced by a guest write; unknown, "
                         "missing and short forms are ZEsarUX's errors",
          csp == "0 255 0 191 \ncommand> " && set2 == "\ncommand> " &&
              cl2 == "1 2 3 4 \ncommand> " && w.x1 == 1 && w.x2 == 2 && w.y1 == 3 && w.y2 == 4 &&
              cun == "ERROR. Unknown clip window\ncommand> " &&
              c0 == "ERROR. Needs one parameter\ncommand> " &&
              c4 == "ERROR. Needs five parameters\ncommand> ",
          esc(csp) + " / " + esc(cl2));

    const std::string wp  = c.cmd("write-port 254 2");
    const std::string wp1 = c.cmd("write-port 254");
    std::string       wpr;
    {
        RzxOn rzx(rig.emu);
        wpr = c.cmd("write-port 254 3");
    }
    check("ZRCP-PORT-01", "write-port 254 2 is a guest OUT: the border is 2; one parameter is "
                          "ZEsarUX's error; under an RZX the refusal is said and the border "
                          "stays",
          wp == "\ncommand> " && rig.dbg->ula_screen_regs().border == 2 &&
              wp1 == "ERROR. Needs two parameters\ncommand> " &&
              wpr == "Error. write-port refused: refused_rzx\ncommand> " &&
              rig.dbg->ula_screen_regs().border == 2,
          esc(wpr));
}

// ===========================================================================
// WP-3 — control and the run state machine
// ===========================================================================

/// The register line + TSTATES + the disassembly at PC, as a stop or a step
/// reply carries it, without a `fired` line.
static bool is_stop_shape(const std::string& r, std::uint16_t pc, bool step = true) {
    char head[16];
    std::snprintf(head, sizeof(head), "PC=%04x ", pc);
    char dis[16];
    std::snprintf(dis, sizeof(dis), "\n  %04X ", pc);
    return starts_with(r, head) && r.find(" TSTATES: ") != std::string::npos &&
           r.find(dis) != std::string::npos && ends_with(r, step ? PROMPT_STEP : PROMPT) &&
           r.find("Breakpoint fired") == std::string::npos &&
           std::count(r.begin(), r.end(), '>') == 1;
}

static bool zrcp_transient_left(Rig& rig) {
    for (const auto& s : rig.dbg->subscriptions(true))
        if (s.transient) return true;
    return false;
}

static void control_rows() {
    {
        Rig rig;
        Zc  c(rig);
        const bool        was_running = !rig.dbg->state().paused;
        const std::string e1          = c.cmd("enter-cpu-step");
        const auto        st          = rig.dbg->state();
        const std::string e2          = c.cmd("encs");
        check("ZRCP-CTL-01", "enter-cpu-step pauses a running machine (User, this client) and the "
                             "prompt becomes command@cpu-step> ([T2]); a second one is the same",
              was_running && e1 == "\ncommand@cpu-step> " && st.paused &&
                  st.pause_reason.kind == PauseReason::Kind::User && e2 == e1 &&
                  rig.dbg->state().paused,
              esc(e1));
        const std::string x1 = c.cmd("exit-cpu-step");
        const bool        running = !rig.dbg->state().paused;
        const std::string x2 = c.cmd("ecs");
        check("ZRCP-CTL-02", "exit-cpu-step resumes the machine and restores command> ; a "
                             "second one is ZEsarUX's error ([T3])",
              x1 == "\ncommand> " && running &&
                  x2 == "Error. You are not in step to step mode\ncommand> ",
              esc(x2));
    }
    {
        Rig rig;
        rig.load({0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        const std::string out  = c.cmd("cpu-step");
        c.cmd("enter-cpu-step");
        const std::string step = c.cmd("cs");
        check("ZRCP-CTL-03", "cpu-step outside step mode is ZEsarUX's error; inside, the machine "
                             "executes exactly one instruction and the reply is the register "
                             "line + TSTATES + the disassembly at the new PC, no fired line ([T3])",
              out == "Error. You must first enter cpu-step mode\ncommand> " &&
                  rig.pc() == PROG + 1 && is_stop_shape(step, PROG + 1) &&
                  ends_with(step, "\n  8001 NOP\ncommand@cpu-step> "),
              esc(step));

        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->run(other);
        const bool        resumed = !rig.dbg->state().paused;
        const std::string s2      = c.cmd("cpu-step");
        check("ZRCP-CTL-04", "a cpu-step after another client resumed the machine pauses it "
                             "first: still exactly one instruction, then paused (§4.4)",
              resumed && rig.pc() == PROG + 2 && rig.dbg->state().paused &&
                  is_stop_shape(s2, PROG + 2),
              esc(s2));
        rig.dbg->detach(other);
    }
    {
        // cpu-step-over across a CALL: asynchronous — the subroutine runs in the
        // loop owner's frames and the reply is the stop at the next instruction.
        Rig rig;
        rig.load({0xCD, 0x00, 0x90, 0x18, 0xFE});
        rig.load({0x3E, 0x42, 0x32, 0x00, 0xA0, 0xC9}, 0x9000);
        rig.load({0xCD, 0x00, 0x90, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string now = c.send_once("cpu-step-over\n");
        const bool        running = !rig.dbg->state().paused;
        const std::string r = c.wait_ticks(8);
        check("ZRCP-CTL-05", "cpu-step-over at a CALL answers nothing at once (the machine runs), "
                             "then stops at the instruction after it with the subroutine run (A000 "
                             "= 42), as a plain stop — no fired line for its own target",
              now.empty() && running && rig.dbg->state().paused && rig.pc() == PROG + 3 &&
                  peek(rig, 0xA000, 1)[0] == 0x42 && is_stop_shape(r, PROG + 3) &&
                  !zrcp_transient_left(rig),
              esc(now) + " / " + esc(r));
    }
    {
        Rig rig;
        rig.emu.mmu().write(0xFF00, 0x34);
        rig.emu.mmu().write(0xFF01, 0x12);
        rig.load({0xC9});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string ret = c.send_once("cpu-step-over\n");
        const std::uint16_t after_ret = rig.pc();
        rig.load({0xC3, 0x78, 0x56});
        const std::string jp = c.send_once("cso\n");
        check("ZRCP-CTL-06", "cpu-step-over on a RET or a JP is a plain step, answered in the "
                             "same pump (ZEsarUX: nothing to run to)",
              after_ret == 0x1234 && is_stop_shape(ret, 0x1234) && rig.pc() == 0x5678 &&
                  is_stop_shape(jp, 0x5678),
              esc(ret) + " / " + esc(jp));
    }
    {
        // JR $: the step-over target is never reached. ZEsarUX hangs there;
        // jnext stops on data (§10), discards the line, and leaves no target.
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("cpu-step-over\n");
        for (int i = 0; i < 4; ++i) rig.tick();
        const std::string quiet = c.p->take();
        const bool        armed = zrcp_transient_left(rig) && !rig.dbg->state().paused;
        const std::string stop  = c.send_once("\n");
        rig.pump();
        const std::string more = c.p->take();
        check("ZRCP-CTL-07", "cpu-step-over on JR $ never reaches its target: silent while the "
                             "machine runs, then any data stops it with a plain reply, the line "
                             "is not executed, and its target is removed",
              quiet.empty() && armed && rig.dbg->state().paused && is_stop_shape(stop, PROG) &&
                  more.empty() && !zrcp_transient_left(rig),
              esc(stop) + " / " + esc(more));
    }
    {
        // A CORRUPT machine refuses every execute verb (CTL-11).
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const auto                      other = rig.dbg->attach({"loader", ClientKind::Test}).value;
        const std::vector<std::uint8_t> junk(64, 0x5A);
        const Result latched = rig.dbg->load_state_bytes(other, junk.data(), junk.size());
        const std::uint16_t pc0   = rig.pc();
        const std::string   step  = c.cmd("cpu-step");
        const std::string   exitc = c.cmd("exit-cpu-step");
        check("ZRCP-CTL-08", "on a corrupt machine cpu-step and exit-cpu-step are refused with the "
                             "corruption text; nothing executes and step mode stays (the prompt "
                             "tells the truth)",
              latched != Result::Ok && rig.pc() == pc0 &&
                  step == "Error. Machine state is corrupt after a failed rewind; acknowledge it "
                          "in the jnext debugger\ncommand@cpu-step> " &&
                  exitc == step && rig.dbg->state().paused,
              "latched=" + std::string(jnext::dbg::result_name(latched)) + " " + esc(step));
        rig.dbg->detach(other);
    }
    {
        Rig rig;
        rig.load({0xED, 0xFF, 0x18, 0xFE});
        rig.dbg->set_magic_breakpoint(true);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string s = c.cmd("cpu-step");
        check("ZRCP-CTL-09", "cpu-step over the magic opcode: the machine stops after it and the "
                             "reply is a step's, with no fired line (ZEsarUX names nothing after "
                             "a step)",
              rig.pc() == PROG + 2 && rig.dbg->state().paused && is_stop_shape(s, PROG + 2),
              esc(s));
    }
}

static const std::string kRunning =
    "Running until a breakpoint, key press or data sent, menu opening or other event\n";

static void run_rows() {
    {
        Rig rig;
        Zc  c(rig);
        const std::string r = c.cmd("run");
        check("ZRCP-RUN-01", "run outside step mode: the first line, then ZEsarUX's error, and "
                             "nothing runs differently ([T2] minus its stray CR, §10)",
              r == kRunning + "Error. You must first enter cpu-step mode\ncommand> ", esc(r));
    }
    {
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0xED, 0xFF, 0x18, 0xFE});
        rig.dbg->set_magic_breakpoint(true);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string first   = c.send_once("run\n");
        const bool        running = !rig.dbg->state().paused;
        const std::string stop    = c.wait_ticks(8);
        const auto        st      = rig.dbg->state();
        check("ZRCP-RUN-02", "run: \"Running until…\\n\" and NO prompt at once (DeZog asserts it "
                             "is the whole first reply), the machine running; the magic opcode "
                             "stops it: \"Breakpoint fired: Magic breakpoint\\n\" + the stop at "
                             "8005, and the machine IS stopped there",
              first == kRunning && running && st.paused &&
                  st.pause_reason.kind == PauseReason::Kind::Magic && rig.pc() == PROG + 5 &&
                  starts_with(stop, "Breakpoint fired: Magic breakpoint\nPC=8005 ") &&
                  ends_with(stop, " TSTATES: " + std::to_string(rig.dbg->time().cycle_in_frame / 8) +
                                      "\n  8005 JR 8005\ncommand@cpu-step> "),
              esc(first) + " / " + esc(stop));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        rig.tick();
        const std::string quiet = c.p->take();
        const bool        running = !rig.dbg->state().paused;
        const std::string stop  = c.send_once("\n");
        rig.pump();
        rig.pump();
        const std::string more = c.p->take();
        check("ZRCP-RUN-03", "run then a bare newline (DeZog's pause): one stop reply, no fired "
                             "line, exactly one prompt; the newline is not executed as a command "
                             "(no second prompt); the machine is stopped ([T3])",
              quiet.empty() && running && rig.dbg->state().paused && is_stop_shape(stop, PROG) &&
                  more.empty(),
              esc(stop) + " / " + esc(more));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const std::string stop = c.send_once("get-registers\n");
        rig.pump();
        const std::string more = c.p->take();
        const std::string next = c.cmd("about");
        check("ZRCP-RUN-04", "a command sent during a run stops it and is discarded, not executed: "
                             "one stop reply, no register reply after it ([T3]); the next line is "
                             "a command again",
              is_stop_shape(stop, PROG) && more.empty() &&
                  next == "jnext ZRCP remote command protocol\ncommand@cpu-step> ",
              esc(stop) + " / " + esc(more));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const std::string stop = c.send_once("get-reg");
        c.p->send("isters\nabout\n");
        const std::string next = c.wait();
        check("ZRCP-RUN-05", "a line split across the stop is discarded whole — up to its newline "
                             "— and only the command after it is executed",
              is_stop_shape(stop, PROG) &&
                  next == "jnext ZRCP remote command protocol\ncommand@cpu-step> ",
              esc(stop) + " / " + esc(next));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->pause(other);
        rig.pump();
        const std::string stop = c.p->take();
        check("ZRCP-RUN-06", "another client's pause during this client's run ends it with a "
                             "plain stop reply, in the same pump (§4.4)",
              is_stop_shape(stop, PROG) && rig.dbg->state().pause_reason.by == other,
              esc(stop));
        rig.dbg->detach(other);
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const std::string both = c.send_once("run\nabout\n");
        rig.pump();
        const std::string more = c.p->take();
        check("ZRCP-RUN-07", "\"run\\nabout\\n\" in one send: the pipelined line is data sent "
                             "during the run — it stops it and is never answered",
              starts_with(both + more, kRunning) && is_stop_shape((both + more).substr(kRunning.size()), PROG) &&
                  (both + more).find("jnext ZRCP") == std::string::npos,
              esc(both + more));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const auto                      other = rig.dbg->attach({"loader", ClientKind::Test}).value;
        const std::vector<std::uint8_t> junk(64, 0x5A);
        rig.dbg->load_state_bytes(other, junk.data(), junk.size());
        const std::string r    = c.send_once("run\nabout\n");
        const std::size_t cut  = r.find(PROMPT_STEP);
        const std::string stop = cut == std::string::npos ? r : r.substr(0, cut + std::strlen(PROMPT_STEP));
        const std::string next = cut == std::string::npos ? "" : r.substr(cut + std::strlen(PROMPT_STEP));
        check("ZRCP-RUN-08", "a run the backend refuses (a corrupt machine) is answered in its "
                             "own pass with the first line and the stop, fired \"Machine corrupt "
                             "after failed rewind\"; the machine stays paused, and a line "
                             "pipelined after it is a command again, not an interrupt",
              starts_with(stop, kRunning + "Breakpoint fired: Machine corrupt after failed rewind\n") &&
                  next == "jnext ZRCP remote command protocol\ncommand@cpu-step> " &&
                  rig.dbg->state().paused,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const std::uint16_t pc0 = rig.pc();
        const std::string v  = c.cmd("run verbose");
        const std::string ns = c.cmd("run no-stop-on-data");
        const std::string ui = c.cmd("run 10 update-immediately");
        const std::string big = c.cmd("run 1000001");
        const std::string bad = c.cmd("run 0x10");
        check("ZRCP-RUN-09", "run verbose / no-stop-on-data / update-immediately are declined by "
                             "name; a limit past 1000000 and a bad number are refused; nothing ran",
              v == "Error. Unsupported in jnext: run verbose\ncommand@cpu-step> " &&
                  ns == "Error. Unsupported in jnext: no-stop-on-data\ncommand@cpu-step> " &&
                  ui == "Error. Unsupported in jnext: update-immediately\ncommand@cpu-step> " &&
                  big == "Error. Unsupported in jnext: a run limit above 1000000\ncommand@cpu-step> " &&
                  bad == "Error. Invalid run limit: 0x10\ncommand@cpu-step> " &&
                  rig.dbg->state().paused && rig.pc() == pc0,
              esc(big));
    }
}

static void run_edge_rows() {
    {
        // §4.4: another client's pause while this client is idle at the prompt
        // sends nothing, and the prompt keeps the client's own belief.
        Rig rig;
        Zc  c(rig);
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->pause(other);
        rig.pump();
        rig.pump();
        const std::string unsolicited = c.p->take();
        const std::string prompt      = c.cmd("noop");
        check("ZRCP-RUN-16", "another client's pause while this client is idle sends nothing, "
                             "and the prompt stays command> (step mode is the client's belief, "
                             "§4.4)",
              unsolicited.empty() && prompt == "\ncommand> " && rig.dbg->state().paused,
              esc(unsolicited));
        rig.dbg->detach(other);
    }
    {
        // §4.4: resumed by someone else while in step mode — `run` then simply
        // stays attached to the running machine, and its reply arrives with the
        // next stop.
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->run(other);
        const std::string head    = c.send_once("run\n");
        rig.tick();
        const std::string quiet   = c.p->take();
        const bool        running = !rig.dbg->state().paused;
        rig.dbg->pause(other);
        rig.pump();
        const std::string stop = c.p->take();
        check("ZRCP-RUN-17", "run on a machine another client already resumed: the first line, "
                             "silence while it runs, and the reply at the next stop (§4.4)",
              head == kRunning && quiet.empty() && running && is_stop_shape(stop, PROG),
              esc(head) + " / " + esc(stop));
        rig.dbg->detach(other);
    }
    {
        // A stop caused INSIDE a pump — here a DZRP client's CMD_PAUSE served
        // after the ZRCP service in the same drain — is answered in that same
        // pump, by on_notify: "a stop in this tick is notified in this tick".
        Rig  rig;
        auto dz  = std::make_unique<jnext::remote::dzrp::DzrpServer>(*rig.dbg);
        auto dzl = std::make_unique<FakeListener>();
        auto* dzlp = dzl.get();
        dz->server().open(std::move(dzl), "127.0.0.1", 0);
        rig.dbg->add_service(dz->server());
        auto d = dzlp->connect();
        rig.pump();
        // CMD_INIT (1), seq 1: version 2.2.0 + an empty name.
        d->send(std::string("\x04\x00\x00\x00\x01\x01\x02\x02\x00\x00", 10));
        rig.pump();
        d->take();
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const bool running = !rig.dbg->state().paused;
        // CMD_PAUSE (7), seq 2, no payload.
        d->send(std::string("\x00\x00\x00\x00\x02\x07", 6));
        rig.pump();
        const std::string stop = c.p->take();
        check("ZRCP-RUN-18", "a stop another service causes inside the same pump (a DZRP "
                             "CMD_PAUSE) is answered in that pump, after its drain",
              running && rig.dbg->state().paused && is_stop_shape(stop, PROG), esc(stop));
        rig.dbg->remove_service(dz->server());
    }
}

/// NOPs from PROG, then JR $ — room for a counted run.
static void load_nops(Rig& rig, int n) {
    std::vector<std::uint8_t> code(static_cast<std::size_t>(n), 0x00);
    code.push_back(0x18);
    code.push_back(0xFE);
    rig.load(code);
}

static void run_limit_rows() {
    {
        Rig rig;
        load_nops(rig, 10);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 5");
        check("ZRCP-RUN-10", "run 5 executes exactly five instructions: \"…, 5 opcodes run, or "
                             "other event\\nReturning after 5 opcodes\\n\" + the stop ([T3]) at "
                             "8005",
              starts_with(r, "Running until a breakpoint, key press or data sent, menu opening, "
                             "5 opcodes run, or other event\nReturning after 5 opcodes\nPC=8005 ") &&
                  rig.pc() == PROG + 5 && rig.dbg->state().paused &&
                  is_stop_shape(r.substr(r.find("PC=")), PROG + 5),
              esc(r));
    }
    {
        // THE BUDGET: with a clock that moves 1 ms a read, one slice is two
        // steps. On the running budget (one service call per pump) a run 100
        // therefore parks after each slice — never one pump for the lot.
        Rig rig;
        load_nops(rig, 100);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        g_clock_ticks = true;
        c.p->send("run 100\n");
        rig.dbg->pump(PumpBudget{});
        const std::string head  = c.p->take();
        const std::uint16_t pc1 = rig.pc();
        rig.dbg->pump(PumpBudget{});
        const std::string quiet = c.p->take();
        const std::uint16_t pc2 = rig.pc();
        std::string rest;
        for (int i = 0; i < 200 && !Zc::has_prompt(rest); ++i) {
            rig.dbg->pump(PumpBudget{});
            rest += c.p->take();
        }
        g_clock_ticks = false;
        check("ZRCP-RUN-11", "run 100 is parked between slices: the command's pass runs nothing, "
                             "the next pass a slice (two steps on the ticking clock) and no reply; "
                             "the run completes at 8064 over later pumps",
              starts_with(head, "Running until") && !Zc::has_prompt(head) && pc1 == PROG &&
                  quiet.empty() && pc2 == PROG + 2 && rig.pc() == PROG + 100 &&
                  starts_with(rest, "Returning after 100 opcodes\nPC=8064 "),
              "pc1=" + std::to_string(pc1) + " pc2=" + std::to_string(pc2) + " " + esc(rest));
    }
    {
        Rig rig;
        load_nops(rig, 100);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        g_clock_ticks = true;
        c.p->send("run 100\n");
        rig.dbg->pump(PumpBudget{});
        rig.dbg->pump(PumpBudget{});
        c.p->take();
        c.p->send("\n");
        rig.dbg->pump(PumpBudget{});
        const std::string stop = c.p->take();
        g_clock_ticks = false;
        rig.pump();
        const std::string more = c.p->take();
        check("ZRCP-RUN-12", "data sent during run 100 stops it where it is: a plain stop, no "
                             "\"Returning after\", the newline not executed",
              rig.pc() == PROG + 2 && is_stop_shape(stop, PROG + 2) && more.empty() &&
                  stop.find("Returning") == std::string::npos,
              esc(stop));
    }
    {
        // REQ-zrcp-05: an event inside a step is the step's reason, so a
        // counted run ends on it — the magic opcode with its fired line.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0xED, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        rig.dbg->set_magic_breakpoint(true);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 8");
        check("ZRCP-RUN-13", "run 8 over the magic opcode stops right after it (four "
                             "instructions): \"Breakpoint fired: Magic breakpoint\", no "
                             "\"Returning after\" (REQ-zrcp-05)",
              rig.pc() == PROG + 5 &&
                  rig.dbg->state().pause_reason.kind == PauseReason::Kind::Magic &&
                  r.find("\nBreakpoint fired: Magic breakpoint\nPC=8005 ") != std::string::npos &&
                  r.find("Returning") == std::string::npos,
              esc(r));
    }
    {
        // Another client's watch, hit inside a step, ends the count too — a
        // plain stop: it is not this client's to name.
        Rig rig;
        rig.load({0x3E, 0x01, 0x32, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        jnext::dbg::Subscription w;
        w.kind      = jnext::dbg::EventKind::Mem;
        w.filter.lo = w.filter.hi = 0x9000;
        w.access    = jnext::dbg::Access::Write;
        w.action    = jnext::dbg::Action::Stop;
        rig.dbg->subscribe(other, w);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 8");
        check("ZRCP-RUN-14", "run 8 ends at the step whose write another client watches (after "
                             "LD (9000),A, at 8005), as a plain stop; the reason is the Watch "
                             "(REQ-zrcp-05)",
              rig.pc() == PROG + 5 &&
                  rig.dbg->state().pause_reason.kind == PauseReason::Kind::Watch &&
                  is_stop_shape(r.substr(r.find("PC=")), PROG + 5) &&
                  r.find("Returning") == std::string::npos,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        // A step that LANDS on an armed Execute ends the count there, before
        // it runs — the GH #221 step-off would skip it on the next resume.
        Rig rig;
        load_nops(rig, 10);
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        jnext::dbg::Subscription bp;
        bp.kind      = jnext::dbg::EventKind::Execute;
        bp.filter.lo = bp.filter.hi = PROG + 3;
        bp.action    = jnext::dbg::Action::Stop;
        rig.dbg->subscribe(other, bp);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 8");
        check("ZRCP-RUN-15", "run 8 stops on landing at 8003, where another client has an "
                             "Execute breakpoint (probe_execute), with 8003 not yet executed",
              rig.pc() == PROG + 3 && is_stop_shape(r.substr(r.find("PC=")), PROG + 3) &&
                  r.find("Returning") == std::string::npos,
              esc(r));
        rig.dbg->detach(other);
    }
}

/// A service registered after the ZRCP one that hard-resets the machine from
/// inside the pump's drain, as another client's command would — once.
struct ResetService final : jnext::dbg::Service {
    Debugger&            dbg;
    jnext::dbg::ClientId by;
    bool                 armed = false;
    ResetService(Debugger& d, jnext::dbg::ClientId c) : dbg(d), by(c) {}
    jnext::dbg::ServiceStep service_once(int) override {
        if (!armed) return jnext::dbg::ServiceStep::Idle;
        armed = false;
        dbg.reset(by, jnext::dbg::ResetKind::Hard);
        return jnext::dbg::ServiceStep::Serviced;
    }
    void flush_notifications() override {}
    bool peer_connected() const override { return false; }
};

static void reset_rows() {
    {
        Rig rig;
        int boots = 0;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig, &boots]() {
            ++boots;
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r    = c.cmd("hard-reset-cpu");
        const std::string regs = c.cmd("get-registers");
        check("ZRCP-RST-01", "hard-reset-cpu in step mode: the cold boot runs before the empty "
                             "reply, the machine is still paused, at PC 0000, and the next "
                             "command sees it (§4.6)",
              boots == 1 && r == "\ncommand@cpu-step> " && rig.dbg->state().paused &&
                  rig.pc() == 0x0000 && starts_with(regs, "PC=0000 "),
              esc(r) + " / " + esc(regs));
    }
    {
        Rig rig;
        Zc  c(rig);
        const std::string r = c.cmd("hard-reset-cpu");
        check("ZRCP-RST-02", "with no cold-boot driver registered: \"Error. Unsupported in "
                             "jnext: hard-reset-cpu\" — never a silent empty reply (§4.6 rule 6)",
              r == "Error. Unsupported in jnext: hard-reset-cpu\ncommand> ", esc(r));
    }
    {
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const auto   other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        const Result reset = rig.dbg->reset(other, jnext::dbg::ResetKind::Hard);
        rig.pump();
        const std::string r       = c.p->take();
        const bool        running = !rig.dbg->state().paused;
        check("ZRCP-RST-03", "another client's hard reset during this client's run completes the "
                             "run reply from the Reset{Hard} event — a plain stop with the fresh "
                             "machine at 0000 — and the machine is NOT paused (§4.6 rule 4)",
              reset == Result::Ok && running && is_stop_shape(r, 0x0000), esc(r));
        rig.dbg->detach(other);
    }
    {
        // A cpu-step-over in flight across another client's hard reset: the
        // reply is completed from the event, and its target — which would
        // otherwise be re-applied onto the new machine and stop it with
        // nobody waiting — is removed.
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("cpu-step-over\n");
        rig.tick();
        const bool   armed = zrcp_transient_left(rig);
        const auto   other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->reset(other, jnext::dbg::ResetKind::Hard);
        rig.pump();
        const std::string r = c.p->take();
        check("ZRCP-RST-05", "a cpu-step-over in flight across another client's hard reset is "
                             "answered from the event (a plain stop at 0000) and leaves no "
                             "target behind on the new machine",
              armed && is_stop_shape(r, 0x0000) && !zrcp_transient_left(rig) &&
                  !rig.dbg->state().paused,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        // A line that arrives after a reset answered the run but before that
        // answer went out was sent while the client still waited on the run:
        // it is an interrupt, discarded, and the reply is the reset's — with
        // the machine still running (a reset is never a pause).
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->reset(other, jnext::dbg::ResetKind::Hard);
        c.p->send("about\n");
        rig.pump();
        rig.pump();
        const std::string r = c.p->take();
        check("ZRCP-RST-06", "a line sent before the reset's run reply went out is an interrupt: "
                             "discarded (never answered), the reply is the reset stop at 0000, "
                             "and the machine is not paused",
              is_stop_shape(r, 0x0000) && r.find("jnext ZRCP") == std::string::npos &&
                  !rig.dbg->state().paused,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        // A reset made INSIDE a pump, by a service drained after this one, is
        // answered in that same pump (on_notify), not one tick later.
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const auto   other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        ResetService rs(*rig.dbg, other);
        rig.dbg->add_service(rs);
        rs.armed = true;
        rig.dbg->pump(PumpBudget{});
        const std::string r = c.p->take();
        rig.dbg->remove_service(rs);
        check("ZRCP-RST-09", "a hard reset another service makes inside the pump's drain is "
                             "answered in that same pump, after the drain",
              !rs.armed && is_stop_shape(r, 0x0000) && !rig.dbg->state().paused, esc(r));
        rig.dbg->detach(other);
    }
    {
        // A PARKED run n answered by a reset steps nothing more: the new
        // machine stays exactly where the reset left it (paused at 0000 —
        // it was paused, and a paused machine stays paused, rule 3).
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        load_nops(rig, 100);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        g_clock_ticks = true;
        c.p->send("run 100\n");
        rig.dbg->pump(PumpBudget{});
        rig.dbg->pump(PumpBudget{});
        c.p->take();
        const bool parked = rig.pc() == PROG + 2;
        const auto other  = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->reset(other, jnext::dbg::ResetKind::Hard);
        rig.dbg->pump(PumpBudget{});
        g_clock_ticks = false;
        const std::string r = c.p->take();
        check("ZRCP-RST-07", "a run n parked between slices and answered by another client's hard "
                             "reset steps the new machine no further: the reply is the plain stop "
                             "at 0000 and PC is still 0000",
              parked && is_stop_shape(r, 0x0000) && rig.pc() == 0x0000 &&
                  r.find("Returning") == std::string::npos,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        // get-tstates-partial counts the NEW machine after hard-reset-cpu:
        // the base set before it is dropped, not subtracted from a counter
        // that restarted under it.
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        load_nops(rig, 8);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("run 3");
        c.cmd("reset-tstates-partial");
        c.cmd("hard-reset-cpu");
        c.cmd("write-memory-raw 32768 " + std::string(80, '0') + "18FE");
        c.cmd("set-register PC=8000H");
        c.cmd("run 30");
        char want[16];
        std::snprintf(want, sizeof(want), "%09llu",
                      static_cast<unsigned long long>(rig.dbg->time().tstates_total));
        const std::string p = c.cmd("get-tstates-partial");
        check("ZRCP-RST-08", "after hard-reset-cpu the partial count is the new machine's own "
                             "(the base taken before the reset no longer applies)",
              p == reply_of(want, true) && rig.dbg->time().tstates_total >= 120, esc(p));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("reset-cpu");
        check("ZRCP-RST-04", "reset-cpu is the soft reset: empty reply, PC 0000, step mode and "
                             "the pause kept ([T3])",
              r == "\ncommand@cpu-step> " && rig.pc() == 0x0000 && rig.dbg->state().paused,
              esc(r));
    }
}

static void nmi_rows() {
    // The Multiface NMI button behind NR 0x06 bit 3 (zxnext.vhd:2090, 6348):
    // with the gate open, generate-nmi is taken at the next boundaries and the
    // CPU goes to 0066H, as ZEsarUX's [T4] generate-nmi + cpu-step did.
    auto steps_after = [](bool press, std::string& reply) {
        Rig rig(MachineType::ZXN_ISSUE2);
        load_nops(rig, 16);
        Zc c(rig);
        c.cmd("tbblue-set-register 6 8");
        c.cmd("enter-cpu-step");
        if (press) reply = c.cmd("generate-nmi");
        bool at_0066 = false;
        for (int i = 0; i < 4 && !at_0066; ++i) {
            c.cmd("cpu-step");
            at_0066 = rig.pc() == 0x0066;
        }
        return at_0066;
    };
    std::string reply, unused;
    const bool with    = steps_after(true, reply);
    const bool without = steps_after(false, unused);
    check("ZRCP-NMI-01", "generate-nmi presses the Multiface NMI button: empty reply, and within "
                         "four steps the CPU is at 0066H — which the same steps without it never "
                         "reach",
          reply == "\ncommand@cpu-step> " && with && !without, esc(reply));
}

static void coexist_rows() {
    {
        // ALONGSIDE DZRP: two servers on one Debugger, one client each. A DZRP
        // CMD_LOOPBACK and a ZRCP command are both answered in one pump, and
        // the ZRCP client's enter-cpu-step is the machine both see.
        Rig  rig;
        auto dz  = std::make_unique<jnext::remote::dzrp::DzrpServer>(*rig.dbg);
        auto dzl = std::make_unique<FakeListener>();
        auto* dzlp = dzl.get();
        dz->server().open(std::move(dzl), "127.0.0.1", 0);
        rig.dbg->add_service(dz->server());
        Zc   z(rig);
        auto d = dzlp->connect();
        rig.pump();
        // CMD_LOOPBACK (15), seq 7, payload "hi": length counts the payload.
        d->send(std::string("\x02\x00\x00\x00\x07\x0Fhi", 8));
        z.p->send("enter-cpu-step\n");
        rig.pump();
        const std::string dr = d->take();
        const std::string zr = z.p->take();
        check("ZRCP-SES-06", "ZRCP alongside DZRP: both servers on one backend answer their own "
                             "client in the same pump (DZRP's loopback echoed, ZRCP's "
                             "enter-cpu-step pausing the shared machine)",
              dr == std::string("\x03\x00\x00\x00\x07hi", 7) && zr == "\ncommand@cpu-step> " &&
                  rig.dbg->state().paused,
              esc(dr) + " / " + esc(zr));
        rig.dbg->remove_service(dz->server());
    }
    {
        // A cpu-step-over in flight when the client hangs up: its target goes
        // with the client (SES-01), so it cannot stop the machine later.
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("cpu-step-over\n");
        const bool armed = zrcp_transient_left(rig);
        c.p->close();
        for (int i = 0; i < 4; ++i) rig.tick();
        check("ZRCP-SES-05", "a hang-up during cpu-step-over removes its target with the client "
                             "(SES-01): no transient is left to stop the machine",
              armed && !zrcp_transient_left(rig) && !rig.dbg->attached() &&
                  !rig.dbg->state().paused);
    }
}

// ===========================================================================
// Review round 1 — the legacy MMU= projection, the session reset, the
// cpu-step-over plain-step set, the tbblue name mappings, and the edges
// ===========================================================================

/// Eight slots of a legacy machine: ROM image `rom` at 0000, banks 5 and 2,
/// bank `top` at C000 — as jnext's MMU maps 16 K banks onto 8 K pages.
static std::array<jnext::dbg::SlotInfo, 8> legacy_slots(int rom, int top) {
    std::array<jnext::dbg::SlotInfo, 8> s{};
    const int banks[3] = {5, 2, top};
    for (int i = 0; i < 2; ++i) {
        s[static_cast<std::size_t>(i)].is_rom         = true;
        s[static_cast<std::size_t>(i)].effective_page = static_cast<std::uint8_t>(rom * 2 + i);
    }
    for (int b = 0; b < 3; ++b)
        for (int h = 0; h < 2; ++h) {
            auto& x          = s[static_cast<std::size_t>(2 + b * 2 + h)];
            x.nr_page        = static_cast<std::uint8_t>(banks[b] * 2 + h);
            x.effective_page = x.nr_page;
        }
    return s;
}

/// DeZog 3.7.4's 128K decoder (`DecodeZesaruxRegistersZx128k.parseSlots`):
/// the first four 4-digit fields of MMU=, a value >= 0x8000 is ROM 8 + (v & 1).
static std::vector<int> dezog_128k(const std::string& line) {
    std::vector<int> out;
    const std::size_t at = line.find("MMU=");
    if (at == std::string::npos) return out;
    for (int i = 0; i < 4; ++i) {
        const int v = std::stoi(line.substr(at + 4 + 4 * static_cast<std::size_t>(i), 4), nullptr, 16);
        out.push_back(v >= 0x8000 ? 8 + (v & 1) : v);
    }
    return out;
}

static void legacy_rows() {
    {
        Z80Registers r{};
        const std::string l128 = register_line(r, legacy_slots(0, 0), MachineType::ZX128K);
        const std::string l128r1 = register_line(r, legacy_slots(1, 3), MachineType::ZX128K);
        const std::string l48 = register_line(r, legacy_slots(0, 0), MachineType::ZX48K);
        auto all_ram = legacy_slots(0, 3);
        const int cfg[4] = {4, 7, 6, 3};
        for (int i = 0; i < 8; ++i) {
            all_ram[static_cast<std::size_t>(i)].is_rom = false;
            all_ram[static_cast<std::size_t>(i)].effective_page =
                static_cast<std::uint8_t>(cfg[i / 2] * 2 + (i & 1));
        }
        const std::string lp3 = register_line(r, all_ram, MachineType::ZX_PLUS3);
        const std::string lp3r = register_line(r, legacy_slots(3, 0), MachineType::ZX_PLUS3);
        check("ZRCP-FMT-11", "MMU= on the 48K / 128K / +3 is ZEsarUX's legacy form (mem128.c): "
                             "four 16K segments, ROM = 8000H+image, RAM = its bank, then four "
                             "0000 — and DeZog's 128K decoder reads [ROM0,5,2,0] and [ROM1,5,2,3] "
                             "from it",
              ends_with(l128, "MMU=80000005000200000000000000000000") &&
                  ends_with(l128r1, "MMU=80010005000200030000000000000000") &&
                  dezog_128k(l128) == std::vector<int>({8, 5, 2, 0}) &&
                  dezog_128k(l128r1) == std::vector<int>({9, 5, 2, 3}) &&
                  ends_with(l48, "MMU=80000005000200000000000000000000") &&
                  ends_with(lp3, "MMU=00040007000600030000000000000000") &&
                  ends_with(lp3r, "MMU=80030005000200000000000000000000"),
              esc(l128) + " / " + esc(lp3));

        const std::string p128  = memory_pages(legacy_slots(1, 7), false, MachineType::ZX128K);
        const std::string pp3   = memory_pages(all_ram, false, MachineType::ZX_PLUS3);
        const std::string p48   = memory_pages(legacy_slots(0, 0), false, MachineType::ZX48K);
        const std::string v128  = memory_pages(legacy_slots(0, 0), true, MachineType::ZX128K);
        const std::string v48   = memory_pages(legacy_slots(0, 0), true, MachineType::ZX48K);
        check("ZRCP-FMT-12", "get-memory-pages on the legacy machines is ZEsarUX's: 128K/+3 four "
                             "16K segments RO<rom> RA<bank> (%X), 48K the two fixed segments ROM "
                             "RAM; verbose spells each segment's span",
              p128 == "RO1 RA5 RA2 RA7 " && pp3 == "RA4 RA7 RA6 RA3 " && p48 == "ROM RAM " &&
                  starts_with(v128, "Segment 1\nLong name: ROM 0\nShort name: RO0\nStart: 0H\n"
                                    "End: 3FFFH\n\nSegment 2\nLong name: RAM 5\nShort name: RA5\n"
                                    "Start: 4000H\nEnd: 7FFFH\n\n") &&
                  v48 == "Segment 1\nLong name: System ROM\nShort name: ROM\nStart: 0H\nEnd: "
                         "3FFFH\n\nSegment 2\nLong name: System RAM\nShort name: RAM\nStart: "
                         "4000H\nEnd: FFFFH\n\n",
              esc(p128) + " / " + esc(v128, 120));
    }
    {
        // LIVE on the 128K machine: the paging port moves what DeZog decodes.
        Rig rig(MachineType::ZX128K);
        Zc  c(rig);
        const std::string r0 = c.cmd("get-registers");
        c.cmd("write-port 32765 19");   // 0x13: bank 3 at C000, ROM 1
        const std::string r1 = c.cmd("get-registers");
        const std::string pg = c.cmd("get-memory-pages");
        check("ZRCP-REG-06", "on a live 128K machine DeZog's 128K decoder reads [ROM0,5,2,0] from "
                             "get-registers, and [ROM1,5,2,3] after OUT 7FFDH 13H; "
                             "get-memory-pages says RO1 RA5 RA2 RA3",
              dezog_128k(r0) == std::vector<int>({8, 5, 2, 0}) &&
                  dezog_128k(r1) == std::vector<int>({9, 5, 2, 3}) &&
                  pg == "RO1 RA5 RA2 RA3 \ncommand> ",
              esc(r0) + " / " + esc(r1) + " / " + esc(pg));
    }
    {
        // LIVE on the +3: ROM 3 by both ROM bits, then the all-RAM 4 7 6 3 map.
        Rig rig(MachineType::ZX_PLUS3);
        Zc  c(rig);
        c.cmd("write-port 32765 16");   // 7FFD bit 4
        c.cmd("write-port 8189 4");     // 1FFD bit 2: ROM 3
        const std::string rom3 = c.cmd("get-memory-pages");
        c.cmd("write-port 8189 7");     // special paging, configuration 3
        const std::string ram  = c.cmd("get-memory-pages");
        const std::string regs = c.cmd("get-registers");
        check("ZRCP-REG-07", "on a live +3: ROM 3 reads RO3, and the all-RAM configuration 3 "
                             "reads RA4 RA7 RA6 RA3 with MMU=0004000700060003 and four 0000",
              rom3 == "RO3 RA5 RA2 RA0 \ncommand> " && ram == "RA4 RA7 RA6 RA3 \ncommand> " &&
                  regs.find("MMU=00040007000600030000000000000000\n") != std::string::npos,
              esc(rom3) + " / " + esc(ram));
    }
}

static void session_reset_rows() {
    // The server outlives its connections: what one client set must not be the
    // next client's (§4.5 "step_mode dies with the session").
    Rig rig;
    {
        Zc a(rig);
        a.cmd("set-cr");
        a.cmd("set-debug-settings 3");
        a.cmd("enter-cpu-step");
        a.p->send("quit\n");
        for (int i = 0; i < 4; ++i) rig.pump();
    }
    Zc b(rig);
    const std::string ds   = b.cmd("get-debug-settings");
    const std::string step = b.cmd("cpu-step");
    check("ZRCP-SES-07", "a new client starts clean after one that set set-cr, debug settings 3 "
                         "and step mode: plain command> prompts with no CR, debug settings 1, and "
                         "cpu-step refused until it enters step mode itself",
          ends_with(b.welcome, "\ncommand> ") && b.welcome.find('\r') == std::string::npos &&
              ds == "1\ncommand> " &&
              step == "Error. You must first enter cpu-step mode\ncommand> ",
          esc(ds) + " / " + esc(step));

    std::string ex, lo;
    for (const char* q : {"exit", "logout"}) {
        Rig r2;
        Zc  c(r2);
        c.p->send(std::string(q) + "\n");
        for (int i = 0; i < 4; ++i) r2.pump();
        (q[0] == 'e' ? ex : lo) = c.p->take() + (c.p->closed_by_server() ? "<closed>" : "");
    }
    check("ZRCP-SES-08", "quit's aliases exit and logout say goodbye and close too",
          ex == "Sayonara baby\n<closed>" && lo == "Sayonara baby\n<closed>", esc(ex) + " / " + esc(lo));
}

static void step_over_set_rows() {
    // §11.3 item 8: every instruction cpu-step-over treats as a plain step —
    // ZEsarUX's si_cpu_step_over_jpret() list, plus JP NZ (C2), JP (IX), JP (IY)
    // and RETI / RETN. Each must answer in the SAME pump with no frame run (a
    // synchronous step); one misclassified would become run_to(pc + len), an
    // address a jump never reaches.
    static const struct { const char* name; std::vector<std::uint8_t> code; } kCases[] = {
        {"JP nn", {0xC3, 0x34, 0x12}},     {"JP NZ", {0xC2, 0x34, 0x12}},
        {"JP Z", {0xCA, 0x34, 0x12}},      {"JP NC", {0xD2, 0x34, 0x12}},
        {"JP C", {0xDA, 0x34, 0x12}},      {"JP PO", {0xE2, 0x34, 0x12}},
        {"JP PE", {0xEA, 0x34, 0x12}},     {"JP P", {0xF2, 0x34, 0x12}},
        {"JP M", {0xFA, 0x34, 0x12}},      {"JP (HL)", {0xE9}},
        {"JP (IX)", {0xDD, 0xE9}},         {"JP (IY)", {0xFD, 0xE9}},
        {"RET", {0xC9}},                   {"RET NZ", {0xC0}},
        {"RET Z", {0xC8}},                 {"RET NC", {0xD0}},
        {"RET C", {0xD8}},                 {"RET PO", {0xE0}},
        {"RET PE", {0xE8}},                {"RET P", {0xF0}},
        {"RET M", {0xF8}},                 {"RETI", {0xED, 0x4D}},
        {"RETN", {0xED, 0x45}},
    };
    Rig rig;
    rig.emu.mmu().write(0xFF00, 0x34);
    rig.emu.mmu().write(0xFF01, 0x12);
    Zc c(rig);
    c.cmd("enter-cpu-step");
    std::string bad;
    for (const auto& k : kCases) {
        rig.load(k.code);
        Z80Registers r = rig.emu.cpu().get_registers();
        r.HL = 0x5678; r.IX = 0x6789; r.IY = 0x789A;
        rig.emu.cpu().set_registers(r);
        const std::string rep = c.send_once("cpu-step-over\n");
        if (!(rig.pc() != PROG && is_stop_shape(rep, rig.pc()))) bad += std::string(" ") + k.name;
        c.rx.clear();
    }
    check("ZRCP-CTL-10", "cpu-step-over is a synchronous plain step on all 23 JP / RET forms "
                         "(ZEsarUX's list + JP NZ, JP (IX), JP (IY), RETI, RETN): each answered "
                         "in the same pump, the machine one instruction on",
          bad.empty(), "not a plain step:" + bad);
}

static void tbblue_name_rows() {
    Rig rig(MachineType::ZXN_ISSUE2);
    Zc  c(rig);
    const char* names[4] = {"ula", "layer2", "sprite", "tilemap"};
    const jnext::dbg::ClipLayer layers[4] = {jnext::dbg::ClipLayer::Ula,
                                             jnext::dbg::ClipLayer::Layer2,
                                             jnext::dbg::ClipLayer::Sprites,
                                             jnext::dbg::ClipLayer::Tilemap};
    for (int i = 0; i < 4; ++i) {
        const int b = 1 + i * 4;
        c.cmd(std::string("tbblue-set-clipwindow ") + names[i] + " " + std::to_string(b) + " " +
              std::to_string(b + 1) + " " + std::to_string(b + 2) + " " + std::to_string(b + 3));
    }
    std::string bad;
    for (int i = 0; i < 4; ++i) {
        const int b = 1 + i * 4;
        const std::string want = std::to_string(b) + " " + std::to_string(b + 1) + " " +
                                 std::to_string(b + 2) + " " + std::to_string(b + 3) + " ";
        const auto w = rig.dbg->clip_window(layers[i]);
        if (c.cmd(std::string("tbblue-get-clipwindow ") + names[i]) != reply_of(want) ||
            w.x1 != b || w.y2 != b + 3)
            bad += std::string(" ") + names[i];
    }
    check("ZRCP-TBB-07", "each of the four clip windows, set to its own distinct values, reads "
                         "back its own — over the wire and in the live layer state",
          bad.empty(), "swapped:" + bad);

    const char* pal[3] = {"ula", "layer2", "sprite"};
    const jnext::dbg::PaletteId ids[6] = {
        jnext::dbg::PaletteId::UlaFirst,    jnext::dbg::PaletteId::UlaSecond,
        jnext::dbg::PaletteId::Layer2First, jnext::dbg::PaletteId::Layer2Second,
        jnext::dbg::PaletteId::SpriteFirst, jnext::dbg::PaletteId::SpriteSecond};
    for (int i = 0; i < 6; ++i)
        c.cmd(std::string("tbblue-set-palette ") + pal[i / 2] + (i & 1 ? " second" : " first") +
              " 7 " + std::to_string(0x101 + i * 0x11));
    std::string pbad;
    for (int i = 0; i < 6; ++i) {
        char want[8];
        std::snprintf(want, sizeof(want), "%03X ", 0x101 + i * 0x11);
        const std::string got = c.cmd(std::string("tbblue-get-palette ") + pal[i / 2] +
                                      (i & 1 ? " second" : " first") + " 7");
        if (got != reply_of(want) || rig.dbg->palette(ids[i])[7] != 0x101 + i * 0x11)
            pbad += std::string(" ") + pal[i / 2] + (i & 1 ? "/second" : "/first");
    }
    check("ZRCP-TBB-08", "each of the six palettes (ula|layer2|sprite x first|second), set at "
                         "entry 7 to its own value, reads back its own — over the wire and in "
                         "the backend's bank",
          pbad.empty(), "swapped:" + pbad);
}

static void edge_rows() {
    {
        std::uint8_t b[20];
        for (int i = 0; i < 20; ++i) b[i] = static_cast<std::uint8_t>(0x41 + i);
        b[2] = 0x7E;
        b[3] = 0x7F;
        const std::string h = hexdump(0xFFF8, b, 20);
        check("ZRCP-FMT-13", "hexdump: the line address wraps past FFFFH to 0008H, and 7EH prints "
                             "while 7FH is a dot (ZEsarUX's c < 32 || c > 126)",
              starts_with(h, "  FFF8H 41 42 7E 7F ") && h.find("|AB~.EF") != std::string::npos &&
                  h.find("\n  0008H 51 52 53 54 ") != std::string::npos,
              esc(h));
    }
    {
        Rig rig;
        Zc  c(rig);
        const std::string mib   = c.cmd("read-memory 0 1048576");
        const std::string stk   = c.cmd("get-stack-backtrace 32768");
        const std::string stk1  = c.cmd("get-stack-backtrace 32769");
        const std::string dis   = c.cmd("disassemble 0 65536");
        const std::string dis1  = c.cmd("disassemble 0 65537");
        check("ZRCP-MEM-06", "the bounds are inclusive: read-memory of exactly 1 MiB, a 32768-word "
                             "backtrace and 65536 disassembly lines are served; one more is refused",
              mib.size() == 2 * 1048576 + std::string("\ncommand> ").size() &&
                  stk.size() == 6 * 32768 + std::string("\ncommand> ").size() &&
                  stk1 == "Error. Too many items (max 32768)\ncommand> " &&
                  std::count(dis.begin(), dis.end(), '\n') == 65536 &&
                  dis1 == "Error. Too many lines (max 65536)\ncommand> ",
              esc(stk1) + " / " + esc(dis1));

        const std::uint16_t pc0 = rig.pc();
        const std::string pc  = c.cmd("set-register PC=10000H");
        const std::string a   = c.cmd("set-register A=100H");
        const std::string iff = c.cmd("set-register IFF1=2");
        const std::string rm  = c.cmd("read-memory 70000 1");
        const std::string wm  = c.cmd("write-memory 65536 1");
        const std::string ds  = c.cmd("disassemble 10000H");
        check("ZRCP-REG-08", "out-of-range values are refused, never truncated (§11.3 items 9, "
                             "11): PC=10000H, A=100H and IFF1=2 change nothing; addresses past "
                             "FFFFH are refused by read-memory, write-memory and disassemble",
              pc == "Error changing register\ncommand> " && a == pc && iff == pc &&
                  rig.pc() == pc0 && rm == "Error. Invalid address: 70000\ncommand> " &&
                  wm == "Error. Invalid address: 65536\ncommand> " &&
                  ds == "Error. Invalid address: 10000H\ncommand> ",
              esc(pc) + " / " + esc(rm));
    }
    {
        // ZEsarUX prints BOTH lines when the count ends on a stop (remote.c:
        // "Returning after" in the loop, then "Breakpoint fired" after it).
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0xED, 0xFF, 0x18, 0xFE});
        rig.dbg->set_magic_breakpoint(true);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 4");
        check("ZRCP-RUN-19", "run 4 whose fourth step is the magic opcode: \"Returning after 4 "
                             "opcodes\" AND \"Breakpoint fired: Magic breakpoint\", in that order",
              r.find("\nReturning after 4 opcodes\nBreakpoint fired: Magic breakpoint\nPC=8005 ") !=
                  std::string::npos,
              esc(r));
    }
    {
        // An interrupt discards ITS line only: complete lines sent after it in
        // the same write are commands.
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        c.p->send("\nabout\n");
        const std::string r = c.wait();
        std::string rest = c.wait(4);
        check("ZRCP-RUN-20", "\"\\nabout\\n\" during a run: the blank line stops it and is "
                             "discarded, the about behind it is answered",
              ends_with(r + rest, "\ncommand@cpu-step> jnext ZRCP remote command protocol\n"
                                  "command@cpu-step> "),
              esc(r + rest));
    }
    {
        // A step refused in the middle of run n (the machine went corrupt) ends
        // the run with its reason, and nothing more executes.
        Rig rig;
        load_nops(rig, 100);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        g_clock_ticks = true;
        c.p->send("run 100\n");
        rig.dbg->pump(PumpBudget{});
        rig.dbg->pump(PumpBudget{});
        c.p->take();
        const auto                      other = rig.dbg->attach({"loader", ClientKind::Test}).value;
        const std::vector<std::uint8_t> junk(64, 0x5A);
        rig.dbg->load_state_bytes(other, junk.data(), junk.size());
        const std::uint16_t pc1 = rig.pc();
        rig.dbg->pump(PumpBudget{});
        g_clock_ticks = false;
        const std::string r = c.p->take();
        check("ZRCP-RUN-21", "a step refused inside run n ends it with the refusal's reason "
                             "(fired \"Machine corrupt after failed rewind\"), not a count line, "
                             "and the machine steps no further",
              starts_with(r, "Breakpoint fired: Machine corrupt after failed rewind\n") &&
                  r.find("Returning") == std::string::npos && rig.pc() == pc1,
              esc(r));
        rig.dbg->detach(other);
    }
    {
        // §4.6 rule 4 is a HARD reset's: a soft reset answers no run.
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.send_once("run\n");
        rig.tick();
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        rig.dbg->reset(other, jnext::dbg::ResetKind::Soft);
        rig.pump();
        rig.tick();
        const std::string quiet   = c.p->take();
        const bool        running = !rig.dbg->state().paused;
        check("ZRCP-RUN-22", "another client's SOFT reset during a run sends nothing: the run goes "
                             "on (only Reset{Hard} completes it)",
              quiet.empty() && running, esc(quiet));
        rig.dbg->detach(other);
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const std::string r = c.cmd("run 1000000", 64);
        check("ZRCP-RUN-23", "run 1000000, the cap itself, is served and counts to the end",
              r.find("\nReturning after 1000000 opcodes\nPC=8000 ") != std::string::npos, esc(r));
    }
}

// ===========================================================================
// Review round 2 — the legacy MMU= in the stop/step reply; the strict-range
// rule enumerated over every sibling verb and every register name
// ===========================================================================

static void round2_rows() {
    {
        // The stop and step replies carry the register line too: on the 128K
        // they must be in ZEsarUX's legacy form, as get-registers is.
        Rig rig(MachineType::ZX128K);
        load_nops(rig, 8);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string step = c.cmd("cpu-step");
        const std::string run  = c.cmd("run 2");
        const std::string want = "MMU=80000005000200000000000000000000 TSTATES: ";
        check("ZRCP-REG-09", "on the 128K, the cpu-step reply and the run n stop reply carry the "
                             "legacy MMU= (DeZog's 128K decoder reads [ROM0,5,2,0] from both)",
              step.find(want) != std::string::npos && run.find(want) != std::string::npos &&
                  dezog_128k(step) == std::vector<int>({8, 5, 2, 0}) &&
                  dezog_128k(run) == std::vector<int>({8, 5, 2, 0}),
              esc(step) + " / " + esc(run));
    }
    {
        // §11.3 item 9 over EVERY address-taking verb: 10000H is refused, and
        // nothing in the address space changes.
        static const struct { const char* cmd; const char* reply; } kVerbs[] = {
            {"read-memory 10000H 4", "Error. Invalid address: 10000H"},
            {"write-memory 10000H 171", "Error. Invalid address: 10000H"},
            {"write-memory-raw 10000H AB", "Error. Invalid address: 10000H"},
            {"hexdump 10000H 16", "Error. Invalid address or length"},
            {"get-crc32 10000H 16", "Error. Invalid address or length"},
            {"disassemble 10000H 2", "Error. Invalid address: 10000H"},
        };
        Rig rig;
        Zc  c(rig);
        const auto before = peek(rig, 0, 0x10000);
        std::string bad;
        for (const auto& v : kVerbs)
            if (c.cmd(v.cmd) != reply_of(v.reply)) bad += std::string(" [") + v.cmd + "]";
        const bool same = peek(rig, 0, 0x10000) == before;
        check("ZRCP-MEM-07", "an address of 10000H is refused by every address-taking verb "
                             "(read-memory, write-memory, write-memory-raw, hexdump, get-crc32, "
                             "disassemble), and the 64 KB address space is unchanged",
              bad.empty() && same, "not refused:" + bad + (same ? "" : " memory changed"));
    }
    {
        // §11.3 item 9 over EVERY set-register name: the widest legal value is
        // set, that value + 1 is refused and changes nothing.
        static const struct { const char* name; std::uint32_t max; } kRegs[] = {
            {"PC", 0xFFFF},  {"SP", 0xFFFF},  {"IX", 0xFFFF},  {"IY", 0xFFFF},
            {"AF", 0xFFFF},  {"BC", 0xFFFF},  {"DE", 0xFFFF},  {"HL", 0xFFFF},
            {"AF'", 0xFFFF}, {"BC'", 0xFFFF}, {"DE'", 0xFFFF}, {"HL'", 0xFFFF},
            {"A", 0xFF},     {"B", 0xFF},     {"C", 0xFF},     {"D", 0xFF},
            {"E", 0xFF},     {"F", 0xFF},     {"H", 0xFF},     {"L", 0xFF},
            {"A'", 0xFF},    {"B'", 0xFF},    {"C'", 0xFF},    {"D'", 0xFF},
            {"E'", 0xFF},    {"F'", 0xFF},    {"H'", 0xFF},    {"L'", 0xFF},
            {"I", 0xFF},     {"R", 0xFF},     {"IM", 2},       {"IFF1", 1},
            {"IFF2", 1},
        };
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        std::string bad;
        for (const auto& k : kRegs) {
            Z80Registers z{};
            z.PC = PROG;
            z.SP = 0xFF00;
            rig.emu.cpu().set_registers(z);
            const std::string ok = c.cmd(std::string("set-register ") + k.name + "=" +
                                         std::to_string(k.max));
            const Z80Registers after_ok = rig.emu.cpu().get_registers();
            const std::string over = c.cmd(std::string("set-register ") + k.name + "=" +
                                           std::to_string(k.max + 1));
            const Z80Registers after_over = rig.emu.cpu().get_registers();
            const bool set = std::memcmp(&after_ok, &z, sizeof z) != 0;
            const bool kept = std::memcmp(&after_over, &after_ok, sizeof z) == 0;
            if (!starts_with(ok, "PC=") || !set || over != "Error changing register\ncommand@cpu-step> " || !kept)
                bad += std::string(" ") + k.name;
        }
        check("ZRCP-REG-10", "every set-register name takes its widest legal value (FFFFH, FFH, "
                             "IM 2, IFF 1) and refuses that value + 1 without changing anything",
              bad.empty(), "wrong:" + bad);
    }
}

// ===========================================================================
// WP-4 — breakpoints and conditions (§1.5, §1.6, §2.4, §3, §4.2)
// ===========================================================================

/// `run` from step mode until the stop reply (or `max` ticks): the
/// "Running until…" line, then everything up to the prompt.
static std::string run_to_stop(Zc& c, int max = 12) {
    const std::string first = c.send_once("run\n");
    return first + c.wait_ticks(max);
}

/// Was the reply `Running until…` + a stop at `pc` whose fired line is `fired`?
static bool fired_stop(const std::string& r, std::uint16_t pc, const std::string& fired) {
    char head[16];
    std::snprintf(head, sizeof(head), "PC=%04x ", pc);
    const std::string want = kRunning + "Breakpoint fired: " + fired + "\n" + head;
    return starts_with(r, want) && ends_with(r, PROMPT_STEP);
}

/// A session with breakpoints enabled, in cpu-step mode.
static void bp_on(Zc& c) {
    c.cmd("enter-cpu-step");
    c.cmd("enable-breakpoints");
}

static std::string ev(Zc& c, const std::string& expr) {
    const std::string r = c.cmd("evaluate " + expr);
    return ends_with(r, "\n" + std::string(PROMPT)) ? r.substr(0, r.size() - 1 - std::strlen(PROMPT))
                                                    : r;
}

static void wp4_condition_rows() {
    // ── the tokeniser and ZEsarUX's re-printing (get-breakpoints) ────────
    {
        static const struct { const char* in; const char* canon; } kCanon[] = {
            {"a<>0 and (hl & 0ffh) = 5", "A<>0 AND (HL&FFH)=5"},  // [T2]
            {"PC=0038H", "PC=38H"},                               // [T2]
            {"PC=0abcdh and SEG3=0005h", "PC=ABCDH AND SEG3=5H"}, // DeZog's long address
            {"SP>=65280", "SP>=65280"},                           // DeZog's step-over
            {"PC=PEEKW(SP-2) AND SP>=65280", "PC=PEEKW(SP-2) AND SP>=65280"},
            {"A>-3", "A>-3"},
            {"B='A' or C=101%", "B='A' OR C=101%"},
            {"[1+2]*{3}", "(1+2)*(3)"},
            {"D=-1H", "D=FFFFFFFFH"},
            {"af'=bc' xor not(hl')", "AF'=BC' XOR NOT(HL')"},
        };
        std::string bad;
        for (const auto& k : kCanon) {
            const Translation t = translate_condition(k.in, true);
            if (!t.ok || t.canonical != k.canon)
                bad += std::string(" [") + k.in + " -> " + t.canonical + (t.ok ? "" : " !ok") + "]";
        }
        check("ZRCP-CND-01", "the tokeniser and ZEsarUX's re-printing (exp_par_tokens_to_exp): "
                             "upper case, no spaces but around AND/OR/XOR, hex as %XH, binary "
                             "and ASCII kept, brackets as ()",
              bad.empty(), bad);
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=0038H");
        c.cmd("set-breakpoint 3 a<>0 and (hl & 0ffh) = 5");
        const std::string all = c.cmd("get-breakpoints 1 3");
        check("ZRCP-CND-02", "get-breakpoints lists a condition as ZEsarUX re-prints it ([T2]: "
                             "PC=0038H -> PC=38H; A<>0 and (HL & 0FFH) = 5 -> A<>0 AND "
                             "(HL&FFH)=5) and an empty slot as None",
              all == "Breakpoints: On\nEnabled 1: PC=38H\nDisabled 2: None\nEnabled 3: A<>0 AND "
                     "(HL&FFH)=5\n\ncommand@cpu-step> ",
              esc(all));
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        static const char* const kBad[] = {"this is garbage", "PC=0 && A==1", "PC=0x38",
                                           "PC=", "(A=1", "A B", "PC==1", "!A", "A||B"};
        std::string bad;
        for (const char* b : kBad) {
            const std::string r = c.cmd(std::string("set-breakpoint 2 ") + b);
            if (r != reply_of("Error. Error setting breakpoint", true)) bad += std::string(" [") + b + "]";
        }
        check("ZRCP-CND-03", "what ZEsarUX cannot tokenise or evaluate is refused with its own "
                             "text: garbage, C operators, 0x literals ([T2]), a missing operand, "
                             "an unclosed bracket, two operands",
              bad.empty(), "accepted:" + bad);
    }
    {
        // ZEsarUX splits at the FIRST top-level operator of the lowest class
        // present, so these differ from any precedence-table reading.
        Rig rig;
        Zc  c(rig);
        static const struct { const char* e; const char* v; } kGroup[] = {
            {"9-3-1", "7"},          // 9-(3-1); a table would say 5
            {"2*3&1", "2"},          // 2*(3&1); a table would say 0
            {"8/4/2", "4"},          // 8/(4/2); a table would say 1
            {"0 AND 0 OR 1", "0"},   // 0 AND (0 OR 1); a table would say 1
            {"1 OR 1 AND 0", "1"},   // 1 OR (1 AND 0)
            {"1+2*3", "7"},          // + splits first: 1+(2*3)
            {"2*3+1", "7"},          // (2*3)+1
            {"1=1=0", "0"},          // 1=(1=0)
            {"[1+2]*{3}", "9"},      // ( [ { all group
            {"(9-3)-1", "5"},
        };
        std::string bad;
        for (const auto& g : kGroup) {
            const std::string r = ev(c, g.e);
            if (r != g.v) bad += std::string(" [") + g.e + "=" + r + "]";
        }
        check("ZRCP-CND-04", "grouping is ZEsarUX's (exp_par_evaluate_token): the first logical "
                             "operator, then the first comparison, then the first + or -, then "
                             "the first other operator, right-nested — never a precedence table",
              bad.empty(), bad);
    }
    {
        // Every honoured register and flag name reads its register.
        Rig rig;
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x12D7; z.BC = 0x3456; z.DE = 0x789A; z.HL = 0xBCDE;
        z.IX = 0x1357; z.IY = 0x2468; z.SP = 0xFEDC; z.PC = 0x8000;
        z.I = 0x3F; z.R = 0x5A; z.IFF1 = 1; z.IFF2 = 0;
        z.AF2 = 0x9A65; z.BC2 = 0x1122; z.DE2 = 0x3344; z.HL2 = 0x5566;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        static const struct { const char* n; int v; } kRegs[] = {
            {"A", 0x12},   {"F", 0xD7},    {"B", 0x34},    {"C", 0x56},   {"D", 0x78},
            {"E", 0x9A},   {"H", 0xBC},    {"L", 0xDE},    {"I", 0x3F},   {"R", 0x5A},
            {"AF", 0x12D7}, {"BC", 0x3456}, {"DE", 0x789A}, {"HL", 0xBCDE}, {"IX", 0x1357},
            {"IY", 0x2468}, {"SP", 0xFEDC}, {"PC", 0x8000}, {"AF'", 0x9A65}, {"BC'", 0x1122},
            {"DE'", 0x3344}, {"HL'", 0x5566}, {"A'", 0x9A},  {"F'", 0x65},   {"B'", 0x11},
            {"C'", 0x22},  {"D'", 0x33},   {"E'", 0x44},   {"H'", 0x55},  {"L'", 0x66},
            {"FS", 1},     {"FZ", 1},      {"FH", 1},      {"FP", 1},     {"FV", 1},
            {"FN", 1},     {"FC", 1},      {"IFF1", 1},    {"IFF2", 0},   {"hl", 0xBCDE},
        };
        std::string bad;
        for (const auto& k : kRegs) {
            const std::string r = ev(c, k.n);
            if (r != std::to_string(k.v)) bad += std::string(" [") + k.n + "=" + r + "]";
        }
        z.AF = 0x1228;  // F: only bits 5 and 3 — every flag reads 0
        rig.emu.cpu().set_registers(z);
        for (const char* f : {"FS", "FZ", "FH", "FP", "FV", "FN", "FC"}) {
            const std::string r = ev(c, f);
            if (r != "0") bad += std::string(" [") + f + "=" + r + " with F=28]";
        }
        z.AF = 0x1204;  // F: P/V alone — FP and FV read it, nothing else does
        rig.emu.cpu().set_registers(z);
        for (const char* f : {"FS", "FZ", "FH", "FP", "FV", "FN", "FC"}) {
            const std::string r  = ev(c, f);
            const bool        pv = std::string(f) == "FP" || std::string(f) == "FV";
            if (r != (pv ? "1" : "0")) bad += std::string(" [") + f + "=" + r + " with F=04]";
        }
        check("ZRCP-CND-05", "every honoured register name reads its register (the 8-bit "
                             "alternates from AF'..HL', FV as FP), each flag 1 when set and 0 "
                             "when clear, IFF1/IFF2, case-insensitively",
              bad.empty(), bad);
    }
    {
        Rig rig;
        Zc  c(rig);
        static const struct { const char* e; const char* v; } kOps[] = {
            {"3=5", "0"},   {"5=5", "1"},   {"3<>5", "1"},  {"5<>5", "0"},  {"3<5", "1"},
            {"5<3", "0"},   {"3>5", "0"},   {"5>3", "1"},   {"3<=5", "1"},  {"5<=5", "1"},
            {"6<=5", "0"},  {"3>=5", "0"},  {"5>=5", "1"},  {"7+3", "10"},  {"7-3", "4"},
            {"3-7", "-4"},  {"7*3", "21"},  {"7/2", "3"},   {"7/0", "65535"}, {"12&10", "8"},
            {"12|10", "14"}, {"12^10", "6"}, {"1 AND 0", "0"}, {"1 AND 2", "1"}, {"0 OR 0", "0"},
            {"0 OR 3", "1"}, {"1 XOR 1", "0"}, {"1 XOR 0", "1"}, {"0 XOR 2", "1"}, {"0 XOR 0", "0"},
            {"NOT(0)", "1"}, {"NOT(5)", "0"}, {"1 and 1", "1"}, {"5 or 0", "1"},
            {"5 XOR 3", "0"}, {"5 XOR 0", "1"}, {"0 XOR 7", "1"}, {"-1 XOR 2", "0"},
        };
        std::string bad;
        for (const auto& o : kOps) {
            const std::string r = ev(c, o.e);
            if (r != o.v) bad += std::string(" [") + o.e + "=" + r + "]";
        }
        check("ZRCP-CND-06", "every operator in both directions (true and false, both operand "
                             "orders): = <> < > <= >= yield 1/0, + - * / & | ^, x/0 = 65535 "
                             "(exp_par_calculate_operador), AND OR XOR on truth (5 XOR 3 is 0), "
                             "NOT()",
              bad.empty(), bad);
    }
    {
        Rig rig;
        rig.load({0x3E, 0x05, 0xC9, 0xED}, PROG);
        Zc c(rig);
        c.cmd("write-memory 65535 171");    // FFFF = AB
        c.cmd("write-memory 0 205");        // 0000 is ROM on the 48K: stays what it was
        const std::string rom0 = ev(c, "PEEK(0)");
        static const struct { const char* e; std::string v; } kMem[] = {
            {"PEEK(8000H)", "62"},             {"PEEKW(8000H)", std::to_string(0x053E)},
            {"PEEK(PC+1)", "5"},               {"PEEKW(FFFFH)", std::to_string(0xAB + 256 * std::stoi(rom0))},
            {"PEEK(-1)", "171"},               {"PEEK(1FFFFH)", "171"},
            {"OPCODE1", "62"},                 {"OPCODE2", std::to_string(0x3E05)},
            {"OPCODE3", std::to_string(0x3E05C9)}, {"OPCODE4", std::to_string(0x3E05C9ED)},
        };
        std::string bad;
        for (const auto& m : kMem) {
            const std::string r = ev(c, m.e);
            if (r != m.v) bad += std::string(" [") + m.e + "=" + r + " want " + m.v + "]";
        }
        check("ZRCP-CND-07", "PEEK / PEEKW read the CPU view at the address mod 64K (PEEKW "
                             "little-endian, wrapping at FFFFH), OPCODE1..4 the bytes at PC "
                             "most significant first",
              bad.empty(), bad);
    }
    {
        Rig rig;
        Zc  c(rig);
        static const struct { const char* e; const char* v; } kNum[] = {
            {"0FFH", "255"}, {"FFH", "255"}, {"38h", "56"}, {"101%", "5"}, {"'A'", "65"},
            {"-3", "-3"},    {"70000", "70000"}, {"FFFFFFFFH", "-1"}, {"0", "0"},
        };
        std::string bad;
        for (const auto& n : kNum) {
            const std::string r = ev(c, n.e);
            if (r != n.v) bad += std::string(" [") + n.e + "=" + r + "]";
        }
        check("ZRCP-CND-08", "numbers as ZEsarUX reads them: decimal, hexadecimal with an H "
                             "suffix (no leading 0 needed), binary with %, 'c', a sign, 32-bit "
                             "signed",
              bad.empty(), bad);
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        static const char* const kDeclined[] = {
            "MRA=1",     "MWV=2",       "PRA=3",     "PWV=4",      "TSTATES>0",
            "TSTATESL=1", "TSTATESP=1", "SCANLINE=1", "OUTFIRED=1", "INFIRED=1",
            "INTFIRED=1", "ENTERROM=1", "EXITROM=1", "HILOWMAPPED=1", "PD765PCN=1",
            "USP=1",     "EPC=1",       "COPPERPC=1", "D0=1",       "A7=1",
            "AC=1",      "SR=1",        "P1=1",      "FPEEK(1)=1", "IN(254)=1",
            "ABS(1)=1",  "BYTE(1)=1",   "WORD(1)=1", "OPMWA(1)=1",
        };
        std::string bad;
        for (const char* d : kDeclined) {
            const std::string r = c.cmd(std::string("set-breakpoint 4 ") + d);
            const std::string e = c.cmd(std::string("evaluate ") + d);
            if (r != reply_of("Error. Error setting breakpoint", true) ||
                !starts_with(e, "Error evaluating parsed string: "))
                bad += std::string(" [") + d + "]";
        }
        const std::string slot = c.cmd("get-breakpoints 4");
        check("ZRCP-CND-09", "every declined name and function (help set-breakpoint lists them) "
                             "is refused by set-breakpoint and by evaluate, and the slot keeps "
                             "what it had",
              bad.empty() && slot == "Breakpoints: On\nDisabled 4: None\n\ncommand@cpu-step> ",
              "not refused:" + bad + " / " + esc(slot));
    }
    {
        Rig rig;
        Zc  c(rig);
        const std::string none  = c.cmd("evaluate");
        const std::string parse = c.cmd("evaluate 0x10");
        const std::string eval  = c.cmd("evaluate PC=");
        const std::string ok    = c.cmd("e 2+2");
        check("ZRCP-CND-10", "evaluate: no expression, \"Error parsing\" for what does not "
                             "tokenise ([T4]: 0x10), \"Error evaluating parsed string: <it>\" for "
                             "what cannot be evaluated, the alias e, a decimal result",
              none == reply_of("Error. No expression") && parse == reply_of("Error parsing") &&
                  eval == reply_of("Error evaluating parsed string: PC=") && ok == reply_of("4"),
              esc(none) + " / " + esc(parse) + " / " + esc(eval) + " / " + esc(ok));
    }
    {
        // SEGn / ROM / RAM — the MMU projection get-registers prints.
        Rig nx(MachineType::ZXN_ISSUE2);
        Zc  c(nx);
        std::string bad;
        const auto slots = nx.dbg->mmu_slots();
        for (int i = 0; i < 8; ++i) {
            const std::string r = ev(c, "SEG" + std::to_string(i));
            const std::string w = std::to_string(mapped_page(slots, i, MachineType::ZXN_ISSUE2));
            if (r != w) bad += " [SEG" + std::to_string(i) + "=" + r + " want " + w + "]";
        }
        const std::string rom = ev(c, "ROM"), ram = ev(c, "RAM");
        Rig r128(MachineType::ZX128K);
        Zc  c128(r128);
        c128.cmd("write-port 32765 19");  // ROM 1, bank 3 at C000 (as REG-06)
        const std::string rom128 = ev(c128, "ROM"), ram128 = ev(c128, "RAM"),
                          seg128 = ev(c128, "SEG3");
        Rig r48;
        Zc  c48(r48);
        const std::string rom48 = ev(c48, "ROM"), seg48 = ev(c48, "SEG0");
        check("ZRCP-CND-11", "SEG0..7 on the Next are get-registers' eight MMU= values; ROM and "
                             "RAM on the 128K the ROM image and the bank at C000; everything "
                             "else 0, as ZEsarUX 12.0",
              bad.empty() && rom == "0" && ram == "0" && rom128 == "1" && ram128 == "3" &&
                  seg128 == "0" && rom48 == "0" && seg48 == "0",
              bad + " next ROM/RAM=" + rom + "/" + ram + " 128K ROM/RAM/SEG3=" + rom128 + "/" +
                  ram128 + "/" + seg128 + " 48K ROM/SEG0=" + rom48 + "/" + seg48);
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        static const char* const kBadNative[] = {"SEG3=5 OR A=1", "(SEG3=5)", "SEG3+1=5",
                                                 "A=1 OR SEG3=5", "5=SEG3", "SEG3=A"};
        std::string bad;
        for (const char* b : kBadNative)
            if (c.cmd(std::string("set-breakpoint 5 ") + b) !=
                reply_of("Error. Error setting breakpoint", true))
                bad += std::string(" [") + b + "]";
        const std::string ok1 = c.cmd("set-breakpoint 5 PC=8000H AND SEG3=5");
        const std::string ok2 = c.cmd("set-breakpoint 6 SEG3<>5 AND ROM=0 AND RAM>=0");
        const std::string evv = c.cmd("evaluate SEG3=0 AND ROM=0");
        check("ZRCP-CND-12", "SEGn / ROM / RAM are honoured as <var><op><number> in the "
                             "top-level AND chain (DeZog's form) and refused anywhere else; "
                             "evaluate gives such a chain 1 or 0",
              bad.empty() && ok1 == reply_of("", true) && ok2 == reply_of("", true) &&
                  evv == reply_of("1", true),
              "accepted:" + bad + " / " + esc(ok1) + " / " + esc(evv));
    }
    {
        // §3.3 — the fast path, seen as the subscription's PC range.
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        static const struct { const char* cond; int lo, hi; bool has_cond; } kFast[] = {
            {"PC=8002H", 0x8002, 0x8002, false},
            {"PC=8002H AND A=0", 0x8002, 0x8002, true},
            {"pc = 32770", 0x8002, 0x8002, false},
            {"A=0 AND PC=8002H", 0x0000, 0xFFFF, true},
            {"SP>=65280", 0x0000, 0xFFFF, true},
            {"PC=8002H OR A=0", 0x0000, 0xFFFF, true},
            {"PC=70000", 0x0000, 0xFFFF, true},
            {"PC=-1", 0x0000, 0xFFFF, true},
            {"PC>8002H", 0x0000, 0xFFFF, true},
            {"PC=8002H+1", 0x0000, 0xFFFF, true},
            {"PC=PEEKW(SP-2) AND SP>=65280", 0x0000, 0xFFFF, true},
        };
        std::string bad;
        for (const auto& f : kFast) {
            c.cmd(std::string("set-breakpoint 7 ") + f.cond);
            int n = 0;
            bool match = false;
            for (const auto& s : rig.dbg->subscriptions(false)) {
                if (s.kind != jnext::dbg::EventKind::Execute) continue;
                ++n;
                match = s.filter.lo == f.lo && s.filter.hi == f.hi && s.has_condition == f.has_cond;
            }
            if (n != 1 || !match) bad += std::string(" [") + f.cond + "]";
        }
        check("ZRCP-CND-13", "§3.3: a leading PC=<16-bit number> conjunct becomes Execute[n,n] "
                             "(with the rest, if any, as its condition); anything else "
                             "Execute[0,FFFF] with the whole condition",
              bad.empty(), bad);
    }
    {
        Rig rig;
        Zc  c(rig);  // 48K: SEG3 is 0
        static const struct { const char* e; const char* v; } kChain[] = {
            {"SEG3=0 AND 0", "0"}, {"SEG3=1 AND 1", "0"}, {"SEG3=0 AND 1", "1"},
            {"SEG3<>0 AND 1", "0"}, {"SEG3<1 AND 2", "1"}, {"SEG3>=1 AND 1", "0"},
            {"SEG3<0 AND 1", "0"},  {"SEG3<=0 AND 1", "1"}, {"SEG3>0 AND 1", "0"},
            {"SEG3>=0 AND 1", "1"},
            {"FF", "Error parsing"}, {"PEEK(0)PEEK(1)", "Error parsing"},
        };
        std::string bad;
        for (const auto& k : kChain) {
            const std::string r = ev(c, k.e);
            if (r != k.v) bad += std::string(" [") + k.e + "=" + r + "]";
        }
        check("ZRCP-CND-14", "evaluate over a chain with a native term is 1 only when every "
                             "conjunct holds, native or not, each comparison as written; a "
                             "letter-only hex number without H, and two operands, do not "
                             "tokenise",
              bad.empty(), bad);
    }
}

static void wp4_slot_rows() {
    {
        // A native term in a live breakpoint: the Next, DeZog's long address.
        Rig rig(MachineType::ZXN_ISSUE2);
        rig.load({0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto seg4 = mapped_page(rig.dbg->mmu_slots(), 4, MachineType::ZXN_ISSUE2);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=8002H AND SEG4=" + std::to_string(seg4 + 1));
        c.cmd("set-breakpoint 2 PC=8003H AND SEG4=" + std::to_string(seg4));
        const std::string r = run_to_stop(c);
        check("ZRCP-BP-18", "PC=… AND SEGn=… (DeZog's long address on the Next): the slot whose "
                            "SEG4 is the page mapped there fires at 8003; the one naming another "
                            "page never fires at 8002",
              fired_stop(r, 0x8003, "PC=8003H AND SEG4=" + std::to_string(seg4)), esc(r));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        static const char* const kGated[] = {
            "set-breakpoint 1 PC=0", "set-breakpoint 0 PC=0", "enable-breakpoint 1",
            "disable-breakpoint 1",  "set-breakpointaction 1", "set-membreakpoint 4000h 1",
            "sb 101 PC=0",
        };
        std::string bad;
        for (const char* g : kGated)
            if (c.cmd(g) != reply_of("Error. You must enable breakpoints first", true))
                bad += std::string(" [") + g + "]";
        const std::string off1 = c.cmd("disable-breakpoints");
        const std::string on1  = c.cmd("enable-breakpoints");
        const std::string on2  = c.cmd("enable-breakpoints");
        const std::string off2 = c.cmd("disable-breakpoints");
        check("ZRCP-BP-01", "the master switch: off at connect, every slot and membreakpoint "
                            "setter refused first with ZEsarUX's text (before the range check: "
                            "slot 0 too), Already enabled / Already disabled",
              bad.empty() && off1 == reply_of("Error. Already disabled", true) &&
                  on1 == reply_of("", true) && on2 == reply_of("Error. Already enabled", true) &&
                  off2 == reply_of("", true),
              "not gated:" + bad + " / " + esc(off1) + " / " + esc(on2));
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        static const char* const kVerbs[] = {"set-breakpoint %d PC=0", "enable-breakpoint %d",
                                             "disable-breakpoint %d", "set-breakpointaction %d"};
        std::string bad;
        for (const char* v : kVerbs)
            for (int n : {0, 1, 100, 101}) {
                char line[64];
                std::snprintf(line, sizeof(line), v, n);
                const std::string r = c.cmd(line);
                const bool in = n >= 1 && n <= 100;
                if ((r == reply_of("Error. Index out of range", true)) == in)
                    bad += std::string(" [") + line + "]";
            }
        const std::string g0   = c.cmd("get-breakpoints 0");
        const std::string g101 = c.cmd("gb 101");
        const std::string a0   = c.cmd("gba 0");
        const std::string noix = c.cmd("enable-breakpoint");
        const std::string nop  = c.cmd("set-breakpoint");
        check("ZRCP-BP-02", "slot bounds 0 / 1 / 100 / 101 on every slot verb ([T2]: Index out "
                            "of range), get-breakpoints / -actions likewise (ERROR. Index out of "
                            "range), and no index at all",
              bad.empty() && g0 == reply_of("ERROR. Index out of range", true) &&
                  g101 == reply_of("ERROR. Index out of range", true) &&
                  a0 == reply_of("ERROR. Index out of range", true) &&
                  noix == reply_of("Error. No index set", true) &&
                  nop == reply_of("Error. No parameters set", true),
              bad + " / " + esc(g0) + " / " + esc(noix) + " / " + esc(nop));
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 2 PC=1");
        c.cmd("disable-breakpoint 2");
        c.cmd("set-breakpoint 4");
        const std::string one  = c.cmd("get-breakpoints 2");
        const std::string four = c.cmd("get-breakpoints 2 3");
        const std::string all  = c.cmd("get-breakpoints");
        c.cmd("disable-breakpoints");
        const std::string off  = c.cmd("get-breakpoints 4 1");
        check("ZRCP-BP-03", "get-breakpoints [index] [items]: the On/Off header, Enabled / "
                            "Disabled per slot (all Disabled while breakpoints are off), None for "
                            "no condition; set-breakpoint N alone empties and enables the slot "
                            "([T2])",
              one == "Breakpoints: On\nDisabled 2: PC=1\n\ncommand@cpu-step> " &&
                  four == "Breakpoints: On\nDisabled 2: PC=1\nDisabled 3: None\nEnabled 4: "
                          "None\n\ncommand@cpu-step> " &&
                  std::count(all.begin(), all.end(), '\n') == 102 &&
                  off == "Breakpoints: Off\nDisabled 4: None\n\ncommand@cpu-step> ",
              esc(one) + " / " + esc(four) + " / " + esc(off));
    }
    {
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpointaction 1");
        c.cmd("set-breakpoint 1 PC=8002H");
        c.cmd("enable-breakpoint 1");
        const std::string r = run_to_stop(c);
        const auto st = rig.dbg->state();
        check("ZRCP-BP-04", "DeZog's three commands then run: Running until… first, then "
                            "\"Breakpoint fired: PC=8002H\\n\" and the stop at 8002, the machine "
                            "paused there (§6.2 item 3)",
              fired_stop(r, 0x8002, "PC=8002H") && st.paused && rig.pc() == 0x8002 &&
                  st.pause_reason.kind == PauseReason::Kind::Breakpoint,
              esc(r));

        // Resuming from the breakpoint does not stop at once (GH #221 step-off);
        // the machine runs on to JR $, where data stops it.
        c.send_once("run\n");
        rig.tick();
        rig.tick();
        const bool running = !rig.dbg->state().paused;
        const std::string stop = c.send_once("\n") + c.wait(8);
        check("ZRCP-BP-05", "run from the breakpoint's own address runs on (it is not hit "
                            "again there) until data stops it at JR $",
              running && is_stop_shape(stop, 0x8003), esc(stop));
    }
    {
        // A failed set leaves the slot as it was (divergence §2.4).
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 9 PC=1234H");
        c.cmd("disable-breakpoint 9");
        const std::string bad = c.cmd("set-breakpoint 9 garbage here");
        const std::string kept = c.cmd("get-breakpoints 9");
        int execs = 0;
        for (const auto& s : rig.dbg->subscriptions(false))
            execs += s.kind == jnext::dbg::EventKind::Execute;
        check("ZRCP-BP-06", "a condition that does not compile is refused and the slot keeps "
                            "its condition and its disabled state (ZEsarUX would empty it), and "
                            "nothing is armed",
              bad == reply_of("Error. Error setting breakpoint", true) &&
                  kept == "Breakpoints: On\nDisabled 9: PC=1234H\n\ncommand@cpu-step> " &&
                  execs == 0,
              esc(kept));
    }
    {
        // A condition on the fast path: fires only where its rest holds.
        Rig rig;
        rig.load({0x00, 0x3E, 0x05, 0x00, 0x00, 0x3E, 0x07, 0x00, 0x18, 0xFE});
        //        8000 NOP  8001 LD A,5   8003 NOP 8004 NOP 8005 LD A,7  8007 NOP 8008 JR $
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=8007H AND A=5");
        c.cmd("set-breakpoint 2 PC=8004H AND A=5");
        const std::string r = run_to_stop(c);
        check("ZRCP-BP-07", "PC=<n> AND <condition>: stops at n only when the condition holds "
                            "there — 8004 with A=5, never 8007 (A=7) — and echoes the slot that "
                            "fired",
              fired_stop(r, 0x8004, "PC=8004H AND A=5") && rig.pc() == 0x8004, esc(r));
        c.cmd("disable-breakpoint 2");
        c.send_once("run\n");
        for (int i = 0; i < 4; ++i) rig.tick();
        const bool running = !rig.dbg->state().paused;
        const std::string stop = c.send_once("\n") + c.wait(8);
        check("ZRCP-BP-08", "with that slot disabled the run passes 8007 (A=7 there) and runs "
                            "on to JR $",
              running && is_stop_shape(stop, 0x8008), esc(stop));
    }
    {
        // DeZog's step-over (SP>=) and step-out (PC=PEEKW(SP-2) AND SP>=), §6.2 item 6.
        Rig rig;
        rig.load({0xCD, 0x10, 0x80, 0x00, 0x18, 0xFE});  // CALL 8010; NOP; JR $
        rig.load({0x00, 0x00, 0xC9}, 0x8010);              // NOP; NOP; RET
        rig.load({0xCD, 0x10, 0x80, 0x00, 0x18, 0xFE});    // PC back at 8000, SP FF00
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpointaction 100");
        c.cmd("set-breakpoint 100 SP>=65280");
        c.cmd("enable-breakpoint 100");
        const std::string over = run_to_stop(c);
        const auto        sp1  = rig.emu.cpu().get_registers().SP;
        c.cmd("disable-breakpoint 100");

        Z80Registers z = rig.emu.cpu().get_registers();
        z.PC = 0x8010;
        z.SP = 0xFEFE;
        rig.emu.cpu().set_registers(z);
        rig.emu.mmu().write(0xFEFE, 0x03);
        rig.emu.mmu().write(0xFEFF, 0x80);
        c.cmd("set-breakpoint 100 PC=PEEKW(SP-2) AND SP>=65280");
        const std::string out = run_to_stop(c);
        check("ZRCP-BP-09", "DeZog's step-over (SP>=65280 run across a CALL) stops after the "
                            "RET at 8003 with SP back at FF00, and its step-out (PC=PEEKW(SP-2) "
                            "AND SP>=65280 run from inside the routine) stops at the return "
                            "address — both general (Execute[0,FFFF]) conditions",
              fired_stop(over, 0x8003, "SP>=65280") && sp1 == 0xFF00 &&
                  fired_stop(out, 0x8003, "PC=PEEKW(SP-2) AND SP>=65280"),
              esc(over) + " / " + esc(out));
    }
    {
        // disable-breakpoints suspends; enable-breakpoints restores, flags kept.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=8002H");
        c.cmd("set-breakpoint 2 PC=8001H");
        c.cmd("disable-breakpoint 2");
        c.cmd("disable-breakpoints");
        const auto armed_off = rig.dbg->subscriptions(false).size();
        c.send_once("run\n");
        for (int i = 0; i < 4; ++i) rig.tick();
        const bool ran = !rig.dbg->state().paused;
        c.send_once("\n");
        c.wait(8);
        // Back to 8000 for the second leg.
        Z80Registers z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        rig.emu.cpu().set_registers(z);
        c.cmd("enable-breakpoints");
        const auto armed_on = rig.dbg->subscriptions(false).size();
        const std::string r = run_to_stop(c);
        check("ZRCP-BP-10", "disable-breakpoints disarms every slot (nothing fires, nothing "
                            "subscribed); enable-breakpoints re-arms exactly the enabled ones "
                            "(slot 1, not the disabled slot 2)",
              armed_off == 0 && ran && armed_on == 1 && fired_stop(r, 0x8002, "PC=8002H"),
              "armed " + std::to_string(armed_off) + "/" + std::to_string(armed_on) + " " + esc(r));
    }
    {
        // Another client's breakpoint is not this session's to name.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        jnext::dbg::Subscription s;
        s.kind      = jnext::dbg::EventKind::Execute;
        s.filter.lo = s.filter.hi = 0x8002;
        rig.dbg->subscribe(other, s);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=8002H");
        const std::string r = run_to_stop(c);
        check("ZRCP-BP-11", "another client's breakpoint, subscribed first, and this session's "
                            "slot match the same instruction: the backend names the other's, "
                            "and the stop still echoes this session's slot",
              rig.dbg->state().pause_reason.by == other && fired_stop(r, 0x8002, "PC=8002H"),
              esc(r));
        c.cmd("disable-breakpoint 1");
        Z80Registers z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        rig.emu.cpu().set_registers(z);
        const std::string r2 = run_to_stop(c);
        check("ZRCP-BP-12", "another client's breakpoint alone: a plain stop, no fired line",
              starts_with(r2, kRunning) && is_stop_shape(r2.substr(kRunning.size()), 0x8002),
              esc(r2));
        rig.dbg->detach(other);
    }
    {
        // Print actions: they do not stop the machine and their line reaches
        // this session.
        Rig rig;
        rig.load({0x3E, 0x2A, 0x00, 0x00, 0x00, 0x18, 0xFE});
        //        8000 LD A,2AH  8002 NOP 8003 NOP 8004 NOP 8005 JR $
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpointaction 1 prints hello world");
        c.cmd("set-breakpoint 1 PC=8002H");
        c.cmd("set-breakpointaction 2 printe A+1");
        c.cmd("set-breakpoint 2 PC=8003H");
        c.cmd("set-breakpointaction 3 printc 65");
        c.cmd("set-breakpoint 3 PC=8004H");
        c.cmd("set-breakpointaction 4 printregs");
        c.cmd("set-breakpoint 4 PC=8004H");
        std::string got = c.send_once("run\n");
        for (int i = 0; i < 4; ++i) {
            rig.tick();
            got += c.p->take();
        }
        const bool running = !rig.dbg->state().paused;
        const std::string stop = c.send_once("\n") + c.wait(8);
        check("ZRCP-BP-13", "prints / printe / printc / printregs actions do not stop the run; "
                            "each sends its line once as \"log> …\" (text, 43, A, the register "
                            "line at 8004) and data stops the run at JR $",
              running && starts_with(got, kRunning) &&
                  got.find("log> hello world\n") != std::string::npos &&
                  got.find("log> 43\n") != std::string::npos &&
                  got.find("log> A\n") != std::string::npos &&
                  got.find("log> PC=8004 ") != std::string::npos &&
                  std::count(got.begin(), got.end(), '\n') == 5 && is_stop_shape(stop, 0x8005),
              esc(got) + " / " + esc(stop));
        const std::string acts = c.cmd("get-breakpointsactions 1 5");
        const std::string bad  = c.cmd("set-breakpointaction 5 call 8000H");
        const std::string back = c.cmd("set-breakpointaction 1 break");
        const std::string act1 = c.cmd("gba 1");
        const std::string menu = c.cmd("set-breakpointaction 2 menu");
        const std::string act2 = c.cmd("gba 2");
        int stops = 0, handlers = 0;
        for (const auto& si : rig.dbg->subscriptions(false)) {
            if (si.filter.lo != 0x8002 && si.filter.lo != 0x8003) continue;
            stops += si.action == jnext::dbg::Action::Stop;
            handlers += si.has_handler;
        }
        check("ZRCP-BP-14", "get-breakpointsactions lists menu for a stopping slot and the "
                            "action as set otherwise; an action jnext does not serve is refused "
                            "by name; break and menu make a slot a stop again (a Stop, no "
                            "handler)",
              acts == "1: prints hello world\n2: printe A+1\n3: printc 65\n4: printregs\n5: "
                      "menu\n\ncommand@cpu-step> " &&
                  bad == reply_of("Error. Unsupported breakpoint action in jnext: call", true) &&
                  back == reply_of("", true) && act1 == "1: menu\n\ncommand@cpu-step> " &&
                  menu == reply_of("", true) && act2 == "2: menu\n\ncommand@cpu-step> " &&
                  stops == 2 && handlers == 0,
              esc(acts) + " / " + esc(bad) + " / " + esc(menu) + " stops=" +
                  std::to_string(stops) + " handlers=" + std::to_string(handlers));
    }
    {
        // run n: landing on this session's slot stops it with the slot's fired
        // line; a general slot whose condition is false does not end it early.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 PC=8003H");
        c.cmd("set-breakpoint 2 A=99");
        const std::string r = c.cmd("run 10", 32);
        check("ZRCP-BP-15", "run n landing where a slot fires (8003) ends there with that slot's "
                            "fired line and no Returning line; the general slot A=99, false "
                            "throughout, does not end it at 8001",
              rig.pc() == 0x8003 &&
                  r.find(" 10 opcodes run, or other event\nBreakpoint fired: PC=8003H\nPC=8003 ") !=
                      std::string::npos &&
                  r.find("Returning after") == std::string::npos && ends_with(r, PROMPT_STEP),
              esc(r));
        c.cmd("disable-breakpoint 1");
        Z80Registers z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        rig.emu.cpu().set_registers(z);
        const std::string r2 = c.cmd("run 3", 32);
        check("ZRCP-BP-16", "with only the false general slot armed, run 3 runs its three "
                            "opcodes and says so",
              rig.pc() == 0x8003 &&
                  r2.find(" 3 opcodes run, or other event\nReturning after 3 opcodes\nPC=8003 ") !=
                      std::string::npos &&
                  r2.find("Breakpoint fired") == std::string::npos,
              esc(r2));
    }
    {
        // The session's breakpoints die with it; the next client starts clean.
        Rig rig;
        {
            Zc c(rig);
            bp_on(c);
            c.cmd("set-breakpoint 1 PC=8000H");
            c.cmd("set-membreakpoint 9000h 3 4");
            c.cmd("set-breakpointaction 2 prints x");
            c.p->send("quit\n");
            c.wait(4);
        }
        Zc c2(rig);
        c2.cmd("enter-cpu-step");
        const std::string gb = c2.cmd("get-breakpoints 1 2");
        const std::string gm = c2.cmd("get-membreakpoints");
        const std::string ga = c2.cmd("gba 2");
        check("ZRCP-BP-17", "quit drops the session's breakpoints: the next client finds "
                            "breakpoints off, every slot None, no memory breakpoint, no action, "
                            "and the backend holds no subscription",
              gb == "Breakpoints: Off\nDisabled 1: None\nDisabled 2: None\n\ncommand@cpu-step> " &&
                  gm == "Breakpoints: Off\n\ncommand@cpu-step> " &&
                  ga == "2: menu\n\ncommand@cpu-step> " && rig.dbg->subscriptions(true).empty(),
              esc(gb) + " / " + esc(gm));
    }
    {
        // run n does not end on a print-action slot: it prints and steps on.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpointaction 1 prints here");
        c.cmd("set-breakpoint 1 PC=8001H");
        const std::string r = c.cmd("run 3", 32);
        check("ZRCP-BP-19", "run n over a print-action slot's address does not stop there, and "
                            "the action runs: one log> line, sent before the reply, then the 3 "
                            "opcodes' reply",
              rig.pc() == 0x8003 && r.find("Returning after 3 opcodes\n") != std::string::npos &&
                  r.find("Breakpoint fired") == std::string::npos &&
                  r.find("log> here\n") != std::string::npos &&
                  r.find("log> here\n") < r.find("Returning after") &&
                  r.find("log> here\n", r.find("log> here\n") + 1) == std::string::npos,
              esc(r));
    }
    {
        // B1 (M2 review): with a PC-free slot of this session armed, run n still
        // stops on ANOTHER client's breakpoint — and so it does with a false slot
        // of this session at the same address.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"gdb", ClientKind::Test}).value;
        jnext::dbg::Subscription s;
        s.kind      = jnext::dbg::EventKind::Execute;
        s.filter.lo = s.filter.hi = 0x8003;
        rig.dbg->subscribe(other, s);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=99");
        const std::string r1 = c.cmd("run 6", 32);
        const auto pc1 = rig.pc();
        c.cmd("disable-breakpoint 1");
        c.cmd("set-breakpoint 2 PC=8003H AND A=99");
        Z80Registers z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        rig.emu.cpu().set_registers(z);
        const std::string r2 = c.cmd("run 6", 32);
        const auto pc2 = rig.pc();
        check("ZRCP-BP-21", "run n with a PC-free slot of this session armed (A=99, false) stops "
                            "at another client's breakpoint (8003, no fired line), and so does "
                            "run n with a false slot of this session at that very address",
              pc1 == 0x8003 && r1.find("Returning after") == std::string::npos &&
                  r1.find("Breakpoint fired") == std::string::npos && pc2 == 0x8003 &&
                  r2.find("Returning after") == std::string::npos &&
                  r2.find("Breakpoint fired") == std::string::npos,
              esc(r1) + " / " + esc(r2));
        rig.dbg->detach(other);
    }
    {
        // B2 (M2 review): ZEsarUX's "On Change" for a PC-free condition. A=0
        // stays true in a NOP / JR loop: the first run fires once, the next one
        // runs on; a false stretch re-arms it.
        Rig rig;
        rig.load({0x00, 0x18, 0xFD});  // 8000 NOP; 8001 JR 8000
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=0");
        const std::string first = run_to_stop(c);
        c.send_once("run\n");
        for (int i = 0; i < 3; ++i) rig.tick();
        const bool ran_on = !rig.dbg->state().paused;
        const std::string plain = c.send_once("\n") + c.wait(8);
        c.cmd("set-register A=1");
        c.send_once("run\n");
        for (int i = 0; i < 2; ++i) rig.tick();
        const std::string quiet = c.send_once("\n") + c.wait(8);   // A=1 throughout: no edge
        c.cmd("set-register A=0");
        const std::string again = run_to_stop(c);
        check("ZRCP-BP-22", "On Change: a PC-free condition that stays true fires once (the first "
                            "run stops on A=0) and the next run runs on while it stays true; "
                            "after a stretch where it was false, it fires again",
              first.find("Breakpoint fired: A=0\n") != std::string::npos && ran_on &&
                  plain.find("Breakpoint fired") == std::string::npos &&
                  quiet.find("Breakpoint fired") == std::string::npos &&
                  again.find("Breakpoint fired: A=0\n") != std::string::npos,
              esc(first, 80) + " / " + esc(plain, 80) + " / " + esc(again, 80));
    }
    {
        // B2: a print action on a condition that stays true prints ONCE, not
        // at every instruction (the review measured 984 255 lines in 3 s).
        Rig rig;
        rig.load({0x00, 0x18, 0xFD});
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpointaction 1 prints hello");
        c.cmd("set-breakpoint 1 A=0");
        std::string got = c.send_once("run\n");
        for (int i = 0; i < 6; ++i) {
            rig.tick();
            got += c.p->take();
        }
        const std::string stop = c.send_once("\n") + c.wait(8);
        std::size_t lines = 0;
        for (std::size_t at = got.find("log> hello\n"); at != std::string::npos;
             at = got.find("log> hello\n", at + 1))
            ++lines;
        check("ZRCP-BP-23", "a print action on a PC-free condition that stays true for six "
                            "frames prints exactly once (On Change), and the run goes on until "
                            "data stops it",
              lines == 1 && starts_with(got, kRunning) && ends_with(stop, PROMPT_STEP) &&
                  stop.find("Breakpoint fired") == std::string::npos,
              "lines=" + std::to_string(lines) + " " + esc(got, 120));
    }
    {
        // On Change inside run n too: the landing check is the slot's one
        // evaluation per boundary there.
        Rig rig;
        rig.load({0x00, 0x18, 0xFD});
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=0");
        const std::string r1 = c.cmd("run 10", 32);
        const std::string r2 = c.cmd("run 10", 32);
        check("ZRCP-BP-24", "run n: a PC-free condition that becomes true ends the first run n "
                            "after one opcode with its fired line; while it stays true the next "
                            "run n runs all 10",
              r1.find("Breakpoint fired: A=0\n") != std::string::npos &&
                  r1.find("Returning after") == std::string::npos &&
                  r2.find("Returning after 10 opcodes\n") != std::string::npos &&
                  r2.find("Breakpoint fired") == std::string::npos,
              esc(r1, 100) + " / " + esc(r2, 100));
    }
    {
        // What does NOT count as another client's breakpoint in run n: a
        // disabled one, and a Log-only subscription.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"gdb", ClientKind::Test}).value;
        jnext::dbg::Subscription off;
        off.kind      = jnext::dbg::EventKind::Execute;
        off.filter.lo = off.filter.hi = 0x8003;
        off.enabled   = false;
        rig.dbg->subscribe(other, off);
        jnext::dbg::Subscription log = off;
        log.filter.lo = log.filter.hi = 0x8004;
        log.enabled   = true;
        log.action    = jnext::dbg::Action::Log;
        rig.dbg->subscribe(other, log);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=99");
        const std::string r = c.cmd("run 6", 32);
        check("ZRCP-BP-25", "run n with a PC-free slot armed runs past another client's DISABLED "
                            "breakpoint (8003) and its Log-only subscription (8004): all 6 "
                            "opcodes",
              rig.pc() == 0x8006 && r.find("Returning after 6 opcodes\n") != std::string::npos,
              esc(r, 160));
        rig.dbg->detach(other);
    }
    {
        // run n honours another client's CONDITIONAL breakpoint as the backend
        // would: stops where its condition holds, runs past it where it does
        // not (probe_execute evaluates it).
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"gdb", ClientKind::Test}).value;
        jnext::dbg::Subscription s;
        s.kind      = jnext::dbg::EventKind::Execute;
        s.filter.lo = s.filter.hi = 0x8003;
        s.condition = [](const jnext::dbg::Event&, const Debugger& d) {
            return (d.registers().AF >> 8) == 0x42;
        };
        rig.dbg->subscribe(other, s);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 B=99");          // a PC-free slot of this session, false
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        const std::string past = c.cmd("run 6", 32);
        const auto pc1 = rig.pc();
        z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        z.AF = 0x4200;
        rig.emu.cpu().set_registers(z);
        const std::string stop = c.cmd("run 6", 32);
        check("ZRCP-BP-28", "run n and another client's conditional breakpoint at 8003: with "
                            "its condition false the 6 opcodes run past it; with it true the run "
                            "stops there, no fired line",
              pc1 == 0x8006 && past.find("Returning after 6 opcodes\n") != std::string::npos &&
                  rig.pc() == 0x8003 && stop.find("Returning after") == std::string::npos &&
                  stop.find("Breakpoint fired") == std::string::npos,
              esc(past, 120) + " / " + esc(stop, 120));
        rig.dbg->detach(other);
    }
    {
        // A legacy BreakpointSet PC breakpoint (probe_execute's EVENT_NONE
        // entry) ends a run n too, with a PC-free slot of this session armed.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        rig.emu.debug_state().breakpoints().add_pc(0x8003);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 B=99");
        const std::string r = c.cmd("run 6", 32);
        check("ZRCP-BP-29", "run n stops at a legacy PC breakpoint (8003) with a PC-free slot "
                            "of this session armed: no fired line, no Returning line",
              rig.pc() == 0x8003 && r.find("Returning after") == std::string::npos &&
                  r.find("Breakpoint fired") == std::string::npos,
              esc(r, 160));
    }
    {
        // R2-2 (M2 review round 2): only a subscription that would STOP ends
        // run n. Another client's handler subscription (a DSL logger: its
        // handler returns Continue) does not, whatever its static action; a
        // static Stop one does.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const auto other = rig.dbg->attach({"dsl", ClientKind::Test}).value;
        int handled = 0;
        jnext::dbg::Subscription h;
        h.kind      = jnext::dbg::EventKind::Execute;
        h.filter.lo = h.filter.hi = 0x8002;
        h.action    = jnext::dbg::Action::Continue;
        h.handler   = [&](const jnext::dbg::Event&, Debugger&) {
            ++handled;
            return jnext::dbg::Action::Continue;
        };
        rig.dbg->subscribe(other, h);
        jnext::dbg::Subscription hs = h;   // a handler with a static Stop
        hs.filter.lo = hs.filter.hi = 0x8003;
        hs.action    = jnext::dbg::Action::Stop;
        rig.dbg->subscribe(other, hs);
        jnext::dbg::Subscription stop;
        stop.kind      = jnext::dbg::EventKind::Execute;
        stop.filter.lo = stop.filter.hi = 0x8005;
        rig.dbg->subscribe(other, stop);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 B=99");
        const std::string r = c.cmd("run 7", 32);
        check("ZRCP-BP-30", "run n runs past another client's handler subscriptions (8002, "
                            "Continue; 8003, static Stop overridden by its handler) and stops at "
                            "its static-Stop breakpoint (8005); no handler ran in run n",
              rig.pc() == 0x8005 && r.find("Returning after") == std::string::npos &&
                  r.find("Breakpoint fired") == std::string::npos && handled == 0,
              esc(r, 160) + " handled=" + std::to_string(handled));
        rig.dbg->detach(other);
    }
    {
        // On-Change state is keyed to the boundary: extra evaluations of the
        // condition at the SAME cycle (a probe_execute) never advance it.
        Rig rig;
        rig.load({0x00, 0x18, 0xFD});
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=0");
        const std::string first = run_to_stop(c);          // the edge: fires
        z = rig.emu.cpu().get_registers();
        z.AF = 0x0100;                                      // A=1, at the same boundary
        rig.emu.cpu().set_registers(z);
        for (int i = 0; i < 3; ++i) rig.dbg->probe_execute(rig.pc());
        z.AF = 0x0000;                                      // A=0 again
        rig.emu.cpu().set_registers(z);
        c.send_once("run\n");
        for (int i = 0; i < 3; ++i) rig.tick();
        const bool ran_on = !rig.dbg->state().paused;
        const std::string plain = c.send_once("\n") + c.wait(8);
        check("ZRCP-BP-31", "probing the slot's condition three times at the stop's own "
                            "boundary (with A momentarily 1) does not move its On-Change state: "
                            "with A=0 throughout the machine's boundaries, the next run runs on",
              first.find("Breakpoint fired: A=0\n") != std::string::npos && ran_on &&
                  plain.find("Breakpoint fired") == std::string::npos,
              esc(first, 80) + " / " + esc(plain, 80));
    }
    {
        // The fired-line fallback names a PC-free slot only when it fired at
        // this boundary (its edge), not merely because it is true there.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        const auto other = rig.dbg->attach({"gdb", ClientKind::Test}).value;
        jnext::dbg::Subscription s3;
        s3.kind      = jnext::dbg::EventKind::Execute;
        s3.filter.lo = s3.filter.hi = 0x8003;
        rig.dbg->subscribe(other, s3);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=0");
        const std::string first  = run_to_stop(c);   // the edge, at 8001
        const std::string second = run_to_stop(c);   // A=0 still: the other's, at 8003
        check("ZRCP-BP-26", "a PC-free slot that fired once and is still true is not named when "
                            "another client's breakpoint stops the machine (8003): plain stop",
              fired_stop(first, 0x8001, "A=0") && rig.pc() == 0x8003 &&
                  second.find("Breakpoint fired") == std::string::npos &&
                  ends_with(second, PROMPT_STEP),
              esc(first, 80) + " / " + esc(second, 120));
        rig.dbg->detach(other);
    }
    {
        // A slot re-set from a PC-free condition to a fast-path one starts
        // fresh: no On-Change state of the old condition survives.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        Z80Registers z = rig.emu.cpu().get_registers();
        z.AF = 0x0000;
        rig.emu.cpu().set_registers(z);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-breakpoint 1 A=0");
        c.cmd("run 2", 32);                          // the edge: its state is now "true"
        c.cmd("set-breakpoint 1 PC=8003H");
        z = rig.emu.cpu().get_registers();
        z.PC = PROG;
        rig.emu.cpu().set_registers(z);
        const std::string r = c.cmd("run 6", 32);
        check("ZRCP-BP-27", "re-set from A=0 (fired) to PC=8003H, run n stops at 8003 with the "
                            "new condition's fired line",
              rig.pc() == 0x8003 && r.find("Breakpoint fired: PC=8003H\n") != std::string::npos,
              esc(r, 160));
    }
    {
        // A bare native variable is a condition on its value: SEG3 is 0 on
        // the 48K, so it never fires.
        Rig rig;
        rig.load({0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        const std::string set = c.cmd("set-breakpoint 1 SEG3");
        c.send_once("run\n");
        for (int i = 0; i < 4; ++i) rig.tick();
        const bool running = !rig.dbg->state().paused;
        const std::string stop = c.send_once("\n") + c.wait(8);
        check("ZRCP-BP-20", "a bare SEG3 condition (0 on the 48K) is accepted and never fires: "
                            "data stops the run at JR $",
              set == reply_of("", true) && running && is_stop_shape(stop, 0x8002), esc(stop));
    }
}

static void wp4_mem_rows() {
    {
        // Another client's watch, subscribed first, on the same write.
        Rig rig;
        rig.load({0x3E, 0xAA, 0x32, 0x01, 0x90, 0x18, 0xFE});  // LD A,AAH; LD (9001H),A; JR $
        const auto other = rig.dbg->attach({"gui", ClientKind::Test}).value;
        jnext::dbg::Subscription w;
        w.kind      = jnext::dbg::EventKind::Mem;
        w.filter.lo = w.filter.hi = 0x9001;
        w.access    = jnext::dbg::Access::Write;
        rig.dbg->subscribe(other, w);
        Zc c(rig);
        bp_on(c);
        c.cmd("set-membreakpoint 9000h 3 4");
        const std::string r = run_to_stop(c);
        const auto by = rig.dbg->state().pause_reason.by;
        rig.dbg->detach(other);
        c.cmd("clear-membreakpoints");
        Rig rig2;
        rig2.load({0x3E, 0xAA, 0x32, 0x01, 0x90, 0x18, 0xFE});
        const auto other2 = rig2.dbg->attach({"gui", ClientKind::Test}).value;
        rig2.dbg->subscribe(other2, w);
        Zc c2(rig2);
        bp_on(c2);
        c2.cmd("set-membreakpoint 9000h 1 4");  // reads only: not this write
        const std::string r2 = run_to_stop(c2);
        check("ZRCP-MBP-08", "another client's watch, first, on the same write: the stop still "
                             "names this session's covering range; a range of this session for "
                             "reads only does not claim a write",
              by == other && fired_stop(r, 0x8005, "Memory Breakpoint Write Address: 9001H") &&
                  starts_with(r2, kRunning) && is_stop_shape(r2.substr(kRunning.size()), 0x8005),
              esc(r) + " / " + esc(r2));
        rig2.dbg->detach(other2);
    }
    {
        Rig rig;
        rig.load({0x3E, 0xAA, 0x32, 0x01, 0x90, 0x18, 0xFE});  // LD A,AAH; LD (9001H),A; JR $
        Zc c(rig);
        bp_on(c);
        const std::string set = c.cmd("set-membreakpoint 9000h 2 2");
        const std::string r   = run_to_stop(c);
        check("ZRCP-MBP-01", "set-membreakpoint <addr>h 2 2 then run: \"Breakpoint fired: Memory "
                             "Breakpoint Write Address: 9001H\" with the address written (§6.2 "
                             "item 5)",
              set == reply_of("", true) &&
                  fired_stop(r, 0x8005, "Memory Breakpoint Write Address: 9001H"),
              esc(r));
    }
    {
        Rig rig;
        rig.load({0x3A, 0x00, 0x90, 0x00, 0x18, 0xFE});  // LD A,(9000H); NOP; JR $
        Zc c(rig);
        bp_on(c);
        c.cmd("set-membreakpoint 9000H 1");
        const std::string r = run_to_stop(c);
        check("ZRCP-MBP-02", "type 1 fires on a read: Memory Breakpoint Read Address: 9000H",
              fired_stop(r, 0x8003, "Memory Breakpoint Read Address: 9000H"), esc(r));
    }
    {
        Rig rig;
        rig.load({0x3E, 0xAA, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x00, 0x18, 0xFE});
        Zc c(rig);
        bp_on(c);
        c.cmd("set-membreakpoint 9000H 4");  // neither read nor write: listed, never fires
        const std::string listed = c.cmd("get-membreakpoints 9000H");
        c.send_once("run\n");
        for (int i = 0; i < 4; ++i) rig.tick();
        const bool running = !rig.dbg->state().paused;
        c.send_once("\n");
        c.wait(8);
        check("ZRCP-MBP-03", "a type with neither bit (4) is stored and listed, and fires on "
                             "nothing",
              listed == "Breakpoints: On\n9000H : 4\n\ncommand@cpu-step> " && running,
              esc(listed));
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        c.cmd("set-membreakpoint 4000h 3 3");
        c.cmd("set-membreakpoint 5000h 1");
        const std::string all  = c.cmd("get-membreakpoints");
        const std::string one  = c.cmd("get-membreakpoints 4001H");
        const std::string zero = c.cmd("get-membreakpoints 4100H");
        const std::string two  = c.cmd("get-membreakpoints 4001H 2");
        const std::string bad1 = c.cmd("get-membreakpoints 10000H");
        const std::string bad2 = c.cmd("get-membreakpoints 0 65537");
        check("ZRCP-MBP-04", "get-membreakpoints [address] [items]: every non-zero address as "
                             "\"%04XH : type\"; an address alone gives that one, 0 included; with "
                             "items that many non-zero ones from it; ZEsarUX's range errors",
              all == "Breakpoints: On\n4000H : 3\n4001H : 3\n4002H : 3\n5000H : 1\n\n"
                     "command@cpu-step> " &&
                  one == "Breakpoints: On\n4001H : 3\n\ncommand@cpu-step> " &&
                  zero == "Breakpoints: On\n4100H : 0\n\ncommand@cpu-step> " &&
                  two == "Breakpoints: On\n4001H : 3\n4002H : 3\n\ncommand@cpu-step> " &&
                  bad1 == reply_of("ERROR. Address out of range", true) &&
                  bad2 == reply_of("ERROR. Items out of range", true),
              esc(all) + " / " + esc(two));
    }
    {
        // §4.2 — the range diff: adjacent runs, a removal in the middle, a
        // run that did not change keeps its subscription.
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        const auto mems = [&]() {
            std::vector<jnext::dbg::SubscriptionInfo> out;
            for (const auto& s : rig.dbg->subscriptions(false))
                if (s.kind == jnext::dbg::EventKind::Mem) out.push_back(s);
            std::sort(out.begin(), out.end(),
                      [](const auto& a, const auto& b) { return a.filter.lo < b.filter.lo; });
            return out;
        };
        c.cmd("set-membreakpoint 9000H 1 2");
        c.cmd("set-membreakpoint 9002H 2 2");   // adjacent, a different type: two runs
        c.cmd("set-membreakpoint A000H 3 1");
        const auto m1 = mems();
        c.cmd("set-membreakpoint 9001H 0 2");   // overlaps both runs
        const auto m2 = mems();
        const bool shape1 = m1.size() == 3 && m1[0].filter.lo == 0x9000 && m1[0].filter.hi == 0x9001 &&
                            m1[0].access == jnext::dbg::Access::Read && m1[1].filter.lo == 0x9002 &&
                            m1[1].filter.hi == 0x9003 && m1[1].access == jnext::dbg::Access::Write &&
                            m1[2].filter.lo == 0xA000 && m1[2].access == jnext::dbg::Access::ReadWrite;
        const bool shape2 = m2.size() == 3 && m2[0].filter.lo == 0x9000 && m2[0].filter.hi == 0x9000 &&
                            m2[1].filter.lo == 0x9003 && m2[1].filter.hi == 0x9003 &&
                            m2[2].filter.lo == 0xA000 && m2[2].id == m1[2].id &&
                            m2[0].id != m1[0].id;
        check("ZRCP-MBP-05", "§4.2: maximal runs of equal type become one Mem subscription each "
                             "(read / write / both), adjacent runs of different type stay apart, "
                             "a type-0 removal across two runs trims both, and an unchanged run "
                             "keeps its subscription",
              shape1 && shape2,
              "m1=" + std::to_string(m1.size()) + " m2=" + std::to_string(m2.size()));
        c.cmd("set-membreakpoint FFFFH 2 2");
        const std::string wrap = c.cmd("get-membreakpoints 0 1");
        const std::string w2   = c.cmd("get-membreakpoints FFFFH");
        c.cmd("clear-membreakpoints");
        const std::string cleared = c.cmd("get-membreakpoints");
        check("ZRCP-MBP-06", "the address wraps at FFFFH (FFFF and 0000 set); "
                             "clear-membreakpoints empties the map and the backend",
              wrap == "Breakpoints: On\n0000H : 2\n\ncommand@cpu-step> " &&
                  w2 == "Breakpoints: On\nFFFFH : 2\n\ncommand@cpu-step> " &&
                  cleared == "Breakpoints: On\n\ncommand@cpu-step> " && mems().empty(),
              esc(wrap) + " / " + esc(cleared));
    }
    {
        Rig rig;
        Zc  c(rig);
        bp_on(c);
        const std::string few  = c.cmd("set-membreakpoint 9000H");
        const std::string addr = c.cmd("set-membreakpoint 10001H 1");
        const std::string max  = c.cmd("set-membreakpoint 10000H 1");
        const std::string type = c.cmd("set-membreakpoint 9000H 256");
        const std::string at0  = c.cmd("get-membreakpoints 0");
        c.cmd("disable-breakpoints");
        int mems = 0;
        for (const auto& s : rig.dbg->subscriptions(false)) mems += s.kind == jnext::dbg::EventKind::Mem;
        const std::string kept = c.cmd("get-membreakpoints");
        c.cmd("enable-breakpoints");
        int rearmed = 0;
        for (const auto& s : rig.dbg->subscriptions(false))
            rearmed += s.kind == jnext::dbg::EventKind::Mem;
        c.cmd("disable-breakpoints");
        check("ZRCP-MBP-07", "set-membreakpoint's errors are ZEsarUX's (two parameters, address "
                             "0..10000H — 10000H is 0000 —, type 0..255); disable-breakpoints "
                             "disarms the memory breakpoints and keeps the map, and "
                             "enable-breakpoints re-arms them",
              few == reply_of("ERROR. Needs two parameters minimum", true) &&
                  addr == reply_of("ERROR. Address out of range", true) &&
                  max == reply_of("", true) && type == reply_of("ERROR. Type out of range", true) &&
                  at0 == "Breakpoints: On\n0000H : 1\n\ncommand@cpu-step> " && mems == 0 &&
                  kept == "Breakpoints: Off\n0000H : 1\n\ncommand@cpu-step> " && rearmed == 1,
              esc(few) + " / " + esc(addr) + " / " + esc(kept));
    }
}

// ===========================================================================
// WP-5 — history, extended stack, coverage, load (§1.7, §2.2-2.3, §4.1)
// ===========================================================================

/// A scratch file in /tmp unique to this process (concurrent runs from other
/// worktrees share /tmp).
static std::string zrcp_tmp(const char* stem, const char* ext) {
    return std::string("/tmp/jnext_zrcp_") + stem + "_" + std::to_string(::getpid()) + ext;
}

static void wp5_history_rows() {
    {
        // history_line, byte for byte, from a synthetic entry (every field
        // distinct, so a swapped pair or byte shows).
        ::TraceEntry e{};
        e.pc = 0x1234; e.sp = 0x5678; e.af = 0x9ABC; e.bc = 0xDEF0; e.hl = 0x1357;
        e.de = 0x2468; e.ix = 0xA1B2; e.iy = 0xC3D4; e.af2 = 0xE5F6; e.bc2 = 0x0718;
        e.hl2 = 0x293A; e.de2 = 0x4B5C; e.i = 0x6D; e.r = 0x7E; e.im = 2; e.iff1 = 1;
        e.iff2 = 0; e.opcode_bytes[0] = 0xED; e.opcode_bytes[1] = 0xB0;
        e.opcode_bytes[2] = 0x01; e.opcode_bytes[3] = 0x02; e.sp_word = 0xBEEF;
        const std::array<std::uint16_t, 8> mmu{{0x8000, 0x8001, 0x000a, 0x000b,
                                                0x0004, 0x0005, 0x0000, 0x0001}};
        const std::string l = history_line(e, mmu);
        e.iff1 = 0; e.iff2 = 1;
        const std::string l2 = history_line(e, mmu);
        check("ZRCP-FMT-14", "a cpu-history line byte for byte (ZEsarUX's "
                             "cpu_history_legacy_regs_bin_to_string): HL before DE, IM%d IFF%c%c, "
                             "(PC)= the opcode bytes in fetch order, (SP)= the word, eight MMU "
                             "values, a trailing space",
              l == "PC=1234 SP=5678 AF=9abc BC=def0 HL=1357 DE=2468 IX=a1b2 IY=c3d4 AF'=e5f6 "
                   "BC'=0718 HL'=293a DE'=4b5c I=6d R=7e IM2 IFF1- (PC)=edb00102 (SP)=beef "
                   "MMU=80008001000a000b0004000500000001 " &&
                  l2.find(" IFF-2 ") != std::string::npos,
              esc(l, 300));
    }
    {
        Rig rig;
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        const std::string none  = c.cmd("cpu-history");
        const std::string get   = c.cmd("cpu-history get 0");
        const std::string size  = c.cmd("cpu-history get-size");
        const std::string is    = c.cmd("cpu-history is-enabled");
        const std::string off   = c.cmd("cpu-history enabled no");
        const std::string bogus = c.cmd("cpu-history frobnicate");
        check("ZRCP-HIS-01", "cpu-history off: get / get-size answer ZEsarUX's \"Error. It's not "
                             "enabled\\n\" (its newline included), is-enabled 0, enabled no "
                             "\"Error. Already disabled\", an unknown action and no action their "
                             "errors",
              none == reply_of("ERROR. Needs at least one parameter", true) &&
                  get == reply_of("Error. It's not enabled\n", true) &&
                  size == reply_of("Error. It's not enabled\n", true) &&
                  is == reply_of("0", true) && off == reply_of("Error. Already disabled", true) &&
                  bogus == reply_of("Error. Unknown parameter", true),
              esc(get) + " / " + esc(off));
    }
    {
        // Three steps: 8000 LD A,5 / 8002 LD B,7 / 8004 NOP.
        Rig rig;
        rig.load({0x3E, 0x05, 0x06, 0x07, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string on  = c.cmd("cpu-history enabled yes");
        const std::string on2 = c.cmd("cpu-history enabled yes");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        const std::string size = c.cmd("cpu-history get-size");
        const std::string g0   = c.cmd("cpu-history get 0");
        const std::string g2   = c.cmd("cpu-history get 2");
        const std::string g3   = c.cmd("cpu-history get 3");
        check("ZRCP-HIS-02", "after three steps: get-size 3, get 0 is the NEWEST entry (8004) and "
                             "get 2 the oldest (8000); get 3 \"ERROR: index out of range\"; a "
                             "second enabled yes \"Error. Already enabled\"",
              on == reply_of("", true) && on2 == reply_of("Error. Already enabled", true) &&
                  size == reply_of("3", true) && starts_with(g0, "PC=8004 ") &&
                  starts_with(g2, "PC=8000 ") &&
                  g3 == reply_of("ERROR: index out of range", true),
              esc(g0, 60) + " / " + esc(g2, 60) + " / " + esc(g3));

        // The entry's fields where DeZog reads them.
        const std::string regs = c.cmd("get-registers");
        const auto mmu_at = regs.find("MMU=");
        const std::string mmu = mmu_at == std::string::npos ? "" : regs.substr(mmu_at, 36);
        const std::string body = g0.substr(0, g0.size() - std::strlen(PROMPT_STEP) - 1);
        check("ZRCP-HIS-03", "get 0 as DeZog reads it: registers BEFORE the instruction (A=05, "
                             "B=07 after the two loads), (PC)= its four fetched bytes 0018FE.., "
                             "(SP)= the word at SP, the get-registers MMU= projection, and the "
                             "trailing space ZEsarUX leaves",
              starts_with(body, "PC=8004 SP=ff00 AF=05") && body.find(" BC=07") != std::string::npos &&
                  body.find(" (PC)=0018fe") != std::string::npos &&
                  body.find(" (SP)=") != std::string::npos && !mmu.empty() &&
                  body.find(" " + mmu + " ") != std::string::npos && ends_with(body, " ") &&
                  body.find('\n') == std::string::npos,
              esc(body, 220) + " / " + esc(mmu));

        const std::string pcs   = c.cmd("cpu-history get-pc 0 3");
        const std::string clamp = c.cmd("cpu-history get-pc 1 9");
        const std::string neg   = c.cmd("cpu-history get-pc 2 2");
        check("ZRCP-HIS-04", "get-pc start n: PCs from start towards the oldest, \"%04x \" each; n "
                             "clamped to the history's size (not to what is left of it: ZEsarUX's "
                             "rule), and past the oldest its per-element error",
              pcs == reply_of("8004 8002 8000 ", true) &&
                  clamp == reply_of("8002 8000 ERROR: index can't be negative ", true) &&
                  neg == reply_of("8000 ERROR: index can't be negative ", true),
              esc(pcs) + " / " + esc(clamp) + " / " + esc(neg));

        const std::string bad0 = c.cmd("cpu-history set-max-size 0");
        const std::string big  = c.cmd("cpu-history set-max-size 1000001");
        const std::string two  = c.cmd("cpu-history set-max-size 2");
        const std::string max  = c.cmd("cpu-history get-max-size");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        const std::string after = c.cmd("cpu-history get-size");
        const std::string clr   = c.cmd("cpu-history clear");
        const std::string empty = c.cmd("cpu-history get-size");
        check("ZRCP-HIS-05", "set-max-size 1..1000000 (else \"ERROR: Value out of range\"), "
                             "get-max-size reads it back, the history then holds at most that "
                             "many; clear empties it",
              bad0 == reply_of("ERROR: Value out of range", true) &&
                  big == reply_of("ERROR: Value out of range", true) && two == reply_of("", true) &&
                  max == reply_of("2", true) && after == reply_of("2", true) &&
                  clr == reply_of("", true) && empty == reply_of("0", true),
              esc(max) + " / " + esc(after) + " / " + esc(empty));

        const std::string st  = c.cmd("cpu-history started yes");
        const std::string ist = c.cmd("cpu-history is-started");
        const std::string rs  = c.cmd("cpu-history restore 0");
        const std::string ge  = c.cmd("cpu-history get-extended 0");
        check("ZRCP-HIS-06", "started yes / is-started 1; restore and get-extended are declined "
                             "by name with the reason, in plain ASCII",
              st == reply_of("", true) && ist == reply_of("1", true) &&
                  starts_with(rs, "Error. Unsupported in jnext: cpu-history restore") &&
                  starts_with(ge, "Error. Unsupported in jnext: cpu-history get-extended") &&
                  std::all_of(rs.begin(), rs.end(), [](char ch) { return (ch & 0x80) == 0; }) &&
                  std::all_of(ge.begin(), ge.end(), [](char ch) { return (ch & 0x80) == 0; }),
              esc(rs) + " / " + esc(ge));
    }
    {
        // ignrepldxr: a run of LDIR iterations shows its first entry only.
        // 8000 LD BC,3 / LD HL,9000 / LD DE,9100 / LDIR / NOP / JR $
        Rig rig;
        rig.load({0x01, 0x03, 0x00, 0x21, 0x00, 0x90, 0x11, 0x00, 0x91, 0xED, 0xB0, 0x00,
                  0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("run 7", 32);  // 3 loads, 3 LDIR iterations, NOP
        const std::string all = c.cmd("cpu-history get-size");
        c.cmd("cpu-history ignrepldxr yes");
        const std::string one = c.cmd("cpu-history get-size");
        const std::string pcs = c.cmd("cpu-history get-pc 0 3");
        c.cmd("cpu-history ignrepldxr no");
        const std::string back = c.cmd("cpu-history get-size");
        check("ZRCP-HIS-07", "ignrepldxr yes: of three consecutive LDIR entries only the first is "
                             "listed (7 -> 5, the newest three are NOP, LDIR, LD DE); no shows "
                             "them all again",
              all == reply_of("7", true) && one == reply_of("5", true) &&
                  pcs == reply_of("800b 8009 8006 ", true) && back == reply_of("7", true),
              esc(all) + " / " + esc(one) + " / " + esc(pcs));
    }
    {
        // ... and of LDDR (ED B8) as of LDIR.
        Rig rig;
        rig.load({0x01, 0x03, 0x00, 0x21, 0x02, 0x90, 0x11, 0x02, 0x91, 0xED, 0xB8, 0x00,
                  0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("cpu-history ignrepldxr yes");
        c.cmd("run 7", 32);
        const std::string one = c.cmd("cpu-history get-size");
        check("ZRCP-HIS-10", "ignrepldxr yes collapses a run of LDDR iterations as it does LDIR "
                             "(7 executed, 5 listed)",
              one == reply_of("5", true), esc(one));
    }
    {
        // ignrephalt: consecutive HALT entries collapse to the first.
        Rig rig;
        rig.load({0x00, 0x76});  // NOP / HALT, interrupts off
        rig.dbg->trace_resize(100000);  // the machine's trace: one frame of HALTs fits
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("cpu-history set-max-size 100000");
        c.cmd("exit-cpu-step");
        rig.tick();
        c.cmd("enter-cpu-step");
        const std::string many = c.cmd("cpu-history get-size");
        c.cmd("cpu-history ignrephalt yes");
        const std::string few  = c.cmd("cpu-history get-size");
        const std::string pcs  = c.cmd("cpu-history get-pc 0 3");
        auto n = [](const std::string& r) { return std::atoi(r.c_str()); };
        check("ZRCP-HIS-08", "ignrephalt yes: a HALT recorded again and again is listed once: "
                             "the history is the NOP and the first HALT",
              n(many) > 2 && few == reply_of("2", true) &&
                  pcs == reply_of("8001 8000 ", true),
              esc(many) + " / " + esc(few) + " / " + esc(pcs));
    }
    {
        // The trace is the machine's: what the session did not turn on, it
        // does not turn off; what it turned on goes off with it.
        Rig rig;
        rig.dbg->set_trace_enabled(true);
        {
            Zc c(rig);
            c.cmd("cpu-history enabled yes");
            c.cmd("cpu-history enabled no");
            c.p->send("quit\n");
            c.wait(4);
        }
        const bool kept = rig.dbg->trace_enabled();
        rig.dbg->set_trace_enabled(false);
        bool on_mid = false;
        {
            Zc c(rig);
            c.cmd("cpu-history enabled yes");
            on_mid = rig.dbg->trace_enabled();
            c.p->send("quit\n");
            c.wait(4);
        }
        check("ZRCP-HIS-09", "a trace another client had on stays on through enabled yes / no; "
                             "one this session turned on goes off when it quits",
              kept && on_mid && !rig.dbg->trace_enabled());
    }
    {
        // B2 (M3 review): clear and set-max-size act on the session's VIEW.
        // The machine's trace — which Step Back and rewind read — keeps every
        // entry and its capacity, and Step Back still reaches past the clear.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        rig.dbg->set_rewind_enabled(true);
        rig.emu.resize_rewind_buffer(8);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");          // the trace is rewind's: not owned
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");                          // PC 8003
        const auto before = rig.dbg->trace_entries();
        const std::size_t n_before = before ? before.value.size() : 0;
        const std::string clr  = c.cmd("cpu-history clear");
        const std::string zero = c.cmd("cpu-history get-size");
        const std::string max  = c.cmd("cpu-history set-max-size 2");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");                          // PC 8006
        const std::string two  = c.cmd("cpu-history get-size");
        const std::string pcs  = c.cmd("cpu-history get-pc 0 2");
        const auto after = rig.dbg->trace_entries();
        const std::size_t n_after = after ? after.value.size() : 0;
        const Result back = rig.dbg->step_back(rig.dbg->attach({"gui", ClientKind::Test}).value, 5);
        const auto pc_back = rig.pc();
        check("ZRCP-HIS-11", "with rewind on: the session's clear empties ITS history (0) and "
                             "set-max-size 2 caps ITS view (the newest 2), while the machine's "
                             "trace keeps all its entries (3 -> 6) and Step Back of 5 lands at "
                             "8001, before the clear",
              clr == reply_of("", true) && zero == reply_of("0", true) &&
                  max == reply_of("", true) && two == reply_of("2", true) &&
                  pcs == reply_of("8005 8004 ", true) && n_before == 3 && n_after == 6 &&
                  back == Result::Ok && pc_back == 0x8001,
              "trace " + std::to_string(n_before) + "->" + std::to_string(n_after) + " back=" +
                  std::to_string(static_cast<int>(back)) + " pc=" + std::to_string(pc_back) +
                  " " + esc(pcs));
    }
    {
        // H09: a slot that is ROM now is taken as ROM for an entry only if the
        // entry recorded the same page. On the Next, slot 0 holds RAM page 4
        // when the entry is recorded and ROM again when it is read.
        Rig rig(MachineType::ZXN_ISSUE2);
        Zc  c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("tbblue-set-register 80 4");          // NR 0x50: slot 0 = RAM page 4
        c.cmd("cpu-step");
        c.cmd("tbblue-set-register 80 255");        // slot 0 = ROM again
        const std::string g = c.cmd("cpu-history get 0");
        const auto at = g.find(" MMU=");
        check("ZRCP-HIS-12", "history MMU=: an entry recorded with RAM page 4 in slot 0 shows "
                             "0004 there although slot 0 is ROM now (a different page)",
              at != std::string::npos && g.compare(at + 5, 4, "0004") == 0, esc(g, 260));
    }
    {
        Rig rig;
        rig.load({0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        const std::string neg  = c.cmd("cpu-history get-pc 0 -1");
        const std::string negi = c.cmd("cpu-history get -1");
        check("ZRCP-HIS-13", "a negative get-pc count answers ZEsarUX's \"Error. Can't be "
                             "negative\"; get -1 is past the newest: \"ERROR: index beyond total "
                             "elements (2)\"",
              neg == reply_of("Error. Can't be negative", true) &&
                  negi == reply_of("ERROR: index beyond total elements (2)", true),
              esc(neg) + " / " + esc(negi));
    }
    {
        // R2-1 (M3 review round 2): a cold boot restarts the clock, so the
        // clear base (a cycle) is dropped; the new machine's steps are shown.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x18, 0xFE});
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            // As the loop owner's: a reconstructed machine, a fresh trace.
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            rig.emu.trace_log().clear();
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("hard-reset-cpu");
        for (int k = 0; k < 5; ++k) c.cmd("cpu-step");
        c.cmd("cpu-history clear");                // base: the 5th step from a cold boot
        c.cmd("hard-reset-cpu");
        for (int k = 0; k < 8; ++k) c.cmd("cpu-step");  // the 5th repeats the base's cycle
        const std::string n8 = c.cmd("cpu-history get-size");
        check("ZRCP-HIS-14", "the reviewer's repro: hard-reset-cpu, 5 steps, clear, hard-reset-cpu, "
                             "8 steps — the cold boot replays the same cycles, yet the history "
                             "shows all 8 (it showed 3)",
              n8 == reply_of("8", true), esc(n8));
    }
    {
        // R2-2 / U01: a base that has left the trace (here another client's
        // Clear Trace) means every entry is newer than the clear.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        rig.dbg->set_trace_enabled(true);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("cpu-history enabled yes");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-history clear");
        rig.dbg->trace_clear();                   // the machine's trace: the base is gone
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        const std::string n = c.cmd("cpu-history get-size");
        const std::string pcs = c.cmd("cpu-history get-pc 0 3");
        check("ZRCP-HIS-15", "after the clear's base entry has left the machine's trace (another "
                             "client cleared it), the three later steps are all shown",
              n == reply_of("3", true) && pcs == reply_of("8004 8003 8002 ", true),
              esc(n) + " / " + esc(pcs));
    }
    {
        // R2-2 / U05: the base is the session's: the next session sees the
        // trace whole.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x18, 0xFE});
        rig.dbg->set_trace_enabled(true);
        {
            Zc c(rig);
            c.cmd("enter-cpu-step");
            c.cmd("cpu-history enabled yes");
            c.cmd("cpu-step");
            c.cmd("cpu-step");
            c.cmd("cpu-history clear");
            c.p->send("quit\n");
            c.wait(4);
        }
        Zc c2(rig);
        c2.cmd("cpu-history enabled yes");
        const std::string n = c2.cmd("cpu-history get-size");
        check("ZRCP-HIS-16", "a session's clear ends with it: the next session's history holds "
                             "the two steps the first one cleared from its own view",
              n == reply_of("2"), esc(n));
    }
    {
        // A restored snapshot moves the clock back, and the re-run steps
        // repeat the cleared base's cycle: the base is dropped, not matched.
        Rig rig;
        rig.load({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");                  // a frame boundary
        c.cmd("snapshot-save s");
        c.cmd("cpu-history enabled yes");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-history clear");               // base: the second step's cycle
        c.cmd("snapshot-load s");                 // the clock back to before both
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        c.cmd("cpu-step");                        // the second repeats the base's cycle
        const std::string pcs = c.cmd("cpu-history get-pc 0 3");
        check("ZRCP-HIS-17", "after snapshot-load moves the clock back, the three re-run steps "
                             "are all shown although one repeats the clear base's cycle",
              pcs == reply_of("8002 8001 8000 ", true), esc(pcs));
    }
}

static void wp5_stack_coverage_rows() {
    {
        // 8000 CALL 8010 / 8003 NOP / JR $ ; 8010 RST 08 ; 0008 is ROM.
        Rig rig;
        rig.load({0xCD, 0x10, 0x80, 0x00, 0x18, 0xFE});
        rig.load({0xCF}, 0x8010);
        rig.load({0xCD, 0x10, 0x80, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string off  = c.cmd("extended-stack get 2");
        const std::string on   = c.cmd("extended-stack enabled yes");
        c.cmd("cpu-step");                       // CALL 8010
        c.cmd("cpu-step");                       // RST 08
        const std::string two  = c.cmd("extended-stack get 3");
        const std::string at   = c.cmd("extended-stack get 1 FEFEH");
        const std::string clr  = c.cmd("extended-stack clear");
        const std::string none = c.cmd("extended-stack");
        const std::string bad  = c.cmd("extended-stack frob");
        check("ZRCP-XST-01", "extended-stack: off -> \"Error. It's not enabled\"; get n lists n "
                             "words from SP as %04XH and its type — the RST's return address "
                             "rst, the CALL's call, anything else default; get n index starts "
                             "at index; clear answers empty; ZEsarUX's errors",
              off == reply_of("Error. It's not enabled", true) && on == reply_of("", true) &&
                  two == "8011H rst\n8003H call\n0000H default\n\ncommand@cpu-step> " &&
                  at == "8003H call\n\ncommand@cpu-step> " && clr == reply_of("", true) &&
                  none == reply_of("ERROR. Needs at least one parameter", true) &&
                  bad == reply_of("Error. Unknown parameter", true),
              esc(two) + " / " + esc(at));
    }
    {
        // An accepted interrupt's return address is maskable_interrupt.
        // 8000 EI / 8001 HALT / 8002 JR $, IM 1: the step over the HALT runs it
        // out to the interrupt (GH #207), which pushes 8002.
        Rig rig;
        rig.load({0xFB, 0x76, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        c.cmd("set-register IM=1");
        c.cmd("extended-stack enabled yes");
        for (int i = 0; i < 6 && rig.pc() != 0x0038; ++i) c.cmd("cpu-step", 64);
        const std::string at  = c.cmd("get-registers");
        const std::string top = c.cmd("extended-stack get 1");
        check("ZRCP-XST-03", "after the HALT is run out to an IM 1 interrupt, the stacked return "
                              "address 8002 is typed maskable_interrupt",
              starts_with(at, "PC=0038 ") &&
                  top == "8002H maskable_interrupt\n\ncommand@cpu-step> ",
              esc(at, 30) + " / " + esc(top));
    }
    {
        Rig rig;
        rig.dbg->set_call_stack_enabled(true);
        {
            Zc c(rig);
            const std::string a = c.cmd("extended-stack enabled no");
            c.cmd("extended-stack enabled yes");
            c.cmd("extended-stack enabled no");
            check("ZRCP-XST-02", "call tracking another client had on stays on through this "
                                 "session's enabled yes / no (DeZog sends no first: \"Error. "
                                 "Already disabled\")",
                  a == reply_of("Error. Already disabled") && rig.dbg->call_stack_enabled());
        }
    }
    {
        // 8000 JP 8ABC ; 8ABC NOP / JR $
        Rig rig;
        rig.load({0x00, 0x18, 0xFE}, 0x8ABC);
        rig.load({0xC3, 0xBC, 0x8A});
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string off = c.cmd("cpu-code-coverage get");
        const std::string on  = c.cmd("cpu-code-coverage enabled yes");
        const std::string on2 = c.cmd("cpu-code-coverage enabled yes");
        c.cmd("cpu-step");
        c.cmd("cpu-step");
        const std::string got = c.cmd("cpu-code-coverage get");
        const std::string clr = c.cmd("cpu-code-coverage clear");
        const std::string emp = c.cmd("cpu-code-coverage get");
        const std::string no  = c.cmd("cpu-code-coverage enabled no");
        const std::string no2 = c.cmd("cpu-code-coverage enabled no");
        check("ZRCP-COV-01", "cpu-code-coverage: off -> \"Error. It's not enabled\"; the executed "
                             "addresses since clear as upper-case %04X and a space, ascending "
                             "(8000 8ABC); clear empties it; Already enabled / disabled",
              off == reply_of("Error. It's not enabled", true) && on == reply_of("", true) &&
                  on2 == reply_of("Error. Already enabled", true) &&
                  got == reply_of("8000 8ABC ", true) && clr == reply_of("", true) &&
                  emp == reply_of("", true) && no == reply_of("", true) &&
                  no2 == reply_of("Error. Already disabled", true) &&
                  !rig.dbg->coverage_enabled(),
              esc(got) + " / " + esc(emp));
    }
    {
        // B3 (M3 review): a session that turned call tracking and coverage on
        // turns them off when it ends — a hang-up, with no enabled no.
        Rig rig;
        bool xst_on = false, cov_on = false;
        {
            Zc c(rig);
            c.cmd("extended-stack enabled yes");
            c.cmd("cpu-code-coverage enabled yes");
            xst_on = rig.dbg->call_stack_enabled();
            cov_on = rig.dbg->coverage_enabled();
            c.p->close();
            for (int i = 0; i < 4; ++i) rig.pump();
        }
        check("ZRCP-XST-04", "a session that turned call tracking and coverage on, and hangs "
                             "up, leaves both off",
              xst_on && cov_on && !rig.dbg->call_stack_enabled() && !rig.dbg->coverage_enabled());
    }
}

static void wp5_load_rows() {
    {
        const std::string bin = zrcp_tmp("load", ".bin");
        {
            std::FILE* f = std::fopen(bin.c_str(), "wb");
            const std::uint8_t b[] = {0xDE, 0xAD, 0xBE, 0xEF};
            std::fwrite(b, 1, 4, f);
            std::fclose(f);
        }
        Rig rig;
        Zc  c(rig);
        const std::string all  = c.cmd("load-binary \"" + bin + "\" 36864 0");
        const std::string two  = c.cmd("load-binary " + bin + " 9100H 2");
        const std::string few  = c.cmd("load-binary " + bin + " 9000H");
        const std::string miss = c.cmd("load-binary /nonexistent/zrcp.bin 36864 0");
        const std::string addr = c.cmd("load-binary " + bin + " 10000H 0");
        const std::string got  = c.cmd("read-memory 36864 4");
        const std::string got2 = c.cmd("read-memory 37120 3");
        std::remove(bin.c_str());
        check("ZRCP-LOAD-01", "load-binary \"file\" address length: length 0 loads the whole file "
                              "(DeZog's form, decimal address), n loads n bytes; fewer than three "
                              "parameters, a missing file and an address past FFFFH are refused",
              all == reply_of("") && two == reply_of("") && got == reply_of("DEADBEEF") &&
                  got2 == reply_of("DEAD00") &&
                  few == reply_of("ERROR. Needs three parameters") &&
                  miss == reply_of("ERROR loading file") &&
                  addr == reply_of("Error. Invalid address: 10000H"),
              esc(got) + " / " + esc(got2) + " / " + esc(miss));
    }
    {
        // B1 (M3 review): a refused load is said, as write-memory says it; a
        // directory is not a file.
        const std::string bin = zrcp_tmp("rzx", ".bin");
        {
            std::FILE* f = std::fopen(bin.c_str(), "wb");
            const std::uint8_t b[] = {0x11, 0x22};
            std::fwrite(b, 1, 2, f);
            std::fclose(f);
        }
        Rig rig;
        Zc  c(rig);
        const auto before = peek(rig, 0x9000, 2);
        std::string refused, wm;
        {
            RzxOn rzx(rig.emu);
            refused = c.cmd("load-binary " + bin + " 36864 0");
            wm      = c.cmd("write-memory 36864 1");
        }
        const auto after = peek(rig, 0x9000, 2);
        const std::string dir = c.cmd("load-binary /tmp 36864 0");
        const auto rom_before = peek(rig, 0x0000, 2);
        const std::string rom = c.cmd("load-binary " + bin + " 0 0");
        const auto rom_after = peek(rig, 0x0000, 2);
        std::remove(bin.c_str());
        check("ZRCP-LOAD-05", "during an RZX session load-binary is refused like write-memory, "
                              "naming the reason and what landed (0 bytes), and memory is "
                              "unchanged; a directory answers \"ERROR loading file\"; a load "
                              "into ROM lands nowhere and is silent, as in ZEsarUX",
              starts_with(refused, "Error. load-binary refused: ") &&
                  refused.find("(0 bytes loaded)") != std::string::npos &&
                  starts_with(wm, "Error. write-memory refused: ") && before == after &&
                  dir == reply_of("ERROR loading file") && rom == reply_of("") &&
                  rom_before == rom_after,
              esc(refused) + " / " + esc(wm) + " / " + esc(dir));
    }
    {
        const std::string out = zrcp_tmp("save", ".bin");
        Rig rig;
        Zc  c(rig);
        c.cmd("write-memory-raw 36864 0102030405");
        const std::string ok   = c.cmd("save-binary \"" + out + "\" 9000H 3");
        std::vector<std::uint8_t> got(16, 0);
        std::size_t n = 0;
        if (std::FILE* f = std::fopen(out.c_str(), "rb")) {
            n = std::fread(got.data(), 1, got.size(), f);
            std::fclose(f);
        }
        const std::string full = c.cmd("save-binary " + out + " 0 0");
        long size = -1;
        if (std::FILE* f = std::fopen(out.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            size = std::ftell(f);
            std::fclose(f);
        }
        std::remove(out.c_str());
        const std::string bad = c.cmd("save-binary /nonexistent/dir/zrcp.bin 0 1");
        check("ZRCP-LOAD-02", "save-binary \"file\" address length writes those bytes of the CPU "
                              "view; length 0 is 64 KB; an unwritable path gets ZEsarUX's own "
                              "\"ERROR loading file\"",
              ok == reply_of("") && n == 3 && got[0] == 1 && got[1] == 2 && got[2] == 3 &&
                  full == reply_of("") && size == 65536 && bad == reply_of("ERROR loading file"),
              esc(ok) + " n=" + std::to_string(n) + " size=" + std::to_string(size));
    }
    {
        // smartload through CAP-CTL-15: the loop owner's driver gets the path
        // with its quotes stripped; a paused machine stays paused.
        Rig rig;
        std::string seen;
        bool        succeed = true;
        jnext::dbg::LoopDriver d;
        d.load = [&](const std::string& path) {
            seen = path;
            if (!succeed) return false;
            Z80Registers r = rig.emu.cpu().get_registers();
            r.PC = 0x6000;
            rig.emu.cpu().set_registers(r);
            return true;
        };
        rig.dbg->set_loop_driver(d);
        Zc c(rig);
        c.cmd("enter-cpu-step");
        const std::string ok  = c.cmd("smartload \"/some dir/my game.nex\"");
        const bool paused     = rig.dbg->state().paused;
        const auto pc         = rig.pc();
        const std::string sl  = c.cmd("sl plain.tap");
        const std::string seen2 = seen;
        succeed = false;
        const std::string bad = c.cmd("smartload broken.nex");
        const std::string no  = c.cmd("smartload");
        check("ZRCP-LOAD-03", "smartload \"file\": the path, quotes stripped and spaces kept, goes "
                              "to the loop owner's load; a paused machine stays paused at the new "
                              "PC; the alias sl; a failed load \"Error. Unknown file format\"; no "
                              "file \"ERROR. No parameter set\"",
              ok == reply_of("", true) && paused && pc == 0x6000 && sl == reply_of("", true) &&
                  seen2 == "plain.tap" && bad == reply_of("Error. Unknown file format", true) &&
                  no == reply_of("ERROR. No parameter set", true),
              esc(ok) + " seen=" + seen + " pc=" + std::to_string(pc));
    }
    {
        Rig rig;
        Zc  c(rig);
        const std::string none = c.cmd("smartload x.nex");
        check("ZRCP-LOAD-04", "with no loop-owner load driver (a bare harness) smartload is "
                              "refused, never a silent success",
              none == reply_of("Error. Unknown file format"), esc(none));
    }
}

static void wp5_snapshot_rows() {
    {
        Rig rig;
        rig.load({0x3E, 0x05, 0x00, 0x18, 0xFE});
        Zc c(rig);
        c.cmd("enter-cpu-step");                         // paused at a frame boundary
        const std::string save = c.cmd("snapshot-save proj/.tmp/state.zsf");
        c.cmd("cpu-step");                               // LD A,5 — now mid-frame
        const std::string mid  = c.cmd("snapshot-save other.zsf");
        const std::string load = c.cmd("snapshot-load proj/.tmp/state.zsf");
        const auto        pc   = rig.pc();
        const std::string unk  = c.cmd("snapshot-load never-saved.zsf");
        const std::string no   = c.cmd("snapshot-save");
        check("ZRCP-SNAP-01", "snapshot-save name at a frame boundary keeps an in-memory "
                              "bookmark under name; mid-frame it is refused with the reason; "
                              "snapshot-load name restores it (PC back at 8000); an unknown name "
                              "and no name are refused",
              save == reply_of("", true) &&
                  starts_with(mid, "Error. The machine is stopped mid-frame") &&
                  load == reply_of("", true) && pc == PROG &&
                  unk == reply_of("Error. No snapshot saved under that name in this session",
                                  true) &&
                  no == reply_of("ERROR. No parameter set", true),
              esc(save) + " / " + esc(mid) + " / " + esc(load));
    }
    {
        Rig rig;
        std::string ninth;
        {
            Zc c(rig);
            c.cmd("enter-cpu-step");
            for (int i = 1; i <= 8; ++i) c.cmd("snapshot-save s" + std::to_string(i));
            const std::string again = c.cmd("snapshot-save s3");
            ninth = c.cmd("snapshot-save s9");
            check("ZRCP-SNAP-02", "8 snapshots per session; saving a held name again replaces "
                                  "it; a ninth name is refused",
                  again == reply_of("", true) &&
                      ninth == reply_of("Error. Too many snapshots in this session (8)", true),
                  esc(ninth));
            c.p->send("quit\n");
            c.wait(4);
        }
        Zc c2(rig);
        const std::string gone = c2.cmd("snapshot-load s1");
        check("ZRCP-SNAP-03", "a session's snapshots end with it: the next client cannot load "
                              "them",
              gone == reply_of("Error. No snapshot saved under that name in this session"),
              esc(gone));
    }
}

int main() {
    std::printf("zrcp_adapter_test — the ZRCP adapter over T's fake transport (GH #280)\n");
    framing_rows();
    table_rows();
    session_rows();
    info_rows();
    format_rows();
    register_rows();
    memory_rows();
    disasm_rows();
    pages_stack_time_rows();
    tbblue_rows();
    control_rows();
    run_rows();
    run_edge_rows();
    run_limit_rows();
    reset_rows();
    nmi_rows();
    coexist_rows();
    legacy_rows();
    session_reset_rows();
    step_over_set_rows();
    tbblue_name_rows();
    edge_rows();
    round2_rows();
    wp4_condition_rows();
    wp4_slot_rows();
    wp4_mem_rows();
    wp5_history_rows();
    wp5_stack_coverage_rows();
    wp5_load_rows();
    wp5_snapshot_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass,
                g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
