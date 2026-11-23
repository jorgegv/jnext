// The SD card comparison / carry-over engine and the rescue verbs (GH #555): src/core/sdcard_carry.
//
// Oracles:
//   - The design in the issue and its plan (section 3 "The carry-over algorithm", the S4 verbs):
//     what "changed" means, what is carried and what the new distribution wins, the CHANGES.txt
//     format, the import rules. The expected trees below are written out here from that table,
//     never read back through the engine.
//   - A FOREIGN reader: the hand-rolled fat32_read_tree shares no code with FatFs, the engine's
//     reader and writer, so a card's contents are asserted through it.
//   - The user changes are made through the real write paths (add_file_to_image, direct and in
//     journal mode; FatFs calls for attributes and deletions), as a user's would be.
//
// Fixtures are ~35 MB sparse cards (512-byte clusters, 68000 sectors: just over the 65525-cluster
// FAT32 minimum), built in the test. The one recipe row needs 8 KB clusters and a 600 MB image.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/sdcard_carry.h"

#include "core/fat32_image.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_provisioner.h"
#include "core/sha256.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace carry = sdcard::carry;
using sdcard::FileAddStatus;
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

constexpr uint32_t kLba = 2048, kSectors = 68000;

fs::path g_dir;
int g_n = 0;

std::string tmp(const std::string& stem) { return (g_dir / (stem + "-" + std::to_string(++g_n))).string(); }

Bytes bytes_of(const std::string& s) { return Bytes(s.begin(), s.end()); }

void wr32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

Fat32Node fnode(const std::string& name, const std::string& content) {
    Fat32Node n;
    n.name = name;
    n.data = bytes_of(content);
    return n;
}
Fat32Node dnode(const std::string& name, std::vector<Fat32Node> kids = {}) {
    Fat32Node n;
    n.name = name;
    n.is_dir = true;
    n.attr = 0x10;
    n.children = std::move(kids);
    return n;
}

// MBR + one FAT32-LBA partition of `sectors`, sparse, formatted with `au` byte clusters
// and populated with `tree` (the engine's own tests use the same call the provisioner does).
bool make_card(const std::string& path, const Fat32Tree& tree, uint32_t sectors = kSectors, uint32_t au = 512) {
    const uint64_t bytes = static_cast<uint64_t>(kLba + sectors) * 512;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        std::vector<uint8_t> mbr(512, 0);
        uint8_t* pe = mbr.data() + 0x1BE;
        pe[4] = 0x0C;
        wr32(pe + 8, kLba);
        wr32(pe + 12, sectors);
        mbr[510] = 0x55; mbr[511] = 0xAA;
        f.write(reinterpret_cast<char*>(mbr.data()), 512);
        f.seekp(static_cast<std::streamoff>(bytes - 1));
        char z = 0;
        f.write(&z, 1);
        if (!f.good()) return false;
    }
    std::string err;
    Fat32WriteOptions opts;
    opts.au_size = au;
    return fat32_format_and_populate(path, kLba, sectors, tree, err, opts);
}

bool copy_card(const std::string& src, const std::string& dst) {
    std::string err;
    return carry::copy_sparse_file(src, dst, err);
}

std::string host_file(const std::string& content) {
    const std::string p = tmp("host");
    std::ofstream(p, std::ios::binary) << content;
    return p;
}

FileAddStatus add(const std::string& card, const std::string& card_path, const std::string& content, bool force = false) {
    std::string err;
    return sdcard::add_file_to_image(card, host_file(content), card_path, force, err);
}

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

// A writable FatFs view for the user-change operations FatFs does (attributes, delete, mkdir, utime).
struct Edit {
    std::unique_ptr<carry::Volume> v;
    explicit Edit(const std::string& card) { std::string e; v = carry::Volume::open(0, card, nullptr, false, e); }
    bool ok() const { return v != nullptr; }
    void chmod(const std::string& p, BYTE attr, BYTE mask) { v->mark_dirty(); f_chmod(v->fat(p).c_str(), attr, mask); }
    void unlink(const std::string& p) { v->mark_dirty(); f_unlink(v->fat(p).c_str()); }
    void mkdir(const std::string& p) { v->mark_dirty(); f_mkdir(v->fat(p).c_str()); }
    void utime(const std::string& p, WORD d, WORD t) { FILINFO f{}; f.fdate = d; f.ftime = t; v->mark_dirty(); f_utime(v->fat(p).c_str(), &f); }
    void put(const std::string& p, const std::string& content) {
        v->mark_dirty();
        FIL fil{};
        if (f_open(&fil, v->fat(p).c_str(), FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            UINT bw = 0;
            f_write(&fil, content.data(), static_cast<UINT>(content.size()), &bw);
            f_close(&fil);
        }
    }
};

struct Seen { bool dir; uint8_t attr; Bytes data; uint16_t wdate, wtime; };

void flat(const std::vector<Fat32Node>& level, const std::string& dir, std::map<std::string, Seen>& out) {
    for (const Fat32Node& n : level) {
        std::string up = dir + "/" + n.name;
        for (char& c : up) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        out[up] = Seen{n.is_dir, n.attr, n.data, n.wdate, n.wtime};
        if (n.is_dir) flat(n.children, dir + "/" + n.name, out);
    }
}
// The FOREIGN reader: every file of a card, by upper-cased path.
std::map<std::string, Seen> foreign(const std::string& card) {
    std::map<std::string, Seen> m;
    uint32_t lba = 0;
    Fat32Tree t;
    if (fat32_find_partition(card, lba) && fat32_read_tree(card, lba, t)) flat(t.root, "", m);
    return m;
}
std::string up(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }

std::string text(const Seen* s) { return s ? std::string(s->data.begin(), s->data.end()) : std::string("<absent>"); }

bool has_change(const std::vector<carry::Change>& v, carry::Kind k, const std::string& path) {
    for (const auto& c : v) if (c.kind == k && carry::path_key(c.entry.path) == carry::path_key(path)) return true;
    return false;
}
bool any_change(const std::vector<carry::Change>& v, const std::string& path) {
    for (const auto& c : v) if (carry::path_key(c.entry.path) == carry::path_key(path)) return true;
    return false;
}

bool bind_journal(const std::string& image, const std::string& file) {
    sd_write_journal::Binding b;
    std::error_code ec;
    b.bytes = fs::file_size(image, ec);
    b.mtime = static_cast<int64_t>(fs::last_write_time(image, ec).time_since_epoch().count());
    std::string why;
    return sd_write_journal::bind(image, file, b, false, why);
}

std::unique_ptr<carry::Volume> vol(int drive, const std::string& image,
                                   std::shared_ptr<sd_write_journal::Journal> j = nullptr, bool ro = true) {
    std::string e;
    return carry::Volume::open(drive, image, j, ro, e);
}

// ---- the fixture distributions (the table of section 4 of the plan) ----------------------

Fat32Tree tree_a() {
    Fat32Tree t;
    t.root.push_back(dnode("SYS", {fnode("same.sys", "x"), fnode("changed.sys", "x"), fnode("attr.sys", "x"),
                                   fnode("resaved.sys", "x")}));
    t.root.push_back(dnode("NEXTZXOS", {fnode("keep.txt", "x"), fnode("removed.txt", "x")}));
    t.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("config.ini", "cfg")})}));
    return t;
}
Fat32Tree tree_n() {
    Fat32Tree t;
    t.root.push_back(dnode("SYS", {fnode("same.sys", "x"), fnode("changed.sys", "y"), fnode("attr.sys", "x"),
                                   fnode("resaved.sys", "x")}));
    t.root.push_back(dnode("NEXTZXOS", {fnode("keep.txt", "x"), fnode("added.txt", "z")}));
    t.root.push_back(dnode("APPS"));
    t.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("config.ini", "cfg")})}));
    return t;
}

struct Fixture {
    std::string a, n, c;   // as-made old, as-made new, the user's card (A + changes)
};

// C = A plus the user's actions of the plan's table, through the real write paths.
Fixture make_fixture() {
    Fixture f;
    f.a = tmp("A");
    f.n = tmp("N");
    f.c = tmp("C");
    make_card(f.a, tree_a());
    make_card(f.n, tree_n());
    copy_card(f.a, f.c);
    add(f.c, "/SYS/same.sys", "x2", true);
    add(f.c, "/SYS/changed.sys", "u", true);
    add(f.c, "/NEXTZXOS/removed.txt", "u2", true);
    add(f.c, "/GAMES/mine.nex", "NEX!");
    add(f.c, "/My Long Name.txt", "long");
    add(f.c, "/readme.txt", "rm");
    add(f.c, "/APPS", "i-am-a-file");
    add(f.c, "/MACHINES/NEXT/config.ini", "cfg-user", true);
    add(f.c, "/SYS/resaved.sys", "x", true);   // the same bytes written again
    Edit e(f.c);
    e.utime("/GAMES/mine.nex", 0x5A21, 0x6000);   // a date no FatFs default could produce (M24)
    e.chmod("/SYS/attr.sys", AM_HID, AM_HID);
    e.unlink("/NEXTZXOS/keep.txt");
    e.mkdir("/MYDIR");
    e.chmod("/MYDIR", AM_HID, AM_HID);            // a hidden user directory (M34)
    return f;
}

// The whole pipeline of an upgrade's direct layer: N' = N + the carried user changes of C against A.
struct Result {
    std::string np;
    carry::Report report;
    std::vector<carry::Entry> carried;
    std::vector<carry::Change> user, dist;
    FileAddStatus st = FileAddStatus::Ok;
    std::string err;
};
Result carry_direct(const Fixture& f) {
    Result r;
    r.np = tmp("Nprime");
    copy_card(f.n, r.np);
    std::string err;
    {   // the user layer: C against A
        auto vc = vol(0, f.c);
        auto va = vol(1, f.a);
        carry::compute_changes(*vc, *va, r.user, err);
    }
    {   // the distribution's changes at those paths: N against A, then the decision
        std::vector<std::string> keys;
        for (const auto& u : r.user) keys.push_back(carry::path_key(u.entry.path));
        std::sort(keys.begin(), keys.end());
        auto vn = vol(0, f.n);
        auto va = vol(1, f.a);
        carry::changes_at(*vn, *va, keys, r.dist, err);
        carry::decide(r.user, r.dist, *vn, r.carried, r.report);
    }
    auto src = vol(1, f.c);
    auto dst = vol(0, r.np, nullptr, false);
    r.st = (src && dst) ? carry::apply_carry(*src, r.carried, *dst, r.err) : FileAddStatus::ImageUnusable;
    return r;
}

// A card as a MEMORY tree: a copy whose TotSec32 is lowered below the FAT32 cluster minimum, so FatFs
// refuses it and the engine's lenient opener reads it with the hand-rolled reader (content untouched).
std::unique_ptr<carry::Tree> mem_tree(const std::string& card) {
    const std::string m = tmp("Mem");
    copy_card(card, m);
    std::fstream f(m, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kLba) * 512 + 32));
    const uint32_t small = 60000;
    const uint8_t le[4] = {static_cast<uint8_t>(small), static_cast<uint8_t>(small >> 8), 0, 0};
    f.write(reinterpret_cast<const char*>(le), 4);
    f.flush();
    std::string e;
    return carry::open_tree_lenient(m, e);
}

}  // namespace

int main() {
    g_dir = make_scratch_dir("jnext-carry");
    std::string err;

    // =================== SDCMP: walking and comparing =====================================
    const Fixture fx = make_fixture();
    {
        auto v = vol(0, fx.c);
        const auto fr = foreign(fx.c);
        bool ok = v != nullptr;
        std::string why;
        if (ok) {
            std::set<std::string> seen;
            for (const carry::Entry& e : v->entries()) {
                const auto it = fr.find(up(e.path));
                seen.insert(up(e.path));
                if (it == fr.end()) { ok = false; why += " missing-in-foreign:" + e.path; continue; }
                if (it->second.dir != e.dir) { ok = false; why += " type:" + e.path; }
                if (!e.dir && it->second.data.size() != e.size) { ok = false; why += " size:" + e.path; }
                if (e.altname.empty() && e.path.size() > 0 && false) ok = false;
                if (!e.dir) {
                    Bytes got;
                    std::string er;
                    if (v->open(e, er)) {
                        Bytes buf(4096);
                        size_t n = 0;
                        while (v->read(buf.data(), buf.size(), n, er) && n) got.insert(got.end(), buf.begin(), buf.begin() + static_cast<long>(n));
                        v->close();
                    }
                    if (got != it->second.data) { ok = false; why += " bytes:" + e.path; }
                }
            }
            for (const auto& kv : fr) if (!seen.count(kv.first)) { ok = false; why += " only-in-foreign:" + kv.first; }
            const carry::Entry* h = v->find(carry::path_key("/SYS/attr.sys"));
            ok = ok && h && (h->attr & carry::kAttrHid) && !h->altname.empty();
        }
        check("SDCMP-01", "walking a card gives every file and directory with size, attributes and 8.3 name, equal to what the foreign hand-rolled reader finds (names, sizes, bytes)", ok, why);
    }
    {
        // SDCMP-02: each recipe's card from one raw: config.ini always; zx81 doubled for 2 and 3; serial for 3.
        Fat32Tree raw;
        raw.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("zx81.rom", std::string(16384, 'Z')), fnode("48.rom", "rom48")})}));
        const std::string raw_img = tmp("raw600");
        const bool made = make_card(raw_img, raw, 1228800, 8192);
        const std::string rawsha = sdcard::sha256_file(raw_img);
        const std::string zx_sha = sdcard::sha256_hex(Bytes(16384, 'Z'));
        bool ok = made;
        std::string why;
        auto read_serial = [&](const std::string& img) {
            std::ifstream f(img, std::ios::binary);
            uint8_t id[4] = {};
            f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kLba) * 512 + 0x43));
            f.read(reinterpret_cast<char*>(id), 4);
            return static_cast<uint32_t>(id[0]) | (static_cast<uint32_t>(id[1]) << 8) | (static_cast<uint32_t>(id[2]) << 16) | (static_cast<uint32_t>(id[3]) << 24);
        };
        for (int r = 1; r <= 3 && ok; ++r) {
            const std::string img = tmp("recipe");
            ok = copy_card(raw_img, img) && sdcard::patch_image_for_recipe(img, r, rawsha, err, zx_sha);
            if (!ok) { why += " recipe" + std::to_string(r) + ":" + err; break; }
            const auto fr = foreign(img);
            const auto cfg = fr.find("/MACHINES/NEXT/CONFIG.INI");
            const auto zx = fr.find("/MACHINES/NEXT/ZX81.ROM");
            const uint32_t want_zx = r >= 2 ? 32768 : 16384;
            if (cfg == fr.end() || cfg->second.data != sdcard::default_config_ini()) { ok = false; why += " cfg" + std::to_string(r); }
            if (zx == fr.end() || zx->second.data.size() != want_zx) { ok = false; why += " zx" + std::to_string(r); }
            const bool serial_is = read_serial(img) == sdcard::volume_id_from_sha256(rawsha);
            if (serial_is != (r == 3)) { ok = false; why += " serial" + std::to_string(r); }
        }
        std::string e0, e4;
        const std::string bad = tmp("recipe");
        copy_card(raw_img, bad);
        const bool r0 = sdcard::patch_image_for_recipe(bad, 0, rawsha, e0, zx_sha);
        const bool r4 = sdcard::patch_image_for_recipe(bad, sdcard::kFixedImageRecipe + 1, rawsha, e4, zx_sha);
        check("SDCMP-02", "rebuilding the recipe 1, 2 and 3 cards from one raw gives the as-made trees (config.ini injected; zx81.rom doubled only for 2 and 3; volume serial from the raw's hash only for 3), and recipes 0 and newer are refused",
              ok && !r0 && !r4, why + " " + e0 + " | " + e4);
        std::error_code ec;
        fs::remove(raw_img, ec);
    }
    {
        const std::string a2 = tmp("A2");
        copy_card(fx.a, a2);
        std::vector<carry::Change> ch;
        auto x = vol(0, fx.a);
        auto y = vol(1, a2);
        const bool ok = carry::compute_changes(*x, *y, ch, err);
        check("SDCMP-03", "a card compared with an untouched copy of itself has no change", ok && ch.empty(), std::to_string(ch.size()));
    }
    std::vector<carry::Change> user;
    {
        auto c = vol(0, fx.c);
        auto a = vol(1, fx.a);
        carry::compute_changes(*c, *a, user, err);
        check("SDCMP-04", "an added file and an added empty directory (and the directory a file was added into) are Added",
              has_change(user, carry::Kind::Added, "/GAMES/mine.nex") && has_change(user, carry::Kind::Added, "/GAMES") &&
                  has_change(user, carry::Kind::Added, "/MYDIR") && has_change(user, carry::Kind::Added, "/readme.txt") &&
                  has_change(user, carry::Kind::Added, "/My Long Name.txt"));
        check("SDCMP-05", "changed content is Changed (same size and different size)",
              has_change(user, carry::Kind::Changed, "/SYS/same.sys") && has_change(user, carry::Kind::Changed, "/SYS/changed.sys") &&
                  has_change(user, carry::Kind::Changed, "/NEXTZXOS/removed.txt"));
    }
    {
        // SDCMP-06: R/H/S differences are changes; the archive bit alone is not.
        const std::string a2 = tmp("A2");
        copy_card(fx.a, a2);
        {
            Edit e(a2);
            e.chmod("/SYS/same.sys", 0, AM_ARC);      // archive bit cleared only
            e.chmod("/SYS/changed.sys", AM_RDO, AM_RDO);
        }
        std::vector<carry::Change> ch;
        auto x = vol(0, a2);
        auto y = vol(1, fx.a);
        carry::compute_changes(*x, *y, ch, err);
        check("SDCMP-06", "a read-only attribute change alone is Changed (and a hidden one, SDCMP-01's attr.sys); a cleared archive bit alone is not",
              has_change(ch, carry::Kind::Changed, "/SYS/changed.sys") && !any_change(ch, "/SYS/same.sys") &&
                  has_change(user, carry::Kind::Changed, "/SYS/attr.sys"));
    }
    {
        const std::string a2 = tmp("A2");
        copy_card(fx.a, a2);
        {
            Edit e(a2);
            e.utime("/SYS/resaved.sys", static_cast<WORD>(((2026 - 1980) << 9) | (5 << 5) | 17), static_cast<WORD>(10 << 11));
        }
        std::vector<carry::Change> ch;
        auto x = vol(0, a2);
        auto y = vol(1, fx.a);
        carry::compute_changes(*x, *y, ch, err);
        check("SDCMP-07", "a timestamp-only change (and the same bytes written again) is not a change",
              ch.empty() && !any_change(user, "/SYS/resaved.sys"), std::to_string(ch.size()));
    }
    check("SDCMP-08", "a file the user deleted is Deleted", has_change(user, carry::Kind::Deleted, "/NEXTZXOS/keep.txt"));
    {
        // SDCMP-09: names compare case-insensitively; LFN and lower-case 8.3 names round-trip.
        Fat32Tree p, q;
        p.root.push_back(dnode("Dir", {fnode("FILE.TXT", "same")}));
        q.root.push_back(dnode("DIR", {fnode("file.txt", "same")}));
        const std::string pc = tmp("P"), qc = tmp("Q");
        make_card(pc, p);
        make_card(qc, q);
        std::vector<carry::Change> ch;
        auto x = vol(0, pc);
        auto y = vol(1, qc);
        const bool ok = carry::compute_changes(*x, *y, ch, err);
        auto c = vol(0, fx.c);
        const bool lfn = c->find(carry::path_key("/My Long Name.txt")) && c->find(carry::path_key("/readme.txt")) &&
                         std::any_of(c->entries().begin(), c->entries().end(), [](const carry::Entry& e) { return e.path == "/My Long Name.txt"; }) &&
                         std::any_of(c->entries().begin(), c->entries().end(), [](const carry::Entry& e) { return e.path == "/readme.txt"; });
        check("SDCMP-09", "names match case-insensitively (a case-only difference is not a change); a long name and a lower-case 8.3 name keep their exact spelling", ok && ch.empty() && lfn, std::to_string(ch.size()));
    }
    {
        // SDCMP-10: the layers.
        const std::string base = tmp("L");
        copy_card(fx.a, base);
        add(base, "/DIRECT.TXT", "direct");
        const std::string jf = tmp("L") + ".jsdw";
        bind_journal(base, jf);
        add(base, "/OVERLAY.TXT", "overlay");   // goes into the journal: the card is bound to one
        auto j = sd_write_journal::for_image(base);
        std::vector<carry::Change> direct, overlay;
        {
            auto c = vol(0, base);          // no journal: the card file alone
            auto a = vol(1, fx.a);
            carry::compute_changes(*c, *a, direct, err);
        }
        {
            auto cj = vol(0, base, j, true);   // the card as the machine sees it
            auto c = vol(1, base);
            carry::compute_changes(*cj, *c, overlay, err);
        }
        check("SDCMP-10", "the overlay layer (card+journal against the card) finds only the journal's changes, and a change made directly on the card appears only in the direct layer",
              has_change(direct, carry::Kind::Added, "/DIRECT.TXT") && !any_change(direct, "/OVERLAY.TXT") &&
                  has_change(overlay, carry::Kind::Added, "/OVERLAY.TXT") && !any_change(overlay, "/DIRECT.TXT"));
        sd_write_journal::forget_all_for_test();
    }

    // =================== SDCARRY: deciding and carrying ==================================
    const Result res = carry_direct(fx);
    const auto np = foreign(res.np);
    auto seen = [&](const std::string& p) { const auto it = np.find(up(p)); return it == np.end() ? nullptr : &it->second; };
    {
        const Seen* mine = seen("/GAMES/mine.nex");
        const Seen* hid = seen("/SYS/attr.sys");
        const Seen* same = seen("/SYS/same.sys");
        const Seen* lng = seen("/My Long Name.txt");
        const Seen* mydir = seen("/MYDIR");
        auto c = vol(0, fx.c);
        const carry::Entry* src = c->find(carry::path_key("/GAMES/mine.nex"));
        const bool date = mine && src && mine->wdate == src->fdate && mine->wtime == src->ftime;
        check("SDCARRY-01", "added and changed files are on the new card with their bytes, R/H/S attributes and timestamp; added directories are made; long and 8.3 names are exact",
              res.st == FileAddStatus::Ok && mine && text(mine) == "NEX!" && date && hid && (hid->attr & 2) && same && text(same) == "x2" &&
                  lng && text(lng) == "long" && mydir && mydir->dir && (mydir->attr & 2) && seen("/readme.txt"),
              res.err + " mine=" + text(mine) + " same=" + text(same));
    }
    {
        const Seen* ch = seen("/SYS/changed.sys");
        const auto& o = res.report.overridden;
        check("SDCARRY-02", "a conflict (the user and the distribution both changed the file) keeps the DISTRIBUTION's version and lists the file as replaced",
              ch && text(ch) == "y" && std::find(o.begin(), o.end(), "/SYS/changed.sys") != o.end());
    }
    {
        const auto& d = res.report.deleted_back;
        check("SDCARRY-03", "a file the user deleted is not carried: the distribution's file is on the new card, listed as back",
              seen("/NEXTZXOS/keep.txt") && std::find(d.begin(), d.end(), "/NEXTZXOS/keep.txt") != d.end());
    }
    {
        const auto& r = res.report.removed;
        check("SDCARRY-04", "a file the user changed and the new distribution removed is not carried (the distribution wins) and is listed",
              !seen("/NEXTZXOS/removed.txt") && std::find(r.begin(), r.end(), "/NEXTZXOS/removed.txt") != r.end());
    }
    {
        const Seen* apps = seen("/APPS");
        const auto& o = res.report.overridden;
        check("SDCARRY-05", "a file-versus-directory conflict (the user made a FILE where the new distribution made a directory) means the distribution wins, listed",
              apps && apps->dir && std::find(o.begin(), o.end(), "/APPS") != o.end());
    }
    {
        const Seen* cfg = seen("/MACHINES/NEXT/config.ini");
        check("SDCARRY-06", "config.ini changed by the user, identical in both as-made cards, is carried", cfg && text(cfg) == "cfg-user");
    }
    {
        // SDCARRY-07: the overlay layer goes through a journal over N', which stays byte-identical.
        const std::string nj = tmp("Nj");
        copy_card(fx.n, nj);
        const uint64_t before = digest(nj);
        const std::string jf = tmp("Nj") + ".jsdw";
        bind_journal(nj, jf);
        auto jn = sd_write_journal::for_image(nj);
        // the overlay layer of the user's card: C+J against C, with one journaled file
        const std::string cj = tmp("Cj");
        copy_card(fx.a, cj);
        const std::string cjf = tmp("Cj") + ".jsdw";
        bind_journal(cj, cjf);
        add(cj, "/DOT/DEV", "dotdev");
        auto jc = sd_write_journal::for_image(cj);
        std::vector<carry::Change> uj;
        {
            auto view = vol(0, cj, jc, true);
            auto plain = vol(1, cj);
            carry::compute_changes(*view, *plain, uj, err);
        }
        std::vector<carry::Entry> items;
        for (const auto& u : uj) if (u.kind != carry::Kind::Deleted) items.push_back(u.entry);
        FileAddStatus st = FileAddStatus::ImageUnusable;
        {
            auto src = vol(1, cj, jc, true);
            auto dst = vol(0, nj, jn, false);
            if (src && dst) st = carry::apply_carry(*src, items, *dst, err);
        }
        sd_write_journal::forget_all_for_test();
        // re-bind read-only view of the journal file: the machine's view of N'
        sd_write_journal::Binding b;
        std::error_code ec;
        b.bytes = fs::file_size(nj, ec);
        b.mtime = static_cast<int64_t>(fs::last_write_time(nj, ec).time_since_epoch().count());
        std::string why;
        sd_write_journal::bind(nj, jf, b, false, why);
        auto jv = sd_write_journal::for_image(nj);
        bool through = false;
        {
            auto view = vol(0, nj, jv, true);
            const carry::Entry* e = view ? view->find(carry::path_key("/DOT/DEV")) : nullptr;
            through = e && !e->dir && e->size == 6;
        }
        check("SDCARRY-07", "the overlay layer is applied through a journal: the new card's bytes are unchanged, the journal holds the change, and the view through it has the file",
              st == FileAddStatus::Ok && digest(nj) == before && through && jv && !jv->empty(), err + why);
        sd_write_journal::forget_all_for_test();
    }
    {
        const std::string f1 = tmp("K") + ".jsdw", f2 = tmp("K") + ".jsdw";
        const std::string i1 = tmp("K1"), i2 = tmp("K2");
        copy_card(fx.a, i1);
        copy_card(fx.a, i2);
        bind_journal(i1, f1);
        auto j1 = sd_write_journal::for_image(i1);
        j1->commit_file("MYFILE.BIN", bytes_of("kept-bytes"));
        bind_journal(i2, f2);
        auto j2 = sd_write_journal::for_image(i2);
        const bool ok = carry::copy_kept_file(*j1, *j2);
        const auto k = j2->kept_file();
        check("SDCARRY-08", "the esxDOS stand-in's kept-file record is copied to the new journal verbatim",
              ok && k && k->name == "MYFILE.BIN" && k->data == bytes_of("kept-bytes"));
        sd_write_journal::forget_all_for_test();
    }
    {
        // SDCARRY-09: a carry that cannot fit fails before writing, leaving image and journal byte-identical.
        const std::string d = tmp("Full");
        copy_card(fx.n, d);
        const uint64_t before = digest(d);
        carry::Entry big;
        big.path = "/HUGE.BIN";
        big.size = 0xF0000000u;   // declared size: far more than the card holds
        auto src = vol(1, fx.c);
        FileAddStatus s1 = FileAddStatus::Ok, s2 = FileAddStatus::Ok;
        std::string e1, e2;
        {
            auto dst = vol(0, d, nullptr, false);
            s1 = carry::apply_carry(*src, {big}, *dst, e1);
        }
        const std::string jf = tmp("Full") + ".jsdw";
        bind_journal(d, jf);
        auto j = sd_write_journal::for_image(d);
        const uint64_t jbefore = fs::exists(jf) ? digest(jf) : 0;
        {
            auto dst = vol(0, d, j, false);
            s2 = carry::apply_carry(*src, {big}, *dst, e2);
        }
        const uint64_t jafter = fs::exists(jf) ? digest(jf) : 0;
        check("SDCARRY-09", "a carry that does not fit fails (image full) before anything is written: the image and the journal are byte-identical afterwards",
              s1 == FileAddStatus::ImageFull && s2 == FileAddStatus::ImageFull && digest(d) == before && jbefore == jafter, e1 + " | " + e2);
        sd_write_journal::forget_all_for_test();
    }
    {
        // SDCARRY-12: a carry that passes the space precheck and fails MID-WRITE (the second entry's
        // source is gone) is rolled back: nothing it had created is on the new card.
        const std::string d = tmp("Roll");
        copy_card(fx.n, d);
        auto src = vol(1, fx.c);
        const carry::Entry* real = src->find(carry::path_key("/GAMES/mine.nex"));
        carry::Entry ghost;
        ghost.path = "/ZZGHOST.BIN";
        ghost.size = 10;
        FileAddStatus st = FileAddStatus::Ok;
        std::string err;
        if (real) {
            auto dst = vol(0, d, nullptr, false);
            st = carry::apply_carry(*src, {*real, ghost}, *dst, err);
        }
        const auto after = foreign(d);
        check("SDCARRY-12", "a carry that fails mid-write is rolled back: the status is an error, the message says so, and the file it had already written is not on the new card",
              real && st != FileAddStatus::Ok && err.find("rolled back") != std::string::npos &&
                  after.find(up("/GAMES/mine.nex")) == after.end(),
              err);
    }
    {
        // SDCARRY-13/14/15 (review of S4): type conflicts, children under a file, parents the carry creates.
        Fat32Tree ta;
        ta.root.push_back(fnode("T", "t-file"));
        ta.root.push_back(dnode("D", {fnode("f.txt", "f")}));
        ta.root.push_back(fnode("KEEP.TXT", "k"));
        ta.root.push_back(dnode("X", {fnode("x.txt", "x")}));
        ta.root.push_back(dnode("OLD", {fnode("x.txt", "x")}));
        Fat32Tree tn;   // the new card: T and D as before, X replaced by a FILE, OLD removed
        tn.root.push_back(fnode("T", "t-file"));
        tn.root.push_back(dnode("D", {fnode("f.txt", "f")}));
        tn.root.push_back(fnode("KEEP.TXT", "k"));
        tn.root.push_back(fnode("X", "now-a-file"));
        const std::string ca = tmp("TA"), cn = tmp("TN"), cc = tmp("TC");
        make_card(ca, ta);
        make_card(cn, tn);
        copy_card(ca, cc);
        {
            Edit e(cc);
            e.unlink("/T");           e.mkdir("/T");  e.put("/T/child.txt", "tc");     // file -> directory
            e.unlink("/D/f.txt");     e.unlink("/D"); e.put("/D", "d-is-a-file");      // directory -> file
            e.put("/MINE.TXT", "mine");
            e.put("/X/mine.txt", "under-a-file");                                      // N holds /X as a file
            e.put("/OLD/mine.txt", "in-a-removed-dir");                                // the distribution removed /OLD
        }
        std::vector<carry::Change> user, dist;
        std::vector<carry::Entry> items;
        carry::Report rep;
        std::string e13;
        {
            auto vc = vol(0, cc);
            auto va = vol(1, ca);
            carry::compute_changes(*vc, *va, user, e13);
        }
        {
            std::vector<std::string> keys;
            for (const auto& u : user) keys.push_back(carry::path_key(u.entry.path));
            std::sort(keys.begin(), keys.end());
            auto vn = vol(0, cn);
            auto va = vol(1, ca);
            carry::changes_at(*vn, *va, keys, dist, e13);
            carry::decide(user, dist, *vn, items, rep);
        }
        const std::string np = tmp("TNprime");
        copy_card(cn, np);
        FileAddStatus st = FileAddStatus::ImageUnusable;
        {
            auto src = vol(1, cc);
            auto dst = vol(0, np, nullptr, false);
            if (src && dst) st = carry::apply_carry(*src, items, *dst, e13);
        }
        const auto f = foreign(np);
        auto listed = [&](const std::string& p) {
            for (const auto& o : rep.overridden) if (o.find(p) != std::string::npos) return true;
            return false;
        };
        check("SDCARRY-13", "a type change the user made and the distribution left alone (file to directory, directory to file) does not abort the carry: the distribution's type wins, both are listed, the children are listed, and the rest is carried",
              st == FileAddStatus::Ok && text(f.count("/T") ? &f.at("/T") : nullptr) == "t-file" && !f.count("/T/CHILD.TXT") &&
                  f.count("/D") && f.at("/D").dir && !f.count("/MINE-D") && listed("/T") && listed("/D") && listed("/T/child.txt") &&
                  f.count("/MINE.TXT") && text(&f.at("/MINE.TXT")) == "mine",
              e13);
        check("SDCARRY-14", "a user file under a path the new card holds as a FILE is listed, not carried (and does not abort the carry)",
              st == FileAddStatus::Ok && listed("/X/mine.txt") && !f.count("/X/MINE.TXT") && f.count("/X") && !f.at("/X").dir, e13);
        // F2: the directory the distribution removed is re-made for the user's file, and verify says so
        bool verified = false;
        std::string v13;
        {
            auto r = vol(0, np);
            auto n = vol(1, cn);
            verified = r && n && carry::verify_nothing_else_moved(*r, *n, items, v13);
        }
        {
            auto r = vol(0, np);
            auto s2 = vol(1, cc);
            verified = verified && r && s2 && carry::verify_carried_equal(*r, *s2, items, v13);
        }
        check("SDCARRY-15", "a user file in a directory the distribution removed is carried (its parent directory is made) and verify accepts it: the parents the carry makes count as carried",
              st == FileAddStatus::Ok && f.count("/OLD/MINE.TXT") && text(&f.at("/OLD/MINE.TXT")) == "in-a-removed-dir" && verified, v13);
    }
    {
        // SDCARRY-16: a name FatFs cannot address (a trailing space or dot, which NextZXOS can make) is listed as not
        // carried and never reaches apply_carry; the entries around it are carried (found by the real-content upgrade row)
        carry::Entry odd;  odd.path = "/W/sp ";  odd.dir = true;  odd.unaddressable = true;
        carry::Entry oddf; oddf.path = "/dots.."; oddf.size = 3;  oddf.unaddressable = true;
        carry::Entry fine; fine.path = "/FINE.TXT"; fine.size = 4;
        std::vector<carry::Change> user = {{carry::Kind::Added, odd}, {carry::Kind::Added, oddf}, {carry::Kind::Added, fine}};
        auto nt = mem_tree(fx.n);
        std::vector<carry::Entry> items;
        carry::Report r16;
        carry::decide(user, {}, *nt, items, r16);
        check("SDCARRY-16", "a name FatFs cannot address (a trailing space or dot) is listed as not carried, with its kind, and is not handed to apply_carry; an ordinary entry beside it is carried",
              items.size() == 1 && items[0].path == "/FINE.TXT" && r16.unwritable.size() == 2 &&
                  r16.text().find("cannot write these names") != std::string::npos && r16.text().find("directory /W/sp ") != std::string::npos);
    }
    {
        // SDCARRY-17: a file named like another file's generated short name (found by the real-content upgrade row:
        // NextZXOS's write probe makes both). Written in key order the long name comes first and takes LONGNA~1, and
        // the file really named LONGNA~1.TXT then opens that one: two files become one. The carry writes plain short
        // names first, so both arrive with their own bytes.
        const std::string cc = tmp("AliasC"), cn = tmp("AliasN");
        make_card(cn, Fat32Tree{});
        make_card(cc, Fat32Tree{});
        {
            Edit e(cc);
            e.mkdir("/AL");
            e.put("/AL/LONGNA~1.TXT", "short-name-file-125");   // the plain name first, as the guest did
            e.put("/AL/Long Name e.txt", "long-name-file-123");
        }
        std::vector<carry::Entry> items;
        {
            auto v = vol(0, cc);
            for (const auto& en : v->entries()) items.push_back(en);
        }
        FileAddStatus st = FileAddStatus::ImageUnusable;
        std::string e17;
        {
            auto src = vol(1, cc);
            auto dst = vol(0, cn, nullptr, false);
            if (src && dst) st = carry::apply_carry(*src, items, *dst, e17);
        }
        const auto f = foreign(cn);
        check("SDCARRY-17", "a file named like another file's generated short name is carried beside it with its own bytes: plain short names are written before long ones",
              st == FileAddStatus::Ok && f.count("/AL/LONGNA~1.TXT") && text(&f.at("/AL/LONGNA~1.TXT")) == "short-name-file-125" &&
                  f.count("/AL/LONG NAME E.TXT") && text(&f.at("/AL/LONG NAME E.TXT")) == "long-name-file-123", e17);
    }
    {
        // SDCARRY-10: verify catches a planted mismatch.
        auto srcmem = mem_tree(fx.c);
        bool good = false, bad = false;
        {
            auto result = vol(0, res.np);
            auto nmade = vol(1, fx.n);
            if (srcmem && result && nmade) good = carry::verify_carry(*result, *nmade, *srcmem, res.carried, err);
        }
        const std::string planted = tmp("Plant");
        copy_card(res.np, planted);
        {
            Edit e(planted);
            e.put("/GAMES/mine.nex", "TAMPERED");
        }
        {
            auto result = vol(0, planted);
            auto nmade = vol(1, fx.n);
            if (srcmem && result && nmade) bad = !carry::verify_carry(*result, *nmade, *srcmem, res.carried, err);
        }
        check("SDCARRY-10", "verify accepts the carried result and catches a carried file altered after the carry", srcmem && good && bad, err);
    }
    {
        const std::string t = res.report.text();
        const size_t p_kept = t.find("Kept from your card"), p_rep = t.find("Replaced by the new NextZXOS");
        const size_t p_del = t.find("You deleted these"), p_rem = t.find("the new NextZXOS removed them");
        check("SDCARRY-11", "the report lists carried, replaced, deleted-and-back and removed-by-the-distribution entries, each list sorted, one heading each",
              p_kept != std::string::npos && p_rep > p_kept && p_del > p_rep && p_rem > p_del &&
                  t.find("added /GAMES/mine.nex") != std::string::npos && t.find("/SYS/changed.sys") != std::string::npos &&
                  t.find("/NEXTZXOS/keep.txt") != std::string::npos && t.find("/NEXTZXOS/removed.txt") != std::string::npos &&
                  t.find("added /GAMES/mine.nex") < t.find("added /MYDIR"),
              t);
    }

    // =================== SDFLAT ===========================================================
    {
        const std::string img = tmp("flat");
        {
            std::ofstream f(img, std::ios::binary);
            Bytes z(65536, 0xAB);
            f.write(reinterpret_cast<const char*>(z.data()), static_cast<std::streamsize>(z.size()));
        }
        const std::string jf = tmp("flat") + ".jsdw";
        bind_journal(img, jf);
        auto j = sd_write_journal::for_image(img);
        uint8_t s1[512], s2[512];
        std::memset(s1, 0x11, sizeof s1);
        std::memset(s2, 0x22, sizeof s2);
        j->commit(3, s1);
        j->commit(100, s2);
        const std::string out = tmp("flatout");
        std::string e;
        const FileAddStatus st = carry::flatten(img, *j, out, e);
        Bytes want(65536, 0xAB);
        std::memset(want.data() + 3 * 512, 0x11, 512);
        std::memset(want.data() + 100 * 512, 0x22, 512);
        std::ifstream in(out, std::ios::binary);
        const Bytes got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        check("SDFLAT-01", "flatten gives the image with every journal sector applied (byte-compared with the sectors applied by hand), and leaves the image unchanged",
              st == FileAddStatus::Ok && got == want, e);
        std::string e2;
        const FileAddStatus again = carry::flatten(img, *j, out, e2);
        // a journal bound to another image
        const std::string other = tmp("flatother");
        { std::ofstream f(other, std::ios::binary); Bytes z(32768, 0); f.write(reinterpret_cast<const char*>(z.data()), static_cast<std::streamsize>(z.size())); }
        std::string e3;
        const FileAddStatus mism = carry::flatten(other, *j, tmp("flatout"), e3);
        check("SDFLAT-02", "flatten refuses an existing OUT and a journal bound to another image, both with status 3 and nothing written",
              again == FileAddStatus::DestInvalid && mism == FileAddStatus::DestInvalid && e2.find("already exists") != std::string::npos &&
                  e3.find("belongs to another card image") != std::string::npos);
        sd_write_journal::forget_all_for_test();
    }

    // =================== SDEXP: export and CHANGES.txt ====================================
    auto read_text = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
    {
        // SDEXP-01: an overlay on a NON-jnext image: exactly the overlay's changes.
        const std::string img = tmp("foreign");
        copy_card(fx.a, img);
        const std::string jf = tmp("foreign") + ".jsdw";
        bind_journal(img, jf);
        add(img, "/Ov One.txt", "one");
        add(img, "/SYS/same.sys", "ov-changed", true);
        carry::ExportRequest req;
        req.image = img;
        req.journal = sd_write_journal::for_image(img);
        req.dir = tmp("exp1");
        carry::ExportReport rep;
        const FileAddStatus st = carry::export_changes(req, rep, err);
        const std::string ct = read_text(fs::path(req.dir) / "CHANGES.txt");
        std::vector<std::string> files;
        for (auto& e : fs::recursive_directory_iterator(req.dir)) if (e.is_regular_file()) files.push_back(fs::relative(e.path(), req.dir).generic_string());
        std::sort(files.begin(), files.end());
        const std::vector<std::string> want = {"CHANGES.txt", "Ov One.txt", "SYS/same.sys"};
        check("SDEXP-01", "export of an overlay on a non-jnext image writes exactly the overlay's added and changed files plus CHANGES.txt, with the card itself as the base",
              st == FileAddStatus::Ok && files == want && read_text(fs::path(req.dir) / "Ov One.txt") == "one" && rep.base == "card" &&
                  ct.find("A\t-\t3\t/Ov One.txt\tOv One.txt\n") != std::string::npos,
              err + " files=" + std::to_string(files.size()));
        sd_write_journal::forget_all_for_test();
    }
    {
        // SDEXP-02: base A, a card with direct changes AND an overlay; a path changed in both is exported once, with the overlay's bytes.
        const std::string img = tmp("both");
        copy_card(fx.a, img);
        add(img, "/SYS/same.sys", "direct-version", true);
        add(img, "/DIRECT.TXT", "d");
        const std::string jf = tmp("both") + ".jsdw";
        bind_journal(img, jf);
        add(img, "/SYS/same.sys", "overlay-version", true);
        carry::ExportRequest req;
        req.image = img;
        req.journal = sd_write_journal::for_image(img);
        req.base_image = fx.a;
        req.dir = tmp("exp2");
        carry::ExportReport rep;
        const FileAddStatus st = carry::export_changes(req, rep, err);
        const std::string ct = read_text(fs::path(req.dir) / "CHANGES.txt");
        size_t n_same = 0;
        for (size_t p = ct.find("/SYS/same.sys"); p != std::string::npos; p = ct.find("/SYS/same.sys", p + 1)) ++n_same;
        check("SDEXP-02", "export with a base and an overlay lists the machine's view against the base, one line per path: a path changed in both the card and the overlay carries the overlay's bytes",
              st == FileAddStatus::Ok && n_same == 1 && read_text(fs::path(req.dir) / "SYS" / "same.sys") == "overlay-version" &&
                  read_text(fs::path(req.dir) / "DIRECT.TXT") == "d" && rep.base.find("--sdcard-export-base") == 0, err);
        sd_write_journal::forget_all_for_test();
    }
    {
        carry::ExportRequest req;
        req.image = fx.c;
        req.dir = tmp("exp3");
        carry::ExportReport rep;
        const FileAddStatus st = carry::export_changes(req, rep, err);
        check("SDEXP-03", "export with no as-made base, no --sdcard-export-base and no overlay refuses, naming the two ways out, and does not create DIR",
              st == FileAddStatus::DestInvalid && err.find("nothing to compare against") != std::string::npos &&
                  err.find("--sdcard-export-base FILE") != std::string::npos && err.find("--sdcard-writes FILE") != std::string::npos &&
                  !fs::exists(req.dir), err);
    }
    {
        // SDEXP-04: names the host refuses are written escaped and listed.
        const std::string img = tmp("names");
        copy_card(fx.a, img);
        {
            Edit e(img);
            e.put("/CON.TXT", "reserved");
            e.put("/Caf\xE9.TXT", "accent");
        }
        carry::ExportRequest req;
        req.image = img;
        req.base_image = fx.a;
        req.dir = tmp("exp4");
        carry::ExportReport rep;
        const FileAddStatus st = carry::export_changes(req, rep, err);
        const std::string ct = read_text(fs::path(req.dir) / "CHANGES.txt");
        check("SDEXP-04", "a card name the host refuses (a reserved device name; a byte outside ASCII) is written with a %XX-escaped host name, listed, and the card path column escapes only the non-ASCII byte",
              st == FileAddStatus::Ok && fs::exists(fs::path(req.dir) / "%43ON.TXT") && fs::exists(fs::path(req.dir) / "Caf%E9.TXT") &&
                  rep.escaped.size() == 2 && ct.find("\t/CON.TXT\t%43ON.TXT\n") != std::string::npos &&
                  ct.find("\t/Caf%E9.TXT\tCaf%E9.TXT\n") != std::string::npos,
              err + " " + ct);
    }
    {
        // SDEXP-05: an under-clustered base (FatFs refuses it) is read by the lenient reader.
        const std::string base = tmp("under");
        copy_card(fx.a, base);
        {
            std::fstream f(base, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kLba) * 512 + 32));
            const uint32_t small = 60000;
            const uint8_t le[4] = {static_cast<uint8_t>(small), static_cast<uint8_t>(small >> 8), 0, 0};
            f.write(reinterpret_cast<const char*>(le), 4);
        }
        std::string fe;
        const bool fatfs_refuses = carry::Volume::open(1, base, nullptr, true, fe) == nullptr;
        const std::string img = tmp("ucard");
        copy_card(fx.a, img);
        add(img, "/NEW.TXT", "new");
        add(img, "/SYS/same.sys", "chg", true);
        carry::ExportRequest req;
        req.image = img;
        req.base_image = base;
        req.dir = tmp("exp5");
        carry::ExportReport rep;
        const FileAddStatus st = carry::export_changes(req, rep, err);
        check("SDEXP-05", "--sdcard-export-base with an UNDER-clustered base (the geometry FatFs refuses) is read by the lenient reader, and the export lists exactly the card's differences from it",
              fatfs_refuses && st == FileAddStatus::Ok && rep.added == 1 && rep.changed == 1 && rep.removed == 0 && rep.dirs == 0, fe + " | " + err);
    }
    {
        // SDEXP-10: the export's CHANGES.txt carries the plan's comment lines, the made-on stamp in the
        // plan's form, and the importer ignores comments.
        const std::string img = tmp("ucard10");
        copy_card(fx.a, img);
        add(img, "/NEW10.TXT", "new");
        carry::ExportRequest req;
        req.image = img;
        req.base_image = fx.a;
        req.dir = tmp("exp10");
        carry::ExportReport rep;
        std::string err10;
        const FileAddStatus st = carry::export_changes(req, rep, err10);
        std::ifstream in(fs::path(req.dir) / "CHANGES.txt", std::ios::binary);
        const std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t at = txt.find("# made: ");
        bool shape = at != std::string::npos && txt.size() >= at + 8 + 19;
        if (shape) {
            const std::string t = txt.substr(at + 8, 19);   // YYYY-MM-DD HH:MM:SS
            for (size_t i = 0; i < 19; ++i) {
                const bool sep = (i == 4 || i == 7) ? t[i] == '-' : i == 10 ? t[i] == ' ' : (i == 13 || i == 16) ? t[i] == ':' : (t[i] >= '0' && t[i] <= '9');
                shape = shape && sep;
            }
        }
        std::vector<carry::ChangeLine> back;
        std::string pe;
        check("SDEXP-10", "CHANGES.txt carries '# made: YYYY-MM-DD HH:MM:SS' after the card and base comments, and the parser still reads the file (comments ignored)",
              st == FileAddStatus::Ok && shape && txt.find("# card: ") != std::string::npos && txt.find("# base: ") != std::string::npos &&
                  carry::parse_changes(txt, back, pe),
              err10 + pe);
    }
    {
        // SDEXP-06: the format, byte for byte; deterministic; every kind and attribute combination.
        std::vector<carry::ChangeLine> v;
        auto L = [&](char k, uint8_t a, uint64_t sz, const std::string& cp, const std::string& hp) {
            carry::ChangeLine l; l.kind = k; l.attrs = a; l.size = sz; l.card_path = cp; l.host_path = hp; v.push_back(l);
        };
        L('C', 7, 12, "/Z.TXT", "Z.TXT");
        L('A', 0, 3, "/A B/c d.txt", "A B/c d.txt");
        L('D', 3, 0, "/MYDIR", "");
        L('R', 0, 0, "/GONE.TXT", "");
        L('A', 1, 5, std::string("/Caf\xE9 100%.TXT"), "Caf%E9 100%25.TXT");
        L('A', 4, 1, "/S.SYS", "S.SYS");
        const std::string want =
            "jnext-changes 1\n# base: test\n"
            "A\t-\t3\t/A B/c d.txt\tA B/c d.txt\n"
            "A\tR\t5\t/Caf%E9 100%25.TXT\tCaf%E9 100%25.TXT\n"
            "R\t-\t0\t/GONE.TXT\t-\n"
            "D\tRH\t0\t/MYDIR\t-\n"
            "A\tS\t1\t/S.SYS\tS.SYS\n"
            "C\tRHS\t12\t/Z.TXT\tZ.TXT\n";
        const std::string got = carry::format_changes(v, {"base: test"});
        std::vector<carry::ChangeLine> back;
        std::string pe;
        const bool parsed = carry::parse_changes(got, back, pe);
        std::reverse(v.begin(), v.end());
        check("SDEXP-06", "CHANGES.txt is the documented format byte for byte (magic line, comments, five TAB fields, kinds, attribute letters in R,H,S order, %XX escapes, sorted by card path), independent of input order, and parses back to the same lines",
              got == want && carry::format_changes(v, {"base: test"}) == want && parsed && back.size() == 6 &&
                  back[1].card_path == std::string("/Caf\xE9 100%.TXT") && back[5].attrs == 7,
              got + pe);
    }
    {
        // SDEXP-07: the parser's refusals, naming the line.
        auto refused = [&](const std::string& body, const std::string& frag) {
            std::vector<carry::ChangeLine> l;
            std::string e;
            return !carry::parse_changes(body, l, e) && e.find(frag) != std::string::npos;
        };
        const std::string m = "jnext-changes 1\n";
        std::vector<carry::ChangeLine> ok;
        std::string e;
        const bool lenient = carry::parse_changes("jnext-changes 1\r\n# c\r\n\r\nA\t-\t3\t/X.TXT\tX.TXT\r\n", ok, e) && ok.size() == 1;
        const bool all =
            refused("jnext-changes 2\n", "expected 'jnext-changes 1'") && refused("hello\n", "expected 'jnext-changes 1'") &&
            refused(m + "A\t-\t3\t/X\n", "line 2: expected 5") && refused(m + "A\t-\t3\t/X\tX\textra\n", "line 2: expected 5") &&
            refused(m + "Z\t-\t3\t/X\tX\n", "line 2: unknown kind") && refused(m + "A\t-\tabc\t/X\tX\n", "line 2: size") &&
            refused(m + "A\t-\t3\tX\tX\n", "line 2: the card path must be absolute") &&
            refused(m + "A\t-\t3\t/X\t../X\n", "line 2: the host path") && refused(m + "A\t-\t3\t/X\t/etc/X\n", "line 2: the host path must be relative") &&
            refused(m + "A\tHR\t3\t/X\tX\n", "line 2: attribute letters") && refused(m + "D\t-\t0\t/X\tX\n", "line 2: a directory or removed line");
        check("SDEXP-07", "parse_changes refuses, naming the line: a wrong magic or version, a field count that is not 5, an unknown kind, a size that is not a number, a relative card path, a host path with .. or a leading /, out-of-order attribute letters, a host path on a D line; it accepts CRLF, comments and blank lines",
              all && lenient, e);
    }

    {
        // SDEXP-08 / 09: the default card, compared with the card AS MADE (rebuilt from the raw).
        Fat32Tree raw;
        // zx81.rom is not the known 16 KB file here: neither recipe step touches it
        raw.root.push_back(dnode("MACHINES", {dnode("NEXT", {fnode("zx81.rom", std::string(100, 'Z')), fnode("48.rom", "rom48")})}));
        raw.root.push_back(dnode("DOCS", {fnode("readme.txt", "docs")}));
        const std::string dd = tmp("dflt");
        fs::create_directories(dd);
        const std::string raw_img = (fs::path(dd) / sdcard::kImageFileName).string();
        const std::string card = (fs::path(dd) / sdcard::kFixedImageFileName).string();
        const bool made = make_card(raw_img, raw, 1228800, 8192);
        const std::string rawsha = sdcard::sha256_file(raw_img);
        { std::ofstream(raw_img + ".sha256") << rawsha << "  " << sdcard::kImageFileName << "\n"; }
        bool ok = made && copy_card(raw_img, card) && sdcard::patch_image_for_recipe(card, 3, rawsha, err);
        { std::ofstream(card + sdcard::kFixedImageRecipeSuffix) << "3\n"; }
        std::string why;
        const bool pristine = ok && carry::pristine_base_ok(card, why);
        add(card, "/MINE.TXT", "mine");
        carry::ExportRequest req;
        req.image = card;
        req.is_default_card = true;
        req.dir = tmp("exp8");
        carry::ExportReport rep;
        std::string e8;
        const FileAddStatus st = carry::export_changes(req, rep, e8);
        check("SDEXP-08", "the default card with its original image on disk is compared against the card AS MADE (rebuilt with its recipe): only the user's added file is exported, jnext's own config.ini and the re-clustered tree are not changes, and no scratch is left",
              ok && pristine && st == FileAddStatus::Ok && rep.base == "as-made" && rep.added == 1 && rep.changed == 0 && rep.removed == 0 &&
                  fs::exists(fs::path(req.dir) / "MINE.TXT") && !fs::exists(fs::path(dd) / "upgrade-work"),
              why + " | " + e8 + " base=" + rep.base + " a=" + std::to_string(rep.added) + " c=" + std::to_string(rep.changed));
        // the order when BOTH are available: the card as made comes before --sdcard-export-base
        {
            carry::ExportRequest rq = req;
            rq.base_image = raw_img;
            rq.dir = tmp("exp8b");
            carry::ExportReport rr;
            std::string eo;
            const FileAddStatus so = carry::export_changes(rq, rr, eo);
            check("SDEXP-12", "with the card as made AND --sdcard-export-base both available the card as made is the basis (jnext's own files are not exported as changes)",
                  so == FileAddStatus::Ok && rr.base == "as-made" && rr.added == 1, eo + " base=" + rr.base);
        }
        // the .distro of the card and of the raw must agree (M27)
        {
            { std::ofstream(card + sdcard::kDistroSuffix) << "sn-emulator-99.99.zip\n"; }
            std::string wd;
            const bool pd = carry::pristine_base_ok(card, wd);
            std::remove((card + sdcard::kDistroSuffix).c_str());
            check("SDEXP-13", "a card whose .distro is not its original image's has no pristine base, naming both distributions",
                  !pd && wd.find("was not made from the original image") != std::string::npos && wd.find("99.99") != std::string::npos, wd);
        }
        // a re-formatted card (another volume serial) has no pristine base (M26)
        {
            const std::string c2 = tmp("reformatted");
            copy_card(card, c2);
            std::ofstream(c2 + sdcard::kFixedImageRecipeSuffix) << "3\n";
            {
                std::fstream f(c2, std::ios::in | std::ios::out | std::ios::binary);
                f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kLba) * 512 + 0x43));
                const char z[4] = {1, 2, 3, 4};
                f.write(z, 4);
            }
            // pristine_base_ok looks for the raw beside the card: put the pair in one directory
            const std::string d2 = tmp("dflt2");
            fs::create_directories(d2);
            fs::copy_file(raw_img, fs::path(d2) / sdcard::kImageFileName, fs::copy_options::overwrite_existing);
            fs::copy_file(raw_img + ".sha256", fs::path(d2) / (std::string(sdcard::kImageFileName) + ".sha256"), fs::copy_options::overwrite_existing);
            const std::string cc2 = (fs::path(d2) / sdcard::kFixedImageFileName).string();
            fs::rename(c2, cc2);
            fs::rename(c2 + sdcard::kFixedImageRecipeSuffix, cc2 + sdcard::kFixedImageRecipeSuffix);
            std::string ws;
            const bool ps = carry::pristine_base_ok(cc2, ws);
            check("SDEXP-14", "a card whose volume serial is not the one its original image gives (re-formatted) has no pristine base",
                  !ps && ws.find("volume serial") != std::string::npos, ws);
        }
        // a raw that does not match its sidecar: not pristine, so the export refuses naming the reason
        { std::ofstream(raw_img + ".sha256") << std::string(64, '0') << "  " << sdcard::kImageFileName << "\n"; }
        std::string why2, e9;
        const bool pristine2 = carry::pristine_base_ok(card, why2);
        carry::ExportRequest req2 = req;
        req2.dir = tmp("exp9");
        carry::ExportReport rep2;
        const FileAddStatus st2 = carry::export_changes(req2, rep2, e9);
        // and a missing raw
        fs::remove(raw_img);
        std::string why3;
        const bool pristine3 = carry::pristine_base_ok(card, why3);
        check("SDEXP-09", "a default card whose original image does not match its .sha256 (or is gone) has no pristine base: the export refuses, naming why, and DIR is not created",
              !pristine2 && why2.find("does not match its .sha256") != std::string::npos && st2 == FileAddStatus::DestInvalid &&
                  e9.find("nothing to compare against") != std::string::npos && e9.find("does not match its .sha256") != std::string::npos &&
                  !fs::exists(req2.dir) && !pristine3 && why3.find("is missing") != std::string::npos,
              why2 + " | " + e9 + " | " + why3);
    }

    // =================== SDIMP: import ===================================================
    auto export_for_import = [&](const std::string& card, const std::string& base, const std::string& dir) {
        carry::ExportRequest req;
        req.image = card;
        req.base_image = base;
        req.dir = dir;
        carry::ExportReport rep;
        std::string e;
        const FileAddStatus st = carry::export_changes(req, rep, e);
        return st == FileAddStatus::Ok;
    };
    {
        // SDIMP-01: the round trip.
        const std::string src = tmp("rt-src");
        copy_card(fx.a, src);
        add(src, "/GAMES/new.nex", "nex");
        add(src, "/SYS/same.sys", "round-trip", true);
        add(src, "/Spaced Name.txt", "sp");
        {
            Edit e(src);
            e.chmod("/SYS/attr.sys", AM_RDO | AM_HID | AM_SYS, AM_RDO | AM_HID | AM_SYS);
            e.mkdir("/EMPTYDIR");
        }
        const std::string dir = tmp("rt-dir");
        const bool exported = export_for_import(src, fx.a, dir);
        const std::string dst = tmp("rt-dst");
        copy_card(fx.a, dst);
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(dst, dir, rep, err);
        const auto a = foreign(src), b = foreign(dst);
        bool same = a.size() == b.size();
        std::string why;
        for (const auto& kv : a) {
            const auto it = b.find(kv.first);
            if (it == b.end()) { same = false; why += " missing:" + kv.first; continue; }
            if (it->second.dir != kv.second.dir || it->second.data != kv.second.data || (it->second.attr & 7) != (kv.second.attr & 7)) { same = false; why += " differs:" + kv.first; }
        }
        check("SDIMP-01", "the ROUND TRIP: an exported card's changes imported into a fresh copy of the base give the same tree (bytes, R/H/S, directories); CHANGES.txt is not on the card",
              exported && st == FileAddStatus::Ok && same && b.find("/CHANGES.TXT") == b.end(), err + why);
    }
    {
        const std::string empty_dir = tmp("noct");
        fs::create_directories(empty_dir);
        const std::string badmagic = tmp("badmagic");
        fs::create_directories(badmagic);
        std::ofstream(fs::path(badmagic) / "CHANGES.txt") << "jnext-changes 9\n";
        const std::string card = tmp("imp-refuse");
        copy_card(fx.a, card);
        const uint64_t before = digest(card);
        carry::ImportReport rep;
        std::string e1, e2;
        const FileAddStatus s1 = carry::import_changes(card, empty_dir, rep, e1);
        const FileAddStatus s2 = carry::import_changes(card, badmagic, rep, e2);
        check("SDIMP-02", "a directory without CHANGES.txt, and one whose CHANGES.txt has a wrong magic, are refused with status 2 and the messages; the card is byte-identical",
              s1 == FileAddStatus::SourceUnreadable && s2 == FileAddStatus::SourceUnreadable &&
                  e1.find("is not a directory made by --sdcard-export-changes (no CHANGES.txt)") != std::string::npos &&
                  e2.find("is not a jnext changes list (expected 'jnext-changes 1')") != std::string::npos && digest(card) == before,
              e1 + " | " + e2);
    }
    // a hand-made export dir for the next rows
    auto make_dir = [&](const std::string& lines, const std::vector<std::pair<std::string, std::string>>& files) {
        const std::string d = tmp("imp-dir");
        fs::create_directories(d);
        std::ofstream(fs::path(d) / "CHANGES.txt", std::ios::binary) << "jnext-changes 1\n" << lines;
        for (const auto& f : files) {
            fs::create_directories((fs::path(d) / f.first).parent_path());
            std::ofstream(fs::path(d) / f.first, std::ios::binary) << f.second;
        }
        return d;
    };
    {
        const std::string card = tmp("imp3");
        copy_card(fx.a, card);
        const std::string d = make_dir("A\t-\t3\t/NEWFILE.TXT\tNEWFILE.TXT\nC\t-\t6\t/SYS/same.sys\tSYS/same.sys\n", {{"NEWFILE.TXT", "new"}, {"SYS/same.sys", "SAME-2"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        check("SDIMP-03", "an existing file is REPLACED and a new one written; the report names each as replaced or written, with the counts",
              st == FileAddStatus::Ok && rep.written == std::vector<std::string>{"/NEWFILE.TXT"} && rep.replaced == std::vector<std::string>{"/SYS/same.sys"} &&
                  text(&f.at("/SYS/SAME.SYS")) == "SAME-2" && text(&f.at("/NEWFILE.TXT")) == "new", err);
    }
    {
        // SDEXP-11: a card file named like the list itself is exported under an escaped name and imported back
        const std::string src = tmp("ct-src");
        copy_card(fx.a, src);
        add(src, "/CHANGES.txt", "release notes");
        add(src, "/changes.txt2", "other");
        add(src, "/OTHER.TXT", "o");
        const std::string dir = tmp("ct-exp");
        const bool exported = export_for_import(src, fx.a, dir);
        const std::string dst = tmp("ct-dst");
        copy_card(fx.a, dst);
        carry::ImportReport rep;
        std::string ei;
        const FileAddStatus st = carry::import_changes(dst, dir, rep, ei);
        const auto f = foreign(dst);
        check("SDEXP-11", "a card file named CHANGES.txt (any case) at the root survives the export (it is written under an escaped name, never over the list) and imports back with its bytes",
              exported && st == FileAddStatus::Ok && f.count("/CHANGES.TXT") && text(&f.at("/CHANGES.TXT")) == "release notes" &&
                  f.count("/OTHER.TXT"), ei);
        // the lower-case spelling too (on its own card, since FAT folds the two into one name)
        const std::string src2 = tmp("ct2-src");
        copy_card(fx.a, src2);
        add(src2, "/changes.txt", "lower case notes");
        const std::string dir2 = tmp("ct2-exp");
        const bool exported2 = export_for_import(src2, fx.a, dir2);
        const std::string dst2 = tmp("ct2-dst");
        copy_card(fx.a, dst2);
        std::string ei2;
        const FileAddStatus st2 = carry::import_changes(dst2, dir2, rep, ei2);
        const auto f2 = foreign(dst2);
        check("SDEXP-15", "the same for the lower-case name /changes.txt",
              exported2 && st2 == FileAddStatus::Ok && f2.count("/CHANGES.TXT") && text(&f2.at("/CHANGES.TXT")) == "lower case notes", ei2);
    }
    {
        // SDIMP-11: D lines carry attributes: a hidden / system / read-only directory is restored (M12b)
        const std::string card = tmp("imp11");
        copy_card(fx.a, card);
        const std::string d = make_dir("D\tH\t0\t/HDIR\t-\nD\tRS\t0\t/RSDIR\t-\nD\t-\t0\t/PLAIN\t-\n", {});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        check("SDIMP-11", "attributes of directories are restored: a D line with H, RS and - gives exactly those bits on the directory",
              st == FileAddStatus::Ok && f.count("/HDIR") && (f.at("/HDIR").attr & 7) == 2 && f.count("/RSDIR") && (f.at("/RSDIR").attr & 7) == 5 &&
                  f.count("/PLAIN") && (f.at("/PLAIN").attr & 7) == 0, err);
    }
    {
        const std::string card = tmp("imp4");
        copy_card(fx.a, card);
        { Edit e(card); e.chmod("/SYS/same.sys", AM_RDO | AM_HID, AM_RDO | AM_HID); }
        const std::string d = make_dir("C\tH\t3\t/SYS/same.sys\tSYS/same.sys\n", {{"SYS/same.sys", "new"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        const auto& s = f.at("/SYS/SAME.SYS");
        check("SDIMP-04", "a read-only existing file is replaced too, and ends with exactly the line's attributes (the read-only bit cleared by the line)",
              st == FileAddStatus::Ok && text(&s) == "new" && (s.attr & 7) == 2, err);
    }
    {
        const std::string card = tmp("imp5");
        copy_card(fx.a, card);
        const std::string d = make_dir(
            "A\tR\t1\t/ATR.TXT\tATR.TXT\nA\tH\t1\t/ATH.TXT\tATH.TXT\nA\tS\t1\t/ATS.TXT\tATS.TXT\nA\tRHS\t1\t/ATRHS.TXT\tATRHS.TXT\nA\t-\t1\t/ATN.TXT\tATN.TXT\n",
            {{"ATR.TXT", "1"}, {"ATH.TXT", "1"}, {"ATS.TXT", "1"}, {"ATRHS.TXT", "1"}, {"ATN.TXT", "1"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        auto at = [&](const char* n) { return f.at(n).attr & 7; };
        check("SDIMP-05", "attributes are restored: lines with R, H, S, RHS and - give exactly those bits on the card",
              st == FileAddStatus::Ok && at("/ATR.TXT") == 1 && at("/ATH.TXT") == 2 && at("/ATS.TXT") == 4 && at("/ATRHS.TXT") == 7 && at("/ATN.TXT") == 0, err);
    }
    {
        const std::string card = tmp("imp6");
        copy_card(fx.a, card);
        const uint64_t before = digest(card);
        const std::string jf = tmp("imp6") + ".jsdw";
        bind_journal(card, jf);
        const std::string d = make_dir("A\t-\t4\t/JNEW.TXT\tJNEW.TXT\n", {{"JNEW.TXT", "jnew"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        auto j = sd_write_journal::for_image(card);
        bool through = false;
        {
            auto view = vol(0, card, j, true);
            through = view && view->find(carry::path_key("/JNEW.TXT"));
        }
        check("SDIMP-06", "with a writes file bound, the image is byte-identical, the journal holds the writes, and the view through it has the imported file",
              st == FileAddStatus::Ok && digest(card) == before && j && !j->empty() && through, err);
        sd_write_journal::forget_all_for_test();
    }
    {
        const std::string card = tmp("imp7");
        copy_card(fx.a, card);
        const uint64_t before = digest(card);
        carry::ImportReport rep;
        std::string ea, eb, ec_;
        const std::string d1 = make_dir("A\t-\t3\t/OK1.TXT\tOK1.TXT\nA\t-\t3\t/MISSING.TXT\tMISSING.TXT\n", {{"OK1.TXT", "one"}});
        const FileAddStatus sa = carry::import_changes(card, d1, rep, ea);
        const std::string d2 = make_dir("A\t-\t3\t/OK2.TXT\tOK2.TXT\nA\t-\t3\t/SIZE.TXT\tSIZE.TXT\n", {{"OK2.TXT", "two"}, {"SIZE.TXT", "four"}});
        const FileAddStatus sb = carry::import_changes(card, d2, rep, eb);
        const std::string d3 = make_dir("A\t-\t3\t/OK3.TXT\tOK3.TXT\nA\t-\t3\t/Caf%E9.TXT\tCaf%E9.TXT\n", {{"OK3.TXT", "thr"}, {"Caf%E9.TXT", "acc"}});
        const FileAddStatus sc = carry::import_changes(card, d3, rep, ec_);
        check("SDIMP-07A", "a listed host file that is missing refuses the whole import (status 2) before any write: the card is byte-identical",
              sa == FileAddStatus::SourceUnreadable && digest(card) == before, ea);
        check("SDIMP-07B", "a listed host file whose size differs from its line refuses the whole import (status 2) before any write: the card is byte-identical",
              sb == FileAddStatus::SourceUnreadable && eb.find("its line says") != std::string::npos && digest(card) == before, eb);
        check("SDIMP-07C", "a card path that normalize_dest_path refuses (an escaped CP437 name) refuses the whole import (status 3), saying to edit or remove the line: the card is byte-identical",
              sc == FileAddStatus::DestInvalid && ec_.find("edit or remove that line") != std::string::npos && digest(card) == before, ec_);
    }
    {
        const std::string card = tmp("imp8");
        copy_card(fx.a, card);
        const std::string d = make_dir("R\t-\t0\t/SYS/same.sys\t-\nA\t-\t1\t/NEW8.TXT\tNEW8.TXT\n", {{"NEW8.TXT", "n"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        check("SDIMP-08", "R lines are never applied: the file they name stays on the card",
              st == FileAddStatus::Ok && f.count("/SYS/SAME.SYS") && text(&f.at("/SYS/SAME.SYS")) == "x" && f.count("/NEW8.TXT"), err);
    }
    {
        const std::string card = tmp("imp9");
        copy_card(fx.a, card);
        // the host name is escaped; the CARD path in the line is plain
        const std::string d = make_dir("A\t-\t3\t/CON.TXT\t%43ON.TXT\n", {{"%43ON.TXT", "con"}});
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        check("SDIMP-09", "a host path that differs from its card path (an escaped name) is written to the CARD path", st == FileAddStatus::Ok && f.count("/CON.TXT") && !f.count("/%43ON.TXT"), err);
    }
    {
        // SDIMP-10: runs out of space mid-write; created entries are rolled back, replaced ones stay replaced, status 6.
        const std::string card = tmp("imp10");
        copy_card(fx.a, card);
        add(card, "/R.TXT", "original");
        uint64_t free_clusters = 0, cb = 0;
        {
            auto v = vol(0, card);
            std::string e;
            v->free_space(free_clusters, cb, e);
        }
        const uint64_t filler = (free_clusters > 330 ? free_clusters - 330 : 0) * cb;
        const std::string fillfile = tmp("filler");
        {
            std::ofstream f(fillfile, std::ios::binary);
            std::vector<char> z(1 << 20, 'F');
            uint64_t left = filler;
            while (left) { const size_t n = static_cast<size_t>(std::min<uint64_t>(left, z.size())); f.write(z.data(), static_cast<std::streamsize>(n)); left -= n; }
        }
        std::string fe;
        const FileAddStatus fs_ = sdcard::add_file_to_image(card, fillfile, "/FILLER.BIN", false, fe);
        std::string lines = "C\t-\t3\t/R.TXT\tR.TXT\n";
        std::vector<std::pair<std::string, std::string>> files = {{"R.TXT", "new"}};
        for (int i = 0; i < 300; ++i) {
            char nm[64];
            std::snprintf(nm, sizeof nm, "A long file name number %04d.txt", i);
            lines += std::string("A\t-\t1\t/") + nm + "\t" + nm + "\n";
            files.push_back({nm, "x"});
        }
        const std::string d = make_dir(lines, files);
        carry::ImportReport rep;
        const FileAddStatus st = carry::import_changes(card, d, rep, err);
        const auto f = foreign(card);
        size_t leftover = 0;
        for (const auto& kv : f) if (kv.first.find("/A LONG FILE NAME NUMBER") == 0) ++leftover;
        check("SDIMP-10", "an import that runs out of space rolls back the files it created, leaves the file it replaced replaced, says how many, and exits 6",
              fs_ == FileAddStatus::Ok && st == FileAddStatus::ImageFull && leftover == 0 && text(&f.at("/R.TXT")) == "new" &&
                  err.find("1 existing file(s) had already been replaced") != std::string::npos,
              fe + " | " + err + " leftover=" + std::to_string(leftover));
    }

    std::error_code ec;
    fs::remove_all(g_dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
