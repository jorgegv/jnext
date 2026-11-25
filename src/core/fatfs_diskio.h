#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace sd_write_journal { class Journal; }

/// Glue that backs ChaN FatFs (third_party/fatfs) on host image files.
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
//
// With a `journal` (an SD-writes journal bound to the image) the drive runs in
// journal mode: the image is opened read-only and never written; reads are the
// image with the journal's sectors laid over them, and writes are committed to
// the journal. The journal's sector numbers are absolute card sectors, so the
// glue adds `part_lba` to every FatFs sector. Without one (the default, and
// what the SD-card provisioner uses) the image is opened read/write.
//
// `read_only` opens the image read-only whatever else is given (GH #555: a comparison must not be
// able to write): every write then fails with RES_WRPRT. Together with a journal it is the card
// as the machine sees it, journal sectors laid over the image, nothing written anywhere.
bool attach(uint8_t pdrv, const std::string& image_path, uint32_t part_lba,
            uint32_t sector_count, std::string& err,
            std::shared_ptr<sd_write_journal::Journal> journal = nullptr,
            bool read_only = false);

// Flush and close the backing file for `pdrv`.
void detach(uint8_t pdrv);

/// Test observables (GH #558): how many attaches and how many SUCCESSFUL disk_write calls this process has made. The
/// second is the number of times a write was noted in SdCardDevice::host_write_generation() (once per successful call,
/// never for a refused or failed write, never for a read).
std::uint64_t attach_count_for_test();
std::uint64_t write_count_for_test();

} // namespace fatfs_glue
