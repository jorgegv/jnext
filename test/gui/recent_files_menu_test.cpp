// File > Recent Files wiring test (GitHub issue #406).
//
// No VHDL oracle: host UI behaviour. The oracle is the issue's request -- a
// "Recent Files" menu (SimCoupe's) so a rebuilt program is one click away and
// the Load dialog opens on the file last loaded -- and the plan's rules: a
// SUCCESSFUL load the user asked for, or a startup --load, is recorded (a
// failed one and Quick Load are not), most recent first, ten at most, persisted in jnext.conf, a
// vanished file is dropped with a message, Preferences leaves the list alone.
//
// Drives a real offscreen MainWindow: handle_load_path() + load_finished() (the
// frontend's report) and handle_tape_path(), and triggers the real menu
// actions. The load callback stands in for the cold boot.
//
// The Load Program and Open Tape pickers are opened for real (RFM-31/32) and
// shown to select the seeded file. NOT covered: the native macOS open panel (it
// is Cocoa, not Qt's own dialog), which Linux CI cannot open.
//
// Run: ./build/test/recent_files_menu_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "gui/app_config.h"
#include "gui/main_window.h"
#include "gui/preferences_dialog.h"

#include <QAbstractButton>
#include <QAction>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QApplication>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QString>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

namespace fs = std::filesystem;
fs::path g_root;
int      g_cfg_n = 0;

std::string touch(const std::string& name, const std::string& body = "x")
{
    const fs::path p = g_root / name;
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << body;
    return p.string();
}

// A well-formed one-block TAP, so Tape > Open accepts it.
std::string good_tap(const std::string& name)
{
    const unsigned char b[] = {19, 0, 0, 3, 'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
                               5, 0, 0, 0x80, 0, 0x80, 0};
    unsigned char x = 0;
    for (int i = 2; i < 20; ++i) x ^= b[i];
    std::string s(reinterpret_cast<const char*>(b), sizeof b);
    s[20] = static_cast<char>(x);
    return touch(name, s);
}

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    std::unique_ptr<MainWindow> win;
    std::string handed;
    int         handed_n = 0;
    bool        ok = false;
    std::string cfgdir;

    // `keep_cfg`: reuse the previous fixture's config dir (a restart).
    explicit Fixture(bool keep_cfg = false)
    {
        if (!keep_cfg) {
            cfgdir = (g_root / ("cfg" + std::to_string(++g_cfg_n))).string();
            fs::create_directories(cfgdir);
            jtp::set_env("JNEXT_CONFIG_DIR", cfgdir.c_str());
        }
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win = std::make_unique<MainWindow>();
        win->set_debugger(backend.get());
        win->set_emulator(&emu);
        win->set_load_file_callback([this](const std::string& p, bool) { handed = p; ++handed_n; });
        QApplication::processEvents();
        ok = true;
    }
    // A menu load that worked / failed, as the frontend reports it.
    void load(const std::string& path, bool good = true)
    {
        win->handle_load_path(QString::fromStdString(path));
        win->load_finished(path, good);
    }
    QStringList entries() const
    {
        QStringList out;
        for (QAction* a : win->recent_menu()->actions())
            if (!a->isSeparator()) out << a->text();
        return out;
    }
    QStringList list() const { return win->app_config().data().recent_files; }
};

// Dismisses the "Load Failed" box a refused tape raises.
struct DialogWatcher {
    int    seen = 0;
    QTimer timer;
    DialogWatcher() {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            if (auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                ++seen;
                mb->done(QMessageBox::Ok);
            }
        });
        timer.start(1);
    }
};

// Opens the real picker behind the menu action `text` and reports what it was
// seeded with (the file it has selected, and its directory); then rejects it.
struct Picked { int seen = 0; std::string selected, dir; };
Picked open_picker(MainWindow& w, const char* text)
{
    Picked p;
    QAction* act = nullptr;
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == QString::fromUtf8(text)) act = a;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* fd = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!fd || !fd->isVisible()) return;
        ++p.seen;
        p.selected = fd->selectedFiles().value(0).toStdString();
        p.dir = fd->directory().absolutePath().toStdString();
        static_cast<QDialog*>(fd)->reject();
    });
    timer.start(1);
    if (act) act->trigger();
    timer.stop();
    QApplication::processEvents();
    return p;
}

std::string fmt_n(int n) { return std::to_string(n); }
QString S(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QStringList& l) { return l.join("|").toStdString(); }

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::printf("File > Recent Files wiring (GH #406)\n\n");

    g_root = fs::temp_directory_path() / ("jnext-recent-menu-" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root);

    const std::string a = good_tap("a.tap"), b = good_tap("b.tap");

    {
        Fixture f;
        check("RFM-01", "with nothing loaded the submenu is one disabled (empty) item and a disabled Clear",
              f.ok && f.win->recent_menu() && f.win->recent_menu()->title() == "Recent &Files" &&
                  f.entries().size() == 2 && f.entries()[0] == "(empty)" &&
                  !f.win->recent_menu()->actions()[0]->isEnabled() &&
                  !f.win->recent_menu()->actions().back()->isEnabled(),
              f.ok ? str(f.entries()) : "no fixture");
    }
    {
        Fixture f;
        f.load(a);
        const QList<QAction*> acts = f.win->recent_menu()->actions();
        check("RFM-02", "a successful menu load adds a numbered entry whose status tip is the full path",
              f.ok && f.entries().size() == 2 && f.entries()[0] == "&1 a.tap" &&
                  acts[0]->statusTip() == S(a) && f.list() == QStringList{S(a)},
              str(f.entries()));
        check("RFM-03", "the Clear item is enabled once there is an entry",
              acts.back()->isEnabled() && acts.back()->text() == "&Clear Recent Files");
    }
    {
        Fixture f;
        f.load(a, false);
        check("RFM-04", "a failed load is not recorded", f.ok && f.list().isEmpty(), str(f.list()));
    }
    {
        Fixture f;
        f.win->load_finished(a, true);   // --load: no handle_load_path() asked for it
        const bool plain = f.list().isEmpty();
        f.win->handle_load_path(S(a));   // a menu load is pending...
        f.win->load_finished(b, true);   // ...and a different file (--load) reports first
        check("RFM-05", "a load nobody registered with the window is not recorded, nor a pending menu load's neighbour",
              f.ok && plain && f.list().isEmpty(), str(f.list()));
    }
    {
        Fixture f;
        f.win->handle_load_path(S(a), true);   // Quick Load's call
        f.win->load_finished(a, true);
        check("RFM-06", "Quick Load (jns already probed) is not recorded", f.ok && f.list().isEmpty(),
              str(f.list()));
    }
    {
        Fixture f;
        f.load(a); f.load(b); f.load(a);
        check("RFM-07", "most recent first, a reloaded file moves to the top without a duplicate",
              f.ok && f.list() == QStringList({S(a), S(b)}) &&
                  f.entries()[0] == "&1 a.tap" && f.entries()[1] == "&2 b.tap", str(f.list()));
    }
    {
        Fixture f;
        for (int i = 0; i < 12; ++i) f.load(touch("n" + std::to_string(i) + ".tap"));
        const QStringList e = f.entries();   // 10 entries + separator skipped + Clear
        check("RFM-08", "ten entries at most, numbered 1..9 then 0, newest first",
              f.ok && f.list().size() == 10 && e.size() == 11 && e[0] == "&1 n11.tap" &&
                  e[8] == "&9 n3.tap" && e[9] == "1&0 n2.tap", str(e));
    }
    {
        Fixture f;
        const std::string amp = touch("x&y.tap");
        f.load(amp);
        check("RFM-09", "an '&' in a file name is a literal '&' in the entry, not a mnemonic",
              f.ok && f.entries()[0] == "&1 x&&y.tap", str(f.entries()));
    }
    {
        Fixture f;
        f.load(a);
        f.win->recent_menu()->actions().back()->trigger();
        check("RFM-10", "Clear Recent Files empties the list and the menu",
              f.ok && f.list().isEmpty() && f.entries()[0] == "(empty)" &&
                  !f.win->recent_menu()->actions().back()->isEnabled(), str(f.list()));
    }
    {
        Fixture f;
        f.load(a); f.load(b);
        f.handed_n = 0;
        f.win->recent_menu()->actions()[1]->trigger();   // "&2 a.tap"
        check("RFM-11", "choosing an entry loads that file through the Load route",
              f.ok && f.handed_n == 1 && f.handed == a, f.handed);
        f.win->load_finished(a, true);
        check("RFM-11b", "and the reloaded file moves to the top",
              f.list() == QStringList({S(a), S(b)}), str(f.list()));
    }
    {
        Fixture f;
        f.win->app_config().data().last_load_dir = S(g_root.string());
        f.load(a);
        check("RFM-12", "load_dialog_seed() is the file just loaded while it is still in the last load directory",
              f.ok && f.win->load_dialog_seed() == S(a), f.ok ? f.win->load_dialog_seed().toStdString() : "no fixture");
    }
    {
        Fixture f;
        const std::string gone = touch("gone.tap");
        f.load(gone); f.load(a);
        fs::remove(gone);
        f.handed_n = 0;
        f.win->recent_menu()->actions()[1]->trigger();   // "&2 gone.tap"
        check("RFM-13", "choosing a vanished file loads nothing, drops it, and says so in the status bar",
              f.ok && f.handed_n == 0 && f.list() == QStringList{S(a)} &&
                  f.win->statusBar()->currentMessage().contains("gone.tap"),
              str(f.list()) + " / " + f.win->statusBar()->currentMessage().toStdString());
    }
    {
        Fixture f;
        const std::string comma = touch("with,comma.tap");
        f.load(a); f.load(comma);
        Fixture g(true);   // same config dir: a restart
        check("RFM-14", "the list persists in jnext.conf and a new window shows it (comma in a name included)",
              g.ok && g.list() == QStringList({S(comma), S(a)}) && g.entries()[0] == "&1 with,comma.tap",
              str(g.list()));
    }
    {
        Fixture f;
        f.win->handle_tape_path(S(good_tap("t.tap")));
        const std::string bad = touch("bad.tap", "nope");
        DialogWatcher w;
        f.win->handle_tape_path(S(bad));   // refused: the "Load Failed" box, dismissed by w
        check("RFM-15", "Tape > Open Tape File records a tape that attached, not one that was refused",
              f.ok && f.list().size() == 1 && f.list()[0].endsWith("t.tap") && w.seen == 1, str(f.list()));
    }
    {
        Fixture f;
        f.load((g_root / "sub" / ".." / "a.tap").string());
        f.load(a);
        check("RFM-16", "two spellings of one path are one entry, stored absolute and normalised",
              f.ok && f.list() == QStringList{S(a)}, str(f.list()));
    }
    {
        Fixture f;
        const std::string zip = std::string(JNEXT_ZIP_FIXTURE_DIR) + "/beeper.tap.zip";
        f.win->handle_load_path(S(zip));
        f.win->load_finished(f.handed, true);   // the extracted member, as the frontend reports it
        check("RFM-17", "a program loaded from inside a .zip is recorded as the archive, not its scratch copy",
              f.ok && f.list() == QStringList{S(fs::path(zip).lexically_normal().string())} &&
                  f.handed != zip, str(f.list()) + " / " + f.handed);
    }
    {
        Fixture f;
        f.win->app_config().data().last_load_dir = S(g_root.string());
        const std::string tmp = touch("vanish.tap");
        f.load(tmp);
        fs::remove(tmp);
        const QString gone_seed = f.ok ? f.win->load_dialog_seed() : QString();
        f.load(a);
        f.win->app_config().data().last_load_dir = S((g_root / "elsewhere").string());
        check("RFM-18", "the dialog seed falls back to the last load directory when the file is gone "
                        "or the user has since browsed elsewhere",
              f.ok && gone_seed == S(g_root.string()) &&
                  f.win->load_dialog_seed() == S((g_root / "elsewhere").string()),
              gone_seed.toStdString() + " / " + f.win->load_dialog_seed().toStdString());
    }
    {
        Fixture f;
        QAction* r = f.ok ? f.win->reload_last_action() : nullptr;
        const bool empty_off = r && !r->isEnabled();
        f.load(a);
        check("RFM-19", "File > Reload Last File carries Alt+L, disabled with an empty list, enabled after a load",
              r && r->text() == "Reload Last File" && r->shortcut() == QKeySequence("Alt+L") &&
                  empty_off && r->isEnabled(),
              r ? r->text().toStdString() : "no action");
    }
    {
        Fixture f;
        f.load(a); f.load(b);
        f.handed_n = 0; f.handed.clear();
        f.win->reload_last_action()->trigger();
        check("RFM-20", "Reload Last File loads the newest recent file, the one entry 1 would",
              f.ok && f.handed_n == 1 && f.handed == b, f.handed);
        f.win->load_finished(b, true);
        check("RFM-20b", "and the list order is unchanged by it", f.list() == QStringList({S(b), S(a)}),
              str(f.list()));
    }
    {
        Fixture f;
        const std::string gone = touch("gone2.tap");
        f.load(a); f.load(gone);
        fs::remove(gone);
        f.handed_n = 0;
        f.win->reload_last_action()->trigger();
        check("RFM-21", "Reload Last File on a vanished newest file loads nothing, drops it and says so",
              f.ok && f.handed_n == 0 && f.list() == QStringList{S(a)} &&
                  f.win->statusBar()->currentMessage().contains("gone2.tap") &&
                  f.win->reload_last_action()->isEnabled(), str(f.list()));
    }
    {
        Fixture f;
        f.win->show();
        QApplication::processEvents();
        f.load(a); f.load(b);
        f.handed_n = 0; f.handed.clear();
        QKeyEvent e(QEvent::KeyPress, Qt::Key_L, Qt::AltModifier);
        QApplication::sendEvent(f.win.get(), &e);
        QApplication::processEvents();
        check("RFM-22", "the Alt+L key chord itself reloads the newest recent file",
              f.ok && f.handed_n == 1 && f.handed == b, fmt_n(f.handed_n) + " " + f.handed);
    }

    {
        Fixture f;
        f.win->set_startup_load(a, a);
        f.win->load_finished(a, true);
        check("RFM-23", "a startup --load that worked is recorded",
              f.ok && f.list() == QStringList{S(a)}, str(f.list()));
    }
    {
        Fixture f;
        f.win->set_startup_load(a, a);
        f.win->load_finished(a, false);
        f.win->load_finished(b, true);   // some other load reporting: not the --load file
        check("RFM-24", "a startup --load that failed is not recorded, nor is another file's report",
              f.ok && f.list().isEmpty(), str(f.list()));
    }
    {
        Fixture f;
        const std::string zip = std::string(JNEXT_ZIP_FIXTURE_DIR) + "/beeper.tap.zip";
        f.win->set_startup_load(b, zip);   // main.cpp loads the extracted copy, names the archive
        f.win->load_finished(b, true);
        check("RFM-25", "a startup --load of a .zip records the archive, not the extracted copy",
              f.ok && f.list() == QStringList{S(fs::path(zip).lexically_normal().string())},
              str(f.list()));
    }

    {
        Fixture f;
        f.load(a);
        f.win->recent_menu()->actions().back()->trigger();   // Clear
        Fixture g(true);
        check("RFM-26", "Clear Recent Files is saved: a restart finds the list empty",
              f.ok && g.ok && g.list().isEmpty(), str(g.list()));
    }
    {
        Fixture f;
        const std::string gone = touch("gone3.tap");
        f.load(a); f.load(gone);
        fs::remove(gone);
        f.win->recent_menu()->actions()[0]->trigger();   // the vanished one
        Fixture g(true);
        check("RFM-27", "dropping a vanished file is saved: a restart does not bring it back",
              f.ok && g.ok && g.list() == QStringList{S(a)}, str(g.list()));
    }
    {
        Fixture f;
        f.load(a);
        f.win->app_config().data().last_load_dir = S((g_root / "elsewhere").string());
        f.win->recent_menu()->actions()[0]->trigger();
        Fixture g(true);
        check("RFM-28", "choosing an entry makes its folder the last load directory, saved like Load Program does",
              f.ok && g.ok && f.win->app_config().data().last_load_dir == S(g_root.string()) &&
                  g.win->app_config().data().last_load_dir == S(g_root.string()),
              f.win->app_config().data().last_load_dir.toStdString());
    }
    {
        Fixture f;
        f.win->set_unattended(true);
        f.win->handle_load_path(S(a));                               // handed, pending
        f.win->handle_load_path(S(touch("junk.xyz")));               // refused before the cold boot
        f.win->load_finished(a, true);
        check("RFM-29", "a refused request made while a load is pending does not take its place in the list",
              f.ok && f.list() == QStringList{S(a)}, str(f.list()));
    }
    {
        Fixture f;
        f.win->handle_load_path(S(a));                  // handed, pending
        f.win->handle_load_path(S(b), true);            // Quick Load's call, handed on: supersedes it
        f.win->load_finished(b, true);
        check("RFM-30", "a Quick Load handed on while a menu load is pending records neither file",
              f.ok && f.list().isEmpty(), str(f.list()));
    }
    {
        Fixture f;
        f.win->app_config().data().last_load_dir = S(g_root.string());
        f.load(a);
        const Picked p = open_picker(*f.win, "&Load NEX File...");
        check("RFM-31", "the real Load Program picker opens with the file last loaded selected",
              f.ok && p.seen > 0 && p.selected == a && p.dir == g_root.string(),
              p.selected + " | " + p.dir);
    }
    {
        Fixture f;
        f.win->app_config().data().last_load_dir = S(g_root.string());
        f.load(a);
        const Picked p = open_picker(*f.win, "&Open Tape File...");
        check("RFM-32", "the real Open Tape picker opens with the file last loaded selected",
              f.ok && p.seen > 0 && p.selected == a && p.dir == g_root.string(),
              p.selected + " | " + p.dir);
    }
    {
        Fixture f;
        f.load(a);
        QAction* first = f.win->recent_menu()->actions()[0];
        check("RFM-33", "the submenu shows tooltips and an entry's tooltip is its full path",
              f.ok && f.win->recent_menu()->toolTipsVisible() && first->toolTip() == S(a),
              first->toolTip().toStdString());
    }
    {
        Fixture f;
        f.load(a);
        QAction* prefs = nullptr;
        for (QAction* x : f.win->findChildren<QAction*>())
            if (x->text() == "&Preferences...") prefs = x;
        f.win->handle_load_path(S(b));   // pending while the dialog is open
        int polls = 0;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&]() {
            auto* d = qobject_cast<PreferencesDialog*>(QApplication::activeModalWidget());
            if (!d || !d->isVisible() || polls++) return;
            f.win->load_finished(b, true);   // the load finishes under the open dialog
            if (auto* bb = d->findChild<QDialogButtonBox*>())
                if (auto* ap = bb->button(QDialogButtonBox::Apply)) ap->click();
            static_cast<QDialog*>(d)->reject();
        });
        timer.start(1);
        if (prefs) prefs->trigger();
        timer.stop();
        Fixture g(true);
        check("RFM-34", "Preferences Apply keeps a file recorded while the dialog was open",
              f.ok && prefs && polls > 0 && f.list() == QStringList({S(b), S(a)}) &&
                  g.list() == QStringList({S(b), S(a)}), str(f.list()));
    }
    {
        Fixture f;
        f.win->set_startup_load(a, a);
        f.win->startup_rzx_started(a);
        Fixture g;   // another file started the same way is not the --load file
        g.win->set_startup_load(a, a);
        g.win->startup_rzx_started(b);
        check("RFM-35", "a startup --load of a recording is recorded once it plays, and only that file",
              f.ok && g.ok && f.list() == QStringList{S(a)} && g.list().isEmpty(), str(f.list()));
    }

    fs::remove_all(g_root, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
