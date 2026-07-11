#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <array>
#include <fstream>

// ---------------------------------------------------------------------------
// Per-drive backing store. FatFs here is used single-threaded, one operation
// at a time (the SD-card provisioner reformats one image), so a plain static
// array of open file streams is sufficient.
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kSectorSize = 512;

struct Backing {
    std::fstream file;
    uint32_t     part_lba     = 0; // partition start offset, in sectors
    uint32_t     sector_count = 0; // volume size, in sectors
    bool         active       = false;
};

std::array<Backing, FF_VOLUMES> g_backing;

} // namespace

namespace fatfs_glue {

bool attach(uint8_t pdrv, const std::string& image_path, uint32_t part_lba,
            uint32_t sector_count, std::string& err) {
    if (pdrv >= FF_VOLUMES) { err = "fatfs_glue: pdrv out of range"; return false; }
    Backing& b = g_backing[pdrv];
    b.file.close();
    b.file.clear();
    b.file.open(image_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!b.file) {
        err = "fatfs_glue: cannot open image r/w: " + image_path;
        return false;
    }
    b.part_lba     = part_lba;
    b.sector_count = sector_count;
    b.active       = true;
    return true;
}

void detach(uint8_t pdrv) {
    if (pdrv >= FF_VOLUMES) return;
    Backing& b = g_backing[pdrv];
    if (b.file.is_open()) { b.file.flush(); b.file.close(); }
    b.file.clear();
    b.active = false;
}

} // namespace fatfs_glue

// ---------------------------------------------------------------------------
// FatFs low-level disk interface (C linkage — called from ff.c).
// ---------------------------------------------------------------------------
extern "C" {

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return STA_NOINIT;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return RES_NOTRDY;
    Backing& b = g_backing[pdrv];
    if (static_cast<uint64_t>(sector) + count > b.sector_count) return RES_PARERR;
    const uint64_t off =
        (static_cast<uint64_t>(b.part_lba) + sector) * kSectorSize;
    const std::streamsize len =
        static_cast<std::streamsize>(count) * kSectorSize;
    b.file.clear();
    b.file.seekg(static_cast<std::streamoff>(off), std::ios::beg);
    b.file.read(reinterpret_cast<char*>(buff), len);
    if (b.file.gcount() != len) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return RES_NOTRDY;
    Backing& b = g_backing[pdrv];
    if (static_cast<uint64_t>(sector) + count > b.sector_count) return RES_PARERR;
    const uint64_t off =
        (static_cast<uint64_t>(b.part_lba) + sector) * kSectorSize;
    const std::streamsize len =
        static_cast<std::streamsize>(count) * kSectorSize;
    b.file.clear();
    b.file.seekp(static_cast<std::streamoff>(off), std::ios::beg);
    b.file.write(reinterpret_cast<const char*>(buff), len);
    if (!b.file.good()) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return RES_NOTRDY;
    Backing& b = g_backing[pdrv];
    switch (cmd) {
        case CTRL_SYNC:
            b.file.flush();
            return b.file.good() ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            *static_cast<LBA_t*>(buff) = b.sector_count;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *static_cast<WORD*>(buff) = static_cast<WORD>(kSectorSize);
            return RES_OK;
        case GET_BLOCK_SIZE:
            *static_cast<DWORD*>(buff) = 1; // no erase-block constraint
            return RES_OK;
        default:
            return RES_OK;
    }
}

} // extern "C"
