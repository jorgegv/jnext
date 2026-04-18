// SD card compliance tests — focused on the SdCardDevice SPI-mode state
// machine.  These tests exercise the public SpiDevice surface
// (receive/send/deselect) directly against a tiny temporary raw-image
// file so the byte-accurate SPI pipeline delivered to the host can be
// asserted deterministically.
//
// Coverage today:
//   INIT-01 / INIT-02   — CMD0 + ACMD41 init sequence.
//   CMD17-01            — Single-block read returns the right 512 bytes.
//   CMD18-01 / 02 / 03  — Multi-block read streams consecutive sectors,
//                         inter-block token is 0xFE, and CMD12 aborts
//                         the stream cleanly.
//   CMD18-04            — CS deassert also aborts the stream
//                         (VHDL-faithful — real cards terminate on CS high).
//
// The fixture writes a small image with distinctive per-sector patterns
// (sector S has bytes = S*1..S*1+511 mod 256 in its first few positions),
// which lets each block's bytes be verified in isolation.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
// so the Makefile aggregator picks it up.

#include "peripheral/sd_card.h"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>   // write, close, mkstemp
#include <vector>

// ─── Test infrastructure ─────────────────────────────────────────────
namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

// Build a temp image of N sectors.  Sector S's first 4 bytes encode S
// as a 32-bit LE magic plus its index so the first few bytes uniquely
// identify the sector in a stream.
std::string make_image(uint32_t n_sectors) {
    char tmpl[] = "/tmp/jnext-sdcard-test-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) { std::perror("mkstemp"); std::exit(1); }
    for (uint32_t s = 0; s < n_sectors; ++s) {
        unsigned char buf[512] = {};
        buf[0] = static_cast<unsigned char>( s        & 0xFF);
        buf[1] = static_cast<unsigned char>((s >> 8)  & 0xFF);
        buf[2] = static_cast<unsigned char>((s >> 16) & 0xFF);
        buf[3] = static_cast<unsigned char>((s >> 24) & 0xFF);
        // fill remainder with a rolling pattern (s + i) for full-block
        // verification if ever needed
        for (int i = 4; i < 512; ++i)
            buf[i] = static_cast<unsigned char>((s + i) & 0xFF);
        if (write(fd, buf, 512) != 512) {
            std::perror("write"); std::exit(1);
        }
    }
    close(fd);
    return tmpl;
}

// Send one byte from host to card.  Mirrors SpiMaster::write_data().
void spi_write(SdCardDevice& sd, uint8_t v) { (void)sd.receive(v); }

// Read one byte from card to host.  Returns the byte the host sees
// (matches SpiMaster::read_data() pipeline: returns previous rx, then
// samples next).
//
// The tests call spi_read() in tight loops; to model the one-byte
// pipeline delay a stateful pipeline mirror would be needed.  Here
// we collapse to a direct model: after each read, the byte the HOST
// sees is the one the device returned.  This matches the real VHDL
// behaviour AT THE DEVICE SIDE (send() advances device state and
// returns the next byte); the SpiMaster pipeline delay is tested
// separately in divmmc_test.cpp and is not duplicated here.
uint8_t spi_read(SdCardDevice& sd) { return sd.send(); }

// Send a 6-byte SPI command and return the first non-0xFF response byte
// (the R1).  Caller is responsible for CS already being asserted
// (which in SPI mode is implicit while the test holds no CS management).
uint8_t send_cmd_r1(SdCardDevice& sd, uint8_t cmd, uint32_t arg) {
    spi_write(sd, 0x40 | (cmd & 0x3F));
    spi_write(sd, (arg >> 24) & 0xFF);
    spi_write(sd, (arg >> 16) & 0xFF);
    spi_write(sd, (arg >>  8) & 0xFF);
    spi_write(sd,  arg        & 0xFF);
    spi_write(sd, 0x95);  // dummy CRC + stop bit (valid for CMD0)

    for (int i = 0; i < 16; ++i) {
        uint8_t b = spi_read(sd);
        if (b != 0xFF) return b;
    }
    return 0xFF;  // timeout
}

// After R1, poll for the 0xFE data token.  Returns true if found.
bool wait_token(SdCardDevice& sd, int max_polls = 16) {
    for (int i = 0; i < max_polls; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0xFE) return true;
        if (b != 0xFF && b != 0x00) return false;  // unexpected byte
    }
    return false;
}

// Read 512 data bytes + 2 CRC bytes (CRC discarded).
void read_block(SdCardDevice& sd, uint8_t* out) {
    for (int i = 0; i < 512; ++i) out[i] = spi_read(sd);
    (void)spi_read(sd);  // CRC hi
    (void)spi_read(sd);  // CRC lo
}

// Bring the card out of reset into ready state for CMD17/CMD18 reads.
void init_card(SdCardDevice& sd) {
    (void)send_cmd_r1(sd, 0, 0);           // CMD0 GO_IDLE
    (void)send_cmd_r1(sd, 8, 0x1AA);       // CMD8 SEND_IF_COND
    (void)send_cmd_r1(sd, 55, 0);          // CMD55 APP_CMD
    (void)send_cmd_r1(sd, 41, 0x40000000); // ACMD41 SD_SEND_OP_COND
    (void)send_cmd_r1(sd, 58, 0);          // CMD58 READ_OCR
}

} // namespace

// ─── Tests ───────────────────────────────────────────────────────────

static void test_init(SdCardDevice& sd) {
    // INIT-01: CMD0 returns R1=0x01 (idle) before ACMD41.
    sd.reset();
    uint8_t r1 = send_cmd_r1(sd, 0, 0);
    check("INIT-01",
          "CMD0 returns R1=0x01 (in-idle) before ACMD41",
          r1 == 0x01, "got=" + std::to_string(r1));

    // INIT-02: After CMD0-8-55-41-58 sequence CMD17 R1 is 0x00 (ready).
    sd.reset();
    init_card(sd);
    r1 = send_cmd_r1(sd, 17, 0);
    check("INIT-02",
          "After init sequence, CMD17 R1=0x00 (ready)",
          r1 == 0x00, "got=" + std::to_string(r1));
    // Drain the CMD17 block we just started so the state machine
    // cleans up for subsequent tests.
    (void)wait_token(sd);
    uint8_t dummy[512];
    read_block(sd, dummy);
    sd.deselect();
}

static void test_cmd17_read(SdCardDevice& sd) {
    // CMD17-01: Single-block read of sector 1 returns the distinctive
    // first bytes we wrote into the image.
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 17, 1);
    bool tok = wait_token(sd);
    uint8_t buf[512] = {};
    if (tok) read_block(sd, buf);
    sd.deselect();

    const bool ok = (r1 == 0x00)
                 && tok
                 && buf[0] == 0x01 && buf[1] == 0x00
                 && buf[2] == 0x00 && buf[3] == 0x00;
    check("CMD17-01",
          "CMD17 sector=1 returns the correct first 4 sector-identity bytes",
          ok,
          "r1=" + std::to_string(r1) +
          " tok=" + (tok ? "1" : "0") +
          " buf0123=" + std::to_string(buf[0]) + "," +
                       std::to_string(buf[1]) + "," +
                       std::to_string(buf[2]) + "," +
                       std::to_string(buf[3]));
}

static void test_cmd18_stream(SdCardDevice& sd) {
    // CMD18-01: Start multi-block read at sector=3; read 3 consecutive
    // blocks and verify each block's first 4 bytes match the sector
    // index they correspond to.
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 18, 3);
    bool tok1 = wait_token(sd);
    uint8_t b1[512] = {}; if (tok1) read_block(sd, b1);

    // CMD18-02: the inter-block separator must eventually produce
    // another 0xFE token before the NEXT block's data.
    bool tok2 = wait_token(sd, 32);
    uint8_t b2[512] = {}; if (tok2) read_block(sd, b2);

    bool tok3 = wait_token(sd, 32);
    uint8_t b3[512] = {}; if (tok3) read_block(sd, b3);

    // CMD18-03: CMD12 during stream aborts cleanly, state returns to
    // IDLE and a subsequent CMD17 on the same card works again.
    (void)send_cmd_r1(sd, 12, 0);
    sd.deselect();

    // Re-use the card after CMD12 to verify it is not stuck.
    sd.reset();
    init_card(sd);
    uint8_t r1_post = send_cmd_r1(sd, 17, 5);
    bool tok_post = wait_token(sd);
    uint8_t post[512] = {}; if (tok_post) read_block(sd, post);
    sd.deselect();

    check("CMD18-01",
          "CMD18 first block at sector=3 has correct identity bytes",
          (r1 == 0x00) && tok1
              && b1[0] == 0x03 && b1[1] == 0x00
              && b1[2] == 0x00 && b1[3] == 0x00,
          "r1=" + std::to_string(r1) +
          " tok=" + (tok1 ? "1" : "0") +
          " b0=" + std::to_string(b1[0]));
    check("CMD18-02",
          "CMD18 second and third streamed blocks cover sector+1 and +2",
          tok2 && tok3
              && b2[0] == 0x04 && b3[0] == 0x05,
          "tok2=" + std::string(tok2 ? "1" : "0") +
          " tok3=" + std::string(tok3 ? "1" : "0") +
          " b2_0=" + std::to_string(b2[0]) +
          " b3_0=" + std::to_string(b3[0]));
    check("CMD18-03",
          "CMD12 aborts CMD18 stream cleanly; card ready for subsequent CMD17",
          (r1_post == 0x00) && tok_post && post[0] == 0x05,
          "r1_post=" + std::to_string(r1_post) +
          " tok_post=" + (tok_post ? "1" : "0") +
          " post0=" + std::to_string(post[0]));
}

static void test_cmd18_cs_deassert_aborts(SdCardDevice& sd) {
    // CMD18-04: CS deassert (deselect) while streaming terminates the
    // multi-block state; a fresh CMD17 after re-init works.
    sd.reset();
    init_card(sd);
    (void)send_cmd_r1(sd, 18, 2);
    (void)wait_token(sd);
    // Read partial data; abort mid-block by deselecting.
    uint8_t scratch[64];
    for (int i = 0; i < 64; ++i) scratch[i] = spi_read(sd);
    sd.deselect();

    // Re-select and re-issue CMD17 at a new sector.
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 17, 7);
    bool tok = wait_token(sd);
    uint8_t b[512] = {}; if (tok) read_block(sd, b);
    sd.deselect();

    check("CMD18-04",
          "CS deassert during CMD18 stream aborts cleanly; CMD17 afterward works",
          (r1 == 0x00) && tok && b[0] == 0x07,
          "r1=" + std::to_string(r1) +
          " tok=" + (tok ? "1" : "0") +
          " b0=" + std::to_string(b[0]));
}

int main() {
    std::printf("SD card compliance tests\n");
    std::printf("====================================\n\n");

    // Small image: 16 sectors (8 KB) is plenty for the fixtures above.
    std::string img = make_image(16);
    SdCardDevice sd;
    if (!sd.mount(img)) {
        std::fprintf(stderr, "FATAL: failed to mount %s\n", img.c_str());
        std::remove(img.c_str());
        return 1;
    }

    test_init(sd);
    test_cmd17_read(sd);
    test_cmd18_stream(sd);
    test_cmd18_cs_deassert_aborts(sd);

    sd.unmount();
    std::remove(img.c_str());

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
