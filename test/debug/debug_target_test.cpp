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
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/rewind_buffer.h"
#include "memory/mmu.h"
#include "platform/emulator_boot.h"
#include "port/nextreg.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
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
        check("TGT-LOC-13", "state().step_mode: None after a completed step_into, RunToCycle while a "
                            "run_to_cycle is armed",
              after_into == jnext::dbg::StepMode::None && armed == jnext::dbg::StepMode::RunToCycle,
              std::to_string(static_cast<int>(after_into)) + "/" +
                  std::to_string(static_cast<int>(armed)));
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

int main() {
    loc_poke_rows();
    loc_refusal_rows();
    loc_mutate_text_rows();
    loc_state_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
