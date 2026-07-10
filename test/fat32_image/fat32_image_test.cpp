// FAT32 image read/write + geometry tests (Task 27).
//
// Validates the native C++ reimplementation of the FAT32 cluster-count fix
// (tools/fix-sdcard-image.sh). All tests are offline and synthetic — no
// network, no external mtools, no 1 GB fixture:
//
//   FAT32-GEOM-01  Real 1 GB numbers @ 32 KB clusters reproduce the bug:
//                  32 758 data clusters, is_fat32 == false (FatFs would
//                  misclassify as FAT16).
//   FAT32-GEOM-02  Same partition @ 8 KB clusters: 130 938 clusters,
//                  fat_size 1023, reserved 32, is_fat32 == true — matches
//                  mformat -c 16 and clears the 65 525 spec minimum.
//   FAT32-GEOM-03  Discriminative: 4 KB clusters yield a different, higher
//                  cluster count than 8 KB.
//   FAT32-GEOM-04  Partition too small for reserved+FAT → valid == false.
//   FAT32-RW-01    Round-trip: a tree (8.3 file, mixed-case file, long name,
//                  empty file, nested subdir) survives write→read intact.
//   FAT32-RW-02    SFN preservation: after write, the emulator's SFN-based
//                  extract_sd_rom() resolves a mixed-case "enNxtmmc.rom" via
//                  its natural short name ENNXTMMC.ROM (regression guard for
//                  the ~N-mangling bug).
//   FAT32-RW-03    Long-name preservation: "CONTRIBUTING.md" reads back byte
//                  for byte.
//   FAT32-UPSERT-01 fat32_tree_upsert creates missing dirs (case-insensitive)
//                  and replaces an existing file.

#include "core/fat32_image.h"
#include "core/sd_rom_extractor.h"

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

// Create a blank image with an MBR + one FAT32-LBA partition at lba=63.
bool make_blank(const std::string& path, uint32_t part_lba, uint32_t total_sectors) {
    const uint64_t bytes = static_cast<uint64_t>(part_lba + total_sectors) * 512;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    std::vector<uint8_t> mbr(512, 0);
    uint8_t* pe = mbr.data() + 0x1BE;
    pe[4] = 0x0C;              // type FAT32 LBA
    wr32(pe + 8, part_lba);    // start LBA
    wr32(pe + 12, total_sectors);
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

} // namespace

int main() {
    // -- FAT32-GEOM-01 / 02 / 03 / 04 : pure geometry, real 1 GB numbers --
    const uint32_t T = 2097089; // partition sectors of the canonical 1 GB image
    {
        Fat32FormatGeom g64 = fat32_compute_geometry(T, 64);
        check("FAT32-GEOM-01", "csize=64 valid", g64.valid);
        check("FAT32-GEOM-01", "csize=64 -> 32758 data clusters", g64.cluster_count == 32758,
              "got " + std::to_string(g64.cluster_count));
        check("FAT32-GEOM-01", "csize=64 misclassified (not FAT32-valid, <65525)",
              !g64.is_fat32);

        Fat32FormatGeom g16 = fat32_compute_geometry(T, 16);
        check("FAT32-GEOM-02", "csize=16 valid", g16.valid);
        check("FAT32-GEOM-02", "csize=16 -> 130938 data clusters", g16.cluster_count == 130938,
              "got " + std::to_string(g16.cluster_count));
        check("FAT32-GEOM-02", "csize=16 fat_size=1023 (matches mformat)",
              g16.fat_size_sectors == 1023, "got " + std::to_string(g16.fat_size_sectors));
        check("FAT32-GEOM-02", "csize=16 reserved=32", g16.reserved_sectors == 32);
        check("FAT32-GEOM-02", "csize=16 clears 65525 -> is_fat32", g16.is_fat32);

        Fat32FormatGeom g8 = fat32_compute_geometry(T, 8);
        check("FAT32-GEOM-03", "csize=8 clusters differ from csize=16",
              g8.cluster_count != g16.cluster_count && g8.cluster_count > g16.cluster_count,
              "c8=" + std::to_string(g8.cluster_count));

        Fat32FormatGeom gtiny = fat32_compute_geometry(16, 16);
        check("FAT32-GEOM-04", "16-sector partition too small -> invalid", !gtiny.valid);
    }

    // -- FAT32-RW-01 / 02 / 03 : write a synthetic volume and read it back --
    {
        const std::string img = tmp_path("rw.img");
        const uint32_t part_lba = 63;
        const uint32_t total = 32768; // ~16 MB volume
        bool blank_ok = make_blank(img, part_lba, total);
        check("FAT32-RW-01", "blank image created", blank_ok);

        Fat32Tree tree;
        tree.root.push_back(fnode("TBBLUE.FW", "uppercase-8.3"));
        tree.root.push_back(fnode("CONTRIBUTING.md", "a-long-file-name-needs-lfn"));
        tree.root.push_back(fnode("empty.txt", ""));
        Fat32Node mach; mach.name = "machines"; mach.is_dir = true;
        Fat32Node next; next.name = "next"; next.is_dir = true;
        std::string rom(8192, '\x7e');
        rom[0] = static_cast<char>(0xF3); rom[1] = static_cast<char>(0xC3);
        next.children.push_back(fnode("enNxtmmc.rom", rom)); // mixed case
        mach.children.push_back(next);
        tree.root.push_back(mach);

        Fat32FormatGeom geom = fat32_compute_geometry(total, 8);
        std::string err;
        bool wr = geom.valid && fat32_write_volume(img, part_lba, geom, tree, err);
        check("FAT32-RW-01", "write_volume succeeded", wr, err);

        uint32_t plba = 0;
        bool found = fat32_find_partition(img, plba);
        check("FAT32-RW-01", "partition rediscovered at 63", found && plba == 63);

        Fat32Tree back;
        bool rd = fat32_read_tree(img, plba, back);
        check("FAT32-RW-01", "read_tree succeeded", rd);
        check("FAT32-RW-01", "root has 4 entries", back.root.size() == 4,
              "got " + std::to_string(back.root.size()));

        const Fat32Node* tb = find_child(back.root, "TBBLUE.FW");
        check("FAT32-RW-01", "TBBLUE.FW present with content",
              tb && !tb->is_dir && std::string(tb->data.begin(), tb->data.end()) == "uppercase-8.3");
        const Fat32Node* em = find_child(back.root, "empty.txt");
        check("FAT32-RW-01", "empty.txt present and empty", em && em->data.empty());

        const Fat32Node* cn = find_child(back.root, "CONTRIBUTING.md");
        check("FAT32-RW-03", "long name preserved exactly",
              cn && std::string(cn->data.begin(), cn->data.end()) == "a-long-file-name-needs-lfn");

        const Fat32Node* md = find_child(back.root, "machines");
        const Fat32Node* nx = md ? find_child(md->children, "next") : nullptr;
        const Fat32Node* rn = nx ? find_child(nx->children, "enNxtmmc.rom") : nullptr;
        check("FAT32-RW-01", "nested machines/next/enNxtmmc.rom present",
              rn && rn->data.size() == 8192 && rn->data == std::vector<uint8_t>(rom.begin(), rom.end()));

        // FAT32-RW-02: SFN lookup must find the mixed-case file by its natural
        // uppercase 8.3 short name (ENNXTMMC.ROM). This is the exact path the
        // emulator uses to load peripheral ROMs.
        std::vector<uint8_t> out;
        bool sfn_ok = extract_sd_rom(img, "/machines/next/enNxtmmc.rom", out);
        check("FAT32-RW-02", "extract_sd_rom resolves mixed-case file by SFN",
              sfn_ok && out.size() == 8192, "ok=" + std::to_string(sfn_ok) +
              " size=" + std::to_string(out.size()));

        std::remove(img.c_str());
    }

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
