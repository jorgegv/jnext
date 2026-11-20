#include "core/card_extract.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr uint8_t  kDrive    = 0;
constexpr uint64_t kMaxBytes = 48ull << 20;   // a program's directory: with kMaxEntries, bounds the page's memory and time
constexpr int      kMaxDepth = 32;
// Files and directories together. The cost is the reason, not the memory: FatFs opens a file by path, which
// rescans its directory from the start, so copying n entries of one directory takes n^2/2 comparisons (65,000
// files: 171 s, on the page's main thread; 4,096: about 1 s). The largest program directory of the distribution
// has 43 entries and all of /GAMES 210. It also stops a card that aliases one directory under many names.
constexpr uint64_t kMaxEntries = 4096;

// The card, attached to FatFs drive 0 and mounted for as long as this object lives. Nothing
// here modifies the volume, so nothing is flushed at unmount.
struct Mounted {
    FATFS fs{};
    bool  attached = false, mounted = false;

    bool open(const std::string& card, std::string& why) {
        uint32_t lba = 0;
        if (!fat32_find_partition(card, lba)) {
            why = "'" + card + "' has no FAT32 partition";
            return false;
        }
        uint8_t bpb[512];
        std::ifstream f(card, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(lba) * 512));
        f.read(reinterpret_cast<char*>(bpb), sizeof bpb);
        if (f.gcount() != static_cast<std::streamsize>(sizeof bpb) ||
            (bpb[11] | (bpb[12] << 8)) != 512) {
            why = "'" + card + "' has no readable FAT32 boot sector";
            return false;
        }
        const uint32_t total = bpb[32] | (bpb[33] << 8) | (bpb[34] << 16) | (static_cast<uint32_t>(bpb[35]) << 24);
        if (total == 0) { why = "'" + card + "' declares a zero-sector partition"; return false; }
        std::string err;
        if (!fatfs_glue::attach(kDrive, card, lba, total, err)) { why = "cannot open '" + card + "' (" + err + ")"; return false; }
        attached = true;
        if (f_mount(&fs, "0:", 1) != FR_OK) { why = "'" + card + "' is not a FAT32 filesystem"; return false; }
        mounted = true;
        return true;
    }
    ~Mounted() {
        if (mounted) f_mount(nullptr, "0:", 0);
        if (attached) fatfs_glue::detach(kDrive);
    }
};

bool ends_with_nex(const std::string& s) {
    if (s.size() < 4) return false;
    std::string t = s.substr(s.size() - 4);
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == ".nex";
}

// Copy the FatFs directory `src` (e.g. "0:/GAMES/Next/TX") into the new host directory `dst`.
bool copy_dir(const std::string& src, const fs::path& dst, int depth, uint64_t& bytes, uint64_t& entries, std::string& why) {
    if (depth > kMaxDepth) { why = "'" + src + "' is nested more than " + std::to_string(kMaxDepth) + " directories deep"; return false; }
    std::error_code ec;
    if (depth == 0) fs::create_directories(dst, ec); else fs::create_directory(dst, ec);
    if (ec) { why = "cannot create '" + dst.string() + "'"; return false; }
    DIR d;
    if (f_opendir(&d, src.c_str()) != FR_OK) { why = "cannot read the directory '" + src + "' on the card"; return false; }
    bool ok = true;
    for (;;) {
        FILINFO fi{};
        if (f_readdir(&d, &fi) != FR_OK) { why = "cannot read the directory '" + src + "' on the card"; ok = false; break; }
        if (fi.fname[0] == 0) break;
        const std::string name = fi.fname;
        if (name == "." || name == "..") continue;
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
            why = "'" + src + "/" + name + "' has a name that cannot be copied"; ok = false; break;
        }
        if (++entries > kMaxEntries) { why = "the directory holding the program has more than " + std::to_string(kMaxEntries) + " entries, at '" + src + "'"; ok = false; break; }
        const std::string from = src + "/" + name;
        if (fi.fattrib & AM_DIR) {
            if (!copy_dir(from, dst / name, depth + 1, bytes, entries, why)) { ok = false; break; }
            continue;
        }
        bytes += fi.fsize;
        if (bytes > kMaxBytes) { why = "the directory holding the program is over " + std::to_string(kMaxBytes >> 20) + " MiB at '" + from + "'"; ok = false; break; }
        FIL f;
        if (f_open(&f, from.c_str(), FA_READ) != FR_OK) { why = "cannot read '" + from + "' on the card"; ok = false; break; }
        std::ofstream out((dst / name).string(), std::ios::binary);
        std::vector<uint8_t> buf(64 * 1024);
        uint64_t left = fi.fsize;
        while (ok && left > 0) {
            UINT n = 0;
            const UINT want = static_cast<UINT>(left < buf.size() ? left : buf.size());
            if (f_read(&f, buf.data(), want, &n) != FR_OK || n != want) { why = "cannot read '" + from + "' on the card"; ok = false; break; }
            out.write(reinterpret_cast<const char*>(buf.data()), n);
            left -= n;
        }
        f_close(&f);
        out.close();
        if (ok && !out) { why = "cannot write '" + (dst / name).string() + "'"; ok = false; }
        if (!ok) break;
    }
    f_closedir(&d);
    return ok;
}

}  // namespace

namespace card_extract {

bool copy_program_dir(const std::string& card, const std::string& nex, const std::string& dest,
                      std::string& out_nex, std::string& why) {
    out_nex.clear();
    why.clear();
    if (nex.empty() || nex[0] != '/') { why = "'" + nex + "' is not an absolute path on the card"; return false; }
    if (!ends_with_nex(nex)) { why = "'" + nex + "' is not a .nex file"; return false; }
    const size_t slash = nex.rfind('/');
    if (slash == 0) { why = "'" + nex + "' is in the card's root: the program must be in a directory of its own"; return false; }
    std::error_code ec;
    if (fs::exists(dest, ec)) { why = "the destination '" + dest + "' already exists"; return false; }

    bool ok = false;
    {
        Mounted m;
        if (!m.open(card, why)) return false;
        FILINFO fi{};
        const std::string fat_nex = "0:" + nex;
        const FRESULT r = f_stat(fat_nex.c_str(), &fi);
        if (r != FR_OK) {
            why = "the card has no " + nex;
        } else if (fi.fattrib & AM_DIR) {
            why = "'" + nex + "' is a directory on the card, not a program";
        } else {
            const std::string fat_dir = "0:" + nex.substr(0, slash);
            uint64_t bytes = 0, entries = 0;
            ok = copy_dir(fat_dir, fs::path(dest), 0, bytes, entries, why);
            if (ok) out_nex = dest + "/" + fi.fname;
        }
    }
    if (!ok) fs::remove_all(dest, ec);
    return ok;
}

}  // namespace card_extract
