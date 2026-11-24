#include "core/sd_rom_extractor.h"
#include "core/log.h"
#include "core/sdcard_provisioner.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers — pure-host FAT12/16/32 parser. Read-only, short-name
// lookup, MBR or partition-less volume + BPB + FAT chain + directory walk.
// ---------------------------------------------------------------------------

namespace {

// Little-endian byte reads from a buffer.
inline uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// FAT BPB (FAT12/16/32) extracted into host-friendly form.
struct Fat32Geom {
    uint32_t partition_lba_start = 0; // sector index of the volume start
    uint16_t bytes_per_sector    = 0;
    uint8_t  sectors_per_cluster = 0;
    uint16_t reserved_sectors    = 0;
    uint8_t  num_fats            = 0;
    uint32_t fat_size_sectors    = 0;
    uint32_t root_cluster        = 0; // FAT32 only
    // FAT12/16 only: the fixed root directory region.
    uint32_t root_entries        = 0;
    uint32_t root_dir_lba        = 0;
    uint32_t root_dir_sectors    = 0;
    int      fat_bits            = 32; // 12, 16 or 32
    // Derived:
    uint32_t fat_start_lba       = 0; // == partition_lba_start + reserved
    uint32_t data_start_lba      = 0; // end of FATs (+ fixed root for 12/16)
    uint32_t bytes_per_cluster   = 0;
    uint32_t n_clusters          = 0; // upper bound on any chain's length
};

// FAT32 directory entry attribute bits.
constexpr uint8_t ATTR_READ_ONLY = 0x01;
constexpr uint8_t ATTR_HIDDEN    = 0x02;
constexpr uint8_t ATTR_SYSTEM    = 0x04;
constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_LFN       = ATTR_READ_ONLY | ATTR_HIDDEN |
                                   ATTR_SYSTEM | ATTR_VOLUME_ID; // 0x0F

constexpr uint32_t FAT32_EOC_MARK = 0x0FFFFFF8u;
constexpr uint32_t FAT32_BAD_MARK = 0x0FFFFFF7u;
constexpr uint32_t FAT32_MASK     = 0x0FFFFFFFu;

// Read a sector (or contiguous run of sectors) from the image file.
// Returns true on success.
bool read_sectors(std::ifstream& f, uint32_t lba, uint32_t count,
                  uint16_t bytes_per_sector, uint8_t* out) {
    const uint64_t off  = static_cast<uint64_t>(lba) * bytes_per_sector;
    const uint64_t size = static_cast<uint64_t>(count) * bytes_per_sector;
    f.seekg(static_cast<std::streamoff>(off), std::ios::beg);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    return f.good() && static_cast<uint64_t>(f.gcount()) == size;
}

// The firmware's FatFs `check_fs` (ff.c:2955-2972) on a 512-byte sector:
// 0x55AA at 510, jump byte 0xE9 or 0xEB..0x90, and "FAT" at 0x36 or "FAT3" at
// 0x52. True = a FAT boot record.
bool is_fat_boot_record(const uint8_t* s) {
    if (s[510] != 0x55 || s[511] != 0xAA) return false;
    if (s[0] == 0xE9 || (s[0] == 0xEB && s[2] == 0x90)) {
        if (std::memcmp(s + 0x36, "FAT", 3) == 0) return true;
        if (std::memcmp(s + 0x52, "FAT3", 4) == 0) return true;
    }
    return false;
}

// Locate the FAT volume the way the firmware's FatFs does (`find_volume`,
// ff.c:3041-3066): sector 0 holding a FAT boot record is the volume itself (a
// partition-less "superfloppy": LBA 0); otherwise the 4 MBR entries are tried in
// order, an entry being skipped when its type byte or LBA is 0, and the first
// whose sector is a FAT boot record wins, WHATEVER its type byte. One legacy
// leniency on top, tried only when that scan found nothing (the first entry of
// type 0x0B/0x0C is taken without the boot-record test, as jnext always did):
// this reader accepts every card the firmware does, and picks the same volume.
// Returns true with `lba_out` set (0 is valid: superfloppy).
// `why`, when non-null, receives the same reason the log line carries. The
// ROM extractor only logs; `read_sd_image_identity` must be able to NAME the
// defect in a refusal a user sees, and re-deriving the reason at the call site
// would be a second copy of this function's decisions.
bool find_fat_volume(std::ifstream& f, uint32_t& lba_out, std::string* why = nullptr) {
    auto fail = [&](const char* msg) {
        Log::emulator()->error("sd_rom_extractor: {}", msg);
        if (why) *why = msg;
        return false;
    };
    uint8_t mbr[512];
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(mbr), 512);
    if (!f.good() || f.gcount() != 512) {
        return fail("failed to read MBR (image too small or unreadable)");
    }
    // MBR boot signature must be 0x55 0xAA at bytes 510-511.
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        return fail("invalid MBR signature (expected 0x55 0xAA at offset 0x1FE)");
    }
    if (is_fat_boot_record(mbr)) {
        lba_out = 0;
        return true;
    }
    // Pass 1, the firmware's scan: entries in order, a type byte or LBA of 0
    // skipped, the first whose sector is a FAT boot record wins, whatever its type.
    // Partition table: 4 entries x 16 bytes starting at offset 0x1BE.
    for (int i = 0; i < 4; ++i) {
        const uint8_t* pe   = mbr + 0x1BE + i * 16;
        const uint8_t  type = pe[4];
        const uint32_t lba  = rd_u32(pe + 8);
        if (type == 0 || lba == 0) continue;
        uint8_t vbr[512];
        f.clear();
        f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(lba) * 512), std::ios::beg);
        f.read(reinterpret_cast<char*>(vbr), 512);
        const bool got = f.good() && f.gcount() == 512;
        f.clear();
        if (got && is_fat_boot_record(vbr)) {
            lba_out = lba;
            return true;
        }
    }
    // Pass 2, the legacy leniency, only when the firmware's scan found nothing:
    // the first 0x0B/0x0C entry is taken without the boot-record test.
    for (int i = 0; i < 4; ++i) {
        const uint8_t* pe   = mbr + 0x1BE + i * 16;
        const uint32_t lba  = rd_u32(pe + 8);
        if ((pe[4] == 0x0B || pe[4] == 0x0C) && lba != 0) {
            lba_out = lba;
            return true;
        }
    }
    return fail("no FAT volume found (no FAT boot record at sector 0 or in the MBR partition table)");
}

// Parse the BPB at the volume's first sector and validate.
// `raw_bpb`, when non-null, receives the 512 raw boot-sector bytes. The
// extended fields `read_sd_image_identity` needs (BS_BootSig, BS_VolID,
// BS_VolLab) are not part of `Fat32Geom`, which models only what a FAT walk
// needs, and re-reading the sector to get them would be a second seek to a
// sector this function has already read.
//
// It is a `std::array<uint8_t, 512>*` and not a `uint8_t*` so the LENGTH is
// part of the type. A raw pointer would make the 512-byte contract a comment,
// and a caller that passed something smaller would get a silent overwrite --
// the exact "in range for its type, out of range for what it sizes" family
// this issue has already shipped six times.
//
// Layout: FAT32 when BPB_FATSz16 == 0 (no cluster-count reclassification, so a
// legacy under-clustered image keeps working); otherwise FAT12/16 with the
// sub-type by cluster count exactly as ff.c:3140-3146 (<= 0xFF5 FAT12, <= 0xFFF5
// FAT16); more clusters than that under a FAT12/16 layout is refused (the
// firmware calls it FAT32 and rejects it for RootEntCnt != 0, ff.c:3156).
//
// `why` works exactly as in `find_fat_volume` above.
bool parse_bpb(std::ifstream& f, uint32_t partition_lba, Fat32Geom& g,
               std::array<uint8_t, 512>* raw_bpb = nullptr,
               std::string* why = nullptr) {
    uint8_t bpb[512];
    f.clear();
    f.seekg(0, std::ios::end);
    const std::streamoff img_end = f.tellg();
    const uint64_t image_sectors = img_end > 0 ? static_cast<uint64_t>(img_end) / 512 : 0;
    f.clear();
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(partition_lba) * 512),
            std::ios::beg);
    f.read(reinterpret_cast<char*>(bpb), 512);
    if (!f.good() || f.gcount() != 512) {
        Log::emulator()->error("sd_rom_extractor: failed to read BPB at LBA {}", partition_lba);
        if (why) *why = "failed to read the FAT boot sector at LBA " +
                        std::to_string(partition_lba);
        return false;
    }
    if (raw_bpb) std::memcpy(raw_bpb->data(), bpb, raw_bpb->size());

    g.partition_lba_start = partition_lba;
    g.bytes_per_sector    = rd_u16(bpb + 11);
    g.sectors_per_cluster = bpb[13];
    g.reserved_sectors    = rd_u16(bpb + 14);
    g.num_fats            = bpb[16];
    const uint32_t fatsz16 = rd_u16(bpb + 22);
    const bool fat32_layout = (fatsz16 == 0);
    g.fat_size_sectors    = fat32_layout ? rd_u32(bpb + 36) : fatsz16;
    g.root_cluster        = fat32_layout ? rd_u32(bpb + 44) : 0;
    g.root_entries        = rd_u16(bpb + 17);

    // Sanity: bytes_per_sector must be one of {512, 1024, 2048, 4096}.
    if (g.bytes_per_sector != 512 && g.bytes_per_sector != 1024 &&
        g.bytes_per_sector != 2048 && g.bytes_per_sector != 4096) {
        Log::emulator()->error("sd_rom_extractor: invalid bytes_per_sector={}", g.bytes_per_sector);
        if (why) *why = "invalid FAT bytes_per_sector=" +
                        std::to_string(g.bytes_per_sector);
        return false;
    }
    // sectors_per_cluster must be a power of 2 in [1,128].
    if (g.sectors_per_cluster == 0 ||
        (g.sectors_per_cluster & (g.sectors_per_cluster - 1)) != 0) {
        Log::emulator()->error("sd_rom_extractor: invalid sectors_per_cluster={}", g.sectors_per_cluster);
        if (why) *why = "invalid FAT sectors_per_cluster=" +
                        std::to_string(g.sectors_per_cluster);
        return false;
    }
    if (g.num_fats == 0 || g.fat_size_sectors == 0 ||
        (fat32_layout && g.root_cluster < 2) ||
        (!fat32_layout && g.root_entries == 0)) {
        Log::emulator()->error("sd_rom_extractor: invalid FAT BPB (num_fats={}, fat_size={}, root_cluster={}, root_entries={})",
                               g.num_fats, g.fat_size_sectors, g.root_cluster, g.root_entries);
        if (why) *why = "invalid FAT BPB (num_fats=" +
                        std::to_string(g.num_fats) + " fat_size=" +
                        std::to_string(g.fat_size_sectors) + " root_cluster=" +
                        std::to_string(g.root_cluster) + " root_entries=" +
                        std::to_string(g.root_entries) + ")";
        return false;
    }

    g.fat_start_lba     = g.partition_lba_start + g.reserved_sectors;
    g.data_start_lba    = g.fat_start_lba +
                          static_cast<uint32_t>(g.num_fats) * g.fat_size_sectors;
    g.bytes_per_cluster = static_cast<uint32_t>(g.sectors_per_cluster) *
                          g.bytes_per_sector;
    // A chain cannot visit more clusters than the image FILE holds past the system
    // area, whatever a hostile BPB claims (TotSec32 0xFFFFFFFF, a huge FAT).
    auto cap_by_image = [&](uint64_t n) -> uint32_t {
        const uint64_t room = image_sectors > g.data_start_lba
            ? (image_sectors - g.data_start_lba) / g.sectors_per_cluster : 0;
        return static_cast<uint32_t>(std::min<uint64_t>(n, room));
    };
    if (fat32_layout) {
        // Chain-length bound: the volume's cluster count where the BPB gives
        // one, never more than the FAT can hold entries for.
        const uint64_t fat_entries =
            static_cast<uint64_t>(g.fat_size_sectors) * g.bytes_per_sector / 4;
        g.fat_bits = 32;
        const uint32_t tot32 = rd_u32(bpb + 32);
        const uint32_t sys32 = g.data_start_lba - g.partition_lba_start;
        uint64_t n = fat_entries;
        if (tot32 > sys32) n = std::min<uint64_t>(n, (tot32 - sys32) / g.sectors_per_cluster);
        g.n_clusters = cap_by_image(std::min<uint64_t>(n, 0x0FFFFFFFu));
        return true;
    }

    // FAT12/16: fixed root directory between the FATs and the data area.
    const uint32_t per_sector = g.bytes_per_sector / 32;
    g.root_dir_sectors = (g.root_entries + per_sector - 1) / per_sector;
    g.root_dir_lba     = g.data_start_lba;
    g.data_start_lba  += g.root_dir_sectors;
    uint32_t tsect = rd_u16(bpb + 19);
    if (tsect == 0) tsect = rd_u32(bpb + 32);
    const uint32_t sysect = g.data_start_lba - g.partition_lba_start;
    if (tsect < sysect || (tsect - sysect) / g.sectors_per_cluster == 0) {
        Log::emulator()->error("sd_rom_extractor: invalid FAT12/16 volume size (total_sectors={})", tsect);
        if (why) *why = "invalid FAT12/16 volume size (total_sectors=" +
                        std::to_string(tsect) + ")";
        return false;
    }
    const uint32_t nclst = (tsect - sysect) / g.sectors_per_cluster;
    if (nclst > 0xFFF5u) {
        Log::emulator()->error("sd_rom_extractor: FAT12/16 layout with {} clusters (more than 65525)", nclst);
        if (why) *why = "FAT12/16 layout with " + std::to_string(nclst) +
                        " clusters (more than 65525: not a FAT16 volume)";
        return false;
    }
    g.fat_bits = (nclst <= 0xFF5u) ? 12 : 16;
    g.n_clusters = cap_by_image(nclst);
    return true;
}

// LBA of the first sector of cluster N (N >= 2).
uint32_t cluster_first_lba(const Fat32Geom& g, uint32_t cluster) {
    return g.data_start_lba +
           (cluster - 2) * static_cast<uint32_t>(g.sectors_per_cluster);
}

// End-of-chain / bad-cluster thresholds per FAT width.
uint32_t eoc_mark(const Fat32Geom& g) {
    return g.fat_bits == 12 ? 0xFF8u : g.fat_bits == 16 ? 0xFFF8u : FAT32_EOC_MARK;
}
uint32_t bad_mark(const Fat32Geom& g) {
    return g.fat_bits == 12 ? 0xFF7u : g.fat_bits == 16 ? 0xFFF7u : FAT32_BAD_MARK;
}

// Read the FAT entry for cluster N (12, 16 or 28 bits by volume type).
// Returns the next-cluster value, 0 on read error.
uint32_t fat_next(std::ifstream& f, const Fat32Geom& g, uint32_t cluster) {
    uint64_t byte_off = static_cast<uint64_t>(g.fat_start_lba) * g.bytes_per_sector;
    size_t   len;
    if (g.fat_bits == 12)      { byte_off += cluster + cluster / 2; len = 2; }
    else if (g.fat_bits == 16) { byte_off += static_cast<uint64_t>(cluster) * 2; len = 2; }
    else                       { byte_off += static_cast<uint64_t>(cluster) * 4; len = 4; }
    uint8_t entry[4] = {0, 0, 0, 0};
    f.clear();
    f.seekg(static_cast<std::streamoff>(byte_off), std::ios::beg);
    f.read(reinterpret_cast<char*>(entry), static_cast<std::streamsize>(len));
    if (!f.good() || static_cast<size_t>(f.gcount()) != len) {
        Log::emulator()->error("sd_rom_extractor: failed to read FAT entry for cluster {}", cluster);
        return 0;
    }
    if (g.fat_bits == 12) {
        const uint32_t v = rd_u16(entry);
        return (cluster & 1) ? (v >> 4) : (v & 0xFFF);
    }
    if (g.fat_bits == 16) return rd_u16(entry);
    return rd_u32(entry) & FAT32_MASK;
}

// Build the 11-byte short-name match key from a user-supplied path
// component like "enNxtmmc.rom" → "ENNXTMMC", "ROM" → 11 bytes
// "ENNXTMMCROM". Space-padded, uppercase, ASCII-only. Truncates name
// part to 8 chars and extension to 3 chars (FAT 8.3 limit).
//
// Returns false if component is empty.
bool make_sfn_key(const std::string& component, std::array<char, 11>& key) {
    if (component.empty()) return false;
    key.fill(' ');

    // Special-case "." and ".." — these match SFN entries verbatim with
    // trailing spaces. ('.' alone is rare in lookup paths but harmless.)
    if (component == ".") {
        key[0] = '.';
        return true;
    }
    if (component == "..") {
        key[0] = '.';
        key[1] = '.';
        return true;
    }

    auto dot_pos = component.find_last_of('.');
    std::string base, ext;
    if (dot_pos == std::string::npos) {
        base = component;
    } else {
        base = component.substr(0, dot_pos);
        ext  = component.substr(dot_pos + 1);
    }

    auto upper = [](char c) -> char {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    };

    for (size_t i = 0; i < base.size() && i < 8; ++i) {
        key[i] = upper(base[i]);
    }
    for (size_t i = 0; i < ext.size() && i < 3; ++i) {
        key[8 + i] = upper(ext[i]);
    }
    return true;
}

// Compare a 32-byte directory entry's 11-byte SFN field against a
// user-supplied key. Handles the 0x05 → 0xE5 first-byte aliasing.
bool match_sfn(const uint8_t* entry, const std::array<char, 11>& key) {
    // Build the entry's effective name (with 0x05 aliasing).
    std::array<uint8_t, 11> en{};
    std::memcpy(en.data(), entry, 11);
    if (en[0] == 0x05) en[0] = 0xE5;

    for (size_t i = 0; i < 11; ++i) {
        if (en[i] != static_cast<uint8_t>(key[i])) return false;
    }
    return true;
}

// Read all bytes of one cluster into out_buf.
bool read_cluster(std::ifstream& f, const Fat32Geom& g, uint32_t cluster,
                  std::vector<uint8_t>& out_buf) {
    out_buf.resize(g.bytes_per_cluster);
    return read_sectors(f, cluster_first_lba(g, cluster),
                        g.sectors_per_cluster, g.bytes_per_sector,
                        out_buf.data());
}

// Walk the FAT chain starting at `start_cluster`, scanning directory
// entries for one matching SFN key. On match, fills first_cluster_out
// and file_size_out with the matched entry's data and sets is_dir_out.
// Returns true on match, false otherwise (also false on I/O errors —
// caller treats both equivalently for "not found").
//
// V16-DIVMMC-02 (Pass-16 verify-audit, 2026-05-10): bound the cluster
// chain walk by the total number of clusters representable in the FAT.
// Previously this loop only checked `cluster < FAT32_BAD_MARK` and
// terminated on EOC / read failure / end-of-directory. A malformed or
// hostile FAT image with a CYCLE in a directory's cluster chain (e.g.
// cluster A → cluster B → cluster A) would never hit any of those exit
// conditions and would loop indefinitely, hanging the host process.
// The file-data read path (extract_sd_rom main loop) already had a
// `max_chain_len = (file_size / bytes_per_cluster) + 2` bound, but
// directory traversal had no equivalent guard. Class-(c) defensive —
// the canonical TBBlue distribution image is well-formed, but a forensic
// or user-supplied image could trigger the hang. Fix: use the total
// number of valid clusters in the partition (= fat_size_sectors *
// bytes_per_sector / 4, the number of FAT entries representable) as a
// hard upper bound — well-formed cluster chains can never exceed this.
bool find_in_directory(std::ifstream& f, const Fat32Geom& g,
                       uint32_t start_cluster,
                       const std::array<char, 11>& key,
                       uint32_t& first_cluster_out,
                       uint32_t& file_size_out,
                       bool& is_dir_out) {
    // Scan one directory buffer for the key. 1 = hit, 0 = keep going,
    // -1 = end-of-directory marker (no entries beyond).
    auto scan = [&](const std::vector<uint8_t>& buf) -> int {
        for (uint32_t off = 0; off + 32 <= buf.size(); off += 32) {
            const uint8_t* e = buf.data() + off;
            const uint8_t  first = e[0];
            const uint8_t  attr  = e[11];
            if (first == 0x00) return -1;           // end of directory
            if (first == 0xE5) continue;            // deleted
            if (attr == ATTR_LFN) continue;         // skip LFN slots
            if (attr & ATTR_VOLUME_ID) continue;    // skip volume label
            if (!match_sfn(e, key)) continue;

            // The high cluster word exists on FAT32 only (FAT12/16 ignore it).
            const uint16_t hi = g.fat_bits == 32 ? rd_u16(e + 20) : uint16_t{0};
            const uint16_t lo = rd_u16(e + 26);
            first_cluster_out = (static_cast<uint32_t>(hi) << 16) |
                                static_cast<uint32_t>(lo);
            file_size_out     = rd_u32(e + 28);
            is_dir_out        = (attr & ATTR_DIRECTORY) != 0;
            return 1;
        }
        return 0;
    };

    std::vector<uint8_t> buf;
    // FAT12/16 root directory: a fixed run of sectors, bounded by RootEntCnt
    // (ff.c:3163), not a cluster chain.
    if (start_cluster == 0 && g.fat_bits != 32) {
        buf.resize(static_cast<size_t>(g.root_dir_sectors) * g.bytes_per_sector);
        if (!read_sectors(f, g.root_dir_lba, g.root_dir_sectors,
                          g.bytes_per_sector, buf.data())) return false;
        buf.resize(std::min<size_t>(buf.size(),
                                    static_cast<size_t>(g.root_entries) * 32));
        return scan(buf) == 1;
    }

    uint32_t cluster = start_cluster;
    // Hard upper bound on chain walk: any chain longer than the volume's
    // cluster count is a cycle, so a hostile image costs at most one walk of
    // that length (the FAT's capacity can be 100x larger than the volume).
    const uint64_t max_chain_len = g.n_clusters;
    uint64_t steps = 0;
    while (cluster >= 2 && cluster < bad_mark(g)) {
        if (++steps > max_chain_len) {
            Log::emulator()->error(
                "sd_rom_extractor: directory cluster chain longer than "
                "total FAT entries ({} > {}) - malformed/cyclic FAT",
                steps, max_chain_len);
            return false;
        }
        if (!read_cluster(f, g, cluster, buf)) return false;
        const int r = scan(buf);
        if (r != 0) return r == 1;
        // Advance to next cluster in chain.
        const uint32_t nxt = fat_next(f, g, cluster);
        if (nxt == 0 || nxt >= eoc_mark(g)) return false;
        cluster = nxt;
    }
    return false;
}

// Split a forward-slash path into components, dropping empty entries
// (so "/foo/bar" and "foo/bar" produce identical splits).
std::vector<std::string> split_path(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '/') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool extract_sd_rom(const std::string& sd_image_path,
                    const std::string& sd_path,
                    std::vector<uint8_t>& out,
                    std::size_t* bytes_read_out) {
    out.clear();
    if (bytes_read_out) *bytes_read_out = 0;

    // Open the image read-only, binary mode.
    std::ifstream f(sd_image_path, std::ios::binary);
    if (!f) {
        Log::emulator()->error("sd_rom_extractor: cannot open SD image '{}'", sd_image_path);
        return false;
    }

    // Locate the FAT volume (superfloppy or MBR partition).
    uint32_t part_lba = 0;
    if (!find_fat_volume(f, part_lba)) {
        // find_fat_volume already logged the reason.
        return false;
    }

    // Parse the BPB.
    Fat32Geom g{};
    if (!parse_bpb(f, part_lba, g)) {
        // parse_bpb already logged the reason.
        return false;
    }

    // Split the requested path into components.
    const auto parts = split_path(sd_path);
    if (parts.empty()) {
        Log::emulator()->error("sd_rom_extractor: empty path '{}'", sd_path);
        return false;
    }

    // Walk each path component.
    uint32_t cur_dir_cluster = g.root_cluster;
    uint32_t file_cluster    = 0;
    uint32_t file_size       = 0;
    bool     is_dir          = true;

    for (size_t i = 0; i < parts.size(); ++i) {
        std::array<char, 11> key{};
        if (!make_sfn_key(parts[i], key)) {
            Log::emulator()->error("sd_rom_extractor: invalid path component in '{}'", sd_path);
            return false;
        }
        uint32_t hit_cluster = 0, hit_size = 0;
        bool     hit_is_dir  = false;
        if (!find_in_directory(f, g, cur_dir_cluster, key,
                               hit_cluster, hit_size, hit_is_dir)) {
            Log::emulator()->error("sd_rom_extractor: path '{}' not found in '{}' (component '{}')",
                                   sd_path, sd_image_path, parts[i]);
            return false;
        }

        const bool last = (i + 1 == parts.size());
        if (last) {
            if (hit_is_dir) {
                Log::emulator()->error("sd_rom_extractor: path '{}' resolves to a directory, expected file", sd_path);
                return false;
            }
            file_cluster = hit_cluster;
            file_size    = hit_size;
            is_dir       = false;
        } else {
            if (!hit_is_dir) {
                Log::emulator()->error("sd_rom_extractor: intermediate component '{}' in '{}' is not a directory",
                                       parts[i], sd_path);
                return false;
            }
            cur_dir_cluster = hit_cluster;
            // Special case: FAT32 "." and ".." in the root dir entries
            // sometimes have first_cluster == 0 — meaning root. Defensive
            // remap for any zero cluster encountered as a directory.
            if (cur_dir_cluster == 0) cur_dir_cluster = g.root_cluster;
        }
    }

    if (is_dir) {
        // Should be unreachable given the loop above, but guard anyway.
        Log::emulator()->error("sd_rom_extractor: path '{}' did not resolve to a file", sd_path);
        return false;
    }

    // Empty file is a valid case — return success with empty buffer.
    if (file_size == 0) {
        if (bytes_read_out) *bytes_read_out = 0;
        return true;
    }
    // Zero-cluster non-empty file is malformed.
    if (file_cluster < 2) {
        Log::emulator()->error("sd_rom_extractor: file '{}' has nonzero size but cluster=0 (malformed FAT)", sd_path);
        return false;
    }

    // Walk the file's FAT chain, reading clusters until we've collected
    // file_size bytes.
    out.reserve(file_size);
    std::vector<uint8_t> cluster_buf;
    uint32_t cluster   = file_cluster;
    uint32_t remaining = file_size;
    // Bound on chain length (defends against pathological FAT cycles).
    const uint32_t max_chain_len = (file_size / g.bytes_per_cluster) + 2;
    uint32_t steps = 0;

    while (remaining > 0) {
        if (cluster < 2 || cluster >= bad_mark(g)) {
            Log::emulator()->error("sd_rom_extractor: unexpected EOC/bad-cluster in chain for '{}'", sd_path);
            return false;
        }
        if (++steps > max_chain_len) {
            Log::emulator()->error("sd_rom_extractor: FAT chain longer than expected for '{}'", sd_path);
            return false;
        }
        if (!read_cluster(f, g, cluster, cluster_buf)) {
            Log::emulator()->error("sd_rom_extractor: failed to read cluster {} for '{}'", cluster, sd_path);
            return false;
        }
        const uint32_t take = std::min(remaining, g.bytes_per_cluster);
        out.insert(out.end(), cluster_buf.begin(),
                   cluster_buf.begin() + take);
        remaining -= take;
        if (remaining == 0) break;

        const uint32_t nxt = fat_next(f, g, cluster);
        if (nxt < 2 || nxt >= eoc_mark(g)) {
            Log::emulator()->error("sd_rom_extractor: premature EOC at cluster {} for '{}' (got {} bytes, need {} more)",
                                   cluster, sd_path, file_size - remaining, remaining);
            return false;
        }
        cluster = nxt;
    }

    if (bytes_read_out) *bytes_read_out = file_size;
    return true;
}

// ---------------------------------------------------------------------------
// SD image TIER-1 IDENTITY — GH #27 S7, design §11.3
// ---------------------------------------------------------------------------

bool read_sd_image_identity(const std::string& sd_image_path,
                            SdImageIdentity& out, std::string& why) {
    out = SdImageIdentity{};
    why.clear();

    // EVERY failure path leaves `out` empty, not partly filled. The size and
    // the MBR digest are known before the FAT32 parse can fail, so without this
    // a refused image would hand back two of the five fields — and an identity
    // with two fields set compares unequal against everything, including
    // itself, which is a refusal nobody can act on. The one caller in the tree
    // clears its own output and returns, so this is not a live defect; it is
    // the contract made total instead of resting on one caller's discipline.
    auto fail = [&out]() { out = SdImageIdentity{}; return false; };

    std::ifstream f(sd_image_path, std::ios::binary);
    if (!f) {
        why = "cannot open SD image '" + sd_image_path + "'";
        Log::emulator()->error("sd_rom_extractor: {}", why);
        return fail();
    }

    // Size first: it is the one field that needs no parsing, and a zero-length
    // or truncated file fails the MBR read below with a clearer message.
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    if (end < 0) {
        why = "cannot determine the size of SD image '" + sd_image_path + "'";
        Log::emulator()->error("sd_rom_extractor: {}", why);
        return fail();
    }
    out.image_bytes = static_cast<uint64_t>(end);

    // Digest the MBR PARTITION TABLE (0x1BE..0x1FF, 66 bytes), not the whole
    // sector — see the field's doc-comment for why the bootstrap code is
    // excluded.
    uint8_t mbr[512];
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(mbr), 512);
    if (!f.good() || f.gcount() != 512) {
        why = "failed to read the MBR of '" + sd_image_path +
              "' (image too small or unreadable)";
        Log::emulator()->error("sd_rom_extractor: {}", why);
        return fail();
    }
    out.mbr_partition_table_sha256 = sdcard::sha256_hex(
        std::vector<uint8_t>(mbr + 0x1BE, mbr + 0x200));
    if (out.mbr_partition_table_sha256.empty()) {
        why = "SHA-256 of the MBR partition table failed";
        Log::emulator()->error("sd_rom_extractor: {}", why);
        return fail();
    }

    uint32_t part_lba = 0;
    if (!find_fat_volume(f, part_lba, &why)) {
        why = "'" + sd_image_path + "': " + why;
        return fail();
    }
    out.partition_lba = part_lba;

    Fat32Geom g{};
    std::array<uint8_t, 512> bpb{};
    if (!parse_bpb(f, part_lba, g, &bpb, &why)) {
        why = "'" + sd_image_path + "': " + why;
        return fail();
    }

    // BS_BootSig (offset 0x42) == 0x29 is what makes BS_VolID and BS_VolLab
    // defined at all. Without it the bytes at 0x43/0x47 are not a volume
    // serial and not a label, so they are left EMPTY rather than reported as
    // an identity nothing can trust. `jns::SdIdentity::populated()` is then
    // false and the reader refuses — JNSI-13's rule, reached honestly.
    // FAT12/16 keep the extended fields at 0x26/0x27/0x2B, FAT32 at
    // 0x42/0x43/0x47 (ff.c:408-424).
    const bool fat32 = (g.fat_bits == 32);
    const size_t kOffBootSig = fat32 ? 0x42 : 0x26;
    const size_t kOffVolID   = fat32 ? 0x43 : 0x27;
    const size_t kOffVolLab  = fat32 ? 0x47 : 0x2B;
    constexpr size_t kVolLabLen  = 11;
    if (bpb[kOffBootSig] != 0x29) {
        Log::emulator()->warn(
            "sd_rom_extractor: '{}' FAT boot sector has no extended signature "
            "(BS_BootSig=0x{:02X}, expected 0x29); volume serial and label are "
            "undefined and are not recorded",
            sd_image_path, bpb[kOffBootSig]);
        return true;
    }

    const uint32_t volid = rd_u32(bpb.data() + kOffVolID);
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%08x", volid);
    out.fat32_volume_id = hex;

    out.fat32_bs_vollab.reserve(kVolLabLen);
    for (size_t i = 0; i < kVolLabLen; ++i) {
        const uint8_t c = bpb[kOffVolLab + i];
        out.fat32_bs_vollab.push_back(
            (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?');
    }
    return true;
}

// ---------------------------------------------------------------------------
// The FAT volume as the firmware finds it, for a FatFs mount (GH #557)
// ---------------------------------------------------------------------------

bool find_sd_fat_volume(const std::string& sd_image_path, uint32_t& lba, uint32_t& sectors,
                        std::string& why) {
    lba = 0;
    sectors = 0;
    why.clear();
    std::ifstream f(sd_image_path, std::ios::binary);
    if (!f) {
        why = "cannot open SD image '" + sd_image_path + "'";
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff end = f.tellg();
    const uint64_t file_bytes = end > 0 ? static_cast<uint64_t>(end) : 0;
    f.clear();
    uint32_t vol_lba = 0;
    if (!find_fat_volume(f, vol_lba, &why)) {
        why = "'" + sd_image_path + "': " + why;
        return false;
    }
    Fat32Geom g{};
    std::array<uint8_t, 512> bpb{};
    if (!parse_bpb(f, vol_lba, g, &bpb, &why)) {
        why = "'" + sd_image_path + "': " + why;
        return false;
    }
    if (g.bytes_per_sector != 512) {
        why = "'" + sd_image_path + "' uses " + std::to_string(g.bytes_per_sector) +
              "-byte sectors; jnext supports 512-byte sectors only";
        return false;
    }
    uint32_t total = rd_u16(bpb.data() + 19);
    if (total == 0) total = rd_u32(bpb.data() + 32);
    if (total == 0) {
        why = "'" + sd_image_path + "' declares a zero-sector FAT volume";
        return false;
    }
    const uint64_t need = (static_cast<uint64_t>(vol_lba) + total) * 512;
    if (need > file_bytes) {
        why = "'" + sd_image_path + "' is truncated: its FAT volume needs " + std::to_string(need) +
              " bytes but the file is " + std::to_string(file_bytes);
        return false;
    }
    lba = vol_lba;
    sectors = total;
    return true;
}
