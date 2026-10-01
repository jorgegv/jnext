#include "core/sdcard_file_add.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint8_t  kDrive       = 0;        // FatFs volume "0:"
constexpr uint32_t kSectorSize  = 512;      // fatfs_diskio supports only 512
constexpr uint32_t kCopyChunk   = 256 * 1024;
// FAT32 stores a file size in 32 bits, so 4 GiB - 1 is the hard ceiling. A
// larger source is refused up front rather than after copying 4 GiB.
constexpr uint64_t kMaxFileSize = 0xFFFFFFFFull;

const char* fr_str(FRESULT r) {
    switch (r) {
        case FR_OK:                  return "FR_OK";
        case FR_DISK_ERR:            return "FR_DISK_ERR";
        case FR_INT_ERR:             return "FR_INT_ERR";
        case FR_NOT_READY:           return "FR_NOT_READY";
        case FR_NO_FILE:             return "FR_NO_FILE";
        case FR_NO_PATH:             return "FR_NO_PATH";
        case FR_INVALID_NAME:        return "FR_INVALID_NAME";
        case FR_DENIED:              return "FR_DENIED";
        case FR_EXIST:               return "FR_EXIST";
        case FR_INVALID_OBJECT:      return "FR_INVALID_OBJECT";
        case FR_WRITE_PROTECTED:     return "FR_WRITE_PROTECTED";
        case FR_INVALID_DRIVE:       return "FR_INVALID_DRIVE";
        case FR_NOT_ENABLED:         return "FR_NOT_ENABLED";
        case FR_NO_FILESYSTEM:       return "FR_NO_FILESYSTEM";
        case FR_MKFS_ABORTED:        return "FR_MKFS_ABORTED";
        case FR_TIMEOUT:             return "FR_TIMEOUT";
        case FR_LOCKED:              return "FR_LOCKED";
        case FR_NOT_ENOUGH_CORE:     return "FR_NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER:   return "FR_INVALID_PARAMETER";
        default:                     return "FR_?";
    }
}

// Little-endian reads from the BPB sector.
uint16_t rd_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}
uint32_t rd_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Characters FAT forbids in a name. '/' is the separator and is handled by the
// splitter; '\\' is listed here because FatFs would otherwise take it as a
// SECOND separator and quietly turn one name into a directory chain.
bool is_forbidden_name_char(char c) {
    return std::strchr("\"*:<>?\\|", c) != nullptr;
}

// Split on '/', dropping empty components so "/a/b", "a/b" and "a//b" agree.
std::vector<std::string> split_slash(const std::string& p) {
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

// Partition geometry needed to present the volume to FatFs.
struct PartitionInfo {
    uint32_t lba           = 0;
    uint32_t total_sectors = 0;
};

// Read the partition's BPB and take its sector count. Mirrors what the
// provisioner does before f_mkfs (sdcard_provisioner.cpp), and additionally
// rejects geometry fatfs_diskio cannot present (non-512-byte sectors) and a
// partition that does not fit inside the file.
bool read_partition_info(const std::string& image_path, PartitionInfo& out,
                         std::string& err) {
    // Openability first: fat32_find_partition() answers false for a missing
    // file and for a file with no partition table alike, and "no FAT32-LBA
    // partition in its MBR" is a confusing thing to be told about a path that
    // was simply mistyped.
    std::ifstream f(image_path, std::ios::binary);
    if (!f) {
        err = "cannot open SD image '" + image_path + "'";
        return false;
    }
    if (!fat32_find_partition(image_path, out.lba)) {
        err = "'" + image_path +
              "' has no FAT32-LBA partition in its MBR (not an SD-card image?)";
        return false;
    }
    f.seekg(0, std::ios::end);
    const uint64_t file_size = static_cast<uint64_t>(f.tellg());

    uint8_t bpb[kSectorSize];
    f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(out.lba) * kSectorSize),
            std::ios::beg);
    f.read(reinterpret_cast<char*>(bpb), kSectorSize);
    if (!f.good() || f.gcount() != static_cast<std::streamsize>(kSectorSize)) {
        err = "cannot read the FAT32 boot sector of '" + image_path +
              "' (image truncated?)";
        return false;
    }
    const uint16_t bytes_per_sector = rd_u16(bpb + 11);
    if (bytes_per_sector != kSectorSize) {
        err = "'" + image_path + "' uses " + std::to_string(bytes_per_sector) +
              "-byte sectors; jnext supports 512-byte sectors only";
        return false;
    }
    // FAT32 keeps the count in TotSec32 only: the spec requires TotSec16 (at
    // offset 19) to be zero on a FAT32 volume, so a value there would mean
    // this is not one. Reading it as a fallback was written and then removed —
    // it can only make an unmountable image look mountable.
    out.total_sectors = rd_u32(bpb + 32);
    if (out.total_sectors == 0) {
        err = "'" + image_path + "' declares a zero-sector FAT32 partition";
        return false;
    }
    const uint64_t need =
        (static_cast<uint64_t>(out.lba) + out.total_sectors) * kSectorSize;
    if (need > file_size) {
        err = "'" + image_path + "' is truncated: its partition needs " +
              std::to_string(need) + " bytes but the file is " +
              std::to_string(file_size);
        return false;
    }
    return true;
}

}  // namespace

namespace sdcard {

const char* file_add_status_name(FileAddStatus s) {
    switch (s) {
        case FileAddStatus::Ok:               return "ok";
        case FileAddStatus::SourceUnreadable: return "source-unreadable";
        case FileAddStatus::DestInvalid:      return "dest-invalid";
        case FileAddStatus::DestExists:       return "dest-exists";
        case FileAddStatus::ImageUnusable:    return "image-unusable";
        case FileAddStatus::ImageFull:        return "image-full";
    }
    return "?";
}

bool same_image_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    namespace fs = std::filesystem;

    // Filesystem identity first, and it is DEFINITIVE in both directions: when
    // both paths exist, `equivalent` compares the device+inode the OS reports,
    // so it answers true for a symlink, a hard link, a relative spelling and a
    // case-different one on a case-insensitive volume, and false for two files
    // that merely look alike.
    std::error_code ec;
    const bool eq = fs::equivalent(a, b, ec);
    if (!ec) return eq;

    // One of them cannot be stat'ed — normally the default image on a machine
    // that has never provisioned one. Compare lexically-normalised paths
    // instead: weakly_canonical does not require the file to exist and still
    // resolves '.', '..' and any existing leading components.
    std::error_code eca, ecb;
    const fs::path ca = fs::weakly_canonical(a, eca);
    const fs::path cb = fs::weakly_canonical(b, ecb);
    if (!eca && !ecb) return ca == cb;

    // Nothing worked (a path the host rejects outright). Fall back to the
    // string comparison this used to be: no worse than before, never throws.
    return a == b;
}

bool normalize_dest_path(const std::string& dest_path,
                         std::string& fatfs_path_out,
                         std::string& err) {
    fatfs_path_out.clear();
    const std::vector<std::string> parts = split_slash(dest_path);
    if (parts.empty()) {
        err = "destination path '" + dest_path + "' names no file";
        return false;
    }
    std::string out = "0:";
    for (const std::string& c : parts) {
        if (c == "." || c == "..") {
            err = "destination path '" + dest_path +
                  "' contains '" + c + "'; give the full path from the card root";
            return false;
        }
        // FF_MAX_LFN is 255; a longer component cannot be stored at all.
        if (c.size() > FF_MAX_LFN) {
            err = "name '" + c + "' in the destination path is longer than " +
                  std::to_string(static_cast<int>(FF_MAX_LFN)) + " characters";
            return false;
        }
        for (char ch : c) {
            const unsigned char u = static_cast<unsigned char>(ch);
            if (u < 0x20 || u > 0x7E) {
                err = "name '" + c +
                      "' in the destination path is not printable ASCII; "
                      "jnext writes ASCII names only";
                return false;
            }
            if (is_forbidden_name_char(ch)) {
                err = std::string("character '") + ch + "' in the destination "
                      "path is not allowed in a FAT name"
                      + (ch == '\\' ? " (use '/' to separate directories)" : "");
                return false;
            }
        }
        // FAT silently strips trailing dots and spaces, so a name that ends in
        // one is not the name that would appear on the card. Refuse instead of
        // writing something else than was asked for.
        if (c.back() == '.' || c.back() == ' ') {
            err = "name '" + c +
                  "' in the destination path ends in a '.' or a space, which "
                  "FAT strips; the file would not have the name asked for";
            return false;
        }
        out += "/";
        out += c;
    }
    fatfs_path_out = out;
    return true;
}

namespace {

// The image, attached to FatFs drive 0: and mounted, for as long as this
// object lives. Unmount (which flushes FatFs's own caches) happens before the
// backing file is closed, on every return path.
struct MountedCard {
    FATFS fs{};
    bool  attached = false;
    bool  mounted  = false;

    FileAddStatus mount(const std::string& image_path, std::string& err) {
        PartitionInfo part;
        if (!read_partition_info(image_path, part, err))
            return FileAddStatus::ImageUnusable;
        std::string glue_err;
        if (!fatfs_glue::attach(kDrive, image_path, part.lba,
                                part.total_sectors, glue_err)) {
            err = "cannot open SD image '" + image_path +
                  "' for writing (" + glue_err + ")";
            return FileAddStatus::ImageUnusable;
        }
        attached = true;
        const FRESULT fr = f_mount(&fs, "0:", 1 /* mount now */);
        if (fr != FR_OK) {
            err = "'" + image_path +
                  "' does not hold a FAT32 filesystem this build can mount (" +
                  fr_str(fr) +
                  "). An under-clustered image (< 65525 clusters) is rejected "
                  "by the Next's own firmware too; tools/fix-sdcard-image.sh "
                  "re-clusters one";
            return FileAddStatus::ImageUnusable;
        }
        mounted = true;
        return FileAddStatus::Ok;
    }

    ~MountedCard() {
        if (mounted)  f_mount(nullptr, "0:", 0);
        if (attached) fatfs_glue::detach(kDrive);
    }
};

// Free clusters on the mounted card, and the cluster size.
FileAddStatus card_free_space(const std::string& image_path,
                              uint64_t& free_clusters, uint64_t& cluster_bytes,
                              std::string& err) {
    DWORD  nfree = 0;
    FATFS* fsp   = nullptr;
    const FRESULT fr = f_getfree("0:", &nfree, &fsp);
    if (fr != FR_OK || fsp == nullptr) {
        err = "cannot read the free-space map of '" + image_path + "' (" +
              fr_str(fr) + ")";
        return FileAddStatus::ImageUnusable;
    }
    free_clusters = nfree;
    cluster_bytes = static_cast<uint64_t>(fsp->csize) * kSectorSize;
    return FileAddStatus::Ok;
}

// Make sure the directory `fat_dir` exists on the card, creating it when it
// does not. `name` is its last component and `dest_display` the user's whole
// destination, both for messages. `created` says whether this call made it.
FileAddStatus ensure_card_dir(const std::string& fat_dir,
                              const std::string& name,
                              const std::string& dest_display,
                              bool& created, std::string& err) {
    created = false;
    const FRESULT mk = f_mkdir(fat_dir.c_str());
    if (mk == FR_OK) {
        created = true;
        return FileAddStatus::Ok;
    }
    if (mk == FR_EXIST) {
        // Something is already there — it has to be a DIRECTORY, or the rest
        // of the path cannot exist. f_mkdir reports FR_EXIST for an existing
        // file just the same, so ask.
        FILINFO fno{};
        const FRESULT st = f_stat(fat_dir.c_str(), &fno);
        if (st != FR_OK) {
            err = "cannot inspect '" + name + "' on the card (" + fr_str(st) + ")";
            return FileAddStatus::DestInvalid;
        }
        if ((fno.fattrib & AM_DIR) == 0) {
            err = "'" + name + "' already exists on the card as a file, so '" +
                  dest_display + "' cannot be a path through it";
            return FileAddStatus::DestInvalid;
        }
        return FileAddStatus::Ok;
    }
    if (mk == FR_DENIED) {
        err = "cannot create directory '" + name +
              "' on the card: no free space or no free directory slots";
        return FileAddStatus::ImageFull;
    }
    err = "cannot create directory '" + name + "' on the card (" + fr_str(mk) + ")";
    return FileAddStatus::DestInvalid;
}

// Write `src_size` bytes of `src` to the card file `fat_path`, whose parent
// directory exists. `replaced` (passed in false) is set when a file was
// already there.
FileAddStatus write_card_file(const std::string& image_path,
                              const std::string& fat_path,
                              const std::string& dest_path,
                              const std::string& host_file,
                              std::ifstream& src, uint64_t src_size,
                              bool overwrite, bool& replaced,
                              std::string& err) {
    // ---- what is already at the destination ---------------------------------
    // Only the two things f_open cannot answer are decided here — whether the
    // destination is a directory, and whether it is marked read-only. Whether
    // it EXISTS is left to f_open's FA_CREATE_NEW below, so there is exactly
    // one authority for that and not two that can disagree; the size is kept
    // purely to put a number in the refusal message.
    uint64_t existing_size = 0;
    {
        FILINFO fno{};
        FRESULT st = f_stat(fat_path.c_str(), &fno);
        if (st == FR_OK) {
            if (fno.fattrib & AM_DIR) {
                err = "'" + dest_path +
                      "' already exists on the card as a directory";
                return FileAddStatus::DestInvalid;
            }
            if (overwrite && (fno.fattrib & AM_RDO)) {
                err = "'" + dest_path +
                      "' is marked read-only on the card and was not replaced";
                return FileAddStatus::DestInvalid;
            }
            existing_size = static_cast<uint64_t>(fno.fsize);
            replaced = true;
        } else if (st != FR_NO_FILE && st != FR_NO_PATH) {
            err = "cannot inspect '" + dest_path + "' on the card (" +
                  fr_str(st) + ")";
            return FileAddStatus::DestInvalid;
        }
    }

    // ---- copy -----------------------------------------------------------------
    // Decision: an existing destination is REFUSED unless --sdcard-file-force
    // was given. Silently replacing DRV-A.DSK would destroy a disk image.
    // FA_CREATE_NEW is what enforces it: the file is never opened for writing
    // at all, so nothing can go wrong between the decision and the truncation.
    FIL fp{};
    FRESULT fr = f_open(&fp, fat_path.c_str(),
                        FA_WRITE | (overwrite ? FA_CREATE_ALWAYS : FA_CREATE_NEW));
    if (fr == FR_EXIST) {
        err = "'" + dest_path + "' already exists on the card (" +
              std::to_string(existing_size) +
              " bytes); pass --sdcard-file-force to replace it";
        return FileAddStatus::DestExists;
    }
    if (fr == FR_DENIED) {
        // FatFs reports FR_DENIED on create when the volume or the directory
        // table is full. Free space was already checked, so this is the
        // directory-slot case.
        err = "cannot create '" + dest_path +
              "' on the card: its directory has no free slots left";
        return FileAddStatus::ImageFull;
    }
    if (fr != FR_OK) {
        err = "cannot create '" + dest_path + "' on the card (" + fr_str(fr) + ")";
        return FileAddStatus::DestInvalid;
    }

    // From here on a failure leaves a partial file, which is worse than no
    // file at all — so every failing path below closes and unlinks it.
    auto abandon = [&](FileAddStatus s, const std::string& why) {
        f_close(&fp);
        f_unlink(fat_path.c_str());
        err = why + " ('" + dest_path + "' was removed from the card)";
        return s;
    };

    std::vector<uint8_t> buf(kCopyChunk);
    uint64_t remaining = src_size;
    while (remaining > 0) {
        const uint32_t take = static_cast<uint32_t>(
            remaining < kCopyChunk ? remaining : kCopyChunk);
        src.read(reinterpret_cast<char*>(buf.data()),
                 static_cast<std::streamsize>(take));
        if (static_cast<uint64_t>(src.gcount()) != take) {
            return abandon(FileAddStatus::SourceUnreadable,
                           "'" + host_file + "' became shorter while it was "
                           "being copied");
        }
        UINT bw = 0;
        fr = f_write(&fp, buf.data(), static_cast<UINT>(take), &bw);
        if (fr != FR_OK) {
            return abandon(FileAddStatus::ImageUnusable,
                           "writing to '" + image_path + "' failed (" +
                           fr_str(fr) + ")");
        }
        if (bw != take) {
            return abandon(FileAddStatus::ImageFull,
                           "'" + image_path + "' ran out of space while "
                           "copying '" + host_file + "'");
        }
        remaining -= take;
    }

    fr = f_close(&fp);
    if (fr != FR_OK) {
        f_unlink(fat_path.c_str());
        err = "cannot flush '" + dest_path + "' to '" + image_path + "' (" +
              fr_str(fr) + "); it was removed from the card";
        return FileAddStatus::ImageUnusable;
    }
    return FileAddStatus::Ok;
}

// "dir/" -> "dir". A root ("/") is its own parent and stays nameless.
void strip_trailing_separator(std::filesystem::path& p) {
    if (!p.has_filename() && p.has_parent_path())
        p = p.parent_path();
}

// ---------------------------------------------------------------------------
// Directory trees (GH #292)
// ---------------------------------------------------------------------------

// One thing to put on the card. `rel` is its path below the destination.
struct TreeNode {
    std::vector<std::string> rel;
    std::filesystem::path    host;
    bool                     is_dir = false;
    uint64_t                 size   = 0;
};

std::string upper_ascii(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string join_card(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& c : parts) { out += "/"; out += c; }
    return out.empty() ? "/" : out;
}

// Walk the host directory `dir` (already known to be one), appending what it
// holds to `out` in pre-order — a directory before its contents — sorted by
// name so the card comes out the same on every host. `ancestors` holds the
// canonical path of every directory on the way down: meeting one of them again
// means a symlink loops back, and following it would never end.
//
// Everything that would make the copy fail on the host side is found HERE,
// before the image is opened: a partial copy is never the way a refusal is
// discovered.
FileAddStatus scan_host_dir(const std::filesystem::path& dir,
                            std::vector<std::string>& rel,
                            std::vector<std::filesystem::path>& ancestors,
                            std::vector<TreeNode>& out, std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path canon = fs::canonical(dir, ec);
    if (ec) {
        err = "cannot resolve directory '" + dir.u8string() + "' (" +
              ec.message() + ")";
        return FileAddStatus::SourceUnreadable;
    }
    for (const fs::path& a : ancestors) {
        if (a == canon) {
            err = "'" + dir.u8string() + "' is a symbolic link back to '" +
                  a.u8string() + "', which contains it; copying it would "
                  "never end";
            return FileAddStatus::SourceUnreadable;
        }
    }

    std::vector<fs::path> entries;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        err = "cannot list directory '" + dir.u8string() + "' (" +
              ec.message() + ")";
        return FileAddStatus::SourceUnreadable;
    }
    for (; it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        entries.push_back(it->path());
    }
    if (ec) {
        err = "cannot list directory '" + dir.u8string() + "' (" +
              ec.message() + ")";
        return FileAddStatus::SourceUnreadable;
    }
    std::sort(entries.begin(), entries.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().u8string() < b.filename().u8string();
              });

    // FAT compares names without regard to case, so two host files that
    // differ only in case would be ONE file on the card — the second copy
    // either refused as a clash with the first or, with force, replacing it.
    // Either way the card would not hold what the host holds. Refused up
    // front, naming both.
    std::vector<std::pair<std::string, std::string>> seen;  // upper, original
    for (const fs::path& p : entries) {
        const std::string name = p.filename().u8string();
        const std::string up   = upper_ascii(name);
        for (const auto& s : seen) {
            if (s.first == up) {
                err = "'" + s.second + "' and '" + name + "' in '" +
                      dir.u8string() + "' would be the same name on the card "
                      "(FAT names are not case-sensitive)";
                return FileAddStatus::DestInvalid;
            }
        }
        seen.emplace_back(up, name);
    }

    ancestors.push_back(canon);
    for (const fs::path& p : entries) {
        const std::string name = p.filename().u8string();
        std::string fat_unused, why;
        if (!normalize_dest_path("/" + name, fat_unused, why)) {
            err = "'" + p.u8string() + "' cannot be copied: " + why;
            return FileAddStatus::DestInvalid;
        }
        rel.push_back(name);

        // status() FOLLOWS a symlink, which is the policy: the card gets what
        // the link points at.
        const fs::file_status st = fs::status(p, ec);
        if (ec || !fs::exists(st)) {
            err = fs::is_symlink(fs::symlink_status(p, ec))
                ? "'" + p.u8string() + "' is a symbolic link to nothing"
                : "cannot read '" + p.u8string() + "'";
            return FileAddStatus::SourceUnreadable;
        }
        if (fs::is_directory(st)) {
            out.push_back(TreeNode{rel, p, true, 0});
            const FileAddStatus s = scan_host_dir(p, rel, ancestors, out, err);
            if (s != FileAddStatus::Ok) return s;
        } else if (fs::is_regular_file(st)) {
            const uintmax_t size = fs::file_size(p, ec);
            std::ifstream probe(p, std::ios::binary);
            if (ec || !probe) {
                err = "cannot open source file '" + p.u8string() + "'";
                return FileAddStatus::SourceUnreadable;
            }
            if (size > kMaxFileSize) {
                err = "'" + p.u8string() + "' is " + std::to_string(size) +
                      " bytes; FAT32 cannot store a file of 4 GiB or more";
                return FileAddStatus::SourceUnreadable;
            }
            out.push_back(TreeNode{rel, p, false, static_cast<uint64_t>(size)});
        } else {
            err = "'" + p.u8string() + "' is neither a regular file nor a "
                  "directory (a device, FIFO or socket); it cannot be copied "
                  "onto a FAT card";
            return FileAddStatus::SourceUnreadable;
        }
        rel.pop_back();
    }
    ancestors.pop_back();
    return FileAddStatus::Ok;
}

FileAddStatus add_dir_to_image(const std::string& image_path,
                               const std::string& host_dir,
                               const std::string& dest_path,
                               bool overwrite, std::string& err,
                               AddSummary* summary) {
    namespace fs = std::filesystem;

    // ---- 1. the destination: a path, or "/" for the card root ---------------
    // add_to_image() has already replaced an empty destination with the
    // default one, so no components here means the user wrote "/".
    const std::vector<std::string> dest_parts = split_slash(dest_path);
    if (!dest_parts.empty()) {
        std::string unused;
        if (!normalize_dest_path(dest_path, unused, err))
            return FileAddStatus::DestInvalid;
    }

    // ---- 2. the host tree, entirely, before the image is touched ------------
    // The destination itself and the directories leading to it come first, as
    // directory nodes of their own; the tree hangs below them.
    std::vector<TreeNode> nodes;
    for (std::size_t i = 0; i < dest_parts.size(); ++i) {
        TreeNode n;
        n.rel.assign(dest_parts.begin(), dest_parts.begin() + i + 1);
        n.is_dir = true;
        nodes.push_back(n);
    }
    {
        std::vector<std::string> rel = dest_parts;
        std::vector<fs::path>    ancestors;
        const FileAddStatus st =
            scan_host_dir(fs::path(host_dir), rel, ancestors, nodes, err);
        if (st != FileAddStatus::Ok) return st;
    }

    // ---- 3. the image ------------------------------------------------------
    MountedCard card;
    FileAddStatus st = card.mount(image_path, err);
    if (st != FileAddStatus::Ok) return st;

    // ---- 4. what is already on the card -------------------------------------
    // Every clash is found now, so a refusal leaves the card untouched.
    // Directories already there are merged into; files follow the single-file
    // rule. A hard conflict is reported at once; existing files are counted
    // so the refusal can say how many there are, not just the first.
    std::vector<bool> exists(nodes.size(), false);
    std::size_t clashes = 0;
    std::string first_clash;
    uint64_t    first_clash_size = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const TreeNode& n = nodes[i];
        const std::string card_path = join_card(n.rel);
        const std::string fat_path  = "0:" + card_path;
        FILINFO fno{};
        const FRESULT fr = f_stat(fat_path.c_str(), &fno);
        if (fr == FR_NO_FILE || fr == FR_NO_PATH) continue;
        if (fr != FR_OK) {
            err = "cannot inspect '" + card_path + "' on the card (" +
                  fr_str(fr) + ")";
            return FileAddStatus::DestInvalid;
        }
        const bool card_is_dir = (fno.fattrib & AM_DIR) != 0;
        if (n.is_dir && !card_is_dir) {
            err = "'" + card_path + "' already exists on the card as a file, "
                  "so the directory '" + host_dir + "' cannot be copied there";
            return FileAddStatus::DestInvalid;
        }
        if (!n.is_dir && card_is_dir) {
            err = "'" + card_path + "' already exists on the card as a "
                  "directory, so '" + n.host.u8string() +
                  "' cannot be copied there";
            return FileAddStatus::DestInvalid;
        }
        if (!n.is_dir && overwrite && (fno.fattrib & AM_RDO)) {
            err = "'" + card_path +
                  "' is marked read-only on the card and was not replaced";
            return FileAddStatus::DestInvalid;
        }
        exists[i] = true;
        if (!n.is_dir && !overwrite) {
            if (clashes++ == 0) {
                first_clash      = card_path;
                first_clash_size = static_cast<uint64_t>(fno.fsize);
            }
        }
    }
    if (clashes > 0) {
        err = "'" + first_clash + "' already exists on the card (" +
              std::to_string(first_clash_size) + " bytes)" +
              (clashes > 1 ? " and so do " + std::to_string(clashes - 1) +
                             " more files of this copy"
                           : std::string()) +
              "; pass --sdcard-file-force to replace them. Nothing was written";
        return FileAddStatus::DestExists;
    }

    // ---- 5. free space ----------------------------------------------------------
    // Data clusters for every file plus one cluster per directory that has to
    // be made. That is a LOWER bound — a directory with many entries needs
    // more than one cluster, and a parent may need to grow — so passing it
    // does not promise the copy fits; running out later is still caught, and
    // rolled back, below. What it does promise is that a copy which cannot
    // POSSIBLY fit is refused with the card untouched.
    uint64_t free_clusters = 0, cluster_bytes = 0;
    st = card_free_space(image_path, free_clusters, cluster_bytes, err);
    if (st != FileAddStatus::Ok) return st;
    uint64_t need = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].is_dir)
            need += exists[i] ? 0 : 1;
        else
            need += (nodes[i].size + cluster_bytes - 1) / cluster_bytes;
    }
    if (need > free_clusters) {
        err = "'" + image_path + "' has " +
              std::to_string(free_clusters * cluster_bytes / 1024) +
              " KB free; '" + host_dir + "' needs at least " +
              std::to_string(need * cluster_bytes / 1024) + " KB";
        return FileAddStatus::ImageFull;
    }

    // ---- 6. copy, rolling back on failure --------------------------------------
    std::vector<std::string> created;   // FatFs paths, in creation order
    unsigned replaced_count = 0, files = 0, dirs_created = 0;
    uint64_t bytes = 0;
    auto rollback = [&](FileAddStatus s) {
        std::size_t left = 0;
        for (auto r = created.rbegin(); r != created.rend(); ++r)
            if (f_unlink(r->c_str()) != FR_OK) ++left;
        err += "; the copy was rolled back";
        if (left > 0)
            err += ", but " + std::to_string(left) +
                   " of the entries it created could not be removed";
        if (replaced_count > 0)
            err += ". " + std::to_string(replaced_count) +
                   " existing file(s) had already been replaced "
                   "(--sdcard-file-force) and cannot be restored";
        return s;
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const TreeNode& n = nodes[i];
        const std::string card_path = join_card(n.rel);
        const std::string fat_path  = "0:" + card_path;
        if (n.is_dir) {
            bool made = false;
            st = ensure_card_dir(fat_path, n.rel.back(), card_path, made, err);
            if (st != FileAddStatus::Ok) return rollback(st);
            if (made) { created.push_back(fat_path); ++dirs_created; }
            continue;
        }
        std::ifstream src(n.host, std::ios::binary);
        if (!src) {
            err = "cannot open source file '" + n.host.u8string() + "'";
            return rollback(FileAddStatus::SourceUnreadable);
        }
        bool replaced = false;
        st = write_card_file(image_path, fat_path, card_path, n.host.u8string(),
                             src, n.size, overwrite, replaced, err);
        if (st != FileAddStatus::Ok) {
            // write_card_file already removed its own partial file; a file it
            // was REPLACING is gone with it.
            if (replaced) ++replaced_count;
            return rollback(st);
        }
        if (replaced) ++replaced_count;
        else          created.push_back(fat_path);
        ++files;
        bytes += n.size;
    }

    if (summary) {
        *summary = AddSummary{};
        summary->is_dir       = true;
        summary->dest         = join_card(dest_parts);
        summary->files        = files;
        summary->dirs_created = dirs_created;
        summary->bytes        = bytes;
    }
    return FileAddStatus::Ok;
}

}  // namespace

FileAddStatus add_file_to_image(const std::string& image_path,
                                const std::string& host_file,
                                const std::string& dest_path,
                                bool overwrite,
                                std::string& err) {
    err.clear();

    // ---- 1. the source -----------------------------------------------------
    std::ifstream src(host_file, std::ios::binary | std::ios::ate);
    if (!src) {
        err = "cannot open source file '" + host_file + "'";
        return FileAddStatus::SourceUnreadable;
    }
    const std::streamoff src_end = src.tellg();
    // Defensive, and known to be so: every host path that gets past the open
    // above reports a size, so nothing in the test suite reaches this branch
    // (it is a declared surviving mutant in sdcard_file_add_test's header). It
    // stays because a negative size would otherwise be cast to an enormous
    // unsigned one and drive the copy loop with it.
    if (src_end < 0) {
        err = "cannot determine the size of '" + host_file +
              "' (is it a directory?)";
        return FileAddStatus::SourceUnreadable;
    }
    const uint64_t src_size = static_cast<uint64_t>(src_end);
    if (src_size > kMaxFileSize) {
        err = "'" + host_file + "' is " + std::to_string(src_size) +
              " bytes; FAT32 cannot store a file of 4 GiB or more";
        return FileAddStatus::SourceUnreadable;
    }
    src.seekg(0, std::ios::beg);
    if (!src) {
        err = "cannot rewind source file '" + host_file + "'";
        return FileAddStatus::SourceUnreadable;
    }

    // ---- 2. the destination path ------------------------------------------
    std::string fat_path;
    if (!normalize_dest_path(dest_path, fat_path, err))
        return FileAddStatus::DestInvalid;

    // ---- 3. the image ------------------------------------------------------
    MountedCard card;
    FileAddStatus st = card.mount(image_path, err);
    if (st != FileAddStatus::Ok) return st;

    // ---- 4. free space -----------------------------------------------------
    // Asked BEFORE anything is created, so a too-big file is refused with the
    // card untouched instead of leaving a half-written entry behind.
    uint64_t free_clusters = 0, cluster_bytes = 0;
    st = card_free_space(image_path, free_clusters, cluster_bytes, err);
    if (st != FileAddStatus::Ok) return st;
    const uint64_t need_clusters =
        (src_size + cluster_bytes - 1) / cluster_bytes;
    if (need_clusters > free_clusters) {
        err = "'" + image_path + "' has " +
              std::to_string(free_clusters * cluster_bytes / 1024) +
              " KB free; '" + host_file + "' needs " +
              std::to_string(need_clusters * cluster_bytes / 1024) + " KB";
        return FileAddStatus::ImageFull;
    }

    // ---- 5. intermediate directories ---------------------------------------
    // Decision: missing directories are CREATED. `--sdcard-file-dest
    // /DEMOS/x.nex` on a card with no /DEMOS must work without a second step.
    {
        const std::vector<std::string> parts = split_slash(dest_path);
        std::string prefix = "0:";
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            prefix += "/";
            prefix += parts[i];
            bool created = false;
            st = ensure_card_dir(prefix, parts[i], dest_path, created, err);
            if (st != FileAddStatus::Ok) return st;
        }
    }

    // ---- 6 + 7. what is already there, and the copy -------------------------
    bool replaced = false;
    return write_card_file(image_path, fat_path, dest_path, host_file, src,
                           src_size, overwrite, replaced, err);
}

std::string file_add_usage_error(int add_count, int dest_count,
                                 bool dest_empty, bool force) {
    if (add_count == 0) {
        if (dest_count > 0) return "--sdcard-file-dest requires --sdcard-file-add FILE.";
        if (force)          return "--sdcard-file-force requires --sdcard-file-add FILE.";
        return std::string();
    }
    if (add_count > 1 || dest_count > 1) {
        return std::string(add_count > 1 ? "--sdcard-file-add"
                                         : "--sdcard-file-dest") +
               " was given more than once: one run copies ONE file or "
               "directory. Put several files in a directory and add that, or "
               "run jnext once per file.";
    }
    if (dest_count > 0 && dest_empty) {
        return "--sdcard-file-dest was given an empty PATH. Leave the option "
               "out to copy into the root of the card.";
    }
    return std::string();
}

bool default_dest_path(const std::string& host_path,
                       std::string& dest_out,
                       std::string& err) {
    namespace fs = std::filesystem;
    dest_out.clear();
    // Lexical only: the name is the one the user TYPED. Resolving symlinks
    // here would land `latest.nex -> v3.nex` as /v3.nex.
    fs::path p = fs::path(host_path).lexically_normal();
    // `.`, `..`, an empty path and a trailing separator ("games/") leave no
    // name of their own; the directory they mean has one.
    const std::string bare = p.filename().u8string();
    if (bare.empty() || bare == "." || bare == "..") {
        // On error absolute() returns an empty path, which has no name.
        std::error_code ec;
        p = fs::absolute(fs::path(host_path), ec).lexically_normal();
        strip_trailing_separator(p);
    }
    // After lexically_normal() an absolute path cannot end in "." or "..";
    // only the host root is left without a name.
    const std::string name = p.filename().u8string();
    if (name.empty()) {
        err = "cannot take a card name from '" + host_path +
              "'; give --sdcard-file-dest PATH";
        return false;
    }
    std::string fat_path, why;
    if (!normalize_dest_path("/" + name, fat_path, why)) {
        err = "the card name taken from '" + host_path + "' is unusable (" +
              why + "); give --sdcard-file-dest PATH";
        return false;
    }
    dest_out = "/" + name;
    return true;
}

FileAddStatus add_to_image(const std::string& image_path,
                           const std::string& host_path,
                           const std::string& dest_path,
                           bool overwrite,
                           std::string& err,
                           AddSummary* summary) {
    namespace fs = std::filesystem;
    err.clear();

    // What the source IS decides everything else, so it is asked first —
    // following a symlink, which is what a user giving one means.
    std::error_code ec;
    const fs::file_status sst = fs::status(fs::path(host_path), ec);
    const bool is_dir = !ec && fs::is_directory(sst);

    // The destination: given, or the card root under the source's own name.
    std::string dest = dest_path;
    if (dest.empty()) {
        if (!default_dest_path(host_path, dest, err))
            return FileAddStatus::DestInvalid;
    }

    if (!is_dir) {
        // Not a directory: a regular file, or something add_file_to_image()
        // will refuse with the message it always gave (missing, unreadable).
        // A FIFO is refused HERE instead — opening one to read it would block
        // until something writes to it, which in a script is forever.
        if (!ec && fs::exists(sst) && !fs::is_regular_file(sst)) {
            err = "'" + host_path + "' is neither a regular file nor a "
                  "directory (a device, FIFO or socket); it cannot be copied "
                  "onto a FAT card";
            return FileAddStatus::SourceUnreadable;
        }
        const FileAddStatus st =
            add_file_to_image(image_path, host_path, dest, overwrite, err);
        if (st == FileAddStatus::Ok && summary) {
            *summary = AddSummary{};
            summary->dest  = dest;
            summary->files = 1;
            summary->bytes = fs::file_size(fs::path(host_path), ec);
        }
        return st;
    }
    return add_dir_to_image(image_path, host_path, dest, overwrite, err,
                            summary);
}

}  // namespace sdcard
