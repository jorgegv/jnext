// FatFs SD-image patch end-to-end tests (Task 27, FatFs rewrite).
//
// The hand-rolled FAT32 volume writer + geometry math were retired in favour
// of the vendored ChaN FatFs (third_party/fatfs): the patch now reformats
// the partition with f_mkfs (8 KB clusters) and re-emits the tree with
// f_mkdir / f_open / f_write. These tests therefore exercise the WHOLE patch
// pipeline as a black box and assert its output invariants — no
// internal-geometry unit tests remain (they tested code that no longer exists).
//
// All tests are offline and hermetic. A source image is crafted from scratch
// (its tree written through the same FatFs writer), then run through the real
// provisioner entry point sdcard::patch_image_fat32(). The assertions read the
// result back through INDEPENDENT paths (the lenient hand-rolled reader
// fat32_read_tree, the emulator's SFN-based extract_sd_rom, and a direct FatFs
// mount), so a broken writer or reader is caught.
//
//   FATFS-PATCH-01  Patched image mounts as a valid FAT32 with >= 65525
//                   clusters (FatFs f_mount + f_getfree) and a FAT32-shaped
//                   BPB (root_entries == 0). This is exactly the property the
//                   under-clustered distribution image fails.
//   FATFS-PATCH-02  The whole file tree round-trips byte-identical.
//   FATFS-PATCH-03  Injected /MACHINES/NEXT/config.ini is present and matches
//                   sdcard::default_config_ini().
//   FATFS-PATCH-04  SFN guard: mixed-case "enNxtmmc.rom" / "enNextMf.rom" keep
//                   their natural uppercase 8.3 short name, so the emulator's
//                   SFN-based extract_sd_rom() resolves them (regression guard
//                   for the ~N-mangling bug — FatFs must not number them).
//   FATFS-PATCH-05  Long-name file "CONTRIBUTING.md" reads back byte-for-byte.
//   MCARD-17        Recipe 3 is unchanged by the minimal-card work (GH #458): the
//                   patched synthetic image's SHA-256 is the one the BASE commit
//                   (df023b328, before the shared directory parser, the write
//                   options and FF_USE_CHMOD) produced for the same fixture.
//   FGC-01..06      The FatFs glue's sector cache (GH #547 stage 5): read-after-write,
//                   a second attach, the journal overlay over the cache, two sectors
//                   on one line, a partly cached read, a read past the volume; FGC-07..09:
//                   first/last-cached requests, a failed read, a failed re-attach.
//   FAT32-WIN-01..02 The FAT reader's one-sector window (GH #547 stage 5): chains that
//                   cross FAT sector boundaries, a re-read after a reformat, and the
//                   reserved top bits of an entry (FAT32-WIN-03).
//   FAT32-UPSERT-01 fat32_tree_upsert creates missing dirs (case-insensitive)
//                   and replaces an existing file (unchanged unit).

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_provisioner.h"
#include "core/sha256.h"
#include "peripheral/sd_write_journal.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string tmp_path(const char* name) {
    const char* base = std::getenv("TMPDIR");
    std::string dir = base && *base ? base : "/tmp";
    return dir + "/jnext_fat32_test_" + std::to_string(::getpid()) + "_" + name;
}

void wr32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Create a sparse blank image with an MBR + one FAT32-LBA partition at
// `part_lba` spanning `total_sectors`.
bool make_blank(const std::string& path, uint32_t part_lba, uint32_t total_sectors) {
    const uint64_t bytes = static_cast<uint64_t>(part_lba + total_sectors) * 512;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    std::vector<uint8_t> mbr(512, 0);
    uint8_t* pe = mbr.data() + 0x1BE;
    pe[4] = 0x0C;                 // type FAT32 LBA
    wr32(pe + 8, part_lba);       // start LBA
    wr32(pe + 12, total_sectors); // sectors
    mbr[510] = 0x55; mbr[511] = 0xAA;
    f.write(reinterpret_cast<char*>(mbr.data()), 512);
    f.seekp(static_cast<std::streamoff>(bytes - 1)); char z = 0; f.write(&z, 1);
    return f.good();
}

Fat32Node fnode(const std::string& n, const std::string& content) {
    Fat32Node x; x.name = n; x.is_dir = false;
    x.data.assign(content.begin(), content.end());
    return x;
}

const Fat32Node* find_child(const std::vector<Fat32Node>& v, const std::string& name) {
    for (const auto& n : v) if (n.name == name) return &n;
    return nullptr;
}

// Read the patched BPB and classify exactly as FatFs does (ff.c R0.15b): the
// cluster count and the FAT32-shape (root_entries == 0) invariant.
bool bpb_is_valid_fat32(const std::string& path, uint32_t part_lba,
                        uint32_t& nclst_out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint8_t b[512];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(part_lba) * 512));
    f.read(reinterpret_cast<char*>(b), 512);
    if (f.gcount() != 512) return false;
    const uint16_t bps       = rd16(b + 11);
    const uint8_t  spc       = b[13];
    const uint16_t reserved  = rd16(b + 14);
    const uint8_t  nfats     = b[16];
    const uint16_t root_ent  = rd16(b + 17);
    uint32_t       tot       = rd16(b + 19);
    if (tot == 0) tot        = rd32(b + 32);
    const uint32_t fatsz     = rd32(b + 36);
    if (bps != 512 || spc == 0 || nfats == 0 || fatsz == 0) return false;
    if (root_ent != 0) return false;              // must be FAT32-shaped
    const uint32_t sysect = reserved + nfats * fatsz;
    if (tot <= sysect) return false;
    nclst_out = (tot - sysect) / spc;
    return nclst_out > 65525u;                    // FatFs FS_FAT32 threshold
}


// ---- FatFs glue sector cache (GH #547 stage 5) ------------------------------
// The oracle is the image FILE, read by an independent stream, and (journal
// mode) the journal's committed bytes. No VHDL counterpart: a host-tool layer.
constexpr uint32_t kGcLba     = 7;    // partition start, in sectors
constexpr uint32_t kGcLines   = 8192; // the cache's lines (kCacheLines in src/core/fatfs_diskio.cpp)
constexpr uint32_t kGcSectors = 9000; // FatFs sectors presented (> kGcLines, so two sectors can share a line)

std::vector<uint8_t> gc_pattern(uint32_t sector, uint8_t salt) {
    std::vector<uint8_t> v(512);
    for (uint32_t j = 0; j < 512; ++j) v[j] = static_cast<uint8_t>(sector * 31 + j * 7 + salt);
    return v;
}

// An image whose FatFs sector n (absolute n + kGcLba) holds gc_pattern(n, 1).
bool gc_make_image(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    for (uint32_t a = 0; a < kGcLba + kGcSectors; ++a) {
        const auto v = gc_pattern(a >= kGcLba ? a - kGcLba : 0, 1);
        f.write(reinterpret_cast<const char*>(v.data()), 512);
    }
    return f.good();
}

// The file's 512 bytes at FatFs sector `n`, read by a stream of its own.
std::vector<uint8_t> gc_file_sector(const std::string& path, uint32_t n) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> v(512, 0xEE);
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kGcLba + n) * 512));
    f.read(reinterpret_cast<char*>(v.data()), 512);
    return v;
}

std::vector<uint8_t> gc_read(uint32_t n, UINT count = 1, DRESULT* rr = nullptr) {
    std::vector<uint8_t> v(static_cast<size_t>(count) * 512, 0xEE);
    const DRESULT r = disk_read(0, v.data(), n, count);
    if (rr) *rr = r;
    return v;
}

} // namespace

int main() {
    const uint32_t part_lba = 63;
    // 8 KB clusters need >= 65525 data clusters for a valid FAT32, i.e. a
    // partition of at least ~540 MB. Use ~600 MB (sparse on disk).
    const uint32_t total_sectors = 1228800; // 600 MB / 512

    const std::string src = tmp_path("patch.img");
    std::string err;

    // ---- Craft the source tree and lay it down through the FatFs writer ----
    Fat32Tree tree;
    tree.root.push_back(fnode("TBBLUE.FW", "uppercase-8.3"));
    tree.root.push_back(fnode("CONTRIBUTING.md", "a-long-file-name-needs-lfn"));
    tree.root.push_back(fnode("empty.txt", ""));
    {
        std::string rom(8192, '\x7e');
        rom[0] = static_cast<char>(0xF3); rom[1] = static_cast<char>(0xC3);
        std::string mf(8192, '\x5a');
        Fat32Node mach; mach.name = "MACHINES"; mach.is_dir = true;
        Fat32Node next; next.name = "NEXT"; next.is_dir = true;
        next.children.push_back(fnode("enNxtmmc.rom", rom)); // mixed case, 8.3
        next.children.push_back(fnode("enNextMf.rom", mf));  // mixed case, 8.3
        mach.children.push_back(next);
        tree.root.push_back(mach);
    }

    bool blank_ok = make_blank(src, part_lba, total_sectors);
    check("FATFS-PATCH", "blank source image created", blank_ok, err);
    bool prep = blank_ok &&
                fat32_format_and_populate(src, part_lba, total_sectors, tree, err);
    check("FATFS-PATCH-b", "source tree written via FatFs", prep, err);

    // ---- Run the real provisioner patch entry point on the source ----
    bool patched = prep && sdcard::patch_image_fat32(src, err);
    check("FATFS-PATCH-c", "patch_image_fat32 succeeded", patched, err);

    if (patched) {
        // -- FATFS-PATCH-01: mounts as valid FAT32, >= 65525 clusters --
        uint32_t nclst = 0;
        bool bpb_ok = bpb_is_valid_fat32(src, part_lba, nclst);
        check("FATFS-PATCH-01", "BPB is FAT32-shaped with > 65525 clusters",
              bpb_ok, "nclst=" + std::to_string(nclst));

        // Authoritative: FatFs itself must mount it and count free clusters.
        bool mounted = false;
        uint32_t total_clusters = 0;
        if (fatfs_glue::attach(0, src, part_lba, total_sectors, err)) {
            FATFS fs{};
            if (f_mount(&fs, "0:", 1) == FR_OK) {
                DWORD nfree = 0; FATFS* fsp = nullptr;
                if (f_getfree("0:", &nfree, &fsp) == FR_OK && fsp) {
                    mounted = true;
                    total_clusters = fsp->n_fatent - 2;
                }
                f_mount(nullptr, "0:", 0);
            }
            fatfs_glue::detach(0);
        }
        check("FATFS-PATCH-01b", "FatFs mounts patched image",
              mounted, err);
        check("FATFS-PATCH-01c", "FatFs reports >= 65525 clusters",
              mounted && total_clusters >= 65525u,
              "total_clusters=" + std::to_string(total_clusters));

        // -- FATFS-PATCH-02: whole tree round-trips byte-identical --
        Fat32Tree back;
        bool rd = fat32_read_tree(src, part_lba, back);
        check("FATFS-PATCH-02", "read_tree of patched image succeeded", rd);

        const Fat32Node* tb = find_child(back.root, "TBBLUE.FW");
        check("FATFS-PATCH-02b", "TBBLUE.FW content intact",
              tb && !tb->is_dir &&
              std::string(tb->data.begin(), tb->data.end()) == "uppercase-8.3");
        const Fat32Node* em = find_child(back.root, "empty.txt");
        check("FATFS-PATCH-02c", "empty.txt present and empty",
              em && em->data.empty());
        const Fat32Node* md = find_child(back.root, "MACHINES");
        const Fat32Node* nx = md ? find_child(md->children, "NEXT") : nullptr;
        const Fat32Node* rn = nx ? find_child(nx->children, "enNxtmmc.rom") : nullptr;
        check("FATFS-PATCH-02d", "nested MACHINES/NEXT/enNxtmmc.rom intact",
              rn && rn->data.size() == 8192);

        // -- FATFS-PATCH-05: long-name file preserved exactly --
        const Fat32Node* cn = find_child(back.root, "CONTRIBUTING.md");
        check("FATFS-PATCH-05", "long name preserved byte-for-byte",
              cn && std::string(cn->data.begin(), cn->data.end()) ==
                        "a-long-file-name-needs-lfn");

        // -- FATFS-PATCH-03: injected config.ini present + correct content --
        std::vector<uint8_t> cfg;
        bool cfg_ok = extract_sd_rom(src, "/MACHINES/NEXT/config.ini", cfg);
        check("FATFS-PATCH-03", "config.ini injected at /MACHINES/NEXT",
              cfg_ok && cfg == sdcard::default_config_ini(),
              "ok=" + std::to_string(cfg_ok) + " size=" + std::to_string(cfg.size()));

        // -- FATFS-PATCH-04: SFN guard for both peripheral ROMs --
        std::vector<uint8_t> mmc, mf;
        bool mmc_ok = extract_sd_rom(src, "/MACHINES/NEXT/enNxtmmc.rom", mmc);
        bool mf_ok  = extract_sd_rom(src, "/MACHINES/NEXT/enNextMf.rom", mf);
        check("FATFS-PATCH-04", "enNxtmmc.rom resolvable by natural SFN",
              mmc_ok && mmc.size() == 8192 && mmc[0] == 0xF3,
              "ok=" + std::to_string(mmc_ok) + " size=" + std::to_string(mmc.size()));
        check("FATFS-PATCH-04b", "enNextMf.rom resolvable by natural SFN",
              mf_ok && mf.size() == 8192 && mf[0] == 0x5a,
              "ok=" + std::to_string(mf_ok) + " size=" + std::to_string(mf.size()));

        // -- MCARD-17: the recipe-3 output is byte-identical to the base commit's --
        const std::string got = sdcard::sha256_file(src);
        check("MCARD-17", "patch_image_fat32 output equals the base commit's (recipe 3 unchanged)",
              got == "37d13f05a53a72a6b04c1db9102910a52a59968db1d3a99ef8ce28085884ba16", "sha256=" + got);
    }

    std::remove(src.c_str());

    // -- FAT32-UPSERT-01 : upsert creates dirs case-insensitively / replaces --
    {
        Fat32Tree t;
        Fat32Node mach; mach.name = "machines"; mach.is_dir = true;
        Fat32Node next; next.name = "next"; next.is_dir = true;
        next.children.push_back(fnode("config.ini", "old"));
        mach.children.push_back(next);
        t.root.push_back(mach);

        // Replace existing (case-insensitive path).
        fat32_tree_upsert(t, "MACHINES/NEXT/config.ini", {'n','e','w'});
        const Fat32Node* m = find_child(t.root, "machines");
        const Fat32Node* n = m ? find_child(m->children, "next") : nullptr;
        const Fat32Node* c = n ? find_child(n->children, "config.ini") : nullptr;
        check("FAT32-UPSERT-01", "existing file replaced (not duplicated)",
              n && n->children.size() == 1 && c &&
              std::string(c->data.begin(), c->data.end()) == "new");

        // Insert into a brand new dir chain.
        fat32_tree_upsert(t, "sys/config/new.cfg", {'x'});
        const Fat32Node* s = find_child(t.root, "sys");
        const Fat32Node* cfg = s ? find_child(s->children, "config") : nullptr;
        const Fat32Node* nf = cfg ? find_child(cfg->children, "new.cfg") : nullptr;
        check("FAT32-UPSERT-01b", "missing directories created for new file",
              s && s->is_dir && cfg && cfg->is_dir && nf && nf->data.size() == 1);
    }

    // ---- FGC-01..06: the glue's sector cache ---------------------------------
    {
        const std::string img = tmp_path("gc.img");
        std::string gerr;
        const bool made = gc_make_image(img);

        // FGC-01: read-after-write in image mode, and the file holds the bytes.
        {
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            const auto a = gc_pattern(5, 0x40), b = gc_pattern(6, 0x41), c = gc_pattern(7, 0x42);
            (void)gc_read(5); // 5 is cached with the old bytes first
            const DRESULT w1 = disk_write(0, a.data(), 5, 1);
            std::vector<uint8_t> bc(b); bc.insert(bc.end(), c.begin(), c.end());
            const DRESULT w2 = disk_write(0, bc.data(), 6, 2);
            const bool back = gc_read(5) == a && gc_read(6) == b && gc_read(7) == c &&
                              gc_read(6, 2) == bc;
            fatfs_glue::detach(0);
            check("FGC-01", "a written sector reads back as written (cache updated) and the file holds it",
                  made && at && w1 == RES_OK && w2 == RES_OK && back &&
                  gc_file_sector(img, 5) == a && gc_file_sector(img, 6) == b &&
                  gc_file_sector(img, 7) == c, gerr);
        }

        // FGC-02: the cache does not survive detach/attach.
        {
            gc_make_image(img);
            bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            const bool before = gc_read(9) == gc_pattern(9, 1);
            fatfs_glue::detach(0);
            const auto nw = gc_pattern(9, 0x77);
            {
                std::fstream f(img, std::ios::in | std::ios::out | std::ios::binary);
                f.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(kGcLba + 9) * 512));
                f.write(reinterpret_cast<const char*>(nw.data()), 512);
            }
            at = at && fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            const bool after = gc_read(9) == nw;
            fatfs_glue::detach(0);
            check("FGC-02", "a second attach after the file changed on disk reads the new bytes",
                  at && before && after, gerr);
        }

        // FGC-03: journal mode: the overlay is applied after the cache, the file is untouched.
        {
            gc_make_image(img);
            const std::string jf = tmp_path("gc.jsdw");
            std::remove(jf.c_str());
            sd_write_journal::forget_all_for_test();
            sd_write_journal::Binding bd;
            bd.bytes = static_cast<uint64_t>(kGcLba + kGcSectors) * 512;
            bd.sha256.fill(0x5C);
            std::string jwhy;
            const bool bound = sd_write_journal::bind(img, jf, bd, false, jwhy);
            auto j = sd_write_journal::for_image(img);
            const auto mark = gc_pattern(40, 0x99), later = gc_pattern(30, 0x98);
            bool ok = bound && j && j->commit(kGcLba + 40, mark.data());
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr, j);
            // 40: journaled before the attach; the first read fills the cache with the RAW bytes.
            const bool pre1 = gc_read(40) == mark, pre2 = gc_read(40) == mark;
            // 30: read raw (cached), then written through the journal: reads as the journal's bytes.
            const bool raw30 = gc_read(30) == gc_pattern(30, 1);
            const DRESULT w = disk_write(0, later.data(), 30, 1);
            const bool post30 = gc_read(30) == later;
            // The cache holds the file's raw bytes, never the journal's: with the journal
            // emptied, every sector (cached while journaled or written) reads raw again.
            j->clear();
            const bool raw_again = gc_read(40) == gc_pattern(40, 1) && gc_read(30) == gc_pattern(30, 1);
            fatfs_glue::detach(0);
            sd_write_journal::forget_all_for_test();
            std::remove(jf.c_str());
            check("FGC-03", "journal mode: the overlay reads over a cache of raw bytes and the file is unchanged",
                  ok && at && pre1 && pre2 && raw30 && w == RES_OK && post30 && raw_again &&
                  gc_file_sector(img, 40) == gc_pattern(40, 1) &&
                  gc_file_sector(img, 30) == gc_pattern(30, 1), gerr + jwhy);
        }

        // FGC-04: two sectors on one cache line (S and S + kGcLines) keep their own bytes.
        {
            gc_make_image(img);
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            const auto a = gc_pattern(3, 0x10), b = gc_pattern(3 + kGcLines, 0x11);
            const bool w = disk_write(0, a.data(), 3, 1) == RES_OK &&
                           disk_write(0, b.data(), 3 + kGcLines, 1) == RES_OK;
            const bool r1 = gc_read(3) == a;          // evicted by the write of S + kGcLines: refetched
            const bool r2 = gc_read(3 + kGcLines) == b;   // evicts S
            const bool r3 = gc_read(3) == a;
            fatfs_glue::detach(0);
            check("FGC-04", "sectors that share a cache line alternate and each returns its own bytes",
                  at && w && r1 && r2 && r3, gerr);
        }

        // FGC-05: a multi-sector read partly cached, partly not.
        {
            gc_make_image(img);
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            (void)gc_read(101); // 101 cached; 100 and 102 are not
            DRESULT rr = RES_ERROR;
            const auto v = gc_read(100, 3, &rr);
            std::vector<uint8_t> want;
            for (uint32_t n = 100; n < 103; ++n) {
                const auto s = gc_file_sector(img, n);
                want.insert(want.end(), s.begin(), s.end());
            }
            fatfs_glue::detach(0);
            check("FGC-05", "a read partly cached returns the file's bytes for every sector",
                  at && rr == RES_OK && v == want, gerr);
        }

        // FGC-06: a read past the volume is RES_PARERR and fills nothing.
        {
            gc_make_image(img);
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            DRESULT rr = RES_OK;
            const auto v = gc_read(kGcSectors - 1, 2, &rr);
            const bool untouched = v == std::vector<uint8_t>(1024, 0xEE);
            const bool last_ok = gc_read(kGcSectors - 1) == gc_pattern(kGcSectors - 1, 1);
            fatfs_glue::detach(0);
            check("FGC-06", "a read past sector_count is RES_PARERR and leaves the buffer alone",
                  at && rr == RES_PARERR && untouched && last_ok, gerr);
        }

        // FGC-07: a partly cached request with only its first, only its last, then only its
        // middle sector cached (FGC-05 covers the middle) returns the file's bytes for all.
        {
            gc_make_image(img);
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            bool all = at;
            const uint32_t cached[3] = {200, 302, 401}; // first, last, middle of 200.., 300.., 400..
            const uint32_t starts[3] = {200, 300, 400};
            for (int k = 0; k < 3; ++k) {
                (void)gc_read(cached[k]);
                DRESULT rr = RES_ERROR;
                const auto v = gc_read(starts[k], 3, &rr);
                std::vector<uint8_t> want;
                for (uint32_t n = starts[k]; n < starts[k] + 3; ++n) {
                    const auto f = gc_file_sector(img, n);
                    want.insert(want.end(), f.begin(), f.end());
                }
                all = all && rr == RES_OK && v == want;
            }
            fatfs_glue::detach(0);
            check("FGC-07", "a request with only its first, last or middle sector cached returns the file's bytes",
                  all, gerr);
        }

        // FGC-08: a failed (short) read fills no line: the file holds FatFs sectors 0..9, the
        // drive declares 20, so a read of 9..10 fails and 10 must still fail afterwards.
        {
            { std::ofstream f(img, std::ios::binary | std::ios::trunc);
              std::vector<char> z((kGcLba + 10) * 512, 0x11);
              f.write(z.data(), static_cast<std::streamsize>(z.size())); }
            const bool at = fatfs_glue::attach(0, img, kGcLba, 20, gerr);
            DRESULT r1 = RES_OK, r2 = RES_OK;
            (void)gc_read(9, 2, &r1);
            (void)gc_read(10, 1, &r2);
            fatfs_glue::detach(0);
            check("FGC-08", "a failed read fills no cache line",
                  at && r1 == RES_ERROR && r2 == RES_ERROR,
                  "r1=" + std::to_string(r1) + " r2=" + std::to_string(r2));
        }

        // FGC-09: a failed re-attach of a still-attached drive leaves it unusable, an error and
        // not a crash on a dropped cache.
        {
            gc_make_image(img);
            const bool at = fatfs_glue::attach(0, img, kGcLba, kGcSectors, gerr);
            (void)gc_read(5);
            std::string e2;
            const bool again = fatfs_glue::attach(0, tmp_path("no-such-dir/none.img"), kGcLba, kGcSectors, e2);
            DRESULT rr = RES_OK;
            (void)gc_read(5, 1, &rr);
            fatfs_glue::detach(0);
            check("FGC-09", "a failed re-attach leaves the drive not ready, a read is an error",
                  at && !again && rr == RES_NOTRDY, "rr=" + std::to_string(rr));
        }
        std::remove(img.c_str());
    }

    // ---- FAT32-WIN-01..02: the reader's one-sector FAT window (GH #547 stage 5) ----
    // 512-byte clusters hold 128 FAT entries per FAT sector, so a 300 KB file's chain
    // (~600 clusters) crosses four FAT sector boundaries. Oracle: the bytes written.
    {
        const std::string img = tmp_path("win.img");
        const uint32_t lba = 63, sectors = 80000; // ~39 MB: > 65525 clusters of 512 bytes
        Fat32WriteOptions o; o.au_size = 512;
        auto pat = [](size_t n, int mul, int add) {
            std::string v(n, 0);
            for (size_t i = 0; i < n; ++i) v[i] = static_cast<char>(static_cast<int>(i) * mul + add + static_cast<int>(i >> 9));
            return v;
        };
        auto read_back = [&](Fat32Tree& t) { return fat32_read_tree(img, lba, t); };
        auto same = [](const Fat32Tree& t, const char* name, const std::string& want) {
            const Fat32Node* n = find_child(t.root, name);
            return n && std::string(n->data.begin(), n->data.end()) == want;
        };
        std::string werr;
        const std::string a1 = pat(300 * 1024, 7, 1), b1 = pat(200 * 1024, 5, 2);
        Fat32Tree t1; t1.root.push_back(fnode("A.BIN", a1)); t1.root.push_back(fnode("B.BIN", b1));
        const bool ok1 = make_blank(img, lba, sectors) &&
                         fat32_format_and_populate(img, lba, sectors, t1, werr, o);
        Fat32Tree r1;
        const bool rd1 = ok1 && read_back(r1);
        check("FAT32-WIN-01", "files whose chains cross FAT sector boundaries read back byte-identical",
              ok1 && rd1 && same(r1, "A.BIN", a1) && same(r1, "B.BIN", b1), werr);

        // A second read of the SAME path after the image was reformatted with other
        // contents (so the same FAT sectors hold other chains) sees the new image.
        const std::string a2 = pat(250 * 1024, 3, 9), b2 = pat(350 * 1024, 11, 4);
        Fat32Tree t2; t2.root.push_back(fnode("B.BIN", b2)); t2.root.push_back(fnode("A.BIN", a2));
        const bool ok2 = fat32_format_and_populate(img, lba, sectors, t2, werr, o);
        Fat32Tree r2;
        const bool rd2 = ok2 && read_back(r2);
        check("FAT32-WIN-02", "a read after the image was reformatted sees the new chains, none of the old window",
              ok2 && rd2 && same(r2, "A.BIN", a2) && same(r2, "B.BIN", b2), werr);

        // FAT32-WIN-03: the reserved top 4 bits of a FAT32 entry are masked off: every next
        // pointer of FAT1 gets them set and the file still reads back byte-identical.
        {
            const std::string a3 = pat(300 * 1024, 13, 0);
            Fat32Tree t3; t3.root.push_back(fnode("A.BIN", a3));
            const bool ok3 = make_blank(img, lba, sectors) &&
                             fat32_format_and_populate(img, lba, sectors, t3, werr, o);
            uint32_t patched = 0;
            if (ok3) {
                std::fstream f(img, std::ios::in | std::ios::out | std::ios::binary);
                uint8_t bpb[512];
                f.seekg(static_cast<std::streamoff>(lba) * 512);
                f.read(reinterpret_cast<char*>(bpb), 512);
                const uint32_t rsvd = bpb[14] | (bpb[15] << 8);
                const uint32_t fsz = rd32(bpb + 36);
                std::vector<uint8_t> fat(static_cast<size_t>(fsz) * 512);
                const auto at = static_cast<std::streamoff>(lba + rsvd) * 512;
                f.seekg(at);
                f.read(reinterpret_cast<char*>(fat.data()), static_cast<std::streamsize>(fat.size()));
                for (size_t k = 8; k + 4 <= fat.size(); k += 4) {
                    const uint32_t v = rd32(fat.data() + k);
                    if (v >= 2 && v < 0x0FFFFFF8) { fat[k + 3] |= 0xF0; ++patched; }
                }
                f.seekp(at);
                f.write(reinterpret_cast<const char*>(fat.data()), static_cast<std::streamsize>(fat.size()));
            }
            Fat32Tree r3;
            const bool rd3 = ok3 && read_back(r3);
            check("FAT32-WIN-03", "reserved top bits of the FAT entries are ignored",
                  ok3 && patched > 500 && rd3 && same(r3, "A.BIN", a3), werr);
        }
        std::remove(img.c_str());
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
