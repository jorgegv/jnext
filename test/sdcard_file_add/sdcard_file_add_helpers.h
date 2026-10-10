// Fixture plumbing of the --sdcard-file-add suites (GH #214); see
// sdcard_file_add_includes.h. Include AFTER the suite's own counters, check()
// and skip(). Moved here verbatim from sdcard_file_add_test.cpp.
#pragma once

namespace {

// ---------------------------------------------------------------------------
// Fixture plumbing
// ---------------------------------------------------------------------------

// Partition layout of every fixture image. 68000 sectors at 1 sector/cluster
// leaves ~66900 clusters, just over the 65525 FAT32 minimum FatFs enforces,
// for a 34.8 MB file that is entirely sparse until written.
constexpr uint32_t kPartLba      = 2048;
constexpr uint32_t kPartSectors  = 68000;
constexpr uint32_t kSectorSize   = 512;
constexpr uint32_t kClusterBytes = 512;   // au_size passed to f_mkfs

fs::path g_scratch;

void wr_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

bool write_host_file(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty())
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool read_host_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return true;
}

// Deterministic pseudo-random payload, so a byte-comparison failure is a real
// difference and not two runs of zeros agreeing by accident.
std::vector<uint8_t> payload(std::size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t s = seed * 2654435761u + 1u;
    for (std::size_t i = 0; i < n; ++i) {
        s = s * 1103515245u + 12345u;
        v[i] = static_cast<uint8_t>(s >> 16);
    }
    return v;
}

// 64-bit FNV-1a over a whole file; used to assert an image was NOT modified.
uint64_t file_digest(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return 0;
    uint64_t h = 1469598103934665603ull;
    std::vector<char> buf(1 << 20);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            h ^= static_cast<uint8_t>(buf[i]);
            h *= 1099511628211ull;
        }
    }
    return h;
}

// Write an MBR whose single entry is a type-0x0C FAT32-LBA partition.
bool write_mbr(const fs::path& image) {
    std::fstream f(image, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    uint8_t mbr[kSectorSize] = {};
    uint8_t* pe = mbr + 0x1BE;
    pe[0] = 0x00;                  // not bootable
    pe[1] = pe[2] = pe[3] = 0xFE;  // CHS start (ignored for LBA)
    pe[4] = 0x0C;                  // FAT32 LBA
    pe[5] = pe[6] = pe[7] = 0xFE;  // CHS end
    wr_u32(pe + 8,  kPartLba);
    wr_u32(pe + 12, kPartSectors);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    f.seekp(0, std::ios::beg);
    f.write(reinterpret_cast<const char*>(mbr), kSectorSize);
    return f.good();
}

// Create a fresh fixture image: MBR + a FAT32 volume holding
//   /README.TXT          (48 bytes)
//   /NEXTZXOS/KEEPME.BIN (3000 bytes, spans several 512-byte clusters)
// Both are read back after every write test, so a writer that corrupts the
// existing tree is caught rather than merely "not proven safe".
//
// f_mkfs is called here rather than through fat32_format_and_populate()
// because that helper pins 8 KB clusters, which would need a 537 MB fixture.
bool make_fixture(const fs::path& image, std::string& why) {
    std::error_code ec;
    fs::remove(image, ec);
    { std::ofstream create(image, std::ios::binary); if (!create) { why = "create"; return false; } }
    fs::resize_file(image,
                    static_cast<std::uintmax_t>(kPartLba + kPartSectors) * kSectorSize, ec);
    if (ec) { why = "resize: " + ec.message(); return false; }
    if (!write_mbr(image)) { why = "mbr"; return false; }

    std::string err;
    if (!fatfs_glue::attach(0, image.string(), kPartLba, kPartSectors, err)) {
        why = "attach: " + err;
        return false;
    }
    struct Detacher { ~Detacher() { fatfs_glue::detach(0); } } detacher;

    MKFS_PARM parm{};
    parm.fmt     = FM_FAT32 | FM_SFD;
    parm.n_fat   = 2;
    parm.align   = 0;
    parm.n_root  = 0;
    parm.au_size = kClusterBytes;
    std::vector<uint8_t> work(64 * 1024);
    if (f_mkfs("0:", &parm, work.data(), static_cast<UINT>(work.size())) != FR_OK) {
        why = "f_mkfs";
        return false;
    }
    FATFS fsobj{};
    if (f_mount(&fsobj, "0:", 1) != FR_OK) { why = "f_mount"; return false; }

    auto emit = [](const char* path, const std::vector<uint8_t>& data) -> bool {
        FIL fp{};
        if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
        UINT bw = 0;
        const bool ok = data.empty() ||
            (f_write(&fp, data.data(), static_cast<UINT>(data.size()), &bw) == FR_OK &&
             bw == data.size());
        return f_close(&fp) == FR_OK && ok;
    };
    bool ok = emit("0:/README.TXT", payload(48, 7));
    ok = ok && f_mkdir("0:/NEXTZXOS") == FR_OK;
    ok = ok && emit("0:/NEXTZXOS/KEEPME.BIN", payload(3000, 11));
    f_mount(nullptr, "0:", 0);
    if (!ok) { why = "populate"; return false; }
    return true;
}

// The two fixture files, re-read through the independent short-name reader
// after every mutation. `extract_sd_rom` is a different code path from the
// writer (its own MBR/BPB/FAT walk), so this is a real check that the write
// left the rest of the volume alone.
bool fixture_tree_intact(const fs::path& image) {
    std::vector<uint8_t> got;
    if (!extract_sd_rom(image.string(), "/README.TXT", got)) return false;
    if (got != payload(48, 7)) return false;
    if (!extract_sd_rom(image.string(), "/NEXTZXOS/KEEPME.BIN", got)) return false;
    return got == payload(3000, 11);
}

// Read the two FAT copies of a fixture image and report whether they agree.
// A writer that updates only FAT #1 passes every read-back test jnext can run
// (its readers all use FAT #1) and produces a card that fsck and any driver
// using the mirror will disagree with.
bool fats_agree(const fs::path& image, std::string& detail) {
    std::ifstream f(image, std::ios::binary);
    if (!f) { detail = "open"; return false; }
    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good()) { detail = "bpb"; return false; }
    const uint16_t reserved = static_cast<uint16_t>(bpb[14] | (bpb[15] << 8));
    const uint8_t  n_fats   = bpb[16];
    const uint32_t fat_sz   = static_cast<uint32_t>(bpb[36]) |
                              (static_cast<uint32_t>(bpb[37]) << 8) |
                              (static_cast<uint32_t>(bpb[38]) << 16) |
                              (static_cast<uint32_t>(bpb[39]) << 24);
    if (n_fats != 2 || fat_sz == 0) { detail = "geometry"; return false; }
    const uint64_t fat0 =
        (static_cast<uint64_t>(kPartLba) + reserved) * kSectorSize;
    const uint64_t fat1 = fat0 + static_cast<uint64_t>(fat_sz) * kSectorSize;
    const std::size_t len = static_cast<std::size_t>(fat_sz) * kSectorSize;
    std::vector<uint8_t> a(len), b(len);
    f.seekg(static_cast<std::streamoff>(fat0));
    f.read(reinterpret_cast<char*>(a.data()), static_cast<std::streamsize>(len));
    f.seekg(static_cast<std::streamoff>(fat1));
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(len));
    if (!f.good()) { detail = "read"; return false; }
    for (std::size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            detail = "first difference at FAT byte " + std::to_string(i);
            return false;
        }
    }
    return true;
}

// FSInfo free-cluster count (offset 488 of the FSInfo sector, whose number the
// BPB gives at offset 48). 0xFFFFFFFF means "unknown".
uint32_t fsinfo_free_count(const fs::path& image) {
    std::ifstream f(image, std::ios::binary);
    if (!f) return 0xFFFFFFFFu;
    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good()) return 0xFFFFFFFFu;
    const uint16_t fsinfo_sec = static_cast<uint16_t>(bpb[48] | (bpb[49] << 8));
    uint8_t sec[kSectorSize];
    f.seekg(static_cast<std::streamoff>(
        (static_cast<uint64_t>(kPartLba) + fsinfo_sec) * kSectorSize));
    f.read(reinterpret_cast<char*>(sec), kSectorSize);
    if (!f.good()) return 0xFFFFFFFFu;
    // Signatures 0x41615252 / 0x61417272 / 0xAA550000.
    if (sec[0] != 0x52 || sec[1] != 0x52 || sec[2] != 0x61 || sec[3] != 0x41)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>(sec[488]) |
           (static_cast<uint32_t>(sec[489]) << 8) |
           (static_cast<uint32_t>(sec[490]) << 16) |
           (static_cast<uint32_t>(sec[491]) << 24);
}

// Free clusters counted in FAT #1 itself, not taken from FSInfo. FSInfo is a
// HINT FatFs keeps; a cluster allocated and never freed again (a leak) would
// show as a FAT entry still in use, whatever the hint says.
uint32_t fat_scan_free(const fs::path& image) {
    std::ifstream f(image, std::ios::binary);
    if (!f) return 0xFFFFFFFFu;
    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kPartLba) * kSectorSize));
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good()) return 0xFFFFFFFFu;
    auto u32 = [&](int o) {
        return static_cast<uint32_t>(bpb[o]) | (static_cast<uint32_t>(bpb[o + 1]) << 8) |
               (static_cast<uint32_t>(bpb[o + 2]) << 16) |
               (static_cast<uint32_t>(bpb[o + 3]) << 24);
    };
    const uint32_t spc      = bpb[13];
    const uint32_t reserved = static_cast<uint32_t>(bpb[14] | (bpb[15] << 8));
    const uint32_t n_fats   = bpb[16];
    const uint32_t total    = u32(32);
    const uint32_t fat_sz   = u32(36);
    if (spc == 0) return 0xFFFFFFFFu;
    const uint32_t clusters = (total - reserved - n_fats * fat_sz) / spc;
    std::vector<uint8_t> fat(static_cast<std::size_t>(clusters + 2) * 4);
    f.seekg(static_cast<std::streamoff>(
        (static_cast<uint64_t>(kPartLba) + reserved) * kSectorSize));
    f.read(reinterpret_cast<char*>(fat.data()), static_cast<std::streamsize>(fat.size()));
    if (!f.good()) return 0xFFFFFFFFu;
    uint32_t n = 0;
    for (uint32_t c = 2; c < clusters + 2; ++c) {
        const uint8_t* e = fat.data() + c * 4;
        const uint32_t v = (static_cast<uint32_t>(e[0]) | (static_cast<uint32_t>(e[1]) << 8) |
                            (static_cast<uint32_t>(e[2]) << 16) |
                            (static_cast<uint32_t>(e[3]) << 24)) & 0x0FFFFFFFu;
        if (v == 0) ++n;
    }
    return n;
}

// The two free-cluster accounts agree: FSInfo (what FatFs last WROTE) and a
// scan of FAT #1 (what is really allocated). A cluster freed only in FatFs's
// memory and never flushed shows up as exactly this disagreement — the lost
// cluster fsck.vfat reclaims (GH #292 review round 3).
bool free_counts_agree(const fs::path& image) {
    const uint32_t a = fsinfo_free_count(image);
    return a != 0xFFFFFFFFu && a == fat_scan_free(image);
}

// Does `long_name` appear in the directory `dir` of the image, with `size`
// bytes? Uses fat32_read_tree, which reconstructs VFAT long names — the short
// name reader (extract_sd_rom) cannot see them at all.
bool tree_has(const fs::path& image, const std::string& dir,
              const std::string& long_name, std::size_t size) {
    uint32_t part = 0;
    if (!fat32_find_partition(image.string(), part)) return false;
    Fat32Tree tree;
    if (!fat32_read_tree(image.string(), part, tree)) return false;
    const std::vector<Fat32Node>* level = &tree.root;
    if (!dir.empty()) {
        const Fat32Node* found = nullptr;
        for (const auto& n : *level)
            if (n.is_dir && n.name == dir) { found = &n; break; }
        if (!found) return false;
        level = &found->children;
    }
    for (const auto& n : *level)
        if (!n.is_dir && n.name == long_name && n.data.size() == size) return true;
    return false;
}

// The node at `path` ("/A/B/c.txt", long names, exact case) in a tree read by
// fat32_read_tree, or nullptr. That reader is jnext's lenient hand-rolled one,
// NOT FatFs — so a tree written through FatFs is read back by a different
// implementation.
const Fat32Node* find_node(const Fat32Tree& tree, const std::string& path) {
    const std::vector<Fat32Node>* level = &tree.root;
    const Fat32Node* found = nullptr;
    std::string comp;
    std::vector<std::string> parts;
    for (char c : path) {
        if (c == '/') { if (!comp.empty()) parts.push_back(comp); comp.clear(); }
        else comp.push_back(c);
    }
    if (!comp.empty()) parts.push_back(comp);
    for (const std::string& want : parts) {
        found = nullptr;
        for (const auto& n : *level)
            if (n.name == want) { found = &n; break; }
        if (!found) return nullptr;
        level = &found->children;
    }
    return found;
}

bool read_tree(const fs::path& image, Fat32Tree& tree) {
    uint32_t part = 0;
    if (!fat32_find_partition(image.string(), part)) return false;
    tree = Fat32Tree{};
    return fat32_read_tree(image.string(), part, tree);
}

// Does the card hold `data` at `path`?
bool card_file_is(const fs::path& image, const std::string& path,
                  const std::vector<uint8_t>& data) {
    Fat32Tree tree;
    if (!read_tree(image, tree)) return false;
    const Fat32Node* n = find_node(tree, path);
    return n && !n->is_dir && n->data == data;
}

bool card_has(const fs::path& image, const std::string& path) {
    Fat32Tree tree;
    if (!read_tree(image, tree)) return false;
    return find_node(tree, path) != nullptr;
}

// The LOGICAL content of the whole card — every name, its kind and its bytes,
// in directory order — read by the independent fat32 reader. A copy that fails
// part-way cannot leave the card BYTE-identical (FAT marks a deleted entry
// 0xE5 and never shrinks a directory that grew), but it must leave this
// identical: nothing it made may remain.
void digest_nodes(const std::vector<Fat32Node>& nodes, uint64_t& h) {
    auto mix = [&h](const void* p, std::size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    for (const Fat32Node& n : nodes) {
        mix(n.is_dir ? "D" : "F", 1);
        mix(n.name.data(), n.name.size());
        const uint64_t sz = n.data.size();
        mix(&sz, sizeof sz);
        if (!n.data.empty()) mix(n.data.data(), n.data.size());
        mix("{", 1);
        digest_nodes(n.children, h);
        mix("}", 1);
    }
}

uint64_t tree_digest(const fs::path& image) {
    Fat32Tree tree;
    if (!read_tree(image, tree)) return 0;
    uint64_t h = 1469598103934665603ull;
    digest_nodes(tree.root, h);
    return h;
}

}  // namespace
