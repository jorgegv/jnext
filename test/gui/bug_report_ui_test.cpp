// ===========================================================================
// File > Save Bug Report... in the Qt window (GH #492 stage 2).
//
// NO VHDL ORACLE: a host GUI action. The oracles are the menu as the user sees it (action text and
// position), the file the real action writes through the real file dialog, and the settings file on
// disk. The writer itself is bug_report_test's subject.
//
//   BRUI-01  the File menu has "Save Bug Report..." directly after "Quick Load Snapshot"
//   BRUI-02  the real action opens a save dialog proposing the writer's file name; the path chosen
//            gets a report whose producer.frontend is qt
//   BRUI-03  the proposed name is the writer's name for the loaded program
//   BRUI-04  the report carries the settings file the Qt frontend read: a path blanked, a machine
//            type kept (config_file_read true)
//   BRUI-05  a path that cannot be written is refused: false, and the status bar says so
//   BRUI-06  a hand-edited [debugger_keys] entry the window REALLY logs as refused (name, value and a
//            reason that quotes the value) reaches no member of the report
//
// Qt is required (MainWindow is a real QWidget), no display is: main() forces the offscreen platform.
// Run: ./build/test/bug_report_ui_test
// ===========================================================================

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "gui/main_window.h"
#include "platform/bug_report.h"
#include "save/zip_archive.h"
#include "third_party/nlohmann-json/nlohmann/json.hpp"
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool ok, const std::string& detail = {})
{
    report_row_id(id);
    ++g_total;
    if (ok) { ++g_pass; std::printf("[PASS] %-10s %s\n", id, desc); }
    else    { ++g_fail; std::printf("[FAIL] %-10s %s%s%s\n", id, desc, detail.empty() ? "" : " -- ", detail.c_str()); }
}

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;   // outlives the window
    MainWindow win;

    explicit Fixture(const std::string& program)
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.no_sdcard = true;
        cfg.load_file = "program.nex";
        cfg.silent = true;
        cfg.rewind_buffer_frames = 0;
        cfg.command_line = { "jnext", "--no-sdcard" };
        emu.init(cfg);
        emu.inject_binary(program, 0x8000, 0x8000);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        win.set_unattended(true);
    }
};

// The File menu's actions, in order.
QList<QAction*> file_actions(MainWindow& win)
{
    for (QAction* top : win.menuBar()->actions())
        if (top->menu() && top->text().remove('&') == QStringLiteral("File")) return top->menu()->actions();
    return {};
}

// Answer each modal that opens while `open` runs.
int drive_modals(const std::function<void(QWidget*)>& act, const std::function<void()>& open)
{
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

nlohmann::json member_json(const std::string& zip_path, const std::string& name)
{
    QFile f(QString::fromStdString(zip_path));
    if (!f.open(QIODevice::ReadOnly)) return nlohmann::json();
    const QByteArray b = f.readAll();
    jnext::zip::Reader r;
    std::string why, text;
    if (!r.open(reinterpret_cast<const uint8_t*>(b.constData()), static_cast<size_t>(b.size()), why)) return nlohmann::json();
    if (!r.read_text(name, text, why)) return nlohmann::json();
    return nlohmann::json::parse(text, nullptr, false);
}

// True when `needle` is in any member of the zip at `zip_path` (as text, decompressed).
bool zip_contains(const std::string& zip_path, const std::string& needle)
{
    QFile f(QString::fromStdString(zip_path));
    if (!f.open(QIODevice::ReadOnly)) return true;   // unreadable: fail the row
    const QByteArray b = f.readAll();
    jnext::zip::Reader r;
    std::string why;
    if (!r.open(reinterpret_cast<const uint8_t*>(b.constData()), static_cast<size_t>(b.size()), why)) return true;
    for (const auto& e : r.entries()) {
        std::vector<uint8_t> data;
        if (!r.read(e.name, data, why)) return true;
        if (std::search(data.begin(), data.end(), needle.begin(), needle.end()) != data.end()) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // MainWindow reads jnext.conf: keep it off the user's real ~/.jnext, and give the rows one to read.
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());
    {
        QFile conf(dir.path() + QStringLiteral("/jnext.conf"));
        if (!conf.open(QIODevice::WriteOnly)) return 1;
        conf.write("[startup]\nmachine_type=next\n[paths]\nsd_card_path=/mnt/secret/card.img\n");
    }
    QApplication app(argc, argv);

    const std::string prog = (dir.path() + QStringLiteral("/gamefile.bin")).toStdString();
    {
        QFile p(QString::fromStdString(prog));
        if (!p.open(QIODevice::WriteOnly)) return 1;
        p.write(QByteArray(64, '\x42'));
    }

    std::printf("=== File > Save Bug Report... (GH #492 stage 2) ===\n\n");

    {
        Fixture f{prog};
        const QList<QAction*> acts = file_actions(f.win);
        int quick_load = -1, bug = -1;
        for (int i = 0; i < acts.size(); ++i) {
            if (acts[i]->text() == QStringLiteral("Quick Load Snapshot")) quick_load = i;
            if (acts[i]->text() == QStringLiteral("Save Bug Report...")) bug = i;
        }
        check("BRUI-01", "File menu: \"Save Bug Report...\" directly after \"Quick Load Snapshot\", with no shortcut",
              quick_load >= 0 && bug == quick_load + 1 && acts[bug]->shortcut().isEmpty(),
              "quick_load=" + std::to_string(quick_load) + " bug=" + std::to_string(bug));
    }

    {
        Fixture f{prog};
        QAction* action = nullptr;
        for (QAction* a : file_actions(f.win)) if (a->text() == QStringLiteral("Save Bug Report...")) action = a;
        const std::string target = (dir.path() + QStringLiteral("/chosen.jnext-bug.zip")).toStdString();
        QString proposed, dialog_dir;
        const int polls = drive_modals(
            [&](QWidget* w) {
                if (auto* fd = qobject_cast<QFileDialog*>(w)) {
                    if (auto* edit = fd->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) proposed = edit->text();
                    dialog_dir = fd->directory().absolutePath();
                    fd->selectFile(QString::fromStdString(target));
                    static_cast<QDialog*>(fd)->accept();   // QFileDialog::accept() is protected
                }
            },
            [&]() { if (action) action->trigger(); });
        const nlohmann::json man = member_json(target, "manifest.json");
        const QString shown = f.win.statusBar()->currentMessage();
        check("BRUI-02", "the real action opens a save dialog; the path chosen gets a report made by the Qt frontend, and the status bar says so",
              action && polls > 0 && man.is_object() && man.value("kind", "") == "jnext-bug-report" &&
              man["producer"]["frontend"] == "qt" &&
              shown == QStringLiteral("Bug report saved: chosen.jnext-bug.zip (memory: included)"),
              shown.toStdString() + " | "
              "polls=" + std::to_string(polls) + " manifest=" + (man.is_object() ? "ok" : "missing"));
        const std::string expect_prefix = "gamefile.";
        const std::string p = proposed.toStdString();
        check("BRUI-03", "the dialog opens in the home folder and proposes the writer's name for the loaded program (PROGRAM.YYYYMMDDhhmm.jnext-bug.zip)",
              dialog_dir == QDir::home().absolutePath() && p.rfind(expect_prefix, 0) == 0 && p.size() == expect_prefix.size() + 12 + std::string(".jnext-bug.zip").size() &&
              p.compare(p.size() - 14, 14, ".jnext-bug.zip") == 0 &&
              f.win.proposed_bug_report_name().toStdString().rfind(expect_prefix, 0) == 0,
              "proposed='" + p + "'");

        const nlohmann::json st = member_json(target, "settings.json");
        check("BRUI-04", "the report carries the settings file the window read: the machine type kept, the SD path blanked",
              st.is_object() && st["config_file_read"] == true && st["config_file"].is_object() &&
              st["config_file"]["startup.machine_type"] == "next" &&
              st["config_file"]["paths.sd_card_path"] == "<blanked>",
              st.is_object() ? st.dump().substr(0, 200) : "no settings.json");
    }

    {
        Fixture f{prog};
        const bool ok = f.win.save_bug_report_to(dir.path() + QStringLiteral("/no-such-dir/x/r.zip"));
        check("BRUI-05", "a path that cannot be written is refused, and the status bar says so",
              !ok && f.win.statusBar()->currentMessage().startsWith(QStringLiteral("Bug report failed")),
              f.win.statusBar()->currentMessage().toStdString());
    }

    {
        // A second settings directory whose [debugger_keys] holds entries the config layer refuses: a
        // known action bound to a name no key has, one with a colon in the value, an unknown action.
        QTemporaryDir dir2;
        QFile conf2(dir2.path() + QStringLiteral("/jnext.conf"));
        const bool wrote = dir2.isValid() && conf2.open(QIODevice::WriteOnly);
        if (wrote) {
            conf2.write("[debugger_keys]\nstep_into=zq7secret-notes\npause=\"a: zq7secret-password\"\n"
                        "zq7secret_action=Ctrl+X\n");
            conf2.close();
        }
        qputenv("JNEXT_CONFIG_DIR", dir2.path().toUtf8());
        Fixture f{prog};   // the window reads that file and logs every refusal
        const std::string out = (dir.path() + QStringLiteral("/refused.jnext-bug.zip")).toStdString();
        const bool saved = wrote && f.win.save_bug_report_to(QString::fromStdString(out));
        check("BRUI-06", "refused [debugger_keys] entries, logged by the real window, reach no member of the report",
              saved && !zip_contains(out, "zq7secret"), saved ? "a member holds the sentinel" : "no report");
        qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());
    }

    std::printf("\n============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
