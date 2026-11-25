#pragma once

#include "core/sdcard_file_add.h"   // FileAddStatus: every SD card verb shares its exit statuses

#include <cstdint>
#include <string>
#include <vector>

/// Copy a file or a directory OUT of an SD-card image into a host directory
/// (GH #557): the engine behind `--sdcard-file-extract PATH`, a copy-and-exit
/// command like `--sdcard-file-add`, in the other direction.
///
/// The engine is the vendored ChaN FatFs, mounted READ-ONLY on the volume the
/// firmware would mount (find_sd_fat_volume: FAT12/16/32, partitioned or not),
/// with the card's SD-writes file laid over it when one is bound. It is the
/// same library family the Next's firmware runs, so long names, 8.3 aliases
/// and case rules are the machine's own. An under-clustered FAT32 image, which
/// the firmware refuses, is refused too (status 5).
///
/// The rules, settled in the issue and the developer guide:
///   * The card path separates with '/' or '\' (a FAT name cannot contain
///     either); the leading separator is optional; a component is matched
///     against the long and the 8.3 name, ASCII letters in any case.
///   * The local entry is named after the card's STORED name, escaped for the
///     host with sdcard::carry::escape_name(name, true) on every OS (the same
///     names `--sdcard-export-changes` writes), never the spelling typed.
///   * An existing local entry of that name (file, directory or symbolic link,
///     dangling or not) is refused, untouched: an extraction never overwrites.
///   * ALL OR NOTHING: every structural problem is found before anything is
///     created; a failure while copying removes what this run created.
///   * Nothing is written to the image or to the writes file.
namespace sdcard {

/// What extract_from_image() did.
struct ExtractSummary {
    bool        is_dir = false;
    std::string card_path;            ///< as stored on the card, '/'-separated, bytes outside printable ASCII as %XX
    std::string host_path;            ///< the top-level local entry created
    unsigned    files = 0, dirs = 0;  ///< dirs counts the top one for a directory
    uint64_t    bytes = 0;
    std::vector<std::string> escaped; ///< host relative paths with an escaped component
    bool        through_writes = false;  ///< read through a bound --sdcard-writes file that exists
};

/// Copy the file or directory at `card_path` on `image_path` (seen through its bound SD-writes
/// file, if any) into `dest_dir`, under its stored name.
///
/// Statuses (FileAddStatus, as every SD card verb): Ok 0; SourceUnreadable 2 the card path is
/// malformed or names nothing on the card; DestInvalid 3 `dest_dir` is not a directory or a local
/// entry cannot be created or written; DestExists 4 the local name exists; ImageUnusable 5 the card
/// is missing, not a FAT volume FatFs mounts, not 512-byte sectors, truncated, a writes file that
/// rewrote its partition table or boot sector, or a damaged directory structure (a directory
/// reached twice, two names of one directory that the host cannot tell apart, a file that does not
/// hold its size, a name FatFs cannot open by either of its names). ImageFull 6 is not used.
/// `err` receives a one-sentence explanation on failure.
///
/// `card_path_is_bytes` (default false, the command line's behaviour): the path is CP437 BYTES the caller took from a
/// listing of this card (the SD card file manager), which FatFs matches natively, so a byte above 0x7E is accepted and
/// only control bytes are refused. The command line's strings are UTF-8 and cannot carry such a byte, so it refuses
/// them with the advice to use the 8.3 name or the parent folder (and that advice has no answer when the 8.3 name
/// itself holds a high byte: the parent folder is then the only route).
/// PRECONDITION in that mode: every component of the path is the `open_name` its listing gives for that entry (the
/// spelling FatFs resolves to THAT entry). A component FatFs resolves to a different entry than the listing's (a
/// duplicate 8.3 name behind an earlier entry) is refused (SourceUnreadable) instead of extracting the other entry's
/// bytes under this one's name. Control bytes and DEL are refused.
FileAddStatus extract_from_image(const std::string& image_path, const std::string& card_path,
                                 const std::string& dest_dir, std::string& err,
                                 ExtractSummary* summary = nullptr, bool card_path_is_bytes = false);

}  // namespace sdcard
