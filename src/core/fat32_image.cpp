#include "core/fat32_image.h"
#include "core/log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>

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
inline void wr_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void wr_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

constexpr uint8_t ATTR_VOLUME_ID = 0x08;
constexpr uint8_t ATTR_DIRECTORY = 0x10;
constexpr uint8_t ATTR_LFN       = 0x0F;
constexpr uint32_t FAT32_EOC     = 0x0FFFFFFFu;
constexpr uint32_t FAT32_EOC_MIN = 0x0FFFFFF8u;
constexpr uint32_t FAT32_MASK    = 0x0FFFFFFFu;

} // namespace

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
Fat32FormatGeom fat32_compute_geometry(uint32_t total_sectors,
                                       uint8_t  sectors_per_cluster,
                                       uint16_t bytes_per_sector,
                                       uint8_t  num_fats,
                                       uint16_t reserved_sectors) {
    Fat32FormatGeom g;
    g.total_sectors       = total_sectors;
    g.bytes_per_sector    = bytes_per_sector;
    g.sectors_per_cluster = sectors_per_cluster;
    g.num_fats            = num_fats;
    g.reserved_sectors    = reserved_sectors;

    if (sectors_per_cluster == 0 || bytes_per_sector == 0 || num_fats == 0)
        return g; // valid == false

    const uint32_t entries_per_sector = bytes_per_sector / 4; // FAT32: 4 bytes/entry

    // Iteratively size the FAT: the FAT must hold one 32-bit entry per data
    // cluster (plus 2 reserved entries). Reducing the data region to make
    // room for the FAT can lower the cluster count, which lowers the needed
    // FAT size, so iterate to a fixed point. Same approach as FatFs f_mkfs.
    uint32_t fat_size = 1;
    for (int iter = 0; iter < 64; ++iter) {
        if (total_sectors <= reserved_sectors +
                                  static_cast<uint32_t>(num_fats) * fat_size)
            return g; // partition too small

        const uint32_t data_sectors =
            total_sectors - reserved_sectors -
            static_cast<uint32_t>(num_fats) * fat_size;
        const uint32_t clusters = data_sectors / sectors_per_cluster;
        // FAT needs entries for clusters 0..clusters+1.
        const uint32_t need_entries = clusters + 2;
        const uint32_t need_fat_size =
            (need_entries + entries_per_sector - 1) / entries_per_sector;
        if (need_fat_size == fat_size) {
            g.fat_size_sectors = fat_size;
            g.cluster_count    = clusters;
            g.valid            = true;
            g.is_fat32         = clusters > 65525u;
            return g;
        }
        fat_size = need_fat_size;
    }
    // Did not converge (should not happen for sane inputs).
    return g;
}

// ---------------------------------------------------------------------------
// Reader — recursive directory tree read (with VFAT LFN reconstruction)
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

// ---------------------------------------------------------------------------
// Writer — fresh FAT32 volume with VFAT LFN
// ---------------------------------------------------------------------------
namespace {

// A cleaned, uppercased 8.3 short name (11 bytes, space padded) plus flags.
struct ShortName {
    std::array<uint8_t, 11> sfn{};
    uint8_t nt_flags = 0;   // 0x08 lowercase base, 0x10 lowercase ext
    bool    needs_lfn = false; // an LFN is required to preserve the display name
    bool    is_natural = false; // sfn is the natural uppercase 8.3 (no ~N needed)
};

bool is_sfn_char(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u < 0x20) return false;
    // Disallowed 8.3 punctuation.
    static const char* bad = "\"*+,./:;<=>?[\\]|";
    return std::strchr(bad, c) == nullptr;
}

char up(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

// Decide whether `name` fits a lossless 8.3 (with NT case flags) or needs LFN,
// and build the natural short name. Numeric-tail (~N) mangling is applied by
// the caller for uniqueness / long names.
ShortName natural_shortname(const std::string& name) {
    ShortName sn;
    sn.sfn.fill(' ');
    std::string base, ext;
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) { base = name; }
    else { base = name.substr(0, dot); ext = name.substr(dot + 1); }

    bool ok = !base.empty() && base.size() <= 8 && ext.size() <= 3 &&
              name.find('.') == name.find_last_of('.'); // at most one dot
    for (char c : base) if (!is_sfn_char(c) || c == ' ') ok = false;
    for (char c : ext)  if (!is_sfn_char(c) || c == ' ') ok = false;

    if (ok) {
        // The natural uppercase 8.3 IS the short name (preserved so the
        // emulator's SFN-based ROM lookups keep working). Whether we ALSO
        // need an LFN depends only on faithful case reconstruction.
        bool base_lower = false, base_upper = false, ext_lower = false, ext_upper = false;
        for (char c : base) { if (std::islower((unsigned char)c)) base_lower = true;
                              if (std::isupper((unsigned char)c)) base_upper = true; }
        for (char c : ext)  { if (std::islower((unsigned char)c)) ext_lower = true;
                              if (std::isupper((unsigned char)c)) ext_upper = true; }
        for (size_t i = 0; i < base.size(); ++i) sn.sfn[i]     = static_cast<uint8_t>(up(base[i]));
        for (size_t i = 0; i < ext.size();  ++i) sn.sfn[8 + i] = static_cast<uint8_t>(up(ext[i]));
        sn.is_natural = true;
        const bool base_mixed = base_lower && base_upper;
        const bool ext_mixed  = ext_lower && ext_upper;
        if (base_mixed || ext_mixed) {
            // Mixed case cannot be captured by the two NT flags → need LFN.
            sn.needs_lfn = true;
        } else {
            if (base_lower) sn.nt_flags |= 0x08;
            if (ext_lower)  sn.nt_flags |= 0x10;
            sn.needs_lfn = false;
        }
        return sn;
    }

    // Name does not fit 8.3 (too long / invalid chars) → mangled ~N + LFN.
    sn.needs_lfn = true;
    sn.is_natural = false;

    // Build a mangled base (uppercased, invalid chars → '_') for the ~N form.
    std::string cbase, cext;
    for (char c : base) { if (c == ' ' || c == '.') continue;
                          cbase.push_back(is_sfn_char(c) ? up(c) : '_'); }
    for (char c : ext)  { if (c == ' ') continue;
                          cext.push_back(is_sfn_char(c) ? up(c) : '_'); }
    if (cbase.empty()) cbase = "_";
    if (cbase.size() > 8) cbase.resize(8);
    if (cext.size()  > 3) cext.resize(3);
    for (size_t i = 0; i < cbase.size(); ++i) sn.sfn[i]     = static_cast<uint8_t>(cbase[i]);
    for (size_t i = 0; i < cext.size();  ++i) sn.sfn[8 + i] = static_cast<uint8_t>(cext[i]);
    return sn;
}

// Apply a "~N" numeric tail to the base of a short name, keeping the ext.
void apply_numeric_tail(ShortName& sn, const std::string& name, int n) {
    // Recompute cleaned base/ext.
    std::string base, ext;
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) { base = name; }
    else { base = name.substr(0, dot); ext = name.substr(dot + 1); }
    std::string cbase, cext;
    for (char c : base) { if (c == ' ' || c == '.') continue;
                          cbase.push_back(is_sfn_char(c) ? up(c) : '_'); }
    for (char c : ext)  { if (c == ' ') continue;
                          cext.push_back(is_sfn_char(c) ? up(c) : '_'); }
    if (cbase.empty()) cbase = "_";
    if (cext.size() > 3) cext.resize(3);

    std::string tail = "~" + std::to_string(n);
    if (cbase.size() + tail.size() > 8) cbase.resize(8 - tail.size());
    std::string b = cbase + tail;

    sn.sfn.fill(' ');
    for (size_t i = 0; i < b.size() && i < 8; ++i) sn.sfn[i] = static_cast<uint8_t>(b[i]);
    for (size_t i = 0; i < cext.size() && i < 3; ++i) sn.sfn[8 + i] = static_cast<uint8_t>(cext[i]);
    sn.nt_flags = 0;
}

uint8_t sfn_checksum(const std::array<uint8_t, 11>& sfn) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; ++i)
        sum = static_cast<uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i]);
    return sum;
}

// Number of 32-byte dir slots an entry consumes (SFN + LFN slots).
size_t entry_slots(const ShortName& sn, const std::string& name) {
    if (!sn.needs_lfn) return 1;
    size_t lfn = (name.size() + 12) / 13; // ceil(len/13)
    return lfn + 1;
}

// Fixed timestamp: 2024-01-01 00:00:00 (avoids zero-date quirks).
constexpr uint16_t FIXED_DATE = ((2024 - 1980) << 9) | (1 << 5) | 1;
constexpr uint16_t FIXED_TIME = 0;

// A node annotated with its allocation.
struct Alloc {
    const Fat32Node* node = nullptr; // null for synthetic root
    uint32_t first_cluster = 0;      // 0 == empty
    uint32_t cluster_count = 0;      // clusters spanned (contiguous)
    ShortName sname;
    std::vector<Alloc> children;     // for dirs
};

uint32_t bytes_to_clusters(uint64_t bytes, uint32_t bpc) {
    return static_cast<uint32_t>((bytes + bpc - 1) / bpc);
}

// Compute the number of directory-entry bytes a directory needs, and assign
// unique short names to its children.
uint32_t dir_size_bytes(const std::vector<Fat32Node>& children, bool has_dotdot,
                        std::vector<ShortName>& snames_out) {
    size_t slots = has_dotdot ? 2 : 0; // "." and ".."
    std::set<std::array<uint8_t, 11>> used;
    if (has_dotdot) {
        std::array<uint8_t, 11> dot{};   dot.fill(' ');   dot[0] = '.';
        std::array<uint8_t, 11> dotdot{}; dotdot.fill(' '); dotdot[0] = '.'; dotdot[1] = '.';
        used.insert(dot); used.insert(dotdot);
    }
    snames_out.clear();
    snames_out.reserve(children.size());
    for (const auto& c : children) {
        ShortName sn = natural_shortname(c.name);
        // Mangle to a "~N" short name only when the natural 8.3 is
        // unavailable (name too long / invalid) or collides with an already
        // assigned short name in this directory. A name that merely needs an
        // LFN for case (e.g. "enNxtmmc.rom") keeps its natural SFN
        // "ENNXTMMC.ROM" so SFN-based ROM lookups still resolve it.
        if (!sn.is_natural || used.count(sn.sfn)) {
            int n = 1;
            do { apply_numeric_tail(sn, c.name, n); ++n; }
            while (used.count(sn.sfn) && n < 1000000);
            sn.needs_lfn = true;
        }
        used.insert(sn.sfn);
        slots += entry_slots(sn, c.name);
        snames_out.push_back(sn);
    }
    slots += 1; // end-of-directory 0x00 marker
    return static_cast<uint32_t>(slots * 32);
}

// Recursively allocate clusters for a directory node and its subtree.
// `next` is the running cluster allocator (starts at 2 for the root).
// `is_root` selects whether "." / ".." are present.
void allocate(Alloc& a, const std::vector<Fat32Node>& children, bool is_root,
              uint32_t parent_cluster, uint32_t bpc, uint32_t& next) {
    std::vector<ShortName> snames;
    const uint32_t dir_bytes = dir_size_bytes(children, !is_root, snames);
    const uint32_t dir_clusters = std::max<uint32_t>(1, bytes_to_clusters(dir_bytes, bpc));

    a.first_cluster = next;
    a.cluster_count = dir_clusters;
    next += dir_clusters;

    a.children.resize(children.size());
    for (size_t i = 0; i < children.size(); ++i) {
        Alloc& ca = a.children[i];
        ca.node  = &children[i];
        ca.sname = snames[i];
        if (children[i].is_dir) {
            allocate(ca, children[i].children, /*is_root=*/false,
                     a.first_cluster, bpc, next);
        } else {
            const uint64_t sz = children[i].data.size();
            if (sz == 0) { ca.first_cluster = 0; ca.cluster_count = 0; }
            else {
                ca.first_cluster = next;
                ca.cluster_count = bytes_to_clusters(sz, bpc);
                next += ca.cluster_count;
            }
        }
    }
    (void)parent_cluster;
}

// Emit one entry's raw bytes (LFN slots + SFN) into `buf`.
void emit_entry(std::vector<uint8_t>& buf, const Alloc& ca, uint32_t parent_root) {
    const std::string& name = ca.node->name;
    const bool is_dir = ca.node->is_dir;
    const uint32_t sz = is_dir ? 0 : static_cast<uint32_t>(ca.node->data.size());

    if (ca.sname.needs_lfn) {
        const uint8_t csum = sfn_checksum(ca.sname.sfn);
        const size_t n = (name.size() + 12) / 13;
        // Emit slots highest-sequence first.
        for (size_t s = n; s >= 1; --s) {
            uint8_t e[32];
            std::memset(e, 0, 32);
            e[0] = static_cast<uint8_t>(s | (s == n ? 0x40 : 0));
            e[11] = ATTR_LFN;
            e[13] = csum;
            static const int idx[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            for (int k = 0; k < 13; ++k) {
                size_t ci = (s - 1) * 13 + k;
                uint16_t u;
                if (ci < name.size())      u = static_cast<uint8_t>(name[ci]);
                else if (ci == name.size()) u = 0x0000; // NUL terminator
                else                        u = 0xFFFF; // padding
                wr_u16(e + idx[k], u);
            }
            buf.insert(buf.end(), e, e + 32);
            if (s == 1) break; // avoid size_t underflow
        }
    }

    uint8_t e[32];
    std::memset(e, 0, 32);
    std::memcpy(e, ca.sname.sfn.data(), 11);
    e[11] = is_dir ? ATTR_DIRECTORY : 0x00;
    e[12] = ca.sname.nt_flags;
    wr_u16(e + 14, FIXED_TIME);            // create time
    wr_u16(e + 16, FIXED_DATE);            // create date
    wr_u16(e + 18, FIXED_DATE);            // access date
    wr_u16(e + 20, static_cast<uint16_t>(ca.first_cluster >> 16));
    wr_u16(e + 22, FIXED_TIME);            // write time
    wr_u16(e + 24, FIXED_DATE);            // write date
    wr_u16(e + 26, static_cast<uint16_t>(ca.first_cluster & 0xFFFF));
    wr_u32(e + 28, sz);
    buf.insert(buf.end(), e, e + 32);
    (void)parent_root;
}

// Build a directory's raw byte image (dot/dotdot + children entries).
std::vector<uint8_t> build_dir_bytes(const Alloc& a, bool is_root,
                                     uint32_t parent_cluster) {
    std::vector<uint8_t> buf;
    if (!is_root) {
        uint8_t e[32];
        // "."
        std::memset(e, 0, 32);
        std::memset(e, ' ', 11); e[0] = '.';
        e[11] = ATTR_DIRECTORY;
        wr_u16(e + 16, FIXED_DATE); wr_u16(e + 24, FIXED_DATE); wr_u16(e + 18, FIXED_DATE);
        wr_u16(e + 20, static_cast<uint16_t>(a.first_cluster >> 16));
        wr_u16(e + 26, static_cast<uint16_t>(a.first_cluster & 0xFFFF));
        buf.insert(buf.end(), e, e + 32);
        // ".." (0 when parent is root)
        std::memset(e, 0, 32);
        std::memset(e, ' ', 11); e[0] = '.'; e[1] = '.';
        e[11] = ATTR_DIRECTORY;
        wr_u16(e + 16, FIXED_DATE); wr_u16(e + 24, FIXED_DATE); wr_u16(e + 18, FIXED_DATE);
        wr_u16(e + 20, static_cast<uint16_t>(parent_cluster >> 16));
        wr_u16(e + 26, static_cast<uint16_t>(parent_cluster & 0xFFFF));
        buf.insert(buf.end(), e, e + 32);
    }
    for (const auto& ca : a.children)
        emit_entry(buf, ca, 0);
    return buf;
}

} // namespace

bool fat32_write_volume(const std::string& image_path, uint32_t part_lba,
                        const Fat32FormatGeom& geom, const Fat32Tree& tree,
                        std::string& err) {
    if (!geom.valid) { err = "invalid geometry"; return false; }
    const uint32_t bps = geom.bytes_per_sector;
    const uint32_t bpc = geom.sectors_per_cluster * bps;

    // 1. Allocate clusters for the whole tree (root at cluster 2).
    Alloc root;
    uint32_t next = 2;
    allocate(root, tree.root, /*is_root=*/true, 0, bpc, next);
    const uint32_t used_clusters = next - 2;
    if (used_clusters > geom.cluster_count) {
        err = "tree does not fit: needs " + std::to_string(used_clusters) +
              " clusters, volume has " + std::to_string(geom.cluster_count);
        return false;
    }

    // 2. Build the FAT (in memory). Each node occupies a contiguous run, so
    //    its chain is n -> n+1 -> ... -> EOC.
    const uint32_t fat_entries = geom.fat_size_sectors * bps / 4;
    std::vector<uint32_t> fat(fat_entries, 0);
    fat[0] = 0x0FFFFFF8u;  // media descriptor in low byte
    fat[1] = 0x0FFFFFFFu;  // EOC / clean-shutdown flags

    auto chain_run = [&](uint32_t first, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t cl = first + i;
            if (cl >= fat_entries) return;
            fat[cl] = (i + 1 < count) ? (cl + 1) : FAT32_EOC;
        }
    };

    // Open image for read/write (in place; do not truncate).
    std::fstream f(image_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) { err = "cannot open image for writing: " + image_path; return false; }

    auto write_at = [&](uint64_t byte_off, const uint8_t* data, size_t len) -> bool {
        f.seekp(static_cast<std::streamoff>(byte_off), std::ios::beg);
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
        return f.good();
    };

    const uint64_t part_byte = static_cast<uint64_t>(part_lba) * bps;
    const uint32_t fat_start_lba = part_lba + geom.reserved_sectors;
    const uint32_t data_start_lba =
        fat_start_lba + geom.num_fats * geom.fat_size_sectors;
    auto cluster_byte = [&](uint32_t cl) -> uint64_t {
        return static_cast<uint64_t>(data_start_lba + (cl - 2) * geom.sectors_per_cluster) * bps;
    };

    // 3. Write directory + file data, and fill FAT chains. Depth-first.
    bool ok = true;
    std::function<void(const Alloc&, bool, uint32_t)> write_node =
        [&](const Alloc& a, bool is_root, uint32_t parent_cluster) {
            if (!ok) return;
            // Directory bytes for this dir.
            chain_run(a.first_cluster, a.cluster_count);
            std::vector<uint8_t> dbytes = build_dir_bytes(a, is_root, parent_cluster);
            dbytes.resize(static_cast<size_t>(a.cluster_count) * bpc, 0);
            if (!write_at(cluster_byte(a.first_cluster), dbytes.data(), dbytes.size()))
                { ok = false; return; }
            for (const auto& ca : a.children) {
                if (!ok) return;
                if (ca.node->is_dir) {
                    write_node(ca, false, a.first_cluster);
                } else if (ca.first_cluster >= 2) {
                    chain_run(ca.first_cluster, ca.cluster_count);
                    std::vector<uint8_t> fb = ca.node->data;
                    fb.resize(static_cast<size_t>(ca.cluster_count) * bpc, 0);
                    if (!write_at(cluster_byte(ca.first_cluster), fb.data(), fb.size()))
                        { ok = false; return; }
                }
            }
        };
    write_node(root, true, 0);
    if (!ok) { err = "write error in data region"; return false; }

    // 4. Write both FATs.
    std::vector<uint8_t> fatbytes(static_cast<size_t>(geom.fat_size_sectors) * bps, 0);
    for (uint32_t i = 0; i < fat_entries; ++i)
        wr_u32(fatbytes.data() + i * 4, fat[i] & FAT32_MASK);
    for (uint8_t n = 0; n < geom.num_fats; ++n) {
        const uint64_t off =
            static_cast<uint64_t>(fat_start_lba + n * geom.fat_size_sectors) * bps;
        if (!write_at(off, fatbytes.data(), fatbytes.size()))
            { err = "write error in FAT region"; return false; }
    }

    // 5. Boot sector (BPB).
    std::vector<uint8_t> boot(bps, 0);
    boot[0] = 0xEB; boot[1] = 0x58; boot[2] = 0x90;
    std::memcpy(boot.data() + 3, "MSDOS5.0", 8);
    wr_u16(boot.data() + 11, static_cast<uint16_t>(bps));
    boot[13] = geom.sectors_per_cluster;
    wr_u16(boot.data() + 14, geom.reserved_sectors);
    boot[16] = geom.num_fats;
    wr_u16(boot.data() + 17, 0);              // root entries (0 for FAT32)
    wr_u16(boot.data() + 19, 0);              // total sectors 16
    boot[21] = 0xF8;                          // media descriptor
    wr_u16(boot.data() + 22, 0);              // FAT size 16 (0 for FAT32)
    wr_u16(boot.data() + 24, 63);             // sectors per track
    wr_u16(boot.data() + 26, 255);            // heads
    wr_u32(boot.data() + 28, part_lba);       // hidden sectors
    wr_u32(boot.data() + 32, geom.total_sectors);
    wr_u32(boot.data() + 36, geom.fat_size_sectors);
    wr_u16(boot.data() + 40, 0);              // ext flags (mirrored FATs)
    wr_u16(boot.data() + 42, 0);              // fs version
    wr_u32(boot.data() + 44, 2);              // root cluster
    wr_u16(boot.data() + 48, 1);              // FSInfo sector
    wr_u16(boot.data() + 50, 6);              // backup boot sector
    boot[64] = 0x80;                          // drive number
    boot[66] = 0x29;                          // ext boot signature
    wr_u32(boot.data() + 67, 0x4A4E5854u);    // volume id ("JNXT")
    std::memcpy(boot.data() + 71, "NO NAME    ", 11);
    std::memcpy(boot.data() + 82, "FAT32   ", 8);
    boot[510] = 0x55; boot[511] = 0xAA;
    if (!write_at(part_byte, boot.data(), boot.size()))
        { err = "write error: boot sector"; return false; }
    // Backup boot sector at reserved sector 6.
    if (!write_at(part_byte + static_cast<uint64_t>(6) * bps, boot.data(), boot.size()))
        { err = "write error: backup boot"; return false; }

    // 6. FSInfo (sector 1) and its backup (sector 7).
    const uint32_t free_clusters = geom.cluster_count - used_clusters;
    std::vector<uint8_t> fsinfo(bps, 0);
    wr_u32(fsinfo.data() + 0, 0x41615252u);   // "RRaA"
    wr_u32(fsinfo.data() + 484, 0x61417272u); // "rrAa"
    wr_u32(fsinfo.data() + 488, free_clusters);
    wr_u32(fsinfo.data() + 492, next);        // next-free hint
    fsinfo[510] = 0x55; fsinfo[511] = 0xAA;
    if (!write_at(part_byte + static_cast<uint64_t>(1) * bps, fsinfo.data(), fsinfo.size()))
        { err = "write error: fsinfo"; return false; }
    if (!write_at(part_byte + static_cast<uint64_t>(7) * bps, fsinfo.data(), fsinfo.size()))
        { err = "write error: fsinfo backup"; return false; }

    f.flush();
    if (!f.good()) { err = "flush failed"; return false; }
    return true;
}
