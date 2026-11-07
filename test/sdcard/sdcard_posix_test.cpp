// SD card rows that need POSIX facilities: a host write that really fails at
// the OS (GH #455). The seam is RLIMIT_FSIZE, which makes write(2) fail with
// EFBIG on a file the process may otherwise write; Windows has no equivalent,
// so the rows live in a suite the manifest declares `# os: posix`.
//
// Oracle: SD Physical Layer Simplified Spec 9.10, 7.3.3.1: a block the card
// could not program is answered with data response 0x0D and is not
// programmed; the card keeps working afterwards. A rejected block must never
// land in the image later, and the card must stay readable AND writable.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "peripheral/sd_card.h"
#include "../row_id.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string make_image(uint32_t n_sectors) {
    char tmpl[] = "/tmp/jnext-sdcard-posix-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { std::perror("mkstemp"); std::exit(1); }
    for (uint32_t s = 0; s < n_sectors; ++s) {
        unsigned char buf[512];
        for (int i = 0; i < 512; ++i)
            buf[i] = static_cast<unsigned char>((s * 7 + i) & 0xFF);
        if (write(fd, buf, 512) != 512) { std::perror("write"); std::exit(1); }
    }
    close(fd);
    return tmpl;
}

uint8_t fixture_byte(uint32_t sector, int i) {
    return static_cast<uint8_t>((sector * 7 + i) & 0xFF);
}

void spi_write(SdCardDevice& sd, uint8_t v) { (void)sd.receive(v); }
uint8_t spi_read(SdCardDevice& sd) { return sd.send(); }

uint8_t send_cmd_r1(SdCardDevice& sd, uint8_t cmd, uint32_t arg) {
    spi_write(sd, 0x40 | (cmd & 0x3F));
    spi_write(sd, (arg >> 24) & 0xFF);
    spi_write(sd, (arg >> 16) & 0xFF);
    spi_write(sd, (arg >> 8) & 0xFF);
    spi_write(sd, arg & 0xFF);
    spi_write(sd, 0x95);
    for (int i = 0; i < 16; ++i) {
        const uint8_t b = spi_read(sd);
        if (b != 0xFF) return b;
    }
    return 0xFF;
}

void init_card(SdCardDevice& sd) {
    (void)send_cmd_r1(sd, 0, 0);
    (void)send_cmd_r1(sd, 8, 0x1AA);
    (void)send_cmd_r1(sd, 55, 0);
    (void)send_cmd_r1(sd, 41, 0x40000000);
    (void)send_cmd_r1(sd, 58, 0);
}

// RLIMIT_FSIZE soft limit, restored by the destructor. Nothing may be printed
// while it is low: the harness's stdout may be a file past the limit.
struct FsizeLimit {
    rlimit saved{};
    bool active = false;
    void lower(rlim_t bytes) {
        if (!active) getrlimit(RLIMIT_FSIZE, &saved);
        rlimit r = saved;
        r.rlim_cur = bytes;
        active = setrlimit(RLIMIT_FSIZE, &r) == 0;
    }
    void restore() {
        if (active) { setrlimit(RLIMIT_FSIZE, &saved); active = false; }
    }
    ~FsizeLimit() { restore(); }
};

// CMD24 of `sector` filled with `fill`; the limit is low only for the final
// CRC byte, the receive() that flushes. `keep_low` keeps it low after it.
uint8_t cmd24_failing(SdCardDevice& sd, FsizeLimit& lim, uint32_t sector,
                      uint8_t fill, bool keep_low, uint8_t* after) {
    (void)send_cmd_r1(sd, 24, sector);
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, fill);
    spi_write(sd, 0xFF);
    lim.lower(4 * 512);
    spi_write(sd, 0xFF);                // final CRC byte: commit point
    if (!keep_low) lim.restore();
    uint8_t resp = 0xFF;
    for (int i = 0; i < 32; ++i) {
        const uint8_t b = spi_read(sd);
        if (b != 0xFF) { resp = b; break; }
    }
    *after = spi_read(sd);
    return resp;
}

bool reads_original(SdCardDevice& sd, uint32_t sector) {
    if (send_cmd_r1(sd, 17, sector) != 0x00) return false;
    bool tok = false;
    for (int i = 0; i < 16 && !tok; ++i) tok = spi_read(sd) == 0xFE;
    if (!tok) return false;
    bool ok = true;
    for (int i = 0; i < 512; ++i) ok = ok && spi_read(sd) == fixture_byte(sector, i);
    (void)spi_read(sd);
    (void)spi_read(sd);
    sd.deselect();
    return ok;
}

bool file_sector_is_original(const std::string& path, uint32_t sector) {
    std::ifstream f(path, std::ios::binary);
    const std::string all((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    if (all.size() < (sector + 1) * 512ull) return false;
    for (int i = 0; i < 512; ++i)
        if (static_cast<uint8_t>(all[sector * 512ull + i]) != fixture_byte(sector, i))
            return false;
    return true;
}

}  // namespace

int main() {
    std::signal(SIGXFSZ, SIG_IGN);
    std::printf("SD card POSIX rows (host write errors)\n");

    // SDP-IOERR-01 (transient failure) and SDP-IOERR-03 (the same card then
    // writes again) share one card object.
    {
        const std::string img = make_image(8);
        SdCardDevice sd;
        sd.mount(img, false);
        init_card(sd);
        FsizeLimit lim;
        uint8_t after = 0;
        const uint8_t resp = cmd24_failing(sd, lim, 4, 0x5A, false, &after);
        sd.deselect();
        const bool rd1 = reads_original(sd, 1);
        const bool rd4 = reads_original(sd, 4);

        check("SDP-IOERR-01",
              "transient host write error: CMD24 answers 0x0D with no busy "
              "byte, the card still reads sectors 1 and 4, and the rejected "
              "block never lands in the file afterwards (SD spec 7.3.3.1)",
              resp == 0x0D && after == 0xFF && rd1 && rd4 &&
              (sd.unmount(), file_sector_is_original(img, 4)),
              "resp=" + std::to_string(resp) + " after=" + std::to_string(after) +
              " rd1=" + std::to_string(rd1) + " rd4=" + std::to_string(rd4));

        // SDP-IOERR-03 on a fresh object (-01 unmounted its own): the real
        // point is the card recovered read-write, so repeat the failure on a
        // card kept mounted and write again.
        SdCardDevice sd2;
        sd2.mount(img, false);
        init_card(sd2);
        uint8_t a2 = 0;
        const uint8_t r_fail = cmd24_failing(sd2, lim, 4, 0x5A, false, &a2);
        sd2.deselect();
        (void)send_cmd_r1(sd2, 24, 5);
        spi_write(sd2, 0xFE);
        for (int i = 0; i < 512; ++i) spi_write(sd2, 0x3C);
        spi_write(sd2, 0xFF);
        spi_write(sd2, 0xFF);
        uint8_t r_ok = 0xFF;
        for (int i = 0; i < 32; ++i) {
            const uint8_t b = spi_read(sd2);
            if (b != 0xFF) { r_ok = b; break; }
        }
        sd2.deselect();
        sd2.unmount();
        bool sec5 = false;
        {
            std::ifstream f(img, std::ios::binary);
            const std::string all((std::istreambuf_iterator<char>(f)),
                                  std::istreambuf_iterator<char>());
            sec5 = all.size() >= 6 * 512;
            for (int i = 0; sec5 && i < 512; ++i)
                sec5 = static_cast<uint8_t>(all[5 * 512 + i]) == 0x3C;
        }
        check("SDP-IOERR-03",
              "after a failed host write the card recovered read-write, not "
              "degraded to read-only: the next CMD24 is accepted (0x05) and "
              "its block is in the file",
              r_fail == 0x0D && r_ok == 0x05 && sec5,
              "fail=" + std::to_string(r_fail) + " next=" + std::to_string(r_ok) +
              " sector5=" + std::to_string(sec5));
        std::remove(img.c_str());
    }

    // SDP-IOERR-02: persistent failure; the limit stays low through reads.
    {
        const std::string img = make_image(8);
        SdCardDevice sd;
        sd.mount(img, false);
        init_card(sd);
        FsizeLimit lim;
        uint8_t after = 0;
        const uint8_t resp = cmd24_failing(sd, lim, 4, 0x5A, true, &after);
        sd.deselect();
        const bool rd1 = reads_original(sd, 1);
        const bool rd2 = reads_original(sd, 2);
        lim.restore();
        sd.unmount();
        check("SDP-IOERR-02",
              "persistent host write error: CMD24 answers 0x0D and the card "
              "still reads sectors 1 and 2 while the failure lasts",
              resp == 0x0D && rd1 && rd2,
              "resp=" + std::to_string(resp) + " rd1=" + std::to_string(rd1) +
              " rd2=" + std::to_string(rd2));
        std::remove(img.c_str());
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
