// The backup an upgrade leaves, and the menus and verbs around it (GH #555 S6): go back (a swap of
// the current set and previous/), delete the backup, the status of the card set and the File-menu
// truth table.
//
// Oracles: the design in the issue and its plan (section 3, S5): what is carried, what the new
// distribution wins, where every file ends up, what a failure leaves behind. Cards are read back
// through a FOREIGN reader (the hand-rolled fat32_read_tree shares no code with FatFs); the user's
// changes are made through the real write paths. No network: the "download" copies a zip built here.
//
// Fixtures are ~35 MB sparse cards (512-byte clusters), made here; the swap only moves files, so the
// cards need not be real distributions. Writes files are real journals bound to their card.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/fat32_image.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_provisioner.h"
#include "core/sdcard_upgrade.h"
#include "core/sdcard_backup.h"
#include "core/sha256.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"
#include "../scratch_dir.h"
#include "../test_portable.h"

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "third_party/fatfs/ff.h"

namespace fs = std::filesystem;
namespace carry = sdcard::carry;
using Bytes = std::vector<uint8_t>;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

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

constexpr uint32_t kLba = 63, kSectors = 68000;
fs::path g_dir;
int g_n = 0;
std::string tmp(const std::string& stem) { return (g_dir / (stem + "-" + std::to_string(++g_n))).string(); }
Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }
void wr32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

Fat32Node fnode(const std::string& name, const std::string& content) {
    Fat32Node n; n.name = name; n.data = bytes_of(content); return n;
}

bool make_card(const std::string& path, const Fat32Tree& tree) {
    const uint64_t bytes = static_cast<uint64_t>(kLba + kSectors) * 512;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::vector<uint8_t> mbr(512, 0);
        uint8_t* pe = mbr.data() + 0x1BE;
        pe[4] = 0x0C; wr32(pe + 8, kLba); wr32(pe + 12, kSectors);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        f.write(reinterpret_cast<char*>(mbr.data()), 512);
        f.seekp(static_cast<std::streamoff>(bytes - 1));
        char z = 0; f.write(&z, 1);
        if (!f.good()) return false;
    }
    std::string err;
    Fat32WriteOptions o; o.au_size = 512;
    return fat32_format_and_populate(path, kLba, kSectors, tree, err, o);
}

void put_text(const std::string& p, const std::string& t) { std::ofstream(p, std::ios::trunc | std::ios::binary) << t; }
uint64_t digest(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    uint64_t h = 1469598103934665603ull;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        for (std::streamsize i = 0; i < f.gcount(); ++i) { h ^= static_cast<uint8_t>(buf[i]); h *= 1099511628211ull; }
    }
    return h;
}

// A card's files by upper-cased path, read by the foreign reader.

struct Edit {
    std::unique_ptr<carry::Volume> v;
    Edit(const std::string& card, std::shared_ptr<sd_write_journal::Journal> j = nullptr) {
        std::string e; v = carry::Volume::open(0, card, j, false, e);
    }
    void put(const std::string& p, const std::string& content) {
        v->mark_dirty();
        FIL fil{};
        if (f_open(&fil, v->fat(p).c_str(), FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw = 0; f_write(&fil, content.data(), static_cast<UINT>(content.size()), &bw); f_close(&fil);
        }
    }
    void mkdir(const std::string& p) { v->mark_dirty(); f_mkdir(v->fat(p).c_str()); }
};

Fat32Tree small_tree(const std::string& marker) {
    Fat32Tree t;
    t.root.push_back(fnode("MARKER.TXT", marker));
    return t;
}

std::string g_cfg, g_dir_sd;
std::string card() { return g_dir_sd + "/" + sdcard::kFixedImageFileName; }
std::string prev(const std::string& n) { return g_dir_sd + "/previous/" + n; }

// A card set in `where` ("" = the default dir, else a sub dir name) is written by `put_set`.
// One set = card (a real small FAT32 card), raw, their sidecars, and one writes file (a real journal).
void make_set(const std::string& base, const std::string& distro, const std::string& marker, const std::string& ov_file,
              const std::string& ov_content) {
    std::error_code ec;
    fs::create_directories(base, ec);
    const std::string c = base + "/" + sdcard::kFixedImageFileName, r = base + "/" + sdcard::kImageFileName;
    make_card(c, small_tree(marker));
    put_text(r, "raw-" + marker);
    put_text(r + ".sha256", "sha-" + marker + "\n");
    put_text(r + sdcard::kDistroSuffix, distro + "\n");
    put_text(c + sdcard::kDistroSuffix, distro + "\n");
    put_text(c + sdcard::kFixedImageRecipeSuffix, "3\n");
    if (!ov_file.empty()) {
        sd_write_journal::Binding b;
        b.bytes = fs::file_size(c, ec);
        b.mtime = static_cast<int64_t>(fs::last_write_time(c, ec).time_since_epoch().count());
        std::string why;
        sd_write_journal::bind(c, ov_file, b, true, why);
        auto j = sd_write_journal::for_image(c);
        { Edit e(c, j); e.put("/OV.TXT", ov_content); }
        sd_write_journal::forget_image(c);
        // loading a writes file compacts superseded records: do that once now, so a later load changes no byte
        sd_write_journal::bind(c, ov_file, b, false, why);
        sd_write_journal::forget_image(c);
    }
}

// The set's files as one comparable string.
std::string fingerprint(const std::string& base, const std::string& ov_file) {
    std::string s;
    for (const std::string& n : {std::string(sdcard::kFixedImageFileName), std::string(sdcard::kFixedImageFileName) + sdcard::kDistroSuffix,
                                 std::string(sdcard::kImageFileName), std::string(sdcard::kImageFileName) + ".sha256"})
        s += n + ":" + std::to_string(digest(base + "/" + n)) + ";";
    if (!ov_file.empty()) s += "ov:" + std::to_string(digest(ov_file)) + ";";
    return s;
}

// The default dir holds the CURRENT set (99.99, with overlay `ovc`) and previous/ the BACKUP (24.11, overlay `ovb`).
struct Pair { std::string ovc, ovb, cur_before, prev_before; uint64_t ovc_digest = 0, ovb_digest = 0; };
Pair make_pair() {
    std::error_code ec;
    fs::remove_all(g_dir_sd, ec);
    Pair p;
    p.ovc = (g_dir / "cur.jsdw").string();
    p.ovb = (g_dir / "old.jsdw").string();
    fs::remove(p.ovc, ec);
    fs::remove(p.ovb, ec);
    // the backup set is built beside, then moved into previous/ with its overlay recorded as an upgrade does
    const std::string stage = tmp("stage");
    make_set(stage, "sn-emulator-24.11.zip", "OLD", p.ovb, "old-overlay");
    fs::create_directories(g_dir_sd, ec);
    fs::create_directories(g_dir_sd + "/previous/overlays", ec);
    for (const auto& e : fs::directory_iterator(stage, ec)) fs::rename(e.path(), g_dir_sd + "/previous/" + e.path().filename().string(), ec);
    fs::rename(p.ovb, g_dir_sd + "/previous/overlays/0-old.jsdw", ec);
    put_text(g_dir_sd + "/previous/overlays/LIST", "0-old.jsdw\t" + p.ovb + "\n");
    make_set(g_dir_sd, "sn-emulator-99.99.zip", "NEW", p.ovc, "new-overlay");
    sdcard::upgrade::register_overlay(g_dir_sd, p.ovc);
    p.ovc_digest = digest(p.ovc);
    p.ovb_digest = digest(g_dir_sd + "/previous/overlays/0-old.jsdw");
    p.cur_before = fingerprint(g_dir_sd, p.ovc);
    // the backup's overlay lives in previous/overlays: fingerprint its set with that file
    p.prev_before = fingerprint(g_dir_sd + "/previous", g_dir_sd + "/previous/overlays/0-old.jsdw");
    return p;
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

int main() {
    using namespace sdcard::backup;
    g_dir = make_scratch_dir("jnext-backup");
    g_cfg = (g_dir / "cfg").string();
    jtp::set_env("JNEXT_CONFIG_DIR", g_cfg.c_str());
    jtp::make_dir(g_cfg.c_str());
    g_dir_sd = sdcard::default_sdcard_dir();
    std::string err;

    // ---- SDBAK-01/02: the swap ------------------------------------------------------------------
    {
        Pair p = make_pair();
        const Result r1 = go_back(g_dir_sd, err);
        const std::string now_cur = fingerprint(g_dir_sd, p.ovb);                       // the old set, its overlay back at its path
        const std::string now_prev = fingerprint(g_dir_sd + "/previous", g_dir_sd + "/previous/overlays/0-cur.jsdw");
        const bool swapped = r1 == Result::Ok && now_prev.substr(0, now_prev.find(";ov:")) == p.cur_before.substr(0, p.cur_before.find(";ov:"));
        const std::string declined = [&] { std::ifstream f(card() + sdcard::kDeclinedSuffix); std::string l; std::getline(f, l); while (!l.empty() && l.back() == '\r') l.pop_back(); return l; }();
        check("SDBAK-01A", "go back swaps the sets: the old card, original image and sidecars are the current ones, the card that was current is the backup, byte-identical",
              swapped && now_cur.substr(0, now_cur.find(";ov:")) == p.prev_before.substr(0, p.prev_before.find(";ov:")) &&
                  !fs::exists(g_dir_sd + "/go-back-work"), err);
        check("SDBAK-01B", "the overlays swap too: the old writes file is back at its path with its bytes, the current one is in previous/overlays",
              fs::exists(p.ovb) && !fs::exists(p.ovc) && digest(p.ovb) == p.ovb_digest &&
                  fs::exists(g_dir_sd + "/previous/overlays/0-cur.jsdw") && digest(g_dir_sd + "/previous/overlays/0-cur.jsdw") == p.ovc_digest,
              "ovb=" + std::to_string(fs::exists(p.ovb)) + " ovc=" + std::to_string(fs::exists(p.ovc)) + " ovbd=" + std::to_string(digest(p.ovb) == p.ovb_digest) +
                  " prevcur=" + std::to_string(fs::exists(g_dir_sd + "/previous/overlays/0-cur.jsdw")) + " list=" + [&] {
                      std::ifstream f(g_dir_sd + "/overlays.list"); std::string l, a; while (std::getline(f, l)) a += l + "|"; return a; }());
        check("SDBAK-02", "go back records the distribution it left as declined, so the next start does not offer it again",
              declined == "sn-emulator-99.99.zip", declined);
        const Result r2 = go_back(g_dir_sd, err);
        check("SDBAK-01C", "a second go back restores the original arrangement: the current set, its overlay, the backup and its overlay are byte-identical to the start",
              r2 == Result::Ok && fingerprint(g_dir_sd, p.ovc) == p.cur_before &&
                  fingerprint(g_dir_sd + "/previous", g_dir_sd + "/previous/overlays/0-old.jsdw") == p.prev_before, err);
    }

    // ---- SDBAK-03/04: delete and size ----------------------------------------------------------------
    {
        Pair p = make_pair();
        std::error_code ec;
        fs::create_directories(g_dir_sd + "/test-suite", ec);
        put_text(g_dir_sd + "/test-suite/keep.txt", "k");
        fs::create_directories(g_dir_sd + "/upgrade-work", ec);
        put_text(g_dir_sd + "/upgrade-work/w.txt", "w");
        uint64_t sum = 0;
        for (const auto& e : fs::recursive_directory_iterator(g_dir_sd + "/previous", ec))
            if (e.is_regular_file(ec)) sum += static_cast<uint64_t>(e.file_size(ec));
        const Status st = card_set_status(g_dir_sd, "sn-emulator-99.99.zip");
        check("SDBAK-04", "the backup size is the sum of the files of previous/, and the status names the backup's distribution",
              st.backup && st.backup_bytes == sum && sum > 0 && st.backup_distro == "sn-emulator-24.11.zip", std::to_string(st.backup_bytes) + " vs " + std::to_string(sum));
        const Result r = delete_backup(g_dir_sd, err);
        check("SDBAK-03", "delete removes previous/ only: the card, test-suite/ and upgrade-work/ are still there",
              r == Result::Ok && !fs::exists(g_dir_sd + "/previous") && fs::exists(card()) && fs::exists(g_dir_sd + "/test-suite/keep.txt") &&
                  fs::exists(g_dir_sd + "/upgrade-work/w.txt") && fs::exists(p.ovc), err);
        std::string e1, e2;
        check("SDBAK-06", "go back and delete with no backup return NoBackup and change nothing",
              go_back(g_dir_sd, e1) == Result::NoBackup && delete_backup(g_dir_sd, e2) == Result::NoBackup && fs::exists(card()) && has(e1, "no previous") && has(e2, "no previous"));
    }

    // ---- SDBAK-05A..D: a go back interrupted at each step is finished by the next start ------------------
    for (int step = 1; step <= 4; ++step) {
        Pair p = make_pair();
        g_fail_step_for_test = step;
        const Result r1 = go_back(g_dir_sd, err);
        const bool recovered = recover(g_dir_sd, err);
        const std::string now_cur = fingerprint(g_dir_sd, p.ovb);
        const char* id = step == 1 ? "SDBAK-05A" : step == 2 ? "SDBAK-05B" : step == 3 ? "SDBAK-05C" : "SDBAK-05D";
        check(id, "a go back interrupted at this step (1..4 = 05A..05D) is finished by recovery: the old set is current, the old current set is the backup",
              r1 == Result::Failed && recovered && now_cur.substr(0, now_cur.find(";ov:")) == p.prev_before.substr(0, p.prev_before.find(";ov:")) &&
                  fs::exists(prev(sdcard::kFixedImageFileName)) && !fs::exists(g_dir_sd + "/go-back-work") && fs::exists(p.ovb),
              "step " + std::to_string(step) + " " + err);
    }

    // ---- SDBAK-08..10: a restore target that is missing or unwritable ---------------------------------------------
    // The set as a whole, to prove "nothing changed".
    auto whole = [&] {
        std::string t;
        for (const auto& e : fs::recursive_directory_iterator(g_dir_sd)) {
            if (!e.is_regular_file()) continue;
            std::error_code ec;
            t += e.path().string() + ":" + std::to_string(digest(e.path().string())) + ";";
        }
        return t;
    };
    auto retarget = [&](const std::string& target) {   // the backup's writes file is to be restored to `target`
        std::ofstream(g_dir_sd + "/previous/overlays/LIST") << "0-old.jsdw\t" << target << "\n";
    };
    {
        // 08A: the directory is gone: the verb creates it and restores the file
        Pair p = make_pair();
        const std::string gone = (g_dir / "vanished-project").string() + "/dev.jsdw";
        std::error_code ec; fs::remove_all(fs::path(gone).parent_path(), ec);
        retarget(gone);
        const Result r = go_back(g_dir_sd, err);
        check("SDBAK-08A", "go back creates the missing directory of a writes file it restores, and restores the file with its bytes",
              r == Result::Ok && fs::exists(gone) && digest(gone) == p.ovb_digest && !fs::exists(g_dir_sd + "/go-back-work"), err);
        // 08B: the same through recovery: the directory is removed again after the swap was stopped before step 2
        Pair q = make_pair();
        retarget(gone);
        fs::remove_all(fs::path(gone).parent_path(), ec);
        g_fail_step_for_test = 2;
        const Result r1 = go_back(g_dir_sd, err);
        fs::remove_all(fs::path(gone).parent_path(), ec);
        const bool rec = recover(g_dir_sd, err);
        check("SDBAK-08B", "recovery creates a restore directory that vanished after step 1 and restores the file",
              r1 == Result::Failed && rec && fs::exists(gone) && digest(gone) == q.ovb_digest && fs::exists(prev(sdcard::kFixedImageFileName)), err);
        fs::remove_all(fs::path(gone).parent_path(), ec);
    }
    {
        // 09A: a restore target nothing can be written to (its parent is a FILE): refused before anything moves
        Pair p = make_pair();
        const std::string blocker = (g_dir / "blocker-file").string();
        { std::ofstream(blocker) << "i am a file"; }
        retarget(blocker + "/dev.jsdw");
        const std::string before = whole();
        std::string e9;
        const Result r = go_back(g_dir_sd, e9);
        check("SDBAK-09A", "go back refuses, with nothing changed, when a writes file cannot be restored to its path: the set is byte-identical and no scratch is left",
              r == Result::Failed && whole() == before && !fs::exists(g_dir_sd + "/go-back-work") && e9.find("dev.jsdw") != std::string::npos, e9);
        (void)p;
    }
    {
        // 09B: the target was fine when the swap began and is not when it is finished: recovery finishes, keeps the
        // copy in the backup and says where
        Pair p = make_pair();
        const std::string dirx = (g_dir / "later-blocked").string();
        std::error_code ec; fs::remove_all(dirx, ec);
        fs::create_directories(dirx, ec);
        retarget(dirx + "/dev.jsdw");
        g_fail_step_for_test = 2;
        const Result r1 = go_back(g_dir_sd, err);
        fs::remove_all(dirx, ec);
        { std::ofstream(dirx) << "now a file"; }   // the directory became a file
        const bool rec = recover(g_dir_sd, err);
        bool kept = false;
        for (const auto& e : fs::directory_iterator(g_dir_sd + "/previous/overlays", ec))
            if (e.path().filename().string().find("unrestored") != std::string::npos && digest(e.path().string()) == p.ovb_digest) kept = true;
        check("SDBAK-09B", "recovery finishes a swap whose restore target became unwritable: the old set is current, and the writes file's copy is kept in the backup under an 'unrestored' name",
              r1 == Result::Failed && rec && fs::exists(prev(sdcard::kFixedImageFileName)) && !fs::exists(g_dir_sd + "/go-back-work") && kept, err);
        fs::remove(dirx, ec);
    }
    {
        // 10: an unfinished earlier go back is not run over
        Pair p = make_pair();
        std::error_code ec; fs::create_directories(g_dir_sd + "/go-back-work", ec);
        const std::string before = whole();
        std::string e10;
        const Result r = go_back(g_dir_sd, e10);
        check("SDBAK-10", "a go back refuses to start while an earlier one is unfinished (go-back-work/ exists): nothing changes and the message says so",
              r == Result::Failed && whole() == before && e10.find("unfinished") != std::string::npos, e10);
        (void)p;
    }

    // ---- SDBAK-07: distributions order ------------------------------------------------------------------------
    check("SDBAK-07", "distributions compare by version: 24.11 < 26.11 < 26.11a < 26.12; equal names are equal; a name with no version is unknown",
          compare_distributions("sn-emulator-24.11.zip", "sn-emulator-26.11.zip") == -1 &&
              compare_distributions("sn-emulator-26.11.zip", "sn-emulator-26.11a.zip") == -1 &&
              compare_distributions("sn-emulator-26.11a.zip", "sn-emulator-26.12.zip") == -1 &&
              compare_distributions("sn-emulator-26.12.zip", "sn-emulator-24.11.zip") == 1 &&
              compare_distributions("sn-emulator-24.11.zip", "sn-emulator-24.11.zip") == 0 &&
              compare_distributions("whatever.zip", "sn-emulator-24.11.zip") == 2);

    // ---- SDMENU-01..06: the File menu's truth table ---------------------------------------------------------
    auto status = [](bool card_present, Relation rel, bool backup, uint64_t bytes) {
        Status s; s.card_present = card_present; s.relation = rel; s.backup = backup; s.backup_bytes = bytes; return s;
    };
    {
        const MenuState m = menu_state(status(true, Relation::Older, false, 0), true);
        check("SDMENU-01", "no backup, an older card in slot 0: Upgrade enabled; Go back and Delete disabled",
              m.upgrade_enabled && !m.go_back_enabled && !m.delete_enabled && m.backup_bytes == 0);
    }
    {
        const MenuState m = menu_state(status(true, Relation::Current, true, 2000000000ull), true);
        check("SDMENU-02", "a backup exists: Go back and Delete enabled, with its size",
              m.go_back_enabled && m.delete_enabled && m.backup_bytes == 2000000000ull && size_label(m.backup_bytes) == "1.9 GB");
    }
    {
        const MenuState m = menu_state(status(true, Relation::Current, false, 0), true);
        check("SDMENU-03", "the card is of the current distribution: Upgrade disabled", !m.upgrade_enabled);
    }
    {
        const MenuState m = menu_state(status(true, Relation::Older, true, 5), true);
        check("SDMENU-04", "the card is older: Upgrade enabled (with a backup present too)", m.upgrade_enabled && m.go_back_enabled);
    }
    {
        const MenuState m = menu_state(status(true, Relation::Newer, false, 0), true);
        check("SDMENU-05", "the card is NEWER than what jnext downloads: Upgrade disabled", !m.upgrade_enabled);
    }
    {
        const MenuState m = menu_state(status(true, Relation::Older, true, 5), false);
        check("SDMENU-06", "slot 0 does not hold the default card: Upgrade disabled (Go back and Delete follow the backup)",
              !m.upgrade_enabled && m.go_back_enabled && m.delete_enabled);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    return g_fail ? 1 : 0;
}
