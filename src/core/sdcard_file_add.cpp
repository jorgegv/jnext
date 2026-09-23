#include "core/sdcard_file_add.h"

#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <array>
#include <cstdio>
#include <cstring>
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
    PartitionInfo part;
    if (!read_partition_info(image_path, part, err))
        return FileAddStatus::ImageUnusable;

    std::string glue_err;
    if (!fatfs_glue::attach(kDrive, image_path, part.lba, part.total_sectors,
                            glue_err)) {
        err = "cannot open SD image '" + image_path +
              "' for writing (" + glue_err + ")";
        return FileAddStatus::ImageUnusable;
    }
    // RAII: flush + close the backing file on every return path below.
    struct Detacher {
        uint8_t drive;
        ~Detacher() { fatfs_glue::detach(drive); }
    } detacher{kDrive};

    FATFS   fs{};
    FRESULT fr = f_mount(&fs, "0:", 1 /* mount now */);
    if (fr != FR_OK) {
        err = "'" + image_path +
              "' does not hold a FAT32 filesystem this build can mount (" +
              fr_str(fr) +
              "). An under-clustered image (< 65525 clusters) is rejected by "
              "the Next's own firmware too; tools/fix-sdcard-image.sh "
              "re-clusters one";
        return FileAddStatus::ImageUnusable;
    }
    // RAII: unmount (which flushes FatFs's own caches) before the Detacher
    // closes the file.
    struct Unmounter {
        ~Unmounter() { f_mount(nullptr, "0:", 0); }
    } unmounter;

    // ---- 4. free space -----------------------------------------------------
    // Asked BEFORE anything is created, so a too-big file is refused with the
    // card untouched instead of leaving a half-written entry behind.
    DWORD   free_clusters = 0;
    FATFS*  fsp           = nullptr;
    fr = f_getfree("0:", &free_clusters, &fsp);
    if (fr != FR_OK || fsp == nullptr) {
        err = "cannot read the free-space map of '" + image_path + "' (" +
              fr_str(fr) + ")";
        return FileAddStatus::ImageUnusable;
    }
    const uint64_t cluster_bytes =
        static_cast<uint64_t>(fsp->csize) * kSectorSize;
    const uint64_t need_clusters =
        (src_size + cluster_bytes - 1) / cluster_bytes;
    if (need_clusters > free_clusters) {
        err = "'" + image_path + "' has " +
              std::to_string(static_cast<uint64_t>(free_clusters) * cluster_bytes / 1024) +
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
            FRESULT mk = f_mkdir(prefix.c_str());
            if (mk == FR_EXIST) {
                // Something is already there — it has to be a DIRECTORY, or
                // the rest of the path cannot exist. f_mkdir reports FR_EXIST
                // for an existing file just the same, so ask.
                FILINFO fno{};
                FRESULT st = f_stat(prefix.c_str(), &fno);
                if (st != FR_OK) {
                    err = "cannot inspect '" + parts[i] + "' on the card (" +
                          fr_str(st) + ")";
                    return FileAddStatus::DestInvalid;
                }
                if ((fno.fattrib & AM_DIR) == 0) {
                    err = "'" + parts[i] +
                          "' already exists on the card as a file, so '" +
                          dest_path + "' cannot be a path through it";
                    return FileAddStatus::DestInvalid;
                }
            } else if (mk == FR_DENIED) {
                err = "cannot create directory '" + parts[i] +
                      "' on the card: no free space or no free directory slots";
                return FileAddStatus::ImageFull;
            } else if (mk != FR_OK) {
                err = "cannot create directory '" + parts[i] + "' on the card (" +
                      fr_str(mk) + ")";
                return FileAddStatus::DestInvalid;
            }
        }
    }

    // ---- 6. what is already at the destination ------------------------------
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
        } else if (st != FR_NO_FILE && st != FR_NO_PATH) {
            err = "cannot inspect '" + dest_path + "' on the card (" +
                  fr_str(st) + ")";
            return FileAddStatus::DestInvalid;
        }
    }

    // ---- 7. copy -----------------------------------------------------------
    // Decision: an existing destination is REFUSED unless --sdcard-file-force
    // was given. Silently replacing DRV-A.DSK would destroy a disk image.
    // FA_CREATE_NEW is what enforces it: the file is never opened for writing
    // at all, so nothing can go wrong between the decision and the truncation.
    FIL fp{};
    fr = f_open(&fp, fat_path.c_str(),
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

}  // namespace sdcard
