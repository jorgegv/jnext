// Persistent breakpoints (--persistent-breakpoints) — GH #219.
//
// The feature: breakpoints and watchpoints normally go dead the moment the
// debugger window is closed, because DebuggerManager::set_enabled(false) calls
// DebugState::set_active(false) and every breakpoint check in the hot loop sat
// behind active(). --persistent-breakpoints keeps them live for the whole run;
// a hit pauses the machine and the frontend reopens the window.
//
// Two halves, deliberately:
//
//   PBPS-*  the DebugState predicate. active() is "the debugger is DRIVING the
//           machine" (step modes, the render-every-frame hint, the
//           per-instruction VideoTiming walk); armed() is the narrower "are
//           breakpoints live". Separating them is the whole design: forcing
//           active() true with the window shut would switch on machinery that
//           costs real time and that nobody is looking at.
//
//   PBPW-*  the WIRING — a real Emulator, a real Z80 program, breakpoints and
//           watchpoints armed through the same DebugState the debugger uses,
//           with active() FALSE throughout to model a closed window.
//
// The DEFAULT is pinned as hard as the feature is. PBPW-01/-06/-08 assert that
// without the flag a closed window still means nothing fires: they are what
// stops a later change from silently making persistence the default.
//
// No ROM, no SD image: a 48K machine with a program written straight into RAM
// and PC/SP set by hand (same idiom as test/debug/step_out_test.cpp).
//
// Run: ./build/test/persistent_bp_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// ── Tiny test harness (matches test/debug/step_out_test.cpp style) ─────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

// ── Emulator helpers ───────────────────────────────────────────────────

static constexpr uint16_t PROG    = 0x8000;
static constexpr uint16_t BP_ADDR = 0x8006;   // the LD A,(DATA) below
static constexpr uint16_t DATA    = 0x9000;
static constexpr uint16_t TEST_SP = 0xFF00;

// A 48K machine running this program from 0x8000, interrupts off:
//
//   8000  00           NOP
//   8001  00           NOP
//   8002  00           NOP
//   8003  00           NOP
//   8004  00           NOP
//   8005  00           NOP
//   8006  3A 00 90     LD A,(0x9000)     <- BP_ADDR; also the watched READ
//   8009  3E 5A        LD A,0x5A
//   800B  32 00 90     LD (0x9000),A     <- the watched WRITE
//   800E  18 FE        JR $              <- park
//
// The six leading NOPs put BP_ADDR far enough from the reset PC that "the
// machine never ran" and "the machine ran and stopped at the breakpoint" can
// never be confused.
// `touch_flag = false` leaves cfg.persistent_breakpoints at its DECLARED
// DEFAULT instead of assigning it — that is what PBPW-13 needs, and it is the
// only way a row can notice the default being flipped in emulator_config.h.
static void build(Emulator& emu, bool persistent, bool touch_flag = true) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    if (touch_flag) cfg.persistent_breakpoints = persistent;
    emu.init(cfg);

    const uint8_t prog[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x90,
        0x3E, 0x5A,
        0x32, 0x00, 0x90,
        0x18, 0xFE,
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
    emu.mmu().write(DATA, 0x00);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt in the middle of the run
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// Free-run up to `max_frames` frames, stopping as soon as the debugger pauses.
static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc(const Emulator& emu) {
    return const_cast<Emulator&>(emu).cpu().get_registers().PC;
}

int main() {
    std::printf("\n======================================================\n");
    std::printf("Persistent breakpoints (--persistent-breakpoints), GH #219\n");
    std::printf("======================================================\n\n");

    // ─────────────────── PBPS: the DebugState predicate ────────────────

    // PBPS-01 — the default state of a fresh DebugState: nothing armed.
    {
        DebugState ds;
        check("PBPS-01", "fresh DebugState: active/persistent/armed all false",
              !ds.active() && !ds.persistent_breakpoints() && !ds.armed());
    }

    // PBPS-02 — the legacy coupling, unchanged: opening the debugger arms,
    // closing it disarms. This is the behaviour the default must keep.
    {
        DebugState ds;
        ds.set_active(true);
        const bool on = ds.armed();
        ds.set_active(false);
        check("PBPS-02", "without the flag, armed() tracks active() exactly",
              on && !ds.armed());
    }

    // PBPS-03 — the flag on its own arms the check, with the debugger not
    // driving anything. Both halves matter: armed true AND active still false.
    {
        DebugState ds;
        ds.set_persistent_breakpoints(true);
        check("PBPS-03", "the flag alone arms breakpoints without activating "
              "the debugger",
              ds.armed() && !ds.active() && ds.persistent_breakpoints());
    }

    // PBPS-04 — THE feature, at predicate level: open the window, close it,
    // and the check survives the close.
    {
        DebugState ds;
        ds.set_persistent_breakpoints(true);
        ds.set_active(true);
        const bool on = ds.armed() && ds.active();
        ds.set_active(false);
        check("PBPS-04", "with the flag, closing the debugger leaves armed() "
              "true and active() false",
              on && ds.armed() && !ds.active());
    }

    // PBPS-05 — order independence: the flag can be latched after the debugger
    // was already opened and closed, and vice versa. (init() sets it before the
    // GUI exists; emulator_cold_boot() restores active() after init.)
    {
        DebugState ds;
        ds.set_active(true);
        ds.set_active(false);
        ds.set_persistent_breakpoints(true);
        const bool late = ds.armed();
        ds.set_persistent_breakpoints(false);
        check("PBPS-05", "armed() is recomputed by BOTH setters, in either order",
              late && !ds.armed());
    }

    // ─────────────────── PBPW: wiring, against a real machine ──────────

    // PBPW-01 — THE DEFAULT. No flag, debugger not active (window closed): the
    // execute breakpoint does NOT fire and the machine runs to the park. This
    // is #207's reported behaviour, deliberately preserved; a change that made
    // persistence the default flips this row.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-01", "default: execute breakpoint does NOT fire with the "
              "debugger window closed",
              !emu.debug_state().paused() && pc(emu) == 0x800E);
    }

    // PBPW-02 — THE FEATURE. Same machine, same closed window, flag on: the
    // breakpoint fires and the machine stops ON it (PC == BP_ADDR, the
    // instruction not yet executed).
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-02", "with the flag: execute breakpoint fires with the "
              "window closed and stops AT the breakpoint PC",
              emu.debug_state().paused() && pc(emu) == BP_ADDR);
    }

    // PBPW-03 — the hit must NOT switch the debugger on behind the user's
    // back. active() stays false, so the render-every-frame hint and the
    // per-instruction VideoTiming walk (both active()-gated) stay off; only
    // the frontend's set_enabled(true) may flip it. Mutation: implement the
    // feature by forcing active() true and this row fails.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-03", "a persistent hit pauses without activating the "
              "debugger (active() still false)",
              emu.debug_state().paused() && !emu.debug_state().active());
    }

    // PBPW-04 — with the window ALREADY OPEN the flag changes nothing: same
    // stop, same PC, and active() is what the caller set it to.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-04", "with the flag and the window open, behaviour is "
              "indistinguishable from today",
              emu.debug_state().paused() && pc(emu) == BP_ADDR &&
              emu.debug_state().active());
    }

    // PBPW-05 — control for PBPW-04: without the flag and with the window
    // open, the identical stop happens. Together the two rows show the flag
    // adds nothing to the open-window case rather than merely not breaking it.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-05", "control: without the flag, an OPEN window stops at "
              "the same breakpoint PC",
              emu.debug_state().paused() && pc(emu) == BP_ADDR);
    }

    // PBPW-06 — DEFAULT, write watchpoint: no flag, window closed, the write
    // to 0x9000 does not stop the machine and the value lands.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::WRITE);
        run_until_paused(emu);
        check("PBPW-06", "default: WRITE watchpoint does NOT fire with the "
              "window closed",
              !emu.debug_state().paused() && emu.mmu().read(DATA) == 0x5A);
    }

    // PBPW-07 — FEATURE, write watchpoint. Data breakpoints stop AFTER the
    // instruction that made the access, so the store has already happened and
    // PC is on the next instruction (the park at 0x800E).
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::WRITE);
        run_until_paused(emu);
        check("PBPW-07", "with the flag: WRITE watchpoint fires with the "
              "window closed, after the storing instruction",
              emu.debug_state().paused() && pc(emu) == 0x800E &&
              emu.mmu().read(DATA) == 0x5A);
    }

    // PBPW-08 — DEFAULT, read watchpoint: no flag, window closed, nothing
    // stops. Read and write watchpoints are checked at separate MMU sites, so
    // both directions get their own default-pinning row.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        run_until_paused(emu);
        check("PBPW-08", "default: READ watchpoint does NOT fire with the "
              "window closed",
              !emu.debug_state().paused() && pc(emu) == 0x800E);
    }

    // PBPW-09 — FEATURE, read watchpoint. The read is made by `LD A,(0x9000)`
    // at 0x8006, so the machine stops with PC on the instruction after it.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_watchpoint(DATA, WatchType::READ);
        run_until_paused(emu);
        check("PBPW-09", "with the flag: READ watchpoint fires with the window "
              "closed, after the loading instruction",
              emu.debug_state().paused() && pc(emu) == 0x8009);
    }

    // PBPW-10 — the config plumbing, end to end: EmulatorConfig ->
    // Emulator::init -> DebugState. Without it the flag would parse and do
    // nothing. The negative half is the same assertion PBPW-01 relies on.
    {
        Emulator on, off;
        build(on,  /*persistent=*/true);
        build(off, /*persistent=*/false);
        check("PBPW-10", "EmulatorConfig::persistent_breakpoints reaches "
              "DebugState through init()",
              on.debug_state().persistent_breakpoints() &&
              on.debug_state().armed() && !on.debug_state().active() &&
              !off.debug_state().persistent_breakpoints() &&
              !off.debug_state().armed());
    }

    // PBPW-11 — closing the debugger window does not disarm it. This is the
    // exact sequence DebuggerManager::set_enabled(false) performs — resume,
    // then deactivate — and the specified outcome is that armed() survives it,
    // so the NEXT hit still stops the machine.
    //
    // What this row deliberately does NOT assert is where the machine ends up
    // afterwards. jnext's resume() does not step off the breakpoint it is
    // standing on, so a resume with PC still on a live breakpoint re-fires
    // immediately — and that is PRE-EXISTING and DEFAULT behaviour, not
    // something --persistent-breakpoints introduced: with no flag at all and
    // the window OPEN, pressing Run/F5 while stopped at a breakpoint leaves PC
    // exactly where it was (measured on this same fixture while writing this
    // suite). Pinning that here would freeze a defect; asserting the opposite
    // would fail against the tree it is being written on. So the row asserts
    // the contract of THIS feature and no more.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        const bool stopped = emu.debug_state().paused() && pc(emu) == BP_ADDR;

        // Exactly what closing the window does, in order.
        emu.debug_state().resume();
        emu.debug_state().set_active(false);

        check("PBPW-11", "closing the debugger window at a hit leaves "
              "breakpoints armed (armed() true, active() false)",
              stopped && emu.debug_state().armed() &&
              !emu.debug_state().active());
    }

    // PBPW-12 — the SECOND hit, with the window closed throughout. Remove the
    // breakpoint the machine is standing on, add another one further along,
    // and it stops there too: persistence is a property of the run, not a
    // one-shot that the first hit consumes.
    {
        Emulator emu;
        build(emu, /*persistent=*/true);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        const bool first = emu.debug_state().paused() && pc(emu) == BP_ADDR;

        emu.debug_state().breakpoints().remove_pc(BP_ADDR);
        emu.debug_state().breakpoints().add_pc(0x800B);   // LD (0x9000),A
        emu.debug_state().resume();
        run_until_paused(emu);

        check("PBPW-12", "a second breakpoint fires with the window still "
              "closed (persistence is not one-shot)",
              first && emu.debug_state().paused() && pc(emu) == 0x800B);
    }

    // PBPW-13 — THE DECLARED DEFAULT, pinned at its source. Every other row
    // assigns cfg.persistent_breakpoints explicitly, so none of them can see
    // the declared default in emulator_config.h change: flipping it to `true`
    // leaves the whole suite green. This row never touches the field, so an
    // opt-in feature turned on for everybody fails HERE — the machine must
    // still be unarmed and must still run past the breakpoint.
    {
        Emulator emu;
        build(emu, /*persistent=*/false, /*touch_flag=*/false);
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        run_until_paused(emu);
        check("PBPW-13", "a config nobody touched leaves persistence OFF: "
              "nothing armed, machine runs past the breakpoint",
              !EmulatorConfig{}.persistent_breakpoints &&
              !emu.debug_state().persistent_breakpoints() &&
              !emu.debug_state().armed() &&
              !emu.debug_state().paused() && pc(emu) == 0x800E);
    }

    // ── Summary ────────────────────────────────────────────────────────

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
