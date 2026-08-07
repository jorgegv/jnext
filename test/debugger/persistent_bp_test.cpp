// Persistent breakpoints — the FRONTEND half (GH #219).
//
// test/debug/persistent_bp_test.cpp proves the core keeps breakpoints armed
// with the debugger window closed. This suite proves the other half of the
// feature, which lives entirely on the Qt side and which the core deliberately
// knows nothing about: when a hit pauses a machine whose debugger window is
// shut, the FRONTEND forces that window open, on the breakpoint.
//
// It drives the REAL DebuggerManager — the same set_enabled() the View menu and
// the window's close button call, and the same check_breakpoint_hit() QtApp
// runs on every frame tick — against a real 48K Emulator with a hand-written
// program. Nothing is stubbed, so the rows fail if EITHER half breaks:
//
//   PBPUI-02 needs the core to keep the breakpoint armed AND the frontend to
//            re-enable + show the window.
//   PBPUI-03 is the DEFAULT, pinned: no flag, closed window, the machine runs
//            straight past the breakpoint and the window stays shut.
//
// Qt is required (DebuggerManager owns real QWidgets), but no display is:
// main() forces the offscreen QPA platform, like the other debugger suites.
// Run: ./build/test/debugger_persistent_bp_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QApplication>
#include <QMainWindow>

#include <cstdint>
#include <cstdio>

namespace {

int g_total = 0;
int g_pass  = 0;
int g_fail  = 0;

void check(const char* id, const char* desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

constexpr uint16_t PROG    = 0x8000;
constexpr uint16_t BP_ADDR = 0x8006;
constexpr uint16_t TEST_SP = 0xFF00;

// Same fixture program as the core suite: six NOPs, then a landmark
// instruction at BP_ADDR, then a park at 0x800E.
void build(Emulator& emu, bool persistent) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.persistent_breakpoints = persistent;
    emu.init(cfg);

    const uint8_t prog[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x3A, 0x00, 0x90,          // 8006  LD A,(0x9000)
        0x3E, 0x5A,                // 8009  LD A,0x5A
        0x32, 0x00, 0x90,          // 800B  LD (0x9000),A
        0x18, 0xFE,                // 800E  JR $
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

uint16_t pc(Emulator& emu) { return emu.cpu().get_registers().PC; }

void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

// One self-contained fixture: a 48K Emulator, a bare QMainWindow and a real
// DebuggerManager, with the debugger opened once and then CLOSED — the state a
// user is in after inspecting a program and dismissing the debugger.
struct Fixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;

    explicit Fixture(bool persistent) {
        build(emu, persistent);
        mgr = new DebuggerManager(&win, &emu, &win);   // parented → auto-freed
        mgr->set_enabled(true);                        // create + show window
        emu.debug_state().breakpoints().add_pc(BP_ADDR);
        mgr->set_enabled(false);                       // user closes it again
    }

    bool window_visible() const {
        DebuggerWindow* w = mgr->debugger_window_ptr();
        return w && w->isVisible();
    }
};

} // namespace

int main(int argc, char** argv) {
    // DebuggerManager owns QWidgets, so a QApplication is required — but not a
    // display: force the offscreen QPA platform.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("\n======================================================\n");
    std::printf("Persistent breakpoints — frontend (GH #219)\n");
    std::printf("======================================================\n\n");

    // PBPUI-01 — the disable path itself. set_enabled(false) still clears
    // active() (the debugger stops driving, and its costly per-instruction
    // machinery goes with it), but with the flag it must NOT disarm: armed()
    // survives. This is the exact line the issue named,
    // debugger_manager.cpp's `debug_state().set_active(false)`.
    {
        Fixture fx(/*persistent=*/true);
        check("PBPUI-01", "closing the debugger leaves breakpoints armed but "
              "the debugger inactive",
              !fx.mgr->is_enabled() && !fx.window_visible() &&
              fx.emu.debug_state().armed() && !fx.emu.debug_state().active());
    }

    // PBPUI-02 — THE FEATURE, end to end. Window closed, breakpoint set, the
    // machine runs: it stops ON the breakpoint, and the frame-tick call
    // QtApp makes — check_breakpoint_hit() — forces the window back open with
    // the debugger re-enabled. Break either half (revert the armed() gate in
    // the core, or the auto-enable in check_breakpoint_hit) and this fails.
    {
        Fixture fx(/*persistent=*/true);
        run_until_paused(fx.emu);
        const bool stopped = fx.emu.debug_state().paused() &&
                             pc(fx.emu) == BP_ADDR;
        const bool shut_before = !fx.window_visible();

        fx.mgr->check_breakpoint_hit();   // what QtApp::on_frame_tick calls

        check("PBPUI-02", "a hit with the window closed stops at the breakpoint "
              "and the frontend forces the debugger window open",
              stopped && shut_before && fx.mgr->is_enabled() &&
              fx.window_visible() && pc(fx.emu) == BP_ADDR);
    }

    // PBPUI-03 — THE DEFAULT, pinned at the frontend boundary. No flag: the
    // same close leaves nothing armed, the machine runs past the breakpoint to
    // the park, and no window is raised. A change that made persistence the
    // default flips this row.
    {
        Fixture fx(/*persistent=*/false);
        run_until_paused(fx.emu);
        const bool ran_past = !fx.emu.debug_state().paused() &&
                              pc(fx.emu) == 0x800E;

        fx.mgr->check_breakpoint_hit();

        check("PBPUI-03", "default: a closed window disarms, the machine runs "
              "past the breakpoint and nothing is raised",
              !fx.emu.debug_state().armed() && ran_past &&
              !fx.mgr->is_enabled() && !fx.window_visible());
    }

    // PBPUI-04 — with the window ALREADY OPEN the flag is invisible: the hit
    // stops in the same place, the debugger stays enabled, and the window is
    // simply still there. Together with PBPUI-05 this says the flag adds
    // nothing to the open-window case rather than merely not breaking it.
    {
        Fixture fx(/*persistent=*/true);
        fx.mgr->set_enabled(true);        // reopen before running
        run_until_paused(fx.emu);
        fx.mgr->check_breakpoint_hit();
        check("PBPUI-04", "with the flag and the window open, the hit behaves "
              "exactly as it does today",
              fx.emu.debug_state().paused() && pc(fx.emu) == BP_ADDR &&
              fx.mgr->is_enabled() && fx.window_visible());
    }

    // PBPUI-05 — the control: the identical sequence with NO flag.
    {
        Fixture fx(/*persistent=*/false);
        fx.mgr->set_enabled(true);
        run_until_paused(fx.emu);
        fx.mgr->check_breakpoint_hit();
        check("PBPUI-05", "control: without the flag, an open window stops at "
              "the same breakpoint and stays open",
              fx.emu.debug_state().paused() && pc(fx.emu) == BP_ADDR &&
              fx.mgr->is_enabled() && fx.window_visible());
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
