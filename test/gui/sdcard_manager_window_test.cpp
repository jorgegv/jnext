// The SD card file manager window (GH #558 S3): src/gui/sdcard_manager_dialog.*, src/gui/sdcard_card_model.*.
//
// ORACLES. The issue and its owner decisions, nothing read back from the code:
//   - two panes (the card left, a host folder right), each navigated and viewed (icons or list) on its own;
//   - the card selector offers the two slots only; a card jnext cannot write, a write-protected card and a state
//     that forbids a card change are SHOWN read-only (forced Read-only box, greyed, with the reason);
//   - copy both ways, rename, delete (the card permanently, the host to the trash), New Folder in both panes, drag
//     and drop with Shift = move; an overwrite asks; the warning before the FIRST card change must be accepted,
//     and a declined one changes nothing;
//   - the machine is paused while the window is open; a card change ends in a hard reset, a browse does not;
//   - what the window opens on follows the owner's amendment: the emulator wins (a remembered card that is not
//     mounted gives the mounted one; another card or a vanished folder gives the ROOT; a card change while open
//     resets the view to the new card's root); the host pane falls back to the nearest existing parent.
// What the user SEES is asserted: the model's rows and display names, the widgets' enabled state, what is on the card
// (read back with extract_from_image, an independent reader) and on the host, the number of message boxes shown.
// Fixtures are built at run time (FatFs f_mkfs); never a NextZXOS image, in any form. Qt is required, a display is not.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
// Qt's `slots` keyword would swallow the FAT helper's local named slots: this test uses none of Qt's keywords. The
// main window's header needs them while it is read, so they are undefined after the last include instead of
// disabled (QT_NO_KEYWORDS would break that header).
#include <QAbstractItemView>
#include <QAction>
#include <QScrollBar>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDropEvent>
#include <QMenu>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QStatusBar>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>
#include <QUrl>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_card_ops.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_file_extract.h"
#include "core/sha256.h"
#include "debug/debugger.h"
#include "core/held_files.h"
#include "core/output_files.h"
#include "gui/app_config.h"
#include "gui/main_window.h"
#include "gui/sdcard_card_model.h"
#include "gui/sdcard_manager_dialog.h"
#include "peripheral/sd_card.h"
#include "peripheral/sd_write_journal.h"
#include "platform/emulator_boot.h"
#include "platform/sdcard_manager.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace sm = sdmanager;
using jnext::dbg::ClientId;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::Debugger;

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
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

}  // namespace

#undef signals
#undef slots
#undef emit
#include "../sdcard_file_extract/sdfx_helpers.h"

namespace {

using Tree = std::map<std::string, Bytes>;

const Bytes kWotef = payload(1500, 1);
const Bytes kFileBin = payload(400, 4);
const Bytes kA = payload(700, 5);
const Bytes kB = payload(1, 6);
const Bytes kHost1 = payload(210, 71), kHost2 = payload(220, 72), kX = payload(120, 73);

bool populate(FATFS&, Pokes&)
{
    return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef) && f_mkdir("0:/GAMES/SUB") == FR_OK &&
           f_mkdir("0:/MYDIR") == FR_OK && fat_write("0:/MYDIR/A.BIN", kA) && fat_write("0:/MYDIR/B.BIN", kB) &&
           f_mkdir("0:/EMPTYD") == FR_OK && fat_write("0:/FILE.BIN", kFileBin);
}

// /SHD: an earlier long entry "foo" (alias FOO~1) and a later 8.3-only "FOO". FatFs resolves the name FOO to the first, so
// the second has no spelling that opens IT: the card pane shows it and never acts on it (empty open_name).
const Bytes kD1 = payload(200, 21), kD2 = payload(210, 22);
bool populate_shadow(FATFS& fsv, Pokes& pokes)
{
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/SHD") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
        return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/SHD"), 2,
             {{u"foo", "FOO~1      ", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"", "FOO        ", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    return c1 != 0 && c2 != 0;
}

// A card whose names hold code page 437 bytes (FatFs's OEM encoding): /caf\x82.bin and /D\x82R/inner.bin ("\x82" is e-acute).
const Bytes kCaf = payload(333, 81);
bool populate_cp437(FATFS&, Pokes&)
{
    return fat_write("0:/caf\x82.bin", kCaf) && f_mkdir("0:/D\x82R") == FR_OK && fat_write("0:/D\x82R/inner.bin", kA) &&
           fat_write("0:/plain.bin", kB);
}

struct Img {
    std::string p;
    explicit Img(const std::string& master)
    {
        p = tmp_name("c") + ".img";
        fs::copy_file(master, p, fs::copy_options::overwrite_existing);
    }
    ~Img() { std::error_code ec; fs::remove(p, ec); }
    Img(const Img&) = delete;
    Img& operator=(const Img&) = delete;
};

fs::path fresh_dir(const std::string& stem)
{
    const fs::path d = g_dir / (stem + "-" + std::to_string(++g_n));
    fs::create_directories(d);
    return d;
}

bool on_card(const std::string& img, const std::string& path)
{
    bool ex = false, d = false;
    std::string err;
    return sdcard::cardops::stat(img, path, ex, d, err) == sdcard::FileAddStatus::Ok && ex;
}

Tree extracted(const std::string& img, const std::string& card_path)
{
    const fs::path d = fresh_dir("x");
    std::string err;
    sdcard::extract_from_image(img, card_path, d.string(), err);
    Tree t = tree_of(d);
    std::error_code ec;
    fs::remove_all(d, ec);
    return t;
}

// The machine and its backend (with a loop driver that counts cold boots).
struct Rig {
    std::unique_ptr<Emulator> emu_owner = std::make_unique<Emulator>();
    Emulator& emu = *emu_owner;
    EmulatorConfig cfg;
    std::unique_ptr<Debugger> dbg;
    int boots = 0;
    explicit Rig(bool no_sd = false, MachineType type = MachineType::ZX48K)
    {
        cfg.type = type;
        cfg.no_sdcard = no_sd;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::LoopDriver d;
        d.cold_boot = [this]() {
            ++boots;
            emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            return true;
        };
        dbg->set_loop_driver(d);
    }
};

// What the boxes of a dialog saw, and what to answer.
struct Boxes {
    std::vector<std::string> titles, texts;
    std::function<QMessageBox::StandardButton(const QString&)> answer = [](const QString&) { return QMessageBox::Yes; };
    std::vector<QString> names;   // answers for the text prompts, in order
    size_t next_name = 0;
    void say(const QString& n) { names = {n}; next_name = 0; }
    std::vector<std::string> trashed;
    bool trash_ok = true;
    bool boxes_shown_is_zero() const { return titles.empty(); }
    int count(const char* title_part) const
    {
        int n = 0;
        for (const auto& t : titles) if (t.find(title_part) != std::string::npos) ++n;
        return n;
    }
};

void wire(SdManagerDialog& d, Boxes& b)
{
    d.stop_polling_for_test();
    d.set_box_hook_for_test([&b](const QString& title, const QString& text, QMessageBox::StandardButtons) {
        b.titles.push_back(title.toStdString());
        b.texts.push_back(text.toStdString());
        return b.answer(title);
    });
    d.set_text_hook_for_test([&b](const QString&, const QString&, const QString&) -> std::optional<QString> {
        if (b.next_name >= b.names.size()) return std::nullopt;
        return b.names[b.next_name++];
    });
    d.set_trash_hook_for_test([&b](const QString& path, QString& err) {
        b.trashed.push_back(path.toStdString());
        if (!b.trash_ok) { err = QStringLiteral("no trash here"); return false; }
        std::error_code ec;
        fs::remove_all(path.toStdString(), ec);
        return !ec;
    });
}

void pump_until(const std::function<bool()>& done, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
}

template <class T> T* child(QObject& o, const char* name) { return o.findChild<T*>(QString::fromLatin1(name)); }

// The host pane: wait for the directory to be read, then select the named entries.
void select_host(SdManagerDialog& d, const std::vector<std::string>& paths)
{
    auto* view = child<QListView>(d, "host_view");
    auto* model = qobject_cast<SdHostModel*>(view->model());
    pump_until([&] { return model->rowCount() > (model->has_up_row() ? 1 : 0); });
    view->selectionModel()->clearSelection();
    for (const std::string& p : paths) {
        const QModelIndex i = model->index_of_path(QString::fromStdString(p));
        view->selectionModel()->select(i, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

void select_card(SdManagerDialog& d, const std::vector<std::string>& names)
{
    auto* view = child<QListView>(d, "card_view");
    auto* model = qobject_cast<SdCardModel*>(view->model());
    view->selectionModel()->clearSelection();
    for (int r = 0; r < model->rowCount(); ++r) {
        const std::string n = model->index(r, 0).data(Qt::DisplayRole).toString().toStdString();
        if (std::find(names.begin(), names.end(), n) != names.end())
            view->selectionModel()->select(model->index(r, 0), QItemSelectionModel::Select);
    }
}

std::vector<std::string> card_names(SdManagerDialog& d)
{
    auto* model = qobject_cast<SdCardModel*>(child<QListView>(d, "card_view")->model());
    std::vector<std::string> v;
    for (int r = 0; r < model->rowCount(); ++r) v.push_back(model->index(r, 0).data(Qt::DisplayRole).toString().toStdString());
    return v;
}

// Double-click the card pane's folder `name`, and its up row (row 0 of every folder but the root).
void enter_card(SdManagerDialog& d, const char* name)
{
    auto* view = child<QListView>(d, "card_view");
    auto* model = qobject_cast<SdCardModel*>(view->model());
    for (int r = 0; r < model->rowCount(); ++r)
        if (model->index(r, 0).data().toString() == name) { Q_EMIT view->doubleClicked(model->index(r, 0)); return; }
}
void up_card(SdManagerDialog& d)
{
    auto* view = child<QListView>(d, "card_view");
    Q_EMIT view->doubleClicked(view->model()->index(0, 0));
}
void up_host(SdManagerDialog& d)
{
    auto* view = child<QListView>(d, "host_view");
    Q_EMIT view->doubleClicked(view->model()->index(0, 0));
}

bool has(const std::vector<std::string>& v, const std::string& n) { return std::find(v.begin(), v.end(), n) != v.end(); }

sm::Remembered none_remembered() { return {}; }

QMimeData* uri_list(const std::vector<std::string>& paths)
{
    auto* m = new QMimeData;
    QList<QUrl> urls;
    for (const std::string& p : paths) urls << QUrl::fromLocalFile(QString::fromStdString(p));
    m->setUrls(urls);
    return m;
}

QMimeData* card_mime(const std::string& image, const std::vector<std::string>& paths)
{
    auto* m = new QMimeData;
    QStringList l;
    for (const std::string& p : paths) l << QString::fromStdString(p);
    m->setData(QString::fromLatin1(SdCardModel::kMime), SdCardModel::encode_items(QString::fromStdString(image), l));
    return m;
}

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg_dir;
    if (!cfg_dir.isValid()) { std::printf("  FAIL SDMW-00: no scratch directory\nTotal:    0  Passed:    0  Failed:    1  Skipped:    0\n"); return 1; }
    qputenv("JNEXT_CONFIG_DIR", cfg_dir.path().toUtf8());
    QApplication app(argc, argv);
    g_dir = make_scratch_dir("sdmw");
    sd_write_journal::forget_all_for_test();

    const std::string G = tmp_name("general") + ".img";
    const int gt = make_card(G, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate);
    const std::string F16 = tmp_name("fat16") + ".img";
    const int t16 = make_card(F16, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS&, Pokes&) {
        return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
    });
    const std::string sha_g = sha_file(G);
    const fs::path host = g_dir / "host";
    fs::create_directories(host);
    write_host_file(host / "h1.bin", kHost1);
    write_host_file(host / "h2.bin", kHost2);

    // SDMW-01
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        d.show();
        auto* card = child<QListView>(d, "card_view");
        auto* hostv = child<QListView>(d, "host_view");
        const bool left = card && hostv && card->mapTo(&d, QPoint(0, 0)).x() < hostv->mapTo(&d, QPoint(0, 0)).x();
        const bool models = card && hostv && qobject_cast<SdCardModel*>(card->model()) && qobject_cast<SdHostModel*>(hostv->model());
        // the icon view is gone: both panes are lists, and nothing offers another mode, and the old Up buttons are gone too
        const bool list_only = card->viewMode() == QListView::ListMode && hostv->viewMode() == QListView::ListMode &&
                               !d.findChild<QToolButton*>(QStringLiteral("card_mode_button")) && !d.findChild<QToolButton*>(QStringLiteral("host_mode_button")) &&
                               !d.findChild<QToolButton*>(QStringLiteral("card_up")) && !d.findChild<QToolButton*>(QStringLiteral("host_up"));
        const char* names[] = {"card_copy", "card_rename", "card_delete", "card_new_folder", "host_copy", "host_rename", "host_delete", "host_new_folder"};
        bool actions = true;
        for (const char* n : names) actions = actions && d.findChild<QAction*>(QString::fromLatin1(n)) != nullptr;
        check("SDMW-01", "two panes, the card left and the host right, each a native list view (SdCardModel, SdHostModel over QFileSystemModel), list only (no icon view, no Up button); each pane has Copy, Rename, Delete and New Folder actions",
              left && models && list_only && actions, "left=" + std::to_string(left));
    }
    // SDMW-02
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        const std::vector<std::string> root = card_names(d);
        const bool listed = has(root, "GAMES") && has(root, "MYDIR") && has(root, "FILE.BIN") && root.size() == 4 &&
                            root.front() == "EMPTYD";   // directories first, then by name
        auto* card = child<QListView>(d, "card_view");
        auto* model = qobject_cast<SdCardModel*>(card->model());
        int games = -1;
        for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "GAMES") games = r;
        Q_EMIT card->doubleClicked(model->index(games, 0));
        const bool entered = d.card_dir() == "/GAMES" && has(card_names(d), "WOTEF.NEX") && has(card_names(d), "SUB");
        up_card(d);
        const bool up = d.card_dir() == "/";
        auto* hostv = child<QListView>(d, "host_view");
        auto* hmodel = qobject_cast<SdHostModel*>(hostv->model());
        pump_until([&] { return hmodel->rowCount() >= 3; });
        // the host folder holds two entries and the up row
        const bool host_ok = d.host_dir() == QString::fromStdString(host.string()) && hmodel->rowCount() == 3 &&
                             !hmodel->source()->index(hmodel->source()->rootPath()).data(Qt::DecorationRole).isNull();
        const bool icons = !model->index(games, 0).data(Qt::DecorationRole).value<QIcon>().isNull();
        check("SDMW-02", "the card pane lists the fixture root (directories first, then by name), a double-click enters a folder and the up row returns; the host pane is a QFileSystemModel at the resolved folder; entries carry the platform's generic icons",
              listed && entered && up && host_ok && icons, "root=" + std::to_string(root.size()));
    }
    // SDMW-03
    {
        Img a(G), c(F16);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* combo = child<QComboBox>(d, "slot_combo");
        auto* im = qobject_cast<QStandardItemModel*>(combo->model());
        const bool two = combo->count() == 2;
        const bool empty_disabled = im && !im->item(1)->isEnabled() && im->item(0)->isEnabled();
        rig.emu.sd_card2().mount(c.p, false);
        d.check_card_change();   // the machine has a second card now
        const bool enabled_now = im && im->item(1)->isEnabled();   // the selector follows the machine, not only the shown slot
        rig.emu.sd_card2().unmount();   // and it goes away again: the selector follows
        d.check_card_change();
        const bool disabled_again = im && !im->item(1)->isEnabled();
        rig.emu.sd_card2().mount(c.p, false);
        d.check_card_change();
        combo->setCurrentIndex(1);
        const bool second = d.slot() == 1 && d.card_dir() == "/" && has(card_names(d), "GAMES") && card_names(d).size() == 1;
        check("SDMW-03", "the selector lists the two slots only (an empty one disabled, and the selector follows a card that appears or goes away); choosing the second slot lists the second card at its root",
              two && empty_disabled && enabled_now && disabled_again && second, "slot=" + std::to_string(d.slot()));
    }
    // SDMW-41, SDMW-42, SDMW-43 (owner change: the selector names the slots "SD Card 1: <file>" / "SD Card 2: <empty>")
    {
        Img a(G), c(F16), c2(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* combo = child<QComboBox>(d, "slot_combo");
        const QString name_a = QString::fromStdString(fs::path(a.p).filename().string());
        const QString path_a = QDir::toNativeSeparators(QString::fromStdString(a.p));
        check("SDMW-41", "a mounted slot reads 'SD Card N: <image file name>' (numbered from 1) and its tooltip is the image's full path",
              combo->itemText(0) == QStringLiteral("SD Card 1: ") + name_a && combo->itemData(0, Qt::ToolTipRole).toString() == path_a,
              combo->itemText(0).toStdString() + " | " + combo->itemData(0, Qt::ToolTipRole).toString().toStdString());
        check("SDMW-42", "an empty slot reads 'SD Card 2: <empty>' and is disabled",
              combo->itemText(1) == QStringLiteral("SD Card 2: <empty>") && combo->itemData(1, Qt::ToolTipRole).toString().isEmpty() &&
                  !qobject_cast<QStandardItemModel*>(combo->model())->item(1)->isEnabled(),
              combo->itemText(1).toStdString());
        rig.emu.sd_card2().mount(c.p, false);
        d.check_card_change();
        const bool second = combo->itemText(1) == QStringLiteral("SD Card 2: ") + QString::fromStdString(fs::path(c.p).filename().string()) &&
                            combo->itemData(1, Qt::ToolTipRole).toString() == QDir::toNativeSeparators(QString::fromStdString(c.p));
        rig.emu.sd_card2().unmount();
        d.check_card_change();
        const bool gone = combo->itemText(1) == QStringLiteral("SD Card 2: <empty>");
        rig.emu.sd_card().unmount();
        rig.emu.sd_card().mount(c2.p, false);   // slot 1's card is replaced by another file
        d.check_card_change();
        const bool replaced = combo->itemText(0) == QStringLiteral("SD Card 1: ") + QString::fromStdString(fs::path(c2.p).filename().string()) &&
                              combo->itemData(0, Qt::ToolTipRole).toString() == QDir::toNativeSeparators(QString::fromStdString(c2.p));
        check("SDMW-43", "the labels follow the machine while the window is open: a card inserted in the other slot, ejected again, and the shown slot's image replaced by another file",
              second && gone && replaced, std::to_string(second) + std::to_string(gone) + std::to_string(replaced));
    }
    // SDMW-04
    {
        auto state = [&](SdManagerDialog& d, bool& forced, bool& greyed, std::string& tip) {
            auto* ro = child<QCheckBox>(d, "read_only_box");
            forced = ro->isChecked();
            greyed = !ro->isEnabled();
            tip = ro->toolTip().toStdString();
        };
        auto writes_off = [&](SdManagerDialog& d) {
            const char* names[] = {"card_rename", "card_delete", "card_new_folder", "host_copy"};
            bool off = !child<QPushButton>(d, "copy_to_card")->isEnabled();
            for (const char* n : names) off = off && !d.findChild<QAction*>(QString::fromLatin1(n))->isEnabled();
            return off;
        };
        // a writable card: the box is off and enabled; ticking it disables the writing actions and refuses a drop
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        select_host(d, {(host / "h1.bin").string()});
        bool f0, g0; std::string t0;
        state(d, f0, g0, t0);
        const bool plain = !f0 && !g0 && child<QPushButton>(d, "copy_to_card")->isEnabled();
        child<QCheckBox>(d, "read_only_box")->setChecked(true);
        const bool ticked = writes_off(d);
        std::unique_ptr<QMimeData> m(uri_list({(host / "h1.bin").string()}));
        const bool refused = !d.drop_on_card(*m, false) && sha_file(a.p) == sha_g;
        // a FAT16 card
        Img f(F16);
        Rig rig2;
        rig2.emu.sd_card().mount(f.p, false);
        SdManagerDialog d2(rig2.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d2, b);
        bool f1, g1; std::string t1;
        state(d2, f1, g1, t1);
        const bool fat16 = f1 && g1 && t1.find("first FAT32 partition") != std::string::npos && writes_off(d2);
        // --sdcard-readonly
        Img w(G);
        Rig rig3;
        rig3.emu.sd_card().mount(w.p, true);
        SdManagerDialog d3(rig3.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d3, b);
        bool f2, g2; std::string t2;
        state(d3, f2, g2, t2);
        const bool wp = f2 && g2 && t2.find("--sdcard-readonly") != std::string::npos && writes_off(d3);
        // while an RZX records
        Img r(G);
        Rig rig4;
        rig4.emu.sd_card().mount(r.p, false);
        rig4.emu.start_rzx_recording((g_dir / "w04.rzx").string());
        SdManagerDialog d4(rig4.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d4, b);
        bool f3, g3; std::string t3;
        state(d4, f3, g3, t3);
        const bool rzx = f3 && g3 && t3.find("RZX") != std::string::npos && writes_off(d4);
        check("SDMW-04", "a writable card has an enabled, unticked Read-only box; ticking it disables every card-writing action and refuses a drop on the card; the box is FORCED on and greyed, with the reason, for a FAT16 card, for --sdcard-readonly and while an RZX records",
              plain && ticked && refused && fat16 && wp && rzx,
              std::to_string(plain) + std::to_string(ticked) + std::to_string(refused) + std::to_string(fat16) + std::to_string(wp) + std::to_string(rzx));
    }
    // SDMW-05
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        select_host(d, {(host / "h1.bin").string(), (host / "h2.bin").string()});
        child<QPushButton>(d, "copy_to_card")->click();
        const bool first = on_card(a.p, "/h1.bin") && on_card(a.p, "/h2.bin") && b.count("Change the SD card") == 0;
        const bool refreshed = has(card_names(d), "h1.bin") && has(card_names(d), "h2.bin");
        select_host(d, {(host / "h1.bin").string()});
        child<QPushButton>(d, "copy_to_card")->click();   // exists now: asks to replace, and nothing else
        const bool once = b.count("Change the SD card") == 0 && b.count("Already exists") == 1;
        check("SDMW-05", "'< Copy' copies the host selection onto the card: no question about changing the card is asked, the card pane shows the new files, and an existing one asks",
              first && refreshed && once && extracted(a.p, "/h1.bin") == Tree{{"h1.bin", kHost1}},
              "first=" + std::to_string(first) + " refreshed=" + std::to_string(refreshed) + " once=" + std::to_string(once));
    }
    // SDMW-06
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path out = fresh_dir("drop-out");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        std::unique_ptr<QMimeData> m1(card_mime(a.p, {"/GAMES/WOTEF.NEX"}));
        const bool copy = d.drop_on_host(*m1, false) && read_host_file(out / "WOTEF.NEX") == kWotef && on_card(a.p, "/GAMES/WOTEF.NEX") &&
                          b.count("Change the SD card") == 0;
        std::unique_ptr<QMimeData> m2(card_mime(a.p, {"/FILE.BIN"}));
        const bool move = d.drop_on_host(*m2, true) && read_host_file(out / "FILE.BIN") == kFileBin && !on_card(a.p, "/FILE.BIN") &&
                          b.count("Change the SD card") == 0;
        // a drop from another card is refused
        std::unique_ptr<QMimeData> m3(card_mime((g_dir / "other.img").string(), {"/MYDIR"}));
        const bool foreign = !d.drop_on_host(*m3, false) && !fs::exists(out / "MYDIR");
        // host files onto the card: a copy, and with Shift a move (the original goes to the trash)
        const fs::path src = fresh_dir("drop-src");
        write_host_file(src / "c1.bin", kHost1);
        write_host_file(src / "c2.bin", kHost2);
        std::unique_ptr<QMimeData> m4(uri_list({(src / "c1.bin").string()}));
        const bool to_card = d.drop_on_card(*m4, false) && on_card(a.p, "/c1.bin") && fs::exists(src / "c1.bin");
        std::unique_ptr<QMimeData> m5(uri_list({(src / "c2.bin").string()}));
        const bool to_card_move = d.drop_on_card(*m5, true) && on_card(a.p, "/c2.bin") && !fs::exists(src / "c2.bin") &&
                                  b.trashed.size() == 1 && b.trashed[0] == (src / "c2.bin").string();
        check("SDMW-06", "a drop of the card's items on the host pane copies them (the card is not changed), with Shift it moves (the card source is removed, with no question about changing the card); a drop from another card is refused; local files dropped on the card pane are copied, with Shift moved (the original to the trash)",
              copy && move && foreign && to_card && to_card_move,
              std::to_string(copy) + std::to_string(move) + std::to_string(foreign) + std::to_string(to_card) + std::to_string(to_card_move));
    }
    // SDMW-07
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("del");
        write_host_file(hd / "d1.bin", kHost1);
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        select_card(d, {"FILE.BIN"});
        b.answer = [](const QString& t) { return t.contains("Delete") ? QMessageBox::No : QMessageBox::Yes; };
        d.findChild<QAction*>("card_delete")->trigger();
        const bool declined = on_card(a.p, "/FILE.BIN") && sha_file(a.p) == sha_g;
        b.answer = [](const QString&) { return QMessageBox::Yes; };
        d.findChild<QAction*>("card_delete")->trigger();
        const bool card_gone = !on_card(a.p, "/FILE.BIN") && !has(card_names(d), "FILE.BIN");
        select_host(d, {(hd / "d1.bin").string()});
        d.findChild<QAction*>("host_delete")->trigger();
        const bool host_trashed = b.trashed.size() == 1 && b.trashed[0] == (hd / "d1.bin").string() && !fs::exists(hd / "d1.bin");
        check("SDMW-07", "Delete asks: No changes nothing; Yes on the card removes it permanently, and on the host sends it to the trash",
              declined && card_gone && host_trashed,
              std::to_string(declined) + std::to_string(card_gone) + std::to_string(host_trashed));
    }
    // SDMW-08
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("ren");
        write_host_file(hd / "r1.bin", kHost1);
        write_host_file(hd / "r2.bin", kHost2);
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        select_card(d, {"FILE.BIN"});
        b.say(QStringLiteral("RENAMED.BIN"));
        d.findChild<QAction*>("card_rename")->trigger();
        const bool card_ok = on_card(a.p, "/RENAMED.BIN") && !on_card(a.p, "/FILE.BIN") && has(card_names(d), "RENAMED.BIN");
        select_card(d, {"RENAMED.BIN"});
        b.say(QStringLiteral("MYDIR"));
        d.findChild<QAction*>("card_rename")->trigger();   // an existing name: refused, never overwritten
        const bool card_refused = on_card(a.p, "/RENAMED.BIN") && on_card(a.p, "/MYDIR/A.BIN");
        select_host(d, {(hd / "r1.bin").string()});
        b.say(QStringLiteral("r1b.bin"));
        d.findChild<QAction*>("host_rename")->trigger();
        const bool host_ok = fs::exists(hd / "r1b.bin") && !fs::exists(hd / "r1.bin");
        select_host(d, {(hd / "r1b.bin").string()});
        b.say(QStringLiteral("r2.bin"));
        d.findChild<QAction*>("host_rename")->trigger();
        const bool host_refused = fs::exists(hd / "r1b.bin") && read_host_file(hd / "r2.bin") == kHost2;
        check("SDMW-08", "Rename on the card and on the host; a name that exists is refused on both sides and nothing is overwritten",
              card_ok && card_refused && host_ok && host_refused,
              std::to_string(card_ok) + std::to_string(card_refused) + std::to_string(host_ok) + std::to_string(host_refused));
    }
    // SDMW-09
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("rem9");
        fs::create_directories(hd / "sub");
        sm::Remembered saved;
        {
            SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, (hd / "sub").string()}, QString::fromStdString(host.string()));
            wire(d, b);
            auto* card = child<QListView>(d, "card_view");
            auto* model = qobject_cast<SdCardModel*>(card->model());
            int games = -1;
            for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "GAMES") games = r;
            Q_EMIT card->doubleClicked(model->index(games, 0));
            saved = d.remembered();
        }
        SdManagerDialog again(rig.emu, saved, QString::fromStdString(host.string()));
        wire(again, b);
        check("SDMW-09", "close and reopen in-process: the same card, card folder and host folder",
              saved.card_image == a.p && again.slot() == 0 && again.card_dir() == "/GAMES" && again.host_dir() == QString::fromStdString((hd / "sub").string()) &&
                  b.titles.empty(),
              again.card_dir().toStdString());
    }
    // SDMW-10
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("rem10");
        const QString ini = QString::fromStdString((g_dir / "restart.conf").string());
        {
            SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
            wire(d, b);
            AppConfig cfg(ini);
            cfg.load();
            const sm::Remembered r = d.remembered();
            cfg.data().sdmanager_card = QString::fromStdString(r.card_image);
            cfg.data().sdmanager_card_dir = QString::fromStdString(r.card_dir);
            cfg.data().sdmanager_host_dir = QString::fromStdString(r.host_dir);
            cfg.save();
        }
        AppConfig cfg2(ini);   // "a second run": a new AppConfig over the same file
        cfg2.load();
        sm::Remembered r2;
        r2.card_image = cfg2.data().sdmanager_card.toStdString();
        r2.card_dir = cfg2.data().sdmanager_card_dir.toStdString();
        r2.host_dir = cfg2.data().sdmanager_host_dir.toStdString();
        SdManagerDialog d2(rig.emu, r2, QString::fromStdString(host.string()));
        wire(d2, b);
        check("SDMW-10", "restart: a second AppConfig over the same file gives the window the same card, card folder and host folder",
              d2.slot() == 0 && d2.card_dir() == "/" && d2.host_dir() == QString::fromStdString(hd.string()) && r2.card_image == a.p,
              r2.card_image + "|" + r2.host_dir);
    }
    // SDMW-11
    {
        Img a(G), c(F16);
        Rig rig;
        rig.emu.sd_card2().mount(c.p, false);   // only slot 1 holds a card now; the remembered card (slot 0's) is gone
        Boxes b;
        const fs::path base = fresh_dir("rem11");
        fs::create_directories(base / "a" / "b");
        sm::Remembered gone;
        gone.card_image = a.p;
        gone.card_dir = sm::encode_card_dir("/GAMES/SUB");
        gone.host_dir = (base / "a" / "b" / "c").string();   // the leaf does not exist
        SdManagerDialog d(rig.emu, gone, QString::fromStdString(host.string()));
        wire(d, b);
        const bool emulator_wins = d.slot() == 1 && d.card_dir() == "/" && has(card_names(d), "GAMES");
        const bool host_parent = d.host_dir() == QString::fromStdString((base / "a" / "b").string());
        // the remembered card is mounted but the folder is gone: the ROOT, not the nearest parent
        Img a2(G);
        Rig rig2;
        rig2.emu.sd_card().mount(a2.p, false);
        sm::Remembered stale;
        stale.card_image = a2.p;
        stale.card_dir = sm::encode_card_dir("/GAMES/NOPE/DEEP");
        SdManagerDialog d2(rig2.emu, stale, QString::fromStdString(host.string()));
        wire(d2, b);
        check("SDMW-11", "remembered state that disagrees with the emulator: a card that is not mounted gives the mounted one at its ROOT, a vanished card folder gives the root (no nearest parent), a vanished host folder gives its nearest existing parent; no message box is shown",
              emulator_wins && host_parent && d2.slot() == 0 && d2.card_dir() == "/" && b.titles.empty(),
              std::to_string(emulator_wins) + std::to_string(host_parent) + d2.card_dir().toStdString());
    }
    // SDMW-12
    {
        Img a(G), c(G), e(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{a.p, sm::encode_card_dir("/GAMES"), {}}, QString::fromStdString(host.string()));
        wire(d, b);
        const bool opened_in = d.card_dir() == "/GAMES";
        const bool quiet = !d.check_card_change();   // nothing changed: nothing happens
        // another jnext thread (a remote debugger) puts a different card in the slot
        rig.emu.sd_card().unmount();
        rig.emu.sd_card().mount(c.p, false);
        const bool reset = d.check_card_change() && d.slot() == 0 && d.card_dir() == "/" && d.remembered().card_image == c.p && has(card_names(d), "MYDIR");
        // an operation before the poll fires also notices, shows the truth and does not act on the stale view
        rig.emu.sd_card().unmount();
        rig.emu.sd_card().mount(e.p, false);
        select_card(d, {"GAMES"});
        const std::string before = sha_file(e.p);
        d.findChild<QAction*>("card_delete")->trigger();
        const bool stale_refused = sha_file(e.p) == before && d.card_dir() == "/" && has(card_names(d), "MYDIR") && b.texts.back().find("changed") != std::string::npos;
        // the card is ejected
        rig.emu.sd_card().unmount();
        const bool ejected = d.check_card_change() && d.slot() == -1 && card_names(d).empty();
        check("SDMW-12", "a card change while the window is open resets the card view to the new card's ROOT; an operation aimed at a stale view notices first and acts on nothing; an ejected card leaves an empty pane",
              opened_in && quiet && reset && stale_refused && ejected,
              std::to_string(opened_in) + std::to_string(quiet) + std::to_string(reset) + std::to_string(stale_refused) + std::to_string(ejected));
    }
    // SDMW-13
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("nf");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        b.say(QStringLiteral("NEWDIR"));
        d.findChild<QAction*>("card_new_folder")->trigger();
        const bool card_ok = on_card(a.p, "/NEWDIR") && has(card_names(d), "NEWDIR") && b.count("Change the SD card") == 0;
        b.say(QStringLiteral("hostnew"));
        d.findChild<QAction*>("host_new_folder")->trigger();
        const bool host_ok = fs::is_directory(hd / "hostnew");
        b.say(QStringLiteral("NEWDIR"));
        d.findChild<QAction*>("card_new_folder")->trigger();   // exists: refused, reported
        const bool refused = b.texts.back().find("already exists") != std::string::npos;
        check("SDMW-13", "New Folder in both panes, and an existing name is refused with the reason",
              card_ok && host_ok && refused, std::to_string(card_ok) + std::to_string(host_ok) + std::to_string(refused));
    }
    // SDMW-44 (replaces SDMW-14: there is no question before a change any more)
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path out = fresh_dir("noq-out");
        write_host_file(out / "h1.bin", kHost1);
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        d.show();
        select_host(d, {(out / "h1.bin").string()});
        child<QPushButton>(d, "copy_to_card")->click();                                   // copy onto the card
        const bool copied = on_card(a.p, "/h1.bin");
        select_card(d, {"h1.bin"});
        b.say(QStringLiteral("h1-renamed.bin"));
        d.findChild<QAction*>("card_rename")->trigger();                                  // rename on the card
        const bool renamed = on_card(a.p, "/h1-renamed.bin") && !on_card(a.p, "/h1.bin");
        b.say(QStringLiteral("NEWD"));
        d.findChild<QAction*>("card_new_folder")->trigger();                              // New Folder on the card
        const bool made = on_card(a.p, "/NEWD");
        std::unique_ptr<QMimeData> mv(card_mime(a.p, {"/FILE.BIN"}));
        const bool moved = d.drop_on_host(*mv, true) && !on_card(a.p, "/FILE.BIN");       // move off the card
        select_card(d, {"h1-renamed.bin"});
        d.findChild<QAction*>("card_delete")->trigger();                                  // delete on the card (asks "Delete")
        const bool deleted = !on_card(a.p, "/h1-renamed.bin");
        const bool no_question = b.count("Change the SD card") == 0;
        const bool note = d.findChild<QLabel*>("card_note") && d.findChild<QLabel*>("card_note")->isVisible() &&
                          d.findChild<QLabel*>("card_note")->text() ==
                              QStringLiteral("The machine sees a change when it next reads the folder. Do not change files a running program is using.");
        // with a writes file bound the note names it
        Img w(G);
        const std::string jf = (fresh_dir("noq-j") / "w.sdw").string();
        sd_write_journal::Binding bd;
        bd.bytes = fs::file_size(w.p);
        bd.sha256.fill(0x1D);
        std::string why;
        sd_write_journal::bind(w.p, jf, bd, false, why);
        Rig rig2;
        rig2.emu.sd_card().mount(w.p, false);
        SdManagerDialog d2(rig2.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d2, b);
        const QString note2 = d2.findChild<QLabel*>("card_note")->text();
        const bool names_file = note2.contains(QStringLiteral("when it next reads the folder")) && note2.contains(QDir::toNativeSeparators(QString::fromStdString(jf)));
        sd_write_journal::forget_image(w.p);
        check("SDMW-44", "copy, rename, New Folder, move and delete on the card ask no 'Change the SD card?' question (only the delete's own confirmation); the permanent note is shown; with a writes file bound the note names the file",
              copied && renamed && made && moved && deleted && no_question && note && names_file,
              std::string(copied ? "1" : "0") + (renamed ? "1" : "0") + (made ? "1" : "0") + (moved ? "1" : "0") + (deleted ? "1" : "0") +
                  (no_question ? "1" : "0") + (note ? "1" : "0") + (names_file ? "1" : "0"));
    }
    // SDMW-15
    {
        // opening the window and changing the card leave the machine alone: it keeps running (frames advance while the
        // window is open), is never paused, and ends the visit with no cold boot and no reset request
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        rig.emu.run_frame();
        const uint32_t f0 = rig.dbg->state().frame;
        bool never_paused = !rig.dbg->state().paused;
        {
            SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
            wire(d, b);
            never_paused = never_paused && !rig.dbg->state().paused;
            enter_card(d, "GAMES");   // browsing
            rig.emu.run_frame();
            select_host(d, {(host / "h1.bin").string()});
            child<QPushButton>(d, "copy_to_card")->click();   // changing the card
            never_paused = never_paused && !rig.dbg->state().paused;
            for (int i = 0; i < 3; ++i) rig.emu.run_frame();
        }
        const bool advanced = rig.dbg->state().frame > f0;
        check("SDMW-15", "opening the window never pauses the machine: frames advance while it is open, a visit that browsed and then changed the card ends with zero cold boots and no reset requested, and the machine is never paused",
              never_paused && advanced && on_card(a.p, "/GAMES/h1.bin") && rig.boots == 0 && !rig.emu.take_hard_reset_request(),
              "boots=" + std::to_string(rig.boots));
    }
    // SDMW-16
    {
        // the held list is read at EACH operation: a recording that starts while the window is open is protected
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("held");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        const std::string rzx = (hd / "late.rzx").string();
        write_host_file(hd / "late.rzx", kX);
        rig.emu.start_rzx_recording(rzx);   // started AFTER the window opened
        select_host(d, {rzx});
        d.findChild<QAction*>("host_delete")->trigger();
        const bool late = fs::exists(rzx) && b.trashed.empty() && b.texts.back().find("emulator is using") != std::string::npos;
        // the mounted image itself, and renaming a held file
        const fs::path imgdir = fs::path(a.p).parent_path();
        sm::Remembered at_img;
        at_img.host_dir = imgdir.string();
        SdManagerDialog d2(rig.emu, at_img, QString::fromStdString(host.string()));
        wire(d2, b);
        select_host(d2, {a.p});
        d2.findChild<QAction*>("host_delete")->trigger();
        b.say(QStringLiteral("renamed.img"));
        d2.findChild<QAction*>("host_rename")->trigger();
        const bool image = fs::exists(a.p) && b.trashed.empty() && !fs::exists(imgdir / "renamed.img");
        check("SDMW-16", "the held list is read at each operation: a recording started after the window opened is protected from Delete, and the mounted image can be neither deleted nor renamed",
              late && image, std::to_string(late) + std::to_string(image));
    }
    // SDMW-17
    {
        // a card entry that cannot be addressed is shown but never acted on; and a model's drag payload round-trips hostile names
        const std::string weird = "/ESC/a\nb%41";
        const QByteArray enc = SdCardModel::encode_items(QStringLiteral("/tmp/x y.img"), QStringList{QString::fromStdString(weird), QStringLiteral("/GAMES")});
        QString img;
        QStringList paths;
        const bool round = SdCardModel::decode_items(enc, img, paths) && img == QLatin1String("/tmp/x y.img") && paths.size() == 2 &&
                           paths[0].toStdString() == weird && enc.count('\n') == 2;
        QString i2;
        QStringList p2;
        const bool malformed = !SdCardModel::decode_items(QByteArray("%ZZ\n/a"), i2, p2) && !SdCardModel::decode_items(QByteArray("only-one-line"), i2, p2);
        const bool nav = SdCardModel::parent_path(QStringLiteral("/A/B")) == QLatin1String("/A") && SdCardModel::parent_path(QStringLiteral("/A")) == QLatin1String("/") &&
                         SdCardModel::parent_path(QStringLiteral("/")) == QLatin1String("/") && SdCardModel::child_path(QStringLiteral("/"), QStringLiteral("X")) == QLatin1String("/X");
        check("SDMW-17", "the drag payload is %XX-escaped line by line, so a name with a newline or a percent round-trips, a malformed payload is refused, and parent/child paths compose",
              round && malformed && nav, std::to_string(round) + std::to_string(malformed) + std::to_string(nav));
    }

    // SDMW-19
    {
        const std::string S = tmp_name("shadow") + ".img";
        const int st = make_card(S, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_shadow);
        Rig rig;
        rig.emu.sd_card().mount(S, false);
        Boxes b;
        const fs::path out = fresh_dir("shadow-out");
        const std::string sha = sha_file(S);
        SdManagerDialog d(rig.emu, sm::Remembered{S, sm::encode_card_dir("/SHD"), out.string()},
                          QString::fromStdString(host.string()));
        wire(d, b);
        auto* model = qobject_cast<SdCardModel*>(child<QListView>(d, "card_view")->model());
        int addressable = 0, shown = 0;
        for (int r = 0; r < model->rowCount(); ++r) {
            if (model->is_up_row(r)) continue;   // the up row is not an entry of the card
            ++shown;
            if (!model->path_of(r).isEmpty()) ++addressable;
        }
        select_card(d, {"FOO", "foo"});
        child<QPushButton>(d, "copy_to_host")->click();
        const Tree got = tree_of(out);
        const bool copy_one = shown == 2 && addressable == 1 && got.size() == 1 && got.count("foo") && got.at("foo") == kD1;
        // only the unaddressable entry selected: nothing to act on
        select_card(d, {"FOO"});
        d.findChild<QAction*>("card_delete")->trigger();
        d.findChild<QAction*>("card_rename")->trigger();
        const bool nothing = sha_file(S) == sha && b.count("Delete") == 0;
        check("SDMW-19", "an entry no spelling can open (a shadowed 8.3 FOO behind a long foo) is listed but has no path: a copy of both copies only the addressable one, and Delete or Rename aimed at it acts on nothing (the card is byte-identical)",
              st == FS_FAT32 && copy_one && nothing, std::to_string(shown) + std::to_string(addressable) + std::to_string(copy_one) + std::to_string(nothing));
    }
    // SDMW-20
    {
        // F1: a card entry whose name holds a code page 437 byte can be copied, renamed and deleted
        const std::string K = tmp_name("cp437") + ".img";
        const int kt = make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        const fs::path out = fresh_dir("cp-out");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        const bool shown = has(card_names(d), "caf%82.bin") && has(card_names(d), "D%82R");
        select_card(d, {"caf%82.bin"});
        child<QPushButton>(d, "copy_to_host")->click();
        const bool copied = read_host_file(out / "caf%82.bin") == kCaf && b.count("SD Card File Manager") == 0;
        b.say(QStringLiteral("plain2.bin"));
        select_card(d, {"caf%82.bin"});
        d.findChild<QAction*>("card_rename")->trigger();
        const bool renamed = on_card(K, "/plain2.bin") && !has(card_names(d), "caf%82.bin") && b.count("SD Card File Manager") == 0;
        select_card(d, {"D%82R"});
        d.findChild<QAction*>("card_delete")->trigger();
        const bool deleted = !has(card_names(d), "D%82R") && b.count("SD Card File Manager") == 0;
        check("SDMW-20", "a card entry named with a code page 437 byte (caf<e-acute>.bin, D<e-acute>R) is shown escaped and can be copied to the host, renamed and deleted (the path stays byte-exact through the window)",
              kt == FS_FAT32 && shown && copied && renamed && deleted,
              std::to_string(shown) + std::to_string(copied) + std::to_string(renamed) + std::to_string(deleted));
    }
    // SDMW-21
    {
        // F1: a folder with such a name can be entered, and is remembered and restored byte-exactly (the stored value stays ASCII)
        const std::string K = tmp_name("cp437") + ".img";
        make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* card = child<QListView>(d, "card_view");
        auto* model = qobject_cast<SdCardModel*>(card->model());
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "D%82R") row = r;
        Q_EMIT card->doubleClicked(model->index(row, 0));
        const bool entered = SdCardModel::card_bytes(d.card_dir()) == "/D\x82R" && has(card_names(d), "inner.bin");
        const sm::Remembered rem = d.remembered();
        bool ascii = true;
        for (unsigned char ch : rem.card_dir) if (ch < 0x20 || ch > 0x7E) ascii = false;
        const QString ini = QString::fromStdString((g_dir / "cp437.conf").string());
        {
            AppConfig w(ini);
            w.load();
            w.data().sdmanager_card = QString::fromStdString(rem.card_image);
            w.data().sdmanager_card_dir = QString::fromStdString(rem.card_dir);
            w.save();
        }
        AppConfig r(ini);
        r.load();
        sm::Remembered back;
        back.card_image = r.data().sdmanager_card.toStdString();
        back.card_dir = r.data().sdmanager_card_dir.toStdString();
        SdManagerDialog again(rig.emu, back, QString::fromStdString(host.string()));
        wire(again, b);
        check("SDMW-21", "a folder whose name holds a code page 437 byte can be entered; the remembered value is ASCII (%XX), survives the config file, and reopens the window in that same folder",
              entered && ascii && rem.card_dir == "/D%82R" && SdCardModel::card_bytes(again.card_dir()) == "/D\x82R" && has(card_names(again), "inner.bin"),
              rem.card_dir);
    }
    // SDMW-22
    {
        // F1: the drag payload carries such a name byte-exactly, and a drop of it copies the right entry
        const std::string K = tmp_name("cp437") + ".img";
        make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        const fs::path out = fresh_dir("cp-drag");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        auto* model = qobject_cast<SdCardModel*>(child<QListView>(d, "card_view")->model());
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "caf%82.bin") row = r;
        std::unique_ptr<QMimeData> m(model->mimeData({model->index(row, 0)}));
        QString img;
        QStringList paths;
        const bool decoded = m && SdCardModel::decode_items(m->data(QString::fromLatin1(SdCardModel::kMime)), img, paths) &&
                             paths.size() == 1 && SdCardModel::card_bytes(paths[0]) == "/caf\x82.bin";
        const bool dropped = m && d.drop_on_host(*m, false) && read_host_file(out / "caf%82.bin") == kCaf;
        check("SDMW-22", "the drag payload of an entry named with a code page 437 byte carries the path byte-exactly, and dropping it on the host pane copies that entry",
              decoded && dropped, std::to_string(decoded) + std::to_string(dropped) + (b.texts.empty() ? std::string(" no box") : " " + b.texts.back()) + " host=" + [&] { std::string l; for (auto& e : fs::directory_iterator(out)) l += e.path().filename().string() + ","; return l; }());
    }
    // SDMW-23
    {
        // G1: Qt's own inline rename must not exist: the host model is read-only, its items are not editable, no view edits in place
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* hostv = child<QListView>(d, "host_view");
        auto* hmodel = qobject_cast<SdHostModel*>(hostv->model());
        pump_until([&] { return hmodel->rowCount() >= 3; });
        const QModelIndex first = hmodel->index(1, 0);   // row 0 is the up row
        const bool read_only = hmodel->source()->isReadOnly() && !(hmodel->flags(first) & Qt::ItemIsEditable);
        const bool no_edit = hostv->editTriggers() == QAbstractItemView::NoEditTriggers &&
                             child<QListView>(d, "card_view")->editTriggers() == QAbstractItemView::NoEditTriggers;
        check("SDMW-23", "the host pane's model is read-only with no editable item and neither view edits in place (Qt's inline rename would bypass the held-file check)",
              read_only && no_edit, std::to_string(read_only) + std::to_string(no_edit));
    }
    // SDMW-24
    {
        // G2: the context menu is the way to New Folder (and Copy, Rename, Delete) in BOTH panes: drive the real signal and read the popup
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto menu_texts = [&](const char* view) {
            QStringList texts;
            bool seen = false;
            QTimer::singleShot(0, [&] {
                if (auto* m = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    seen = true;
                    for (QAction* act : m->actions()) texts << act->text();
                    m->close();
                }
            });
            Q_EMIT child<QListView>(d, view)->customContextMenuRequested(QPoint(3, 3));
            QCoreApplication::processEvents();
            return seen ? texts : QStringList{QStringLiteral("<no menu>")};
        };
        const QStringList card = menu_texts("card_view"), hostm = menu_texts("host_view");
        auto full = [](const QStringList& t, int n) {
            const QString j = t.join(QLatin1Char('|'));
            return t.size() == n && j.contains(QStringLiteral("Rename")) && j.contains(QStringLiteral("Delete")) &&
                   j.contains(QStringLiteral("New Folder")) && j.contains(QStringLiteral("Copy"));
        };
        check("SDMW-24", "the context menu of each pane lists Copy, Rename, Delete and New Folder, and the card pane's also Refresh (F5)",
              full(card, 5) && card.join(QLatin1Char('|')).contains(QStringLiteral("Refresh")) && full(hostm, 4), card.join(QLatin1Char('|')).toStdString() + " / " + hostm.join(QLatin1Char('|')).toStdString());
    }
    // SDMW-25
    {
        // G3: "Shift = move" through the REAL drop event, on both views
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path out = fresh_dir("real-drop");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        auto send_drop = [&](const char* view, const QMimeData& m, Qt::KeyboardModifiers mods) {
            auto* v = child<QListView>(d, view);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            QDropEvent ev(QPointF(4, 4), Qt::CopyAction | Qt::MoveAction, &m, Qt::LeftButton, mods);
#else
            QDropEvent ev(QPoint(4, 4), Qt::CopyAction | Qt::MoveAction, &m, Qt::LeftButton, mods);
#endif
            ev.ignore();
            d.deliver_event_for_test(v == child<QListView>(d, "card_view") ? SdManagerDialog::Side::Card : SdManagerDialog::Side::Host, ev);
            return ev.isAccepted();
        };
        std::unique_ptr<QMimeData> c1(card_mime(a.p, {"/MYDIR/A.BIN"}));
        const bool plain = send_drop("host_view", *c1, Qt::NoModifier) && read_host_file(out / "A.BIN") == kA && on_card(a.p, "/MYDIR/A.BIN");
        std::unique_ptr<QMimeData> c2(card_mime(a.p, {"/FILE.BIN"}));
        const bool shifted = send_drop("host_view", *c2, Qt::ShiftModifier) && read_host_file(out / "FILE.BIN") == kFileBin && !on_card(a.p, "/FILE.BIN");
        const fs::path src = fresh_dir("real-src");
        write_host_file(src / "s1.bin", kHost1);
        write_host_file(src / "s2.bin", kHost2);
        std::unique_ptr<QMimeData> u1(uri_list({(src / "s1.bin").string()}));
        const bool to_card = send_drop("card_view", *u1, Qt::NoModifier) && on_card(a.p, "/s1.bin") && fs::exists(src / "s1.bin");
        std::unique_ptr<QMimeData> u2(uri_list({(src / "s2.bin").string()}));
        const bool to_card_move = send_drop("card_view", *u2, Qt::ShiftModifier) && on_card(a.p, "/s2.bin") && !fs::exists(src / "s2.bin");
        std::unique_ptr<QMimeData> c3(card_mime(a.p, {"/GAMES"}));
        const bool ignored = !send_drop("card_view", *c3, Qt::NoModifier);
        check("SDMW-25", "through a real QDropEvent: a plain drop copies and Shift moves, in both directions; a drop of the card's own items on the card pane is ignored",
              plain && shifted && to_card && to_card_move && ignored,
              std::to_string(plain) + std::to_string(shifted) + std::to_string(to_card) + std::to_string(to_card_move) + std::to_string(ignored) +
                  (b.texts.empty() ? std::string(" no box") : " " + b.texts.back()));
    }
    // SDMW-26
    {
        // G4: the shown folder disappears while the window is open (a guest or a remote client removed it): the view falls back to the root
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{a.p, sm::encode_card_dir("/MYDIR"), {}}, QString::fromStdString(host.string()));
        wire(d, b);
        const bool in_folder = d.card_dir() == "/MYDIR";
        std::string err;
        sdcard::cardops::remove(a.p, "/MYDIR", err);
        d.refresh();
        check("SDMW-26", "the shown card folder removed while the window is open: refresh() shows the root, with the root's listing and no error",
              in_folder && d.card_dir() == "/" && has(card_names(d), "GAMES") && b.titles.empty(), d.card_dir().toStdString());
    }
    // SDMW-27
    {
        // G5: a card rename as the first change asks nothing and resets nothing
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        {
            SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
            wire(d, b);
            select_card(d, {"FILE.BIN"});
            b.say(QStringLiteral("R2.BIN"));
            d.findChild<QAction*>("card_rename")->trigger();
        }
        const bool no_box = b.count("Change the SD card") == 0 && b.boxes_shown_is_zero();
        check("SDMW-27", "a card rename as the first change shows no box at all, makes the change, and the machine is not reset (zero cold boots, no reset request)",
              no_box && on_card(a.p, "/R2.BIN") && rig.boots == 0 && !rig.emu.take_hard_reset_request(), "boots=" + std::to_string(rig.boots));
    }
    // SDMW-28
    {
        // G6: a card-to-host drop whose host name collides with a held file OTHER than the shown image is refused, never trashed
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path out = fresh_dir("held-drop");
        write_host_file(out / "FILE.BIN", kX);
        held_files::Hold hold((out / "FILE.BIN").string());   // a recording, the other slot's image...
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        std::unique_ptr<QMimeData> m(card_mime(a.p, {"/FILE.BIN"}));
        d.drop_on_host(*m, false);
        check("SDMW-28", "a card-to-host drop whose name is a held host file (not the shown image) is refused: the file is never trashed or replaced, and the reason is shown",
              b.trashed.empty() && read_host_file(out / "FILE.BIN") == kX && !b.texts.empty() && b.texts.back().find("emulator is using") != std::string::npos,
              b.texts.empty() ? std::string("no text") : b.texts.back());
    }
    // SDMW-29
    {
        // G7: a held host file as the source of '< Copy' is refused and the card is untouched
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("held-src");
        write_host_file(hd / "rec.bin", kX);
        held_files::Hold hold((hd / "rec.bin").string());
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        select_host(d, {(hd / "rec.bin").string()});
        child<QPushButton>(d, "copy_to_card")->click();
        check("SDMW-29", "a held host file as the source of '< Copy' is refused with the reason and nothing is written to the card",
              !on_card(a.p, "/rec.bin") && !b.texts.empty() && b.texts.back().find("emulator is using") != std::string::npos,
              b.texts.empty() ? std::string("no text") : b.texts.back());
    }
    // SDMW-30
    {
        // '< Copy' INTO a folder named with a code page 437 byte (the destination site carries bytes too)
        const std::string K = tmp_name("cp437") + ".img";
        make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{K, sm::encode_card_dir("/D\x82R"), (host).string()}, QString::fromStdString(host.string()));
        wire(d, b);
        const bool inside = SdCardModel::card_bytes(d.card_dir()) == "/D\x82R";
        select_host(d, {(host / "h1.bin").string()});
        child<QPushButton>(d, "copy_to_card")->click();
        check("SDMW-30", "'< Copy' into a folder named with a code page 437 byte puts the file in that folder",
              inside && on_card(K, "/D\x82R/h1.bin") && !on_card(K, "/h1.bin") && has(card_names(d), "h1.bin"),
              b.texts.empty() ? std::string("no box") : b.texts.back());
    }
    // SDMW-31
    {
        // New Folder INSIDE a folder named with a code page 437 byte
        const std::string K = tmp_name("cp437") + ".img";
        make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{K, sm::encode_card_dir("/D\x82R"), {}}, QString::fromStdString(host.string()));
        wire(d, b);
        b.say(QStringLiteral("SUB"));
        d.findChild<QAction*>("card_new_folder")->trigger();
        check("SDMW-31", "New Folder inside a folder named with a code page 437 byte creates it there",
              on_card(K, "/D\x82R/SUB") && !on_card(K, "/SUB") && has(card_names(d), "SUB"),
              b.texts.empty() ? std::string("no box") : b.texts.back());
    }
    // SDMW-32
    {
        // a drop INTO a folder named with a code page 437 byte
        const std::string K = tmp_name("cp437") + ".img";
        make_card(K, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_cp437);
        Rig rig;
        rig.emu.sd_card().mount(K, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{K, sm::encode_card_dir("/D\x82R"), {}}, QString::fromStdString(host.string()));
        wire(d, b);
        std::unique_ptr<QMimeData> m(uri_list({(host / "h2.bin").string()}));
        const bool ok = d.drop_on_card(*m, false);
        check("SDMW-32", "a drop of host files into a folder named with a code page 437 byte copies them into that folder",
              ok && on_card(K, "/D\x82R/h2.bin") && !on_card(K, "/h2.bin"), b.texts.empty() ? std::string("no box") : b.texts.back());
    }
    // GH #558 (owner changes): the up row, the equal and aligned panes, the default size.
    auto press_enter = [](QListView* v) {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(v, &ev);
    };
    // SDMW-37
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* view = child<QListView>(d, "card_view");
        auto* model = qobject_cast<SdCardModel*>(view->model());
        const bool root_none = !model->has_up_row() && card_names(d).front() == "EMPTYD";
        enter_card(d, "GAMES");
        const std::vector<std::string> in_games = card_names(d);
        const QModelIndex up = model->index(0, 0);
        const bool shown = d.card_dir() == "/GAMES" && model->has_up_row() && model->is_up_row(0) && in_games.front() == ".." &&
                           !up.data(Qt::DecorationRole).value<QIcon>().isNull() && in_games.size() == 3;
        // never a target: not selectable, not draggable, no path, and a forced selection stays empty
        view->selectionModel()->select(up, QItemSelectionModel::Select);
        const bool inert = !(model->flags(up) & Qt::ItemIsSelectable) && !(model->flags(up) & Qt::ItemIsDragEnabled) &&
                           model->path_of(0).isEmpty() && view->selectionModel()->selectedIndexes().isEmpty() &&
                           !d.findChild<QPushButton*>("copy_to_host")->isEnabled() && !d.findChild<QAction*>("card_delete")->isEnabled() &&
                           !d.findChild<QAction*>("card_rename")->isEnabled() && model->mimeData({up}) == nullptr;
        // keyboard: Enter on a folder enters it, Enter on the up row goes to the parent
        view->setCurrentIndex(model->index(1, 0));   // SUB
        press_enter(view);
        const bool enter_dir = d.card_dir() == "/GAMES/SUB";
        const bool sub_has_up = card_names(d).front() == ".." && card_names(d).size() == 1;
        view->setCurrentIndex(model->index(0, 0));
        press_enter(view);
        const bool enter_up = d.card_dir() == "/GAMES";
        up_card(d);   // a double-click on the up row
        const bool dbl_up = d.card_dir() == "/";
        check("SDMW-37", "the card pane: no up row at the root, an up row first (arrow icon, '..') in every other folder; it is not selectable, draggable or a target of any action; a double-click and Enter on it go to the parent, and Enter on a folder enters it",
              root_none && shown && inert && enter_dir && sub_has_up && enter_up && dbl_up,
              std::string("root=") + std::to_string(root_none) + " shown=" + std::to_string(shown) + " inert=" + std::to_string(inert) + " enter=" +
                  std::to_string(enter_dir) + std::to_string(sub_has_up) + std::to_string(enter_up) + std::to_string(dbl_up));
    }
    // SDMW-38
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("up38");
        fs::create_directories(hd / "inner");
        write_host_file(hd / "x.bin", kX);
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, (hd / "inner").string()}, QString::fromStdString(host.string()));
        wire(d, b);
        auto* view = child<QListView>(d, "host_view");
        auto* model = qobject_cast<SdHostModel*>(view->model());
        pump_until([&] { return model->has_up_row(); });
        const bool shown = model->has_up_row() && model->rowCount() == 1 && model->index(0, 0).data().toString() == ".." &&
                           !model->index(0, 0).data(Qt::DecorationRole).value<QIcon>().isNull();
        const QModelIndex up = model->index(0, 0);
        view->selectionModel()->select(up, QItemSelectionModel::Select);
        const bool inert = !(model->flags(up) & Qt::ItemIsSelectable) && !(model->flags(up) & Qt::ItemIsDragEnabled) &&
                           model->file_path(up).isEmpty() && view->selectionModel()->selectedIndexes().isEmpty() &&
                           !d.findChild<QAction*>("host_delete")->isEnabled() && !d.findChild<QAction*>("host_rename")->isEnabled() &&
                           !d.findChild<QAction*>("host_copy")->isEnabled() && model->mimeData({up}) == nullptr;
        view->setCurrentIndex(model->index(0, 0));
        press_enter(view);   // Enter on the up row: the parent, which lists inner and x.bin after the up row
        pump_until([&] { return model->rowCount() >= 3; });
        const bool kb_up = d.host_dir() == QString::fromStdString(hd.string()) && model->rowCount() == 3;
        // a double-click on the folder, then on the up row again
        const QModelIndex inner = model->index_of_path(QString::fromStdString((hd / "inner").string()));
        Q_EMIT view->doubleClicked(inner);
        const bool entered = d.host_dir() == QString::fromStdString((hd / "inner").string());
        up_host(d);
        const bool dbl_up = d.host_dir() == QString::fromStdString(hd.string());
        // the filesystem root has no up row
        SdManagerDialog dr(rig.emu, sm::Remembered{{}, {}, QDir::rootPath().toStdString()}, QString::fromStdString(host.string()));
        wire(dr, b);
        auto* rmodel = qobject_cast<SdHostModel*>(child<QListView>(dr, "host_view")->model());
        const bool root_none = dr.host_dir() == QDir::rootPath() && !rmodel->has_up_row();
        check("SDMW-38", "the host pane: an up row first in every folder but the filesystem root (none there); it is not selectable, draggable or a target of any action; Enter and a double-click on it go to the parent, a double-click on a folder enters it",
              shown && inert && kb_up && entered && dbl_up && root_none,
              std::string("shown=") + std::to_string(shown) + " inert=" + std::to_string(inert) + " kb=" + std::to_string(kb_up) + " enter=" + std::to_string(entered) +
                  " up=" + std::to_string(dbl_up) + " root=" + std::to_string(root_none));
    }
    // SDMW-39
    {
        Img a(G), c2(F16);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        rig.emu.sd_card2().mount(c2.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        d.show();
        auto* card = child<QListView>(d, "card_view");
        auto* hostv = child<QListView>(d, "host_view");
        auto* combo = child<QComboBox>(d, "slot_combo");
        auto* ro = child<QCheckBox>(d, "read_only_box");
        auto* close = child<QPushButton>(d, "close_button");
        std::string detail;
        bool all = true;
        for (const QSize sz : {SdManagerDialog::default_size(), QSize(700, 400), QSize(1300, 800)}) {
            d.resize(sz);
            QCoreApplication::processEvents();
            const QPoint cp = card->mapTo(&d, QPoint(0, 0)), hp = hostv->mapTo(&d, QPoint(0, 0));
            const bool same_size = card->size() == hostv->size();
            const bool aligned = cp.y() == hp.y() && cp.y() + card->height() == hp.y() + hostv->height();
            // the selector and the Read-only box are in the bottom row, with Close (centres within 2 px), below the panes
            auto cy = [&](QWidget* w) { return w->mapTo(&d, QPoint(0, 0)).y() + w->height() / 2; };
            const bool bottom_row = std::abs(cy(combo) - cy(close)) <= 2 && std::abs(cy(ro) - cy(close)) <= 2 &&
                                    combo->mapTo(&d, QPoint(0, 0)).y() >= cp.y() + card->height();
            all = all && same_size && aligned && bottom_row;
            detail += std::to_string(sz.width()) + "x" + std::to_string(sz.height()) + ":" + std::to_string(same_size) + std::to_string(aligned) +
                      std::to_string(bottom_row) + " ";
        }
        check("SDMW-39", "the two panes are the same size with their top and bottom edges aligned at the default size and at a smaller and a larger one, and the slot selector and the Read-only box sit in the bottom row with Close",
              all, detail);
    }
    // SDMW-40
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        const QSize def = SdManagerDialog::default_size();
        check("SDMW-40", "the default window size is the earlier 900x520 plus 20% in each direction (1080x624), and a new window opens at it",
              def == QSize(1080, 624) && def.width() * 5 == 900 * 6 && def.height() * 5 == 520 * 6 && d.size() == def,
              std::to_string(d.width()) + "x" + std::to_string(d.height()));
    }
    // The main window's integration (GH #558, amended 2026-11-25): File > SD Card File Manager... and the toolbar button
    // open a MODELESS window, one instance, over a machine that keeps running; it never pauses or resets anything; what it
    // closed on is saved to jnext.conf.
    auto find_dialog = []() -> SdManagerDialog* {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* d = qobject_cast<SdManagerDialog*>(w)) return d;
        return nullptr;
    };
    // SDMW-33
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        MainWindow win;
        win.set_debugger(rig.dbg.get());
        win.set_emulator(&rig.emu);
        Boxes b;
        const fs::path hd = fresh_dir("mw33");
        AppConfigData want;
        want.sdmanager_host_dir = QString::fromStdString(hd.string());   // as --set sdcard_manager.host_dir=... would
        want.sdmanager_card = QString::fromStdString(a.p);
        want.sdmanager_card_dir = QString::fromStdString(sm::encode_card_dir("/GAMES"));
        win.apply_startup_config(want, false);
        win.sd_manager_action()->trigger();
        SdManagerDialog* d = find_dialog();
        const bool opened = d && d->isVisible() && !d->isModal() && d->windowModality() == Qt::NonModal;
        const bool running = !rig.dbg->state().paused;
        const bool on_cmdline_dir = d && d->host_dir() == QString::fromStdString(hd.string()) && d->card_dir() == QStringLiteral("/GAMES");
        if (d) wire(*d, b);
        win.sd_manager_action()->trigger();   // a second trigger while it is up raises the same window
        int dialogs = 0;
        for (QWidget* w : QApplication::topLevelWidgets()) if (qobject_cast<SdManagerDialog*>(w)) ++dialogs;
        const bool same = find_dialog() == d;
        if (d) {
            up_host(*d);   // the folder it closes on is NOT the command line's: the GUI change takes over
            d->accept();
        }
        QCoreApplication::processEvents();
        AppConfig saved(AppConfig::default_config_path());
        saved.load();
        // a second visit opens on what the first closed on, not on the command line's value
        win.sd_manager_action()->trigger();
        SdManagerDialog* d2 = find_dialog();
        const bool reopened_on_parent = d2 && d2->host_dir() == QString::fromStdString(hd.parent_path().string());
        if (d2) { wire(*d2, b); d2->accept(); }
        QCoreApplication::processEvents();
        check("SDMW-33", "File > SD Card File Manager... opens a MODELESS window over a running machine, once (a second trigger raises the same window); closing it with no change does nothing to the machine (zero cold boots) and saves the card and host folder it closed on, which the next visit opens on instead of a command-line value",
              opened && running && on_cmdline_dir && dialogs == 1 && same && !rig.dbg->state().paused && rig.boots == 0 && !find_dialog() &&
                  saved.data().sdmanager_card == QString::fromStdString(a.p) && saved.data().sdmanager_host_dir == QString::fromStdString(hd.parent_path().string()) &&
                  reopened_on_parent &&
                  saved.data().sdmanager_card_dir == QString::fromStdString(sm::encode_card_dir("/GAMES")),
              std::string("opened=") + (opened ? "y" : "n") + " running=" + (running ? "y" : "n") + " boots=" + std::to_string(rig.boots) + " card=" +
                  saved.data().sdmanager_card.toStdString() + " host=" + saved.data().sdmanager_host_dir.toStdString());
    }
    {
        // the command line gave card 0 and /MYDIR; the user switches to the second card and enters /GAMES, then closes: the
        // next visit opens on THAT (the GUI change takes over from the command-line card and card folder)
        Img a(G), c2(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        rig.emu.sd_card2().mount(c2.p, false);
        MainWindow win;
        win.set_debugger(rig.dbg.get());
        win.set_emulator(&rig.emu);
        Boxes b;
        AppConfigData want;
        want.sdmanager_card = QString::fromStdString(a.p);
        want.sdmanager_card_dir = QString::fromStdString(sm::encode_card_dir("/MYDIR"));
        win.apply_startup_config(want, false);
        win.sd_manager_action()->trigger();
        SdManagerDialog* d = find_dialog();
        const bool first_on_cmdline = d && d->slot() == 0 && d->card_dir() == QStringLiteral("/MYDIR");
        if (d) {
            wire(*d, b);
            child<QComboBox>(*d, "slot_combo")->setCurrentIndex(1);
            auto* card = child<QListView>(*d, "card_view");
            auto* model = qobject_cast<SdCardModel*>(card->model());
            int games = -1;
            for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "GAMES") games = r;
            if (games >= 0) Q_EMIT card->doubleClicked(model->index(games, 0));
            d->accept();
        }
        pump_until([&] { return !find_dialog() && !rig.dbg->state().paused; }, 3000);
        win.sd_manager_action()->trigger();
        SdManagerDialog* d2 = find_dialog();
        const bool reopened = d2 && d2->slot() == 1 && d2->card_dir() == QStringLiteral("/GAMES");
        if (d2) { wire(*d2, b); d2->accept(); }
        pump_until([&] { return !find_dialog() && !rig.dbg->state().paused; }, 3000);
        check("SDMW-36", "the card and card folder the window closed on replace the command-line ones for the next visit (second card, /GAMES instead of card 0, /MYDIR)",
              first_on_cmdline && reopened, std::string("first=") + (first_on_cmdline ? "y" : "n") + " slot=" + (d2 ? std::to_string(d2->slot()) : "none") +
                  " dir=" + (d2 ? d2->card_dir().toStdString() : ""));
    }
    // SDMW-49 (replaces SDMW-34 and SDMW-35: nothing resets or pauses any more)
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        MainWindow win;
        win.set_debugger(rig.dbg.get());
        win.set_emulator(&rig.emu);
        Boxes b;
        win.sd_manager_action()->trigger();
        SdManagerDialog* d = find_dialog();
        if (d) {
            wire(*d, b);
            b.say(QStringLiteral("NEWDIR"));
            d->findChild<QAction*>("card_new_folder")->trigger();   // a card change
            d->accept();
        }
        for (int i = 0; i < 3; ++i) rig.emu.run_frame();
        QCoreApplication::processEvents();
        const std::string msg = win.statusBar()->currentMessage().toStdString();
        check("SDMW-49", "a visit that changes the card and closes ends with zero cold boots, no reset requested, a machine that was never paused and no 'restarts' line on the status bar",
              d && on_card(a.p, "/NEWDIR") && rig.boots == 0 && !rig.emu.take_hard_reset_request() && !rig.dbg->state().paused &&
                  msg.find("restart") == std::string::npos,
              "boots=" + std::to_string(rig.boots) + " msg=" + msg);
    }
    // SDMW-47: modeless for real: the main window takes a key press while the file manager is open (the guest gets it) and the
    // machine's frames advance
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        MainWindow win;
        win.set_debugger(rig.dbg.get());
        win.set_emulator(&rig.emu);
        std::vector<int> guest_keys;
        win.set_key_callback([&](SDL_Scancode sc, bool pressed) { if (pressed) guest_keys.push_back(static_cast<int>(sc)); });
        win.show();
        QApplication::processEvents();
        Boxes b;
        win.sd_manager_action()->trigger();
        SdManagerDialog* d = find_dialog();
        if (d) wire(*d, b);
        const uint32_t f0 = rig.dbg->state().frame;
        QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QApplication::sendEvent(&win, &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
        QApplication::sendEvent(&win, &release);
        for (int i = 0; i < 3; ++i) rig.emu.run_frame();
        win.sd_manager_action()->trigger();
        int dialogs = 0;
        for (QWidget* w : QApplication::topLevelWidgets()) if (qobject_cast<SdManagerDialog*>(w)) ++dialogs;
        check("SDMW-47", "while the file manager is open the main window still takes key presses (the guest receives them), the machine's frames advance, and a second File > SD Card File Manager... raises the same window (one instance)",
              d && d->isVisible() && !d->isModal() && !guest_keys.empty() && rig.dbg->state().frame > f0 && dialogs == 1,
              "keys=" + std::to_string(guest_keys.size()) + " dialogs=" + std::to_string(dialogs));
        if (d) d->accept();
        QCoreApplication::processEvents();
    }
    // SDMW-45: the card pane FOLLOWS the card. Two arms: the guest writes (SPI CMD24 on the mounted device: the write
    // generation moves) and another program writes a plain image through its OWN file handle (size and time move, the
    // generation does not). A selected entry stays selected by name, and a poll that finds nothing changed lists nothing.
    {
        // the sectors a plain add of GUEST.BIN changes, taken from a scratch copy
        Img scratch(G);
        std::string perr;
        sdcard::add_to_image(scratch.p, (host / "h1.bin").string(), "/GUEST.BIN", false, perr, nullptr, true);
        std::vector<std::pair<uint32_t, Bytes>> changed;
        {
            std::ifstream f0(G, std::ios::binary), f1(scratch.p, std::ios::binary);
            std::vector<char> x(512), y(512);
            for (uint32_t sct = 0; f0.read(x.data(), 512) && f1.read(y.data(), 512); ++sct)
                if (x != y) changed.push_back({sct, Bytes(y.begin(), y.end())});
        }
        auto cmd = [](SdCardDevice& sd, uint8_t c, uint32_t arg) {
            (void)sd.receive(static_cast<uint8_t>(0x40 | c));
            for (int sh : {24, 16, 8, 0}) (void)sd.receive(static_cast<uint8_t>(arg >> sh));
            (void)sd.receive(0x95);
            for (int i = 0; i < 16; ++i) {
                const uint8_t r1 = sd.send();
                if (r1 != 0xFF) return r1;
            }
            return static_cast<uint8_t>(0xFF);
        };
        auto guest_write = [&](SdCardDevice& sd, uint32_t sector, const Bytes& data) {
            if (cmd(sd, 24, sector) != 0x00) return false;
            (void)sd.receive(0xFE);
            for (int i = 0; i < 512; ++i) (void)sd.receive(data[static_cast<size_t>(i)]);
            (void)sd.receive(0xFF);
            (void)sd.receive(0xFF);
            for (int i = 0; i < 32; ++i)
                if (sd.send() != 0xFF) break;
            sd.deselect();
            return true;
        };
        Img a(G);
        // a writes file is bound: the guest's writes go into it and the IMAGE does not change, so only the write generation
        // can tell the pane (a plain image's time would move with every guest write)
        const std::string jf45 = (fresh_dir("sw45") / "w.sdw").string();
        {
            sd_write_journal::Binding bd;
            bd.bytes = fs::file_size(a.p);
            bd.sha256.fill(0x45);
            std::string why;
            sd_write_journal::bind(a.p, jf45, bd, false, why);
        }
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        SdCardDevice& dev = rig.emu.sd_card();
        (void)cmd(dev, 0, 0); (void)cmd(dev, 8, 0x1AA); (void)cmd(dev, 55, 0); (void)cmd(dev, 41, 0x40000000); (void)cmd(dev, 58, 0);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        select_card(d, {"FILE.BIN"});
        const int r0 = d.card_reloads_for_test();
        d.check_card_change();
        d.check_card_change();
        const bool quiet = d.card_reloads_for_test() == r0;                                   // nothing changed: nothing listed
        bool wrote = !changed.empty();
        for (const auto& ch : changed) wrote = wrote && guest_write(dev, ch.first, ch.second);
        d.check_card_change();
        const bool guest_arm = wrote && has(card_names(d), "GUEST.BIN") && d.card_reloads_for_test() == r0 + 1 && sha_file(a.p) == sha_g;
        auto* view = child<QListView>(d, "card_view");
        const QModelIndexList sel = view->selectionModel()->selectedIndexes();
        const bool kept = sel.size() == 1 && sel.first().data().toString() == "FILE.BIN";    // still selected, by name
        d.check_card_change();
        const bool settled = d.card_reloads_for_test() == r0 + 1;
        // the outside arm: a SEPARATE file handle rewrites the directory sectors of a copy that has the file removed
        Img b2(G);
        Rig rig2;
        rig2.emu.sd_card().mount(b2.p, false);
        SdManagerDialog d2(rig2.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d2, b);
        const bool before_has = has(card_names(d2), "GUEST.BIN");
        const auto gen0 = SdCardDevice::host_write_generation();
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));   // the image's time must be able to move
            std::fstream f(b2.p, std::ios::in | std::ios::out | std::ios::binary);
            for (const auto& ch : changed) {
                f.seekp(static_cast<std::streamoff>(ch.first) * 512);
                f.write(reinterpret_cast<const char*>(ch.second.data()), 512);
            }
        }
        const bool gen_still = SdCardDevice::host_write_generation() == gen0;
        const int r2 = d2.card_reloads_for_test();
        d2.check_card_change();
        sd_write_journal::forget_image(a.p);
        const bool outside_arm = !before_has && gen_still && has(card_names(d2), "GUEST.BIN") && d2.card_reloads_for_test() == r2 + 1;
        check("SDMW-45", "the card pane re-lists by itself when the guest writes (write generation) and when another program rewrites a plain image (size and time, no generation), keeps a selected entry selected by name, and a poll that finds nothing changed lists nothing",
              quiet && guest_arm && kept && settled && outside_arm,
              std::string(quiet ? "1" : "0") + (guest_arm ? "1" : "0") + (kept ? "1" : "0") + (settled ? "1" : "0") + (outside_arm ? "1" : "0") +
                  " reloads=" + std::to_string(d.card_reloads_for_test()));
    }
    // SDMW-46: no action on a stale listing (the window twin of SDMT-37)
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        select_card(d, {"FILE.BIN"});
        // the guest renames it away and creates another FILE.BIN of another size, after the pane listed
        std::string err;
        const bool renamed = sdcard::cardops::rename(a.p, "/FILE.BIN", "OLD.BIN", err) == sdcard::FileAddStatus::Ok;
        const bool made = sdcard::add_to_image(a.p, (host / "h2.bin").string(), "/FILE.BIN", false, err, nullptr, true) == sdcard::FileAddStatus::Ok;
        const std::string sha = sha_file(a.p);
        b.answer = [](const QString&) { return QMessageBox::Yes; };
        d.findChild<QAction*>("card_delete")->trigger();
        const bool message = !b.texts.empty() && b.texts.back().find("changed on the card since it was listed") != std::string::npos;
        const bool nothing = sha_file(a.p) == sha && on_card(a.p, "/FILE.BIN") && on_card(a.p, "/OLD.BIN");
        const bool refreshed = has(card_names(d), "OLD.BIN") && has(card_names(d), "FILE.BIN");   // the pane shows the current listing
        check("SDMW-46", "a Delete on an entry that changed since it was listed (renamed away and re-created under its name) refuses with 'changed on the card since it was listed', removes nothing, and the pane then shows the current listing",
              renamed && made && message && nothing && refreshed, b.texts.empty() ? std::string("no box") : b.texts.back());
    }
    // SDMW-48: inert while a card operation owns the card
    {
        Img a(G), c2(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, sm::Remembered{a.p, sm::encode_card_dir("/GAMES"), {}}, QString::fromStdString(host.string()));
        wire(d, b);
        bool busy = true;
        d.set_card_busy([&] { return busy; });
        const auto at0 = fatfs_glue::attach_count_for_test();
        for (int i = 0; i < 3; ++i) d.check_card_change();                // three poll intervals
        d.refresh();
        d.findChild<QAction*>("card_rename")->trigger();
        d.findChild<QAction*>("card_delete")->trigger();
        d.findChild<QAction*>("card_new_folder")->trigger();
        d.findChild<QAction*>("card_refresh")->trigger();
        d.copy_to_host();
        d.copy_to_card();
        const bool no_calls = fatfs_glue::attach_count_for_test() == at0 && b.titles.empty();
        bool off = true;
        for (const char* n : {"card_copy", "card_rename", "card_delete", "card_new_folder", "card_refresh"})
            off = off && !d.findChild<QAction*>(QString::fromLatin1(n))->isEnabled();
        off = off && !child<QPushButton>(d, "copy_to_host")->isEnabled() && !child<QPushButton>(d, "copy_to_card")->isEnabled() &&
              !child<QComboBox>(d, "slot_combo")->isEnabled();
        const bool says = child<QLabel>(d, "card_note")->text().contains(QStringLiteral("being changed by another action"));
        // the card is swapped meanwhile; when the operation ends the pane resolves again, at the new card's root
        rig.emu.sd_card().unmount();
        rig.emu.sd_card().mount(c2.p, false);
        busy = false;
        d.check_card_change();
        const bool resolved = fatfs_glue::attach_count_for_test() > at0 && d.card_dir() == QStringLiteral("/") && d.slot() == 0 &&
                              d.findChild<QAction*>("card_new_folder")->isEnabled();
        check("SDMW-48", "while a card operation owns the card the window makes no card call over three poll intervals and every action, whatever it is aimed at, does nothing (no FatFs attach, no box), the card actions and the selector are disabled and the note says so; when it ends the pane resolves again, at the root if the card changed",
              no_calls && off && says && resolved,
              std::string(no_calls ? "1" : "0") + (off ? "1" : "0") + (says ? "1" : "0") + (resolved ? "1" : "0"));
    }
    // SDMW-53: a drag carries the stamps of what was listed, so a drop of a stale entry is refused (the drag twin of SDMW-46)
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path out = fresh_dir("drag-stale");
        SdManagerDialog d(rig.emu, sm::Remembered{{}, {}, out.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        auto* model = qobject_cast<SdCardModel*>(child<QListView>(d, "card_view")->model());
        int row = -1;
        for (int r = 0; r < model->rowCount(); ++r) if (model->index(r, 0).data().toString() == "FILE.BIN") row = r;
        std::unique_ptr<QMimeData> m(row >= 0 ? model->mimeData({model->index(row, 0)}) : nullptr);
        QString img;
        QStringList paths, stamps;
        const bool carries = m && SdCardModel::decode_items(m->data(QString::fromLatin1(SdCardModel::kMime)), img, paths, &stamps) &&
                             stamps.size() == 1 && !stamps[0].isEmpty();
        // after the drag started, the guest renames it away and creates another FILE.BIN of another size
        std::string err;
        const bool renamed = sdcard::cardops::rename(a.p, "/FILE.BIN", "OLD.BIN", err) == sdcard::FileAddStatus::Ok;
        const bool made = sdcard::add_to_image(a.p, (host / "h2.bin").string(), "/FILE.BIN", false, err, nullptr, true) == sdcard::FileAddStatus::Ok;
        const std::string sha = sha_file(a.p);
        const bool copy_refused = m && !d.drop_on_host(*m, false);
        const bool move_refused = m && !d.drop_on_host(*m, true);
        const bool message = !b.texts.empty() && b.texts.back().find("changed on the card since it was listed") != std::string::npos;
        const bool nothing = tree_of(out).empty() && sha_file(a.p) == sha && on_card(a.p, "/FILE.BIN") && on_card(a.p, "/OLD.BIN");
        check("SDMW-53", "a drag payload carries the stamp of each listed entry; dropping it on the host pane after the entry changed (renamed away and re-created under its name) is refused with 'changed on the card since it was listed', copy and move, and nothing is extracted or removed",
              carries && renamed && made && copy_refused && move_refused && message && nothing, b.texts.empty() ? std::string("no box") : b.texts.back());
    }
    // SDMW-54 / 55 / 56: the pane keeps its scroll position on a re-list, re-lists when the window is activated, and Refresh is F5
    {
        Img a(G);
        std::string err;
        for (int i = 0; i < 70; ++i) {
            char nm[32];
            std::snprintf(nm, sizeof nm, "/F%02d.BIN", i);
            sdcard::add_to_image(a.p, (host / "h2.bin").string(), nm, false, err, nullptr, true);
        }
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        d.resize(700, 400);
        d.show();
        QCoreApplication::processEvents();
        auto* sb = child<QListView>(d, "card_view")->verticalScrollBar();
        sb->setValue(sb->maximum() / 2);
        const int scrolled = sb->value();
        // another program changes the image (size and time move, no write generation): the poll re-lists it
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool added = sdcard::add_to_image(a.p, (host / "h2.bin").string(), "/NEW.BIN", false, err, nullptr, true) == sdcard::FileAddStatus::Ok;
        const int r0 = d.card_reloads_for_test();
        d.check_card_change();
        QCoreApplication::processEvents();   // the view lays the new rows out (its scroll range is settled there)
        const bool relisted = d.card_reloads_for_test() == r0 + 1 && has(card_names(d), "NEW.BIN");
        check("SDMW-54", "a re-list of a long folder keeps the card pane's scroll position",
              scrolled > 0 && added && relisted && sb->value() == scrolled,
              "scrolled=" + std::to_string(scrolled) + " now=" + std::to_string(sb->value()) + " max=" + std::to_string(sb->maximum()));
        // bringing the window to the front lists the folder again, whatever the poll saw
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const bool added2 = sdcard::add_to_image(a.p, (host / "h2.bin").string(), "/LATE.BIN", false, err, nullptr, true) == sdcard::FileAddStatus::Ok;
        const int r1 = d.card_reloads_for_test();
        d.activateWindow();
        QEvent ev(QEvent::ActivationChange);
        QCoreApplication::sendEvent(&d, &ev);
        const bool active = d.isActiveWindow();
        check("SDMW-55", "bringing the window to the front lists the card folder again at once (the activation re-list)",
              added2 && active && d.card_reloads_for_test() > r1 && has(card_names(d), "LATE.BIN"),
              std::string(active ? "active " : "inactive ") + "reloads " + std::to_string(r1) + "->" + std::to_string(d.card_reloads_for_test()));
        // Refresh is F5 in both panes, and it lists the folder again
        QAction* cr = d.findChild<QAction*>("card_refresh");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        sdcard::add_to_image(a.p, (host / "h2.bin").string(), "/F5.BIN", false, err, nullptr, true);
        const int r2 = d.card_reloads_for_test();
        if (cr) cr->trigger();
        check("SDMW-56", "the card pane's Refresh action is bound to F5 and lists the folder again",
              cr && cr->shortcut() == QKeySequence(Qt::Key_F5) && d.card_reloads_for_test() > r2 && has(card_names(d), "F5.BIN"),
              cr ? cr->shortcut().toString().toStdString() : "no action");
    }
    // SDMW-52: a title over each pane says what it is, and the panes stay equal and aligned
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        d.show();
        QCoreApplication::processEvents();
        auto* ct = child<QLabel>(d, "card_title");
        auto* ht = child<QLabel>(d, "host_title");
        auto* cv = child<QListView>(d, "card_view");
        auto* hv = child<QListView>(d, "host_view");
        auto* cp = child<QLabel>(d, "card_path");
        auto* hp = child<QLabel>(d, "host_path");
        auto top = [&](QWidget* w) { return w->mapTo(&d, QPoint(0, 0)).y(); };
        auto left = [&](QWidget* w) { return w->mapTo(&d, QPoint(0, 0)).x(); };
        const bool text = ct && ht && ct->text().contains(QStringLiteral("SD Card")) && ht->text().contains(QStringLiteral("Host filesystem")) &&
                          !ct->text().contains(QStringLiteral("Host")) && !ht->text().contains(QStringLiteral("SD"));
        // each title is above its own pane (title, then the path line, then the list), in its own column
        const bool order = ct && ht && top(ct) < top(cp) && top(cp) < top(cv) && top(ht) < top(hp) && top(hp) < top(hv) &&
                           left(ct) < left(ht) && std::abs(left(ct) - left(cv)) <= 4 && std::abs(left(ht) - left(hv)) <= 4;
        const bool aligned = ct && ht && top(ct) == top(ht) && top(cv) == top(hv) && cv->size() == hv->size() &&
                             top(cv) + cv->height() == top(hv) + hv->height();
        check("SDMW-52", "the card pane is titled 'SD Card' and the host pane 'Host filesystem', each title above its own pane's path line and list, and the two panes (titles, lists) stay the same size and aligned top and bottom",
              text && order && aligned,
              std::string("text=") + (text ? "1" : "0") + " order=" + (order ? "1" : "0") + " aligned=" + (aligned ? "1" : "0"));
    }
    // SDMW-51: the Read-only box forced on for a card jnext cannot write gives the user's own choice back when a writable card is shown again
    {
        Img a(G), f16(F16);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        rig.emu.sd_card2().mount(f16.p, false);
        Boxes b;
        SdManagerDialog d(rig.emu, none_remembered(), QString::fromStdString(host.string()));
        wire(d, b);
        auto* ro = child<QCheckBox>(d, "read_only_box");
        auto* combo = child<QComboBox>(d, "slot_combo");
        const bool start = !ro->isChecked() && ro->isEnabled();                 // a writable card: unticked, usable
        combo->setCurrentIndex(1);                                              // the FAT16 card: forced on and greyed
        const bool forced = ro->isChecked() && !ro->isEnabled();
        combo->setCurrentIndex(0);                                              // back: the user's choice (unticked)
        const bool unticked = !ro->isChecked() && ro->isEnabled();
        ro->setChecked(true);                                                   // the user ticks it,
        combo->setCurrentIndex(1);
        combo->setCurrentIndex(0);                                              // and it is still ticked after a visit to the other card
        const bool still_ticked = ro->isChecked() && ro->isEnabled();
        check("SDMW-51", "the Read-only box is forced on and greyed for a card jnext cannot write and gives the user's own choice back (unticked, or ticked) when a writable card is shown again",
              start && forced && unticked && still_ticked,
              std::string(start ? "1" : "0") + (forced ? "1" : "0") + (unticked ? "1" : "0") + (still_ticked ? "1" : "0"));
    }
    // SDMW-50: the host pane matches the card pane: the same icon for a file and for a folder, and the same row height
    {
        Img a(G);
        Rig rig;
        rig.emu.sd_card().mount(a.p, false);
        Boxes b;
        const fs::path hd = fresh_dir("look");
        fs::create_directories(hd / "dirx");
        write_host_file(hd / "filex.bin", kX);
        SdManagerDialog d(rig.emu, sm::Remembered{a.p, sm::encode_card_dir("/GAMES"), hd.string()}, QString::fromStdString(host.string()));
        wire(d, b);
        d.show();
        auto* cv = child<QListView>(d, "card_view");
        auto* hv = child<QListView>(d, "host_view");
        auto* hm = qobject_cast<SdHostModel*>(hv->model());
        pump_until([&] { return hm->rowCount() >= 3; });
        QCoreApplication::processEvents();
        auto row_of = [](QAbstractItemView* v, const char* text) {
            for (int r = 0; r < v->model()->rowCount(); ++r)
                if (v->model()->index(r, 0).data().toString() == text) return v->model()->index(r, 0);
            return QModelIndex();
        };
        const QModelIndex cf = row_of(cv, "WOTEF.NEX"), cd = row_of(cv, "SUB"), hf = row_of(hv, "filex.bin"), hdir = row_of(hv, "dirx");
        auto icon_ok = [](const QModelIndex& i) { return i.isValid() && !i.data(Qt::DecorationRole).value<QIcon>().isNull(); };
        const int h = cv->visualRect(cf).height();
        const bool heights = h > 0 && cv->visualRect(cd).height() == h && hv->visualRect(hf).height() == h && hv->visualRect(hdir).height() == h &&
                             cv->visualRect(cv->model()->index(0, 0)).height() == h && hv->visualRect(hv->model()->index(0, 0)).height() == h;
        const bool icons = icon_ok(cf) && icon_ok(cd) && icon_ok(hf) && icon_ok(hdir);
        check("SDMW-50", "both panes show an icon on a file row and on a folder row and every row (the up row included) has the same height in both panes",
              heights && icons, "h=" + std::to_string(h) + " heights=" + std::to_string(heights) + " icons=" + std::to_string(icons));
    }
    check("SDMW-18", "the fixture cards are real volumes of the types the rows claim (FAT32 partitioned, FAT16 partitioned)", gt == FS_FAT32 && t16 == FS_FAT16);
    sd_write_journal::forget_all_for_test();
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
