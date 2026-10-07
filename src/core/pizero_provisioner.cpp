#include "core/pizero_provisioner.h"

#include "core/fat32_image.h"
#include "core/log.h"

#include <algorithm>
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

#ifndef _WIN32
#include <openssl/evp.h>
#endif

namespace fs = std::filesystem;

namespace pizero {

const char* const kDefaultRelease = "1_93D";
const char* const kLatest         = "latest";
const char* const kMirrorUrl      = "https://zx.xalior.com/NextPi2";

namespace {

constexpr const char* kImageName   = "nextpi.img";
constexpr const char* kReleaseName = "release";
constexpr const char* kKernel      = "kernel.img";
constexpr const char* kDtb         = "bcm2708-rpi-zero.dtb";

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

std::string mirror_of(const ProvisionOptions& opts) {
    if (!opts.mirror.empty()) return opts.mirror;
    const char* env = std::getenv("JNEXT_PIZERO_MIRROR");   // test seam, like JNEXT_SDCARD_DISTRO_URL
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

std::string default_dir() {
    return (fs::path(sdcard::default_sdcard_dir()).parent_path() / "pizero").string();
}

std::vector<std::string> parse_release_listing(const std::string& html) {
    static const std::regex link(R"(NextPi-([A-Za-z0-9_.-]+?)\.tar\.gz(?![A-Za-z0-9.]))");
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(html.begin(), html.end(), link);
         it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1];
        if (seen.insert(name).second) out.push_back(name);
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

std::string prepared_release(const std::string& dir) {
    const fs::path d(dir);
    std::error_code ec;
    for (const fs::path& p : {d / kImageName, d / "boot" / kKernel, d / "boot" / kDtb})
        if (!fs::is_regular_file(p, ec)) return {};
    return trim(read_file((d / kReleaseName).string()));
}

#ifdef _WIN32
std::string md5_file(const std::string&) { return {}; }
#else
std::string md5_file(const std::string& path) {
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
        const uint64_t padded = (size + 511) / 512 * 512;

        if (type == 'L' || type == 'x') {
            // Metadata for the NEXT entry: a GNU long name, or pax records.
            std::string data(static_cast<std::size_t>(padded), '\0');
            if (gz_read_full(gz, data.data(), data.size()) != data.size()) break;
            data.resize(static_cast<std::size_t>(size));
            if (type == 'L') {
                long_name = data.c_str();
            } else {
                std::size_t pos = 0;
                while (pos < data.size()) {
                    const std::size_t sp = data.find(' ', pos);
                    if (sp == std::string::npos) break;
                    const std::size_t len = std::strtoull(data.c_str() + pos, nullptr, 10);
                    if (len == 0 || pos + len > data.size()) break;
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
        if (have_pax_size) size = pax_size;
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
#ifdef _WIN32
    (void)opts;
    res.error = "the Pi Zero (NextPi under QEMU) is not supported on Windows";
    return res;
#else
    const std::string dir     = opts.dir.empty() ? default_dir() : opts.dir;
    const std::string wanted  = trim(opts.release.empty() ? std::string(kDefaultRelease) : opts.release);
    const std::string mirror  = mirror_of(opts);
    const auto download       = opts.download ? opts.download : sdcard::DownloadFn(sdcard::default_http_download);
    const auto confirm        = opts.confirm ? opts.confirm : sdcard::ConfirmFn(sdcard::cli_confirm);
    const auto busy           = opts.busy ? opts.busy
                                          : sdcard::BusyFn([](const std::string&, const std::function<bool()>& w) { return w(); });
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        res.error = dir + ": " + ec.message();
        return res;
    }
    const std::string current = prepared_release(dir);

    // "latest" is resolved against the mirror's listing on every start, and is
    // the only case that touches the network when a release is already there.
    std::string target = wanted;
    if (wanted == kLatest) {
        const std::string listing = (fs::path(dir) / ".listing.html").string();
        std::string err;
        const bool ok = download(mirror + "/", listing, {}, err);
        const std::vector<std::string> releases = ok ? parse_release_listing(read_file(listing)) : std::vector<std::string>{};
        fs::remove(listing, ec);
        if (releases.empty()) {
            const std::string why = ok ? "no NextPi release listed at " + mirror
                                       : "cannot reach " + mirror + ": " + err;
            if (!current.empty()) {
                res.status  = sdcard::ProvisionStatus::Ok;
                res.dir     = dir;
                res.release = current;
                res.warning = why + "; using the installed NextPi " + current;
                return res;
            }
            res.error = why;
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
    std::ostringstream msg;
    if (current.empty()) {
        msg << "The Pi Zero needs NextPi " << target << ", which is not installed yet.\n\n"
            << "Download it now from " << mirror << "?\n"
            << "It is about 6 GB, and needs about 22 GB of free space in\n" << dir
            << " while it is unpacked (about 15 GB afterwards).";
    } else {
        msg << "The Pi Zero is set to NextPi " << target << ", but " << dir << " holds "
            << current << ".\n\nDownload NextPi " << target << " (about 6 GB) and replace it?\n"
            << "The changes NextPi made to its card (overlay.qcow2) are discarded.";
    }
    if (!confirm(msg.str())) {
        if (!current.empty()) {
            res.status  = sdcard::ProvisionStatus::Ok;
            res.dir     = dir;
            res.release = current;
            res.warning = "kept the installed NextPi " + current + " (" + target + " not downloaded)";
            return res;
        }
        res.status = sdcard::ProvisionStatus::Declined;
        res.error  = "NextPi download declined";
        return res;
    }

    const fs::space_info space = fs::space(dir, ec);
    if (!ec && space.available < opts.space_needed) {
        res.error = dir + " has " + std::to_string(space.available >> 30) + " GB free; NextPi needs about " +
                    std::to_string(opts.space_needed >> 30) + " GB while it is unpacked";
        return res;
    }

    // The checksum first: it is tiny, and a release name the mirror does not
    // have fails here rather than after a long download.
    const std::string archive = (fs::path(dir) / archive_name).string();
    const std::string md5_path = archive + ".md5";
    std::string err;
    if (!download(mirror + "/" + archive_name + ".md5", md5_path, {}, err)) {
        res.error = "NextPi " + target + ": cannot download " + archive_name + ".md5 from " + mirror +
                    ": " + err;
        return res;
    }
    std::smatch m;
    const std::string md5_text = read_file(md5_path);
    if (!std::regex_search(md5_text, m, std::regex("[0-9a-fA-F]{32}"))) {
        res.error = md5_path + ": no MD5 in it";
        return res;
    }
    std::string expected = m.str();
    std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);

    // An archive left by an interrupted run is reused if it checks out.
    bool have_archive = fs::exists(archive, ec) && md5_file(archive) == expected;
    if (!have_archive) {
        if (!download(mirror + "/" + archive_name, archive, opts.progress, err)) {
            fs::remove(archive, ec);
            res.error = "NextPi download failed: " + err;
            return res;
        }
        bool matches = false;
        busy("Checking the NextPi download", [&] { matches = md5_file(archive) == expected; return matches; });
        if (!matches) {
            fs::remove(archive, ec);
            res.error = archive + ": MD5 mismatch (the download is corrupt); try again";
            return res;
        }
    }

    // Unpack beside the old release and swap only once everything is there,
    // so a failure leaves the previous NextPi (if any) usable.
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
        res.error = "NextPi " + target + ": " + err;
        return res;
    }

    fs::remove(fs::path(dir) / kImageName, ec);
    fs::remove(fs::path(dir) / "overlay.qcow2", ec);    // belonged to the old image
    fs::remove_all(fs::path(dir) / "boot", ec);
    fs::rename(image_part, fs::path(dir) / kImageName, ec);
    if (!ec) fs::rename(boot_part, fs::path(dir) / "boot", ec);
    if (ec || !write_file((fs::path(dir) / kReleaseName).string(), target + "\n")) {
        res.error = dir + ": cannot install NextPi " + target + (ec ? ": " + ec.message() : "");
        return res;
    }
    fs::remove(archive, ec);
    fs::remove(md5_path, ec);
    Log::uart()->info("NextPi {} installed in {}", target, dir);

    res.status  = sdcard::ProvisionStatus::Ok;
    res.dir     = dir;
    res.release = target;
    return res;
#endif
}

} // namespace pizero
