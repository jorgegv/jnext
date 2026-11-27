// jnext::dbg target seam (epic #486, R1.1 / R1.2).
//
// TGT-LOC-*  the LOCAL facade, `Debugger(Emulator&)`: characterisation rows. They
//            were committed BEFORE the seam refactor and pass on the unrefactored
//            tree, which is their oracle ("behaviour must not change", design
//            R1.1 acceptance): the MUTATE texts, refusal orders and write counts
//            the existing suites leave unpinned.
// TGT-01-*   `Debugger(Target&)` over a `FakeTarget`: dispatch, local-only
//            refusals, and the facade's normalisation of hostile target answers.
// TGT-02..04 the capability model.
//
// There is no VHDL counterpart (debugger architecture, jnext-internal).
//
// Run: ./build/test/debug_target_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/rzx_player.h"
#include "debug/local_target.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/rewind_buffer.h"
#include "memory/mmu.h"
#include "platform/emulator_boot.h"
#include "port/nextreg.h"
#include "version.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"
#include "fake_target.h"

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::CapabilitySet;
using jnext::dbg::FakeTarget;
using jnext::dbg::RegisterValidity;
using jnext::dbg::MemSpace;
using jnext::dbg::RegId;
using jnext::dbg::ResetKind;
using jnext::dbg::Result;

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
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ", detail.c_str());
    }
}

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%X", v);
    return b;
}

static jnext::dbg::ClientInfo client(const char* name) {
    jnext::dbg::ClientInfo ci;
    ci.name = name;
    ci.kind = jnext::dbg::ClientKind::Test;
    return ci;
}

/// Records every push; only the log lines matter here.
struct RecListener : jnext::dbg::Listener {
    std::vector<std::string> logs;
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(ClientId) override {}
    void on_reset(ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { logs.push_back(t); }
    std::vector<std::string> mutates() const {
        std::vector<std::string> out;
        for (const auto& l : logs)
            if (l.rfind("MUTATE ", 0) == 0) out.push_back(l);
        return out;
    }
    void clear() { logs.clear(); }
};

static constexpr uint16_t PROG = 0x8000;

/// A 48K machine with a rewind ring (so a MUTATE writes a mark) and the program
/// NOP NOP NOP JR $ at 0x8000, PC and SP set by hand, frame interrupts off.
static void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 10;
    emu.init(cfg);
    const uint8_t prog[] = {0x00, 0x00, 0x00, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = PROG; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

/// Is a rewind mark recorded at the current clock? (see RewindBuffer::mark_mutated)
static bool marked(Emulator& emu) {
    RewindBuffer* rb = emu.rewind_buffer();
    return rb && rb->replay_crosses_mutation(emu.clock().get() + 1);
}

struct Local {
    Emulator    emu;
    Debugger*   dbg = nullptr;
    RecListener rec;
    ClientId    a = 0;
    Local() {
        build(emu);
        dbg = new Debugger(emu);
        a = dbg->attach(client("A")).value;
        dbg->set_listener(a, &rec);
        emu.run_frame();
        emu.run_frame();
        dbg->pause(a);
        // The frames the ring needs are in; the mutation attributes from here.
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        rec.clear();
    }
    ~Local() { delete dbg; }
};

static void loc_poke_rows() {
    {
        Local m;
        const uint8_t v = 0x5A;
        const auto r = m.dbg->poke(m.a, MemSpace::page(10), 0x2000, 1, &v);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-01", "poke(Page) at 0x2000 (one past the page): RefusedUnavailable, 0 landed, "
                            "one MUTATE line with the old byte 0x0, rewind marked",
              r.status == Result::RefusedUnavailable && r.value == 0 && lines.size() == 1 &&
                  lines[0].find("mem page10:0x2000 0x0 -> 0x5A") != std::string::npos && marked(m.emu),
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Local m;
        const uint8_t v = 0;
        const auto r = m.dbg->poke(m.a, MemSpace::page(10), 0x0100, 0, &v);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-02", "poke(Page) n=0: Ok, 0, one '0 bytes' range line, rewind marked",
              r.status == Result::Ok && r.value == 0 && lines.size() == 1 &&
                  lines[0].find("mem page10:0x0100 0 bytes") != std::string::npos && marked(m.emu),
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Local m;
        const uint8_t v[4] = {1, 2, 3, 4};
        const auto r = m.dbg->poke(m.a, MemSpace::page(10), 0x1FFE, 4, v);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-03", "poke(Page) crossing the page end: RefusedUnavailable, the in-page count, "
                            "a '2 bytes' range line",
              r.status == Result::RefusedUnavailable && r.value == 2 && lines.size() == 1 &&
                  lines[0].find("mem page10:0x1FFE 2 bytes") != std::string::npos,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Local m;
        const uint8_t v[4] = {1, 2, 3, 4};
        const auto r = m.dbg->poke(m.a, MemSpace::cpu(), 0x3FFE, 4, v);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-04", "poke(Cpu) straddling ROM and RAM: RefusedReadOnly, the RAM byte count, "
                            "'2 of 4 bytes (the rest read-only)'",
              r.status == Result::RefusedReadOnly && r.value == 2 && lines.size() == 1 &&
                  lines[0].find("2 of 4 bytes (the rest read-only)") != std::string::npos,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Local m;
        const uint8_t v[2] = {1, 2};
        const auto r = m.dbg->poke(m.a, MemSpace::cpu(), 0x0100, 2, v);
        check("TGT-LOC-05", "poke(Cpu) wholly on ROM: RefusedReadOnly, 0, no MUTATE line, no rewind mark",
              r.status == Result::RefusedReadOnly && r.value == 0 && m.rec.mutates().empty() &&
                  !marked(m.emu),
              std::to_string(m.rec.mutates().size()) + " lines");
    }
    {
        Local m;
        const uint8_t v = 1;
        const auto r = m.dbg->poke(m.a, MemSpace::cpu(), 0x9000, 0, &v);
        check("TGT-LOC-06", "poke(Cpu) n=0: Ok, 0, no line, no mark",
              r.status == Result::Ok && r.value == 0 && m.rec.mutates().empty() && !marked(m.emu));
    }
}

static void loc_refusal_rows() {
    Local m;
    m.emu.rzx_player().start(RzxRecording{});
    const uint8_t b[5] = {0, 0, 0, 0, 0};
    const uint8_t v = 1;
    // Argument faults come BEFORE the RZX refusal ...
    const bool bad_args =
        m.dbg->poke(m.a, MemSpace::cpu(), 0x9000, 1, nullptr).status == Result::RefusedUnavailable &&
        m.dbg->set_mmu_slot(m.a, 8, 0) == Result::RefusedUnavailable &&
        m.dbg->set_sprite_attr_raw(m.a, 0, b, 4) == Result::RefusedUnavailable &&
        m.dbg->write_pattern_ram(m.a, 0, nullptr, 1) == Result::RefusedUnavailable;
    // ... and the same calls with valid arguments are RefusedRzx, writing nothing.
    const uint8_t before = m.emu.mmu().peek(0x9000);
    const bool valid_args =
        m.dbg->poke(m.a, MemSpace::cpu(), 0x9000, 1, &v).status == Result::RefusedRzx &&
        m.dbg->set_mmu_slot(m.a, 6, 0) == Result::RefusedRzx &&
        m.dbg->set_sprite_attr_raw(m.a, 0, b, 5) == Result::RefusedRzx &&
        m.dbg->write_pattern_ram(m.a, 0, &v, 1) == Result::RefusedRzx &&
        m.dbg->set_register(m.a, RegId::A, 1) == Result::RefusedRzx &&
        m.dbg->nextreg_write(m.a, 0x15, 1) == Result::RefusedRzx &&
        m.dbg->port_out(m.a, 0x00FE, 1) == Result::RefusedRzx &&
        m.dbg->port_in(m.a, 0x00FE).status == Result::RefusedRzx;
    check("TGT-LOC-07", "under an RZX playback invalid arguments are RefusedUnavailable, valid ones "
                        "RefusedRzx, and nothing is written or logged",
          bad_args && valid_args && m.emu.mmu().peek(0x9000) == before && m.rec.mutates().empty(),
          std::string(bad_args ? "" : "args ") + (valid_args ? "" : "valid"));
}

static void loc_mutate_text_rows() {
    {
        Local m;
        Z80Registers r = m.emu.cpu().get_registers();
        r.AF = 0x1200;
        m.emu.cpu().set_registers(r);
        const Result bad = m.dbg->set_register(m.a, RegId::Count, 1);
        const bool quiet = m.rec.mutates().empty();
        const Result ok = m.dbg->set_register(m.a, RegId::A, 0x1234);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-08", "set_register(Count) is Unsupported with no line; set_register(A, 0x1234) "
                            "writes 0x34 and logs the old A and the raw value",
              bad == Result::Unsupported && quiet && ok == Result::Ok &&
                  (m.emu.cpu().get_registers().AF >> 8) == 0x34 && lines.size() == 1 &&
                  lines[0].find("MUTATE reg A 0x12 -> 0x1234") != std::string::npos,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Local m;
        m.emu.nextreg().write(0x15, 0x03);
        const uint8_t old_page = m.emu.mmu().get_nr_page(6);
        m.rec.clear();
        const Result n = m.dbg->nextreg_write(m.a, 0x15, 0x01);
        const Result s = m.dbg->set_mmu_slot(m.a, 6, 0x20);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-09", "nextreg_write and set_mmu_slot log 'nextreg 0x15 old -> new' and "
                            "'mmu slot 6 old -> new'",
              n == Result::Ok && s == Result::Ok && lines.size() == 2 &&
                  lines[0].find("nextreg 0x15 0x3 -> 0x1") != std::string::npos &&
                  lines[1].find("mmu slot 6 " + hex(old_page) + " -> 0x20") != std::string::npos,
              lines.size() == 2 ? lines[0] + " | " + lines[1] : std::string("lines"));
    }
    {
        Local m;
        m.dbg->port_in(m.a, 0x00FE);
        const bool in_marked = marked(m.emu);
        const auto in_lines = m.rec.mutates();
        m.rec.clear();
        m.dbg->port_out(m.a, 0x00FE, 0x05);
        const auto out_lines = m.rec.mutates();
        check("TGT-LOC-10", "port_in logs 'port in 0x00FE v -> v' and marks the rewind ring; port_out "
                            "logs 'port out 0x00FE' '= 0x05'",
              in_lines.size() == 1 && in_lines[0].find("port in 0x00FE") != std::string::npos &&
                  in_marked && out_lines.size() == 1 &&
                  out_lines[0].find("port out 0x00FE = 0x05") != std::string::npos,
              (in_lines.empty() ? std::string("none") : in_lines[0]) + " | " +
                  (out_lines.empty() ? std::string("none") : out_lines[0]));
    }
    {
        Local m;
        const uint8_t attr[5] = {1, 2, 3, 4, 5};
        const uint8_t pat[3]  = {7, 8, 9};
        m.dbg->set_sprite_attr_raw(m.a, 3, attr, 5);
        m.dbg->write_pattern_ram(m.a, 0x0100, pat, 3);
        const auto lines = m.rec.mutates();
        check("TGT-LOC-11", "sprite writes log 'sprite 3 5 attribute bytes' and "
                            "'sprite pattern 0x0100 3 bytes'",
              lines.size() == 2 && lines[0].find("MUTATE sprite 3 5 attribute bytes") == 0 &&
                  lines[1].find("MUTATE sprite pattern 0x0100 3 bytes") == 0,
              lines.size() == 2 ? lines[0] + " | " + lines[1] : std::string("lines"));
    }
}

static void loc_state_rows() {
    {
        Local m;
        m.dbg->step_into(m.a);
        const auto st = m.dbg->state();
        check("TGT-LOC-12", "state() after a step_into: paused, pc is registers().PC, cycle and frame "
                            "are time().master_cycle and time().frame",
              st.paused && st.pc == m.dbg->registers().PC &&
                  st.cycle == m.dbg->time().master_cycle && st.frame == m.dbg->time().frame,
              "pc=" + hex(st.pc));
    }
    {
        Local m;
        m.dbg->step_into(m.a);
        const auto after_into = m.dbg->state().step_mode;
        Local n;
        n.dbg->run_to_cycle(n.a, n.emu.clock().get() + 100000);
        const auto armed = n.dbg->state().step_mode;
        // A step_over armed on a CALL: CALL 0x9000 at 0x8000 (the program of build() is NOPs).
        Local o;
        const uint8_t call[3] = {0xCD, 0x00, 0x90};
        for (int i = 0; i < 3; ++i) o.emu.mmu().write(static_cast<uint16_t>(PROG + i), call[i]);
        o.dbg->step_over(o.a);
        const auto over = o.dbg->state().step_mode;
        check("TGT-LOC-13", "state().step_mode: None after a completed step_into, RunToCycle while a "
                            "run_to_cycle is armed, Over while a step_over over a CALL is armed",
              after_into == jnext::dbg::StepMode::None && armed == jnext::dbg::StepMode::RunToCycle &&
                  over == jnext::dbg::StepMode::Over,
              std::to_string(static_cast<int>(after_into)) + "/" +
                  std::to_string(static_cast<int>(armed)) + "/" + std::to_string(static_cast<int>(over)));
    }
    {
        Local m;
        const uint8_t v = 1;
        const uint8_t attr[5] = {1, 2, 3, 4, 5};
        uint64_t rev = m.dbg->state_revision();
        bool each_once = true;
        auto once = [&](bool did) {
            const uint64_t now = m.dbg->state_revision();
            if (did && now != rev + 1) each_once = false;
            if (!did && now != rev) each_once = false;
            rev = now;
        };
        m.dbg->set_register(m.a, RegId::B, 1);                     once(true);
        m.dbg->poke(m.a, MemSpace::cpu(), 0x9000, 1, &v);          once(true);
        m.dbg->set_mmu_slot(m.a, 6, 0x20);                         once(true);
        m.dbg->nextreg_write(m.a, 0x15, 1);                        once(true);
        m.dbg->port_out(m.a, 0x00FE, 1);                           once(true);
        m.dbg->set_sprite_attr_raw(m.a, 0, attr, 5);               once(true);
        m.dbg->write_pattern_ram(m.a, 0, &v, 1);                   once(true);
        m.dbg->set_register(m.a, RegId::Count, 1);                 once(false);
        m.dbg->set_mmu_slot(m.a, 9, 1);                            once(false);
        m.dbg->poke(m.a, MemSpace::cpu(), 0x0100, 1, &v);          // ROM: refused, no line
        once(false);
        check("TGT-LOC-14", "every mutating verb bumps state_revision() exactly once, a refused one not at all",
              each_once);
    }
    {
        Local m;
        jnext::dbg::LoopDriver d;
        auto boot = [&]() {
            emulator_frontend_cold_boot(m.emu, m.emu.config(), std::string(), ColdBootHooks{});
        };
        d.cold_boot = [&]() { boot(); return true; };
        d.load      = [&](const std::string&) { boot(); return true; };
        m.dbg->set_loop_driver(d);
        m.dbg->reset(m.a, ResetKind::Hard);
        m.emu.mmu().write(0xA000, 0x77);
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0xBEEF; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        m.emu.cpu().set_registers(r);
        uint8_t got = 0;
        m.dbg->peek(MemSpace::cpu(), 0xA000, 1, &got);
        check("TGT-LOC-15", "after reset(Hard) peek, registers and mmu_slots read the rebuilt machine",
              got == 0x77 && m.dbg->registers().PC == 0xBEEF &&
                  m.dbg->mmu_slots()[5].nr_page == m.emu.mmu().get_nr_page(5),
              hex(got) + " " + hex(m.dbg->registers().PC));
    }
}

// ── TGT-01: `Debugger(Target&)` over a FakeTarget ───────────────────────────

struct Remote {
    FakeTarget  t;
    Debugger    d;
    RecListener rec;
    ClientId    a = 0;
    Remote() : d(t) {
        a = d.attach(client("A")).value;
        d.set_listener(a, &rec);
        rec.clear();
        t.clear();
    }
};

static MemSpace alt_rom() {
    MemSpace s;
    s.kind = MemSpace::Kind::AltRom;
    return s;
}

static std::string reg_arg(RegId r, unsigned v) {
    return "(" + std::to_string(static_cast<int>(r)) + "," + std::to_string(v) + ")";
}

static void tgt01_dispatch_rows() {
    {
        FakeTarget t;
        { Debugger d(t); }
        check("TGT-01-01", "constructing and destroying a Debugger over a target makes no machine call",
              t.calls.empty(), std::to_string(t.calls.size()) + " calls");
    }
    {
        Remote m;
        const Z80Registers r = m.d.registers();
        check("TGT-01-02", "registers() is the target's registers, through one registers() call",
              r.PC == 0x4321 && r.AF == 0x1122 && m.t.count("registers") == 1);
    }
    {
        Remote m;
        m.t.regs.AF = 0x00FF;   // so the old A the line logs is the fake's 0x00
        const Result r = m.d.set_register(m.a, RegId::A, 0x12);
        const auto lines = m.rec.mutates();
        check("TGT-01-03", "set_register(A, 0x12) reaches the target as (A, 0x12) and logs one MUTATE "
                           "line naming the client",
              r == Result::Ok && m.t.count(("set_register" + reg_arg(RegId::A, 0x12)).c_str()) == 1 &&
                  lines.size() == 1 && lines[0].find("MUTATE reg A 0x0 -> 0x12 by " + std::to_string(m.a)) == 0,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Remote m;
        m.t.ram[0x9000] = 0xAA; m.t.ram[0x9001] = 0xBB; m.t.ram[0x9002] = 0xCC;
        uint8_t b[3] = {};
        const auto r = m.d.peek(MemSpace::cpu(), 0x9000, 3, b);
        check("TGT-01-04", "peek(Cpu) is forwarded with (Cpu, addr, n); the bytes and the count are the target's",
              r.status == Result::Ok && r.value == 3 && b[0] == 0xAA && b[2] == 0xCC &&
                  m.t.count("read") == 1 && m.t.calls[0].find(",36864,3)") != std::string::npos,
              m.t.calls.empty() ? std::string("no call") : m.t.calls[0]);
    }
    {
        Remote m;
        const uint8_t v = 0x5A;
        const auto r = m.d.poke(m.a, MemSpace::cpu(), 0x9000, 1, &v);
        const auto lines = m.rec.mutates();
        check("TGT-01-05", "poke(Cpu) is forwarded, logs 'mem cpu:0x9000 old -> new' and needs no rewind ring",
              r.status == Result::Ok && r.value == 1 && m.t.count("write") == 1 && m.t.ram[0x9000] == 0x5A &&
                  lines.size() == 1 && lines[0].find("MUTATE mem cpu:0x9000 0x0 -> 0x5A") == 0,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Remote m;
        const uint8_t v = 1;
        const auto a = m.d.poke(m.a, MemSpace::rom(0), 0, 1, &v);
        const auto b = m.d.poke(m.a, MemSpace::page(jnext::dbg::PAGE_SENTINEL_ROM_LO), 0, 1, &v);
        const auto c = m.d.poke(m.a, MemSpace::page(jnext::dbg::PAGE_SENTINEL_ROM_HI), 0, 1, &v);
        check("TGT-01-06", "poke(Rom) and poke(Page 0xFE / 0xFF) are RefusedReadOnly and the target is not called",
              a.status == Result::RefusedReadOnly && b.status == Result::RefusedReadOnly &&
                  c.status == Result::RefusedReadOnly && m.t.calls.empty(), std::to_string(m.t.calls.size()));
    }
    {
        Remote m;
        const uint8_t v = 1;
        uint8_t b = 0;
        const auto w = m.d.poke(m.a, alt_rom(), 0, 1, &v);
        const auto r = m.d.peek(alt_rom(), 0, 1, &b);
        check("TGT-01-07", "an identity-only kind (AltRom) is Unsupported for poke and peek and the target is not called",
              w.status == Result::Unsupported && r.status == Result::Unsupported && m.t.calls.empty());
    }
    {
        Remote m;
        const auto slots = m.d.mmu_slots();
        const Result s = m.d.set_mmu_slot(m.a, 6, 0x30);
        const auto lines = m.rec.mutates();
        const int before = m.t.count("set_mmu_slot");
        const Result bad = m.d.set_mmu_slot(m.a, 8, 0x30);
        check("TGT-01-08", "mmu_slots() and set_mmu_slot() are forwarded, the line is 'mmu slot 6 old -> new'; "
                           "slot 8 is refused before the target",
              slots[3].nr_page == 0x23 && s == Result::Ok && before == 1 && lines.size() == 1 &&
                  lines[0].find("MUTATE mmu slot 6 0x26 -> 0x30") == 0 &&
                  bad == Result::RefusedUnavailable && m.t.count("set_mmu_slot") == 1,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Remote m;
        m.t.nr[0x15] = 0x07;
        const uint8_t v = m.d.nextreg_peek(0x15);
        const Result w = m.d.nextreg_write(m.a, 0x15, 0x01);
        check("TGT-01-09", "nextreg_peek() and nextreg_write() are forwarded",
              v == 0x07 && w == Result::Ok && m.t.nr[0x15] == 0x01 && m.t.count("nextreg_write") == 1);
    }
    {
        Remote m;
        const auto in = m.d.port_in(m.a, 0x00FE);
        const Result o = m.d.port_out(m.a, 0x00FE, 0x05);
        const Result soft = m.d.port_out(m.a, 0x253B, 0x01);   // the local soft-reset delivery guard is not reached
        check("TGT-01-10", "port_in(), port_out() and port_out(0x253B, 1) are forwarded (the delivery guard is local-only)",
              in.status == Result::Ok && in.value == 0xA5 && o == Result::Ok && soft == Result::Ok &&
                  m.t.count("port_in") == 1 && m.t.count("port_out") == 2);
    }
    {
        Remote m;
        const uint8_t attr[5] = {1, 2, 3, 4, 5};
        const uint8_t pat[2] = {9, 8};
        const auto sp = m.d.sprites();
        const auto ar = m.d.sprite_attr_raw(3);
        const Result sa = m.d.set_sprite_attr_raw(m.a, 3, attr, 5);
        const auto pr = m.d.pattern_ram();
        const Result wp = m.d.write_pattern_ram(m.a, 0x10, pat, 2);
        const auto pal = m.d.sprite_palette_rgb333(1, 5);
        const auto clip = m.d.sprite_clip();
        (void)clip;
        check("TGT-01-11", "the seven sprite verbs are forwarded",
              sp.size() == jnext::dbg::SPRITE_COUNT && ar.status == Result::Ok && sa == Result::Ok &&
                  pr.size == jnext::dbg::PATTERN_RAM_BYTES && wp == Result::Ok && pal.value == 0x1FF &&
                  m.t.count("sprites") == 1 && m.t.count("sprite_attr_raw") == 1 &&
                  m.t.count("set_sprite_attr_raw") == 1 && m.t.count("pattern_ram") == 1 &&
                  m.t.count("write_pattern_ram") == 1 && m.t.count("sprite_palette_rgb333") == 1 &&
                  m.t.count("sprite_clip") == 1);
    }
    {
        Remote m;
        m.t.rom = MemSpace::rom(2);
        check("TGT-01-12", "rom_select() is the target's", m.d.rom_select() == MemSpace::rom(2) &&
                                                                 m.t.count("rom_select") == 1);
    }
    {
        Remote m;
        const auto st = m.d.state();
        check("TGT-01-13", "state() is the target's run_state(); a paused target nothing explains reports User with no "
                           "owner (the local fallback); cycle and frame are 0 because a facade over a target has no "
                           "MachineTime",
              st.paused && st.cycle == 0 && st.frame == 0 && st.pc == 0x4321 &&
                  st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
                  st.pause_reason.by == jnext::dbg::CLIENT_NONE);
    }
}

static void tgt01_refusal_rows() {
    using jnext::dbg::Subscription;
    {
        Remote m;
        const Result pr = m.d.pause(m.a);
        const Result rr = m.d.run(m.a);
        const Result r[] = {
            m.d.step_into(m.a), m.d.step_over(m.a), m.d.step_out(m.a),
            m.d.run_to(m.a, 0x8000), m.d.run_to_source_line(m.a, "x.c:1"),
            m.d.source_step(m.a, Debugger::SourceStep::Into), m.d.source_step(m.a, Debugger::SourceStep::Over),
            m.d.source_step(m.a, Debugger::SourceStep::Out)};
        bool all = true;
        for (Result x : r) all = all && x == Result::Unsupported;
        check("TGT-01-14", "pause and run reach the target (one call each); the steps, run_to, run_to_source_line "
                           "and source_step Into/Over/Out are Unsupported; step_instruction is never called",
              pr == Result::Ok && rr == Result::Ok && all && !m.t.called("step_instruction") &&
                  m.t.calls.size() == 2 && m.t.calls[0] == "pause" && m.t.calls[1] == "resume");
    }
    {
        Remote m;
        const Result r[] = {
            m.d.run_to_cycle(m.a, 1), m.d.run_to_frame(m.a, 9), m.d.run_to_end_of_frame(m.a),
            m.d.run_to_end_of_scanline(m.a), m.d.step_back(m.a, 1), m.d.rewind_to_frame(m.a, 0),
            m.d.reset(m.a, ResetKind::Soft), m.d.reset(m.a, ResetKind::Hard), m.d.load(m.a, "x.nex"),
            m.d.set_magic_breakpoint(true), m.d.acknowledge_corruption(0), m.d.set_persistent_breakpoints(true),
            m.d.source_step(m.a, Debugger::SourceStep::Back),
            m.d.source_step(m.a, Debugger::SourceStep::ReverseContinue)};
        bool all = true;
        for (Result x : r) all = all && x == Result::Unsupported;
        check("TGT-01-15", "the local-only control verbs are Unsupported and the target is not called",
              all && m.t.calls.empty());
    }
    {
        Remote m;
        const jnext::dbg::Time tm = m.d.time();
        bool ok = true;
        ok = ok && m.d.time_epoch() == 0 && tm.master_cycle == 0 && m.d.raster().phc == 0;
        ok = ok && m.d.machine().tstates_per_frame == 0 && m.d.copper().pc == 0;
        ok = ok && !m.d.turbosound_enabled() && m.d.audio_mute_mask() == 0 && m.d.audio_channel_mute_mask() == 0;
        ok = ok && m.d.framebuffer().size == 0 && m.d.palette(jnext::dbg::PaletteId::UlaActive).empty() &&
             m.d.palette_priority(jnext::dbg::PaletteId::UlaActive).empty() && m.d.active_ula_palette_bank() == 0;
        ok = ok && m.d.ula_screen_dump().empty() && m.d.input_state().port_1f == 0 && m.d.bookmarks(m.a).empty();
        ok = ok && m.d.call_stack().empty() && !m.d.call_stack_enabled() && !m.d.trace_enabled() &&
             !m.d.coverage_enabled() && !m.d.mem_coverage_enabled() && m.d.paging_ports().port_7ffd == 0 &&
             m.d.nextreg_selected() == 0;
        ok = ok && !m.d.at_frame_boundary() && !m.d.rewind_enabled() && m.d.rewind_range().capacity == 0 &&
             !m.d.armed() && !m.d.persistent_breakpoints() && !m.d.magic_breakpoint() &&
             !m.d.resume_blocked_by_corruption().has_value();
        const auto rb = m.d.rewind_blocked();
        ok = ok && rb.has_value() && *rb == Result::Unsupported;
        ok = ok && m.d.clip_window(jnext::dbg::ClipLayer::Ula).x2 == jnext::dbg::ClipWindow{}.x2 &&
             m.d.ctc_state()[0].time_constant == jnext::dbg::CtcChannelState{}.time_constant &&
             !m.d.dma_state().active && m.d.audio_state().psg_clock_hz == 0 &&
             m.d.mem_coverage_spaces().empty() && m.d.mem_coverage_fetch(0) == 0;
        check("TGT-01-16", "the local-only queries return their empty values and the target is not called",
              ok && m.t.calls.empty(), std::to_string(m.t.calls.size()));
    }
    {
        Remote m;
        bool all = true;
        auto no = [&](Result r) { all = all && r == Result::Unsupported; };
        uint8_t buf[4] = {};
        no(m.d.set_trace_enabled(true)); no(m.d.trace_clear()); no(m.d.trace_resize(8));
        no(m.d.trace_export("/nonexistent/x")); no(m.d.trace_entries().status);
        no(m.d.coverage_enable(true)); no(m.d.coverage_clear()); no(m.d.mem_coverage_enable(true));
        no(m.d.mem_coverage_clear()); no(m.d.mem_coverage(MemSpace::cpu(), 0, 1, buf).status);
        no(m.d.set_audio_mute_mask(m.a, 1)); no(m.d.set_audio_channel_mute_mask(m.a, 1));
        no(m.d.clear_audio_drops(m.a)); no(m.d.ay_registers(0).status);
        no(m.d.set_audio_scope_enabled(m.a, true)); no(m.d.stems_record_start(m.a, "/nonexistent"));
        no(m.d.stems_record_stop(m.a)); no(m.d.audio_scope_samples(4).status);
        no(m.d.set_audio_log_enabled(m.a, true)); no(m.d.audio_log_clear(m.a)); no(m.d.audio_log_resize(8));
        no(m.d.audio_log_entries(0).status); no(m.d.audio_log_export("/nonexistent/x"));
        no(m.d.psg_record_start(m.a, "/nonexistent/x")); no(m.d.psg_record_stop(m.a));
        no(m.d.screenshot(m.a, "/nonexistent/x.png", 0xFF, jnext::dbg::ScreenshotFormat::Png));
        no(m.d.flush_captures(m.a)); no(m.d.save_snapshot(m.a, "/nonexistent/x.sna"));
        no(m.d.press_key(m.a, std::string("a"), 1).status); no(m.d.press_key(m.a, jnext::dbg::MatrixKey{}, 1).status);
        no(m.d.set_key(m.a, 0, 0, true)); no(m.d.set_extended_key(m.a, 0, true));
        no(m.d.set_joystick(m.a, jnext::dbg::JoystickSide::Left, 1)); no(m.d.press_nmi(m.a, jnext::dbg::NmiButton::Mf));
        no(m.d.bookmark_save(m.a, "b", jnext::dbg::SaveStateMode::AdvanceToBoundary));
        no(m.d.bookmark_restore(m.a, "b")); no(m.d.save_state_bytes(m.a, jnext::dbg::SaveStateMode::AdvanceToBoundary).status);
        no(m.d.load_state_bytes(m.a, buf, 4)); no(m.d.set_rewind_enabled(true)); no(m.d.resize_rewind_buffer(4));
        no(m.d.set_call_stack_enabled(m.a, true));
        no(m.d.set_palette(m.a, jnext::dbg::PaletteId::UlaActive, 0, 1)); no(m.d.set_border(m.a, 1));
        uint32_t px[8] = {};
        no(m.d.render_layer(jnext::dbg::Layer::UlaPrimary, 0, px, 8));
        no(m.d.set_loop_driver(jnext::dbg::LoopDriver{})); no(m.d.on_cold_boot_begin()); no(m.d.on_cold_boot_done());
        check("TGT-01-17", "every local-only refusing verb is Unsupported and the target is not called",
              all && m.t.calls.empty(), std::to_string(m.t.calls.size()));
    }
    {
        Remote m;
        bool all = true;
        for (size_t k = 0; k < jnext::dbg::EVENT_KIND_COUNT; ++k) {
            Subscription s;
            s.kind = static_cast<jnext::dbg::EventKind>(k);
            if (s.kind == jnext::dbg::EventKind::Port) s.filter.port_mask = 0x00FF;
            if (s.kind == jnext::dbg::EventKind::Execute) {
                s.filter.lo = s.filter.hi = 0x8000;
                all = all && m.d.subscribe(m.a, s).status == Result::Ok;
            } else if (s.kind == jnext::dbg::EventKind::Mem) {
                s.filter.lo = 0x4000;
                s.filter.hi = 0x40FF;
                all = all && m.d.subscribe(m.a, s).status == Result::Ok;
            } else {
                all = all && m.d.subscribe(m.a, s).status == Result::Unsupported;
            }
        }
        Subscription like;
        const auto g = m.d.subscribe_source_line(m.a, "x.c:1", like);
        check("TGT-01-18", "Execute and Mem subscriptions are accepted (one add_breakpoint, one add_watchpoint at the "
                           "target); every other kind and subscribe_source_line are Unsupported; raise_host_event is "
                           "Ok with nothing delivered",
              all && g.status == Result::Unsupported && m.d.raise_host_event(m.a, "x") == Result::Ok &&
                  m.d.subscriptions(true).size() == 2 && m.t.count("add_breakpoint") == 1 &&
                  m.t.count("add_watchpoint") == 1 && m.t.calls.size() == 2);
    }
}

struct CountService final : jnext::dbg::Service {
    int serviced = 0, flushed = 0;
    jnext::dbg::ServiceStep service_once(int) override {
        ++serviced;
        return jnext::dbg::ServiceStep::Idle;
    }
    void flush_notifications() override { ++flushed; }
    bool peer_connected() const override { return true; }
};

static void tgt01_host_rows() {
    {
        Remote m;
        CountService svc;
        const Result as = m.d.add_service(svc);
        const jnext::dbg::PumpBudget pb;
        const auto hint = m.d.pump(pb);
        m.d.log(m.a, jnext::dbg::LogLevel::Info, "hello");
        bool heard = false;
        for (const auto& l : m.rec.logs) heard = heard || l == "hello [client " + std::to_string(m.a) + "]";
        const bool live = m.d.set_live_raster(m.a, true) == Result::Ok && m.d.live_raster() && m.d.attached();
        const Result det = m.d.detach(m.a);
        check("TGT-01-19", "session verbs work without a machine: attach, set_listener, add_service, pump "
                           "(the service runs, the target is not touched), log, live_raster, attached, detach",
              as == Result::Ok && svc.serviced == 1 && svc.flushed == 1 && hint.remote_attached && hint.paused &&
                  heard && live && det == Result::Ok && !m.d.attached() && m.t.calls.empty(),
              std::to_string(svc.serviced) + "/" + std::to_string(m.t.calls.size()));
    }
    {
        Remote m;
        const std::string dir = jtp::make_temp_dir("tgtmap");
        const std::string map = dir + "/t.map";
        {
            FILE* f = std::fopen(map.c_str(), "w");
            std::fputs("START = $8000\nNEXT = $8003\n", f);
            std::fclose(f);
        }
        const auto n = m.d.load_map(map, jnext::dbg::MapFormat::Simple, nullptr);
        std::filesystem::remove_all(dir);
        const auto name = m.d.lookup(0x8000);
        const auto addr = m.d.lookup_name("NEXT");
        m.d.set_user_label(MemSpace::page(0x24), 0x1000, "USER");
        // LD A,$12 at 0x8000 (3E 12), NOP at 0x8002 (00), read through the target's Cpu reads.
        m.t.ram[0x8000] = 0x3E; m.t.ram[0x8001] = 0x12; m.t.ram[0x8002] = 0x00;
        const auto dis = m.d.disassemble(0x8000, 2, nullptr);
        const int len = m.d.instruction_length(0x8000);
        const bool no_regs = !m.t.called("registers");   // disassembly alone needs no PC
        // More host-side verbs that read the machine only through the facade.
        const bool no_frames = m.d.c_frames().empty() && m.d.source_location() == nullptr &&
                               !m.d.is_call_like(0x8000);
        (void)m.d.load_program_sidecars("/nonexistent/prog.nex");
        check("TGT-01-20", "host-side verbs work: load_map, lookup, lookup_name, set_user_label, and "
                           "disassemble / instruction_length read the target's memory through Cpu reads",
              n.status == Result::Ok && n.value == 2 && name.has_value() && *name == "START" &&
                  addr.has_value() && *addr == 0x8003 && no_frames && !dis.empty() && dis[0].byte_count == 2 && len == 2 &&
                  m.t.count("read") > 0 && no_regs,
              std::to_string(dis.size()) + " lines, " + std::to_string(m.t.count("read")) + " reads");
    }
    {
        Remote m;
        m.t.slots[2].is_rom = true;
        m.t.slots[2].effective_page = 3;
        m.t.slots[2].space = MemSpace::rom(1);
        m.t.slots[3].effective_page = 0x41;
        m.t.slots[3].space = MemSpace::page(0x41);
        check("TGT-01-21", "source_page() and effective_page() come from the target's slots: a RAM slot gives "
                           "Page{nr}, a ROM slot gives Rom{i}",
              m.d.source_page(0x4010) == MemSpace::rom(1) && m.d.effective_page(0x4010) == 3 &&
                  m.d.source_page(0x6010) == MemSpace::page(0x41) && m.d.effective_page(0x6010) == 0x41);
    }
    {
        Remote m;
        m.t.count_extra = 7;
        uint8_t b[4] = {};
        const uint8_t v[4] = {1, 2, 3, 4};
        const auto r = m.d.peek(MemSpace::cpu(), 0x9000, 4, b);
        m.rec.clear();
        const auto w = m.d.poke(m.a, MemSpace::cpu(), 0x9000, 4, v);
        const auto lines = m.rec.mutates();
        check("TGT-01-22", "a target reporting n + 7 bytes is clamped: peek and poke report n, and the MUTATE "
                           "line says '4 bytes'",
              r.value == 4 && w.value == 4 && w.status == Result::Ok && lines.size() == 1 &&
                  lines[0].find(" 4 bytes by ") != std::string::npos && lines[0].find("11") == std::string::npos,
              lines.empty() ? std::string("no line") : lines[0]);
    }
    {
        Remote m;
        m.t.sprite_list.resize(3);
        const size_t few = m.d.sprites().size();
        m.t.sprite_list.resize(200);
        const size_t many = m.d.sprites().size();
        check("TGT-01-23", "sprites() is exactly SPRITE_COUNT whether the target returns 3 or 200",
              few == jnext::dbg::SPRITE_COUNT && many == jnext::dbg::SPRITE_COUNT,
              std::to_string(few) + "/" + std::to_string(many));
    }
    {
        Remote m;
        m.t.pattern.resize(10);
        const bool short_empty = m.d.pattern_ram().empty() && m.d.pattern_ram().data == nullptr;
        m.t.pattern.assign(jnext::dbg::PATTERN_RAM_BYTES, 0);
        m.t.pattern_null = true;
        const bool null_empty = m.d.pattern_ram().empty();
        m.t.pattern_null = false;
        check("TGT-01-24", "pattern_ram() is an empty view for a 10-byte target view and for a null one, "
                           "the whole view otherwise",
              short_empty && null_empty && m.d.pattern_ram().size == jnext::dbg::PATTERN_RAM_BYTES);
    }
}

// ── TGT-01-25..30: a target that fails or answers short ─────────────────────

static void tgt01_failure_rows() {
    {
        Remote m;
        m.t.short_by = 2;
        uint8_t b[6] = {};
        const auto r = m.d.peek(MemSpace::cpu(), 0x9000, 6, b);
        check("TGT-01-25", "a target reading 2 bytes short under Ok: peek reports RefusedUnavailable with the short count",
              r.status == Result::RefusedUnavailable && r.value == 4,
              std::to_string(static_cast<int>(r.status)) + "/" + std::to_string(r.value));
    }
    {
        Remote m;
        m.t.write_status = Result::Unsupported;
        const uint8_t v = 7;
        const uint64_t rev = m.d.state_revision();
        const auto r = m.d.poke(m.a, MemSpace::cpu(), 0x9000, 1, &v);
        check("TGT-01-26", "a target whose write fails outright: poke(Cpu) returns that status, 0 bytes, logs no MUTATE "
                           "line and does not bump state_revision()",
              r.status == Result::Unsupported && r.value == 0 && m.rec.mutates().empty() &&
                  m.d.state_revision() == rev);
    }
    {
        Remote m;
        m.t.write_status = Result::Unsupported;
        const uint8_t v = 7;
        const uint64_t rev = m.d.state_revision();
        const auto r = m.d.poke(m.a, MemSpace::page(10), 0x10, 1, &v);
        check("TGT-01-27", "a target whose write fails outright: poke(Page) passes that status through unchanged, logs "
                           "no MUTATE line and does not bump state_revision()",
              r.status == Result::Unsupported && m.rec.mutates().empty() && m.d.state_revision() == rev);
    }
    {
        Remote m;
        m.t.set_register_status = Result::Unsupported;
        const uint64_t rev = m.d.state_revision();
        const Result r = m.d.set_register(m.a, RegId::A, 1);
        check("TGT-01-28", "a target that refuses set_register: the verb returns that status with no MUTATE line "
                           "and no revision bump",
              r == Result::Unsupported && m.rec.mutates().empty() && m.d.state_revision() == rev);
    }
    {
        Remote m;
        m.t.port_in_status = Result::Unsupported;
        m.t.port_out_status = Result::Unsupported;
        const uint64_t rev = m.d.state_revision();
        const auto in = m.d.port_in(m.a, 0x00FE);
        const Result out = m.d.port_out(m.a, 0x00FE, 1);
        check("TGT-01-29", "a target that fails port_in and port_out: each returns that status, no MUTATE line, "
                           "no revision bump",
              in.status == Result::Unsupported && out == Result::Unsupported && m.rec.mutates().empty() &&
                  m.d.state_revision() == rev);
    }
    {
        Remote m;
        m.t.clip.x1 = 3; m.t.clip.x2 = 4; m.t.clip.y1 = 5; m.t.clip.y2 = 6;
        const auto sp = m.d.clip_window(jnext::dbg::ClipLayer::Sprites);
        const jnext::dbg::ClipWindow none{};
        auto empty = [&](const jnext::dbg::ClipWindow& w) {
            return w.x1 == none.x1 && w.x2 == none.x2 && w.y1 == none.y1 && w.y2 == none.y2;
        };
        check("TGT-01-30", "non-local clip_window(Sprites) is the target's sprite clip; Layer2, Ula and Tilemap are empty",
              sp.x1 == 3 && sp.x2 == 4 && sp.y1 == 5 && sp.y2 == 6 &&
                  empty(m.d.clip_window(jnext::dbg::ClipLayer::Layer2)) &&
                  empty(m.d.clip_window(jnext::dbg::ClipLayer::Ula)) &&
                  empty(m.d.clip_window(jnext::dbg::ClipLayer::Tilemap)));
    }
}

// ── TGT-01-31..34: source maps on a facade with no machine ──────────────────

static const char* const kNextDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

// An SLD of x.asm: records at 0x8000 (line 1) and 0x8003 (line 2) on NR page 36, the
// page the fake maps at slot 4.
static std::string sld_text() {
    return std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice +
           "x.asm|1||0|36|32768|T|\n" + "x.asm|2||0|36|32771|T|\n";
}

static void write_file(const std::string& path, const std::string& text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

static void tgt01_source_rows() {
    {
        Remote m;
        const std::string dir = jtp::make_temp_dir("tgtsld");
        write_file(dir + "/x.sld", sld_text());
        const auto out = m.d.load_source_map(dir + "/x.sld", true, false, false);
        const auto* loc = m.d.source_location(0x8000);
        const auto* loc2 = m.d.source_location(0x8003);
        const bool resolved = loc && loc->line == 1 && loc->file == "x.asm" && loc2 && loc2->line == 2;
        // The same, with the identity check left for the first execution.
        Remote p;
        const auto pend = p.d.load_source_map(dir + "/x.sld", true, true, false);
        std::filesystem::remove_all(dir);
        check("TGT-01-31", "load_source_map of an SLD on a facade with no machine loads its records, and "
                           "source_location() resolves them through the target's slots (also with "
                           "verify_at_first_execution)",
              out.count == 2 && out.error.empty() && resolved && pend.count == 2,
              "count=" + std::to_string(out.count) + " " + out.error);
    }
    {
        Remote m;
        const std::string map = std::string(JNEXT_Z88DK_FIXTURE_DIR) + "/master/sccz80-debug/prog.map";
        const auto out = m.d.load_source_map(map, true, false, false);
        check("TGT-01-32", "load_source_map of a z88dk map on a facade with no machine loads (its space closure "
                           "reads through the target)",
              out.count > 0 && out.error.empty(), "count=" + std::to_string(out.count) + " " + out.error);
    }
    {
        Remote m;
        const std::string dir = jtp::make_temp_dir("tgtsld");
        write_file(dir + "/x.sld", sld_text());
        (void)m.d.load_source_map(dir + "/x.sld", true, false, false);
        const Result r = m.d.clear_source_map();
        std::filesystem::remove_all(dir);
        check("TGT-01-33", "clear_source_map empties the source map of a facade with no machine",
              r == Result::Ok && m.d.source_map().empty() && m.d.source_location(0x8000) == nullptr);
    }
    {
        Remote m;
        const std::string dir = jtp::make_temp_dir("tgtsld");
        write_file(dir + "/prog.nex", "x");
        write_file(dir + "/prog.sld", sld_text());
        const auto out = m.d.load_program_sidecars(dir + "/prog.nex");
        const auto* loc = m.d.source_location(0x8000);
        std::filesystem::remove_all(dir);
        check("TGT-01-34", "load_program_sidecars of a program with an SLD beside it attaches the SLD on a facade "
                           "with no machine",
              out.sources == 2 && loc && loc->line == 1, "sources=" + std::to_string(out.sources));
    }
}

// ── TGT-02 / TGT-03 / TGT-04: the capability model ──────────────────────────

static CapabilitySet all_but(std::initializer_list<jnext::dbg::Capability> lack) {
    CapabilitySet s = CapabilitySet::all();
    for (auto c : lack) s.set(c, false);
    return s;
}

static CapabilitySet served_set() {
    using jnext::dbg::Capability;
    CapabilitySet s;
    for (auto c : {Capability::Run, Capability::Pause, Capability::ExecuteBreakpoints,
                   Capability::Watchpoints, Capability::CommandsWhileRunning,
                   Capability::RegisterWrite, Capability::Memory, Capability::MemoryWhileRunning,
                   Capability::PhysicalMemory, Capability::MmuSlots, Capability::NextRegs,
                   Capability::Ports, Capability::SpriteData, Capability::SpritePaletteClip})
        s.set(c);
    return s;
}

// The served verbs, each with the reason it is refused when the one capability
// it needs is missing; every row below drives the same list.
struct ServedCalls {
    Result set_register, poke_cpu, poke_page, set_mmu_slot, nextreg_write, port_in, port_out, set_attr,
        write_pattern;
    Result peek_cpu, peek_page, peek_rom, peek_divmmc, attr_raw, palette;
};

static ServedCalls run_served(Remote& m) {
    ServedCalls r{};
    uint8_t b[4] = {};
    const uint8_t v[5] = {1, 2, 3, 4, 5};
    r.set_register   = m.d.set_register(m.a, RegId::A, 1);
    r.poke_cpu       = m.d.poke(m.a, MemSpace::cpu(), 0x9000, 1, v).status;
    r.poke_page      = m.d.poke(m.a, MemSpace::page(10), 0x10, 1, v).status;
    r.set_mmu_slot   = m.d.set_mmu_slot(m.a, 6, 0x30);
    r.nextreg_write  = m.d.nextreg_write(m.a, 0x15, 1);
    r.port_in        = m.d.port_in(m.a, 0x00FE).status;
    r.port_out       = m.d.port_out(m.a, 0x00FE, 1);
    r.set_attr       = m.d.set_sprite_attr_raw(m.a, 3, v, 5);
    r.write_pattern  = m.d.write_pattern_ram(m.a, 0, v, 2);
    r.peek_cpu       = m.d.peek(MemSpace::cpu(), 0x9000, 2, b).status;
    r.peek_page      = m.d.peek(MemSpace::page(10), 0x10, 2, b).status;
    r.peek_rom       = m.d.peek(MemSpace::rom(1), 0x10, 2, b).status;
    MemSpace dm;
    dm.kind = MemSpace::Kind::DivMmcRam;
    r.peek_divmmc    = m.d.peek(dm, 0x10, 2, b).status;
    r.attr_raw       = m.d.sprite_attr_raw(3).status;
    r.palette        = m.d.sprite_palette_rgb333(0, 1).status;
    return r;
}

static bool is_ok(Result r) { return r == Result::Ok; }
static bool is_un(Result r) { return r == Result::Unsupported; }

static void tgt02_rows() {
    using jnext::dbg::Capability;
    using jnext::dbg::CAPABILITY_COUNT;
    {
        Local m;
        bool all = m.dbg->capabilities() == CapabilitySet::all() && CAPABILITY_COUNT == 36;
        for (size_t c = 0; c < CAPABILITY_COUNT; ++c)
            all = all && m.dbg->why_unavailable(static_cast<Capability>(c)).empty();
        check("TGT-02-01", "a local facade has all 36 capabilities and why_unavailable() is empty for each", all);
    }
    {
        Remote m;   // the fake claims every capability
        check("TGT-02-02", "a target claiming every capability gets exactly the fourteen the facade serves over a target",
              m.d.capabilities() == served_set() && m.d.capabilities().bits() == served_set().bits());
    }
    {
        Remote m;
        bool ok = true;
        for (size_t c = 0; c < CAPABILITY_COUNT; ++c) {
            const auto cap = static_cast<Capability>(c);
            const bool has = m.d.capabilities().has(cap);
            ok = ok && (has == m.d.why_unavailable(cap).empty());
        }
        check("TGT-02-03", "non-local: why_unavailable(c) is empty exactly for the capabilities the facade has",
              ok);
    }
    {
        Remote m;
        check("TGT-02-04", "non-local why_unavailable(Trace) is the section 11 reason, verbatim",
              m.d.why_unavailable(Capability::Trace) == "trace: local only");
    }
    {
        Remote m;
        m.t.caps = CapabilitySet{};
        const ServedCalls r = run_served(m);
        check("TGT-02-05", "a target with no capability: every served machine verb is Unsupported and the target is not called",
              is_un(r.set_register) && is_un(r.poke_cpu) && is_un(r.poke_page) && is_un(r.set_mmu_slot) &&
                  is_un(r.nextreg_write) && is_un(r.port_in) && is_un(r.port_out) && is_un(r.set_attr) &&
                  is_un(r.write_pattern) && is_un(r.peek_cpu) && is_un(r.peek_page) && is_un(r.peek_rom) &&
                  is_un(r.peek_divmmc) && is_un(r.attr_raw) && is_un(r.palette) && m.t.calls.empty(),
              std::to_string(m.t.calls.size()) + " calls");
    }
    {
        Remote m;
        m.t.caps = CapabilitySet{};
        m.t.validity = RegisterValidity{};
        const auto slots = m.d.mmu_slots();
        const jnext::dbg::SlotInfo none{};
        bool slots_empty = true;
        for (const auto& s : slots) slots_empty = slots_empty && s.nr_page == none.nr_page && s.space == none.space;
        const Z80Registers z = m.d.registers();
        check("TGT-02-06", "a target with no capability and no valid register: the direct queries return their empty values "
                           "and the target is not called",
              z.PC == 0 && z.AF == 0 && slots_empty && m.d.rom_select() == MemSpace::cpu() &&
                  m.d.nextreg_peek(0x15) == 0 && m.d.sprites().size() == jnext::dbg::SPRITE_COUNT &&
                  m.d.pattern_ram().empty() && m.d.sprite_clip().x2 == jnext::dbg::ClipWindow{}.x2 &&
                  m.d.source_page(0x8000) == MemSpace::cpu() && m.t.calls.empty(),
              std::to_string(m.t.calls.size()) + " calls");
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::RegisterWrite});
        const ServedCalls r = run_served(m);
        check("TGT-02-07", "lacking only RegisterWrite: set_register is refused, every other served verb is forwarded",
              is_un(r.set_register) && is_ok(r.poke_cpu) && is_ok(r.poke_page) && is_ok(r.set_mmu_slot) &&
                  is_ok(r.nextreg_write) && is_ok(r.port_in) && is_ok(r.peek_cpu) && is_ok(r.peek_page) &&
                  !m.t.called("set_register"));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::Memory});
        const ServedCalls r = run_served(m);
        check("TGT-02-08", "lacking only Memory: peek and poke of the Cpu space are refused; Page and Rom are forwarded",
              is_un(r.poke_cpu) && is_un(r.peek_cpu) && is_ok(r.poke_page) && is_ok(r.peek_page) &&
                  is_ok(r.peek_rom) && is_ok(r.set_register));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::PhysicalMemory});
        const ServedCalls r = run_served(m);
        m.t.rom = MemSpace::rom(2);
        check("TGT-02-09", "lacking only PhysicalMemory: Page, Rom and DivMmcRam reads and Page writes are refused, "
                           "rom_select() is cpu(); the Cpu space is forwarded",
              is_un(r.poke_page) && is_un(r.peek_page) && is_un(r.peek_rom) && is_un(r.peek_divmmc) &&
                  m.d.rom_select() == MemSpace::cpu() && is_ok(r.peek_cpu) && is_ok(r.poke_cpu));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::MmuSlots});
        const ServedCalls r = run_served(m);
        check("TGT-02-10", "lacking only MmuSlots: mmu_slots() is the default, set_mmu_slot is refused and source_page falls back to cpu()",
              is_un(r.set_mmu_slot) && m.d.mmu_slots()[3].nr_page == jnext::dbg::SlotInfo{}.nr_page &&
                  m.d.source_page(0x6000) == MemSpace::cpu() && m.d.effective_page(0x6000) == 0 &&
                  !m.t.called("mmu_slots") &&
                  !m.t.called("set_mmu_slot") && is_ok(r.nextreg_write));
    }
    {
        Remote m;
        m.t.nr[0x15] = 9;
        m.t.caps = all_but({Capability::NextRegs});
        const ServedCalls r = run_served(m);
        check("TGT-02-11", "lacking only NextRegs: nextreg_peek() is 0 without a call and nextreg_write is refused",
              is_un(r.nextreg_write) && m.d.nextreg_peek(0x15) == 0 && !m.t.called("nextreg_read") &&
                  !m.t.called("nextreg_write") && is_ok(r.port_in));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::Ports});
        const ServedCalls r = run_served(m);
        check("TGT-02-12", "lacking only Ports: port_in and port_out are refused",
              is_un(r.port_in) && is_un(r.port_out) && !m.t.called("port_in") && !m.t.called("port_out") &&
                  is_ok(r.nextreg_write));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::SpriteData});
        const ServedCalls r = run_served(m);
        check("TGT-02-13", "lacking only SpriteData: the five sprite-data verbs are refused or empty",
              is_un(r.set_attr) && is_un(r.write_pattern) && is_un(r.attr_raw) && m.d.pattern_ram().empty() &&
                  m.d.sprites().size() == jnext::dbg::SPRITE_COUNT && !m.t.called("sprites") &&
                  !m.t.called("pattern_ram") && is_ok(r.palette));
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::SpritePaletteClip});
        const ServedCalls r = run_served(m);
        check("TGT-02-14", "lacking only SpritePaletteClip: sprite_palette_rgb333 is refused and sprite_clip is the default",
              is_un(r.palette) && m.d.sprite_clip().x2 == jnext::dbg::ClipWindow{}.x2 &&
                  !m.t.called("sprite_clip") && is_ok(r.attr_raw));
    }

    {
        // The refusal order survives a missing capability: read-only refusals before the gate.
        MemSpace alt;
        alt.kind = MemSpace::Kind::AltRom;
        const uint8_t v = 1;
        bool ok = true;
        std::string seen;
        for (int variant = 0; variant < 2; ++variant) {
            Remote m;
            m.t.caps = variant == 0 ? CapabilitySet{} : all_but({Capability::PhysicalMemory});
            const auto rom = m.d.poke(m.a, MemSpace::rom(0), 0, 1, &v).status;
            const auto lo = m.d.poke(m.a, MemSpace::page(jnext::dbg::PAGE_SENTINEL_ROM_LO), 0, 1, &v).status;
            const auto hi = m.d.poke(m.a, MemSpace::page(jnext::dbg::PAGE_SENTINEL_ROM_HI), 0, 1, &v).status;
            const auto id = m.d.poke(m.a, alt, 0, 1, &v).status;
            ok = ok && rom == Result::RefusedReadOnly && lo == Result::RefusedReadOnly &&
                 hi == Result::RefusedReadOnly && id == Result::Unsupported && m.t.calls.empty();
            seen += std::to_string(m.t.calls.size()) + " ";
        }
        check("TGT-02-17", "without a capability (none, or only PhysicalMemory) poke(Rom) and poke(Page 0xFE / 0xFF) are "
                           "RefusedReadOnly and poke(AltRom) is Unsupported, before the gate, with zero target calls",
              ok, seen);
    }
    {
        // The non-local twin of TGT-LOC-07: argument faults are RefusedUnavailable, not Unsupported.
        Remote m;
        m.t.caps = CapabilitySet{};
        uint8_t b[5] = {};
        const uint8_t v[5] = {1, 2, 3, 4, 5};
        const bool ok =
            m.d.poke(m.a, MemSpace::cpu(), 0x9000, 1, nullptr).status == Result::RefusedUnavailable &&
            m.d.peek(MemSpace::cpu(), 0x9000, 1, nullptr).status == Result::RefusedUnavailable &&
            m.d.set_mmu_slot(m.a, 8, 0x30) == Result::RefusedUnavailable &&
            m.d.set_sprite_attr_raw(m.a, 3, v, 4) == Result::RefusedUnavailable &&
            m.d.set_sprite_attr_raw(m.a, static_cast<uint8_t>(jnext::dbg::SPRITE_COUNT), v, 5) ==
                Result::RefusedUnavailable &&
            m.d.sprite_attr_raw(static_cast<uint8_t>(jnext::dbg::SPRITE_COUNT)).status == Result::RefusedUnavailable &&
            m.d.write_pattern_ram(m.a, 0, nullptr, 1) == Result::RefusedUnavailable &&
            m.d.sprite_palette_rgb333(2, 0).status == Result::RefusedUnavailable;
        (void)b;
        check("TGT-02-18", "on a target with no capability, argument faults (null buffers, slot 8, a bad sprite index or "
                           "size, palette bank 2) are RefusedUnavailable, not Unsupported, with zero target calls",
              ok && m.t.calls.empty(), std::to_string(m.t.calls.size()) + " calls");
    }
    {
        Remote m;
        m.t.caps = all_but({Capability::Memory});
        m.t.ram[0x8000] = 0x3E;
        m.t.ram[0x8001] = 0x12;
        const auto dis = m.d.disassemble(0x8000, 2, nullptr);
        const int len = m.d.instruction_length(0x8000);
        const bool call = m.d.is_call_like(0x8000);
        check("TGT-02-19", "lacking only Memory: disassemble, instruction_length and is_call_like make no read call "
                           "(the bytes are 0)",
              !dis.empty() && dis[0].byte_count == 1 && len == 1 && !call && !m.t.called("read"),
              std::to_string(m.t.count("read")) + " reads");
    }
    {
        // D24: a dezogif session with no debuggee. No new capability bit.
        Remote m;
        m.t.caps = all_but({Capability::Run, Capability::InstructionStep, Capability::RunToCursor});
        m.t.why_text = "no program loaded: upload one";
        m.t.validity.set(RegisterValidity::Field::Core, false);
        const ServedCalls r = run_served(m);
        const Z80Registers z = m.d.registers();
        check("TGT-02-15", "a target with no debuggee: Run, InstructionStep and RunToCursor are unavailable with "
                           "'no program loaded: upload one', Core is invalid and registers() is zeros without a call, "
                           "while memory, NextREG, ports and the slots are still served",
              m.d.why_unavailable(Capability::Run) == "no program loaded: upload one" &&
                  m.d.why_unavailable(Capability::InstructionStep) == "no program loaded: upload one" &&
                  m.d.why_unavailable(Capability::RunToCursor) == "no program loaded: upload one" &&
                  !m.d.register_validity().valid(RegisterValidity::Field::Core) &&
                  m.d.register_validity().valid(RegisterValidity::Field::IM) && z.PC == 0 && z.AF == 0 &&
                  CAPABILITY_COUNT == 36 && is_ok(r.peek_cpu) && is_ok(r.set_mmu_slot) && is_ok(r.nextreg_write) &&
                  is_ok(r.poke_cpu) && is_ok(r.port_in) && m.d.capabilities().has(Capability::MmuSlots));
    }
    {
        // A capability the target has but this facade does not serve yet, and a missing one with no reason.
        Remote m;
        m.t.caps = all_but({Capability::Pause});
        check("TGT-02-16", "InstructionStep claimed by the target but not served: 'not served over a remote target by this "
                           "jnext'; Pause missing with no reason from the target: 'not available on this target'",
              !m.d.capabilities().has(Capability::InstructionStep) &&
                  m.d.why_unavailable(Capability::InstructionStep) == "not served over a remote target by this jnext" &&
                  m.d.why_unavailable(Capability::Pause) == "not available on this target");
    }
    {
        Local m;
        const auto ti = m.dbg->target_info();
        check("TGT-03-01", "a local facade's target_info() is {Local, 'jnext', the version, 'local'}",
              ti.kind == jnext::dbg::TargetKind::Local && ti.name == "jnext" && ti.version == JNEXT_VERSION_STRING &&
                  ti.link == "local");
    }
    {
        Remote m;
        m.t.info_v.name = std::string(1 << 20, 'n') + "\nsecond line";
        m.t.info_v.version = "v\x01\x02";
        m.t.info_v.link = "tcp:127.0.0.1:14000";
        const auto ti = m.d.target_info();
        check("TGT-03-02", "a remote target reports Remote, and its identity strings come back verbatim "
                           "(a 1 MB name, an embedded newline, control characters)",
              ti.kind == jnext::dbg::TargetKind::Remote && ti.name == m.t.info_v.name &&
                  ti.version == m.t.info_v.version && ti.link == m.t.info_v.link);
    }
    {
        Local m;
        const auto v = m.dbg->register_validity();
        bool all = true;
        for (unsigned f = 0; f < static_cast<unsigned>(RegisterValidity::Field::Count); ++f)
            all = all && v.valid(static_cast<RegisterValidity::Field>(f));
        check("TGT-04-01", "a local facade's register_validity() has all 8 fields valid",
              all && v.bits == RegisterValidity::all().bits);
    }
    {
        Remote m;
        m.t.validity.bits = 0xFFFFFFFFu;   // a hostile mask with bits above Count
        check("TGT-04-02", "non-local register_validity() is the target's mask with the bits above Count dropped",
              m.d.register_validity().bits == RegisterValidity::all().bits);
        m.t.validity.bits = 0;
        m.t.validity.set(RegisterValidity::Field::Core).set(RegisterValidity::Field::IM);
        const auto v = m.d.register_validity();
        check("TGT-04-04", "non-local register_validity() carries the target's fields one by one (Core and IM only)",
              v.valid(RegisterValidity::Field::Core) && v.valid(RegisterValidity::Field::IM) &&
                  !v.valid(RegisterValidity::Field::IFF1) && !v.valid(RegisterValidity::Field::Memptr));
    }
    {
        Remote m;
        m.t.validity.set(RegisterValidity::Field::Core, false);
        const Z80Registers z = m.d.registers();
        check("TGT-04-03", "a target whose validity lacks Core: registers() is Z80Registers{} and the target's registers() is not called",
              z.PC == 0 && z.AF == 0 && !m.t.called("registers"));
    }
}

// ── TGT-05: run control, breakpoints and watchpoints over a target ──────────
//
// Oracle: the LOCAL facade's documented semantics (debugger_control.cpp, CTL-01/02 and
// SES-01; debugger_events.cpp, the subscription table; Debugger::state(), CTL-13). There
// is no VHDL counterpart.

/// Every push, with the paused ones kept whole.
struct StopListener : jnext::dbg::Listener {
    std::vector<jnext::dbg::PausedInfo> paused;
    int resumed = 0;
    void on_paused(const jnext::dbg::PausedInfo& p) override { paused.push_back(p); }
    void on_resumed(ClientId) override { ++resumed; }
    void on_reset(ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string&) override {}
};

/// A Remote whose listener keeps the stops, primed and running: the next stop is an edge.
struct Running {
    FakeTarget    t;
    Debugger      d;
    StopListener  sl;
    ClientId      a = 0;
    Running() : d(t) {
        a = d.attach(client("A")).value;
        d.set_listener(a, &sl);
        d.pump(jnext::dbg::PumpBudget{});   // primes the edge detector on the paused fake
        d.run(a);
        d.pump(jnext::dbg::PumpBudget{});   // the Resumed edge
        sl.paused.clear();
        sl.resumed = 0;
        t.clear();
    }
    void stop(jnext::dbg::TargetStop::Why why, uint16_t addr, uint16_t page = jnext::dbg::PAGE_ANY,
              const char* text = "") {
        jnext::dbg::TargetStop ts;
        ts.why = why; ts.addr = addr; ts.page = page; ts.text = text;
        t.stops.push_back(ts);
    }
    void pump() { d.pump(jnext::dbg::PumpBudget{}); }
};

static jnext::dbg::Subscription exec_sub(uint16_t addr) {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Execute;
    s.filter.lo = s.filter.hi = addr;
    return s;
}

static jnext::dbg::Subscription mem_sub(uint16_t lo, uint16_t hi, jnext::dbg::Access a) {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Mem;
    s.filter.lo = lo;
    s.filter.hi = hi;
    s.access = a;
    return s;
}

static void tgt05_rows() {
    using jnext::dbg::Access;
    using jnext::dbg::Capability;
    using jnext::dbg::PauseReason;
    using jnext::dbg::TargetStop;
    {
        Remote m;                       // paused fake
        const Result r1 = m.d.run(m.a);
        const int resumed_once = m.t.count("resume");
        const Result r2 = m.d.run(m.a);  // now running: a no-op
        Remote nr;
        nr.t.caps = all_but({Capability::Run});
        const Result r3 = nr.d.run(nr.a);
        check("TGT-05-01", "run(): resume once on a paused target; Ok with no call on a running one; Unsupported "
                           "with no call when the target lacks Run",
              r1 == Result::Ok && resumed_once == 1 && r2 == Result::Ok && m.t.count("resume") == 1 &&
                  r3 == Result::Unsupported && nr.t.calls.empty());
    }
    {
        Remote m;                       // already paused
        const Result r1 = m.d.pause(m.a);
        const auto st = m.d.state();
        Remote np;
        np.t.caps = all_but({Capability::Pause});
        const Result r2 = np.d.pause(np.a);
        check("TGT-05-02", "pause(): reaches the target even when it is already paused; state() is User by the caller; "
                           "Unsupported with no call when the target lacks Pause",
              r1 == Result::Ok && m.t.count("pause") == 1 && st.pause_reason.kind == PauseReason::Kind::User &&
                  st.pause_reason.by == m.a && r2 == Result::Unsupported && np.t.calls.empty());
    }
    {
        Remote m;
        const auto a = m.d.subscribe(m.a, exec_sub(0x8000));
        const auto b = m.d.subscribe(m.a, exec_sub(0x8000));
        const int adds = m.t.count("add_breakpoint");
        m.d.unsubscribe(m.a, a.value);
        const int removes_after_one = m.t.count("remove_breakpoint");
        m.d.unsubscribe(m.a, b.value);
        check("TGT-05-03", "two Execute subscriptions on one address make one target breakpoint; removing one keeps it; "
                           "removing the other removes it by its handle",
              a.status == Result::Ok && b.status == Result::Ok && adds == 1 && removes_after_one == 0 &&
                  m.t.count("remove_breakpoint") == 1 && m.t.calls.back() == "remove_breakpoint(1)");
    }
    {
        Remote m;
        const ClientId b = m.d.attach(client("B")).value;
        const auto sa = m.d.subscribe(m.a, exec_sub(0x8000));
        jnext::dbg::Subscription tr = exec_sub(0x9000);
        tr.transient = true;
        m.d.subscribe(m.a, tr);
        m.d.subscribe(b, exec_sub(0xA000));
        m.t.clear();
        m.d.set_enabled(m.a, sa.value, false);
        const bool off = m.t.count("remove_breakpoint") == 1;
        m.t.clear();
        m.d.set_enabled(m.a, sa.value, true);
        const bool on = m.t.count("add_breakpoint") == 1 && m.t.calls[0] == "add_breakpoint(32768,65535)";
        m.t.clear();
        m.d.set_master_enabled(false);   // everything but the transient goes
        const bool master_off = m.t.count("remove_breakpoint") == 2 && m.t.count("add_breakpoint") == 0;
        m.t.clear();
        m.d.set_master_enabled(true);
        const bool master_on = m.t.count("add_breakpoint") == 2;
        m.t.clear();
        m.d.set_client_enabled(b, false);
        const bool client_off = m.t.count("remove_breakpoint") == 1;
        check("TGT-05-04", "the target mirrors the table: a disabled subscription is removed and re-added on enable; "
                           "master off removes all but the transient; a client switched off loses only its own",
              off && on && master_off && master_on && client_off);
    }
    {
        Remote m;
        jnext::dbg::Subscription range = exec_sub(0x8000);
        range.filter.hi = 0x8010;
        jnext::dbg::Subscription space = exec_sub(0x8000);
        space.filter.space = MemSpace::rom(0);
        jnext::dbg::Subscription pages = mem_sub(0x4000, 0x40FF, Access::ReadWrite);
        pages.filter.pages = {1, 2};
        jnext::dbg::Subscription bad_exec = exec_sub(0x8000);
        bad_exec.filter.page = 0xE0;
        jnext::dbg::Subscription bad_mem = mem_sub(0x4000, 0x40FF, Access::ReadWrite);
        bad_mem.filter.page = 0xFFFE;
        jnext::dbg::Subscription inverted = mem_sub(0x4100, 0x40FF, Access::ReadWrite);
        jnext::dbg::Subscription port;
        port.kind = jnext::dbg::EventKind::Port;
        port.filter.port_mask = 0x00FF;
        jnext::dbg::Subscription frame;
        frame.kind = jnext::dbg::EventKind::Frame;
        const Result got[] = {m.d.subscribe(m.a, range).status, m.d.subscribe(m.a, space).status,
                              m.d.subscribe(m.a, pages).status, m.d.subscribe(m.a, bad_exec).status,
                              m.d.subscribe(m.a, bad_mem).status, m.d.subscribe(m.a, inverted).status,
                              m.d.subscribe(m.a, port).status, m.d.subscribe(m.a, frame).status};
        const Result want[] = {Result::RefusedUnavailable, Result::RefusedUnavailable, Result::RefusedUnavailable,
                               Result::RefusedUnavailable, Result::RefusedUnavailable, Result::RefusedUnavailable,
                               Result::Unsupported, Result::Unsupported};
        bool all = true;
        for (size_t i = 0; i < 8; ++i) all = all && got[i] == want[i];
        check("TGT-05-05", "what a target cannot hold is refused before any target call and leaves the table alone: an "
                           "Execute range or space, Mem pages, page 0xE0, page 0xFFFE, an inverted range; Port and Frame "
                           "are Unsupported",
              all && m.t.calls.empty() && m.d.subscriptions(true).empty());
    }
    {
        Remote m;
        m.t.add_bp_status = Result::RefusedUnavailable;
        const Result r1 = m.d.subscribe(m.a, exec_sub(0x8000)).status;
        m.t.add_bp_status = Result::Unsupported;   // a link that is gone
        const Result r2 = m.d.subscribe(m.a, exec_sub(0x8001)).status;
        m.t.add_bp_status = Result::Ok;
        m.t.add_wp_status = Result::RefusedUnavailable;
        const Result r3 = m.d.subscribe(m.a, mem_sub(0x4000, 0x40FF, Access::Write)).status;
        check("TGT-05-06", "a target that refuses a breakpoint or watchpoint refuses the subscribe with its own status "
                           "(Unsupported for a lost link) and the table never holds the entry",
              r1 == Result::RefusedUnavailable && r2 == Result::Unsupported && r3 == Result::RefusedUnavailable &&
                  m.d.subscriptions(true).empty());
    }
    {
        Running m;
        const auto id = m.d.subscribe(m.a, exec_sub(0x8000));
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.pump();
        const auto st = m.d.state();
        check("TGT-05-07", "a Breakpoint stop on a subscription: one Paused with matched == [id]; state() is "
                           "Breakpoint{id, owner, addr}",
              m.sl.paused.size() == 1 && m.sl.paused[0].matched.size() == 1 &&
                  m.sl.paused[0].matched[0].event_id == id.value && m.sl.paused[0].matched[0].addr == 0x8000 &&
                  st.paused && st.pause_reason.kind == PauseReason::Kind::Breakpoint && st.pause_reason.id == id.value &&
                  st.pause_reason.by == m.a && st.pause_reason.addr == 0x8000 && m.t.count("resume") == 0);
    }
    {
        Running m;
        int evaluated = 0;
        bool answer = false;
        jnext::dbg::Subscription s = exec_sub(0x8000);
        s.condition = [&](const jnext::dbg::Event&, const Debugger&) { ++evaluated; return answer; };
        m.d.subscribe(m.a, s);
        m.t.clear();
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.pump();
        const bool first = evaluated == 1 && m.t.count("resume") == 1 && m.sl.paused.empty();
        answer = true;
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.pump();
        check("TGT-05-08", "a stop whose condition is false is resumed at once with no Paused; a later true one stops",
              first && evaluated == 2 && m.t.count("resume") == 1 && m.sl.paused.size() == 1);
    }
    {
        Running m;
        jnext::dbg::Subscription s = exec_sub(0x8000);
        s.condition = [](const jnext::dbg::Event&, const Debugger&) { return false; };
        m.d.subscribe(m.a, s);
        m.d.pause(m.a);
        m.t.clear();
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.pump();
        const auto st = m.d.state();
        check("TGT-05-09", "a pause() since the last resume wins: an unclaimed stop is not resumed and state() is User "
                           "by the caller",
              m.t.count("resume") == 0 && st.paused && st.pause_reason.kind == PauseReason::Kind::User &&
                  st.pause_reason.by == m.a);
    }
    {
        Running m;
        m.stop(TargetStop::Why::Other, 0x1234, jnext::dbg::PAGE_ANY, "T");
        m.pump();
        const auto st = m.d.state();
        m.t.clear();
        m.d.detach(m.a);
        check("TGT-05-10", "an Other stop is nobody's verb: state() is User with no owner and the remote's text; "
                           "detaching every client does not resume it",
              st.paused && st.pause_reason.kind == PauseReason::Kind::User &&
                  st.pause_reason.by == jnext::dbg::CLIENT_NONE && st.pause_reason.text == "T" &&
                  m.t.count("resume") == 0);
    }
    {
        Running m;
        jnext::dbg::Subscription once = exec_sub(0x8000);
        once.once = true;
        const auto o = m.d.subscribe(m.a, once);
        jnext::dbg::Subscription tr = exec_sub(0x9000);
        tr.transient = true;
        const auto t = m.d.subscribe(m.a, tr);
        m.t.clear();
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.pump();
        // handles: the once breakpoint was added first (1), the transient second (2)
        check("TGT-05-11", "a once subscription is removed from the target after its accepted hit, and a transient at the "
                           "stop",
              o.status == Result::Ok && t.status == Result::Ok && m.t.count("remove_breakpoint") == 2 &&
                  m.t.calls[0] == "remove_breakpoint(1)" && m.t.calls[1] == "remove_breakpoint(2)" &&
                  m.sl.paused.size() == 1);
    }
    {
        Running m;
        const auto w = m.d.subscribe(m.a, mem_sub(0x4000, 0x40FF, Access::Write));
        m.stop(TargetStop::Why::WatchWrite, 0x4010, 5);
        m.pump();
        const auto st = m.d.state();
        const bool hit = m.sl.paused.size() == 1 && m.sl.paused[0].matched.size() == 1 &&
                         st.pause_reason.kind == PauseReason::Kind::Watch && st.pause_reason.id == w.value &&
                         st.pause_reason.addr == 0x4010 && st.pause_reason.access == Access::Write;
        m.d.run(m.a);
        m.t.clear();
        m.stop(TargetStop::Why::WatchRead, 0x4010, 5);
        m.pump();
        check("TGT-05-12", "a watch stop inside a write watch's range is Watch{id, addr, Write}; a read of a write-only "
                           "watch matches nothing and is resumed",
              hit && m.t.count("resume") == 1 && m.sl.paused.size() == 1);
    }
    {
        Remote m;
        m.d.pause(m.a);
        m.t.clear();
        m.d.detach(m.a);
        const bool released = m.t.count("resume") == 1;
        Remote n;
        const ClientId b = n.d.attach(client("B")).value;
        n.d.pause(n.a);
        n.t.clear();
        n.d.detach(n.a);
        const auto st = n.d.state();
        // A target that lacks Run is never resumed by a detach either.
        Remote nr;
        nr.t.caps = all_but({Capability::Run});
        nr.d.pause(nr.a);
        nr.t.clear();
        nr.d.detach(nr.a);
        check("TGT-05-13", "SES-01 over a target: the pausing client's detach resumes when it was the last arming "
                           "client, and hands the pause to the one that remains otherwise; a target without Run is "
                           "not resumed",
              released && n.t.count("resume") == 0 && st.paused && st.pause_reason.by == b &&
                  nr.t.count("resume") == 0);
    }
    {
        Running m;
        jnext::dbg::Subscription never = exec_sub(0x8000);
        never.condition = [](const jnext::dbg::Event&, const Debugger&) { return false; };
        m.d.subscribe(m.a, never);
        const auto real = m.d.subscribe(m.a, exec_sub(0x9000));
        m.t.clear();
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.stop(TargetStop::Why::Breakpoint, 0x9000);
        m.pump();
        const bool one = m.sl.paused.size() == 1 && m.sl.paused[0].matched.size() == 1 &&
                         m.sl.paused[0].matched[0].event_id == real.value && m.t.count("resume") == 1 &&
                         m.sl.resumed == 0;
        // Still stopped at the end of the pump before, a resumed-at-once stop and a real one in the next:
        // the machine moved in between, so that is a second Paused, and only the resume count shows it.
        m.stop(TargetStop::Why::Breakpoint, 0x8000);
        m.stop(TargetStop::Why::Breakpoint, 0x9000);
        m.pump();
        check("TGT-05-14", "a stop resumed at once and a real stop in one pump give exactly one Paused (and no Resumed "
                           "for the one nobody saw); the same pair again while still stopped gives a second",
              one && m.sl.paused.size() == 2 && m.t.count("resume") == 2 && m.sl.resumed == 0);
    }
    {
        Local m;
        jnext::dbg::LocalTarget lt(m.emu);
        jnext::dbg::TargetStop ts;
        const auto bp = lt.add_breakpoint(0x8000, jnext::dbg::PAGE_ANY);
        const auto wp = lt.add_watchpoint(0x4000, 0x40FF, jnext::dbg::PAGE_ANY, Access::Write);
        check("TGT-05-15", "a LocalTarget answers the seven TGT-05 primitives Unsupported or false: the local facade has "
                           "its own stop machinery",
              lt.resume() == Result::Unsupported && lt.pause() == Result::Unsupported && !lt.take_stop(ts) &&
                  bp.status == Result::Unsupported && wp.status == Result::Unsupported &&
                  lt.remove_breakpoint(1) == Result::Unsupported && lt.remove_watchpoint(1) == Result::Unsupported);
    }
    {
        Remote m;
        const auto w = m.d.subscribe(m.a, mem_sub(0x0000, 0xFFFF, Access::ReadWrite));
        const bool two = m.t.count("add_watchpoint") == 2 && m.t.calls[0] == "add_watchpoint(0,32767,65535,3)" &&
                         m.t.calls[1] == "add_watchpoint(32768,65535,65535,3)";
        m.t.clear();
        m.d.unsubscribe(m.a, w.value);
        check("TGT-05-16", "a full-range Mem watch is two target watches (0-0x7FFF, 0x8000-0xFFFF), both removed when "
                           "it goes",
              w.status == Result::Ok && two && m.t.count("remove_watchpoint") == 2);
    }
}

int main() {
    loc_poke_rows();
    loc_refusal_rows();
    loc_mutate_text_rows();
    loc_state_rows();
    tgt01_dispatch_rows();
    tgt01_refusal_rows();
    tgt01_host_rows();
    tgt01_failure_rows();
    tgt01_source_rows();
    tgt02_rows();
    tgt05_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
