#include "core/fat32_image.h"
#include "core/log.h"

#include <cctype>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Little-endian byte helpers
// ---------------------------------------------------------------------------
namespace {

inline uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_LFN       = 0x0F;
constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8u;
constexpr uint32_t FAT32_MASK    = 0x0FFFFFFFu;

} // namespace

// ---------------------------------------------------------------------------
// Reader — recursive directory tree read (with VFAT LFN reconstruction).
//
// Lenient by design: the source image is the under-clustered distribution
// image that strict FatFs rejects. This reader parses the FAT32-shaped BPB and
// walks the cluster chains directly, so it succeeds where FatFs will not.
// ---------------------------------------------------------------------------
namespace {

struct ReadGeom {
    uint32_t part_lba            = 0;
    uint16_t bytes_per_sector    = 0;
    uint8_t  sectors_per_cluster = 0;
    uint16_t reserved_sectors    = 0;
    uint8_t  num_fats            = 0;
    uint32_t fat_size_sectors    = 0;
    uint32_t root_cluster        = 0;
    uint32_t fat_start_lba       = 0;
    uint32_t data_start_lba      = 0;
    uint32_t bytes_per_cluster   = 0;
};

bool parse_read_geom(std::ifstream& f, uint32_t part_lba, ReadGeom& g) {
    uint8_t bpb[512];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(part_lba) * 512),
            std::ios::beg);
    f.read(reinterpret_cast<char*>(bpb), 512);
    if (!f.good() || f.gcount() != 512) return false;

    g.part_lba            = part_lba;
    g.bytes_per_sector    = rd_u16(bpb + 11);
    g.sectors_per_cluster = bpb[13];
    g.reserved_sectors    = rd_u16(bpb + 14);
    g.num_fats            = bpb[16];
    g.fat_size_sectors    = rd_u32(bpb + 36);
    g.root_cluster        = rd_u32(bpb + 44);

    if (g.bytes_per_sector == 0 || g.sectors_per_cluster == 0 ||
        g.num_fats == 0 || g.fat_size_sectors == 0 || g.root_cluster < 2)
        return false;

    g.fat_start_lba   = part_lba + g.reserved_sectors;
    g.data_start_lba  = g.fat_start_lba +
                        static_cast<uint32_t>(g.num_fats) * g.fat_size_sectors;
    g.bytes_per_cluster = static_cast<uint32_t>(g.sectors_per_cluster) *
                          g.bytes_per_sector;
    return true;
}

uint32_t cluster_lba(const ReadGeom& g, uint32_t cl) {
    return g.data_start_lba + (cl - 2) * g.sectors_per_cluster;
}

bool read_cluster(std::ifstream& f, const ReadGeom& g, uint32_t cl,
                  std::vector<uint8_t>& buf) {
    buf.resize(g.bytes_per_cluster);
    const uint64_t off =
        static_cast<uint64_t>(cluster_lba(g, cl)) * g.bytes_per_sector;
    f.seekg(static_cast<std::streamoff>(off), std::ios::beg);
    f.read(reinterpret_cast<char*>(buf.data()), g.bytes_per_cluster);
    return f.good() && static_cast<uint32_t>(f.gcount()) == g.bytes_per_cluster;
}

uint32_t fat_next(std::ifstream& f, const ReadGeom& g, uint32_t cl) {
    const uint64_t off =
        static_cast<uint64_t>(g.fat_start_lba) * g.bytes_per_sector +
        static_cast<uint64_t>(cl) * 4;
    uint8_t e[4];
    f.seekg(static_cast<std::streamoff>(off), std::ios::beg);
    f.read(reinterpret_cast<char*>(e), 4);
    if (!f.good() || f.gcount() != 4) return 0;
    return rd_u32(e) & FAT32_MASK;
}

// Read a full cluster chain's raw bytes (bounded against cycles).
bool read_chain(std::ifstream& f, const ReadGeom& g, uint32_t start,
                std::vector<uint8_t>& out) {
    out.clear();
    const uint64_t max_clusters =
        (static_cast<uint64_t>(g.fat_size_sectors) * g.bytes_per_sector) / 4;
    uint32_t cl = start;
    uint64_t steps = 0;
    std::vector<uint8_t> buf;
    while (cl >= 2 && cl < FAT32_EOC_MIN) {
        if (++steps > max_clusters) return false; // cyclic FAT
        if (!read_cluster(f, g, cl, buf)) return false;
        out.insert(out.end(), buf.begin(), buf.end());
        cl = fat_next(f, g, cl);
        if (cl == 0) return false;
    }
    return true;
}

// Decode the display name from an SFN 8.3 field + NT case flags (byte 12).
std::string sfn_to_name(const uint8_t* e) {
    const uint8_t nt = e[12];
    std::string base, ext;
    for (int i = 0; i < 8; ++i) {
        uint8_t c = e[i];
        if (c == ' ') break;
        if (i == 0 && c == 0x05) c = 0xE5;
        if (nt & 0x08) c = static_cast<uint8_t>(std::tolower(c));
        base.push_back(static_cast<char>(c));
    }
    for (int i = 0; i < 3; ++i) {
        uint8_t c = e[8 + i];
        if (c == ' ') break;
        if (nt & 0x10) c = static_cast<uint8_t>(std::tolower(c));
        ext.push_back(static_cast<char>(c));
    }
    if (ext.empty()) return base;
    return base + "." + ext;
}

// Assemble one LFN slot's 13 UTF-16 code units into `chars` (low byte only).
void lfn_slot_chars(const uint8_t* e, std::string& chars) {
    static const int idx[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (int i = 0; i < 13; ++i) {
        uint16_t u = rd_u16(e + idx[i]);
        if (u == 0x0000 || u == 0xFFFF) break;
        chars.push_back(static_cast<char>(u & 0xFF));
    }
}

// Parse the directory whose cluster chain begins at `start_cluster`,
// filling `out` with its child nodes. Recurses into subdirectories.
bool read_directory(std::ifstream& f, const ReadGeom& g, uint32_t start_cluster,
                    std::vector<Fat32Node>& out, int depth) {
    if (depth > 64) return false; // runaway nesting guard
    std::vector<uint8_t> dir;
    if (!read_chain(f, g, start_cluster, dir)) return false;

    std::string lfn_acc; // accumulated long name (reversed slot order)
    bool have_lfn = false;

    for (size_t off = 0; off + 32 <= dir.size(); off += 32) {
        const uint8_t* e = dir.data() + off;
        const uint8_t first = e[0];
        const uint8_t attr  = e[11];
        if (first == 0x00) break;      // end of directory
        if (first == 0xE5) { lfn_acc.clear(); have_lfn = false; continue; }
        if (attr == ATTR_LFN) {
            // LFN slots precede their SFN, stored highest-sequence first.
            // Prepend each slot's chars to build the name in order.
            std::string slot;
            lfn_slot_chars(e, slot);
            lfn_acc = slot + lfn_acc;
            have_lfn = true;
            continue;
        }
        if (attr & ATTR_VOLUME_ID) { lfn_acc.clear(); have_lfn = false; continue; }

        // Regular SFN entry.
        std::string name = have_lfn && !lfn_acc.empty() ? lfn_acc : sfn_to_name(e);
        lfn_acc.clear();
        have_lfn = false;

        if (name == "." || name == "..") continue;

        const bool is_dir = (attr & ATTR_DIRECTORY) != 0;
        const uint32_t cl = (static_cast<uint32_t>(rd_u16(e + 20)) << 16) |
                            rd_u16(e + 26);
        const uint32_t size = rd_u32(e + 28);

        Fat32Node node;
        node.name   = name;
        node.is_dir = is_dir;

        if (is_dir) {
            if (cl >= 2) {
                if (!read_directory(f, g, cl, node.children, depth + 1))
                    return false;
            }
        } else if (size > 0) {
            if (cl < 2) return false;
            std::vector<uint8_t> raw;
            if (!read_chain(f, g, cl, raw)) return false;
            if (raw.size() < size) return false;
            node.data.assign(raw.begin(), raw.begin() + size);
        }
        out.push_back(std::move(node));
    }
    return true;
}

} // namespace

bool fat32_find_partition(const std::string& image_path, uint32_t& part_lba_out) {
    std::ifstream f(image_path, std::ios::binary);
    if (!f) return false;
    uint8_t mbr[512];
    f.read(reinterpret_cast<char*>(mbr), 512);
    if (!f.good() || f.gcount() != 512) return false;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return false;
    for (int i = 0; i < 4; ++i) {
        const uint8_t* pe = mbr + 0x1BE + i * 16;
        const uint8_t type = pe[4];
        const uint32_t lba = rd_u32(pe + 8);
        if ((type == 0x0B || type == 0x0C) && lba > 0) {
            part_lba_out = lba;
            return true;
        }
    }
    return false;
}

bool fat32_read_tree(const std::string& image_path, uint32_t part_lba,
                     Fat32Tree& out) {
    out.root.clear();
    std::ifstream f(image_path, std::ios::binary);
    if (!f) {
        Log::emulator()->error("fat32_read_tree: cannot open '{}'", image_path);
        return false;
    }
    ReadGeom g{};
    if (!parse_read_geom(f, part_lba, g)) {
        Log::emulator()->error("fat32_read_tree: invalid BPB in '{}'", image_path);
        return false;
    }
    if (!read_directory(f, g, g.root_cluster, out.root, 0)) {
        Log::emulator()->error("fat32_read_tree: failed to read directory tree");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tree upsert
// ---------------------------------------------------------------------------
namespace {
std::vector<std::string> split_path(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
} // namespace

void fat32_tree_upsert(Fat32Tree& tree, const std::string& path,
                       const std::vector<uint8_t>& data) {
    auto parts = split_path(path);
    if (parts.empty()) return;
    std::vector<Fat32Node>* level = &tree.root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        Fat32Node* dir = nullptr;
        for (auto& n : *level)
            if (n.is_dir && ieq(n.name, parts[i])) { dir = &n; break; }
        if (!dir) {
            Fat32Node nd; nd.name = parts[i]; nd.is_dir = true;
            level->push_back(std::move(nd));
            dir = &level->back();
        }
        level = &dir->children;
    }
    const std::string& fname = parts.back();
    for (auto& n : *level)
        if (!n.is_dir && ieq(n.name, fname)) { n.data = data; return; }
    Fat32Node nf; nf.name = fname; nf.is_dir = false; nf.data = data;
    level->push_back(std::move(nf));
}
