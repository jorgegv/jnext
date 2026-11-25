// ===========================================================================
// File > Upgrade SD Card..., Go Back to the Previous SD Card and Delete the Previous SD Card Backup
// (GH #555 S6): the three actions exist in the File menu, their enablement follows
// sdcard::backup::menu_state() for the real default card set, and the delete entry names the backup's
// size. Asserted on what the user sees (the menu text and the enabled state), on an offscreen MainWindow
// over a scratch config directory whose default card is an older distribution (the address the provisioner
// would download is the seam's 99.99). Qt is required, a display is not.
// ===========================================================================
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QElapsedTimer>
#include "peripheral/sd_card.h"
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/fatfs_diskio.h"
#include "core/sdcard_backup.h"
#include "core/sdcard_upgrade.h"
#include "peripheral/sd_write_journal.h"
#include "platform/emulator_boot.h"
#include "core/sdcard_provisioner.h"
#include "debug/debugger.h"
#include "gui/main_window.h"
#include "gui/sdcard_manager_dialog.h"
#include "../row_id.h"

namespace fs = std::filesystem;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool ok, const std::string& detail = {})
{
    report_row_id(id);
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("  PASS %s: %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s%s\n", id, desc, detail.empty() ? "" : " [", detail.c_str(), detail.empty() ? "" : "]");
    }
}

QAction* action_starting(MainWindow& win, const QString& prefix)
{
    for (QAction* a : win.menuBar()->findChildren<QAction*>())
        if (a->text().startsWith(prefix)) return a;
    return nullptr;
}

void put(const std::string& p, size_t bytes)
{
    std::error_code ec;
    fs::create_directories(fs::path(p).parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const std::string blk(1 << 16, 'x');
    for (size_t done = 0; done < bytes; done += blk.size()) f.write(blk.data(), static_cast<std::streamsize>(std::min(blk.size(), bytes - done)));
}

// A window whose slot 0 holds `card` (the default card or another image).
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;
    explicit Fixture(const std::string& card)
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.silent = true;
        cfg.rewind_buffer_frames = 0;
        cfg.sd_card_image = card;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
    }
};

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg_dir;
    if (!cfg_dir.isValid()) { std::printf("  FAIL SDMENU-00: no scratch directory\nTotal:    0  Passed:    0  Failed:    1  Skipped:    0\n"); return 1; }
    qputenv("JNEXT_CONFIG_DIR", cfg_dir.path().toUtf8());
    qputenv("JNEXT_SDCARD_DISTRO_URL", "http://127.0.0.1:1/sn-emulator-99.99.zip");
    QApplication app(argc, argv);

    const std::string dir  = sdcard::default_sdcard_dir();
    const std::string card = sdcard::default_sdcard_image_path();
    put(card, 1 << 16);
    { std::ofstream(card + sdcard::kDistroSuffix) << "sn-emulator-24.11.zip\n"; }

    {
        Fixture f(card);
        QAction* up = action_starting(f.win, QStringLiteral("Upgrade SD Card"));
        QAction* gb = action_starting(f.win, QStringLiteral("Go Back to the Previous SD Card"));
        QAction* del = action_starting(f.win, QStringLiteral("Delete the Previous SD Card Backup"));
        check("SDMENU-07A", "File has the three card-set actions; with an older default card in slot 0 and no backup only Upgrade is enabled",
              up && gb && del && up->isEnabled() && !gb->isEnabled() && !del->isEnabled(),
              std::string("up=") + (up ? (up->isEnabled() ? "on" : "off") : "missing") + " back=" + (gb ? (gb->isEnabled() ? "on" : "off") : "missing") +
                  " del=" + (del ? (del->isEnabled() ? "on" : "off") : "missing"));
    }
    put(dir + "/previous/" + sdcard::kFixedImageFileName, 3 * (1 << 20));
    {
        Fixture f(card);
        f.win.refresh_card_set_actions();
        QAction* up = action_starting(f.win, QStringLiteral("Upgrade SD Card"));
        QAction* gb = action_starting(f.win, QStringLiteral("Go Back to the Previous SD Card"));
        QAction* del = action_starting(f.win, QStringLiteral("Delete the Previous SD Card Backup"));
        check("SDMENU-07B", "with a backup present Go Back and Delete are enabled as well",
              up && gb && del && up->isEnabled() && gb->isEnabled() && del->isEnabled());
        check("SDMENU-08", "the delete entry's label carries the backup's size",
              del && del->text() == QStringLiteral("Delete the Previous SD Card Backup (3 MB)..."), del ? del->text().toStdString() : "missing");
    }
    {
        const std::string other = dir + "/other.img";
        put(other, 1 << 16);
        Fixture f(other);
        QAction* up = action_starting(f.win, QStringLiteral("Upgrade SD Card"));
        QAction* gb = action_starting(f.win, QStringLiteral("Go Back to the Previous SD Card"));
        check("SDMENU-07C", "slot 0 holds another image than the default card: Upgrade is disabled, Go Back follows the backup",
              up && gb && !up->isEnabled() && gb->isEnabled());
    }

    // ---- the live flow: the real actions, fired with QAction::trigger(), over a running Emulator ------------------------
    // The confirm box is answered by a timer that clicks its OK button; the SD-change service that QtApp runs on every
    // frame tick is stepped by hand (emulator_service_sd_card_change, then the frontend's sd_card_change_finished, as
    // QtApp does), with the event loop pumped between the steps (the work runs outside the tick, in a singleShot).
    struct Live {
        std::vector<std::string> seq;            // what happened, in order
        bool ejected_at_work = false;            // the card was out of the machine when the work ran
        bool insert_pending_at_work = false;     // the insert had already been requested when the work ran
        bool reset_requested = false;
        bool journal_at_work = false;            // the card was still registered with a writes file when the work ran
        bool reset_same_tick = false;            // requested in the tick that serviced the insert (QtApp polls it right after)
        int  dialogs = 0;                        // boxes nobody answered (the warning of a failure)
        bool mounted_at_end = false;
        bool ran_a_frame = false;
        std::string journal_file;                // the writes file registered for the default card at the end
    };
    // Any box that is still up 40 ms after it appeared is one nobody answers (the confirm is clicked at 0 ms): it is
    // counted and closed, so a defect that raises an unexpected warning fails a row instead of hanging the suite.
    int unexpected_dialogs = 0;
    std::string last_box_text;
    QTimer watchdog;
    watchdog.setInterval(40);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        if (QWidget* m = QApplication::activeModalWidget()) {
            ++unexpected_dialogs;
            if (auto* mb = qobject_cast<QMessageBox*>(m)) last_box_text = mb->text().toStdString();
            m->close();
        }
    });
    watchdog.start();
    auto answer_ok = [] {
        QTimer::singleShot(0, [] {
            if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                if (QAbstractButton* ok = mb->button(QMessageBox::Ok)) ok->click();
        });
    };
    std::function<void()> in_work;   // run while the card is out of the machine (card_op_ set), when a row sets it
    auto live_run = [&](QAction* act, Fixture& f, Live& L) {
        EmulatorConfig frontend_cfg = f.emu.config();
        bool cfg_set = false;
        auto service = [&](const char* what) {
            if (auto sd = emulator_service_sd_card_change(f.emu, frontend_cfg, cfg_set)) {
                L.seq.push_back(std::string(what) + (sd->change.image.empty() ? ":eject" : ":insert"));
                f.win.sd_card_change_finished(QString::fromStdString(sd->change.image), QString::fromStdString(sd->error), sd->change.slot);
                if (!sd->change.image.empty() && sd->error.empty()) L.reset_same_tick = f.emu.take_hard_reset_request();
            }
            QCoreApplication::processEvents();
        };
        f.win.card_op_before_work_for_test = [&] {
            L.seq.push_back("work");
            if (in_work) in_work();
            L.ejected_at_work = f.emu.sd_card().image_path().empty();
            L.journal_at_work = sd_write_journal::for_image(sdcard::default_sdcard_image_path()) != nullptr;
            auto pending = f.emu.take_sd_card_change_request();
            L.insert_pending_at_work = pending.has_value();
            if (pending) f.emu.request_sd_card_change(*pending);
        };
        const int dialogs_before = unexpected_dialogs;
        answer_ok();
        act->trigger();                // confirm, then the eject request
        service("tick1");              // the eject is serviced; the work is queued outside the tick
        QCoreApplication::processEvents();   // the work runs and asks for the insert
        service("tick2");              // the insert is serviced; the reset is queued
        QCoreApplication::processEvents();
        L.reset_requested = L.reset_same_tick || f.emu.take_hard_reset_request();
        L.mounted_at_end = !f.emu.sd_card().image_path().empty();
        f.emu.run_frame();
        L.ran_a_frame = true;
        L.dialogs = unexpected_dialogs - dialogs_before;
        const auto j = sd_write_journal::for_image(sdcard::default_sdcard_image_path());
        L.journal_file = j ? j->file() : std::string();
    };
    auto make_journal = [&](const std::string& card_path, const std::string& file) {
        sd_write_journal::Binding b;
        std::error_code ec;
        fs::create_directories(fs::path(file).parent_path(), ec);
        b.bytes = fs::file_size(card_path, ec);
        b.mtime = static_cast<int64_t>(fs::last_write_time(card_path, ec).time_since_epoch().count());
        std::string why;
        sd_write_journal::bind(card_path, file, b, true, why);
        uint8_t sec[sd_write_journal::kSectorBytes];
        std::memset(sec, 0x5A, sizeof sec);
        sd_write_journal::for_image(card_path)->commit(9, sec);
        sd_write_journal::forget_image(card_path);
    };
    const std::string ovfile = dir + "/session.jsdw";
    auto reset_set = [&] {
        std::error_code ec;
        fs::remove_all(dir, ec);
        put(card, 1 << 16);
        { std::ofstream(card + sdcard::kDistroSuffix) << "sn-emulator-24.11.zip\n"; }
    };
    {
        // go back: the previous card (and its writes file) comes in
        reset_set();
        put(dir + "/previous/" + sdcard::kFixedImageFileName, 1 << 16);
        { std::ofstream(dir + "/previous/" + sdcard::kFixedImageFileName + sdcard::kDistroSuffix) << "sn-emulator-23.01.zip\n"; }
        const std::string prev_card = dir + "/previous/" + sdcard::kFixedImageFileName;
        // the backup's writes file lives in previous/overlays and is recorded for restoring to ovfile
        make_journal(prev_card, dir + "/previous/overlays/0-session.jsdw");
        { std::ofstream(dir + "/previous/overlays/LIST") << "0-session.jsdw\t" << ovfile << "\n"; }
        Fixture f(card);
        Live L;
        live_run(f.win.go_back_sd_action(), f, L);
        const std::vector<std::string> want = {"tick1:eject", "work", "tick2:insert"};
        check("SDMENU-09", "go back, live, a plain session: the card is ejected before the work, the work runs with the card out and no insert yet requested, the card is inserted after it, the hard reset is requested in the tick that serviced the insert, and NO writes file is bound (the session had none, the restored card has a listed one)",
              L.dialogs == 0 && L.seq == want && L.ejected_at_work && !L.insert_pending_at_work && L.reset_same_tick && L.mounted_at_end && L.ran_a_frame &&
                  L.journal_file.empty() && fs::exists(dir + "/previous/" + sdcard::kFixedImageFileName),
              [&] { std::string t; for (const auto& x : L.seq) t += x + " "; return t + "| journal=" + L.journal_file; }());
    }
    {
        // upgrade: the work (a stand-in for provision_sd_card) replaces the card and rebuilds the session's writes file
        reset_set();
        make_journal(card, ovfile);
        { sd_write_journal::Binding b; std::error_code ec; b.bytes = fs::file_size(card, ec);
          b.mtime = static_cast<int64_t>(fs::last_write_time(card, ec).time_since_epoch().count());
          std::string why; sd_write_journal::bind(card, ovfile, b, false, why); }   // the session's registry entry
        Fixture f(card);
        Live L;
        f.win.card_op_upgrade_work_for_test = [&](std::string&) {
            put(card, (1 << 16) + 4096);                       // another card
            make_journal(card, ovfile);                        // its writes file, rebuilt under the same name
            return true;
        };
        live_run(f.win.upgrade_sd_action(), f, L);
        const std::vector<std::string> want = {"tick1:eject", "work", "tick2:insert"};
        check("SDMENU-10", "upgrade, live: the card is ejected before the work, the work runs with the card out and no insert requested yet, the card is inserted after it, the rebuilt writes file is bound to the new card, the machine is hard-reset and keeps running",
              L.dialogs == 0 && L.seq == want && L.ejected_at_work && !L.insert_pending_at_work && L.reset_same_tick && L.mounted_at_end && L.ran_a_frame &&
                  L.journal_file == ovfile && fs::file_size(card) == (1 << 16) + 4096,
              [&] { std::string t; for (const auto& x : L.seq) t += x + " "; return t + "| journal=" + L.journal_file; }());
    }
    {
        // go back, a session WITH its own writes file: it is restored to its path, belongs to the card that came back, and is bound
        reset_set();
        const std::string prev_card = dir + "/previous/" + sdcard::kFixedImageFileName;
        put(prev_card, 1 << 16);
        make_journal(card, ovfile);   // the session's file over the current card ...
        {   // ... bound in the registry, and listed
            sd_write_journal::Binding b; std::error_code ec; b.bytes = fs::file_size(card, ec);
            b.mtime = static_cast<int64_t>(fs::last_write_time(card, ec).time_since_epoch().count());
            std::string why; sd_write_journal::bind(card, ovfile, b, false, why);
            sdcard::upgrade::register_overlay(dir, ovfile);
        }
        make_journal(prev_card, dir + "/previous/overlays/0-session.jsdw");   // the backup's copy, for the card that comes back
        { std::ofstream(dir + "/previous/overlays/LIST") << "0-session.jsdw\t" << ovfile << "\n"; }
        Fixture f(card);
        Live L;
        live_run(f.win.go_back_sd_action(), f, L);
        check("SDMENU-12", "go back, live, a session with its own writes file: the file that belongs to the card that came back is bound to it",
              L.dialogs == 0 && L.reset_same_tick && L.mounted_at_end && L.journal_file == ovfile, L.journal_file);
    }
    {
        // a failing upgrade: the work reports failure -> the warning is shown, the card is back in the machine, no reset
        reset_set();
        Fixture f(card);
        Live L;
        f.win.card_op_upgrade_work_for_test = [&](std::string& err) { err = "no network"; return false; };
        live_run(f.win.upgrade_sd_action(), f, L);
        check("SDMENU-13", "upgrade that fails, live: the warning is shown once, the card is put back in the machine (it was ejected for the work) and nothing is reset",
              L.dialogs == 1 && L.mounted_at_end && !L.reset_requested && L.ejected_at_work && L.seq.size() == 3 && L.seq[2] == "tick2:insert");
    }
    {
        // a session with its own writes file, an upgrade that fails: the file is detached from the card while the work
        // runs (a late commit of the stand-in must not reach a file the work moves) and bound again when the card comes back
        reset_set();
        make_journal(card, ovfile);
        { sd_write_journal::Binding b; std::error_code ec; b.bytes = fs::file_size(card, ec);
          b.mtime = static_cast<int64_t>(fs::last_write_time(card, ec).time_since_epoch().count());
          std::string why; sd_write_journal::bind(card, ovfile, b, false, why); }
        Fixture f(card);
        Live L;
        f.win.card_op_upgrade_work_for_test = [&](std::string& err) { err = "no network"; return false; };
        live_run(f.win.upgrade_sd_action(), f, L);
        check("SDMENU-15", "the session's writes file is detached from the card for the whole of the work and bound again when a failed action puts the card back",
              !L.journal_at_work && L.dialogs == 1 && L.mounted_at_end && L.journal_file == ovfile, L.journal_file);
    }
    {
        // a failing go back (a restore target nothing can be written to): refused with nothing changed, the card is back
        reset_set();
        put(dir + "/previous/" + sdcard::kFixedImageFileName, 1 << 16);
        const std::string blocker = dir + "/blocker";
        { std::ofstream(blocker) << "a file"; }
        { std::ofstream(dir + "/previous/overlays-dummy") << "x"; }
        fs::create_directories(dir + "/previous/overlays");
        { std::ofstream(dir + "/previous/overlays/0-x.jsdw") << "x"; }
        { std::ofstream(dir + "/previous/overlays/LIST") << "0-x.jsdw\t" << blocker << "/x.jsdw\n"; }
        const auto before = fs::file_size(card);
        Fixture f(card);
        Live L;
        live_run(f.win.go_back_sd_action(), f, L);
        check("SDMENU-14", "go back that fails, live: the warning is shown once, the card in use is untouched and back in the machine, nothing is reset",
              L.dialogs == 1 && L.mounted_at_end && !L.reset_requested && fs::exists(dir + "/previous/" + sdcard::kFixedImageFileName) &&
                  fs::file_size(card) == before && !fs::exists(dir + "/go-back-work"));
    }
    {
        // delete: only previous/ goes; the card stays in the machine and nothing resets
        reset_set();
        put(dir + "/previous/" + sdcard::kFixedImageFileName, 1 << 16);
        Fixture f(card);
        f.win.refresh_card_set_actions();
        answer_ok();
        f.win.delete_backup_action()->trigger();
        QCoreApplication::processEvents();
        check("SDMENU-11", "delete, live: previous/ is gone, the card is still in the machine, and nothing was ejected or reset",
              !fs::exists(dir + "/previous") && fs::exists(card) && !f.emu.sd_card().image_path().empty() &&
                  !f.emu.take_sd_card_change_request().has_value() && !f.emu.take_hard_reset_request());
    }

    {
        // GH #558: File > SD Card File Manager..., and its Main toolbar button right after Load
        reset_set();
        Fixture f(card);
        QMenu* file = nullptr;
        for (QMenu* m : f.win.menuBar()->findChildren<QMenu*>())
            if (m->title().remove('&') == QStringLiteral("File")) file = m;
        QAction* sd = f.win.sd_manager_action();
        const QList<QAction*> acts = file ? file->actions() : QList<QAction*>();
        const int at = acts.indexOf(sd);
        const bool after_card_set = at > 0 && acts[at - 1] == f.win.delete_backup_action();
        QAction* load_btn = nullptr;
        QAction* sd_btn = nullptr;
        for (QToolBar* tb : f.win.findChildren<QToolBar*>()) {
            const QList<QAction*> a = tb->actions();
            for (int i = 0; i + 1 < a.size(); ++i)
                if (a[i]->text() == QStringLiteral("Load")) { load_btn = a[i]; sd_btn = a[i + 1]; }
        }
        check("SDMENU-16", "File has 'SD Card File Manager...' right after the card-set entries, with no hotkey and no mnemonic; the Main toolbar has a button right after Load, bound to the sd-manager icon, with the tooltip 'SD Card File Manager'",
              sd && sd->text() == QStringLiteral("SD Card File Manager...") && sd->shortcut().isEmpty() && !sd->text().contains('&') && after_card_set &&
                  load_btn && sd_btn && sd_btn->property("jnext_icon").toString() == QStringLiteral("sd-manager") &&
                  sd_btn->toolTip() == QStringLiteral("SD Card File Manager"),
              sd ? sd->text().toStdString() : "no action");
    }
    {
        // while a card operation has the card out (card_op_ set), the manager refuses with a box and opens nothing
        reset_set();
        Fixture f(card);
        Live L;
        bool window_opened = false, paused = false;
        in_work = [&] {
            f.win.sd_manager_action()->trigger();   // the watchdog counts and closes the box
            for (QWidget* w : QApplication::topLevelWidgets())
                if (qobject_cast<SdManagerDialog*>(w)) window_opened = true;
            paused = f.backend->state().paused;
        };
        f.win.card_op_upgrade_work_for_test = [&](std::string&) { put(card, (1 << 16) + 4096); return true; };
        live_run(f.win.upgrade_sd_action(), f, L);
        in_work = nullptr;
        check("SDMENU-17", "File > SD Card File Manager... during a card operation shows one refusing box, opens no window and does not pause the machine",
              L.dialogs == 1 && !window_opened && !paused && !f.backend->state().paused, "boxes=" + std::to_string(L.dialogs));
        // a window with no machine behind it cannot pause one: refused with a box as well
        MainWindow bare;
        const int before = unexpected_dialogs;
        bare.sd_manager_action()->trigger();
        bool bare_opened = false;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (qobject_cast<SdManagerDialog*>(w)) bare_opened = true;
        check("SDMENU-18", "File > SD Card File Manager... in a window with no machine shows one refusing box and opens nothing",
              unexpected_dialogs - before == 1 && !bare_opened, "boxes=" + std::to_string(unexpected_dialogs - before));
    }

    {
        // an open manager window is handed the main window's card-operation predicate: while an upgrade has the card out
        // it makes no FatFs call over three poll intervals and its note says the card is being changed
        reset_set();
        Fixture f(card);
        f.win.sd_manager_action()->trigger();
        SdManagerDialog* dlg = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* d = qobject_cast<SdManagerDialog*>(w)) dlg = d;
        Live L;
        std::uint64_t attaches = 0;
        bool note_busy = false, off = false;
        in_work = [&] {
            if (!dlg) return;
            const auto at0 = fatfs_glue::attach_count_for_test();
            for (int i = 0; i < 3; ++i) dlg->check_card_change();
            dlg->refresh();
            attaches = fatfs_glue::attach_count_for_test() - at0;
            QLabel* note = dlg->findChild<QLabel*>("card_note");
            note_busy = note && note->text().contains(QStringLiteral("being changed by another action"));
            QPushButton* b = dlg->findChild<QPushButton*>("copy_to_host");
            off = b && !b->isEnabled();
        };
        f.win.card_op_upgrade_work_for_test = [&](std::string&) { put(card, (1 << 16) + 4096); return true; };
        live_run(f.win.upgrade_sd_action(), f, L);
        in_work = nullptr;
        if (dlg) dlg->accept();
        QCoreApplication::processEvents();
        check("SDMENU-20", "with the file manager window open, an upgrade that has the card out leaves it inert: no FatFs attach over three poll intervals and a refresh, the note says the card is being changed, and its copy buttons are disabled",
              dlg && attaches == 0 && note_busy && off && L.dialogs == 0,
              "attaches=" + std::to_string(attaches) + (note_busy ? " note=1" : " note=0") + (off ? " off=1" : " off=0"));
    }

    {
        // GH #558: an image changed from outside JNEXT is warned about, one box per kind, naming the card and the way out
        reset_set();
        Fixture f(card);
        auto warn = [&](int slot, int kind) {
            const int before = unexpected_dialogs;
            last_box_text.clear();
            f.win.card_file_changed(slot, kind, QStringLiteral("/some/dir/card.img"));
            QElapsedTimer t;
            t.start();
            while (unexpected_dialogs == before && t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return unexpected_dialogs - before;
        };
        using OC = SdCardDevice::OutsideChange;
        const int n1 = warn(0, static_cast<int>(OC::Replaced));
        const bool t1 = last_box_text.find("replaced by another file") != std::string::npos && last_box_text.find("The SD card image") != std::string::npos &&
                        last_box_text.find("Insert SD Card Image") != std::string::npos;
        const int n2 = warn(1, static_cast<int>(OC::Resized));
        const bool t2 = last_box_text.find("was resized") != std::string::npos && last_box_text.find("The second SD card image") != std::string::npos;
        const int n3 = warn(0, static_cast<int>(OC::Rewritten));
        const bool t3 = last_box_text.find("SD-writes file") != std::string::npos && last_box_text.find("may be damaged") != std::string::npos;
        const int n0 = warn(0, static_cast<int>(OC::None));   // nothing to say: no box (the call returns after the timeout)
        check("SDMENU-19", "an image replaced, resized or rewritten under a writes file from outside JNEXT raises exactly one warning box per report, naming the card and pointing to the file manager; no report, no box",
              n1 == 1 && t1 && n2 == 1 && t2 && n3 == 1 && t3 && n0 == 0,
              std::to_string(n1) + std::to_string(n2) + std::to_string(n3) + std::to_string(n0));
    }

    {
        // GH #558: the wording follows what the machine's writes do (a writes file takes them), and a deleted image is told too
        reset_set();
        Fixture f(card);
        auto text = [&](int kind, bool wf, int slot = 0) {
            const int before = unexpected_dialogs;
            last_box_text.clear();
            f.win.card_file_changed(slot, kind, QStringLiteral("/some/dir/card.img"), wf);
            QElapsedTimer t;
            t.start();
            while (unexpected_dialogs == before && t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            return last_box_text;
        };
        using OC = SdCardDevice::OutsideChange;
        auto has = [](const std::string& s, const char* w) { return s.find(w) != std::string::npos; };
        const std::string rp = text(static_cast<int>(OC::Replaced), false), rw = text(static_cast<int>(OC::Replaced), true);
        const std::string dp = text(static_cast<int>(OC::Deleted), false), dw = text(static_cast<int>(OC::Deleted), true);
        const std::string r2 = text(static_cast<int>(OC::Replaced), false, 1);   // the second card: its own menu item
        const bool second = has(r2, "Insert Second SD Card Image") && has(rp, "File > Insert SD Card Image") && !has(rp, "Second");
        check("SDMENU-21", "the warning for a replaced or deleted image says the machine's writes are lost on exit when there is no writes file, and that they go into the writes file (which no longer fits, or cannot be used without the image) when there is one",
              has(rp, "replaced by another file") && has(rp, "lost when JNEXT exits") && !has(rp, "SD-writes file") &&
                  has(rw, "replaced by another file") && has(rw, "no longer fits the new image") && !has(rw, "lost when JNEXT exits") &&
                  has(dp, "was deleted") && has(dp, "lost when JNEXT exits") && !has(dp, "SD-writes file") &&
                  has(dw, "was deleted") && has(dw, "cannot be used again without the image") && !has(dw, "lost when JNEXT exits") && !has(dw, "new image") &&
                  !has(dp, "File Manager") && !has(dw, "File Manager") && !has(rp, "File Manager") && has(rp, "Insert SD Card Image") && has(rw, "Insert SD Card Image") && second,
              rw + " | " + dw);
    }
    {
        // GH #558: the status tick's poll turns what Emulator::check_card_files() finds, in either slot, into one box each
        reset_set();
        const std::string card2 = dir + "/card2.img";
        put(card2, 1 << 16);
        make_journal(card2, dir + "/w2.jsdw");
        { sd_write_journal::Binding b; std::error_code bec; b.bytes = fs::file_size(card2, bec);
          b.mtime = static_cast<int64_t>(fs::last_write_time(card2, bec).time_since_epoch().count());
          std::string why; sd_write_journal::bind(card2, dir + "/w2.jsdw", b, false, why); }   // the second card has a writes file
        Fixture f(card);
        const bool m2 = f.emu.sd_card2().mount(card2, false);
        const auto none = f.emu.check_card_files();
        std::error_code ec;
        fs::resize_file(card, (1 << 16) + 512, ec);   // slot 1 (index 0): grown
        fs::last_write_time(card2, fs::last_write_time(card2, ec) + std::chrono::seconds(30), ec);   // slot 2 (index 1): rewritten, it has a writes file
        const auto evs = f.emu.check_card_files();
        const bool ev_ok = evs.size() == 2 && evs[0].slot == 0 && evs[0].kind == SdCardDevice::OutsideChange::Resized && evs[0].image == card &&
                           evs[1].slot == 1 && evs[1].kind == SdCardDevice::OutsideChange::Rewritten && evs[1].image == card2 && !evs[0].writes_file && evs[1].writes_file;
        const bool again = f.emu.check_card_files().empty();   // each kind once
        // the window's poll shows a box per report: re-arm with fresh reports (a new mount latches nothing)
        f.emu.sd_card().unmount();
        f.emu.sd_card().mount(card, false);
        fs::resize_file(card, (1 << 16) + 1024, ec);
        const int before = unexpected_dialogs;
        f.win.poll_card_files();
        QElapsedTimer t;
        t.start();
        while (unexpected_dialogs == before && t.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        check("SDMENU-22", "Emulator::check_card_files() reports a change of either slot's image once, with the slot, the kind and the image, and the main window's poll turns a report into a warning box",
              m2 && none.empty() && ev_ok && again && unexpected_dialogs - before == 1,
              std::to_string(evs.size()) + " boxes=" + std::to_string(unexpected_dialogs - before));
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
