#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/sdcard_provisioner.h"   // DownloadFn / ConfirmFn / ProgressFn / BusyFn

/// The Pi Zero's NextPi directory, prepared on first use (`--pizero`).
/// Design: doc/design/PIZERO-DESIGN.md §3.6.
///
/// `PiQemu` boots NextPi from a directory holding the release's SD-card image
/// and the kernel and device tree QEMU loads directly. This module makes that
/// directory exist, the way `sdcard::provision_sd_card` makes the SD image
/// exist: the first run that needs it asks, downloads the NextPi release from
/// its mirror with a progress bar, checks the archive's MD5, unpacks the image,
/// and copies `kernel.img` and `bcm2708-rpi-zero.dtb` out of the image's FAT32
/// boot partition. Every later run finds it ready and touches no network.
///
/// THE DIRECTORY (default `<config-dir>/pizero`, next to `<config-dir>/sdcard`):
///   nextpi.img                    the release's SD-card image, never written
///   boot/kernel.img               copied out of the image's boot partition
///   boot/bcm2708-rpi-zero.dtb     ditto
///   release                       the release name it was made from
///   overlay.qcow2, qemu.log       made by PiQemu, not here
/// The archive is deleted once unpacked: it is ~6 GB, and the image ~15 GB.
///
/// THE RELEASE is a name on the mirror (`1_93D` → `NextPi-1_93D.tar.gz`), or
/// "latest" for the newest one listed there. A pinned name is the default
/// (`kDefaultRelease`), so what users run changes only when jnext does. A
/// directory made from a different release than the one asked for is
/// re-provisioned — after asking — and its overlay discarded, because an
/// overlay only makes sense over the image it was made on. With "latest", an
/// unreachable mirror is not an error when a release is already there: it is
/// used, with a warning.
///
/// POSIX ONLY, like the Pi link itself (`provision` refuses on Windows).
namespace pizero {

extern const char* const kDefaultRelease;   // "1_93D"
extern const char* const kLatest;           // "latest"
extern const char* const kMirrorUrl;        // "https://zx.xalior.com/NextPi2"

/// <config-dir>/pizero — $JNEXT_CONFIG_DIR when set, else $HOME/.jnext, as for
/// the SD image.
std::string default_dir();

/// The release names a mirror index page lists (`NextPi-<name>.tar.gz` links),
/// deduplicated. Pure.
std::vector<std::string> parse_release_listing(const std::string& html);

/// <0, 0, >0 as `a` sorts before, equal to, after `b`, comparing runs of digits
/// numerically (so 1_100 > 1_93D), as `sort -V` does. Pure.
int compare_release(const std::string& a, const std::string& b);

/// The release recorded in `dir`, or "" when `dir` is not a complete NextPi
/// directory (any of the image, the two boot files or the marker missing).
std::string prepared_release(const std::string& dir);

/// Lowercase hex MD5 of a file ("" if it cannot be read).
std::string md5_file(const std::string& path);

/// Stream the gzip-compressed tar `archive` and write the first regular-file
/// entry whose name ends in `suffix` to `out_path`. Handles GNU base-256 and
/// pax sizes (the NextPi image is past the 8 GB the octal field can hold) and
/// GNU/pax long names. `progress` (may be empty) gets compressed bytes read and
/// the archive size; returning false aborts.
bool extract_tar_gz_entry(const std::string& archive, const std::string& suffix,
                          const std::string& out_path, const sdcard::ProgressFn& progress,
                          std::string& err);

/// Copy kernel.img and bcm2708-rpi-zero.dtb from the first FAT32 partition of
/// `image_path` into `boot_dir`.
bool extract_boot_files(const std::string& image_path, const std::string& boot_dir,
                        std::string& err);

/// Terminal progress and busy messages for the NextPi download, worded for it
/// (sdcard::cli_progress / cli_busy say "SD-card image" and "a few seconds";
/// unpacking NextPi's 15 GB image takes about a minute).
bool cli_progress(uint64_t downloaded, uint64_t total);
bool cli_busy(const std::string& phase, const std::function<bool()>& work);

struct ProvisionOptions {
    std::string dir;                      ///< "" → default_dir()
    std::string release;                  ///< "" → kDefaultRelease; or "latest"
    std::string mirror;                   ///< "" → $JNEXT_PIZERO_MIRROR, else kMirrorUrl
    sdcard::DownloadFn download;          ///< defaults to sdcard::default_http_download
    sdcard::ConfirmFn  confirm;           ///< defaults to sdcard::cli_confirm
    sdcard::ProgressFn progress;          ///< download progress (may be empty)
    sdcard::BusyFn     busy;              ///< wraps check + unpack (may be empty)
    /// Free bytes needed before downloading; a test seam (production: ~22 GB).
    uint64_t           space_needed = 22ull * 1024 * 1024 * 1024;
};

struct ProvisionResult {
    sdcard::ProvisionStatus status = sdcard::ProvisionStatus::Failed;
    std::string dir;        ///< the ready directory (status Ok)
    std::string release;    ///< the release it holds (status Ok)
    std::string error;      ///< why not, otherwise
    std::string warning;    ///< Ok, but worth saying (e.g. "latest" unreachable)
};

/// Make `opts.dir` a ready NextPi directory for `opts.release`, downloading
/// only when it is not ready (or holds another release) and the user agrees.
ProvisionResult provision(const ProvisionOptions& opts);

} // namespace pizero
