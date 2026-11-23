// The SD card upgrade flow (GH #555 S5): provision_sd_card carrying the user's files onto a new
// distribution's card and switching the whole set, with the old one kept in previous/.
//
// Oracles: the design in the issue and its plan (section 3, S5): what is carried, what the new
// distribution wins, where every file ends up, what a failure leaves behind. Cards are read back
// through a FOREIGN reader (the hand-rolled fat32_read_tree shares no code with FatFs); the user's
// changes are made through the real write paths. No network: the "download" copies a zip built here.
//
// Fixtures are 600 MB sparse cards (the 8 KB-cluster recipe needs that much). Every scenario starts
// from copies of one old card (made by the real recipe) and one new zip, built once.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/fat32_image.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_provisioner.h"
#include "core/sdcard_upgrade.h"
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

constexpr uint32_t kLba = 63, kSectors = 1228800;
fs::path g_dir;
int g_n = 0;
std::string tmp(const std::string& stem) { return (g_dir / (stem + "-" + std::to_string(++g_n))).string(); }
Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }
void wr32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

Fat32Node fnode(const std::string& name, const std::string& content) {
    Fat32Node n; n.name = name; n.data = bytes_of(content); return n;
}
Fat32Node dnode(const std::string& name, std::vector<Fat32Node> kids = {}) {
    Fat32Node n; n.name = name; n.is_dir = true; n.attr = 0x10; n.children = std::move(kids); return n;
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
    return fat32_format_and_populate(path, kLba, kSectors, tree, err);
}

bool sparse_copy(const std::string& src, const std::string& dst) {
    std::string err;
    return carry::copy_sparse_file(src, dst, err);
}
void put_text(const std::string& p, const std::string& t) { std::ofstream(p, std::ios::trunc | std::ios::binary) << t; }
std::string get_text(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool exists(const std::string& p) { std::error_code ec; return fs::exists(p, ec); }
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
struct Seen { bool dir; Bytes data; };
void flat(const std::vector<Fat32Node>& level, const std::string& dir, std::map<std::string, Seen>& out) {
    for (const Fat32Node& n : level) {
        std::string up = dir + "/" + n.name;
        for (char& c : up) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        out[up] = Seen{n.is_dir, n.data};
        if (n.is_dir) flat(n.children, dir + "/" + n.name, out);
    }
}
std::map<std::string, Seen> foreign(const std::string& card, const std::string& journal_file = {}) {
    // With a journal file the card is first flattened to a scratch image, so the foreign reader sees the machine's view.
    std::string img = card;
    if (!journal_file.empty()) {
        sd_write_journal::Binding b;
        std::error_code ec;
        b.bytes = fs::file_size(card, ec);
        b.mtime = static_cast<int64_t>(fs::last_write_time(card, ec).time_since_epoch().count());
        std::string why;
        if (!sd_write_journal::bind(card, journal_file, b, false, why)) return {};
        auto j = sd_write_journal::for_image(card);
        img = tmp("flatview");
        std::string e;
        if (!j || carry::flatten(card, *j, img, e) != sdcard::FileAddStatus::Ok) return {};
    }
    std::map<std::string, Seen> m;
    uint32_t lba = 0;
    Fat32Tree t;
    if (fat32_find_partition(img, lba) && fat32_read_tree(img, lba, t)) flat(t.root, "", m);
    return m;
}
std::string text(const std::map<std::string, Seen>& m, const std::string& p) {
    const auto it = m.find(p);
    return it == m.end() ? std::string("<absent>") : std::string(it->second.data.begin(), it->second.data.end());
}

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

Fat32Tree tree_old() {
    Fat32Tree t;
    t.root.push_back(dnode("SYS", {fnode("same.sys", "x"), fnode("changed.sys", "x")}));
    t.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("48.rom", std::string(16384, 'R')), fnode("config.ini", "cfg")})}));
    return t;
}
Fat32Tree tree_new() {
    Fat32Tree t;
    t.root.push_back(dnode("SYS", {fnode("same.sys", "x"), fnode("changed.sys", "y")}));
    t.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("48.rom", std::string(16384, 'R')), fnode("config.ini", "cfg")})}));
    t.root.push_back(dnode("NEWDIR", {fnode("new.txt", "n")}));
    return t;
}

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}

bool make_zip_of(const std::string& zip, const std::string& name, const std::string& src) {
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    uint32_t crc = static_cast<uint32_t>(crc32(0L, Z_NULL, 0));
    uint64_t size = 0;
    std::vector<char> buf(1 << 20);
    std::vector<uint8_t> comp, chunk(1 << 16);
    z_stream zs{};
    if (deflateInit2(&zs, Z_BEST_SPEED, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    auto pump = [&](int flush) {
        do {
            zs.next_out = chunk.data();
            zs.avail_out = static_cast<uInt>(chunk.size());
            deflate(&zs, flush);
            comp.insert(comp.end(), chunk.begin(), chunk.end() - zs.avail_out);
        } while (zs.avail_out == 0);
    };
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n <= 0) break;
        crc = static_cast<uint32_t>(crc32(crc, reinterpret_cast<const Bytef*>(buf.data()), static_cast<uInt>(n)));
        size += static_cast<uint64_t>(n);
        zs.next_in = reinterpret_cast<Bytef*>(buf.data());
        zs.avail_in = static_cast<uInt>(n);
        pump(Z_NO_FLUSH);
    }
    zs.avail_in = 0;
    pump(Z_FINISH);
    deflateEnd(&zs);
    const uint32_t csize = static_cast<uint32_t>(comp.size());
    std::vector<uint8_t> head;
    put32(head, 0x04034b50); put16(head, 20); put16(head, 0); put16(head, 8); put16(head, 0); put16(head, 0);
    put32(head, crc); put32(head, csize); put32(head, static_cast<uint32_t>(size));
    put16(head, static_cast<uint16_t>(name.size())); put16(head, 0);
    head.insert(head.end(), name.begin(), name.end());
    std::vector<uint8_t> tail;
    put32(tail, 0x02014b50); put16(tail, 20); put16(tail, 20); put16(tail, 0); put16(tail, 8); put16(tail, 0); put16(tail, 0);
    put32(tail, crc); put32(tail, csize); put32(tail, static_cast<uint32_t>(size));
    put16(tail, static_cast<uint16_t>(name.size())); put16(tail, 0); put16(tail, 0); put16(tail, 0); put16(tail, 0);
    put32(tail, 0); put32(tail, 0);
    tail.insert(tail.end(), name.begin(), name.end());
    const uint32_t cd_size = static_cast<uint32_t>(tail.size());
    const uint32_t cd_off  = static_cast<uint32_t>(head.size() + comp.size());
    put32(tail, 0x06054b50); put16(tail, 0); put16(tail, 0); put16(tail, 1); put16(tail, 1);
    put32(tail, cd_size); put32(tail, cd_off); put16(tail, 0);
    std::ofstream out(zip, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(head.data()), static_cast<std::streamsize>(head.size()));
    out.write(reinterpret_cast<const char*>(comp.data()), static_cast<std::streamsize>(comp.size()));
    out.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    return static_cast<bool>(out);
}


const std::string kNew = "sn-emulator-99.99.zip";
const std::string kOld = "sn-emulator-24.11.zip";

struct World {
    std::string cfg, dir, raw, fixed, raw_sha, raw_distro, fixed_distro, recipe, declined;
    std::string old_raw_t, old_card_t;     // templates
    std::string new_zip;
    std::string url_new = "http://127.0.0.1:1/" + kNew;
    bool ok = false;
    std::string err;
};
World w;

bool build_world() {
    w.cfg = (g_dir / "cfg").string();
    jtp::set_env("JNEXT_CONFIG_DIR", w.cfg.c_str());
    jtp::make_dir(w.cfg.c_str());
    jtp::make_dir((w.cfg + "/sdcard").c_str());
    w.dir = sdcard::default_sdcard_dir();
    w.raw = sdcard::default_sdcard_raw_image_path();
    w.fixed = sdcard::default_sdcard_image_path();
    w.raw_sha = w.raw + ".sha256";
    w.raw_distro = w.raw + sdcard::kDistroSuffix;
    w.fixed_distro = w.fixed + sdcard::kDistroSuffix;
    w.recipe = w.fixed + sdcard::kFixedImageRecipeSuffix;
    w.declined = w.fixed + sdcard::kDeclinedSuffix;
    w.old_raw_t = tmp("oldraw");
    w.old_card_t = tmp("oldcard");
    if (!make_card(w.old_raw_t, tree_old())) { w.err = "old raw"; return false; }
    const std::string sha = sdcard::sha256_file(w.old_raw_t);
    if (!sparse_copy(w.old_raw_t, w.old_card_t)) { w.err = "old card copy"; return false; }
    std::string e;
    if (!sdcard::patch_image_for_recipe(w.old_card_t, sdcard::kFixedImageRecipe, sha, e)) { w.err = "patch old: " + e; return false; }
    const std::string new_raw = tmp("newraw");
    w.new_zip = tmp("new") + ".zip";
    if (!make_card(new_raw, tree_new()) || !make_zip_of(w.new_zip, "cspect-next-1gb.img", new_raw)) { w.err = "new zip"; return false; }
    std::error_code ec;
    fs::remove(new_raw, ec);
    jtp::set_env("JNEXT_SDCARD_DISTRO_URL", w.url_new.c_str());
    w.ok = true;
    return true;
}

// The default location holds the OLD set (raw + card + sidecars), as a normal install, plus the user's files.
void install_old() {
    std::error_code ec;
    fs::remove_all(w.dir, ec);
    fs::create_directories(w.dir, ec);
    sparse_copy(w.old_raw_t, w.raw);
    sparse_copy(w.old_card_t, w.fixed);
    put_text(w.raw_sha, sdcard::sha256_file(w.raw) + "  cspect-next-1gb.img\n");
    put_text(w.raw_distro, kOld + "\n");
    put_text(w.fixed_distro, kOld + "\n");
    put_text(w.recipe, std::to_string(sdcard::kFixedImageRecipe) + "\n");
    Edit e(w.fixed);
    e.mkdir("/GAMES");
    e.put("/GAMES/mine.nex", "NEX!");        // an added file (in a new directory)
    e.put("/SYS/same.sys", "xx");            // a changed file the distribution did not touch
    e.put("/SYS/changed.sys", "u");          // a changed file the distribution also changed: it wins
}

int g_downloads = 0;
sdcard::DownloadFn zip_dl = [](const std::string&, const std::string& dst, const sdcard::ProgressFn&, std::string&) {
    ++g_downloads;
    std::error_code ec;
    fs::copy_file(w.new_zip, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
};
sdcard::DownloadFn failing_dl = [](const std::string&, const std::string&, const sdcard::ProgressFn&, std::string& err) {
    ++g_downloads; err = "stub: no network"; return false;
};

struct Spy {
    int calls = 0;
    std::string msg;
    bool carries = true;
    sdcard::UpgradeChoice answer = sdcard::UpgradeChoice::Replace;
};
sdcard::UpgradeFn spy_fn(Spy& sp) {
    return [&sp](const std::string&, const std::string&, const std::string& m, bool carries) {
        ++sp.calls; sp.msg = m; sp.carries = carries; return sp.answer;
    };
}

sdcard::ProvisionOptions opts_with(Spy* sp, sdcard::UpgradeMode mode = sdcard::UpgradeMode::Default) {
    sdcard::ProvisionOptions o;
    o.download = zip_dl;
    o.retries = 0;
    o.upgrade_mode = mode;
    if (sp) o.upgrade = spy_fn(*sp);
    return o;
}

std::string prev(const std::string& n) { return sdcard::upgrade::previous_dir(w.dir) + "/" + n; }

bool new_set_in_place() {
    return get_text(w.fixed_distro) == kNew + "\n" && get_text(w.raw_distro) == kNew + "\n" &&
           get_text(w.recipe) == std::to_string(sdcard::kFixedImageRecipe) + "\n" && !exists(sdcard::upgrade::work_dir(w.dir));
}

// The first file in `dir` whose name starts with `prefix` ("" if none).
std::string first_in(const std::string& dir, const std::string& prefix) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().filename().string().rfind(prefix, 0) == 0) return e.path().string();
    return {};
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

int main() {
    g_dir = make_scratch_dir("jnext-upgrade");
    if (!build_world()) {
        check("SDUPG-00", "the fixtures (an old card made by the recipe, a new distribution's zip) are built", false, w.err);
        std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
        return 1;
    }

    // ---- SDUPG-01/02: the offer, and an upgrade that carries the user's files ------------------
    {
        install_old();
        Spy sp;
        sdcard::ProvisionOptions o = opts_with(&sp);
        std::string reported;
        o.show_report = [&](const std::string& r) { reported = r; };
        const uint64_t card_before = digest(w.fixed);
        const std::string raw_before = sdcard::sha256_file(w.raw);
        const auto r = sdcard::provision_sd_card(o);
        check("SDUPG-01", "the offer names both versions, says the files are carried over and the old card kept as a backup, and does not say DELETES",
              sp.calls == 1 && has(sp.msg, "NextZXOS 24.11") && has(sp.msg, "NextZXOS 99.99") && has(sp.msg, "carries the files you saved") &&
                  has(sp.msg, "kept as a backup") && !has(sp.msg, "DELETES"),
              sp.msg);
        const auto nf = foreign(w.fixed);
        const auto pf = foreign(prev(sdcard::kFixedImageFileName));
        check("SDUPG-02", "Upgrade: the added and the changed file are on the new card, the distribution's version wins a conflict, and the old card, raw and sidecars are in previous/ byte-identical",
              r.status == sdcard::ProvisionStatus::Ok && r.warning.empty() && new_set_in_place() &&
                  text(nf, "/GAMES/MINE.NEX") == "NEX!" && text(nf, "/SYS/SAME.SYS") == "xx" && text(nf, "/SYS/CHANGED.SYS") == "y" &&
                  text(nf, "/NEWDIR/NEW.TXT") == "n" &&
                  digest(prev(sdcard::kFixedImageFileName)) == card_before && sdcard::sha256_file(prev(sdcard::kImageFileName)) == raw_before &&
                  get_text(prev(std::string(sdcard::kFixedImageFileName) + sdcard::kDistroSuffix)) == kOld + "\n" &&
                  text(pf, "/GAMES/MINE.NEX") == "NEX!",
              r.error + r.warning);
        const std::string rep_file = get_text(prev(sdcard::upgrade::kReportName));
        check("SDUPG-13", "the report file in previous/ and the report handed to the frontend are the same list, and name the carried and the replaced files",
              !reported.empty() && reported == rep_file && has(rep_file, "/GAMES/mine.nex") && has(rep_file, "/SYS/changed.sys"), rep_file);
    }

    // ---- SDUPG-03/04: decline, headless ---------------------------------------------------------
    {
        install_old();
        g_downloads = 0;
        Spy sp; sp.answer = sdcard::UpgradeChoice::KeepDontAskAgain;
        auto r = sdcard::provision_sd_card(opts_with(&sp));
        const bool wrote = get_text(w.declined) == kNew + "\n";
        Spy sp2;
        r = sdcard::provision_sd_card(opts_with(&sp2));
        Spy sp3;
        r = sdcard::provision_sd_card(opts_with(&sp3, sdcard::UpgradeMode::Ask));
        check("SDUPG-03", "Keep-and-don't-ask writes .distro-declined and the next start makes no offer; ask re-asks",
              wrote && sp2.calls == 0 && sp3.calls == 1 && g_downloads == 1 && get_text(w.fixed_distro) == kNew + "\n", "downloads=" + std::to_string(g_downloads));
    }
    {
        install_old();
        g_downloads = 0;
        const uint64_t before = digest(w.fixed);
        const auto r = sdcard::provision_sd_card(opts_with(nullptr));
        check("SDUPG-04", "with no UpgradeFn (headless) nothing is asked, downloaded or changed",
              r.status == sdcard::ProvisionStatus::Ok && g_downloads == 0 && digest(w.fixed) == before && !exists(prev("")) && !exists(w.declined));
    }

    // ---- SDUPG-05/06: failures before the switch leave the old set in use ------------------------
    {
        install_old();
        g_downloads = 0;
        const uint64_t cb = digest(w.fixed), rb = digest(w.raw);
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Yes);
        o.download = failing_dl;
        const auto r = sdcard::provision_sd_card(o);
        check("SDUPG-05", "a failed download leaves the card, raw and sidecars byte-identical, removes upgrade-work and returns Ok with the warning",
              r.status == sdcard::ProvisionStatus::Ok && !r.warning.empty() && digest(w.fixed) == cb && digest(w.raw) == rb &&
                  get_text(w.fixed_distro) == kOld + "\n" && !exists(sdcard::upgrade::work_dir(w.dir)) && !exists(prev("")),
              r.warning);
    }
    {
        install_old();
        const uint64_t cb = digest(w.fixed);
        // a writes file that is not a journal at all: the carry-over cannot read it and refuses before the switch
        const std::string bogus = tmp("bogus.jsdw");
        put_text(bogus, "this is not a writes file");
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Yes);
        o.overlays.push_back(bogus);
        const auto r = sdcard::provision_sd_card(o);
        check("SDUPG-06A", "an overlay that does not belong to the card fails the carry-over before the switch: old set in use, nothing left behind",
              r.status == sdcard::ProvisionStatus::Ok && !r.warning.empty() && digest(w.fixed) == cb && !exists(sdcard::upgrade::work_dir(w.dir)) &&
                  !exists(prev("")) && get_text(bogus) == "this is not a writes file",
              r.warning + r.error);
    }
    // ---- SDUPG-06B..G / 11: a switch interrupted at each step rolls forward at the next start ------
    for (int step = 1; step <= 6; ++step) {
        install_old();
        sdcard::upgrade::g_fail_switch_step_for_test = step;
        const auto r1 = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));
        const bool interrupted = r1.status == sdcard::ProvisionStatus::Failed && has(r1.error, "interrupted");
        // the state where it stopped: before step 3 the new raw is in place and the card is not (a card is
        // never in place without the raw it was made from)
        bool mid_ok = true;
        if (step == 3) mid_ok = exists(w.raw) && get_text(w.raw_distro) == kNew + "\n" && !exists(w.fixed);
        const auto r2 = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::No));   // the next start
        const auto nf = foreign(w.fixed);
        const bool ok = interrupted && mid_ok && r2.status == sdcard::ProvisionStatus::Ok && new_set_in_place() &&
                        text(nf, "/GAMES/MINE.NEX") == "NEX!" && get_text(prev(std::string(sdcard::kFixedImageFileName) + sdcard::kDistroSuffix)) == kOld + "\n";
        const char* id = step == 1 ? "SDUPG-06B" : step == 2 ? "SDUPG-06C" : step == 3 ? "SDUPG-06D" : step == 4 ? "SDUPG-06E" : step == 5 ? "SDUPG-06F" : "SDUPG-06G";
        check(id, "a switch interrupted at this step (1..6 = 06B..06G) is finished by the next start: the full new set is in place and the old one in previous/; stopped before step 3 the new raw is in place and the card is not",
              ok, "step " + std::to_string(step) + " mid=" + std::to_string(mid_ok) + " r1=" + r1.error + " r2=" + r2.error);
    }
    {
        install_old();
        std::error_code ec;
        fs::create_directories(sdcard::upgrade::work_dir(w.dir), ec);
        put_text(sdcard::upgrade::work_dir(w.dir) + "/card.new", "half");
        const uint64_t cb = digest(w.fixed);
        const auto r = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::No));
        check("SDUPG-11A", "an upgrade-work directory without SWITCHING is deleted at the next start, the old set intact",
              r.status == sdcard::ProvisionStatus::Ok && !exists(sdcard::upgrade::work_dir(w.dir)) && digest(w.fixed) == cb);
    }

    // ---- SDUPG-07: no pristine base ---------------------------------------------------------------
    struct Why { const char* id; const char* desc; std::function<void()> spoil; const char* word; };
    const Why whys[] = {
        {"SDUPG-07A", "a missing raw: yes refuses with the reason", [] { fs::remove(w.raw); }, "original image"},
        {"SDUPG-07B", "a raw whose sha256 does not match: yes refuses", [] { put_text(w.raw_sha, std::string(64, '0') + "  cspect-next-1gb.img\n"); }, "sha"},
        {"SDUPG-07C", "a raw of another distribution than the card: yes refuses", [] { put_text(w.raw_distro, "sn-emulator-23.01.zip\n"); }, "original image"},
        {"SDUPG-07D", "a recipe-3 card whose volume serial is not the raw's: yes refuses", [] {
             std::fstream f(w.fixed, std::ios::in | std::ios::out | std::ios::binary);
             f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kLba) * 512 + 67));
             const char z[4] = {1, 2, 3, 4}; f.write(z, 4); }, "serial"},
        {"SDUPG-07E", "a card FatFs cannot mount: yes refuses", [] { put_text(w.fixed, "not a card"); }, ""},
    };
    for (const Why& y : whys) {
        install_old();
        y.spoil();
        g_downloads = 0;
        const auto r = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));
        check(y.id, y.desc,
              r.status == sdcard::ProvisionStatus::Failed && has(r.error, "cannot be carried over") && has(r.error, "--sdcard-upgrade fresh") &&
                  g_downloads == 0 && (y.word[0] == 0 || has(r.error, y.word)),
              r.error);
    }
    {
        install_old();
        fs::remove(w.raw);
        g_downloads = 0;
        const auto r = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Fresh));
        const auto nf = foreign(w.fixed);
        check("SDUPG-07F", "fresh works where yes refuses: a new card, nothing carried, the old card in previous/",
              r.status == sdcard::ProvisionStatus::Ok && new_set_in_place() && g_downloads == 1 && text(nf, "/GAMES/MINE.NEX") == "<absent>" &&
                  exists(prev(sdcard::kFixedImageFileName)),
              r.error + r.warning);
    }

    // ---- SDUPG-08/09/10: overlays ------------------------------------------------------------------
    auto make_overlay = [&](const std::string& file, const std::string& path, const std::string& content) {
        sd_write_journal::Binding b;
        std::error_code ec;
        b.bytes = fs::file_size(w.fixed, ec);
        b.mtime = static_cast<int64_t>(fs::last_write_time(w.fixed, ec).time_since_epoch().count());
        std::string why;
        sd_write_journal::bind(w.fixed, file, b, true, why);
        auto j = sd_write_journal::for_image(w.fixed);
        { Edit e(w.fixed, j); e.put(path, content); }
        sd_write_journal::forget_image(w.fixed);
    };
    {
        install_old();
        const std::string ov = tmp("user.jsdw");
        make_overlay(ov, "/DOT_DEV.BIN", "dotdev");
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Fresh);
        o.overlays.push_back(ov);
        const auto r = sdcard::provision_sd_card(o);
        check("SDUPG-08", "fresh start: the command line's writes file is moved into previous/overlays and is gone from its path, so the next bind starts empty",
              r.status == sdcard::ProvisionStatus::Ok && !exists(ov) && !first_in(prev("overlays"), "0-user").empty() && new_set_in_place(), r.error + r.warning);
    }
    {
        install_old();
        const std::string ov = tmp("user.jsdw");
        make_overlay(ov, "/DOT_DEV.BIN", "dotdev");
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Yes);
        o.overlays.push_back(ov);
        const auto r = sdcard::provision_sd_card(o);
        // the machine's view: bind the rebuilt FILE to the new card by size + mtime and read it back
        const auto view = foreign(w.fixed, ov);
        const auto plain = foreign(w.fixed);
        check("SDUPG-09", "an explicit writes file is rebuilt under the same name: it binds to the new card, the view has its carried file and the card file itself does not",
              r.status == sdcard::ProvisionStatus::Ok && new_set_in_place() && text(view, "/DOT_DEV.BIN") == "dotdev" &&
                  text(plain, "/DOT_DEV.BIN") == "<absent>" && text(view, "/GAMES/MINE.NEX") == "NEX!" && !exists(ov + ".new"),
              r.error + r.warning);
    }
    {
        install_old();
        const std::string ova = tmp("a.jsdw"), ovb = tmp("b.jsdw"), ovgone = tmp("gone.jsdw");
        make_overlay(ova, "/A.BIN", "aaa");
        make_overlay(ovb, "/B.BIN", "bbb");
        sdcard::upgrade::register_overlay(w.dir, ova);
        sdcard::upgrade::register_overlay(w.dir, ovgone);          // gone
        sdcard::upgrade::register_overlay(w.dir, ovb);
        // ovb is rebound to ANOTHER image: it no longer belongs to the card
        const std::string other = tmp("other.img");
        sparse_copy(w.fixed, other);
        { Edit e(other); e.put("/X", "x"); }
        {
            sd_write_journal::Binding b;
            std::error_code ec;
            b.bytes = fs::file_size(other, ec);
            b.mtime = static_cast<int64_t>(fs::last_write_time(other, ec).time_since_epoch().count());
            std::string why;
            sd_write_journal::bind(other, ovb, b, true, why);
            sd_write_journal::forget_image(other);
        }
        const auto r = sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));
        const auto view = foreign(w.fixed, ova);
        std::vector<std::string> list;
        { std::ifstream f(w.dir + "/overlays.list"); std::string l; while (std::getline(f, l)) list.push_back(l); }
        check("SDUPG-10", "overlays.list: a listed writes file that exists and binds to the card is rebuilt; one that is gone or bound to another image is skipped and pruned",
              r.status == sdcard::ProvisionStatus::Ok && new_set_in_place() && text(view, "/A.BIN") == "aaa" &&
                  list.size() == 1 && list[0] == ova,
              r.error + r.warning + " list=" + std::to_string(list.size()));
    }

    // ---- SDUPG-11B..D: the marker present, partial states --------------------------------------------
    {
        install_old();
        // step 1 half done: the old card already moved, the rest not
        sdcard::upgrade::g_fail_switch_step_for_test = 2;
        (void)sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));
        const bool marker = exists(sdcard::upgrade::work_dir(w.dir) + "/" + sdcard::upgrade::kSwitchMarker);
        std::error_code ec;
        fs::remove(sdcard::upgrade::work_dir(w.dir) + "/s1.done", ec);   // as if the step-1 mark was lost
        std::string note, err;
        const bool ok = sdcard::upgrade::recover(w.dir, note, err);
        check("SDUPG-11B", "SWITCHING present and a lost step-1 mark: recovery repeats the idempotent step and rolls forward to the full new set",
              marker && ok && new_set_in_place() && exists(prev(sdcard::kFixedImageFileName)), err);
    }
    {
        install_old();
        sdcard::upgrade::g_fail_switch_step_for_test = 4;
        (void)sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));
        std::string note, err;
        const bool ok = sdcard::upgrade::recover(w.dir, note, err);
        const bool again = sdcard::upgrade::recover(w.dir, note, err);
        check("SDUPG-11C", "a switch interrupted before step 4 finishes it; running recovery again changes nothing",
              ok && again && new_set_in_place() && exists(prev(sdcard::kFixedImageFileName)) && !exists(w.dir + "/previous.new"));
    }

    // ---- SDUPG-12: space --------------------------------------------------------------------------------
    {
        install_old();
        g_downloads = 0;
        const uint64_t cb = digest(w.fixed);
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Yes);
        o.free_space = [](const std::string&) { return uint64_t(1000); };
        const auto r = sdcard::provision_sd_card(o);
        check("SDUPG-12", "not enough free space: refused before any download, the old set untouched, the work directory gone",
              r.status == sdcard::ProvisionStatus::Ok && has(r.warning, "not enough free disk space") && g_downloads == 0 &&
                  digest(w.fixed) == cb && !exists(sdcard::upgrade::work_dir(w.dir)),
              r.warning);
    }

    // ---- SDUPG-14: a second upgrade replaces the backup, and the offer said so -------------------------------
    {
        install_old();
        (void)sdcard::provision_sd_card(opts_with(nullptr, sdcard::UpgradeMode::Yes));   // first upgrade: backup of 24.11
        // now the "current" distribution moves on: a card of 99.99 and an offer of 99.98
        const std::string url2 = "http://127.0.0.1:1/sn-emulator-99.98.zip";
        jtp::set_env("JNEXT_SDCARD_DISTRO_URL", url2.c_str());
        Spy sp;
        sdcard::ProvisionOptions o = opts_with(&sp);
        const auto r = sdcard::provision_sd_card(o);
        jtp::set_env("JNEXT_SDCARD_DISTRO_URL", w.url_new.c_str());
        check("SDUPG-14", "with a backup present the offer says it is replaced by this one, and the upgrade replaces it (the backup is now the 99.99 card)",
              sp.calls == 1 && has(sp.msg, "backup of an earlier upgrade is replaced") &&
                  (r.status == sdcard::ProvisionStatus::Ok) && get_text(prev(std::string(sdcard::kFixedImageFileName) + sdcard::kDistroSuffix)) == kNew + "\n",
              sp.msg + r.error + r.warning);
    }
    {
        install_old();
        const std::string ov = tmp("backup.jsdw");
        make_overlay(ov, "/Z.BIN", "z");
        const bool before = sdcard::upgrade::file_belongs_to_backup(w.dir, ov);
        sdcard::ProvisionOptions o = opts_with(nullptr, sdcard::UpgradeMode::Fresh);
        o.overlays.push_back(ov);
        (void)sdcard::provision_sd_card(o);
        // a writes file bound to the previous card, copied back out, belongs to the backup
        std::error_code ec;
        const std::string back = tmp("back.jsdw");
        fs::copy_file(first_in(prev("overlays"), "0-backup"), back, ec);
        check("SDUPG-15", "a writes file bound to the backup card is recognised as such (the refusal then names --sdcard-go-back); one bound to nothing there is not",
              !before && sdcard::upgrade::file_belongs_to_backup(w.dir, back) && !sdcard::upgrade::file_belongs_to_backup(w.dir, tmp("none.jsdw")));
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    return g_fail ? 1 : 0;
}
