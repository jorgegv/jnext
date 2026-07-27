#include "core/sdcard_provisioner.h"
#include "core/fat32_image.h"
#include "core/log.h"

#include <zlib.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace sdcard {

const char* const kDistroUrl =
    "https://www.specnext.com/distro/24.11/sn-emulator-24.11.zip";
const char* const kImageFileName = "cspect-next-1gb.img";
const char* const kFixedImageFileName = "cspect-next-1gb-fixed.img";

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
namespace {
std::string home_dir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    return ".";
}
// jnext's per-user state directory. $JNEXT_CONFIG_DIR overrides it, exactly as
// in src/gui/app_config.cpp (AppConfig::default_config_path) and
// src/debugger/debugger_window.cpp (jnext_config_dir) — the SD image has always
// lived in the SAME directory as jnext.conf and Debugger.conf (see
// src/gui/app_config.h), so all three must resolve it the same way. The
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
bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
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
} // namespace

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

// The network download (default_http_download) and the SHA-256 helpers
// (sha256_hex / sha256_file) live in the per-platform backend files:
//   sdcard_provisioner_net_curl.cpp  — libcurl + OpenSSL EVP (Linux/macOS)
//   sdcard_provisioner_net_win.cpp   — WinHTTP + BCrypt/CNG (Windows, GH #108
//                                      Phase B: no curl/OpenSSL DLLs shipped,
//                                      Win7-clean import set)

bool cli_progress(uint64_t downloaded, uint64_t total) {
    // Throttle to whole-percent changes to avoid flooding stderr.
    static int last_pct = -1;
    if (total > 0) {
        const int pct = static_cast<int>((downloaded * 100ULL) / total);
        if (pct != last_pct) {
            last_pct = pct;
            std::fprintf(stderr, "\rDownloading SD-card image: %3d%%", pct);
            if (pct >= 100) std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    } else {
        // No Content-Length: show accumulated KiB every ~1 MiB.
        static uint64_t last_mib = 0;
        const uint64_t mib = downloaded >> 20;
        if (mib != last_mib) {
            last_mib = mib;
            std::fprintf(stderr, "\rDownloading SD-card image: %llu MiB",
                         static_cast<unsigned long long>(mib));
            std::fflush(stderr);
        }
    }
    return true; // CLI progress never cancels
}

bool cli_busy(const std::string& phase, const std::function<bool()>& work) {
    // Print the phase (no trailing newline yet) so the terminal shows work is
    // in progress, then run it and finish the line with the outcome. When a
    // download preceded this, cli_progress ends its bar with a newline only in
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
// FAT32 patch orchestration
// ---------------------------------------------------------------------------
bool patch_image_fat32(const std::string& image_path, std::string& err) {
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

    // Read the whole tree into memory (files total only a few MB).
    Fat32Tree tree;
    if (!fat32_read_tree(image_path, part_lba, tree)) {
        err = "failed to read FAT32 tree";
        return false;
    }

    // Inject the default config.ini.
    fat32_tree_upsert(tree, "MACHINES/NEXT/config.ini", default_config_ini());

    // Reformat the partition in place to a spec-valid FAT32 with 8 KB clusters
    // (via vendored ChaN FatFs f_mkfs + f_write) and re-emit the whole tree.
    if (!fat32_format_and_populate(image_path, part_lba, total_sectors, tree, err)) {
        return false;
    }
    Log::emulator()->info(
        "sdcard: patched {} -> FAT32 with 8 KB clusters ({} partition sectors)",
        image_path, total_sectors);
    return true;
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

    // The FIXED (patched) image is what jnext boots from. If it is already
    // present, use it — unless a re-provision is forced.
    if (!opts.force_download && file_exists(fixed)) {
        r.status = ProvisionStatus::Ok;
        r.path   = fixed;
        Log::emulator()->info("sdcard: using default image {}", fixed);
        return r;
    }

    std::string err;
    const std::string raw_sha_path = raw + ".sha256";

    // Optimization: if the raw official image is already present (and we are
    // not force-re-downloading), skip the large download and produce the fixed
    // image from it — but ONLY if the raw's SHA256 matches its `.sha256`
    // sidecar. A missing/unreadable sidecar or a mismatch marks the raw
    // untrusted (stale/corrupt) and forces the full download cycle.
    bool raw_trusted = false;
    if (!opts.force_download && file_exists(raw)) {
        const std::string stored = read_stored_sha256(raw_sha_path);
        if (stored.empty()) {
            Log::emulator()->warn(
                "sdcard: raw image {} has no readable SHA256 sidecar; "
                "re-downloading", raw);
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

    if (!raw_trusted) {
        ConfirmFn confirm = opts.confirm ? opts.confirm : ConfirmFn(cli_confirm);
        DownloadFn download = opts.download ? opts.download
                                            : DownloadFn(default_http_download);

        if (!opts.auto_confirm) {
            std::string msg =
                "No SD-card image was found at ~/.jnext/.\n\n"
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

        const std::string zip_tmp = dir + "/sn-emulator.zip.part";
        if (!download(distro_url, zip_tmp, opts.progress, err)) {
            r.status = ProvisionStatus::Failed;
            r.error  = "download failed: " + err;
            return r;
        }

        // Extract the OFFICIAL image to the raw path and keep it pristine.
        if (!unzip_entry(zip_tmp, kImageFileName, raw, err)) {
            std::remove(zip_tmp.c_str());
            std::remove(raw.c_str()); // never trust a partial raw
            r.status = ProvisionStatus::Failed;
            r.error  = "unzip failed: " + err;
            return r;
        }
        std::remove(zip_tmp.c_str());

        // Record the SHA256 of the freshly-downloaded pristine raw, so a later
        // skip-redownload can prove the raw is intact. Written only AFTER a
        // fully successful download+unzip.
        const std::string digest = sha256_file(raw);
        if (!digest.empty()) {
            std::ofstream sf(raw_sha_path, std::ios::trunc);
            if (sf) sf << digest << "  " << kImageFileName << "\n";
        }
    }

    // Produce the fixed image from the pristine raw one: copy, then FAT32
    // recluster the COPY in place (the raw official image is left untouched).
    // Both steps run over a ~1 GB image and take several seconds with no
    // natural progress hook (f_mkfs is one blocking call), so they are wrapped
    // in the BusyFn seam: the GUI animates a "Fixing downloaded image…" dialog
    // on a worker thread; the CLI prints a one-line message; tests (no busy)
    // run the work inline unchanged.
    CopyFn copy = opts.copy ? opts.copy : CopyFn(default_copy_file);
    auto do_fix = [&]() -> bool {
        if (!copy(raw, fixed, err)) { err = "cannot produce fixed image: " + err; return false; }
        if (!patch_image_fat32(fixed, err)) { err = "patch failed: " + err;       return false; }
        return true;
    };
    const bool fixed_ok = opts.busy ? opts.busy("Fixing downloaded image", do_fix)
                                     : do_fix();
    if (!fixed_ok) {
        std::remove(fixed.c_str()); // never leave a truncated / half-patched image
        r.status = ProvisionStatus::Failed;
        r.error  = err;             // do_fix() already prefixed the failing step
        return r;
    }

    r.status = ProvisionStatus::Ok;
    r.path   = fixed;
    return r;
}

} // namespace sdcard
