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
#include "debug/debugger.h"
#include "debugger/breakpoint_model.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QApplication>
#include <QMainWindow>

#include <cstdint>
#include <cstdio>
#include "../row_id.h"

namespace {

int g_total = 0;
int g_pass  = 0;
int g_fail  = 0;

void check(const char* id, const char* desc, bool cond) {
    report_row_id(id);
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
    // GH #278 WP2 — the loop owner's backend (QtApp::debugger()), built
    // after init() and declared before the window, so it outlives the manager.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;

    explicit Fixture(bool persistent) {
        build(emu, persistent);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented → auto-freed
        mgr->set_enabled(true);                        // create + show window
        // GH #278 WP4c — the user's breakpoint, set as the GUI sets it: a backend
        // subscription of the GUI's observer client, which outlives the window.
        mgr->breakpoints().add(BreakpointModel::Execute, BP_ADDR);
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

    // PBPUI-01 — the disable path itself. set_enabled(false) still stops the
    // debugger driving the machine (its costly per-instruction machinery goes
    // with it: GH #278 WP4c, the window's client detaches — the attach and the
    // live raster that replaced the `active()` bit the issue named), but with
    // the flag it must NOT disarm: armed() survives.
    {
        Fixture fx(/*persistent=*/true);
        check("PBPUI-01", "closing the debugger leaves breakpoints armed but "
              "the debugger inactive",
              !fx.mgr->is_enabled() && !fx.window_visible() &&
              fx.emu.debug_state().armed() && !fx.emu.debug_state().attached() &&
              !fx.emu.debug_state().raster_live());
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

    // ── GH #278 WP2 — the window's adapter is a backend client ONLY while
    //    the window is open (qt-frontend.md §4 as built). A pause from any
    //    source must still open a CLOSED window: it is pulled from the backend
    //    on every tick, not pushed to an attached client. One row per source;
    //    each first shows that nothing is attached at the moment of the stop.

    // PBPUI-06 — the MAGIC breakpoint (ED FF), no flag: the machine is unarmed
    // until the opcode executes, the hook stops it, and the next tick opens the
    // window on it.
    {
        Fixture fx(/*persistent=*/false);
        fx.emu.set_magic_breakpoint(true);
        fx.emu.mmu().write(PROG, 0xED);
        fx.emu.mmu().write(PROG + 1, 0xFF);
        const bool detached_before = !fx.backend->attached() && !fx.backend->armed();
        run_until_paused(fx.emu);
        const jnext::dbg::RunState st = fx.backend->state();
        const bool stopped = st.paused &&
                             st.pause_reason.kind == jnext::dbg::PauseReason::Kind::Magic &&
                             pc(fx.emu) == PROG + 2;
        const bool shut_before = !fx.window_visible();

        fx.mgr->check_breakpoint_hit();

        check("PBPUI-06", "with the window closed and nothing attached, a magic "
              "breakpoint's stop opens the debugger window on the next tick",
              detached_before && stopped && shut_before && fx.mgr->is_enabled() &&
              fx.window_visible());
    }

    // PBPUI-07 — a PERSISTENT breakpoint (GH #219): the machine is armed by the
    // flag alone — no client attached — and the hit still opens the window.
    // PBPUI-02's feature, with the "nobody attached" half made explicit.
    {
        Fixture fx(/*persistent=*/true);
        const bool armed_unattached = fx.backend->armed() && !fx.backend->attached();
        run_until_paused(fx.emu);
        const bool stopped = fx.backend->state().paused && pc(fx.emu) == BP_ADDR;
        const bool shut_before = !fx.window_visible();

        fx.mgr->check_breakpoint_hit();

        check("PBPUI-07", "with the window closed, armed only by "
              "--persistent-breakpoints (no client attached), a breakpoint hit "
              "opens the debugger window on the next tick",
              armed_unattached && stopped && shut_before && fx.mgr->is_enabled() &&
              fx.window_visible());
    }

    // PBPUI-08 — ANOTHER CLIENT's pause (owner Q5): a remote attaches and
    // pauses; the next tick opens the window, and the pause is still that
    // client's — the window neither re-owns nor resumes it.
    {
        Fixture fx(/*persistent=*/false);
        fx.emu.run_frame();                        // unarmed: runs on to the park
        const jnext::dbg::ClientId remote =
            fx.backend->attach(jnext::dbg::ClientInfo{"remote", jnext::dbg::ClientKind::Dzrp})
                .value;
        const bool shut_before = !fx.window_visible() && !fx.mgr->is_enabled();
        fx.backend->pause(remote);

        fx.mgr->check_breakpoint_hit();

        const jnext::dbg::RunState st = fx.backend->state();
        check("PBPUI-08", "with the window closed, another client's pause opens the "
              "debugger window on the next tick and stays that client's pause",
              shut_before && fx.mgr->is_enabled() && fx.window_visible() && st.paused &&
              st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
              st.pause_reason.by == remote);
    }

    // PBPUI-09 — CLOSING the window after a magic breakpoint opened it leaves
    // the machine DISARMED, as closing always has. The magic hook used to set
    // the legacy DebugState::active() bit, which only the close cleared; GH
    // #278 WP4c retired it — the hook now HOLDS its stop only until the resume
    // (DebugState's magic hold) — so the Run releases the hold and the close
    // detaches the window's client, and one magic hit cannot leave a leftover
    // breakpoint firing with the window shut — PBPUI-03's default. The user
    // resumes with Run before closing, so the row does not depend on what a
    // close does to a machine still paused by the magic breakpoint (resumed;
    // see qt-frontend.md §4.1).
    {
        Fixture fx(/*persistent=*/false);
        fx.emu.set_magic_breakpoint(true);
        fx.emu.mmu().write(PROG, 0xED);
        fx.emu.mmu().write(PROG + 1, 0xFF);
        run_until_paused(fx.emu);
        fx.mgr->check_breakpoint_hit();            // opens the window
        const bool opened = fx.mgr->is_enabled() && fx.window_visible() &&
                            fx.backend->attached() && fx.emu.debug_state().magic_hold();
        fx.mgr->on_run();                          // F5: resume from the magic stop
        fx.mgr->set_enabled(false);                // then close the window
        const bool disarmed = !fx.backend->armed() && !fx.backend->attached() &&
                              !fx.emu.debug_state().magic_hold();
        run_until_paused(fx.emu);                  // runs past BP_ADDR to the park
        fx.mgr->check_breakpoint_hit();

        check("PBPUI-09", "closing the window after a magic breakpoint opened it "
              "disarms the machine: the leftover breakpoint no longer stops it",
              opened && disarmed && !fx.emu.debug_state().paused() &&
              pc(fx.emu) == 0x800E && !fx.mgr->is_enabled() && !fx.window_visible());
    }

    // PBPUI-10 — CLOSING the window leaves nothing of the window switched on:
    // no client attached, the machine unarmed, the live raster off AND
    // call-stack tracking off. Tracking is what the open window switched on
    // through the backend (INS-12); a close that forgot to switch it off would
    // leave the per-instruction CALL/RET tracking running for the rest of the
    // session, with nobody looking — and, through the backend's record of the
    // request, re-applied after every hard reset (GH #278 WP2 review round 1).
    {
        Fixture fx(/*persistent=*/false);          // opened once, then closed
        const bool closed_off = !fx.mgr->is_enabled() && !fx.backend->attached() &&
                                !fx.backend->armed() && !fx.backend->live_raster() &&
                                !fx.backend->call_stack_enabled() &&
                                !fx.emu.call_stack().enabled();
        fx.mgr->set_enabled(true);                 // and once more, to be sure it
        const bool open_on = fx.backend->attached() && fx.backend->armed() &&
                             fx.backend->live_raster() &&
                             fx.backend->call_stack_enabled();
        fx.mgr->set_enabled(false);                // was switched on by the open
        const bool closed_again = !fx.backend->attached() && !fx.backend->armed() &&
                                  !fx.backend->live_raster() &&
                                  !fx.backend->call_stack_enabled() &&
                                  !fx.emu.call_stack().enabled();
        check("PBPUI-10", "closing the window leaves no client attached, the machine "
              "unarmed, the live raster off and call-stack tracking off",
              closed_off && open_on && closed_again);
    }

    // PBPUI-11 — the manager DESTROYED with the window still open (its parent
    // window torn down with no close, no set_enabled(false)) takes its client
    // with it: the backend, which outlives it, is left neither armed nor
    // attached, with the live raster and call-stack tracking off. Otherwise the
    // loop owner's process-lifetime backend would carry a dead window's client —
    // the machine armed, breakpoints live with no window — for the rest of the
    // run.
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        jnext::dbg::Debugger backend(emu);
        bool open_on = false;
        {
            auto* host = new QMainWindow;
            auto* mgr  = new DebuggerManager(host, backend, host);
            mgr->set_enabled(true);
            open_on = backend.attached() && backend.armed() && backend.live_raster() &&
                      backend.call_stack_enabled();
            DebuggerWindow* w = mgr->debugger_window_ptr();
            delete w;          // the parentless window first (the fixtures' order)
            delete host;       // ~QMainWindow -> ~DebuggerManager, still enabled
        }
        check("PBPUI-11", "a manager destroyed with its window open detaches: the "
              "surviving backend is neither armed nor attached, live raster and "
              "call-stack tracking off",
              open_on && !backend.attached() && !backend.armed() &&
              !backend.live_raster() && !backend.call_stack_enabled() &&
              !emu.call_stack().enabled());
    }

    // ── GH #278 WP4c — the GUI's breakpoints are an OBSERVER client's backend
    //    subscriptions (REQ-qt-32), owned for the manager's lifetime, while the
    //    window's own client — the one that arms — comes and goes with the
    //    window. These two rows are the cases the rejected design ("attach while
    //    the window is open OR the flag is set, keep the model in the adapter
    //    while detached") would have changed (qt-frontend.md §4.1b).

    // PBPUI-12 — close and reopen keep the breakpoint; with the window closed
    // and no flag the machine is unarmed and runs past it; reopened, it stops.
    {
        Fixture fx(/*persistent=*/false);          // added while open, then closed
        const bool kept_closed = fx.mgr->breakpoints().pc_exists(BP_ADDR) &&
                                 !fx.backend->armed();
        run_until_paused(fx.emu);
        const bool ran_past = !fx.emu.debug_state().paused() && pc(fx.emu) == 0x800E;

        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = PROG;
        fx.emu.cpu().set_registers(r);
        fx.mgr->set_enabled(true);                 // reopen
        run_until_paused(fx.emu);
        check("PBPUI-12", "the GUI's breakpoint survives closing and reopening the "
              "window; closed (no flag) the machine is unarmed and runs past it, "
              "reopened it stops on it",
              kept_closed && ran_past && fx.emu.debug_state().paused() &&
              pc(fx.emu) == BP_ADDR && fx.mgr->breakpoints().pc_exists(BP_ADDR));
    }

    // PBPUI-13 — a REMOTE client attached with the window closed arms the
    // machine, and the GUI's breakpoint fires — as a BreakpointSet entry did —
    // and opens the window (owner Q5); the stop is the GUI's breakpoint's.
    {
        Fixture fx(/*persistent=*/false);
        fx.backend->attach(jnext::dbg::ClientInfo{"remote", jnext::dbg::ClientKind::Dzrp});
        const bool shut_armed = !fx.window_visible() && fx.backend->armed();
        run_until_paused(fx.emu);
        const jnext::dbg::RunState st = fx.backend->state();
        const bool stopped = st.paused && pc(fx.emu) == BP_ADDR &&
                             st.pause_reason.kind == jnext::dbg::PauseReason::Kind::Breakpoint &&
                             st.pause_reason.by == fx.mgr->breakpoints().client();
        fx.mgr->check_breakpoint_hit();
        check("PBPUI-13", "with the window closed and a remote client attached, the "
              "GUI's breakpoint fires and the next tick opens the window",
              shut_armed && stopped && fx.mgr->is_enabled() && fx.window_visible());
    }

    // PBPUI-14 — the manager DESTROYED takes the GUI's breakpoint owner with it:
    // the observer client detaches, its subscriptions go, and nothing is left
    // behind in the backend that outlives it (with its listener, a pointer into
    // a freed model).
    {
        Emulator emu;
        build(emu, /*persistent=*/false);
        jnext::dbg::Debugger backend(emu);
        bool owned = false;
        {
            auto* host = new QMainWindow;
            auto* mgr  = new DebuggerManager(host, backend, host);
            mgr->breakpoints().add(BreakpointModel::Execute, BP_ADDR);
            owned = backend.subscriptions(true).size() == 1;
            delete host;       // ~QMainWindow -> ~DebuggerManager -> ~BreakpointModel
        }
        check("PBPUI-14", "a manager destroyed takes the GUI's breakpoint owner with "
              "it: no subscription is left in the surviving backend",
              owned && backend.subscriptions(true).empty() && !backend.armed());
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
