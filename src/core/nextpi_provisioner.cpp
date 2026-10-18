#include "core/nextpi_provisioner.h"

#include "core/fat32_image.h"
#include "core/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

#include <zlib.h>

#ifdef _WIN32
// The one place this file touches the Win32 API: MD5 through CNG, the twin of
// the OpenSSL path below. NOMINMAX / LEAN_AND_MEAN keep <windows.h> from
// redefining names this file uses (std::min / std::max).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace fs = std::filesystem;

namespace nextpi {

const char* const kDefaultRelease = "1_93D";
const char* const kLatest         = "latest";
const char* const kMirrorUrl      = "https://zx.xalior.com/NextPi2";

namespace {

std::atomic<uint64_t> g_md5_calls{0};

// The files the provisioner itself creates while fetching: the archive, its
// .md5, the "latest" listing, each with the backend's partial and record
// suffixes. Anything else in the directory means a setup was tried there.
bool is_fetch_file(const fs::directory_entry& e) {
    std::error_code ec;
    if (!e.is_regular_file(ec)) return false;
    std::string n = e.path().filename().string();
    for (const std::string& suf : {std::string(), std::string(".md5"), sdcard::partial_path(""),
                                   sdcard::resume_record_path("")}) {
        if (suf.empty() || (n.size() > suf.size() && n.compare(n.size() - suf.size(), suf.size(), suf) == 0)) {
            const std::string base = suf.empty() ? n : n.substr(0, n.size() - suf.size());
            if (base == ".listing.html") return true;
            const std::string pre = "NextPi-", post = ".tar.gz";
            if (base.size() > pre.size() + post.size() && base.compare(0, pre.size(), pre) == 0 &&
                base.compare(base.size() - post.size(), post.size(), post) == 0 &&
                valid_release_name(base.substr(pre.size(), base.size() - pre.size() - post.size())))
                return true;
        }
    }
    return false;
}

constexpr const char* kImageName   = "nextpi.img";
constexpr const char* kReleaseName = "release";
constexpr const char* kKernel      = "kernel.img";
constexpr const char* kDtb         = "bcm2708-rpi-zero.dtb";

// Archive sanity limits: no entry larger than 1 TB (NextPi's image is 15 GB),
// no GNU long-name / pax record larger than 1 MB.
constexpr uint64_t kMaxEntrySize    = 1ull << 40;
constexpr uint64_t kMaxMetadataSize = 1ull << 20;

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << data;
    return static_cast<bool>(f);
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// A tar size field: octal text, or GNU base-256 when the top bit of the
/// first byte is set (sizes past 8 GB — the NextPi image is one).
uint64_t tar_size(const unsigned char* f, std::size_t len) {
    if (f[0] & 0x80) {
        uint64_t v = f[0] & 0x7F;
        for (std::size_t i = 1; i < len; ++i) v = (v << 8) | f[i];
        return v;
    }
    uint64_t v = 0;
    for (std::size_t i = 0; i < len && f[i]; ++i)
        if (f[i] >= '0' && f[i] <= '7') v = v * 8 + (f[i] - '0');
    return v;
}

/// Read exactly `n` bytes (or fewer at end of stream). Returns bytes read.
std::size_t gz_read_full(gzFile gz, void* buf, std::size_t n) {
    std::size_t got = 0;
    auto* p = static_cast<unsigned char*>(buf);
    while (got < n) {
        const int r = gzread(gz, p + got, static_cast<unsigned>(std::min<std::size_t>(n - got, 1u << 30)));
        if (r <= 0) break;
        got += static_cast<std::size_t>(r);
    }
    return got;
}

/// `path` with the home directory written as "~", for messages.
std::string display_path(const std::string& path) {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        const std::string h(home);
        if (path == h) return "~";
        if (path.size() > h.size() && path.compare(0, h.size(), h) == 0 && path[h.size()] == '/')
            return "~" + path.substr(h.size());
    }
    return path;
}

std::string mirror_of(const ProvisionOptions& opts) {
    if (!opts.mirror.empty()) return opts.mirror;
    const char* env = std::getenv("JNEXT_NEXTPI_MIRROR");   // test seam, like JNEXT_SDCARD_DISTRO_URL
    if (env && *env) return env;
    return kMirrorUrl;
}

} // namespace

bool cli_progress(uint64_t downloaded, uint64_t total) {
    static int last_pct = -1;
    if (total == 0) return true;
    const int pct = static_cast<int>((downloaded * 100ULL) / total);
    if (pct != last_pct) {
        last_pct = pct;
        std::fprintf(stderr, "\rDownloading NextPi: %3d%%", pct);
        if (pct >= 100) std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
    return true;
}

bool cli_busy(const std::string& phase, const std::function<bool()>& work) {
    std::fprintf(stderr, "%s (this can take a minute or two)... ", phase.c_str());
    std::fflush(stderr);
    const bool ok = work ? work() : true;
    std::fprintf(stderr, "%s\n", ok ? "done" : "failed");
    std::fflush(stderr);
    return ok;
}

StartRequest start_request(bool gui, bool cli_set, bool cli_value, bool saved_enabled) {
    StartRequest r;
    r.wanted       = cli_set ? cli_value : (gui && saved_enabled);
    r.asked_on_cli = cli_set && cli_value;
    return r;
}

StartOutcome start_outcome(bool asked_on_cli, sdcard::ProvisionStatus provisioned, bool started) {
    if (started) return StartOutcome::Started;
    if (provisioned == sdcard::ProvisionStatus::Declined) return StartOutcome::Declined;
    return asked_on_cli ? StartOutcome::Exit : StartOutcome::WarnAndContinue;
}

bool valid_release_name(const std::string& name) {
    if (name.empty() || name.size() > 64 || name[0] == '.' || name[0] == '-') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

std::string default_dir() {
    return (fs::path(sdcard::default_sdcard_dir()).parent_path() / "nextpi").string();
}

std::vector<std::string> parse_release_listing(const std::string& html) {
    static const std::regex link(R"(NextPi-([A-Za-z0-9_.-]+?)\.tar\.gz(?![A-Za-z0-9.]))");
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(html.begin(), html.end(), link);
         it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1];
        if (valid_release_name(name) && seen.insert(name).second) out.push_back(name);
    }
    return out;
}

int compare_release(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j]))) {
            std::size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
            while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
            const std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
            const std::string sa = na.substr(std::min(na.find_first_not_of('0'), na.size()));
            const std::string sb = nb.substr(std::min(nb.find_first_not_of('0'), nb.size()));
            if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
            if (sa != sb) return sa < sb ? -1 : 1;
            i = i2;
            j = j2;
        } else {
            if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;
}

uint64_t md5_file_calls() { return g_md5_calls.load(); }

std::string unprepared_reason(const std::string& dir) {
    const fs::path d(dir);
    std::error_code ec;
    for (const fs::path& p : {d / kImageName, d / "boot" / kKernel, d / "boot" / kDtb})
        if (!fs::is_regular_file(p, ec)) return p.string() + " is missing";
    const fs::path marker = d / kReleaseName;
    if (!fs::is_regular_file(marker, ec)) return marker.string() + " is missing";
    if (trim(read_file(marker.string())).empty()) return marker.string() + " is empty";
    return {};
}

std::string prepared_release(const std::string& dir) {
    if (!unprepared_reason(dir).empty()) return {};
    return trim(read_file((fs::path(dir) / kReleaseName).string()));
}

#ifdef _WIN32
// CNG (bcrypt.dll, Vista-era, already linked for the SD-card SHA-256), so the
// Windows build carries no OpenSSL. Streamed in 1 MiB reads like the OpenSSL
// twin: the archive is gigabytes. "" on any failure, as there.
std::string md5_file(const std::string& path) {
    ++g_md5_calls;
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    BCRYPT_ALG_HANDLE  alg  = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    DWORD obj_len = 0, got = 0, md_len = 0;
    std::vector<UCHAR> obj;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0) return {};
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                          sizeof(obj_len), &got, 0) >= 0
        && BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&md_len),
                             sizeof(md_len), &got, 0) >= 0
        && md_len > 0 && md_len <= 64) {
        obj.resize(obj_len);
        if (BCryptCreateHash(alg, &hash, obj.data(), obj_len, nullptr, 0, 0) >= 0) {
            std::vector<char> buf(1 << 20);
            bool ok = true;
            while (true) {
                f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const std::streamsize n = f.gcount();
                if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()),
                                            static_cast<ULONG>(n), 0) < 0) {
                    ok = false;
                    break;
                }
                if (f.bad()) { ok = false; break; }
                if (f.eof()) break;
            }
            UCHAR md[64];
            if (ok && BCryptFinishHash(hash, md, md_len, 0) >= 0) {
                static const char* hexd = "0123456789abcdef";
                for (DWORD i = 0; i < md_len; ++i) {
                    result.push_back(hexd[md[i] >> 4]);
                    result.push_back(hexd[md[i] & 0x0F]);
                }
            }
            BCryptDestroyHash(hash);
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}
#else
std::string md5_file(const std::string& path) {
    ++g_md5_calls;
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    std::string result;
    if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) == 1) {
        std::vector<char> buf(1 << 20);
        bool ok = true;
        while (true) {
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = f.gcount();
            if (n > 0 && EVP_DigestUpdate(ctx, buf.data(), static_cast<size_t>(n)) != 1) {
                ok = false;
                break;
            }
            if (f.bad()) { ok = false; break; }
            if (f.eof()) break;
        }
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int md_len = 0;
        if (ok && EVP_DigestFinal_ex(ctx, md, &md_len) == 1) {
            static const char* hexd = "0123456789abcdef";
            for (unsigned int i = 0; i < md_len; ++i) {
                result.push_back(hexd[md[i] >> 4]);
                result.push_back(hexd[md[i] & 0x0F]);
            }
        }
    }
    EVP_MD_CTX_free(ctx);
    return result;
}
#endif

bool extract_tar_gz_entry(const std::string& archive, const std::string& suffix,
                          const std::string& out_path, const sdcard::ProgressFn& progress,
                          std::string& err) {
    std::error_code ec;
    const uint64_t archive_size = fs::file_size(archive, ec);
    gzFile gz = gzopen(archive.c_str(), "rb");
    if (!gz) {
        err = archive + ": cannot open";
        return false;
    }
    gzbuffer(gz, 1 << 18);

    std::string long_name;      // from a GNU 'L' or pax 'path' record
    uint64_t    pax_size = 0;   // from a pax 'size' record
    bool        have_pax_size = false;
    std::vector<unsigned char> buf(1 << 20);
    unsigned char hdr[512];
    bool found = false;

    while (gz_read_full(gz, hdr, sizeof hdr) == sizeof hdr) {
        if (std::all_of(hdr, hdr + sizeof hdr, [](unsigned char c) { return c == 0; })) break;

        std::string name(reinterpret_cast<char*>(hdr), strnlen(reinterpret_cast<char*>(hdr), 100));
        // POSIX ustar splits long paths into prefix + name; GNU tar ("ustar  ")
        // uses those bytes for other fields.
        if (std::memcmp(hdr + 257, "ustar\0", 6) == 0) {
            const std::string prefix(reinterpret_cast<char*>(hdr + 345),
                                     strnlen(reinterpret_cast<char*>(hdr + 345), 155));
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        uint64_t size = tar_size(hdr + 124, 12);
        const char type = static_cast<char>(hdr[156]);
        // A size near 2^64 would wrap the rounding below, and a huge metadata
        // record would be allocated whole: either way a corrupt or hostile
        // archive must be an error, never an exception that ends jnext after
        // a 6 GB download.
        if (size > kMaxEntrySize) {
            err = archive + ": malformed (an entry claims " + std::to_string(size) + " bytes)";
            gzclose(gz);
            return false;
        }
        const uint64_t padded = (size + 511) / 512 * 512;

        if (type == 'L' || type == 'x') {
            // Metadata for the NEXT entry: a GNU long name, or pax records.
            // Real ones are a few hundred bytes.
            if (size > kMaxMetadataSize) {
                err = archive + ": malformed (a " + std::to_string(size) +
                      "-byte name/metadata record)";
                gzclose(gz);
                return false;
            }
            std::string data(static_cast<std::size_t>(padded), '\0');
            if (gz_read_full(gz, data.data(), data.size()) != data.size()) break;
            data.resize(static_cast<std::size_t>(size));
            if (type == 'L') {
                long_name = data.c_str();
            } else {
                // Each record is "<len> <key>=<value>\n", <len> counting the
                // whole record. The length is checked against what is LEFT
                // (pos + len could wrap, and a wrapped record would reach
                // back over the ones before it), and must cover its own
                // "<len> " and end on the '\n'.
                std::size_t pos = 0;
                while (pos < data.size()) {
                    const std::size_t sp = data.find(' ', pos);
                    const std::size_t len = std::strtoull(data.c_str() + pos, nullptr, 10);
                    if (sp == std::string::npos || len == 0 || len > data.size() - pos ||
                        sp + 1 >= pos + len || data[pos + len - 1] != '\n') {
                        err = archive + ": malformed (a pax record overruns its header)";
                        gzclose(gz);
                        return false;
                    }
                    const std::string rec = data.substr(sp + 1, pos + len - sp - 2);   // drop '\n'
                    const std::size_t eq = rec.find('=');
                    if (eq != std::string::npos) {
                        if (rec.compare(0, eq, "path") == 0) long_name = rec.substr(eq + 1);
                        if (rec.compare(0, eq, "size") == 0) {
                            pax_size = std::strtoull(rec.c_str() + eq + 1, nullptr, 10);
                            have_pax_size = true;
                        }
                    }
                    pos += len;
                }
            }
            continue;
        }

        if (!long_name.empty()) name = long_name;
        if (have_pax_size) {
            if (pax_size > kMaxEntrySize) {
                err = archive + ": malformed (a pax size of " + std::to_string(pax_size) + " bytes)";
                gzclose(gz);
                return false;
            }
            size = pax_size;
        }
        long_name.clear();
        have_pax_size = false;
        const uint64_t data_blocks = (size + 511) / 512 * 512;
        const bool regular = (type == '0' || type == '\0' || type == '7');

        if (regular && ends_with(name, suffix)) {
            std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                err = out_path + ": cannot create";
                gzclose(gz);
                return false;
            }
            uint64_t left = size;
            while (left > 0) {
                const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(left, buf.size()));
                const std::size_t got = gz_read_full(gz, buf.data(), want);
                if (got != want) {
                    err = archive + ": truncated inside " + name;
                    gzclose(gz);
                    return false;
                }
                out.write(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(got));
                if (!out) {
                    err = out_path + ": write failed (disk full?)";
                    gzclose(gz);
                    return false;
                }
                left -= got;
                if (progress && !progress(static_cast<uint64_t>(gzoffset(gz)), archive_size)) {
                    err = "cancelled";
                    gzclose(gz);
                    return false;
                }
            }
            found = true;
            break;
        }

        // Skip this entry's data.
        uint64_t left = data_blocks;
        while (left > 0) {
            const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(left, buf.size()));
            if (gz_read_full(gz, buf.data(), want) != want) break;
            left -= want;
        }
        if (left > 0) break;
    }
    gzclose(gz);
    if (!found) err = archive + ": no entry ending in " + suffix;
    return found;
}

bool extract_boot_files(const std::string& image_path, const std::string& boot_dir,
                        std::string& err) {
    uint32_t lba = 0;
    if (!fat32_find_partition(image_path, lba)) {
        err = image_path + ": no FAT32 boot partition";
        return false;
    }
    Fat32Tree tree;
    if (!fat32_read_tree(image_path, lba, tree)) {
        err = image_path + ": cannot read its boot partition";
        return false;
    }
    std::error_code ec;
    fs::create_directories(boot_dir, ec);
    for (const char* want : {kKernel, kDtb}) {
        auto it = std::find_if(tree.root.begin(), tree.root.end(), [&](const Fat32Node& n) {
            if (n.is_dir || n.name.size() != std::strlen(want)) return false;
            for (std::size_t i = 0; i < n.name.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(n.name[i])) != want[i]) return false;
            return true;
        });
        if (it == tree.root.end()) {
            err = image_path + ": no " + want + " in its boot partition";
            return false;
        }
        if (!write_file((fs::path(boot_dir) / want).string(),
                        std::string(it->data.begin(), it->data.end()))) {
            err = (fs::path(boot_dir) / want).string() + ": write failed";
            return false;
        }
    }
    return true;
}

ProvisionResult provision(const ProvisionOptions& opts) {
    ProvisionResult res;
    const std::string dir     = opts.dir.empty() ? default_dir() : opts.dir;
    const std::string wanted  = trim(opts.release.empty() ? std::string(kDefaultRelease) : opts.release);
    const std::string mirror  = mirror_of(opts);
    const auto download       = opts.download ? opts.download : sdcard::DownloadFn(sdcard::default_http_download);
    const auto confirm        = opts.confirm ? opts.confirm : sdcard::ConfirmFn(sdcard::cli_confirm);
    const auto busy           = opts.busy ? opts.busy
                                          : sdcard::BusyFn([](const std::string&, const std::function<bool()>& w) { return w(); });
    // The SHORT fetches (the release list, the two .md5 files) are the only ones that go
    // through opts.short_fetch, so a GUI can show feedback when one is slow (GH #385). The
    // archive download is not one of them: it has opts.progress and a dialog of its own.
    // With the seam empty a short fetch is a plain download with no ProgressFn.
    const auto short_download = [&](const char* label, const std::string& url, const std::string& dest,
                                    std::string& e) {
        if (!opts.short_fetch) return download(url, dest, {}, e);
        return opts.short_fetch(label, [&](const sdcard::ProgressFn& pf) { return download(url, dest, pf, e); });
    };
    constexpr const char* kListLabel = "Reading the list of NextPi releases...";
    constexpr const char* kMd5Label  = "Fetching the NextPi checksum...";
    std::error_code ec;
    // Did the user (or an earlier install) already put something here? The
    // provisioner's own fetch files do not count: a lone archive is explained by
    // the archive check below, a partial download by the continuation note. A
    // manual unpack (`NextPi-1_93D.tar`, a `NextPi-1_93D/` folder) does count.
    bool dir_had_entries = false;
    if (fs::is_directory(dir, ec)) {
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            if (!is_fetch_file(e)) { dir_had_entries = true; break; }
        }
    }
    ec.clear();
    fs::create_directories(dir, ec);
    if (ec) {
        res.error = dir + ": " + ec.message();
        return res;
    }
    const std::string current = prepared_release(dir);
    std::string reason;
    if (current.empty() && dir_had_entries) {
        reason = unprepared_reason(dir);
        if (!reason.empty())
            Log::uart()->warn("NextPi: {} cannot be used: {}", dir, reason);
    }

    // "latest" is resolved against the mirror's listing on every start, and is
    // the only case that touches the network when a release is already there.
    // The release name becomes part of a file name and a URL, so it is checked
    // before either: what the mirror's own names use, and nothing else.
    if (wanted != kLatest && !valid_release_name(wanted)) {
        res.error = "\"" + wanted + "\" is not a NextPi release name (letters, digits, '.', '_' and "
                    "'-' only), nor \"latest\"";
        return res;
    }

    std::string target = wanted;
    if (wanted == kLatest) {
        const std::string listing = (fs::path(dir) / ".listing.html").string();
        const std::string list_url = mirror + "/";
        std::string err;
        const bool ok = short_download(kListLabel, list_url, listing, err);
        const std::vector<std::string> releases = ok ? parse_release_listing(read_file(listing)) : std::vector<std::string>{};
        fs::remove(listing, ec);
        if (releases.empty()) {
            const std::string what = "cannot read the list of NextPi releases";
            const std::string why  = ok ? "the page lists no NextPi release" : err;
            if (!current.empty()) {
                res.status  = sdcard::ProvisionStatus::Ok;
                res.dir     = dir;
                res.release = current;
                res.warning = sdcard::fetch_failure(what, list_url, why, "jnext uses the installed NextPi " + current);
                return res;
            }
            res.error = sdcard::fetch_failure(what, list_url, why, "nothing was downloaded");
            return res;
        }
        target = *std::max_element(releases.begin(), releases.end(),
                                   [](const std::string& a, const std::string& b) {
                                       return compare_release(a, b) < 0;
                                   });
    }

    if (!current.empty() && current == target) {
        res.status  = sdcard::ProvisionStatus::Ok;
        res.dir     = dir;
        res.release = current;
        return res;
    }

    const std::string archive_name = "NextPi-" + target + ".tar.gz";
    const std::string archive  = (fs::path(dir) / archive_name).string();
    const std::string md5_path = archive + ".md5";
    std::string err;
    auto md5_in = [](const std::string& text) {
        std::smatch mm;
        if (!std::regex_search(text, mm, std::regex("[0-9a-fA-F]{32}"))) return std::string();
        std::string d = mm.str();
        std::transform(d.begin(), d.end(), d.begin(), ::tolower);
        return d;
    };

    const std::string archive_url = mirror + "/" + archive_name;
    const std::string md5_url     = archive_url + ".md5";
    uint64_t part_have = 0, part_total = 0;
    auto gb = [](uint64_t b) {
        char t[32];
        std::snprintf(t, sizeof t, "%.1f GB", static_cast<double>(b) / (1024.0 * 1024.0 * 1024.0));
        return std::string(t);
    };
    // A finished download whose last step (the rename to the archive's name)
    // failed last time: finish it now, with no request. It then takes the
    // archive check below, and its MD5 is the judge.
    if (!sdcard::regular_file_exists(archive) &&
        sdcard::partial_state(archive, archive_url, part_have, part_total) == sdcard::PartialState::Complete) {
        std::string perr;
        if (!sdcard::finish_complete_partial(archive, archive_url, perr)) {
            res.error = perr + "; the complete download is kept in " + sdcard::partial_path(archive) +
                        " and is used next time";
            Log::uart()->warn("NextPi: {}", res.error);
            return res;
        }
    }

    // An archive already in the directory (fetched by hand, or left by an
    // interrupted run) is looked at BEFORE anything is asked: if it matches its
    // MD5 it is installed with no download. A rejected one is never deleted here;
    // a download, if the user agrees to one, overwrites it.
    bool reuse = false;
    std::string archive_note;
    std::string local_digest;   // the archive's MD5 as computed by the check below
    if (sdcard::regular_file_exists(archive)) {
        std::string expected = md5_in(read_file(md5_path));
        std::string expected_from = md5_path;
        if (expected.empty()) {
            if (short_download(kMd5Label, md5_url, md5_path, err)) {
                expected = md5_in(read_file(md5_path));
                expected_from = md5_url;
                if (expected.empty()) err = "the file holds no MD5";
            }
            if (expected.empty())
                archive_note = sdcard::fetch_failure("jnext cannot check " + archive +
                                                         ": there is no MD5 beside it, and the mirror's could not be used",
                                                     md5_url, err, "");
        }
        if (!expected.empty()) {
            std::string actual;
            busy("Checking the NextPi download", [&] { actual = md5_file(archive); return !actual.empty(); });
            local_digest = actual;
            if (actual == expected) {
                reuse = true;
                Log::uart()->info("NextPi: {} matches its MD5; installing it without downloading", archive);
            } else {
                archive_note = archive + " does not match its MD5 (" +
                               (actual.empty() ? std::string("unreadable") : actual) + ", expected " +
                               expected + " from " + expected_from + "): it is incomplete or damaged.";
            }
        }
        if (!archive_note.empty()) Log::uart()->warn("NextPi: {}", archive_note);
    }

    // The same text is the GUI dialog's body and the terminal prompt (which
    // appends " [y/N]"), so: the question LAST, no line breaks inside a
    // sentence (the dialog wraps by itself), the path alone on its line, and
    // what each answer leads to said before it is asked.
    // A download that stopped (GH #358): the backend kept `<archive>.part` and its
    // record, and continues it. Never adopted as an archive: only a finished
    // download is renamed to the archive's name.
    const bool resuming = !reuse && sdcard::resumable_partial(archive, archive_url, part_have, part_total);
    if (!reuse || !current.empty()) {
        std::ostringstream msg;
        if (resuming)
            msg << "A previous download of NextPi " << target << " stopped after " << gb(part_have) << " of "
                << gb(part_total) << " (in " << sdcard::partial_path(archive) << "); jnext continues it from there.\n\n";
        if (!reason.empty()) msg << "NextPi in this folder cannot be used: " << reason << ".\n\n";
        if (!archive_note.empty()) msg << archive_note << "\n\n";
        if (current.empty()) {
            msg << "NextPi is the software that runs on the Raspberry Pi of the Spectrum "
                   "Next, and it is not installed yet.\n\n"
                << "jnext can download it now (NextPi " << target << ", about 6 GB) into:\n"
                << display_path(dir) << "\n\n"
                << "This happens only once. About 22 GB of free space is needed there while "
                   "it is unpacked; it uses about 15 GB afterwards.\n\n"
                << "If you choose No, NextPi is not started. To stop being asked, turn it off "
                   "in Settings > Preferences > NextPi.\n\n"
                << "Download NextPi now?";
        } else if (reuse) {
            msg << "jnext is set to use NextPi " << target << ", but NextPi " << current
                << " is installed in:\n"
                << display_path(dir) << "\n\n"
                << "jnext can install NextPi " << target << " from the downloaded " << archive_name
                << " and replace it. Anything NextPi " << current << " saved to its own card is lost.\n\n"
                << "If you choose No, jnext keeps using NextPi " << current << ".\n\n"
                << "Install NextPi " << target << " now?";
        } else {
            msg << "jnext is set to use NextPi " << target << ", but NextPi " << current
                << " is installed in:\n"
                << display_path(dir) << "\n\n"
                << "jnext can download NextPi " << target << " now (about 6 GB) and replace it. "
                   "Anything NextPi " << current << " saved to its own card is lost.\n\n"
                << "If you choose No, jnext keeps using NextPi " << current << ".\n\n"
                << "Download NextPi " << target << " now?";
        }
        if (!confirm(msg.str())) {
            if (!current.empty()) {
                res.status  = sdcard::ProvisionStatus::Ok;
                res.dir     = dir;
                res.release = current;
                // The user's own answer: logged, never a warning (GH #385), so no dialog follows it.
                Log::uart()->info("NextPi: kept the installed NextPi {} ({} not {})", current, target,
                                  reuse ? "installed" : "downloaded");
                return res;
            }
            res.status = sdcard::ProvisionStatus::Declined;
            res.error  = "NextPi download declined";
            return res;
        }
    }

    const fs::space_info space = fs::space(dir, ec);
    if (!ec && space.available < opts.space_needed) {
        res.error = dir + " has " + std::to_string(space.available >> 30) + " GB free; NextPi needs about " +
                    std::to_string(opts.space_needed >> 30) + " GB while it is unpacked";
        return res;
    }

    if (!reuse) {
        // The checksum first: it is tiny, and a release name the mirror does not
        // have fails here rather than after a long download.
        const std::string md5_what = "cannot fetch the MD5 checksum of NextPi " + target;
        if (!short_download(kMd5Label, md5_url, md5_path, err)) {
            res.error = sdcard::fetch_failure(md5_what, md5_url, err, "the NextPi download did not start");
            return res;
        }
        const std::string expected = md5_in(read_file(md5_path));
        if (expected.empty()) {
            res.error = sdcard::fetch_failure(md5_what, md5_url, "the file holds no MD5",
                                              "the NextPi download did not start");
            return res;
        }
        // An archive that checks out against the mirror's MD5 is reused.
        // (The digest the check above already computed is reused: hashing 5.7 GB
        // again outside busy() would freeze the GUI.)
        bool already_good = false;
        if (!local_digest.empty()) {
            already_good = local_digest == expected;
        } else if (sdcard::regular_file_exists(archive)) {
            busy("Checking the NextPi download", [&] { already_good = md5_file(archive) == expected; return already_good; });
        }
        if (!already_good) {
            // A failed download does NOT delete the archive: the backend owns the
            // partial (`<archive>.part`, kept for resuming) and writes `archive`
            // only when complete, and one that wrongly reports failure for a
            // complete file (GH #358) leaves it for the next run's MD5 check.
            if (!download(archive_url, archive, opts.progress, err)) {
                uint64_t h2 = 0, t2 = 0;
                std::string next;
                switch (sdcard::partial_state(archive, archive_url, h2, t2)) {
                case sdcard::PartialState::Resumable:
                    next = "the " + gb(h2) + " fetched so far are kept and the download continues next time";
                    break;
                case sdcard::PartialState::Complete:
                    next = "the complete download is kept in " + sdcard::partial_path(archive) +
                           " and is used next time";
                    break;
                default:
                    next = "nothing was kept, so it is downloaded from the beginning next time";
                    break;
                }
                res.error = sdcard::fetch_failure("NextPi download failed", archive_url, err, next);
                return res;
            }
            std::string actual;
            busy("Checking the NextPi download", [&] { actual = md5_file(archive); return actual == expected; });
            if (actual != expected) {
                fs::remove(archive, ec);
                res.error = sdcard::fetch_failure(
                    "NextPi download failed", archive_url,
                    "the download does not match its MD5 (got " +
                        (actual.empty() ? std::string("nothing") : actual) + ", expected " + expected + ")",
                    "the corrupt download was removed, so it is downloaded from the beginning next time");
                return res;
            }
        }
    }
    const std::string kept = "; the download is kept in " + archive + " and is used next time";

    // Unpack beside the old release, so a failure up to here leaves the
    // previous NextPi (if any) untouched and usable.
    const fs::path image_part = fs::path(dir) / "nextpi.img.part";
    const fs::path boot_part  = fs::path(dir) / "boot.part";
    fs::remove_all(boot_part, ec);
    const bool unpacked = busy("Unpacking the NextPi image", [&] {
        return extract_tar_gz_entry(archive, ".img", image_part.string(), {}, err) &&
               extract_boot_files(image_part.string(), boot_part.string(), err);
    });
    if (!unpacked) {
        fs::remove(image_part, ec);
        fs::remove_all(boot_part, ec);
        res.error = "NextPi " + target + ": " + err + kept;
        return res;
    }

    // THE SWAP. Not atomic as a whole — a directory of files cannot be
    // replaced in one step — but crash-safe, because the `release` marker is
    // what makes a directory "prepared" and it is removed FIRST and written
    // LAST (to a temporary name, then renamed over, which is atomic). An
    // install interrupted anywhere in between leaves no marker, so the next
    // start re-provisions instead of booting a half-replaced NextPi.
    const fs::path marker = fs::path(dir) / kReleaseName;
    const fs::path boot   = fs::path(dir) / "boot";
    const fs::path boot_old = fs::path(dir) / "boot.old";
    auto install_failed = [&](const std::string& what) {
        res.error = dir + ": cannot install NextPi " + target + ": " + what + kept;
        return res;
    };
    fs::remove(marker, ec);
    if (ec) return install_failed(ec.message());
    fs::remove(fs::path(dir) / "overlay.qcow2", ec);    // belonged to the old image
    fs::rename(image_part, fs::path(dir) / kImageName, ec);   // atomically replaces the old one
    if (ec) return install_failed(ec.message());
    fs::remove_all(boot_old, ec);
    if (fs::exists(boot, ec)) fs::rename(boot, boot_old, ec);
    if (ec) return install_failed(ec.message());
    fs::rename(boot_part, boot, ec);
    if (ec) return install_failed(ec.message());
    fs::remove_all(boot_old, ec);
    const fs::path marker_part = fs::path(dir) / "release.part";
    if (!write_file(marker_part.string(), target + "\n")) return install_failed("cannot write the release marker");
    fs::rename(marker_part, marker, ec);
    if (ec) return install_failed(ec.message());
    fs::remove(archive, ec);
    fs::remove(md5_path, ec);
    Log::uart()->info("NextPi {} installed in {}", target, dir);

    res.status  = sdcard::ProvisionStatus::Ok;
    res.dir     = dir;
    res.release = target;
    return res;
}

} // namespace nextpi
