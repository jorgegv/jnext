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
#include <QMenuBar>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/sdcard_provisioner.h"
#include "debug/debugger.h"
#include "gui/main_window.h"
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
        std::printf("[PASS] %-12s %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %-12s %s%s%s\n", id, desc, detail.empty() ? "" : " -- ", detail.c_str());
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
    if (!cfg_dir.isValid()) { std::printf("[FAIL] no scratch directory\nTotal:    0  Passed:    0  Failed:    1  Skipped:    0\n"); return 1; }
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

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
