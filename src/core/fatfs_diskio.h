#pragma once

#include <cstdint>
#include <string>

/// Glue that backs ChaN FatFs (src/third_party/fatfs) on host image files.
///
/// FatFs addresses a "physical drive" through the disk_read / disk_write /
/// disk_ioctl callbacks. Here each drive is an MBR-partitioned image file: the
/// glue applies the partition's start-LBA offset so FatFs sees a linear volume
/// that begins at the partition's BPB (sector 0 of the presented drive). This
/// lets f_mkfs format the partition in place without disturbing the MBR.
///
/// Only 512-byte sectors are supported (FF_MIN_SS == FF_MAX_SS == 512).
namespace fatfs_glue {

// Attach `image_path` (opened read/write) to FatFs physical drive `pdrv`
// (0..FF_VOLUMES-1). FatFs will see `sector_count` 512-byte sectors starting
// at byte offset `part_lba * 512` in the file. Returns true, else sets `err`.
bool attach(uint8_t pdrv, const std::string& image_path, uint32_t part_lba,
            uint32_t sector_count, std::string& err);

// Flush and close the backing file for `pdrv`.
void detach(uint8_t pdrv);

} // namespace fatfs_glue
