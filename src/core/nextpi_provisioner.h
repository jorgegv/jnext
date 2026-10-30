#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/sdcard_provisioner.h"   // DownloadFn / ConfirmFn / ProgressFn / BusyFn

/// The NextPi directory, prepared on first use (`--nextpi`).
/// Design: doc/design/NEXTPI-DESIGN.md §3.6.
///
/// `PiQemu` boots NextPi from a directory holding the release's SD-card image
/// and the kernel and device tree QEMU loads directly. This module makes that
/// directory exist, the way `sdcard::provision_sd_card` makes the SD image
/// exist: the first run that needs it asks, downloads the NextPi release from
/// its mirror with a progress bar, checks the archive's MD5, unpacks the image,
/// and copies `kernel.img` and `bcm2708-rpi-zero.dtb` out of the image's FAT32
/// boot partition. Every later run finds it ready and touches no network.
/// An archive already in the directory (what a user fetched by hand) is
/// checked FIRST, before anything is asked: against the `.md5` beside it, or
/// the mirror's if there is none, and when it matches it is unpacked with no
/// question and no download. Nothing is deleted before the user agrees to a
/// download, and a download step that fails never deletes a complete file.
/// A directory that cannot be used is reported with the piece that is missing.
///
/// THE DIRECTORY (default `<config-dir>/nextpi`, next to `<config-dir>/sdcard`):
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
namespace nextpi {

extern const char* const kDefaultRelease;   // "1_93D"
extern const char* const kLatest;           // "latest"
extern const char* const kMirrorUrl;        // the NextPi release mirror (doc/REFERENCES.md)

/// True for a name the mirror could use for a release: 1..64 of letters,
/// digits, '.', '_' and '-', not starting with '.' or '-'. The release name
/// goes into a file name and a URL, so nothing else is accepted. Pure.
bool valid_release_name(const std::string& name);

/// <config-dir>/nextpi — $JNEXT_CONFIG_DIR when set, else $HOME/.jnext, as for
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

/// "" when `dir` is a complete NextPi directory, else the first thing wrong:
/// `<path> is missing`, or `<dir>/release is empty`. `prepared_release` is
/// built on it, so the layout is defined once.
std::string unprepared_reason(const std::string& dir);

/// Lowercase hex MD5 of a file ("" if it cannot be read).
std::string md5_file(const std::string& path);

/// How many times md5_file has been called in this process (a test hook: a
/// row proves a 5.7 GB hash is never taken outside the busy dialog).
uint64_t md5_file_calls();

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
/// (sdcard::make_cli_progress / cli_busy say "SD-card image" and "a few seconds";
/// unpacking NextPi's 15 GB image takes about a minute). The progress also says
/// the stall note once no data has arrived for 5 s (sdcard::ConsoleWait, GH #385),
/// on stderr and in the log (the uart logger). `out` and `now` are for the tests.
sdcard::ProgressFn make_cli_progress(std::FILE* out = stderr, sdcard::ConsoleNowFn now = {});
bool cli_busy(const std::string& phase, const std::function<bool()>& work);

/// The console's ShortFetchFn (GH #385): for the frontends with no dialog (--headless,
/// the SDL-only build). A short fetch that takes a second says what it fetches and from
/// where ("<label> <url>"), and one that stalls says so, with the dialog's words, on
/// stderr and in the log (the uart logger, as the rest of this module logs); a quicker
/// one says nothing. A fresh sdcard::ConsoleWait for each fetch, so every one counts
/// from its own start. All three parameters are for the tests: the log's copy of a line
/// (empty: the uart logger), the stream (stderr) and the clock (steady_clock).
class CliShortFetch {
public:
    explicit CliShortFetch(sdcard::ConsoleLogFn log = {}, std::FILE* out = stderr, sdcard::ConsoleNowFn now = {});
    bool operator()(const std::string& label, const std::string& url,
                    const std::function<bool(const sdcard::ProgressFn&)>& fetch) const;

private:
    sdcard::ConsoleLogFn log_;
    std::FILE*           out_;
    sdcard::ConsoleNowFn now_;
};

struct ProvisionOptions {
    std::string dir;                      ///< "" → default_dir()
    std::string release;                  ///< "" → kDefaultRelease; or "latest"
    std::string mirror;                   ///< "" → $JNEXT_NEXTPI_MIRROR, else kMirrorUrl
    sdcard::DownloadFn download;          ///< defaults to sdcard::default_http_download
    sdcard::ConfirmFn  confirm;           ///< defaults to sdcard::cli_confirm
    sdcard::ProgressFn progress;          ///< download progress (may be empty)
    sdcard::BusyFn     busy;              ///< wraps check + unpack (may be empty)
    /// Wraps the SHORT fetches only (the release list of "latest", the two .md5
    /// ones), so a frontend can show feedback when one is slow: the GUI a dialog,
    /// the console a line (CliShortFetch). It is told the label and the address of
    /// each. The archive download is not wrapped, it has `progress`. Empty: no
    /// feedback (the tests; GH #385).
    sdcard::ShortFetchFn short_fetch;
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

/// Whether to start NextPi, and whether the command line asked for it.
struct StartRequest {
    bool wanted       = false;   ///< try to start it
    bool asked_on_cli = false;   ///< --nextpi was given (a failure is then an error)
};

/// main.cpp's decision. `gui` is a GUI session — the only kind that reads
/// Preferences; `cli_set` is "--nextpi or --no-nextpi was given" and
/// `cli_value` which one; `saved_enabled` is the [nextpi] `enabled`
/// preference. The command line wins either way; without it, a GUI session
/// follows the preference and a headless one starts nothing. Pure.
StartRequest start_request(bool gui, bool cli_set, bool cli_value, bool saved_enabled);

/// The five text settings of Preferences > NextPi (empty = the built-in default), and the
/// optional command-line flag that sets each for one run (GH #407): --nextpi-dir,
/// --nextpi-release, --nextpi-qemu, --nextpi-audio, --nextpi-qemu-args.
struct Settings {
    std::string dir, release, qemu_binary, audio, qemu_args;
};
struct CliSettings {
    std::optional<std::string> dir, release, qemu_binary, audio, qemu_args;
};

/// The names of the --nextpi-* setting flags that were given, in a fixed order. main.cpp warns
/// about each when NextPi is not enabled for the run (GH #407). Pure.
std::vector<std::string> given_flags(const CliSettings& cli);

/// A flag that was given replaces the saved value, an empty one included (it means the
/// default); a flag that was not given leaves the saved value. Pure.
Settings merge_settings(const Settings& saved, const CliSettings& cli);

/// What jnext does once it has tried to start NextPi (main.cpp):
enum class StartOutcome {
    Started,          ///< it is running
    Declined,         ///< the user said no to the download: start without it, quietly
    WarnAndContinue,  ///< it failed, but was only enabled in Preferences: say so, start without it
    Exit,             ///< it failed, and --nextpi asked for it: an error, like any unusable option
};

/// The policy behind StartOutcome. `asked_on_cli` is "--nextpi was given";
/// `provisioned` is the provisioner's verdict; `started` is whether QEMU runs.
/// Declining is a choice, not a failure — the download dialog promises only
/// that NextPi is not started — and a preference must never stop jnext from
/// starting, so a broken QEMU cannot lock anyone out. Pure.
StartOutcome start_outcome(bool asked_on_cli, sdcard::ProvisionStatus provisioned, bool started);

/// Make `opts.dir` a ready NextPi directory for `opts.release`, downloading
/// only when it is not ready (or holds another release) and the user agrees.
ProvisionResult provision(const ProvisionOptions& opts);

} // namespace nextpi
