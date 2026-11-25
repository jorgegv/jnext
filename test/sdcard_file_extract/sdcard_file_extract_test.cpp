// --sdcard-file-extract: copy a file or directory OUT of an SD card image (GH #557), src/core/sdcard_file_extract.
//
// Oracles:
//   - The issue text and the decisions of its plan: the card path syntax, the lookup rules, the local
//     name (the card's STORED name, escaped for the host by the same escape_name the export uses), the
//     refusal of an existing local name, all-or-nothing, the exit statuses (file-add's table).
//   - FatFs R0.15b (third_party/fatfs/ff.c, the library the firmware runs): which cards mount
//     (find_volume), how a long or 8.3 name is matched (create_name, dir_find), what get_fileinfo returns.
//   - The FAT specification (fatgen103): the layout of the hand-made directory entries and their LFN
//     checksum. The expected bytes and names below are written out here, never read back through the engine.
//
// Fixtures are built at run time (FatFs f_mkfs plus byte pokes); never a NextZXOS image, in any form.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/sdcard_file_extract.h"

#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sha256.h"
#include "peripheral/sd_write_journal.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
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
using sdcard::ExtractSummary;

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

#include "sdfx_helpers.h"

namespace {

using Tree = std::map<std::string, Bytes>;

struct Run {
    FileAddStatus st = FileAddStatus::Ok;
    std::string err;
    ExtractSummary sum;
    fs::path dest;
};

fs::path fresh_dest() {
    const fs::path d = g_dir / ("dest-" + std::to_string(++g_n));
    fs::create_directories(d);
    return d;
}

Run run_in(const std::string& img, const std::string& card_path, const fs::path& dest) {
    Run r;
    r.dest = dest;
    r.st = sdcard::extract_from_image(img, card_path, dest.string(), r.err, &r.sum);
    return r;
}
Run run(const std::string& img, const std::string& card_path) { return run_in(img, card_path, fresh_dest()); }
// The same with the path as CP437 BYTES taken from a listing (the SD card file manager's entry point).
std::string g_bytes_card;   // the card of the bytes-mode rows (SDFX-38/39 build it, 40/41 reuse it)
Run run_bytes(const std::string& img, const std::string& card_path) {
    Run r;
    r.dest = fresh_dest();
    r.st = sdcard::extract_from_image(img, card_path, r.dest.string(), r.err, &r.sum, true);
    return r;
}

std::string info(const Run& r) {
    return std::string("status=") + std::to_string(static_cast<int>(r.st)) + " err=" + r.err;
}

bool is(const Run& r, FileAddStatus st) { return r.st == st; }
bool empty_dest(const Run& r) { return tree_of(r.dest).empty(); }

// The general card: FAT32, MBR type 0x0C at LBA 2048, built once and only read.
const Bytes kWotef = payload(1500, 1);      // 3 clusters
const Bytes kMixed = payload(800, 2);
const Bytes kLong = payload(300, 3);
const Bytes kFileBin = payload(400, 4);
const Bytes kMyA = payload(700, 5);
const Bytes kMyB = payload(1, 6);
const Bytes kMySub = payload(600, 7);
const Bytes kMyFile = payload(900, 8);
const Bytes kFrag = payload(40 * 512, 9);
const Bytes kAttr = payload(100, 10);

bool populate_general(FATFS&, Pokes&) {
    bool ok = f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
    ok = ok && f_mkdir("0:/MIXED") == FR_OK && fat_write("0:/MIXED/WotEF.nex", kMixed);
    ok = ok && fat_write("0:/Long File Name.txt", kLong);
    ok = ok && fat_write("0:/FILE.BIN", kFileBin);
    ok = ok && f_mkdir("0:/MYDIR") == FR_OK && fat_write("0:/MYDIR/A.BIN", kMyA) &&
         fat_write("0:/MYDIR/B.BIN", kMyB) && fat_write("0:/MYDIR/FILE.BIN", kMyFile) &&
         f_mkdir("0:/MYDIR/SUB") == FR_OK && fat_write("0:/MYDIR/SUB/C.BIN", kMySub) &&
         f_mkdir("0:/MYDIR/EMPTYD") == FR_OK && fat_write("0:/MYDIR/EMPTY.TXT", {});
    ok = ok && f_mkdir("0:/FRAG") == FR_OK && fat_write_fragmented("0:/FRAG/FRAG.BIN", "0:/FRAG/DECOY.BIN", kFrag);
    ok = ok && f_mkdir("0:/ATTR") == FR_OK && fat_write("0:/ATTR/R.BIN", kAttr) &&
         fat_write("0:/ATTR/H.BIN", kAttr) && fat_write("0:/ATTR/S.BIN", kAttr);
    ok = ok && f_chmod("0:/ATTR/R.BIN", AM_RDO, AM_RDO) == FR_OK &&
         f_chmod("0:/ATTR/H.BIN", AM_HID, AM_HID) == FR_OK && f_chmod("0:/ATTR/S.BIN", AM_SYS, AM_SYS) == FR_OK;
    return ok;
}

std::string make_general(int& fs_type) {
    const std::string p = tmp_name("general") + ".img";
    fs_type = make_card(p, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_general);
    return p;
}

// ---- hostile cards -------------------------------------------------------------------------------

// FAT16 card with /ODD: five names a host cannot take as they are, hand-made as LFN entries.
const Bytes kD1 = payload(200, 21), kD2 = payload(210, 22), kD3 = payload(220, 23), kD4 = payload(230, 24), kD5 = payload(240, 25);

bool populate_odd(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/ODD") != FR_OK) return false;
    const char* names[5] = {"0:/DATA/D1.BIN", "0:/DATA/D2.BIN", "0:/DATA/D3.BIN", "0:/DATA/D4.BIN", "0:/DATA/D5.BIN"};
    const Bytes* data[5] = {&kD1, &kD2, &kD3, &kD4, &kD5};
    uint32_t cl[5];
    for (int i = 0; i < 5; ++i) {
        if (!fat_write(names[i], *data[i])) return false;
        cl[i] = file_cluster(names[i]);
    }
    const uint32_t odd = dir_cluster("0:/ODD");
    std::vector<HandEnt> ents = {
        {u"CON.TXT", "CON~1   TXT", 0x20, cl[0], static_cast<uint32_t>(kD1.size())},
        {u"A:B", "A_B        ", 0x20, cl[1], static_cast<uint32_t>(kD2.size())},
        {u"FOO.", "FOO~1      ", 0x20, cl[2], static_cast<uint32_t>(kD3.size())},
        {u"x/y", "X_Y        ", 0x20, cl[3], static_cast<uint32_t>(kD4.size())},
        {u"café", "CAF~1      ", 0x20, cl[4], static_cast<uint32_t>(kD5.size())},
    };
    hand_dir(fsv, odd, 2, ents, pokes);
    return odd != 0;
}

// FAT16 card with /ODD2/<LFN "..">.
bool populate_dotdot(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/ODD2") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1)) return false;
    const uint32_t c = file_cluster("0:/DATA/D1.BIN");
    hand_dir(fsv, dir_cluster("0:/ODD2"), 2,
             {{u"..", "DOTDOT~1   ", 0x20, c, static_cast<uint32_t>(kD1.size())}}, pokes);
    return c != 0;
}

// /DUP: the short entry ABC and the long entry "abc" (the same bytes, so only the names tell them apart):
// FatFs reads "abc" as ABC, and a case-insensitive host cannot hold both.
bool populate_dup(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/DUP") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
        !fat_write("0:/DATA/D2.BIN", kD2))
        return false;
    hand_dir(fsv, dir_cluster("0:/DUP"), 2,
             {{u"", "ABC        ", 0x20, file_cluster("0:/DATA/D1.BIN"), static_cast<uint32_t>(kD1.size())},
              {u"abc", "ABC~1      ", 0x20, file_cluster("0:/DATA/D1.BIN"), static_cast<uint32_t>(kD1.size())}},
             pokes);
    return true;
}

// GH #558: /DUPS holds a short-only DER.BIN (D1) and a LONG entry "D<0x82> a long name.bin" (D2) whose short name is the SAME
// DER.BIN (a damaged or foreign-written directory); /SFNO holds a short-only X<0x90>Y.BIN (no long name, a high byte in the
// 8.3 name itself); /caf<0x82>.bin is a FatFs-made long name with a code page 437 byte.
bool populate_bytes(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/DUPS") != FR_OK || f_mkdir("0:/DUPX") != FR_OK || f_mkdir("0:/SFNO") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
        !fat_write("0:/DATA/D2.BIN", kD2) || !fat_write("0:/caf\x82.bin", kLong))
        return false;
    const uint32_t c1 = file_cluster("0:/DATA/D1.BIN"), c2 = file_cluster("0:/DATA/D2.BIN");
    hand_dir(fsv, dir_cluster("0:/DUPS"), 2,
             {{u"", "DER     BIN", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"D\u00E9 a long name.bin", "DER     BIN", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    // /DUPX: a short-only DER.BIN, then a long name D<e-acute>R.BIN
    // whose 8.3 name is the SAME: FatFs resolves a typed "D<0x82>R.BIN" to the FIRST entry (the listing cannot address the long one)
    hand_dir(fsv, dir_cluster("0:/DUPX"), 2,
             {{u"", "DER     BIN", 0x20, c1, static_cast<uint32_t>(kD1.size())},
              {u"D\u00E9R.BIN", "DER     BIN", 0x20, c2, static_cast<uint32_t>(kD2.size())}},
             pokes);
    hand_dir(fsv, dir_cluster("0:/SFNO"), 2, {{u"", std::string("X\x90Y     BIN"), 0x20, c2, static_cast<uint32_t>(kD2.size())}}, pokes);
    return c1 != 0 && c2 != 0;
}

// FAT32: /LOOP/SUB points at /LOOP's own cluster; /LOOP0/SUB has start cluster 0 (the root on FAT32).
bool populate_loops(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/LOOP") != FR_OK || f_mkdir("0:/LOOP0") != FR_OK) return false;
    hand_dir(fsv, dir_cluster("0:/LOOP"), 2, {{u"", "SUB        ", 0x10, dir_cluster("0:/LOOP"), 0}}, pokes);
    hand_dir(fsv, dir_cluster("0:/LOOP0"), 2, {{u"", "SUB        ", 0x10, 0, 0}}, pokes);
    return true;
}

// /BROKEN: a good file, then an entry that says 4096 bytes and owns one 512-byte cluster.
const Bytes kGood = payload(600, 31);
bool populate_broken(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/BROKEN") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1) ||
        !fat_write("0:/BROKEN/A_GOOD.BIN", kGood))
        return false;
    hand_dir(fsv, dir_cluster("0:/BROKEN"), 3,
             {{u"", "B_BAD   BIN", 0x20, file_cluster("0:/DATA/D1.BIN"), 4096}}, pokes);
    return true;
}

// /BADNAME: long name "x." and short name "A*B": FatFs can open it by neither.
bool populate_badname(FATFS& fsv, Pokes& pokes) {
    if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/BADNAME") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1)) return false;
    hand_dir(fsv, dir_cluster("0:/BADNAME"), 2,
             {{u"x.", "A*B        ", 0x20, file_cluster("0:/DATA/D1.BIN"), static_cast<uint32_t>(kD1.size())}}, pokes);
    return true;
}

// A FAT32 volume, partition-less, whose BPB says 32456 data clusters (1 sector each): FatFs, like the
// Next's firmware, refuses it (a FAT32 layout under the FAT32 minimum).
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

}  // namespace

int main() {
    g_dir = make_scratch_dir("sdfx");

    int gt = 0;
    const std::string G = make_general(gt);
    const std::string g_sha0 = sha_file(G);

    // SDFX-01 .. SDFX-04: the card formats the firmware mounts
    {
        const Run r = run(G, "/GAMES/WOTEF.NEX");
        check("SDFX-01", "FAT32, MBR type 0x0C: a 3-cluster file extracts byte-exact under its own name (the issue)",
              gt == FS_FAT32 && is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"WOTEF.NEX", kWotef}} &&
                  r.sum.files == 1 && !r.sum.is_dir && r.sum.bytes == kWotef.size(),
              info(r));
    }
    {
        const std::string p = tmp_name("fat16") + ".img";
        const int t = make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS&, Pokes&) {
            return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
        });
        const Run r = run(p, "/GAMES/WOTEF.NEX");
        check("SDFX-02", "FAT16, MBR type 0x06 (not 0x0B/0x0C): extracts (ff.c find_volume takes any non-zero type over a FAT boot record)",
              t == FS_FAT16 && is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"WOTEF.NEX", kWotef}}, info(r));
    }
    {
        const std::string p = tmp_name("fat12") + ".img";
        const int t = make_card(p, 0, 2000, FM_FAT, 512, 512, 0,
                                [&](FATFS&, Pokes&) { return fat_write("0:/TEST.BIN", kWotef); });
        const Run r = run(p, "/TEST.BIN");
        check("SDFX-03", "FAT12, no partition table: a file in the fixed root extracts (FAT12 entry encoding, ff.c get_fat)",
              t == FS_FAT12 && is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"TEST.BIN", kWotef}}, info(r));
    }
    {
        const std::string p = tmp_name("fat32sfd") + ".img";
        const int t = make_card(p, 0, kFat32Sectors, FM_FAT32, 512, 0, 0,
                                [&](FATFS&, Pokes&) { return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef); });
        const Run r = run(p, "/GAMES/WOTEF.NEX");
        check("SDFX-04", "FAT32, no partition table: extracts",
              t == FS_FAT32 && is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"WOTEF.NEX", kWotef}}, info(r));
    }

    // SDFX-05 .. SDFX-06
    {
        const Run r = run(G, "/MYDIR");
        const Tree want = {{"MYDIR/", {}}, {"MYDIR/A.BIN", kMyA}, {"MYDIR/B.BIN", kMyB}, {"MYDIR/FILE.BIN", kMyFile},
                           {"MYDIR/SUB/", {}}, {"MYDIR/SUB/C.BIN", kMySub}, {"MYDIR/EMPTYD/", {}}, {"MYDIR/EMPTY.TXT", {}}};
        check("SDFX-05", "a directory extracts recursively into ./MYDIR: names, bytes, an empty subdirectory and an empty file; counts",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == want && r.sum.is_dir && r.sum.files == 5 &&
                  r.sum.dirs == 3 && r.sum.bytes == kMyA.size() + kMyB.size() + kMyFile.size() + kMySub.size(),
              info(r) + " files=" + std::to_string(r.sum.files) + " dirs=" + std::to_string(r.sum.dirs));
    }
    {
        const Run r = run(G, "/FRAG/FRAG.BIN");
        check("SDFX-06", "a file whose cluster chain is fragmented (a decoy written alternately) extracts byte-exact",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"FRAG.BIN", kFrag}}, info(r));
    }

    // SDFX-07 .. SDFX-10: names
    {
        const Run r = run(G, "/Long File Name.txt");
        check("SDFX-07", "a long name is found by itself and the local name is the long name",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"Long File Name.txt", kLong}}, info(r));
    }
    {
        const Run r = run(G, "/LONGFI~1.TXT");
        check("SDFX-08", "the same file by its 8.3 alias: the local name is still the LONG name (the stored name, not the spelling typed)",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"Long File Name.txt", kLong}} &&
                  r.sum.card_path == "/Long File Name.txt",
              info(r) + " card_path=" + r.sum.card_path);
    }
    {
        const Run r = run(G, "/mixed/WOTEF.NEX");
        check("SDFX-09", "a card name WotEF.nex asked for as /mixed/WOTEF.NEX (any case): the local name keeps the stored case",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"WotEF.nex", kMixed}} &&
                  r.sum.card_path == "/MIXED/WotEF.nex",
              info(r) + " card_path=" + r.sum.card_path);
    }
    {
        const Run a = run(G, "\\MYDIR\\FILE.BIN");
        const Run b = run(G, "MYDIR/FILE.BIN");
        const Run c = run(G, "//MYDIR//FILE.BIN");
        const Tree want = {{"FILE.BIN", kMyFile}};
        check("SDFX-10", "a backslash separates like a slash; the leading separator is optional; empty components collapse",
              is(a, FileAddStatus::Ok) && is(b, FileAddStatus::Ok) && is(c, FileAddStatus::Ok) &&
                  tree_of(a.dest) == want && tree_of(b.dest) == want && tree_of(c.dest) == want,
              info(a) + " | " + info(b) + " | " + info(c));
    }

    // SDFX-11 .. SDFX-16: refusals of the card path (status 2, nothing extracted)
    {
        const Run r = run(G, "/NOPE.BIN");
        check("SDFX-11", "a path that is not on the card is status 2 and extracts nothing",
              is(r, FileAddStatus::SourceUnreadable) && empty_dest(r), info(r));
    }
    {
        const Run r = run(G, "/FILE.BIN/X");
        check("SDFX-12", "a path through a file (/FILE.BIN/X) is status 2 and extracts nothing",
              is(r, FileAddStatus::SourceUnreadable) && r.err.find("is a file, not a directory") != std::string::npos &&
                  empty_dest(r),
              info(r));
    }
    {
        const Run a = run(G, "");
        const Run b = run(G, "/");
        const Run c = run(G, "//");
        auto names_form = [](const Run& r) { return r.err.find("e.g. /DIR/NAME") != std::string::npos; };
        check("SDFX-13", "an empty path, / and // name nothing: status 2, the message gives the form, nothing extracted",
              is(a, FileAddStatus::SourceUnreadable) && is(b, FileAddStatus::SourceUnreadable) &&
                  is(c, FileAddStatus::SourceUnreadable) && names_form(a) && names_form(b) && names_form(c) &&
                  empty_dest(a) && empty_dest(b) && empty_dest(c),
              info(a) + " | " + info(b) + " | " + info(c));
    }
    {
        const Run a = run(G, "/MYDIR/../FILE.BIN");
        const Run b = run(G, "/./FILE.BIN");
        check("SDFX-14", "a '..' or '.' component is refused up front (status 2) and the message names it",
              is(a, FileAddStatus::SourceUnreadable) && a.err.find("contains '..'") != std::string::npos &&
                  a.err.find("full path from the card root") != std::string::npos &&
                  is(b, FileAddStatus::SourceUnreadable) && b.err.find("contains '.'") != std::string::npos &&
                  b.err.find("full path from the card root") != std::string::npos &&
                  empty_dest(a) && empty_dest(b),
              info(a) + " | " + info(b));
    }
    {
        const Run a = run(G, "/FILE.BIN.");
        const Run b = run(G, "/FILE.BIN ");
        // the same inside the path, with either separator (FatFs would open /MYDIR/SUB/C.BIN for SUB.)
        const Run c = run(G, "/MYDIR/SUB./C.BIN");
        const Run d = run(G, "\\MYDIR\\SUB.\\C.BIN");
        check("SDFX-15", "a trailing dot or space is refused (FatFs strips them, so /FILE.BIN. would open /FILE.BIN), also on a directory in the middle of the path with / or \\: status 2, nothing extracted",
              is(a, FileAddStatus::SourceUnreadable) && is(b, FileAddStatus::SourceUnreadable) &&
                  is(c, FileAddStatus::SourceUnreadable) && is(d, FileAddStatus::SourceUnreadable) &&
                  empty_dest(a) && empty_dest(b) && empty_dest(c) && empty_dest(d),
              info(a) + " | " + info(b) + " | " + info(c) + " | " + info(d));
    }
    {
        const Run r = run(G, "/caf\xC3\xA9");
        check("SDFX-16", "a byte outside printable ASCII (UTF-8 argument) is status 2 and the message points at the 8.3 name",
              is(r, FileAddStatus::SourceUnreadable) && r.err.find("use the 8.3 name") != std::string::npos && empty_dest(r), info(r));
    }

    // SDFX-17, SDFX-18: the destination
    {
        const fs::path d1 = fresh_dest();
        write_host_file(d1 / "WOTEF.NEX", {'k', 'e', 'e', 'p'});
        const Run a = run_in(G, "/GAMES/WOTEF.NEX", d1);
        const fs::path d2 = fresh_dest();
        fs::create_directories(d2 / "WOTEF.NEX");
        write_host_file(d2 / "WOTEF.NEX" / "mine.txt", {'m'});
        const Run b = run_in(G, "/GAMES/WOTEF.NEX", d2);
        check("SDFX-17", "an existing local file, or directory, of the name is refused with status 4 and left untouched",
              is(a, FileAddStatus::DestExists) && tree_of(d1) == Tree{{"WOTEF.NEX", Bytes{'k', 'e', 'e', 'p'}}} &&
                  is(b, FileAddStatus::DestExists) && tree_of(d2) == Tree{{"WOTEF.NEX/", {}}, {"WOTEF.NEX/mine.txt", Bytes{'m'}}},
              info(a) + " | " + info(b));
    }
    {
        const Run r = run_in(G, "/GAMES/WOTEF.NEX", g_dir / "no-such-dest");
        check("SDFX-18", "a destination directory that does not exist is status 3",
              is(r, FileAddStatus::DestInvalid) && r.err.find("not a directory") != std::string::npos &&
                  !fs::exists(g_dir / "no-such-dest"),
              info(r));
    }

    // SDFX-19 .. SDFX-20: names a host cannot hold
    {
        const std::string p = tmp_name("odd") + ".img";
        const int t = make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, populate_odd);
        const Run r = run(p, "/ODD");
        const Tree want = {{"ODD/", {}}, {"ODD/%43ON.TXT", kD1}, {"ODD/A%3AB", kD2}, {"ODD/FOO%2E", kD3},
                           {"ODD/x%2Fy", kD4}, {"ODD/caf%82", kD5}};
        std::vector<std::string> esc = r.sum.escaped;
        std::sort(esc.begin(), esc.end());
        const std::vector<std::string> want_esc = {"%43ON.TXT", "A%3AB", "FOO%2E", "caf%82", "x%2Fy"};
        // the same entry as the TOP of an extraction, asked for by its 8.3 alias: the local name is escaped too
        const Run top = run(p, "/ODD/A_B");
        check("SDFX-19", "LFN names CON.TXT, A:B, FOO. (read by its 8.3 name), x/y and a CP437 byte 0x82 land as %43ON.TXT, A%3AB, FOO%2E, x%2Fy, caf%82 with their own bytes; all five are listed; the top entry A:B asked for by its 8.3 name lands as A%3AB",
              t == FS_FAT16 && is(r, FileAddStatus::Ok) && tree_of(r.dest) == want && esc == want_esc &&
                  is(top, FileAddStatus::Ok) && tree_of(top.dest) == Tree{{"A%3AB", kD2}} &&
                  top.sum.escaped == std::vector<std::string>{"A%3AB"},
              info(r) + " tree=" + [&] { std::string s; for (auto& kv : tree_of(r.dest)) s += kv.first + ";"; return s; }());
    }
    {
        const std::string p = tmp_name("dotdot") + ".img";
        const int t = make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, populate_dotdot);
        const fs::path d = fresh_dest();
        std::set<std::string> before;
        for (const auto& e : fs::directory_iterator(g_dir)) before.insert(e.path().filename().string());
        const Run r = run_in(p, "/ODD2", d);
        std::set<std::string> after;
        for (const auto& e : fs::directory_iterator(g_dir)) after.insert(e.path().filename().string());
        check("SDFX-20", "a file named .. on the card lands as .%2E inside the extracted directory; nothing is written outside it",
              t == FS_FAT16 && is(r, FileAddStatus::Ok) && tree_of(d) == Tree{{"ODD2/", {}}, {"ODD2/.%2E", kD1}} &&
                  before == after,
              info(r));
    }

    // SDFX-21 .. SDFX-25: damaged cards (status 5, nothing left behind)
    {
        const std::string p = tmp_name("dup") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, populate_dup);
        const Run r = run(p, "/DUP");
        check("SDFX-21", "two entries of one directory that differ only in letter case (short ABC, long abc) are status 5, nothing extracted",
              is(r, FileAddStatus::ImageUnusable) && empty_dest(r), info(r));
    }
    {
        const std::string p = tmp_name("loops") + ".img";
        const int t = make_card(p, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_loops);
        const Run r = run(p, "/LOOP");
        check("SDFX-22", "a subdirectory whose start cluster is its parent's: status 5, terminates, nothing extracted",
              t == FS_FAT32 && is(r, FileAddStatus::ImageUnusable) && empty_dest(r), info(r));
        const Run z = run(p, "/LOOP0");
        check("SDFX-23", "a subdirectory with start cluster 0 (the root on FAT32): status 5, terminates, nothing extracted",
              is(z, FileAddStatus::ImageUnusable) && empty_dest(z), info(z));
    }
    {
        const std::string p = tmp_name("broken") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, populate_broken);
        const Run r = run(p, "/BROKEN");
        check("SDFX-24", "a file whose chain ends before its size: status 5, and the file already copied and the directory are removed again",
              is(r, FileAddStatus::ImageUnusable) && empty_dest(r), info(r));
    }
    {
        const std::string p = tmp_name("badname") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, populate_badname);
        const Run r = run(p, "/BADNAME");
        check("SDFX-25", "an entry FatFs can open by neither its long name (x.) nor its 8.3 name (A*B): status 5 naming it, nothing extracted",
              is(r, FileAddStatus::ImageUnusable) && r.err.find("x.") != std::string::npos && empty_dest(r), info(r));
    }

    // SDFX-26 .. SDFX-27: the writes file
    std::string jimg, jfile, jimg_sha0, jfile_sha1;
    {
        jimg = tmp_name("journal") + ".img";
        jfile = tmp_name("journal") + ".sdw";
        const Bytes oldb = payload(500, 41), keep = payload(300, 42), newf = payload(700, 43), repl = payload(650, 44);
        make_card(jimg, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
            return fat_write("0:/OLD.BIN", oldb) && fat_write("0:/KEEP.BIN", keep);
        });
        jimg_sha0 = sha_file(jimg);
        const fs::path hn = g_dir / "host-new.bin", hr = g_dir / "host-old.bin";
        write_host_file(hn, newf);
        write_host_file(hr, repl);
        sd_write_journal::forget_all_for_test();
        bind_journal(jimg, jfile);
        std::string aerr;
        const FileAddStatus a1 = sdcard::add_to_image(jimg, hn.string(), "/NEW.BIN", false, aerr);
        const FileAddStatus a2 = sdcard::add_to_image(jimg, hr.string(), "/OLD.BIN", true, aerr);
        jfile_sha1 = sha_file(jfile);
        const Run n = run(jimg, "/NEW.BIN");
        const Run o = run(jimg, "/OLD.BIN");
        const Run k = run(jimg, "/KEEP.BIN");
        sd_write_journal::forget_all_for_test();
        const Run n2 = run(jimg, "/NEW.BIN");
        const Run o2 = run(jimg, "/OLD.BIN");
        check("SDFX-26", "with a writes file bound, extract gives the journal's bytes for an added and a replaced file (through_writes); without it, not found and the old bytes",
              a1 == FileAddStatus::Ok && a2 == FileAddStatus::Ok && is(n, FileAddStatus::Ok) &&
                  tree_of(n.dest) == Tree{{"NEW.BIN", newf}} && n.sum.through_writes &&
                  tree_of(o.dest) == Tree{{"OLD.BIN", repl}} && tree_of(k.dest) == Tree{{"KEEP.BIN", keep}} &&
                  is(n2, FileAddStatus::SourceUnreadable) && is(o2, FileAddStatus::Ok) &&
                  tree_of(o2.dest) == Tree{{"OLD.BIN", oldb}} && !o2.sum.through_writes,
              info(n) + " | " + info(o) + " | " + info(n2) + " | " + info(o2) + " | " + aerr);
    }
    {
        const std::string p = tmp_name("jboot") + ".img";
        make_card(p, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C,
                  [&](FATFS&, Pokes&) { return fat_write("0:/OLD.BIN", kWotef); });
        const std::string jf = tmp_name("jboot") + ".sdw";
        Bytes sec(512, 0);
        {
            std::ifstream f(p, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(kLba) * 512);
            f.read(reinterpret_cast<char*>(sec.data()), 512);
        }
        sd_write_journal::forget_all_for_test();
        bind_journal(p, jf);
        sd_write_journal::for_image(p)->commit(kLba, sec.data());
        const Run a = run(p, "/OLD.BIN");
        sd_write_journal::forget_all_for_test();
        const std::string jf2 = tmp_name("jmbr") + ".sdw";
        bind_journal(p, jf2);
        Bytes mbr(512, 0);
        {
            std::ifstream f(p, std::ios::binary);
            f.read(reinterpret_cast<char*>(mbr.data()), 512);
        }
        sd_write_journal::for_image(p)->commit(0, mbr.data());
        const Run b = run(p, "/OLD.BIN");
        sd_write_journal::forget_all_for_test();
        check("SDFX-27", "a writes file that holds the volume's boot sector, or sector 0, is refused (status 5): the card it describes is not the geometry read from the image",
              is(a, FileAddStatus::ImageUnusable) && empty_dest(a) && is(b, FileAddStatus::ImageUnusable) && empty_dest(b),
              info(a) + " | " + info(b));
    }

    // SDFX-28: the image and the writes file are never written
    {
        const std::string g_sha1 = sha_file(G);
        check("SDFX-28", "after every extraction above, the general card and the journal card (and its writes file) have the digests they had",
              !g_sha0.empty() && g_sha0 == g_sha1 && !jimg_sha0.empty() && jimg_sha0 == sha_file(jimg) &&
                  !jfile_sha1.empty() && jfile_sha1 == sha_file(jfile),
              g_sha0 + " vs " + g_sha1);
    }

    // SDFX-29: a read-only image file
    {
        const std::string p = tmp_name("ro") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS&, Pokes&) {
            return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", kWotef);
        });
        std::error_code ec;
        fs::permissions(p, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                        fs::perm_options::remove, ec);
        const std::string sha = sha_file(p);
        const Run r = run(p, "/GAMES/WOTEF.NEX");
        const bool same = sha_file(p) == sha;
        fs::permissions(p, fs::perms::owner_write, fs::perm_options::add, ec);
        check("SDFX-29", "an image file without write permission extracts (it is opened read-only) and is not changed",
              is(r, FileAddStatus::Ok) && tree_of(r.dest) == Tree{{"WOTEF.NEX", kWotef}} && same, info(r));
    }

    // SDFX-30 .. SDFX-32: unusable cards (status 5)
    {
        const Run miss = run((g_dir / "no-such.img").string(), "/X.BIN");
        const std::string rnd = tmp_name("random") + ".img";
        write_host_file(rnd, payload(8192, 51));
        const Run bad = run(rnd, "/X.BIN");
        const std::string uc = tmp_name("under") + ".img";
        make_under_clustered(uc);
        const Run under = run(uc, "/X.BIN");
        check("SDFX-30", "a missing image, an image of random bytes and an under-clustered FAT32 layout are status 5; the last names its cause",
              is(miss, FileAddStatus::ImageUnusable) && is(bad, FileAddStatus::ImageUnusable) &&
                  is(under, FileAddStatus::ImageUnusable) && under.err.find("under-clustered") != std::string::npos &&
                  under.err.find("fix-sdcard-image.sh") != std::string::npos,
              info(miss) + " | " + info(bad) + " | " + info(under));
    }
    {
        const std::string p = tmp_name("ss1024") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06,
                  [&](FATFS&, Pokes&) { return fat_write("0:/X.BIN", kWotef); });
        poke_at(p, static_cast<uint64_t>(kLba) * 512 + 11, {0x00, 0x04});   // BPB_BytsPerSec = 1024
        const Run r = run(p, "/X.BIN");
        check("SDFX-31", "a 1024-byte-sector BPB is status 5 saying 512-byte sectors only",
              is(r, FileAddStatus::ImageUnusable) && r.err.find("512-byte sectors only") != std::string::npos, info(r));
    }
    {
        const std::string p = tmp_name("trunc") + ".img";
        make_card(p, 0, kFat32Sectors, FM_FAT32, 512, 0, 0,
                  [&](FATFS&, Pokes&) { return fat_write("0:/X.BIN", kWotef); });
        Bytes big(4);
        put32(big.data(), kFat32Sectors + 100000);                           // BPB_TotSec32 beyond the file
        poke_at(p, 32, big);
        const Run r = run(p, "/X.BIN");
        check("SDFX-32", "a BPB whose volume is larger than the image file is status 5 saying truncated (and does not hang)",
              is(r, FileAddStatus::ImageUnusable) && r.err.find("truncated") != std::string::npos, info(r));
    }

    // SDFX-33: R, H, S attributes are not reasons to skip a file
    {
        const Run r = run(G, "/ATTR");
        check("SDFX-33", "read-only, hidden and system files are extracted like any other",
              is(r, FileAddStatus::Ok) &&
                  tree_of(r.dest) == Tree{{"ATTR/", {}}, {"ATTR/R.BIN", kAttr}, {"ATTR/H.BIN", kAttr}, {"ATTR/S.BIN", kAttr}},
              info(r));
    }

    // SDFX-34: a partitioned card whose last file sits in the volume's last clusters (the glue's sector bound
    // is the volume's size, so a volume offset that is wrong shows only there)
    {
        const std::string p = tmp_name("tail") + ".img";
        const Bytes tail = payload(1500, 61);
        uint32_t tail_clust = 0, last_clust = 0;
        const int t = make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS& fsv, Pokes&) {
            DWORD nfree = 0;
            FATFS* fp = nullptr;
            if (f_getfree("0:", &nfree, &fp) != FR_OK || nfree < 8) return false;
            if (!fat_write("0:/FILL.BIN", Bytes(static_cast<size_t>(nfree - 3) * 512, 0xEE))) return false;
            if (!fat_write("0:/TAIL.BIN", tail)) return false;
            tail_clust = file_cluster("0:/TAIL.BIN");
            last_clust = fsv.n_fatent - 1;
            return true;
        });
        const Run r = run(p, "/TAIL.BIN");
        check("SDFX-34", "on a partitioned FAT16 card full to the end, the file in the volume's last clusters extracts byte-exact (the volume offset and size reach FatFs right)",
              t == FS_FAT16 && tail_clust + 2 >= last_clust && is(r, FileAddStatus::Ok) &&
                  tree_of(r.dest) == Tree{{"TAIL.BIN", tail}},
              info(r) + " tail=" + std::to_string(tail_clust) + " last=" + std::to_string(last_clust));
    }

    // SDFX-35: an ESC in a long name never reaches a message or the summary raw
    {
        const std::string p = tmp_name("esc") + ".img";
        make_card(p, kLba, kFat16Sectors, FM_FAT, 512, 512, 0x06, [&](FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/ESC") != FR_OK || f_mkdir("0:/ESC2") != FR_OK ||
                !fat_write("0:/DATA/D1.BIN", kD1))
                return false;
            const uint32_t c = file_cluster("0:/DATA/D1.BIN");
            const uint32_t n = static_cast<uint32_t>(kD1.size());
            hand_dir(fsv, dir_cluster("0:/ESC"), 2, {{u"\u001b[2Jevil", "EVIL~1     ", 0x20, c, n}}, pokes);
            hand_dir(fsv, dir_cluster("0:/ESC2"), 2, {{u"x\u001b", "A*B        ", 0x20, c, n}}, pokes);
            return true;
        });
        const Run ok = run(p, "/ESC/EVIL~1");
        const Run bad = run(p, "/ESC2");
        auto no_esc = [](const std::string& x) { return x.find('\x1b') == std::string::npos; };
        check("SDFX-35", "an ESC in a long name lands as %1B in the local name and the summary, and a refusal naming it shows %1B: no raw ESC reaches the output",
              is(ok, FileAddStatus::Ok) && tree_of(ok.dest) == Tree{{"%1B[2Jevil", kD1}} && no_esc(ok.sum.card_path) &&
                  ok.sum.card_path == "/ESC/%1B[2Jevil" && no_esc(ok.sum.host_path) &&
                  is(bad, FileAddStatus::ImageUnusable) && no_esc(bad.err) && bad.err.find("%1B") != std::string::npos,
              info(ok) + " | " + info(bad));
    }

    // SDFX-36: --sdcard-export-changes lists a name it cannot export (a trailing dot) without a raw ESC
    {
        const std::string base = tmp_name("expbase") + ".img", card = tmp_name("expcard") + ".img";
        auto pop = [&](bool with, FATFS& fsv, Pokes& pokes) {
            if (f_mkdir("0:/DATA") != FR_OK || f_mkdir("0:/D") != FR_OK || !fat_write("0:/DATA/D1.BIN", kD1)) return false;
            if (with)
                hand_dir(fsv, dir_cluster("0:/D"), 2,
                         {{u"e\u001b[2J.", "EVIL~1     ", 0x20, file_cluster("0:/DATA/D1.BIN"), static_cast<uint32_t>(kD1.size())}}, pokes);
            return true;
        };
        make_card(base, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& f, Pokes& k) { return pop(false, f, k); });
        make_card(card, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS& f, Pokes& k) { return pop(true, f, k); });
        sdcard::carry::ExportRequest req;
        req.image = card;
        req.base_image = base;
        req.dir = (g_dir / "export-out").string();
        sdcard::carry::ExportReport rep;
        std::string err;
        const FileAddStatus st = sdcard::carry::export_changes(req, rep, err);
        bool raw = false;
        for (const std::string& n : rep.skipped) raw = raw || n.find('\x1b') != std::string::npos;
        check("SDFX-36", "export-changes lists an unexportable entry whose long name holds an ESC as %1B, never raw",
              st == FileAddStatus::Ok && rep.skipped.size() == 1 && !raw && rep.skipped[0].find("%1B") != std::string::npos,
              "status=" + std::to_string(static_cast<int>(st)) + " " + err + " skipped=" + std::to_string(rep.skipped.size()));
    }

    // SDFX-38 / SDFX-39 (GH #558): the BYTES entry point (card_path_is_bytes) of the SD card file manager
    {
        const std::string p2 = tmp_name("bytes") + ".img";
        const int bt = make_card(p2, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, populate_bytes);
        g_bytes_card = p2;
        const Run cli = run(p2, "/caf\x82.bin");                 // the command line still refuses it, with its advice
        const Run by = run_bytes(p2, "/caf\x82.bin");
        check("SDFX-38", "a path holding a CP437 byte is refused on the command line (unchanged: status 2, the 8.3 advice) and extracted with card_path_is_bytes, under its escaped host name and with its own bytes",
              bt == FS_FAT32 && is(cli, FileAddStatus::SourceUnreadable) && cli.err.find("parent directory") != std::string::npos && empty_dest(cli) &&
                  is(by, FileAddStatus::Ok) && tree_of(by.dest) == Tree{{"caf%82.bin", kLong}},
              info(cli) + " | " + info(by));
        // F1a: the long entry's short name duplicates an earlier entry's: the bytes path acts on THE ENTRY NAMED, never on the other
        const Run dup = run_bytes(p2, "/DUPS/D\x82 a long name.bin");
        const Run first = run_bytes(p2, "/DUPS/DER.BIN");
        // F1b: a short-only name with a high byte (no ASCII alias exists)
        const Run sfn = run_bytes(p2, "/SFNO/X\x90Y.BIN");
        check("SDFX-39", "bytes path: a long entry whose 8.3 name duplicates an earlier entry's extracts ITS OWN bytes (the earlier entry keeps its own), and a short-only name with a high byte extracts",
              is(dup, FileAddStatus::Ok) && tree_of(dup.dest) == Tree{{"D%82 a long name.bin", kD2}} && is(first, FileAddStatus::Ok) &&
                  tree_of(first.dest) == Tree{{"DER.BIN", kD1}} && is(sfn, FileAddStatus::Ok) && tree_of(sfn.dest) == Tree{{"X%90Y.BIN", kD2}},
              info(dup) + " | " + info(first) + " | " + info(sfn));
    }
    // SDFX-40 (GH #558 K2): bytes mode refuses DEL (0x7F) in a component, as it does a control byte; a byte above 0x7E is not refused
    {
        const Run del = run_bytes(g_bytes_card, "/caf\x7F.bin");
        const Run ctl = run_bytes(g_bytes_card, "/caf\x01.bin");
        const Run hi = run_bytes(g_bytes_card, "/caf\x82.bin");
        check("SDFX-40", "bytes mode: a component holding DEL (0x7F) or a control byte is refused (status 2, nothing written); a byte above 0x7E is accepted",
              is(del, FileAddStatus::SourceUnreadable) && del.err.find("outside printable ASCII") != std::string::npos && empty_dest(del) &&
                  is(ctl, FileAddStatus::SourceUnreadable) && empty_dest(ctl) && is(hi, FileAddStatus::Ok),
              info(del) + " | " + info(ctl) + " | " + info(hi));
    }
    // SDFX-41 (GH #558 N1): bytes mode, a path the listing cannot address (a long entry behind an earlier entry with the SAME 8.3 name)
    // is refused at the engine, not extracted as the earlier entry under this name; the earlier entry itself still extracts
    {
        const Run un = run_bytes(g_bytes_card, "/DUPX/D\x82R.BIN");
        const Run first = run_bytes(g_bytes_card, "/DUPX/DER.BIN");
        check("SDFX-41", "bytes mode: a path FatFs resolves to a different entry than the one named (duplicate 8.3 name behind an earlier entry) is refused, nothing written; the earlier entry extracts under its own name",
              is(un, FileAddStatus::SourceUnreadable) && empty_dest(un) && un.err.find("different one") != std::string::npos &&
                  is(first, FileAddStatus::Ok) && tree_of(first.dest) == Tree{{"DER.BIN", kD1}},
              info(un) + " | " + info(first));
    }
    // SDFX-37: a writes file that does not exist yet is not "read through"
    {
        const std::string p = tmp_name("nojf") + ".img";
        make_card(p, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C,
                  [&](FATFS&, Pokes&) { return fat_write("0:/OLD.BIN", kWotef); });
        const std::string jf = tmp_name("nojf") + ".sdw";
        sd_write_journal::forget_all_for_test();
        std::error_code rec;
        fs::remove(jf, rec);
        bind_journal(p, jf);
        const Run r = run(p, "/OLD.BIN");
        const bool existed = fs::exists(jf, rec);
        sd_write_journal::forget_all_for_test();
        check("SDFX-37", "a writes file bound but not yet on disk is not reported as read through (through_writes false), and the file is still extracted",
              !existed && is(r, FileAddStatus::Ok) && !r.sum.through_writes && tree_of(r.dest) == Tree{{"OLD.BIN", kWotef}},
              info(r));
    }

    std::error_code ec;
    fs::permissions(g_dir, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(g_dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
