// SD-card provisioner tests (Task 27).
//
// Exercises the offline-testable pieces of the download/provision flow. The
// network fetch and the user confirmation are behind std::function seams, so
// NO test here ever touches the network.
//
//   PROV-PATH-01   default_sdcard_dir / image_path derive from $HOME.
//   PROV-PREC-01   Explicit --sd-card wins; download seam is never invoked.
//   PROV-PREC-02   Default-location image is used when present; no download.
//   PROV-PREC-03   --sdcard-download-force ignores an explicit path and an
//                  existing default image (routes to the download branch).
//   PROV-CONF-01   No image + declined confirm → Declined; no download.
//   PROV-CONF-02   Confirm accepted + download seam fails → Failed; seam ran.
//   PROV-UNZIP-01  Extract a STORED entry from a crafted zip.
//   PROV-UNZIP-02  Extract a DEFLATED entry from a crafted zip.
//   PROV-UNZIP-03  Missing entry → false.

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
        const std::string dir = sdcard::default_sdcard_dir();
        const std::string img = sdcard::default_sdcard_image_path();
        check("PROV-PATH-01", "default dir == $HOME/.jnext/sdcard",
              dir == g_tmpdir + "/.jnext/sdcard", dir);
        check("PROV-PATH-01", "default image == dir/cspect-next-1gb.img",
              img == dir + "/cspect-next-1gb.img", img);
    }

    // Download seam that records invocation and fails (so no real net op).
    bool download_called = false;
    sdcard::DownloadFn recording_dl =
        [&](const std::string&, const std::string&, std::string& err) {
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
