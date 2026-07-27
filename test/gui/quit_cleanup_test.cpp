// ===========================================================================
// Issue #131 — File > Quit must run closeEvent(), not bypass it.
//
// THE DEFECT. The Quit QAction was connected straight to
// QApplication::quit(), so MainWindow::closeEvent() never ran and everything
// it does was skipped: stopping an active recording, and tearing the debugger
// down. Closing the same window with [X] did both. One action, two behaviours,
// decided only by how the user invoked it.
//
// WHAT THE USER ACTUALLY LOST — measured, because the issue's own guess was
// wrong and a test suite must not enshrine it. The debugger teardown was
// skipped, and with it DebuggerWindow::save_geometry(), which a quit reaches
// only through DebuggerManager::set_enabled(false) (debugger_manager.cpp:153)
// or DebuggerWindow::closeEvent (debugger_window.cpp:196). A resized or moved
// debugger window therefore lost its layout on File > Quit and kept it on [X].
// The RECORDING was not lost: main.cpp:870-873 stops any still-active
// recording once run() returns, and ~VideoRecorder() (video_recorder.cpp:32-35)
// is a second net — verified end-to-end against the pre-fix binary, where Alt+Q
// during --record still produced a valid MP4. Q131-03 below therefore proves
// closeEvent's recorder branch runs, and claims nothing more than that.
//
// THE FIX. Quit now calls close(), which delivers a QCloseEvent synchronously
// and therefore runs closeEvent(); qApp->quit() follows only if that close was
// accepted.
//
// WHAT THESE ROWS ASSERT. The OBSERVABLE EFFECT of the cleanup, never "a
// function was called": the debugger really ends up disabled, its window really
// ends up hidden, and a live recording really stops. Q131-04 additionally pins
// the route itself — a hidden main window is something close() produces and
// QApplication::quit() does not.
//
// WHAT closeEvent() DOES, and why Quit still always quits. Its whole body is:
// stop an in-flight recording, disable the debugger with prompt_on_corrupt =
// false and close the debugger window, then QMainWindow::closeEvent(). Nothing
// there calls event->ignore() and nothing there opens a dialog — the corruption
// resume-prompt is explicitly suppressed on this path (Task 60f, pinned by
// debugger_quit_gate_test QG-01/QG-04). It can BLOCK briefly, because stopping
// a recording runs the ffmpeg mux synchronously, but it cannot refuse. So
// close() returns true and the quit proceeds; the `if` guards a future veto,
// not a current one.
//
// THE RECORDING ROW NEEDS NO REAL FFMPEG. VideoRecorder::start() consults
// ffmpeg only through ffmpeg_available(), and stop() with no captured frames
// removes its temp files and returns before building any encode command. A
// stub `ffmpeg` placed first on PATH is therefore enough to make the row
// hermetic — the same idiom video_recorder_cmd_test's SF group uses.
//
// MainWindow owns QWidgets, so a QApplication is required — but not a display:
// the offscreen QPA platform is forced in main(), the same idiom as
// esc_break_test, host_hotkey_test and the debugger panel suites.
// ===========================================================================
#include <QAction>
#include <QApplication>
#include <QDataStream>
#include <QSettings>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/video_recorder.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "gui/main_window.h"

namespace fs = std::filesystem;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

fs::path g_dir;   // scratch dir: holds the stub ffmpeg and the recording paths

// Stub `ffmpeg` first on PATH. Only `-version` is ever reached by these rows
// (see the header note on stop()), so the stub does nothing else.
bool install_ffmpeg_stub() {
    g_dir = fs::temp_directory_path() / ("jnext_quit_cleanup_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(g_dir, ec);
    if (ec) return false;

    const fs::path stub = g_dir / "ffmpeg";
    { std::ofstream f(stub); f << "#!/bin/sh\nexit 0\n"; if (!f) return false; }
    if (::chmod(stub.c_str(), 0755) != 0) return false;

    // Isolate every QSettings write (DebuggerWindow::save_geometry) to the
    // scratch dir so no developer's real ~/.jnext is touched — the same
    // JNEXT_CONFIG_DIR isolation the regression suite uses.
    if (::setenv("JNEXT_CONFIG_DIR", g_dir.c_str(), 1) != 0) return false;

    const char* old_path = std::getenv("PATH");
    const std::string new_path =
        g_dir.string() + ":" + (old_path ? old_path : "/usr/bin:/bin");
    return ::setenv("PATH", new_path.c_str(), 1) == 0;
}

// A real Next Emulator plus a real MainWindow bound to it, which is what
// creates the DebuggerManager (main_window.cpp set_emulator()).
struct Fixture {
    Emulator    emu;
    MainWindow  win;
    bool        ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        win.set_emulator(&emu);
        QApplication::processEvents();
        ok = win.debugger_manager() != nullptr;
    }
};

// The File > Quit action, located by its untranslated text (the same way
// host_hotkey_test and DebuggerManager locate theirs).
QAction* quit_action(MainWindow& w) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == QStringLiteral("&Quit")) return a;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Q131 — triggering Quit runs closeEvent's cleanup
// ---------------------------------------------------------------------------

void test_debugger_teardown() {
    Fixture fx;
    if (!fx.ok) {
        check("Q131-01", "fixture: Next emulator + MainWindow with a DebuggerManager",
              false, "emulator init or debugger manager unavailable");
        check("Q131-02", "fixture: Next emulator + MainWindow with a DebuggerManager",
              false, "emulator init or debugger manager unavailable");
        return;
    }

    DebuggerManager* mgr = fx.win.debugger_manager();
    mgr->set_enabled(true);                       // creates + shows its window
    QApplication::processEvents();
    const bool armed = mgr->is_enabled();

    QAction* q = quit_action(fx.win);
    if (q) q->trigger();
    QApplication::processEvents();

    // Q131-01 — the observable effect of closeEvent's debugger teardown.
    check("Q131-01",
          "Quit leaves the debugger disabled (it was enabled beforehand)",
          q != nullptr && armed && !mgr->is_enabled(),
          std::string("action=") + (q ? "found" : "missing") +
          " armed=" + (armed ? "1" : "0") +
          " still_enabled=" + (mgr->is_enabled() ? "1" : "0"));

    // Q131-02 — the other half of that teardown: the debugger window is closed,
    // which is what lets the last top-level window go away.
    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    check("Q131-02",
          "Quit leaves the debugger window not visible",
          dbg != nullptr && !dbg->isVisible(),
          std::string("window=") + (dbg ? "exists" : "missing") +
          " visible=" + (dbg && dbg->isVisible() ? "1" : "0"));
}

void test_recorder_stop() {
    Fixture fx;
    if (!fx.ok) {
        check("Q131-03", "fixture: Next emulator + MainWindow with a DebuggerManager",
              false, "emulator init or debugger manager unavailable");
        return;
    }

    const std::string out = (g_dir / "q131.mp4").string();
    const fs::path    raw = g_dir / "jnext_rec_video.raw";   // VideoRecorder's temp
    const bool started = fx.emu.start_recording(out) &&
                         fx.emu.video_recorder().is_recording();

    QAction* q = quit_action(fx.win);
    if (q) q->trigger();
    QApplication::processEvents();

    std::error_code ec;
    const bool raw_gone = !fs::exists(raw, ec);

    // Q131-03 — closeEvent's FIRST branch: a recording live at Quit is stopped
    // here, and its raw temp file removed with it. Scope, stated precisely so
    // this row is not over-read: it proves the branch runs, NOT that the MP4
    // would otherwise be lost — main.cpp:870-873 and ~VideoRecorder() both
    // stop it too (see the header). `started` is part of the condition so a
    // fixture that never began recording FAILS instead of passing vacuously on
    // an already-false flag.
    check("Q131-03",
          "Quit stops a live recording and removes its raw temp file",
          started && !fx.emu.video_recorder().is_recording() && raw_gone,
          std::string("started=") + (started ? "1" : "0") +
          " still_recording=" + (fx.emu.video_recorder().is_recording() ? "1" : "0") +
          " raw_removed=" + (raw_gone ? "1" : "0"));
}

// Q131-05 — the user-visible loss, asserted where the user would notice it: in
// the file the next session reads back. save_geometry() writes width/height as
// a QDataStream blob under "debugger/size" in <config dir>/Debugger.conf
// (debugger_window.cpp:630-638), and a quit reaches it only through the
// teardown Q131-01 covers. JNEXT_CONFIG_DIR points that at the scratch dir, so
// no developer's real ~/.jnext is touched (the same isolation the regression
// suite uses).
void test_geometry_persisted() {
    const fs::path conf = g_dir / "Debugger.conf";
    std::error_code ec;
    fs::remove(conf, ec);

    Fixture fx;
    if (!fx.ok) {
        check("Q131-05", "fixture: Next emulator + MainWindow with a DebuggerManager",
              false, "emulator init or debugger manager unavailable");
        return;
    }

    DebuggerManager* mgr = fx.win.debugger_manager();
    mgr->set_enabled(true);
    QApplication::processEvents();

    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    // A size the loader will accept (it rejects implausibly small ones,
    // debugger_window.cpp:141) and that no default could coincidentally be.
    const int want_w = 1234, want_h = 567;
    if (dbg) dbg->resize(want_w, want_h);
    QApplication::processEvents();

    QAction* q = quit_action(fx.win);
    if (q) q->trigger();
    QApplication::processEvents();

    int got_w = -1, got_h = -1;
    {
        QSettings settings(QString::fromStdString(conf.string()), QSettings::IniFormat);
        const QByteArray blob = settings.value("debugger/size").toByteArray();
        if (!blob.isEmpty()) {
            QDataStream ds(blob);
            ds >> got_w >> got_h;
        }
    }

    char detail[128];
    std::snprintf(detail, sizeof(detail), "saved=%dx%d expected=%dx%d",
                  got_w, got_h, want_w, want_h);
    check("Q131-05",
          "Quit persists the debugger window size to Debugger.conf",
          dbg != nullptr && q != nullptr && got_w == want_w && got_h == want_h,
          detail);
}

void test_close_route() {
    Fixture fx;
    if (!fx.ok) {
        check("Q131-04", "fixture: Next emulator + MainWindow with a DebuggerManager",
              false, "emulator init or debugger manager unavailable");
        return;
    }

    fx.win.show();
    QApplication::processEvents();
    const bool shown = fx.win.isVisible();

    QAction* q = quit_action(fx.win);
    if (q) q->trigger();
    QApplication::processEvents();

    // Q131-04 — the route, not just its effects. Hiding the window is what
    // close() does; QApplication::quit() leaves it on screen. This row is the
    // one that fails if Quit is ever wired straight to quit() again even while
    // some other code path happens to leave the debugger disabled.
    check("Q131-04",
          "Quit hides the main window, as close() does and quit() does not",
          q != nullptr && shown && !fx.win.isVisible(),
          std::string("shown_before=") + (shown ? "1" : "0") +
          " visible_after=" + (fx.win.isVisible() ? "1" : "0"));
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Issue #131 - Quit runs closeEvent's cleanup\n");
    std::printf("==========================================\n\n");

    if (!install_ffmpeg_stub()) {
        std::printf("  FAIL Q131-00: harness: stub ffmpeg installed first on PATH "
                    "[cannot create %s]\n", g_dir.c_str());
        std::printf("\nTotal:    1  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }

    test_debugger_teardown();
    test_recorder_stop();
    test_geometry_persisted();
    test_close_route();
    std::printf("  Group: Q131           - done\n");

    std::error_code ec;
    fs::remove_all(g_dir, ec);

    std::printf("\n==========================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
