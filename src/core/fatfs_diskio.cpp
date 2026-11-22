#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include "peripheral/sd_write_journal.h"

#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Per-drive backing store. FatFs here is used single-threaded, one operation
// at a time (the SD-card provisioner reformats one image), so a plain static
// array of open file streams is sufficient.
// ---------------------------------------------------------------------------
namespace {

constexpr uint32_t kSectorSize = 512;

// Direct-mapped, write-through sector cache, one per drive, living from
// attach() to detach(). FatFs keeps a single sector window per volume, so a
// path lookup re-reads the directory from the image, and every uncached read
// here costs an lseek plus an 8 KiB stream-buffer refill (GH #547 stage 5).
// The cache holds the image FILE's raw bytes, indexed by the FatFs sector
// number: never journal data (the overlay is applied after it). Every write goes
// to the stream first (into its 8 KiB put area), so detach() and CTRL_SYNC still
// flush the STREAM, as before; only the cache needs no flush. A write the
// stream buffered and cannot flush is therefore reported later, by CTRL_SYNC
// (FatFs's f_close/f_sync, which the callers check), not by the write.
// 8192 lines = 4 MiB per attached drive. Measured on
// minimal_card_test (8400 files in one directory): read syscalls 19.1 M with no
// cache, 9.3 M with 128 lines, 0.86 M with 1024, 0.10 M with 8192. The data is
// not initialised (a tag says which lines are valid), so a native attach commits
// no page it does not use (the WebAssembly build takes all 4 MiB of linear memory,
// which it can grow).
constexpr size_t   kCacheLines = 8192;
constexpr uint64_t kNoTag      = ~0ull;

struct Backing {
    std::fstream file;
    uint32_t     part_lba     = 0; // partition start offset, in sectors
    uint32_t     sector_count = 0; // volume size, in sectors
    bool         active       = false;
    std::shared_ptr<sd_write_journal::Journal> journal; // non-null = journal mode
    std::unique_ptr<uint8_t[]> cache_data; // kCacheLines * kSectorSize, null = detached
    std::vector<uint64_t>      cache_tag;  // FatFs sector held by each line, or kNoTag

    void cache_reset() {
        cache_data.reset(new uint8_t[kCacheLines * kSectorSize]); // left uninitialised
        cache_tag.assign(kCacheLines, kNoTag);
    }
    void cache_drop() {
        cache_data.reset();
        std::vector<uint64_t>().swap(cache_tag);
    }
    void cache_put(uint64_t sector, const uint8_t* src) {
        const size_t i = static_cast<size_t>(sector % kCacheLines);
        cache_tag[i] = sector;
        std::memcpy(cache_data.get() + i * kSectorSize, src, kSectorSize);
    }
    void cache_invalidate(uint64_t sector) {
        const size_t i = static_cast<size_t>(sector % kCacheLines);
        if (cache_tag[i] == sector) cache_tag[i] = kNoTag;
    }
    bool cache_get(uint64_t sector, uint8_t* dst) const {
        const size_t i = static_cast<size_t>(sector % kCacheLines);
        if (cache_tag[i] != sector) return false;
        std::memcpy(dst, cache_data.get() + i * kSectorSize, kSectorSize);
        return true;
    }
};

std::array<Backing, FF_VOLUMES> g_backing;

} // namespace

namespace fatfs_glue {

bool attach(uint8_t pdrv, const std::string& image_path, uint32_t part_lba,
            uint32_t sector_count, std::string& err,
            std::shared_ptr<sd_write_journal::Journal> journal) {
    if (pdrv >= FF_VOLUMES) { err = "fatfs_glue: pdrv out of range"; return false; }
    Backing& b = g_backing[pdrv];
    b.active = false; // a failed re-attach leaves the drive not ready, never half torn down
    b.file.close();
    b.file.clear();
    b.cache_drop();
    b.journal = std::move(journal);
    if (b.journal) {
        b.file.open(image_path, std::ios::in | std::ios::binary);
        if (!b.file) {
            err = "fatfs_glue: cannot open image read-only: " + image_path;
            return false;
        }
    } else {
        b.file.open(image_path, std::ios::in | std::ios::out | std::ios::binary);
        if (!b.file) {
            err = "fatfs_glue: cannot open image r/w: " + image_path;
            return false;
        }
    }
    b.part_lba     = part_lba;
    b.sector_count = sector_count;
    b.cache_reset();
    b.active       = true;
    return true;
}

void detach(uint8_t pdrv) {
    if (pdrv >= FF_VOLUMES) return;
    Backing& b = g_backing[pdrv];
    if (b.file.is_open()) { b.file.flush(); b.file.close(); }
    b.file.clear();
    b.journal.reset();
    b.cache_drop();
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
    // Served from the cache only when every sector of the request is there;
    // otherwise the whole request is read from the file (a failed read fills
    // no line).
    bool all_cached = true;
    for (UINT i = 0; i < count && all_cached; ++i)
        all_cached = b.cache_get(sector + i, buff + static_cast<size_t>(i) * kSectorSize);
    if (!all_cached) {
        b.file.clear();
        b.file.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        b.file.read(reinterpret_cast<char*>(buff), len);
        if (b.file.gcount() != len) return RES_ERROR;
        for (UINT i = 0; i < count; ++i)
            b.cache_put(sector + i, buff + static_cast<size_t>(i) * kSectorSize);
    }
    // The journal overlay comes AFTER the cache, on both paths.
    if (b.journal) {
        for (UINT i = 0; i < count; ++i)
            b.journal->lookup(b.part_lba + static_cast<uint32_t>(sector) + i,
                              buff + static_cast<size_t>(i) * kSectorSize);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return RES_NOTRDY;
    Backing& b = g_backing[pdrv];
    if (static_cast<uint64_t>(sector) + count > b.sector_count) return RES_PARERR;
    if (b.journal) {
        for (UINT i = 0; i < count; ++i) {
            if (!b.journal->commit(b.part_lba + static_cast<uint32_t>(sector) + i,
                                   buff + static_cast<size_t>(i) * kSectorSize))
                return RES_ERROR;
        }
        return RES_OK;
    }
    const uint64_t off =
        (static_cast<uint64_t>(b.part_lba) + sector) * kSectorSize;
    const std::streamsize len =
        static_cast<std::streamsize>(count) * kSectorSize;
    b.file.clear();
    b.file.seekp(static_cast<std::streamoff>(off), std::ios::beg);
    b.file.write(reinterpret_cast<const char*>(buff), len);
    if (!b.file.good()) {
        // A failed write may have reached the file in part: the lines of the
        // request no longer match it, so they are dropped, never updated.
        for (UINT i = 0; i < count; ++i) b.cache_invalidate(sector + i);
        return RES_ERROR;
    }
    for (UINT i = 0; i < count; ++i)
        b.cache_put(sector + i, buff + static_cast<size_t>(i) * kSectorSize);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv >= FF_VOLUMES || !g_backing[pdrv].active) return RES_NOTRDY;
    Backing& b = g_backing[pdrv];
    switch (cmd) {
        case CTRL_SYNC:
            if (b.journal) return RES_OK; // every commit is already appended
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
