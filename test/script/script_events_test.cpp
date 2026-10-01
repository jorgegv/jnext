// jnext::script — the script ENGINE over a real machine: rules registered as
// backend subscriptions, delivered by the emulator, and acting on it (GH #26
// WP3, epic #276 package S).
//
// The contract is doc/design/debug-subsystem/dsl-frontend.md §2.2 (rules and
// firing), §2.6 (actions and the frame-edge rule), §2.7 (mutation), §3 (the
// worked scripts), §6.3 (stop / exit) and §6.5 (errors); the choices WP3 made
// where those are silent are its "WP3 as built" appendix (I).
//
//   SCRIPT-EV-REG-*     what a rule BECOMES: kind, filter, access, once,
//                       condition, enabled — one subscription per rule except
//                       the execute page range (F3, F5); load errors register
//                       nothing; the §3(a) PAGE warning (F4)
//   SCRIPT-EV-*         what a rule DOES on a running machine: payloads,
//                       predicates, once / enable / disable, stop / exit /
//                       assert, the log line, run-time errors, the mutation
//                       verbs, the input and capture actions at the frame edge,
//                       `on stop` (F6), NextREG / Copper / DMA (F1, F2)
//   SCRIPT-EV-WORK-*    the §3 worked scripts, loaded and behaving
//
// Every row drives `ScriptEngine` directly on an `Emulator` + `Debugger`, with
// a test client listening to the log; CLI loading is WP4.
//
// Run: ./build/test/script_events_test

#include "script/evaluator.h"
#include "script/script_engine.h"
#include "script/script_host.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "peripheral/dma.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

using namespace jnext::script;
using jnext::dbg::Access;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::Result;
using jnext::dbg::Subscription;
using jnext::dbg::SubscriptionInfo;
using DbgEvent = jnext::dbg::Event;

// ── Tiny test harness (matches debugger_backend_test) ──────────────────────

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
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " — ", detail.c_str());
    }
}

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "%X", v);
    return b;
}

static std::string dstr(const std::vector<Diagnostic>& ds) {
    if (ds.empty()) return "(none)";
    std::string s;
    for (const Diagnostic& d : ds) s += "[" + d.to_string() + "] ";
    return s;
}

// ── The rig ────────────────────────────────────────────────────────────────

/// A test client's listener: every log line (the engine's and the backend's
/// MUTATE lines), every exit request and every pause push, in one ordered
/// trail with the engine host's exits so their ORDER can be asserted.
struct Sink : jnext::dbg::Listener {
    std::vector<std::string>             lines;
    std::vector<int>                     exits;
    std::vector<jnext::dbg::PausedInfo>  paused;
    size_t                               subs_changed = 0;
    std::vector<std::string>*            order = nullptr;

    void on_paused(const jnext::dbg::PausedInfo& i) override { paused.push_back(i); }
    void on_resumed(ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override { ++subs_changed; }
    void on_exit_requested(int code) override {
        exits.push_back(code);
        if (order) order->push_back("notify" + std::to_string(code));
    }
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { lines.push_back(t); }

    size_t count(const std::string& needle) const {
        size_t n = 0;
        for (const auto& l : lines)
            if (l.find(needle) != std::string::npos) ++n;
        return n;
    }
    std::string first(const std::string& needle) const {
        for (const auto& l : lines)
            if (l.find(needle) != std::string::npos) return l;
        return {};
    }
    std::string tail(size_t n = 4) const {
        std::string s;
        for (size_t i = lines.size() > n ? lines.size() - n : 0; i < lines.size(); ++i)
            s += "{" + lines[i] + "} ";
        return s.empty() ? "(no log)" : s;
    }
};

static constexpr uint16_t PROG = 0x8000;

struct Rig {
    Emulator                     emu;
    std::unique_ptr<Debugger>    dbg;
    Sink                         sink;
    ClientId                     tc = jnext::dbg::CLIENT_NONE;
    std::vector<int>             host_exits;
    std::vector<std::string>     order;
    std::unique_ptr<ScriptEngine> eng;
    LoadResult                   last;

    explicit Rig(const std::vector<uint8_t>& prog,
                 MachineType type = MachineType::ZX48K, bool host = true) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        for (size_t i = 0; i < prog.size(); ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = PROG;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "script_events_test";
        ci.kind = jnext::dbg::ClientKind::Test;
        tc = dbg->attach(ci).value;
        dbg->set_listener(tc, &sink);
        sink.order = &order;
        EngineHost h;
        if (host)
            h.exit = [this](int c) {
                host_exits.push_back(c);
                order.push_back("host" + std::to_string(c));
            };
        eng = std::make_unique<ScriptEngine>(*dbg, h);
        dbg->pump(jnext::dbg::PumpBudget{});  // the first pump only primes
    }
    ~Rig() {
        eng.reset();
        dbg->set_listener(tc, nullptr);
        dbg->detach(tc);
        dbg.reset();
    }

    bool load(const std::string& src, const std::string& file = "t.jds") {
        last = eng->load(src, file);
        return last.ok();
    }
    /// Run `n` frames, pumping after each; stops early once paused.
    void frames(int n) {
        for (int i = 0; i < n && !paused(); ++i) {
            emu.run_frame();
            dbg->pump(jnext::dbg::PumpBudget{});
        }
    }
    bool paused() const { return dbg->state().paused; }
    uint16_t pc() { return emu.cpu().get_registers().PC; }
    uint8_t peek(uint16_t a) {
        uint8_t b = 0;
        dbg->peek(jnext::dbg::MemSpace::cpu(), a, 1, &b);
        return b;
    }
    /// The engine's subscriptions, in registration order.
    std::vector<SubscriptionInfo> subs() const {
        std::vector<SubscriptionInfo> out;
        for (const auto& s : dbg->subscriptions(true))
            if (s.owner == eng->client()) out.push_back(s);
        return out;
    }
    uint64_t hits(size_t rule) const {
        const auto v = eng->rules();
        return rule < v.size() ? v[rule].hits : UINT64_MAX;
    }
};

static std::string show(const SubscriptionInfo& s) {
    std::string out = "kind=" + std::to_string(static_cast<int>(s.kind)) +
                      " lo=" + hex(s.filter.lo) + " hi=" + hex(s.filter.hi) +
                      " page=" + hex(s.filter.page) + " pages=" + std::to_string(s.filter.pages.size()) +
                      " access=" + std::to_string(static_cast<int>(s.access)) +
                      " cond=" + std::to_string(s.has_condition) + " once=" + std::to_string(s.once) +
                      " en=" + std::to_string(s.enabled);
    return out;
}

static std::string show(const std::vector<SubscriptionInfo>& v) {
    std::string s = "n=" + std::to_string(v.size());
    for (const auto& x : v) s += " {" + show(x) + "}";
    return s;
}

static void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static const std::vector<uint8_t> kPark = {0x18, 0xFE};  // JR $

// The writer every payload row runs:
//   8000 3E 5A     LD A,0x5A
//   8002 32 00 90  LD (0x9000),A
//   8005 3E 77     LD A,0x77
//   8007 32 01 90  LD (0x9001),A
//   800A 18 FE     JR $
static const std::vector<uint8_t> kWriter = {0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3E, 0x77,
                                             0x32, 0x01, 0x90, 0x18, 0xFE};

// A loop that writes 0x9000 on every pass:
//   8000 21 00 90  LD HL,0x9000
//   8003 34        INC (HL)
//   8004 18 FD     JR 0x8003
static const std::vector<uint8_t> kLoop = {0x21, 0x00, 0x90, 0x34, 0x18, 0xFD};

// =========================================================================
// REGISTRATION — what a rule becomes
// =========================================================================

static void reg_rows() {
    {
        Rig g(kPark);
        const bool ok = g.load("on execute 0x8000..0x80FF do log \"x\" end\n");
        const auto s = g.subs();
        check("SCRIPT-EV-REG-EXEC", "`on execute LO..HI` is ONE Execute subscription over [LO,HI], no "
                                    "page, no condition, not once, enabled, Continue",
              ok && s.size() == 1 && s[0].kind == EventKind::Execute && s[0].filter.lo == 0x8000 &&
                  s[0].filter.hi == 0x80FF && s[0].filter.page == jnext::dbg::PAGE_ANY &&
                  !s[0].has_condition && s[0].has_handler && !s[0].once && s[0].enabled &&
                  s[0].action == jnext::dbg::Action::Continue,
              dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on execute 0x8000 page 5 once when A == 1 do log \"x\" end\n");
        const auto s = g.subs();
        check("SCRIPT-EV-REG-EXEC-PAGE", "`on execute A page P once when …`: the page qualifier, `once` and "
                                         "a predicate, still one subscription",
              ok && s.size() == 1 && s[0].filter.lo == 0x8000 && s[0].filter.hi == 0x8000 &&
                  s[0].filter.page == 5 && s[0].once && s[0].has_condition,
              dstr(g.last.errors) + " " + show(s));
    }
    {
        // F3: the Execute filter takes ONE page, so a page range is one
        // subscription per page — still one rule.
        Rig g(kPark);
        const bool ok = g.load("on execute page 4..6 do log \"x\" end\n");
        const auto s = g.subs();
        const auto r = g.eng->rules();
        bool shape = s.size() == 3;
        for (size_t k = 0; shape && k < 3; ++k)
            shape = s[k].kind == EventKind::Execute && s[k].filter.page == 4 + k && s[k].filter.lo == 0 &&
                    s[k].filter.hi == 0xFFFF && !s[k].has_condition;
        check("SCRIPT-EV-REG-EXEC-PAGES", "`on execute page 4..6` is one Execute subscription per page "
                                          "(4, 5, 6) over the whole address space, all owned by ONE rule (F3)",
              ok && shape && r.size() == 1 && r[0].subs.size() == 3, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok16  = g.load("on execute page 0..15 do log \"x\" end\n", "a.jds");
        const size_t n16 = g.subs().size();
        const bool ok17  = g.load("on execute page 0..16 do log \"x\" end\n", "b.jds");
        const auto e17   = g.last.errors;
        check("SCRIPT-EV-REG-EXEC-PAGES-MAX", "an execute page range is bounded at 16 pages: 0..15 loads as "
                                              "16 subscriptions, 0..16 is a load error at the range and adds none",
              ok16 && n16 == 16 && !ok17 && e17.size() == 1 && e17[0].pos.line == 1 &&
                  e17[0].message.find("at most 16") != std::string::npos && g.subs().size() == 16,
              "n16=" + std::to_string(n16) + " " + dstr(e17) + " now=" + std::to_string(g.subs().size()));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on read 0x4000..0x5AFF do log \"x\" end\n"
                               "on write page 0x20..0x22 do log \"x\" end\n"
                               "on write 0xC000..0xFFFF page 0x22 do log \"x\" end\n");
        const auto s = g.subs();
        const bool rd = s.size() == 3 && s[0].kind == EventKind::Mem && s[0].access == Access::Read &&
                        s[0].filter.lo == 0x4000 && s[0].filter.hi == 0x5AFF && s[0].filter.pages.empty();
        const bool pg = s.size() == 3 && s[1].kind == EventKind::Mem && s[1].access == Access::Write &&
                        s[1].filter.pages == std::vector<uint16_t>{0x20, 0x21, 0x22} && !s[1].has_condition;
        const bool rp = s.size() == 3 && s[2].access == Access::Write && s[2].filter.lo == 0xC000 &&
                        s[2].filter.hi == 0xFFFF && s[2].filter.page == 0x22 && s[2].filter.pages.empty();
        check("SCRIPT-EV-REG-MEM", "`read`/`write` are Mem subscriptions with their access; `write page "
                                   "P1..P2` is ONE subscription whose page SET is P1..P2 with no predicate "
                                   "(the backend's filter, not `PAGE ==`); `LO..HI page P` is the AND form",
              ok && rd && pg && rp, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on io_write 0xFE do log \"x\" end\n"
                               "on io_read 0x253B do log \"x\" end\n"
                               "on io_write mask 0x00FF value 0x57 do log \"x\" end\n"
                               "on io_write 0x10..0x20 do log \"x\" end\n");
        const auto s = g.subs();
        const bool sh = s.size() == 4 && s[0].kind == EventKind::Port && s[0].access == Access::Write &&
                        s[0].filter.port_mask == 0x00FF && s[0].filter.port_value == 0xFE &&
                        !s[0].has_condition && s[1].access == Access::Read &&
                        s[1].filter.port_mask == 0xFFFF && s[1].filter.port_value == 0x253B &&
                        s[2].filter.port_mask == 0x00FF && s[2].filter.port_value == 0x57 &&
                        s[3].filter.port_mask == 0 && s[3].has_condition;
        check("SCRIPT-EV-REG-IO", "io rules are Port subscriptions: a port <= 0xFF decodes on its low byte "
                                  "(GH #222), a wider one on 16 bits, `mask … value …` as written, and a port "
                                  "RANGE as match-all plus an engine range condition",
              ok && sh, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on nextreg 0x50..0x57 do log \"x\" end\n"
                               "on frame 10 do log \"x\" end\n"
                               "on frame do log \"x\" end\n"
                               "on scanline 95 do log \"x\" end\n"
                               "on cycle 123456 do log \"x\" end\n");
        const auto s = g.subs();
        const bool sh = s.size() == 5 && s[0].kind == EventKind::NextRegWrite &&
                        s[0].filter.regs == std::vector<uint8_t>{0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57} &&
                        s[1].kind == EventKind::Frame && s[1].filter.frame == 10 &&
                        s[2].kind == EventKind::Frame && s[2].filter.frame == jnext::dbg::FRAME_EVERY &&
                        s[3].kind == EventKind::Scanline && s[3].filter.scanline == 95 &&
                        s[4].kind == EventKind::Cycle && s[4].filter.cycle == 123456;
        check("SCRIPT-EV-REG-TIME", "`nextreg R1..R2` names the register set; `frame N` / `frame` (every); "
                                    "`scanline N`; `cycle N`",
              ok && sh, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on interrupt do log \"x\" end\non nmi do log \"x\" end\n"
                               "on reset do log \"x\" end\non hostkey 3 do log \"x\" end\n");
        const auto s = g.subs();
        const bool sh = s.size() == 4 && s[0].kind == EventKind::IntAck && s[1].kind == EventKind::Nmi &&
                        s[2].kind == EventKind::Reset && s[2].filter.reset_kind == jnext::dbg::ResetKind::Any &&
                        s[3].kind == EventKind::Host && std::string(s[3].filter.host_name) == "script3";
        check("SCRIPT-EV-REG-ACCEPT", "`interrupt` / `nmi` / `reset` (either kind, ONE subscription) / "
                                      "`hostkey 3` (the host event `script3`)",
              ok && sh, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on copper move 0x43 do log \"x\" end\n"
                               "on copper wait at 10..20 do log \"x\" end\n"
                               "on copper halt do log \"x\" end\n");
        const auto s = g.subs();
        const bool sh = s.size() == 3 && s[0].kind == EventKind::Copper &&
                        s[0].filter.copper_kind == jnext::dbg::CopperEventKind::Move &&
                        s[0].filter.regs == std::vector<uint8_t>{0x43} &&
                        s[1].filter.copper_kind == jnext::dbg::CopperEventKind::Wait &&
                        s[1].filter.lo == 10 && s[1].filter.hi == 20 &&
                        s[2].filter.copper_kind == jnext::dbg::CopperEventKind::Halt;
        check("SCRIPT-EV-REG-COPPER", "copper rules: one Copper subscription per sub-kind, `move R` the "
                                      "register set, `at A..B` the Copper-PC range",
              ok && sh, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on dma start do log \"x\" end\n"
                               "on dma byte 0x8000..0x9FFF do log \"x\" end\n"
                               "on dma byte do log \"x\" end\n"
                               "on dma end do log \"x\" end\n");
        const auto s = g.subs();
        const bool sh = s.size() == 4 && s[0].kind == EventKind::Dma &&
                        s[0].filter.dma_kind == jnext::dbg::DmaEventKind::Start && !s[0].has_condition &&
                        s[1].filter.dma_kind == jnext::dbg::DmaEventKind::Byte && s[1].filter.lo == 0x8000 &&
                        s[1].filter.hi == 0x9FFF && s[1].has_condition &&
                        s[2].filter.dma_kind == jnext::dbg::DmaEventKind::Byte && s[2].filter.lo == 0 &&
                        s[2].filter.hi == 0xFFFF && !s[2].has_condition &&
                        s[3].filter.dma_kind == jnext::dbg::DmaEventKind::End;
        check("SCRIPT-EV-REG-DMA", "dma rules: one Dma subscription per sub-kind; a `byte` range "
                                   "pre-selects in the backend (either endpoint) and carries the engine's "
                                   "destination condition (F2); an unranged `byte` has none",
              ok && sh, dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on dma byte page 3 do log \"x\" end\n");
        check("SCRIPT-EV-REG-DMA-PAGE", "`dma byte page P` is a load error (the DMA has no page filter, F2) "
                                        "and registers nothing",
              !ok && g.last.errors.size() == 1 &&
                  g.last.errors[0].message.find("no page filter") != std::string::npos && g.subs().empty(),
              dstr(g.last.errors));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on stop do log \"x\" end\ndisabled g: on write 0x9000 do log \"x\" end\n");
        const auto s = g.subs();
        const auto r = g.eng->rules();
        check("SCRIPT-EV-REG-STOP-DISABLED", "an `on stop` rule is no subscription; a `disabled` rule is "
                                             "registered with its own enable flag off",
              ok && r.size() == 2 && r[0].subs.empty() && s.size() == 1 && !s[0].enabled && !r[1].enabled,
              dstr(g.last.errors) + " " + show(s));
    }
    {
        Rig g(kPark);
        const bool a = g.load("on write 0x9000..0x8000 do log \"x\" end\n", "a.jds");
        const auto ea = g.last.errors;
        const bool b = g.load("on scanline 2000 do log \"x\" end\n", "b.jds");
        const auto eb = g.last.errors;
        const bool c = g.load("on write 0x10000 do log \"x\" end\n", "c.jds");
        const auto ec = g.last.errors;
        check("SCRIPT-EV-REG-BOUNDS", "filter bounds are checked at load: a range ending below its start, a "
                                      "scanline outside 0..1023 and an address outside 0..0xFFFF are load "
                                      "errors at the bound, and register nothing",
              !a && ea.size() == 1 && ea[0].message.find("ends below its start") != std::string::npos &&
                  !b && eb.size() == 1 && eb[0].message.find("outside 0..1023") != std::string::npos &&
                  !c && ec.size() == 1 && ec[0].message.find("outside 0..65535") != std::string::npos &&
                  g.subs().empty() && g.eng->rules().empty(),
              dstr(ea) + dstr(eb) + dstr(ec));
    }
    {
        Rig g(kPark);
        const bool a = g.load("on io_write 0xF0..0x110 do log \"x\" end\n", "a.jds");
        const auto ea = g.last.errors;
        const bool b = g.load("on io_read 0xF0..0xFF do log \"x\" end\non io_read 0x100..0x110 do log \"x\" end\n",
                              "b.jds");
        check("SCRIPT-EV-REG-IO-STRADDLE", "a port range straddling 0xFF is a load error (it would need the "
                                           "low-byte and the exact decode at once) and registers nothing; the two "
                                           "halves load",
              !a && ea.size() == 1 && ea[0].pos.line == 1 && ea[0].pos.column == 13 &&
                  ea[0].message.find("GH #222") != std::string::npos && b && g.subs().size() == 2,
              dstr(ea) + " " + show(g.subs()));
    }
    {
        // §6.5: "nothing runs partially" — two good rules and one bad.
        Rig g(kPark);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        const size_t changed0 = g.sink.subs_changed;
        const bool ok = g.load("on write 0x9000 do log \"a\" end\n"
                               "on frame do log \"b\" end\n"
                               "on io_write 0x10000 do log \"c\" end\n");
        g.dbg->pump(jnext::dbg::PumpBudget{});
        check("SCRIPT-EV-REG-ATOMIC", "a script with one bad rule registers NONE of its rules — not even "
                                      "transiently: the backend's subscription model never changed",
              !ok && g.last.errors.size() == 1 && g.last.errors[0].pos.line == 3 && g.subs().empty() &&
                  g.eng->rules().empty() && g.sink.subs_changed == changed0,
              dstr(g.last.errors) + " " + show(g.subs()) + " changed=" +
                  std::to_string(g.sink.subs_changed - changed0));
    }
    {
        // Bounds are evaluated with the script's own variables and symbols.
        Rig g(kPark);
        const std::string map = "/tmp/jnext_sev_bounds.map";
        write_file(map, "BUF = $9100 ; const\n");
        g.dbg->load_map(map, jnext::dbg::MapFormat::Simple);
        const bool ok = g.load("var base = 0x9000\non write base..base + 3 do log \"x\" end\n"
                               "on write @BUF..(@BUF + 0xFF) do log \"x\" end\n");
        const auto s = g.subs();
        check("SCRIPT-EV-REG-VARBOUND", "filter bounds may use `var` initializers and `@symbols`, evaluated "
                                        "at load",
              ok && s.size() == 2 && s[0].filter.lo == 0x9000 && s[0].filter.hi == 0x9003 &&
                  s[1].filter.lo == 0x9100 && s[1].filter.hi == 0x91FF,
              dstr(g.last.errors) + " " + show(s));
    }
    {
        // F4: the §3(a) warning, with the bounds known.
        Rig g(kPark);
        const bool wide = g.load("on write 0x8000..0xBFFF when PAGE == 5 do log \"x\" end\n", "w.jds");
        const auto ww   = g.last.warnings;
        const bool one  = g.load("on write 0x8000..0x9FFF when PAGE == 5 do log \"x\" end\n", "o.jds");
        const auto wo   = g.last.warnings;
        const bool pq   = g.load("on write 0x8000..0xBFFF page 5 when PAGE == 5 do log \"x\" end\n", "p.jds");
        const auto wp   = g.last.warnings;
        const bool nq   = g.load("on write 0x8000..0xBFFF when VALUE == 5 do log \"x\" end\n", "n.jds");
        const auto wn   = g.last.warnings;
        check("SCRIPT-EV-REG-PAGEWARN", "a `PAGE ==` predicate over a range wider than one 8K slot loads "
                                        "WITH a warning at the comparison and a logged line; one slot, a page "
                                        "qualifier, or no PAGE test: no warning (F4)",
              wide && ww.size() == 1 && ww[0].pos.line == 1 && ww[0].pos.column == 35 &&
                  ww[0].message.find("page") != std::string::npos && one && wo.empty() && pq &&
                  wp.empty() && nq && wn.empty() && g.sink.count("SCRIPT WARNING w.jds:1:35:") == 1 &&
                  g.subs().size() == 4,
              dstr(ww) + " | " + dstr(wo) + dstr(wp) + dstr(wn) + " " + g.sink.tail());
    }
}

// =========================================================================
// DELIVERY — payloads, predicates, once / enable / disable
// =========================================================================

static void delivery_rows() {
    {
        Rig g(kWriter);
        g.emu.mmu().write(0x9000, 0x11);
        g.emu.mmu().write(0x9001, 0x22);
        const unsigned page = g.emu.mmu().get_effective_page(4);
        const bool ok = g.load("on write 0x9000..0x9001 do log \"W ${ADDR:x4} ${VALUE:x2} prev ${PREV:x2} "
                               "pc ${PC:x4} src ${SOURCE} page ${PAGE:x2}\" end\n");
        g.frames(1);
        char want0[96], want1[96];
        std::snprintf(want0, sizeof want0, "W 9000 5A prev 11 pc 8002 src 0 page %02X", page);
        std::snprintf(want1, sizeof want1, "W 9001 77 prev 22 pc 8007 src 0 page %02X", page);
        check("SCRIPT-EV-WRITE-PAYLOAD", "a write rule sees ADDR, VALUE, PREV, the WRITER's PC, SOURCE == CPU "
                                         "and the physical PAGE behind the address, once per write",
              ok && g.sink.count("W 90") == 2 && g.sink.count(want0) == 1 && g.sink.count(want1) == 1,
              g.sink.tail());
    }
    {
        // §8 3(d) mutation: the predicate is the backend's — a non-matching
        // write never enters the rule body.
        Rig g(kLoop);
        size_t writes = 0, sevens = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write; s.filter.lo = s.filter.hi = 0x9000;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent& e, Debugger&) {
            ++writes;
            if (e.value == 7) ++sevens;
            return jnext::dbg::Action::Continue;
        };
        g.dbg->subscribe(g.tc, s);
        const bool ok = g.load("on write 0x9000 when VALUE == 0x200 do log \"never\" end\n"
                               "on write 0x9000 when VALUE == 7 do log \"seven\" end\n");
        g.frames(1);
        check("SCRIPT-EV-PREDICATE", "a `when` is the subscription's condition: thousands of non-matching "
                                     "writes enter the rule body ZERO times; the matching ones exactly once each",
              ok && writes > 1000 && sevens > 0 && g.hits(0) == 0 && g.hits(1) == sevens &&
                  g.sink.count("seven") == sevens,
              "writes=" + std::to_string(writes) + " sevens=" + std::to_string(sevens) + " hits=" +
                  std::to_string(g.hits(0)) + "/" + std::to_string(g.hits(1)));
    }
    {
        Rig g(kLoop);
        const bool ok = g.load("w: on write 0x9000 once do log \"once\" end\n"
                               "on hostkey 1 do enable w end\n");
        g.frames(2);
        const uint64_t h1 = g.hits(0);
        const bool spent  = !g.subs().empty() && !g.subs()[0].enabled;
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(1);
        const uint64_t h2 = g.hits(0);
        g.frames(1);
        const uint64_t h3 = g.hits(0);
        check("SCRIPT-EV-ONCE", "`once` fires on the first accepted write only (thousands follow), and "
                                "`enable` re-arms it: exactly one more firing (§2.2)",
              ok && h1 == 1 && spent && h2 == 2 && h3 == 2 && g.sink.count("once") == 2,
              "h=" + std::to_string(h1) + "," + std::to_string(h2) + "," + std::to_string(h3) +
                  " spent=" + std::to_string(spent));
    }
    {
        // §2.2: `once` is the RULE's, not each subscription's. A page range is
        // one subscription per page (F3); the program runs through both pages
        // every pass:  8000 JP 0xA000 ; A000 JP 0x8000.
        Rig g({0xC3, 0x00, 0xA0});
        g.emu.mmu().write(0xA000, 0xC3);
        g.emu.mmu().write(0xA001, 0x00);
        g.emu.mmu().write(0xA002, 0x80);
        const unsigned p4 = g.emu.mmu().get_effective_page(4), p5 = g.emu.mmu().get_effective_page(5);
        const bool ok = p5 == p4 + 1 &&
                        g.load("w: on execute page " + std::to_string(p4) + ".." + std::to_string(p5) +
                               " once do log \"ONCE ${PC:x4}\" end\n"
                               "on hostkey 1 do enable w end\n");
        g.frames(2);
        const uint64_t h1 = g.hits(0);
        size_t live = 0;
        for (const auto& s : g.subs())
            if (s.kind == EventKind::Execute && s.enabled) ++live;
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(2);
        const uint64_t h2 = g.hits(0);
        check("SCRIPT-EV-ONCE-PAGES", "`on execute page P..P+1 once` fires ONCE for the whole rule though it "
                                      "is two subscriptions and the program runs through both pages every pass "
                                      "(both are spent), and `enable` re-arms the rule for exactly one more",
              ok && h1 == 1 && live == 0 && h2 == 2 && g.sink.count("] ONCE ") == 2,
              "h1=" + std::to_string(h1) + " live=" + std::to_string(live) + " h2=" + std::to_string(h2) + " " +
                  g.sink.tail(3));
    }
    {
        Rig g(kLoop);
        const bool ok = g.load("disabled g: on write 0x9000 do log \"g\" end\n"
                               "w: on write 0x9000 do log \"w\" end\n"
                               "on hostkey 1 do enable g end\n"
                               "on hostkey 2 do disable w end\n");
        g.frames(1);
        const uint64_t g1 = g.hits(0), w1 = g.hits(1);
        g.dbg->raise_host_event(g.tc, "script2");
        g.frames(1);
        const uint64_t w2 = g.hits(1);
        g.frames(1);
        const uint64_t w3 = g.hits(1);
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(1);
        const uint64_t g4 = g.hits(0);
        const auto r = g.eng->rules();
        check("SCRIPT-EV-ENABLE-DISABLE", "a `disabled` rule never fires until `enable`d from a hostkey rule; "
                                          "`disable` stops a live rule within the frame it is raised in and it "
                                          "stays stopped",
              ok && g1 == 0 && w1 > 100 && w2 >= w1 && w3 == w2 && g4 > 100 && r[0].enabled && !r[1].enabled,
              "g1=" + std::to_string(g1) + " w1=" + std::to_string(w1) + " w2=" + std::to_string(w2) +
                  " w3=" + std::to_string(w3) + " g4=" + std::to_string(g4));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on hostkey 4 do log \"key ${KEY}\" end\n");
        g.dbg->raise_host_event(g.tc, "script3");
        g.frames(1);
        const size_t other = g.sink.count("key ");
        g.dbg->raise_host_event(g.tc, "script4");
        g.frames(1);
        check("SCRIPT-EV-HOSTKEY", "`on hostkey 4` fires on the host event `script4` only, with KEY == 4",
              ok && other == 0 && g.sink.count("key 4") == 1, g.sink.tail());
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do log \"first\" end\n"
                               "on write 0x9000 do log \"second\" end\n", "a.jds") &&
                        g.load("on write 0x9000 do log \"third\" end\n", "b.jds");
        g.frames(1);
        std::string seq;
        for (const auto& l : g.sink.lines)
            for (const char* w : {"first", "second", "third"})
                if (l.find(w) != std::string::npos) seq += std::string(w) + " ";
        check("SCRIPT-EV-ORDER", "rules on one event run in file order, then across files in load order",
              ok && seq == "first second third ", seq);
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on frame do log \"F${FRAME}\" end\n");
        g.frames(3);
        check("SCRIPT-EV-FRAME-TAG", "`FRAME` is the pre-increment tag with no rewind buffer: 0, 1, 2 over "
                                     "the first three frames after load",
              ok && !g.dbg->rewind_enabled() && g.sink.count("F0") == 1 && g.sink.count("F1") == 1 &&
                  g.sink.count("F2") == 1 && g.sink.count("F3") == 0,
              g.sink.tail());
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do log indent 3 \"hello ${VALUE:x2}\" end\n"
                               "on write 0x9001 do log indent (0 - 4) \"neg\" end\n");
        uint32_t f = 0;
        uint64_t c = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write; s.filter.lo = s.filter.hi = 0x9000;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent& e, Debugger& d) {
            f = d.time().frame;
            c = e.cycle;
            return jnext::dbg::Action::Continue;
        };
        g.dbg->subscribe(g.tc, s);
        g.frames(1);
        const std::string want = "[jds F:" + std::to_string(f) + " C:" + std::to_string(c) + "]    hello 5A";
        const std::string line = g.sink.first("hello");
        const std::string neg  = g.sink.first("neg");
        check("SCRIPT-EV-LOG", "`log indent n` is `[jds F:<FRAME> C:<CYCLE>]`, one space, n spaces, the "
                               "interpolated text — FRAME the live tag, CYCLE the event's own (captured at "
                               "the write, before the instruction ended); a negative indent is 0",
              ok && c != 0 && line.compare(0, want.size(), want) == 0 && neg.find("] neg") != std::string::npos,
              "want{" + want + "} got{" + line + "} neg{" + neg + "}");
    }
}

// =========================================================================
// STOP / EXIT / ASSERT / ERRORS
// =========================================================================

static void stop_rows() {
    {
        Rig g(kWriter);
        uint64_t wc = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write; s.filter.lo = s.filter.hi = 0x9001;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent& e, Debugger&) { wc = e.cycle; return jnext::dbg::Action::Continue; };
        g.dbg->subscribe(g.tc, s);
        const bool ok = g.load("on write 0x9001 do stop \"caught ${VALUE:x2}\" end\n"
                               "on stop do log \"ON-STOP ${REASON} pc ${PC:x4}\" end\n");
        g.frames(2);
        const auto st = g.dbg->state();
        char want[64];
        std::snprintf(want, sizeof want, "ON-STOP caught 77 pc %04X", g.sink.paused.empty() ? 0u : g.sink.paused[0].pc);
        check("SCRIPT-EV-STOP", "`stop` on a write pauses at the end of the writing instruction (PC 0x800A), "
                                "logs `SCRIPT STOP: <reason> at PC=<writer> FRAME= CYCLE=<the write's>`, requests no exit under the Pause "
                                "policy, and an `on stop` rule sees the rule's REASON and the paused PC (F6)",
              ok && st.paused && g.pc() == 0x800A && wc != 0 &&
                  g.sink.count("SCRIPT STOP: caught 77 at PC=8007 FRAME=0 CYCLE=" + std::to_string(wc) + " ") == 1 &&
                  g.sink.exits.empty() && g.host_exits.empty() && g.sink.paused.size() == 1 &&
                  g.sink.count(want) == 1,
              "pc=" + hex(g.pc()) + " " + g.sink.tail());
    }
    {
        Rig g(kWriter);
        g.dbg->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        const bool ok = g.load("on write 0x9001 do stop end\n");
        g.frames(2);
        check("SCRIPT-EV-STOP-EXIT3", "under the headless policy a `stop` with no `exit` requests exit 3, "
                                      "and the engine itself asks for no exit code",
              ok && g.paused() && g.sink.exits == std::vector<int>{3} && g.host_exits.empty(), g.sink.tail());
    }
    {
        Rig g(kWriter);
        g.dbg->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        const bool ok = g.load("on write 0x9000 do exit 7 end\n");
        g.frames(2);
        check("SCRIPT-EV-EXIT", "`exit 7` hands 7 to the loop owner BEFORE the backend's stop asks for 3 — "
                                "the owner keeps the first — and pauses after the instruction",
              ok && g.paused() && g.pc() == 0x8005 && g.host_exits == std::vector<int>{7} &&
                  g.order.size() == 2 && g.order[0] == "host7" && g.order[1] == "notify3" &&
                  g.sink.count("SCRIPT EXIT 7") == 1,
              "pc=" + hex(g.pc()) + " order=" + (g.order.empty() ? "" : g.order[0]) + " " + g.sink.tail());
    }
    {
        Rig g(kWriter, MachineType::ZX48K, /*host=*/false);
        const bool ok = g.load("on write 0x9000 do exit 0 end\n");
        g.frames(2);
        check("SCRIPT-EV-EXIT-GUI", "with no exit hook (the GUI) `exit` logs and pauses; nothing exits",
              ok && g.paused() && g.sink.exits.empty() && g.sink.count("SCRIPT EXIT 0") == 1, g.sink.tail());
    }
    {
        Rig g(kWriter);
        const std::string path = "/tmp/jnext_sev_exit.png";
        std::remove(path.c_str());
        const bool ok = g.load("on frame 0 do screenshot \"" + path + "\" exit 0 end\n", "a.jds");
        g.frames(2);
        Rig h(kWriter);
        const bool ok2 = h.load("on frame 0 do save_snapshot \"/tmp/jnext_sev_never.sna\" exit 0 end\n", "b.jds");
        h.frames(2);
        Rig k(kWriter);
        const bool ok3 = k.load("on frame 0 do exit 0 end\n", "c.jds");
        k.frames(2);
        check("SCRIPT-EV-EXIT-CAPTURE", "`exit 0` with a screenshot or a save_snapshot still pending (both wait "
                                        "for a later frame) is exit 1, each logged (§2.6); with nothing pending "
                                        "it stays 0",
              ok && g.host_exits == std::vector<int>{1} &&
                  g.sink.count("SCRIPT: a screenshot was not written") == 1 && ok2 &&
                  h.host_exits == std::vector<int>{1} &&
                  h.sink.count("SCRIPT: 1 save_snapshot(s) never written") == 1 && ok3 &&
                  k.host_exits == std::vector<int>{0},
              g.sink.tail(2) + " | " + h.sink.tail(2));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do assert VALUE == 0x5A \"never\" end\n"
                               "on write 0x9001 do assert VALUE == 0 \"bad value ${VALUE:x2}\" end\n");
        g.frames(2);
        check("SCRIPT-EV-ASSERT", "a passing `assert` does nothing; a failing one logs `ASSERT FAILED: msg` "
                                  "and stops like `stop msg`",
              ok && g.paused() && g.pc() == 0x800A && g.sink.count("ASSERT FAILED: never") == 0 &&
                  g.sink.count("ASSERT FAILED: bad value 77") == 1 &&
                  g.sink.count("SCRIPT STOP: bad value 77 at PC=8007") == 1,
              g.sink.tail());
    }
    {
        // §6.5: a run-time error disables the rule, is logged with file:line:col,
        // and the exit 1 comes at the NEXT frame edge, not in the delivery.
        Rig g(kWriter);
        const bool ok = g.load("var d = 0\n"
                               "on write 0x9000..0x9001 do log \"${100 / (VALUE - 0x5A)}\" end\n", "rt.jds");
        size_t exits_at_second_write = 99;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write; s.filter.lo = s.filter.hi = 0x9001;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger&) {
            exits_at_second_write = g.host_exits.size();
            return jnext::dbg::Action::Continue;
        };
        g.dbg->subscribe(g.tc, s);
        g.frames(2);
        const auto r = g.eng->rules();
        const auto sub = g.subs();
        check("SCRIPT-EV-RUNTIME", "a run-time error disables its rule's subscription (the next write does "
                                   "not enter it), logs `SCRIPT ERROR file:L:C: … — rule … disabled`, and "
                                   "exits 1 ONCE, at the frame edge, not at the failing delivery",
              ok && r.size() == 1 && r[0].dead && r[0].hits == 1 && g.eng->runtime_errors() == 1 &&
                  !sub.empty() && !sub[0].enabled &&
                  g.sink.count("SCRIPT ERROR rt.jds:2:39: division by zero — rule at 2:1 disabled") == 1 &&
                  exits_at_second_write == 0 && g.host_exits == std::vector<int>{1} && !g.paused(),
              "at2=" + std::to_string(exits_at_second_write) + " " + g.sink.tail());
    }
    {
        // A rule a run-time error disabled stays disabled: `enable` does not
        // revive it.
        Rig g(kLoop);
        const bool ok = g.load("bad: on write 0x9000 do log \"${1 / 0}\" end\n"
                               "on hostkey 1 do enable bad end\n");
        g.frames(1);
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(2);
        const auto r = g.eng->rules();
        const auto sub = g.subs();
        check("SCRIPT-EV-RUNTIME-STAYS", "`enable` on a rule a run-time error disabled leaves it dead: one entry, "
                                         "its subscription still off",
              ok && r.size() == 2 && r[0].dead && r[0].hits == 1 && !sub.empty() && !sub[0].enabled &&
                  g.eng->runtime_errors() == 1,
              "hits=" + std::to_string(r.empty() ? 0 : r[0].hits));
    }
    {
        // F6: an `on stop` rule for a stop the script did not cause.
        Rig g(kPark);
        const bool ok = g.load("on stop when REASON == \"user\" do log \"U pc ${PC:x4} r ${REASON}\" end\n"
                               "on stop when REASON != \"user\" do log \"OTHER\" end\n"
                               "on stop once do log \"ONCE\" end\n"
                               "disabled q: on stop do log \"DIS\" end\n");
        g.frames(1);
        g.dbg->pause(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        const auto st = g.dbg->state();
        g.dbg->run(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        g.frames(1);
        g.dbg->pause(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        char want[48];
        std::snprintf(want, sizeof want, "U pc %04X r user", st.pc);
        check("SCRIPT-EV-ONSTOP", "`on stop` runs on ANY pause (here a client's `pause`, twice): REASON names "
                                  "it, PC is the paused PC, its `when` selects, `once` fires once and a "
                                  "`disabled` one never (F6)",
              ok && st.paused && g.sink.count(want) == 2 && g.sink.count("OTHER") == 0 && g.hits(0) == 2 &&
                  g.hits(1) == 0 && g.sink.count("ONCE") == 1 && g.sink.count("DIS") == 0,
              std::string(want) + " " + g.sink.tail());
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on frame 0 do dump_regs dump_mmu dump_mem 0x9000 20 end\n"
                               "on frame 1 do dump_mem 0x9000 5000 end\n");
        g.emu.mmu().write(0x9013, 0xAB);
        g.frames(2);
        const std::string m0 = g.sink.first(" 9000:");
        const std::string m1 = g.sink.first(" 9010:");
        check("SCRIPT-EV-DUMP", "`dump_regs` / `dump_mmu` are one line each; `dump_mem a 20` is 16 + 4 "
                                "bytes; a length over 4096 is a run-time error",
              ok && g.sink.count(" regs AF=") == 1 && g.sink.count("PC=8000") == 1 && g.sink.count(" mmu 0:") == 1 &&
                  m0.find(" 9000: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 [") != std::string::npos &&
                  m1.find(" 9010: 00 00 00 AB [") != std::string::npos && g.sink.count("] 90") == 2 &&
                  g.eng->runtime_errors() == 1 && g.sink.count("0..4096") == 1,
              g.sink.tail(6));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8000 once do snap s end\n"
                               "on execute 0x800A once do dump_diff s log \"depth ${depth(s)} A0 ${s.A:x2}\" end\n");
        g.frames(1);
        check("SCRIPT-EV-SNAP", "`snap` at one event and `dump_diff` at another compare against the machine "
                                "at the second (the A register changed)",
              ok && g.sink.count("dump_diff s:") >= 1 && g.sink.count("depth 1 A0 ") == 1, g.sink.tail(5));
    }
}

// =========================================================================
// MUTATION (§2.7)
// =========================================================================

static void mutation_rows() {
    {
        // A set PC at an execute delivery acts BEFORE the instruction: the
        // store at 0x8002 never runs.
        Rig g(kWriter);
        g.emu.mmu().write(0x9000, 0x11);
        const bool ok = g.load("on execute 0x8002 once do set PC = 0x8005 end\n");
        g.frames(1);
        check("SCRIPT-EV-MUT-PRE", "`set PC` in an execute rule redirects BEFORE the instruction at PC: the "
                                   "skipped store never writes, the rest of the program runs",
              ok && g.peek(0x9000) == 0x11 && g.peek(0x9001) == 0x77 && g.pc() == 0x800A,
              "9000=" + hex(g.peek(0x9000)) + " 9001=" + hex(g.peek(0x9001)));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do set mem[0x9000] = 0xEE end\n"
                               "on write 0x9001 do log \"peek ${mem[0xA000]}\" end\n"
                               "on read 0xA000 do log \"READ\" end\n");
        g.frames(1);
        check("SCRIPT-EV-MUT-NOEVENT", "a rule that pokes the address it watches fires ONCE (a script write "
                                       "raises no event), the poke lands; a script read of a watched address "
                                       "fires no read rule",
              ok && g.hits(0) == 1 && g.peek(0x9000) == 0xEE && g.hits(1) == 1 && g.hits(2) == 0 &&
                  g.sink.count("READ") == 0,
              "hits=" + std::to_string(g.hits(0)) + "/" + std::to_string(g.hits(2)) + " 9000=" + hex(g.peek(0x9000)));
    }
    {
        Rig g(kPark);
        g.emu.nextreg().write(0x7F, 0x00);
        const bool ok = g.load("on execute 0x8000 once do\n"
                               "  set A = 0x12  set ZF = 1  set CF = 0  set IM = 2  set HL = 0x1234\n"
                               "  set mem16[0xA000] = 0xBEEF  set nextreg[0x7F] = 0x42\n"
                               "  set phys[0x10, 0x0005] = 0x99  set AUDIO_MUTE = 5  out 0xFE 3\n"
                               "end\n");
        g.emu.cpu().set_registers([&] { auto r = g.emu.cpu().get_registers(); r.AF = 0x0001; return r; }());
        g.frames(1);
        const Z80Registers r = g.emu.cpu().get_registers();
        uint8_t ph = 0;
        g.dbg->peek(jnext::dbg::MemSpace::page(0x10), 5, 1, &ph);
        check("SCRIPT-EV-MUT-VERBS", "every mutation verb lands through the debugger route: a register, a "
                                     "flag (set and clear, F's other bits kept), IM, a pair, a CPU-view word, a "
                                     "NextREG, a physical byte, the mute mask, a port (border 3) — each with a "
                                     "backend MUTATE line",
              ok && (r.AF >> 8) == 0x12 && (r.AF & 0xFF) == 0x40 && r.IM == 2 && r.HL == 0x1234 &&
                  g.peek(0xA000) == 0xEF && g.peek(0xA001) == 0xBE && g.emu.nextreg().read(0x7F) == 0x42 &&
                  ph == 0x99 && g.dbg->audio_mute_mask() == 5 && g.emu.ula().get_border() == 3 &&
                  g.sink.count("MUTATE") >= 9,
              "AF=" + hex(r.AF) + " IM=" + std::to_string(r.IM) + " ph=" + hex(ph) + " mute=" +
                  hex(g.dbg->audio_mute_mask()) + " MUTATE=" + std::to_string(g.sink.count("MUTATE")));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8000 once do set mem[0x0010] = 1 end\n"
                               "on write 0x9000 do set mem16[0x3FFF] = 0x1234 end\n"
                               "on write 0x9001 do set phys[0xE0, 0] = 1 end\n"
                               "on execute 0x800A once do set IM = 3 end\n", "m.jds");
        g.frames(1);
        const auto r = g.eng->rules();
        check("SCRIPT-EV-MUT-REFUSED", "a mutation that does not wholly land is a run-time error, never "
                                       "silent: a ROM byte (0 of 1 landed), a word straddling ROM/RAM (1 of 2 "
                                       "landed — and that one byte stays), a page the machine lacks, IM 3",
              ok && r.size() == 4 && r[0].dead && r[1].dead && r[2].dead && r[3].dead &&
                  g.eng->runtime_errors() == 4 && g.sink.count("m.jds:1:27: `set` at 0010: 0 of 1 byte(s) landed") == 1 &&
                  g.sink.count("`set` at 3FFF: 1 of 2 byte(s) landed") == 1 && g.peek(0x4000) == 0x12 &&
                  g.sink.count("`set phys[]` was refused (invalid_page)") == 1 &&
                  g.sink.count("`IM` is 0, 1 or 2") == 1,
              g.sink.tail(6));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000..0x9001 do set mem[0xA000] = VALUE end\n");
        g.dbg->resize_rewind_buffer(4);
        g.dbg->set_rewind_enabled(true);
        g.frames(1);
        check("SCRIPT-EV-REWIND-WARN", "a script that mutates while the rewind buffer is on logs ONE warning "
                                       "(§2.7), and the mutations still land",
              ok && g.dbg->rewind_enabled() && g.sink.count("rewind buffer is on") == 1 &&
                  g.peek(0xA000) == 0x77,
              g.sink.tail());
    }
}

// =========================================================================
// INPUT AND CAPTURE AT THE FRAME EDGE (§2.6)
// =========================================================================

// A keyboard poll: LD BC,0xFBFE ; IN A,(C) ; JR back to the IN.
static const std::vector<uint8_t> kKeyPoll = {0x01, 0xFE, 0xFB, 0xED, 0x78, 0x18, 0xFC};

struct PortLog {
    std::vector<std::pair<uint32_t, uint8_t>> reads;  // (frame, value)
    /// Frames in which at least one read saw `bit` clear / set.
    bool any_down(uint32_t f, int bit) const {
        for (auto& r : reads) if (r.first == f && !(r.second & (1 << bit))) return true;
        return false;
    }
    bool all_down(uint32_t f, int bit) const {
        bool seen = false;
        for (auto& r : reads) {
            if (r.first != f) continue;
            seen = true;
            if (r.second & (1 << bit)) return false;
        }
        return seen;
    }
    std::string frames_down(int bit) const {
        std::string s;
        uint32_t last = UINT32_MAX;
        for (auto& r : reads)
            if (!(r.second & (1 << bit)) && r.first != last) { s += std::to_string(r.first) + " "; last = r.first; }
        return s;
    }
};

static void watch_port(Rig& g, PortLog& p) {
    Subscription s;
    s.kind = EventKind::Port; s.access = Access::Read;
    s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
    s.action = jnext::dbg::Action::Continue;
    s.handler = [&p](const DbgEvent& e, Debugger&) {
        p.reads.emplace_back(e.frame, e.value);
        return jnext::dbg::Action::Continue;
    };
    g.dbg->subscribe(g.tc, s);
}

static void input_rows() {
    {
        // SCRIPT-EV-INJ-ORDER: a level press issued mid-frame 2 is seen by no
        // read of frame 2 and by every read of frame 3.
        Rig g(kKeyPoll);
        PortLog p;
        watch_port(g, p);
        const bool ok = g.load("on scanline 100 when FRAME == 2 do press \"q\" end\n"
                               "on frame 4 do release \"q\" end\n");
        g.frames(7);
        check("SCRIPT-EV-INJ-ORDER", "a level `press` issued in frame 2 is visible to every port read of frame "
                                     "3 and to none of frame 2; `release` in frame 4 (at its edge) clears it "
                                     "from frame 5",
              ok && !p.any_down(2, 0) && p.all_down(3, 0) && p.all_down(4, 0) && !p.any_down(5, 0) &&
                  !p.any_down(6, 0),
              "down in frames: " + p.frames_down(0));
    }
    {
        Rig g(kKeyPoll);
        PortLog p;
        watch_port(g, p);
        const bool ok = g.load("on frame 2 do press \"w\" for 3 end\n");
        g.frames(9);
        // The backend's auto-type pulse (IN-01, `tick_auto_type`, the one
        // `--delayed-keypress-frames` uses): pressed by the tick at E_2,
        // released by the tick at E_4 — a hold of 3 ticks is visible to the
        // guest for frames 3 and 4 (backend row IN-01-07 pins the same span).
        check("SCRIPT-EV-PULSE", "`press \"w\" for 3` issued at frame 2's edge is pressed AT that edge and "
                                 "released 3 ticks later: down for every read of frames 3 and 4, none of 2 or 5",
              ok && !p.any_down(2, 1) && p.all_down(3, 1) && p.all_down(4, 1) && !p.any_down(5, 1) &&
                  !p.any_down(6, 1),
              "down in frames: " + p.frames_down(1));
    }
    {
        Rig g(kKeyPoll);
        const bool ok = g.load("on frame 0 do press \"nosuchkey\" end\n"
                               "on frame 0 do press \"1,3\" press \"caps+e\" end\n");
        g.frames(2);
        const auto in = g.dbg->input_state();
        check("SCRIPT-EV-KEYS", "an unknown key name is a run-time error naming it; `row,col` presses that "
                                "matrix bit; a compound name presses both of its keys",
              ok && g.eng->runtime_errors() == 1 && g.sink.count("unknown key `nosuchkey`") == 1 &&
                  !(in.matrix[1] & (1 << 3)) && !(in.matrix[0] & 1) && !(in.matrix[2] & (1 << 2)) &&
                  (in.matrix[1] & 0x17) == 0x17,
              "row0=" + hex(in.matrix[0]) + " row1=" + hex(in.matrix[1]) + " row2=" + hex(in.matrix[2]) +
                  " " + g.sink.tail());
    }
    {
        // A joystick issued mid-frame waits for the edge.
        Rig g(kPark);
        uint16_t mid = 0xFFFF;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 150;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger& d) {
            if (mid == 0xFFFF) mid = d.input_state().joy_left12;
            return jnext::dbg::Action::Continue;
        };
        const bool ok = g.load("on scanline 100 once do joystick 1 0x21 end\n"
                               "on frame 1 do joystick 2 0x02 end\n");
        g.dbg->subscribe(g.tc, s);
        g.frames(1);
        const uint16_t after = g.dbg->input_state().joy_left12;
        g.frames(1);
        const uint16_t right = g.dbg->input_state().joy_right12;
        // A frame rule with no deferred action before it: the engine's own
        // edge subscription does not exist yet, so only "at once" lands it in
        // this frame (one made during the delivery is not visited by it).
        Rig h(kPark);
        const bool ok2 = h.load("on frame 1 do joystick 2 0x04 end\n");
        h.frames(2);
        const uint16_t first = h.dbg->input_state().joy_right12;
        check("SCRIPT-EV-JOYSTICK", "`joystick 1` from a scanline rule is applied at the frame edge (still 0 "
                                    "later in the same frame, 0x21 after it); from a frame rule at once, even "
                                    "as the script's first deferred-class action",
              ok && mid == 0 && after == 0x21 && right == 0x02 && ok2 && first == 0x04,
              "mid=" + hex(mid) + " after=" + hex(after) + " right=" + hex(right) + " first=" + hex(first));
    }
    {
        Rig g(kPark);
        const std::string same = "/tmp/jnext_sev_same.scr", diff = "/tmp/jnext_sev_diff.scr";
        g.frames(1);
        std::vector<uint8_t> scr = g.dbg->ula_screen_dump();
        write_file(same, std::string(scr.begin(), scr.end()));
        scr[100] ^= 0xFF;
        write_file(diff, std::string(scr.begin(), scr.end()));
        const std::string shrt = "/tmp/jnext_sev_short.scr";
        write_file(shrt, std::string(scr.begin(), scr.begin() + 100));
        scr[100] ^= 0xFF;
        scr[0] ^= 0xFF;
        const std::string zero = "/tmp/jnext_sev_zero.scr";
        write_file(zero, std::string(scr.begin(), scr.end()));
        size_t fails_mid = 99;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 200;
        s.action = jnext::dbg::Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger&) {
            if (fails_mid == 99) fails_mid = g.sink.count("ASSERT FAILED");
            return jnext::dbg::Action::Continue;
        };
        const bool ok = g.load("on scanline 50 once do compare_scr \"" + same + "\" \"same\" "
                               "compare_scr \"" + diff + "\" \"differs\" "
                               "compare_scr \"" + shrt + "\" \"short\" "
                               "compare_scr \"" + zero + "\" \"zero\" end\n");
        g.dbg->subscribe(g.tc, s);
        g.frames(3);
        check("SCRIPT-EV-COMPARE-SCR", "`compare_scr` runs at the frame edge (nothing failed later in the "
                                       "frame it was issued in): an equal file passes, a different one logs the "
                                       "first differing offset (or the size) and `ASSERT FAILED: msg` and stops",
              ok && fails_mid == 0 && g.paused() && g.sink.count("ASSERT FAILED: same") == 0 &&
                  g.sink.count("ASSERT FAILED: differs") == 1 &&
                  g.sink.count("first difference at offset 100") == 1 &&
                  g.sink.count("size 100 != screen 6912") == 1 && g.sink.count("ASSERT FAILED: short") == 1 &&
                  g.sink.count("first difference at offset 0 ") == 1 && g.sink.count("ASSERT FAILED: zero") == 1,
              "mid=" + std::to_string(fails_mid) + " " + g.sink.tail());
    }
    {
        Rig g(kPark);
        const std::string scr = "/tmp/jnext_sev_shot.scr", sna = "/tmp/jnext_sev_snap.sna";
        std::remove(scr.c_str());
        std::remove(sna.c_str());
        const bool ok = g.load("on frame 0 do screenshot \"" + scr + "\" save_snapshot \"" + sna + "\" end\n"
                               "on frame 1 do save_snapshot \"/nonexistent/x.sna\" end\n"
                               "on frame 2 do compare_scr \"/nonexistent/x.scr\" \"m\" end\n");
        g.frames(4);
        const auto f = read_file(scr);
        check("SCRIPT-EV-CAPTURE", "`screenshot \"*.scr\"` writes the 6912-byte ULA image at the next frame; "
                                   "`save_snapshot` writes its file at the next boundary; an unwritable "
                                   "snapshot and an unreadable `compare_scr` file are run-time errors",
              ok && f.size() == 6912 && f == g.dbg->ula_screen_dump() && !read_file(sna).empty() &&
                  g.eng->runtime_errors() == 2 && g.sink.count("cannot read") == 1 &&
                  g.sink.count("`save_snapshot` \"/nonexistent/x.sna\" was not written") == 1,
              "scr=" + std::to_string(f.size()) + " " + g.sink.tail());
    }
}

static void boundary_rows() {
    {
        // GH #222 for RANGES: ports 0x00..0xFF decode on the low byte, so the
        // high byte (A for OUT (n),A; A for IN A,(n)) does not matter.
        //   LD A,0x12 ; OUT (0x15),A ; OUT (0x21),A ; OUT (0x10),A ;
        //   LD A,0x34 ; IN A,(0x15) ; IN A,(0x17) ; JR $
        Rig g({0x3E, 0x12, 0xD3, 0x15, 0xD3, 0x21, 0xD3, 0x10, 0x3E, 0x34, 0xDB, 0x15, 0xDB, 0x17, 0x18, 0xFE});
        const bool ok = g.load("on io_write 0x10..0x20 do log \"W ${PORT:x4}\" end\n"
                               "on io_read 0x14..0x16 do log \"R ${PORT:x4}\" end\n"
                               "on io_write 0x15 do log \"S ${PORT:x4}\" end\n");
        g.frames(1);
        check("SCRIPT-EV-IO-RANGE-LOW", "a port range inside 0x00..0xFF decodes on the low byte like a single "
                                        "port (GH #222): `io_write 0x10..0x20` sees OUT (0x15) and OUT (0x10) with "
                                        "A = 0x12 in the high byte and not OUT (0x21); `io_read 0x14..0x16` sees "
                                        "IN A,(0x15) and not (0x17); the single port 0x15 agrees",
              ok && g.hits(0) == 2 && g.sink.count("W 1215") == 1 && g.sink.count("W 1210") == 1 &&
                  g.hits(1) == 1 && g.sink.count("R 3415") == 1 && g.hits(2) == 1 && g.sink.count("S 1215") == 1,
              "hits=" + std::to_string(g.hits(0)) + "/" + std::to_string(g.hits(1)) + "/" +
                  std::to_string(g.hits(2)) + " " + g.sink.tail(4));
    }
    {
        // Two writes at ONE boundary (LD (nn),HL): the first fails, and the
        // second — already in the same drain — must not enter the dead rule.
        //   LD HL,0x1234 ; LD (0x9000),HL ; JR $
        Rig g({0x21, 0x34, 0x12, 0x22, 0x00, 0x90, 0x18, 0xFE});
        const bool ok = g.load("on write 0x9000..0x9001 do log \"q ${1 / (VALUE - 0x34)}\" end\n");
        g.frames(1);
        const auto r = g.eng->rules();
        check("SCRIPT-EV-RUNTIME-SAME-BOUNDARY", "a rule that fails on the first of two writes delivered at one "
                                                 "boundary is not entered for the second",
              ok && r.size() == 1 && r[0].dead && r[0].hits == 1 && g.sink.count("] q ") == 0,
              "hits=" + std::to_string(r.empty() ? 0 : r[0].hits) + " " + g.sink.tail(3));
    }
    {
        // A save issued mid-frame by a rule that also stops: a pump that finds
        // the machine paused INSIDE a frame leaves the save queued — running
        // the frame out would move the user's machine — and the save happens
        // at the next frame boundary after the resume.
        Rig g(kLoop);
        const std::string sna = "/tmp/jnext_sev_mid.sna";
        std::remove(sna.c_str());
        const bool ok = g.load("on write 0x9000 once when FRAME == 1 do save_snapshot \"" + sna + "\" stop end\n");
        // Two frames in ONE tick, as a fast-forwarding loop owner runs them:
        // the pump then sees frame 0 ended AND the machine stopped inside
        // frame 1, which is when `on_frame_ended()` reaches the engine mid-frame.
        g.emu.run_frame();
        g.emu.run_frame();
        const uint64_t clk = g.emu.clock().get();
        g.dbg->pump(jnext::dbg::PumpBudget{});
        const bool held = g.paused() && g.dbg->time().frame == 1 && g.dbg->time().cycle_in_frame > 0 &&
                          read_file(sna).empty() && g.emu.clock().get() == clk &&
                          g.sink.count("advanced to the frame boundary") == 0;
        g.dbg->run(g.tc);
        g.frames(2);
        check("SCRIPT-EV-SNAPSHOT-BOUNDARY", "a `save_snapshot` queued mid-frame is not written while the machine "
                                             "is paused inside the frame (nothing advances it), and is written at the "
                                             "next frame boundary after the resume",
              ok && held && !read_file(sna).empty() && g.eng->runtime_errors() == 0,
              "held=" + std::to_string(held) + " " + g.sink.tail(3));
    }
    {
        // OUT (n),A puts A on the high byte: A = 0x12.
        //   LD A,0x12 ; OUT (0x20),A ; OUT (0x21),A ; OUT (0x0F),A ; OUT (0xFE),A ; JR $
        Rig g({0x3E, 0x12, 0xD3, 0x20, 0xD3, 0x21, 0xD3, 0x0F, 0xD3, 0xFE, 0x18, 0xFE});
        const bool ok = g.load("on io_write 0x1220..0x1221 do log \"R ${PORT:x4}\" end\n"
                               "on io_write 0x1220 do log \"X ${PORT:x4}\" end\n"
                               "on io_write 0xFE do log \"L ${PORT:x4} ${VALUE:x2}\" end\n");
        g.frames(1);
        check("SCRIPT-EV-IO-RANGE", "an io range matches its ports inclusively and no others; a port above "
                                    "0xFF matches all 16 bits; a port <= 0xFF matches its low byte whatever the "
                                    "high byte (GH #222)",
              ok && g.hits(0) == 2 && g.sink.count("R 1220") == 1 && g.sink.count("R 1221") == 1 &&
                  g.hits(1) == 1 && g.sink.count("X 1220") == 1 && g.hits(2) == 1 &&
                  g.sink.count("L 12FE 12") == 1,
              "hits=" + std::to_string(g.hits(0)) + "/" + std::to_string(g.hits(1)) + "/" +
                  std::to_string(g.hits(2)) + " " + g.sink.tail(4));
    }
}

// =========================================================================
// NEXTREG / COPPER / DMA
// =========================================================================

static constexpr uint16_t HALT_WORD = 0x8000u | 0x1FFu;
static uint16_t move_word(uint8_t reg, uint8_t val) { return static_cast<uint16_t>(((reg & 0x7F) << 8) | val); }
static uint16_t wait_word(int hpos, int vpos) {
    return static_cast<uint16_t>(0x8000u | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
}
static void copper_load(Emulator& emu, const std::vector<uint16_t>& words) {
    emu.nextreg().write(0x61, 0);
    emu.nextreg().write(0x62, 0);
    for (uint16_t w : words) {
        emu.nextreg().write(0x63, static_cast<uint8_t>(w >> 8));
        emu.nextreg().write(0x63, static_cast<uint8_t>(w & 0xFF));
    }
    emu.nextreg().write(0x61, 0);
    emu.nextreg().write(0x62, 0xC0);  // mode 11: run from 0, restart at every vsync
}

// A DMA block: port A `src` (memory, inc) -> port B `dst` (memory, inc, or
// the I/O port `dst` when `io_dst`), `len` bytes, continuous.
static void dma_block(Emulator& emu, uint16_t src, uint16_t dst, uint16_t len, bool io_dst = false) {
    Dma& d = emu.dma();
    auto w = [&](uint8_t v) { d.write(v, false); };
    w(0x7D); w(static_cast<uint8_t>(src)); w(static_cast<uint8_t>(src >> 8));
    w(static_cast<uint8_t>(len)); w(static_cast<uint8_t>(len >> 8));
    w(0x14);
    w(io_dst ? 0x28 : 0x10);
    w(0xAD); w(static_cast<uint8_t>(dst)); w(static_cast<uint8_t>(dst >> 8));
    w(0xCF); w(0x87);
}

static void device_rows() {
    {
        // §8: a `page` filter follows the PAGE, wherever it is mapped.
        //   LD A,0x11 ; LD (0x8100),A ; LD (0xA100),A ; NEXTREG 0x56,4 ;
        //   LD (0xC200),A ; JR $
        Rig g({0x3E, 0x11, 0x32, 0x00, 0x81, 0x32, 0x00, 0xA1, 0xED, 0x91, 0x56, 0x04,
               0x32, 0x00, 0xC2, 0x18, 0xFE}, MachineType::ZXN_ISSUE2);
        const unsigned p4 = g.emu.mmu().get_effective_page(4);
        const bool ok = g.load("on write page " + std::to_string(p4) + " do log \"PG ${ADDR:x4} ${PAGE}\" end\n");
        g.frames(1);
        const std::string a = "PG 8100 " + std::to_string(p4), c = "PG C200 " + std::to_string(p4);
        check("SCRIPT-EV-PAGE-FILTER", "`on write page P` fires for a write to P at 0x8000 and, after an MMU "
                                       "remap puts P at 0xC000, there too — and never for the write to another "
                                       "page between them",
              ok && g.hits(0) == 2 && g.sink.count(a) == 1 && g.sink.count(c) == 1 && g.sink.count("PG A1") == 0,
              "page=" + std::to_string(p4) + " " + g.sink.tail(3));
    }
    {
        Rig g(kLoop);
        const bool ok = g.load("on write 0x9000 do log \"w\" end\non scanline 10 do joystick 1 1 end\n");
        g.frames(3);
        const size_t before = g.subs().size();
        const uint64_t hits = g.hits(0);
        g.eng->unload_all();
        const size_t lines = g.sink.lines.size();
        g.frames(1);
        check("SCRIPT-EV-UNLOAD", "the engine keeps ONE frame-edge subscription however many deferred "
                                  "actions it queues (three frames, three joysticks); `unload_all` removes "
                                  "every subscription it made and nothing fires after it",
              ok && before == 3 && hits > 0 && g.subs().empty() && g.eng->rules().empty() &&
                  g.sink.lines.size() == lines,
              "before=" + std::to_string(before) + " after=" + std::to_string(g.subs().size()));
    }
    {
        // More events at one boundary than the ring holds: the ring shrunk to
        // 4 through the backend's test hook, and a burst of 16 Copper MOVEs
        // inside one instruction slot (the path backend rows EVT-OVF-* use).
        Rig g({0x00, 0x18, 0xFD});
        const bool ok = g.load("on copper move do log \"m ${CPC}\" end\n");
        g.emu.debug_state().event_table()->shrink_ring_for_test(4);
        std::vector<uint16_t> prog;
        for (int i = 0; i < 16; ++i) prog.push_back(move_word(0x30, static_cast<uint8_t>(0x10 + i)));
        prog.push_back(HALT_WORD);
        copper_load(g.emu, prog);
        g.emu.nextreg().write(0x62, 0x40);  // run once
        g.emu.execute_single_instruction();
        g.emu.execute_single_instruction();
        const std::string l = g.sink.first("SCRIPT: event ring overflowed at CYCLE ");
        check("SCRIPT-EV-OVERFLOW", "a ring overflow is logged ONCE per boundary — not once per kept "
                                    "delivery — with its cycle and the dropped count (§2.2), and the kept "
                                    "events still run",
              ok && g.sink.count("event ring overflowed") == 1 && l.find(" events dropped") != std::string::npos &&
                  g.hits(0) == 4 && g.sink.count("m 3") == 1,
              "n=" + std::to_string(g.sink.count("event ring overflowed")) + " hits=" + std::to_string(g.hits(0)) +
                  " " + l);
    }
    {
        // NEXTREG 0x7F,0x33 ; NOP ; JR $
        Rig g({0xED, 0x91, 0x7F, 0x33, 0x00, 0x18, 0xFE});
        g.emu.nextreg().write(0x7F, 0x5A);
        const bool ok = g.load("on nextreg 0x7F do log \"NR ${REG:x2}=${VALUE:x2} prev ${PREV:x2} now "
                               "${nextreg[0x7F]:x2} src ${SOURCE} pc ${PC:x4}\" end\n");
        g.frames(1);
        check("SCRIPT-EV-NEXTREG-CPU", "a CPU NextREG write is delivered after commit (nextreg[REG] == VALUE), "
                                       "with PREV, SOURCE == CPU and the WRITER's PC",
              ok && g.sink.count("NR 7F=33 prev 5A now 33 src 0 pc 8000") == 1, g.sink.tail());
    }
    {
        Rig g(kPark);
        g.emu.nextreg().write(0x7F, 0x00);
        g.emu.nextreg().write(0x7F, 0x3C);
        const bool ok = g.load("on nextreg 0x7F once do log \"NR src ${SOURCE} v ${VALUE:x2} prev ${PREV:x2} "
                               "now ${nextreg[0x7F]:x2}\" end\n"
                               "on copper move 0x7F once do log \"CM cpc ${CPC} reg ${REG:x2} v ${VALUE:x2}\" end\n"
                               "on copper halt once do log \"CH cpc ${CPC}\" end\n");
        copper_load(g.emu, {move_word(0x7F, 0x44), HALT_WORD});
        g.frames(2);
        check("SCRIPT-EV-COPPER", "a Copper MOVE is a NextREG write with SOURCE == COPPER, delivered after "
                                  "commit with PREV, and a `copper move` with CPC, REG, VALUE; the HALT is a "
                                  "`copper halt` at its CPC",
              ok && g.sink.count("NR src 2 v 44 prev 3C now 44") == 1 && g.sink.count("CM cpc 0 reg 7F v 44") == 1 &&
                  g.sink.count("CH cpc 1") == 1,
              g.sink.tail());
    }
    {
        // F2: the byte filter is the DESTINATION.
        Rig g(kPark);
        for (int i = 0; i < 4; ++i) g.emu.mmu().write(static_cast<uint16_t>(0xA000 + i), 0x11);
        const bool ok = g.load("on dma byte 0xA000..0xA0FF do log \"SRCRANGE\" end\n"
                               "on dma byte 0x9000..0x90FF do log \"B ${SRC:x4}->${DST:x4} ${VALUE:x2}\" end\n"
                               "on dma byte do log \"ANY\" end\n");
        dma_block(g.emu, 0xA000, 0x9000, 4);
        g.frames(2);
        check("SCRIPT-EV-DMA-DST", "`on dma byte LO..HI` matches the DESTINATION only: a range holding just "
                                   "the source never fires, the destination range sees all 4 bytes (F2)",
              ok && g.hits(0) == 0 && g.hits(1) == 4 && g.hits(2) == 4 &&
                  g.sink.count("B A003->9003 11") == 1,
              "hits=" + std::to_string(g.hits(0)) + "/" + std::to_string(g.hits(1)) + "/" +
                  std::to_string(g.hits(2)));
    }
    {
        // F1 end to end: IO_DST on `dma start`.
        Rig g(kPark);
        g.emu.nextreg().select(0x16);
        g.emu.mmu().write(0xA000, 0x42);
        const bool ok = g.load("on dma start do log \"S io_src ${IO_SRC} io_dst ${IO_DST} ${SRC:x4}->${DST:x4}\" end\n"
                               "on io_write 0x253B do log \"P ${VALUE:x2} src ${SOURCE}\" end\n");
        dma_block(g.emu, 0xA000, 0x253B, 1, /*io_dst=*/true);
        g.frames(2);
        check("SCRIPT-EV-DMA-START-IO", "a `dma start` rule reads IO_SRC / IO_DST (F1), and a DMA byte to a "
                                        "port is an `io_write` with SOURCE == DMA",
              ok && g.sink.count("S io_src 0 io_dst 1 A000->253B") == 1 && g.sink.count("P 42 src 1") == 1,
              g.sink.tail());
    }
}

// =========================================================================
// THE §3 WORKED SCRIPTS
// =========================================================================

static const char* kGuard =
    "# guard.jds — code-area write protection (ChaseTheBug ranges)\n"
    "var armed = 0\n"
    "\n"
    "disabled rom_guard: on write 0x0000..0x3FFF do\n"
    "    log \"write to code area ${ADDR:x4} (page ${PAGE:x2}) <- ${VALUE:x2} from PC ${PC:x4} src ${SOURCE}\"\n"
    "    stop \"write into MMU0/1 code area\"\n"
    "end\n"
    "\n"
    "disabled main_guard: on write 0x8000..(@__data_crt_head - 1) do\n"
    "    log \"write to main code ${ADDR:x4} <- ${VALUE:x2} from PC ${PC:x4}\"\n"
    "    stop \"write into main code area\"\n"
    "end\n";

static const char* kMmu =
    "disabled mmu_guard: on nextreg 0x51 when not ((nextreg[0x50] + 1 == VALUE) or (nextreg[0x50] == 0xFF and VALUE == 0xFF)) do\n"
    "    log \"MMU0 is ${nextreg[0x50]:x2} whereas MMU1 write is ${VALUE:x2} (src ${SOURCE}, PC ${PC:x4})\"\n"
    "    stop \"MMU1 inconsistent with MMU0\"\n"
    "end\n"
    "\n"
    "on nextreg 0x51 when armed == 1 do\n"
    "    log \"MMU1 write ${VALUE:x2}\"\n"
    "end\n";

static const char* kIsr =
    "# isr.jds\n"
    "on execute @isr do\n"
    "    snap isr\n"
    "    log indent (depth(isr) * 2) \"==> isr at ${PC:x4}\"\n"
    "end\n"
    "\n"
    "on execute @isr_exit do\n"
    "    log indent (depth(isr) * 2) \"<== isr at ${PC:x4}, Ret=${stack[0]:x4}\"\n"
    "    if depth(isr) == 0 then\n"
    "        log \"Warning: isr exit with empty entry stack\"\n"
    "    else\n"
    "        if changed(isr, regs) then\n"
    "            log \"Warning: registers differ on exit from isr\"\n"
    "            dump_diff isr\n"
    "            stop \"isr clobbered registers\"\n"
    "        end\n"
    "        if changed(isr, stack0) then\n"
    "            log \"Warning: top of stack modified ${isr.STACK0:x4} vs ${stack[0]:x4}\"\n"
    "            stop \"isr modified return address\"\n"
    "        end\n"
    "        if not IFF1 then\n"
    "            stop \"isr exit with interrupts disabled\"\n"
    "        end\n"
    "        if changed(isr, mmu) then\n"
    "            dump_diff isr\n"
    "            stop \"isr changed an MMU slot\"\n"
    "        end\n"
    "        unsnap isr\n"
    "    end\n"
    "end\n";

static const char* kMempoint =
    "mempoint: on write 0x2222 when VALUE == 0xB7 do\n"
    "    log \"MemPoint hit at ${ADDR:x4}: forbidden value ${VALUE:x2} from PC ${PC:x4}\"\n"
    "    stop \"MemPoint\"\n"
    "end\n";

static const char* kHostkeys =
    "on hostkey 1 do          # Alt+1 in the GUI, --script-key F 1 headless\n"
    "    enable rom_guard\n"
    "    enable main_guard\n"
    "    enable mmu_guard\n"
    "    enable mempoint\n"
    "    set armed = 1\n"
    "    log \"MemWatch enabled\"\n"
    "end\n"
    "\n"
    "on hostkey 2 do\n"
    "    disable rom_guard\n"
    "    disable main_guard\n"
    "    disable mmu_guard\n"
    "    disable mempoint\n"
    "    set armed = 0\n"
    "    log \"MemWatch disabled\"\n"
    "end\n";

static const char* kPaletteInit =
    "# palette_init.jds — CI assertion, exit code is the verdict\n"
    "on execute @palette_init_done once do\n"
    "    assert mem[0x9000] == 0xAA \"sentinel missing in palette buffer\"\n"
    "    assert A == 0 \"A must be 0 after palette init\"\n"
    "    log \"PASS palette init\"\n"
    "    exit 0\n"
    "end\n"
    "on frame 300 do\n"
    "    log \"FAIL: palette_init_done never reached\"\n"
    "    exit 1\n"
    "end\n";

static const char* kSpriteY =
    "# sprite_y.jds — value-conditional port watch, reproducer for a raster bug\n"
    "on io_write 0x57 when VALUE >= 192 do\n"
    "    log \"sprite attr write ${VALUE:x2} at CYCLE ${CYCLE} frame ${FRAME} cvc ${CVC}\"\n"
    "    dump_regs\n"
    "    dump_mem 0x5C00 64\n"
    "    stop \"sprite Y >= 192\"\n"
    "end\n";

static const char* kLatency =
    "# latency.jds — interrupt acceptance to handler entry, in master cycles\n"
    "var t_int = 0\n"
    "on interrupt do\n"
    "    set t_int = CYCLE\n"
    "end\n"
    "on execute 0x0038 do\n"
    "    log \"IM1 handler after ${CYCLE - t_int} master cycles (cvc ${CVC}, hc_ula ${HC_ULA})\"\n"
    "end\n";

static const char* kCopper =
    "# copper.jds — Copper-side view: where along the frame did the palette flip land?\n"
    "on copper move 0x43 do\n"
    "    log \"Copper MOVE NR43=${VALUE:x2} at copper PC ${CPC} on cvc ${CVC} hc_ula ${HC_ULA}\"\n"
    "end\n"
    "on copper wait when WAIT_V == 95 do\n"
    "    log \"WAIT(95,${WAIT_H}) satisfied at cvc ${CVC} hc_ula ${HC_ULA}\"\n"
    "    assert CVC == 95 and HC_ULA >= WAIT_H \"WAIT for line 95 is satisfied on the Copper's own line 95, "
    "at or past its threshold (GH #181)\"\n"
    "end\n"
    "on copper halt once do log \"copper HALT at ${CPC}\" end\n";

static const char* kDma =
    "# dma.jds — DMA-side view: a transfer must stay inside the sprite pattern upload window\n"
    "on dma start do\n"
    "    log \"DMA ${SRC:x4} -> ${DST:x4} len ${LEN} mode ${DMA_MODE} (io dst ${IO_DST})\"\n"
    "end\n"
    "on dma byte when not IO_DST and (DST < 0x4000) do\n"
    "    stop \"DMA wrote into ROM/banked code at ${DST:x4} from ${SRC:x4}\"\n"
    "end\n"
    "on dma end do assert LEN == 256 \"sprite upload must move exactly 256 bytes\" end\n";

// `line.jds`, with the capture path made the row's own (the design's is
// /tmp/at-1M.png).
static std::string line_jds(const std::string& png) {
    return "# line.jds — raster position assertions\n"
           "on scanline 95 do\n"
           "    assert nextreg[0x43] & 0x70 == 0x10 \"palette select wrong at line 95\"\n"
           "end\n"
           "on cycle 1000000 once do\n"
           "    screenshot \"" + png + "\"\n"
           "end\n";
}

static const std::string kWorkMap = "/tmp/jnext_sev_work.map";

static void load_work_map(Rig& g) {
    write_file(kWorkMap, "__data_crt_head = $8200 ; const\n"
                         "isr = $0038 ; const\n"
                         "isr_exit = $003C ; const\n"
                         "palette_init_done = $8008 ; const\n");
    g.dbg->load_map(kWorkMap, jnext::dbg::MapFormat::Simple);
}

static void work_rows() {
    {
        Rig g(kPark);
        load_work_map(g);
        std::string bad;
        int n = 0;
        const std::vector<std::pair<const char*, std::string>> scripts = {
            {"3a+3b+3d+3e", std::string(kGuard) + kMmu + kMempoint + kHostkeys},
            {"3c", kIsr}, {"palette_init", kPaletteInit}, {"sprite_y", kSpriteY},
            {"latency", kLatency}, {"copper", kCopper}, {"dma", kDma},
            {"line", line_jds("/tmp/jnext_sev_at1M.png")}};
        for (const auto& s : scripts) {
            if (g.load(s.second, s.first)) ++n;
            else bad += std::string(s.first) + ": " + dstr(g.last.errors);
        }
        check("SCRIPT-EV-WORK-LOAD", "every §3 worked script loads and registers on a real machine with the "
                                     "MAP its symbols come from",
              n == 8 && bad.empty() && !g.subs().empty(), bad);
    }
    {
        // 3(a) + 3(e): the guards stay off until hostkey 1, then catch a write
        // into the main code range with its writer's PC.
        //   8000 3E 5A  LD A,0x5A ; 8002 32 F0 81  LD (0x81F0),A ; 8005 18 F9  JR 0x8000
        Rig g({0x3E, 0x5A, 0x32, 0xF0, 0x81, 0x18, 0xF9});
        load_work_map(g);
        const bool ok = g.load(std::string(kGuard) + kMmu + kMempoint + kHostkeys, "guard.jds");
        g.frames(2);
        const bool quiet = !g.paused();
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(2);
        check("SCRIPT-EV-WORK-GUARD", "3(a)+(e): disabled guards let the program write its own code area; after "
                                      "hostkey 1 the next write into 0x8000..__data_crt_head-1 stops with the "
                                      "address, value and writer's PC logged",
              ok && quiet && g.paused() && g.sink.count("MemWatch enabled") == 1 &&
                  g.sink.count("write to main code 81F0 <- 5A from PC 8002") == 1 &&
                  g.sink.count("SCRIPT STOP: write into main code area at PC=8002") == 1,
              g.sink.tail());
    }
    {
        // 3(b): NEXTREG 0x50,0x22 ; NEXTREG 0x51,<v> ; JR $ — consistent and not.
        auto run = [](uint8_t v, Rig*& out) {
            out = new Rig({0xED, 0x91, 0x50, 0x22, 0xED, 0x91, 0x51, v, 0x18, 0xFE}, MachineType::ZXN_ISSUE2);
            load_work_map(*out);
            out->load(std::string(kGuard) + kMmu + kMempoint + kHostkeys, "guard.jds");
            out->dbg->raise_host_event(out->tc, "script1");
            out->frames(2);
        };
        Rig* good = nullptr;
        Rig* bad  = nullptr;
        run(0x23, good);
        run(0x30, bad);
        check("SCRIPT-EV-WORK-MMU", "3(b): MMU1 = MMU0 + 1 passes; MMU1 = 0x30 after MMU0 = 0x22 stops with "
                                    "both values and the writing NEXTREG's PC (0x8004), SOURCE CPU",
              !good->paused() && good->sink.count("MMU1 write 23") == 1 && bad->paused() &&
                  bad->sink.count("MMU0 is 22 whereas MMU1 write is 30 (src 0, PC 8004)") == 1 &&
                  bad->sink.count("SCRIPT STOP: MMU1 inconsistent with MMU0") == 1,
              good->sink.tail(2) + " | " + bad->sink.tail(3));
        delete good;
        delete bad;
    }
    {
        // 3(c): an IM1 handler in RAM (MMU0 -> RAM page 0x10). The clean
        // handler is NOP NOP NOP EI ; RET at 0x3C; the clobbering one loads HL.
        auto run = [](bool clobber, Rig*& out) {
            out = new Rig({0xED, 0x56, 0xFB, 0x18, 0xFE}, MachineType::ZXN_ISSUE2);  // IM 1 ; EI ; JR $
            out->emu.nextreg().write(0x50, 0x10);
            const uint8_t clean[] = {0x00, 0x00, 0x00, 0xFB, 0xC9};
            const uint8_t dirty[] = {0x21, 0x34, 0x12, 0xFB, 0xC9};
            for (int i = 0; i < 5; ++i) out->emu.mmu().write(static_cast<uint16_t>(0x38 + i), clobber ? dirty[i] : clean[i]);
            load_work_map(*out);
            out->load(kIsr, "isr.jds");
            out->frames(3);
        };
        Rig* good = nullptr;
        Rig* bad  = nullptr;
        run(false, good);
        run(true, bad);
        check("SCRIPT-EV-WORK-ISR", "3(c): a clean IM1 handler is traced in and out with no stop; one that "
                                    "clobbers HL stops `isr clobbered registers` with a dump_diff",
              !good->paused() && good->sink.count("==> isr at 0038") >= 2 &&
                  good->sink.count("<== isr at 003C, Ret=8003") >= 2 && bad->paused() &&
                  bad->sink.count("SCRIPT STOP: isr clobbered registers") == 1 &&
                  bad->sink.count("dump_diff isr:") >= 1,
              good->sink.tail(3) + " | " + bad->sink.tail(3));
        delete good;
        delete bad;
    }
    {
        // 3(d) on a RAM-mapped 0x2222 (MMU1 -> page 0x11):
        //   LD A,0 ; LD (0x2222),A ; LD A,0xB7 ; LD (0x2222),A ; JR $
        Rig g({0x3E, 0x00, 0x32, 0x22, 0x22, 0x3E, 0xB7, 0x32, 0x22, 0x22, 0x18, 0xFE}, MachineType::ZXN_ISSUE2);
        g.emu.nextreg().write(0x51, 0x11);
        const bool ok = g.load(kMempoint, "mempoint.jds");
        g.frames(2);
        check("SCRIPT-EV-WORK-MEMPOINT", "3(d): a write of 0x00 to 0x2222 passes, the write of 0xB7 stops "
                                         "`MemPoint` with the writer's PC",
              ok && g.paused() && g.hits(0) == 1 && g.sink.count("MemPoint hit at 2222: forbidden value B7 from PC 8007") == 1,
              g.sink.tail());
    }
    {
        // 3(f) palette_init: LD A,0xAA ; LD (0x9000),A ; XOR A ; NOP <- 0x8008 is palette_init_done
        Rig g({0x3E, 0xAA, 0x32, 0x00, 0x90, 0xAF, 0x00, 0x00, 0x00, 0x18, 0xFE});
        load_work_map(g);
        const bool ok = g.load(kPaletteInit, "palette_init.jds");
        g.frames(2);
        check("SCRIPT-EV-WORK-PALETTE", "3(f) palette_init: both asserts hold at @palette_init_done, PASS is "
                                        "logged and the verdict is exit 0",
              ok && g.host_exits == std::vector<int>{0} && g.sink.count("PASS palette init") == 1 &&
                  g.sink.count("ASSERT FAILED") == 0,
              g.sink.tail());
    }
    {
        // 3(f) sprite_y: LD A,0x40 ; OUT (0x57),A ; LD A,0xC8 ; OUT (0x57),A ; JR $
        Rig g({0x3E, 0x40, 0xD3, 0x57, 0x3E, 0xC8, 0xD3, 0x57, 0x18, 0xFE});
        const bool ok = g.load(kSpriteY, "sprite_y.jds");
        g.frames(2);
        check("SCRIPT-EV-WORK-SPRITEY", "3(f) sprite_y: a Y of 0x40 passes, 0xC8 logs, dumps the registers and "
                                        "64 bytes (4 lines) and stops",
              ok && g.paused() && g.hits(0) == 1 && g.sink.count("sprite attr write C8") == 1 &&
                  g.sink.count(" regs AF=C8") == 1 && g.sink.count(" 5C30:") == 1 &&
                  g.sink.count("SCRIPT STOP: sprite Y >= 192") == 1,
              g.sink.tail(7));
    }
    {
        // 3(f) latency: IM 1 ; EI ; JR $, with RAM at 0x38 holding EI ; RET.
        Rig g({0xED, 0x56, 0xFB, 0x18, 0xFE}, MachineType::ZXN_ISSUE2);
        g.emu.nextreg().write(0x50, 0x10);
        g.emu.mmu().write(0x38, 0xFB);
        g.emu.mmu().write(0x39, 0xC9);
        const bool ok = g.load(kLatency, "latency.jds");
        g.frames(3);
        const std::string l = g.sink.first("IM1 handler after ");
        long n = -1;
        if (!l.empty()) n = std::strtol(l.c_str() + l.find("after ") + 6, nullptr, 10);
        check("SCRIPT-EV-WORK-LATENCY", "3(f) latency: every frame's acceptance is followed by the handler "
                                        "entry 104 master cycles later — the IM1 acknowledge's 13 T at 3.5 MHz, "
                                        "measured from the IntAck's own CYCLE",
              ok && g.hits(0) >= 2 && g.hits(1) == g.hits(0) && n == 104, l);
    }
    {
        // 3(f) copper: WAIT(52, 95) ; MOVE NR 0x43,0x10 ; HALT. A WAIT for
        // line 95 is satisfied when the Copper's own line counter IS 95
        // (copper.vhd:94, copper.cpp:199-201) — GH #181's "the following raw
        // line" is RAW_VC, not CVC. §3(f) asserted `CVC == 96` and always
        // stopped (F7); it now asserts `CVC == 95 and HC_ULA >= WAIT_H`. The
        // script runs verbatim; the old assert, swapped back in, must stop.
        Rig g(kPark, MachineType::ZXN_ISSUE2), fixed(kPark, MachineType::ZXN_ISSUE2);
        std::string old = kCopper;
        const std::string from = "assert CVC == 95 and HC_ULA >= WAIT_H";
        old.replace(old.find(from), from.size(), "assert CVC == 96");
        const bool ok = g.load(old, "copper_old.jds") && fixed.load(kCopper, "copper.jds");
        copper_load(g.emu, {wait_word(52, 95), move_word(0x43, 0x10), HALT_WORD});
        copper_load(fixed.emu, {wait_word(52, 95), move_word(0x43, 0x10), HALT_WORD});
        g.frames(3);
        fixed.frames(3);
        check("SCRIPT-EV-WORK-COPPER", "3(f) copper: the WAIT for line 95 is delivered with WAIT_H 428 at the "
                                       "Copper's cvc 95, hc_ula 428; the script (corrected, F7) runs clean with the "
                                       "MOVE logged at copper PC 1 every frame and the HALT once at PC 2, and the "
                                       "old `CVC == 96` assert would stop it",
              ok && g.paused() && g.sink.count("WAIT(95,428) satisfied at cvc 95 hc_ula 428") == 1 &&
                  g.sink.count("ASSERT FAILED: WAIT for line 95") == 1 && !fixed.paused() &&
                  fixed.sink.count("WAIT(95,428) satisfied at cvc 95 hc_ula 428") >= 2 &&
                  fixed.sink.count("Copper MOVE NR43=10 at copper PC 1 on cvc 95") >= 2 &&
                  fixed.sink.count("copper HALT at 2") == 1 && fixed.sink.count("ASSERT FAILED") == 0,
              g.sink.tail(3) + " | " + fixed.sink.tail(3));
    }
    {
        // 3(f) dma: a 256-byte block passes; one that writes below 0x4000 stops.
        Rig good(kPark), bad(kPark);
        const bool ok = good.load(kDma, "dma.jds") && bad.load(kDma, "dma.jds");
        dma_block(good.emu, 0xA000, 0x9000, 256);
        dma_block(bad.emu, 0xA000, 0x3000, 16);
        good.frames(3);
        bad.frames(3);
        check("SCRIPT-EV-WORK-DMA", "3(f) dma: a 256-byte memory block logs its start and passes the end "
                                    "assert; a block into 0x3000 stops on its first byte",
              ok && !good.paused() && good.sink.count("DMA A000 -> 9000 len 256 mode 1 (io dst 0)") == 1 &&
                  good.sink.count("ASSERT FAILED") == 0 && bad.paused() &&
                  bad.sink.count("SCRIPT STOP: DMA wrote into ROM/banked code at 3000 from A000") == 1,
              good.sink.tail(2) + " | " + bad.sink.tail(2));
    }
    {
        // 3(f) line: NR 0x43 select bits right at line 95; a capture at cycle 1M.
        const std::string png = "/tmp/jnext_sev_at1M.png";
        std::remove(png.c_str());
        Rig good(kPark), bad(kPark);
        good.emu.nextreg().write(0x43, 0x10);
        const bool ok = good.load(line_jds(png), "line.jds") && bad.load(line_jds("/tmp/jnext_sev_unused.png"), "line.jds");
        good.frames(3);
        bad.frames(3);
        check("SCRIPT-EV-WORK-LINE", "3(f) line: with NR 0x43 = 0x10 the scanline-95 assert holds and the "
                                     "cycle-1000000 screenshot is written; with 0x00 it stops",
              ok && !good.paused() && !read_file(png).empty() && bad.paused() &&
                  bad.sink.count("ASSERT FAILED: palette select wrong at line 95") == 1,
              good.sink.tail(2) + " | " + bad.sink.tail(2));
    }
}


// =========================================================================
// BOUNDS — each limit pinned on both sides (review round 2)
// =========================================================================

static void bounds_rows() {
    {
        // A flag `set` to the state it already has keeps it (set is an OR, not
        // a toggle), and the other bits of F stay.
        Rig g(kPark);
        Z80Registers r0 = g.emu.cpu().get_registers();
        r0.AF = 0x0040;  // ZF set, CF clear
        g.emu.cpu().set_registers(r0);
        const bool ok = g.load("on execute 0x8000 once do set ZF = 1 set CF = 0 set SF = 1 end\n");
        g.frames(1);
        const uint16_t f = g.emu.cpu().get_registers().AF & 0xFF;
        check("SCRIPT-EV-MUT-FLAGS-SAME", "`set ZF = 1` on a set ZF keeps it set, `set CF = 0` on a clear CF keeps "
                                          "it clear, `set SF = 1` sets SF, and no other bit of F moves",
              ok && f == 0xC0, "F=" + hex(f));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on dma byte 0x8000..0x9FFF page 3 do log \"x\" end\n");
        check("SCRIPT-EV-REG-DMA-RANGE-PAGE", "`dma byte A..B page P` is a load error at the page (the DMA has no "
                                              "page filter, F2) — never a range rule that silently drops the page — "
                                              "and registers nothing",
              !ok && g.last.errors.size() == 1 && g.last.errors[0].pos.line == 1 &&
                  g.last.errors[0].pos.column == 33 &&
                  g.last.errors[0].message.find("no page filter") != std::string::npos && g.subs().empty() &&
                  g.eng->rules().empty(),
              dstr(g.last.errors) + " " + show(g.subs()));
    }
    {
        // The backend refuses a Port subscription whose filter is its trap
        // default (mask 0xFFFF, value 0): the second rule is refused AFTER the
        // first has subscribed, and the first must be rolled back.
        Rig g(kPark);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        const bool ok = g.load("on write 0x9000 do log \"a\" end\n"
                               "on io_write mask 0xFFFF value 0x0000 do log \"b\" end\n");
        check("SCRIPT-EV-REG-ROLLBACK", "a backend refusal of a later rule's subscription is a load error at "
                                        "that rule, and the rules already subscribed are unsubscribed — the "
                                        "script registers nothing",
              !ok && g.last.errors.size() == 1 && g.last.errors[0].pos.line == 2 &&
                  g.last.errors[0].message.find("refused") != std::string::npos && g.subs().empty() &&
                  g.eng->rules().empty(),
              dstr(g.last.errors) + " " + show(g.subs()));
    }
    {
        Rig g(kPark);
        const bool ok  = g.load("on copper wait at 1000..1023 do log \"x\" end\n", "a.jds");
        const auto s0  = g.subs();
        const bool bad = g.load("on copper wait at 1000..1024 do log \"x\" end\n", "b.jds");
        check("SCRIPT-EV-REG-COPPER-AT-MAX", "a `copper … at` range reaches the last Copper PC, 1023, and no "
                                             "further",
              ok && s0.size() == 1 && s0[0].filter.lo == 1000 && s0[0].filter.hi == 1023 && !bad &&
                  g.last.errors.size() == 1 &&
                  g.last.errors[0].message.find("outside 0..1023") != std::string::npos,
              dstr(g.last.errors) + " " + show(s0));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do log indent 300 \"deep\" end\n"
                               "on write 0x9001 do log indent 255 \"edge\" end\n");
        g.frames(1);
        const std::string d = g.sink.first("deep"), e = g.sink.first("edge");
        const std::string pad = "] " + std::string(255, ' ');
        check("SCRIPT-EV-LOG-INDENT-MAX", "`log indent` is clamped at 255: 300 indents 255 spaces, 255 indents "
                                          "255",
              ok && d.find(pad + "deep") != std::string::npos && d.find(pad + " deep") == std::string::npos &&
                  e.find(pad + "edge") != std::string::npos && e.find(pad + " edge") == std::string::npos,
              "deep=" + std::to_string(d.size()) + " edge=" + std::to_string(e.size()));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on frame 0 do dump_mem 0x8000 4096 end\n");
        g.frames(1);
        check("SCRIPT-EV-DUMP-MAX", "`dump_mem a 4096` (the bound itself) is legal: 256 lines, no error",
              ok && g.eng->runtime_errors() == 0 && g.sink.count(" 8000: ") == 1 && g.sink.count(" 8FF0: ") == 1 &&
                  g.sink.count(" 9000: ") == 0,
              "errors=" + std::to_string(g.eng->runtime_errors()) + " " + g.sink.tail(2));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on frame 0 do press \"0,4\" end\non frame 0 do press \"0,5\" end\n");
        g.frames(2);
        const auto in = g.dbg->input_state();
        check("SCRIPT-EV-KEY-COLUMN", "a `row,col` key has five columns: `0,4` presses row 0 column 4, `0,5` is "
                                      "an unknown key",
              ok && !(in.matrix[0] & (1 << 4)) && g.eng->runtime_errors() == 1 &&
                  g.sink.count("unknown key `0,5`") == 1,
              "row0=" + hex(in.matrix[0]) + " " + g.sink.tail(2));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on frame 0 do press \"a\" for 0 end\non frame 0 do press \"s\" for 1 end\n");
        g.frames(2);
        check("SCRIPT-EV-PRESS-FOR-ZERO", "`press … for 0` is a run-time error naming the bound; `for 1` is legal",
              ok && g.eng->runtime_errors() == 1 &&
                  g.sink.count("`press … for` needs at least 1 frame") == 1 && !g.eng->rules()[1].dead,
              g.sink.tail(2));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on write 0x9000 do set phys[0x10, 0x1FFF] = 0x5A end\n"
                               "on write 0x9001 do set phys[0x10, 0x2000] = 0x5B end\n");
        g.frames(1);
        uint8_t last = 0;
        g.dbg->peek(jnext::dbg::MemSpace::page(0x10), 0x1FFF, 1, &last);
        check("SCRIPT-EV-PHYS-OFFSET", "`set phys[p, 0x1FFF]` writes a page's last byte; offset 0x2000 is a "
                                       "run-time error naming the page bound",
              ok && last == 0x5A && g.eng->runtime_errors() == 1 &&
                  g.sink.count("`set phys[]` outside a page's 0..0x1FFF") == 1,
              "last=" + hex(last) + " " + g.sink.tail(2));
    }
    {
        // A rule that has already died must not be reported (or counted) again:
        // its body queues a `compare_scr` of a missing file, then fails; the
        // queued compare fails again at the edge, against the dead rule.
        Rig g(kPark);
        const bool ok = g.load("on scanline 10 once do compare_scr \"/nonexistent/x.scr\" \"m\" log \"${1 / 0}\" end\n",
                               "d.jds");
        g.frames(2);
        check("SCRIPT-EV-RUNTIME-ONCE-ONLY", "a second run-time error against a rule already disabled by one is "
                                             "neither counted nor logged again, and the exit stays a single 1",
              ok && g.eng->runtime_errors() == 1 && g.sink.count("SCRIPT ERROR d.jds:") == 1 &&
                  g.host_exits == std::vector<int>{1},
              g.sink.tail(4));
    }
}

// =========================================================================
// THE SCRIPT HOST — what the loop owners run `--script` through (WP4)
// =========================================================================

/// A machine, its Debugger, a log listener on a client of its own, and a
/// ScriptHost: what HeadlessApp / SdlApp / QtApp hold.
struct HostRig {
    Emulator                  emu;
    std::unique_ptr<Debugger> dbg;
    Sink                      sink;
    ClientId                  tc = jnext::dbg::CLIENT_NONE;
    std::unique_ptr<ScriptHost> host;
    explicit HostRig(const std::vector<uint8_t>& prog = kPark) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        for (size_t i = 0; i < prog.size(); ++i) emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        dbg = std::make_unique<Debugger>(emu);
        dbg->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        jnext::dbg::ClientInfo ci;
        ci.name = "log";
        ci.kind = jnext::dbg::ClientKind::Test;
        ci.observer = true;   // listens, arms nothing
        tc = dbg->attach(ci).value;
        dbg->set_listener(tc, &sink);
        host = std::make_unique<ScriptHost>();
        dbg->pump(jnext::dbg::PumpBudget{});
    }
    ~HostRig() {
        host.reset();
        dbg->set_listener(tc, nullptr);
        dbg->detach(tc);
        dbg.reset();
    }
    bool start(const ScriptHostOptions& o) { return host->start(*dbg, o); }
    /// The loop owner's tick: a frame, a pump, then the exit check.
    int run(int frames) {
        for (int i = 0; i < frames; ++i) {
            emu.run_frame();
            dbg->pump(jnext::dbg::PumpBudget{});
            if (host->exit_requested()) return i;
        }
        return -1;
    }
    size_t engine_subs() const {
        size_t n = 0;
        for (const auto& x : dbg->subscriptions(true))
            if (x.owner != tc) ++n;
        return n;
    }
};

static std::string tmp_file(const std::string& name, const std::string& text) {
    const std::string path = "/tmp/jnext_sev_host_" + name;
    write_file(path, text);
    return path;
}

static void host_rows() {
    {
        HostRig g;
        ScriptHostOptions o;
        const bool ok = g.start(o);
        check("SCRIPT-HOST-NONE", "with no --script and no --map the host loads nothing, attaches nothing "
                                  "and arms nothing: a run without scripts is the run it always was",
              ok && !g.host->active() && !g.dbg->armed() && g.engine_subs() == 0 &&
                  !g.host->exit_requested() && g.host->unreached_verdicts() == 0,
              "armed=" + std::to_string(g.dbg->armed()));
    }
    {
        HostRig g;
        const std::string map = tmp_file("ok.map", "_main_loop                      = $8000 ; addr, local, , main, , main.c:12\n");
        ScriptHostOptions o;
        o.map_file = map;
        o.scripts  = {tmp_file("a.jds", "on execute @_main_loop once do log \"A\" end\n"),
                      tmp_file("b.jds", "on frame 0 do log \"B\" end\n")};
        const bool ok = g.start(o);
        g.run(1);
        const auto r = g.host->engine() ? g.host->engine()->rules() : std::vector<ScriptEngine::RuleView>{};
        check("SCRIPT-HOST-LOAD", "--map loads into the backend's symbol table before the scripts, so "
                                  "`@symbol` resolves; every --script loads in order, and they run",
              ok && g.host->active() && g.dbg->lookup_name("_main_loop") == std::optional<uint16_t>(0x8000) &&
                  r.size() == 2 && r[0].file == o.scripts[0] && r[1].file == o.scripts[1] &&
                  g.sink.count("] A") == 1 && g.sink.count("] B") == 1 &&
                  g.sink.count("--map " + map + ": 1 symbols") == 1,
              g.sink.tail(4));
    }
    {
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("good.jds", "on frame 0 do log \"G\" end\n"),
                     tmp_file("bad.jds", "on frame 0 do\n  log \"x\" +\nend\n")};
        const bool ok = g.start(o);
        check("SCRIPT-HOST-LOAD-ERROR", "a script with an error fails the start, reported as "
                                        "`SCRIPT ERROR file:line:column: message`, and NOTHING of any "
                                        "script stays registered — the good one included",
              !ok && !g.host->active() && g.engine_subs() == 0 &&
                  g.sink.count("SCRIPT ERROR " + o.scripts[1] + ":2:11: ") == 1 &&
                  g.sink.count("--script " + o.scripts[1] + ": not loaded (1 error)") == 1,
              g.sink.tail(3));
    }
    {
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {"/nonexistent/x.jds"};
        const bool a = g.start(o);
        HostRig h;
        ScriptHostOptions p;
        p.map_file = "/nonexistent/x.map";
        p.scripts  = {tmp_file("c.jds", "on frame 0 do log \"C\" end\n")};
        const bool b = h.start(p);
        HostRig k;
        ScriptHostOptions q;
        q.map_file = tmp_file("empty.map", "; nothing here\n");
        const bool c = k.start(q);
        HostRig m;
        ScriptHostOptions w;
        w.keys = {{5, 1}};
        const bool d = m.start(w);
        check("SCRIPT-HOST-START-ERRORS", "each is a failed start with its reason: an unreadable --script, "
                                          "an unloadable --map, a --map with no symbols, a --script-key with "
                                          "no --script",
              !a && g.sink.count("--script /nonexistent/x.jds: cannot be read") == 1 && !b &&
                  h.sink.count("--map /nonexistent/x.map: cannot be loaded") == 1 && !h.host->active() &&
                  !c && k.sink.count("no symbols found") == 1 && !d &&
                  m.sink.count("--script-key needs a --script") == 1,
              g.sink.tail(1) + h.sink.tail(1) + k.sink.tail(1) + m.sink.tail(1));
    }
    {
        // §6.3: the FIRST code wins — `exit 5` at frame 2, a stop at frame 4.
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("e.jds", "on frame 2 do exit 5 end\non frame 4 do stop end\n")};
        const bool ok = g.start(o);
        const int at = g.run(8);
        HostRig h;
        ScriptHostOptions p;
        p.scripts = {tmp_file("s.jds", "on frame 1 do stop \"halt\" end\n")};
        const bool ok2 = h.start(p);
        const int at2 = h.run(8);
        HostRig k;
        ScriptHostOptions q;
        q.scripts = {tmp_file("r.jds", "on frame 1 do log \"${1 / 0}\" end\n")};
        const bool ok3 = k.start(q);
        const int at3 = k.run(8);
        check("SCRIPT-HOST-EXIT", "the loop owner's exit code: `exit 5` gives 5 (and the stop it causes "
                                  "does not replace it); a `stop` under ExitNonZero gives 3, heard by the "
                                  "host's own listener; a run-time error at frame 1's edge gives 1 at the next edge",
              ok && at == 2 && g.host->exit_code() == 5 && ok2 && at2 == 1 && h.host->exit_code() == 3 &&
                  ok3 && at3 == 2 && k.host->exit_code() == 1,
              "at=" + std::to_string(at) + "/" + std::to_string(at2) + "/" + std::to_string(at3) + " codes=" +
                  std::to_string(g.host->exit_code()) + "/" + std::to_string(h.host->exit_code()) + "/" +
                  std::to_string(k.host->exit_code()));
    }
    {
        // The Qt GUI: a script never ends the program.
        HostRig g;
        g.dbg->set_stop_policy(jnext::dbg::StopPolicy::Pause);
        ScriptHostOptions o;
        o.exits   = false;
        o.scripts = {tmp_file("gui.jds", "on frame 1 do exit 4 end\n")};
        const bool ok = g.start(o);
        const int at = g.run(4);
        check("SCRIPT-HOST-GUI", "with `exits = false` (the Qt GUI) `exit 4` pauses the machine and "
                                 "requests no exit",
              ok && at == -1 && !g.host->exit_requested() && g.dbg->state().paused &&
                  g.sink.count("SCRIPT EXIT 4") == 1,
              "at=" + std::to_string(at));
    }
    {
        // --script-key FRAME N: the hostkey rule runs at the edge of frame
        // FRAME (where `on frame FRAME` runs), not before.
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("k.jds", "on hostkey 3 do log \"K${KEY} F${FRAME}\" end\n"
                                       "on frame 7 do log \"E7\" end\n"
                                       "on hostkey 2 do log \"Z${KEY} F${FRAME}\" joystick 1 0x10 end\n")};
        o.keys = {{7, 3}, {0, 2}};
        const bool ok = g.start(o);
        g.run(1);
        const uint16_t joy0 = g.dbg->input_state().joy_left12;
        g.run(6);
        const size_t before = g.sink.count("K3 F7");
        g.run(1);
        size_t e7 = 0, k3 = 0;
        for (size_t i = 0; i < g.sink.lines.size(); ++i) {
            if (g.sink.lines[i].find("] E7") != std::string::npos) e7 = i + 1;
            if (g.sink.lines[i].find("K3 F7") != std::string::npos) k3 = i + 1;
        }
        check("SCRIPT-HOST-KEY", "`--script-key 7 3` runs `on hostkey 3` once, at the end of frame 7 "
                                 "(FRAME == 7, beside `on frame 7`) and not during frames 0..6; a key for "
                                 "frame 0 runs at the end of frame 0, and what its rule queues for the edge "
                                 "(a joystick) lands at that same edge",
              ok && before == 0 && g.sink.count("K3 F7") == 1 && e7 != 0 && k3 != 0 && joy0 == 0x10 &&
                  g.sink.count("Z2 F0") == 1 && g.host->unreached_verdicts() == 0,
              g.sink.tail(4));
    }
    {
        // §7.3: the verdicts a run never reached.
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("v.jds", "on frame 1 do exit 0 end\n"
                                       "on frame 300 do if 1 then compare_scr \"/nonexistent\" \"m\" end end\n"
                                       "on frame 0 do log \"no verdict\" end\n")};
        o.keys = {{400, 1}};
        const bool ok = g.start(o);
        const size_t at_start = g.host->unreached_verdicts();
        g.run(1);
        const size_t after_frame0 = g.host->unreached_verdicts();
        check("SCRIPT-HOST-UNREACHED", "the unreached verdicts are the rules holding `exit` / `compare_scr` "
                                       "(an `if` branch included) that have not fired, plus scheduled keys "
                                       "not yet delivered — and a rule with no verdict never counts",
              ok && at_start == 3 && after_frame0 == 3 && g.run(2) == 0 && g.host->unreached_verdicts() == 2,
              "start=" + std::to_string(at_start) + " f0=" + std::to_string(after_frame0) + " end=" +
                  std::to_string(g.host->unreached_verdicts()));
    }
}

static void host_deferred_rows() {
    // A deferred action still queued counts as unreached: the machine is
    // stopped mid-frame by the rule that queued it, before the edge.
    HostRig g;
    g.dbg->set_stop_policy(jnext::dbg::StopPolicy::Pause);
    ScriptHostOptions o;
    o.exits   = false;
    o.scripts = {tmp_file("d.jds", "on scanline 100 once do joystick 1 0x01 stop \"mid\" end\n")};
    const bool ok = g.start(o);
    g.run(1);
    const bool mid = g.dbg->state().paused;
    const size_t pending = g.host->unreached_verdicts();
    g.dbg->run(g.tc);
    g.run(1);
    check("SCRIPT-HOST-UNREACHED-DEFERRED", "a deferred action still queued (a joystick issued mid-frame, the "
                                            "machine stopped before the edge) counts as unreached, and no "
                                            "longer once the edge has applied it",
          ok && mid && pending == 1 && g.host->unreached_verdicts() == 0 &&
              g.dbg->input_state().joy_left12 == 0x01,
          "mid=" + std::to_string(mid) + " pending=" + std::to_string(pending) + " after=" +
              std::to_string(g.host->unreached_verdicts()));
}

static void host_round1_rows() {
    {
        // An `exit` in an `else` branch is a verdict too.
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("else.jds", "on frame 300 do if FRAME == 0 then log \"x\" else exit 2 end end\n")};
        const bool ok = g.start(o);
        check("SCRIPT-HOST-UNREACHED-ELSE", "a rule whose `exit` sits in the `else` branch of an `if` counts as "
                                            "an unreached verdict until it fires",
              ok && g.host->unreached_verdicts() == 1, "n=" + std::to_string(g.host->unreached_verdicts()));
    }
    {
        // A key scheduled for a frame already past is delivered at the NEXT
        // edge, not lost (and then counted as never delivered for ever).
        Rig g(kPark);
        const bool ok = g.load("on hostkey 3 do log \"K3 F${FRAME}\" end\n");
        g.frames(3);
        g.eng->queue_host_key(0, 3);
        const size_t pending = g.eng->unreached_verdicts();
        g.frames(1);
        check("SCRIPT-HOST-KEY-PAST", "a key queued for frame 0 after frame 2 has ended is delivered at the "
                                      "next edge (frame 3), once, and is no longer pending",
              ok && pending == 1 && g.sink.count("K3 F3") == 1 && g.sink.count("K3 F") == 1 &&
                  g.eng->unreached_verdicts() == 0,
              "pending=" + std::to_string(pending) + " " + g.sink.tail(2));
    }
    {
        Rig g(kPark);
        const bool ok = g.load("on hostkey 4 do log \"K4\" end\n");
        g.eng->queue_host_key(5, 4);
        const size_t before = g.eng->unreached_verdicts();
        g.eng->unload_all();
        g.frames(7);
        check("SCRIPT-HOST-KEY-UNLOAD", "`unload_all` drops the scheduled keys too: nothing pending, nothing "
                                        "raised",
              ok && before == 1 && g.eng->unreached_verdicts() == 0 && g.sink.count("K4") == 0,
              "before=" + std::to_string(before));
    }
}

static void host_gui_rows() {
    {
        // A static stop under ExitNonZero: the loop owner's exit code is 3.
        HostRig g;
        ScriptHostOptions o;
        o.scripts = {tmp_file("ss.jds", "on execute 0x8000 do stop \"here\" end\n")};
        const bool ok = g.start(o);
        const int at = g.run(2);
        check("SCRIPT-HOST-STATIC-EXIT3", "a stop-only `execute` rule (a static Stop) under the headless policy "
                                          "still gives the loop owner exit 3, its stop logged",
              ok && at == 0 && g.host->exit_code() == 3 && g.sink.count("SCRIPT STOP: here at PC=8000") == 1,
              "at=" + std::to_string(at) + " code=" + std::to_string(g.host->exit_code()));
    }
    {
        // The Script tab's Event column: the filter as REGISTERED.
        Rig g(kPark);
        const std::string map = tmp_file("d.map", "SYM = $9100 ; const\n");
        g.dbg->load_map(map, jnext::dbg::MapFormat::Simple);
        const bool ok = g.load("on write 0x9000..0x9001 do log \"x\" end\n"
                               "on write @SYM do log \"x\" end\n"
                               "on execute page 4..5 do log \"x\" end\n"
                               "on io_write 0xFE do log \"x\" end\n"
                               "on frame 10 do log \"x\" end\n"
                               "on copper move 0x43 at 1..3 do log \"x\" end\n"
                               "on dma byte 0x4000..0x57FF do log \"x\" end\n"
                               "on hostkey 3 do log \"x\" end\n");
        std::string got;
        for (const auto& r : g.eng->rules()) got += r.event + "|";
        check("SCRIPT-HOST-DESCRIBE", "each rule's event is described with its filter as registered — a "
                                      "range, a symbol resolved, a page range, a port, a frame, a Copper "
                                      "register and PC range, a DMA range, a host key",
              ok && got == "write 9000..9001|write 9100|execute page 4..5|io_write 00FE|frame 10|"
                           "copper move 0043 at 1..3|dma byte 4000..57FF|hostkey 3|",
              got);
    }
    {
        // status(): the FIRST exit, the stops a rule caused (not an `on stop`
        // rule's own `stop`, which only logs), the last stop's reason.
        HostRig g(kLoop);
        g.dbg->set_stop_policy(jnext::dbg::StopPolicy::Pause);
        ScriptHostOptions o;
        o.exits = false;
        o.scripts = {tmp_file("st.jds", "on frame 0 do exit 4 end\non frame 2 do exit 6 end\n"
                                        "on frame 3 do stop \"three\" end\non stop do stop \"nested\" end\n")};
        const bool ok = g.start(o);
        for (int i = 0; i < 4; ++i) {
            g.run(1);
            if (g.dbg->state().paused) g.dbg->run(g.tc);
        }
        const auto st = g.host->engine()->status();
        check("SCRIPT-HOST-STATUS", "status() keeps the FIRST exit code, counts the stops rules caused (an "
                                    "`on stop` rule's own `stop` only logs) and names the last one",
              ok && st.exit_code == std::optional<int>(4) && st.stops == 1 && st.last_stop == "three",
              "exit=" + (st.exit_code ? std::to_string(*st.exit_code) : std::string("none")) +
                  " stops=" + std::to_string(st.stops) + " last=" + st.last_stop);
    }
    {
        // The GUI's path: start() with nothing, then load from the menu.
        HostRig g(kLoop);
        ScriptHostOptions o;
        o.exits = false;
        const bool started = g.start(o);
        const bool armed0 = g.dbg->armed();
        const LoadResult r = g.host->load_file(tmp_file("gui1.jds", "on frame 1 do log \"G1\" end\n"));
        const LoadResult bad = g.host->load_file(tmp_file("gui2.jds", "on frame 1 do\n  log +\nend\n"));
        g.run(3);
        const auto lines = g.host->log_since(0);
        bool tagged = false, g1 = false, loaded = false, err = false;
        int loaded_n = 0;
        for (const auto& l : lines) {
            if (l.find("loaded at FRAME") != std::string::npos) ++loaded_n;
            if (l.find("[client") != std::string::npos || l.find("ATTACH") != std::string::npos) tagged = true;
            if (l.find("] G1") != std::string::npos) g1 = true;
            if (l.find("loaded at FRAME") != std::string::npos) loaded = true;
            if (l.find("SCRIPT ERROR") != std::string::npos && l.find("gui2.jds:2:") != std::string::npos) err = true;
        }
        check("SCRIPT-HOST-GUI-LOAD", "a script loaded at run time (the menu) registers and runs; one with "
                                      "an error is refused and the first stays; the log carries the engine's "
                                      "lines without the backend's client tag, the load note and the error",
              started && !armed0 && r.ok() && !bad.ok() && g.host->files().size() == 1 && g.dbg->armed() &&
                  g1 && loaded && loaded_n == 1 && err && !tagged,
              "files=" + std::to_string(g.host->files().size()) + " lines=" + std::to_string(lines.size()));
    }
    {
        HostRig g(kLoop);
        ScriptHostOptions o;
        o.exits = false;
        g.start(o);
        const std::string a = tmp_file("gui3.jds", "on write 0x9000 once do log \"A\" end\n");
        g.host->load_file(a);
        g.run(1);
        const uint64_t hits = g.host->engine()->rules()[0].hits;
        const auto again = g.host->reload();
        const bool reloaded = again.size() == 1 && again[0].ok() && g.host->files() == std::vector<std::string>{a} &&
                              g.host->engine()->rules()[0].hits == 0;
        g.host->unload_all();
        check("SCRIPT-HOST-GUI-UNLOAD", "reload() reloads the same files (rules start over); unload_all() "
                                        "drops every script AND the engine, so nothing stays armed",
              hits == 1 && reloaded && g.host->files().empty() && g.host->engine() == nullptr &&
                  !g.dbg->armed() && g.engine_subs() == 0,
              "hits=" + std::to_string(hits) + " armed=" + std::to_string(g.dbg->armed()));
    }
    {
        // The log is a ring of MAX_LOG_LINES; log_since() returns what is
        // still held after a sequence number, never past the end.
        HostRig g(kLoop);
        ScriptHostOptions o;
        o.exits = false;
        g.start(o);
        g.host->load_file(tmp_file("gui4.jds", "on write 0x9000 do log \"L ${VALUE}\" end\n"));
        g.run(1);
        const uint64_t seq = g.host->log_seq();
        const auto all = g.host->log_since(0);
        const auto none = g.host->log_since(seq);
        const auto last2 = g.host->log_since(seq - 2);
        check("SCRIPT-HOST-GUI-LOG", "the script log keeps the last MAX_LOG_LINES lines: log_since(0) is "
                                     "the whole ring, log_since(seq) is empty, log_since(seq-2) the newest two",
              seq > ScriptHost::MAX_LOG_LINES && all.size() == ScriptHost::MAX_LOG_LINES && none.empty() &&
                  last2.size() == 2 && last2[1] == all.back(),
              "seq=" + std::to_string(seq) + " all=" + std::to_string(all.size()));
    }
}

// ── STATIC STOPS — a stop-only `execute` rule is a breakpoint other clients
//    can see (a static `Stop`, no handler); its bookkeeping runs at the pause ──

static void static_stop_rows() {
    {
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8005 when A == 0x5A do stop \"five ${PC:x4} A=${A:x2} P${PAGE}\" end\n"
                               "on stop do log \"R ${REASON}\" end\n");
        const auto subs = g.subs();
        const bool shape = subs.size() == 1 && subs[0].action == jnext::dbg::Action::Stop && !subs[0].has_handler &&
                           subs[0].has_condition && g.dbg->probe_execute(0x8005) && !g.dbg->probe_execute(0x8007);
        g.frames(2);
        const auto st = g.eng->status();
        check("SCRIPT-EV-STATIC-STOP", "a stop-only `execute` rule registers as a static Stop with its `when` as "
                                       "the condition and no handler — listed by probe_execute at its PC, not "
                                       "elsewhere — and when it fires the machine pauses at it, its message "
                                       "(payload and registers interpolated) is logged as before, `on stop` sees it "
                                       "as REASON, and its hit and the stop verdict are counted",
              ok && shape && g.paused() && g.pc() == 0x8005 &&
                  g.sink.count("SCRIPT STOP: five 8005 A=5A P" + std::to_string(g.emu.mmu().get_effective_page(4)) +
                               " at PC=8005 FRAME=0 CYCLE=") == 1 &&
                  g.sink.count("R five 8005 A=5A P") == 1 && g.hits(0) == 1 && st.stops == 1 &&
                  st.last_stop.rfind("five 8005 A=5A P", 0) == 0,
              show(subs) + " " + g.sink.tail(3));
    }
    {
        // Two static stops at one PC: both are counted and logged, and REASON
        // is the FIRST's — the one the backend stopped on.
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8005 do stop \"first\" end\n"
                               "on execute 0x8005 do stop \"second\" end\n"
                               "on stop do log \"R ${REASON}\" end\n");
        g.frames(1);
        check("SCRIPT-EV-STATIC-STOP-TWO", "two static stops at one PC are both counted and logged, and `on stop` "
                                           "sees the first one's message as REASON",
              ok && g.paused() && g.hits(0) == 1 && g.hits(1) == 1 && g.eng->status().stops == 2 &&
                  g.sink.count("R first") == 1 && g.sink.count("SCRIPT STOP: second") == 1,
              g.sink.tail(4));
    }
    {
        // A rule that does more than stop keeps its handler: the actions after
        // the `stop` still run, and the subscription is not a static Stop.
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8005 do stop \"s\" log \"after\" end\n");
        const auto subs = g.subs();
        g.frames(1);
        check("SCRIPT-EV-STATIC-STOP-MIXED", "a rule whose body is more than `stop` stays a handler subscription "
                                             "(Continue + handler) and runs every action",
              ok && subs.size() == 1 && subs[0].has_handler && subs[0].action == jnext::dbg::Action::Continue &&
                  g.paused() && g.sink.count("] after") == 1,
              show(subs));
    }
    {
        Rig g(kWriter);
        const bool ok = g.load("on execute 0x8005 when A == 0x11 do stop \"never\" end\n");
        g.frames(2);
        check("SCRIPT-EV-STATIC-STOP-WHEN", "a static stop whose condition does not hold neither stops nor counts",
              ok && !g.paused() && g.hits(0) == 0 && g.sink.count("SCRIPT STOP") == 0 &&
                  g.eng->status().stops == 0,
              g.sink.tail(2));
    }
    {
        // `once`, `enable` re-arm, and unload, for a static stop.
        Rig g(kLoop);
        const bool ok = g.load("w: on execute 0x8003 once do stop \"loop\" end\n"
                               "on hostkey 1 do enable w end\n");
        g.frames(1);
        const bool first = g.paused() && g.hits(0) == 1;
        g.dbg->run(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        g.frames(2);
        const bool spent = !g.paused() && g.hits(0) == 1 && !g.subs()[0].enabled;
        g.dbg->raise_host_event(g.tc, "script1");
        g.frames(1);
        const bool rearmed = g.paused() && g.hits(0) == 2;
        g.eng->unload_all();
        check("SCRIPT-EV-STATIC-STOP-ONCE", "a static stop with `once` stops once and is spent, `enable` re-arms "
                                            "it for exactly one more, and unload removes it",
              ok && first && spent && rearmed && g.subs().empty() && g.sink.count("SCRIPT STOP: loop") == 2,
              "first=" + std::to_string(first) + " spent=" + std::to_string(spent) + " rearmed=" +
                  std::to_string(rearmed) + " " + g.sink.tail(2));
    }
    {
        // A page range: one static stop per page; `once` spends them all.
        Rig g({0xC3, 0x00, 0xA0});
        g.emu.mmu().write(0xA000, 0xC3);
        g.emu.mmu().write(0xA001, 0x00);
        g.emu.mmu().write(0xA002, 0x80);
        const unsigned p4 = g.emu.mmu().get_effective_page(4);
        const bool ok = g.load("on execute page " + std::to_string(p4) + ".." + std::to_string(p4 + 1) +
                               " once do stop \"pg\" end\n");
        g.frames(1);
        const bool first = g.paused() && g.hits(0) == 1;
        g.dbg->run(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        g.frames(2);
        size_t live = 0;
        for (const auto& x : g.subs()) if (x.enabled) ++live;
        check("SCRIPT-EV-STATIC-STOP-PAGES", "a stop-only page range is a static stop per page, and its `once` "
                                             "spends every page's subscription at the first stop",
              ok && first && !g.paused() && g.hits(0) == 1 && live == 0 && g.subs().size() == 2,
              "live=" + std::to_string(live) + " hits=" + std::to_string(g.hits(0)));
    }
}

int main() {
    std::printf("script_events_test — the debugger DSL engine on a real machine (GH #26 WP3)\n");

    // An EvalError no row expected must not end the run: the group fails, the
    // rest still run, and its unreached rows are missing from `Total:`.
    auto run_group = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const EvalError& e) {
            ++g_fail;
            std::printf("  FAIL (group %s aborted): unexpected EvalError %s\n", name, e.d.to_string().c_str());
        }
    };
    run_group("reg", reg_rows);
    run_group("delivery", delivery_rows);
    run_group("stop", stop_rows);
    run_group("mutation", mutation_rows);
    run_group("input", input_rows);
    run_group("boundary", boundary_rows);
    run_group("device", device_rows);
    run_group("work", work_rows);
    run_group("bounds", bounds_rows);
    run_group("host", host_rows);
    run_group("host_deferred", host_deferred_rows);
    run_group("host_round1", host_round1_rows);
    run_group("host_gui", host_gui_rows);
    run_group("static_stop", static_stop_rows);

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
