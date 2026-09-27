// jnext::dbg::Debugger — the backend suite, work package B1 of epic #276.
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
// No ROM, no SD image: a 48K or +3 machine with a program written straight into
// RAM and PC/SP set by hand — the `step_out_test` / `rewind_test` idiom.
//
// Run: ./build/test/debugger_backend_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/rewind_buffer.h"
#include "input/keyboard.h"
#include "memory/mmu.h"
#include "peripheral/dma.h"
#include "video/palette.h"
#include "video/sprites.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::Result;

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
        dbg.run_to(3, AFTER_CALL);                 // arms the one-shot, resumes
        check("CTL-02-04", "a redundant run() while running keeps the pending run-to target",
              dbg.run(7) == Result::Ok &&
              emu.debug_state().breakpoints().has_oneshot() &&
              emu.debug_state().breakpoints().oneshot_addr() == AFTER_CALL);
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
        check("PEND-B2-01", "subscribe() (B2) refuses as unsupported",
              dbg.subscribe(1, jnext::dbg::Subscription{}).status == Result::Unsupported);
        check("PEND-B2-02", "and the model lists nothing rather than half a model",
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
        // probe_execute is answered for real over today's model, not refused.
        emu.debug_state().breakpoints().add_pc(0x1234);
        check("PEND-B2-03", "probe_execute() answers over today's breakpoint set",
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

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
