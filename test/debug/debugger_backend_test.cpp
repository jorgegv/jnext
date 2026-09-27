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

#include <cstring>
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
        const auto slots = dbg.mmu_slots();
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

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
