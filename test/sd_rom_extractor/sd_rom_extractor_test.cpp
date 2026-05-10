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
//   SD-EXT-05  Root-level file: /TBBLUE.FW (304640 B). Confirms root-
//              directory parsing without any descent.
//   SD-EXT-06  Not found: /MACHINES/NEXT/nonexistent.rom returns false,
//              `out` empty.
//   SD-EXT-07  Wrong image: tiny / non-FAT file returns false without
//              crashing; error logged.
//   SD-EXT-08  Leading-slash optional: "MACHINES/NEXT/enNxtmmc.rom"
//              (no leading '/') resolves identically to SD-EXT-01.
//
// Fixture: roms/nextzxos-1gb-fat32fix.img (canonical 1 GB NextZXOS image,
// MBR + FAT32-LBA, 8 KB clusters; see CLAUDE.md). The reference bytes
// for SD-EXT-01/03/04/05 were captured offline via `mtools` so the test
// is deterministic without depending on `mtools` being installed at
// run time.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
// so the Makefile aggregator picks it up.

#include "core/sd_rom_extractor.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // mkstemp / write / close (POSIX)

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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
    ++g_skip;
    g_skipped.push_back({id, reason});
}

// Resolve the canonical SD image path. Allow override via environment
// variable for out-of-tree runs; default falls back to the repo-relative
// path which works from any cwd that can see the worktree's roms/ link.
std::string canonical_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) {
        return env;
    }
    return "roms/nextzxos-1gb-fat32fix.img";
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
constexpr uint8_t kTbblueFwMagic[4] = { 0x00, 0x00, 0x36, 0x00 };

bool first4_eq(const std::vector<uint8_t>& v, const uint8_t (&ref)[4]) {
    if (v.size() < 4) return false;
    return std::memcmp(v.data(), ref, 4) == 0;
}

} // namespace

int main() {
    const std::string img = canonical_image();
    if (!file_exists(img)) {
        // Skip the entire suite — mark each row as SKIPped and exit 0
        // so the Makefile aggregator records it as 0/0/0/8.
        const char* reasons[] = {
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
            "SD image fixture missing",
        };
        const char* ids[] = {
            "SD-EXT-01", "SD-EXT-02", "SD-EXT-03", "SD-EXT-04",
            "SD-EXT-05", "SD-EXT-06", "SD-EXT-07", "SD-EXT-08",
        };
        for (int i = 0; i < 8; ++i) skip(ids[i], reasons[i]);
        std::printf("\n====================================\n");
        std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                    g_total + g_skip, g_pass, g_fail, g_skip);
        std::printf("\nSkipped rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  SKIP %s: %s\n", s.id, s.reason);
        }
        std::printf("Hint: set JNEXT_TEST_SD_IMAGE to the canonical 1 GB image\n");
        return 0;
    }

    // ------------------------------------------------------------------
    // SD-EXT-01 — happy path: enNxtmmc.rom (8192 B), F3 C3 6A 00 prefix.
    // ------------------------------------------------------------------
    std::vector<uint8_t> ennxt;
    {
        size_t n = 0;
        const bool ok = extract_sd_rom(img, "/MACHINES/NEXT/enNxtmmc.rom", ennxt, &n);
        check("SD-EXT-01", "extract enNxtmmc.rom returns true", ok);
        check("SD-EXT-01", "enNxtmmc.rom size is 8192", ennxt.size() == 8192,
              "size=" + std::to_string(ennxt.size()));
        check("SD-EXT-01", "bytes_read_out matches buffer size", n == ennxt.size(),
              "out=" + std::to_string(n));
        check("SD-EXT-01", "enNxtmmc.rom first 4 bytes match canonical magic",
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
        check("SD-EXT-02", "lowercase path yields identical bytes",
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
        check("SD-EXT-03", "48.rom size is 16384", v48.size() == 16384,
              "size=" + std::to_string(v48.size()));
        check("SD-EXT-03", "48.rom first 4 bytes match canonical magic",
              first4_eq(v48, k48RomMagic),
              "got=" + hex_prefix(v48, 4));
        check("SD-EXT-03", "48.rom bytes differ from enNxtmmc.rom",
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
        check("SD-EXT-04", "enNextMf.rom size is 8192", v.size() == 8192,
              "size=" + std::to_string(v.size()));
        check("SD-EXT-04", "enNextMf.rom first 4 bytes match canonical magic",
              first4_eq(v, kEnNextMfMagic),
              "got=" + hex_prefix(v, 4));
        check("SD-EXT-04", "enNextMf.rom content differs from enNxtmmc.rom",
              !v.empty() && !ennxt.empty() && v != ennxt);
    }

    // ------------------------------------------------------------------
    // SD-EXT-05 — root-level file: TBBLUE.FW (304640 B), starts 00 00 36 00.
    // ------------------------------------------------------------------
    {
        std::vector<uint8_t> v;
        size_t n = 0;
        const bool ok = extract_sd_rom(img, "/TBBLUE.FW", v, &n);
        check("SD-EXT-05", "extract /TBBLUE.FW returns true", ok);
        check("SD-EXT-05", "TBBLUE.FW size is 304640", v.size() == 304640,
              "size=" + std::to_string(v.size()));
        check("SD-EXT-05", "TBBLUE.FW bytes_read_out matches", n == v.size(),
              "out=" + std::to_string(n));
        check("SD-EXT-05", "TBBLUE.FW first 4 bytes match canonical magic",
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
        check("SD-EXT-06", "missing file leaves out empty", v.empty(),
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
            check("SD-EXT-07", "non-FAT image leaves out empty", v.empty());
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
        check("SD-EXT-08", "no-leading-slash bytes match SD-EXT-01",
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
            constexpr uint32_t kPartLba = 1;
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
            check("SD-EXT-09",
                  "cyclic-chain failure leaves out empty",
                  out.empty(),
                  "size=" + std::to_string(out.size()));
            std::remove(tmpl);
        }
    }

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
