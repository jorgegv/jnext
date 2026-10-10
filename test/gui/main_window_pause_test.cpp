// ===========================================================================
// Pause from the emulator window (GitHub issue #306).
//
// No VHDL oracle: this is host UI wiring. The oracle is the issue text ("add a
// button in the main emulator window that pauses/resumes emulation") plus the
// design in the plan (Machine > P&ause, Alt+U, same QAction on the Main toolbar
// and on the window; the pause is the debugger BACKEND's, made by an observer
// client that MainWindow owns; a pause by exactly that client does not open the
// debugger window; every other pause still does; the checkmark and the status
// bar's "Paused" cell are pulled from the backend's state()).
//
// "The per-tick call" below is exactly what QtApp::TickEffects::post_frames()
// calls: DebuggerManager::check_breakpoint_hit() (debugger builds) and then
// MainWindow::sync_pause_state(). No suite can construct a QtApp, so the call
// itself in post_frames() is pinned by MWP-12 as a source check; everything it
// calls is driven here.
//
// Row count is the same in both Qt configurations; a row that needs the
// DebuggerManager asserts the Qt-only property in the #else arm.
//
// Run: ./build/test/main_window_pause_test
// ===========================================================================

#include "gui/main_window.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "platform/emulator_boot.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#endif

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
    }
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

constexpr uint16_t PROG = 0x8000;

// A 48K machine parked in a JR $ loop at 0x8000.
void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    emu.init(cfg);
    const uint8_t prog[] = {0x18, 0xFE};   // 8000  JR $
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = PROG;
    r.SP = 0xFF00;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// Same idiom as host_hotkey_test: sendEvent passes through the shortcut map.
void send(MainWindow& w, Qt::Key k, Qt::KeyboardModifiers mods, bool press) {
    QKeyEvent e(press ? QEvent::KeyPress : QEvent::KeyRelease, k, mods);
    QApplication::sendEvent(&w, &e);
}

void alt_u(MainWindow& w) {
    send(w, Qt::Key_Alt, Qt::NoModifier, true);
    send(w, Qt::Key_U, Qt::AltModifier, true);
    QApplication::processEvents();
    send(w, Qt::Key_U, Qt::AltModifier, false);
    send(w, Qt::Key_Alt, Qt::NoModifier, false);
}

QAction* find_pause(MainWindow& w) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == QStringLiteral("P&ause")) return a;
    return nullptr;
}

QLabel* find_paused_label(MainWindow& w) {
    for (QLabel* l : w.statusBar()->findChildren<QLabel*>())
        if (l->text() == QStringLiteral("Paused")) return l;
    return nullptr;
}

// A machine, its backend (declared first, so it outlives the window — the
// QtApp contract) and a shown MainWindow bound to both.
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    std::unique_ptr<MainWindow> w;
    std::vector<int> guest_keys;   // scancodes the guest saw pressed

    Fixture() {
        build(emu);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        w = std::make_unique<MainWindow>();
        w->set_debugger(backend.get());
        w->set_emulator(&emu);
        w->set_key_callback([this](SDL_Scancode sc, bool pressed) {
            if (pressed) guest_keys.push_back(static_cast<int>(sc));
        });
        w->show();
        w->activateWindow();
        QApplication::processEvents();
    }
    ~Fixture() {
#ifdef ENABLE_DEBUGGER
        // The debugger window is a top-level with no parent and outlives its
        // manager (the app exits with it); a later fixture's processEvents()
        // would deliver its pending resize to panels reading the freed backend.
        DebuggerWindow* dw = nullptr;
        if (w) if (auto* mgr = w->debugger_manager()) dw = mgr->debugger_window_ptr();
#endif
        w.reset();        // detaches its pause client from the backend first (may be null already)
#ifdef ENABLE_DEBUGGER
        delete dw;
#endif
        backend.reset();
    }

    QAction* action() { return find_pause(*w); }
    QLabel*  label()  { return find_paused_label(*w); }
    bool paused() { return emu.debug_state().paused(); }

    // What QtApp::TickEffects::post_frames() calls.
    void tick() {
#ifdef ENABLE_DEBUGGER
        if (auto* mgr = w->debugger_manager()) mgr->check_breakpoint_hit();
#endif
        w->sync_pause_state();
    }
    bool debugger_open() {
#ifdef ENABLE_DEBUGGER
        auto* mgr = w->debugger_manager();
        if (!mgr) return false;
        DebuggerWindow* dw = mgr->debugger_window_ptr();
        return mgr->is_enabled() || (dw && dw->isVisible());
#else
        return false;
#endif
    }
    bool indicators(bool want) {
        QAction* a = action();
        QLabel* l = label();
        return a && l && a->isChecked() == want && l->isVisibleTo(w.get()) == want;
    }
};

// MWP-12 — the per-tick wiring. No suite can construct a QtApp, so the one
// line that makes QtApp pull the pause state per tick is checked in the source
// of QtApp::TickEffects::post_frames(): the call must be there, in code (not a
// comment), AFTER the ENABLE_DEBUGGER block so a Qt-without-debugger build gets
// it too. What the call does is MWP-04/07. Reached on the real binary by hand
// (GH #306 report, check A9): this row is a source check, not a behavioural one.
bool post_frames_syncs_pause_state() {
    std::ifstream in(JNEXT_QT_APP_CPP);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    const size_t b = src.find("void QtApp::TickEffects::post_frames(");
    const size_t e = src.find("void QtApp::TickEffects::set_timer_interval(");
    if (b == std::string::npos || e == std::string::npos || e < b) return false;
    std::string body = src.substr(b, e - b), code;
    std::istringstream ls(body);
    for (std::string line; std::getline(ls, line);) {
        const size_t c = line.find("//");
        code += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
    }
    const size_t call = code.find("main_window_->sync_pause_state();");
    const size_t last_endif = code.rfind("#endif");
    return call != std::string::npos && last_endif != std::string::npos && call > last_endif;
}

void test_pause() {
    // MWP-01 — one checkable QAction, Alt+U, in the Machine menu and on the
    // Main toolbar as the SAME object (one checkmark).
    {
        Fixture fx;
        QAction* in_menu = nullptr;
        for (QAction* top : fx.w->menuBar()->actions()) {
            if (!top->menu() || top->text() != QStringLiteral("&Machine")) continue;
            for (QAction* a : top->menu()->actions())
                if (a->text() == QStringLiteral("P&ause")) in_menu = a;
        }
        QAction* on_toolbar = nullptr;
        for (QToolBar* tb : fx.w->findChildren<QToolBar*>())
            if (tb->windowTitle() == QStringLiteral("Main"))
                for (QAction* a : tb->actions())
                    if (a->text() == QStringLiteral("P&ause")) on_toolbar = a;
        check("MWP-01", "Machine > P&ause is checkable, Alt+U, and the Main toolbar holds the same action",
              in_menu && in_menu->isCheckable() &&
              in_menu->shortcut() == QKeySequence(Qt::ALT | Qt::Key_U) &&
              in_menu->toolTip().contains(QStringLiteral("Alt+U")) &&
              on_toolbar == in_menu,
              std::string("menu=") + (in_menu ? "1" : "0") + " toolbar=" +
              (on_toolbar ? (on_toolbar == in_menu ? "same" : "other") : "0"));
    }

    // MWP-02 — Alt+U with the debugger closed pauses the machine as a User
    // pause, arms nothing (an observer client) and types no U into the guest.
    {
        Fixture fx;
        alt_u(*fx.w);
        const auto st = fx.backend->state();
        bool u_in_guest = false;
        for (int sc : fx.guest_keys) if (sc == SDL_SCANCODE_U) u_in_guest = true;
        check("MWP-02", "Alt+U pauses (User), arms nothing, and types no U into the guest",
              fx.paused() && st.paused &&
              st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
              !fx.emu.debug_state().armed() && !u_in_guest,
              std::string("paused=") + (fx.paused() ? "1" : "0") + " armed=" +
              (fx.emu.debug_state().armed() ? "1" : "0") + " u=" + (u_in_guest ? "1" : "0"));

        // MWP-03 — the per-tick call does not open the debugger window.
        fx.tick();
#ifdef ENABLE_DEBUGGER
        check("MWP-03", "the emulator window's own pause does not open the debugger window",
              fx.paused() && fx.w->debugger_manager() &&
              !fx.w->debugger_manager()->is_enabled() && !fx.debugger_open());
#else
        check("MWP-03", "the emulator window's own pause works with no debugger built in",
              fx.paused() && !fx.debugger_open());
#endif
    }

    // MWP-04 — the checkmark and the label are set by toggle_pause() itself,
    // read BEFORE any per-tick call; and they clear again the same way.
    {
        Fixture fx;
        const bool idle_hidden = fx.indicators(false);
        alt_u(*fx.w);
        const bool on_now = fx.paused() && fx.indicators(true);
        alt_u(*fx.w);
        const bool off_now = !fx.paused() && fx.indicators(false);
        check("MWP-04", "Alt+U sets then clears the checkmark and the Paused label with no tick in between",
              idle_hidden && on_now && off_now,
              std::string("idle=") + (idle_hidden ? "1" : "0") + " on=" + (on_now ? "1" : "0") +
              " off=" + (off_now ? "1" : "0"));
    }

    // MWP-05 — positive control for MWP-03: a SECOND client of the same kind
    // and observer flag pauses, and the debugger window DOES open. The
    // exemption is keyed on the client id.
    {
        Fixture fx;
        alt_u(*fx.w);   // creates this window's own client, then resume
        alt_u(*fx.w);
        const auto other = fx.backend->attach(
            jnext::dbg::ClientInfo{"other", jnext::dbg::ClientKind::Gui, true});
        fx.backend->pause(other.value);
        fx.tick();
#ifdef ENABLE_DEBUGGER
        check("MWP-05", "another client's pause (same kind, observer) still opens the debugger window",
              fx.paused() && fx.w->debugger_manager() && fx.w->debugger_manager()->is_enabled());
#else
        check("MWP-05", "another client's pause is shown by the checkmark and label",
              fx.paused() && fx.indicators(true));
#endif
        fx.backend->detach(other.value);
    }

    // MWP-06 — debugger open: Alt+U goes through the manager's on_pause /
    // on_run, whose signals fire synchronously (the own-client path would only
    // emit them from the next tick). Read before any tick.
    {
        Fixture fx;
#ifdef ENABLE_DEBUGGER
        auto* mgr = fx.w->debugger_manager();
        mgr->set_enabled(true);
        QSignalSpy paused_spy(mgr, &DebuggerManager::paused);
        QSignalSpy resumed_spy(mgr, &DebuggerManager::resumed);
        alt_u(*fx.w);
        const bool p1 = fx.paused() && paused_spy.count() == 1;
        // The open debugger window took the activation (set_enabled raises it);
        // a window shortcut only fires in the active window, as in the product.
        fx.w->activateWindow();
        QApplication::processEvents();
        alt_u(*fx.w);
        const bool p2 = !fx.paused() && resumed_spy.count() == 1;
        check("MWP-06", "with the debugger open, Alt+U pauses and resumes through the manager (signals before any tick)",
              p1 && p2 && paused_spy.count() == 1,
              std::string("paused_signals=") + std::to_string(paused_spy.count()) +
              " resumed_signals=" + std::to_string(resumed_spy.count()));
#else
        alt_u(*fx.w);
        const bool p1 = fx.paused();
        alt_u(*fx.w);
        check("MWP-06", "without a debugger, Alt+U pauses and resumes the machine",
              p1 && !fx.paused());
#endif
    }

    // MWP-07 — the checkmark and label are PULLED from the backend's state:
    // a pause made by another path (the debugger's F9 verb) shows, and so does
    // its resume.
    {
        Fixture fx;
#ifdef ENABLE_DEBUGGER
        auto* mgr = fx.w->debugger_manager();
        mgr->set_enabled(true);
        mgr->on_pause();
        fx.tick();
        const bool shown = fx.paused() && fx.indicators(true);
        mgr->on_run();
        fx.tick();
        const bool cleared = !fx.paused() && fx.indicators(false);
#else
        const auto other = fx.backend->attach(
            jnext::dbg::ClientInfo{"other", jnext::dbg::ClientKind::Test, false});
        fx.backend->pause(other.value);
        fx.tick();
        const bool shown = fx.paused() && fx.indicators(true);
        fx.backend->run(other.value);
        fx.tick();
        const bool cleared = !fx.paused() && fx.indicators(false);
        fx.backend->detach(other.value);
#endif
        check("MWP-07", "a pause and a resume made elsewhere reach the checkmark and label on the next tick",
              shown && cleared,
              std::string("shown=") + (shown ? "1" : "0") + " cleared=" + (cleared ? "1" : "0"));
    }

    // MWP-08 — a cold boot while paused (QtApp's bracket) stays paused, keeps
    // the owner, and leaves the debugger window shut.
    {
        Fixture fx;
        alt_u(*fx.w);
        fx.backend->on_cold_boot_begin();
        emulator_cold_boot(fx.emu, fx.emu.config());   // QtApp::boot_machine()
        fx.backend->on_cold_boot_done();
        fx.tick();
        bool shut = !fx.debugger_open();
        check("MWP-08", "a cold boot while paused stays paused, quietly (debugger window still shut)",
              fx.paused() && shut && fx.indicators(true),
              std::string("paused=") + (fx.paused() ? "1" : "0") + " shut=" + (shut ? "1" : "0"));
    }

    // MWP-09 — with the menu bar, toolbars and status bar hidden, as
    // toggle_fullscreen() leaves them, Alt+U still pauses (the action is also
    // on the window).
    {
        Fixture fx;
        fx.w->menuBar()->hide();
        for (QToolBar* tb : fx.w->findChildren<QToolBar*>()) tb->hide();
        QApplication::processEvents();
        alt_u(*fx.w);
        check("MWP-09", "Alt+U pauses with the menu bar and toolbars hidden (fullscreen chrome)",
              fx.paused());
    }

    // MWP-10 — a bare window with no backend: triggering the action is safe
    // and leaves it unchecked.
    {
        MainWindow bare;
        QAction* a = find_pause(bare);
        if (a) a->trigger();
        QApplication::processEvents();
        QLabel* l = find_paused_label(bare);
        check("MWP-10", "with no debugger bound, triggering Pause is a no-op: unchecked, label hidden",
              a && !a->isChecked() && l && !l->isVisibleTo(&bare));
    }

    // MWP-11 — the window's pause client goes with the window: destroying it
    // while it holds the pause leaves the machine running, not stuck paused on
    // a backend that outlives it.
    {
        Fixture fx;
        alt_u(*fx.w);
        const bool was = fx.paused();
        fx.w.reset();
        check("MWP-11", "destroying the window while paused releases its pause",
              was && !fx.paused());
    }

    // MWP-12 — see post_frames_syncs_pause_state().
    check("MWP-12", "QtApp::post_frames() pulls the pause state each tick, outside the debugger block",
          post_frames_syncs_pause_state());
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The debugger window saves Debugger.conf; the real ~/.jnext is the user's.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("GH #306 - Pause from the emulator window\n");
    std::printf("========================================\n\n");
    test_pause();
    std::printf("  Group: MWP            - done\n");
    std::printf("\n========================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
