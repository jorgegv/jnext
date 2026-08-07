// Resume steps off the breakpoint it is standing on — GH #221.
//
// The defect: DebugState::resume() cleared paused_ and nothing else, and the
// hot loop's breakpoint gate runs BEFORE the instruction at PC. So Run/F5 at a
// breakpoint re-matched the unchanged PC on the very next iteration and the
// machine re-paused having executed nothing. Measured on the default
// configuration, no CLI flag involved:
//
//     After free-run:                  paused=1 PC=0x8006
//     After resume():                  paused=0 PC=0x8006
//     After 4 more run_frame() calls:  paused=1 PC=0x8006
//
// It was never F5-only. Step Over, Step Out, Run to Here and Run to EOF/EOSL
// all clear paused_ through the same gate, so all of them were pinned in place
// by a breakpoint under the PC too.
//
// The fix is one instruction wide: every transition out of paused arms a
// step-off, and the gate consumes it on the FIRST breakpoint test of the
// resumed run — the one that, by construction, can only be looking at the
// address the user pressed Run at. Everything else still fires.
//
// Two halves:
//
//   RSOP-*  the DebugState predicate — consume_step_off() is one-shot, every
//           resume-family transition arms it, and disarming breakpoints drops
//           it.
//
//   RSOW-*  the WIRING — a real Emulator, a real Z80 program, real
//           breakpoints/watchpoints, driven through run_frame() exactly as a
//           frontend drives it. This is where "the machine actually made
//           progress" is asserted, and where the deliberate NON-suppressions
//           are pinned: a breakpoint at the next address, and the same
//           breakpoint on the next pass round a loop, must both still stop the
//           machine.
//
// No ROM, no SD image: 48K machines with programs written straight into RAM
// and PC/SP set by hand (same idiom as test/debug/step_out_test.cpp and
// test/debug/persistent_bp_test.cpp).
//
// NOTE FOR ANYONE MUTATION-TESTING THIS SUITE — two ways to break the same
// mechanism kill DIFFERENT row counts, and neither number is wrong:
//
//   * mutate the CALL SITE (drop `!debug_state_.consume_step_off() &&` from
//     run_frame()'s gate)          -> 11 kills, all RSOW-*.
//   * mutate the FUNCTION BODY (stub consume_step_off() to return false)
//                                  -> 14 kills: the same 11, plus RSOP-02,
//     -03 and -05, which call the predicate directly and never reach the gate.
//
// The RSOP rows are predicate-level by design, so a call-site mutation cannot
// touch them. Two reviewers reached different totals for exactly this reason.
//
// Run: ./build/test/resume_step_off_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

// ── Tiny test harness (matches test/debug/persistent_bp_test.cpp style) ─

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = std::string()) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
        if (!detail.empty())
            std::printf("        %s\n", detail.c_str());
    }
}

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Machine fixtures ───────────────────────────────────────────────────

static constexpr uint16_t TEST_SP = 0xFF00;
static constexpr uint16_t DATA    = 0x9000;

// LINEAR — the #221 repro fixture, byte-identical to persistent_bp_test's so
// the two suites are talking about the same machine:
//
//   8000  00 x6        NOP x6
//   8006  3A 00 90     LD A,(0x9000)   <- BP_ADDR; also the watched READ
//   8009  3E 5A        LD A,0x5A
//   800B  32 00 90     LD (0x9000),A   <- the watched WRITE
//   800E  18 FE        JR $            <- park
static constexpr uint16_t LIN_PROG = 0x8000;
static constexpr uint16_t LIN_BP   = 0x8006;
static constexpr uint16_t LIN_NEXT = 0x8009;
static constexpr uint16_t LIN_WR   = 0x800B;
static constexpr uint16_t LIN_PARK = 0x800E;

// LOOP — two instructions, so "the machine moved" and "the machine is back at
// the same PC" are both observable at once:
//
//   8000  3E 00        LD A,0
//   8002  3C           INC A           <- LOOP_TOP; A counts the laps
//   8003  18 FD        JR LOOP_TOP
static constexpr uint16_t LOOP_PROG = 0x8000;
static constexpr uint16_t LOOP_TOP  = 0x8002;

// CALL — for Step Out armed inside a subroutine that starts on a breakpoint:
//
//   8000  CD 00 90     CALL 0x9000
//   8003  00           NOP             <- where Step Out must land
//   8004  18 FE        JR $            <- park
//   9000  00           NOP             <- SUB_ENTRY; the breakpoint
//   9001  C9           RET
static constexpr uint16_t CALL_PROG   = 0x8000;
static constexpr uint16_t CALL_RETURN = 0x8003;
static constexpr uint16_t SUB_ENTRY   = 0x9000;

static void load(Emulator& emu, uint16_t at, const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i)
        emu.mmu().write(static_cast<uint16_t>(at + i), bytes[i]);
}

// Common machine setup: 48K, interrupts off (no frame interrupt landing in the
// middle of a measured run), SP parked well away from the program.
static void boot(Emulator& emu, uint16_t pc, bool persistent = false) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.persistent_breakpoints = persistent;
    emu.init(cfg);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = pc;
    r.SP   = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

static void build_linear(Emulator& emu, bool persistent = false) {
    boot(emu, LIN_PROG, persistent);
    static const uint8_t prog[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x90,
        0x3E, 0x5A,
        0x32, 0x00, 0x90,
        0x18, 0xFE,
    };
    load(emu, LIN_PROG, prog, sizeof(prog));
    emu.mmu().write(DATA, 0x00);
}

static void build_loop(Emulator& emu) {
    boot(emu, LOOP_PROG);
    static const uint8_t prog[] = { 0x3E, 0x00, 0x3C, 0x18, 0xFD };
    load(emu, LOOP_PROG, prog, sizeof(prog));
}

static void build_call(Emulator& emu) {
    boot(emu, CALL_PROG);
    static const uint8_t main_[] = { 0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE };
    static const uint8_t sub_[]  = { 0x00, 0xC9 };
    load(emu, CALL_PROG, main_, sizeof(main_));
    load(emu, SUB_ENTRY, sub_,  sizeof(sub_));
}

// Free-run up to `max_frames` frames, stopping as soon as the debugger pauses.
static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc(Emulator& emu)  { return emu.cpu().get_registers().PC; }
static uint8_t  reg_a(Emulator& emu) {
    return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("Resume steps off its breakpoint (GH #221)\n");
    std::printf("======================================================\n\n");

    // ─────────────── RSOP: the DebugState predicate ────────────────────

    // RSOP-01 — a fresh DebugState has nothing armed. Without this, a
    // permanently-true predicate would pass every other predicate row.
    {
        DebugState ds;
        check("RSOP-01", "fresh DebugState: no step-off armed",
              !ds.consume_step_off());
    }

    // RSOP-02 — resume() arms it, and it is ONE-SHOT. The second call is what
    // makes the suppression exactly one instruction wide; a predicate that
    // stayed true would go deaf to every later breakpoint.
    {
        DebugState ds;
        ds.set_active(true);
        ds.pause();
        ds.resume();
        const bool first  = ds.consume_step_off();
        const bool second = ds.consume_step_off();
        check("RSOP-02", "resume() arms the step-off exactly once",
              first && !second);
    }

    // RSOP-03 — EVERY transition out of paused arms it, not just resume().
    // This is the row that fails if a new resume path is added later and
    // forgets: they all go through DebugState::unpause_() for that reason.
    // Step Over / Step Out / Run to Here / Run to EOF all resumed onto their
    // own breakpoint before #221 in exactly the way F5 did.
    {
        struct { const char* what; void (*arm)(DebugState&); } cases[] = {
            { "resume",             [](DebugState& d){ d.resume(); } },
            { "step_into",          [](DebugState& d){ d.step_into(); } },
            { "step_over",          [](DebugState& d){ d.step_over(0x8003); } },
            { "step_out",           [](DebugState& d){ d.step_out(0xFF00); } },
            { "run_to",             [](DebugState& d){ d.run_to(0x9000); } },
            { "run_to_cycle",       [](DebugState& d){ d.run_to_cycle(1000); } },
            { "step_back",          [](DebugState& d){ d.step_back(1); } },
            { "run_back_to_cycle",  [](DebugState& d){ d.run_back_to_cycle(1000); } },
        };
        bool all = true;
        std::string missing;
        for (const auto& c : cases) {
            DebugState ds;
            ds.set_active(true);
            ds.pause();
            c.arm(ds);
            const bool armed = ds.consume_step_off();
            if (!armed) { all = false; missing += std::string(" ") + c.what; }
        }
        check("RSOP-03", "every resume-family transition arms the step-off",
              all, fmt("did NOT arm:%s", missing.c_str()));
    }

    // RSOP-04 — pause() does NOT arm it. The arm belongs to leaving the paused
    // state, and a pause that armed one would let the very next breakpoint test
    // through for free.
    {
        DebugState ds;
        ds.set_active(true);
        ds.pause();
        check("RSOP-04", "pause() does not arm a step-off",
              !ds.consume_step_off());
    }

    // RSOP-06 — THE PRECONDITION. A resume-family call on an ALREADY-RUNNING
    // machine arms nothing, because there is no address the user is standing on
    // to protect: PC is wherever the free run has reached and keeps moving,
    // so an arm raised here would be spent on some later, unrelated breakpoint
    // test — swallowing a hit the user deliberately set. Only a real
    // paused -> running edge arms.
    //
    // Not hypothetical, and not reachable only through the API. GH #223 has
    // since closed the F5/Run route specifically — DebuggerManager::on_run()
    // now returns early when the machine is not paused, so neither the global
    // F5 handler nor the toolbar's "F5: Continue" button reaches resume() any
    // more — but this is a DebugState-level contract, not an on_run() one, and
    // the other resume-family entry points are unchanged: "Run to Here" still
    // has no pause check by deliberate design (run_to() sets a fresh one-shot,
    // which is sensible on a running machine), and set_enabled(false)'s
    // auto-resume calls resume() directly. RSOW-13 is the route that is still
    // live end to end; RSOW-12 now documents the DebugState contract rather
    // than a reachable F5 sequence.
    {
        struct { const char* what; void (*again)(DebugState&); } cases[] = {
            { "resume",             [](DebugState& d){ d.resume(); } },
            { "step_into",          [](DebugState& d){ d.step_into(); } },
            { "step_over",          [](DebugState& d){ d.step_over(0x8003); } },
            { "step_out",           [](DebugState& d){ d.step_out(0xFF00); } },
            { "run_to",             [](DebugState& d){ d.run_to(0x9000); } },
            { "run_to_cycle",       [](DebugState& d){ d.run_to_cycle(1000); } },
            { "step_back",          [](DebugState& d){ d.step_back(1); } },
            { "run_back_to_cycle",  [](DebugState& d){ d.run_back_to_cycle(1000); } },
        };
        bool all = true;
        std::string armed_anyway;
        for (const auto& c : cases) {
            DebugState ds;
            ds.set_active(true);
            ds.pause();
            ds.resume();                 // the real edge
            (void)ds.consume_step_off(); // ...spent by the first gate test
            c.again(ds);                 // now issued while already running
            if (ds.consume_step_off()) {
                all = false;
                armed_anyway += std::string(" ") + c.what;
            }
        }
        check("RSOP-06", "a resume-family call on an already-running machine "
              "arms nothing", all,
              fmt("armed anyway:%s", armed_anyway.c_str()));
    }

    // RSOP-05 — disarming breakpoints drops a pending arm with them. The gate
    // that consumes it does not run while !armed(), so an arm left over from a
    // resume issued just before the debugger was closed would survive, with PC
    // free-running the whole time, and suppress an unrelated test the moment
    // breakpoints were armed again. --persistent-breakpoints is the case that
    // must NOT lose it: armed() stays true there, so the arm stays too, and
    // that is what RSOW-09 exercises end to end.
    {
        DebugState off;
        off.set_active(true);
        off.pause();
        off.resume();
        off.set_active(false);          // armed() -> false: drop it
        off.set_active(true);
        const bool dropped = !off.consume_step_off();

        DebugState keep;
        keep.set_persistent_breakpoints(true);
        keep.set_active(true);
        keep.pause();
        keep.resume();
        keep.set_active(false);         // armed() stays true: keep it
        const bool kept = keep.armed() && keep.consume_step_off();

        check("RSOP-05", "disarming breakpoints drops the arm; persistence "
              "keeps it", dropped && kept,
              fmt("dropped=%d kept=%d", dropped ? 1 : 0, kept ? 1 : 0));
    }

    // ─────────────── RSOW: wiring, against a real machine ──────────────

    // RSOW-01 — THE DEFECT. Stop at a breakpoint, press Run, and the machine
    // must make progress. Before #221 the three states below read
    // 0x8006 / 0x8006 / 0x8006 with paused=1 at the end: the resume executed
    // nothing at all.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        const bool released = !emu.debug_state().paused();

        run_until_paused(emu);
        check("RSOW-01", "Run at a breakpoint resumes: PC advances past it and "
              "execution continues to the park",
              stopped && released && !emu.debug_state().paused() &&
              pc(emu) == LIN_PARK,
              fmt("stopped=%d released=%d after-run paused=%d PC=$%04X "
                  "(want $%04X)", stopped ? 1 : 0, released ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_PARK));
    }

    // RSOW-02 — and the step-off swallows NOTHING. A breakpoint on the very
    // next instruction still stops the machine, one instruction of progress
    // later. This is the row that fails if the fix is widened into "ignore
    // breakpoints for a while after a resume".
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        emu.debug_state().breakpoints().add_pc(LIN_NEXT);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        run_until_paused(emu);

        check("RSOW-02", "a breakpoint at the NEXT instruction still fires "
              "after the step-off",
              first && emu.debug_state().paused() && pc(emu) == LIN_NEXT,
              fmt("first=%d paused=%d PC=$%04X (want $%04X)", first ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_NEXT));
    }

    // RSOW-03 — nor is the suppression sticky. Round a two-instruction loop the
    // SAME breakpoint fires again on the next lap, with A proving the lap
    // happened. A "suppress while PC has not changed" fix passes RSOW-01/02 and
    // fails here — and would make a breakpoint on a `JR $` park loop
    // unbreakable.
    {
        Emulator emu;
        build_loop(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LOOP_TOP);
        run_until_paused(emu);
        const bool first  = emu.debug_state().paused() && pc(emu) == LOOP_TOP;
        const uint8_t a0  = reg_a(emu);

        emu.debug_state().resume();
        run_until_paused(emu);
        const uint8_t a1 = reg_a(emu);

        check("RSOW-03", "the same breakpoint fires again on the next lap of a "
              "loop (the step-off is one instruction, not sticky)",
              first && emu.debug_state().paused() && pc(emu) == LOOP_TOP &&
              a1 == static_cast<uint8_t>(a0 + 1),
              fmt("first=%d paused=%d PC=$%04X (want $%04X) A %u -> %u",
                  first ? 1 : 0, emu.debug_state().paused() ? 1 : 0,
                  pc(emu), LOOP_TOP, a0, a1));
    }

    // RSOW-04 — Run to Here (a one-shot) ON THE ADDRESS THE MACHINE IS ALREADY
    // STOPPED AT. This is the reachable one-shot-at-current-PC case:
    // DebuggerManager's Step Over cannot produce one (its one-shot is PC +
    // instruction length, and no instruction is zero bytes long), but
    // right-clicking "Run to Here" on the line you are stopped on can, and so
    // can F5 into a one-shot left by an earlier action.
    //
    // The specified behaviour is a full lap, which is what every other debugger
    // does and what the user asking for it means: run, and stop when control
    // comes BACK here. Before #221 it stopped instantly, having executed
    // nothing — indistinguishable from doing nothing at all.
    {
        Emulator emu;
        build_loop(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LOOP_TOP);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == LOOP_TOP;
        const uint8_t a0 = reg_a(emu);

        emu.debug_state().breakpoints().remove_pc(LOOP_TOP);   // one-shot only
        emu.debug_state().run_to(LOOP_TOP);
        run_until_paused(emu);
        const uint8_t a1 = reg_a(emu);

        check("RSOW-04", "Run to Here at the current PC runs one full lap "
              "instead of stopping instantly",
              first && emu.debug_state().paused() && pc(emu) == LOOP_TOP &&
              a1 == static_cast<uint8_t>(a0 + 1),
              fmt("first=%d paused=%d PC=$%04X (want $%04X) A %u -> %u",
                  first ? 1 : 0, emu.debug_state().paused() ? 1 : 0,
                  pc(emu), LOOP_TOP, a0, a1));
    }

    // RSOW-05 — Step Over from a breakpoint. The one-shot sits at PC + length,
    // so the step must execute the instruction under the breakpoint and stop
    // there. Before #221 the gate re-matched the breakpoint first and the step
    // ended where it began, with StepMode::OVER consumed by the pause — the
    // user pressed F10 and nothing happened.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == LIN_BP;

        // LD A,(0x9000) is 3 bytes; this is exactly what
        // DebuggerManager::on_step_over() computes for a call-like instruction.
        emu.debug_state().step_over(LIN_NEXT);
        run_until_paused(emu);

        check("RSOW-05", "Step Over from a breakpoint lands on the following "
              "instruction",
              first && emu.debug_state().paused() && pc(emu) == LIN_NEXT,
              fmt("first=%d paused=%d PC=$%04X (want $%04X)", first ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_NEXT));
    }

    // RSOW-06 — Step Out from a breakpoint at the subroutine's first
    // instruction. StepMode::OUT's predicate is post-instruction, so it was
    // never the broken half; what was broken is that the gate re-paused before
    // a single instruction of the subroutine ran, and pause() clears
    // step_mode_ — so F8 disarmed itself and the user was left exactly where
    // they started.
    {
        Emulator emu;
        build_call(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(SUB_ENTRY);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == SUB_ENTRY;

        emu.debug_state().step_out(emu.cpu().get_registers().SP);
        run_until_paused(emu);

        check("RSOW-06", "Step Out from a breakpoint at the subroutine entry "
              "returns to the caller",
              first && emu.debug_state().paused() && pc(emu) == CALL_RETURN,
              fmt("first=%d paused=%d PC=$%04X (want $%04X)", first ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), CALL_RETURN));
    }

    // RSOW-07 — Run to EOF / Run to EOSL (StepMode::RUN_TO_CYCLE) from a
    // breakpoint. Its own test is a clock comparison the gate reaches only
    // AFTER should_break, so a breakpoint under the PC used to stop it dead
    // before a single cycle was spent towards the target.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == LIN_BP;

        const uint64_t target = emu.clock().get() + 5000;
        emu.debug_state().run_to_cycle(target);
        run_until_paused(emu);

        check("RSOW-07", "Run to EOF/EOSL from a breakpoint reaches its target "
              "cycle instead of re-stopping on the breakpoint",
              first && emu.debug_state().paused() &&
              emu.clock().get() >= target && pc(emu) != LIN_BP,
              fmt("first=%d paused=%d clock=%llu (want >= %llu) PC=$%04X "
                  "(want != $%04X)", first ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0,
                  static_cast<unsigned long long>(emu.clock().get()),
                  static_cast<unsigned long long>(target), pc(emu), LIN_BP));
    }

    // RSOW-08 — a WATCHPOINT stop, then Run. The machine came to rest AFTER the
    // instruction that made the access, so PC is the next instruction; the
    // step-off must not swallow an execute breakpoint further along. Here the
    // read watchpoint on 0x9000 fires inside LD A,(0x9000) and the machine must
    // still stop at the execute breakpoint on 0x800B.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        emu.debug_state().breakpoints().add_pc(LIN_WR);
        run_until_paused(emu);
        const bool watched = emu.debug_state().paused() && pc(emu) == LIN_NEXT;

        emu.debug_state().resume();
        run_until_paused(emu);

        check("RSOW-08", "after a watchpoint stop, Run still honours an execute "
              "breakpoint further along",
              watched && emu.debug_state().paused() && pc(emu) == LIN_WR,
              fmt("watched=%d (PC after watch $%04X, want $%04X) paused=%d "
                  "PC=$%04X (want $%04X)", watched ? 1 : 0, pc(emu), LIN_NEXT,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_WR));
    }

    // RSOW-09 — the #219 consequence named in the issue. With
    // --persistent-breakpoints, closing the debugger window while stopped ON a
    // breakpoint runs DebuggerManager::set_enabled(false)'s exact sequence —
    // resume(), then set_active(false) — and armed() deliberately survives it.
    // Before #221 the resume landed straight back on the live breakpoint, which
    // fired and reopened the window the user had just closed. The arm must
    // survive the set_active(false) for this to work, which is the half of
    // RSOP-05 that keeps it.
    {
        Emulator emu;
        build_linear(emu, /*persistent=*/true);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        emu.debug_state().set_active(false);
        run_until_paused(emu);

        check("RSOW-09", "closing the debugger window at a hit does not "
              "immediately re-fire it (breakpoints still armed)",
              stopped && emu.debug_state().armed() &&
              !emu.debug_state().active() && !emu.debug_state().paused() &&
              pc(emu) == LIN_PARK,
              fmt("stopped=%d armed=%d active=%d paused=%d PC=$%04X (want $%04X)",
                  stopped ? 1 : 0, emu.debug_state().armed() ? 1 : 0,
                  emu.debug_state().active() ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_PARK));
    }

    // RSOW-10 — the same close WITHOUT persistence, then reopening the window.
    // The resume's arm is dropped when breakpoints go dead (RSOP-05), so the
    // breakpoint the machine happens to be sitting on when the window comes
    // back still fires. Without that drop the stale arm would eat the first
    // check of the reopened session.
    {
        Emulator emu;
        build_linear(emu, /*persistent=*/false);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        emu.debug_state().set_active(false);   // breakpoints go dead: arm dropped
        emu.debug_state().set_active(true);    // window back, still at LIN_BP
        run_until_paused(emu);

        check("RSOW-10", "reopening the debugger re-arms the breakpoint under "
              "the PC (no stale step-off)",
              stopped && emu.debug_state().paused() && pc(emu) == LIN_BP,
              fmt("stopped=%d paused=%d PC=$%04X (want $%04X)", stopped ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_BP));
    }

    // RSOW-11 — a user-initiated Pause that happens to land on a breakpoint
    // address. "Do not re-trigger the breakpoint I am standing on" is about
    // WHERE the machine is, not about why it stopped, so Run must leave it —
    // the same rule gdb applies to `continue` at a breakpointed $pc. Pause the
    // machine one instruction short of the breakpoint, step onto it, then Run.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().pause();
        // Walk to the breakpoint address with nothing armed, so the machine
        // arrives there by stepping rather than by a hit.
        while (pc(emu) != LIN_BP)
            emu.debugger_step();
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        const bool at_bp = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        run_until_paused(emu);

        check("RSOW-11", "Run from a Pause that landed on a breakpoint steps "
              "off it too",
              at_bp && !emu.debug_state().paused() && pc(emu) == LIN_PARK,
              fmt("at_bp=%d paused=%d PC=$%04X (want $%04X)", at_bp ? 1 : 0,
                  emu.debug_state().paused() ? 1 : 0, pc(emu), LIN_PARK));
    }

    // RSOW-12 — THE SWALLOWED HIT, end to end. A redundant Run on a machine
    // that is already running must not arm anything: PC is out in the program,
    // not under the user's cursor, so an arm raised there is spent on whatever
    // breakpoint test comes next — one the user deliberately set.
    //
    // The sequence WAS ordinary use: stop at a breakpoint, press Run, alt-tab
    // back to the game, press F5 again at the emulator window, then hit a
    // breakpoint — which, before the unpause_() edge check, did not stop.
    // GH #223 closed that particular door one level up (on_run() now returns
    // early when the machine is already running), so this row no longer stands
    // for a reachable F5 sequence. It stays as the DebugState-level contract:
    // resume() is public, set_enabled(false) calls it directly, and the rule
    // "an arm is raised only on a real paused -> running edge" is what keeps
    // any future caller from swallowing a hit. GH223-02 in debugger_menu_test
    // covers the F5 route itself, at the DebuggerManager level where the guard
    // actually lives.
    //
    // The park is rewritten to a ONE-WAY jump before the second Run, so the
    // breakpointed address is visited exactly once more: a self-loop would
    // re-enter it on the next instruction and hide the swallow completely.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();     // the REAL edge; its arm is spent below
        emu.run_frame();                // free-runs to the park at LIN_PARK
        const bool running = !emu.debug_state().paused() && pc(emu) == LIN_PARK;

        // JP 0x9100 at the park, and a park of its own at the destination, so
        // LIN_PARK executes exactly once more and the machine settles either
        // way — the two outcomes are then unambiguous.
        static const uint8_t oneway[] = { 0xC3, 0x00, 0x91 };
        static const uint8_t faraway[] = { 0x18, 0xFE };
        load(emu, LIN_PARK, oneway, sizeof(oneway));
        load(emu, 0x9100,   faraway, sizeof(faraway));
        emu.debug_state().breakpoints().add_pc(LIN_PARK);

        emu.debug_state().resume();     // REDUNDANT: the machine is running
        run_until_paused(emu);

        check("RSOW-12", "a redundant Run on a running machine does not swallow "
              "the next breakpoint",
              stopped && running && emu.debug_state().paused() &&
              pc(emu) == LIN_PARK,
              fmt("stopped=%d running=%d paused=%d PC=$%04X (want $%04X; "
                  "$9100 means the hit was swallowed)", stopped ? 1 : 0,
                  running ? 1 : 0, emu.debug_state().paused() ? 1 : 0,
                  pc(emu), LIN_PARK));
    }

    // RSOW-13 — the same for Run to Here, which has the identical ungated path:
    // DisasmPanel emits run_to_requested from both Enter and the context menu,
    // and DebuggerManager's handler calls run_to(addr) with no pause check. The
    // one-shot here targets an address the program never reaches, so the ONLY
    // thing under test is whether the call armed a step-off it had no right to.
    {
        Emulator emu;
        build_linear(emu);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(LIN_BP);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == LIN_BP;

        emu.debug_state().resume();
        emu.run_frame();
        const bool running = !emu.debug_state().paused() && pc(emu) == LIN_PARK;

        static const uint8_t oneway[] = { 0xC3, 0x00, 0x91 };
        static const uint8_t faraway[] = { 0x18, 0xFE };
        load(emu, LIN_PARK, oneway, sizeof(oneway));
        load(emu, 0x9100,   faraway, sizeof(faraway));
        emu.debug_state().breakpoints().add_pc(LIN_PARK);

        emu.debug_state().run_to(0x9500);   // REDUNDANT, and never reached
        run_until_paused(emu);

        check("RSOW-13", "a Run to Here issued on a running machine does not "
              "swallow the next breakpoint",
              stopped && running && emu.debug_state().paused() &&
              pc(emu) == LIN_PARK,
              fmt("stopped=%d running=%d paused=%d PC=$%04X (want $%04X; "
                  "$9100 means the hit was swallowed)", stopped ? 1 : 0,
                  running ? 1 : 0, emu.debug_state().paused() ? 1 : 0,
                  pc(emu), LIN_PARK));
    }

    // ── Summary ────────────────────────────────────────────────────────

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
