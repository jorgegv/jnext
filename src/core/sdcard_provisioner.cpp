#include "core/sdcard_provisioner.h"
#include "core/fat32_image.h"
#include "core/log.h"

#include <zlib.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace sdcard {

const char* const kDistroUrl =
    "https://www.specnext.com/distro/24.11/sn-emulator-24.11.zip";
const char* const kImageFileName = "cspect-next-1gb.img";

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
namespace {
std::string home_dir() {
    const char* h = std::getenv("HOME");
    if (h && *h) return h;
    return ".";
}
// Create a directory path recursively (best effort). Returns true if the
// final directory exists afterwards.
bool make_dirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && cur.size() > 1) {
            ::mkdir(cur.c_str(), 0755); // ignore EEXIST
        }
    }
    ::mkdir(path.c_str(), 0755);
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
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
} // namespace

std::string default_sdcard_dir() {
    return home_dir() + "/.jnext/sdcard";
}
std::string default_sdcard_image_path() {
    return default_sdcard_dir() + "/" + kImageFileName;
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

// ---------------------------------------------------------------------------
// Download (default impl shells out to curl / wget)
// ---------------------------------------------------------------------------
bool default_http_download(const std::string& url, const std::string& dest_path,
                           std::string& err) {
    // No libcurl in the jnext dependency set; shell out to curl (preferred,
    // present on Windows 10+, macOS, Linux) then wget as a fallback. This is
    // the only non-native piece and is fully behind the DownloadFn seam.
    auto shell_quote = [](const std::string& s) {
        std::string q = "'";
        for (char c : s) { if (c == '\'') q += "'\\''"; else q.push_back(c); }
        q += "'";
        return q;
    };
    const std::string u = shell_quote(url);
    const std::string d = shell_quote(dest_path);
    std::string cmd = "curl -L --fail -o " + d + " " + u;
    Log::emulator()->info("sdcard: downloading via curl: {}", url);
    int rc = std::system(cmd.c_str());
    if (rc == 0 && file_exists(dest_path)) return true;

    cmd = "wget -O " + d + " " + u;
    Log::emulator()->info("sdcard: curl failed (rc={}), retrying via wget", rc);
    rc = std::system(cmd.c_str());
    if (rc == 0 && file_exists(dest_path)) return true;

    err = "download failed (curl and wget both unavailable or errored)";
    return false;
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

    // Reformat to 8 KB clusters (sectors_per_cluster = 16).
    Fat32FormatGeom geom = fat32_compute_geometry(total_sectors, /*spc=*/16);
    if (!geom.valid || !geom.is_fat32) {
        err = "computed geometry is not a valid FAT32 (cluster_count=" +
              std::to_string(geom.cluster_count) + ")";
        return false;
    }
    if (!fat32_write_volume(image_path, part_lba, geom, tree, err)) {
        return false;
    }
    Log::emulator()->info(
        "sdcard: patched {} -> {} clusters/8KB, {} data clusters (FAT32 valid)",
        image_path, geom.sectors_per_cluster, geom.cluster_count);
    return true;
}

// ---------------------------------------------------------------------------
// Top-level resolution
// ---------------------------------------------------------------------------
ProvisionResult provision_sd_card(const ProvisionOptions& opts) {
    ProvisionResult r;

    // An explicit --sd-card path ALWAYS wins, unconditionally. It may name an
    // arbitrary, unrelated image; --sdcard-download-force must never re-download
    // over it or ignore it. --sdcard-download-force applies ONLY to the
    // default-location provisioning flow (recovering a corrupted
    // ~/.jnext/sdcard/cspect-next-1gb.img).
    if (!opts.explicit_path.empty()) {
        if (opts.force_download) {
            std::fprintf(stderr,
                "warning: --sdcard-download-force ignored because an explicit "
                "--sd-card was provided (%s).\n", opts.explicit_path.c_str());
        }
        r.status = ProvisionStatus::Ok;
        r.path   = opts.explicit_path;
        return r;
    }

    const std::string dir  = default_sdcard_dir();
    const std::string dest = default_sdcard_image_path();

    if (!opts.force_download && file_exists(dest)) {
        r.status = ProvisionStatus::Ok;
        r.path   = dest;
        Log::emulator()->info("sdcard: using default image {}", dest);
        return r;
    }

    // Need to download.
    ConfirmFn confirm = opts.confirm ? opts.confirm : ConfirmFn(cli_confirm);
    DownloadFn download = opts.download ? opts.download
                                        : DownloadFn(default_http_download);

    if (!opts.auto_confirm) {
        std::string msg =
            std::string("No SD-card image found at ") + dest +
            ".\nDownload the NextZXOS distribution image from " +
            kDistroUrl + " (large download) and install it there?";
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

    const std::string zip_tmp = dir + "/sn-emulator.zip.part";
    std::string err;
    if (!download(kDistroUrl, zip_tmp, err)) {
        r.status = ProvisionStatus::Failed;
        r.error  = "download failed: " + err;
        return r;
    }

    if (!unzip_entry(zip_tmp, kImageFileName, dest, err)) {
        std::remove(zip_tmp.c_str());
        r.status = ProvisionStatus::Failed;
        r.error  = "unzip failed: " + err;
        return r;
    }
    std::remove(zip_tmp.c_str());

    if (!patch_image_fat32(dest, err)) {
        r.status = ProvisionStatus::Failed;
        r.error  = "patch failed: " + err;
        return r;
    }

    r.status = ProvisionStatus::Ok;
    r.path   = dest;
    return r;
}

} // namespace sdcard
