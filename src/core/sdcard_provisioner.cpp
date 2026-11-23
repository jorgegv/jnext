#include "core/sdcard_provisioner.h"
#include "core/fat32_image.h"
#include "core/log.h"
#include "core/minimal_card.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_upgrade.h"
#include "core/sdcard_backup.h"
#include "core/image_lock.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <sys/types.h>

namespace sdcard {

const char* const kDistroUrl =
    "https://www.specnext.com/distro/26.11a/sn-emulator-26.11a.zip";
const char* const kImageFileName = "cspect-next-1gb.img";
const char* const kFixedImageFileName = "cspect-next-1gb-fixed.img";
const int kFixedImageRecipe = 3;
const char* const kFixedImageRecipeSuffix = ".recipe";
const char* const kDistroSuffix = ".distro";
const char* const kLegacyDistribution = "sn-emulator-24.11.zip";
const char* const kDeclinedSuffix = ".distro-declined";
const char* const kZx81Rom16kSha256 =
    "c294e4b60a0eba85c02a3d8b37f77de6c4516d873682dcf333813bd392d82aac";
}  // namespace sdcard

// The one distribution this release accepts for the browser's minimal card (GH #458), next to
// the URL it is downloaded from (the URL is written once, in kDistroUrl above). A release that
// moves to a new distribution changes kDistroUrl and ALL of this in one commit: the size and the
// two input SHA-256s come from the developer's own download, card_sha256 from
// `make-minimal-card --unpinned` on it (recipe stays unless the builder's output changed).
// MCARD-16 and `make minimal-image-test` fail on a partial update.
namespace minimal_card {
const Pins kDistribution = {
    sdcard::kDistroUrl,
    "sn-emulator-26.11a.zip",
    68971934ull,
    "0245d847da440155fb6f55d6cfadc64c0158118d7cea98066395d68f93fb3ab0",
    "e2b635a27703741ef20bfb4597f3a2dd46257af175fa44ddd8f0b9dba8408b23",
    "02d7a324c1c5ee7f8671c7fe82b61fdef6689863f8d5fc877f54135145048690",
    2,
};
}  // namespace minimal_card

namespace sdcard {

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
namespace {
// HOME first, USERPROFILE second: the order that makes this the directory
// QDir::homePath() reports on both platforms (src/core/screenshot.cpp does the
// same). A native Windows session normally has no HOME, and without the second
// name the SD image and the NextPi directory landed in the launch folder while
// jnext.conf went to %USERPROFILE%\.jnext.
std::string home_dir() {
    for (const char* var : {"HOME", "USERPROFILE"}) {
        const char* h = std::getenv(var);
        if (h && *h) return h;
    }
    return ".";
}
// jnext's per-user state directory. $JNEXT_CONFIG_DIR overrides it, exactly as
// in src/gui/app_config.cpp (AppConfig::default_config_path) and
// src/debugger/debugger_window.cpp (jnext_config_dir) — the SD image has always
// lived in the SAME directory as jnext.conf (see src/gui/app_config.h, which
// also holds the debugger layout since GH #312), so both must resolve it the
// same way. The
// provisioner not reading the variable was an oversight, and it is what forced
// every regression run to share one mutable 1 GB image (GH #65): the suite now
// points $JNEXT_CONFIG_DIR at a per-run directory holding a private clone.
std::string config_dir() {
    const char* d = std::getenv("JNEXT_CONFIG_DIR");
    if (d && *d) return d;
    return home_dir() + "/.jnext";
}
// Create a directory path recursively (best effort). Returns true if the
// final directory exists afterwards. Uses std::filesystem so it is portable
// (POSIX mkdir() takes a mode argument; MinGW/Windows mkdir() does not).
bool make_dirs(const std::string& path) {
    // create_directories applies the OS default mode (0777 & ~umask, normally
    // 0755) — same result as the previous explicit mkdir(..., 0755) under a
    // typical umask of 022. This is a private per-user cache dir, not a
    // permission-sensitive location, so the umask default is fine.
    std::error_code ec;
    std::filesystem::create_directories(path, ec); // no error if it exists
    return std::filesystem::is_directory(path);
}
// GH #271 — if a download fails while we are inside a Flatpak sandbox, say so.
//
// A Flatpak without --share=network has its own network namespace with nothing
// but loopback in it, so libcurl reports "Could not resolve hostname" for a
// host the very same machine resolves fine one shell out. That is a confusing
// message to receive, and the reporter of #271 drew the only reasonable
// conclusion available to him — that jnext's downloader was broken — and spent
// his time proving wget worked instead of looking at sandbox permissions.
//
// jnext's own Flatpak now ships the permission (and
// packaging/flatpak/verify-permissions.sh keeps it there), but this note is
// still worth its four lines: an older install, a third-party build, or a
// `flatpak override --unshare=network` all land a user in exactly the same
// place with exactly the same message.
//
// Returns "" — and therefore changes NOTHING — outside a sandbox.
std::string sandbox_network_note(const ProvisionOptions& opts) {
    if (opts.sandbox_marker.empty() || !regular_file_exists(opts.sandbox_marker))
        return {};
    return "\n  note: jnext is running inside a Flatpak sandbox, which has its own"
           "\n  network namespace. If the same URL downloads fine outside the sandbox,"
           "\n  the sandbox has no network access. Check and grant it with:"
           "\n    flatpak info --show-permissions io.github.zxjogv.jnext"
           "\n    flatpak override --user --share=network io.github.zxjogv.jnext";
}
void real_sleep_ms(unsigned ms, const std::string&) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
std::string basename_of(const std::string& p) {
    auto pos = p.find_last_of("/\\");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}
bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}
// Read the hex digest token out of a sha256sum-style sidecar file
// ("<hex>  <filename>\n"). Returns "" if the file is missing/unreadable or
// holds no leading hex token.
std::string read_stored_sha256(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string tok;
    if (!(f >> tok)) return {};
    for (char c : tok)
        if (!std::isxdigit((unsigned char)c)) return {};
    for (char& c : tok) c = static_cast<char>(std::tolower((unsigned char)c));
    return tok;
}
// First line of a small text sidecar with surrounding white space removed; "" when absent.
std::string read_text_line(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (!f || !std::getline(f, line)) return {};
    const char* ws = " \t\r\n";
    const size_t b = line.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    return line.substr(b, line.find_last_not_of(ws) - b + 1);
}
bool write_text_line(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::trunc);
    f << text << "\n";
    f.close();
    return static_cast<bool>(f);
}
} // namespace

std::string distribution_of_url(const std::string& url) {
    std::string u = url;
    const size_t q = u.find_first_of("?#");
    if (q != std::string::npos) u.resize(q);
    const size_t slash = u.find_last_of('/');
    return slash == std::string::npos ? u : u.substr(slash + 1);
}

std::string distribution_title(const std::string& distribution) {
    static const char pre[] = "sn-emulator-", post[] = ".zip";
    const size_t lp = sizeof pre - 1, lq = sizeof post - 1;
    if (distribution.size() > lp + lq && distribution.compare(0, lp, pre) == 0 &&
        distribution.compare(distribution.size() - lq, lq, post) == 0) {
        const std::string v = distribution.substr(lp, distribution.size() - lp - lq);
        // Digits and dots, then at most one lower-case letter after a digit ("26.11a").
        const size_t n = v.find_first_not_of("0123456789.");
        const bool ok = n == std::string::npos ||
                        (n + 1 == v.size() && n > 0 && v[n] >= 'a' && v[n] <= 'z' &&
                         std::isdigit(static_cast<unsigned char>(v[n - 1])));
        if (ok) return "NextZXOS " + v;
    }
    return distribution;
}

uint32_t volume_id_from_sha256(const std::string& hex) {
    if (hex.size() < 8) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 8; ++i) {
        const char c = hex[static_cast<size_t>(i)];
        if (!std::isxdigit(static_cast<unsigned char>(c))) return 0;
        v = (v << 4) | static_cast<uint32_t>(c <= '9' ? c - '0' : (std::tolower(c) - 'a' + 10));
    }
    return v;
}

bool regular_file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

// ---------------------------------------------------------------------------
// Resumable downloads (GH #358): the partial-file contract, once. See the
// comment above DownloadFn in sdcard_provisioner.h.
// ---------------------------------------------------------------------------
std::string partial_path(const std::string& dest) { return dest + ".part"; }
std::string resume_record_path(const std::string& dest) { return partial_path(dest) + ".resume"; }

namespace {
uint64_t file_bytes(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

std::string trimmed(std::string v) {
    const char* ws = " \t\r\n";
    const auto b = v.find_first_not_of(ws);
    if (b == std::string::npos) return {};
    return v.substr(b, v.find_last_not_of(ws) - b + 1);
}

bool starts_with_icase(const std::string& s, const char* lower) {
    for (std::size_t i = 0; lower[i]; ++i) {
        if (i >= s.size() || std::tolower(static_cast<unsigned char>(s[i])) != lower[i]) return false;
    }
    return true;
}
} // namespace

bool read_resume_info(const std::string& dest, ResumeInfo& out) {
    std::ifstream f(resume_record_path(dest), std::ios::binary);
    if (!f) return false;
    std::string url, val, total;
    if (!std::getline(f, url) || !std::getline(f, val) || !std::getline(f, total)) return false;
    url = trimmed(url);
    const auto sp = val.find(' ');
    if (url.empty() || sp == std::string::npos) return false;
    ResumeInfo r;
    r.url = url;
    r.validator_kind = val.substr(0, sp);
    r.validator = trimmed(val.substr(sp + 1));
    if ((r.validator_kind != "etag" && r.validator_kind != "date") || r.validator.empty()) return false;
    char* end = nullptr;
    const std::string t = trimmed(total);
    r.total = std::strtoull(t.c_str(), &end, 10);
    if (t.empty() || *end != '\0' || r.total == 0) return false;
    out = r;
    return true;
}

bool write_resume_info(const std::string& dest, const ResumeInfo& info) {
    std::ofstream f(resume_record_path(dest), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << info.url << "\n" << info.validator_kind << " " << info.validator << "\n" << info.total << "\n";
    return static_cast<bool>(f);
}

void discard_partial(const std::string& dest) {
    std::error_code ec;
    std::filesystem::remove(partial_path(dest), ec);
    std::filesystem::remove(resume_record_path(dest), ec);
}

PartialState partial_state(const std::string& dest, const std::string& url, uint64_t& have, uint64_t& total) {
    std::error_code ec;
    if (!std::filesystem::exists(partial_path(dest), ec)) return PartialState::None;
    ResumeInfo r;
    if (!read_resume_info(dest, r) || r.url != url) return PartialState::Stale;
    const uint64_t n = file_bytes(partial_path(dest));
    if (n == 0) return PartialState::Stale;
    have = n;
    total = r.total;
    if (n < r.total) return PartialState::Resumable;
    return n == r.total ? PartialState::Complete : PartialState::Oversize;
}

bool resumable_partial(const std::string& dest, const std::string& url, uint64_t& have, uint64_t& total) {
    return partial_state(dest, url, have, total) == PartialState::Resumable;
}

bool finish_complete_partial(const std::string& dest, const std::string& url, std::string& err) {
    PartialDownload part(url, dest, true);
    return part.complete() && part.finish(err);
}

void parse_response_line(const std::string& line, HttpResponseInfo& r) {
    if (line.compare(0, 5, "HTTP/") == 0) {
        r = HttpResponseInfo{};
        const auto sp = line.find(' ');
        if (sp != std::string::npos) r.status = std::strtol(line.c_str() + sp + 1, nullptr, 10);
        return;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) return;
    const std::string name = line.substr(0, colon);
    const std::string value = trimmed(line.substr(colon + 1));
    auto is = [&](const char* lower) { return starts_with_icase(name, lower) && name.size() == std::strlen(lower); };
    if (is("content-range")) r.content_range = value;
    else if (is("etag")) r.etag = value;
    else if (is("last-modified")) r.last_modified = value;
    else if (is("content-length")) {
        char* end = nullptr;
        r.content_length = std::strtoull(value.c_str(), &end, 10);
        r.has_content_length = !value.empty() && *end == '\0';
    }
}

PartialDownload::PartialDownload(const std::string& url, const std::string& dest, bool allow_resume)
    : url_(url), dest_(dest) {
    uint64_t have = 0, total = 0;
    const PartialState st = allow_resume ? partial_state(dest, url, have, total) : PartialState::Stale;
    if (st == PartialState::Resumable && read_resume_info(dest, rec_)) {
        ranged_ = true;
        have_ = base_ = have;
        total_ = total;
    } else if (st == PartialState::Complete && read_resume_info(dest, rec_)) {
        // The whole file is there and its last step (the rename) is what failed:
        // finish it, never fetch it again (GH #358). complete() tells the backend.
        complete_ = true;
        total_ = total;
    } else {
        if (st == PartialState::Oversize)
            Log::emulator()->warn("download: {} is larger ({} bytes) than the {} its record names; discarded",
                                  partial_path(dest), have, total);
        discard_partial(dest);   // stale (no record, another url), foreign, or not asked for
    }
}

PartialDownload::~PartialDownload() { close_file(); }

void PartialDownload::close_file() {
    if (f_) { std::fclose(f_); f_ = nullptr; }
}

uint64_t PartialDownload::part_size() const { return file_bytes(partial_path(dest_)); }

std::string PartialDownload::range_value() const { return "bytes=" + std::to_string(have_) + "-"; }
std::string PartialDownload::if_range_value() const { return rec_.validator; }

PartialDownload::Begin PartialDownload::begin(const HttpResponseInfo& r, std::string& err) {
    begun_ = true;
    if (r.status == 416 && ranged_) { discard_partial(dest_); return Begin::Retry; }
    if (r.status >= 400) {
        err = http_status_reason(r.status);
        cancelled();
        return Begin::Error;
    }
    if (r.status == 206) {
        unsigned long long s = 0, e = 0, t = 0;
        const bool good = ranged_ &&
            std::sscanf(r.content_range.c_str(), "bytes %llu-%llu/%llu", &s, &e, &t) == 3 &&
            s == have_ && t == rec_.total && e >= s && e < t;
        if (!good) {
            if (!ranged_) { err = "unexpected partial content (HTTP 206)"; cancelled(); return Begin::Error; }
            discard_partial(dest_);
            begun_ = false;
            return Begin::Retry;
        }
        // Append at an explicit 64-bit offset instead of trusting the CRT's "ab"
        // past 4 GiB; a positioning fault is a clean restart, never a splice.
        f_ = std::fopen(partial_path(dest_).c_str(), "r+b");
        if (!f_) { err = "cannot create output file: " + partial_path(dest_); return Begin::Error; }
#ifdef _WIN32
        const bool at = _fseeki64(f_, static_cast<__int64>(have_), SEEK_SET) == 0 &&
                        static_cast<uint64_t>(_ftelli64(f_)) == have_;
#else
        const bool at = fseeko(f_, static_cast<off_t>(have_), SEEK_SET) == 0 &&
                        static_cast<uint64_t>(ftello(f_)) == have_;
#endif
        if (!at) {
            close_file();
            discard_partial(dest_);
            begun_ = false;
            return Begin::Retry;
        }
        return Begin::Ok;
    }
    // 200 (the range was ignored or the validator changed), or any other
    // success: start from byte 0 and say what this response is a validator for.
    discard_partial(dest_);
    ranged_ = false;
    have_ = base_ = 0;
    total_ = r.has_content_length ? r.content_length : 0;
    ResumeInfo rec;
    rec.url = url_;
    rec.total = total_;
    if (!r.etag.empty() && r.etag.compare(0, 2, "W/") != 0) { rec.validator_kind = "etag"; rec.validator = r.etag; }
    else if (!r.last_modified.empty()) { rec.validator_kind = "date"; rec.validator = r.last_modified; }
    f_ = std::fopen(partial_path(dest_).c_str(), "wb");
    if (!f_) { err = "cannot create output file: " + partial_path(dest_); return Begin::Error; }
    if (!rec.validator.empty() && rec.total > 0) { rec_ = rec; write_resume_info(dest_, rec); }
    return Begin::Ok;
}

bool PartialDownload::write(const char* data, std::size_t n) {
    return f_ && std::fwrite(data, 1, n, f_) == n;
}

bool PartialDownload::report(const ProgressFn& progress, uint64_t got) const {
    return !progress || progress(base_ + got, total_);
}

bool PartialDownload::finish(std::string& err) {
    const bool flushed = !f_ || std::fflush(f_) == 0;
    close_file();
    const uint64_t n = part_size();
    if (!flushed) {
        err = "write/flush error on " + partial_path(dest_);
        cancelled();
        return false;
    }
    if (total_ > 0 && n != total_) {
        if (n < total_)
            err = "connection closed after " + std::to_string(n) + " of " + std::to_string(total_) + " bytes";
        else {
            err = "received " + std::to_string(n) + " bytes, expected " + std::to_string(total_);
            discard_partial(dest_);
            return false;
        }
        cancelled();
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(partial_path(dest_), dest_, ec);
    if (ec) {
        err = "cannot move " + partial_path(dest_) + " to " + dest_ + ": " + ec.message();
        return false;
    }
    std::filesystem::remove(resume_record_path(dest_), ec);
    return true;
}

void PartialDownload::cancelled() {
    close_file();
    ResumeInfo r;
    if (part_size() > 0 && read_resume_info(dest_, r)) return;   // kept: bytes and a validator
    discard_partial(dest_);
}

void PartialDownload::fail(std::string& err, bool clean_eof) {
    close_file();
    if (begun_ && total_ > 0) {
        const uint64_t n = part_size();
        if (n < total_) {
            const std::string at = std::to_string(n) + " of " + std::to_string(total_) + " bytes";
            err = clean_eof ? "connection closed after " + at : err + " (after " + at + ")";
        }
    }
    cancelled();
}

// ---------------------------------------------------------------------------
// The tick / stall / cancel contract (GH #358): see the comment above DownloadFn
// in sdcard_provisioner.h. The rule lives here, once; both backends and the GUI
// call it.
// ---------------------------------------------------------------------------
uint64_t StallWatch::idle_ms(uint64_t bytes, Clock::time_point now) {
    if (bytes != last_bytes_) {   // any change, a drop included (a restarted attempt)
        last_bytes_ = bytes;
        since_ = now;
        return 0;
    }
    if (now <= since_) return 0;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - since_).count());
}

std::string stall_reason(int stall_timeout_ms) {
    return "stalled: no data received for " + std::to_string(stall_timeout_ms / 1000) + " s";
}

std::string stall_note(uint64_t idle_ms) {
    if (idle_ms < static_cast<uint64_t>(kStallNoticeMs)) return {};
    // The text does not promise that the part is kept: that depends on the
    // server's validator, and the final error says what happened to it.
    return "Stalled: no data received for " + std::to_string(idle_ms / 1000) +
           " s. The network or the server may be down; jnext gives up after " +
           std::to_string(DownloadTimings{}.stall_timeout_ms / 1000) + " s.";
}

bool download_step(PartialDownload& part, const ProgressFn& progress, uint64_t got,
                   StallWatch& watch, const DownloadTimings& timings, std::string& err) {
    if (!part.report(progress, got)) {
        part.cancelled();
        err = "cancelled by user";
        return false;
    }
    if (watch.idle_ms(got, StallWatch::Clock::now()) >= static_cast<uint64_t>(timings.stall_timeout_ms)) {
        err = stall_reason(timings.stall_timeout_ms);
        part.fail(err, false);
        return false;
    }
    return true;
}

bool default_http_download(const std::string& url, const std::string& dest_path,
                           const ProgressFn& progress, std::string& err) {
    return http_download(url, dest_path, progress, err, DownloadTimings{});
}

// ---------------------------------------------------------------------------
// What a failed fetch says (GH #385): see the comment above net_reason in the
// header. The wordings live here, once, for both backends and every caller.
// ---------------------------------------------------------------------------
std::string net_reason(NetFailure kind, const std::string& raw) {
    const char* words = "the transfer failed";
    switch (kind) {
    case NetFailure::NameNotFound:   words = "the server's name could not be found"; break;
    case NetFailure::CannotConnect:  words = "could not connect to the server"; break;
    case NetFailure::TimedOut:       words = "the server did not answer in time"; break;
    case NetFailure::ConnectionLost: words = "the connection to the server was lost"; break;
    case NetFailure::NoAnswer:       words = "the server closed the connection without answering"; break;
    case NetFailure::Tls:            words = "the secure connection to the server failed"; break;
    case NetFailure::BadAddress:     words = "the address is not a valid http or https URL"; break;
    case NetFailure::Redirect:       words = "the server's redirect could not be followed"; break;
    case NetFailure::Other:          break;
    }
    return std::string(words) + " (" + raw + ")";
}

std::string http_status_reason(long status) {
    const char* words = "the server refused the request";
    if (status == 404 || status == 410) words = "the server does not have this file";
    else if (status == 401 || status == 403) words = "the server refused access";
    else if (status >= 500 && status <= 599) words = "the server reported an error";
    return std::string(words) + " (HTTP " + std::to_string(status) + ")";
}

// True when `err` ends in "(HTTP NNN)" with NNN a 4xx other than 408 (timeout) and 429 (rate limit).
static bool permanent_http_refusal(const std::string& err) {
    const size_t p = err.rfind("(HTTP ");
    if (p == std::string::npos || err.back() != ')') return false;
    const long st = std::strtol(err.c_str() + p + 6, nullptr, 10);
    return st >= 400 && st <= 499 && st != 408 && st != 429;
}

std::string fetch_failure(const std::string& what, const std::string& url,
                          const std::string& reason, const std::string& next) {
    std::string m = what + ": " + reason + "\n  URL: " + url;
    if (!next.empty()) m += "\n  " + next;
    return m;
}

std::string default_state_dir() { return config_dir(); }
std::string default_sdcard_dir() {
    return config_dir() + "/sdcard";
}
std::string default_sdcard_image_path() {
    return default_sdcard_dir() + "/" + kFixedImageFileName;
}
std::string default_sdcard_raw_image_path() {
    return default_sdcard_dir() + "/" + kImageFileName;
}

// Byte-copy `src` to `dst` (truncating dst). Streams in 1 MiB chunks so a
// 1 GB image does not need 1 GB of RAM. Verifies completeness: any read error
// on the source, or a copied size that does not equal the source size, fails
// and removes the partial destination — so a truncated copy can never pass.
bool default_copy_file(const std::string& src, const std::string& dst,
                       std::string& err) {
    std::error_code ec;
    const uintmax_t src_size = std::filesystem::file_size(src, ec);
    if (ec) { err = "cannot stat source image: " + src; return false; }

    std::ifstream in(src, std::ios::binary);
    if (!in) { err = "cannot open source image: " + src; return false; }
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) { err = "cannot create image: " + dst; return false; }

    std::vector<char> buf(1 << 20);
    uintmax_t written = 0;
    while (true) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = in.gcount();
        if (n > 0) {
            out.write(buf.data(), n);
            written += static_cast<uintmax_t>(n);
        }
        if (in.bad()) { err = "read error on source image: " + src; break; }
        if (in.eof()) break;
    }
    const bool ok = out.good() && !in.bad() && written == src_size;
    out.close();
    if (!ok) {
        if (err.empty()) {
            if (!out.good()) err = "write error copying to " + dst;
            else err = "incomplete copy to " + dst + " (" +
                       std::to_string(written) + "/" +
                       std::to_string(src_size) + " bytes)";
        }
        std::remove(dst.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// config.ini (verbatim from tools/fix-sdcard-image.sh)
// ---------------------------------------------------------------------------
std::vector<uint8_t> default_config_ini() {
    static const char* kIni =
        "scandoubler=1\n50_60hz=0\ntimex=1\npsgmode=0\nstereomode=1\n"
        "intsnd=1\nturbosound=1\ndac=1\ndivmmc=0\ndivports=1\nmf=0\n"
        "joystick1=2\njoystick2=0\nps2=0\nscanlines=0\nturbokey=1\n"
        "timing=0\ndefault=0\ndma=0\nkeyb_issue=0\nay48=0\nuart_i2c=1\n"
        "kmouse=1\nulaplus=1\nhdmisound=1\nbeepmode=0\nbuttonswap=0\n"
        "mousedpi=1\n";
    const uint8_t* p = reinterpret_cast<const uint8_t*>(kIni);
    return std::vector<uint8_t>(p, p + std::strlen(kIni));
}

// The network download (http_download; default_http_download forwards to it above)
// and the SHA-256 helpers (sha256_hex / sha256_file) live in the per-platform
// backend files:
//   sdcard_provisioner_net_curl.cpp  — libcurl + OpenSSL EVP (Linux/macOS)
//   sdcard_provisioner_net_win.cpp   — WinHTTP + BCrypt/CNG (Windows, GH #108
//                                      Phase B: no curl/OpenSSL DLLs shipped,
//                                      Win7-clean import set)

// ---------------------------------------------------------------------------
// Feedback on the console (GH #385): see the comment above ConsoleWait in the header.
// ---------------------------------------------------------------------------
ConsoleWait::ConsoleWait(std::FILE* out, ConsoleLogFn log, std::string slow_line, ConsoleNowFn now)
    : out_(out), log_(std::move(log)), slow_line_(std::move(slow_line)),
      now_(now ? std::move(now) : ConsoleNowFn(StallWatch::Clock::now)) {}

void ConsoleWait::say(const std::string& line, bool warning, bool new_line_first) {
    if (new_line_first) std::fputc('\n', out_);
    std::fprintf(out_, "%s\n", line.c_str());
    std::fflush(out_);
    if (log_) log_(warning, line);
}

bool ConsoleWait::tick(uint64_t downloaded, bool line_open) {
    const StallWatch::Clock::time_point now = now_();
    if (!started_) {   // the first tick starts both clocks
        started_ = true;
        began_   = now;
        watch_   = StallWatch(now);
    }
    bool said = false;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - began_).count();
    if (!slow_line_.empty() && !slow_said_ && elapsed_ms >= kShortFetchShowMs) {
        slow_said_ = true;
        say(slow_line_, false, line_open);
        said = true;
        line_open = false;   // the line just said ended with a newline
    }
    const uint64_t idle = watch_.idle_ms(downloaded, now);
    if (idle < static_cast<uint64_t>(kStallNoticeMs)) {
        stall_said_ = false;   // bytes arrived (or not long enough yet): a later stall speaks again
    } else if (!stall_said_) {
        stall_said_ = true;
        say(stall_note(idle), true, line_open);
        said = true;
    }
    return said;
}

CliProgress::CliProgress(std::string what, bool mib_if_unknown, ConsoleLogFn log, std::FILE* out,
                         ConsoleNowFn now)
    : what_(std::move(what)), mib_if_unknown_(mib_if_unknown), out_(out),
      wait_(out, std::move(log), {}, std::move(now)) {}

bool CliProgress::operator()(uint64_t downloaded, uint64_t total) {
    // Throttle to whole-percent changes to avoid flooding the terminal.
    if (total > 0) {
        const int pct = static_cast<int>((downloaded * 100ULL) / total);
        if (pct != last_pct_) {
            last_pct_ = pct;
            std::fprintf(out_, "\rDownloading %s: %3d%%", what_.c_str(), pct);
            if (pct >= 100) std::fprintf(out_, "\n");
            std::fflush(out_);
            open_ = pct < 100;
        }
    } else if (mib_if_unknown_) {
        // No Content-Length: show accumulated MiB every ~1 MiB.
        const uint64_t mib = downloaded >> 20;
        if (mib != last_mib_) {
            last_mib_ = mib;
            std::fprintf(out_, "\rDownloading %s: %llu MiB", what_.c_str(),
                         static_cast<unsigned long long>(mib));
            std::fflush(out_);
            open_ = true;
        }
    }
    if (wait_.tick(downloaded, open_)) open_ = false;
    return true; // CLI progress never cancels
}

ProgressFn make_cli_progress(std::FILE* out, ConsoleNowFn now) {
    return CliProgress("SD-card image", true, [](bool warning, const std::string& line) {
        if (warning) Log::emulator()->warn("SD-card image: {}", line);
        else         Log::emulator()->info("SD-card image: {}", line);
    }, out, std::move(now));
}

bool cli_busy(const std::string& phase, const std::function<bool()>& work) {
    // Print the phase (no trailing newline yet) so the terminal shows work is
    // in progress, then run it and finish the line with the outcome. When a
    // download preceded this, CliProgress ends its bar with a newline only in
    // the Content-Length branch (the common case for the distro URL); the rare
    // no-Content-Length path would leave this appended to the "N MiB" line — a
    // cosmetic wart, not a correctness issue.
    std::fprintf(stderr, "%s (this can take a few seconds)... ", phase.c_str());
    std::fflush(stderr);
    const bool ok = work ? work() : true;
    std::fprintf(stderr, "%s\n", ok ? "done" : "failed");
    std::fflush(stderr);
    return ok;
}

bool cli_confirm(const std::string& message) {
    std::fprintf(stderr, "%s [y/N] ", message.c_str());
    std::fflush(stderr);
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    for (char c : line) {
        if (c == 'y' || c == 'Y') return true;
        if (!std::isspace((unsigned char)c)) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Unzip (single entry, streaming, zlib raw inflate)
// ---------------------------------------------------------------------------
namespace {
uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

bool unzip_entry(const std::string& zip_path, const std::string& entry_basename,
                 const std::string& out_path, std::string& err) {
    std::ifstream zf(zip_path, std::ios::binary);
    if (!zf) { err = "cannot open zip: " + zip_path; return false; }
    zf.seekg(0, std::ios::end);
    const int64_t zsize = zf.tellg();
    if (zsize < 22) { err = "zip too small"; return false; }

    // Locate End Of Central Directory (EOCD, sig 0x06054b50) in the last
    // 64 KiB + 22 bytes.
    const int64_t scan = std::min<int64_t>(zsize, 65557);
    std::vector<uint8_t> tail(static_cast<size_t>(scan));
    zf.seekg(zsize - scan, std::ios::beg);
    zf.read(reinterpret_cast<char*>(tail.data()), scan);
    int64_t eocd = -1;
    for (int64_t i = scan - 22; i >= 0; --i) {
        if (rd32(tail.data() + i) == 0x06054b50u) { eocd = i; break; }
    }
    if (eocd < 0) { err = "EOCD not found (not a zip?)"; return false; }
    const uint32_t cd_count  = rd16(tail.data() + eocd + 10);
    const uint32_t cd_size   = rd32(tail.data() + eocd + 12);
    const uint32_t cd_offset = rd32(tail.data() + eocd + 16);

    // Read the central directory.
    std::vector<uint8_t> cd(cd_size);
    zf.seekg(cd_offset, std::ios::beg);
    zf.read(reinterpret_cast<char*>(cd.data()), cd_size);
    if (!zf.good()) { err = "cannot read central directory"; return false; }

    uint16_t method = 0;
    uint32_t comp_size = 0;
    uint32_t local_off = 0;
    uint32_t stored_crc = 0;
    bool found = false;
    size_t p = 0;
    for (uint32_t i = 0; i < cd_count && p + 46 <= cd.size(); ++i) {
        if (rd32(cd.data() + p) != 0x02014b50u) break;
        const uint16_t m    = rd16(cd.data() + p + 10);
        const uint32_t csz  = rd32(cd.data() + p + 20);
        const uint16_t nlen = rd16(cd.data() + p + 28);
        const uint16_t elen = rd16(cd.data() + p + 30);
        const uint16_t clen = rd16(cd.data() + p + 32);
        const uint32_t loff = rd32(cd.data() + p + 42);
        const uint32_t crc  = rd32(cd.data() + p + 16);
        std::string name(reinterpret_cast<char*>(cd.data() + p + 46), nlen);
        if (ieq(basename_of(name), entry_basename)) {
            method = m; comp_size = csz; local_off = loff; stored_crc = crc;
            found = true;
            break;
        }
        p += 46 + nlen + elen + clen;
    }
    if (!found) { err = "entry not found in zip: " + entry_basename; return false; }

    // Read the local file header to find the data offset.
    uint8_t lfh[30];
    zf.seekg(local_off, std::ios::beg);
    zf.read(reinterpret_cast<char*>(lfh), 30);
    if (!zf.good() || rd32(lfh) != 0x04034b50u) { err = "bad local header"; return false; }
    const uint16_t lnlen = rd16(lfh + 26);
    const uint16_t lelen = rd16(lfh + 28);
    const int64_t data_off = static_cast<int64_t>(local_off) + 30 + lnlen + lelen;

    std::ofstream of(out_path, std::ios::binary | std::ios::trunc);
    if (!of) { err = "cannot create output: " + out_path; return false; }
    zf.seekg(data_off, std::ios::beg);

    // Accumulate the CRC-32 of the extracted (uncompressed) bytes so we can
    // verify it against the zip's stored CRC — a cheap guard against a
    // truncated / corrupted download.
    uLong out_crc = crc32(0L, Z_NULL, 0);

    if (method == 0) { // stored
        std::vector<char> buf(1 << 20);
        uint32_t left = comp_size;
        while (left > 0) {
            const std::streamsize n = std::min<uint32_t>(left, buf.size());
            zf.read(buf.data(), n);
            if (zf.gcount() != n) { err = "short read (stored)"; return false; }
            of.write(buf.data(), n);
            out_crc = crc32(out_crc, reinterpret_cast<const Bytef*>(buf.data()),
                            static_cast<uInt>(n));
            left -= static_cast<uint32_t>(n);
        }
        if (!of.good()) { err = "write error"; return false; }
        if (static_cast<uint32_t>(out_crc) != stored_crc) {
            err = "CRC mismatch (corrupt/truncated download)"; return false;
        }
        return true;
    }
    if (method != 8) { err = "unsupported compression method " + std::to_string(method); return false; }

    // Raw deflate (windowBits = -15) streaming inflate.
    z_stream strm{};
    if (inflateInit2(&strm, -15) != Z_OK) { err = "inflateInit2 failed"; return false; }
    std::vector<uint8_t> in(1 << 20), out(1 << 20);
    uint32_t left = comp_size;
    int ret = Z_OK;
    bool okflag = true;
    while (ret != Z_STREAM_END) {
        if (strm.avail_in == 0) {
            if (left == 0) break;
            const std::streamsize n = std::min<uint32_t>(left, in.size());
            zf.read(reinterpret_cast<char*>(in.data()), n);
            if (zf.gcount() != n) { err = "short read (deflate)"; okflag = false; break; }
            left -= static_cast<uint32_t>(n);
            strm.next_in = in.data();
            strm.avail_in = static_cast<uInt>(n);
        }
        strm.next_out = out.data();
        strm.avail_out = static_cast<uInt>(out.size());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            err = "inflate error " + std::to_string(ret); okflag = false; break;
        }
        const size_t have = out.size() - strm.avail_out;
        if (have) {
            of.write(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(have));
            out_crc = crc32(out_crc, out.data(), static_cast<uInt>(have));
        }
        if (ret == Z_BUF_ERROR && strm.avail_in == 0 && left == 0) break;
    }
    inflateEnd(&strm);
    if (!okflag || !of.good()) { if (err.empty()) err = "write error"; return false; }
    if (static_cast<uint32_t>(out_crc) != stored_crc) {
        err = "CRC mismatch (corrupt/truncated download)"; return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GH #284: the 16 KB zx81.rom
// ---------------------------------------------------------------------------
// The 24.11 distribution's MACHINES/NEXT/menu.def declares the ZX81 entry as
// mode 1 (128K), which the firmware loads as TWO 16 KB pages (tbblue
// src/firmware/app/src/boot.c, load_roms: `default: i = 2`). But the image
// ships zx81.rom as a single 16 KB page (Farrow's v3.13, tbblue 751d0b31), so
// loadFile's second f_read hits end of file, returns fewer bytes than asked,
// and the boot menu's ZX81 entry stops on "error reading!" — on a real Next
// with this card too. Upstream fixed the file after 24.11, in tbblue e2df8e15
// ("Double emuROM, so it loads in personality and in cartmode"): the new file
// is the same 16 KB twice. No distribution ships it yet, so this applies the
// same change, built from the image's own bytes (no ROM content in jnext).
//
// Guarded by the FULL SHA-256 of that exact file: a user's own zx81.rom, a
// future distribution's 32 KB file, or any other size is left alone.
bool double_known_zx81_rom(Fat32Tree& tree, const std::string& known_sha256) {
    auto find = [](std::vector<Fat32Node>& level, const char* name,
                   bool is_dir) -> Fat32Node* {
        for (auto& n : level)
            if (n.is_dir == is_dir && ieq(n.name, name)) return &n;
        return nullptr;
    };
    Fat32Node* machines = find(tree.root, "MACHINES", true);
    Fat32Node* next = machines ? find(machines->children, "NEXT", true) : nullptr;
    Fat32Node* rom = next ? find(next->children, "zx81.rom", false) : nullptr;
    if (!rom || rom->data.size() != 16384 || sha256_hex(rom->data) != known_sha256)
        return false;
    const std::vector<uint8_t> page = rom->data; // insert() may not read *this
    rom->data.insert(rom->data.end(), page.begin(), page.end());
    return true;
}

// ---------------------------------------------------------------------------
// FAT32 patch orchestration
// ---------------------------------------------------------------------------
bool patch_image_fat32(const std::string& image_path, std::string& err,
                       const std::string& zx81_known_sha256, uint32_t volume_id) {
    uint32_t part_lba = 0;
    if (!fat32_find_partition(image_path, part_lba)) {
        err = "no FAT32 partition found in " + image_path;
        return false;
    }
    // Read total_sectors from the source BPB (partition size is preserved).
    uint32_t total_sectors = 0;
    {
        std::ifstream f(image_path, std::ios::binary);
        if (!f) { err = "cannot open image: " + image_path; return false; }
        uint8_t bpb[512];
        f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(part_lba) * 512));
        f.read(reinterpret_cast<char*>(bpb), 512);
        if (!f.good()) { err = "cannot read BPB"; return false; }
        total_sectors = rd32(bpb + 32);
        if (total_sectors == 0) total_sectors = rd16(bpb + 19);
    }
    if (total_sectors == 0) { err = "cannot determine partition size"; return false; }

    // Read the whole tree into memory (about 90 MB of file data on the 24.11
    // image and 121 MB on the 26.11 one, measured; the run peaks at 150 MB RSS).
    Fat32Tree tree;
    if (!fat32_read_tree(image_path, part_lba, tree)) {
        err = "failed to read FAT32 tree";
        return false;
    }

    // Inject the default config.ini.
    fat32_tree_upsert(tree, "MACHINES/NEXT/config.ini", default_config_ini());

    // GH #284: make the known 16 KB zx81.rom the 32 KB file the firmware boot
    // menu needs (see double_known_zx81_rom above).
    if (double_known_zx81_rom(tree, zx81_known_sha256)) {
        Log::emulator()->info(
            "sdcard: doubled the 16 KB /MACHINES/NEXT/zx81.rom to 32 KB so the "
            "firmware boot menu can load the ZX81 entry");
    }

    // Reformat the partition in place to a spec-valid FAT32 with 8 KB clusters
    // (via vendored ChaN FatFs f_mkfs + f_write) and re-emit the whole tree.
    if (!fat32_format_and_populate(image_path, part_lba, total_sectors, tree, err)) {
        return false;
    }
    // Recipe 3: the volume serial is the distribution's own (BS_VolID32 at VBR + 0x43, and
    // its copy in the backup boot sector at VBR + 6 sectors, where f_mkfs puts it).
    if (volume_id != 0) {
        std::fstream f(image_path, std::ios::in | std::ios::out | std::ios::binary);
        const uint8_t id[4] = { static_cast<uint8_t>(volume_id), static_cast<uint8_t>(volume_id >> 8),
                                static_cast<uint8_t>(volume_id >> 16), static_cast<uint8_t>(volume_id >> 24) };
        for (uint64_t sec : {uint64_t{0}, uint64_t{6}}) {
            f.seekp(static_cast<std::streamoff>((static_cast<uint64_t>(part_lba) + sec) * 512 + 0x43));
            f.write(reinterpret_cast<const char*>(id), 4);
        }
        f.flush();
        if (!f) { err = "cannot write the volume serial into " + image_path; return false; }
    }
    Log::emulator()->info(
        "sdcard: patched {} -> FAT32 with 8 KB clusters ({} partition sectors)",
        image_path, total_sectors);
    return true;
}

bool patch_image_for_recipe(const std::string& image_path, int recipe,
                            const std::string& raw_sha256_hex, std::string& err,
                            const std::string& zx81_known_sha256) {
    if (recipe < 1 || recipe > kFixedImageRecipe) {
        err = "this jnext cannot rebuild a card made by recipe " + std::to_string(recipe) +
              " (it knows recipes 1 to " + std::to_string(kFixedImageRecipe) + ")";
        return false;
    }
    // Recipe 1 never doubled zx81.rom: a hash no file has leaves it alone.
    const std::string zx = recipe >= 2 ? zx81_known_sha256 : std::string("none");
    const uint32_t serial = recipe >= 3 ? volume_id_from_sha256(raw_sha256_hex) : 0;
    return patch_image_fat32(image_path, err, zx, serial);
}

// ---------------------------------------------------------------------------
// Top-level resolution
// ---------------------------------------------------------------------------
ProvisionResult provision_sd_card(const ProvisionOptions& opts) {
    ProvisionResult r;

    // An explicit --sdcard path ALWAYS wins, unconditionally. It may name an
    // arbitrary, unrelated image; --sdcard-download-force must never re-download
    // over it or ignore it. --sdcard-download-force applies ONLY to the
    // default-location provisioning flow (recovering a corrupted
    // ~/.jnext/sdcard/cspect-next-1gb.img).
    if (!opts.explicit_path.empty()) {
        if (opts.force_download) {
            std::fprintf(stderr,
                "warning: --sdcard-download-force ignored because an explicit "
                "--sdcard was provided (%s).\n", opts.explicit_path.c_str());
        }
        r.status = ProvisionStatus::Ok;
        r.path   = opts.explicit_path;
        return r;
    }

    const std::string dir   = default_sdcard_dir();
    const std::string raw   = default_sdcard_raw_image_path();   // pristine
    const std::string fixed = default_sdcard_image_path();       // patched

    // GH #555: an interrupted upgrade is finished (SWITCHING present) or its scratch removed.
    {
        std::string note, rerr;
        if (!backup::recover(dir, rerr)) {
            r.status = ProvisionStatus::Failed;
            r.error  = "an interrupted go-back to the previous SD card could not be finished: " + rerr;
            return r;
        }
        if (!upgrade::recover(dir, note, rerr)) {
            r.status = ProvisionStatus::Failed;
            r.error  = "an interrupted SD card upgrade could not be finished: " + rerr +
                       " (the old set is in " + upgrade::previous_dir(dir) + ".new)";
            return r;
        }
    }

    // Distro URL: $JNEXT_SDCARD_DISTRO_URL overrides kDistroUrl when set
    // and non-empty. A TEST SEAM in the same spirit as $JNEXT_CONFIG_DIR
    // above (GH #108 Phase B): it lets a harness point the REAL download
    // path (libcurl / WinHTTP) at a local fixture server instead of the
    // multi-hundred-MB specnext.com zip — e.g. the wine packaging smoke,
    // which proves the Windows WinHTTP backend end-to-end against a
    // loopback HTTP server. Not a user feature; deliberately absent from
    // --help and the man page.
    const char* env_url = std::getenv("JNEXT_SDCARD_DISTRO_URL");
    const std::string distro_url =
        (env_url && *env_url) ? std::string(env_url) : std::string(kDistroUrl);
    // Which distribution that address is (the zip's file name), and the sidecars that
    // record it beside the images.
    const std::string current_dist = distribution_of_url(distro_url);
    const std::string fixed_distro = fixed + kDistroSuffix;
    const std::string raw_distro   = raw + kDistroSuffix;
    const std::string declined     = fixed + kDeclinedSuffix;
    auto dist_of = [](const std::string& sidecar) {
        const std::string d = read_text_line(sidecar);
        return d.empty() ? std::string(kLegacyDistribution) : d;
    };

    // A card that exists is replaced ATOMICALLY when it is rebuilt (--sdcard-download-force,
    // or an upgrade the user accepted): everything is made beside it as "<name>.new" and
    // moved over it only when it all worked, so a failed download or patch leaves the
    // user's card exactly as it was.
    bool replacing = false;
    bool upgrade_accepted = false;
    bool carry_files = true;    // an accepted upgrade carries the user's files over (false: a fresh start)
    std::vector<std::string> overlays;
    std::vector<ImageLock> held_locks;   // listed overlays another jnext must not take while the upgrade runs
    std::string kept_note;   // a failed upgrade that left the old card in use
    auto use_existing = [&]() {
        r.status = ProvisionStatus::Ok;
        r.path   = fixed;
        Log::emulator()->info("sdcard: using default image {}", fixed);
        return r;
    };

    if (regular_file_exists(fixed)) {
        if (opts.force_download) {
            replacing = true;
        } else {
            const std::string have = dist_of(fixed_distro);
            if (ieq(have, current_dist)) return use_existing();

            // A card of a NEWER distribution (or a newer recipe, or a name that cannot be placed in the order):
            // an older jnext never touches it and offers nothing. It is used as it is; a GUI is told once per
            // card distribution (remembered in "<card>.newer-seen").
            {
                const int rel = backup::compare_distributions(have, current_dist);
                const std::string rec = read_text_line(fixed + kFixedImageRecipeSuffix);
                const int card_recipe = rec.empty() ? 1 : std::atoi(rec.c_str());
                if (rel == 1 || rel == 2 || card_recipe > kFixedImageRecipe) {
                    const std::string msg = "This SD card is " + distribution_title(have) + ", newer than this jnext's " +
                                            distribution_title(current_dist) + "; it is used as it is.";
                    Log::emulator()->warn("sdcard: this SD card is {}, newer than this jnext's {}; it is used as it is",
                                          distribution_title(have), distribution_title(current_dist));
                    const std::string seen = fixed + ".newer-seen";
                    if (opts.notice && !ieq(read_text_line(seen), have)) {
                        opts.notice(msg);
                        write_text_line(seen, have);
                    }
                    return use_existing();
                }
            }

            // The card is of an older (or other) distribution than the one jnext downloads now.
            // It holds the user's own files: an upgrade carries them over and keeps the old card
            // as a backup, and nothing happens without their yes.
            if (opts.upgrade_mode == UpgradeMode::Ask) std::remove(declined.c_str());
            const std::string new_title = distribution_title(current_dist);
            const std::string old_title = distribution_title(have);
            // The overlays the upgrade would rebuild: the command line's and the listed ones that bind to the
            // card (read from their headers: nothing is bound or rewritten). Another jnext holding one of
            // them, or the card, means no offer and no upgrade: its files are not ours to move.
            overlays = upgrade::usable_overlays(dir, fixed, opts.overlays);
            std::vector<ImageLock> overlay_locks;
            for (const std::string& o : overlays) {
                if (std::find(opts.overlays.begin(), opts.overlays.end(), o) != opts.overlays.end()) continue;   // main holds it
                std::string lwhy;
                auto lk = ImageLock::acquire(o, ImageLock::Kind::Writes, lwhy);
                if (!lk) {
                    Log::emulator()->info("sdcard: {} is available; this card is {}; not upgraded now: {}", new_title, old_title, lwhy);
                    return use_existing();
                }
                overlay_locks.push_back(std::move(*lk));
            }
            held_locks = std::move(overlay_locks);
            std::string base_why;
            bool base_ok = carry::pristine_base_ok(fixed, base_why);
            if (base_ok) {
                std::vector<std::string> all = overlays;
                for (const std::string& o : opts.overlays) if (std::find(all.begin(), all.end(), o) == all.end()) all.push_back(o);
                const std::string op = upgrade::overlay_problem(fixed, all);
                if (!op.empty()) { base_ok = false; base_why = op; }
            }
            switch (opts.upgrade_mode) {
            case UpgradeMode::Never:
                if (!write_text_line(declined, current_dist))
                    Log::emulator()->warn("sdcard: cannot write {}", declined);
                Log::emulator()->info("sdcard: {} is available; this card is {}; not offering it again",
                                      new_title, old_title);
                return use_existing();
            case UpgradeMode::No:
                Log::emulator()->info("sdcard: {} is available; this card is {}; keeping it this run",
                                      new_title, old_title);
                return use_existing();
            case UpgradeMode::Yes:
                if (!base_ok) {
                    r.status = ProvisionStatus::Failed;
                    r.error  = "the files on this card cannot be carried over (" + base_why +
                               "); --sdcard-upgrade fresh starts a new card and keeps this one as the backup";
                    r.refused = true;
                    return r;
                }
                upgrade_accepted = true;
                break;
            case UpgradeMode::Fresh:
                upgrade_accepted = true;
                carry_files = false;
                break;
            case UpgradeMode::Default:
            case UpgradeMode::Ask:
                if (ieq(read_text_line(declined), current_dist)) return use_existing();
                if (!opts.upgrade) {
                    if (base_ok)
                        Log::emulator()->info(
                            "sdcard: {} is available; this card is {}. Run jnext --sdcard-upgrade yes to "
                            "upgrade it (your files are kept, and the old card is kept as a backup).",
                            new_title, old_title);
                    else
                        Log::emulator()->info(
                            "sdcard: {} is available; this card is {}. Your files cannot be carried over ({}); "
                            "jnext --sdcard-upgrade fresh makes a new card and keeps this one as the backup.",
                            new_title, old_title, base_why);
                    return use_existing();
                }
                {
                    const bool distro_is_pinned = distro_url == kDistroUrl;
                    auto gb = [](uint64_t b) {
                        char t[32];
                        std::snprintf(t, sizeof t, "%.1f GB", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
                        return std::string(t);
                    };
                    const uint64_t need = upgrade::space_needed(
                        fixed, distro_is_pinned ? minimal_card::kDistribution.zip_size : 0, overlays);
                    std::error_code bec;
                    const bool backup_exists = std::filesystem::exists(upgrade::previous_dir(dir), bec);
                    std::string msg =
                        new_title + " is available. The SD card image jnext uses is " + old_title + ".\n\n";
                    msg += base_ok
                        ? "Upgrading downloads " + new_title + (distro_is_pinned
                              ? " (" + std::to_string(minimal_card::kDistribution.zip_size / 1000000) + " MB)"
                              : std::string()) +
                          " and carries the files you saved over to the new card; the files NextZXOS "
                          "itself changed are replaced by the new version, and you get a list of both.\n\n"
                        : "Your current card cannot be compared with the one it came from (" + base_why +
                          "), so the files on it cannot be carried over. Starting fresh downloads " + new_title +
                          (distro_is_pinned ? " (" + std::to_string(minimal_card::kDistribution.zip_size / 1000000) + " MB)"
                                            : std::string()) +
                          " and makes a new card; your files stay on the old one, which is kept.\n\n";
                    msg += "Your current card is kept as a backup (" + gb(std::filesystem::exists(fixed, bec) ? std::filesystem::file_size(fixed, bec) : 0) +
                           " of disk); you can go back to it or delete it from the File menu. About " +
                           gb(need) + " of free disk space is needed while the upgrade runs.";
                    if (backup_exists)
                        msg += "\n\nThe backup of an earlier upgrade is replaced by this one.";
                    msg += std::string("\n\nNot now goes on using ") + old_title + " and nothing is changed (it asks again next "
                           "time); Keep my card (don't ask again) does the same and stops this offer for " + new_title +
                           ". You can upgrade later with jnext --sdcard-upgrade " + (base_ok ? "yes" : "fresh") + ".";
                    switch (opts.upgrade(have, current_dist, msg, base_ok)) {
                    case UpgradeChoice::Replace:
                        upgrade_accepted = true;
                        carry_files = base_ok;
                        break;
                    case UpgradeChoice::KeepDontAskAgain:
                        if (!write_text_line(declined, current_dist))
                            Log::emulator()->warn("sdcard: cannot write {}", declined);
                        return use_existing();
                    case UpgradeChoice::Keep:
                    default:
                        return use_existing();
                    }
                }
                break;
            }
            replacing = true;
        }
    }
    const bool upgrading = replacing && upgrade_accepted;
    const std::string work = upgrade::work_dir(dir);

    std::string err;
    const std::string raw_sha_path = raw + ".sha256";

    // Optimization: if the raw official image is already present (and we are
    // not force-re-downloading), skip the large download and produce the fixed
    // image from it — but ONLY if the raw's SHA256 matches its `.sha256`
    // sidecar AND it is of the distribution being installed. A missing/unreadable
    // sidecar or a mismatch marks the raw untrusted (stale/corrupt) and forces the
    // full download cycle; a raw of another distribution is never re-derived into a
    // card that says it is this one.
    bool raw_trusted = false;
    if (!opts.force_download && !upgrading && regular_file_exists(raw)) {
        const std::string stored = read_stored_sha256(raw_sha_path);
        if (stored.empty()) {
            Log::emulator()->warn(
                "sdcard: raw image {} has no readable SHA256 sidecar; "
                "re-downloading", raw);
        } else if (!ieq(dist_of(raw_distro), current_dist)) {
            Log::emulator()->info(
                "sdcard: raw image {} is {}, not {}; downloading", raw,
                dist_of(raw_distro), current_dist);
        } else {
            const std::string actual = sha256_file(raw);
            if (!actual.empty() && ieq(actual, stored)) {
                raw_trusted = true;
                Log::emulator()->info(
                    "sdcard: raw image {} SHA256 verified; reusing", raw);
            } else {
                Log::emulator()->warn(
                    "sdcard: raw image {} SHA256 mismatch (have {}, expected "
                    "{}); re-downloading", raw, actual, stored);
            }
        }
    }

    // The raw lands in "<raw>.new" when it replaces an existing one; the old raw (and the
    // old card) stay until the new card is complete.
    const std::string raw_new   = upgrading ? work + "/raw.new"  : raw + ".new";
    const std::string fixed_new = upgrading ? work + "/card.new" : fixed + ".new";
    const std::string raw_out   = replacing ? raw_new : raw;
    const std::string fixed_out = replacing ? fixed_new : fixed;
    bool raw_downloaded = false;
    std::string raw_digest;

    // A failed replacement leaves the old card in use. An upgrade the user accepted
    // reports that and goes on with the old card (Ok + a note); --sdcard-download-force
    // was asked to recover a card, so it fails.
    auto fail = [&](const std::string& why) {
        if (upgrading) {
            std::error_code rec;
            std::filesystem::remove_all(work, rec);
        } else if (replacing) {
            std::remove(raw_new.c_str());
            std::remove(fixed_new.c_str());
        }
        if (replacing && upgrade_accepted) {
            r.status = ProvisionStatus::Ok;
            r.path   = fixed;
            r.error.clear();
            r.warning = "The upgrade to " + distribution_title(current_dist) + " failed, so the "
                        "current SD card image stays in use and nothing was changed. "
                        "To try again, run jnext --sdcard-upgrade yes.\n\n" + why;
        } else {
            r.status = ProvisionStatus::Failed;
            r.error  = why;
        }
        return r;
    };

    if (!raw_trusted) {
        ConfirmFn confirm = opts.confirm ? opts.confirm : ConfirmFn(cli_confirm);
        DownloadFn download = opts.download ? opts.download
                                            : DownloadFn(default_http_download);

        // The download is resumable (GH #358): the backend keeps `<zip>.part` when
        // a transfer stops, and --sdcard-download-force means start over.
        const std::string zip_tmp = dir + "/sn-emulator.zip";
        if (opts.force_download) discard_partial(zip_tmp);
        uint64_t part_have = 0, part_total = 0;
        const bool resuming = resumable_partial(zip_tmp, distro_url, part_have, part_total);
        auto mb = [](uint64_t b) {
            char t[32];
            std::snprintf(t, sizeof t, "%.1f MB", static_cast<double>(b) / (1024.0 * 1024.0));
            return std::string(t);
        };

        if (!opts.auto_confirm && !upgrade_accepted) {
            std::string msg =
                std::string(replacing ? "The SD-card image in ~/.jnext/ will be replaced." :
                                        "No SD-card image was found at ~/.jnext/.") +
                (resuming ? " (continuing a download that stopped at " + mb(part_have) + " of " +
                                mb(part_total) + ")"
                          : std::string()) + "\n\n"
                "Download the NextZXOS official distribution image and "
                "install it there?";
            if (!confirm(msg)) {
                r.status = ProvisionStatus::Declined;
                r.error  = "download declined by user";
                return r;
            }
        }

        if (!make_dirs(dir)) {
            r.status = ProvisionStatus::Failed;
            r.error  = "cannot create directory " + dir;
            return r;
        }

        if (upgrading) {
            // The disk must hold the zip, the new raw and card, the old card as made and the
            // overlays; refused before anything is downloaded.
            const std::vector<std::string>& ovs = carry_files ? overlays : std::vector<std::string>();
            const uint64_t need = upgrade::space_needed(
                fixed, distro_url == kDistroUrl ? minimal_card::kDistribution.zip_size : 0, ovs);
            std::error_code sec;
            const uint64_t have_free = opts.free_space ? opts.free_space(dir)
                                                       : static_cast<uint64_t>(std::filesystem::space(dir, sec).available);
            if (!opts.free_space && sec) {
                Log::emulator()->warn("sdcard: cannot read the free disk space of {}: {}", dir, sec.message());
            } else if (have_free < need) {
                auto gbs = [](uint64_t b) {
                    char t[32];
                    std::snprintf(t, sizeof t, "%.1f GB", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
                    return std::string(t);
                };
                return fail("not enough free disk space for the upgrade: " + gbs(need) + " are needed in " + dir +
                            " and " + gbs(have_free) + " are free");
            }
            std::error_code mec;
            std::filesystem::remove_all(work, mec);
            if (!make_dirs(work)) return fail("cannot create directory " + work);
        }

        // A failed fetch names the address, the reason in plain words and what
        // happens next (GH #385); the sandbox note, when there is one, comes last.
        //
        // One attempt is the download AND the extraction of the image from it:
        // a server that answers with a short or wrong body (GH #497: a ~12 KB page
        // instead of the zip) "downloads" fine and only fails to unzip. Either
        // failure is retried up to opts.retries more times, waiting
        // 2 s, then twice that, ... between them. A user cancel is
        // not retried; neither is an HTTP 4xx answer (not 408/429): the server refused,
        // asking again only delays the same failure. A kept partial is continued by the next attempt (GH #358).
        const SleepFn sleep = opts.sleep ? opts.sleep : SleepFn(real_sleep_ms);
        unsigned backoff_ms = 2000;
        if (const char* e = std::getenv("JNEXT_SDCARD_RETRY_BACKOFF_MS"); e && *e)
            backoff_ms = static_cast<unsigned>(std::strtoul(e, nullptr, 10));
        std::string why;
        bool fetched = false;
        for (int attempt = 0;; ++attempt) {
            if (download(distro_url, zip_tmp, opts.progress, err)) {
                if (unzip_entry(zip_tmp, kImageFileName, raw_out, err)) { fetched = true; break; }
                std::remove(zip_tmp.c_str());
                std::remove(raw_out.c_str()); // never trust a partial raw
                why = fetch_failure("unzip failed", distro_url, err,
                                    "the download was removed, so the next start downloads it from the beginning");
            } else {
                uint64_t h2 = 0, t2 = 0;
                const PartialState ps = partial_state(zip_tmp, distro_url, h2, t2);
                const char* next =
                    ps == PartialState::Resumable ? "the part fetched so far is kept and the next start continues it"
                  : ps == PartialState::Complete  ? "the complete download is kept and the next start uses it"
                  : "nothing was kept, so the next start downloads it from the beginning";
                why = fetch_failure("download failed", distro_url, err, next) + sandbox_network_note(opts);
            }
            if (err == "cancelled by user" || attempt >= opts.retries || permanent_http_refusal(err)) break;
            const unsigned wait_ms = backoff_ms << attempt;
            const std::string note = "Download attempt " + std::to_string(attempt + 1) + " failed: " + err +
                                     "\nRetry " + std::to_string(attempt + 1) + " of " +
                                     std::to_string(opts.retries) + " in " + std::to_string(wait_ms / 1000) + " s";
            std::fputs(("\n" + note + "...\n").c_str(), stderr);
            std::string one_line = note;
            std::replace(one_line.begin(), one_line.end(), '\n', ';');
            Log::emulator()->warn("sdcard: {}", one_line);
            sleep(wait_ms, note);
        }
        if (!fetched) return fail(why);
        std::remove(zip_tmp.c_str());
        raw_downloaded = true;

        // Record the SHA256 of the freshly-downloaded pristine raw, so a later
        // skip-redownload can prove the raw is intact. Written only AFTER a
        // fully successful download+unzip (for a replacement: once the new card is
        // complete, below). The raw's distribution is recorded with it.
        raw_digest = sha256_file(raw_out);
        if (!replacing && !raw_digest.empty()) {
            std::ofstream sf(raw_sha_path, std::ios::trunc);
            if (sf) sf << raw_digest << "  " << kImageFileName << "\n";
            sf.close();
            write_text_line(raw_distro, current_dist);
        }
    } else {
        raw_digest = read_stored_sha256(raw_sha_path);
    }
    const std::string& raw_src = raw_downloaded ? raw_out : raw;

    // Produce the fixed image from the pristine raw one: copy, then FAT32
    // recluster the COPY in place (the raw official image is left untouched).
    // Both steps run over a ~1 GB image and take several seconds with no
    // natural progress hook (f_mkfs is one blocking call), so they are wrapped
    // in the BusyFn seam: the GUI animates a "Fixing downloaded image…" dialog
    // on a worker thread; the CLI prints a one-line message; tests (no busy)
    // run the work inline unchanged.
    CopyFn copy = opts.copy ? opts.copy : CopyFn(default_copy_file);
    // The recipe sidecar says which kFixedImageRecipe produced the fixed
    // image. Removed first (when the card is made in place), so a failed derive never
    // leaves one describing an image that no longer exists; written last, only after
    // the patch worked. Best effort, like the raw's .sha256 above: a missing sidecar
    // only means "recipe unknown", never an unusable image.
    const std::string recipe_path = fixed + kFixedImageRecipeSuffix;
    const uint32_t volume_id = volume_id_from_sha256(raw_digest);
    auto do_fix = [&]() -> bool {
        if (!replacing) std::remove(recipe_path.c_str());
        if (!copy(raw_src, fixed_out, err)) { err = "cannot produce fixed image: " + err; return false; }
        if (!patch_image_fat32(fixed_out, err, kZx81Rom16kSha256, volume_id)) {
            err = "patch failed: " + err;
            return false;
        }
        return true;
    };
    const bool fixed_ok = opts.busy ? opts.busy("Fixing downloaded image", do_fix)
                                     : do_fix();
    if (!fixed_ok) {
        std::remove(fixed_out.c_str()); // never leave a truncated / half-patched image
        return fail(err);               // do_fix() already prefixed the failing step
    }

    // GH #555: an upgrade carries the user's files over, verifies the result and switches the
    // whole set (the old one becomes the backup in previous/). Any failure before the switch
    // leaves the old set exactly as it was.
    if (upgrading) {
        upgrade::Plan plan;
        plan.dir = dir; plan.card = fixed; plan.raw = raw;
        plan.new_card = fixed_new; plan.new_raw = raw_new;
        plan.new_distro = current_dist; plan.new_raw_sha = raw_digest; plan.new_recipe = kFixedImageRecipe;
        plan.carry = carry_files;
        if (carry_files) plan.overlays = overlays;
        else plan.overlays = overlays;   // a fresh start sets them aside (previous/overlays)
        carry::Report rep;
        std::string cerr_;
        auto do_carry = [&]() { return upgrade::carry_over(plan, rep, cerr_); };
        const bool carried_ok = opts.busy ? opts.busy("Carrying your files over", do_carry) : do_carry();
        if (!carried_ok) return fail("carrying your files over failed, so nothing was changed: " + cerr_);
        const std::string report_text = carry_files ? rep.text() : std::string();
        std::string serr;
        if (!upgrade::switch_set(plan, report_text, serr)) {
            // From the first switch step on the only way is forward: the next start finishes it.
            r.status = ProvisionStatus::Failed;
            r.error  = "the SD card upgrade was interrupted (" + serr + "); the next start finishes it";
            return r;
        }
        Log::emulator()->info("sdcard: upgraded to {}; the previous card is in {}", distribution_title(current_dist),
                              upgrade::previous_dir(dir));
        if (!report_text.empty()) {
            std::istringstream is(report_text);
            std::string line;
            while (std::getline(is, line)) Log::emulator()->info("sdcard: upgrade report: {}", line);
            r.report = report_text;
            if (opts.show_report) opts.show_report(report_text);
        }
        r.status = ProvisionStatus::Ok;
        r.path   = fixed;
        r.upgraded = true;
        return r;
    }

    // The new card is complete. A replacement moves it (and the new raw) over the old ones
    // only now.
    if (replacing) {
        std::error_code ec;
        if (raw_downloaded) {
            std::filesystem::rename(raw_new, raw, ec);
            if (ec) return fail("cannot move the new raw image into place: " + ec.message());
        }
        std::filesystem::rename(fixed_new, fixed, ec);
        if (ec) return fail("cannot move the new SD card image into place: " + ec.message());
        if (raw_downloaded && !raw_digest.empty()) {
            std::ofstream sf(raw_sha_path, std::ios::trunc);
            if (sf) sf << raw_digest << "  " << kImageFileName << "\n";
            sf.close();
            write_text_line(raw_distro, current_dist);
        }
    }
    if (!write_text_line(recipe_path, std::to_string(kFixedImageRecipe)))
        Log::emulator()->warn("sdcard: cannot write recipe sidecar {}", recipe_path);
    write_text_line(fixed_distro, current_dist);
    std::remove(declined.c_str());   // a new card of this distribution: the old "do not ask" is moot

    r.status = ProvisionStatus::Ok;
    r.path   = fixed;
    return r;
}

} // namespace sdcard
