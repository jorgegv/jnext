// SD card file operations: probe, list, stat, rename, make directory, remove (GH #558), src/core/sdcard_card_ops.
//
// Oracles:
//   - The issue text and the plan: which cards are writable (the first FAT32-LBA partition, the volume
//     the writers use), refuse-and-name for a read-only entry, never overwrite, byte-identical refusals,
//     every change through the writes file when one is bound.
//   - FatFs R0.15b (third_party/fatfs/ff.c, the library the firmware runs): how a long or 8.3 name is
//     matched (create_name, dir_find), what f_readdir returns (get_fileinfo), f_rename's own-entry rule.
//   - The FAT specification (fatgen103): the layout of the hand-made directory entries; the boot-sector
//     fields read raw to find the root directory sector (SDOPS-24).
// Listings are checked against what the fixtures wrote through FatFs; writes against a re-mount, the image
// digest, and the bytes extract_from_image (an independent reader) returns.
//
// Fixtures are built at run time (FatFs f_mkfs plus byte pokes); never a NextZXOS image, in any form.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/sdcard_card_ops.h"

#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_file_extract.h"
#include "core/sha256.h"
#include "peripheral/sd_card.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdcard::FileAddStatus;
namespace ops = sdcard::cardops;

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

}  // namespace

#include "../sdcard_file_extract/sdfx_helpers.h"

namespace {

using Tree = std::map<std::string, Bytes>;
using St = FileAddStatus;

const Bytes kWotef = payload(1500, 1);      // 3 clusters
const Bytes kFileBin = payload(400, 4);
const Bytes kLong = payload(300, 3);
const Bytes kAttr = payload(100, 10);
const Bytes kX = payload(333, 11);
const Bytes kA = payload(700, 5);
const Bytes kB = payload(1, 6);
const Bytes kC = payload(600, 7);
const Bytes kD1 = payload(200, 21), kD2 = payload(210, 22);
const Bytes kQ = payload(90, 12), kOk = payload(95, 13);

bool populate_general(FATFS&, Pokes&) {
    bool ok = f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef) &&
              f_mkdir("0:/GAMES/SUB") == FR_OK && f_mkdir("0:/GAMES/SUB/DEEP") == FR_OK &&
              fat_write("0:/GAMES/SUB/DEEP/X.BIN", kX);
    ok = ok && f_mkdir("0:/MYDIR") == FR_OK && fat_write("0:/MYDIR/A.BIN", kA) && fat_write("0:/MYDIR/B.BIN", kB) &&
         f_mkdir("0:/MYDIR/SUB") == FR_OK && fat_write("0:/MYDIR/SUB/C.BIN", kC) && f_mkdir("0:/MYDIR/EMPTYD") == FR_OK;
    ok = ok && f_mkdir("0:/EMPTYD") == FR_OK && fat_write("0:/FILE.BIN", kFileBin) &&
         fat_write("0:/Long File Name.txt", kLong) && fat_write("0:/R.BIN", kAttr) && fat_write("0:/H.BIN", kAttr) &&
         fat_write("0:/S.BIN", kAttr);
    ok = ok && f_chmod("0:/R.BIN", AM_RDO, AM_RDO) == FR_OK && f_chmod("0:/H.BIN", AM_HID, AM_HID) == FR_OK &&
         f_chmod("0:/S.BIN", AM_SYS, AM_SYS) == FR_OK;
    ok = ok && f_mkdir("0:/RO") == FR_OK && fat_write("0:/RO/A_RO.BIN", kQ) &&
         f_chmod("0:/RO/A_RO.BIN", AM_RDO, AM_RDO) == FR_OK && fat_write("0:/RO/OK.BIN", kOk) &&
         f_mkdir("0:/RO/SUB") == FR_OK && fat_write("0:/RO/SUB/Q.BIN", kQ);
    return ok;
}

// /ODD: the short entry NAME and a long entry "NAME." (FatFs cannot open it by that name: it strips the
// dot and would open NAME); /ESC: a long name holding an ESC.
bool populate_odd(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/ODD") != FR_OK || f_mkdir("0:/ESC") != FR_OK ||
        !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
        return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/ODD"), 2,
             {{u"", "NAME       ", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"NAME.", "NAME~1     ", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    hand_dir(fsv, dir_cluster("0:/ESC"), 2,
             {{u"\u001b[2Jevil", "EVIL~1     ", 0x20, c1, static_cast<uint32_t>(kD1.size())}}, pokes);
    return c1 != 0 && c2 != 0;
}

// A FAT32 volume, partition-less, whose BPB says 32456 data clusters (1 sector each): FatFs, like the
// Next's firmware, refuses it.
bool make_under_clustered(const std::string& path) {
    const uint32_t tot = 33000, rsv = 32, fatsz = 256;
    Bytes img(static_cast<size_t>(rsv + 2 * fatsz + 4) * 512, 0);
    uint8_t* b = img.data();
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    put16(b + 11, 512);
    b[13] = 1;
    put16(b + 14, rsv);
    b[16] = 2;
    put32(b + 32, tot);
    put32(b + 36, fatsz);
    put32(b + 44, 2);
    std::memcpy(b + 0x52, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(tot) * 512 - 1));
    const char z = 0;
    f.write(&z, 1);
    return f.good();
}

void bind_journal(const std::string& image, const std::string& jf) {
    std::error_code ec;
    fs::remove(jf, ec);
    sd_write_journal::Binding b;
    b.bytes = fs::file_size(image);
    b.sha256.fill(0x5C);
    std::string why;
    sd_write_journal::bind(image, jf, b, false, why);
}

// A private copy of a master card, removed when the block ends.
struct Img {
    std::string p;
    explicit Img(const std::string& master) {
        p = tmp_name("c") + ".img";
        fs::copy_file(master, p, fs::copy_options::overwrite_existing);
    }
    ~Img() { std::error_code ec; fs::remove(p, ec); }
    Img(const Img&) = delete;
    Img& operator=(const Img&) = delete;
};

struct Ls {
    St st = St::Ok;
    std::string err;
    std::vector<ops::CardEntry> e;
};
Ls ls(const std::string& img, const std::string& dir) {
    Ls r;
    r.st = ops::list_dir(img, dir, r.e, r.err);
    return r;
}
// "A/,B.BIN": directories carry a trailing '/'.
std::string names(const Ls& l) {
    std::string s;
    for (const auto& x : l.e) { if (!s.empty()) s += ","; s += x.name + (x.dir ? "/" : ""); }
    return s;
}
std::string names_of(const std::string& img, const std::string& dir) { return names(ls(img, dir)); }
const ops::CardEntry* find(const Ls& l, const std::string& name) {
    for (const auto& x : l.e) if (x.name == name) return &x;
    return nullptr;
}

bool exists(const std::string& img, const std::string& path, bool* is_dir = nullptr) {
    bool ex = false, d = false;
    std::string err;
    const St st = ops::stat(img, path, ex, d, err);
    if (is_dir) *is_dir = d;
    return st == St::Ok && ex;
}

// What extract_from_image (an independent reader: it opens the card read-only, writes file overlay) puts on the host.
Tree extracted(const std::string& img, const std::string& card_path) {
    const fs::path d = g_dir / ("x-" + std::to_string(++g_n));
    fs::create_directories(d);
    std::string err;
    sdcard::extract_from_image(img, card_path, d.string(), err);
    Tree t = tree_of(d);
    std::error_code ec;
    fs::remove_all(d, ec);
    return t;
}

uint32_t free_clusters(const std::string& img, uint32_t lba = kLba, uint32_t sectors = kFat32Sectors) {
    std::string err;
    if (!fatfs_glue::attach(0, img, lba, sectors, err, nullptr, true)) return 0xFFFFFFFFu;
    static FATFS f;
    f = FATFS{};
    DWORD n = 0xFFFFFFFFu;
    FATFS* fp = nullptr;
    if (f_mount(&f, "0:", 1) == FR_OK) f_getfree("0:", &n, &fp);
    f_mount(nullptr, "0:", 0);
    fatfs_glue::detach(0);
    return n;
}

std::string general_names = "EMPTYD/,GAMES/,MYDIR/,RO/,FILE.BIN,H.BIN,Long File Name.txt,R.BIN,S.BIN";

std::string info(const Ls& l) { return "st=" + std::to_string(static_cast<int>(l.st)) + " " + l.err + " names=" + names(l); }

std::string stn(St s) { return std::to_string(static_cast<int>(s)); }

}  // namespace

int main() {
    g_dir = make_scratch_dir("sdops");
    sd_write_journal::forget_all_for_test();

    const std::string G = tmp_name("general") + ".img";
    const int gt = make_card(G, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_general);
    const std::string O = tmp_name("odd") + ".img";
    make_card(O, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_odd);
    const std::string F16 = tmp_name("fat16") + ".img";
    const int t16 = make_card(F16, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS&, Pokes&) {
        return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
    });
    const std::string SFD = tmp_name("sfd") + ".img";
    const int tsfd = make_card(SFD, 0, kFat32Sectors, FM_FAT32, 512, 0, 0,
                               [&](FATFS&, Pokes&) { return fat_write("0:/FILE.BIN", kFileBin); });
    const std::string sha_g = sha_file(G);

    // host files used to put things on a card
    const fs::path host = g_dir / "host";
    fs::create_directories(host / "T" / "L1" / "L2");
    write_host_file(host / "three.bin", kWotef);
    write_host_file(host / "T" / "G.BIN", kA);
    write_host_file(host / "T" / "L1" / "H1.BIN", kB);
    write_host_file(host / "T" / "L1" / "L2" / "F.BIN", kC);

    // SDOPS-01
    {
        const Ls l = ls(G, "/");
        bool ok = l.st == St::Ok && names(l) == general_names;
        const ops::CardEntry* f = find(l, "FILE.BIN");
        const ops::CardEntry* lg = find(l, "Long File Name.txt");
        const ops::CardEntry* r = find(l, "R.BIN");
        const ops::CardEntry* h = find(l, "H.BIN");
        const ops::CardEntry* s = find(l, "S.BIN");
        const ops::CardEntry* gd = find(l, "GAMES");
        ok = ok && f && lg && r && h && s && gd && f->size == kFileBin.size() && !f->dir && f->attr == 0 &&
             lg->size == kLong.size() && lg->altname == "LONGFI~1.TXT" && lg->open_name == "Long File Name.txt" &&
             r->attr == 1 && h->attr == 2 && s->attr == 4 && gd->dir && gd->size == 0 && gd->open_name == "GAMES";
        check("SDOPS-01", "root of a partitioned FAT32 card: every entry, directories first then by name, sizes, R/H/S attributes, long name with its alias",
              gt == FS_FAT32 && ok, info(l));
    }
    // SDOPS-02
    {
        const Ls a = ls(G, "/GAMES/SUB/DEEP"), b = ls(G, ""), c = ls(G, "/"), d = ls(G, "games/sub"), e = ls(G, "/NOPE");
        check("SDOPS-02", "a directory two levels down lists; \"/\" and \"\" both mean the root; any case; a missing directory is SourceUnreadable",
              a.st == St::Ok && names(a) == "X.BIN" && a.e[0].size == kX.size() && names(b) == general_names &&
                  names(c) == general_names && names(d) == "DEEP/" && e.st == St::SourceUnreadable && e.e.empty(),
              info(a) + " | " + info(d) + " | " + stn(e.st));
    }
    // SDOPS-03
    {
        sd_write_journal::forget_all_for_test();
        Img c(G);
        const std::string jf = tmp_name("w") + ".sdw";
        bind_journal(c.p, jf);
        std::string err;
        const St st = sdcard::add_to_image(c.p, (host / "three.bin").string(), "/GAMES/THREE.BIN", false, err);
        const Ls l = ls(c.p, "/GAMES");
        const bool same = sha_file(c.p) == sha_g;
        const Tree t = extracted(c.p, "/GAMES/THREE.BIN");
        sd_write_journal::forget_all_for_test();
        check("SDOPS-03", "through a bound writes file: a file put there by add_to_image is listed, read back, and the image is byte-identical",
              st == St::Ok && same && names(l) == "SUB/,THREE.BIN,WOTEF.NEX" && t == Tree{{"THREE.BIN", kWotef}}, info(l) + " " + err);
    }
    // SDOPS-04
    {
        sd_write_journal::forget_all_for_test();
        Img c(G);
        const std::string jf = tmp_name("w") + ".sdw";
        bind_journal(c.p, jf);
        std::string err;
        sdcard::add_to_image(c.p, (host / "three.bin").string(), "/THREE.BIN", false, err);
        const std::string si = sha_file(c.p), sj = sha_file(jf);
        const auto mi = fs::last_write_time(c.p), mj = fs::last_write_time(jf);
        ops::CardInfo ci;
        const St p = ops::probe(c.p, ci, err);
        const Ls l = ls(c.p, "/MYDIR");
        bool ex = false, dr = false;
        const St s = ops::stat(c.p, "/MYDIR/SUB", ex, dr, err);
        const bool same = sha_file(c.p) == si && sha_file(jf) == sj && fs::last_write_time(c.p) == mi &&
                          fs::last_write_time(jf) == mj;
        sd_write_journal::forget_all_for_test();
        // and the same without a journal
        const std::string s0 = sha_file(G);
        const auto m0 = fs::last_write_time(G);
        ops::probe(G, ci, err);
        ls(G, "/");
        ops::stat(G, "/FILE.BIN", ex, dr, err);
        check("SDOPS-04", "probe, list and stat write nothing: image and writes file byte-identical, modification times unchanged",
              p == St::Ok && l.st == St::Ok && s == St::Ok && ex && same && sha_file(G) == s0 && fs::last_write_time(G) == m0,
              stn(p) + stn(l.st) + stn(s));
    }
    // SDOPS-05
    {
        ops::CardInfo ci;
        std::string err;
        const St p = ops::probe(F16, ci, err);
        const Ls l = ls(F16, "/GAMES");
        check("SDOPS-05", "a FAT16 partitioned card lists, and probe says not writable with the first-FAT32-partition reason",
              t16 == FS_FAT16 && p == St::Ok && !ci.writable && ci.read_only_reason.find("first FAT32 partition") != std::string::npos &&
                  l.st == St::Ok && names(l) == "WOTEF.NEX",
              info(l) + " " + ci.read_only_reason);
    }
    // SDOPS-06
    {
        ops::CardInfo ci;
        std::string err;
        const St p = ops::probe(SFD, ci, err);
        const Ls l = ls(SFD, "/");
        check("SDOPS-06", "a partition-less FAT32 card lists, and is not writable",
              tsfd == FS_FAT32 && p == St::Ok && !ci.writable && !ci.read_only_reason.empty() && l.st == St::Ok && names(l) == "FILE.BIN",
              info(l));
    }
    // SDOPS-07
    {
        sd_write_journal::forget_all_for_test();
        ops::CardInfo a, b;
        std::string err;
        const St pa = ops::probe(G, a, err);
        Img c(G);
        const std::string jf = tmp_name("w") + ".sdw";
        bind_journal(c.p, jf);
        const St pb0 = ops::probe(c.p, b, err);
        const bool before = b.through_writes;
        sdcard::add_to_image(c.p, (host / "three.bin").string(), "/THREE.BIN", false, err);
        const St pb = ops::probe(c.p, b, err);
        sd_write_journal::forget_all_for_test();
        check("SDOPS-07", "the first FAT32-LBA partition is writable; through_writes is true once a bound writes file exists, false before",
              pa == St::Ok && a.writable && a.read_only_reason.empty() && !a.through_writes && pb0 == St::Ok && !before &&
                  pb == St::Ok && b.writable && b.through_writes,
              stn(pa) + stn(pb0) + stn(pb));
    }
    // SDOPS-08
    {
        const std::string p = tmp_name("under") + ".img";
        make_under_clustered(p);
        ops::CardInfo ci;
        std::string e1, e2;
        const St s1 = ops::probe(p, ci, e1);
        std::vector<ops::CardEntry> v;
        const St s2 = ops::list_dir(p, "/", v, e2);
        check("SDOPS-08", "an under-clustered FAT32 image: ImageUnusable with the re-cluster hint, for probe and list",
              s1 == St::ImageUnusable && s2 == St::ImageUnusable && e1.find("fix-sdcard-image") != std::string::npos,
              e1 + " | " + e2);
    }
    // SDOPS-09
    {
        bool ok = true;
        std::string detail;
        for (uint32_t sector : {0u, kLba}) {
            sd_write_journal::forget_all_for_test();
            Img c(G);
            const std::string jf = tmp_name("w") + ".sdw";
            bind_journal(c.p, jf);
            uint8_t sec[512];
            std::memset(sec, 0xA5, sizeof sec);
            auto j = sd_write_journal::for_image(c.p);
            const bool committed = j && j->commit(sector, sec);
            ops::CardInfo ci;
            std::string err;
            const std::string before = sha_file(c.p);
            std::vector<ops::CardEntry> v;
            bool ex = false, dr = false;
            const St sp = ops::probe(c.p, ci, err);
            const St sl = ops::list_dir(c.p, "/", v, err);
            const St ss = ops::stat(c.p, "/FILE.BIN", ex, dr, err);
            const St sr = ops::rename(c.p, "/FILE.BIN", "X.BIN", err);
            const St sm = ops::make_dir(c.p, "/", "NEW", err);
            const St sx = ops::remove(c.p, "/FILE.BIN", err);
            ok = ok && committed && sp == St::ImageUnusable && sl == St::ImageUnusable && ss == St::ImageUnusable &&
                 sr == St::ImageUnusable && sm == St::ImageUnusable && sx == St::ImageUnusable && sha_file(c.p) == before;
            detail += " sector" + std::to_string(sector) + ":" + stn(sp) + stn(sl) + stn(ss) + stn(sr) + stn(sm) + stn(sx);
        }
        sd_write_journal::forget_all_for_test();
        check("SDOPS-09", "a writes file holding the partition table or the boot sector: ImageUnusable for probe, list, stat and every write",
              ok, detail);
    }
    // SDOPS-10
    {
        bool ex = false, dr = false;
        std::string err;
        auto q = [&](const std::string& p, bool& e, bool& d) { return ops::stat(G, p, e, d, err); };
        bool e1, d1, e2, d2, e3, d3, e4, d4, e5, d5;
        const St s1 = q("/GAMES", e1, d1), s2 = q("/games/wotef.nex", e2, d2), s3 = q("/NOPE", e3, d3),
                 s4 = q("/GAMES/WOTEF.NEX/x", e4, d4), s5 = q("/", e5, d5);
        const St s6 = ops::stat(G, "/a/../b", ex, dr, err);
        check("SDOPS-10", "stat: a directory, a file in any case, the root; a missing path (or one through a file) is exists=false and Ok; '..' is refused",
              s1 == St::Ok && e1 && d1 && s2 == St::Ok && e2 && !d2 && s3 == St::Ok && !e3 && !d3 && s4 == St::Ok && !e4 &&
                  s5 == St::Ok && e5 && d5 && s6 == St::DestInvalid,
              stn(s1) + stn(s2) + stn(s3) + stn(s4) + stn(s5) + stn(s6));
    }
    // SDOPS-11
    {
        Img c(G);
        std::string err;
        const St a = ops::rename(c.p, "/FILE.BIN", "RENAMED.BIN", err);
        const St b = ops::rename(c.p, "/long file name.TXT", "Another long name.txt", err);
        const Ls l = ls(c.p, "/");
        check("SDOPS-11", "rename a file (8.3 and long names): the new name lists, the old does not, the content is byte-identical",
              a == St::Ok && b == St::Ok && find(l, "RENAMED.BIN") && !find(l, "FILE.BIN") && find(l, "Another long name.txt") &&
                  !find(l, "Long File Name.txt") && extracted(c.p, "/RENAMED.BIN") == Tree{{"RENAMED.BIN", kFileBin}} &&
                  extracted(c.p, "/Another long name.txt") == Tree{{"Another long name.txt", kLong}},
              info(l) + " " + err);
    }
    // SDOPS-12
    {
        Img c(G);
        std::string err;
        const std::string before = sha_file(c.p);
        const St a = ops::rename(c.p, "/FILE.BIN", "h.bin", err);
        const St b = ops::rename(c.p, "/FILE.BIN", "GAMES", err);
        check("SDOPS-12", "rename onto an existing sibling (a file, a directory, any case): DestExists, the image byte-identical",
              a == St::DestExists && b == St::DestExists && sha_file(c.p) == before, stn(a) + stn(b));
    }
    // SDOPS-13
    {
        Img c(G);
        std::string err;
        const St a = ops::rename(c.p, "/FILE.BIN", "file.bin", err);
        const St b = ops::rename(c.p, "/Long File Name.txt", "LONG FILE NAME.TXT", err);
        const Ls l = ls(c.p, "/");
        check("SDOPS-13", "a case-only rename changes the stored spelling of the same entry, content intact",
              a == St::Ok && b == St::Ok && find(l, "file.bin") && !find(l, "FILE.BIN") && find(l, "LONG FILE NAME.TXT") &&
                  names(l).size() == general_names.size() && extracted(c.p, "/file.bin") == Tree{{"file.bin", kFileBin}},
              info(l) + " " + err);
    }
    // SDOPS-14
    {
        Img c(G);
        std::string err;
        const std::string before = sha_file(c.p);
        const std::vector<std::string> bad = {"a/b", "x.", "x ", "*", "..", ".", "", std::string(256, 'a'), "caf\xE9", "a\\b", "a:b", "tab\t", "q?", "a|b"};
        bool ok = true;
        std::string detail;
        for (const std::string& n : bad) {
            const St s = ops::rename(c.p, "/FILE.BIN", n, err);
            if (s != St::DestInvalid) { ok = false; detail += " [" + n + "]=" + stn(s); }
        }
        const bool same = sha_file(c.p) == before;
        const St good = ops::rename(c.p, "/H.BIN", std::string(255, 'a'), err);
        check("SDOPS-14", "invalid new names (separator, trailing dot or space, wildcard, dot names, empty, 256 characters, non-ASCII, control, reserved characters): DestInvalid, image byte-identical; 255 characters is fine",
              ok && same && good == St::Ok, detail + " same=" + (same ? "1" : "0") + " good=" + stn(good));
    }
    // SDOPS-15
    {
        Img c(G);
        std::string err;
        const St a = ops::rename(c.p, "/MYDIR", "NEWDIR", err);
        const Tree want = {{"NEWDIR/", {}}, {"NEWDIR/A.BIN", kA}, {"NEWDIR/B.BIN", kB}, {"NEWDIR/SUB/", {}},
                           {"NEWDIR/SUB/C.BIN", kC}, {"NEWDIR/EMPTYD/", {}}};
        check("SDOPS-15", "rename a directory: its subtree comes along intact and the old name is gone",
              a == St::Ok && extracted(c.p, "/NEWDIR") == want && !exists(c.p, "/MYDIR") && exists(c.p, "/newdir/sub/c.bin"), err);
    }
    // SDOPS-16
    {
        Img c(G);
        std::string err;
        const uint32_t f0 = free_clusters(c.p);
        sdcard::add_to_image(c.p, (host / "three.bin").string(), "/THREE.BIN", false, err);
        const uint32_t f1 = free_clusters(c.p);
        ops::RemoveSummary sum;
        const St a = ops::remove(c.p, "/three.bin", err, &sum);
        const uint32_t f2 = free_clusters(c.p);
        check("SDOPS-16", "remove a file: gone, its three clusters free again (free count back to the value before it was added), counted in the summary",
              a == St::Ok && !exists(c.p, "/THREE.BIN") && f1 + 3 == f0 && f2 == f0 && sum.files == 1 && sum.dirs == 0 &&
                  names_of(c.p, "/") == general_names,
              err + " f0=" + std::to_string(f0) + " f1=" + std::to_string(f1) + " f2=" + std::to_string(f2));
    }
    // SDOPS-17
    {
        Img c(G);
        std::string err;
        const uint32_t f0 = free_clusters(c.p);
        const St add = sdcard::add_to_image(c.p, (host / "T").string(), "/T", false, err);
        const uint32_t f1 = free_clusters(c.p);
        ops::RemoveSummary sum;
        const St a = ops::remove(c.p, "/T", err, &sum);
        const uint32_t f2 = free_clusters(c.p);
        const Ls l = ls(c.p, "/");
        check("SDOPS-17", "remove a 3-level tree: everything gone, free count back to the pre-copy value, the volume re-mounts and lists cleanly, summary counts 3 files and 3 directories",
              add == St::Ok && a == St::Ok && f1 < f0 && f2 == f0 && l.st == St::Ok && names(l) == general_names && sum.files == 3 && sum.dirs == 3,
              err + " f0=" + std::to_string(f0) + " f1=" + std::to_string(f1) + " f2=" + std::to_string(f2) + " " + std::to_string(sum.files) + "/" + std::to_string(sum.dirs));
    }
    // SDOPS-18
    {
        Img c(G);
        std::string e1, e2;
        const std::string before = sha_file(c.p);
        const St a = ops::remove(c.p, "/RO", e1);
        const St b = ops::remove(c.p, "/R.BIN", e2);
        check("SDOPS-18", "remove refuses a tree holding a read-only file, and a read-only file itself, naming it; the image byte-identical",
              a == St::DestInvalid && b == St::DestInvalid && e1.find("A_RO.BIN") != std::string::npos &&
                  e1.find("read-only") != std::string::npos && e2.find("R.BIN") != std::string::npos &&
                  e2.find("read-only") != std::string::npos && sha_file(c.p) == before,
              e1 + " | " + e2);
    }
    // SDOPS-19
    {
        Img c(G);
        std::string err;
        const std::string before = sha_file(c.p);
        const St a = ops::remove(c.p, "/", err), b = ops::rename(c.p, "/", "X", err), d = ops::remove(c.p, "", err),
                 e = ops::rename(c.p, "", "X", err);
        check("SDOPS-19", "remove and rename of the root, as \"/\" or \"\": DestInvalid, untouched",
              a == St::DestInvalid && b == St::DestInvalid && d == St::DestInvalid && e == St::DestInvalid && sha_file(c.p) == before,
              stn(a) + stn(b) + stn(d) + stn(e));
    }
    // SDOPS-20
    {
        sd_write_journal::forget_all_for_test();
        Img c(G);
        const std::string jf = tmp_name("w") + ".sdw";
        bind_journal(c.p, jf);
        std::string err;
        const St a = ops::rename(c.p, "/FILE.BIN", "J.BIN", err);
        const St b = ops::remove(c.p, "/H.BIN", err);
        const St d = ops::remove(c.p, "/MYDIR", err);
        const bool same = sha_file(c.p) == sha_g;
        std::error_code ec;
        const bool jexists = fs::exists(jf, ec);
        const Ls l = ls(c.p, "/");
        const Tree t = extracted(c.p, "/J.BIN");
        sd_write_journal::forget_all_for_test();
        check("SDOPS-20", "rename and remove in journal mode: the image is byte-identical, the writes file exists, the changes are visible through it",
              a == St::Ok && b == St::Ok && d == St::Ok && same && jexists && names(l) == "EMPTYD/,GAMES/,RO/,J.BIN,Long File Name.txt,R.BIN,S.BIN" &&
                  t == Tree{{"J.BIN", kFileBin}},
              info(l) + " " + err);
    }
    // SDOPS-21
    {
        const Ls l = ls(O, "/ODD");
        const ops::CardEntry* n = find(l, "NAME");
        const ops::CardEntry* dn = find(l, "NAME.");
        const bool listed = l.st == St::Ok && l.e.size() == 2 && n && dn && n->open_name == "NAME" && dn->open_name == "NAME~1" &&
                            n->size == kD1.size() && dn->size == kD2.size();
        std::string err;
        Img c1(O);
        const St r = ops::rename(c1.p, "/ODD/NAME~1", "REN.BIN", err);
        const Ls l1 = ls(c1.p, "/ODD");
        const bool renamed = r == St::Ok && names(l1) == "NAME,REN.BIN" && extracted(c1.p, "/ODD/NAME") == Tree{{"NAME", kD1}} &&
                             extracted(c1.p, "/ODD/REN.BIN") == Tree{{"REN.BIN", kD2}};
        Img c2(O);
        const St x = ops::remove(c2.p, "/ODD/NAME~1", err);
        const Ls l2 = ls(c2.p, "/ODD");
        const bool removed = x == St::Ok && names(l2) == "NAME" && extracted(c2.p, "/ODD/NAME") == Tree{{"NAME", kD1}};
        check("SDOPS-21", "an entry whose long name ends in a dot, beside a sibling NAME: it lists with its 8.3 alias as open_name; rename and remove by open_name act on THAT entry and leave NAME alone",
              listed && renamed && removed, info(l) + " | " + info(l1) + " | " + info(l2));
    }
    // SDOPS-22
    {
        ops::CardEntry e;
        e.name = std::string("a\x1b") + "%" + "\xE9";
        const std::string want_show = "a%1B%25%E9";
        const Ls l = ls(O, "/ESC");
        const Tree t = extracted(O, "/ESC/EVIL~1");
        const bool one = l.st == St::Ok && l.e.size() == 1;
        check("SDOPS-22", "display_name escapes control bytes, high bytes and '%'; host_name_for equals the name extract_from_image creates for the same entry",
              ops::display_name(e) == want_show && one && ops::display_name(l.e[0]) == "%1B[2Jevil" && l.e[0].open_name == "EVIL~1" &&
                  t.size() == 1 && t.count(ops::host_name_for(l.e[0])) == 1 && ops::host_name_for(l.e[0]) == "%1B[2Jevil",
              ops::display_name(e) + " | " + (t.empty() ? std::string("-") : t.begin()->first));
    }
    // SDOPS-24: the engine writes the image while an SdCardDevice has it open read-write (the manager's
    // situation). On Windows this is the share-mode question: the CRT opens with deny-none (_SH_DENYNO).
    {
        Img c(G);
        // The root directory's sector, from the boot sector's own fields (FAT spec, fatgen103 section 3.1).
        Bytes bpb(512);
        {
            std::ifstream f(c.p, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(kLba) * 512);
            f.read(reinterpret_cast<char*>(bpb.data()), 512);
        }
        auto u16 = [&](size_t o) { return static_cast<uint32_t>(bpb[o] | (bpb[o + 1] << 8)); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        const uint32_t root_sector = kLba + u16(14) + bpb[16] * u32(36) + (u32(44) - 2) * bpb[13];
        auto host_sector = [&](uint32_t sec) {
            Bytes b(512);
            std::ifstream f(c.p, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(sec) * 512);
            f.read(reinterpret_cast<char*>(b.data()), 512);
            return b;
        };
        SdCardDevice sd;
        const bool mounted = sd.mount(c.p);
        auto cmd = [&](uint8_t n, uint32_t arg) {
            sd.receive(0x40 | n);
            sd.receive(static_cast<uint8_t>(arg >> 24)); sd.receive(static_cast<uint8_t>(arg >> 16));
            sd.receive(static_cast<uint8_t>(arg >> 8)); sd.receive(static_cast<uint8_t>(arg));
            sd.receive(0x95);
            for (int i = 0; i < 16; ++i) { const uint8_t b = sd.send(); if (b != 0xFF) return b; }
            return uint8_t{0xFF};
        };
        cmd(0, 0); cmd(8, 0x1AA); cmd(55, 0); cmd(41, 0x40000000); cmd(58, 0);
        auto read_sector = [&](uint32_t sec) {
            Bytes b(512, 0);
            const uint8_t r1 = cmd(17, sec);
            bool tok = false;
            for (int i = 0; i < 16 && !tok; ++i) tok = sd.send() == 0xFE;
            if (r1 == 0 && tok) for (auto& x : b) x = sd.send();
            sd.send(); sd.send();
            sd.deselect();
            return b;
        };
        const Bytes dev0 = read_sector(root_sector);         // fills the device's stream buffer
        std::string err;
        const St a = ops::rename(c.p, "/FILE.BIN", "RENAMED.BIN", err);
        const Bytes dev1 = read_sector(root_sector);
        const Bytes img1 = host_sector(root_sector);
        auto has = [](const Bytes& b, const char* s) { return std::search(b.begin(), b.end(), s, s + std::strlen(s)) != b.end(); };
        const bool renamed_seen = a == St::Ok && dev1 == img1 && dev1 != dev0 && has(dev1, "RENAMED");
        const St r = ops::remove(c.p, "/RENAMED.BIN", err);
        const Bytes dev2 = read_sector(root_sector);
        const Bytes img2 = host_sector(root_sector);
        const bool removed_seen = r == St::Ok && dev2 == img2 && dev2 != dev1 && !has(dev2, "RENAMED");
        sd.unmount();
        check("SDOPS-24", "a rename and a remove succeed while an SdCardDevice has the image mounted read-write, and the device then reads the new directory sector (CMD17)",
              mounted && renamed_seen && removed_seen, err + " a=" + stn(a) + " r=" + stn(r));
    }
    // SDOPS-25
    {
        std::string err;
        const std::string before = sha_file(F16);
        const St a = ops::rename(F16, "/GAMES", "X", err), b = ops::remove(F16, "/GAMES", err), d = ops::make_dir(F16, "/", "NEW", err);
        check("SDOPS-25", "any write on a card that is not writable (FAT16): DestInvalid with the reason, nothing written",
              a == St::DestInvalid && b == St::DestInvalid && d == St::DestInvalid && err.find("first FAT32 partition") != std::string::npos &&
                  sha_file(F16) == before,
              stn(a) + stn(b) + stn(d) + " " + err);
    }
    // SDOPS-26
    {
        Img c(G);
        std::string err;
        const St a = ops::make_dir(c.p, "/", "NEWDIR", err);
        const St b = ops::make_dir(c.p, "/games/sub", "Brand New Folder", err);
        bool d1 = false, d2 = false;
        const bool e1 = exists(c.p, "/NEWDIR", &d1), e2 = exists(c.p, "/GAMES/SUB/Brand New Folder", &d2);
        const Ls n = ls(c.p, "/GAMES/SUB/Brand New Folder");
        const Ls sub = ls(c.p, "/GAMES/SUB");
        const Ls root = ls(c.p, "/");
        check("SDOPS-26", "make_dir creates a directory at the root and in a nested one (any case in the path): it lists as an empty directory, nothing else changes",
              a == St::Ok && b == St::Ok && e1 && d1 && e2 && d2 && n.st == St::Ok && n.e.empty() && names(sub) == "Brand New Folder/,DEEP/" &&
                  names(root) == "EMPTYD/,GAMES/,MYDIR/,NEWDIR/,RO/,FILE.BIN,H.BIN,Long File Name.txt,R.BIN,S.BIN" &&
                  extracted(c.p, "/GAMES/WOTEF.NEX") == Tree{{"WOTEF.NEX", kWotef}},
              info(root) + " " + err);
    }
    // SDOPS-27
    {
        Img c(G), o(O);
        std::string err;
        const std::string before = sha_file(c.p), before_o = sha_file(o.p);
        const St a = ops::make_dir(c.p, "/", "FILE.BIN", err), b = ops::make_dir(c.p, "/", "file.bin", err),
                 d = ops::make_dir(c.p, "/", "GAMES", err), e = ops::make_dir(c.p, "/", "games", err),
                 f = ops::make_dir(c.p, "/", "long file name.TXT", err), g = ops::make_dir(o.p, "/ODD", "NAME", err);
        check("SDOPS-27", "make_dir onto an existing name (a file or a directory, any case, a sibling of a hostile entry): DestExists, nothing is overwritten, the images byte-identical",
              a == St::DestExists && b == St::DestExists && d == St::DestExists && e == St::DestExists && f == St::DestExists &&
                  g == St::DestExists && sha_file(c.p) == before && sha_file(o.p) == before_o,
              stn(a) + stn(b) + stn(d) + stn(e) + stn(f) + stn(g));
    }
    // SDOPS-28
    {
        Img c(G);
        std::string err;
        const std::string before = sha_file(c.p);
        const std::vector<std::string> bad = {"a/b", "x.", "x ", "*", "..", ".", "", std::string(256, 'a'), "caf\xE9", "a\\b", "a:b", "tab\t", "q?", "a|b", "NAME."};
        bool ok = true;
        std::string detail;
        for (const std::string& n : bad) {
            const St s = ops::make_dir(c.p, "/", n, err);
            if (s != St::DestInvalid) { ok = false; detail += " [" + n + "]=" + stn(s); }
        }
        const St missing = ops::make_dir(c.p, "/NOPE", "X", err);
        const St in_file = ops::make_dir(c.p, "/FILE.BIN", "X", err);
        const St dotdot = ops::make_dir(c.p, "/..", "X", err);
        check("SDOPS-28", "make_dir refuses invalid and hostile names like rename does, a parent that is missing (SourceUnreadable), a file or '..' (DestInvalid); the image byte-identical",
              ok && missing == St::SourceUnreadable && in_file == St::DestInvalid && dotdot == St::DestInvalid && sha_file(c.p) == before,
              detail + " " + stn(missing) + stn(in_file) + stn(dotdot));
    }
    // SDOPS-29
    {
        std::string err;
        const std::string before = sha_file(SFD);
        const St a = ops::make_dir(SFD, "/", "NEW", err), b = ops::rename(SFD, "/FILE.BIN", "X.BIN", err), d = ops::remove(SFD, "/FILE.BIN", err);
        check("SDOPS-29", "make_dir, rename and remove on a partition-less FAT32 card: DestInvalid, nothing written",
              a == St::DestInvalid && b == St::DestInvalid && d == St::DestInvalid && sha_file(SFD) == before, stn(a) + stn(b) + stn(d));
    }
    // SDOPS-30
    {
        sd_write_journal::forget_all_for_test();
        Img c(G);
        const std::string jf = tmp_name("w") + ".sdw";
        bind_journal(c.p, jf);
        std::string err;
        const St a = ops::make_dir(c.p, "/GAMES", "JNEW", err);
        const St again = ops::make_dir(c.p, "/GAMES", "jnew", err);
        std::error_code ec;
        bool dr = false;
        const bool e = exists(c.p, "/GAMES/JNEW", &dr);
        const bool same = sha_file(c.p) == sha_g, jexists = fs::exists(jf, ec);
        sd_write_journal::forget_all_for_test();
        check("SDOPS-30", "make_dir in journal mode: the image is byte-identical, the writes file exists, the directory is visible through it, and a repeat is DestExists",
              a == St::Ok && again == St::DestExists && e && dr && same && jexists, stn(a) + stn(again) + " " + err);
    }

    // ---- round 2: guards a hostile or damaged card exercises, and the FAT-level oracle of the flush ----

    // A card whose /P holds one full 512-byte cluster (".", "..", 14 empty files), with `free_left` free clusters.
    auto make_full = [&](const std::string& path, uint32_t free_left) {
        make_card(path, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
            if (f_mkdir("0:/P") != FR_OK) return false;
            for (int i = 0; i < 14; ++i) {
                char d[32];
                std::snprintf(d, sizeof d, "0:/P/F%02d.BIN", i);
                if (!fat_write(d, {})) return false;
            }
            DWORD n = 0;
            FATFS* fp = nullptr;
            if (f_getfree("0:", &n, &fp) != FR_OK || n < free_left) return false;
            return n == free_left || fat_write("0:/FILLER.BIN", Bytes(static_cast<size_t>(n - free_left) * 512, 0x33));
        });
    };
    // The FAT copies of a card, read raw from the boot sector's own fields (fatgen103): FSInfo is not consulted,
    // because f_getfree() trusts it.
    auto fat_region = [&](const std::string& p) {
        Bytes bpb(512);
        {
            std::ifstream f(p, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(kLba) * 512);
            f.read(reinterpret_cast<char*>(bpb.data()), 512);
        }
        auto u16 = [&](size_t o) { return static_cast<uint32_t>(bpb[o] | (bpb[o + 1] << 8)); };
        auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
        const uint64_t off = (static_cast<uint64_t>(kLba) + u16(14)) * 512, len = static_cast<uint64_t>(bpb[16]) * u32(36) * 512;
        Bytes b(len);
        std::ifstream f(p, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(off));
        f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(len));
        return b;
    };

    // SDOPS-31
    {
        const std::string FP1 = tmp_name("full1") + ".img";
        make_full(FP1, 1);
        const Bytes fat0 = fat_region(FP1);
        std::string err;
        const St st = ops::make_dir(FP1, "/P", "D1", err);
        const Bytes fat1 = fat_region(FP1);
        check("SDOPS-31", "make_dir in a full directory with one free cluster (f_mkdir takes it, then dir_register fails): ImageFull, the FAT copies byte-identical (the flush before the unmount frees the cluster on disk), no directory made",
              st == St::ImageFull && fat0 == fat1 && !exists(FP1, "/P/D1") && !fat0.empty(),
              stn(st) + " " + err + " fat_same=" + (fat0 == fat1 ? "1" : "0"));
        std::error_code ec;
        fs::remove(FP1, ec);
    }
    // SDOPS-32
    {
        const std::string FP0 = tmp_name("full0") + ".img";
        make_full(FP0, 0);
        const std::string before = sha_file(FP0);
        std::string err;
        const St st = ops::rename(FP0, "/P/F00.BIN", "RENAMED.BIN", err);
        check("SDOPS-32", "rename in a full directory with no free cluster: ImageFull, the image byte-identical",
              st == St::ImageFull && sha_file(FP0) == before, stn(st) + " " + err);
        std::error_code ec;
        fs::remove(FP0, ec);
    }
    // SDOPS-33 / SDOPS-34: names another entry matches first (dir_find takes the first match in directory order)
    const std::string CL = tmp_name("coll") + ".img";
    make_card(CL, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
        if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/COL") != FR_OK || f_mkdir("0:/COL2") != FR_OK ||
            f_mkdir("0:/COL3") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
            return false;
        const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
        const uint32_t n1 = static_cast<uint32_t>(kD1.size()), n2 = static_cast<uint32_t>(kD2.size());
        // zzz owns the short name BAR, so the long name "bar" of the entry after it would open zzz
        hand_dir(fsv, dir_cluster("0:/COL"), 2, {{u"zzz", "BAR        ", 0x20, c1, n1}, {u"bar", "BAR~1      ", 0x20, c2, n2}}, pokes);
        // two long names equal under case folding
        hand_dir(fsv, dir_cluster("0:/COL2"), 2, {{u"Foo", "FOO~1      ", 0x20, c1, n1}, {u"FOO", "FOO~2      ", 0x20, c2, n2}}, pokes);
        // equal under the code page's case folding (e-acute / E-acute)
        hand_dir(fsv, dir_cluster("0:/COL3"), 2, {{u"café", "CAF~1      ", 0x20, c1, n1}, {u"CAFÉ", "CAF~2      ", 0x20, c2, n2}}, pokes);
        return c1 != 0 && c2 != 0;
    });
    {
        const Ls l = ls(CL, "/COL");
        const ops::CardEntry* bar = find(l, "bar");
        const ops::CardEntry* zzz = find(l, "zzz");
        std::string err;
        Img c1(CL), c2(CL);
        const St r = bar ? ops::rename(c1.p, "/COL/" + bar->open_name, "NEW.BIN", err) : St::ImageUnusable;
        const bool renamed = r == St::Ok && names_of(c1.p, "/COL") == "NEW.BIN,zzz" &&
                             extracted(c1.p, "/COL/NEW.BIN") == Tree{{"NEW.BIN", kD2}} && extracted(c1.p, "/COL/zzz") == Tree{{"zzz", kD1}};
        const St x = bar ? ops::remove(c2.p, "/COL/" + bar->open_name, err) : St::ImageUnusable;
        const bool removed = x == St::Ok && names_of(c2.p, "/COL") == "zzz" && extracted(c2.p, "/COL/zzz") == Tree{{"zzz", kD1}};
        check("SDOPS-33", "a long name equal to ANOTHER entry's earlier 8.3 name: it lists with its own alias as open_name, and rename and remove by open_name act on that entry, not on the sibling (A19)",
              l.st == St::Ok && bar && zzz && bar->open_name == "BAR~1" && zzz->open_name == "zzz" && renamed && removed, info(l));
    }
    {
        const Ls l = ls(CL, "/COL2"), l3 = ls(CL, "/COL3");
        const ops::CardEntry* foo = find(l, "Foo");
        const ops::CardEntry* foo2 = find(l, "FOO");
        const ops::CardEntry* e1 = find(l3, "caf\x82");
        const ops::CardEntry* e2 = find(l3, "CAF\x90");
        std::string err;
        Img c(CL);
        const St x = foo2 ? ops::remove(c.p, "/COL2/" + foo2->open_name, err) : St::ImageUnusable;
        check("SDOPS-34", "two long names that differ only in case (ASCII, and code page 437 letters): the later one is opened by its alias, and removing it by open_name leaves the earlier one",
              l.st == St::Ok && foo && foo2 && foo->open_name == "Foo" && foo2->open_name == "FOO~2" && x == St::Ok &&
                  names_of(c.p, "/COL2") == "Foo" && extracted(c.p, "/COL2/Foo") == Tree{{"Foo", kD1}} && l3.st == St::Ok && e1 && e2 &&
                  e1->open_name == e1->name && e2->open_name == "CAF~2",
              info(l) + " | " + info(l3));
    }
    // SDOPS-35 / SDOPS-36: damaged directory structures
    const std::string ZP = tmp_name("zero") + ".img";
    make_card(ZP, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
        if (f_mkdir("0:/TOPD") != FR_OK || f_mkdir("0:/XL") != FR_OK || f_mkdir("0:/KEEP") != FR_OK ||
            !fat_write("0:/KEEP/K.BIN", kD1) || !fat_write("0:/TOPD/T.BIN", kD2))
            return false;
        hand_dir(fsv, dir_cluster("0:/TOPD"), 3, {{u"", "EVIL       ", 0x10, 0, 0}}, pokes);                 // start cluster 0 = the root
        hand_dir(fsv, dir_cluster("0:/XL"), 2, {{u"", "LINK       ", 0x10, dir_cluster("0:/KEEP"), 0}}, pokes);  // cross-link to /KEEP
        return true;
    });
    {
        Img c(ZP);
        std::string err;
        const std::string before = sha_file(c.p);
        const St st = ops::remove(c.p, "/TOPD", err);
        check("SDOPS-35", "remove of a tree holding a subdirectory whose start cluster is 0 (the FAT32 root, so the walk would cycle): ImageUnusable naming the directory reached twice, nothing removed, image byte-identical",
              st == St::ImageUnusable && err.find("twice") != std::string::npos && sha_file(c.p) == before, stn(st) + " " + err);
    }
    {
        Img c(ZP), g(G);
        std::string err, err2;
        const std::string before = sha_file(c.p);
        const St st = ops::remove(c.p, "/XL", err);
        const bool refused = st == St::ImageUnusable && err.find("not owned") != std::string::npos && sha_file(c.p) == before &&
                             names_of(c.p, "/KEEP") == "K.BIN";
        const St ok = ops::remove(g.p, "/GAMES", err2);     // control: a 3-level tree with honest ".." entries goes
        check("SDOPS-36", "remove refuses a subdirectory cross-linked to a directory OUTSIDE the tree (its \"..\" does not name its parent): ImageUnusable, image byte-identical, /KEEP keeps its file; a tree with honest \"..\" entries is removed",
              refused && ok == St::Ok && !exists(g.p, "/GAMES"), stn(st) + " " + err + " | " + stn(ok) + " " + err2);
    }
    // SDOPS-37: long names FatFs would open as another entry, or refuses
    {
        const std::string HN = tmp_name("host") + ".img";
        make_card(HN, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/HOST") != FR_OK || f_mkdir("0:/HOST/DATA") != FR_OK ||
                !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2) || !fat_write("0:/HOST/DATA/D1.BIN", kD1))
                return false;
            const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
            const uint32_t n1 = static_cast<uint32_t>(kD1.size()), n2 = static_cast<uint32_t>(kD2.size());
            hand_dir(fsv, dir_cluster("0:/HOST"), 3,
                     {{u"", "SP         ", 0x20, c1, n1},
                      {u"SP ", "SP~1       ", 0x20, c2, n2},                       // trailing space: FatFs strips it, "SP " would open SP
                      {u"DATA\\D1.BIN", "DATA_D~1BIN", 0x20, c1, n1},              // backslash: a path separator to FatFs
                      {u"a*b", "A_B~1      ", 0x20, c1, n1},
                      {u"q?z", "Q_Z~1      ", 0x20, c1, n1},
                      {u"a|b", "A_B~2      ", 0x20, c1, n1}},
                     pokes);
            return c1 != 0 && c2 != 0;
        });
        const Ls l = ls(HN, "/HOST");
        auto open_of = [&](const std::string& n) { const ops::CardEntry* e = find(l, n); return e ? e->open_name : std::string("<missing>"); };
        std::string err;
        Img c(HN);
        const St r = ops::rename(c.p, "/HOST/SP~1", "SPX", err);
        check("SDOPS-37", "a long name with a trailing space, a backslash or one of * ? | opens by its 8.3 alias, never by itself; renaming through that alias leaves the sibling SP alone",
              l.st == St::Ok && open_of("SP ") == "SP~1" && open_of("SP") == "SP" && open_of("DATA\\D1.BIN") == "DATA_D~1.BIN" &&
                  open_of("a*b") == "A_B~1" && open_of("q?z") == "Q_Z~1" && open_of("a|b") == "A_B~2" && r == St::Ok &&
                  names_of(c.p, "/HOST").find("SPX") != std::string::npos && extracted(c.p, "/HOST/SP") == Tree{{"SP", kD1}} &&
                  extracted(c.p, "/HOST/SPX") == Tree{{"SPX", kD2}},
              info(l));
        std::error_code ec;
        fs::remove(HN, ec);
    }
    // SDOPS-38: an entry no name opens
    {
        const std::string BN = tmp_name("badname") + ".img";
        make_card(BN, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/BADNAME") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
                !fat_write("0:/DATA/D2.BIN", kD2))
                return false;
            // the unopenable entry first, a good file after it: removal runs in reverse, so only the refusal keeps the card whole
            hand_dir(fsv, dir_cluster("0:/BADNAME"), 2,
                     {{u"x.", "A*B        ", 0x20, file_cluster("0:/DATA/D1.BIN"), static_cast<uint32_t>(kD1.size())},
                      {u"", "GOOD    BIN", 0x20, file_cluster("0:/DATA/D2.BIN"), static_cast<uint32_t>(kD2.size())}},
                     pokes);
            return true;
        });
        const Ls l = ls(BN, "/BADNAME");
        const ops::CardEntry* bad = find(l, "x.");
        std::string err;
        const std::string before = sha_file(BN);
        const St st = ops::remove(BN, "/BADNAME", err);
        check("SDOPS-38", "an entry addressable by neither its long nor its 8.3 name lists with an empty open_name, and removing the tree that holds it is refused up front: DestInvalid, the image byte-identical",
              l.st == St::Ok && bad && bad->open_name.empty() && st == St::DestInvalid && sha_file(BN) == before, info(l) + " " + stn(st) + " " + err);
        std::error_code ec;
        fs::remove(BN, ec);
    }
    // SDOPS-39: a read-only DIRECTORY inside the tree
    {
        const std::string RT = tmp_name("rodir") + ".img";
        make_card(RT, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
            return f_mkdir("0:/RT") == FR_OK && fat_write("0:/RT/A.BIN", kA) && f_mkdir("0:/RT/RD") == FR_OK &&
                   fat_write("0:/RT/RD/X.BIN", kX) && f_chmod("0:/RT/RD", AM_RDO, AM_RDO) == FR_OK;
        });
        std::string err;
        const std::string before = sha_file(RT);
        const St st = ops::remove(RT, "/RT", err);
        check("SDOPS-39", "remove refuses a tree holding a read-only directory (every read-only entry, not only files), naming it: DestInvalid, the image byte-identical",
              st == St::DestInvalid && err.find("RD") != std::string::npos && err.find("read-only") != std::string::npos && sha_file(RT) == before,
              stn(st) + " " + err);
        std::error_code ec;
        fs::remove(RT, ec);
    }
    // SDOPS-40: a path that is not on the card
    {
        Img c(G);
        std::string err;
        const std::string before = sha_file(c.p);
        const St a = ops::rename(c.p, "/NOPE", "X", err), b = ops::remove(c.p, "/NOPE", err), d = ops::remove(c.p, "/GAMES/NOPE", err),
                 e = ops::rename(c.p, "/NOPE/Y", "Z", err), f = ops::remove(c.p, "/FILE.BIN/X", err);
        check("SDOPS-40", "rename and remove of a path that is not on the card (missing, below a missing directory, through a file): SourceUnreadable, the image byte-identical",
              a == St::SourceUnreadable && b == St::SourceUnreadable && d == St::SourceUnreadable && e == St::SourceUnreadable &&
                  f == St::SourceUnreadable && sha_file(c.p) == before,
              stn(a) + stn(b) + stn(d) + stn(e) + stn(f));
    }
    // SDOPS-41: the order is case folded
    {
        const std::string SO = tmp_name("sort") + ".img";
        make_card(SO, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
            return fat_write("0:/Banana.txt", kA) && fat_write("0:/apple.txt", kB) && fat_write("0:/Cherry.txt", kC) &&
                   f_mkdir("0:/zdir") == FR_OK && f_mkdir("0:/Adir") == FR_OK;
        });
        const Ls l = ls(SO, "/");
        check("SDOPS-41", "directories first, then names ordered with ASCII case folded (apple before Banana before Cherry)",
              l.st == St::Ok && names(l) == "Adir/,zdir/,apple.txt,Banana.txt,Cherry.txt", info(l));
        std::error_code ec;
        fs::remove(SO, ec);
    }

    // ---- round 3: FatFs's own name resolution, and the ".." check on honest and damaged cards ----

    // SDOPS-42 / SDOPS-43: long names FatFs matches in Unicode. Y (size 200) comes first, X (size 210) after it.
    const std::string UC = tmp_name("unicode") + ".img";
    make_card(UC, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
        if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/GR") != FR_OK || f_mkdir("0:/UN") != FR_OK ||
            !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
            return false;
        const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
        const uint32_t n1 = static_cast<uint32_t>(kD1.size()), n2 = static_cast<uint32_t>(kD2.size());
        // Sigma / sigma: a pair in code page 437 that has no upper-case mapping in a hand-written table
        hand_dir(fsv, dir_cluster("0:/GR"), 2, {{u"ΣX", "Y~1        ", 0x20, c1, n1}, {u"σX", "X~1        ", 0x20, c2, n2}}, pokes);
        // A-circumflex is not in code page 437: that long name lists under its alias, FatFs still matches it
        hand_dir(fsv, dir_cluster("0:/UN"), 2, {{u"Â" u"bc", "Y~1        ", 0x20, c1, n1}, {u"â" u"bc", "X~1        ", 0x20, c2, n2}}, pokes);
        return true;
    });
    for (const char* d : {"/GR", "/UN"}) {
        const Ls l = ls(UC, d);
        const ops::CardEntry* x = nullptr;
        const ops::CardEntry* y = nullptr;
        for (const auto& e : l.e) { if (e.size == kD2.size()) x = &e; if (e.size == kD1.size()) y = &e; }
        std::string err, err2;
        Img c1(UC), c2(UC);
        const St rm = x && !x->open_name.empty() ? ops::remove(c1.p, std::string(d) + "/" + x->open_name, err) : St::ImageUnusable;
        const Ls a = ls(c1.p, d);
        const St rn = x && !x->open_name.empty() ? ops::rename(c2.p, std::string(d) + "/" + x->open_name, "NEW.BIN", err2) : St::ImageUnusable;
        const Ls b = ls(c2.p, d);
        const bool removed = rm == St::Ok && a.e.size() == 1 && a.e[0].size == kD1.size();     // X gone, Y (size 200) intact
        const bool renamed = rn == St::Ok && b.e.size() == 2 && find(b, "NEW.BIN") && find(b, "NEW.BIN")->size == kD2.size() &&
                             extracted(c2.p, std::string(d) + "/NEW.BIN") == Tree{{"NEW.BIN", kD2}};
        const bool pick = l.st == St::Ok && l.e.size() == 2 && x && y && !x->open_name.empty() && !y->open_name.empty();
        if (std::string(d) == "/GR")
            check("SDOPS-42", "two long names equal in Unicode but not in a hand-written table (sigma, Sigma): the later one is not opened by its long name, and remove and rename by open_name act on it, not on the earlier entry, whose bytes stay",
                  pick && removed && renamed, info(l) + " " + err + err2);
        else
            check("SDOPS-43", "a long name with a character outside code page 437 lists under its alias yet FatFs matches it as the long name: the entry after it is never opened by a name that resolves to the earlier one; remove and rename by open_name act on the right entry",
                  pick && removed && renamed, info(l) + " " + err + err2);
    }
    // SDOPS-44: a nested honest tree is removed (the everyday case: the manager deletes inside a directory)
    {
        Img c(G);
        std::string err, err2;
        const St a = ops::remove(c.p, "/GAMES/SUB", err);
        const bool first = a == St::Ok && !exists(c.p, "/GAMES/SUB") && exists(c.p, "/GAMES/WOTEF.NEX");
        const St b = ops::remove(c.p, "/MYDIR/SUB", err2);
        check("SDOPS-44", "remove of a directory below the root, at two depths (/GAMES/SUB with a subdirectory, /MYDIR/SUB): Ok, the siblings stay, so the parent of the top directory is the directory it was found in",
              first && b == St::Ok && !exists(c.p, "/MYDIR/SUB") && exists(c.p, "/MYDIR/A.BIN") && names_of(c.p, "/GAMES") == "WOTEF.NEX",
              err + " | " + err2);
    }
    // SDOPS-45: directories above cluster 65535 (the high word of a ".." entry)
    {
        const std::string BG = tmp_name("big") + ".img";
        uint32_t big_cluster = 0;
        make_card(BG, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
            if (!fat_write("0:/FILLER.BIN", Bytes(static_cast<size_t>(65600) * 512, 0x44))) return false;
            if (f_mkdir("0:/BIG") != FR_OK || f_mkdir("0:/BIG/SUB") != FR_OK || !fat_write("0:/BIG/SUB/F.BIN", kA)) return false;
            big_cluster = dir_cluster("0:/BIG");
            return big_cluster > 65535 && dir_cluster("0:/BIG/SUB") > 65535;
        });
        std::string err;
        const St a = ops::remove(BG, "/BIG", err);
        check("SDOPS-45", "remove of a tree whose directories sit above cluster 65535 (a \"..\" entry's high word is non-zero): Ok, gone",
              big_cluster > 65535 && a == St::Ok && !exists(BG, "/BIG") && exists(BG, "/FILLER.BIN"),
              "cluster=" + std::to_string(big_cluster) + " " + stn(a) + " " + err);
        std::error_code ec;
        fs::remove(BG, ec);
    }
    // SDOPS-46 .. SDOPS-48: ".." entries of directories directly under the root, and of a cluster that is no directory
    const std::string DD = tmp_name("dotdot") + ".img";
    make_card(DD, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
        if (f_mkdir("0:/RD") != FR_OK || !fat_write("0:/RD/R.BIN", kA) || f_mkdir("0:/A") != FR_OK || f_mkdir("0:/A/B") != FR_OK ||
            !fat_write("0:/A/B/B.BIN", kB) || f_mkdir("0:/X") != FR_OK)
            return false;
        // RD's ".." names the root by its cluster number instead of 0 (accepted: both name the root)
        const uint64_t rd_sector = static_cast<uint64_t>(fsv.database) + static_cast<uint64_t>(dir_cluster("0:/RD") - 2) * fsv.csize;
        const uint32_t root = static_cast<uint32_t>(fsv.dirbase);
        pokes.push_back({rd_sector * 512 + 32 + 20, {static_cast<uint8_t>(root >> 16), static_cast<uint8_t>(root >> 24)}});
        pokes.push_back({rd_sector * 512 + 32 + 26, {static_cast<uint8_t>(root), static_cast<uint8_t>(root >> 8)}});
        // /LINK, directly under the root, shares /A/B's cluster, whose ".." names A
        hand_dir(fsv, fsv.dirbase, 4, {{u"", "LINK       ", 0x10, dir_cluster("0:/A/B"), 0}}, pokes);
        // /X/FAKE points at a file's data cluster; the bytes there carry /X's cluster where a ".." entry's would be, but not the ".." name
        Bytes data(512, 'A');
        const uint32_t x = dir_cluster("0:/X");
        data[32 + 20] = static_cast<uint8_t>(x >> 16); data[32 + 21] = static_cast<uint8_t>(x >> 24);
        data[32 + 26] = static_cast<uint8_t>(x);       data[32 + 27] = static_cast<uint8_t>(x >> 8);
        if (!fat_write("0:/DATA.BIN", data)) return false;
        hand_dir(fsv, dir_cluster("0:/X"), 2, {{u"", "FAKE       ", 0x10, file_cluster("0:/DATA.BIN"), 0}}, pokes);
        return true;
    });
    {
        Img c(DD);
        std::string err;
        const St a = ops::remove(c.p, "/RD", err);
        check("SDOPS-46", "a root-level directory whose \"..\" names the root by its cluster number (not 0) is removed: both name the root",
              a == St::Ok && !exists(c.p, "/RD"), stn(a) + " " + err);
    }
    {
        Img c(DD);
        std::string err;
        const std::string before = sha_file(c.p);
        const St a = ops::remove(c.p, "/LINK", err);
        check("SDOPS-47", "a directory directly under the root that shares another directory's cluster (its \"..\" names that directory, not the root): ImageUnusable, nothing removed, the image byte-identical, /A/B keeps its file",
              a == St::ImageUnusable && err.find("not owned") != std::string::npos && sha_file(c.p) == before && names_of(c.p, "/A/B") == "B.BIN",
              stn(a) + " " + err);
    }
    {
        Img c(DD);
        std::string err;
        const std::string before = sha_file(c.p);
        const St a = ops::remove(c.p, "/X", err);
        check("SDOPS-48", "a directory entry pointing at a cluster that is no directory (no \"..\" entry in its first slots): ImageUnusable, nothing removed, the image byte-identical",
              a == St::ImageUnusable && err.find("not owned") != std::string::npos && sha_file(c.p) == before, stn(a) + " " + err);
    }

    // SDOPS-49: two entries indistinguishable by every field FatFs shows (same names, size, attributes, date, time)
    {
        const std::string DP = tmp_name("dup") + ".img";
        make_card(DP, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/DUP") != FR_OK || f_mkdir("0:/DUP2") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
                !fat_write("0:/DATA/D2.BIN", Bytes(kD1.size(), 0x66)))
                return false;
            const uint32_t n = static_cast<uint32_t>(kD1.size());
            hand_dir(fsv, dir_cluster("0:/DUP"), 2,
                     {{u"Dup", "DUP~1      ", 0x20, file_cluster("0:/DATA/D1.BIN"), n}, {u"Dup", "DUP~1      ", 0x20, file_cluster("0:/DATA/D2.BIN"), n}}, pokes);
            // the same two names, sizes that differ: the fields tell them apart, but FatFs still resolves both names to the first
            hand_dir(fsv, dir_cluster("0:/DUP2"), 2,
                     {{u"Dup", "DUP~1      ", 0x20, file_cluster("0:/DATA/D1.BIN"), n}, {u"Dup", "DUP~1      ", 0x20, file_cluster("0:/DATA/D2.BIN"), n + 1}}, pokes);
            return true;
        });
        const Ls l = ls(DP, "/DUP"), l2 = ls(DP, "/DUP2");
        std::string err;
        const std::string before = sha_file(DP);
        const St st = ops::remove(DP, "/DUP", err);
        check("SDOPS-49", "two entries that FatFs cannot tell apart (same long name, 8.3 name, size, attributes, date and time), or that share both names and differ only in size: the first is opened by its name, the second has no open_name, so removing the directory is refused with the image byte-identical",
              l.st == St::Ok && l.e.size() == 2 && l.e[0].open_name == "Dup" && l.e[1].open_name.empty() &&
                  st == St::DestInvalid && sha_file(DP) == before && l2.st == St::Ok && l2.e.size() == 2 && l2.e[0].open_name == "Dup" &&
                  l2.e[1].open_name.empty(),
              info(l) + " " + info(l2) + " " + stn(st) + " " + err);
        std::error_code ec;
        fs::remove(DP, ec);
    }

    // SDOPS-50: Sigma / sigma with EVERY field equal (size, attributes, date, time): only the stored names tell the entries apart
    {
        const std::string GE = tmp_name("greq") + ".img";
        make_card(GE, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/GR") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
                !fat_write("0:/DATA/D2.BIN", Bytes(kD1.size(), 0x66)))
                return false;
            const uint32_t n = static_cast<uint32_t>(kD1.size());
            hand_dir(fsv, dir_cluster("0:/GR"), 2,
                     {{u"ΣX", "Y~1        ", 0x20, file_cluster("0:/DATA/D1.BIN"), n}, {u"σX", "X~1        ", 0x20, file_cluster("0:/DATA/D2.BIN"), n}}, pokes);
            return true;
        });
        const Ls l = ls(GE, "/GR");
        std::string err;
        Img c(GE);
        // X is the entry after Y in directory order: its content is the 0x66 fill
        const ops::CardEntry* x = l.e.size() == 2 && l.e[0].name == "\xE4X" ? &l.e[1] : (l.e.size() == 2 ? &l.e[0] : nullptr);
        const St r = x && !x->open_name.empty() ? ops::remove(c.p, "/GR/" + x->open_name, err) : St::ImageUnusable;
        const Ls a = ls(c.p, "/GR");
        const Tree y = a.e.size() == 1 ? extracted(c.p, "/GR/" + a.e[0].altname) : Tree{};     // by alias: extract takes printable ASCII only
        check("SDOPS-50", "Sigma / sigma long names with the same size, attributes, date and time: the later entry is not opened by its long name (FatFs returns the earlier one), so remove by open_name takes the later one and the earlier one's bytes stay",
              l.st == St::Ok && x && !x->open_name.empty() && r == St::Ok && a.e.size() == 1 && a.e[0].name == "\xE4X" && y.size() == 1 &&
                  y.begin()->second == kD1,
              info(l) + " open=[" + (x ? x->open_name : "-") + "] " + stn(r) + " " + err + " after=" + info(a) + " ysz=" + std::to_string(y.size()));
        std::error_code ec;
        fs::remove(GE, ec);
    }

    // ---- round 4: the slot decides which entry a name resolves to ----

    // SDOPS-51 .. SDOPS-53: FatFs resolves X's name to the EARLIER entry Y (it matches by long name, or by a duplicated
    // 8.3 name), and hands back the typed spelling, so no name or field tells them apart. All fields equal: size, attributes, date, time.
    const std::string HP = tmp_name("hole") + ".img";
    make_card(HP, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
        if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/H7") != FR_OK || f_mkdir("0:/H8") != FR_OK || f_mkdir("0:/H9") != FR_OK ||
            !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
            return false;
        const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
        // Y {long "foo", 8.3 R~1} then X {8.3 only, FOO}: "FOO" opens Y by its long name
        hand_dir(fsv, dir_cluster("0:/H7"), 2, {{u"foo", "R~1        ", 0x20, c1, 200}, {u"", "FOO        ", 0x20, c2, 200}}, pokes);
        // Y {Foo, FOO~1} then X {FOO, FOO~1}: a duplicated 8.3 name
        hand_dir(fsv, dir_cluster("0:/H8"), 2, {{u"Foo", "FOO~1      ", 0x20, c1, 200}, {u"FOO", "FOO~1      ", 0x20, c2, 200}}, pokes);
        // Y {FOO, R~1} then X {8.3 FOO}: the same case
        hand_dir(fsv, dir_cluster("0:/H9"), 2, {{u"FOO", "R~1        ", 0x20, c1, 200}, {u"", "FOO        ", 0x20, c2, 200}}, pokes);
        return true;
    });
    const Bytes y_data(kD1.begin(), kD1.begin() + 200), x_data(kD2.begin(), kD2.begin() + 200);
    for (const char* d : {"/H7", "/H8", "/H9"}) {
        const Ls l = ls(HP, d);
        const ops::CardEntry* x = nullptr;
        const ops::CardEntry* y = nullptr;
        for (const auto& e : l.e) {
            const bool is_x = e.name == "FOO" && (std::string(d) == "/H8" ? true : e.altname.empty());
            if (std::string(d) == "/H8") { if (e.name == "FOO") x = &e; else y = &e; }
            else if (is_x) x = &e; else y = &e;
        }
        std::string err;
        Img c1(HP), c2(HP);
        const std::string before = sha_file(c2.p);
        const St whole = ops::remove(c2.p, d, err);           // X has no open_name, so the tree is refused
        // removing Y by ITS open_name leaves X, whose data is intact and which now resolves
        const St ry = y && !y->open_name.empty() ? ops::remove(c1.p, std::string(d) + "/" + y->open_name, err) : St::ImageUnusable;
        const Ls a = ls(c1.p, d);
        const Tree t = a.e.size() == 1 && !a.e[0].open_name.empty() ? extracted(c1.p, std::string(d) + "/" + a.e[0].open_name) : Tree{};
        const bool ok = l.st == St::Ok && l.e.size() == 2 && x && y && x->open_name.empty() && !y->open_name.empty() && whole == St::DestInvalid &&
                        sha_file(c2.p) == before && ry == St::Ok && a.e.size() == 1 && !a.e[0].open_name.empty() && t.size() == 1 &&
                        t.begin()->second == x_data;
        if (std::string(d) == "/H7")
            check("SDOPS-51", "an 8.3-only FOO after an earlier entry with the long name foo (all fields equal): FatFs opens FOO as the earlier entry, so FOO has no open_name; the tree holding it is refused with the image byte-identical, and removing the earlier entry by ITS open_name leaves FOO's data",
                  ok, info(l) + " " + stn(whole) + stn(ry));
        else if (std::string(d) == "/H8")
            check("SDOPS-52", "two entries with the long names Foo and FOO and the same 8.3 name, all fields equal: the later one has no open_name (both spellings resolve to the earlier), the tree is refused with the image byte-identical, and removing the earlier one by its open_name leaves the later one's data",
                  ok, info(l) + " " + stn(whole) + stn(ry));
        else
            check("SDOPS-53", "an 8.3-only FOO after an earlier entry with the long name FOO (all fields equal): FOO has no open_name, the tree is refused with the image byte-identical, and removing the earlier entry by its open_name leaves FOO's data",
                  ok, info(l) + " " + stn(whole) + stn(ry));
    }
    // SDOPS-54: the same slot OFFSET in two different sectors of one directory
    {
        const std::string TS = tmp_name("twosec") + ".img";
        make_card(TS, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/TS") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
                return false;
            for (int i = 0; i < 20; ++i) {                    // 2 + 20 entries: the directory takes a second 512-byte cluster
                char n[32];
                std::snprintf(n, sizeof n, "0:/TS/F%02d.BIN", i);
                if (!fat_write(n, {})) return false;
            }
            f_syncvol("0:");
            const uint32_t first = dir_cluster("0:/TS");
            BYTE sec[512];
            if (disk_read(0, sec, fsv.fatbase + (static_cast<LBA_t>(first) * 4) / 512, 1) != RES_OK) return false;
            const size_t o = (static_cast<size_t>(first) * 4) % 512;
            const uint32_t next = (sec[o] | (sec[o + 1] << 8) | (sec[o + 2] << 16) | (static_cast<uint32_t>(sec[o + 3]) << 24)) & 0x0FFFFFFF;
            if (next < 2 || next >= 0x0FFFFFF8) return false;
            const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
            // Y {long "foo", 8.3 R~1} puts its 8.3 slot at byte offset 96 of the first sector; X {8.3 FOO} at offset 96 of the second
            hand_dir(fsv, first, 2, {{u"foo", "R~1        ", 0x20, c1, 200}}, pokes);
            hand_dir(fsv, next, 3, {{u"", "FOO        ", 0x20, c2, 200}}, pokes);
            return true;
        });
        const Ls l = ls(TS, "/TS");
        const ops::CardEntry* x = nullptr;
        const ops::CardEntry* y = nullptr;
        for (const auto& e : l.e) { if (e.name == "FOO" && e.altname.empty()) x = &e; if (e.name == "foo") y = &e; }
        check("SDOPS-54", "the same slot offset in two sectors of one directory (an earlier foo, a later 8.3-only FOO): FOO has no open_name, foo has",
              l.st == St::Ok && x && y && x->open_name.empty() && y->open_name == "foo", info(l));
        std::error_code ec;
        fs::remove(TS, ec);
    }

    // SDOPS-55 / SDOPS-56: the slot's SECTOR half, on cards where sectors do not equal clusters. An earlier `foo` has its 8.3 slot at byte
    // offset 96 of sector 0, a later 8.3-only `FOO` at offset 96 of sector 1 of the SAME directory cluster (2-sector clusters) or of the
    // same fixed FAT16 root (no cluster at all): only the sector tells them apart.
    {
        const std::string AU = tmp_name("au1k") + ".img";
        const int t = make_card(AU, kLba, 140000, FM_FAT32, 1024, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/TS") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2))
                return false;
            for (int i = 0; i < 18; ++i) {                    // slots 2..19 of the directory's first cluster (32 slots, 2 sectors)
                char n[32];
                std::snprintf(n, sizeof n, "0:/TS/F%02d.BIN", i);
                if (!fat_write(n, {})) return false;
            }
            const uint32_t d = dir_cluster("0:/TS"), c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
            hand_dir(fsv, d, 2, {{u"foo", "R~1        ", 0x20, c1, 200}}, pokes);        // 8.3 slot 3: sector 0, offset 96
            hand_dir(fsv, d, 19, {{u"", "FOO        ", 0x20, c2, 200}}, pokes);          // slot 19: sector 1, offset 96
            return fsv.csize == 2;
        });
        const Ls l = ls(AU, "/TS");
        const ops::CardEntry* x = nullptr;
        const ops::CardEntry* y = nullptr;
        for (const auto& e : l.e) { if (e.name == "FOO" && e.altname.empty()) x = &e; if (e.name == "foo") y = &e; }
        std::string err;
        Img c(AU);
        const St ry = y && !y->open_name.empty() ? ops::remove(c.p, "/TS/" + y->open_name, err) : St::ImageUnusable;
        const Ls a = ls(c.p, "/TS");
        const ops::CardEntry* ax = nullptr;
        for (const auto& e : a.e) if (e.name == "FOO") ax = &e;
        const Tree tx = ax && !ax->open_name.empty() ? extracted(c.p, "/TS/" + ax->open_name) : Tree{};
        check("SDOPS-55", "on a card with 2 sectors per cluster, an earlier foo (sector 0) and a later 8.3-only FOO (sector 1, same offset) of ONE directory cluster: FOO has no open_name, foo has, and removing foo by its open_name leaves FOO's data",
              t == FS_FAT32 && l.st == St::Ok && x && y && x->open_name.empty() && y->open_name == "foo" && ry == St::Ok && ax && !ax->open_name.empty() &&
                  tx.size() == 1 && tx.begin()->second == x_data,
              "type=" + std::to_string(t) + " " + info(l) + " " + stn(ry) + " " + err);
        std::error_code ec;
        fs::remove(AU, ec);
    }
    {
        const std::string R16 = tmp_name("f16root") + ".img";
        const int t = make_card(R16, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) || !fat_write("0:/DATA/D2.BIN", kD2)) return false;
            for (int i = 0; i < 19; ++i) {                    // the fixed root: slots 1..19 (DATA is slot 0)
                char n[32];
                std::snprintf(n, sizeof n, "0:/F%02d.BIN", i);
                if (!fat_write(n, {})) return false;
            }
            const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
            // hand_dir addresses a directory by cluster; the fixed root is a run of sectors from dirbase, so present it as "cluster 2 of 1-sector clusters"
            FATFS root = fsv;
            root.database = root.dirbase;
            root.csize = 1;
            hand_dir(root, 2, 2, {{u"foo", "R~1        ", 0x20, c1, 200}}, pokes);       // 8.3 slot 3: root sector 0, offset 96
            hand_dir(root, 2, 19, {{u"", "FOO        ", 0x20, c2, 200}}, pokes);         // slot 19: root sector 1, offset 96
            return true;
        });
        const Ls l = ls(R16, "/");
        const ops::CardEntry* x = nullptr;
        const ops::CardEntry* y = nullptr;
        for (const auto& e : l.e) { if (e.name == "FOO" && e.altname.empty()) x = &e; if (e.name == "foo") y = &e; }
        check("SDOPS-56", "in a FAT16 fixed root (no cluster: the directory is a run of sectors), an earlier foo (sector 0) and a later 8.3-only FOO (sector 1, same offset): FOO has no open_name, foo has",
              t == FS_FAT16 && l.st == St::Ok && x && y && x->open_name.empty() && y->open_name == "foo", "type=" + std::to_string(t) + " " + info(l));
        std::error_code ec;
        fs::remove(R16, ec);
    }

    // SDOPS-57 (GH #558): add_to_image with the destination as CP437 BYTES (dest_is_bytes): into a folder named with a high byte,
    // and into a long-named folder whose 8.3 name DUPLICATES an earlier folder's (/DA and /DB): the file lands in the folder
    // NAMED, never in the other; the command line's behaviour is unchanged (a byte above 0x7E is refused).
    {
        const std::string P = tmp_name("addb") + ".img";
        make_card(P, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DUPD") != FR_OK || f_mkdir("0:/DA") != FR_OK || f_mkdir("0:/DB") != FR_OK || f_mkdir("0:/D\x82R") != FR_OK)
                return false;
            hand_dir(fsv, dir_cluster("0:/DUPD"), 2,
                     {{u"", "DER        ", 0x10, dir_cluster("0:/DA"), 0},
                      {u"D\u00E9 a long folder", "DER        ", 0x10, dir_cluster("0:/DB"), 0}},
                     pokes);
            return true;
        });
        const std::string src = (host / "three.bin").string();
        std::string e1, e2, e3;
        const St cli = sdcard::add_to_image(P, src, "/D\x82R/x.bin", false, e1);
        const St in_cp = sdcard::add_to_image(P, src, "/D\x82R/x.bin", false, e2, nullptr, true);
        const St dup = sdcard::add_to_image(P, src, "/DUPD/D\x82 a long folder/y.bin", false, e3, nullptr, true);
        check("SDOPS-57", "add_to_image with dest_is_bytes: into a CP437-named folder, and into the long-named folder whose 8.3 name duplicates /DA's (it lands in /DB, /DA stays empty); without the flag a byte above 0x7E is still refused",
              cli == St::DestInvalid && e1.find("ASCII") != std::string::npos && in_cp == St::Ok && names_of(P, "/D\x82R") == "x.bin" &&
                  dup == St::Ok && names_of(P, "/DB") == "y.bin" && names_of(P, "/DA").empty(),
              e1 + " | " + e2 + " | " + e3 + " | DB=" + names_of(P, "/DB") + " DA=" + names_of(P, "/DA"));
    }

    // SDOPS-58 (GH #558 K3): dest_is_bytes refuses a DEL (0x7F) component and a control byte, and creates nothing
    {
        const std::string P = tmp_name("addb7f") + ".img";
        make_card(P, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) { return true; });
        const std::string src = (host / "three.bin").string();
        std::string e1, e2, e3;
        const St del = sdcard::add_to_image(P, src, "/D\x7F/x.bin", false, e1, nullptr, true);
        const St ctl = sdcard::add_to_image(P, src, "/D\x01/x.bin", false, e2, nullptr, true);
        const St hi = sdcard::add_to_image(P, src, "/D\x82/x.bin", false, e3, nullptr, true);
        check("SDOPS-58", "add_to_image with dest_is_bytes: a destination component holding DEL (0x7F) or a control byte is refused (DestInvalid, no folder made); a byte above 0x7E is accepted",
              del == St::DestInvalid && e1.find("printable ASCII") != std::string::npos && ctl == St::DestInvalid && hi == St::Ok &&
                  names_of(P, "/").find("D\x82") != std::string::npos && names_of(P, "/").find('\x7F') == std::string::npos,
              e1 + " | " + e2 + " | " + e3 + " | root=" + names_of(P, "/"));
    }
    // SDOPS-59 (GH #558 N1): dest_is_bytes, a destination the listing cannot address (a long entry behind an earlier entry with the
    // SAME 8.3 name: a folder, and a file) is refused by the engine, and neither the earlier folder nor the earlier file is touched
    {
        const std::string P = tmp_name("addbun") + ".img";
        make_card(P, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DUPX") != FR_OK || f_mkdir("0:/DA") != FR_OK || f_mkdir("0:/DB") != FR_OK || !fat_write("0:/KEEP.BIN", kD1) ||
                !fat_write("0:/OTHER.BIN", kD2))
                return false;
            hand_dir(fsv, dir_cluster("0:/DUPX"), 2,
                     {{u"", "DER        ", 0x10, dir_cluster("0:/DA"), 0},
                      {u"D\u00E9R", "DER        ", 0x10, dir_cluster("0:/DB"), 0},
                      {u"", "FEL     BIN", 0x20, file_cluster("0:/KEEP.BIN"), static_cast<uint32_t>(kD1.size())},
                      {u"F\u00E9L.BIN", "FEL     BIN", 0x20, file_cluster("0:/OTHER.BIN"), static_cast<uint32_t>(kD2.size())}},
                     pokes);
            return true;
        });
        const std::string src = (host / "three.bin").string();
        std::string e1, e2, e3, e4;
        const St as_tree = sdcard::add_to_image(P, (host / "T").string(), "/DUPX/D\x82R", false, e4, nullptr, true);
        const St in_dir = sdcard::add_to_image(P, src, "/DUPX/D\x82R/y.bin", false, e1, nullptr, true);
        const St on_file = sdcard::add_to_image(P, src, "/DUPX/F\x82L.BIN", true, e2, nullptr, true);
        const St named = sdcard::add_to_image(P, src, "/DUPX/DER/z.bin", false, e3, nullptr, true);
        check("SDOPS-59", "add_to_image with dest_is_bytes: a folder (as a path, or as a copied tree) or a file FatFs resolves to an earlier entry (same 8.3 name) is refused (DestInvalid), the earlier folder and file keep their contents; the earlier folder by its own name still takes a file",
              as_tree == St::DestInvalid && e4.find("cannot address") != std::string::npos && in_dir == St::DestInvalid && on_file == St::DestInvalid && e1.find("cannot address") != std::string::npos &&
                  e2.find("cannot address") != std::string::npos && named == St::Ok && names_of(P, "/DA") == "z.bin" && names_of(P, "/DB").empty(),
              e1 + " | " + e2 + " | " + e3 + " | " + e4 + " | DA=" + names_of(P, "/DA") + " DB=" + names_of(P, "/DB"));
    }

    // SDCOH-05 (GH #558, 2026-11-25): every SUCCESSFUL write through the card engines is a jnext write: it moves
    // SdCardDevice::host_write_generation() (the warm start and the digest baseline compare it), once per disk_write
    // call, on both arms (plain file, writes file). A read never moves it, and neither does a refused write (a FAT16
    // card's DestInvalid, a read-only attach's write protect). The count comes from the glue's own disk_write counter, so
    // the row checks the mechanism and not just "some growth".
    {
        auto copy_of = [&](const std::string& master) {
            const std::string p = tmp_name("coh") + ".img";
            fs::copy_file(master, p, fs::copy_options::overwrite_existing);
            return p;
        };
        struct Snap {
            std::uint64_t gen, writes;
        };
        auto snap = [] { return Snap{SdCardDevice::host_write_generation(), fatfs_glue::write_count_for_test()}; };
        std::string err;
        auto moves_with_writes = [&](const Snap& a, const Snap& b) {
            return b.writes > a.writes && b.gen - a.gen == b.writes - a.writes;
        };
        auto unchanged = [&](const Snap& a, const Snap& b) { return a.gen == b.gen && a.writes == b.writes; };
        bool plain_ok = true, journal_ok = true, reads_ok = true, refused_ok = true;
        for (int mode = 0; mode < 2; ++mode) {
            const std::string c = copy_of(G);
            if (mode == 1) {
                sd_write_journal::Binding bd;
                bd.bytes = fs::file_size(c);
                bd.sha256.fill(0x6E);
                std::string why;
                sd_write_journal::bind(c, (g_dir / ("coh" + std::to_string(++g_n) + ".sdw")).string(), bd, false, why);
            }
            const Snap a = snap();
            const bool r1 = ops::rename(c, "/FILE.BIN", "FILE2.BIN", err) == St::Ok;
            const Snap b = snap();
            const bool r2 = sdcard::add_to_image(c, (host / "three.bin").string(), "/ADDED.BIN", false, err, nullptr, true) == St::Ok;
            const Snap d = snap();
            (mode == 0 ? plain_ok : journal_ok) = r1 && r2 && moves_with_writes(a, b) && moves_with_writes(b, d);
            // reads
            ops::CardInfo ci;
            std::vector<ops::CardEntry> le;
            const Snap e0 = snap();
            ops::probe(c, ci, err);
            ops::list_dir(c, "/", le, err);
            extracted(c, "/MYDIR");
            reads_ok = reads_ok && unchanged(e0, snap());
            sd_write_journal::forget_image(c);
        }
        {   // a refused write: FAT16 card (DestInvalid), then a read-only attach's write
            const std::string f = copy_of(F16);
            const Snap a = snap();
            const bool dest_invalid = ops::rename(f, "/GAMES", "G2", err) == St::DestInvalid;
            std::string aerr;
            const bool attached = fatfs_glue::attach(0, G, kLba, kFat32Sectors, aerr, nullptr, true);
            std::vector<uint8_t> blk(512, 0xA5);
            const DRESULT wr = disk_write(0, blk.data(), 100, 1);
            fatfs_glue::detach(0);
            refused_ok = dest_invalid && attached && wr == RES_WRPRT && unchanged(a, snap());
        }
        check("SDCOH-05", "a successful engine write (rename, add_to_image) moves the write generation exactly once per disk_write call, on a plain card and with a writes file; probe, list_dir and extract never move it; a refused write (a FAT16 card's DestInvalid, a read-only attach's write protect) does not",
              plain_ok && journal_ok && reads_ok && refused_ok,
              std::string("plain=") + (plain_ok ? "1" : "0") + " journal=" + (journal_ok ? "1" : "0") + " reads=" + (reads_ok ? "1" : "0") +
                  " refused=" + (refused_ok ? "1" : "0"));
    }

    sd_write_journal::forget_all_for_test();
    std::error_code ec;
    fs::permissions(g_dir, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(g_dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
