#include "core/fat32_image.h"
#include "core/fatfs_diskio.h"
#include "core/log.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <functional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// FatFs-backed FAT32 (re)format + tree writer (Task 27, replaces the
// hand-rolled fat32_write_volume). The source image is read with the lenient
// hand-rolled reader (FatFs itself rejects the under-clustered source — it
// classifies < 65525 clusters as FAT16 and then fails on the FAT32-shaped BPB,
// ff.c R0.15b lines 3145 + 3155). The destination is produced with the
// battle-tested FatFs f_mkfs / f_write so the emitted image is FatFs-faithful
// (the same library family the Next firmware runs), by construction.
// ---------------------------------------------------------------------------

namespace {

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

} // namespace

bool fat32_format_and_populate(const std::string& image_path, uint32_t part_lba,
                               uint32_t total_sectors, const Fat32Tree& tree,
                               std::string& err) {
    constexpr uint8_t kDrive = 0; // FatFs volume "0:"

    if (!fatfs_glue::attach(kDrive, image_path, part_lba, total_sectors, err))
        return false;

    // RAII detach (also flushes) on every return path.
    struct Detacher {
        uint8_t drive;
        ~Detacher() { fatfs_glue::detach(drive); }
    } detacher{kDrive};

    // 1. f_mkfs: fresh FAT32 with 8 KB clusters. FM_SFD keeps the filesystem
    //    starting at sector 0 of the presented (partition) volume — no nested
    //    partition table — so the image's MBR + partition geometry survive.
    MKFS_PARM parm{};
    parm.fmt     = FM_FAT32 | FM_SFD;
    parm.n_fat   = 2;
    parm.align   = 0;    // let FatFs choose data alignment
    parm.n_root  = 0;    // ignored for FAT32
    parm.au_size = 8192; // 8 KB clusters (>= 65525 clusters -> valid FAT32)

    std::vector<uint8_t> work(64 * 1024); // larger buffer -> faster FAT init
    FRESULT fr = f_mkfs("0:", &parm, work.data(),
                        static_cast<UINT>(work.size()));
    if (fr != FR_OK) {
        err = std::string("f_mkfs failed: ") + fr_str(fr);
        return false;
    }

    FATFS fs{};
    fr = f_mount(&fs, "0:", 1 /* mount now */);
    if (fr != FR_OK) {
        err = std::string("f_mount failed: ") + fr_str(fr);
        return false;
    }

    // 2. Recursively write the in-memory tree. FatFs generates standard 8.3
    //    SFNs, so a mixed-case-but-8.3 name like "enNxtmmc.rom" keeps its
    //    natural uppercase short name ENNXTMMC.ROM (no ~N tail) — required by
    //    the SFN-based sd_rom_extractor lookups.
    bool ok = true;
    std::string where;
    std::function<void(const std::vector<Fat32Node>&, const std::string&)> emit =
        [&](const std::vector<Fat32Node>& nodes, const std::string& prefix) {
            if (!ok) return;
            for (const auto& n : nodes) {
                const std::string path = prefix + "/" + n.name;
                if (n.is_dir) {
                    FRESULT r = f_mkdir(path.c_str());
                    if (r != FR_OK && r != FR_EXIST) {
                        ok = false;
                        where = "f_mkdir " + path + ": " + fr_str(r);
                        return;
                    }
                    emit(n.children, path);
                    if (!ok) return;
                } else {
                    FIL fp{};
                    FRESULT r = f_open(&fp, path.c_str(),
                                       FA_CREATE_ALWAYS | FA_WRITE);
                    if (r != FR_OK) {
                        ok = false;
                        where = "f_open " + path + ": " + fr_str(r);
                        return;
                    }
                    if (!n.data.empty()) {
                        UINT bw = 0;
                        r = f_write(&fp, n.data.data(),
                                    static_cast<UINT>(n.data.size()), &bw);
                        if (r == FR_OK && bw != n.data.size()) {
                            r = FR_DISK_ERR; // volume full / short write
                        }
                    }
                    FRESULT rc = f_close(&fp);
                    if (r != FR_OK || rc != FR_OK) {
                        ok = false;
                        where = "f_write " + path + ": " + fr_str(r != FR_OK ? r : rc);
                        return;
                    }
                }
            }
        };
    emit(tree.root, "0:");

    f_mount(nullptr, "0:", 0); // unmount + flush

    if (!ok) { err = where; return false; }
    return true;
}
