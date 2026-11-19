// ===========================================================================
// --no-sdcard in the GUI (GH #540): the session has no card slot, so the
// window says so. File > Insert SD Card Image... and Eject SD Card are
// disabled (the second slot's too), and the status bar shows "SD: none". A session with a card (or
// without the mode) shows neither: the status bar is as it always was.
//
// Asserted on what the user sees: the actions by their menu text and the
// status cell by its text, not by member pointers.
//
// Qt is required (MainWindow is a real QWidget), no display is: main() forces
// the offscreen QPA platform, as esp_status_test does.
// Run: ./build/test/no_sdcard_ui_test
// ===========================================================================

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <string>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "gui/main_window.h"
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool ok, const std::string& detail = {})
{
    report_row_id(id);
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %-12s %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %-12s %s%s%s\n", id, desc, detail.empty() ? "" : " -- ", detail.c_str());
    }
}

// The visible status-bar cell whose text starts with "SD:", or null.
QLabel* sd_cell(MainWindow& win)
{
    for (QLabel* label : win.statusBar()->findChildren<QLabel*>())
        if (label->text().startsWith(QStringLiteral("SD:")) && label->isVisibleTo(&win)) return label;
    return nullptr;
}

// The menu action with this text (mnemonic included), or null.
QAction* action(MainWindow& win, const QString& text)
{
    for (QAction* a : win.menuBar()->findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;   // outlives the window
    MainWindow win;

    explicit Fixture(bool no_sdcard)
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.silent = true;
        cfg.rewind_buffer_frames = 0;
        cfg.no_sdcard = no_sdcard;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
    }
};

const QString kInsert = QStringLiteral("&Insert SD Card Image...");
const QString kEject  = QStringLiteral("&Eject SD Card");
const QString kInsert2 = QStringLiteral("Insert Second SD Card Image...");
const QString kEject2  = QStringLiteral("Eject Second SD Card");

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // MainWindow reads (and may write) jnext.conf: keep it off the user's real ~/.jnext.
    QTemporaryDir cfg_dir;
    if (!cfg_dir.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg_dir.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("=== --no-sdcard in the GUI (GH #540) ===\n\n");

    {
        Fixture f{true};
        QLabel* cell = sd_cell(f.win);
        check("NOSDUI-01", "--no-sdcard: the status bar shows \"SD: none\"",
              cell && cell->text() == QStringLiteral("SD: none"),
              cell ? cell->text().toStdString() : "no SD cell");
        QAction* ins = action(f.win, kInsert);
        QAction* ej  = action(f.win, kEject);
        check("NOSDUI-02", "--no-sdcard: File > Insert SD Card Image... and Eject SD Card are disabled",
              ins && ej && !ins->isEnabled() && !ej->isEnabled(),
              std::string("insert=") + (ins ? (ins->isEnabled() ? "enabled" : "disabled") : "missing") +
              " eject=" + (ej ? (ej->isEnabled() ? "enabled" : "disabled") : "missing"));
    }
    {
        Fixture f{false};
        QAction* ins = action(f.win, kInsert);
        QAction* ej  = action(f.win, kEject);
        check("NOSDUI-03", "without --no-sdcard: no SD cell, and both card actions are enabled",
              sd_cell(f.win) == nullptr && ins && ej && ins->isEnabled() && ej->isEnabled());
    }

    {
        Fixture off{true}, on{false};
        QAction* i1 = action(off.win, kInsert2);
        QAction* e1 = action(off.win, kEject2);
        QAction* i2 = action(on.win, kInsert2);
        QAction* e2 = action(on.win, kEject2);
        check("NOSDUI-04", "File > Insert / Eject Second SD Card Image are disabled with --no-sdcard, enabled without it",
              i1 && e1 && i2 && e2 && !i1->isEnabled() && !e1->isEnabled() && i2->isEnabled() && e2->isEnabled(),
              std::string("no-sdcard: insert2=") + (i1 ? (i1->isEnabled() ? "enabled" : "disabled") : "missing") +
              " eject2=" + (e1 ? (e1->isEnabled() ? "enabled" : "disabled") : "missing") +
              "; control: insert2=" + (i2 ? (i2->isEnabled() ? "enabled" : "disabled") : "missing") +
              " eject2=" + (e2 ? (e2->isEnabled() ? "enabled" : "disabled") : "missing"));
    }

    std::printf("\n============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
