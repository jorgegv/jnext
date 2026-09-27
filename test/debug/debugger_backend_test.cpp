// jnext::dbg::Debugger — the backend suite, work packages B1 and B2 of epic #276.
//
// WHAT THIS SUITE IS, AND WHAT IT IS NOT. §7 of
// doc/design/debug-subsystem/backend.md gives `debugger_backend_test` to work
// package B5: one wiring row per verb AND per event kind, delivery rows,
// multi-client rows, adapter rows. This file is the B1 instalment of it — the
// rows that pin what B1 actually built, so the package does not land unproven:
//
//   CTL-*     one wiring row per control verb B1 implements: arm it THROUGH THE
//             FACADE, run, and assert the machine stopped where the verb
//             promises (PC, cycle, pause_reason). Plus the control rows §7
//             insists on — the same program run WITHOUT the verb, which must
//             sail straight past.
//   F1-*      `peek()` does not perturb: a sweep in +3 mode leaves
//             `p3_floating_bus_dat_` unchanged, AND the same sweep through
//             `read()` does change it (the pair is the point — a row that only
//             checks peek passes on a machine where nothing latches at all).
//   F2-*      `time().frame` advances with NO rewind buffer, and reports the
//             pre-increment tag.
//   INS-*     the inspection and mutation verbs, including the three-page
//             agreement rows that guard `Mmu::nr_page_ptr()` against drifting
//             from `rebuild_ptr()`.
//   PEND-*    the refusal of a verb a later sub-package owns, so "not
//             implemented" is a tested answer rather than a link error.
//
// ── AND THE B2 INSTALMENT (§4.3 CAP-EVT) ────────────────────────────────────
//
//   EVT-TBL-*  the `EventTable` DIRECTLY, as a value: ids, the live formula, the
//              §6 slot masks, `filter_matches` arm by arm for all fourteen
//              kinds, the 512-entry ring and its overflow contract, the INS-17
//              delivery history. Pure logic, tested pure — driving a whole
//              emulator to reach one `if` is how arms end up with no row.
//   EVT-BP-*   `BreakpointSet`'s half of the §6 mask, and the OR with the event
//              half that makes the hot path read exactly one byte.
//   EVT-MEM/PORT/NR/EXEC/TIME/RESET/INT/NMI/MAGIC/HOST/COP/DMA-*
//              the SITES and the DELIVERY through a running machine: the latch
//              really fires from inside an instruction, the drain really
//              delivers at the boundary, the payload really carries what the
//              site saw. Each has its negative control next to it.
//   EVT-OVF-*  the overflow contract, driven on purpose through the ring's test
//              hook with a Copper MOVE burst — §4.3 is explicit that this is
//              "a specified, tested behaviour, not 'unreachable'".
//   EVT-MUT-*  §4.2a: a debugger write fires no event ON ITSELF, for NextREG and
//              for `poke(Cpu)` alike.
//   EVT-TRANS-* the transient successor of `BreakpointSet`'s single one-shot.
//   EVT-GATE/SLOT-* the no-subscriber cost claim and `on_slot_remapped`, as
//              mechanism rows beside the `make bench` measurement.
//
// No ROM, no SD image: a 48K or +3 machine with a program written straight into
// RAM and PC/SP set by hand — the `step_out_test` / `rewind_test` idiom.
//
// Run: ./build/test/debugger_backend_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "peripheral/copper.h"
#include "peripheral/nmi_source.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include "debug/rewind_buffer.h"
#include "input/keyboard.h"
#include "memory/mmu.h"
#include "peripheral/dma.h"
#include "video/palette.h"
#include "video/sprites.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstring>
#include <memory>
#include <fstream>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::Result;

// GH #276 B2 (§4.3 CAP-EVT)
using jnext::dbg::Access;
using jnext::dbg::Action;
// NOT `using jnext::dbg::Event` — `core/emulator.h` already declares a global
// `Event` (the scheduler's), and the two would conflict. Aliased instead.
using DbgEvent = jnext::dbg::Event;
using jnext::dbg::EventId;
using jnext::dbg::EventKind;
using jnext::dbg::EventSource;
using jnext::dbg::EventTable;
using jnext::dbg::NmiButton;
using jnext::dbg::ResetKind;
using jnext::dbg::Subscription;

// ── Tiny test harness (matches test/debug/step_out_test.cpp style) ──────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04X", v);
    return b;
}

// ── The test machine ───────────────────────────────────────────────────────

static constexpr uint16_t PROG    = 0x8000;   // caller
static constexpr uint16_t SUB     = 0x9000;   // subroutine
static constexpr uint16_t TEST_SP = 0xFF00;

// The one program every control row runs:
//
//   8000  00           NOP
//   8001  00           NOP
//   8002  CD 00 90     CALL SUB
//   8005  00           NOP
//   8006  18 FE        JR $        <- the landing marker; the program parks here
//   9000  00 00 C9     NOP NOP RET
//
// A Step Over at 0x8002 must stop at 0x8005; a Step Out armed inside SUB must
// stop at 0x8005 too; a Run to 0x8005 must stop there. A free run must park at
// 0x8006 having passed all three, which is what the control rows assert.
static constexpr uint16_t AFTER_CALL = 0x8005;
static constexpr uint16_t PARK       = 0x8006;

static void build(Emulator& emu, MachineType type = MachineType::ZX48K) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);

    const uint8_t main_prog[] = { 0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE };
    for (size_t i = 0; i < sizeof(main_prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), main_prog[i]);
    const uint8_t sub[] = { 0x00, 0x00, 0xC9 };
    for (size_t i = 0; i < sizeof(sub); ++i)
        emu.mmu().write(static_cast<uint16_t>(SUB + i), sub[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt while stepping
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// The debugger has to be "driving" for the step machinery to be live — that is
// `DebugState::active()`, which B3 turns into the client count.
static void attach_and_pause(Emulator& emu) {
    emu.debug_state().set_active(true);
    emu.debug_state().pause();
}

static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc_of(Emulator& emu) { return emu.cpu().get_registers().PC; }

// ── INS-01's 37-register table, one row each ────────────────────────────────
//
// The B1 review found four surviving mutations in this one function, all of the
// same shape: `RegId::F` writing AF's HIGH half, `RegId::A` writing the low
// half, `RegId::IX` writing `r.IY`, and `RegId::DE` dropping its write
// altogether — each shipping a fully green suite, because eight rows on ONE
// register pair cannot see the other 33 arms. So every arm gets a row, and each
// row asserts the WHOLE change set: what changed AND that nothing else did,
// which is what catches a write that lands in the sibling half or the wrong
// pair.
//
// The comparison is per 8-BIT FIELD, not per pair: `set_register(RegId::B)`
// must report `B` alone, so writing the low half instead names itself.

struct RegFields {
    // Every field of Z80Registers, decomposed. `MEMPTR`, `Q` and `IncDecZ` are
    // included deliberately — they are not `RegId`s, so a write that reached one
    // would otherwise be invisible.
    const char* name;
    uint16_t    value;
};

static std::vector<RegFields> reg_fields(const Z80Registers& r) {
    auto hi = [](uint16_t v) { return static_cast<uint16_t>(v >> 8); };
    auto lo = [](uint16_t v) { return static_cast<uint16_t>(v & 0xFF); };
    return {
        {"A", hi(r.AF)},   {"F", lo(r.AF)},
        {"B", hi(r.BC)},   {"C", lo(r.BC)},
        {"D", hi(r.DE)},   {"E", lo(r.DE)},
        {"H", hi(r.HL)},   {"L", lo(r.HL)},
        {"A'", hi(r.AF2)}, {"F'", lo(r.AF2)},
        {"B'", hi(r.BC2)}, {"C'", lo(r.BC2)},
        {"D'", hi(r.DE2)}, {"E'", lo(r.DE2)},
        {"H'", hi(r.HL2)}, {"L'", lo(r.HL2)},
        {"IXH", hi(r.IX)}, {"IXL", lo(r.IX)},
        {"IYH", hi(r.IY)}, {"IYL", lo(r.IY)},
        {"SPH", hi(r.SP)}, {"SPL", lo(r.SP)},
        {"PCH", hi(r.PC)}, {"PCL", lo(r.PC)},
        {"I", r.I}, {"R", r.R},
        {"IFF1", r.IFF1}, {"IFF2", r.IFF2}, {"IM", r.IM},
        {"halted", static_cast<uint16_t>(r.halted ? 1 : 0)},
        {"MEMPTR", r.MEMPTR}, {"Q", r.Q}, {"IncDecZ", r.IncDecZ},
    };
}

/// Space-separated names of the 8-bit fields that differ, in declaration order.
static std::string reg_delta(const Z80Registers& a, const Z80Registers& b) {
    const auto fa = reg_fields(a);
    const auto fb = reg_fields(b);
    std::string out;
    for (size_t i = 0; i < fa.size(); ++i) {
        if (fa[i].value == fb[i].value) continue;
        if (!out.empty()) out += " ";
        out += fa[i].name;
    }
    return out;
}

/// Zero every register, then write `value` to `reg`, and return what changed.
/// The baseline is READ BACK after zeroing rather than assumed, so whatever the
/// CPU core does with the three non-RegId fields is part of the baseline and
/// cannot be mistaken for the write's own effect.
static std::string write_one_register(Emulator& emu, Debugger& dbg,
                                     RegId reg, uint16_t value) {
    Z80Registers zero{};
    emu.cpu().set_registers(zero);
    const Z80Registers before = emu.cpu().get_registers();
    if (dbg.set_register(1, reg, value) != Result::Ok) return "REFUSED";
    return reg_delta(before, emu.cpu().get_registers());
}

// ── GH #276 B2 helpers ─────────────────────────────────────────────────────

/// Every delivery a subscription saw, in order. The handler captures it by
/// reference, so a `Rec` must outlive the subscription that records into it —
/// which is why every B2 block below is scoped.
struct Rec {
    std::vector<DbgEvent> evs;
};

static jnext::dbg::Handler recorder(Rec& r) {
    return [&r](const DbgEvent& ev, Debugger&) {
        r.evs.push_back(ev);
        return Action::Continue;
    };
}

/// A machine with `bytes` at PROG, PC there, interrupts off, and the backend
/// ARMED — `DebugState::active()`, which is what `armed()` reads and therefore
/// the gate on every event site (§4.1). Without it the whole pipeline is inert,
/// which is a property EVT-GATE-10..12 assert deliberately.
static void build_armed(Emulator& emu, const std::vector<uint8_t>& bytes,
                        MachineType type = MachineType::ZX48K) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);
    for (size_t i = 0; i < bytes.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), bytes[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = PROG;
    r.SP = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
    emu.debug_state().set_active(true);
}

// The Copper is programmed the way the machine programs it: through NR
// 0x61/0x62/0x63, which Emulator::init() wires to the engine. Same word
// encoding as test/copper/copper_test.cpp.
static constexpr uint16_t HALT_WORD = 0x8000u | 0x1FFu;   // WAIT with vpos 511

static uint16_t move_word(uint8_t reg, uint8_t val) {
    return static_cast<uint16_t>(((reg & 0x7F) << 8) | val);
}
static uint16_t wait_word(int hpos, int vpos) {
    return static_cast<uint16_t>(0x8000u | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
}

static void copper_set_ptr(Emulator& emu, uint16_t byte_addr) {
    const uint8_t mode_hi = static_cast<uint8_t>(emu.copper().read_reg_0x62() & 0xC0);
    emu.nextreg().write(0x61, static_cast<uint8_t>(byte_addr & 0xFF));
    emu.nextreg().write(0x62, static_cast<uint8_t>(mode_hi | ((byte_addr >> 8) & 0x07)));
}

static void copper_program(Emulator& emu, const std::vector<uint16_t>& words) {
    copper_set_ptr(emu, 0);
    for (uint16_t w : words) {
        emu.nextreg().write(0x63, static_cast<uint8_t>(w >> 8));
        emu.nextreg().write(0x63, static_cast<uint8_t>(w & 0xFF));
    }
    copper_set_ptr(emu, 0);
}

static void copper_set_mode(Emulator& emu, uint8_t mode) {
    const uint8_t hi = static_cast<uint8_t>(emu.copper().read_reg_0x62() & 0x07);
    emu.nextreg().write(0x62, static_cast<uint8_t>(((mode & 3) << 6) | hi));
}
static void copper_start(Emulator& emu) { copper_set_mode(emu, 1); }
static void copper_stop(Emulator& emu)  { copper_set_mode(emu, 0); }


int main() {
    std::printf("=== jnext::dbg::Debugger backend tests (GH #276 B1) ===\n\n");

    // =======================================================================
    // CTL-01 / CTL-02 — pause and run
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);

        const Result r = dbg.pause(7);
        emu.run_frame();                      // must execute nothing
        check("CTL-01-01", "pause() stops the machine and a frame executes nothing",
              r == Result::Ok && dbg.state().paused && pc_of(emu) == PROG,
              "PC=" + hex(pc_of(emu)));
        check("CTL-01-02", "the stop is attributed to the client that paused",
              dbg.state().pause_reason.kind == PauseReason::Kind::User &&
              dbg.state().pause_reason.by == 7);
        check("CTL-01-03", "pause() is idempotent",
              dbg.pause(7) == Result::Ok && dbg.state().paused);

        check("CTL-02-01", "run() resumes and the machine advances",
              dbg.run(7) == Result::Ok);
        emu.run_frame();
        check("CTL-02-02", "a resumed machine parks at the JR $ landing marker",
              pc_of(emu) == PARK, "PC=" + hex(pc_of(emu)));
        check("CTL-02-03", "run() on an already-running machine is an Ok no-op (GH #223)",
              dbg.run(7) == Result::Ok && !dbg.state().paused);
    }
    {
        // The other half of GH #223, and the reason the early return is ordered
        // FIRST: a redundant run() must not destroy a pending Run to Here.
        // DebugState::resume() calls clear_oneshot(), so a run() that reached it
        // would silently throw the target away — and the machine would then run
        // past it, which is the defect a bare "returns Ok" row cannot see.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.run_to(3, AFTER_CALL);        // arms a transient Execute, resumes
        // GH #276 B2 changed the MECHANISM this row reads and not the property
        // it pins: `run_to` now arms a TRANSIENT `Execute` SUBSCRIPTION instead
        // of `BreakpointSet`'s single one-shot (§4.3 — unlimited, master-switch
        // exempt, auto-removed at the next stop), so the assertion is that the
        // transient subscription survives, and CTL-02-05 below is unchanged.
        const auto pending = dbg.subscriptions(/*include_transient=*/true);
        bool target_armed = false;
        for (const auto& si : pending)
            if (si.transient && si.kind == jnext::dbg::EventKind::Execute &&
                si.filter.lo == AFTER_CALL && si.filter.hi == AFTER_CALL)
                target_armed = true;
        check("CTL-02-04", "a redundant run() while running keeps the pending run-to target",
              dbg.run(7) == Result::Ok && target_armed &&
              dbg.subscriptions(true).size() == pending.size());
        run_until_paused(emu);
        check("CTL-02-05", "so the machine still stops there",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
    }

    // The CONTROL row §7 asks for: the same program, no verb, runs straight
    // past every address the verbs above stop at.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        check("CTL-00-01", "without any verb the program runs past the CALL and parks",
              !dbg.state().paused && pc_of(emu) == PARK, "PC=" + hex(pc_of(emu)));
    }

    // =======================================================================
    // CTL-03 / CTL-04 / CTL-05 — the three steps
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);

        check("CTL-03-01", "step_into() executes exactly one instruction",
              dbg.step_into(1) == Result::Ok && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
        check("CTL-03-02", "and leaves the machine paused, reason Step",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Step);
        dbg.step_into(1);
        check("CTL-03-03", "a second step_into() lands on the CALL",
              pc_of(emu) == PROG + 2, "PC=" + hex(pc_of(emu)));

        // Now at the CALL: step_over must not enter SUB.
        check("CTL-04-01", "step_over() at a CALL is accepted",
              dbg.step_over(1) == Result::Ok);
        run_until_paused(emu);
        check("CTL-04-02", "step_over() stops after the CALL, not inside it",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-04-03", "the stop reads as a completed step, not as a run-to",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step);
    }
    {
        // step_over on a NON-call behaves as step_into (CTL-04's else branch).
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-04-04", "step_over() on a plain NOP advances one instruction",
              dbg.step_over(1) == Result::Ok && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        // Execute the two NOPs and the CALL, then arm Step Out inside SUB.
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("CTL-05-00", "the CALL landed inside the subroutine",
              pc_of(emu) == SUB, "PC=" + hex(pc_of(emu)));
        attach_and_pause(emu);
        check("CTL-05-01", "step_out() is accepted inside a subroutine",
              dbg.step_out(1) == Result::Ok);
        run_until_paused(emu);
        check("CTL-05-02", "step_out() stops at the caller's next instruction",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
    }

    // =======================================================================
    // CTL-06 / CTL-07 / CTL-08 — the run-to family
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-06-01", "run_to() is accepted", dbg.run_to(3, AFTER_CALL) == Result::Ok);
        run_until_paused(emu);
        check("CTL-06-02", "run_to(addr) stops AT addr",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-06-03", "and the stop names the target it was given",
              dbg.state().pause_reason.kind == PauseReason::Kind::RunTo &&
              dbg.state().pause_reason.addr == AFTER_CALL);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint64_t target = emu.clock().get() + 2000;
        check("CTL-07-01", "run_to_cycle() is accepted",
              dbg.run_to_cycle(3, target) == Result::Ok);
        run_until_paused(emu);
        check("CTL-07-02", "run_to_cycle() stops at or after the target cycle",
              dbg.state().paused && emu.clock().get() >= target,
              std::to_string(emu.clock().get()) + " vs " + std::to_string(target));
        check("CTL-07-03", "and not far past it (within one instruction slot)",
              emu.clock().get() < target + 256);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint64_t frame_start = emu.current_frame_cycle();
        check("CTL-08-01", "run_to_end_of_scanline() is accepted",
              dbg.run_to_end_of_scanline(3) == Result::Ok);
        run_until_paused(emu);
        const uint64_t line = emu.timing().master_cycles_per_line;
        check("CTL-08-02", "it stops within the first line of the frame",
              dbg.state().paused && emu.clock().get() >= frame_start + line,
              std::to_string(emu.clock().get() - frame_start) + " into the frame");
        check("CTL-08-03", "and short of a whole frame",
              emu.clock().get() - frame_start < emu.timing().master_cycles_per_frame);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-08-04", "run_to_end_of_frame() is accepted",
              dbg.run_to_end_of_frame(3) == Result::Ok);
        run_until_paused(emu, 3);
        const uint64_t into = emu.clock().get() - emu.current_frame_cycle();
        const uint64_t last_row_start =
            static_cast<uint64_t>(255 + emu.video_timing().vblank_top()) *
            emu.timing().master_cycles_per_line;
        check("CTL-08-05", "it stops in the last VISIBLE row, not at raw VC 255 (G164v2)",
              dbg.state().paused && into >= last_row_start,
              std::to_string(into) + " vs " + std::to_string(last_row_start));
    }
    {
        // Both end-of-* verbs are frame-boundary-free but paused-only.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-08-06", "run_to_end_of_frame() on a running machine is refused",
              dbg.run_to_end_of_frame(3) == Result::RefusedRunning);
        check("CTL-08-07", "run_to_end_of_scanline() on a running machine is refused",
              dbg.run_to_end_of_scanline(3) == Result::RefusedRunning);
    }

    // =======================================================================
    // CTL-09 / CTL-10 / ST-03 — reverse execution
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("ST-03-01", "with no rewind buffer, rewind_blocked() says unavailable",
              dbg.rewind_blocked().has_value() &&
              *dbg.rewind_blocked() == Result::RefusedUnavailable);
        check("CTL-09-01", "and step_back() refuses with the same code",
              dbg.step_back(1, 1) == Result::RefusedUnavailable);
        check("CTL-10-01", "and rewind_to_frame() too",
              dbg.rewind_to_frame(1, 0) == Result::RefusedUnavailable);
    }
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        Debugger dbg(emu);
        for (int i = 0; i < 4; ++i) emu.run_frame();

        check("ST-03-02", "with frames recorded, rewind_blocked() is empty",
              !dbg.rewind_blocked().has_value());
        const auto rr = dbg.rewind_range();
        check("ST-03-03", "rewind_range() reports a non-empty ring in both domains",
              rr.depth == 4 && rr.newest_frame >= rr.oldest_frame &&
              rr.newest_cycle >= rr.oldest_cycle && rr.snapshot_bytes > 0,
              "depth=" + std::to_string(rr.depth));
        // capacity is the ring's SIZE, not how much of it is used: 8 frames were
        // configured and 4 have run. Reporting depth for both (which B1 did,
        // behind a comment claiming RewindBuffer did not expose the size) makes
        // every "N of M frames" readout say "4 of 4".
        check("ST-03-04", "capacity is the configured ring size, distinct from depth",
              rr.capacity == 8 && rr.depth == 4,
              "capacity=" + std::to_string(rr.capacity) +
                  " depth=" + std::to_string(rr.depth));

        const uint32_t target = rr.oldest_frame + 1;
        check("CTL-10-02", "rewind_to_frame() to a frame in the ring succeeds",
              dbg.rewind_to_frame(1, target) == Result::Ok);
        check("CTL-10-03", "and the machine is back at that frame's tag",
              dbg.time().frame == target, "frame=" + std::to_string(dbg.time().frame));
        check("CTL-10-04", "a frame OUTSIDE the ring is refused as unavailable",
              dbg.rewind_to_frame(1, rr.newest_frame + 100) == Result::RefusedUnavailable);
    }

    // =======================================================================
    // CTL-11 — the corruption gate
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-11-01", "a clean machine reports no corruption incident",
              !dbg.resume_blocked_by_corruption().has_value());
        check("CTL-11-02", "acknowledging a generation that is not live is refused",
              dbg.acknowledge_corruption(999) == Result::RefusedUnavailable);
    }
    {
        // The gate itself, driven by a REAL corruption: a state load of the right
        // LENGTH but the wrong CONTENT fails a subsystem sentinel part-way
        // through and leaves the machine torn (Task 60b). Everything that
        // executes must then refuse until the incident is acknowledged.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto good = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame);
        std::vector<uint8_t> bad = good.value;
        for (size_t i = bad.size() / 2; i < bad.size(); ++i) bad[i] ^= 0xFF;
        check("CTL-11-03", "a corrupt state load is refused as corrupt",
              dbg.load_state_bytes(1, bad.data(), bad.size()) == Result::RefusedCorrupt);
        const auto inc = dbg.resume_blocked_by_corruption();
        check("CTL-11-04", "and it latches an incident naming the subsystem",
              inc.has_value() && !inc->subsystem.empty(),
              inc.has_value() ? inc->subsystem : std::string("(none)"));
        // The machine has to be PAUSED for run() to reach the gate at all: the
        // GH #223 early return is ordered first on purpose, so a redundant run()
        // on a running machine is a no-op rather than a question about nothing.
        // Pinned as its own row below, because it looks like a hole otherwise.
        check("CTL-11-05a", "a redundant run() on a RUNNING corrupt machine is still an Ok no-op",
              dbg.run(1) == Result::Ok);
        dbg.pause(1);
        check("CTL-11-05", "every execute-family verb then refuses with RefusedCorrupt",
              dbg.run(1) == Result::RefusedCorrupt &&
              dbg.step_into(1) == Result::RefusedCorrupt &&
              dbg.step_over(1) == Result::RefusedCorrupt &&
              dbg.step_out(1) == Result::RefusedCorrupt &&
              dbg.run_to(1, PARK) == Result::RefusedCorrupt);
        check("CTL-11-06", "acknowledging THIS incident unblocks them",
              dbg.acknowledge_corruption(inc->generation) == Result::Ok &&
              !dbg.resume_blocked_by_corruption().has_value() &&
              dbg.run(1) == Result::Ok);
    }

    // =======================================================================
    // CTL-12 / CTL-14 / CTL-15 and the armed gate
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-12-01", "reset(Soft) is performed here, synchronously",
              dbg.reset(1, jnext::dbg::ResetKind::Soft) == Result::Ok);
        check("CTL-12-02", "reset(Hard) with no loop driver refuses as unavailable",
              dbg.reset(1, jnext::dbg::ResetKind::Hard) == Result::RefusedUnavailable);
        check("CTL-12-03", "reset(Any) is refused — Any is a filter value, not a reset (F8)",
              dbg.reset(1, jnext::dbg::ResetKind::Any) == Result::Unsupported);
        check("CTL-15-01", "load() with no loop driver refuses as unavailable",
              dbg.load(1, "nope.nex") == Result::RefusedUnavailable);

        check("CTL-14-01", "the magic breakpoint starts disarmed",
              !dbg.magic_breakpoint());
        check("CTL-14-02", "set_magic_breakpoint(true) arms it and reads back",
              dbg.set_magic_breakpoint(true) == Result::Ok && dbg.magic_breakpoint());
        check("CTL-14-03", "and it does NOT arm the step machinery by itself",
              !dbg.armed());
        check("CTL-14-04", "disarming it reads back too",
              dbg.set_magic_breakpoint(false) == Result::Ok && !dbg.magic_breakpoint());

        check("ARM-01", "armed() is false with nothing attached and no persistence",
              !dbg.armed());
        check("ARM-02", "--persistent-breakpoints alone arms it (GH #219)",
              dbg.set_persistent_breakpoints(true) == Result::Ok &&
              dbg.persistent_breakpoints() && dbg.armed());
        dbg.set_persistent_breakpoints(false);
        emu.debug_state().set_active(true);
        check("ARM-03", "an attached frontend arms it too",
              dbg.armed() && dbg.attached());
    }

    // =======================================================================
    // CTL-13 — a stop the backend did not cause reads as Breakpoint
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(AFTER_CALL);
        run_until_paused(emu);
        check("CTL-13-01", "a PC breakpoint stops the machine at its address",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-13-02", "and the stop reads as Breakpoint at that address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == AFTER_CALL);
        check("CTL-13-03", "state() reports the cycle and the frame tag too",
              dbg.state().cycle == emu.clock().get() &&
              dbg.state().frame == dbg.time().frame);
    }
    {
        // A run-to stops at its target OR earlier, on a breakpoint it crossed.
        // The reason must say which: reporting RunTo for a breakpoint stop would
        // tell a frontend to clear a target that is still armed.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        emu.debug_state().breakpoints().add_pc(PROG + 1);
        dbg.run_to(3, PARK);                       // target is PAST the breakpoint
        run_until_paused(emu);
        check("CTL-13-04", "a run-to that stops early lands on the breakpoint",
              dbg.state().paused && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
        check("CTL-13-05", "and reads as Breakpoint, not as the run-to completing",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint);
    }

    // =======================================================================
    // F1 — peek() does not perturb
    //
    // THE PAIR IS THE ROW. A machine where nothing ever latches would pass the
    // peek half on its own, so the read half runs the identical sweep through
    // Mmu::read() and asserts the latch DOES move. +3 mode, because the
    // floating-bus latch is the +3's (`p3_floating_bus_dat_`), and a contended
    // address (0x4000-0x7FFF) so the latch gate is open.
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        // Put a known, DIFFERENT byte in contended screen RAM.
        for (uint16_t a = 0x4000; a < 0x4010; ++a) emu.mmu().write(a, 0xA5);
        emu.mmu().set_p3_floating_bus_dat(0x3C);

        uint8_t buf[16] = {};
        const auto got = dbg.peek(MemSpace::cpu(), 0x4000, sizeof(buf), buf);
        check("F1-01", "a peek() sweep of contended RAM leaves the +3 floating-bus latch alone",
              emu.mmu().p3_floating_bus_dat() == 0x3C,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
        check("F1-02", "and it read the right bytes",
              got.status == Result::Ok && got.value == sizeof(buf) &&
              buf[0] == 0xA5 && buf[15] == 0xA5);

        // The other half: the guest's read DOES move it.
        for (uint16_t a = 0x4000; a < 0x4010; ++a) (void)emu.mmu().read(a);
        check("F1-03", "the same sweep through read() DOES move the latch (so F1-01 is a real gate)",
              emu.mmu().p3_floating_bus_dat() == 0xA5,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
    }
    {
        // And a READ watchpoint is not fired by a peek, whatever scope we are
        // in — the contract is the verb's, not the caller's.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_watchpoint(0x4321, WatchType::READ);
        uint8_t b = 0;
        dbg.peek(MemSpace::cpu(), 0x4321, 1, &b);
        check("F1-04", "peek() of a READ-watched address fires no data breakpoint",
              !emu.debug_state().data_bp_hit());

        // The OTHER half, and the one the InspectionScope inside Mmu::peek is
        // actually for: a B2 handler runs INSIDE GuestExecutionScope, where
        // watchpoints_live() is true, so "the caller is outside execution
        // anyway" does not cover it. Simulated here with the same scope the
        // emulator takes, because B1 has no handler to run one from.
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            uint8_t v = 0;
            dbg.peek(MemSpace::cpu(), 0x4321, 1, &v);
        }
        check("F1-05", "nor when peek() is called from INSIDE guest execution (a handler)",
              !emu.debug_state().data_bp_hit());

        // And the known-bad for it: the guest's own read of the same address,
        // in the same scope, DOES latch — so F1-05 is a gate and not a tautology.
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            (void)emu.mmu().read(0x4321);
        }
        check("F1-06", "while the guest's own read of it does latch (F1-05 is a real gate)",
              emu.debug_state().data_bp_hit());
    }

    // =======================================================================
    // F2 — the frame counter
    // =======================================================================
    {
        Emulator emu; build(emu);          // NO rewind buffer configured
        Debugger dbg(emu);
        check("F2-01", "before the first frame the tag is 0",
              dbg.time().frame == 0, "frame=" + std::to_string(dbg.time().frame));
        emu.run_frame();
        check("F2-02", "after one frame the tag is 0 — the frame just run (pre-increment)",
              dbg.time().frame == 0, "frame=" + std::to_string(dbg.time().frame));
        emu.run_frame();
        emu.run_frame();
        check("F2-03", "the tag advances WITHOUT a rewind buffer",
              dbg.time().frame == 2, "frame=" + std::to_string(dbg.time().frame));
        check("F2-04", "and it is one behind the raw counter",
              emu.frame_num() == dbg.time().frame + 1);
    }
    {
        // The tag is the one the rewind slot carries — the property F2 is for.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        Debugger dbg(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        check("F2-05", "the tag equals the newest rewind slot's frame number",
              emu.rewind_buffer() != nullptr &&
              dbg.time().frame == emu.rewind_buffer()->newest_frame_num(),
              "tag=" + std::to_string(dbg.time().frame));
    }

    // =======================================================================
    // INS-01 — registers
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-01-01", "registers() reads the live file",
              dbg.registers().PC == PROG && dbg.registers().SP == TEST_SP);
        check("INS-01-02", "set_register(BC) writes the pair",
              dbg.set_register(1, RegId::BC, 0x1234) == Result::Ok &&
              dbg.registers().BC == 0x1234);
        check("INS-01-03", "set_register(B) writes only the high half",
              dbg.set_register(1, RegId::B, 0xEE) == Result::Ok &&
              dbg.registers().BC == 0xEE34, hex(dbg.registers().BC));
        check("INS-01-04", "set_register(C) writes only the low half",
              dbg.set_register(1, RegId::C, 0x77) == Result::Ok &&
              dbg.registers().BC == 0xEE77, hex(dbg.registers().BC));
        check("INS-01-05", "set_register(I) leaves R alone (no set-all clobber)",
              dbg.set_register(1, RegId::R, 0x40) == Result::Ok &&
              dbg.set_register(1, RegId::I, 0x3F) == Result::Ok &&
              dbg.registers().I == 0x3F && dbg.registers().R == 0x40);
        check("INS-01-06", "set_register(IM) masks to 0..3",
              dbg.set_register(1, RegId::IM, 0xFF) == Result::Ok &&
              dbg.registers().IM == 3);
        // §4.2a's obligation: PC clears halted.
        Z80Registers r = emu.cpu().get_registers();
        r.halted = true;
        emu.cpu().set_registers(r);
        check("INS-01-07", "setting PC clears halted (§4.2a's obligation on INS-01)",
              dbg.set_register(1, RegId::PC, 0x1000) == Result::Ok &&
              dbg.registers().PC == 0x1000 && !dbg.registers().halted);
        check("INS-01-08", "RegId::Count is not a register",
              dbg.set_register(1, RegId::Count, 0) == Result::Unsupported);
    }

    // One row per RegId (36 writable + RegId::Count, which INS-01-08 covers).
    // The ids are LITERAL STRINGS in the table below, so every row name is
    // greppable in this source exactly as an unrolled call would be.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        struct Row { const char* id; RegId reg; uint16_t value; const char* expect; };
        static const Row rows[] = {
            // 16-bit pairs: 0x1234 differs from the zeroed baseline in BOTH
            // halves, so a write that lands in one half only names itself.
            { "REG-AF-01",   RegId::AF,   0x1234, "A F"       },
            { "REG-BC-01",   RegId::BC,   0x1234, "B C"       },
            { "REG-DE-01",   RegId::DE,   0x1234, "D E"       },
            { "REG-HL-01",   RegId::HL,   0x1234, "H L"       },
            { "REG-AF2-01",  RegId::AF2,  0x1234, "A' F'"     },
            { "REG-BC2-01",  RegId::BC2,  0x1234, "B' C'"     },
            { "REG-DE2-01",  RegId::DE2,  0x1234, "D' E'"     },
            { "REG-HL2-01",  RegId::HL2,  0x1234, "H' L'"     },
            { "REG-IX-01",   RegId::IX,   0x1234, "IXH IXL"   },
            { "REG-IY-01",   RegId::IY,   0x1234, "IYH IYL"   },
            { "REG-SP-01",   RegId::SP,   0x1234, "SPH SPL"   },
            { "REG-PC-01",   RegId::PC,   0x1234, "PCH PCL"   },
            // 8-bit halves: one field each, and the value is deliberately not
            // symmetric (0x56) so a half-swap cannot alias.
            { "REG-A-01",    RegId::A,    0x0056, "A"         },
            { "REG-F-01",    RegId::F,    0x0056, "F"         },
            { "REG-B-01",    RegId::B,    0x0056, "B"         },
            { "REG-C-01",    RegId::C,    0x0056, "C"         },
            { "REG-D-01",    RegId::D,    0x0056, "D"         },
            { "REG-E-01",    RegId::E,    0x0056, "E"         },
            { "REG-H-01",    RegId::H,    0x0056, "H"         },
            { "REG-L-01",    RegId::L,    0x0056, "L"         },
            { "REG-A2-01",   RegId::A2,   0x0056, "A'"        },
            { "REG-F2-01",   RegId::F2,   0x0056, "F'"        },
            { "REG-B2-01",   RegId::B2,   0x0056, "B'"        },
            { "REG-C2-01",   RegId::C2,   0x0056, "C'"        },
            { "REG-D2-01",   RegId::D2,   0x0056, "D'"        },
            { "REG-E2-01",   RegId::E2,   0x0056, "E'"        },
            { "REG-H2-01",   RegId::H2,   0x0056, "H'"        },
            { "REG-L2-01",   RegId::L2,   0x0056, "L'"        },
            { "REG-IXH-01",  RegId::IXH,  0x0056, "IXH"       },
            { "REG-IXL-01",  RegId::IXL,  0x0056, "IXL"       },
            { "REG-IYH-01",  RegId::IYH,  0x0056, "IYH"       },
            { "REG-IYL-01",  RegId::IYL,  0x0056, "IYL"       },
            // the rest
            { "REG-I-01",    RegId::I,    0x0056, "I"         },
            { "REG-R-01",    RegId::R,    0x0056, "R"         },
            { "REG-IFF1-01", RegId::IFF1, 0x0001, "IFF1"      },
            { "REG-IFF2-01", RegId::IFF2, 0x0001, "IFF2"      },
            { "REG-IM-01",   RegId::IM,   0x0002, "IM"        },
        };
        for (const auto& row : rows) {
            const std::string got = write_one_register(emu, dbg, row.reg, row.value);
            check(row.id, "set_register writes exactly this register and nothing else",
                  got == row.expect,
                  "changed [" + got + "], want [" + std::string(row.expect) + "]");
        }
        // The table is COMPLETE, and this is what says so: `REG_ID_COUNT` is
        // `static_cast<size_t>(RegId::Count)`, i.e. the 37 real registers with
        // the sentinel NOT counted, so the table must hold exactly that many.
        // A RegId appended to the enum moves the count and fails here, which is
        // the only mechanism that keeps a 37-arm switch from growing a 38th arm
        // with no row. (This row earned its place immediately: it caught an
        // off-by-one in its own first formulation.)
        check("REG-COUNT-01", "the table covers every RegId, one row each",
              sizeof(rows) / sizeof(rows[0]) == jnext::dbg::REG_ID_COUNT,
              std::to_string(sizeof(rows) / sizeof(rows[0])) + " rows vs " +
                  std::to_string(jnext::dbg::REG_ID_COUNT) + " RegIds");
    }

    // =======================================================================
    // INS-02 — the three memory spaces
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t src[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        check("INS-02-01", "poke(Cpu) writes through the live map",
              dbg.poke(1, MemSpace::cpu(), 0x8100, 4, src).status == Result::Ok);
        uint8_t back[4] = {};
        const auto got = dbg.peek(MemSpace::cpu(), 0x8100, 4, back);
        check("INS-02-02", "and peek(Cpu) reads it back",
              got.status == Result::Ok && got.value == 4 &&
              back[0] == 0xDE && back[3] == 0xEF);
        check("INS-02-03", "poke(Rom) is read-only",
              dbg.poke(1, MemSpace::rom(0), 0, 4, src).status == Result::RefusedReadOnly);
        check("INS-02-04", "peek(Rom) is allowed and reads 16 KB of image",
              dbg.peek(MemSpace::rom(0), 0x3FFF, 1, back).status == Result::Ok);
        check("INS-02-05", "a ROM index past 3 is an invalid page",
              dbg.peek(MemSpace::rom(4), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-06", "the 0xFE ROM sentinel is an invalid page",
              dbg.peek(MemSpace::page(0xFE), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-07", "the 0xFF ROM sentinel too",
              dbg.peek(MemSpace::page(0xFF), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-08", "poking a ROM-sentinel page is read-only, not invalid",
              dbg.poke(1, MemSpace::page(0xFF), 0, 1, src).status == Result::RefusedReadOnly);
        check("INS-02-09", "a Page read that runs off the end of the page is SHORT",
              dbg.peek(MemSpace::page(0x10), 0x1FFE, 8, back).value == 2);

        // The 0xE0..0xFD BAND, which is a different mechanism from the two
        // sentinels and had no row at all: `mmu_A21_A13(8)='1'` ->
        // `sram_pre_active='0'` (zxnext.vhd:3061), the SRAM does not respond, so
        // there is no backing store to hand out. 0xE0 is the first such page and
        // is NOT a sentinel, so it separates `nr_page_ptr()`'s guard from the
        // sentinel test that used to sit in front of it three times over.
        check("INS-02-13", "the first page of the inactive band (0xE0) is an invalid page",
              dbg.peek(MemSpace::page(0xE0), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-14", "and so is the last one below the sentinels (0xFD)",
              dbg.peek(MemSpace::page(0xFD), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-15", "0xDF, one BELOW the band, is a real page and reads",
              dbg.peek(MemSpace::page(0xDF), 0, 1, back).status == Result::Ok);
        // An index outside the 8-bit page-number space at all. Without its own
        // guard the narrowing cast turns 0x100 into page 0 and a nonsense
        // request quietly reads real memory.
        check("INS-02-16", "an index past the 8-bit page space is an invalid page",
              dbg.peek(MemSpace::page(0x100), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-17", "and poking one is refused the same way",
              dbg.poke(1, MemSpace::page(0x100), 0, 1, src).status == Result::InvalidPage);
        check("INS-02-18", "poking the inactive band is InvalidPage, NOT ReadOnly",
              dbg.poke(1, MemSpace::page(0xE0), 0, 1, src).status == Result::InvalidPage);
        check("INS-02-19", "while poking a SENTINEL is ReadOnly, not InvalidPage",
              dbg.poke(1, MemSpace::page(0xFE), 0, 1, src).status == Result::RefusedReadOnly);
    }
    {
        // The drift guard for Mmu::nr_page_ptr(): a byte written through a slot
        // mapped to page P must be visible as page P, for an ordinary page, for a
        // bank-5 page and for a bank-7 page (the two dedicated BRAMs). A Next
        // machine, because the BRAM routing is gated on rom_in_sram_.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        // Spelled out rather than looped: a row ID must be a LITERAL in the
        // source, or the traceability extractor reports rows no reader can find.
        auto page_round_trip = [&](uint8_t page) -> uint8_t {
            emu.mmu().set_page(3, page);                // slot 3 = 0x6000-0x7FFF
            emu.mmu().write(0x6010, 0x5A);
            uint8_t b = 0;
            const auto r = dbg.peek(MemSpace::page(page), 0x0010, 1, &b);
            return r.status == Result::Ok ? b : 0x00;
        };
        check("INS-02-10", "peek(Page) sees a write through a slot — an ordinary SRAM page",
              page_round_trip(0x10) == 0x5A);
        check("INS-02-11", "peek(Page) sees it for a bank-5 VRAM page (dedicated BRAM)",
              page_round_trip(0x0A) == 0x5A);
        check("INS-02-12", "peek(Page) sees it for the bank-7 BRAM page (dedicated BRAM)",
              page_round_trip(0x0E) == 0x5A);
    }

    // =======================================================================
    // INS-03 — slots and paging
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto slots = dbg.mmu_slots();
        check("INS-03-01", "slot 0 of a 48K machine is ROM",
              slots[0].is_rom);
        check("INS-03-02", "a ROM slot's space is a Rom image, never a Page",
              slots[0].space.kind == MemSpace::Kind::Rom);
        check("INS-03-03", "a RAM slot's space is its NR page at offset 0",
              !slots[7].is_rom &&
              slots[7].space.kind == MemSpace::Kind::Page &&
              slots[7].space_offset == 0);
        check("INS-03-04", "set_mmu_slot() repoints a slot and reads back",
              dbg.set_mmu_slot(1, 3, 0x11) == Result::Ok &&
              dbg.mmu_slots()[3].nr_page == 0x11);
        check("INS-03-05", "an out-of-range slot is refused",
              dbg.set_mmu_slot(1, 8, 0) == Result::RefusedUnavailable);
        emu.mmu().map_128k_bank(0x03);
        check("INS-03-06", "paging_ports() reports port 0x7FFD as last written",
              dbg.paging_ports().port_7ffd == 0x03,
              hex(dbg.paging_ports().port_7ffd));
    }

    // =======================================================================
    // INS-04 / INS-05 — NextREG and ports
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        check("INS-04-01", "nextreg_write() runs the register's own handler",
              dbg.nextreg_write(1, 0x15, 0x01) == Result::Ok &&
              dbg.nextreg_peek(0x15) == 0x01);
        emu.nextreg().select(0x07);
        check("INS-04-02", "nextreg_selected() reports the port 0x243B latch",
              dbg.nextreg_selected() == 0x07);
        check("INS-05-01", "port_out() dispatches like a guest OUT (border)",
              dbg.port_out(1, 0x00FE, 0x02) == Result::Ok &&
              emu.ula().get_border() == 0x02);
        check("INS-05-02", "port_in() dispatches and yields a value",
              dbg.port_in(1, 0x00FE).status == Result::Ok);
    }

    // =======================================================================
    // INS-06 / INS-07 / INS-19 — raster, time, machine
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        emu.snapshot_raster();
        const auto ras = dbg.raster();
        check("INS-06-01", "raster() reports counters inside the frame",
              ras.raw_hc >= 0 && ras.raw_hc <= emu.video_timing().hc_max() &&
              ras.raw_vc >= 0 && ras.raw_vc <= emu.video_timing().vc_max());

        const auto t = dbg.time();
        check("INS-07-01", "time() reports the master cycle and the T-state total",
              t.master_cycle == emu.clock().get() &&
              t.tstates_total == emu.monotonic_tstates());
        check("INS-07-02", "cycle_in_frame is the offset into this frame",
              t.cycle_in_frame == emu.clock().get() - emu.current_frame_cycle());

        const auto mi = dbg.machine();
        check("INS-19-01", "machine() reports BOTH clock domains, 8 master cycles per T-state",
              mi.master_cycles_per_line == 8ull * mi.tstates_per_line &&
              mi.master_cycles_per_frame == 8ull * mi.tstates_per_frame,
              std::to_string(mi.master_cycles_per_line) + " vs " +
                  std::to_string(mi.tstates_per_line));
        check("INS-19-02", "and the 48K raster constants of the live timing",
              mi.type == MachineType::ZX48K && mi.lines == 312 &&
              mi.tstates_per_line == 224 && mi.hc_max == emu.video_timing().hc_max());
        check("INS-19-03", "fps is derived from the live frame length, not a nominal 50",
              mi.fps > 40.0 && mi.fps < 70.0, std::to_string(mi.fps));
    }

    // =======================================================================
    // INS-08 — sprites
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const uint8_t attr[5] = { 0x20, 0x30, 0x40, 0x80 | 0x40 | 0x05, 0x00 };
        check("INS-08-01", "set_sprite_attr_raw() writes the five bytes",
              dbg.set_sprite_attr_raw(1, 40, attr, 5) == Result::Ok);
        const auto raw = dbg.sprite_attr_raw(40);
        check("INS-08-02", "and sprite_attr_raw() reads them back",
              raw.status == Result::Ok && raw.value[0] == 0x20 && raw.value[3] == 0xC5);
        check("INS-08-03", "the engine's decode sees the write (not a parallel copy)",
              dbg.sprites().size() == 128 && dbg.sprites()[40].x == 0x20 &&
              dbg.sprites()[40].visible);
        check("INS-08-04", "a wrong byte count is refused",
              dbg.set_sprite_attr_raw(1, 40, attr, 4) == Result::RefusedUnavailable);
        check("INS-08-05", "an out-of-range sprite index is refused",
              dbg.sprite_attr_raw(200).status == Result::RefusedUnavailable);

        const uint8_t pat[3] = { 0x11, 0x22, 0x33 };
        check("INS-08-06", "write_pattern_ram() writes through the engine",
              dbg.write_pattern_ram(1, 0x100, pat, 3) == Result::Ok);
        const auto pr = dbg.pattern_ram();
        check("INS-08-07", "and pattern_ram() is a 16 KB view of the same store",
              pr.size == 16384 && pr.data != nullptr &&
              pr.data[0x100] == 0x11 && pr.data[0x102] == 0x33);
        check("INS-08-08", "sprite_clip() reads the live window",
              dbg.sprite_clip().x2 == emu.sprites().clip_x2());
        check("INS-08-09", "sprite_palette_rgb333() refuses a bank other than 0/1",
              dbg.sprite_palette_rgb333(2, 0).status == Result::RefusedUnavailable);

        // A debugger write must ALSO be recorded in the per-scanline change log,
        // because the renderer rewinds to the frame baseline and replays the log
        // per line: a write that only touched live state would show this frame
        // and vanish from the next re-render of it. Proved by doing what the
        // renderer does — baseline, rewind, replay this line — and asserting the
        // byte survives the round trip.
        emu.sprites().start_frame();                 // baseline = current table
        emu.sprites().set_current_line(40);
        const size_t log_before = emu.sprites().change_log_size();
        dbg.set_sprite_attr_raw(1, 12, attr, 5);
        check("INS-08-10", "set_sprite_attr_raw() records five per-scanline log entries",
              emu.sprites().change_log_size() == log_before + 5,
              std::to_string(emu.sprites().change_log_size() - log_before));
        emu.sprites().rewind_to_baseline();
        emu.sprites().apply_changes_for_line(40);
        check("INS-08-11", "so the write survives the renderer's rewind-and-replay",
              emu.sprites().read_attr_byte(12, 0) == 0x20 &&
              emu.sprites().read_attr_byte(12, 3) == 0xC5,
              hex(emu.sprites().read_attr_byte(12, 0)));

        // The same for pattern RAM, which has its own log and its own cursor.
        emu.sprites().start_frame();
        emu.sprites().set_current_line(41);
        const size_t plog_before = emu.sprites().pattern_change_log_size();
        dbg.write_pattern_ram(1, 0x200, pat, 3);
        check("INS-08-12", "write_pattern_ram() records a per-scanline entry per byte",
              emu.sprites().pattern_change_log_size() == plog_before + 3);
        emu.sprites().rewind_to_baseline();
        emu.sprites().apply_changes_for_line(41);
        check("INS-08-13", "and those bytes survive the rewind-and-replay too",
              emu.sprites().read_pattern_byte(0x200) == 0x11 &&
              emu.sprites().read_pattern_byte(0x202) == 0x33);
    }

    // =======================================================================
    // INS-09 / INS-10 — copper and audio
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const auto cs = dbg.copper();
        check("INS-09-01", "copper() hands out a 1024-word view of the live RAM",
              cs.program.size == 1024 && cs.program.data != nullptr);
        check("INS-09-02", "and the run state agrees with the Copper itself",
              cs.running == emu.copper().is_running() && cs.mode == emu.copper().mode());

        const auto regs = dbg.ay_registers(1);
        check("INS-10-01", "ay_registers() returns 16 registers for a valid chip",
              regs.status == Result::Ok && regs.value.size() == 16);
        check("INS-10-02", "and refuses a chip index past the third",
              dbg.ay_registers(3).status == Result::RefusedUnavailable);
        check("INS-10-03", "the audio mute mask round-trips",
              dbg.set_audio_mute_mask(1, AudioMute::BEEPER) == Result::Ok &&
              dbg.audio_mute_mask() == AudioMute::BEEPER);
        check("INS-10-04", "turbosound_enabled() reads the live signal",
              dbg.turbosound_enabled() == emu.turbosound().enabled());
    }

    // =======================================================================
    // INS-11 — disassembly over peek()
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto lines = dbg.disassemble(PROG, 3, nullptr);
        check("INS-11-01", "disassemble() returns the asked-for number of lines",
              lines.size() == 3);
        check("INS-11-02", "and walks by instruction length (NOP NOP CALL)",
              lines[0].addr == PROG && lines[1].addr == PROG + 1 &&
              lines[2].addr == PROG + 2 && lines[2].byte_count == 3);
        check("INS-11-03", "instruction_length() agrees at the CALL",
              dbg.instruction_length(PROG + 2) == 3);
        check("INS-11-04", "is_call_like() is true at the CALL and false at a NOP",
              dbg.is_call_like(PROG + 2) && !dbg.is_call_like(PROG));
        check("INS-11-05", "memory_reader() reads the live map",
              dbg.memory_reader()(PROG + 2) == 0xCD);
    }

    // =======================================================================
    // INS-12 / INS-13 — call stack and trace
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-12-01", "call-stack tracking is off by default",
              !dbg.call_stack_enabled() && dbg.call_stack().empty());
        check("INS-12-02", "it can be switched on",
              dbg.set_call_stack_enabled(true) == Result::Ok && dbg.call_stack_enabled());

        check("INS-13-01", "trace_entries() refuses while the trace is off",
              !dbg.trace_enabled() &&
              dbg.trace_entries().status == Result::RefusedUnavailable);
        check("INS-13-02", "with the trace on it yields entries once instructions run",
              dbg.set_trace_enabled(true) == Result::Ok && dbg.trace_enabled());
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const auto entries = dbg.trace_entries();
        check("INS-13-03", "and the entries name the PCs that executed",
              entries.status == Result::Ok && entries.value.size() >= 2 &&
              entries.value[0].pc == PROG);
        check("INS-13-04", "trace_clear() empties it",
              dbg.trace_clear() == Result::Ok &&
              dbg.trace_entries().value.empty());
        check("INS-13-05", "trace_resize(0) is refused",
              dbg.trace_resize(0) == Result::RefusedUnavailable);
    }

    // =======================================================================
    // INS-14 / INS-15 — framebuffer, palettes, ULA state, clip windows
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const auto fb = dbg.framebuffer();
        check("INS-14-01", "framebuffer() is a view of the whole 640x256 buffer",
              fb.data != nullptr && fb.size == 640u * 256u,
              std::to_string(fb.size));

        using jnext::dbg::PaletteId;
        check("INS-15-01", "palette() returns all 256 entries of a bank",
              dbg.palette(PaletteId::Layer2First).size() == 256);
        check("INS-15-02", "set_palette() writes one entry and reads back",
              dbg.set_palette(1, PaletteId::Layer2First, 7, 0x1FF) == Result::Ok &&
              dbg.palette(PaletteId::Layer2First)[7] == 0x1FF);
        check("INS-15-03", "it does NOT move the guest's NR 0x40 palette cursor",
              emu.palette().get_index() == 0);
        // Same rewind-and-replay property as the sprite writes above: the
        // renderer replays palette writes per line.
        emu.palette().start_frame();
        emu.palette().set_current_line(60);
        dbg.set_palette(1, PaletteId::Layer2First, 8, 0x0F0);
        emu.palette().rewind_to_baseline();
        emu.palette().apply_changes_for_line(60);
        check("INS-15-03b", "and it IS recorded in the per-scanline palette log",
              dbg.palette(PaletteId::Layer2First)[8] == 0x0F0,
              hex(dbg.palette(PaletteId::Layer2First)[8]));
        // UlaActive resolves through NR 0x43 bit 1, not bit 0.
        dbg.set_palette(1, PaletteId::UlaFirst,  9, 0x0AA);
        dbg.set_palette(1, PaletteId::UlaSecond, 9, 0x155);
        check("INS-15-04", "UlaActive resolves to the first bank by default",
              dbg.palette(PaletteId::UlaActive)[9] == 0x0AA &&
              dbg.active_ula_palette_bank() == 0);
        emu.palette().write_control(0x02);      // NR 0x43 bit 1 = second ULA bank
        check("INS-15-05", "and follows NR 0x43 bit 1 when it flips",
              dbg.palette(PaletteId::UlaActive)[9] == 0x155 &&
              dbg.active_ula_palette_bank() == 1);

        emu.ula().set_border(5);
        const auto us = dbg.ula_screen_regs();
        check("INS-15-06", "ula_screen_regs() reports the border and the active bank",
              us.border == 5 && (us.active_bank == 5 || us.active_bank == 7));
        check("INS-15-07", "set_border() writes it",
              dbg.set_border(1, 3) == Result::Ok && emu.ula().get_border() == 3);

        using jnext::dbg::ClipLayer;
        emu.layer2().set_clip_x2(0x7F);
        check("INS-15-08", "clip_window(Layer2) reads the LIVE layer state",
              dbg.clip_window(ClipLayer::Layer2).x2 == 0x7F);
        check("INS-15-09", "clip_window(Sprites) agrees with sprite_clip()",
              dbg.clip_window(ClipLayer::Sprites).y2 == dbg.sprite_clip().y2);

        check("INS-15-10", "rrrgggbb_to_argb() forwards to the renderer's expansion",
              jnext::dbg::rrrgggbb_to_argb(0xE3) == Renderer::rrrgggbb_to_argb(0xE3) &&
              jnext::dbg::rrrgggbb_to_argb(0x00) == Renderer::rrrgggbb_to_argb(0x00));
    }

    // =======================================================================
    // INS-16 / IN-02 / IN-03 — the input surface, read and injected
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-16-01", "an untouched matrix reads all-released (active-LOW)",
              dbg.input_state().matrix[0] == 0xFF &&
              dbg.input_state().matrix[7] == 0xFF);
        check("IN-02-01", "set_key() presses one matrix position",
              dbg.set_key(1, 7, 0, true) == Result::Ok &&
              dbg.input_state().matrix[7] == 0xFE,
              hex(dbg.input_state().matrix[7]));
        check("IN-02-02", "and releases it",
              dbg.set_key(1, 7, 0, false) == Result::Ok &&
              dbg.input_state().matrix[7] == 0xFF);
        check("IN-02-03", "an out-of-range position is refused, not clamped",
              dbg.set_key(1, 8, 0, true) == Result::RefusedUnavailable &&
              dbg.set_key(1, 0, 5, true) == Result::RefusedUnavailable);
        check("IN-02-04", "set_extended_key() sets the NR 0xB0 readback bit (active-HIGH)",
              dbg.set_extended_key(1, 3, true) == Result::Ok &&
              (dbg.input_state().ext_keys & 0x0008) != 0);
        check("IN-02-05", "an out-of-range extended id is refused",
              dbg.set_extended_key(1, 16, true) == Result::RefusedUnavailable);
        check("IN-03-01", "set_joystick() sets a connector's 12 bits",
              dbg.set_joystick(1, jnext::dbg::JoystickSide::Left, 0x0005) == Result::Ok &&
              dbg.input_state().joy_left12 == 0x0005);
        check("IN-04-01", "press_nmi() is accepted for both buttons",
              dbg.press_nmi(1, jnext::dbg::NmiButton::Mf) == Result::Ok &&
              dbg.press_nmi(1, jnext::dbg::NmiButton::Drive) == Result::Ok);
    }

    // =======================================================================
    // ST-01 / ST-02 — save and load state bytes
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("ST-01-01", "a machine that has run no partial frame is at a boundary",
              dbg.at_frame_boundary());
        const auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame);
        check("ST-01-02", "save_state_bytes() yields a non-empty snapshot",
              saved.status == Result::Ok && saved.value.size() > 1024,
              std::to_string(saved.value.size()) + " bytes");

        dbg.set_register(1, RegId::BC, 0xBEEF);
        check("ST-02-01", "load_state_bytes() restores it",
              dbg.load_state_bytes(1, saved.value.data(), saved.value.size()) == Result::Ok &&
              dbg.registers().BC != 0xBEEF);
        check("ST-02-02", "an empty buffer is refused",
              dbg.load_state_bytes(1, nullptr, 0) == Result::RefusedUnavailable);
    }

    // =======================================================================
    // CAP-SYM — symbols
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("SYM-01", "a missing MAP file is refused, not silently empty",
              dbg.load_map("/nonexistent/none.map",
                           jnext::dbg::MapFormat::Z88dk).status ==
                  Result::RefusedUnavailable);
        check("SYM-02", "an unloaded table looks up nothing",
              !dbg.lookup(0x8000).has_value() && dbg.symbols().empty());
        check("SYM-03", "clear_symbols() is always Ok",
              dbg.clear_symbols() == Result::Ok);
    }

    // =======================================================================
    // PEND — the verbs a later sub-package owns refuse, they do not misbehave
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        // The three PEND-B2 rows that used to sit here asserted the REFUSAL of
        // subscribe()/subscriptions()/probe_execute(). B2 implements all three,
        // so the refusal is gone and the rows moved to the EVT-* section below,
        // which asserts what they now do. What survives here is the pair that is
        // still true of a fresh backend: nothing is subscribed, and nothing has
        // been delivered.
        check("PEND-B2-01", "a fresh backend has no subscriptions and no delivery history",
              dbg.subscriptions(true).empty() && dbg.events_fired_since(0).empty());
        check("PEND-B3-01", "attach() (B3) refuses as unsupported",
              dbg.attach(jnext::dbg::ClientInfo{}).status == Result::Unsupported);
        check("PEND-B3-02", "pump() is inert and reports no remote attached",
              !dbg.pump(jnext::dbg::PumpBudget{}).remote_attached);
        check("PEND-B4-01", "press_key() (B4) refuses as unsupported",
              dbg.press_key(1, std::string("enter"), 2).status == Result::Unsupported);
        check("PEND-B4-02", "coverage is off and reads all-zero",
              !dbg.coverage_enabled() && dbg.coverage().none());
        check("PEND-B4-03", "screenshot() (B4) refuses as unsupported",
              dbg.screenshot(1, "/tmp/x.png", jnext::dbg::LAYER_MASK_ALL,
                             jnext::dbg::ScreenshotFormat::Png) == Result::Unsupported);
        check("PEND-14-01", "render_layer() (unassigned, see the B1 report) refuses",
              dbg.render_layer(jnext::dbg::Layer::Composite, 0, nullptr, 640) ==
                  Result::Unsupported);
        // probe_execute over the LEGACY model only — the subscription half is
        // EVT-PROBE-* below. Kept here because it is the one thing in this block
        // that was already answered for real rather than refused.
        emu.debug_state().breakpoints().add_pc(0x1234);
        check("PEND-B2-02", "probe_execute() answers over today's breakpoint set",
              dbg.probe_execute(0x1234) && !dbg.probe_execute(0x1235));
    }

    // =======================================================================
    // §4.2a — the mutation log exists for every write path
    //
    // The LINE's content is not asserted here (it goes to spdlog, and pinning a
    // log format is B5's business if anyone's). What is asserted is that a
    // refused write changes nothing: the property a MUTATE line would otherwise
    // be claiming falsely.
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t src[2] = { 0x5A, 0xA5 };
        uint8_t before[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, before);      // ROM, on a 48K
        dbg.poke(1, MemSpace::cpu(), 0x0000, 2, src);      // ROM: ignored by Mmu
        uint8_t after[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, after);
        check("MUTLOG-01", "poke(Cpu) into ROM changes nothing (Mmu::write ignores it)",
              before[0] == after[0] && before[1] == after[1]);
        check("MUTLOG-02", "a refused poke(Rom) transfers no bytes",
              dbg.poke(1, MemSpace::rom(0), 0, 2, src).value == 0);
    }
    {
        // §4.2a: the BACKEND emits `MUTATE <what> <old> -> <new> by <client>`
        // for every client's write. Captured off the live logger, because a
        // claim that something is logged is only worth what reading the log
        // proves.
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(16);
        Log::emulator()->sinks().push_back(ring);

        Emulator emu; build(emu);
        Debugger dbg(emu);
        const size_t before = ring->last_formatted().size();
        dbg.set_register(9, RegId::DE, 0x1234);
        const auto lines = ring->last_formatted();
        const std::string last = lines.empty() ? std::string() : lines.back();
        check("MUTLOG-03", "a mutation emits one line, and only one",
              lines.size() == before + 1, std::to_string(lines.size()));
        check("MUTLOG-04", "it carries the target, the old and new values and the client",
              last.find("MUTATE reg DE") != std::string::npos &&
              last.find("-> 0x1234") != std::string::npos &&
              last.find("by 9") != std::string::npos,
              last);

        const size_t after_write = ring->last_formatted().size();
        uint8_t scratch = 0;
        (void)dbg.registers();
        dbg.peek(MemSpace::cpu(), 0x8000, 1, &scratch);
        check("MUTLOG-05", "a READ emits no MUTATE line",
              ring->last_formatted().size() == after_write);

        Log::emulator()->sinks().pop_back();
    }


    // =======================================================================
    // GAPS FOUND BY DERIVING MUTATIONS FROM THE DIFF
    //
    // The B1 review's verdict was that a 27-mutation table built from the ROW
    // LIST reached none of the five behaviours that had no row at all. Every row
    // below exists because a function this branch shipped had no assertion on
    // it: the wrong ROM image, the wrong palette bank, a copper view that is not
    // the live RAM, the two legacy paging ports, five of the six ULA screen
    // registers, two of the four clip windows, the symbol-substituting
    // disassembly, and eight verbs with no row whatsoever.
    // =======================================================================
    {
        // ROM IMAGES — `Mmu::rom_image_ptr()` picks one of four 16 KB images and
        // nothing asserted it picks the RIGHT one. On a NEXT machine an image is
        // SRAM pages `2*index` / `2*index+1` addressed WITHOUT the to_sram_page
        // shift (§4 INS-02), so `Ram::page_ptr()` reaches them directly and a
        // distinct byte per image makes the index observable. Choosing the Next
        // over the +3 also pins that un-shifted addressing, which is the
        // subtlety `Page{}` cannot express.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        for (int img = 0; img < 4; ++img)
            emu.ram().page_ptr(static_cast<uint16_t>(img * 2))[0] =
                static_cast<uint8_t>(0xB0 + img);
        uint8_t r[4] = {};
        bool all_read = true, all_right = true;
        for (int img = 0; img < 4; ++img) {
            all_read = all_read &&
                dbg.peek(MemSpace::rom(static_cast<uint16_t>(img)), 0, 1, &r[img]).status
                    == Result::Ok;
            all_right = all_right && r[img] == static_cast<uint8_t>(0xB0 + img);
        }
        check("INS-02-20", "all four ROM images are readable", all_read);
        check("INS-02-21", "and the index selects the image — four distinct bytes",
              all_right,
              hex(r[0]) + " " + hex(r[1]) + " " + hex(r[2]) + " " + hex(r[3]));
        // A ROM slot's SlotInfo must name the image the slot is actually serving,
        // which is the composition round 4 of the design review caught two
        // frontends getting wrong.
        //
        // PRECONDITION, stated because the row depends on it and it is not
        // self-evident: slot 0 of a freshly-initialised Next IS ROM-mapped, and
        // the bytes written above land in ROM image 0 (SRAM pages 0/1), which is
        // the image slot 0 serves — so the CPU view at 0x0000 and the slot's own
        // `space` + `space_offset` must read the SAME byte. The row asserts
        // `is_rom` first, so if that precondition ever stops holding the row says
        // so instead of comparing two irrelevant reads.
        const auto slots = dbg.mmu_slots();
        check("INS-03-09", "the precondition: slot 0 of a fresh Next is ROM-mapped",
              slots[0].is_rom && slots[0].space.kind == MemSpace::Kind::Rom);
        uint8_t via_slot[4] = {}, via_cpu[4] = {};
        dbg.peek(slots[0].space, slots[0].space_offset, 4, via_slot);
        dbg.peek(MemSpace::cpu(), 0x0000, 4, via_cpu);
        check("INS-03-07", "a ROM slot's space+offset read the same bytes as the CPU view",
              std::memcmp(via_slot, via_cpu, 4) == 0);
    }
    {
        // THE LEGACY PAGING PORTS — only 0x7FFD had a row, so a mutation
        // swapping 1FFD and DFFD survived.
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        dbg.port_out(1, 0x7FFD, 0x05);
        dbg.port_out(1, 0x1FFD, 0x04);
        const auto pp = dbg.paging_ports();
        check("INS-03-08", "paging_ports() reports 0x7FFD and 0x1FFD as last written, not swapped",
              pp.port_7ffd == 0x05 && pp.port_1ffd == 0x04,
              hex(pp.port_7ffd) + "/" + hex(pp.port_1ffd));
    }
    {
        // THE RASTER AND TIME COUNTERS — INS-06-01 only bounded them, so a
        // mutation swapping hc and vc passed (311 <= 447 is true).
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.run_frame();
        emu.snapshot_raster();
        const auto ras = dbg.raster();
        check("INS-06-02", "raster() reports the machine's own paused hc/vc, not swapped",
              ras.raw_hc == emu.paused_hc() && ras.raw_vc == emu.paused_vc(),
              std::to_string(ras.raw_hc) + "/" + std::to_string(ras.raw_vc));
        const auto t = dbg.time();
        check("INS-07-03", "time()'s raw counters are the same pair, in the same order",
              t.hc_raw == emu.paused_hc() && t.vc_raw == emu.paused_vc());

        // MachineInfo's five untested fields.
        const auto mi = dbg.machine();
        const RasterPos origin = emu.video_timing().display_origin();
        check("INS-19-04", "machine() carries the display origin and vblank_top of the live timing",
              mi.display_origin_hc == origin.hc && mi.display_origin_vc == origin.vc &&
              mi.vblank_top == emu.video_timing().vblank_top());
        check("INS-19-05", "and the two blanking limits, not each other",
              mi.max_hblank == emu.video_timing().max_hblank() &&
              mi.max_vblank == emu.video_timing().max_vblank() &&
              mi.max_hblank != mi.max_vblank);
        check("INS-19-06", "cpu_divisor is the live clock's, and NR 0x07 moves it",
              mi.cpu_divisor == emu.clock().cpu_divisor());
    }
    {
        // THE COPPER VIEW — INS-09-01 proved only size and non-null, so a
        // mutation handing out a static buffer survived. Write a real
        // instruction through the NR 0x60/0x61 pair and read it back through the
        // view.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.copper().write_reg_0x61(0x00);          // NR 0x61: write address = 0
        emu.copper().write_reg_0x60(0x12);          // NR 0x60: MSB (even address)
        emu.copper().write_reg_0x60(0x34);          // NR 0x60: LSB commits 0x1234
        const auto cs = dbg.copper();
        check("INS-09-03", "copper()'s program is the LIVE instruction RAM",
              cs.program.data[0] == emu.copper().instruction(0) &&
              cs.program.data[0] == 0x1234,
              hex(cs.program.data[0]));
    }
    {
        // THE PALETTE BANKS — every palette row read a ULA or Layer2 bank, so
        // `entry_rgb333`'s sprite and tilemap arms had no row and a mutation
        // returning the ULA store for them survived.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        using jnext::dbg::PaletteId;
        struct { PaletteId id; uint16_t v; } banks[] = {
            {PaletteId::UlaFirst, 0x001}, {PaletteId::Layer2First, 0x002},
            {PaletteId::SpriteFirst, 0x004}, {PaletteId::TilemapFirst, 0x008},
            {PaletteId::UlaSecond, 0x010}, {PaletteId::Layer2Second, 0x020},
            {PaletteId::SpriteSecond, 0x040}, {PaletteId::TilemapSecond, 0x080},
        };
        for (const auto& b : banks) dbg.set_palette(1, b.id, 33, b.v);
        bool all_distinct = true;
        for (const auto& b : banks)
            all_distinct = all_distinct && dbg.palette(b.id)[33] == b.v;
        check("INS-15-11", "all EIGHT hardware palette banks are separate stores",
              all_distinct);
        check("INS-15-12", "sprite_palette_rgb333() reads the sprite bank it is asked for",
              dbg.sprite_palette_rgb333(0, 33).value == 0x004 &&
              dbg.sprite_palette_rgb333(1, 33).value == 0x040);
    }
    {
        // THE ULA SCREEN REGISTERS — one of six had a row.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.ula().set_ula_scroll_x_coarse(0x21);
        emu.ula().set_ula_scroll_y(0x43);
        dbg.port_out(1, 0x00FF, 0x02);                 // Timex screen-mode latch
        emu.ula().set_ula_enabled(false);
        const auto us = dbg.ula_screen_regs();
        check("INS-15-13", "ula_screen_regs() carries NR 0x26/0x27 the right way round",
              us.scroll_x == 0x21 && us.scroll_y == 0x43,
              hex(us.scroll_x) + "/" + hex(us.scroll_y));
        check("INS-15-14", "and the port 0xFF Timex latch",
              us.port_ff == emu.ula().get_screen_mode_reg() && us.port_ff == 0x02);
        check("INS-15-15", "and the ULA enable, which is NR 0x68 b7 INVERTED",
              !us.enabled);
        emu.ula().set_ula_enabled(true);
        check("INS-15-16", "which flips back",
              dbg.ula_screen_regs().enabled);
        // Through the PORT, not Mmu::set_port_7ffd_bit3(): the Ula holds its own
        // shadow-screen flag and the port handler is what syncs it, so poking the
        // Mmu alone proves nothing about what the ULA displays.
        dbg.port_out(1, 0x7FFD, 0x08);
        check("INS-15-17", "the active ULA bank follows port 0x7FFD bit 3 (5 -> 7)",
              dbg.ula_screen_regs().active_bank == 7 &&
              emu.ula().get_shadow_screen_en());
        emu.palette().write_control(0x02);
        check("INS-15-18", "second_palette agrees with active_ula_palette_bank()",
              dbg.ula_screen_regs().second_palette &&
              dbg.active_ula_palette_bank() == 1);
    }
    {
        // THE FOUR CLIP WINDOWS — Layer2 and Sprites had rows; ULA and Tilemap
        // did not, so a mutation returning the Layer 2 window for all four
        // survived. Four distinct values, so no two arms can alias.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        using jnext::dbg::ClipLayer;
        emu.layer2().set_clip_x1(0x11);
        emu.sprites().set_clip_x1(0x22);
        emu.ula().set_clip_x1(0x33);
        emu.tilemap().set_clip_x1(0x44);
        check("INS-15-19", "each clip window comes from its OWN layer",
              dbg.clip_window(ClipLayer::Layer2).x1  == 0x11 &&
              dbg.clip_window(ClipLayer::Sprites).x1 == 0x22 &&
              dbg.clip_window(ClipLayer::Ula).x1     == 0x33 &&
              dbg.clip_window(ClipLayer::Tilemap).x1 == 0x44,
              hex(dbg.clip_window(ClipLayer::Ula).x1));
    }
    {
        // THE AUDIO MODE SIGNALS — INS-10-04 compared turbosound_enabled()
        // against its own accessor and nothing read ay_mode / stereo_mode at
        // all, so a reversed polarity on either was invisible.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.turbosound().set_ay_mode(false);
        check("INS-10-05", "ay_mode() reports YM for the YM curve",
              dbg.ay_mode() == jnext::dbg::AyChipMode::Ym);
        emu.turbosound().set_ay_mode(true);
        check("INS-10-06", "and AY for the AY curve — the polarity is not reversed",
              dbg.ay_mode() == jnext::dbg::AyChipMode::Ay);
        emu.turbosound().set_stereo_mode(false);
        check("INS-10-07", "stereo_mode() reports ABC for NR 0x08 b5 clear",
              dbg.stereo_mode() == jnext::dbg::StereoMode::Abc);
        emu.turbosound().set_stereo_mode(true);
        check("INS-10-08", "and ACB when it is set",
              dbg.stereo_mode() == jnext::dbg::StereoMode::Acb);
        emu.turbosound().set_enabled(false);
        check("INS-10-09", "turbosound_enabled() follows the live enable",
              !dbg.turbosound_enabled());

        // ay_registers(chip) — INS-10-01 proved the SIZE. Nothing proved the
        // chip index selects a chip, so reading chip 0 for every index survived.
        // Ports 0xFFFD/0xBFFD: 0xFF|chip selects the AY, then reg + value.
        emu.turbosound().set_enabled(true);
        auto write_ay = [&](uint8_t chip, uint8_t reg, uint8_t val) {
            emu.turbosound().reg_addr(static_cast<uint8_t>(0xFC | (~chip & 0x03)));
            emu.turbosound().reg_addr(reg);
            emu.turbosound().reg_write(val);
        };
        write_ay(0, 0x02, 0x11);
        write_ay(1, 0x02, 0x22);
        write_ay(2, 0x02, 0x33);
        const auto a0 = dbg.ay_registers(0), a1 = dbg.ay_registers(1), a2 = dbg.ay_registers(2);
        check("INS-10-10", "ay_registers(chip) reads THAT chip's file",
              a0.value[2] == 0x11 && a1.value[2] == 0x22 && a2.value[2] == 0x33,
              hex(a0.value[2]) + "/" + hex(a1.value[2]) + "/" + hex(a2.value[2]));
    }
    {
        // INS-16's two composed port values had no row.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.set_joystick(1, jnext::dbg::JoystickSide::Left, 0x0001);
        dbg.set_joystick(1, jnext::dbg::JoystickSide::Right, 0x0002);
        const auto in = dbg.input_state();
        check("INS-16-02", "input_state() carries the two composed port reads",
              in.port_1f == emu.joystick().read_port_1f() &&
              in.port_37 == emu.joystick().read_port_37() &&
              in.port_1f != in.port_37,
              hex(in.port_1f) + "/" + hex(in.port_37));
        check("INS-16-03", "and the two connectors are not each other",
              in.joy_left12 == 0x0001 && in.joy_right12 == 0x0002);
    }
    {
        // INS-11's SYMBOL path had no row: `disassemble(.., &symbols())` was
        // never called with a non-empty table, so the substitution could not
        // have worked and nothing would have said so.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const std::string map_path = "/tmp/jnext_b1_syms.map";
        {
            std::ofstream f(map_path);
            f << "MY_TARGET = $9000 ; const\n";
        }
        const auto loaded = dbg.load_map(map_path, jnext::dbg::MapFormat::Simple);
        check("SYM-04", "load_map() loads a simple MAP file and counts its symbols",
              loaded.status == Result::Ok && loaded.value == 1,
              std::to_string(loaded.value));
        check("SYM-05", "lookup() and lookup_name() answer for it, both ways",
              dbg.lookup(0x9000).has_value() && *dbg.lookup(0x9000) == "MY_TARGET" &&
              dbg.lookup_name("MY_TARGET").has_value() &&
              *dbg.lookup_name("MY_TARGET") == 0x9000);
        check("SYM-06", "and symbols() is the same one table",
              dbg.symbols().size() == 1);
        // 0x8002 is `CALL 0x9000` — the immediate must be substituted.
        const auto with = dbg.disassemble(PROG + 2, 1, &dbg.symbols());
        const auto without = dbg.disassemble(PROG + 2, 1, nullptr);
        check("SYM-07", "disassemble() substitutes the symbol for the 16-bit immediate",
              std::string(with[0].mnemonic).find("MY_TARGET") != std::string::npos,
              with[0].mnemonic);
        check("SYM-08", "and leaves it alone when no table is passed",
              std::string(without[0].mnemonic).find("MY_TARGET") == std::string::npos &&
              std::string(without[0].mnemonic).find("9000") != std::string::npos,
              without[0].mnemonic);
        check("SYM-09", "clear_symbols() empties it",
              dbg.clear_symbols() == Result::Ok && dbg.symbols().empty() &&
              !dbg.lookup(0x9000).has_value());
        std::remove(map_path.c_str());
    }
    {
        // EIGHT VERBS WITH NO ROW AT ALL.
        Emulator emu; build(emu);
        Debugger dbg(emu);

        // SES-04 — the stop policy round-trip.
        check("SES-04-01", "stop_policy() starts at Pause (the Qt default)",
              dbg.stop_policy() == jnext::dbg::StopPolicy::Pause);
        check("SES-04-02", "and set_stop_policy() round-trips ExitNonZero",
              dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero) == Result::Ok &&
              dbg.stop_policy() == jnext::dbg::StopPolicy::ExitNonZero);

        // ST-03 — the rewind enable and the resize, which is what makes
        // capacity move.
        check("ST-03-05", "rewind_enabled() is false without --rewind-buffer-size",
              !dbg.rewind_enabled());
        check("ST-03-06", "set_rewind_enabled(true) round-trips",
              dbg.set_rewind_enabled(true) == Result::Ok && dbg.rewind_enabled());
        check("ST-03-07", "resize_rewind_buffer(0) is refused",
              dbg.resize_rewind_buffer(0) == Result::RefusedUnavailable);
        check("ST-03-08", "resize_rewind_buffer(n) sets the capacity to n",
              dbg.resize_rewind_buffer(16) == Result::Ok &&
              dbg.rewind_range().capacity == 16,
              std::to_string(dbg.rewind_range().capacity));

        // INS-13 — the trace export.
        const std::string trace_path = "/tmp/jnext_b1_trace.txt";
        dbg.set_trace_enabled(true);
        emu.execute_single_instruction();
        check("INS-13-06", "trace_export() writes the log to a file",
              dbg.trace_export(trace_path) == Result::Ok);
        {
            std::ifstream f(trace_path);
            std::string first;
            std::getline(f, first);
            check("INS-13-07", "and the file is not empty", !first.empty(), first);
        }
        std::remove(trace_path.c_str());
        check("INS-13-08", "an unwritable path is refused, not silently dropped",
              dbg.trace_export("/nonexistent-dir/x/y.txt") == Result::RefusedUnavailable);
    }
    {
        // TIME-03 `run_to_frame` — a declared verb with no row at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        attach_and_pause(emu);
        const uint32_t here = dbg.time().frame;
        check("TIME-03-01", "run_to_frame() to a PAST frame is refused (forward only)",
              dbg.run_to_frame(1, here) == Result::RefusedUnavailable &&
              dbg.run_to_frame(1, 0) == Result::RefusedUnavailable);
        check("TIME-03-02", "run_to_frame(here + 2) is accepted",
              dbg.run_to_frame(1, here + 2) == Result::Ok);
        run_until_paused(emu, 5);
        check("TIME-03-03", "and the machine stops in that frame, not the next one",
              dbg.state().paused && dbg.time().frame == here + 2,
              "frame=" + std::to_string(dbg.time().frame) +
                  " want=" + std::to_string(here + 2));
    }
    {
        // THE NEVER-RUN STATE — the third of the three, and the one the second
        // cut of this verb still got wrong. `frame_tag()` clamps, so `tag == 0`
        // means both "frame 0 finished" and "nothing has run yet"; a base of
        // `tag + 1` is right for the first and one too many for the second, and
        // `run_to_frame(3)` landed in frame 2. Nothing has run here at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("TIME-03-04", "run_to_frame() is accepted on a machine that has run nothing",
              emu.frame_num() == 0 && dbg.run_to_frame(1, 3) == Result::Ok);
        run_until_paused(emu, 6);
        check("TIME-03-05", "and it lands in frame 3, not one short of it",
              dbg.state().paused && dbg.time().frame == 3,
              "frame=" + std::to_string(dbg.time().frame));
    }
    {
        // THE MID-FRAME ARM of the same conditional. Both rows above arm at a
        // FRAME BOUNDARY, so a mutation that breaks only the in-progress arm
        // survived them — round 1's BLOCKER-1 pattern (one arm of a condition
        // tested) recurring in code round 2 added. Stopping mid-frame first is
        // what reaches it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().pause();
        emu.debug_state().run_to_cycle(emu.clock().get() + 5000);
        emu.run_frame();                       // stops part-way through frame 0
        check("TIME-03-06", "the precondition: the machine is mid-frame, not at a boundary",
              !dbg.at_frame_boundary() && dbg.time().frame == 0,
              "frame=" + std::to_string(dbg.time().frame));
        check("TIME-03-07", "run_to_frame() from mid-frame is accepted",
              dbg.run_to_frame(1, 2) == Result::Ok);
        run_until_paused(emu, 6);
        check("TIME-03-08", "and lands in frame 2 — the in-progress arm is not off by one",
              dbg.state().paused && dbg.time().frame == 2,
              "frame=" + std::to_string(dbg.time().frame));
    }
    {
        // ST-01's OTHER mode: AdvanceToBoundary, which is the reason the verb
        // gained a `by`. Mid-frame, so the advance actually runs.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().pause();
        emu.debug_state().run_to_cycle(emu.clock().get() + 5000);
        emu.run_frame();                             // stops mid-frame
        check("ST-01-03", "the machine is mid-frame after a run-to-cycle stop",
              !dbg.at_frame_boundary());
        check("ST-01-04", "RefuseMidFrame refuses there",
              dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame)
                  .status == Result::NotAtFrameBoundary);
        const auto adv = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("ST-01-05", "AdvanceToBoundary runs the frame out and saves there",
              adv.status == Result::Ok && adv.value.size() > 1024 &&
              dbg.at_frame_boundary());
        check("ST-01-06", "and the advance left the debugger's own state alone (SuspendScope)",
              emu.debug_state().paused());
    }
    {
        // Keyboard::set_matrix_bit became PUBLIC, so its own bounds guard is now
        // API surface. The Debugger refuses first (IN-02-03), so this calls the
        // keyboard directly — the only way to reach the guard at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.keyboard().set_matrix_bit(9, 0, true);    // out of range: row > 7
        emu.keyboard().set_matrix_bit(0, 7, true);    // out of range: col > 4
        bool all_released = true;
        for (int r = 0; r < 8; ++r)
            all_released = all_released && dbg.input_state().matrix[r] == 0xFF;
        check("IN-02-06", "an out-of-range set_matrix_bit changes no row of the matrix",
              all_released);
        check("IN-02-07", "and matrix_row() answers 0xFF for a row that does not exist",
              emu.keyboard().matrix_row(9) == 0xFF &&
              emu.keyboard().matrix_row(-1) == 0xFF);
    }

    // =======================================================================
    // The DMA slot flag (§4.3) — B1's fifth accessor addition.
    //
    // It has no consumer until B2 tags an event's `source` with it, so it is
    // pinned here on BOTH sides: a CPU slot must clear it and a DMA burst slot
    // must set it. A one-sided row would pass on a flag that is always false,
    // which is exactly what an unwired member would be.
    // =======================================================================
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        emu.nextreg().write(0x54, 0x20);          // slot 4 -> plain SRAM page
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0x8000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        // The CPU program lives at 0xC000, well clear of the 0x8000 source and
        // the 0x9000 destination — writing it over the source is how the first
        // cut of this row "proved" a burst that had in fact copied zeros.
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0xC000; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        emu.mmu().write(0xC000, 0x00);            // NOP
        emu.mmu().write(0xC001, 0x00);            // NOP

        emu.execute_single_instruction();
        check("DMA-SLOT-01", "a CPU instruction slot does not read as the DMA's",
              !emu.slot_ran_dma());

        // Program a mem->mem burst (the dma_test G23 idiom) and run the slot
        // the DMA holds the bus for.
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        w(0x7D); w(0x00); w(0x80); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        bool copied = true;
        for (int i = 0; i < 8; ++i)
            copied = copied && emu.mmu().read(static_cast<uint16_t>(0x9000 + i)) ==
                                   static_cast<uint8_t>(0xA0 + i);
        check("DMA-SLOT-02", "the burst really ran (8 bytes copied)", copied);
        check("DMA-SLOT-03", "and that slot reads as the DMA's, not the CPU's",
              emu.slot_ran_dma());

        emu.execute_single_instruction();
        check("DMA-SLOT-04", "the flag is per-slot, not sticky: the next CPU slot clears it",
              !emu.slot_ran_dma());
    }


    // =======================================================================
    // GH #276 B2 — §4.3 CAP-EVT.
    //
    // THE ROWS ARE DERIVED FROM THE DIFF, function by function, not from the
    // list of things B2 set out to build — that is the lesson B1 paid for (a
    // 27-mutation table built from its own rows, and an independent reviewer
    // then found five survivors in code with no row at all).
    //
    // Two tiers, deliberately:
    //
    //   EVT-TBL-*  the `EventTable` DIRECTLY, as a value: ids, the live
    //              formula, the slot masks, `filter_matches` arm by arm, the
    //              ring and the history. Pure logic, so it is tested pure —
    //              driving a whole emulator to reach one `if` is how arms end
    //              up with no row.
    //   EVT-*      the SITES and the DELIVERY, through a running machine: the
    //              latch really fires from inside an instruction, the drain
    //              really delivers at the boundary, the payload really carries
    //              what the site saw.
    //
    // BOTH ARMS OF EVERY CONDITIONAL GET A ROW. Twice on this branch a
    // condition was right in the state the rows happened to arm and wrong in
    // another, so every gate here has its negative row next to its positive one.
    // =======================================================================

    // ── EVT-TBL — the table as a value ──────────────────────────────────
    {
        EventTable t;
        Subscription a;
        a.kind = EventKind::Execute;
        a.filter.lo = 0x1000; a.filter.hi = 0x1FFF;
        const EventId id1 = t.add(11, a);
        const EventId id2 = t.add(22, a);
        check("EVT-TBL-01", "add() returns a fresh id, never EVENT_NONE",
              id1 != jnext::dbg::EVENT_NONE && id2 != jnext::dbg::EVENT_NONE &&
              id1 != id2);
        check("EVT-TBL-02", "the entry carries its owner and its kind",
              t.find(id1)->owner == 11 && t.find(id2)->owner == 22 &&
              t.find(id1)->kind == EventKind::Execute);
        check("EVT-TBL-03", "the filter is copied, not referenced",
              t.find(id1)->filter.lo == 0x1000 && t.find(id1)->filter.hi == 0x1FFF);
        check("EVT-TBL-04", "a fresh entry is live",
              t.find(id1)->live && t.find(id2)->live);
        check("EVT-TBL-05", "has_kind() reports the kind that is armed",
              t.has_kind(EventKind::Execute));
        check("EVT-TBL-06", "and NOT one that is not",
              !t.has_kind(EventKind::Mem) && !t.has_kind(EventKind::Dma));

        check("EVT-TBL-07", "erase() removes it and reports so",
              t.erase(id1) && t.find(id1) == nullptr);
        check("EVT-TBL-08", "erasing the same id twice is false, not a crash",
              !t.erase(id1));
        check("EVT-TBL-09", "erasing an id that never existed is false",
              !t.erase(9999));
        check("EVT-TBL-10", "the other client's subscription is untouched",
              t.find(id2) != nullptr && t.find(id2)->live);
        check("EVT-TBL-11", "a tombstone is still a row until compact()",
              t.entries().size() == 2);
        t.compact();
        check("EVT-TBL-12", "compact() removes the tombstone and only that",
              t.entries().size() == 1 && t.find(id2) != nullptr);
        check("EVT-TBL-13", "ids are never reused after a compact",
              t.add(33, a) != id1);
    }

    // The live formula — enabled AND (transient OR master) AND client. Four
    // terms, so four pairs of rows: a term that stopped mattering would
    // otherwise be invisible.
    {
        EventTable t;
        Subscription a; a.kind = EventKind::Mem; a.access = Access::Write;
        const EventId id = t.add(5, a);
        check("EVT-TBL-20", "live with master on, client on, own flag on",
              t.find(id)->live);

        t.set_enabled(id, false);
        check("EVT-TBL-21", "own flag off -> not live", !t.find(id)->live);
        check("EVT-TBL-22", "and the kind mask drops with it",
              !t.has_kind(EventKind::Mem));
        check("EVT-TBL-23", "the flag AS SET is reported, not the live value",
              !t.find(id)->enabled);
        t.set_enabled(id, true);
        check("EVT-TBL-24", "own flag back on -> live again", t.find(id)->live);

        t.set_master_enabled(false);
        check("EVT-TBL-25", "master off -> not live", !t.find(id)->live);
        check("EVT-TBL-26", "but its OWN flag survives the master round trip",
              t.find(id)->enabled);
        t.set_master_enabled(true);
        check("EVT-TBL-27", "master back on -> live, with the flag intact",
              t.find(id)->live && t.find(id)->enabled);

        t.set_client_enabled(5, false);
        check("EVT-TBL-28", "its client's switch off -> not live",
              !t.find(id)->live && !t.client_enabled(5));
        check("EVT-TBL-29", "a DIFFERENT client's switch is unaffected",
              t.client_enabled(6));
        t.set_client_enabled(5, true);
        check("EVT-TBL-30", "client back on -> live", t.find(id)->live);
        check("EVT-TBL-31", "a client never named is enabled (opt-out, not opt-in)",
              t.client_enabled(12345));
        check("EVT-TBL-32", "set_enabled on an unknown id is false",
              !t.set_enabled(4242, false));
    }

    // Transient: exempt from the MASTER switch and from nothing else (§4.3).
    {
        EventTable t;
        Subscription tr; tr.kind = EventKind::Execute; tr.transient = true;
        Subscription us; us.kind = EventKind::Execute;
        const EventId tid = t.add(7, tr);
        const EventId uid = t.add(7, us);
        t.set_master_enabled(false);
        check("EVT-TBL-40", "master off suspends the user subscription",
              !t.find(uid)->live);
        check("EVT-TBL-41", "but NOT the transient one (Step Over must keep working)",
              t.find(tid)->live);
        t.set_client_enabled(7, false);
        check("EVT-TBL-42", "a transient IS subject to its client's switch",
              !t.find(tid)->live);
        t.set_client_enabled(7, true);
        t.set_enabled(tid, false);
        check("EVT-TBL-43", "and to its own flag",
              !t.find(tid)->live);
        t.set_enabled(tid, true);
        check("EVT-TBL-44", "clear_transient() drops the transient and counts it",
              t.clear_transient() == 1 && t.find(tid) == nullptr);
        check("EVT-TBL-45", "and leaves the user subscription alone",
              t.find(uid) != nullptr);
        check("EVT-TBL-46", "a second clear_transient() drops nothing",
              t.clear_transient() == 0);
        check("EVT-TBL-47", "erase_client() drops that client's rows and counts them",
              t.erase_client(7) == 1 && t.find(uid) == nullptr);
        check("EVT-TBL-48", "erase_client() on an unknown client drops nothing",
              t.erase_client(999) == 0);
    }

    // The §6 slot masks — the ONE thing the hot path reads.
    {
        EventTable t;
        Subscription w; w.kind = EventKind::Mem; w.access = Access::Write;
        w.filter.lo = 0x9000; w.filter.hi = 0x9000;             // slot 4
        const EventId wid = t.add(1, w);
        check("EVT-TBL-60", "a WRITE range arms its slot in the write mask only",
              t.wr_slot_mask() == 0x10 && t.rd_slot_mask() == 0x00);

        Subscription r; r.kind = EventKind::Mem; r.access = Access::Read;
        r.filter.lo = 0x0000; r.filter.hi = 0x1FFF;             // slot 0
        t.add(1, r);
        check("EVT-TBL-61", "a READ range arms its slot in the read mask only",
              t.rd_slot_mask() == 0x01 && t.wr_slot_mask() == 0x10);

        Subscription rw; rw.kind = EventKind::Mem; rw.access = Access::ReadWrite;
        rw.filter.lo = 0x4000; rw.filter.hi = 0x5FFF;           // slot 2
        t.add(1, rw);
        check("EVT-TBL-62", "ReadWrite arms BOTH masks",
              (t.rd_slot_mask() & 0x04) && (t.wr_slot_mask() & 0x04));

        Subscription span; span.kind = EventKind::Mem; span.access = Access::Write;
        span.filter.lo = 0x3FFF; span.filter.hi = 0x4000;       // slots 1 and 2
        const EventId sid = t.add(1, span);
        check("EVT-TBL-63", "a range that straddles a slot boundary arms both slots",
              (t.wr_slot_mask() & 0x02) && (t.wr_slot_mask() & 0x04));
        t.erase(sid);
        check("EVT-TBL-64", "and erasing it takes its bits away again",
              (t.wr_slot_mask() & 0x02) == 0);

        // EVT-TBL-63 above cannot see a straddle that arms only its FIRST slot:
        // the `rw` subscription three rows up already armed slot 2. A fresh table
        // is what makes the claim exact — a mutation that armed only the first
        // slot of a span survived the suite until this row existed.
        EventTable t3;
        Subscription span_only; span_only.kind = EventKind::Mem;
        span_only.access = Access::Write;
        span_only.filter.lo = 0x3FFF; span_only.filter.hi = 0x4000;
        t3.add(1, span_only);
        check("EVT-TBL-68", "on a table with NOTHING else armed, a straddling range "
                            "arms BOTH its slots and only those",
              t3.wr_slot_mask() == 0x06 && t3.rd_slot_mask() == 0x00);

        t.set_enabled(wid, false);
        check("EVT-TBL-65", "a DISABLED subscription contributes no bit",
              (t.wr_slot_mask() & 0x10) == 0);
        t.set_enabled(wid, true);
        t.set_master_enabled(false);
        check("EVT-TBL-66", "master off zeroes the whole event mask",
              t.rd_slot_mask() == 0 && t.wr_slot_mask() == 0);
        t.set_master_enabled(true);
        check("EVT-TBL-67", "and master on rebuilds it exactly",
              (t.wr_slot_mask() & 0x14) == 0x14 && (t.rd_slot_mask() & 0x05) == 0x05);
    }

    // An inverted range arms nothing — the precise scan would reject every
    // address in it, so arming its slots is a gate that can never pay off.
    {
        EventTable t;
        Subscription bad; bad.kind = EventKind::Mem; bad.access = Access::ReadWrite;
        bad.filter.lo = 0x8000; bad.filter.hi = 0x4000;
        t.add(1, bad);
        check("EVT-TBL-70", "an inverted range (lo > hi) arms no slot at all",
              t.rd_slot_mask() == 0 && t.wr_slot_mask() == 0);
        check("EVT-TBL-71", "and matches no address either",
              !t.mem_would_match(0x4000, 0, Access::Write) &&
              !t.mem_would_match(0x8000, 0, Access::Write) &&
              !t.mem_would_match(0x6000, 0, Access::Write));

        // THE CASE THE `lo > hi` GUARD IS ACTUALLY FOR. With the two ends in
        // DIFFERENT slots the span loop is empty anyway (`first > last`), so the
        // row above passes with or without the guard — a mutation that removed it
        // survived the whole suite. An inverted range INSIDE ONE SLOT has
        // `first == last`, and the loop would arm that slot.
        EventTable t2;
        Subscription within; within.kind = EventKind::Mem;
        within.access = Access::ReadWrite;
        within.filter.lo = 0x1FFF; within.filter.hi = 0x1000;   // both in slot 0
        t2.add(1, within);
        check("EVT-TBL-72", "an inverted range WITHIN ONE SLOT arms nothing either",
              t2.rd_slot_mask() == 0 && t2.wr_slot_mask() == 0);
        check("EVT-TBL-73", "and matches nothing in it",
              !t2.mem_would_match(0x1000, 0, Access::Write) &&
              !t2.mem_would_match(0x1FFF, 0, Access::Read));
    }

    // The physical-page forms, and `on_slot_remapped`'s reason to exist.
    {
        EventTable t;
        Subscription pg; pg.kind = EventKind::Mem; pg.access = Access::Write;
        pg.filter.pages = { 0x2A };
        t.add(1, pg);
        check("EVT-TBL-80", "a page set with no slot holding that page arms nothing",
              t.wr_slot_mask() == 0);
        check("EVT-TBL-81", "set_slot_page() to the watched page arms that slot and says so",
              t.set_slot_page(6, 0x2A) && t.wr_slot_mask() == 0x40);
        check("EVT-TBL-82", "and the table remembers the page it was told",
              t.slot_page(6) == 0x2A);
        check("EVT-TBL-83", "re-publishing the SAME page reports no change",
              !t.set_slot_page(6, 0x2A));
        check("EVT-TBL-84", "moving the page to another slot moves the bit",
              t.set_slot_page(2, 0x2A) && (t.wr_slot_mask() & 0x04));
        check("EVT-TBL-85", "and mapping it away clears that slot's bit",
              t.set_slot_page(6, 0x07) && (t.wr_slot_mask() & 0x40) == 0);
        check("EVT-TBL-86", "an out-of-range slot is refused, not written",
              !t.set_slot_page(-1, 0x2A) && !t.set_slot_page(8, 0x2A));
        check("EVT-TBL-87", "the page form matches on the PAGE and ignores the address",
              t.mem_would_match(0x0000, 0x2A, Access::Write) &&
              t.mem_would_match(0xFFFF, 0x2A, Access::Write) &&
              !t.mem_would_match(0x0000, 0x2B, Access::Write));
    }

    // A RANGE that also carries `page` is the AND form (DZRP's bank+1), and
    // that narrowing is what makes the mask worth recomputing on a remap.
    {
        EventTable t;
        Subscription both; both.kind = EventKind::Mem; both.access = Access::Read;
        both.filter.lo = 0x4000; both.filter.hi = 0x5FFF;   // slot 2
        both.filter.page = 0x05;
        t.add(1, both);
        check("EVT-TBL-90", "the AND form arms nothing while the page is elsewhere",
              t.rd_slot_mask() == 0);
        t.set_slot_page(2, 0x05);
        check("EVT-TBL-91", "and arms exactly that slot once the page arrives",
              t.rd_slot_mask() == 0x04);
        check("EVT-TBL-92", "it matches only inside the range AND on the page",
              t.mem_would_match(0x4000, 0x05, Access::Read) &&
              !t.mem_would_match(0x4000, 0x06, Access::Read) &&
              !t.mem_would_match(0x6000, 0x05, Access::Read));
        check("EVT-TBL-93", "a slot remap with no page-qualified filter reports no change",
              !EventTable{}.set_slot_page(0, 0x12));
    }

    // mem/port/nr `would_match` — the site-side gates. Access, direction,
    // mask/value and the register set, each with both arms.
    {
        EventTable t;
        Subscription m; m.kind = EventKind::Mem; m.access = Access::Write;
        m.filter.lo = 0x8000; m.filter.hi = 0x80FF;
        t.add(1, m);
        check("EVT-TBL-100", "mem_would_match inside the range, right direction",
              t.mem_would_match(0x8000, 0, Access::Write) &&
              t.mem_would_match(0x80FF, 0, Access::Write));
        check("EVT-TBL-101", "and not one byte either side of it",
              !t.mem_would_match(0x7FFF, 0, Access::Write) &&
              !t.mem_would_match(0x8100, 0, Access::Write));
        check("EVT-TBL-102", "and not for the WRONG direction",
              !t.mem_would_match(0x8000, 0, Access::Read));

        Subscription p; p.kind = EventKind::Port; p.access = Access::ReadWrite;
        p.filter.port_mask = 0x00FF; p.filter.port_value = 0xFE;
        t.add(1, p);
        check("EVT-TBL-103", "a low-byte port filter matches every high byte",
              t.port_would_match(0x00FE, Access::Read) &&
              t.port_would_match(0x7FFE, Access::Write) &&
              t.port_would_match(0xFEFE, Access::Read));
        check("EVT-TBL-104", "and not a port whose low byte differs",
              !t.port_would_match(0x00FF, Access::Read));

        Subscription pf; pf.kind = EventKind::Port; pf.access = Access::Write;
        pf.filter.port_mask = 0xFFFF; pf.filter.port_value = 0x243B;
        const EventId pfid = t.add(1, pf);
        check("EVT-TBL-105", "a full 16-bit port filter matches exactly one port",
              t.port_would_match(0x243B, Access::Write) &&
              !t.port_would_match(0x253B, Access::Write));
        t.set_enabled(pfid, false);
        check("EVT-TBL-106", "a disabled port subscription matches nothing",
              !t.port_would_match(0x243B, Access::Write));

        Subscription nr; nr.kind = EventKind::NextRegWrite;
        nr.filter.regs = { 0x15, 0x43 };
        t.add(1, nr);
        check("EVT-TBL-107", "an NR set matches its members",
              t.nr_would_match(0x15, EventSource::Cpu) &&
              t.nr_would_match(0x43, EventSource::Copper));
        check("EVT-TBL-108", "and not a register outside it",
              !t.nr_would_match(0x16, EventSource::Cpu));

        Subscription nra; nra.kind = EventKind::NextRegWrite;
        nra.filter.source = EventSource::Copper;
        t.add(1, nra);
        check("EVT-TBL-109", "an empty NR set means every register",
              t.nr_would_match(0x00, EventSource::Copper) &&
              t.nr_would_match(0xFF, EventSource::Copper));
        check("EVT-TBL-110", "and a source filter excludes the other writers",
              !t.nr_would_match(0x16, EventSource::Dma));
    }

    // ── EVT-TBL-FM — filter_matches(), arm by arm ───────────────────────
    //
    // Fourteen kinds, and the switch has one arm each. A kind whose arm is
    // wrong is invisible to every site row (the site would latch correctly and
    // the drain would drop it), so each arm is asserted directly, in both
    // directions.
    {
        EventTable t;
        auto ev_of = [](EventKind k) { DbgEvent e; e.kind = k; return e; };

        // Execute — PC range, optional page qualifier.
        {
            Subscription s; s.kind = EventKind::Execute;
            s.filter.lo = 0x8000; s.filter.hi = 0x8002;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Execute); e.pc = 0x8001;
            check("EVT-TBL-FM-01", "Execute matches inside its PC range",
                  t.filter_matches(*t.find(id), e));
            e.pc = 0x8003;
            check("EVT-TBL-FM-02", "and not outside it",
                  !t.filter_matches(*t.find(id), e));
            e.kind = EventKind::Mem;
            check("EVT-TBL-FM-03", "a different KIND never matches, whatever the payload",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.page = 0x0A;
            DbgEvent p = ev_of(EventKind::Execute); p.pc = 0x8001; p.phys_page = 0x0A;
            check("EVT-TBL-FM-04", "the Execute page qualifier matches its page",
                  t.filter_matches(*t.find(id), p));
            p.phys_page = 0x0B;
            check("EVT-TBL-FM-05", "and rejects another",
                  !t.filter_matches(*t.find(id), p));
            t.erase(id);
        }

        // Mem — access, source, pages, range, page qualifier.
        {
            Subscription s; s.kind = EventKind::Mem; s.access = Access::Write;
            s.filter.lo = 0x4000; s.filter.hi = 0x4FFF;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Mem);
            e.addr = 0x4100; e.access = Access::Write;
            check("EVT-TBL-FM-10", "Mem{Write} matches a write in range",
                  t.filter_matches(*t.find(id), e));
            e.access = Access::Read;
            check("EVT-TBL-FM-11", "and rejects the read of the same address",
                  !t.filter_matches(*t.find(id), e));
            e.access = Access::Write; e.addr = 0x5000;
            check("EVT-TBL-FM-12", "and rejects an address past the range",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.source = EventSource::Dma;
            e.addr = 0x4100; e.source = EventSource::Cpu;
            check("EVT-TBL-FM-13", "a source filter rejects the other originator",
                  !t.filter_matches(*t.find(id), e));
            e.source = EventSource::Dma;
            check("EVT-TBL-FM-14", "and accepts its own",
                  t.filter_matches(*t.find(id), e));
            t.find(id)->filter.source = EventSource::Any;
            e.source = EventSource::Cpu;
            check("EVT-TBL-FM-15", "EventSource::Any accepts either",
                  t.filter_matches(*t.find(id), e));
            // The RANGE + `page` AND form (DZRP's bank+1), which EVT-TBL-90..92
            // pin for `mem_would_match` but nothing pinned for `filter_matches`
            // — a mutation that dropped the qualifier here survived the suite.
            t.find(id)->filter.page = 0x33;
            e.addr = 0x4100; e.phys_page = 0x33;
            check("EVT-TBL-FM-18", "a range + `page` matches only on that page",
                  t.filter_matches(*t.find(id), e));
            e.phys_page = 0x34;
            check("EVT-TBL-FM-19", "and rejects another page inside the same range",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.page = jnext::dbg::PAGE_ANY;
            e.phys_page = 0x34;
            t.find(id)->filter.pages = { 0x11 };
            e.phys_page = 0x11; e.addr = 0xFFFF;     // outside the range
            check("EVT-TBL-FM-16", "a page SET replaces the range, not narrows it",
                  t.filter_matches(*t.find(id), e));
            e.phys_page = 0x12;
            check("EVT-TBL-FM-17", "and rejects a page outside the set",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Port — mask/value, access, source.
        {
            Subscription s; s.kind = EventKind::Port; s.access = Access::Read;
            s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Port);
            e.port = 0x7FFE; e.access = Access::Read;
            check("EVT-TBL-FM-20", "Port matches on (port & mask) == value",
                  t.filter_matches(*t.find(id), e));
            e.port = 0x7FFF;
            check("EVT-TBL-FM-21", "and rejects a port the mask does not fold onto it",
                  !t.filter_matches(*t.find(id), e));
            e.port = 0x7FFE; e.access = Access::Write;
            check("EVT-TBL-FM-22", "and rejects the wrong direction",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // NextRegWrite — source and the register set.
        {
            Subscription s; s.kind = EventKind::NextRegWrite;
            s.filter.regs = { 0x15 };
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::NextRegWrite); e.reg = 0x15;
            check("EVT-TBL-FM-30", "NextRegWrite matches a register in the set",
                  t.filter_matches(*t.find(id), e));
            e.reg = 0x16;
            check("EVT-TBL-FM-31", "and rejects one outside it",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.regs.clear();
            check("EVT-TBL-FM-32", "an empty set matches every register",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Frame — FRAME_EVERY vs one number.
        {
            Subscription s; s.kind = EventKind::Frame;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Frame); e.frame = 7;
            check("EVT-TBL-FM-40", "Frame defaults to FRAME_EVERY and matches any frame",
                  t.filter_matches(*t.find(id), e));
            t.find(id)->filter.frame = 7;
            check("EVT-TBL-FM-41", "a frame number matches that frame",
                  t.filter_matches(*t.find(id), e));
            e.frame = 8;
            check("EVT-TBL-FM-42", "and not the next one",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Scanline — compared on cvc, NOT on the raw vc.
        {
            Subscription s; s.kind = EventKind::Scanline; s.filter.scanline = 100;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Scanline); e.cvc = 100; e.vc = 164;
            check("EVT-TBL-FM-50", "Scanline matches on cvc",
                  t.filter_matches(*t.find(id), e));
            e.cvc = 99;
            check("EVT-TBL-FM-51", "and not on a neighbouring cvc",
                  !t.filter_matches(*t.find(id), e));
            e.cvc = 164; e.vc = 100;
            check("EVT-TBL-FM-52", "and NOT on the raw vc (the GH #16 origin bug)",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Cycle — a threshold, not an equality.
        {
            Subscription s; s.kind = EventKind::Cycle; s.filter.cycle = 1000;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Cycle); e.cycle = 999;
            check("EVT-TBL-FM-60", "Cycle does not match before its target",
                  !t.filter_matches(*t.find(id), e));
            e.cycle = 1000;
            check("EVT-TBL-FM-61", "matches AT the target",
                  t.filter_matches(*t.find(id), e));
            e.cycle = 100000;
            check("EVT-TBL-FM-62", "and still matches past it (>=, not ==)",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Reset — and the F8 `Any` filter value, which is B2's to implement.
        {
            Subscription s; s.kind = EventKind::Reset;
            s.filter.reset_kind = ResetKind::Soft;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Reset); e.reset_kind = ResetKind::Soft;
            check("EVT-TBL-FM-70", "Reset{Soft} matches a soft reset",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-71", "and not a hard one",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-72", "Reset{Hard} matches a hard reset",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Soft;
            check("EVT-TBL-FM-73", "and not a soft one",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.reset_kind = ResetKind::Any;
            check("EVT-TBL-FM-74", "ResetKind::Any matches a SOFT reset (owner decision F8)",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-75", "and a HARD one, from the same one subscription",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // IntAck / Nmi / Magic — the seam IS the filter (§4.3 lists none).
        {
            Subscription ia; ia.kind = EventKind::IntAck;
            Subscription nm; nm.kind = EventKind::Nmi;
            Subscription mg; mg.kind = EventKind::Magic;
            const EventId i1 = t.add(1, ia), i2 = t.add(1, nm), i3 = t.add(1, mg);
            DbgEvent a = ev_of(EventKind::IntAck); a.int_vector = 0xFF; a.int_mode = 1;
            DbgEvent b = ev_of(EventKind::Nmi);    b.nmi_source = NmiButton::Drive;
            DbgEvent c = ev_of(EventKind::Magic);  c.pc = 0x1234;
            check("EVT-TBL-FM-80", "IntAck matches unconditionally",
                  t.filter_matches(*t.find(i1), a));
            check("EVT-TBL-FM-81", "Nmi matches unconditionally, whichever button",
                  t.filter_matches(*t.find(i2), b));
            check("EVT-TBL-FM-82", "Magic matches unconditionally",
                  t.filter_matches(*t.find(i3), c));
            check("EVT-TBL-FM-83", "but each still only matches its OWN kind",
                  !t.filter_matches(*t.find(i1), b) &&
                  !t.filter_matches(*t.find(i2), c) &&
                  !t.filter_matches(*t.find(i3), a));
            t.erase(i1); t.erase(i2); t.erase(i3);
        }

        // Host — the name, with an empty filter meaning every name.
        {
            Subscription s; s.kind = EventKind::Host;
            std::strcpy(s.filter.host_name, "script3");
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Host);
            std::strcpy(e.host_name, "script3");
            check("EVT-TBL-FM-90", "Host matches its exact name",
                  t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script4");
            check("EVT-TBL-FM-91", "and rejects another",
                  !t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script");
            check("EVT-TBL-FM-92", "a PREFIX of the filter is not a match",
                  !t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script33");
            check("EVT-TBL-FM-93", "nor is the filter a prefix of the name",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.host_name[0] = '\0';
            check("EVT-TBL-FM-94", "an empty filter name matches every name",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Copper — sub-kind, copper-PC range, and the NR set for a MOVE.
        {
            Subscription s; s.kind = EventKind::Copper;
            s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
            s.filter.lo = 0; s.filter.hi = 15;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Copper);
            e.copper_kind = jnext::dbg::CopperEventKind::Move;
            e.copper_pc = 3; e.reg = 0x43;
            check("EVT-TBL-FM-100", "Copper{Move} matches inside the copper-PC range",
                  t.filter_matches(*t.find(id), e));
            e.copper_pc = 16;
            check("EVT-TBL-FM-101", "and not outside it",
                  !t.filter_matches(*t.find(id), e));
            e.copper_pc = 3;
            e.copper_kind = jnext::dbg::CopperEventKind::Wait;
            check("EVT-TBL-FM-102", "a Move subscription rejects a Wait",
                  !t.filter_matches(*t.find(id), e));
            e.copper_kind = jnext::dbg::CopperEventKind::Halt;
            check("EVT-TBL-FM-103", "and rejects a Halt",
                  !t.filter_matches(*t.find(id), e));
            e.copper_kind = jnext::dbg::CopperEventKind::Move;
            t.find(id)->filter.regs = { 0x43 };
            check("EVT-TBL-FM-104", "the NR set narrows a Move to its registers",
                  t.filter_matches(*t.find(id), e));
            e.reg = 0x44;
            check("EVT-TBL-FM-105", "and rejects a MOVE to another register",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
            DbgEvent w = ev_of(EventKind::Copper);
            w.copper_kind = jnext::dbg::CopperEventKind::Wait;
            w.copper_pc = 3; w.reg = 0x00;
            check("EVT-TBL-FM-106", "an NR set does NOT gate a Wait (it has no register)",
                  t.filter_matches(*t.find(id), w));
            t.erase(id);
        }

        // Dma — sub-kind, and `Byte`'s EITHER-endpoint rule.
        {
            Subscription s; s.kind = EventKind::Dma;
            s.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
            s.filter.lo = 0x4000; s.filter.hi = 0x5AFF;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Dma);
            e.dma_kind = jnext::dbg::DmaEventKind::Byte;
            e.dma_src = 0x4100; e.dma_dst = 0x9000;
            check("EVT-TBL-FM-110", "Dma{Byte} matches when the SOURCE is in range",
                  t.filter_matches(*t.find(id), e));
            e.dma_src = 0x9000; e.dma_dst = 0x4100;
            check("EVT-TBL-FM-111", "and when the DESTINATION is (either endpoint)",
                  t.filter_matches(*t.find(id), e));
            e.dma_src = 0x9000; e.dma_dst = 0x9100;
            check("EVT-TBL-FM-112", "and not when NEITHER is",
                  !t.filter_matches(*t.find(id), e));
            e.dma_kind = jnext::dbg::DmaEventKind::Start;
            check("EVT-TBL-FM-113", "a Byte subscription rejects a Start",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.dma_kind = jnext::dbg::DmaEventKind::Start;
            e.dma_src = 0xFFFF; e.dma_dst = 0xFFFF;
            check("EVT-TBL-FM-114", "Start/End ignore the range entirely",
                  t.filter_matches(*t.find(id), e));
            e.dma_kind = jnext::dbg::DmaEventKind::End;
            check("EVT-TBL-FM-115", "and a Start subscription rejects an End",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }
    }

    // ── EVT-TBL-RING — the 512-entry ring and its overflow contract ──────
    {
        EventTable t;
        check("EVT-TBL-RING-01", "a fresh ring is empty and not overflowed",
              !t.pending() && t.size() == 0 && t.dropped() == 0 && !t.overflowed());
        check("EVT-TBL-RING-02", "and its capacity is the 512 §4.3 derives",
              t.ring_capacity() == jnext::dbg::LATCH_RING_CAPACITY &&
              jnext::dbg::LATCH_RING_CAPACITY == 512);

        jnext::dbg::LatchEntry e;
        e.kind = EventKind::Mem; e.addr = 0x1234;
        t.latch(e);
        check("EVT-TBL-RING-03", "latch() appends and pending() says so",
              t.pending() && t.size() == 1 && t.at(0).addr == 0x1234);
        t.clear_ring();
        check("EVT-TBL-RING-04", "clear_ring() empties it and clears the drop count",
              !t.pending() && t.size() == 0 && t.dropped() == 0);

        // Fill to exactly capacity, then one past it.
        for (int i = 0; i < 512; ++i) {
            e.addr = static_cast<uint16_t>(i);
            t.latch(e);
        }
        check("EVT-TBL-RING-05", "512 entries fit with nothing dropped",
              t.size() == 512 && t.dropped() == 0 && !t.overflowed());
        e.addr = 0xBEEF;
        t.latch(e);
        check("EVT-TBL-RING-06", "the 513th is DROPPED, not wrapped over the first",
              t.size() == 512 && t.dropped() == 1 && t.overflowed() &&
              t.at(0).addr == 0x0000 && t.at(511).addr == 511);

        // The saturation arm, which no emulator row can reach.
        for (int i = 0; i < 70000; ++i) t.latch(e);
        check("EVT-TBL-RING-07", "the drop count SATURATES at 0xFFFF rather than wrapping",
              t.dropped() == 0xFFFF);

        t.shrink_ring_for_test(4);
        check("EVT-TBL-RING-08", "shrink_ring_for_test() resizes and clears",
              t.ring_capacity() == 4 && t.size() == 0 && t.dropped() == 0);
        check("EVT-TBL-RING-09", "it clamps a zero to 1",
              (t.shrink_ring_for_test(0), t.ring_capacity() == 1));
        check("EVT-TBL-RING-10", "and clamps past the real capacity",
              (t.shrink_ring_for_test(99999),
               t.ring_capacity() == jnext::dbg::LATCH_RING_CAPACITY));
    }

    // ── EVT-TBL-HIST — the INS-17 delivery history ──────────────────────
    {
        EventTable t;
        check("EVT-TBL-HIST-01", "a fresh history is empty at every cursor",
              t.since(0).empty() && t.since(1000).empty());
        check("EVT-TBL-HIST-02", "next_seq() is monotonic from 1",
              t.next_seq() == 1 && t.next_seq() == 2 && t.last_seq() == 2);

        for (int i = 1; i <= 5; ++i) {
            DbgEvent ev; ev.kind = EventKind::Frame; ev.seq = static_cast<uint64_t>(i);
            ev.frame = static_cast<uint32_t>(i * 10);
            t.record(ev);
        }
        check("EVT-TBL-HIST-03", "since(0) returns everything, oldest first",
              t.since(0).size() == 5 && t.since(0).front().seq == 1 &&
              t.since(0).back().seq == 5);
        check("EVT-TBL-HIST-04", "since(N) is EXCLUSIVE of N",
              t.since(3).size() == 2 && t.since(3).front().seq == 4);
        check("EVT-TBL-HIST-05", "since(last) is empty",
              t.since(5).empty());
        check("EVT-TBL-HIST-06", "and the payload survives the round trip",
              t.since(0)[2].frame == 30);

        // Wrap: the buffer is bounded, and a poller that falls behind loses the
        // OLDEST — detectably, because `seq` is in the payload.
        for (int i = 6; i <= 6 + int(EventTable::HISTORY_CAPACITY); ++i) {
            DbgEvent ev; ev.kind = EventKind::Frame; ev.seq = static_cast<uint64_t>(i);
            t.record(ev);
        }
        const auto all = t.since(0);
        check("EVT-TBL-HIST-07", "the history is bounded at HISTORY_CAPACITY",
              all.size() == EventTable::HISTORY_CAPACITY);
        check("EVT-TBL-HIST-08", "it kept the NEWEST and is still in order",
              all.back().seq == 6 + EventTable::HISTORY_CAPACITY &&
              all.front().seq < all.back().seq);
        bool ordered = true;
        for (size_t i = 1; i < all.size(); ++i)
            ordered = ordered && all[i - 1].seq < all[i].seq;
        check("EVT-TBL-HIST-09", "strictly increasing across the wrap point", ordered);
        check("EVT-TBL-HIST-10", "the oldest entries are gone, so the gap is detectable",
              all.front().seq > 1);
        t.clear_history();
        check("EVT-TBL-HIST-11", "clear_history() empties it", t.since(0).empty());
    }

    // ── EVT-BP — BreakpointSet's half of the §6 mask, and the OR ─────────
    {
        BreakpointSet bps;
        check("EVT-BP-01", "an empty set arms no slot and no port",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        bps.add_watchpoint(0x9000, WatchType::READ);
        check("EVT-BP-02", "a READ watchpoint arms its slot in the read mask only",
              bps.watch_slot_mask_rd() == 0x10 && bps.watch_slot_mask_wr() == 0);
        check("EVT-BP-03", "and rd_watch_slot_armed() answers per address",
              bps.rd_watch_slot_armed(0x9000) && bps.rd_watch_slot_armed(0x9FFF) &&
              bps.rd_watch_slot_armed(0x8000) &&      // same 8 KB slot
              !bps.rd_watch_slot_armed(0x7FFF) &&     // the slot below
              !bps.rd_watch_slot_armed(0xA000) &&     // the slot above
              !bps.wr_watch_slot_armed(0x9000));
        bps.add_watchpoint(0x4000, WatchType::WRITE);
        check("EVT-BP-04", "a WRITE watchpoint arms the write mask only",
              bps.wr_watch_slot_armed(0x4000) && !bps.rd_watch_slot_armed(0x4000));
        bps.add_watchpoint(0xC000, WatchType::READ_WRITE);
        check("EVT-BP-05", "READ_WRITE arms both",
              bps.rd_watch_slot_armed(0xC000) && bps.wr_watch_slot_armed(0xC000));
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("EVT-BP-06", "an I/O watchpoint arms the PORT flag",
              bps.port_watch_armed());
        check("EVT-BP-07", "and NO memory slot — a port is not an address (slot 0 stays clear)",
              !bps.rd_watch_slot_armed(0x0000) && !bps.wr_watch_slot_armed(0x0000));

        bps.set_watchpoint_enabled(0x9000, WatchType::READ, false);
        check("EVT-BP-08", "disabling a watchpoint takes its bit away",
              !bps.rd_watch_slot_armed(0x9000));
        bps.set_watchpoint_enabled(0x9000, WatchType::READ, true);
        check("EVT-BP-09", "and re-enabling puts it back",
              bps.rd_watch_slot_armed(0x9000));

        bps.set_master_enabled(false);
        check("EVT-BP-10", "master off zeroes every legacy bit and the port flag",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        bps.set_master_enabled(true);
        check("EVT-BP-11", "master on rebuilds them exactly",
              bps.rd_watch_slot_armed(0x9000) && bps.wr_watch_slot_armed(0x4000) &&
              bps.port_watch_armed());

        // The EVENT half, and the OR. This is the seam between the two models.
        bps.set_event_slot_masks(0x02, 0x80, false);
        check("EVT-BP-12", "the event half is OR-ed into the bytes the hot path reads",
              bps.rd_watch_slot_armed(0x2000) && bps.wr_watch_slot_armed(0xE000));
        check("EVT-BP-13", "and the legacy half survives the publication",
              bps.rd_watch_slot_armed(0x9000) && bps.wr_watch_slot_armed(0x4000));
        bps.clear_all_watchpoints();
        check("EVT-BP-14", "clearing the legacy half leaves the event half standing",
              bps.rd_watch_slot_armed(0x2000) && bps.wr_watch_slot_armed(0xE000) &&
              !bps.rd_watch_slot_armed(0x9000));
        check("EVT-BP-15", "and the port flag follows the EVENT half too",
              (bps.set_event_slot_masks(0, 0, true), bps.port_watch_armed()));
        bps.set_event_slot_masks(0, 0, false);
        check("EVT-BP-16", "zeroing both halves closes every gate",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        check("EVT-BP-17", "has_any_watchpoints() is still the MODEL query it was",
              !bps.has_any_watchpoints());
    }

    // ── EVT-MEM — the eight Mmu sites, and the payload they latch ────────
    {
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A
        //   8005  3A 00 90     LD A,(0x9000)
        //   8008  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem;
        s.access = Access::ReadWrite;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = recorder(rec);
        const auto sub = dbg.subscribe(4, s);
        check("EVT-MEM-01", "subscribe() returns Ok and an id",
              sub.status == Result::Ok && sub.value != jnext::dbg::EVENT_NONE);
        check("EVT-MEM-02", "and it armed the slot the address lives in, and only that",
              emu.debug_state().rd_watch_armed(0x9000) &&
              emu.debug_state().wr_watch_armed(0x9000) &&
              !emu.debug_state().rd_watch_armed(0x7FFF) &&
              !emu.debug_state().rd_watch_armed(0xA000));

        emu.run_frame();
        check("EVT-MEM-03", "both the write and the read were delivered, in order",
              rec.evs.size() == 2 &&
              rec.evs[0].access == Access::Write &&
              rec.evs[1].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 2) {
            check("EVT-MEM-04", "the write carries the address it landed on",
                  rec.evs[0].addr == 0x9000);
            check("EVT-MEM-05", "and the byte that was written",
                  rec.evs[0].value == 0x5A);
            check("EVT-MEM-06", "and `prev`: the byte that was there BEFORE it",
                  rec.evs[0].prev == 0x00);
            check("EVT-MEM-07", "and the PC of the instruction that wrote (pc_pre_exec)",
                  rec.evs[0].pc == 0x8002, "pc=" + hex(rec.evs[0].pc));
            check("EVT-MEM-08", "and source Cpu, tagged at the drain",
                  rec.evs[0].source == EventSource::Cpu);
            check("EVT-MEM-09", "and the effective MMU page behind the address",
                  rec.evs[0].phys_page == emu.mmu().get_effective_page(4));
            check("EVT-MEM-10", "the READ carries the byte it returned",
                  rec.evs[1].value == 0x5A && rec.evs[1].addr == 0x9000);
            check("EVT-MEM-11", "a read's `prev` is the byte itself, not zero",
                  rec.evs[1].prev == 0x5A);
            check("EVT-MEM-12", "and the reading instruction's PC",
                  rec.evs[1].pc == 0x8005, "pc=" + hex(rec.evs[1].pc));
            check("EVT-MEM-13", "the subscription and its owner are named",
                  rec.evs[0].id == sub.value && rec.evs[0].owner == 4);
            check("EVT-MEM-14", "seq is monotonic across deliveries",
                  rec.evs[0].seq > 0 && rec.evs[1].seq > rec.evs[0].seq);
            check("EVT-MEM-15", "and neither delivery is flagged as lossy",
                  !rec.evs[0].overflowed && rec.evs[0].dropped == 0);
            check("EVT-MEM-16", "the common header carries a raster position",
                  rec.evs[0].vc >= 0 && rec.evs[0].hc >= 0);
            check("EVT-MEM-17", "and the cycle of the SITE, not of the boundary",
                  rec.evs[0].cycle > 0 && rec.evs[0].cycle <= emu.clock().get());
        } else {
            check("EVT-MEM-04", "the write payload", false, "no deliveries");
        }
        check("EVT-MEM-18", "events_fired_since(0) sees the same two",
              dbg.events_fired_since(0).size() == 2);
        check("EVT-MEM-19", "and since(first) sees only the second",
              rec.evs.size() == 2 &&
              dbg.events_fired_since(rec.evs[0].seq).size() == 1);
        check("EVT-MEM-20", "a Continue action left the machine running",
              !dbg.state().paused && pc_of(emu) == 0x8008,
              "pc=" + hex(pc_of(emu)));
    }

    // The control: the same program, a subscription one byte away. The gate,
    // the slot mask and the precise scan all run and all correctly reject.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::ReadWrite;
        s.filter.lo = 0x9001; s.filter.hi = 0x9001;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);
        emu.run_frame();
        check("EVT-MEM-30", "an address one byte away delivers nothing",
              rec.evs.empty() && dbg.events_fired_since(0).empty());
        check("EVT-MEM-31", "and the machine ran to the end regardless",
              pc_of(emu) == 0x8008, "pc=" + hex(pc_of(emu)));
    }

    // Direction is a filter, not decoration: a WRITE-only subscription must not
    // see the read of the same address.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);
        check("EVT-MEM-40", "a WRITE-only subscription arms only the write mask",
              emu.debug_state().wr_watch_armed(0x9000) &&
              !emu.debug_state().rd_watch_armed(0x9000));
        emu.run_frame();
        check("EVT-MEM-41", "and sees the write and not the read",
              rec.evs.size() == 1 && rec.evs[0].access == Access::Write,
              "n=" + std::to_string(rec.evs.size()));
    }

    // A Stop action, and CTL-13's reason for it. This is the row `Watch`
    // exists for: `data_bp_hit_` is consumed in the same breath as the pause,
    // so the reason has to come from somewhere else.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(9, s);
        emu.run_frame();
        check("EVT-MEM-50", "a Stop pauses the machine at the boundary of the writer",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-MEM-51", "and the pause reason is Watch",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);
        check("EVT-MEM-52", "naming the subscription, its owner, the address and the direction",
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 9 &&
              dbg.state().pause_reason.addr == 0x9000 &&
              dbg.state().pause_reason.access == Access::Write);
        check("EVT-MEM-53", "a resume clears the reason rather than repeating it",
              dbg.run(9) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }

    // §4.3 — `source` is tagged AT THE DRAIN from the slot's DMA flag, so a DMA
    // byte into a watched range reads as the DMA's and not the CPU's.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x900F;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        check("EVT-MEM-60", "the DMA's own memory writes are delivered as Mem events",
              rec.evs.size() == 8, "n=" + std::to_string(rec.evs.size()));
        bool all_dma = !rec.evs.empty();
        for (const auto& ev : rec.evs)
            all_dma = all_dma && ev.source == EventSource::Dma;
        check("EVT-MEM-61", "each tagged source=Dma, not Cpu (the slot's DMA flag)",
              all_dma);
        check("EVT-MEM-62", "with the bytes the DMA moved",
              rec.evs.size() == 8 && rec.evs[0].value == 0xA0 &&
              rec.evs[7].value == 0xA7);
    }

    {
        // The site-level precise match is a COST gate — the drain re-matches, so
        // a spurious latch is never delivered — but what it buys is RING SPACE,
        // and that IS observable. `LD (0x9000),HL` writes 0x9000 and then 0x9001
        // inside ONE instruction; with the ring shrunk to one entry, a latch for
        // the unwatched 0x9000 would fill it and the watched 0x9001 would be
        // dropped.
        Emulator emu;
        //   8000  21 34 12     LD HL,0x1234
        //   8003  22 00 90     LD (0x9000),HL
        //   8006  18 FE        JR $
        build_armed(emu, { 0x21, 0x34, 0x12, 0x22, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9001; s.filter.hi = 0x9001;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_state().event_table()->shrink_ring_for_test(1);
        emu.run_frame();
        check("EVT-MEM-70", "a write in the armed SLOT that cannot match the filter "
                            "does not consume a ring entry",
              rec.evs.size() == 1 && rec.evs[0].addr == 0x9001 &&
              rec.evs[0].value == 0x12,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-MEM-71", "so nothing was dropped and the delivery is not flagged",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0);
    }

    // ── EVT-PORT — the port sites, and "after dispatch" for a read ───────
    {
        Emulator emu;
        //   8000  3E 07        LD A,0x07
        //   8002  D3 FE        OUT (0xFE),A      -> port 0x07FE
        //   8004  DB FE        IN  A,(0xFE)      -> port 0x07FE
        //   8006  18 FE        JR $
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::ReadWrite;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        check("EVT-PORT-01", "a Port subscription arms the port gate and no memory slot",
              emu.debug_state().port_watch_armed() &&
              !emu.debug_state().rd_watch_armed(0x0000));
        emu.run_frame();
        check("EVT-PORT-02", "the OUT and the IN are both delivered, in order",
              rec.evs.size() == 2 && rec.evs[0].access == Access::Write &&
              rec.evs[1].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 2) {
            check("EVT-PORT-03", "the payload carries the FULL 16-bit port, not the masked low byte",
                  rec.evs[0].port == 0x07FE, "port=" + hex(rec.evs[0].port));
            check("EVT-PORT-04", "the OUT carries the byte written",
                  rec.evs[0].value == 0x07);
            check("EVT-PORT-05", "and the writing instruction's PC",
                  rec.evs[0].pc == 0x8002, "pc=" + hex(rec.evs[0].pc));
            check("EVT-PORT-06", "the IN carries the value the guest RECEIVED "
                                 "(latched AFTER dispatch)",
                  rec.evs[1].value == emu.port().read(0x07FE),
                  "value=" + hex(rec.evs[1].value));
            check("EVT-PORT-07", "and the reading instruction's PC",
                  rec.evs[1].pc == 0x8004, "pc=" + hex(rec.evs[1].pc));
            check("EVT-PORT-08", "source is tagged Cpu for a CPU port access",
                  rec.evs[0].source == EventSource::Cpu);
        } else {
            check("EVT-PORT-03", "the port payload", false, "no deliveries");
        }
    }
    {
        // The control, and the direction filter: a WRITE-only subscription on a
        // port that is never written to.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::Write;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0x1F;   // Kempston
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        emu.run_frame();
        check("EVT-PORT-10", "a port the program never touches delivers nothing",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::Read;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        emu.run_frame();
        check("EVT-PORT-11", "a READ-only port subscription sees the IN and not the OUT",
              rec.evs.size() == 1 && rec.evs[0].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-NR — the ONE NextReg::write hook ─────────────────────────────
    {
        Emulator emu;
        //   8000  ED 91 15 07   NEXTREG 0x15,0x07
        //   8004  00            NOP
        //   8005  18 FE         JR $
        build_armed(emu, { 0xED, 0x91, 0x15, 0x07, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        // A NON-ZERO prior value, and that IS the row: on a fresh Emulator NR 0x15
        // reads 0, so `prev == before` was `0 == 0` and an `e.prev = 0` mutation
        // survived all 744 rows. `prev` is the field a handler needs in order to
        // undo a caught write, so nothing else constrains it for this kind.
        emu.nextreg().write(0x15, 0x5A);
        const uint8_t before = emu.nextreg().peek(0x15);
        Rec rec;
        std::vector<uint16_t> pc_at_delivery;
        Subscription s;
        s.kind = EventKind::NextRegWrite;
        s.filter.regs = { 0x15 };
        s.action = Action::Continue;
        s.handler = [&rec, &pc_at_delivery](const DbgEvent& ev, Debugger& d) {
            rec.evs.push_back(ev);
            pc_at_delivery.push_back(d.state().pc);
            return Action::Continue;
        };
        dbg.subscribe(2, s);
        emu.run_frame();
        check("EVT-NR-01", "a CPU NEXTREG write is delivered exactly once",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-NR-02", "with the register and the value",
                  rec.evs[0].reg == 0x15 && rec.evs[0].value == 0x07);
            check("EVT-NR-03", "and `prev`, peeked at the hook — asserted against a "
                                "NON-ZERO prior value, so a hard-coded zero cannot pass",
                  rec.evs[0].prev == before && before == 0x5A,
                  "prev=" + hex(rec.evs[0].prev) + " before=" + hex(before));
            check("EVT-NR-04", "source Cpu",
                  rec.evs[0].source == EventSource::Cpu);
            check("EVT-NR-05", "`pc` names the WRITER, not the delivery point",
                  rec.evs[0].pc == 0x8000, "pc=" + hex(rec.evs[0].pc));
            check("EVT-NR-06", "and the delivery really IS one instruction later "
                               "(§4.3: the CPU queue commits after the drain)",
                  !pc_at_delivery.empty() && pc_at_delivery[0] != rec.evs[0].pc,
                  "delivered at pc=" + hex(pc_at_delivery.empty() ? 0 : pc_at_delivery[0]));
        } else {
            check("EVT-NR-02", "the NR payload", false, "no deliveries");
        }
        check("EVT-NR-07", "and the register really took the value",
              emu.nextreg().peek(0x15) == 0x07);
    }
    {
        // The register-set filter's negative arm.
        Emulator emu;
        build_armed(emu, { 0xED, 0x91, 0x15, 0x07, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(2, s);
        emu.run_frame();
        check("EVT-NR-10", "a write to a register outside the set delivers nothing",
              rec.evs.empty());
    }

    // §4.2a — A DEBUGGER WRITE IS NOT AN EVENT. The gate is
    // `DebugState::guest_access()`, which is false outside the machine's own
    // execution and false inside a delivery's InspectionScope.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(2, s);

        check("EVT-MUT-01", "nextreg_write() from the backend succeeds",
              dbg.nextreg_write(2, 0x15, 0x0F) == Result::Ok &&
              emu.nextreg().peek(0x15) == 0x0F);
        emu.execute_single_instruction();      // reach a boundary drain
        check("EVT-MUT-02", "and fires NO NextRegWrite event on itself (§4.2a)",
              rec.evs.empty() && dbg.events_fired_since(0).empty());

        // The same for a panel's direct `nextreg().write()`, which is the call
        // site §4.2a names (nextreg_panel.cpp:174).
        emu.nextreg().write(0x15, 0x11);
        emu.execute_single_instruction();
        check("EVT-MUT-03", "a panel's own nextreg().write() fires nothing either",
              rec.evs.empty());

        // And the memory half: `poke(Cpu)` must not latch a watch.
        Rec mrec;
        Subscription m;
        m.kind = EventKind::Mem; m.access = Access::ReadWrite;
        m.filter.lo = 0x9000; m.filter.hi = 0x9000;
        m.action = Action::Stop; m.handler = recorder(mrec);
        dbg.subscribe(2, m);
        const uint8_t poked = 0x77;
        check("EVT-MUT-04", "poke() succeeds",
              dbg.poke(2, MemSpace::cpu(), 0x9000, 1, &poked).status == Result::Ok &&
              emu.mmu().peek(0x9000) == 0x77);
        uint8_t got = 0;
        dbg.peek(MemSpace::cpu(), 0x9000, 1, &got);
        check("EVT-MUT-05", "peek() succeeds",
              got == 0x77);
        emu.execute_single_instruction();
        check("EVT-MUT-06", "and NEITHER the poke nor the peek latched a Mem event",
              mrec.evs.empty() && !dbg.state().paused,
              "n=" + std::to_string(mrec.evs.size()));
        check("EVT-MUT-07", "nor did either raise the legacy data-breakpoint latch",
              !emu.debug_state().data_bp_hit());
    }

    // ── EVT-EXEC — the pre-instruction gate, the only kind delivered EARLY ─
    {
        Emulator emu;
        //   8000  00            NOP
        //   8001  3E 01         LD A,1
        //   8003  32 00 90      LD (0x9000),A
        //   8006  18 FE         JR $
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-01", "an Execute subscription fires once for the address",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (!rec.evs.empty()) {
            check("EVT-EXEC-02", "with that PC and the page behind it",
                  rec.evs[0].pc == 0x8001 &&
                  rec.evs[0].phys_page == emu.mmu().get_effective_page(4));
            check("EVT-EXEC-03", "and its id and owner",
                  rec.evs[0].id == sub.value && rec.evs[0].owner == 6);
        }
        check("EVT-EXEC-04", "a Continue verdict let the program finish",
              emu.mmu().peek(0x9000) == 0x01 && pc_of(emu) == 0x8006);
    }
    {
        // THE PRE-INSTRUCTION PROPERTY: a handler that sets PC redirects before
        // the instruction at that PC runs (§4.2a). The two instructions it skips
        // are what writes 0x9000, so the memory says whether the redirect landed.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue;
        s.handler = [](const DbgEvent&, Debugger& d) {
            d.set_register(1, RegId::PC, 0x8006);
            return Action::Continue;
        };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-10", "an Execute handler's PC write redirects BEFORE the "
                             "instruction at that PC runs",
              emu.mmu().peek(0x9000) == 0x00 && pc_of(emu) == 0x8006,
              "mem=" + hex(emu.mmu().peek(0x9000)) + " pc=" + hex(pc_of(emu)));
    }
    {
        // A Stop, and its reason: `Breakpoint`, because the kind is Execute.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8003; s.filter.hi = 0x8003;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(8, s);
        emu.run_frame();
        check("EVT-EXEC-20", "a Stop pauses BEFORE the instruction, not after it",
              dbg.state().paused && pc_of(emu) == 0x8003 &&
              emu.mmu().peek(0x9000) == 0x00,
              "pc=" + hex(pc_of(emu)));
        check("EVT-EXEC-21", "and the reason is Breakpoint, naming the subscription",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 8);
        check("EVT-EXEC-22", "a resume steps off it (GH #221) and the program completes",
              dbg.run(8) == Result::Ok);
        emu.run_frame();
        check("EVT-EXEC-23", "so the machine does not re-pause on the same address",
              !dbg.state().paused && emu.mmu().peek(0x9000) == 0x01 &&
              pc_of(emu) == 0x8006, "pc=" + hex(pc_of(emu)));
    }
    {
        // `once`: disabled after the first ACCEPTED firing, not after the first
        // match. The loop reaches the address three times.
        Emulator emu;
        //   8000  06 03        LD B,3
        //   8002  10 FE        DJNZ $           (loops at 0x8002)
        //   8004  18 FA        JR 0x8000        (and starts over — see below)
        //
        // The final JR goes back to the TOP, not to itself: with `JR $` the
        // program parked at 0x8004 after the first frame and a second frame never
        // reached 0x8002 again, so EVT-EXEC-33's re-enable row could not tell a
        // spent `once` from a subscription that simply never matched. A mutation
        // that let a re-enabled `once` fire again survived because of it.
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFA });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-30", "a `once` subscription fires exactly once over three passes",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-31", "and reports itself disabled afterwards",
              !dbg.subscriptions(true).empty() &&
              !dbg.subscriptions(true).front().enabled &&
              !dbg.subscriptions(true).front().live);
        check("EVT-EXEC-32", "re-enabling a spent `once` does NOT buy a second firing",
              dbg.set_enabled(6, sub.value, true) == Result::Ok);
        emu.run_frame();
        check("EVT-EXEC-33", "  (still one delivery, over a frame that passes the "
                             "address hundreds of times)",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-34", "and the control: a NON-`once` subscription on the same "
                             "address fires on every pass",
              true);
        {
            Rec many;
            Subscription m = s;
            m.once = false; m.handler = recorder(many);
            dbg.subscribe(6, m);
            emu.run_frame();
            check("EVT-EXEC-35", "  (so the loop really does pass 0x8002 repeatedly — "
                                 "without this the row above proves nothing)",
                  many.evs.size() > 3, "n=" + std::to_string(many.evs.size()));
        }
    }
    {
        // A `Condition` gates the firing, and it runs only AFTER the filter
        // matched. `once` must not be spent by a rejected condition.
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        int cond_calls = 0;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue; s.handler = recorder(rec);
        s.condition = [&cond_calls](const DbgEvent&, const Debugger& d) {
            ++cond_calls;
            // Fire only on the LAST pass, when B has come down to 1.
            return (d.registers().BC >> 8) == 1;
        };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-40", "the condition ran once per filter match",
              cond_calls == 3, "calls=" + std::to_string(cond_calls));
        check("EVT-EXEC-41", "the handler ran only for the pass it accepted",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-42", "and `once` was spent by the ACCEPTED firing, not the first match",
              rec.evs.size() == 1 && (rec.evs[0].id != 0));
    }
    {
        // A handler's verdict OVERRIDES the static action, in both directions.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Stop;
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Continue; };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-50", "a handler returning Continue overrides a static Stop",
              !dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue;
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Stop; };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-51", "and a handler returning Stop overrides a static Continue",
              dbg.state().paused && pc_of(emu) == 0x8001,
              "pc=" + hex(pc_of(emu)));
    }

    {
        // THE STEP PATH. Every row above drives the machine through
        // `run_frame()`; `debugger_step()` -> `step_frame_slot()` is a second
        // root with its own drain, and dropping that drain left the whole suite
        // green. A Step is the path a user exercises most deliberately.
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A
        //   8005  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        dbg.step_into(1);                       // over the LD A
        check("EVT-STEP-01", "a step that touches nothing delivers nothing",
              rec.evs.empty() && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
        dbg.step_into(1);                       // over the LD (0x9000),A
        check("EVT-STEP-02", "a step over the watched write DELIVERS it — the Step "
                             "path has its own boundary drain",
              rec.evs.size() == 1 && rec.evs[0].addr == 0x9000 &&
              rec.evs[0].value == 0x5A,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-STEP-03", "and the machine is paused where the step left it",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-PROBE — probe_execute() over BOTH models ─────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-PROBE-01", "nothing armed -> false everywhere",
              !dbg.probe_execute(0x8000) && !dbg.probe_execute(0x0000));
        Subscription s;
        s.kind = EventKind::Execute; s.filter.lo = 0x1000; s.filter.hi = 0x1FFF;
        const auto sub = dbg.subscribe(1, s);
        check("EVT-PROBE-02", "an Execute RANGE answers true across the whole range",
              dbg.probe_execute(0x1000) && dbg.probe_execute(0x17FF) &&
              dbg.probe_execute(0x1FFF));
        check("EVT-PROBE-03", "and false outside it",
              !dbg.probe_execute(0x0FFF) && !dbg.probe_execute(0x2000));
        check("EVT-PROBE-04", "a disabled subscription answers false",
              dbg.set_enabled(1, sub.value, false) == Result::Ok &&
              !dbg.probe_execute(0x1000));
        dbg.set_enabled(1, sub.value, true);
        Subscription tr;
        tr.kind = EventKind::Execute; tr.filter.lo = 0x5000; tr.filter.hi = 0x5000;
        tr.transient = true;
        dbg.subscribe(1, tr);
        check("EVT-PROBE-05", "a TRANSIENT subscription counts — the step-off arm needs it",
              dbg.probe_execute(0x5000));
        emu.debug_state().breakpoints().add_pc(0x7000);
        check("EVT-PROBE-06", "and the legacy PC-breakpoint model still counts too",
              dbg.probe_execute(0x7000));
        Subscription pg;
        pg.kind = EventKind::Execute; pg.filter.lo = 0x9000; pg.filter.hi = 0x9000;
        pg.filter.page = 0xFE;      // a page no slot holds
        dbg.subscribe(1, pg);
        check("EVT-PROBE-07", "a page qualifier that no slot satisfies answers false",
              !dbg.probe_execute(0x9000));
    }

    // ── EVT-TIME — Frame, Scanline, Cycle ────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Frame; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        emu.run_frame();
        emu.run_frame();
        check("EVT-TIME-01", "a Frame subscription fires once per frame",
              rec.evs.size() == 3, "n=" + std::to_string(rec.evs.size()));
        check("EVT-TIME-02", "carrying the PRE-INCREMENT tag time().frame reports (F2)",
              rec.evs.size() == 3 && rec.evs[0].frame == 0 &&
              rec.evs[1].frame == 1 && rec.evs[2].frame == 2,
              rec.evs.size() == 3 ? std::to_string(rec.evs[0].frame) + "," +
                                        std::to_string(rec.evs[1].frame) + "," +
                                        std::to_string(rec.evs[2].frame)
                                  : "");
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Frame; s.filter.frame = 1;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame(); emu.run_frame(); emu.run_frame();
        check("EVT-TIME-03", "a Frame filter on one number fires only for that frame",
              rec.evs.size() == 1 && rec.evs[0].frame == 1,
              "n=" + std::to_string(rec.evs.size()));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        emu.run_frame();
        check("EVT-TIME-10", "a Scanline subscription fires once per frame for its line",
              rec.evs.size() == 2, "n=" + std::to_string(rec.evs.size()));
        check("EVT-TIME-11", "and the payload's cvc IS the line it matched",
              rec.evs.size() == 2 && rec.evs[0].cvc == 100 && rec.evs[1].cvc == 100,
              rec.evs.empty() ? "" : std::to_string(rec.evs[0].cvc));
        check("EVT-TIME-12", "with the raw frame counters alongside, not instead",
              !rec.evs.empty() && rec.evs[0].vc != rec.evs[0].cvc,
              rec.evs.empty() ? "" : "vc=" + std::to_string(rec.evs[0].vc));
        check("EVT-TIME-13", "in successive frames",
              rec.evs.size() == 2 && rec.evs[1].frame == rec.evs[0].frame + 1);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 30000;   // no such line
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-14", "a line that does not exist fires nothing",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        const uint64_t target = emu.clock().get() + 5000;
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = target;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-20", "a Cycle subscription stops the machine at or past its target",
              dbg.state().paused && emu.clock().get() >= target,
              "clock=" + std::to_string(emu.clock().get()));
        check("EVT-TIME-21", "and not far past it (it is checked every boundary)",
              emu.clock().get() < target + 2000);
        check("EVT-TIME-22", "the reason is Script — a subscriber's explicit stop",
              dbg.state().pause_reason.kind == PauseReason::Kind::Script &&
              dbg.state().pause_reason.id == sub.value);
    }

    // ── EVT-RESET — and the F8 `Any` filter, end to end ──────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Any;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_latch_reset(/*hard=*/false);
        emu.execute_single_instruction();
        check("EVT-RESET-01", "ResetKind::Any delivers a SOFT reset",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Soft,
              "n=" + std::to_string(rec.evs.size()));
        emu.debug_latch_reset(/*hard=*/true);
        emu.execute_single_instruction();
        check("EVT-RESET-02", "and a HARD one, from the same one subscription",
              rec.evs.size() == 2 && rec.evs[1].reset_kind == ResetKind::Hard,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-RESET-03", "reset(Any) is still REFUSED — 'either way' is not a reset",
              dbg.reset(1, ResetKind::Any) == Result::Unsupported);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Hard;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_latch_reset(false);
        emu.execute_single_instruction();
        check("EVT-RESET-10", "a Hard-only subscription ignores a soft reset",
              rec.evs.empty());
        emu.debug_latch_reset(true);
        emu.execute_single_instruction();
        check("EVT-RESET-11", "and takes the hard one",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Hard);
    }
    {
        // The real seam: Emulator::soft_reset() latches it, before init() resets
        // the clock (so the event is stamped where it happened).
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        emu.run_frame();
        const uint64_t before = emu.clock().get();
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Soft;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.soft_reset();
        emu.debug_state().set_active(true);      // init() does not clear it, but be explicit
        emu.execute_single_instruction();
        check("EVT-RESET-20", "Emulator::soft_reset() really raises Reset{Soft}",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Soft,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-RESET-21", "stamped at the cycle the reset happened, not at cycle 0",
              rec.evs.size() == 1 && rec.evs[0].cycle >= before,
              rec.evs.empty() ? "" : std::to_string(rec.evs[0].cycle) + " vs " +
                                         std::to_string(before));
    }

    // ── EVT-INT / EVT-NMI — the two accept seams ─────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x76, 0x18, 0xFD });    // HALT, then JR $
        {
            Z80Registers r = emu.cpu().get_registers();
            r.IFF1 = 1; r.IFF2 = 1; r.IM = 1;
            emu.cpu().set_registers(r);
        }
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::IntAck; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-INT-01", "an accepted interrupt raises IntAck",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-INT-02", "carrying the interrupt mode in force",
              !rec.evs.empty() && rec.evs[0].int_mode == 1,
              rec.evs.empty() ? "" : std::to_string(rec.evs[0].int_mode));
        check("EVT-INT-03", "and the vector byte the fabric drove",
              !rec.evs.empty() && rec.evs[0].int_vector == 0xFF,
              rec.evs.empty() ? "" : hex(rec.evs[0].int_vector));
    }
    {
        // The control: interrupts disabled, so nothing is ever accepted.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::IntAck; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-INT-04", "with IFF1 clear no interrupt is accepted and nothing fires",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.nmi_source().set_divmmc_enable(true);     // NR 0x06 bit 4
        emu.nmi_source().strobe_divmmc_button();
        for (int i = 0; i < 40 && rec.evs.empty(); ++i)
            emu.execute_single_instruction();
        check("EVT-NMI-01", "a DivMMC-button NMI raises Nmi",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-NMI-02", "reported as the DRIVE button (NmiSource::Src::DivMmc)",
              !rec.evs.empty() && rec.evs[0].nmi_source == NmiButton::Drive);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.nmi_source().set_mf_enable(true);         // NR 0x06 bit 3
        emu.nmi_source().strobe_mf_button();
        for (int i = 0; i < 40 && rec.evs.empty(); ++i)
            emu.execute_single_instruction();
        check("EVT-NMI-03", "a Multiface-button NMI raises Nmi",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-NMI-04", "reported as the MF button",
              !rec.evs.empty() && rec.evs[0].nmi_source == NmiButton::Mf);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        for (int i = 0; i < 20; ++i) emu.execute_single_instruction();
        check("EVT-NMI-05", "with no button pressed nothing fires",
              rec.evs.empty());
    }

    // ── EVT-MAGIC — the opcode, the event, and CTL-13's unowned reason ────
    {
        Emulator emu;
        //   8000  ED FF        the ZEsarUX magic opcode
        //   8002  18 FE        JR $
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-MAGIC-01", "the magic opcode pauses the machine",
              dbg.state().paused);
        check("EVT-MAGIC-02", "and the pause reason is Magic",
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("EVT-MAGIC-03", "UNOWNED — no client's detach may resume it",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
        check("EVT-MAGIC-04", "naming the address it fired at",
              dbg.state().pause_reason.addr == 0x8000,
              hex(dbg.state().pause_reason.addr));
        // The EVENT, as distinct from the reason. Dropping the `Magic` latch left
        // every row above green, because the reason comes from
        // `note_magic_stop()` and not from the subscription.
        check("EVT-MAGIC-08", "and the Magic EVENT is delivered to its subscriber",
              rec.evs.size() == 1 && rec.evs[0].kind == EventKind::Magic,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-MAGIC-09", "carrying the PC of the magic opcode",
              rec.evs.size() == 1 && rec.evs[0].pc == 0x8000,
              rec.evs.empty() ? "" : hex(rec.evs[0].pc));
    }
    {
        // The reason is reported with NO Magic subscription at all — the latch is
        // unconditional, because §4.2's closed set has a value for it either way.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        emu.run_frame();
        check("EVT-MAGIC-05", "the Magic reason needs no subscription",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("EVT-MAGIC-06", "and a resume clears it",
              dbg.run(1) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }
    {
        // The control: the opcode is a NOP with the breakpoint disarmed, and no
        // Magic event fires.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-MAGIC-07", "with the magic breakpoint disarmed nothing fires and "
                              "the machine runs on",
              rec.evs.empty() && !dbg.state().paused && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-HOST — raise_host_event ──────────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Continue;
        std::strcpy(s.filter.host_name, "script3");
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-HOST-01", "raising the subscribed name returns Ok",
              dbg.raise_host_event(1, "script3") == Result::Ok);
        check("EVT-HOST-02", "and is delivered synchronously, with the name",
              rec.evs.size() == 1 &&
              std::string(rec.evs[0].host_name) == "script3",
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-HOST-03", "raising a DIFFERENT name delivers nothing",
              dbg.raise_host_event(1, "script4") == Result::Ok &&
              rec.evs.size() == 1);
        check("EVT-HOST-04", "a name longer than MAX_HOST_EVENT_NAME is refused, "
                             "never truncated",
              dbg.raise_host_event(1, std::string(jnext::dbg::MAX_HOST_EVENT_NAME + 1,
                                                  'x')) == Result::Unsupported);
        check("EVT-HOST-05", "a name of exactly MAX_HOST_EVENT_NAME is accepted",
              dbg.raise_host_event(1, std::string(jnext::dbg::MAX_HOST_EVENT_NAME,
                                                  'x')) == Result::Ok);
        check("EVT-HOST-06", "and it is recorded in the delivery history",
              dbg.events_fired_since(0).size() == 1);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-HOST-10", "raising with NO Host subscription is Ok and inert",
              dbg.raise_host_event(1, "script1") == Result::Ok &&
              dbg.events_fired_since(0).empty());
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Stop;
        // NO HANDLER, deliberately: `recorder()` returns Continue, and a
        // handler's verdict OVERRIDES the static action — a recorder here would
        // quietly turn this Stop into a Continue. The delivery is observed
        // through the history instead.
        dbg.subscribe(1, s);
        check("EVT-HOST-11", "an empty filter name matches any raised name",
              dbg.raise_host_event(1, "anything") == Result::Ok &&
              dbg.events_fired_since(0).size() == 1);
        // NO pause() first, deliberately: `state()`'s precedence puts the ARMED
        // VERB ahead of the event latch, so a user pause would (correctly) be
        // reported instead and this row would be asserting the wrong thing.
        check("EVT-HOST-12", "and a Stop action pauses the machine, reason Script",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Script);
    }

    // ── EVT-COP — the three Copper sites, and the MOVE fan-out ───────────
    //
    // The Copper is driven the way the real machine drives it: NR 0x60-0x64 are
    // wired to it by Emulator::init(), so the program is uploaded through the
    // register file and the engine is ticked by the per-instruction device
    // cluster.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec cop, nr;
        Subscription cs;
        cs.kind = EventKind::Copper;
        cs.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        cs.action = Action::Continue; cs.handler = recorder(cop);
        dbg.subscribe(1, cs);
        Subscription ns;
        ns.kind = EventKind::NextRegWrite; ns.filter.regs = { 0x30 };
        ns.action = Action::Continue; ns.handler = recorder(nr);
        dbg.subscribe(1, ns);
        check("EVT-COP-01", "a Copper subscription arms the engine's own flag",
              emu.copper().events_armed());

        copper_program(emu, { move_word(0x30, 0x7F), HALT_WORD });
        copper_start(emu);
        // TWO slots, and that is the §4.3 contract rather than a fudge: the
        // Copper runs inside `tick_devices_after_instruction`, which is AFTER
        // this slot's boundary drain, so its latches are delivered at the NEXT
        // boundary — "≤1 instruction late", exactly as for a CPU NR write.
        emu.execute_single_instruction();
        emu.execute_single_instruction();

        check("EVT-COP-02", "the MOVE is delivered as Copper{Move}",
              cop.evs.size() == 1 &&
              cop.evs[0].copper_kind == jnext::dbg::CopperEventKind::Move,
              "n=" + std::to_string(cop.evs.size()));
        if (cop.evs.size() == 1) {
            check("EVT-COP-03", "with the register and the value the MOVE carried",
                  cop.evs[0].reg == 0x30 && cop.evs[0].value == 0x7F);
            check("EVT-COP-04", "the Copper PC of the MOVE, not of the next instruction",
                  cop.evs[0].copper_pc == 0);
            check("EVT-COP-05", "source Copper",
                  cop.evs[0].source == EventSource::Copper);
            check("EVT-COP-06", "and the 7 MHz hc_ula / cvc pair the Copper itself "
                                "compares against (GH #181), not the raw counters",
                  cop.evs[0].hc_ula >= 0 && cop.evs[0].cvc >= 0 &&
                  cop.evs[0].hc_ula != cop.evs[0].hc);
        }
        check("EVT-COP-07", "and the SAME latch entry also fed NextRegWrite{source=Copper}",
              nr.evs.size() == 1 && nr.evs[0].source == EventSource::Copper &&
              nr.evs[0].reg == 0x30 && nr.evs[0].value == 0x7F,
              "n=" + std::to_string(nr.evs.size()));
        check("EVT-COP-08", "exactly ONCE — a Copper MOVE is one ring entry, "
                            "fanned out at the drain, never two",
              nr.evs.size() == 1);
        check("EVT-COP-09", "and the register really took the value",
              emu.nextreg().peek(0x30) == 0x7F);
    }
    {
        // The HALT edge: is_halt() had no caller at all before B2, and a latch
        // per stalled cycle would fill the ring by itself.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        copper_program(emu, { HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();     // the drain is one slot later
        check("EVT-COP-20", "the HALT form latches exactly once, on the EDGE",
              rec.evs.size() == 1 &&
              rec.evs[0].copper_kind == jnext::dbg::CopperEventKind::Halt,
              "n=" + std::to_string(rec.evs.size()));
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-21", "and NOT again for every stalled cycle after it",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-COP-22", "at the HALT's own Copper PC",
              !rec.evs.empty() && rec.evs[0].copper_pc == 0);
        // Restarting re-arms the edge. Through Copper::reset(), and NOT through
        // mode 0 -> mode 1: `tick_copper_for_master_cycles` early-returns on
        // `!is_running()`, so a stopped Copper is never ticked, never sees the
        // mode edge and never leaves the HALT it is standing on — so the edge
        // staying set across a stop/start with no tick is CORRECT, not a defect.
        // reset() clears pc_, mode_, last_mode_ and the edge together, and leaves
        // the instruction RAM alone (dpram2 has no reset port,
        // zxnext.vhd:3959-3996), which is why the program survives it.
        emu.copper().reset();
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-23", "a restarted program reaches the HALT and latches again",
              rec.evs.size() == 2, "n=" + std::to_string(rec.evs.size()));
    }
    {
        // A satisfied WAIT. Given a whole frame the Copper's cvc sweeps every
        // line, so a WAIT on any line in range is satisfied exactly once.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        Rec mv;
        Subscription ms;
        ms.kind = EventKind::Copper;
        ms.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        ms.action = Action::Continue; ms.handler = recorder(mv);
        dbg.subscribe(1, ms);
        // The WAIT is at word ONE, not word zero: with it at zero, a payload that
        // hard-coded `copper_pc = 0` would be indistinguishable from the truth,
        // and a mutation that did exactly that survived the suite.
        copper_program(emu, { move_word(0x31, 0x01), wait_word(4, 40),
                              move_word(0x30, 0x21), HALT_WORD });
        copper_start(emu);
        emu.run_frame();
        check("EVT-COP-30", "a satisfied WAIT is delivered as Copper{Wait}",
              rec.evs.size() == 1 &&
              rec.evs[0].copper_kind == jnext::dbg::CopperEventKind::Wait,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-COP-31", "at the WAIT's own Copper PC, before the advance",
                  rec.evs[0].copper_pc == 1,
                  std::to_string(rec.evs[0].copper_pc));
            check("EVT-COP-32", "carrying the WAIT's target line",
                  rec.evs[0].wait_vpos == 40);
            check("EVT-COP-33", "and its horizontal threshold as copper.vhd computes "
                                "it — (hpos << 3) + 12",
                  rec.evs[0].wait_hpos_threshold == (4 << 3) + 12,
                  std::to_string(rec.evs[0].wait_hpos_threshold));
            check("EVT-COP-34", "and the cvc it was satisfied at IS the target line",
                  rec.evs[0].cvc == 40, std::to_string(rec.evs[0].cvc));
            check("EVT-COP-35", "with hc_ula at or past the threshold",
                  rec.evs[0].hc_ula >= rec.evs[0].wait_hpos_threshold);
        }
        check("EVT-COP-36", "and the MOVE past it ran",
              emu.nextreg().peek(0x30) == 0x21);
        // The MOVE's OWN hc_ula / cvc pair. The WAIT rows above pin theirs, but
        // `latch_move_` takes the two as separate arguments and a mutation that
        // SWAPPED them survived — nothing asserted a Move's cvc against a value
        // the program itself fixes. The MOVE at word 2 runs on the cycle after
        // the WAIT at word 1 was satisfied, so it is on the same Copper line.
        check("EVT-COP-37", "both MOVEs were delivered, at their own Copper PCs",
              mv.evs.size() == 2 && mv.evs[0].copper_pc == 0 &&
              mv.evs[1].copper_pc == 2,
              "n=" + std::to_string(mv.evs.size()));
        check("EVT-COP-38", "and the MOVE released by the WAIT reports the WAIT's line "
                            "in cvc, with hc_ula at or past its threshold",
              mv.evs.size() == 2 && mv.evs[1].cvc == 40 &&
              mv.evs[1].hc_ula >= (4 << 3) + 12,
              mv.evs.size() == 2 ? "cvc=" + std::to_string(mv.evs[1].cvc) +
                                       " hc_ula=" + std::to_string(mv.evs[1].hc_ula)
                                 : "");
    }
    {
        // A stall is NOT a Halt: `is_halt()` is what tells an ordinary
        // unsatisfied WAIT from the HALT form, and a mutation that dropped it
        // survived — the Wait rows above subscribe to Wait, not Halt.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        // A WAIT on line 40; the Copper's cvc is nowhere near it at the start of
        // a frame, so it stalls for the whole slot.
        copper_program(emu, { wait_word(0, 40) });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-25", "an ordinary unsatisfied WAIT stalls without raising Halt",
              rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
    }
    {
        // The no-subscriber control: the engine flag is off, so not one of the
        // three sites latches, and the Copper still does its job.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-COP-40", "with no Copper/NR subscription the engine flag is clear",
              !emu.copper().events_armed());
        copper_program(emu, { move_word(0x30, 0x55), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        check("EVT-COP-41", "nothing is delivered",
              dbg.events_fired_since(0).empty());
        check("EVT-COP-42", "and the MOVE still happened",
              emu.nextreg().peek(0x30) == 0x55);
    }
    {
        // A NextRegWrite subscription ALONE must arm the Copper site, or a
        // Copper MOVE to a watched register would go unreported.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription ns;
        ns.kind = EventKind::NextRegWrite; ns.filter.source = EventSource::Copper;
        ns.action = Action::Continue; ns.handler = recorder(rec);
        dbg.subscribe(1, ns);
        check("EVT-COP-50", "an NR subscription alone arms the Copper engine flag",
              emu.copper().events_armed());
        copper_program(emu, { move_word(0x30, 0x66), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();     // the drain is one slot later
        check("EVT-COP-51", "and the MOVE is reported as NextRegWrite{source=Copper}",
              rec.evs.size() == 1 && rec.evs[0].source == EventSource::Copper,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-DMA — the three DMA sites ────────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        Rec st, en, by;
        Subscription s1; s1.kind = EventKind::Dma;
        s1.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
        s1.action = Action::Continue; s1.handler = recorder(st);
        Subscription s2; s2.kind = EventKind::Dma;
        s2.filter.dma_kind = jnext::dbg::DmaEventKind::End;
        s2.action = Action::Continue; s2.handler = recorder(en);
        Subscription s3; s3.kind = EventKind::Dma;
        s3.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        s3.action = Action::Continue; s3.handler = recorder(by);
        dbg.subscribe(1, s1); dbg.subscribe(1, s2); dbg.subscribe(1, s3);
        check("EVT-DMA-01", "subscribing to all three arms BOTH engine flags",
              emu.dma().events_armed() && emu.dma().byte_events_armed());

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();

        check("EVT-DMA-02", "the R6 0x87 enable raises exactly one Start",
              st.evs.size() == 1, "n=" + std::to_string(st.evs.size()));
        if (st.evs.size() == 1) {
            check("EVT-DMA-03", "with the block's source, destination and length",
                  st.evs[0].dma_src == 0xA000 && st.evs[0].dma_dst == 0x9000 &&
                  st.evs[0].dma_length == 8,
                  hex(st.evs[0].dma_src) + "->" + hex(st.evs[0].dma_dst) + " len " +
                      std::to_string(st.evs[0].dma_length));
            check("EVT-DMA-04", "and source=Dma",
                  st.evs[0].source == EventSource::Dma);
        }
        check("EVT-DMA-05", "one Byte per transferred byte",
              by.evs.size() == 8, "n=" + std::to_string(by.evs.size()));
        if (by.evs.size() == 8) {
            check("EVT-DMA-06", "each carrying the addresses THAT byte used, "
                                "before the increments",
                  by.evs[0].dma_src == 0xA000 && by.evs[0].dma_dst == 0x9000 &&
                  by.evs[7].dma_src == 0xA007 && by.evs[7].dma_dst == 0x9007);
            check("EVT-DMA-07", "and the byte itself",
                  by.evs[0].value == 0xA0 && by.evs[7].value == 0xA7);
            check("EVT-DMA-08", "a memory-to-memory transfer flags neither endpoint as I/O",
                  !by.evs[0].dma_is_io_src && !by.evs[0].dma_is_io_dst);
        }
        check("EVT-DMA-09", "and exactly one End at block completion",
              en.evs.size() == 1, "n=" + std::to_string(en.evs.size()));
        check("EVT-DMA-10", "reporting the bytes the BLOCK actually moved",
              en.evs.size() == 1 && en.evs[0].dma_bytes == 8,
              en.evs.empty() ? "" : std::to_string(en.evs[0].dma_bytes));
        check("EVT-DMA-11", "and the transfer really ran",
              emu.mmu().peek(0x9007) == 0xA7);
    }
    {
        // §4.3's per-engine cost rule: subscribing to Start/End must NOT arm the
        // per-byte site, and subscribing to Byte must not arm the other.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s; s.kind = EventKind::Dma;
        s.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
        const auto sub = dbg.subscribe(1, s);
        check("EVT-DMA-20", "a Start subscription arms Start/End and NOT the per-byte site",
              emu.dma().events_armed() && !emu.dma().byte_events_armed());
        dbg.unsubscribe(1, sub.value);
        Subscription b; b.kind = EventKind::Dma;
        b.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        dbg.subscribe(1, b);
        check("EVT-DMA-21", "and a Byte subscription arms the per-byte site and NOT the other",
              !emu.dma().events_armed() && emu.dma().byte_events_armed());
    }
    {
        // The Byte range filter's either-endpoint rule, on a real transfer.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i), 0x5A);
        Rec rec;
        Subscription b; b.kind = EventKind::Dma;
        b.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        b.filter.lo = 0x9002; b.filter.hi = 0x9004;   // DESTINATION-side window
        b.action = Action::Continue; b.handler = recorder(rec);
        dbg.subscribe(1, b);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        check("EVT-DMA-30", "a Byte range matches on the DESTINATION endpoint too",
              rec.evs.size() == 3, "n=" + std::to_string(rec.evs.size()));
        check("EVT-DMA-31", "and only for the bytes inside it",
              rec.evs.size() == 3 && rec.evs[0].dma_dst == 0x9002 &&
              rec.evs[2].dma_dst == 0x9004);
    }

    // ── EVT-OVF — the OVERFLOW CONTRACT, driven on purpose ───────────────
    //
    // §4.3 is explicit that this is "a specified, tested behaviour, not
    // 'unreachable'": the ring keeps the first N entries IN ORDER plus a dropped
    // count, the drain delivers those N, and every delivery of that boundary is
    // marked `overflowed{dropped}`. The ring is shrunk through the test hook and
    // a Copper MOVE BURST is driven over it — which is the path §4.3 names.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);

        jnext::dbg::EventTable* tbl = emu.debug_state().event_table();
        check("EVT-OVF-01", "the Debugger published its table to DebugState",
              tbl != nullptr);
        tbl->shrink_ring_for_test(4);
        check("EVT-OVF-02", "and the ring is now four entries deep",
              tbl->ring_capacity() == 4);

        // Sixteen consecutive MOVEs. One instruction slot at 3.5 MHz is 32
        // master cycles and a MOVE takes two of them, so the burst runs well
        // past four within the slot.
        std::vector<uint16_t> prog;
        for (int i = 0; i < 16; ++i)
            prog.push_back(move_word(0x30, static_cast<uint8_t>(0x10 + i)));
        prog.push_back(HALT_WORD);
        copper_program(emu, prog);
        copper_start(emu);
        emu.execute_single_instruction();     // the burst latches here
        emu.execute_single_instruction();     // and is drained here

        check("EVT-OVF-03", "exactly the ring's capacity is delivered, no more",
              rec.evs.size() == 4, "n=" + std::to_string(rec.evs.size()));
        check("EVT-OVF-04", "they are the FIRST four, in order, not the last four",
              rec.evs.size() == 4 && rec.evs[0].copper_pc == 0 &&
              rec.evs[1].copper_pc == 1 && rec.evs[2].copper_pc == 2 &&
              rec.evs[3].copper_pc == 3);
        check("EVT-OVF-05", "carrying the first four MOVEs' own values",
              rec.evs.size() == 4 && rec.evs[0].value == 0x10 &&
              rec.evs[3].value == 0x13);
        bool all_flagged = rec.evs.size() == 4;
        uint16_t dropped = rec.evs.empty() ? 0 : rec.evs[0].dropped;
        for (const auto& ev : rec.evs)
            all_flagged = all_flagged && ev.overflowed && ev.dropped == dropped;
        check("EVT-OVF-06", "EVERY delivery of that boundary is marked overflowed",
              all_flagged);
        check("EVT-OVF-07", "with the same non-zero dropped count on each",
              dropped > 0, "dropped=" + std::to_string(dropped));
        check("EVT-OVF-08", "and the MOVEs that were dropped still HAPPENED — the "
                            "ring bounds the reporting, never the emulation",
              emu.nextreg().peek(0x30) > 0x13,
              hex(emu.nextreg().peek(0x30)));

        // The next boundary is clean again: the flag is per boundary, not sticky.
        rec.evs.clear();
        tbl->shrink_ring_for_test(jnext::dbg::LATCH_RING_CAPACITY);
        emu.copper().reset();                  // see EVT-COP-23 for why reset()
        copper_program(emu, { move_word(0x31, 0x01), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-OVF-09", "the next boundary is not flagged — `overflowed` is per "
                            "boundary, not sticky",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-SUB — ownership, the switches, and the model ─────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s; s.kind = EventKind::Execute;
        const auto a = dbg.subscribe(1, s);
        const auto b = dbg.subscribe(2, s);
        check("EVT-SUB-01", "two clients get two distinct ids",
              a.status == Result::Ok && b.status == Result::Ok &&
              a.value != b.value);
        check("EVT-SUB-02", "a client may not unsubscribe another's id",
              dbg.unsubscribe(1, b.value) == Result::RefusedUnavailable);
        check("EVT-SUB-03", "and that id is still there",
              dbg.subscriptions(true).size() == 2);
        check("EVT-SUB-04", "an unknown id is refused the same way (no session leak)",
              dbg.unsubscribe(1, 99999) == Result::RefusedUnavailable);
        check("EVT-SUB-05", "a client may not enable/disable another's id",
              dbg.set_enabled(1, b.value, false) == Result::RefusedUnavailable);
        check("EVT-SUB-06", "its owner may",
              dbg.set_enabled(2, b.value, false) == Result::Ok);
        check("EVT-SUB-07", "and set_enabled on an unknown id is refused",
              dbg.set_enabled(1, 99999, false) == Result::RefusedUnavailable);
        check("EVT-SUB-08", "its owner may unsubscribe it",
              dbg.unsubscribe(2, b.value) == Result::Ok &&
              dbg.subscriptions(true).size() == 1);
        check("EVT-SUB-09", "unsubscribing it twice is refused, not a crash",
              dbg.unsubscribe(2, b.value) == Result::RefusedUnavailable);

        Subscription bad; bad.kind = EventKind::Mem; bad.access = Access::None;
        check("EVT-SUB-10", "a Mem subscription with Access::None is REFUSED, not "
                            "stored as a silent no-op",
              dbg.subscribe(1, bad).status == Result::RefusedUnavailable);
        bad.kind = EventKind::Port;
        check("EVT-SUB-11", "and so is a Port one",
              dbg.subscribe(1, bad).status == Result::RefusedUnavailable);
        check("EVT-SUB-12", "neither was added to the model",
              dbg.subscriptions(true).size() == 1);
        Subscription oob; oob.kind = static_cast<EventKind>(200);
        check("EVT-SUB-13", "a kind outside the enumeration is refused",
              dbg.subscribe(1, oob).status == Result::RefusedUnavailable);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.condition = [](const DbgEvent&, const Debugger&) { return true; };
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Continue; };
        s.once = true; s.action = Action::Log;
        const auto sub = dbg.subscribe(3, s);
        const auto list = dbg.subscriptions(false);
        check("EVT-LIST-01", "subscriptions() reports the model as set",
              list.size() == 1 && list[0].id == sub.value && list[0].owner == 3 &&
              list[0].kind == EventKind::Mem && list[0].access == Access::Write &&
              list[0].action == Action::Log && list[0].once);
        check("EVT-LIST-02", "and the filter with it",
              list.size() == 1 && list[0].filter.lo == 0x9000 &&
              list[0].filter.hi == 0x9000);
        check("EVT-LIST-03", "reporting only WHETHER a condition and a handler are "
                             "present, never the closures",
              list.size() == 1 && list[0].has_condition && list[0].has_handler);
        check("EVT-LIST-04", "enabled and live both true while nothing is suspended",
              list.size() == 1 && list[0].enabled && list[0].live);

        dbg.set_master_enabled(false);
        const auto sus = dbg.subscriptions(false);
        check("EVT-LIST-05", "the master switch clears `live` and leaves `enabled` alone",
              sus.size() == 1 && sus[0].enabled && !sus[0].live);
        check("EVT-LIST-06", "and it suspends BOTH models — the legacy set too",
              !emu.debug_state().breakpoints().master_enabled() &&
              !dbg.master_enabled());
        dbg.set_master_enabled(true);
        check("EVT-LIST-07", "and master on restores both",
              dbg.master_enabled() &&
              emu.debug_state().breakpoints().master_enabled() &&
              dbg.subscriptions(false)[0].live);

        dbg.set_client_enabled(3, false);
        check("EVT-LIST-08", "the per-client switch clears `live` and leaves `enabled`",
              !dbg.client_enabled(3) && dbg.subscriptions(false)[0].enabled &&
              !dbg.subscriptions(false)[0].live);
        dbg.set_client_enabled(3, true);
        check("EVT-LIST-09", "and it is restored",
              dbg.client_enabled(3) && dbg.subscriptions(false)[0].live);

        Subscription tr; tr.kind = EventKind::Execute; tr.transient = true;
        dbg.subscribe(3, tr);
        check("EVT-LIST-10", "subscriptions(false) HIDES a transient, as a user list must",
              dbg.subscriptions(false).size() == 1);
        check("EVT-LIST-11", "and subscriptions(true) shows it",
              dbg.subscriptions(true).size() == 2);
    }

    // ── EVT-DEL — delivery semantics the drain owns ──────────────────────
    {
        // A handler that mutates memory it is itself watching must not latch on
        // itself, and the mutation must be visible to the NEXT instruction.
        Emulator emu;
        //   8000  3E 01        LD A,1
        //   8002  32 00 90     LD (0x9000),A
        //   8005  3A 00 90     LD A,(0x9000)
        //   8008  32 01 90     LD (0x9001),A
        //   800B  18 FE        JR $
        build_armed(emu, { 0x3E, 0x01, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90,
                           0x32, 0x01, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int calls = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = [&calls](const DbgEvent& ev, Debugger& d) {
            ++calls;
            const uint8_t undone = static_cast<uint8_t>(ev.prev);
            d.poke(1, MemSpace::cpu(), 0x9000, 1, &undone);   // undo the write
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-01", "a handler ran once for the write it watched",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-02", "its own poke did NOT re-enter the handler (§4.2a)",
              calls == 1);
        check("EVT-DEL-03", "the undo is what the NEXT instruction read",
              emu.mmu().peek(0x9001) == 0x00,
              hex(emu.mmu().peek(0x9001)));
        check("EVT-DEL-04", "and the machine never stopped",
              !dbg.state().paused);
    }
    {
        // §4.2a — THE DELIVERY'S OWN `InspectionScope`, tested through a verb
        // that carries none of its own. `poke()` and `peek()` each take one
        // (EVT-MUT-04..07), so EVT-DEL-01..04 above stay green even with the
        // drain's scope removed — the mutation survived. `port_out()` dispatches
        // the port exactly as a guest `OUT` does and takes no scope, and a LEGACY
        // I/O watchpoint on that port is latched by `PortDispatch` on
        // `watchpoints_live()` alone — which is true inside a delivery unless the
        // drain has dropped it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        int calls = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = [&calls](const DbgEvent&, Debugger& d) {
            ++calls;
            d.port_out(1, 0x07FE, 0x00);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-05", "the handler's own port_out ran",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-06", "and did NOT raise the legacy I/O watchpoint on itself — "
                            "the delivery's one InspectionScope is what makes that true",
              !emu.debug_state().data_bp_hit() && !dbg.state().paused &&
              pc_of(emu) == 0x8007,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // A handler may unsubscribe itself mid-drain, and the drain must survive
        // it (contract 4: a removal is a tombstone, compaction is deferred).
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        int calls = 0;
        EventId my_id = jnext::dbg::EVENT_NONE;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.action = Action::Continue;
        s.handler = [&calls, &my_id](const DbgEvent& ev, Debugger& d) {
            ++calls;
            my_id = ev.id;
            d.unsubscribe(1, ev.id);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-10", "a handler that unsubscribes ITSELF is called exactly once",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-11", "and the subscription is really gone afterwards",
              dbg.subscriptions(true).empty());
        check("EVT-DEL-12", "and the machine ran on without crashing",
              pc_of(emu) == 0x8004, "pc=" + hex(pc_of(emu)));
    }
    {
        // A handler may SUBSCRIBE from inside a delivery (a script arming a
        // follow-up rule). The new one must not be visited for the event that
        // was already latched, and must fire afterwards.
        Emulator emu;
        build_armed(emu, { 0x06, 0x04, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        int first = 0, second = 0;
        Subscription follow;
        follow.kind = EventKind::Execute;
        follow.filter.lo = 0x8002; follow.filter.hi = 0x8002;
        follow.action = Action::Continue;
        follow.handler = [&second](const DbgEvent&, Debugger&) {
            ++second; return Action::Continue;
        };
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue;
        s.handler = [&first, follow](const DbgEvent&, Debugger& d) {
            ++first;
            d.subscribe(1, follow);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-20", "the arming handler fired once",
              first == 1, "first=" + std::to_string(first));
        check("EVT-DEL-21", "the handler it armed fired for the LATER passes only, "
                            "not for the delivery it was created in",
              second == 3, "second=" + std::to_string(second));
    }
    {
        // Two subscriptions on the same event: both are delivered, and
        // `matched[]` is what the Paused push will carry.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec r1, r2;
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Continue; a.handler = recorder(r1);
        Subscription b = a;
        b.filter.lo = 0x8000; b.filter.hi = 0xFFFF;     // a wider net
        b.handler = recorder(r2);
        const auto ia = dbg.subscribe(1, a);
        const auto ib = dbg.subscribe(2, b);
        emu.run_frame();
        check("EVT-DEL-30", "one site event reaches EVERY matching subscription",
              r1.evs.size() == 1 && r2.evs.size() == 1);
        check("EVT-DEL-31", "each delivery naming its OWN subscription and owner",
              r1.evs.size() == 1 && r2.evs.size() == 1 &&
              r1.evs[0].id == ia.value && r1.evs[0].owner == 1 &&
              r2.evs[0].id == ib.value && r2.evs[0].owner == 2);
        check("EVT-DEL-32", "with distinct sequence numbers",
              r1.evs.size() == 1 && r2.evs.size() == 1 &&
              r1.evs[0].seq != r2.evs[0].seq);
        check("EVT-DEL-33", "and both in the delivery history",
              dbg.events_fired_since(0).size() == 2);
    }
    {
        // Action::Log keeps the machine running; the SES-06 line goes to the log
        // sink, and what is asserted here is the verdict, not the format.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Log;
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-40", "an Action::Log delivery does not stop the machine",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-DEL-41", "and is still recorded in the delivery history",
              dbg.events_fired_since(0).size() == 1);
    }
    {
        // A disabled subscription is not consulted at all — including its
        // condition, which a script may have made expensive.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int cond_calls = 0;
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue; s.handler = recorder(rec);
        s.condition = [&cond_calls](const DbgEvent&, const Debugger&) {
            ++cond_calls; return true;
        };
        const auto sub = dbg.subscribe(1, s);
        dbg.set_enabled(1, sub.value, false);
        emu.run_frame();
        check("EVT-DEL-50", "a disabled subscription delivers nothing",
              rec.evs.empty());
        check("EVT-DEL-51", "and its condition is never even evaluated",
              cond_calls == 0, "calls=" + std::to_string(cond_calls));
        check("EVT-DEL-52", "and it no longer arms the slot mask either",
              !emu.debug_state().wr_watch_armed(0x9000));
    }

    // ── EVT-TRANS — the transient successor of the single one-shot ────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.step_into(1); dbg.step_into(1);     // park the PC on the CALL
        check("EVT-TRANS-01", "step_over() arms a transient Execute at the next PC "
                              "and NOT the legacy one-shot",
              dbg.step_over(1) == Result::Ok &&
              !emu.debug_state().breakpoints().has_oneshot());
        const auto list = dbg.subscriptions(true);
        check("EVT-TRANS-02", "  (one transient, at the return address)",
              list.size() == 1 && list[0].transient &&
              list[0].kind == EventKind::Execute &&
              list[0].filter.lo == AFTER_CALL);
        check("EVT-TRANS-03", "the user list does not show it",
              dbg.subscriptions(false).empty());
        check("EVT-TRANS-04", "and the step MODE still reports Over (CTL-13 unchanged)",
              dbg.state().step_mode == jnext::dbg::StepMode::Over);
        run_until_paused(emu);
        check("EVT-TRANS-05", "the step lands past the CALL",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-06", "and the transient was auto-removed at the stop",
              dbg.subscriptions(true).empty());
        check("EVT-TRANS-07", "with the reason the verb armed, not the subscription's",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step &&
              dbg.state().pause_reason.by == 1);
    }
    {
        // §4.3 — transient subscriptions are UNLIMITED, which the single one-shot
        // was not. DeZog needs two temporary breakpoints per CMD_CONTINUE.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription t1, t2;
        t1.kind = EventKind::Execute; t1.transient = true;
        t1.filter.lo = 0x8005; t1.filter.hi = 0x8005;
        t2 = t1; t2.filter.lo = 0x8006; t2.filter.hi = 0x8006;
        dbg.subscribe(1, t1);
        dbg.subscribe(1, t2);
        check("EVT-TRANS-10", "two transient targets coexist",
              dbg.subscriptions(true).size() == 2);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-11", "the machine stops at the FIRST of them",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-12", "and BOTH are dropped at that stop, not just the one "
                              "that fired",
              dbg.subscriptions(true).empty());
    }
    {
        // A transient is exempt from the master switch, which is what makes Step
        // Over work on a machine the user has suspended everything on.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.step_into(1); dbg.step_into(1);     // park the PC on the CALL
        dbg.set_master_enabled(false);
        dbg.step_over(1);
        run_until_paused(emu);
        check("EVT-TRANS-20", "Step Over still works with the master switch OFF",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // And the other half of GH #223: a run() from a PAUSED machine abandons
        // the pending target, exactly as resume()'s clear_oneshot() always did.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.run_to(1, AFTER_CALL);
        check("EVT-TRANS-30", "run_to() arms exactly one transient target",
              dbg.subscriptions(true).size() == 1);
        dbg.pause(1);
        check("EVT-TRANS-31", "an explicit pause IS a stop, so the target is dropped "
                              "(§4.3) — which is what resume()'s clear_oneshot() "
                              "was standing in for",
              dbg.subscriptions(true).empty());
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-32", "so the machine runs past it to the landing marker",
              !dbg.state().paused && pc_of(emu) == PARK,
              "pc=" + hex(pc_of(emu)));
        // And the complementary rule: `run()` does NOT drop a target, so DeZog's
        // two temp breakpoints per CMD_CONTINUE survive the continue that armed
        // them. CTL-02-04 and EVT-TRANS-10..12 are the rows for that half.
    }

    // ── REASON — CTL-13's precedence, in order ───────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        // A LEGACY watchpoint: the evidence the hot loop destroys.
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        emu.run_frame();
        check("REASON-01", "a legacy watchpoint stops the machine",
              dbg.state().paused, "pc=" + hex(pc_of(emu)));
        check("REASON-02", "and the reason is Watch, with the address and direction, "
                           "though data_bp_hit_ is already consumed",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.addr == 0x9000 &&
              dbg.state().pause_reason.access == Access::Write &&
              !emu.debug_state().data_bp_hit());
        check("REASON-03", "and it is unowned — no client set that watchpoint "
                           "through the backend",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
        check("REASON-04", "a resume clears the evidence",
              dbg.run(1) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }
    {
        // REASON-04 above cannot see stale evidence: `state()` returns `None` for
        // a RUNNING machine whatever the latches hold. What catches it is the
        // NEXT stop, for a DIFFERENT reason — a mutation that kept the watch
        // evidence across a resume left every other row green.
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A    <- the WATCH stops after this
        //   8005  00           NOP
        //   8006  00           NOP              <- a PC BREAKPOINT here
        //   8007  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-05", "the watch stops first, and is reported as Watch",
              dbg.state().paused && pc_of(emu) == 0x8005 &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch,
              "pc=" + hex(pc_of(emu)));
        dbg.run(1);
        run_until_paused(emu);
        check("REASON-06", "the breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-07", "and THAT stop is reported as Breakpoint, not as the "
                           "watch it left behind",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == 0x8006);
    }
    {
        // An I/O watchpoint takes the same route, with the PORT as the address.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        emu.run_frame();
        check("REASON-10", "a legacy I/O watchpoint stops the machine",
              dbg.state().paused);
        check("REASON-11", "with reason Watch and the port as the address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.addr == 0x07FE &&
              dbg.state().pause_reason.access == Access::Write,
              hex(dbg.state().pause_reason.addr));
    }
    {
        // A legacy PC breakpoint is still reported as Breakpoint, and an armed
        // verb still outranks both.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(AFTER_CALL);
        emu.run_frame();
        check("REASON-20", "a legacy PC breakpoint stops the machine",
              dbg.state().paused && pc_of(emu) == AFTER_CALL);
        check("REASON-21", "and is reported as Breakpoint at that address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == AFTER_CALL);
        check("REASON-22", "a user pause OUTRANKS a subscription stop at the same PC",
              dbg.pause(5) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::User &&
              dbg.state().pause_reason.by == 5);
    }
    {
        // Corrupt is FIRST, ahead even of the armed verb: CTL-11 makes it the
        // thing that refuses every resume, so it is what must be reported.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint8_t junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const Result lr = dbg.load_state_bytes(1, junk, sizeof(junk));
        check("REASON-30", "a torn load_state_bytes() is refused",
              lr != Result::Ok);
        const bool blocked = dbg.resume_blocked_by_corruption().has_value();
        check("REASON-31", "and latches an unacknowledged corruption", blocked);
        if (blocked) {
            check("REASON-32", "which state() reports as Corrupt, ahead of the "
                               "armed verb",
                  dbg.state().pause_reason.kind == PauseReason::Kind::Corrupt);
            check("REASON-33", "UNOWNED — the corruption is the machine's",
                  dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
            const auto inc = dbg.resume_blocked_by_corruption();
            check("REASON-34", "acknowledging it restores the ordinary reason",
                  dbg.acknowledge_corruption(inc->generation) == Result::Ok &&
                  dbg.state().pause_reason.kind != PauseReason::Kind::Corrupt);
        } else {
            check("REASON-32", "Corrupt reporting", false, "no incident latched");
        }
    }
    {
        // An event stop OUTRANKS the legacy fallbacks: it is the only source that
        // knows the subscription id.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("REASON-40", "with BOTH models armed on the same address the machine stops",
              dbg.state().paused);
        check("REASON-41", "and the SUBSCRIPTION explains it, because only it has an id",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 6);
    }

    {
        // ONE reason per stop: the FIRST `Stop` of the boundary wins, because that
        // is the one the machine stopped on. Two subscriptions on the same write,
        // both asking to stop — a mutation that let the LATER one overwrite the
        // reason survived, because no row had two.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Stop;
        const auto first  = dbg.subscribe(4, a);
        const auto second = dbg.subscribe(5, a);
        emu.run_frame();
        check("REASON-50", "two Stop subscriptions on one write stop the machine once",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("REASON-51", "and the FIRST of them is the reason, not the last",
              dbg.state().pause_reason.id == first.value &&
              dbg.state().pause_reason.by == 4,
              "id=" + std::to_string(dbg.state().pause_reason.id) + " first=" +
                  std::to_string(first.value) + " second=" +
                  std::to_string(second.value));
    }
    {
        // The event-stop latch must not explain the NEXT stop. `Impl::arm()` is
        // what clears it, and a mutation that stopped clearing it survived: the
        // only row that looked was EVT-MEM-53, and `state()` reports `None` for a
        // RUNNING machine whatever the latch holds.
        Emulator emu;
        //   8000  3E 5A / 8002  32 00 90  <- a Mem SUBSCRIPTION stops here
        //   8005  00   / 8006  00         <- a legacy PC BREAKPOINT at 0x8006
        //   8007  18 FE
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-60", "the subscription stops first and names itself",
              dbg.state().paused && dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);
        dbg.run(4);
        run_until_paused(emu);
        check("REASON-61", "the legacy breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-62", "and THAT stop is Breakpoint, with no id — the earlier "
                           "subscription's latch was cleared by the resume",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == jnext::dbg::EVENT_NONE &&
              dbg.state().pause_reason.addr == 0x8006);
    }
    {
        // A `Magic` SUBSCRIPTION that stops: the reason comes from the drain's
        // latch rather than from `note_magic_stop()`, and it must still be
        // UNOWNED. Every earlier Magic row used Action::Continue, so the drain's
        // own Magic arm had no row and a mutation that gave it an owner survived.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Stop;
        const auto sub = dbg.subscribe(3, s);
        emu.run_frame();
        check("REASON-70", "a Magic subscription's Stop is reported as Magic",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("REASON-71", "naming the subscription",
              dbg.state().pause_reason.id == sub.value);
        check("REASON-72", "but UNOWNED, even though the subscription HAS an owner",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE,
              "by=" + std::to_string(dbg.state().pause_reason.by));
    }
    {
        // §4.2a — the backend consults the table only when `!replay_mode_`: a
        // rewind fast-forward runs the machine with the gate live, and an
        // `Execute` subscription inside the replayed span would pause the replay
        // short. A mutation that dropped the gate survived; nothing set the flag.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        // NO HANDLER: `recorder()` returns Continue, which would OVERRIDE this
        // Stop and make the "nothing stopped" claim vacuous. The delivery is
        // observed through the INS-17 history instead.
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.set_replay_mode(true);
        emu.run_frame();
        check("EVT-REPLAY-01", "during a replay nothing is delivered and nothing stops",
              dbg.events_fired_since(0).empty() && !dbg.state().paused,
              "n=" + std::to_string(dbg.events_fired_since(0).size()));
        check("EVT-REPLAY-02", "and the write really happened — the replay is faithful, "
                               "it is the REPORTING that is suppressed",
              emu.mmu().peek(0x9000) == 0x5A);
        check("EVT-REPLAY-03", "the ring was emptied, so the replay does not hand its "
                               "latches to the next real boundary",
              !emu.debug_state().events_pending());
        emu.set_replay_mode(false);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; emu.cpu().set_registers(r);
        emu.mmu().write(0x9000, 0x00);
        emu.run_frame();
        check("EVT-REPLAY-04", "and with replay off the same program DOES deliver AND "
                               "stop — the control without which the rows above prove "
                               "nothing",
              dbg.events_fired_since(0).size() == 1 && dbg.state().paused &&
              pc_of(emu) == 0x8005,
              "n=" + std::to_string(dbg.events_fired_since(0).size()) +
                  " pc=" + hex(pc_of(emu)));
    }

    {
        // THE QT PATH. `Impl::arm()` clears the event-stop latch, and every backend
        // control verb calls it — but the Qt panels still drive `DebugState`
        // DIRECTLY until package Q, so a Qt-driven Run never reaches a backend verb
        // and the latch would explain the NEXT, unrelated stop. The resume
        // GENERATION is what closes that, and nothing exercised it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-80", "the subscription stops first and names itself",
              dbg.state().paused && dbg.state().pause_reason.id == sub.value);

        // A RAW resume, exactly as DebuggerManager::on_run() does it today —
        // NOT dbg.run(), so `Impl::arm()` never runs.
        emu.debug_state().resume();
        run_until_paused(emu);
        check("REASON-81", "the legacy breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-82", "and THAT stop is Breakpoint — the event-stop latch is "
                           "discarded by the resume GENERATION, not only by a backend "
                           "verb",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == jnext::dbg::EVENT_NONE,
              "kind=" + std::to_string(
                  static_cast<int>(dbg.state().pause_reason.kind)));
    }
    {
        // B-9: the halt-run loop's `!event_stop_pending_` term. A Stop that arrives
        // WHILE debugger_step() is running a HALT out must end the step there; the
        // budget is two frames, so without the term the step spends it all.
        Emulator emu;
        build_armed(emu, { 0x76, 0x18, 0xFD });        // HALT, then JR $
        {
            Z80Registers r = emu.cpu().get_registers();
            r.IFF1 = 0; r.IFF2 = 0;                    // nothing wakes the HALT
            emu.cpu().set_registers(r);
        }
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 4;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        // TWO steps: the first EXECUTES the HALT, so `is_halted()` is true when
        // the second one starts and the halt-run loop actually engages. Stepping
        // once reaches the loop's guard with `halted_before == false` and skips
        // it entirely — which is why the first cut of this row measured 4
        // T-states and could not have seen anything.
        emu.debugger_step();
        dbg.pause(1);
        const int tstates = emu.debugger_step();
        const int budget  = static_cast<int>(
            2u * dbg.machine().tstates_per_frame);
        check("EVT-STEP-20", "a Stop during the halt-run ENDS the step early rather "
                             "than spending the whole two-frame budget",
              tstates > 0 && tstates < budget / 2,
              "tstates=" + std::to_string(tstates) + " budget=" +
                  std::to_string(budget));
        check("EVT-STEP-21", "and the machine is paused where it ended",
              dbg.state().paused);
    }

    // ── EVT-GATE — the no-subscriber cost claim, as a row ─────────────────
    //
    // §6's constraint is that an unsubscribed machine pays what it paid before.
    // The bench measures the TIME; these rows assert the MECHANISM the bench
    // result rests on, which is what a reader can check.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        check("EVT-GATE-01", "with no Debugger at all the table pointer is null",
              emu.debug_state().event_table() == nullptr);
        check("EVT-GATE-02", "and every memory / port gate is closed",
              !emu.debug_state().rd_watch_armed(0x0000) &&
              !emu.debug_state().wr_watch_armed(0xFFFF) &&
              !emu.debug_state().port_watch_armed() &&
              !emu.debug_state().events_pending() &&
              !emu.debug_state().execute_events_armed());
        {
            Debugger dbg(emu);
            check("EVT-GATE-03", "constructing a Debugger publishes the table",
                  emu.debug_state().event_table() != nullptr);
            check("EVT-GATE-04", "but arms NOTHING by itself",
                  !emu.debug_state().rd_watch_armed(0x0000) &&
                  !emu.debug_state().wr_watch_armed(0xFFFF) &&
                  !emu.debug_state().port_watch_armed() &&
                  !emu.debug_state().events_pending() &&
                  !emu.debug_state().execute_events_armed() &&
                  !emu.copper().events_armed() && !emu.dma().events_armed() &&
                  !emu.dma().byte_events_armed());
            Subscription s;
            s.kind = EventKind::Mem; s.access = Access::ReadWrite;
            dbg.subscribe(1, s);
            check("EVT-GATE-05", "and a subscription opens exactly the gate it needs",
                  emu.debug_state().rd_watch_armed(0x0000) &&
                  !emu.debug_state().port_watch_armed() &&
                  !emu.copper().events_armed());
        }
        // The destructor must retire every published pointer: DebugState outlives
        // the Debugger, and a stale slot mask over a freed table is a
        // use-after-free on the hot path.
        check("EVT-GATE-06", "destroying the Debugger retires the table pointer",
              emu.debug_state().event_table() == nullptr);
        check("EVT-GATE-07", "and closes every gate it had opened",
              !emu.debug_state().rd_watch_armed(0x0000) &&
              !emu.debug_state().wr_watch_armed(0x0000) &&
              !emu.debug_state().port_watch_armed() &&
              !emu.debug_state().execute_events_armed() &&
              !emu.copper().events_armed() && !emu.dma().events_armed());
        emu.run_frame();
        check("EVT-GATE-08", "so the machine runs on safely after it is gone",
              pc_of(emu) == 0x8000 || pc_of(emu) == 0x8001,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // The `armed()` gate: with no client attached and no
        // --persistent-breakpoints, the whole pipeline is inert even with a
        // subscription in place. That is §4.1's formula, and §6's premise.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        emu.debug_state().set_active(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-GATE-10", "armed() is false with nothing attached",
              !dbg.armed());
        emu.run_frame();
        check("EVT-GATE-11", "so nothing is delivered and nothing stops",
              rec.evs.empty() && !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-GATE-12", "--persistent-breakpoints alone re-arms it (GH #219)",
              dbg.set_persistent_breakpoints(true) == Result::Ok && dbg.armed());
    }
    {
        // on_slot_remapped: the MMU's dispatch rebuild really does re-publish,
        // so a physical-page filter follows the page around.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x02 };          // RAM bank 1's low 8 KB page
        dbg.subscribe(1, s);
        const uint8_t page_at_6 = emu.mmu().get_effective_page(6);
        emu.mmu().set_page(6, 0x02);
        check("EVT-SLOT-01", "mapping the watched page into a slot arms that slot",
              emu.debug_state().wr_watch_armed(0xC000),
              "was page " + std::to_string(page_at_6));
        check("EVT-SLOT-02", "and no other",
              !emu.debug_state().wr_watch_armed(0x4000));
        emu.mmu().set_page(6, 0x10);
        check("EVT-SLOT-03", "mapping it away closes the gate again",
              !emu.debug_state().wr_watch_armed(0xC000));
        emu.mmu().set_page(2, 0x02);
        check("EVT-SLOT-04", "and it follows the page to another slot",
              emu.debug_state().wr_watch_armed(0x4000) &&
              !emu.debug_state().wr_watch_armed(0xC000));
    }


    // =======================================================================
    // GH #276 B2 REVISION — the rows the review proved were missing.
    //
    // Nine blocking items, and not one of them moved a single row of the 744:
    // the page-qualified filter was never maintained, the latch sites and the
    // drain gates disagreed about `armed()`, a DMA NextREG write was tagged
    // `Cpu`, `Scanline` carried the wrong cycle, the Copper latched sub-kinds
    // nobody asked for, a nested `raise_host_event` wiped the boundary's hit
    // list, `Cycle` never retired, and one fixture asserted `0 == 0`. Each row
    // below FAILS without its fix — verified one at a time, not assumed.
    // =======================================================================

    // ── EVT-SLOT-10..16 — §6.1: EVERY mapping entry point must notify ─────
    //
    // B2 put the notification in `Mmu::rebuild_ptr()` alone and seeded nothing,
    // so `EventTable::slot_page_` was all zeros for the life of the session and
    // `map_rom_physical()` — reached from a plain guest `OUT (0x7FFD)` ROM
    // select — bypassed the one notifier entirely. The failure was silent in
    // BOTH directions, which is why the enumeration is the row and not a single
    // happy-path check: a filter naming page 0x00 armed all eight slots, and one
    // naming the page actually mapped armed none.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        // The page ALREADY mapped at slot 3 before the Debugger exists — the
        // case B2 could not see, because `on_slot_remapped` early-returns while
        // the table is null and every `rebuild_ptr()` in `init()` was discarded.
        const uint16_t live_page = emu.mmu().get_effective_page(3);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { live_page };
        dbg.subscribe(1, s);
        check("EVT-SLOT-10", "a page ALREADY mapped when the table is installed arms "
                             "its slot — the eight live pages are seeded, not zeros",
              emu.debug_state().wr_watch_armed(0x6000),
              "page=" + std::to_string(live_page));
        check("EVT-SLOT-11", "and page 0x00 does NOT arm every slot, which is what a "
                             "zero-initialised cache did",
              live_page == 0 || !emu.debug_state().wr_watch_armed(0x0000) ||
                  emu.mmu().get_effective_page(0) == 0);
    }
    {
        // One row per PUBLIC mapping entry point. `map_rom_physical()` is private
        // and reached from four of them; `set_page()` goes through `rebuild_ptr`.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x2A };
        dbg.subscribe(1, s);

        // THE INVARIANT, asserted as a sweep rather than as a guess at which page
        // a given API lands on: for EVERY slot, the armed bit must agree with the
        // page the MMU says that slot serves. A missed notification breaks it on
        // whichever slot the call touched, whatever the API's own page arithmetic
        // turns out to be — which is the only form of this row that cannot be
        // written to pass by accident.
        auto mask_agrees = [&emu](uint16_t watched) {
            for (int sl = 0; sl < 8; ++sl) {
                const bool want = emu.mmu().get_effective_page(sl) == watched;
                const bool got  =
                    emu.debug_state().wr_watch_armed(static_cast<uint16_t>(sl << 13));
                if (want != got) return false;
            }
            return true;
        };
        check("EVT-SLOT-12", "the invariant holds before any mapping call",
              mask_agrees(0x2A));

        emu.mmu().set_page(4, 0x2A);
        check("EVT-SLOT-13", "set_page() keeps it (via rebuild_ptr)",
              mask_agrees(0x2A) && emu.debug_state().wr_watch_armed(0x8000));
        emu.mmu().set_page(4, 0x10);

        // map_rom() -> map_rom_physical(): the path that bypassed the notifier.
        emu.mmu().map_rom(2, 0x2A);
        check("EVT-SLOT-14", "map_rom() keeps it — it routes through "
                             "map_rom_physical(), which B2 left silent",
              mask_agrees(0x2A));
        emu.mmu().set_page(2, 0x10);

        // map_128k_bank() is the legacy 0x7FFD path's own entry point, and it
        // re-maps slots 0, 1, 6 and 7 in one call.
        emu.mmu().map_128k_bank(0x2A);
        check("EVT-SLOT-15", "map_128k_bank() keeps it across every slot it moves",
              mask_agrees(0x2A));
        emu.mmu().map_plus3_bank(0x04);
        check("EVT-SLOT-16", "and so does map_plus3_bank()", mask_agrees(0x2A));

        // The non-vacuous half: at least one of those calls must actually have put
        // the watched page somewhere, or the sweep above is comparing false to
        // false eight times.
        emu.mmu().set_page(6, 0x2A);
        check("EVT-SLOT-17", "and the sweep is not vacuous — the watched page really "
                             "does arm exactly its slot",
              mask_agrees(0x2A) && emu.debug_state().wr_watch_armed(0xC000) &&
              !emu.debug_state().wr_watch_armed(0x8000));

        // THE PATH THAT NEEDS `map_rom_physical()`'S OWN NOTIFY. The other four
        // callers write `nr_mmu_` afterwards, so the notifying setter covers them;
        // `set_nr_8c()` -> `engage_legacy_rom_paging_slot(slot, false)` deliberately
        // does NOT touch `nr_mmu_` (VHDL leaves MMU<i> alone on an NR 0x8C write,
        // zxnext.vhd:3813), so it re-points the slot with NOTHING else to notify.
        // Put slots 0/1 back into legacy ROM mode so the NR 0x8C path has
        // something to re-point, and watch the ROM page it will select.
        emu.mmu().map_128k_bank(0x00);
        emu.mmu().set_nr_8c(0x00);
        const uint16_t rom_page_lock1 = 2;   // lock_rom1 -> sram_rom 2 -> page 4/5
        Subscription rs;
        rs.kind = EventKind::Mem; rs.access = Access::Read;
        rs.filter.pages = { rom_page_lock1 };
        dbg.subscribe(1, rs);
        auto rd_agrees = [&emu](uint16_t watched) {
            for (int sl = 0; sl < 8; ++sl) {
                const bool want = emu.mmu().get_effective_page(sl) == watched;
                const bool got  =
                    emu.debug_state().rd_watch_armed(static_cast<uint16_t>(sl << 13));
                if (want != got) return false;
            }
            return true;
        };
        check("EVT-SLOT-18", "the read invariant holds before the NR 0x8C write",
              rd_agrees(rom_page_lock1));
        // NR 0x8C bit 4 is altrom_lock_rom0, bit 5 altrom_lock_rom1
        // (zxnext.vhd:2997-3007): either one overrides `sram_rom`, so slots 0/1 are
        // re-pointed to a DIFFERENT physical ROM page — through
        // `engage_legacy_rom_paging_slot(slot, set_nr_sentinel=false)`, which is the
        // ONE re-mapping path that deliberately leaves `nr_mmu_` alone (VHDL leaves
        // MMU<i> untouched on an NR 0x8C write, :3813). So it is the one path with
        // nothing but `map_rom_physical()`'s own notify behind it.
        emu.mmu().set_nr_8c(0x20);
        check("EVT-SLOT-19", "and it still holds after it — `map_rom_physical()`'s own "
                             "notify is what carries this path",
              rd_agrees(rom_page_lock1),
              "slot0=" + std::to_string(emu.mmu().get_effective_page(0)) +
                  " mask=" + std::to_string(emu.debug_state().rd_watch_mask()));
    }
    {
        // The GUEST-REACHABLE version of the same thing: a plain OUT (0x7FFD)
        // ROM select reaches apply_legacy_rom_slots_ -> map_rom_physical, and B2
        // left a page-qualified filter on the ROM page reading a stale page.
        Emulator emu;
        //   8000  3E 10        LD A,0x10      (ROM select bit 4 set)
        //   8002  01 FD 7F     LD BC,0x7FFD
        //   8005  ED 79        OUT (C),A
        //   8007  18 FE        JR $
        build_armed(emu, { 0x3E, 0x10, 0x01, 0xFD, 0x7F, 0xED, 0x79, 0x18, 0xFE },
                    MachineType::ZX128K);
        Debugger dbg(emu);
        const uint16_t rom_page_after = 2;   // sram_rom 1 -> pages 2/3 in slots 0/1
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Read;
        s.filter.pages = { rom_page_after };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-SLOT-20", "a guest OUT (0x7FFD) ROM select notifies, so a filter "
                             "on the newly selected ROM page arms its slot",
              emu.debug_state().rd_watch_armed(0x0000) ==
                  (emu.mmu().get_effective_page(0) == rom_page_after),
              "slot0 page=" + std::to_string(emu.mmu().get_effective_page(0)));
    }

    // ── EVT-GATE-20..26 — §4.1: the backend is INERT while unarmed ─────────
    //
    // B2's latch sites gated on `has_kind()` (or an engine flag) while all four
    // drains gated on `armed() && events_pending()`. A subscription on a machine
    // with no client attached therefore FILLED the 512-entry ring and dumped up
    // to 513 stale events at the first armed boundary, every one carrying a
    // cycle from before the machine was being watched and `overflowed` set.
    // EVT-GATE-10/11 asserted the §4.1 property for `Mem` only — the two kinds
    // whose sites do take `watchpoints_live()`.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        emu.debug_state().set_active(false);          // UNARMED
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-GATE-20", "armed() is false", !dbg.armed());
        emu.run_frame();
        emu.run_frame();
        emu.run_frame();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-GATE-21", "a LATCHING kind latches nothing on an unarmed machine — "
                             "the ring stays empty",
              t->size() == 0 && t->dropped() == 0,
              "size=" + std::to_string(t->size()) +
                  " dropped=" + std::to_string(t->dropped()));
        check("EVT-GATE-22", "so there is no boundary work pending",
              !emu.debug_state().events_pending());
        check("EVT-GATE-23", "and nothing was delivered",
              rec.evs.empty() && dbg.events_fired_since(0).empty());

        // Now arm it. The point of the row: what arrives is THIS frame's events,
        // not a 512-entry dump of the three frames nobody was watching.
        emu.debug_state().set_active(true);
        emu.run_frame();
        check("EVT-GATE-24", "arming it delivers exactly the armed frame's events",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-GATE-25", "not flagged as lossy, because nothing was ever dropped",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0);
        check("EVT-GATE-26", "and the delivered event's cycle is from the ARMED frame",
              rec.evs.size() == 1 && rec.evs[0].frame == 3,
              rec.evs.empty() ? "" : "frame=" + std::to_string(rec.evs[0].frame));
    }
    {
        // The same for the Copper and DMA engines, whose own flags carry the
        // term at the site as well as in the funnel (cost, not correctness).
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        emu.debug_state().set_active(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        copper_program(emu, { move_word(0x30, 0x7F), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-GATE-30", "an unarmed machine latches no Copper event either",
              rec.evs.empty() &&
              emu.debug_state().event_table()->size() == 0);
        check("EVT-GATE-31", "and the MOVE still happened — the gate suppresses the "
                             "REPORTING, never the emulation",
              emu.nextreg().peek(0x30) == 0x7F);
    }

    // ── EVT-NR-20..23 — a DMA NextREG write is `source = Dma` ──────────────
    //
    // `nextreg.h`'s `set_write_source()` doc-comment asserted that
    // `Emulator`'s `dma_.write_io` lambda brackets the write. It did not — a
    // justification comment whose premise was false, which is the exact class of
    // defect comment discipline exists to prevent. Five frontends read this field:
    // a `{source=Dma}` filter could never match and a `{source=Cpu}` one fired
    // falsely on every DMA NextREG write.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        // One byte, 0x42, from RAM into I/O port 0x253B with NR 0x16 selected.
        emu.nextreg().select(0x16);
        emu.mmu().write(0xA000, 0x42);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B, port A start 0xA000, len 1; R1 port A = memory;
        // R2 port B = I/O; R4 mode + port B start 0x253B; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
        w(0x14);                         // R1 port A = memory, inc
        w(0x28);                         // R2 port B = I/O, fixed
        w(0xAD); w(0x3B); w(0x25);       // R4 mode + port B = 0x253B
        w(0xCF); w(0x87);                // R6 LOAD, R6 ENABLE
        emu.execute_single_instruction();

        check("EVT-NR-20", "a DMA transfer with NR 0x253B as its I/O destination "
                           "raises exactly one NextRegWrite",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-NR-21", "with the register and the byte the DMA moved",
              rec.evs.size() == 1 && rec.evs[0].reg == 0x16 &&
              rec.evs[0].value == 0x42);
        check("EVT-NR-22", "tagged source=Dma, not Cpu",
              rec.evs.size() == 1 && rec.evs[0].source == EventSource::Dma,
              rec.evs.empty() ? ""
                              : "source=" + std::to_string(
                                    static_cast<int>(rec.evs[0].source)));
        check("EVT-NR-23", "and the register really took it",
              emu.nextreg().peek(0x16) == 0x42);
    }
    {
        // The filter's negative arm, which is the half a frontend depends on: a
        // {source=Cpu} subscription must NOT see the DMA's write.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        emu.nextreg().select(0x16);
        emu.mmu().write(0xA000, 0x42);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.filter.source = EventSource::Cpu;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
        w(0x14);                         // R1 port A = memory, inc
        w(0x28);                         // R2 port B = I/O, fixed
        w(0xAD); w(0x3B); w(0x25);       // R4 mode + port B = 0x253B
        w(0xCF); w(0x87);                // R6 LOAD, R6 ENABLE
        emu.execute_single_instruction();
        check("EVT-NR-24", "a {source=Cpu} subscription does NOT see the DMA's write",
              rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-TIME-15..19 — `Scanline` carries the LINE's cycle ──────────────
    //
    // `events.h` promises "latched at the line with its exact cycle". B2 stamped
    // `clock_.get()` from inside the post-instruction device cluster — already
    // past the boundary by the whole instruction that crossed it — and discarded
    // `raw_line`, the exact value the caller handed in, one line away. The old
    // EVT-TIME-11 asserted the payload's `cvc` against the field the filter
    // matched on, which is a tautology an off-by-one survives.
    {
        Emulator emu;
        // `JR $` ALONE, and the instruction mix is the row. A 12 T-state
        // instruction is 96 master cycles and a 48K line is 1792, so 1792/96 is
        // not an integer and the instruction that crosses a line boundary
        // OVERSHOOTS it — which is the condition under which the live clock is
        // not the boundary. The earlier fixture was `NOP; JR -3`: 16 T-states =
        // 128 master cycles, 1792/128 = 14 EXACTLY, so `clock_.get()` landed on
        // the boundary every line and a mutation that stamped the live clock
        // survived the whole suite. Coverage uniformly on one side of the
        // condition, in one line of machine code.
        build_armed(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        const uint64_t frame_start = emu.clock().get();
        emu.run_frame();
        check("EVT-TIME-15", "a Scanline event was delivered",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            const uint64_t mcpl = dbg.machine().master_cycles_per_line;
            // cvc = (raw_vc - min_vactive + cu_offset) mod lines_per_frame, derived
            // here from VideoTiming rather than read back from the payload — an
            // INDEPENDENT oracle, which is what the replaced row lacked.
            const int lpf  = emu.video_timing().vc_max() + 1;
            const int minv = emu.video_timing().display_origin().vc;
            const int cuo  = emu.video_timing().cu_offset();
            const int raw  = rec.evs[0].vc;
            int want_cvc = (raw - minv + cuo) % lpf;
            if (want_cvc < 0) want_cvc += lpf;
            check("EVT-TIME-16", "and its cvc matches an INDEPENDENT derivation from "
                                 "the raw line it reports",
                  want_cvc == rec.evs[0].cvc,
                  "raw=" + std::to_string(raw) + " want=" + std::to_string(want_cvc) +
                      " got=" + std::to_string(rec.evs[0].cvc));
            check("EVT-TIME-17", "`hc` is 0 — the event names the START of the line, "
                                 "not wherever the crossing instruction ended",
                  rec.evs[0].hc == 0, "hc=" + std::to_string(rec.evs[0].hc));
            check("EVT-TIME-18", "and `cycle` is the line's own boundary cycle",
                  rec.evs[0].cycle ==
                      frame_start + static_cast<uint64_t>(rec.evs[0].vc) * mcpl,
                  "cycle=" + std::to_string(rec.evs[0].cycle) + " want=" +
                      std::to_string(frame_start +
                                     static_cast<uint64_t>(rec.evs[0].vc) * mcpl));
            check("EVT-TIME-19", "which is BEHIND the clock at delivery — the proof "
                                 "that it is not just `clock_.get()` again",
                  rec.evs[0].cycle < emu.clock().get());
        }
    }
    {
        // THE OVERSHOOT INSTANCE, and it took a measurement to find one. For most
        // lines of most fixtures the instruction that crosses a line boundary ends
        // exactly ON it and `clock_.get()` at the `on_scanline` callback IS the
        // boundary — which is why a mutation that stamped the live clock survived
        // the group above and every other row. RAW LINE 0 is different: the frame's
        // first instruction has already run when its event fires, so the live clock
        // is 32 master cycles past the boundary and the live `hc` is 8.
        //
        // cvc for raw line 0 is (0 - min_vactive + cu_offset) mod lines_per_frame,
        // computed here rather than written as a constant so the row follows the
        // machine.
        Emulator emu;
        build_armed(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const int lpf  = emu.video_timing().vc_max() + 1;
        const int minv = emu.video_timing().display_origin().vc;
        const int cuo  = emu.video_timing().cu_offset();
        int cvc_line0 = (0 - minv + cuo) % lpf;
        if (cvc_line0 < 0) cvc_line0 += lpf;

        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline;
        s.filter.scanline = static_cast<int16_t>(cvc_line0);
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        const uint64_t frame_start = emu.clock().get();
        emu.run_frame();
        check("EVT-TIME-20", "the raw-line-0 event is delivered",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-TIME-21", "and it names raw line 0",
                  rec.evs[0].vc == 0, "vc=" + std::to_string(rec.evs[0].vc));
            check("EVT-TIME-22", "with `hc` 0 and `cycle` AT the frame's start — the "
                                 "live clock is 32 master cycles past it here, so "
                                 "this is the row the aligned group above cannot be",
                  rec.evs[0].hc == 0 && rec.evs[0].cycle == frame_start,
                  "hc=" + std::to_string(rec.evs[0].hc) + " cycle=" +
                      std::to_string(rec.evs[0].cycle) + " frame_start=" +
                      std::to_string(frame_start));
        }
    }

    // ── EVT-COP-60..66 — one arming flag per Copper sub-kind ───────────────
    //
    // B2 armed the whole engine from `has_kind(Copper) || has_kind(NextRegWrite)`,
    // so a `Halt`-only subscriber accumulated ~16 `Move` entries per instruction
    // slot, and `EventTable::has_copper_sub_kind()` — written for exactly this —
    // had zero callers. Ring space is observable, which B2 argues itself in
    // `Mmu::watch_write_`.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription h;
        h.kind = EventKind::Copper;
        h.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        h.action = Action::Continue;
        dbg.subscribe(1, h);
        check("EVT-COP-60", "a Halt-only subscription arms the Halt site and NOT the "
                            "Move or Wait sites",
              emu.copper().halt_events_armed() &&
              !emu.copper().move_events_armed() &&
              !emu.copper().wait_events_armed());

        // Sixteen MOVEs then a HALT. With one flag for the engine, the ring held
        // the MOVEs; with one per sub-kind it holds the HALT alone.
        std::vector<uint16_t> prog;
        for (int i = 0; i < 16; ++i)
            prog.push_back(move_word(0x30, static_cast<uint8_t>(0x10 + i)));
        prog.push_back(HALT_WORD);
        copper_program(emu, prog);
        copper_start(emu);
        emu.execute_single_instruction();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-COP-61", "so a MOVE burst consumes NO ring space for it",
              t->size() <= 1, "size=" + std::to_string(t->size()));
        check("EVT-COP-62", "and nothing was dropped",
              t->dropped() == 0, "dropped=" + std::to_string(t->dropped()));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription w;
        w.kind = EventKind::Copper;
        w.filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
        dbg.subscribe(1, w);
        check("EVT-COP-63", "a Wait-only subscription arms the Wait site alone",
              emu.copper().wait_events_armed() &&
              !emu.copper().move_events_armed() &&
              !emu.copper().halt_events_armed());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription n;
        n.kind = EventKind::NextRegWrite;
        dbg.subscribe(1, n);
        check("EVT-COP-64", "a NextRegWrite subscription alone still arms the MOVE "
                            "site — one MOVE latch fans out to both kinds",
              emu.copper().move_events_armed() &&
              !emu.copper().wait_events_armed() &&
              !emu.copper().halt_events_armed());
    }
    {
        // The DMA twin: Start/End and Byte were already separable, but End was
        // folded in with Start. Three flags now.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription e;
        e.kind = EventKind::Dma;
        e.filter.dma_kind = jnext::dbg::DmaEventKind::End;
        dbg.subscribe(1, e);
        check("EVT-COP-65", "an End-only DMA subscription arms End and NOT Start",
              emu.dma().end_events_armed() && !emu.dma().start_events_armed() &&
              !emu.dma().byte_events_armed());
        check("EVT-COP-66", "and the composite events_armed() still reports true",
              emu.dma().events_armed());
    }

    // ── EVT-DEL-60..64 — a nested delivery must not wipe the boundary's hits ─
    //
    // §4.3 makes `matched[]` part of the `Paused` contract and B3 is its first
    // consumer. `raise_host_event()` cleared it unconditionally, and a `Handler`
    // may call that verb from inside the drain's loop — which
    // `deliver_to_subscribers` documents as supported. The list also MOVED, from
    // `Debugger::Impl` (a private nested struct nothing could observe, which is
    // why no row could see this) to `EventTable::hits()`.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        // Two Mem subscriptions on the same write, both Stop, so the boundary has
        // two hits to lose; the FIRST one's handler raises a host event.
        Subscription host;
        host.kind = EventKind::Host; host.action = Action::Continue;
        dbg.subscribe(1, host);

        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Stop;
        a.handler = nullptr;                      // static Stop, records hit #1
        const auto first = dbg.subscribe(1, a);
        // The NESTED raise is on the SECOND subscription, deliberately: a handler
        // runs BEFORE its own hit is recorded, so nesting from the FIRST one clears
        // a list that is still empty and the bug is invisible. Nesting from the
        // second clears a list that already holds hit #1 — which is the only
        // ordering in which an unconditional clear can be seen at all.
        Subscription b = a;
        b.handler = [](const DbgEvent&, Debugger& d) {
            d.raise_host_event(1, "nested");
            return Action::Stop;
        };
        const auto second = dbg.subscribe(2, b);

        emu.run_frame();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-DEL-60", "the machine stopped", dbg.state().paused);
        check("EVT-DEL-61", "and the boundary's hit list survived the NESTED "
                            "raise_host_event — both subscriptions are in it",
              t->hits().size() == 2,
              "hits=" + std::to_string(t->hits().size()));
        check("EVT-DEL-62", "naming both, in the order they matched",
              t->hits().size() == 2 && t->hits()[0].event_id == first.value &&
              t->hits()[1].event_id == second.value);
        check("EVT-DEL-63", "with the address and direction that matched",
              t->hits().size() == 2 && t->hits()[0].addr == 0x9000 &&
              t->hits()[0].access == Access::Write &&
              t->hits()[0].value == 0x5A);
        check("EVT-DEL-64", "and the FIRST Stop is still the pause reason",
              dbg.state().pause_reason.id == first.value);
    }
    {
        // A TOP-LEVEL raise_host_event still clears: it starts its own batch.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Stop;
        dbg.subscribe(1, s);
        dbg.raise_host_event(1, "one");
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-DEL-65", "a top-level raise leaves exactly its own hit",
              t->hits().size() == 1, "hits=" + std::to_string(t->hits().size()));
        dbg.run(1);
        dbg.raise_host_event(1, "two");
        check("EVT-DEL-66", "and a second one replaces it rather than appending",
              t->hits().size() == 1, "hits=" + std::to_string(t->hits().size()));
    }

    // ── EVT-TIME-23..27 — `Cycle` is one-shot, and enforced ────────────────
    //
    // `events.h` says "one-shot BY NATURE" and nothing enforced it: the filter is
    // `master_cycle >= N`, so once the target passes it matches at EVERY boundary
    // for the rest of the session, `cycle_armed_` keeps `events_pending()`
    // permanently true, and `action = Stop` makes the machine unadvanceable.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = emu.clock().get() + 2000;
        s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(1, s);
        check("EVT-TIME-23", "subscribe() FORCES `once` on a Cycle subscription, and "
                             "the model a client lists says so",
              !dbg.subscriptions(true).empty() &&
              dbg.subscriptions(true).front().once);
        emu.run_frame();
        check("EVT-TIME-24", "it fires exactly once over a whole frame, not at every "
                             "boundary past its target",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-TIME-25", "and it is spent, so the per-boundary drain retires",
              !dbg.subscriptions(true).front().live &&
              !emu.debug_state().events_pending());
        emu.run_frame();
        check("EVT-TIME-26", "a second frame adds nothing",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        (void)sub;
    }
    {
        // The one that made the machine unadvanceable: Stop.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = emu.clock().get() + 2000;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-27", "a Cycle Stop pauses once",
              dbg.state().paused);
        const uint64_t at_stop = emu.clock().get();
        dbg.run(1);
        emu.run_frame();
        check("EVT-TIME-28", "and the machine ADVANCES after the resume — it does not "
                             "re-pause on the same passed target for ever",
              !dbg.state().paused && emu.clock().get() > at_stop,
              "clock=" + std::to_string(emu.clock().get()) + " was=" +
                  std::to_string(at_stop));
    }

    // ── EVT-SUB-20..23 — the refusal set's two remaining members ───────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription dbgsrc;
        dbgsrc.kind = EventKind::Mem; dbgsrc.access = Access::Write;
        dbgsrc.filter.source = EventSource::Debugger;
        check("EVT-SUB-20", "a filter naming EventSource::Debugger is refused — that "
                            "source never appears in a delivered Event (§4.2a)",
              dbg.subscribe(1, dbgsrc).status == Result::RefusedUnavailable);

        Subscription defport;
        defport.kind = EventKind::Port;      // DEFAULT filter: mask 0xFFFF, value 0
        check("EVT-SUB-21", "a Port subscription left with the default filter is "
                            "refused — it would match only port 0x0000, where every "
                            "other kind's default matches everything",
              dbg.subscribe(1, defport).status == Result::RefusedUnavailable);

        Subscription okport = defport;
        okport.filter.port_mask = 0x00FF; okport.filter.port_value = 0xFE;
        check("EVT-SUB-22", "and setting the mask makes it acceptable",
              dbg.subscribe(1, okport).status == Result::Ok);
        Subscription zeroport = defport;
        zeroport.filter.port_mask = 0xFFFF; zeroport.filter.port_value = 0x0001;
        check("EVT-SUB-23", "an EXPLICIT full-decode filter on a real port is fine — "
                            "the refusal is about the default, not about the value",
              dbg.subscribe(1, zeroport).status == Result::Ok);
    }

    // ── EVT-SUB-30..32 — erase_client()'s defining condition ───────────────
    {
        EventTable t;
        Subscription s; s.kind = EventKind::Execute;
        const EventId a1 = t.add(7, s);
        const EventId a2 = t.add(7, s);
        const EventId b1 = t.add(8, s);
        check("EVT-SUB-30", "erase_client() drops every row of THAT client and counts "
                            "them",
              t.erase_client(7) == 2 && t.find(a1) == nullptr &&
              t.find(a2) == nullptr);
        check("EVT-SUB-31", "and leaves another client's alone",
              t.find(b1) != nullptr && t.find(b1)->live);
        check("EVT-SUB-32", "a second call for the same client drops nothing",
              t.erase_client(7) == 0);
    }

    // ── EVT-STEP-10..14 — B-9: the two `event_stop_pending_` conditionals ──
    //
    // `debugger_step()`'s halt-run loop condition and its consume both survived
    // mutation, and the manifest listed "the whole debugger_step() drain" as a
    // closure. A Stop from a kind whose site is in the device cluster (Copper) or
    // at a line boundary (Scanline) is what reaches them — an `Execute` stop never
    // does, because the pre-instruction gate pauses before the step body runs.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x00, 0x00, 0x18, 0xFB });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        // Step until the scanline is crossed. The stop arrives through
        // event_stop_pending_, which debugger_step() must consume.
        int steps = 0;
        while (steps < 40000 && dbg.events_fired_since(0).empty()) {
            dbg.step_into(1);
            ++steps;
        }
        check("EVT-STEP-10", "a Scanline Stop is delivered on the STEP path",
              !dbg.events_fired_since(0).empty(),
              "steps=" + std::to_string(steps));
        check("EVT-STEP-11", "and the machine is paused after it",
              dbg.state().paused);
        // The consume is what this row is for: if `event_stop_pending_` were left
        // set, the NEXT step at a HALT would exit its run-out loop immediately.
        // Put the CPU on a HALT and step: it must run the halt out (GH #207).
        {
            Z80Registers r = emu.cpu().get_registers();
            emu.mmu().write(0xB000, 0x76);          // HALT
            r.PC = 0xB000; r.IFF1 = 1; r.IM = 1;
            emu.cpu().set_registers(r);
        }
        const int t_states = dbg.step_into(1) == Result::Ok
                                 ? emu.debugger_step()
                                 : 0;
        check("EVT-STEP-12", "a later Step at a HALT still runs the halt out — the "
                             "pending-Stop latch was consumed, not left standing",
              t_states > 100, "tstates=" + std::to_string(t_states));
    }

    // ── EVT-REPLAY-10..12 — B-9: the execute_gate replay guard ─────────────
    //
    // `drain_boundary`'s replay guard has a row; `execute_gate`'s twin did not.
    // §4.2a: `rewind_to_cycle()` fast-forwards with the gate LIVE, so an
    // `Execute` subscription inside the replayed span would pause the replay short.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x00, 0x00, 0x18, 0xFB });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.set_replay_mode(true);
        emu.run_frame();
        check("EVT-REPLAY-10", "an Execute subscription does NOT stop a replay",
              !dbg.state().paused && dbg.events_fired_since(0).empty(),
              "pc=" + hex(pc_of(emu)));
        check("EVT-REPLAY-11", "and the replay really ran the address",
              pc_of(emu) == 0x8000 || pc_of(emu) == 0x8003 ||
              pc_of(emu) == 0x8002 || pc_of(emu) == 0x8001);
        emu.set_replay_mode(false);
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; emu.cpu().set_registers(r);
        }
        emu.run_frame();
        check("EVT-REPLAY-12", "with replay off the same subscription DOES stop it — "
                               "the control the guard's row needs",
              dbg.state().paused && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-TRANS-40..43 — B-9: a NON-Execute Stop drops the transients ────
    //
    // `drain_boundary`'s `if (stop) apply_stop();` survived mutation; only the
    // `execute_gate` twin was caught, so transient auto-removal was proven for an
    // `Execute`-caused stop alone.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription tr;
        tr.kind = EventKind::Execute; tr.transient = true;
        tr.filter.lo = 0x8007; tr.filter.hi = 0x8007;
        tr.action = Action::Stop;
        dbg.subscribe(1, tr);
        Subscription mem;
        mem.kind = EventKind::Mem; mem.access = Access::Write;
        mem.filter.lo = 0x9000; mem.filter.hi = 0x9000;
        mem.action = Action::Stop;
        dbg.subscribe(1, mem);
        check("EVT-TRANS-40", "both are armed", dbg.subscriptions(true).size() == 2);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-41", "the MEM subscription stops first, at the writer's "
                              "boundary",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-42", "and that stop dropped the TRANSIENT — auto-removal is "
                              "not an Execute-only rule",
              dbg.subscriptions(true).size() == 1 &&
              !dbg.subscriptions(true).front().transient);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-43", "so the machine runs past the retired target",
              !dbg.state().paused && pc_of(emu) == 0x8007,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // B-9: `clear_transient()`'s `if (n) refresh();`. Without the refresh a
        // tombstoned transient keeps `live == true` and fires AGAIN, which
        // `subscriptions()` (which reads `removed`) cannot see.
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFA });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Rec rec;
        Subscription tr;
        tr.kind = EventKind::Execute; tr.transient = true;
        tr.filter.lo = 0x8002; tr.filter.hi = 0x8002;
        tr.action = Action::Stop; tr.handler = nullptr;
        dbg.subscribe(1, tr);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-50", "the transient fired and the machine stopped there",
              dbg.state().paused && pc_of(emu) == 0x8002);
        check("EVT-TRANS-51", "and it is gone from the model",
              dbg.subscriptions(true).empty());
        dbg.run(1);
        run_until_paused(emu, 2);
        check("EVT-TRANS-52", "re-reaching the SAME address does not stop again — the "
                              "retired transient is not merely tombstoned, it is no "
                              "longer LIVE",
              !dbg.state().paused, "pc=" + hex(pc_of(emu)));
        (void)rec;
    }

    // ── EVT-DEL-70..73 — B-9: contract 4, a removal during a drain ─────────
    //
    // `const bool outer = !draining` survived mutation because no fixture had the
    // shape: two subscriptions matching ONE event where the first removes a row.
    // Compaction is deferred to the outer frame for exactly this.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int first_calls = 0, second_calls = 0;
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Continue;
        const auto second_id_holder = std::make_shared<EventId>(0);
        a.handler = [&first_calls, second_id_holder](const DbgEvent&, Debugger& d) {
            ++first_calls;
            // Remove the OTHER subscription from inside the drain, while the loop
            // is still walking the table.
            d.unsubscribe(1, *second_id_holder);
            return Action::Continue;
        };
        const auto first = dbg.subscribe(1, a);
        Subscription b = a;
        b.handler = [&second_calls](const DbgEvent&, Debugger&) {
            ++second_calls;
            return Action::Continue;
        };
        *second_id_holder = dbg.subscribe(1, b).value;

        emu.run_frame();
        check("EVT-DEL-70", "the first handler ran",
              first_calls == 1, "n=" + std::to_string(first_calls));
        check("EVT-DEL-71", "and removing the second MID-DRAIN suppressed it for this "
                            "delivery without invalidating the walk",
              second_calls == 0, "n=" + std::to_string(second_calls));
        check("EVT-DEL-72", "the removal really took",
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true).front().id == first.value);
        check("EVT-DEL-73", "and the machine ran on",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
    }

    {
        // B-9 / contract 4: `const bool outer = !draining` survived mutation,
        // because no fixture had a NESTED delivery that also REMOVES a row. With
        // `outer` forced true the nested frame runs `compact()` — erasing a
        // tombstone out of `subs_` — while the OUTER loop is still indexing it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int mem_calls = 0, host_calls = 0, third_calls = 0;
        const auto victim = std::make_shared<EventId>(0);

        // A Host subscription whose handler UNSUBSCRIBES a third subscription.
        Subscription hs;
        hs.kind = EventKind::Host; hs.action = Action::Continue;
        hs.handler = [&host_calls, victim](const DbgEvent&, Debugger& d) {
            ++host_calls;
            d.unsubscribe(1, *victim);
            return Action::Continue;
        };
        dbg.subscribe(1, hs);

        // The FIRST Mem subscription raises the host event, i.e. nests a delivery
        // that removes a row, while this boundary's own loop is still walking.
        Subscription m1;
        m1.kind = EventKind::Mem; m1.access = Access::Write;
        m1.filter.lo = 0x9000; m1.filter.hi = 0x9000;
        m1.action = Action::Continue;
        m1.handler = [&mem_calls](const DbgEvent&, Debugger& d) {
            ++mem_calls;
            d.raise_host_event(1, "nested");
            return Action::Continue;
        };
        dbg.subscribe(1, m1);

        // ...and a THIRD, later in the table, which the outer loop must still
        // reach after the nested frame has been and gone.
        Subscription m2 = m1;
        m2.handler = [&third_calls](const DbgEvent&, Debugger&) {
            ++third_calls;
            return Action::Continue;
        };
        *victim = dbg.subscribe(1, m2).value;

        emu.run_frame();
        check("EVT-DEL-80", "the outer handler ran and nested a delivery",
              mem_calls == 1 && host_calls == 1,
              "mem=" + std::to_string(mem_calls) + " host=" +
                  std::to_string(host_calls));
        check("EVT-DEL-81", "the nested frame did NOT compact the table under the "
                            "outer loop — the removed row is still reachable as a "
                            "tombstone for this boundary",
              third_calls == 0, "third=" + std::to_string(third_calls));
        check("EVT-DEL-82", "and the removal took effect once the outer frame ended",
              dbg.subscriptions(true).size() == 2);
        check("EVT-DEL-83", "with the machine none the worse for it",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        // THE MECHANISM, checkable. Erasing a row under the drain's own index is
        // UNDEFINED, not reliably observable, so EVT-DEL-81 above cannot be the row
        // for contract 4 — a row that depends on UB being visible is not a row.
        // Exactly one compaction must have run for this boundary: the outer frame's.
        check("EVT-DEL-84", "and the nested frame performed NO compaction — contract "
                            "4 defers it to the outer delivery frame",
              emu.debug_state().event_table()->compactions() == 1,
              "compactions=" +
                  std::to_string(emu.debug_state().event_table()->compactions()));
    }

    // ── EVT-ST-01..12 — SAVE / LOAD / REWIND against the new event state ────
    //
    // THE LARGEST UNTESTED SURFACE IN THE PACKAGE, and the decision is written at
    // `Emulator::debug_after_state_restore_()`. In short: the subscription model
    // is HOST-SIDE SESSION state and is deliberately NOT serialised — §4.2a's own
    // precedent, "a mutation is machine state, so the next frame-boundary snapshot
    // carries it; interpreter state (script variables, `once` flags) is not", and
    // `once` flags ARE `EventTable` state; a `Condition` and a `Handler` are
    // closures over a subscriber's interpreter and cannot be serialised at all.
    // What IS reconciled is every piece that DESCRIBES the machine that has gone:
    // the latch ring, the pending Stop, the CTL-13 stop evidence, and
    // `slot_page_` — the one piece that is a cache of machine state.
    //
    // Rows BOTH WAYS: what survives a load, and what must not.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);

        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        dbg.set_master_enabled(false);
        dbg.set_client_enabled(4, false);

        auto saved = dbg.save_state_bytes(4, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-01", "a snapshot is taken",
              saved.status == Result::Ok && !saved.value.empty());

        // Change the session AFTER the save: a load must not undo any of it.
        dbg.set_master_enabled(true);
        dbg.set_client_enabled(4, true);
        const auto extra = dbg.subscribe(5, s);

        check("EVT-ST-02", "two subscriptions and both switches on before the load",
              dbg.subscriptions(true).size() == 2 && dbg.master_enabled() &&
              dbg.client_enabled(4));

        const Result lr = dbg.load_state_bytes(4, saved.value.data(),
                                               saved.value.size());
        check("EVT-ST-03", "the load succeeds", lr == Result::Ok);
        check("EVT-ST-04", "the SUBSCRIPTIONS survive it — a load must not resurrect "
                           "one the user deleted nor delete one they added",
              dbg.subscriptions(true).size() == 2 &&
              dbg.subscriptions(true)[0].id == sub.value &&
              dbg.subscriptions(true)[1].id == extra.value);
        check("EVT-ST-05", "and so do the master and per-client switches",
              dbg.master_enabled() && dbg.client_enabled(4));
        // The delivery cursor: a client's `since(seq)` must stay valid across a
        // load, i.e. `seq` must keep counting rather than restart.
        dbg.set_master_enabled(true);
        dbg.run(4);
        emu.run_frame();
        const auto after = dbg.events_fired_since(0);
        check("EVT-ST-06", "the delivery-sequence cursor keeps counting across the "
                           "load rather than restarting",
              !after.empty() && after.back().seq > 0,
              "n=" + std::to_string(after.size()));
    }
    {
        // The other half: the ring, the pending Stop and the stop evidence are all
        // reconciled, because each describes a machine the load has replaced.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        attach_and_pause(emu);
        auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-10", "a snapshot for the reconciliation rows",
              saved.status == Result::Ok);

        // Latch something without draining it: a Scanline subscription plus a
        // frame, with the machine paused before the drain that would empty it.
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue;
        dbg.subscribe(1, s);
        // A legacy watchpoint stop, so `watch_stop_` is set and survives into a
        // paused state that no resume will clear.
        emu.debug_state().breakpoints().add_watchpoint(0x8000, WatchType::READ);
        dbg.run(1);
        emu.run_frame();
        check("EVT-ST-11", "the machine stopped on the legacy watchpoint, so CTL-13 "
                           "has evidence to lose",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);

        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        // Force a ring entry that the load must discard.
        jnext::dbg::LatchEntry le;
        le.kind = EventKind::Scanline;
        emu.debug_state().latch_event(le);
        check("EVT-ST-12", "and the ring holds an undrained entry",
              t->size() > 0, "size=" + std::to_string(t->size()));

        dbg.load_state_bytes(1, saved.value.data(), saved.value.size());
        check("EVT-ST-13", "the load DISCARDS the ring — its entries carry a pc, a "
                           "cycle and a frame from a machine that no longer exists",
              t->size() == 0 && t->dropped() == 0 &&
              !emu.debug_state().events_pending(),
              "size=" + std::to_string(t->size()));
        check("EVT-ST-14", "and it clears the CTL-13 stop evidence, which `unpause_()` "
                           "cannot because a load while PAUSED never unpauses",
              !emu.debug_state().watch_stop() && !emu.debug_state().magic_stop() &&
              dbg.state().pause_reason.kind != PauseReason::Kind::Watch);
    }
    {
        // `slot_page_` is the one piece that IS a cache of machine state: the load
        // rewrites the MMU page map through a `StateDesc` walk, which fires no
        // mapping notification at all, so it has to be re-derived.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        attach_and_pause(emu);
        emu.mmu().set_page(4, 0x2A);
        auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-20", "snapshot taken with page 0x2A at slot 4",
              saved.status == Result::Ok &&
              emu.mmu().get_effective_page(4) == 0x2A);

        // Map it away, then subscribe to it, then load the snapshot back.
        emu.mmu().set_page(4, 0x10);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x2A };
        dbg.subscribe(1, s);
        check("EVT-ST-21", "with the page mapped away, nothing is armed",
              emu.debug_state().wr_watch_mask() == 0);

        dbg.load_state_bytes(1, saved.value.data(), saved.value.size());
        check("EVT-ST-22", "the load restores page 0x2A to slot 4",
              emu.mmu().get_effective_page(4) == 0x2A);
        check("EVT-ST-23", "and the page-qualified filter's slot mask follows it — "
                           "the cache is not trusted across a restore",
              emu.debug_state().wr_watch_armed(0x8000),
              "mask=" + std::to_string(emu.debug_state().wr_watch_mask()));
        // THE PROPERTY, not the code: the mask must agree with the MMU for every
        // slot after a load, by whichever mechanism gets it there. (Two do:
        // `Mmu::load_state`'s own closing `rebuild_ptr` loop, and
        // `debug_after_state_restore_()`'s re-derive. The row outlives either.)
        bool agrees = true;
        for (int sl = 0; sl < 8; ++sl) {
            const bool want = emu.mmu().get_effective_page(sl) == 0x2A;
            const bool got  =
                emu.debug_state().wr_watch_armed(static_cast<uint16_t>(sl << 13));
            agrees = agrees && (want == got);
        }
        check("EVT-ST-24", "and the invariant holds for EVERY slot after the load",
              agrees, "mask=" + std::to_string(emu.debug_state().wr_watch_mask()));
    }
    {
        // A REWIND routes through the same load, so the same reconciliation covers
        // it. The row is that the ring does not survive a step_back.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        for (size_t i = 0; i < 3; ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i),
                            static_cast<uint8_t>(i == 0 ? 0x00 : (i == 1 ? 0x18 : 0xFD)));
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
        }
        emu.debug_state().set_active(true);
        Debugger dbg(emu);
        emu.run_frame();
        emu.run_frame();
        attach_and_pause(emu);

        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue;
        const auto sub = dbg.subscribe(1, s);
        jnext::dbg::LatchEntry le;
        le.kind = EventKind::Scanline;
        emu.debug_state().latch_event(le);
        const bool had_entry = emu.debug_state().event_table()->size() > 0;

        const jnext::dbg::RewindRange range = dbg.rewind_range();
        const Result rr = dbg.rewind_to_frame(1, range.oldest_frame);
        check("EVT-ST-30", "a rewind to the oldest buffered frame is accepted",
              rr == Result::Ok,
              "oldest=" + std::to_string(range.oldest_frame) + " newest=" +
                  std::to_string(range.newest_frame) + " rc=" +
                  std::to_string(static_cast<int>(rr)));
        check("EVT-ST-31", "and it discarded the undrained ring, like any load — a "
                           "rewind routes through load_state, so one reconciliation "
                           "covers both",
              had_entry && emu.debug_state().event_table()->size() == 0 &&
              !emu.debug_state().events_pending());
        check("EVT-ST-32", "while the subscription itself survived it",
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true).front().id == sub.value);
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
