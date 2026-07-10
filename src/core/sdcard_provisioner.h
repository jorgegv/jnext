#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// SD-card image provisioning (Task 27).
///
/// jnext needs a NextZXOS SD image to boot. Historically `--sd-card FILE` was
/// mandatory. This module adds a default-location + optional-download flow so
/// a user can start with just a `.nex` (or nothing) and no `--sd-card`:
///
///   1. `--sd-card FILE` given  → use it verbatim (explicit ALWAYS wins,
///      unconditionally — even alongside --sdcard-download-force, which is
///      ignored in that case, since the named file may be any image).
///   2. else look in $HOME/.jnext/sdcard/cspect-next-1gb.img → use if present.
///   3. else offer to download the canonical distro zip, extract the image,
///      and fix it in place (FAT32 recluster) so our strict reader accepts it.
///
/// --sdcard-download-force forces the re-download+re-patch in step 3 (to
/// recover a corrupted default-location image); it never applies to an
/// explicit --sd-card path.
///
/// The network fetch, the unzip, and the FAT32 patch are separate functions so
/// each is independently unit-testable offline; the network fetch and the user
/// confirmation are behind std::function seams so tests never touch the network.
namespace sdcard {

// Canonical constants.
extern const char* const kDistroUrl;      // specnext distro zip URL
extern const char* const kImageFileName;  // "cspect-next-1gb.img"

// $HOME/.jnext/sdcard  and  $HOME/.jnext/sdcard/cspect-next-1gb.img
std::string default_sdcard_dir();
std::string default_sdcard_image_path();

// The default config.ini injected into /MACHINES/NEXT (matches
// tools/fix-sdcard-image.sh). Returned as raw bytes.
std::vector<uint8_t> default_config_ini();

// Download seam: fetch `url` into local file `dest_path`. Returns true on
// success, else sets `err`. Default impl shells out to curl/wget.
using DownloadFn = std::function<bool(const std::string& url,
                                      const std::string& dest_path,
                                      std::string& err)>;

// Confirmation seam: returns true if the user agrees to download.
using ConfirmFn = std::function<bool(const std::string& message)>;

bool default_http_download(const std::string& url, const std::string& dest_path,
                           std::string& err);
bool cli_confirm(const std::string& message);

// Extract a single named entry (matched by basename, case-insensitive) from
// the zip file `zip_path` into `out_path`. Supports stored (method 0) and
// deflated (method 8) entries; streams so 1 GB entries do not need 1 GB RAM.
bool unzip_entry(const std::string& zip_path, const std::string& entry_basename,
                 const std::string& out_path, std::string& err);

// Fix a freshly extracted image in place: FAT32 recluster to 8 KB clusters
// (so cluster count clears the FAT32 spec minimum) and inject the default
// /MACHINES/NEXT/config.ini. Returns true on success, else sets `err`.
bool patch_image_fat32(const std::string& image_path, std::string& err);

struct ProvisionOptions {
    std::string explicit_path;      // --sd-card value ("" if not given)
    bool        auto_confirm  = false; // --sdcard-download-confirm
    bool        force_download = false; // --sdcard-download-force
    DownloadFn  download;           // defaults to default_http_download
    ConfirmFn   confirm;            // defaults to cli_confirm
};

enum class ProvisionStatus { Ok, Declined, Failed };

struct ProvisionResult {
    ProvisionStatus status = ProvisionStatus::Failed;
    std::string     path;   // resolved image path (valid when status == Ok)
    std::string     error;  // human-readable reason when not Ok
};

// Resolve (and if needed download+patch) the SD image path per the policy
// above. Never touches the network unless a download is actually required
// AND confirmed.
ProvisionResult provision_sd_card(const ProvisionOptions& opts);

} // namespace sdcard
