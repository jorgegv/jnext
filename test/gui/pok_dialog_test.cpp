// GH #381 - Machine > POK Trainers..., driven for real: the real MainWindow, its
// real menu action, the real file dialog, the real trainers dialog and the real
// value prompt, over a real (48K) Emulator and the debugger backend.
//
// ORACLE. The user's flow on the issue: load a .pok, choose a trainer, apply it
// to the running game, undo it, and save a snapshot of the result. What each row
// asserts is observable: the bytes in the machine, the text a row shows, which
// button is enabled, which dialog opened. The decisions behind them (where a poke
// lands, what undo restores, what a state means) are pinned without Qt in
// pok_trainers_test; this suite pins the wiring on top: that the dialog drives the
// session, that the menu action and File > Load reach it, that a rebuilt machine
// refreshes it, and that a refusal is SHOWN. The WoS POKformat.txt gives value 256
// its meaning (ask the user), which PKD-03 drives through the real prompt.
//
// Modal dialogs (the file picker, the value prompt, a refusal) are answered by a
// timer polling QApplication::activeModalWidget(), the idiom of
// nex_v13_dialog_test and source_panel_test. The trainers dialog itself is
// window-modal, so it is also an "active modal widget": every answerer below
// ignores a widget that is not the kind of dialog it is waiting for.
//
// Qt is required, a display is not: main() forces the offscreen QPA platform.
// Run: ./build/test/pok_dialog_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/rzx.h"
#include "core/rzx_player.h"
#include "debug/debugger.h"
#include "gui/main_window.h"
#include "memory/mmu.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%02X", v);
    return b;
}

// ── fixtures ───────────────────────────────────────────────────────────────

std::string g_dir;   // a scratch directory for the .pok files, removed at the end

std::string write_file(const char* name, const std::string& bytes) {
    const std::string path = g_dir + "/" + name;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

/// A real MainWindow over a real 48K machine, with the load callback recorded.
/// The memory the trainers touch holds bytes this suite put there, never equal to
/// what a trainer writes, so a row reads "not applied" and a write is visible.
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;
    std::vector<std::string> loaded;   // every file handed to the load callback
    bool ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        win.set_load_file_callback([this](const std::string& f, bool) { loaded.push_back(f); });
        // 0x8003: the poke of "Infinite lives" is 0x00; 0x8004 and 0x8005: 0x01.
        emu.mmu().write(0x8003, 0x35);
        emu.mmu().write(0x8004, 0x77);
        emu.mmu().write(0x8005, 0x78);
        // The two addresses of the value trainer.
        emu.mmu().write(36864, 0x11);
        emu.mmu().write(36900, 0x33);
        QApplication::processEvents();
        ok = true;
    }

    QDialog* dialog() { return win.findChild<QDialog*>(QStringLiteral("pok_trainers_dialog")); }
    QTreeWidget* tree() {
        QDialog* d = dialog();
        return d ? d->findChild<QTreeWidget*>(QStringLiteral("trainers")) : nullptr;
    }
    QPushButton* button(const char* name) {
        QDialog* d = dialog();
        return d ? d->findChild<QPushButton*>(QString::fromLatin1(name)) : nullptr;
    }
    /// Select trainer `i` (file order).
    void select(int i) {
        if (QTreeWidget* t = tree()) t->setCurrentItem(t->topLevelItem(i));
    }
    QString status(int i) {
        QTreeWidget* t = tree();
        return t && t->topLevelItem(i) ? t->topLevelItem(i)->text(1) : QStringLiteral("<none>");
    }
    QString name(int i) {
        QTreeWidget* t = tree();
        return t && t->topLevelItem(i) ? t->topLevelItem(i)->text(0) : QStringLiteral("<none>");
    }
    uint8_t rd(uint16_t a) { return emu.mmu().peek(a); }
};

/// Answer each modal dialog that opens while `open` runs: `act(widget)` is called
/// for every poll that finds one, and must close it if it is its kind (it is
/// called again until the dialog is gone). Returns how many polls found a modal.
int drive_modals(const std::function<void(QWidget*)>& act, const std::function<void()>& open) {
    int polls = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        QWidget* w = QApplication::activeModalWidget();
        if (!w || !w->isVisible()) return;
        ++polls;
        act(w);
    });
    timer.start(1);
    open();
    timer.stop();
    QApplication::processEvents();
    return polls;
}

void click_ok(QWidget* w) {
    if (auto* mb = qobject_cast<QMessageBox*>(w))
        if (QAbstractButton* b = mb->button(QMessageBox::Ok)) b->click();
}

/// The test file most rows share: four trainers, one with a name in UTF-8, one
/// in Latin-1, one invalid.
std::string fixture_pok() {
    return write_file("shared.pok",
                      std::string("NInfinite lives\nZ  8 32771 0 53\n"            // lines 1-2
                                  "NMoney (\xC2\xA3") + "50)\nZ  8 32772 1 0\n" +   // 3-4, UTF-8
                          "NCaf\xE9\nZ  8 32773 1 0\n"                              // 5-6, Latin-1
                          "NBroken\nZ  8 99 1 0\nY\n");                             // 7-8
}

/// A trainer that leaves two values open.
std::string value_pok() {
    return write_file("value.pok", "NLives\nM  8 36864 256 0\nZ  8 36900 256 0\n");
}

// ── the rows ───────────────────────────────────────────────────────────────

void test_listing() {
    // PKD-01
    Fixture f;
    const bool opened = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    QApplication::processEvents();
    QDialog* d = f.dialog();
    QTreeWidget* t = f.tree();
    const QString money = QStringLiteral("Money (") + QChar(0x00A3) + QStringLiteral("50)");
    const QString cafe = QStringLiteral("Caf") + QChar(0x00E9);
    check("PKD-01", "open_pok_file shows the trainers dialog with every trainer in file order; a UTF-8 name and a "
                    "Latin-1 name both display as text; an invalid trainer reads \"invalid - line N: ...\" "
                    "(WoS POKformat.txt: a name is bytes of the file)",
          opened && d && d->isVisible() && t && t->topLevelItemCount() == 4 &&
              f.name(0) == QStringLiteral("Infinite lives") && f.name(1) == money && f.name(2) == cafe &&
              f.name(3) == QStringLiteral("Broken") && f.status(0) == QStringLiteral("not applied") &&
              f.status(3) == QStringLiteral("invalid - line 8: address 99 is outside 16384-65535"),
          f.name(1).toStdString() + "|" + f.name(2).toStdString() + "|" + f.status(3).toStdString());
}

void test_apply_remove() {
    // PKD-02
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    f.select(0);
    QPushButton* apply = f.button("apply");
    QPushButton* remove = f.button("remove");
    if (apply) apply->click();
    const bool written = f.rd(0x8003) == 0x00 && f.status(0) == QStringLiteral("applied");
    if (remove) remove->click();
    check("PKD-02", "selecting a trainer and pressing Apply writes it to the machine and the row reads \"applied\"; "
                    "Remove puts the byte back and the row reads \"not applied\"",
          f.ok && apply && remove && written && f.rd(0x8003) == 0x35 &&
              f.status(0) == QStringLiteral("not applied"),
          hex(f.rd(0x8003)) + "|" + f.status(0).toStdString());
}

void test_value_prompt() {
    // PKD-03
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(value_pok()));
    f.select(0);
    QPushButton* apply = f.button("apply");

    int offered = -1;
    const int asked = drive_modals(
        [&](QWidget* w) {
            if (auto* in = qobject_cast<QInputDialog*>(w)) {
                offered = in->intValue();
                in->setIntValue(42);
                in->accept();
            }
        },
        [&]() { if (apply) apply->click(); });
    const bool applied = f.rd(36864) == 42 && f.rd(36900) == 42 && f.status(0) == QStringLiteral("applied");

    if (QPushButton* remove = f.button("remove")) remove->click();
    const bool removed = f.rd(36864) == 0x11 && f.rd(36900) == 0x33;

    drive_modals(
        [&](QWidget* w) {
            if (auto* in = qobject_cast<QInputDialog*>(w)) in->reject();
        },
        [&]() { if (apply) apply->click(); });
    const bool cancelled = f.rd(36864) == 0x11 && f.rd(36900) == 0x33 &&
                           f.status(0) == QStringLiteral("not applied");
    check("PKD-03", "a trainer with value-256 pokes asks for the value in a prompt offering the byte memory holds; OK "
                    "with 42 writes 42 to every such poke; Cancel writes nothing (WoS POKformat.txt: 256 = ask "
                    "the user)",
          f.ok && apply && asked > 0 && offered == 0x11 && applied && removed && cancelled,
          "offered=" + std::to_string(offered) + " " + hex(f.rd(36864)) + " " + hex(f.rd(36900)));
}

void test_buttons() {
    // PKD-04
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    QPushButton* apply = f.button("apply");
    QPushButton* remove = f.button("remove");
    f.select(3);   // the invalid one
    const bool invalid_off = apply && remove && !apply->isEnabled() && !remove->isEnabled();
    f.select(0);
    const bool fresh = apply && remove && apply->isEnabled() && !remove->isEnabled();
    if (apply) apply->click();
    const bool applied = apply && remove && !apply->isEnabled() && remove->isEnabled();
    check("PKD-04", "Apply and Remove are both disabled on an invalid trainer; on a fresh one only Apply is "
                    "enabled; on an applied one only Remove is",
          f.ok && invalid_off && fresh && applied,
          std::to_string(invalid_off) + std::to_string(fresh) + std::to_string(applied));
}

void test_load_failure() {
    // PKD-05
    Fixture f;
    const std::string only_y = write_file("only-y.pok", "Y\n");
    QString shown;
    bool opened = true;
    const int boxes = drive_modals(
        [&](QWidget* w) {
            if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                shown = mb->text();
                click_ok(w);
            }
        },
        [&]() { opened = f.win.open_pok_file(QString::fromStdString(only_y)); });
    check("PKD-05", "a file with no trainer is refused in one warning that says it \"holds no trainers\"; "
                    "open_pok_file returns false and no trainers dialog is created",
          f.ok && !opened && boxes > 0 && shown.contains(QStringLiteral("holds no trainers")) && !f.dialog(),
          shown.toStdString());
}

void test_file_load() {
    // PKD-06
    Fixture f;
    const std::string path = fixture_pok();
    f.win.handle_load_path(QString::fromStdString(path));
    QApplication::processEvents();
    check("PKD-06", "File > Load of a .pok opens the trainers dialog on that file and does not ask for a cold boot "
                    "(before this a .pok fell to the NEX route, which reset the machine and then failed)",
          f.ok && f.dialog() && f.dialog()->isVisible() && f.tree() && f.tree()->topLevelItemCount() == 4 &&
              f.loaded.empty(),
          std::to_string(f.loaded.size()) + " load callbacks");
}

void test_filter() {
    // PKD-07
    const QString filter = MainWindow::load_filter();
    check("PKD-07", "the File > Load filter offers \"POK Trainers (*.pok)\" and still starts with the combined "
                    "\"Spectrum Files\" entry, which does not claim .pok",
          filter.contains(QStringLiteral("POK Trainers (*.pok)")) &&
              filter.startsWith(QStringLiteral("Spectrum Files (*.nex *.jns")) &&
              !filter.section(QStringLiteral(";;"), 0, 0).contains(QStringLiteral("pok")),
          filter.toStdString());
}

void test_refusal_shown() {
    // PKD-08
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    f.select(0);
    QPushButton* apply = f.button("apply");
    f.emu.rzx_player().start(RzxRecording{});
    QString shown;
    const int boxes = drive_modals(
        [&](QWidget* w) {
            if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                shown = mb->text();
                click_ok(w);
            }
        },
        [&]() { if (apply) apply->click(); });
    const bool untouched = f.rd(0x8003) == 0x35 && f.status(0) == QStringLiteral("not applied");
    f.emu.rzx_player().stop();
    check("PKD-08", "during an RZX playback Apply shows the refusal in a warning that names the RZX, and memory is "
                    "unchanged (the backend's poke refuses under RZX)",
          f.ok && apply && boxes > 0 && shown.contains(QStringLiteral("RZX")) && untouched,
          shown.toStdString() + "|" + hex(f.rd(0x8003)));
}

void test_menu_action() {
    // PKD-09: the real action, found by its text, the real file picker, the real dialog.
    Fixture f;
    QAction* action = nullptr;
    for (QAction* a : f.win.findChildren<QAction*>())
        if (a->text() == QStringLiteral("POK T&rainers...")) action = a;
    const std::string path = fixture_pok();
    int pickers = 0;
    drive_modals(
        [&](QWidget* w) {
            if (auto* fd = qobject_cast<QFileDialog*>(w)) {
                ++pickers;
                fd->selectFile(QString::fromStdString(path));
                static_cast<QDialog*>(fd)->accept();   // QFileDialog::accept() is protected
            }
        },
        [&]() { if (action) action->trigger(); });
    QApplication::processEvents();
    check("PKD-09", "Machine > POK Trainers... with no file open asks for one in a file dialog, and the file chosen "
                    "opens the trainers dialog on its trainers",
          f.ok && action && pickers > 0 && f.dialog() && f.dialog()->isVisible() && f.tree() &&
              f.tree()->topLevelItemCount() == 4,
          std::to_string(pickers) + " pickers");
}

void test_rebuilt_machine() {
    // PKD-10
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    f.select(0);
    if (QPushButton* apply = f.button("apply")) apply->click();
    const bool applied = f.status(0) == QStringLiteral("applied");
    f.win.set_emulator(&f.emu);   // what QtApp does after every machine rebuild
    QApplication::processEvents();
    check("PKD-10", "after the machine is rebuilt (set_emulator again) the undo records are forgotten and the open "
                    "dialog says so: the applied trainer reads \"applied (found in memory)\"",
          f.ok && applied && f.status(0) == QStringLiteral("applied (found in memory)"),
          f.status(0).toStdString());
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // PKD-09 makes the window remember the directory it picked from: keep that
    // out of the user's own configuration.
    QTemporaryDir config_dir;
    qputenv("JNEXT_CONFIG_DIR", config_dir.path().toUtf8());
    QApplication app(argc, argv);

    g_dir = jtp::make_temp_dir("jnext_pokdlg_");
    std::printf("POK Trainers dialog (GH #381)\n\n");

    test_listing();
    test_apply_remove();
    test_value_prompt();
    test_buttons();
    test_load_failure();
    test_file_load();
    test_filter();
    test_refusal_shown();
    test_menu_action();
    test_rebuilt_machine();

    std::error_code ec;
    std::filesystem::remove_all(g_dir, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
