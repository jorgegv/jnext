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
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
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
        // 0x8003: the poke of "Infinite lives" is 0x00; 0x8004 to 0x8006: 0x01.
        emu.mmu().write(0x8003, 0x35);
        emu.mmu().write(0x8004, 0x77);
        emu.mmu().write(0x8005, 0x78);
        emu.mmu().write(0x8006, 0x79);
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

/// Closes a dialog no row is waiting for. Nothing in this suite leaves one up on
/// purpose: if a defect raises one outside a drive_modals() window, its nested
/// event loop would hang the whole suite. Closing it lets the row fail instead,
/// and says so. The interval is long on purpose: a row that answers its own
/// dialog does so within milliseconds, so a dialog still up after seconds is stuck.
class ModalGuard {
public:
    ModalGuard() {
        QObject::connect(&timer_, &QTimer::timeout, [] {
            QWidget* w = QApplication::activeModalWidget();
            if (qobject_cast<QMessageBox*>(w) || qobject_cast<QInputDialog*>(w) ||
                qobject_cast<QFileDialog*>(w)) {
                std::printf("  NOTE: closed a %s nobody was waiting for\n", w->metaObject()->className());
                w->close();
            }
        });
        timer_.start(3000);
    }

private:
    QTimer timer_;
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

/// The test file most rows share: five trainers, one with a name in UTF-8, one
/// in Latin-1, one invalid, one without a name.
std::string fixture_pok() {
    return write_file("shared.pok",
                      std::string("NInfinite lives\nZ  8 32771 0 53\n"            // lines 1-2
                                  "NMoney (\xC2\xA3") + "50)\nZ  8 32772 1 0\n" +   // 3-4, UTF-8
                          "NCaf\xE9\nZ  8 32773 1 0\n"                              // 5-6, Latin-1
                          "NBroken\nZ  8 99 1 0\n"                                   // 7-8
                          "N\nZ  8 32774 1 0\nY\n");                                 // 9-10, no name
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
                    "Latin-1 name both display as text; an invalid trainer reads \"invalid - line N: ...\"; "
                    "a trainer without a name is shown as \"(no name)\" (WoS POKformat.txt: a name is bytes "
                    "of the file)",
          opened && d && d->isVisible() && t && t->topLevelItemCount() == 5 &&
              f.name(0) == QStringLiteral("Infinite lives") && f.name(1) == money && f.name(2) == cafe &&
              f.name(3) == QStringLiteral("Broken") && f.name(4) == QStringLiteral("(no name)") &&
              f.status(0) == QStringLiteral("not applied") && f.status(4) == QStringLiteral("not applied") &&
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
    // The first trainer is selected when the dialog opens: Apply is ready at once.
    const bool preselected = apply && remove && apply->isEnabled() && !remove->isEnabled();
    f.select(3);   // the invalid one
    const bool invalid_off = apply && remove && !apply->isEnabled() && !remove->isEnabled();
    f.select(0);
    const bool fresh = apply && remove && apply->isEnabled() && !remove->isEnabled();
    if (apply) apply->click();
    const bool applied = apply && remove && !apply->isEnabled() && remove->isEnabled();
    check("PKD-04", "Apply and Remove are both disabled on an invalid trainer; on a fresh one only Apply is "
                    "enabled, and the first trainer is already selected when the dialog opens; on an applied "
                    "one only Remove is",
          f.ok && preselected && invalid_off && fresh && applied,
          std::to_string(preselected) + std::to_string(invalid_off) + std::to_string(fresh) +
              std::to_string(applied));
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
    // The extension is matched in any case.
    Fixture upper;
    upper.win.handle_load_path(QString::fromStdString(write_file("UPPER.POK", "NT\nZ 8 32771 0 53\n")));
    QApplication::processEvents();
    check("PKD-06", "File > Load of a .pok (or .POK) opens the trainers dialog on that file and does not ask for a "
                    "cold boot (before this a .pok fell to the NEX route, which reset the machine and then failed)",
          f.ok && f.dialog() && f.dialog()->isVisible() && f.tree() && f.tree()->topLevelItemCount() == 5 &&
              f.loaded.empty() && upper.dialog() && upper.dialog()->isVisible() && upper.loaded.empty(),
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
                    "opens the trainers dialog on its trainers; the folder it came from is remembered for the "
                    "next picker",
          f.ok && action && pickers > 0 && f.dialog() && f.dialog()->isVisible() && f.tree() &&
              f.tree()->topLevelItemCount() == 5 &&
              f.win.app_config().data().last_load_dir == QFileInfo(QString::fromStdString(path)).absolutePath(),
          std::to_string(pickers) + " pickers " + f.win.app_config().data().last_load_dir.toStdString());
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

/// A file-dialog answerer for drive_modals: picks `path` in the first QFileDialog.
std::function<void(QWidget*)> pick_file(const std::string& path, int* pickers) {
    return [path, pickers](QWidget* w) {
        if (auto* fd = qobject_cast<QFileDialog*>(w)) {
            ++*pickers;
            fd->selectFile(QString::fromStdString(path));
            static_cast<QDialog*>(fd)->accept();   // QFileDialog::accept() is protected
        }
    };
}

void test_file_problems() {
    // PKD-11
    Fixture f;
    std::string junk = "NOk\nZ  8 32771 0 53\n";
    for (int i = 0; i < 25; ++i) junk += "junk\n";
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(write_file("junk.pok", junk)));
    QLabel* label = f.dialog() ? f.dialog()->findChild<QLabel*>(QStringLiteral("problems")) : nullptr;
    const QStringList lines = label ? label->text().split(QLatin1Char('\n')) : QStringList();
    Fixture clean;
    clean.ok = clean.ok && clean.win.open_pok_file(QString::fromStdString(value_pok()));
    QLabel* none = clean.dialog() ? clean.dialog()->findChild<QLabel*>(QStringLiteral("problems")) : nullptr;
    check("PKD-11", "the lines of a file that belong to no trainer are listed with their line numbers, twenty at "
                    "most and then \"and N more\"; a file without any shows no list",
          f.ok && label && label->isVisible() && lines.size() == 21 &&
              lines.first() == QStringLiteral("line 3: unexpected line (starts with 'j')") &&
              lines.last() == QStringLiteral("and 5 more") && none && !none->isVisible(),
          QString::number(lines.size()).toStdString());
}

void test_open_button() {
    // PKD-12
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    const std::string other = value_pok();
    int pickers = 0;
    QPushButton* open = f.button("open");
    drive_modals(pick_file(other, &pickers), [&]() { if (open) open->click(); });
    QApplication::processEvents();
    QLabel* path = f.dialog() ? f.dialog()->findChild<QLabel*>(QStringLiteral("path")) : nullptr;
    const bool replaced = pickers > 0 && f.tree() && f.tree()->topLevelItemCount() == 1 &&
                          f.name(0) == QStringLiteral("Lives") && path &&
                          path->text() == QString::fromStdString(other);
    // Closed, then opened again from the menu: the file is still there, and nothing is asked.
    if (f.dialog()) f.dialog()->reject();
    QAction* action = nullptr;
    for (QAction* a : f.win.findChildren<QAction*>())
        if (a->text() == QStringLiteral("POK T&rainers...")) action = a;
    int asked = 0;
    drive_modals(pick_file(other, &asked), [&]() { if (action) action->trigger(); });
    QApplication::processEvents();
    check("PKD-12", "the dialog's Open POK File... button asks for another file and shows its trainers instead; "
                    "closed and reopened from the Machine menu it shows that file again without asking",
          f.ok && open && replaced && action && asked == 0 && f.dialog() && f.dialog()->isVisible() &&
              f.tree() && f.tree()->topLevelItemCount() == 1,
          std::to_string(pickers) + " " + std::to_string(asked));
}

void test_status_follows_memory() {
    // PKD-16
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(fixture_pok()));
    QPushButton* apply = f.button("apply");
    f.select(0);
    if (apply) apply->click();
    const bool applied = f.status(0) == QStringLiteral("applied");
    // The program writes the byte back; selecting another row re-reads every status.
    f.emu.mmu().write(0x8003, 0x35);
    f.select(1);
    const bool by_selection = f.status(0) == QStringLiteral("not applied");
    // Closed, the dialog cannot know what memory does; shown again, it says.
    f.select(0);
    if (apply) apply->click();
    const bool again = f.status(0) == QStringLiteral("applied");
    if (f.dialog()) f.dialog()->reject();
    f.emu.mmu().write(0x8003, 0x35);
    QAction* action = nullptr;
    for (QAction* a : f.win.findChildren<QAction*>())
        if (a->text() == QStringLiteral("POK T&rainers...")) action = a;
    if (action) action->trigger();
    QApplication::processEvents();
    const bool by_show = f.status(0) == QStringLiteral("not applied");
    check("PKD-16", "the rows follow memory: a byte the program wrote back shows as not applied when another row "
                    "is selected, and when the dialog is shown again after being closed",
          f.ok && apply && action && applied && by_selection && again && by_show,
          std::to_string(applied) + std::to_string(by_selection) + std::to_string(again) +
              std::to_string(by_show));
}

void test_no_machine() {
    // PKD-13
    MainWindow bare;   // no emulator, no debugger
    const bool opened = bare.open_pok_file(QString::fromStdString(fixture_pok()));
    check("PKD-13", "a window that has no machine yet cannot open a POK file: open_pok_file returns false and "
                    "creates no dialog",
          !opened && !bare.findChild<QDialog*>(QStringLiteral("pok_trainers_dialog")));
}

void test_unattended() {
    // PKD-14
    Fixture f;
    f.win.set_unattended(true);
    const std::string only_y = write_file("only-y2.pok", "Y\n");
    bool opened = true;
    const int boxes = drive_modals(
        [&](QWidget* w) { click_ok(w); },   // a dialog nobody is there to answer would block the run
        [&]() { opened = f.win.open_pok_file(QString::fromStdString(only_y)); });
    check("PKD-14", "in an unattended run (nobody to answer a dialog) a POK file that cannot be used is reported in "
                    "the status bar and opens no dialog",
          f.ok && !opened && boxes == 0 &&
              f.win.statusBar()->currentMessage().startsWith(QStringLiteral("Cannot load POK file")),
          f.win.statusBar()->currentMessage().toStdString());
}

void test_unplaceable_value() {
    // PKD-15
    Fixture f;
    f.ok = f.ok && f.win.open_pok_file(QString::fromStdString(
                       write_file("far.pok", "NFar\nZ  3 49443 256 0\n")));   // bank 3: not on a 48K
    f.select(0);
    QPushButton* apply = f.button("apply");
    QString shown;
    bool prompted = false;
    drive_modals(
        [&](QWidget* w) {
            if (auto* in = qobject_cast<QInputDialog*>(w)) {
                prompted = true;
                in->reject();
            } else if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                shown = mb->text();
                click_ok(w);
            }
        },
        [&]() { if (apply) apply->click(); });
    check("PKD-15", "a trainer that asks for a value but has a poke that cannot land (bank 3 on a 48K) shows why "
                    "instead of asking for the value",
          f.ok && apply && !prompted && shown.contains(QStringLiteral("48K")) && f.rd(0xC123) == 0,
          shown.toStdString());
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // PKD-09 makes the window remember the directory it picked from: keep that
    // out of the user's own configuration.
    QTemporaryDir config_dir;
    qputenv("JNEXT_CONFIG_DIR", config_dir.path().toUtf8());
    QApplication app(argc, argv);
    ModalGuard guard;

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
    test_file_problems();
    test_open_button();
    test_status_follows_memory();
    test_no_machine();
    test_unattended();
    test_unplaceable_value();

    std::error_code ec;
    std::filesystem::remove_all(g_dir, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
