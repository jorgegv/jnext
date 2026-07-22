#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// SD-card image provisioning (Task 27).
///
/// jnext needs a NextZXOS SD image to boot. Historically `--sdcard FILE` was
/// mandatory. This module adds a default-location + optional-download flow so
/// a user can start with just a `.nex` (or nothing) and no `--sdcard`:
///
///   1. `--sdcard FILE` given  → use it verbatim (explicit ALWAYS wins,
///      unconditionally — even alongside --sdcard-download-force, which is
///      ignored in that case, since the named file may be any image).
///   2. else look in <config-dir>/sdcard/cspect-next-1gb-fixed.img → use if
///      present, where <config-dir> is $JNEXT_CONFIG_DIR when set, else
///      $HOME/.jnext. This is the PATCHED (FAT32-reclustered) image jnext
///      actually boots from — distinct from the raw download.
///   3. else offer to download the canonical distro zip, extract the raw
///      official `cspect-next-1gb.img` (kept pristine), record its SHA256 in a
///      `cspect-next-1gb.img.sha256` sidecar, copy it to
///      `cspect-next-1gb-fixed.img`, and FAT32-recluster THAT copy so our
///      strict reader accepts it. The fixed image is what is returned/used.
///
/// If the raw `cspect-next-1gb.img` is already present but the fixed one is
/// not, the (large) re-download is skipped and the fixed image is produced
/// straight from the existing raw file — but ONLY if the raw's SHA256 matches
/// its `.sha256` sidecar. A missing/unreadable sidecar or a hash mismatch
/// marks the raw untrusted and forces the full download cycle again.
///
/// --sdcard-download-force forces the re-download+re-patch in step 3 (to
/// recover a corrupted default-location image); it never applies to an
/// explicit --sdcard path.
///
/// The network fetch, the unzip, and the FAT32 patch are separate functions so
/// each is independently unit-testable offline; the network fetch and the user
/// confirmation are behind std::function seams so tests never touch the network.
namespace sdcard {

// Canonical constants.
extern const char* const kDistroUrl;           // specnext distro zip URL
extern const char* const kImageFileName;       // "cspect-next-1gb.img" (raw)
extern const char* const kFixedImageFileName;  // "cspect-next-1gb-fixed.img"

// <config-dir>/sdcard, where <config-dir> is $JNEXT_CONFIG_DIR when set and
// non-empty, else $HOME/.jnext. The override matches AppConfig and the
// debugger window, which resolve the SAME directory the same way; the
// regression suite uses it to give each run a private image (GH #65).
std::string default_sdcard_dir();
// <config-dir>/sdcard/cspect-next-1gb-fixed.img — the PATCHED image jnext
// looks for and boots from at the default location.
std::string default_sdcard_image_path();
// <config-dir>/sdcard/cspect-next-1gb.img — the raw, pristine download the
// fixed image is produced from (kept for skip-redownload).
std::string default_sdcard_raw_image_path();

// The default config.ini injected into /MACHINES/NEXT (matches
// tools/fix-sdcard-image.sh). Returned as raw bytes.
std::vector<uint8_t> default_config_ini();

// Progress seam: invoked periodically during a download with the number of
// bytes downloaded so far and the total (0 if the server sent no
// Content-Length). Returning FALSE aborts the transfer (the download then
// fails and any partial file is removed). May be empty (no progress reporting).
using ProgressFn = std::function<bool(uint64_t downloaded, uint64_t total)>;

// Download seam: fetch `url` into local file `dest_path`, reporting progress
// via `progress` (may be empty). Returns true on success, else sets `err`.
// Default impl uses libcurl.
using DownloadFn = std::function<bool(const std::string& url,
                                      const std::string& dest_path,
                                      const ProgressFn& progress,
                                      std::string& err)>;

// Confirmation seam: returns true if the user agrees to download.
using ConfirmFn = std::function<bool(const std::string& message)>;

// Busy-operation seam: run the slow post-download work (`work` — the raw→fixed
// copy plus the multi-second FAT32 patch) while giving the user feedback under
// the label `phase` (e.g. "Fixing downloaded image…"), so the app never looks
// hung during it. Returns whatever `work` returns. May be empty → the caller
// runs `work` directly with no UI. The GUI wires this to a busy dialog animated
// on a worker thread; the CLI wires `cli_busy` (a one-line message).
using BusyFn = std::function<bool(const std::string& phase,
                                  const std::function<bool()>& work)>;

// Copy seam: byte-copy `src` to `dst`, verifying completeness. Returns true on
// success, else sets `err`. Default impl streams and checks the copied size
// against the source size. Behind a seam so the failure path is unit-testable.
using CopyFn = std::function<bool(const std::string& src,
                                  const std::string& dst, std::string& err)>;

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err);
bool cli_confirm(const std::string& message);
// Lightweight textual (\r NN%) progress for the CLI/headless path.
bool cli_progress(uint64_t downloaded, uint64_t total);
// Default CLI BusyFn: prints "<phase>… " to stderr, runs `work`, then prints
// "done"/"failed". No thread/spinner (a terminal user tolerates a short wait,
// and the message alone shows the app is working).
bool cli_busy(const std::string& phase, const std::function<bool()>& work);
// Real copy implementation (default CopyFn). Streams in chunks and fails on a
// short/incomplete copy (removing the partial destination).
bool default_copy_file(const std::string& src, const std::string& dst,
                       std::string& err);

// SHA256 helpers (OpenSSL EVP). `sha256_hex` digests an in-memory buffer;
// `sha256_file` digests a whole file. Both return a lowercase hex string
// ("" on error, e.g. the file cannot be read).
std::string sha256_hex(const std::vector<uint8_t>& bytes);
std::string sha256_file(const std::string& path);

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
    std::string explicit_path;      // --sdcard value ("" if not given)
    bool        auto_confirm  = false; // --sdcard-download-confirm
    bool        force_download = false; // --sdcard-download-force
    DownloadFn  download;           // defaults to default_http_download
    ConfirmFn   confirm;            // defaults to cli_confirm
    ProgressFn  progress;           // optional; passed into the download
    CopyFn      copy;               // defaults to default_copy_file
    BusyFn      busy;               // optional; wraps the copy+patch step
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
