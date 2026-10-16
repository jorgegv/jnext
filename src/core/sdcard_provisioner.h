#pragma once

#include <cstdint>
#include <cstdio>
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
struct Fat32Tree; // core/fat32_image.h

namespace sdcard {

// Canonical constants.
extern const char* const kDistroUrl;           // specnext distro zip URL
extern const char* const kImageFileName;       // "cspect-next-1gb.img" (raw)
extern const char* const kFixedImageFileName;  // "cspect-next-1gb-fixed.img"

// The version of the recipe that turns the raw image into the fixed one
// (patch_image_fat32). Bump it whenever that recipe changes what it writes.
//   1 — FAT32 re-cluster to 8 KB clusters + default config.ini (Task 27).
//       Images made by this recipe carry no recipe sidecar.
//   2 — also doubles the 16 KB zx81.rom (GH #284).
// provision_sd_card records it next to the image it derives, in
// "<fixed image>" + kFixedImageRecipeSuffix, as the number and a newline.
// jnext itself never rebuilds an existing image because that number is old or
// missing: the image holds the user's own files. The regression suite's
// test-owned master IS rebuilt on it (test/00regression/scripts/
// 01-sdcard-provision.sh reads this constant from the .cpp, so keep its
// definition on one line in the form `const int kFixedImageRecipe = N;`).
extern const int kFixedImageRecipe;
extern const char* const kFixedImageRecipeSuffix; // ".recipe"

// GH #284 — SHA-256 of the 16 KB /MACHINES/NEXT/zx81.rom the 24.11
// distribution image ships (Paul Farrow's ZX81 emulator v3.13, tbblue commit
// 751d0b31).
extern const char* const kZx81Rom16kSha256;

// The per-user state dir (SD image, NextPi, config, warm start, snapshots):
// $JNEXT_CONFIG_DIR when set and non-empty, else $HOME/.jnext.
std::string default_state_dir();
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
// Content-Length). Both include bytes a resumed download already had on disk.
// Returning FALSE aborts the transfer (the download then fails with "cancelled
// by user"; the partial file is kept or removed by the rule below). May be
// empty (no progress reporting).
using ProgressFn = std::function<bool(uint64_t downloaded, uint64_t total)>;

// Download seam: fetch `url` into local file `dest_path`, reporting progress
// via `progress` (may be empty). Returns true on success, else sets `err`.
//
// THE PARTIAL-FILE CONTRACT (GH #358, resumable downloads). The default
// backends write into `<dest_path>.part`, so `dest_path` exists only once the
// transfer is complete (renamed over any older file). Beside it they keep a
// resume record `<dest_path>.part.resume` (url, validator, full size). On a
// failure or cancel the partial and its record are KEPT if and only if the part
// holds bytes AND the server sent a validator (a strong ETag, else
// Last-Modified); otherwise both are removed. The next call for the same url
// asks the server for the rest (`Range` + `If-Range`) and appends only when it
// answers 206 for exactly that range; any other answer starts again from 0.
// The file names, the record format and the resumability test live below, once.

// `err` is a BARE reason — "Could not resolve hostname", "HTTP status 404" —
// and must NOT carry a "download failed" of its own. provision_sd_card adds
// that framing exactly once, and both default backends used to add it too, so
// the reported message read `download failed: download failed: Could not
// resolve hostname` (GH #271: the doubled text is in the bug report itself).
// PROV-PREFIX-01/02 pin the single-prefix form on the real backend.
// Default impl is platform-native: libcurl on Linux/macOS
// (sdcard_provisioner_net_curl.cpp), WinHTTP on Windows
// (sdcard_provisioner_net_win.cpp — GH #108 Phase B, so the Windows packages
// carry no curl/OpenSSL DLLs and stay Win7-clean). Test seam: the env var
// $JNEXT_SDCARD_DISTRO_URL, when set and non-empty, replaces kDistroUrl in
// provision_sd_card (points the real download path at a local fixture server;
// never documented to users).
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

// Is `path` an existing regular file? Not ::stat: MinGW's stat is stat64i32 and
// fails with EOVERFLOW for a file of 4 GiB or more, so on Windows a finished
// 5.7 GB NextPi download (or a large SD image) read as absent (GH #358).
bool regular_file_exists(const std::string& path);

std::string partial_path(const std::string& dest);          // dest + ".part"
std::string resume_record_path(const std::string& dest);    // dest + ".part.resume"

struct ResumeInfo {
    std::string url;
    std::string validator_kind;   // "etag" or "date"
    std::string validator;        // the ETag / Last-Modified value, verbatim
    uint64_t    total = 0;        // the full size of the resource
};
bool read_resume_info(const std::string& dest, ResumeInfo& out);   // false: none / malformed
bool write_resume_info(const std::string& dest, const ResumeInfo& info);
void discard_partial(const std::string& dest);   // removes .part and its record
// The state of `<dest>.part` for `url` (the ONE classifier):
//   None      no .part
//   Stale     .part with no/unreadable record, a record for another url, or empty
//   Resumable record for url, 0 < size < total          (have/total set)
//   Complete  record for url, size == total             (have = total)
//   Oversize  record for url, size >  total             (foreign or damaged)
// A Complete partial is finished (renamed) with no request, never refetched.
enum class PartialState { None, Resumable, Complete, Oversize, Stale };
PartialState partial_state(const std::string& dest, const std::string& url,
                           uint64_t& have, uint64_t& total);
// partial_state() == Resumable.
bool resumable_partial(const std::string& dest, const std::string& url,
                       uint64_t& have, uint64_t& total);
// Turn a Complete partial for `url` into `dest` with no transfer. False if the
// rename fails (err set, partial kept) or if the partial is not Complete (err
// unset; a Stale or Oversize one is discarded): check partial_state() first.
bool finish_complete_partial(const std::string& dest, const std::string& url, std::string& err);

// What the two backends read off an HTTP response (header names are matched
// without regard to case: HTTP/2 sends them lowercase).
struct HttpResponseInfo {
    long        status = 0;
    std::string content_range, etag, last_modified;
    bool        has_content_length = false;
    uint64_t    content_length = 0;
};
// Feed one header line ("HTTP/1.1 206 Partial Content", "ETag: ...") of the
// LAST response; a status line starts a new response (redirects send several).
void parse_response_line(const std::string& line, HttpResponseInfo& r);

// One download attempt's file handling, shared by both backends so the
// decision table exists once. Usage: construct; send `range_value()` as Range
// and `if_range_value()` as If-Range when ranged(); call begin() once the
// response headers are known; write() each body chunk; then finish() on a
// complete body or fail() otherwise. `allow_resume` false = a plain GET.
class PartialDownload {
public:
    PartialDownload(const std::string& url, const std::string& dest, bool allow_resume);
    ~PartialDownload();
    PartialDownload(const PartialDownload&) = delete;
    PartialDownload& operator=(const PartialDownload&) = delete;

    bool ranged() const { return ranged_; }
    // The partial is already the whole file: call finish(), make no request.
    bool complete() const { return complete_; }
    std::string range_value() const;      // "bytes=<have>-"
    std::string if_range_value() const;   // the validator, verbatim

    enum class Begin { Ok, Retry, Error };
    // Decide on the response BEFORE any body byte is written. Retry: the
    // partial was discarded, send a plain GET (once). Error: `err` is set.
    Begin begin(const HttpResponseInfo& r, std::string& err);
    bool begun() const { return begun_; }
    bool write(const char* data, std::size_t n);
    // Report to `progress` with `got` body bytes of this response received;
    // false = the caller cancelled.
    bool report(const ProgressFn& progress, uint64_t got) const;
    // Complete body: check the size, move .part to dest. False sets `err`.
    bool finish(std::string& err);
    // Any other failure or cancel: apply the keep-or-remove rule. `clean_eof`:
    // the transport ended the body without an error of its own.
    void fail(std::string& err, bool clean_eof);
    void cancelled();
    uint64_t total() const { return total_; }

private:
    void close_file();
    uint64_t part_size() const;
    std::string url_, dest_;
    bool ranged_ = false, begun_ = false, complete_ = false;
    uint64_t have_ = 0, base_ = 0, total_ = 0;
    ResumeInfo rec_;
    FILE* f_ = nullptr;
};

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

// SHA256 helpers (OpenSSL EVP on Linux/macOS, BCrypt/CNG on Windows — same
// per-platform split as the download above). `sha256_hex` digests an
// in-memory buffer; `sha256_file` digests a whole file. Both return a
// lowercase hex string ("" on error, e.g. the file cannot be read).
std::string sha256_hex(const std::vector<uint8_t>& bytes);
std::string sha256_file(const std::string& path);

// Extract a single named entry (matched by basename, case-insensitive) from
// the zip file `zip_path` into `out_path`. Supports stored (method 0) and
// deflated (method 8) entries; streams so 1 GB entries do not need 1 GB RAM.
bool unzip_entry(const std::string& zip_path, const std::string& entry_basename,
                 const std::string& out_path, std::string& err);

// GH #284. If `tree` holds MACHINES/NEXT/zx81.rom (matched case-insensitively)
// and that file is exactly the 16 KB file whose SHA-256 is `known_sha256`,
// replace it with itself twice (32 KB) and return true. Any other zx81.rom is
// left untouched, and a missing one is not created; both return false.
bool double_known_zx81_rom(Fat32Tree& tree, const std::string& known_sha256);

// Fix a freshly extracted image in place: FAT32 recluster to 8 KB clusters
// (so cluster count clears the FAT32 spec minimum), inject the default
// /MACHINES/NEXT/config.ini, and double the known 16 KB zx81.rom
// (double_known_zx81_rom). This is recipe kFixedImageRecipe. Returns true on
// success, else sets `err`. `zx81_known_sha256` is a test seam: tests cannot
// ship the ROM, so they pass the hash of a stand-in file. Production callers
// leave the default.
bool patch_image_fat32(const std::string& image_path, std::string& err,
                       const std::string& zx81_known_sha256 = kZx81Rom16kSha256);

struct ProvisionOptions {
    std::string explicit_path;      // --sdcard value ("" if not given)
    bool        auto_confirm  = false; // --sdcard-download-confirm
    bool        force_download = false; // --sdcard-download-force
    DownloadFn  download;           // defaults to default_http_download
    ConfirmFn   confirm;            // defaults to cli_confirm
    ProgressFn  progress;           // optional; passed into the download
    CopyFn      copy;               // defaults to default_copy_file
    BusyFn      busy;               // optional; wraps the copy+patch step

    // GH #271 — diagnosability seam. Path whose EXISTENCE means "this process
    // runs inside a Flatpak sandbox"; /.flatpak-info is the conventional
    // marker — flatpak mounts it into every sandbox (verified by reading it
    // from inside jnext's own bundle), and it is what portal clients test.
    // When it is there, a download failure
    // gets a note saying the sandbox may simply have no network — because the
    // reporter of #271 reasonably read "Could not resolve hostname" as
    // "jnext's downloader is broken" and went off to prove wget worked.
    //
    // It is a FIELD rather than a hard-coded constant so both branches are
    // testable on a host that is not in a sandbox: a test points it at a
    // temporary file. Production never sets it, and off a Flatpak the file
    // does not exist, so nothing changes for anyone else.
    std::string sandbox_marker = "/.flatpak-info";
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
