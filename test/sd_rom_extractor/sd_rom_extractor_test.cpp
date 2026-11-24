// SD ROM extractor compliance tests — exercise the host-side FAT32
// reader added in Wave 0.2 of the Task 8 Multiface plan.
//
// Coverage today:
//   SD-EXT-01  Happy path: extract /MACHINES/NEXT/enNxtmmc.rom (8192 B)
//              and verify the first 4 bytes match the canonical signature.
//   SD-EXT-02  Case-insensitive lookup: lowercase input resolves to the
//              same SFN entry and yields identical bytes to SD-EXT-01.
//   SD-EXT-03  Discriminative: extract /MACHINES/NEXT/48.rom (16384 B);
//              size and content differ from SD-EXT-01.
//   SD-EXT-04  Discriminative: extract /MACHINES/NEXT/enNextMf.rom (8192 B);
//              same size as SD-EXT-01 but different content.
//   SD-EXT-05  Root-level file: /TBBLUE.FW (635904 B). Confirms root-
//              directory parsing without any descent.
//   SD-EXT-06  Not found: /MACHINES/NEXT/nonexistent.rom returns false,
//              `out` empty.
//   SD-EXT-07  Wrong image: tiny / non-FAT file returns false without
//              crashing; error logged.
//   SD-EXT-08  Leading-slash optional: "MACHINES/NEXT/enNxtmmc.rom"
//              (no leading '/') resolves identically to SD-EXT-01.
//   SD-EXT-10..21  (GH #556) FAT12/16/32 cards the firmware's FatFs mounts:
//              any non-zero MBR type over a FAT boot record, a card with no
//              partition table, FAT16/FAT12 chains and root directories, the
//              cluster-count boundaries, and a 48K machine booting from one.
//   SD-EXT-28  a hostile FAT32 BPB (TotSec32 0xFFFFFFFF): the chain walk is capped by the image file.
//   SD-EXT-22..27  (review round 1) firmware scan before the 0x0C leniency, the
//              jump-byte half of check_fs, FAT16 DIR_FstClusHI ignored, and a cyclic
//              FAT16 directory bounded by the cluster count.
//              Fixtures are built at run time (FatFs f_mkfs, the firmware's
//              own library, or by hand); never an image in git.
//
// Fixture: the canonical 1 GB NextZXOS image (MBR + FAT32-LBA, 8 KB
// clusters) that jnext provisions for itself at
// ~/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img; JNEXT_TEST_SD_IMAGE overrides
// it, and run-unit-tests.sh points it at a private per-run clone so a
// concurrent writer cannot tear the read (GH #75/#77). The reference bytes
// for SD-EXT-01/03/04/05 were captured offline via `mtools` so the test
// is deterministic without depending on `mtools` being installed at
// run time.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
// so the Makefile aggregator picks it up.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "../core/log_capture.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // mkstemp / write / close (POSIX)
#include "../row_id.h"
#include "../test_sd_image.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

void skip(const char* id, const char* reason) {
    report_row_id(id);
    ++g_skip;
    g_skipped.push_back({id, reason});
}

// Resolve the canonical SD image path. Default is the image jnext downloads
// and caches itself (and which the regression suite's sdcard-provision row
// ensures exists) — not a roms/ fixture, which a fresh clone never has.
// JNEXT_TEST_SD_IMAGE overrides it for out-of-tree runs.
std::string canonical_image() {
    return test_sd_image_path();
}

bool file_exists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return static_cast<bool>(f);
}

// Format a few leading bytes as hex for failure messages.
std::string hex_prefix(const std::vector<uint8_t>& v, size_t n) {
    std::string s;
    n = std::min(n, v.size());
    char buf[8];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof(buf), "%02X ", v[i]);
        s += buf;
    }
    if (!s.empty()) s.pop_back();
    return s;
}

// Reference signatures for SD-EXT-01/03/04/05 — captured offline via
// mtools from the canonical fixture (see header comment).
constexpr uint8_t kEnNxtmmcMagic[4] = { 0xF3, 0xC3, 0x6A, 0x00 };
constexpr uint8_t kEnNextMfMagic[4] = { 0xF5, 0x3E, 0x02, 0xCD };
constexpr uint8_t k48RomMagic[4]    = { 0xF3, 0xAF, 0x11, 0xFF };
constexpr uint8_t kTbblueFwMagic[4] = { 0x00, 0x00, 0x40, 0x00 };

bool first4_eq(const std::vector<uint8_t>& v, const uint8_t (&ref)[4]) {
    if (v.size() < 4) return false;
    return std::memcmp(v.data(), ref, 4) == 0;
}


// ---------------------------------------------------------------------------
// GH #556 fixtures: FAT12/16/32 cards built at run time.
// Oracle: tbblue.fw FatFs (ff.c find_volume :3041-3066, check_fs :2955-2972,
// FAT sub-type :3140-3146, fixed FAT12/16 root :3163).
// ---------------------------------------------------------------------------

std::vector<std::string> g_scratch;

std::string scratch_path(const char* name) {
    const char* base = std::getenv("TMPDIR");
    const std::string dir = (base && *base) ? base : "/tmp";
    std::string p = dir + "/jnext_sdext_" + std::to_string(::getpid()) + "_" + name + ".img";
    g_scratch.push_back(p);
    return p;
}

void cleanup_scratch() {
    for (const std::string& p : g_scratch) std::remove(p.c_str());
}

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }

std::vector<uint8_t> pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 131 + (i >> 8) * 17 + seed);
    return v;
}

struct MbrEntry { uint8_t type; uint32_t lba; };

// Write a fresh MBR with the given entries (slots 0..n-1) into the image.
bool write_mbr_entries(const std::string& path, const std::vector<MbrEntry>& es) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    uint8_t mbr[512] = {0};
    for (size_t i = 0; i < es.size() && i < 4; ++i) {
        uint8_t* pe = mbr + 0x1BE + i * 16;
        pe[4] = es[i].type;
        put32(pe + 8, es[i].lba);
        put32(pe + 12, 1000);
    }
    mbr[510] = 0x55; mbr[511] = 0xAA;
    f.seekp(0);
    f.write(reinterpret_cast<const char*>(mbr), 512);
    return f.good();
}

bool poke_byte(const std::string& path, uint64_t off, uint8_t v) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    f.seekp(static_cast<std::streamoff>(off));
    f.write(reinterpret_cast<const char*>(&v), 1);
    return f.good();
}

bool fs_write(const char* path, const std::vector<uint8_t>& data) {
    FIL fp{};
    if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    UINT bw = 0;
    const bool ok = data.empty() ||
        (f_write(&fp, data.data(), static_cast<UINT>(data.size()), &bw) == FR_OK && bw == data.size());
    return f_close(&fp) == FR_OK && ok;
}

// Format a volume of `vol_sectors` at `vol_lba` (0 = partition-less card) with
// the firmware's own FatFs library and populate it. `fmt` is FM_FAT (FatFs picks
// FAT12 or FAT16 by cluster count) or FM_FAT32. Returns the FatFs fs_type
// (FS_FAT12/16/32) or 0, so a row can assert the fixture is the format it claims.
int make_fatfs_card(const std::string& path, uint32_t vol_lba, uint32_t vol_sectors,
                    BYTE fmt, uint32_t au, uint32_t n_root,
                    const std::function<bool()>& populate) {
    {
        std::ofstream c(path, std::ios::binary | std::ios::trunc);
        if (!c) return 0;
        c.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(vol_lba + vol_sectors) * 512 - 1));
        const char z = 0;
        c.write(&z, 1);
        if (!c.good()) return 0;
    }
    std::string err;
    if (!fatfs_glue::attach(0, path, vol_lba, vol_sectors, err)) return 0;
    struct Detacher { ~Detacher() { fatfs_glue::detach(0); } } detacher;
    MKFS_PARM parm{};
    parm.fmt = static_cast<BYTE>(fmt | FM_SFD);
    parm.n_fat = 2;
    parm.align = 0;
    parm.n_root = n_root;
    parm.au_size = au;
    std::vector<uint8_t> work(64 * 1024);
    if (f_mkfs("0:", &parm, work.data(), static_cast<UINT>(work.size())) != FR_OK) return 0;
    FATFS fsobj{};
    if (f_mount(&fsobj, "0:", 1) != FR_OK) return 0;
    const bool ok = populate();
    const int type = ok ? fsobj.fs_type : 0;
    f_syncvol("0:");
    f_mount(nullptr, "0:", 0);
    return type;
}

bool populate_rom48(const std::vector<uint8_t>& rom) {
    return f_mkdir("0:/MACHINES") == FR_OK && f_mkdir("0:/MACHINES/NEXT") == FR_OK &&
           fs_write("0:/MACHINES/NEXT/48.rom", rom);
}

constexpr uint32_t kVolSectors = 8000;   // 4 MB: FAT16 at 512-byte clusters
constexpr uint32_t kMbrLba     = 2048;

// A FAT file of 40 clusters, its chain deliberately fragmented by writing a
// decoy file alternately (FatFs hands out the next free cluster each time).
bool populate_fragmented(const std::vector<uint8_t>& data, const char* dir, const char* file) {
    if (f_mkdir("0:/A") != FR_OK || f_mkdir(dir) != FR_OK) return false;
    const std::string path = std::string(dir) + "/" + file;
    const std::string decoy = std::string(dir) + "/DECOY.BIN";
    FIL fa{}, fb{};
    if (f_open(&fa, path.c_str(), FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    if (f_open(&fb, decoy.c_str(), FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    bool ok = true;
    for (size_t off = 0; off < data.size() && ok; off += 512) {
        UINT bw = 0;
        ok = f_write(&fa, data.data() + off, 512, &bw) == FR_OK && bw == 512;
        const uint8_t junk[512] = {0xEE};
        ok = ok && f_write(&fb, junk, 512, &bw) == FR_OK && bw == 512;
    }
    return f_close(&fa) == FR_OK && f_close(&fb) == FR_OK && ok;
}

// A hand-built partition-less FAT12/16 volume with exactly `nclst` clusters
// (1 sector/cluster, 1 FAT, a one-sector root of 16 entries), holding
// /TEST.BIN = cluster 2 -> 3 -> EOC (1024 bytes, 0xA1 then 0xB2), the FAT
// entries encoded the way the firmware classifies the volume (ff.c:3145-3146).
// `expect_bits` receives 12 or 16. Used for the cluster-count boundaries.
// Options: `hi_junk` puts 0x7F7F in the entry's bytes 20-21 (DIR_FstClusHI, which
// FAT12/16 do not use, ff.c ld_clust :1624-1636); `cycle_dir` makes /SUB a
// directory whose chain is 2 -> 3 -> 2 with no terminator and no match;
// `fatsz_override` makes BPB_FATSz16 that large (a FAT far bigger than the volume).
// `tail_sectors` makes the FILE longer than the volume (junk past its end), so the
// image-size cap does not equal the BPB's cluster count.
struct HandOpts { bool hi_junk = false; bool cycle_dir = false; uint32_t fatsz_override = 0; uint32_t tail_sectors = 0; };

bool make_hand_fat(const std::string& path, uint32_t nclst, int& expect_bits,
                   std::vector<uint8_t>& expect_data, const HandOpts& o = {}) {
    const int bits = nclst <= 0xFF5u ? 12 : 16;
    expect_bits = bits;
    const uint32_t entries = nclst + 2;
    const uint32_t fat_bytes = bits == 12 ? (entries * 3 + 1) / 2 : entries * 2;
    const uint32_t fatsz = o.fatsz_override ? o.fatsz_override : (fat_bytes + 511) / 512;
    const uint32_t root_lba = 1 + fatsz;
    const uint32_t data_lba = root_lba + 1;
    const uint32_t tsect = data_lba + nclst;
    std::vector<uint8_t> img(static_cast<size_t>(data_lba + 2) * 512, 0);
    uint8_t* b = img.data();
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    put16(b + 11, 512);
    b[13] = 1;
    put16(b + 14, 1);
    b[16] = 1;
    put16(b + 17, 16);
    if (tsect < 0x10000) put16(b + 19, static_cast<uint16_t>(tsect)); else put32(b + 32, tsect);
    put16(b + 22, static_cast<uint16_t>(fatsz));
    std::memcpy(b + 0x36, bits == 12 ? "FAT12   " : "FAT16   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t* fat = img.data() + 512;
    auto set = [&](uint32_t n, uint32_t v) {
        if (bits == 16) { put16(fat + n * 2, static_cast<uint16_t>(v)); return; }
        uint8_t* q = fat + n + n / 2;
        if (n & 1) { q[0] = static_cast<uint8_t>((q[0] & 0x0F) | ((v & 0x0F) << 4)); q[1] = static_cast<uint8_t>(v >> 4); }
        else       { q[0] = static_cast<uint8_t>(v); q[1] = static_cast<uint8_t>((q[1] & 0xF0) | ((v >> 8) & 0x0F)); }
    };
    const uint32_t eoc = bits == 12 ? 0xFFF : 0xFFFF;
    set(0, bits == 12 ? 0xFF8 : 0xFFF8);
    set(1, eoc);
    if (o.cycle_dir) { set(2, 3); set(3, 2); }
    else             { set(2, 3); set(3, eoc); }
    uint8_t* e = img.data() + static_cast<size_t>(root_lba) * 512;
    std::memcpy(e, "TEST    BIN", 11);
    e[11] = 0x20;
    put16(e + 26, 2);
    put32(e + 28, 1024);
    if (o.hi_junk) put16(e + 20, 0x7F7F);
    if (o.cycle_dir) {
        std::memcpy(e, "SUB        ", 11);
        e[11] = 0x10;
        for (uint32_t c = 0; c < 2; ++c)
            for (int slot = 0; slot < 16; ++slot) {
                uint8_t* v = img.data() + static_cast<size_t>(data_lba + c) * 512 + slot * 32;
                v[0] = 'V';
                v[11] = 0x08;   // volume-label slot: skipped, no 0x00 terminator
            }
    } else {
        std::memset(img.data() + static_cast<size_t>(data_lba) * 512, 0xA1, 512);
        std::memset(img.data() + static_cast<size_t>(data_lba + 1) * 512, 0xB2, 512);
    }
    expect_data.assign(1024, 0xA1);
    std::fill(expect_data.begin() + 512, expect_data.end(), 0xB2);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    // The file is as long as the volume the BPB describes (sparse), so a chain walk's
    // image-size cap equals the BPB's own cluster count.
    f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(tsect + o.tail_sectors) * 512 - 1));
    const char z = 0;
    f.write(&z, 1);
    return f.good();
}

// A hand-built partition-less FAT32 volume (1 sector/cluster, FATSz32 sectors of FAT,
// root = cluster 2 -> 3 -> 2, no terminator, no match) whose BPB claims TotSec32 =
// 0xFFFFFFFF while the FILE holds only the system area and 4 data clusters.
bool make_hostile_fat32(const std::string& path, uint32_t fatsz) {
    const uint32_t data_lba = 1 + fatsz;
    std::vector<uint8_t> img(static_cast<size_t>(data_lba + 4) * 512, 0);
    uint8_t* b = img.data();
    b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
    put16(b + 11, 512);
    b[13] = 1;
    put16(b + 14, 1);
    b[16] = 1;
    put32(b + 32, 0xFFFFFFFFu);
    put32(b + 36, fatsz);
    put32(b + 44, 2);
    std::memcpy(b + 0x52, "FAT32   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    uint8_t* fat = img.data() + 512;
    put32(fat + 2 * 4, 3);
    put32(fat + 3 * 4, 2);
    for (uint32_t c = 0; c < 2; ++c)
        for (int slot = 0; slot < 16; ++slot) {
            uint8_t* v = img.data() + static_cast<size_t>(data_lba + c) * 512 + slot * 32;
            v[0] = 'V'; v[11] = 0x08;   // volume-label slot: skipped, no terminator
        }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
    return f.good();
}

bool extracts_eq(const std::string& img, const char* path, const std::vector<uint8_t>& want) {
    std::vector<uint8_t> out;
    return extract_sd_rom(img, path, out) && out == want;
}

} // namespace

int main() {
    const std::string img = canonical_image();
    if (!file_exists(img)) {
        // A missing fixture is a FATAL harness fault, not a skip.
        //
        // This used to emit 8 hardcoded SKIP rows and exit 0. The suite really
        // has 26 rows, so the other 18 were never announced at all — they did
        // not become skips, they ceased to exist, and the headline count went
        // from 4369/0/38 to 4334/0/46 with nothing failing and nothing said.
        // That is exactly how an unprovisioned worktree produced a smaller,
        // still-green-looking number (Task 37).
        //
        // Exit non-zero with no summary line: the harness reports the suite as
        // FAIL, and no count can absorb it silently.
        std::fprintf(stderr,
            "\nFATAL: SD image fixture missing: %s\n"
            "  This suite cannot run without it, and will NOT pretend to.\n"
            "  Provision it with: jnext --headless --sdcard-download-confirm\n"
            "  Or point JNEXT_TEST_SD_IMAGE at the canonical 1 GB image.\n\n",
            img.c_str());
        return 1;
    }

    // ------------------------------------------------------------------
    // SD-EXT-01 — happy path: enNxtmmc.rom (8192 B), F3 C3 6A 00 prefix.
    // ------------------------------------------------------------------
    std::vector<uint8_t> ennxt;
    {
        size_t n = 0;
        const bool ok = extract_sd_rom(img, "/MACHINES/NEXT/enNxtmmc.rom", ennxt, &n);
        check("SD-EXT-01", "extract enNxtmmc.rom returns true", ok);
        check("SD-EXT-01b", "enNxtmmc.rom size is 8192", ennxt.size() == 8192,
              "size=" + std::to_string(ennxt.size()));
        check("SD-EXT-01c", "bytes_read_out matches buffer size", n == ennxt.size(),
              "out=" + std::to_string(n));
        check("SD-EXT-01d", "enNxtmmc.rom first 4 bytes match canonical magic",
              first4_eq(ennxt, kEnNxtmmcMagic),
              "got=" + hex_prefix(ennxt, 4));
    }

    // ------------------------------------------------------------------
    // SD-EXT-02 — case insensitivity: lowercase 'machines/next' must
    // resolve to the same SFN entry as SD-EXT-01.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        const bool ok = extract_sd_rom(img, "/machines/next/enNxtmmc.rom", v);
        check("SD-EXT-02", "lowercase path components return true", ok);
        check("SD-EXT-02b", "lowercase path yields identical bytes",
              ok && v == ennxt,
              "size=" + std::to_string(v.size()));
    }

    // ------------------------------------------------------------------
    // SD-EXT-03 — discriminative: 48.rom (16384 B), F3 AF 11 FF prefix.
    // ------------------------------------------------------------------
    std::vector<uint8_t> v48;
    {
        const bool ok = extract_sd_rom(img, "/MACHINES/NEXT/48.rom", v48);
        check("SD-EXT-03", "extract 48.rom returns true", ok);
        check("SD-EXT-03b", "48.rom size is 16384", v48.size() == 16384,
              "size=" + std::to_string(v48.size()));
        check("SD-EXT-03c", "48.rom first 4 bytes match canonical magic",
              first4_eq(v48, k48RomMagic),
              "got=" + hex_prefix(v48, 4));
        check("SD-EXT-03d", "48.rom bytes differ from enNxtmmc.rom",
              !ennxt.empty() && !v48.empty() && v48 != ennxt);
    }

    // ------------------------------------------------------------------
    // SD-EXT-04 — discriminative: enNextMf.rom (8192 B), F5 3E 02 CD prefix.
    // Same size as SD-EXT-01, distinct content.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        const bool ok = extract_sd_rom(img, "/MACHINES/NEXT/enNextMf.rom", v);
        check("SD-EXT-04", "extract enNextMf.rom returns true", ok);
        check("SD-EXT-04b", "enNextMf.rom size is 8192", v.size() == 8192,
              "size=" + std::to_string(v.size()));
        check("SD-EXT-04c", "enNextMf.rom first 4 bytes match canonical magic",
              first4_eq(v, kEnNextMfMagic),
              "got=" + hex_prefix(v, 4));
        check("SD-EXT-04d", "enNextMf.rom content differs from enNxtmmc.rom",
              !v.empty() && !ennxt.empty() && v != ennxt);
    }

    // ------------------------------------------------------------------
    // SD-EXT-05 — root-level file: TBBLUE.FW (635904 B), starts 00 00 40 00.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        size_t n = 0;
        const bool ok = extract_sd_rom(img, "/TBBLUE.FW", v, &n);
        check("SD-EXT-05", "extract /TBBLUE.FW returns true", ok);
        check("SD-EXT-05b", "TBBLUE.FW size is 635904", v.size() == 635904,
              "size=" + std::to_string(v.size()));
        check("SD-EXT-05c", "TBBLUE.FW bytes_read_out matches", n == v.size(),
              "out=" + std::to_string(n));
        check("SD-EXT-05d", "TBBLUE.FW first 4 bytes match canonical magic",
              first4_eq(v, kTbblueFwMagic),
              "got=" + hex_prefix(v, 4));
    }

    // ------------------------------------------------------------------
    // SD-EXT-06 — not found: returns false, out remains empty.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        // Pre-load v with junk to verify the API clears it on failure.
        v.assign(123, 0xAA);
        const bool ok = extract_sd_rom(img, "/MACHINES/NEXT/nonexistent.rom", v);
        check("SD-EXT-06", "missing file returns false", !ok);
        check("SD-EXT-06b", "missing file leaves out empty", v.empty(),
              "size=" + std::to_string(v.size()));
    }

    // ------------------------------------------------------------------
    // SD-EXT-07 — wrong image: a tiny 16-byte file that has no MBR
    // signature must be rejected without crashing.
    // ------------------------------------------------------------------
    {
        char tmpl[] = "/tmp/jnext-sd-rom-extractor-bogus-XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) {
            skip("SD-EXT-07", "mkstemp failed; cannot construct bogus image");
            skip("SD-EXT-07b", "mkstemp failed; cannot construct bogus image");
        } else {
            const uint8_t junk[16] = {
                0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F
            };
            (void)write(fd, junk, 16);
            close(fd);
            std::vector<uint8_t> v;
            const bool ok = extract_sd_rom(tmpl, "/anything.rom", v);
            check("SD-EXT-07", "non-FAT image returns false", !ok);
            check("SD-EXT-07b", "non-FAT image leaves out empty", v.empty());
            std::remove(tmpl);
        }
    }

    // ------------------------------------------------------------------
    // SD-EXT-08 — leading-slash optional. Same file as SD-EXT-01.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        const bool ok = extract_sd_rom(img, "MACHINES/NEXT/enNxtmmc.rom", v);
        check("SD-EXT-08", "no-leading-slash path returns true", ok);
        check("SD-EXT-08b", "no-leading-slash bytes match SD-EXT-01",
              ok && v == ennxt);
    }

    // ------------------------------------------------------------------
    // V16-DIVMMC-02 (SD-EXT-09) — pass-16 verify-audit, 2026-05-10.
    // Defensive bound on FAT32 directory cluster-chain walk in
    // `find_in_directory` (sd_rom_extractor.cpp). Pre-fix the directory
    // walk only terminated on EOC / read failure / 0x00 end-of-directory
    // marker; a CYCLIC directory FAT chain (cluster A → cluster B →
    // cluster A) had no exit and looped indefinitely. The file-data read
    // path already had a `max_chain_len` bound — directory walk did not.
    //
    // Discriminative shape: build a synthetic FAT32 image with a minimal
    // valid MBR + BPB but a deliberately cyclic root-directory chain:
    //   cluster 2 → cluster 3 → cluster 2 (cycle)
    // None of the directory entries match the lookup key, and there is
    // no 0x00 end-of-directory marker (every entry is a "fake" volume-
    // label slot that matches no SFN). Pre-fix: extract_sd_rom hangs
    // forever. Post-fix: the bound `max_chain_len = fat_size_sectors *
    // bytes_per_sector / 4` (= total FAT entries in the partition) trips
    // and the function returns false with a logged error.
    //
    // The check uses a simple wall-clock-style guard: spin a thread doing
    // the call, join with a generous timeout. If the function hasn't
    // returned within the timeout, treat as FAIL (a real hang). Post-fix
    // the call returns in microseconds.
    {
        char tmpl[] = "/tmp/jnext-sd-rom-extractor-cyclic-XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) {
            skip("SD-EXT-09",
                 "mkstemp failed; cannot construct cyclic FAT image");
            skip("SD-EXT-09b",
                 "mkstemp failed; cannot construct cyclic FAT image");
        } else {
            close(fd);

            // Build the image in a vector then write it once. Layout:
            //   sector 0     : MBR (boot sig + 1 partition entry → LBA 1)
            //   sector 1     : BPB (FAT32, 1 sector/cluster, 1 reserved
            //                       sector + 1 FAT sector + 4 data clusters)
            //   sector 2     : FAT (entries 0,1,2,3 — entry[2]=3 entry[3]=2)
            //   sector 3..6  : data cluster 2..5 (each holds 16 dir entries)
            //
            // Each data cluster is 512 bytes = 16 directory entries × 32 B.
            // Fill every entry with an ATTR_VOLUME_ID slot (skipped by
            // find_in_directory) so no end-of-dir marker is encountered
            // and no SFN match is found — guaranteeing the loop must
            // walk the full chain.
            constexpr uint32_t kSectorSize = 512;
            constexpr uint32_t kSectorsPerCluster = 1;
            constexpr uint32_t kReservedSectors = 1;
            constexpr uint32_t kNumFats = 1;
            constexpr uint32_t kFatSizeSectors = 1;
            [[maybe_unused]] constexpr uint32_t kPartLba = 1;
            // Total sectors in the synthetic image:
            //   1 (MBR) + 1 (BPB) + 1 (FAT) + 4 (data) = 7
            constexpr uint32_t kTotalSectors = 7;
            std::vector<uint8_t> img_buf(kTotalSectors * kSectorSize, 0x00);

            // MBR (sector 0).
            uint8_t* mbr = img_buf.data();
            mbr[510] = 0x55;
            mbr[511] = 0xAA;
            // Partition table entry 0 at offset 0x1BE.
            uint8_t* pe = mbr + 0x1BE;
            pe[0]  = 0x00;       // boot indicator
            pe[4]  = 0x0C;       // type = FAT32-LBA
            // LBA start (offset 8) = 1.
            pe[8]  = 0x01;
            pe[9]  = 0x00;
            pe[10] = 0x00;
            pe[11] = 0x00;

            // BPB (sector 1).
            uint8_t* bpb = img_buf.data() + 1 * kSectorSize;
            bpb[11] = static_cast<uint8_t>(kSectorSize & 0xFF);
            bpb[12] = static_cast<uint8_t>((kSectorSize >> 8) & 0xFF);
            bpb[13] = kSectorsPerCluster;
            bpb[14] = static_cast<uint8_t>(kReservedSectors & 0xFF);
            bpb[15] = static_cast<uint8_t>((kReservedSectors >> 8) & 0xFF);
            bpb[16] = kNumFats;
            // FATSz32 at offset 36.
            bpb[36] = static_cast<uint8_t>(kFatSizeSectors & 0xFF);
            // Root cluster at offset 44 = 2.
            bpb[44] = 2;

            // FAT (sector 2). 4-byte entries; we have room for
            // bytes_per_sector / 4 = 128 entries. Set entry[2]=3, entry[3]=2
            // → cycle. Other entries default 0 (free).
            uint8_t* fat = img_buf.data() + 2 * kSectorSize;
            // entry[2] @ offset 8 = 3
            fat[8] = 0x03;
            // entry[3] @ offset 12 = 2
            fat[12] = 0x02;

            // Data clusters 2 + 3 (sectors 3 + 4) — both filled with
            // ATTR_VOLUME_ID entries that find_in_directory will skip.
            // Set first byte to 'V' (any byte ≠ 0x00 / 0xE5 ensures the
            // entry is "live"); attribute byte at offset 11 = 0x08 =
            // ATTR_VOLUME_ID. Filling all 16 entries × 2 clusters = 32
            // entries; none match the lookup key; no end-of-dir 0x00.
            for (uint32_t cluster_lba_offset = 0; cluster_lba_offset < 2;
                 ++cluster_lba_offset) {
                uint8_t* dat = img_buf.data() +
                    (3 + cluster_lba_offset) * kSectorSize;
                for (int slot = 0; slot < 16; ++slot) {
                    uint8_t* e = dat + slot * 32;
                    e[0]  = 'V';
                    e[11] = 0x08;  // ATTR_VOLUME_ID — skipped by the loop
                }
            }

            // Write the synthetic image to disk.
            std::ofstream of(tmpl, std::ios::binary);
            of.write(reinterpret_cast<const char*>(img_buf.data()),
                     img_buf.size());
            of.close();

            // Run the call in-process. The fix bounds the chain by
            // `fat_size_sectors * bytes_per_sector / 4` = 128 — far less
            // than any pre-fix infinite-loop iteration would have
            // executed before we'd notice. We don't need a separate
            // thread; if the fix is in place the call returns immediately.
            std::vector<uint8_t> out;
            const bool ok = extract_sd_rom(tmpl, "/anything.rom", out);
            check("SD-EXT-09",
                  "cyclic FAT32 directory chain (cluster 2 ↔ 3) returns "
                  "false instead of looping forever — V16-DIVMMC-02 "
                  "find_in_directory bound",
                  !ok);
            check("SD-EXT-09b",
                  "cyclic-chain failure leaves out empty",
                  out.empty(),
                  "size=" + std::to_string(out.size()));
            std::remove(tmpl);
        }
    }


    // ─────────────────────────────────────────────────────────────────────
    // SD-EXT-10..21 — FAT12/16/32 cards and partition-less cards (GH #556)
    // ─────────────────────────────────────────────────────────────────────
    const std::vector<uint8_t> rom48 = pattern(16384, 3);
    const char* const kRom48 = "/MACHINES/NEXT/48.rom";
    {
        const std::string p = scratch_path("fat16-06");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x06, kMbrLba}});
        check("SD-EXT-10",
              "FAT16 card, MBR type 0x06: 48.rom extracts byte-exact (ff.c:3048-3063)",
              t == FS_FAT16 && mbr && extracts_eq(p, kRom48, rom48), "fs_type=" + std::to_string(t));
        const bool mbr2 = write_mbr_entries(p, {{0x0E, kMbrLba}});
        check("SD-EXT-10b",
              "FAT16 card, MBR type 0x0E: extracts (any non-zero type, ff.c:3050)",
              mbr2 && extracts_eq(p, kRom48, rom48));
    }
    {
        const std::string p = scratch_path("fat16-sfd");
        const int t = make_fatfs_card(p, 0, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        check("SD-EXT-11",
              "FAT16 card with no partition table (boot record at sector 0) extracts (ff.c:3043)",
              t == FS_FAT16 && extracts_eq(p, kRom48, rom48), "fs_type=" + std::to_string(t));
    }
    {
        const std::string p = scratch_path("fat32-sfd");
        const int t = make_fatfs_card(p, 0, 70000, FM_FAT32, 512, 0,
                                      [&] { return populate_rom48(rom48); });
        check("SD-EXT-12",
              "FAT32 card with no partition table extracts (ff.c:3043)",
              t == FS_FAT32 && extracts_eq(p, kRom48, rom48), "fs_type=" + std::to_string(t));
    }
    {
        const std::string p = scratch_path("entry1");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        // Entry 0: type 0x83 over a zero sector (no boot record); entry 1: the FAT16 volume.
        const bool mbr = write_mbr_entries(p, {{0x83, 1}, {0x06, kMbrLba}});
        check("SD-EXT-13",
              "entry 0 type 0x83 over a non-FAT sector, entry 1 the FAT16 volume: found in entry 1 (ff.c:3060-3063)",
              t == FS_FAT16 && mbr && extracts_eq(p, kRom48, rom48));
    }
    {
        const std::string p = scratch_path("type0");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x00, kMbrLba}});
        std::vector<uint8_t> out;
        const bool ok = extract_sd_rom(p, kRom48, out);
        check("SD-EXT-14",
              "an entry of type 0 is skipped even over a valid FAT16 boot record (ff.c:3050)",
              t == FS_FAT16 && mbr && !ok && out.empty());
    }
    {
        const std::string p = scratch_path("notfat");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x83, kMbrLba}});
        // Destroy the "FAT" string at 0x36 so check_fs gives "valid BS, not FAT" (ff.c:2965-2972).
        const bool poked = poke_byte(p, static_cast<uint64_t>(kMbrLba) * 512 + 0x36, 'X');
        std::vector<uint8_t> out;
        const bool ok = extract_sd_rom(p, kRom48, out);
        check("SD-EXT-15",
              "a non-zero type over a sector failing check_fs (no \"FAT\" string) is no volume (ff.c:2965-2972)",
              t == FS_FAT16 && mbr && poked && !ok && out.empty());
    }
    {
        const std::string p = scratch_path("frag16");
        const std::vector<uint8_t> data = pattern(40 * 512, 9);
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_fragmented(data, "0:/A/B", "FRAG.BIN"); });
        const bool mbr = write_mbr_entries(p, {{0x06, kMbrLba}});
        check("SD-EXT-16",
              "FAT16: a 40-cluster fragmented file in a 2nd-level directory extracts byte-exact (16-bit FAT walk, fixed root, ff.c:3163)",
              t == FS_FAT16 && mbr && extracts_eq(p, "/A/B/FRAG.BIN", data));
    }
    {
        const std::string p = scratch_path("frag12");
        const std::vector<uint8_t> data = pattern(40 * 512, 21);
        const int t = make_fatfs_card(p, kMbrLba, 2000, FM_FAT, 512, 512,
                                      [&] { return populate_fragmented(data, "0:/A/B", "FRAG.BIN"); });
        const bool mbr = write_mbr_entries(p, {{0x01, kMbrLba}});
        check("SD-EXT-17",
              "FAT12: a fragmented 40-cluster file (odd and even FAT entries) extracts byte-exact (ff.c:3146, 12-bit packing)",
              t == FS_FAT12 && mbr && extracts_eq(p, "/A/B/FRAG.BIN", data), "fs_type=" + std::to_string(t));
    }
    {
        const std::string p = scratch_path("root16");
        const std::vector<uint8_t> data = pattern(700, 5);
        const int t = make_fatfs_card(p, 0, kVolSectors, FM_FAT, 512, 512, [&] {
            for (int i = 0; i < 40; ++i) {
                char name[32];
                std::snprintf(name, sizeof(name), "0:/F%02d.BIN", i);
                if (!fs_write(name, pattern(1, static_cast<uint8_t>(i)))) return false;
            }
            return fs_write("0:/TARGET.ROM", data);
        });
        check("SD-EXT-18",
              "FAT16 root with more entries than fit one sector: a target in the third root sector is found (root bounded by RootEntCnt, not a cluster)",
              t == FS_FAT16 && extracts_eq(p, "/TARGET.ROM", data));
    }
    {
        // Hand-built volumes at the cluster-count boundaries (ff.c:3144-3156).
        std::vector<uint8_t> want;
        int bits = 0;
        const std::string p1 = scratch_path("n4085"), p2 = scratch_path("n4086");
        const bool m1 = make_hand_fat(p1, 0xFF5, bits, want);
        const bool ok1 = m1 && bits == 12 && extracts_eq(p1, "/TEST.BIN", want);
        const bool m2 = make_hand_fat(p2, 0xFF6, bits, want);
        const bool ok2 = m2 && bits == 16 && extracts_eq(p2, "/TEST.BIN", want);
        check("SD-EXT-17b",
              "4085 clusters is FAT12 and 4086 is FAT16: both extract (ff.c:3145-3146 thresholds)",
              ok1 && ok2, "ok1=" + std::to_string(ok1) + " ok2=" + std::to_string(ok2));
    }
    {
        std::vector<uint8_t> want;
        int bits = 0;
        const std::string p = scratch_path("n70000");
        const bool made = make_hand_fat(p, 70000, bits, want);
        std::vector<uint8_t> out;
        const bool ok = extract_sd_rom(p, "/TEST.BIN", out);
        check("SD-EXT-19",
              "a FAT16-layout BPB with 70000 clusters is refused: the firmware calls it FAT32 and rejects it (ff.c:3144-3156)",
              made && !ok && out.empty());
    }
    {
        std::vector<uint8_t> want;
        int bits = 0;
        const std::string p1 = scratch_path("nFFF5"), p2 = scratch_path("nFFF6");
        const bool m1 = make_hand_fat(p1, 0xFFF5, bits, want);
        const bool ok1 = m1 && bits == 16 && extracts_eq(p1, "/TEST.BIN", want);
        const bool m2 = make_hand_fat(p2, 0xFFF6, bits, want);
        std::vector<uint8_t> out;
        const bool ok2 = m2 && !extract_sd_rom(p2, "/TEST.BIN", out);
        check("SD-EXT-19b",
              "0xFFF5 clusters is the largest FAT16 and extracts; 0xFFF6 is refused (ff.c:3144-3156)",
              ok1 && ok2, "ok1=" + std::to_string(ok1) + " ok2=" + std::to_string(ok2));
    }
    {
        // The legacy arm (D2): a type-0x0C entry over a boot sector that FAILS the
        // firmware's check_fs (jump byte zeroed) is still taken, as it was before
        // GH #556 (SD-EXT-09's fixtures carry no jump byte either).
        const std::string p = scratch_path("legacy0c");
        const int t = make_fatfs_card(p, kMbrLba, 70000, FM_FAT32, 512, 0,
                                      [&] { return populate_rom48(rom48); });
        // Slot 0 is a 0x0C entry at LBA 0 (no volume there): pass 2 must skip it (lba != 0).
        const bool mbr = write_mbr_entries(p, {{0x0C, 0}, {0x0C, kMbrLba}});
        const bool poked = poke_byte(p, static_cast<uint64_t>(kMbrLba) * 512, 0x00);
        check("SD-EXT-20",
              "a type-0x0C entry over a FAT32 boot sector without a jump byte still extracts (legacy 0x0B/0x0C arm kept)",
              t == FS_FAT32 && mbr && poked && extracts_eq(p, kRom48, rom48));
    }
    {
        // 48K machine initialised from a FAT16 card: the host extractor feeds the ROM.
        const std::string p = scratch_path("m48k");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x06, kMbrLba}});
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        cfg.sd_card_image = p;
        cfg.sd_card_readonly = true;
        const bool init_ok = t == FS_FAT16 && mbr && emu->init(cfg);
        size_t bad = 0;
        if (init_ok)
            for (uint32_t a = 0; a < 0x4000; ++a)
                if (emu->mmu().peek(static_cast<uint16_t>(a)) != rom48[a]) ++bad;
        check("SD-EXT-21",
              "a 48K machine initialised with a FAT16 card runs the card's 48.rom (bytes 0x0000..0x3FFF equal)",
              init_ok && bad == 0, "init=" + std::to_string(init_ok) + " bad=" + std::to_string(bad));
    }
    {
        // Review round 1: the firmware's scan comes FIRST, the 0x0B/0x0C leniency
        // only when it finds nothing. A stale 0x0C entry over non-FAT sectors in
        // slot 0 must not hide the real volume in slot 1 (ff.c:3047-3063).
        const std::string p = scratch_path("stale0c");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x0C, 1}, {0x06, kMbrLba}});
        check("SD-EXT-22",
              "entry 0 type 0x0C at LBA 1 over non-FAT sectors, entry 1 type 0x06 FAT16: the volume in entry 1 is found (firmware scan before the legacy arm)",
              t == FS_FAT16 && mbr && extracts_eq(p, kRom48, rom48));
    }
    {
        // check_fs jump-byte rule (ff.c:2965): 0xE9, or 0xEB with 0x90 at byte 2.
        const std::string p = scratch_path("jump");
        const int t = make_fatfs_card(p, kMbrLba, kVolSectors, FM_FAT, 512, 512,
                                      [&] { return populate_rom48(rom48); });
        const bool mbr = write_mbr_entries(p, {{0x06, kMbrLba}});
        const uint64_t vbr = static_cast<uint64_t>(kMbrLba) * 512;
        std::vector<uint8_t> out;
        const bool p0 = poke_byte(p, vbr + 0, 0x00);
        const bool no_jump = !extract_sd_rom(p, kRom48, out);
        check("SD-EXT-23",
              "a non-legacy type over a FAT16 record whose jump byte is 0x00 is no volume (ff.c:2965)",
              t == FS_FAT16 && mbr && p0 && no_jump && out.empty());
        const bool p1 = poke_byte(p, vbr + 0, 0xE9) && poke_byte(p, vbr + 2, 0x00);
        check("SD-EXT-24",
              "jump byte 0xE9 (bytes 1-2 arbitrary) is a FAT boot record: extracts (ff.c:2965)",
              p1 && extracts_eq(p, kRom48, rom48));
        const bool p2 = poke_byte(p, vbr + 0, 0xEB) && poke_byte(p, vbr + 2, 0x00);
        const bool no_eb = !extract_sd_rom(p, kRom48, out);
        check("SD-EXT-25",
              "jump EB xx 00 (no 0x90 at byte 2) is no volume (ff.c:2965)",
              p2 && no_eb && out.empty());
    }
    {
        std::vector<uint8_t> want;
        int bits = 0;
        HandOpts o; o.hi_junk = true;
        const std::string p = scratch_path("hijunk");
        const bool made = make_hand_fat(p, 0xFF6, bits, want, o);
        check("SD-EXT-26",
              "a FAT16 directory entry with non-zero bytes 20-21 (DIR_FstClusHI) extracts byte-exact: the high word is FAT32-only (ff.c ld_clust :1624-1636)",
              made && bits == 16 && extracts_eq(p, "/TEST.BIN", want));
    }
    {
        std::vector<uint8_t> want;
        int bits = 0;
        HandOpts o; o.cycle_dir = true; o.fatsz_override = 65535; o.tail_sectors = 20000;
        const std::string p = scratch_path("cycle16");
        const bool made = make_hand_fat(p, 4100, bits, want, o);
        std::vector<uint8_t> out;
        bool ok = true;
        const std::string log = capture_emulator_log([&] { ok = extract_sd_rom(p, "/SUB/X.BIN", out); });
        const bool bounded = log.find("(4101 > 4100)") != std::string::npos;
        check("SD-EXT-27",
              "a FAT16 card with a cyclic subdirectory chain and a FAT far larger than the volume is refused, the walk stopping at the volume's cluster count: the refusal reads (4101 > 4100), not the FAT's capacity",
              made && !ok && out.empty() && bounded, "bounded=" + std::to_string(bounded));
    }

    {
        // A hostile FAT32 BPB (TotSec32 0xFFFFFFFF, a FAT of 3000 sectors) over a file that
        // holds 4 data clusters: a cyclic root chain is cut at the clusters the FILE holds.
        const std::string p = scratch_path("hostile32");
        const bool made = make_hostile_fat32(p, 3000);
        std::vector<uint8_t> out;
        bool ok = true;
        const std::string log = capture_emulator_log([&] { ok = extract_sd_rom(p, "/X.BIN", out); });
        check("SD-EXT-28",
              "a FAT32 cyclic root chain under an inflated TotSec32 (0xFFFFFFFF) is refused after as many steps as the image file has clusters (5 > 4), not after the BPB's claim",
              made && !ok && out.empty() && log.find("(5 > 4)") != std::string::npos,
              "ok=" + std::to_string(ok));
    }

    cleanup_scratch();
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows:\n");
        for (const auto& s : g_skipped)
            std::printf("  SKIP %s: %s\n", s.id, s.reason);
    }
    return g_fail ? 1 : 0;
}
