// Files the desktop asks JNext to open (macOS Finder double-click, Open With,
// a drop on the Dock icon), through the real filter and the real MainWindow.
//
// No VHDL oracle: host UI. The oracle is the feature design
// (doc/design/MACOS-FINDER-OPEN.md):
//   * a QFileOpenEvent sent to the QApplication reaches the handler with its
//     file and is consumed; other events pass through untouched;
//   * an open inside desktop_open::kLaunchWindowMs of the event loop starting
//     (or before it) is the launch: it loads with no question;
//   * after that it asks first; Cancel loads nothing, Open loads the file;
//   * an unattended run neither asks nor loads;
//   * the real question has Cancel as its default and Esc button.
//
// The load is observed at MainWindow's load callback, the same seam the
// frontend's cold boot hangs off (QtApp::init). Offscreen QPA,
// JNEXT_CONFIG_DIR in a temp dir.
//
// Run: ./build/test/desktop_open_test

#include <QAbstractButton>
#include <QApplication>
#include <QFile>
#include <QFileOpenEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "gui/desktop_open.h"
#include "gui/main_window.h"
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
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

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;
    std::vector<std::string> loads;
    std::vector<QString> asked;
    bool answer = false;

    explicit Fixture(bool inject_confirm = true)
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        win.set_load_file_callback([this](const std::string& f, bool) { loads.push_back(f); });
        if (inject_confirm)
            win.set_confirm_desktop_open_callback([this](const QString& p) {
                asked.push_back(p);
                return answer;
            });
    }
};

// Records the first modal QMessageBox, then answers it, so a wrongly opened
// dialog can never hang the suite (idiom: nex_v13_dialog_test.cpp).
struct DialogWatcher {
    bool open_it;
    bool saw_modal = false, cancel_default = false, cancel_escape = false;
    QTimer timer;

    explicit DialogWatcher(bool open) : open_it(open)
    {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!mb || saw_modal) return;
            saw_modal = true;
            QAbstractButton* cancel = mb->button(QMessageBox::Cancel);
            cancel_default = cancel && static_cast<QAbstractButton*>(mb->defaultButton()) == cancel;
            cancel_escape  = cancel && mb->escapeButton() == cancel;
            if (open_it) {
                for (QAbstractButton* b : mb->buttons())
                    if (mb->buttonRole(b) == QMessageBox::AcceptRole) { b->click(); return; }
                mb->done(QMessageBox::Cancel);
            } else {
                mb->close();   // the Esc path: the escape button
            }
        });
        timer.start(1);
    }
};

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfgdir;
    qputenv("JNEXT_CONFIG_DIR", cfgdir.path().toUtf8());
    QApplication app(argc, argv);

    // A file handle_load_path() hands straight to the load callback.
    const QString tap = cfgdir.filePath(QStringLiteral("game.tap"));
    {
        QFile f(tap);
        if (!f.open(QIODevice::WriteOnly) || f.write("\x13\x00\x00\x00", 4) != 4) {
            std::printf("cannot write the fixture %s\n", tap.toStdString().c_str());
            return 2;
        }
    }
    const std::string tap_s = tap.toStdString();

    // DOP-01
    check("DOP-01", "at_launch: before the loop (-1), at 0 and at window-1 ms is the launch; at the window it is not",
          desktop_open::at_launch(-1) && desktop_open::at_launch(0) &&
              desktop_open::at_launch(desktop_open::kLaunchWindowMs - 1) &&
              !desktop_open::at_launch(desktop_open::kLaunchWindowMs));

    {   // DOP-02, DOP-03
        std::vector<QString> got;
        desktop_open::FileOpenFilter filter([&](const QString& p) { got.push_back(p); });
        app.installEventFilter(&filter);
        QFileOpenEvent fe(tap);
        const bool consumed = QApplication::sendEvent(&app, &fe);
        check("DOP-02", "a QFileOpenEvent sent to the application reaches the handler with its file, consumed",
              consumed && got.size() == 1 && got[0] == tap);
        QObject target;
        QEvent other(QEvent::User);
        QApplication::sendEvent(&target, &other);
        check("DOP-03", "any other event passes the filter untouched", got.size() == 1);
        app.removeEventFilter(&filter);
    }
    {   // DOP-04
        Fixture f;
        f.win.open_from_desktop(tap, /*at_launch=*/true);
        check("DOP-04", "at launch: loads straight away, without asking",
              f.asked.empty() && f.loads.size() == 1 && f.loads[0] == tap_s);
    }
    {   // DOP-05
        Fixture f;
        f.answer = false;
        f.win.open_from_desktop(tap, false);
        check("DOP-05", "running, the answer is Cancel: asked about this file, nothing loaded",
              f.asked.size() == 1 && f.asked[0] == tap && f.loads.empty());
    }
    {   // DOP-06
        Fixture f;
        f.answer = true;
        f.win.open_from_desktop(tap, false);
        check("DOP-06", "running, the answer is Open: the file loads",
              f.asked.size() == 1 && f.loads.size() == 1 && f.loads[0] == tap_s);
    }
    {   // DOP-07
        Fixture f;
        f.answer = true;
        f.win.set_unattended(true);
        f.win.open_from_desktop(tap, false);
        check("DOP-07", "running unattended: asks nobody and loads nothing", f.asked.empty() && f.loads.empty());
    }
    {   // DOP-08
        Fixture f(/*inject_confirm=*/false);
        DialogWatcher w(/*open=*/false);
        f.win.open_from_desktop(tap, false);
        check("DOP-08", "the real question: Cancel is its default and Esc button; Esc loads nothing",
              w.saw_modal && w.cancel_default && w.cancel_escape && f.loads.empty());
    }
    {   // DOP-09
        Fixture f(false);
        DialogWatcher w(/*open=*/true);
        f.win.open_from_desktop(tap, false);
        check("DOP-09", "the real question: Open loads the file", w.saw_modal && f.loads.size() == 1);
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
