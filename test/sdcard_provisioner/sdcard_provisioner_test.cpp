// SD-card provisioner tests (Task 27).
//
// Exercises the offline-testable pieces of the download/provision flow. The
// network fetch and the user confirmation are behind std::function seams, so
// NO test here ever touches the network.
//
//   PROV-PATH-01   default_sdcard_dir / (fixed + raw) image_path derive from
//                  $HOME; the default (used) image is the -fixed.img one.
//   PROV-PREC-01   Explicit --sd-card wins; download seam is never invoked.
//   PROV-PREC-02   Default-location FIXED image is used when present; no download.
//   PROV-PREC-03   --sdcard-download-force ignores an explicit path and an
//                  existing default image (routes to the download branch).
//   PROV-CONF-01   No image + declined confirm → Declined; no download.
//   PROV-CONF-02   Confirm accepted + download seam fails → Failed; seam ran.
//   PROV-PROG-01   The ProvisionOptions.progress seam is forwarded into the
//                  download seam and invoked.
//   PROV-PROG-02   A progress fn returning false (cancel) → download aborts →
//                  Failed.
//   PROV-FIXED-01  Skip-redownload: with a valid raw cspect-next-1gb.img
//                  (+ matching .sha256) present, provision produces
//                  cspect-next-1gb-fixed.img WITHOUT a download; both files
//                  exist afterwards AND the raw is byte-identical (pristine).
//   PROV-COPY-FAIL-01  A CopyFn that fails → Failed AND the fixed image is
//                  removed (no truncated fixed left behind).
//   PROV-PATCH-FAIL-01 A raw that is not a valid FAT32 image → the copy
//                  succeeds but patch_image_fat32(fixed) fails → Failed AND
//                  the fixed image is removed.
//   SHA256-01      sha256_hex / sha256_file match known NIST digests.
//   PROV-SHA-MATCH-01    raw + correct .sha256 → skip-redownload taken
//                  (download seam NOT invoked), fixed produced.
//   PROV-SHA-MISMATCH-01 raw tampered so it no longer matches .sha256 → skip
//                  REJECTED, the download seam IS invoked (full cycle).
//   PROV-SHA-WRITE-01    after a successful (stubbed) download, the .sha256
//                  sidecar exists and matches the raw's actual hash.
//   PROV-UNZIP-01  Extract a STORED entry from a crafted zip.
//   PROV-UNZIP-02  Extract a DEFLATED entry from a crafted zip.
//   PROV-UNZIP-03  Missing entry → false.

#include "core/fat32_image.h"
#include "core/sdcard_provisioner.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string g_tmpdir;

std::string tp(const char* name) { return g_tmpdir + "/" + name; }

void write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}
bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}

uint32_t crc_of(const std::vector<uint8_t>& d) {
    return static_cast<uint32_t>(crc32(0L, d.data(), static_cast<uInt>(d.size())));
}

// Build a minimal single-entry zip. method 0 = stored, 8 = deflate.
std::vector<uint8_t> make_zip(const std::string& name,
                              const std::vector<uint8_t>& data, int method) {
    std::vector<uint8_t> comp;
    if (method == 0) {
        comp = data;
    } else {
        comp.resize(data.size() + 128 + data.size() / 2);
        z_stream s{};
        deflateInit2(&s, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        s.next_in = const_cast<Bytef*>(data.data());
        s.avail_in = static_cast<uInt>(data.size());
        s.next_out = comp.data();
        s.avail_out = static_cast<uInt>(comp.size());
        deflate(&s, Z_FINISH);
        comp.resize(comp.size() - s.avail_out);
        deflateEnd(&s);
    }
    const uint32_t crc = crc_of(data);
    std::vector<uint8_t> z;

    const uint32_t local_off = 0;
    // Local file header.
    put32(z, 0x04034b50);
    put16(z, 20); put16(z, 0); put16(z, static_cast<uint16_t>(method));
    put16(z, 0); put16(z, 0);                 // time/date
    put32(z, crc);
    put32(z, static_cast<uint32_t>(comp.size()));
    put32(z, static_cast<uint32_t>(data.size()));
    put16(z, static_cast<uint16_t>(name.size()));
    put16(z, 0);
    z.insert(z.end(), name.begin(), name.end());
    z.insert(z.end(), comp.begin(), comp.end());

    const uint32_t cd_off = static_cast<uint32_t>(z.size());
    // Central directory header.
    put32(z, 0x02014b50);
    put16(z, 20); put16(z, 20); put16(z, 0); put16(z, static_cast<uint16_t>(method));
    put16(z, 0); put16(z, 0);
    put32(z, crc);
    put32(z, static_cast<uint32_t>(comp.size()));
    put32(z, static_cast<uint32_t>(data.size()));
    put16(z, static_cast<uint16_t>(name.size()));
    put16(z, 0); put16(z, 0); put16(z, 0); put16(z, 0);
    put32(z, 0);
    put32(z, local_off);
    z.insert(z.end(), name.begin(), name.end());

    const uint32_t cd_size = static_cast<uint32_t>(z.size()) - cd_off;
    // EOCD.
    put32(z, 0x06054b50);
    put16(z, 0); put16(z, 0);
    put16(z, 1); put16(z, 1);
    put32(z, cd_size);
    put32(z, cd_off);
    put16(z, 0);
    return z;
}

void wr32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

Fat32Node fnode(const std::string& n, const std::string& content) {
    Fat32Node x; x.name = n; x.is_dir = false;
    x.data.assign(content.begin(), content.end());
    return x;
}

// Build a small-but-spec-valid FAT32 source image (MBR + one FAT32-LBA
// partition) at `path`, sparse on disk. ~600 MB partition so f_mkfs with
// 8 KB clusters clears the 65525-cluster FAT32 minimum — same geometry the
// real distribution image uses. Mirrors fat32_image_test's builder.
bool make_fat32_source(const std::string& path, uint32_t part_lba,
                       uint32_t total_sectors, std::string& err) {
    const uint64_t bytes = static_cast<uint64_t>(part_lba + total_sectors) * 512;
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot create " + path; return false; }
        std::vector<uint8_t> mbr(512, 0);
        uint8_t* pe = mbr.data() + 0x1BE;
        pe[4] = 0x0C;                 // type FAT32 LBA
        wr32(pe + 8, part_lba);       // start LBA
        wr32(pe + 12, total_sectors); // sectors
        mbr[510] = 0x55; mbr[511] = 0xAA;
        f.write(reinterpret_cast<char*>(mbr.data()), 512);
        f.seekp(static_cast<std::streamoff>(bytes - 1)); char z = 0; f.write(&z, 1);
        if (!f.good()) { err = "write error on " + path; return false; }
    }
    Fat32Tree tree;
    tree.root.push_back(fnode("HELLO.TXT", "hi"));
    return fat32_format_and_populate(path, part_lba, total_sectors, tree, err);
}

// Write a sha256sum-style sidecar "<hex>  cspect-next-1gb.img\n" next to `raw`.
void write_sha256_sidecar(const std::string& raw, const std::string& hex) {
    std::ofstream f(raw + ".sha256", std::ios::trunc);
    f << hex << "  cspect-next-1gb.img\n";
}

// Read the first whitespace-delimited token of a text file (the hex digest).
std::string read_first_token(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    f >> tok;
    return tok;
}

} // namespace

int main() {
    // Isolated HOME so path/default-location tests are hermetic.
    char tmpl[] = "/tmp/jnext_prov_XXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) { std::printf("cannot mkdtemp\n"); return 1; }
    g_tmpdir = d;
    setenv("HOME", g_tmpdir.c_str(), 1);

    // -- PROV-PATH-01 --
    {
        const std::string dir   = sdcard::default_sdcard_dir();
        const std::string fixed = sdcard::default_sdcard_image_path();
        const std::string raw   = sdcard::default_sdcard_raw_image_path();
        check("PROV-PATH-01", "default dir == $HOME/.jnext/sdcard",
              dir == g_tmpdir + "/.jnext/sdcard", dir);
        check("PROV-PATH-01", "default (used) image == dir/cspect-next-1gb-fixed.img",
              fixed == dir + "/cspect-next-1gb-fixed.img", fixed);
        check("PROV-PATH-01", "raw download image == dir/cspect-next-1gb.img",
              raw == dir + "/cspect-next-1gb.img", raw);
    }

    // -- SHA256-01: known-answer digests (non-circular NIST vectors) --
    {
        check("SHA256-01", "sha256_hex(\"\")",
              sdcard::sha256_hex({}) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        std::vector<uint8_t> abc = {'a','b','c'};
        check("SHA256-01", "sha256_hex(\"abc\")",
              sdcard::sha256_hex(abc) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        write_file(tp("abc.bin"), abc);
        check("SHA256-01", "sha256_file(\"abc\")",
              sdcard::sha256_file(tp("abc.bin")) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check("SHA256-01", "sha256_file(missing) => empty",
              sdcard::sha256_file(tp("no_such_file.bin")).empty());
    }

    // Download seam that records invocation and fails (so no real net op).
    // New 4-arg signature carries the ProgressFn seam.
    bool download_called = false;
    sdcard::DownloadFn recording_dl =
        [&](const std::string&, const std::string&,
            const sdcard::ProgressFn&, std::string& err) {
            download_called = true; err = "stub: no network"; return false;
        };

    // -- PROV-PREC-01: explicit path wins, no download --
    {
        download_called = false;
        sdcard::ProvisionOptions o;
        o.explicit_path = "/some/explicit/path.img";
        o.download = recording_dl;
        o.confirm = [](const std::string&) { return true; };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PREC-01", "explicit path returned as-is",
              r.status == sdcard::ProvisionStatus::Ok && r.path == "/some/explicit/path.img");
        check("PROV-PREC-01", "download seam not invoked", !download_called);
    }

    // -- PROV-PREC-02: default-location file present --
    {
        download_called = false;
        ::mkdir((g_tmpdir + "/.jnext").c_str(), 0755);
        ::mkdir((g_tmpdir + "/.jnext/sdcard").c_str(), 0755);
        write_file(sdcard::default_sdcard_image_path(), {'i','m','g'});
        sdcard::ProvisionOptions o;
        o.download = recording_dl;
        o.confirm = [](const std::string&) { return true; };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PREC-02", "default-location image used",
              r.status == sdcard::ProvisionStatus::Ok &&
              r.path == sdcard::default_sdcard_image_path());
        check("PROV-PREC-02", "download seam not invoked when local image exists",
              !download_called);
    }

    // -- PROV-PREC-03: an explicit --sd-card ALWAYS wins, even with
    // --sdcard-download-force. Force applies only to the default-location
    // flow; it must never re-download over, or discard, an explicit path.
    {
        download_called = false; // default image still present from PREC-02
        sdcard::ProvisionOptions o;
        o.explicit_path = "/some/explicit/path.img";
        o.force_download = true;
        o.auto_confirm = true; // would skip the prompt IF we reached download
        o.download = recording_dl; // must NOT be called
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PREC-03", "explicit path returned verbatim despite force",
              r.status == sdcard::ProvisionStatus::Ok &&
              r.path == "/some/explicit/path.img", r.path);
        check("PROV-PREC-03", "download seam never invoked when --sd-card given",
              !download_called);
    }

    // Remove the default image for the confirm tests.
    std::remove(sdcard::default_sdcard_image_path().c_str());

    // -- PROV-CONF-01: declined confirm --
    {
        download_called = false;
        sdcard::ProvisionOptions o;
        o.download = recording_dl;
        o.confirm = [](const std::string&) { return false; };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-CONF-01", "declined confirm => Declined",
              r.status == sdcard::ProvisionStatus::Declined);
        check("PROV-CONF-01", "no download after decline", !download_called);
    }

    // -- PROV-CONF-02: accepted confirm, download fails --
    {
        download_called = false;
        bool confirm_called = false;
        sdcard::ProvisionOptions o;
        o.download = recording_dl;
        o.confirm = [&](const std::string&) { confirm_called = true; return true; };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-CONF-02", "confirm seam invoked", confirm_called);
        check("PROV-CONF-02", "download seam invoked after confirm", download_called);
        check("PROV-CONF-02", "download failure => Failed",
              r.status == sdcard::ProvisionStatus::Failed);
    }

    // -- PROV-PROG-01: the progress seam is forwarded into the download seam --
    {
        // No default image present (removed above). A stub download that
        // invokes the ProgressFn it receives, records the call, then fails
        // (so no unzip/patch runs). Proves ProvisionOptions.progress reaches
        // the download seam.
        int progress_calls = 0;
        sdcard::ProvisionOptions o;
        o.auto_confirm = true; // skip the confirm prompt
        o.progress = [&](uint64_t dl, uint64_t total) {
            ++progress_calls; (void)dl; (void)total; return true;
        };
        o.download = [](const std::string&, const std::string&,
                        const sdcard::ProgressFn& prog, std::string& err) {
            if (prog) { prog(0, 100); prog(50, 100); prog(100, 100); }
            err = "stub: no network"; return false;
        };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PROG-01", "progress seam forwarded + invoked",
              progress_calls == 3, "calls=" + std::to_string(progress_calls));
        check("PROV-PROG-01", "download failure still => Failed",
              r.status == sdcard::ProvisionStatus::Failed);
    }

    // -- PROV-PROG-02: a progress fn returning false cancels the download --
    {
        bool saw_cancel = false;
        sdcard::ProvisionOptions o;
        o.auto_confirm = true;
        // The caller's progress fn requests cancel on the first tick.
        o.progress = [&](uint64_t, uint64_t) { return false; };
        // Stub download mimics libcurl: on a false ProgressFn it aborts and
        // reports failure (partial file removed by the real impl).
        o.download = [&](const std::string&, const std::string&,
                         const sdcard::ProgressFn& prog, std::string& err) {
            if (prog && !prog(0, 100)) { saw_cancel = true;
                err = "download cancelled by user"; return false; }
            return true;
        };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PROG-02", "cancel observed at download seam", saw_cancel);
        check("PROV-PROG-02", "cancelled download => Failed",
              r.status == sdcard::ProvisionStatus::Failed);
    }

    ::mkdir((g_tmpdir + "/.jnext").c_str(), 0755);
    ::mkdir((g_tmpdir + "/.jnext/sdcard").c_str(), 0755);
    const std::string raw       = sdcard::default_sdcard_raw_image_path();
    const std::string fixed     = sdcard::default_sdcard_image_path();
    const std::string raw_sha   = raw + ".sha256";

    // -- PROV-FIXED-01 + PROV-SHA-MATCH-01: skip-redownload produces the fixed
    //    image from a trusted raw (SHA256 matches), no download, raw pristine --
    {
        std::remove(fixed.c_str());
        std::string berr;
        // ~600 MB partition at LBA 63 (sparse) → valid 8 KB-cluster FAT32.
        bool src_ok = make_fat32_source(raw, 63, 1228800, berr);
        check("PROV-FIXED-01", "raw FAT32 source built", src_ok, berr);

        const std::string h_before = sdcard::sha256_file(raw);
        write_sha256_sidecar(raw, h_before); // trusted raw

        download_called = false;
        sdcard::ProvisionOptions o;
        o.download = recording_dl; // must NOT be called (skip-redownload)
        o.confirm  = [](const std::string&) { return true; };
        auto r = src_ok ? sdcard::provision_sd_card(o) : sdcard::ProvisionResult{};

        check("PROV-FIXED-01", "provision Ok, path is the fixed image",
              src_ok && r.status == sdcard::ProvisionStatus::Ok &&
              r.path == fixed, r.path);
        check("PROV-SHA-MATCH-01", "no download when raw SHA256 matches",
              !download_called);
        check("PROV-FIXED-01", "fixed image produced", file_exists(fixed));
        check("PROV-FIXED-01", "raw image kept", file_exists(raw));
        // Pristine: raw bytes unchanged (would fail if code patched raw).
        check("PROV-FIXED-01", "raw is byte-identical (pristine)",
              !h_before.empty() && sdcard::sha256_file(raw) == h_before);

        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());
        std::remove(fixed.c_str());
    }

    // -- PROV-COPY-FAIL-01: a CopyFn failure removes the (partial) fixed --
    {
        std::remove(fixed.c_str());
        write_file(raw, {'r','a','w'});           // small trusted raw
        write_sha256_sidecar(raw, sdcard::sha256_file(raw));

        download_called = false;
        sdcard::ProvisionOptions o;
        o.download = recording_dl;                // must NOT be called
        o.confirm  = [](const std::string&) { return true; };
        // CopyFn writes a PARTIAL fixed then fails (mimics a truncated copy):
        // the pre-fix no-remove code would leave this file behind.
        o.copy = [](const std::string&, const std::string& dst,
                    std::string& err) {
            std::ofstream f(dst, std::ios::binary | std::ios::trunc);
            f << "partial";
            err = "stub: copy failed"; return false;
        };
        auto r = sdcard::provision_sd_card(o);
        check("PROV-COPY-FAIL-01", "copy failure => Failed",
              r.status == sdcard::ProvisionStatus::Failed);
        check("PROV-COPY-FAIL-01", "fixed removed after copy failure",
              !file_exists(fixed));
        check("PROV-COPY-FAIL-01", "no download (raw trusted)", !download_called);

        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());
        std::remove(fixed.c_str());
    }

    // -- PROV-PATCH-FAIL-01: a non-FAT32 raw copies OK but patch fails →
    //    the fixed image is removed --
    {
        std::remove(fixed.c_str());
        std::vector<uint8_t> junk(4096, 0xEE);    // no MBR/FAT32 partition
        write_file(raw, junk);
        write_sha256_sidecar(raw, sdcard::sha256_file(raw));

        download_called = false;
        sdcard::ProvisionOptions o;
        o.download = recording_dl;                // must NOT be called
        o.confirm  = [](const std::string&) { return true; };
        // real default copy runs (fixed = copy of junk); patch then fails.
        auto r = sdcard::provision_sd_card(o);
        check("PROV-PATCH-FAIL-01", "patch failure => Failed",
              r.status == sdcard::ProvisionStatus::Failed);
        check("PROV-PATCH-FAIL-01", "fixed removed after patch failure",
              !file_exists(fixed));
        check("PROV-PATCH-FAIL-01", "no download (raw trusted)", !download_called);

        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());
        std::remove(fixed.c_str());
    }

    // -- PROV-SHA-MISMATCH-01: a raw that no longer matches its .sha256 is
    //    untrusted → skip REJECTED, the download seam IS invoked --
    {
        std::remove(fixed.c_str());
        write_file(raw, {'t','a','m','p','e','r','e','d'});
        // Sidecar holds the hash of DIFFERENT bytes → mismatch.
        write_sha256_sidecar(raw, sdcard::sha256_hex({'o','t','h','e','r'}));

        download_called = false;
        sdcard::ProvisionOptions o;
        o.auto_confirm = true;      // skip the prompt; go straight to download
        o.download = recording_dl;  // MUST be invoked (skip rejected)
        auto r = sdcard::provision_sd_card(o);
        check("PROV-SHA-MISMATCH-01", "download invoked on SHA256 mismatch",
              download_called);
        check("PROV-SHA-MISMATCH-01", "mismatch + failed download => Failed",
              r.status == sdcard::ProvisionStatus::Failed);

        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());
        std::remove(fixed.c_str());
    }

    // -- PROV-SHA-WRITE-01: a successful download writes a matching sidecar --
    {
        std::remove(fixed.c_str());
        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());

        // Stub download: produce a STORED zip containing the official image
        // name, so the real unzip lands a raw file at the raw path. Payload is
        // non-FAT32, so the later patch fails — but the sidecar is written
        // BEFORE the copy/patch step, which is what we assert here.
        std::vector<uint8_t> payload;
        for (int i = 0; i < 2048; ++i) payload.push_back(static_cast<uint8_t>(i));
        std::vector<uint8_t> zip = make_zip("cspect-next-1gb.img", payload, 0);
        sdcard::ProvisionOptions o;
        o.auto_confirm = true;
        o.download = [&](const std::string&, const std::string& dst,
                         const sdcard::ProgressFn&, std::string&) {
            write_file(dst, zip); return true; // "downloaded" the zip
        };
        auto r = sdcard::provision_sd_card(o);
        (void)r; // status is Failed (payload isn't FAT32) — not asserted here
        check("PROV-SHA-WRITE-01", "sidecar written after successful download",
              file_exists(raw_sha));
        check("PROV-SHA-WRITE-01", "sidecar matches raw's actual hash",
              !sdcard::sha256_file(raw).empty() &&
              read_first_token(raw_sha) == sdcard::sha256_file(raw));

        std::remove(raw.c_str());
        std::remove(raw_sha.c_str());
        std::remove(fixed.c_str());
    }

    // -- PROV-UNZIP-01/02/03 --
    {
        std::vector<uint8_t> payload;
        for (int i = 0; i < 5000; ++i) payload.push_back(static_cast<uint8_t>(i * 7 + 3));

        std::string err;
        // Stored.
        write_file(tp("stored.zip"), make_zip("cspect-next-1gb.img", payload, 0));
        bool s_ok = sdcard::unzip_entry(tp("stored.zip"), "cspect-next-1gb.img",
                                        tp("out_stored.bin"), err);
        check("PROV-UNZIP-01", "stored entry extracted",
              s_ok && read_file(tp("out_stored.bin")) == payload, err);

        // Deflated.
        write_file(tp("defl.zip"), make_zip("cspect-next-1gb.img", payload, 8));
        bool d_ok = sdcard::unzip_entry(tp("defl.zip"), "cspect-next-1gb.img",
                                        tp("out_defl.bin"), err);
        check("PROV-UNZIP-02", "deflated entry extracted",
              d_ok && read_file(tp("out_defl.bin")) == payload, err);

        // Missing.
        bool m_ok = sdcard::unzip_entry(tp("stored.zip"), "does-not-exist.img",
                                        tp("out_missing.bin"), err);
        check("PROV-UNZIP-03", "missing entry returns false", !m_ok);
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
