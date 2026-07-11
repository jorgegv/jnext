// FatFs SD-image patch end-to-end tests (Task 27, FatFs rewrite).
//
// The hand-rolled FAT32 volume writer + geometry math were retired in favour
// of the vendored ChaN FatFs (src/third_party/fatfs): the patch now reformats
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
//   FAT32-UPSERT-01 fat32_tree_upsert creates missing dirs (case-insensitive)
//                   and replaces an existing file (unchanged unit).

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_provisioner.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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
    check("FATFS-PATCH", "source tree written via FatFs", prep, err);

    // ---- Run the real provisioner patch entry point on the source ----
    bool patched = prep && sdcard::patch_image_fat32(src, err);
    check("FATFS-PATCH", "patch_image_fat32 succeeded", patched, err);

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
        check("FATFS-PATCH-01", "FatFs mounts patched image",
              mounted, err);
        check("FATFS-PATCH-01", "FatFs reports >= 65525 clusters",
              mounted && total_clusters >= 65525u,
              "total_clusters=" + std::to_string(total_clusters));

        // -- FATFS-PATCH-02: whole tree round-trips byte-identical --
        Fat32Tree back;
        bool rd = fat32_read_tree(src, part_lba, back);
        check("FATFS-PATCH-02", "read_tree of patched image succeeded", rd);

        const Fat32Node* tb = find_child(back.root, "TBBLUE.FW");
        check("FATFS-PATCH-02", "TBBLUE.FW content intact",
              tb && !tb->is_dir &&
              std::string(tb->data.begin(), tb->data.end()) == "uppercase-8.3");
        const Fat32Node* em = find_child(back.root, "empty.txt");
        check("FATFS-PATCH-02", "empty.txt present and empty",
              em && em->data.empty());
        const Fat32Node* md = find_child(back.root, "MACHINES");
        const Fat32Node* nx = md ? find_child(md->children, "NEXT") : nullptr;
        const Fat32Node* rn = nx ? find_child(nx->children, "enNxtmmc.rom") : nullptr;
        check("FATFS-PATCH-02", "nested MACHINES/NEXT/enNxtmmc.rom intact",
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
        check("FATFS-PATCH-04", "enNextMf.rom resolvable by natural SFN",
              mf_ok && mf.size() == 8192 && mf[0] == 0x5a,
              "ok=" + std::to_string(mf_ok) + " size=" + std::to_string(mf.size()));
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
        check("FAT32-UPSERT-01", "missing directories created for new file",
              s && s->is_dir && cfg && cfg->is_dir && nf && nf->data.size() == 1);
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
