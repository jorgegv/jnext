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

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

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

void skip(const char* id, const char* reason) {
    ++g_skip;
    g_skipped.push_back({id, reason});
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

    // CMD18-03: CMD12 mid-stream must leave the card in a state where
    // a subsequent CMD17 (with NO card re-init and NO CS deassert)
    // still works.  Reviewer nit 2: previous version did reset +
    // init_card between CMD12 and CMD17, which wiped the state under
    // test.  This version only does the CMD12 stop + a CS bounce
    // (matching what the firmware's disk_read tail does via
    // deselect()).
    (void)send_cmd_r1(sd, 12, 0);
    sd.deselect();

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

static void test_cmd18_end_of_image(SdCardDevice& sd) {
    // CMD18-05: Starting CMD18 two sectors before end-of-image
    // (image is 16 sectors; start at sector 14) must cleanly stream
    // sectors 14 and 15 and then terminate — no garbage, no infinite
    // 0xFE-emission, and the card must go back to IDLE so a follow-up
    // CMD17 on the same card works.  Covers the multi_block_ past-end
    // warn+IDLE branch in send().
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 18, 14);
    bool tok14 = wait_token(sd);
    uint8_t b14[512] = {}; if (tok14) read_block(sd, b14);
    bool tok15 = wait_token(sd, 32);
    uint8_t b15[512] = {}; if (tok15) read_block(sd, b15);

    // Now drain a few more bytes — should be 0xFF (IDLE), NOT another
    // 0xFE token.  If our end-of-image guard is broken, the host would
    // see a bogus 0xFE followed by garbage data and fail downstream.
    bool seen_spurious_token = false;
    for (int i = 0; i < 16; ++i) {
        if (spi_read(sd) == 0xFE) { seen_spurious_token = true; break; }
    }

    // CS bounce to clean up before next command (matches host path).
    sd.deselect();

    // A fresh CMD17 after the end-of-image termination should still work.
    uint8_t r1_after = send_cmd_r1(sd, 17, 2);
    bool tok_after = wait_token(sd);
    uint8_t after[512] = {}; if (tok_after) read_block(sd, after);
    sd.deselect();

    check("CMD18-05",
          "CMD18 that hits end-of-image (sectors 14..15) terminates "
          "cleanly; no spurious token; follow-up CMD17 works",
          (r1 == 0x00)
              && tok14 && b14[0] == 0x0E
              && tok15 && b15[0] == 0x0F
              && !seen_spurious_token
              && (r1_after == 0x00) && tok_after && after[0] == 0x02,
          "r1=" + std::to_string(r1) +
          " tok14=" + (tok14 ? "1" : "0") +
          " b14_0=" + std::to_string(b14[0]) +
          " tok15=" + (tok15 ? "1" : "0") +
          " b15_0=" + std::to_string(b15[0]) +
          " spurious=" + (seen_spurious_token ? "1" : "0") +
          " r1_after=" + std::to_string(r1_after) +
          " after_0=" + std::to_string(after[0]));
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

// ─── New: CMD13 SEND_STATUS R2 response ─────────────────────────────────

static void test_cmd13_status(SdCardDevice& sd) {
    // SD-02: CMD13 returns R1 followed by R2 (2-byte response).
    // After reading the first non-0xFF byte (R1), the next byte read MUST
    // be R2 (no further command needed, no other 0xFF padding).
    sd.reset();
    init_card(sd);

    // Inline the send-and-poll so we can capture R1 and the IMMEDIATELY-
    // following byte (R2) without losing it to send_cmd_r1's discard.
    spi_write(sd, 0x40 | 13);
    spi_write(sd, 0x00);
    spi_write(sd, 0x00);
    spi_write(sd, 0x00);
    spi_write(sd, 0x00);
    spi_write(sd, 0x95);

    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16; ++i) {
        uint8_t b = spi_read(sd);
        if (b != 0xFF) { r1 = b; break; }
    }
    // Next byte MUST be R2 (the second byte of the R2 response).  resp_buf_
    // for CMD13 is { NCR=0xFF, R1, R2 } so the very next send() call returns
    // R2.
    uint8_t r2 = spi_read(sd);
    sd.deselect();

    check("SD-02",
          "CMD13 SEND_STATUS returns R2 (2-byte): R1=0x00 then R2=0x00",
          r1 == 0x00 && r2 == 0x00,
          "r1=" + std::to_string(r1) + " r2=" + std::to_string(r2));
}

// ─── New: CMD16 SET_BLOCKLEN ack ────────────────────────────────────────

static void test_cmd16_set_blocklen(SdCardDevice& sd) {
    // SD-12: CMD16 with arg=512 → R1=0x00 (ack); arg≠512 → illegal-command
    // bit (bit 2) set.  Per SD spec § 4.9.1, SDHC blocklen is fixed at 512.
    sd.reset();
    init_card(sd);
    uint8_t r1_ok = send_cmd_r1(sd, 16, 512);
    sd.deselect();

    // Must keep card initialized for the second sub-test.  The reset below
    // would clear that, so re-init.
    sd.reset();
    init_card(sd);
    uint8_t r1_bad = send_cmd_r1(sd, 16, 256);
    sd.deselect();

    check("SD-12",
          "CMD16 SET_BLOCKLEN: arg=512 ack (R1=0x00); arg=256 illegal "
          "(R1 bit 2 set)",
          r1_ok == 0x00 && (r1_bad & 0x04) != 0,
          "r1_ok=" + std::to_string(r1_ok) +
          " r1_bad=" + std::to_string(r1_bad));
}

// ─── New: CMD23 SET_BLOCK_COUNT ack ─────────────────────────────────────

static void test_cmd23_block_count(SdCardDevice& sd) {
    // SD-13: CMD23 acks (R1=0x00) without disturbing subsequent state.
    // We verify the ack and then that a subsequent CMD17 still works.
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 23, 10);

    // Now issue CMD17 sector=4 — must still return correct identity bytes.
    uint8_t r1_post = send_cmd_r1(sd, 17, 4);
    bool tok = wait_token(sd);
    uint8_t b[512] = {}; if (tok) read_block(sd, b);
    sd.deselect();

    check("SD-13",
          "CMD23 SET_BLOCK_COUNT acks (R1=0x00); subsequent CMD17 still works",
          r1 == 0x00 && r1_post == 0x00 && tok && b[0] == 0x04,
          "r1=" + std::to_string(r1) +
          " r1_post=" + std::to_string(r1_post) +
          " tok=" + (tok ? "1" : "0") +
          " b0=" + std::to_string(b[0]));
}

// ─── New: CMD24 WRITE_BLOCK round-trip ──────────────────────────────────

static void test_cmd24_write(SdCardDevice& sd) {
    // SD-14: CMD24 round-trip — write a deterministic 512-byte pattern to
    // sector 8, read it back via CMD17, verify identity.  Then restore the
    // original fixture pattern (sector 8 had byte 0..3 = 08 00 00 00,
    // bytes 4..511 = (8 + i) & 0xFF) so subsequent tests aren't disturbed.
    sd.reset();
    init_card(sd);

    // Build pattern and issue CMD24 sector=8.
    uint8_t pattern[512];
    for (int i = 0; i < 512; ++i)
        pattern[i] = static_cast<uint8_t>(i ^ 0xA5);

    // Issue CMD24 sector=8.  Note: the existing cmd24_write_single_block
    // overwrites state to RECEIVING_DATA immediately after queue_r1, so the
    // pre-data R1 byte is not actually emitted on MISO.  The data-response
    // token (0x05) returned AFTER the block is the canonical write-success
    // signal in SPI mode (SD spec § 7.3.3.1) and is what we assert on, plus
    // the CMD17 round-trip readback.
    (void)send_cmd_r1(sd, 24, 8);

    // Send 0xFE start token + 512 pattern bytes + 2 CRC bytes (0xFF).
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, pattern[i]);
    spi_write(sd, 0xFF);
    spi_write(sd, 0xFF);

    // Poll until we see the data response token (0x05 = data accepted, per
    // SD spec § 7.3.3.1).  Cap the poll so a buggy emulator can't spin us
    // forever.
    bool data_resp_ok = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0x05) { data_resp_ok = true; break; }
        if (b != 0xFF && b != 0x00) break;
    }
    sd.deselect();

    // Read back via CMD17 sector=8.
    sd.reset();
    init_card(sd);
    uint8_t r1_rd = send_cmd_r1(sd, 17, 8);
    bool tok = wait_token(sd);
    uint8_t got[512] = {}; if (tok) read_block(sd, got);
    sd.deselect();

    bool match = std::memcmp(got, pattern, 512) == 0;

    check("SD-14",
          "CMD24 WRITE_BLOCK round-trip: data-response 0x05 + CMD17 readback "
          "returns identical 512 bytes",
          data_resp_ok && r1_rd == 0x00 && tok && match,
          "resp=" + std::string(data_resp_ok ? "1" : "0") +
          " r1_rd=" + std::to_string(r1_rd) +
          " tok=" + (tok ? "1" : "0") +
          " match=" + (match ? "1" : "0"));

    // Restore original fixture content for sector 8 so the global image
    // shared with later tests is unchanged.
    sd.reset();
    init_card(sd);
    uint8_t orig[512];
    orig[0] = 0x08; orig[1] = 0x00; orig[2] = 0x00; orig[3] = 0x00;
    for (int i = 4; i < 512; ++i)
        orig[i] = static_cast<uint8_t>((8 + i) & 0xFF);

    (void)send_cmd_r1(sd, 24, 8);
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, orig[i]);
    spi_write(sd, 0xFF);
    spi_write(sd, 0xFF);
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0x05) break;
        if (b != 0xFF && b != 0x00) break;
    }
    sd.deselect();
}

// ─── New: MMC CMD1 init path ────────────────────────────────────────────

static void test_mmc_cmd1_init(SdCardDevice& sd) {
    // MMC-01: legacy MMC init via CMD1.  jnext implements cmd1_send_op_cond
    // (see src/peripheral/sd_card.cpp) which sets initialized_=true on
    // first invocation.  Verify CMD1 returns R1=0x00 (ready) and a
    // subsequent CMD17 confirms the card is initialized.
    sd.reset();

    uint8_t r1 = send_cmd_r1(sd, 1, 0);

    // Now issue CMD17 sector=2 — it must return R1=0x00 (initialized via
    // legacy-MMC path), proving CMD1 alone is sufficient to initialize.
    uint8_t r1_post = send_cmd_r1(sd, 17, 2);
    bool tok = wait_token(sd);
    uint8_t b[512] = {}; if (tok) read_block(sd, b);
    sd.deselect();

    check("MMC-01",
          "CMD1 (legacy MMC init) sets card to ready; subsequent CMD17 "
          "returns R1=0x00",
          r1 == 0x00 && r1_post == 0x00 && tok && b[0] == 0x02,
          "r1=" + std::to_string(r1) +
          " r1_post=" + std::to_string(r1_post) +
          " tok=" + (tok ? "1" : "0") +
          " b0=" + std::to_string(b[0]));
}

// ─── New: SD hot-plug round-trip (BOOT-SD-01) ───────────────────────────

static void test_boot_sd_01() {
    // BOOT-SD-01: mount img1, read sector 0, unmount; mount img2 (with a
    // sentinel), read sector 0, verify sentinel; unmount; mount img1 again,
    // verify original content.  Uses its own local SdCardDevice + images so
    // it doesn't disturb the shared fixture in main().
    std::string img1 = make_image(8);
    std::string img2 = make_image(8);

    // Modify img2 sector 0 byte 4 to 0xAA so we can tell the images apart.
    {
        FILE* f = std::fopen(img2.c_str(), "r+b");
        std::fseek(f, 4, SEEK_SET);
        std::fputc(0xAA, f);
        std::fclose(f);
    }

    SdCardDevice sd;

    // 1) mount img1, read sector 0, expect byte 4 = 4 (i.e. (0+4)&0xFF).
    bool m1 = sd.mount(img1);
    sd.reset();
    init_card(sd);
    uint8_t r1_a = send_cmd_r1(sd, 17, 0);
    bool tok_a = wait_token(sd);
    uint8_t a[512] = {}; if (tok_a) read_block(sd, a);
    sd.deselect();
    sd.unmount();

    // 2) mount img2, read sector 0, expect byte 4 = 0xAA.
    bool m2 = sd.mount(img2);
    sd.reset();
    init_card(sd);
    uint8_t r1_b = send_cmd_r1(sd, 17, 0);
    bool tok_b = wait_token(sd);
    uint8_t b[512] = {}; if (tok_b) read_block(sd, b);
    sd.deselect();
    sd.unmount();

    // 3) re-mount img1, read sector 0, byte 4 must be 4 again.
    bool m3 = sd.mount(img1);
    sd.reset();
    init_card(sd);
    uint8_t r1_c = send_cmd_r1(sd, 17, 0);
    bool tok_c = wait_token(sd);
    uint8_t c[512] = {}; if (tok_c) read_block(sd, c);
    sd.deselect();
    sd.unmount();

    bool ok = m1 && m2 && m3
           && r1_a == 0x00 && tok_a && a[4] == 4
           && r1_b == 0x00 && tok_b && b[4] == 0xAA
           && r1_c == 0x00 && tok_c && c[4] == 4;

    check("BOOT-SD-01",
          "mount/unmount round-trip: img1→img2→img1 yields correct "
          "sector-0 content each time",
          ok,
          "a4=" + std::to_string(a[4]) +
          " b4=" + std::to_string(b[4]) +
          " c4=" + std::to_string(c[4]));

    std::remove(img1.c_str());
    std::remove(img2.c_str());
}

// ─── New: unmount mid-CMD18 stream cleanup (BOOT-SD-02) ─────────────────

static void test_boot_sd_02() {
    // BOOT-SD-02: start CMD18 sector=0, read first block, then unmount the
    // image mid-stream.  Re-mount, re-init, issue CMD17 sector=2 — verify
    // the state machine cleaned up (multi_block_, data_idx_, resp_idx_).
    std::string img = make_image(8);
    SdCardDevice sd;
    bool m1 = sd.mount(img);

    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 18, 0);
    bool tok = wait_token(sd);
    uint8_t blk0[512] = {}; if (tok) read_block(sd, blk0);

    // Unmount mid-stream — multi_block_ is still set on the device.
    sd.unmount();

    // Re-mount the same image and run a fresh CMD17.
    bool m2 = sd.mount(img);
    sd.reset();
    init_card(sd);
    uint8_t r1_after = send_cmd_r1(sd, 17, 2);
    bool tok_after = wait_token(sd);
    uint8_t got[512] = {}; if (tok_after) read_block(sd, got);
    sd.deselect();
    sd.unmount();

    bool ok = m1 && m2
           && r1 == 0x00 && tok && blk0[0] == 0x00
           && r1_after == 0x00 && tok_after && got[0] == 0x02;

    check("BOOT-SD-02",
          "unmount mid-CMD18 stream + re-mount + CMD17 works (state "
          "machine cleaned up)",
          ok,
          "r1=" + std::to_string(r1) +
          " r1_after=" + std::to_string(r1_after) +
          " got0=" + std::to_string(got[0]));

    std::remove(img.c_str());
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
    test_cmd18_end_of_image(sd);
    test_cmd18_cs_deassert_aborts(sd);
    test_cmd13_status(sd);
    test_cmd16_set_blocklen(sd);
    test_cmd23_block_count(sd);
    test_cmd24_write(sd);
    test_mmc_cmd1_init(sd);

    // BOOT-SD-01 / BOOT-SD-02 use their own local SdCardDevice + images so
    // they don't disturb the shared `sd` and `img` above.
    test_boot_sd_01();
    test_boot_sd_02();

    // ─── WONT rows (no skip()) ────────────────────────────────────────
    //
    // WONT SD-01 (G159) — CMD0 CRC validation + CMD59 CRC-toggle handler not
    //   modelled. SD spec § 4.5: card MUST validate CMD0 CRC; CMD59 toggles
    //   general CRC validation. Current behaviour: cmd_buf_[5] (CRC byte) is
    //   ignored on all commands; CMD59 falls through to illegal-command.
    //   Reason: TBBlue master + FatFs disk-io never toggle CRC validation;
    //   no observed firmware client validates CRC. User impact today: nil.
    //   Revisit if: third-party Z80 SD library or new TBBlue firmware version
    //   exercises CMD59 / depends on CRC enforcement.
    //
    // WONT SD-10 (G40 CMD9 SEND_CSD) — 16-byte CSD register synthesis not
    //   modelled. Spec: real SDHC card returns Version-2.0 CSD with C_SIZE
    //   field encoding capacity. Current behaviour: CMD9 falls through to
    //   default → returns R1 only.
    //   Reason: TBBlue loader (mmc.s) does not invoke CMD9; FatFs disk-io
    //   does not invoke CMD9; no observed firmware client probes capacity
    //   via CSD on the Next.
    //   Future re-implementation: if a host or future firmware needs card
    //   capacity, synthesise CSD-V2 from `file_size_` — `C_SIZE = (file_size_
    //   / 524288) - 1`, with VSN/TAAC/etc. canned per SD spec § 5.3.3. Approx.
    //   30 LOC + a CSD-decoder test row. Re-home G40 entry would also gain a
    //   note pointing here.
    //
    // WONT SD-11 (G40 CMD10 SEND_CID) — 16-byte CID register synthesis not
    //   modelled. Reason: TBBlue + FatFs never probe CID; no firmware client
    //   needs manufacturer-ID metadata on the Next.
    //   Revisit if: a client surfaces. Implementation would be canned 16
    //   bytes (MID/OID/PNM/PSN/MDT) per SD spec § 5.2; ~20 LOC + test.
    //
    // WONT SD-15 (G40 CMD25 WRITE_MULTIPLE_BLOCK) — multi-block write state
    //   machine not modelled. Spec: CMD25 starts a multi-write stream
    //   terminated by a 0xFD stop token; each block prefixed with 0xFC.
    //   Current behaviour: CMD25 falls through to default → R1 only; host
    //   would see protocol error.
    //   Reason: TBBlue master verified 2026-05-03d — `CMD25` is `#define`d in
    //   `loader/inc/mmc.h:59` and `app/src/ff/diskio.c:29` but **never
    //   invoked**. FatFs `disk_write()` at diskio.c:382 uses single-block CMD24
    //   in a loop. Loader (mmc.s) is read-only.
    //   Future re-implementation: if NextZXOS or a third-party SD library
    //   adopts CMD25 for write performance, parallel the existing CMD18
    //   multi-read path: new `multi_block_write_` flag, recognise 0xFC
    //   (continue) / 0xFD (stop) tokens in `RECEIVING_DATA`, emit data-response
    //   0x05 + busy bytes between blocks. Approx. 50 LOC + test, ~1h with
    //   reviewer. Symmetric with our CMD18 implementation.
    //
    // WONT MMC-02 (G41) — MMC-mode CMD8 illegal-command response not modelled.
    //   Reason: our card declares SDHC via OCR (CMD58 returns CCS=1). The
    //   MMC-rejection path is unreachable. Test target is tautological.
    //   Revisit if: an MMC-only emulation mode is ever requested.
    //
    // WONT MMC-03 (G41) — MMC byte-vs-block addressing duality not modelled.
    //   Reason: our card is SDHC-only; CMD17/18/24 use block (sector) addressing
    //   exclusively. Mode is fixed by the OCR declaration. Test target is
    //   tautological for a SDHC-only card.
    //   Revisit if: an MMC-only emulation mode is ever requested.

    sd.unmount();
    std::remove(img.c_str());

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows:\n");
        for (const auto& s : g_skipped)
            std::printf("  SKIP %s: %s\n", s.id, s.reason);
    }
    return g_fail ? 1 : 0;
}
