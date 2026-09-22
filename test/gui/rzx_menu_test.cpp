// The File menu's RZX actions, driven for real: a failure must be SAID.
//
// Oracle: the contract on MainWindow::handle_rzx_record_path() /
// handle_rzx_stop() / handle_rzx_play_path() and Emulator::start_rzx_recording()
// / stop_rzx_recording(): a recording that cannot be written, one that would
// replace a running recording (throwing it away unwritten), one started while
// a recording plays (no input would reach it), and a file that cannot be
// played are each reported to the user in a dialog. This is jnext policy, not
// hardware, so there is no VHDL citation.
//
// WHY THIS SUITE EXISTS. Every one of those used to end in, at most, a log
// line: the menu handlers dropped the result of start/stop/load on the floor,
// so an unwritable recording was discovered only when the file was not there,
// and "Record RZX..." twice silently discarded the first recording.
//
// Same harness as nex_v13_dialog_test: a real offscreen-QPA MainWindow, the
// post-picker seams called directly (the pickers are modal and untestable),
// and each QMessageBox recorded and answered by a 1 ms timer polling
// QApplication::activeModalWidget().
//
// Qt is required (MainWindow is a real QWidget), no display is: main() forces
// the offscreen QPA platform. Run: ./build/test/rzx_menu_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/main_window.h"

#include <QAction>
#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QTimer>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>   // getpid() — per-process fixture paths (concurrent worktree runs share /tmp)

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

std::string tmp_path(const char* tag, const char* ext) {
    return (std::filesystem::temp_directory_path() /
            ("jnext_rzxmenu_" + std::string(tag) + "_" + std::to_string(::getpid()) + ext))
        .string();
}

std::string magic(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    char m[4] = {0, 0, 0, 0};
    f.read(m, 4);
    return f.gcount() == 4 ? std::string(m, 4) : std::string();
}

/// Records every modal QMessageBox that appears (and its text) and dismisses
/// it, so a wrong dialog can never hang the suite.
struct DialogWatcher {
    int     count = 0;
    QString text;
    QTimer  timer;

    DialogWatcher() {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!mb) return;
            ++count;
            text = mb->text();
            mb->done(QMessageBox::Ok);
        });
        timer.start(1);
    }
    void stop() { timer.stop(); }
};

struct Fixture {
    Emulator   emu;
    MainWindow win;
    bool       ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        win.set_emulator(&emu);
        QApplication::processEvents();
        ok = true;
    }
};

}  // namespace

static void test_rzx_menu() {
    // RZXGUI-01 — a path that cannot be written is refused WITH a dialog that
    // names it, and nothing records.
    {
        Fixture f;
        const std::string path = tmp_path("nodir", "") + "/missing-dir/out.rzx";
        DialogWatcher w;
        if (f.ok) f.win.handle_rzx_record_path(QString::fromStdString(path));
        w.stop();
        check("RZXGUI-01",
              "Record RZX to an unwritable path: an error dialog naming it, and no recording",
              f.ok && w.count == 1 && w.text.contains(QString::fromStdString(path)) &&
                  !f.emu.rzx_recorder().is_recording(),
              fmt("dialogs=%d names_path=%d recording=%d (expect 1,1,0)", w.count,
                  w.text.contains(QString::fromStdString(path)) ? 1 : 0,
                  f.emu.rzx_recorder().is_recording() ? 1 : 0));
    }

    // RZXGUI-02 — a recording whose WRITE fails (/dev/full: opens, then every
    // flush fails ENOSPC) is reported on Stop, and the failure is latched for
    // the exit status.
    {
        Fixture f;
        const std::string path = "/dev/full";
        bool started = false;
        {
            DialogWatcher w0;
            if (f.ok) f.win.handle_rzx_record_path(QString::fromStdString(path));
            w0.stop();
            started = w0.count == 0 && f.emu.rzx_recorder().is_recording();
        }
        if (started) f.emu.run_frame();
        DialogWatcher w;
        if (started) f.win.handle_rzx_stop();
        w.stop();
        check("RZXGUI-02",
              "Stop RZX when the file cannot be written: an error dialog, and the failure "
              "is latched for the exit status",
              started && w.count == 1 && w.text.contains(QString::fromStdString(path)) &&
                  !f.emu.rzx_recorder().is_recording() && f.emu.rzx_output_failed(path),
              fmt("started=%d dialogs=%d latched=%d (expect 1,1,1)", started ? 1 : 0,
                  w.count, f.emu.rzx_output_failed(path) ? 1 : 0));
    }

    // RZXGUI-03 — Record RZX while recording: refused with a dialog; the
    // running recording is NOT replaced (it used to be thrown away unwritten).
    {
        Fixture f;
        const std::string first  = tmp_path("first", ".rzx");
        const std::string second = tmp_path("second", ".rzx");
        const bool started = f.ok && f.emu.start_rzx_recording(first);
        DialogWatcher w;
        if (started) f.win.handle_rzx_record_path(QString::fromStdString(second));
        w.stop();
        check("RZXGUI-03",
              "Record RZX while recording: a dialog, and the running recording keeps going",
              started && w.count == 1 && f.emu.rzx_recorder().is_recording() &&
                  f.emu.rzx_recorder().output_path() == first,
              fmt("started=%d dialogs=%d still_first=%d (expect 1,1,1)", started ? 1 : 0,
                  w.count, f.emu.rzx_recorder().output_path() == first ? 1 : 0));
        f.emu.stop_rzx_recording();
        std::error_code ec;
        std::filesystem::remove(first, ec);
        std::filesystem::remove(second, ec);
    }

    // RZXGUI-04 — Play RZX of a file that is not an RZX: a dialog, nothing plays.
    {
        Fixture f;
        const std::string bad = tmp_path("bad", ".rzx");
        { std::ofstream o(bad, std::ios::binary); o << "NOTRZX NOTRZX NOTRZX"; }
        DialogWatcher w;
        if (f.ok) f.win.handle_rzx_play_path(QString::fromStdString(bad));
        w.stop();
        check("RZXGUI-04", "Play RZX of a garbage file: an error dialog, and nothing plays",
              f.ok && w.count == 1 && !f.emu.rzx_player().is_playing(),
              fmt("dialogs=%d playing=%d (expect 1,0)", w.count,
                  f.emu.rzx_player().is_playing() ? 1 : 0));
        std::error_code ec;
        std::filesystem::remove(bad, ec);
    }

    // RZXGUI-05 — control: a writable path records and saves with NO dialog,
    // and the saved file then plays with no dialog either. Proves the rows
    // above see a dialog because of the failure, not on every call.
    {
        Fixture f;
        const std::string good = tmp_path("good", ".rzx");
        DialogWatcher w;
        if (f.ok) {
            f.win.handle_rzx_record_path(QString::fromStdString(good));
            f.emu.run_frame();
            f.win.handle_rzx_stop();
            f.win.handle_rzx_play_path(QString::fromStdString(good));
        }
        w.stop();
        check("RZXGUI-05",
              "control: record, stop and play a writable file — no dialog, a real RZX, "
              "playing",
              f.ok && w.count == 0 && magic(good) == "RZX!" && f.emu.rzx_player().is_playing() &&
                  !f.emu.rzx_output_failed(good),
              fmt("dialogs=%d magic_ok=%d playing=%d (expect 0,1,1)", w.count,
                  magic(good) == "RZX!" ? 1 : 0, f.emu.rzx_player().is_playing() ? 1 : 0));
        std::error_code ec;
        std::filesystem::remove(good, ec);
    }

    // RZXGUI-06 — closing the window with a recording that cannot be written
    // says so while the window still exists.
    {
        Fixture f;
        const std::string path = "/dev/full";
        const bool started = f.ok && f.emu.start_rzx_recording(path);
        if (started) f.emu.run_frame();
        DialogWatcher w;
        if (started) f.win.close();
        w.stop();
        check("RZXGUI-06",
              "closing the window with an unwritable recording: an error dialog, latched",
              started && w.count == 1 && !f.emu.rzx_recorder().is_recording() &&
                  f.emu.rzx_output_failed(path),
              fmt("started=%d dialogs=%d recording=%d latched=%d (expect 1,1,0,1)",
                  started ? 1 : 0, w.count, f.emu.rzx_recorder().is_recording() ? 1 : 0,
                  f.emu.rzx_output_failed(path) ? 1 : 0));
    }

    // RZXGUI-07 — Record RZX during playback: refused with a dialog (no IN
    // would reach the recorder, so the "recording" would hold no input).
    {
        Fixture f;
        const std::string src = tmp_path("src", ".rzx");
        const std::string out = tmp_path("out", ".rzx");
        bool playing = false;
        if (f.ok && f.emu.start_rzx_recording(src)) {
            f.emu.run_frame();
            f.emu.run_frame();
            playing = f.emu.stop_rzx_recording() && f.emu.load_rzx(src) &&
                      f.emu.rzx_player().is_playing();
        }
        DialogWatcher w;
        if (playing) f.win.handle_rzx_record_path(QString::fromStdString(out));
        w.stop();
        check("RZXGUI-07", "Record RZX while a recording plays: a dialog, and no recording",
              playing && w.count == 1 && !f.emu.rzx_recorder().is_recording(),
              fmt("playing=%d dialogs=%d recording=%d (expect 1,1,0)", playing ? 1 : 0,
                  w.count, f.emu.rzx_recorder().is_recording() ? 1 : 0));
        std::error_code ec;
        std::filesystem::remove(src, ec);
        std::filesystem::remove(out, ec);
    }
}

static void test_rzx_reset_notice() {
    // RZXGUI-08 — a reset that ended an interactive recording which then could
    // not be written: a dialog, posted to the event loop (the frontend calls
    // this from inside a frame tick).
    {
        Fixture f;
        DialogWatcher w;
        if (f.ok) {
            f.win.rzx_recording_ended_by_reset("/dev/full", /*written=*/false,
                                               /*unattended=*/false);
            for (int i = 0; i < 20 && w.count == 0; ++i) QApplication::processEvents();
        }
        w.stop();
        check("RZXGUI-08",
              "a reset ends an interactive recording that could not be written: a dialog",
              f.ok && w.count == 1 && w.text.contains("/dev/full"),
              fmt("dialogs=%d (expect 1)", w.count));
    }

    // RZXGUI-10 — Play RZX while recording: the recording is written and ended
    // first; when that write fails, the user is told the recording is lost —
    // and the playback still runs.
    {
        Fixture f;
        const std::string src = tmp_path("psrc", ".rzx");
        bool ready = f.ok && f.emu.start_rzx_recording(src);
        if (ready) f.emu.run_frame();
        ready = ready && f.emu.stop_rzx_recording() && f.emu.start_rzx_recording("/dev/full");
        if (ready) f.emu.run_frame();
        DialogWatcher w;
        if (ready) f.win.handle_rzx_play_path(QString::fromStdString(src));
        w.stop();
        check("RZXGUI-10",
              "Play RZX while recording to an unwritable file: a dialog about the lost "
              "recording, and the playback runs",
              ready && w.count == 1 && w.text.contains("/dev/full") &&
                  !f.emu.rzx_recorder().is_recording() && f.emu.rzx_player().is_playing(),
              fmt("ready=%d dialogs=%d playing=%d (expect 1,1,1)", ready ? 1 : 0, w.count,
                  f.emu.rzx_player().is_playing() ? 1 : 0));
        std::error_code ec;
        std::filesystem::remove(src, ec);
    }

    // RZXGUI-11 — Machine > Soft Reset (F4) during a recording that cannot be
    // written: the reset ends it, and the user is told it is lost.
    {
        Fixture f;
        QAction* soft = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("&Soft Reset")) soft = a;
        const bool started = f.ok && soft && f.emu.start_rzx_recording("/dev/full");
        if (started) f.emu.run_frame();
        DialogWatcher w;
        if (started) {
            soft->trigger();
            for (int i = 0; i < 20 && w.count == 0; ++i) QApplication::processEvents();
        }
        w.stop();
        check("RZXGUI-11",
              "Soft Reset during an unwritable recording: it ends, and a dialog says it is lost",
              started && !f.emu.rzx_recorder().is_recording() && w.count == 1 &&
                  w.text.contains("/dev/full"),
              fmt("started=%d recording=%d dialogs=%d (expect 1,0,1)", started ? 1 : 0,
                  f.emu.rzx_recorder().is_recording() ? 1 : 0, w.count));
    }

    // RZXGUI-09 — control: a written recording, or an unwritten COMMAND-LINE
    // one (a scripted run must never stop on a question), gets no dialog.
    {
        Fixture f;
        DialogWatcher w;
        if (f.ok) {
            f.win.rzx_recording_ended_by_reset("/tmp/written.rzx", true, false);
            f.win.rzx_recording_ended_by_reset("/dev/full", false, /*unattended=*/true);
            for (int i = 0; i < 20; ++i) QApplication::processEvents();
        }
        w.stop();
        check("RZXGUI-09",
              "control: no dialog for a saved recording, nor for an unattended one",
              f.ok && w.count == 0, fmt("dialogs=%d (expect 0)", w.count));
    }
}

int main(int argc, char** argv) {
    // MainWindow is a real QWidget, so a QApplication is required — but not a
    // display: force the offscreen QPA platform (quit_gate_test idiom).
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("RZX File-menu dialog tests\n\n");

    test_rzx_menu();
    test_rzx_reset_notice();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
