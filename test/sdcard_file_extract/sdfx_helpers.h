// Fixture plumbing of the --sdcard-file-extract suites (GH #557). Include AFTER the suite's own
// counters and check(). Cards are built at run time with FatFs f_mkfs (the firmware's own library,
// a FOREIGN writer for the reader under test only in the sense that the tests also poke hand-made
// directory entries into it) and raw byte pokes; never an image from a file, never a NextZXOS image.
#pragma once

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kLba = 2048;        // the volume of a partitioned card
constexpr uint32_t kFat32Sectors = 68000;   // 512-byte clusters: just over FAT32's 65525-cluster minimum
constexpr uint32_t kFat16Sectors = 8000;

fs::path g_dir;
int g_n = 0;

std::string tmp_name(const std::string& stem) {
    return (g_dir / (stem + "-" + std::to_string(++g_n))).string();
}

void put16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }

// Deterministic pseudo-random payload: a byte comparison that passes is not two runs of zeros agreeing.
Bytes payload(size_t n, uint32_t seed) {
    Bytes v(n);
    uint32_t s = seed * 2654435761u + 1u;
    for (size_t i = 0; i < n; ++i) {
        s = s * 1103515245u + 12345u;
        v[i] = static_cast<uint8_t>(s >> 16);
    }
    return v;
}

[[maybe_unused]] bool write_host_file(const fs::path& p, const Bytes& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return f.good();
}

Bytes read_host_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// A directory's whole tree: relative '/'-path -> bytes; a directory is its path plus '/' (empty bytes).
[[maybe_unused]] std::map<std::string, Bytes> tree_of(const fs::path& root) {
    std::map<std::string, Bytes> out;
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        std::string rel = fs::relative(e.path(), root).generic_string();
        if (e.is_directory()) out[rel + "/"] = {};
        else out[rel] = read_host_file(e.path());
    }
    return out;
}

// What a populate callback leaves behind for after the volume is unmounted: bytes to write at an
// absolute image offset (hand-made directory entries FatFs itself would never write).
struct Poke { uint64_t off; Bytes data; };
using Pokes = std::vector<Poke>;

bool apply_pokes(const std::string& path, const Pokes& pokes) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    for (const Poke& p : pokes) {
        f.seekp(static_cast<std::streamoff>(p.off));
        f.write(reinterpret_cast<const char*>(p.data.data()), static_cast<std::streamsize>(p.data.size()));
    }
    return f.good();
}

[[maybe_unused]] bool poke_at(const std::string& path, uint64_t off, const Bytes& data) { return apply_pokes(path, {{off, data}}); }

bool write_mbr(const std::string& path, uint8_t type, uint32_t lba) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;
    uint8_t mbr[512] = {0};
    uint8_t* pe = mbr + 0x1BE;
    pe[4] = type;
    put32(pe + 8, lba);
    put32(pe + 12, 1000);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    f.seekp(0);
    f.write(reinterpret_cast<const char*>(mbr), 512);
    return f.good();
}

bool fat_write(const std::string& path, const Bytes& data) {
    FIL fp{};
    if (f_open(&fp, path.c_str(), FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    UINT bw = 0;
    const bool ok = data.empty() ||
        (f_write(&fp, data.data(), static_cast<UINT>(data.size()), &bw) == FR_OK && bw == data.size());
    return f_close(&fp) == FR_OK && ok;
}

// First cluster of a file / directory on the mounted volume (0 on a failure).
[[maybe_unused]] uint32_t file_cluster(const std::string& path) {
    FIL fp{};
    if (f_open(&fp, path.c_str(), FA_READ) != FR_OK) return 0;
    const uint32_t c = fp.obj.sclust;
    f_close(&fp);
    return c;
}
[[maybe_unused]] uint32_t dir_cluster(const std::string& path) {
    DIR dj{};
    if (f_opendir(&dj, path.c_str()) != FR_OK) return 0;
    const uint32_t c = dj.obj.sclust;
    f_closedir(&dj);
    return c;
}

// A file of `n_clusters` 512-byte clusters whose chain is fragmented: a decoy file is written
// alternately, and FatFs hands out the next free cluster each time.
[[maybe_unused]] bool fat_write_fragmented(const std::string& path, const std::string& decoy, const Bytes& data) {
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

// Format a volume of `vol_sectors` at `vol_lba` (0 = partition-less) with FatFs and populate it.
// `fmt` is FM_FAT (FatFs picks FAT12 or FAT16 by cluster count) or FM_FAT32. `mbr_type` != 0 writes an
// MBR with one entry of that type at `vol_lba`. Returns the FatFs fs_type (FS_FAT12/16/32) or 0.
int make_card(const std::string& path, uint32_t vol_lba, uint32_t vol_sectors, BYTE fmt, uint32_t au,
              uint32_t n_root, uint8_t mbr_type,
              const std::function<bool(FATFS&, Pokes&)>& populate) {
    {
        std::ofstream c(path, std::ios::binary | std::ios::trunc);
        if (!c) return 0;
        c.seekp(static_cast<std::streamoff>(static_cast<uint64_t>(vol_lba + vol_sectors) * 512 - 1));
        const char z = 0;
        c.write(&z, 1);
        if (!c.good()) return 0;
    }
    Pokes pokes;
    int type = 0;
    {
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
        static FATFS fsobj;
        fsobj = FATFS{};
        if (f_mount(&fsobj, "0:", 1) != FR_OK) return 0;
        const bool ok = populate ? populate(fsobj, pokes) : true;
        type = ok ? fsobj.fs_type : 0;
        f_syncvol("0:");
        f_mount(nullptr, "0:", 0);
    }
    if (mbr_type && !write_mbr(path, mbr_type, vol_lba)) return 0;
    if (!pokes.empty()) {
        // the pokes carry volume-relative offsets: make them absolute
        for (Poke& p : pokes) p.off += static_cast<uint64_t>(vol_lba) * 512;
        if (!apply_pokes(path, pokes)) return 0;
    }
    return type;
}

// ---- hand-made directory entries (FAT specification, fatgen103 sections 6 and 7) ----

struct HandEnt {
    std::u16string lfn;      // empty = a short entry only
    std::string    sfn;      // the 11 bytes of the short name, space padded
    uint8_t        attr = 0x20;
    uint32_t       clust = 0;
    uint32_t       size = 0;
};

uint8_t sfn_checksum(const std::string& sfn11) {
    uint8_t sum = 0;
    for (char c : sfn11) sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + static_cast<uint8_t>(c));
    return sum;
}

// Entries for `ents`, starting at `slot` of the first sector of directory cluster `dir_clst`.
// `fs` is the mounted volume (database, csize); offsets are volume-relative (make_card makes them absolute).
[[maybe_unused]] void hand_dir(const FATFS& fs, uint32_t dir_clst, unsigned slot, const std::vector<HandEnt>& ents, Pokes& pokes) {
    Bytes raw;
    for (const HandEnt& e : ents) {
        if (!e.lfn.empty()) {
            const uint8_t sum = sfn_checksum(e.sfn);
            const size_t n = e.lfn.size();
            const size_t slots = (n + 12) / 13;
            for (size_t k = slots; k >= 1; --k) {
                uint8_t d[32] = {0};
                d[0] = static_cast<uint8_t>(k | (k == slots ? 0x40 : 0));
                d[11] = 0x0F;
                d[13] = sum;
                static const int pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                for (int i = 0; i < 13; ++i) {
                    const size_t idx = (k - 1) * 13 + static_cast<size_t>(i);
                    uint16_t ch = idx < n ? static_cast<uint16_t>(e.lfn[idx]) : (idx == n ? 0x0000 : 0xFFFF);
                    put16(d + pos[i], ch);
                }
                raw.insert(raw.end(), d, d + 32);
            }
        }
        uint8_t s[32] = {0};
        std::memcpy(s, e.sfn.data(), 11);
        s[11] = e.attr;
        put16(s + 20, static_cast<uint16_t>(e.clust >> 16));
        put16(s + 26, static_cast<uint16_t>(e.clust));
        put32(s + 28, e.size);
        raw.insert(raw.end(), s, s + 32);
    }
    const uint64_t sector = static_cast<uint64_t>(fs.database) + static_cast<uint64_t>(dir_clst - 2) * fs.csize;
    pokes.push_back({sector * 512 + static_cast<uint64_t>(slot) * 32, raw});
}

[[maybe_unused]] std::string sha_file(const std::string& p) { return sdcard::sha256_file(p); }

}  // namespace
