#include "core/sd_rom_extractor.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Internal helpers — pure-host FAT32-LBA parser. Read-only, short-name
// lookup, MBR + BPB + FAT chain + directory walk. ~300 LOC.
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

// FAT32 BPB extracted into host-friendly form.
struct Fat32Geom {
    uint32_t partition_lba_start = 0; // sector index of partition start
    uint16_t bytes_per_sector    = 0;
    uint8_t  sectors_per_cluster = 0;
    uint16_t reserved_sectors    = 0;
    uint8_t  num_fats            = 0;
    uint32_t fat_size_sectors    = 0;
    uint32_t root_cluster        = 0;
    // Derived:
    uint32_t fat_start_lba       = 0; // == partition_lba_start + reserved
    uint32_t data_start_lba      = 0; // partition_lba_start + reserved + n*fat_size
    uint32_t bytes_per_cluster   = 0;
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

// Parse the MBR partition table and locate the first FAT32-LBA partition.
// Returns the LBA of the partition's first sector, or 0 on failure.
// (LBA 0 is never a valid partition start since the MBR itself sits there.)
uint32_t find_fat32_partition_lba(std::ifstream& f) {
    uint8_t mbr[512];
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(mbr), 512);
    if (!f.good() || f.gcount() != 512) {
        Log::emulator()->error("sd_rom_extractor: failed to read MBR (image too small or unreadable)");
        return 0;
    }
    // MBR boot signature must be 0x55 0xAA at bytes 510-511.
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        Log::emulator()->error("sd_rom_extractor: invalid MBR signature (expected 0x55 0xAA at offset 0x1FE)");
        return 0;
    }
    // Partition table: 4 entries × 16 bytes starting at offset 0x1BE.
    for (int i = 0; i < 4; ++i) {
        const uint8_t* pe   = mbr + 0x1BE + i * 16;
        const uint8_t  type = pe[4];
        const uint32_t lba  = rd_u32(pe + 8);
        // 0x0B = FAT32 CHS, 0x0C = FAT32 LBA. TBBlue uses 0x0C.
        if ((type == 0x0B || type == 0x0C) && lba > 0) {
            return lba;
        }
    }
    Log::emulator()->error("sd_rom_extractor: no FAT32-LBA partition found in MBR");
    return 0;
}

// Parse the BPB at the partition's first sector and validate.
bool parse_bpb(std::ifstream& f, uint32_t partition_lba, Fat32Geom& g) {
    uint8_t bpb[512];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(partition_lba) * 512),
            std::ios::beg);
    f.read(reinterpret_cast<char*>(bpb), 512);
    if (!f.good() || f.gcount() != 512) {
        Log::emulator()->error("sd_rom_extractor: failed to read BPB at LBA {}", partition_lba);
        return false;
    }

    g.partition_lba_start = partition_lba;
    g.bytes_per_sector    = rd_u16(bpb + 11);
    g.sectors_per_cluster = bpb[13];
    g.reserved_sectors    = rd_u16(bpb + 14);
    g.num_fats            = bpb[16];
    // For FAT32, the 16-bit FAT size at offset 22 must be 0; the real
    // size is at offset 36 (FATSz32).
    g.fat_size_sectors    = rd_u32(bpb + 36);
    g.root_cluster        = rd_u32(bpb + 44);

    // Sanity: bytes_per_sector must be one of {512, 1024, 2048, 4096}.
    if (g.bytes_per_sector != 512 && g.bytes_per_sector != 1024 &&
        g.bytes_per_sector != 2048 && g.bytes_per_sector != 4096) {
        Log::emulator()->error("sd_rom_extractor: invalid bytes_per_sector={}", g.bytes_per_sector);
        return false;
    }
    // sectors_per_cluster must be a power of 2 in [1,128].
    if (g.sectors_per_cluster == 0 ||
        (g.sectors_per_cluster & (g.sectors_per_cluster - 1)) != 0) {
        Log::emulator()->error("sd_rom_extractor: invalid sectors_per_cluster={}", g.sectors_per_cluster);
        return false;
    }
    if (g.num_fats == 0 || g.fat_size_sectors == 0 || g.root_cluster < 2) {
        Log::emulator()->error("sd_rom_extractor: invalid FAT32 BPB (num_fats={}, fat_size={}, root_cluster={})",
                               g.num_fats, g.fat_size_sectors, g.root_cluster);
        return false;
    }

    g.fat_start_lba     = g.partition_lba_start + g.reserved_sectors;
    g.data_start_lba    = g.fat_start_lba +
                          static_cast<uint32_t>(g.num_fats) * g.fat_size_sectors;
    g.bytes_per_cluster = static_cast<uint32_t>(g.sectors_per_cluster) *
                          g.bytes_per_sector;
    return true;
}

// LBA of the first sector of cluster N (N >= 2).
uint32_t cluster_first_lba(const Fat32Geom& g, uint32_t cluster) {
    return g.data_start_lba +
           (cluster - 2) * static_cast<uint32_t>(g.sectors_per_cluster);
}

// Read FAT32 entry for cluster N. Returns next-cluster value (masked to
// 28 bits). Returns 0 on read error.
uint32_t fat_next(std::ifstream& f, const Fat32Geom& g, uint32_t cluster) {
    const uint64_t fat_byte_offset =
        static_cast<uint64_t>(g.fat_start_lba) * g.bytes_per_sector +
        static_cast<uint64_t>(cluster) * 4;
    uint8_t entry[4];
    f.seekg(static_cast<std::streamoff>(fat_byte_offset), std::ios::beg);
    f.read(reinterpret_cast<char*>(entry), 4);
    if (!f.good() || f.gcount() != 4) {
        Log::emulator()->error("sd_rom_extractor: failed to read FAT entry for cluster {}", cluster);
        return 0;
    }
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
bool find_in_directory(std::ifstream& f, const Fat32Geom& g,
                       uint32_t start_cluster,
                       const std::array<char, 11>& key,
                       uint32_t& first_cluster_out,
                       uint32_t& file_size_out,
                       bool& is_dir_out) {
    uint32_t cluster = start_cluster;
    std::vector<uint8_t> buf;
    while (cluster >= 2 && cluster < FAT32_BAD_MARK) {
        if (!read_cluster(f, g, cluster, buf)) return false;
        for (uint32_t off = 0; off + 32 <= buf.size(); off += 32) {
            const uint8_t* e = buf.data() + off;
            const uint8_t  first = e[0];
            const uint8_t  attr  = e[11];
            if (first == 0x00) {
                // End of directory marker — no more entries beyond.
                return false;
            }
            if (first == 0xE5) continue;            // deleted
            if (attr == ATTR_LFN) continue;         // skip LFN slots
            if (attr & ATTR_VOLUME_ID) continue;    // skip volume label
            if (!match_sfn(e, key)) continue;

            const uint16_t hi = rd_u16(e + 20);
            const uint16_t lo = rd_u16(e + 26);
            first_cluster_out = (static_cast<uint32_t>(hi) << 16) |
                                static_cast<uint32_t>(lo);
            file_size_out     = rd_u32(e + 28);
            is_dir_out        = (attr & ATTR_DIRECTORY) != 0;
            return true;
        }
        // Advance to next cluster in chain.
        const uint32_t nxt = fat_next(f, g, cluster);
        if (nxt == 0 || nxt >= FAT32_EOC_MARK) return false;
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

    // Locate FAT32 partition via MBR.
    const uint32_t part_lba = find_fat32_partition_lba(f);
    if (part_lba == 0) {
        // find_fat32_partition_lba already logged the reason.
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
        if (cluster < 2 || cluster >= FAT32_BAD_MARK) {
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
        if (nxt < 2 || nxt >= FAT32_EOC_MARK) {
            Log::emulator()->error("sd_rom_extractor: premature EOC at cluster {} for '{}' (got {} bytes, need {} more)",
                                   cluster, sd_path, file_size - remaining, remaining);
            return false;
        }
        cluster = nxt;
    }

    if (bytes_read_out) *bytes_read_out = file_size;
    return true;
}
