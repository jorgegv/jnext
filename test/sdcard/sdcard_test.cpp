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
//   CMD18-04            — CS deassert mid-block + reset → clean CMD17 after
//   CMD18-05            — an OPEN CMD18 stream SURVIVES CS deassert/reselect
//                         (SPI-mode cards pause on CS high; only CMD12 or a
//                         new command ends the stream. NextZXOS's esxDOS
//                         driver relies on this across driver calls —
//                         the 2026-07-10 NextZXOS-boot fix).
//   SD-NAC-01..05       — ≥1 idle (0xFF) Nac gap byte between R1 and the
//                         0xFE data token on CMD17/CMD18 first block (SD
//                         Physical Layer Simplified Spec § 7.5.2). GH #84:
//                         zero-gap token emission shifted Atic Atac Next's
//                         whole CMD18 stream one sector (its command sender
//                         clocks one flush byte after R1, which swallowed
//                         the token). SD-NAC-03 reproduces that exact host
//                         idiom and asserts the FIRST requested sector is
//                         still delivered.
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
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>   // write, close, mkstemp
#include <sys/stat.h> // chmod (V15-DIVMMC-01: SD-28 RO image test)
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
    // CMD18-04: CS deassert mid-stream no longer kills the card state
    // outright (see CMD18-05), but a full reset + re-init afterwards must
    // still leave a clean card: a fresh CMD17 works.
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

static void test_nac_gap(SdCardDevice& sd) {
    // SD-NAC-01: after CMD17's R1, the very next byte on the bus must be
    // an idle 0xFF (Nac read-access gap, SD Physical Layer Simplified
    // Spec § 7.5.2 — a real card ALWAYS needs ≥1 byte of access time
    // before the start-of-block token), and the 0xFE token must follow.
    // Pre-GH#84-fix the byte right after R1 was the 0xFE token itself.
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 17, 1);
    uint8_t gap = spi_read(sd);          // byte immediately after R1
    bool tok = wait_token(sd);           // token must still arrive after gap
    uint8_t buf[512] = {};
    if (tok) read_block(sd, buf);
    sd.deselect();
    check("SD-NAC-01",
          "CMD17: >=1 idle (0xFF) Nac gap byte between R1 and 0xFE token",
          (r1 == 0x00) && gap == 0xFF && tok && buf[0] == 0x01,
          "r1=" + std::to_string(r1) + " gap=" + std::to_string(gap) +
          " tok=" + (tok ? "1" : "0") + " b0=" + std::to_string(buf[0]));

    // SD-NAC-02: same contract on CMD18's FIRST block (the between-blocks
    // path already emitted a 0xFF filler before each subsequent token;
    // the first block was the one missing it).
    sd.reset();
    init_card(sd);
    r1 = send_cmd_r1(sd, 18, 3);
    gap = spi_read(sd);
    tok = wait_token(sd);
    uint8_t b1[512] = {};
    if (tok) read_block(sd, b1);
    (void)send_cmd_r1(sd, 12, 0);        // stop stream
    sd.deselect();
    check("SD-NAC-02",
          "CMD18 first block: >=1 idle (0xFF) Nac gap byte before 0xFE token",
          (r1 == 0x00) && gap == 0xFF && tok && b1[0] == 0x03,
          "r1=" + std::to_string(r1) + " gap=" + std::to_string(gap) +
          " tok=" + (tok ? "1" : "0") + " b0=" + std::to_string(b1[0]));

    // SD-NAC-03: the GH #84 host idiom. Atic Atac Next's command sender
    // polls for R1 and then clocks EXACTLY ONE extra byte before
    // returning; its data reader then token-polls from scratch. On a
    // real card the extra clock eats a Nac 0xFF; with the pre-fix
    // zero-gap emission it ate the 0xFE token, the token-poll consumed
    // all of block 0 as non-token bytes and synced on block 1's token —
    // shifting the whole stream one sector (RAM[k] = file[k+1]).
    // Assert the first delivered block is STILL the requested sector.
    sd.reset();
    init_card(sd);
    r1 = send_cmd_r1(sd, 18, 3);
    (void)spi_read(sd);                  // the game's one-byte flush after R1
    tok = wait_token(sd);
    uint8_t f1[512] = {};
    if (tok) read_block(sd, f1);
    (void)send_cmd_r1(sd, 12, 0);
    sd.deselect();
    check("SD-NAC-03",
          "CMD18 with one host flush byte after R1 still delivers the FIRST requested sector",
          (r1 == 0x00) && tok && f1[0] == 0x03,
          "r1=" + std::to_string(r1) + " tok=" + (tok ? "1" : "0") +
          " b0=" + std::to_string(f1[0]) + " (b0==4 => one-sector shift)");

    // SD-NAC-04: same § 7.5.2 contract on CMD9 SEND_CSD (GH #98 — the
    // #84 fix reviewer spotted that the CSD/CID block reads still emitted
    // R1 and 0xFE back-to-back). Byte after R1 must be an idle 0xFF, the
    // token must still follow, and the payload must still be the CSD
    // (CSD_STRUCTURE=01 -> first byte 0x40, SD spec § 5.3.3).
    sd.reset();
    init_card(sd);
    r1 = send_cmd_r1(sd, 9, 0);
    gap = spi_read(sd);
    tok = wait_token(sd);
    uint8_t csd0 = tok ? spi_read(sd) : 0;
    for (int i = 0; i < 15 + 2 && tok; ++i) (void)spi_read(sd);  // rest + CRC
    sd.deselect();
    check("SD-NAC-04",
          "CMD9 SEND_CSD: >=1 idle (0xFF) Nac gap byte between R1 and 0xFE token",
          (r1 == 0x00) && gap == 0xFF && tok && csd0 == 0x40,
          "r1=" + std::to_string(r1) + " gap=" + std::to_string(gap) +
          " tok=" + (tok ? "1" : "0") + " csd0=" + std::to_string(csd0));

    // SD-NAC-05: same contract on CMD10 SEND_CID. Payload check: CID[0] is
    // the Manufacturer ID 0x03 (SD spec § 5.2).
    sd.reset();
    init_card(sd);
    r1 = send_cmd_r1(sd, 10, 0);
    gap = spi_read(sd);
    tok = wait_token(sd);
    uint8_t cid0 = tok ? spi_read(sd) : 0;
    for (int i = 0; i < 15 + 2 && tok; ++i) (void)spi_read(sd);  // rest + CRC
    sd.deselect();
    check("SD-NAC-05",
          "CMD10 SEND_CID: >=1 idle (0xFF) Nac gap byte between R1 and 0xFE token",
          (r1 == 0x00) && gap == 0xFF && tok && cid0 == 0x03,
          "r1=" + std::to_string(r1) + " gap=" + std::to_string(gap) +
          " tok=" + (tok ? "1" : "0") + " cid0=" + std::to_string(cid0));
}

static void test_cmd18_stream_survives_deselect(SdCardDevice& sd) {
    // CMD18-05 (NextZXOS-boot fix, 2026-07-10): an open CMD18 stream is
    // PAUSED, not aborted, by CS deassert. NextZXOS's esxDOS driver keeps
    // one CMD18 stream open across driver calls: it reads block N,
    // deselects, and later reselects + token-polls for block N+1 WITHOUT
    // sending any command. Pre-fix jnext aborted the stream in deselect(),
    // so the resumed poll saw endless $FF → esxDOS timeout (error 2) → the
    // native NextZXOS boot died at a RET-to-$0000 sentinel trap.
    sd.reset();
    init_card(sd);
    (void)send_cmd_r1(sd, 18, 2);
    (void)wait_token(sd);
    uint8_t b2[512] = {}; read_block(sd, b2);      // consume block @2 + CRC

    sd.deselect();                                  // driver releases CS

    // Reselect: continue the SAME stream — no command. The card must
    // deliver the next block (sector 3) after idle filler + 0xFE token.
    bool tok = wait_token(sd);
    uint8_t b3[512] = {}; if (tok) read_block(sd, b3);

    // CMD12 closes the stream properly.
    (void)send_cmd_r1(sd, 12, 0);
    sd.deselect();

    check("CMD18-05",
          "open CMD18 stream survives CS deassert; next block streams on "
          "reselect without a command (esxDOS cross-call streaming)",
          (b2[0] == 0x02) && tok && (b3[0] == 0x03),
          std::string("b2_0=") + std::to_string(b2[0]) +
          " tok=" + (tok ? "1" : "0") +
          " b3_0=" + std::to_string(b3[0]));
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

// ─── Read-only mount (GH #77, --sdcard-readonly) ────────────────────────

static void test_readonly_mount() {
    // SD-RO-01/02: `mount(path, read_only=true)` opens the host file
    // read-only ON PURPOSE, so the emulated machine sees a write-protected
    // card. This is the deliberate form of the accidental fallback that
    // mount() has always had when the file is not writable, and it uses the
    // SAME already-implemented path: the host fstream write fails, and CMD24
    // emits the 0x0D "data rejected due to write error" token per SD Physical
    // Layer Simplified Spec § 7.3.3.3 instead of 0x05 "data accepted".
    //
    // Two properties, and both matter — a card that merely discarded the
    // write while answering 0x05 would satisfy the second and lie about the
    // first:
    //   SD-RO-01  the card REPORTS the write error (0x0D, not 0x05);
    //   SD-RO-02  the host file is byte-identical afterwards.
    std::string img = make_image(8);

    // Capture the original sector-8-adjacent content (sector 4 is used here
    // so this row cannot be confused with SD-14's sector 8).
    auto read_sector4 = [&img]() {
        std::ifstream f(img, std::ios::binary);
        f.seekg(4 * 512, std::ios::beg);
        std::string b(512, '\0');
        f.read(&b[0], 512);
        return b;
    };
    const std::string before = read_sector4();

    SdCardDevice ro;
    const bool mounted = ro.mount(img, /*read_only=*/true);
    init_card(ro);

    // CMD24 sector=4 with a pattern that cannot occur in the fixture
    // (make_image fills byte i with (s + i) & 0xFF; 0x5A everywhere is not
    // that for any s), so a silent partial write would still be detected.
    uint8_t r1_wr = send_cmd_r1(ro, 24, 4);
    spi_write(ro, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(ro, 0x5A);
    spi_write(ro, 0xFF);
    spi_write(ro, 0xFF);

    uint8_t resp = 0xFF;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(ro);
        if (b != 0xFF) { resp = b; break; }
    }
    ro.deselect();
    ro.unmount();

    const std::string after = read_sector4();
    std::remove(img.c_str());

    check("SD-RO-01",
          "read-only mount: CMD24 is REJECTED with the 0x0D write-error token "
          "(SD spec 7.3.3.3), not accepted with 0x05",
          mounted && r1_wr == 0x00 && resp == 0x0D,
          "mounted=" + std::string(mounted ? "1" : "0") +
          " r1_wr=" + std::to_string(r1_wr) +
          " resp=0x" + [&]{ char b[8]; std::snprintf(b, sizeof b, "%02X", resp); return std::string(b); }());

    check("SD-RO-02",
          "read-only mount: the host image is byte-identical after a rejected "
          "CMD24 — the write is not silently applied",
          before == after && !before.empty(),
          "identical=" + std::string(before == after ? "1" : "0") +
          " len=" + std::to_string(before.size()));
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

    // Issue CMD24 sector=8.  After the cmd24-r1-emission fix, R1 is
    // emitted on MISO before the data phase (SD spec § 7.2.4 / 7.3.3.1):
    // send_cmd_r1() polls past the NCR 0xFF and returns the actual R1
    // byte, which must be 0x00 for an initialized card.
    uint8_t r1_wr = send_cmd_r1(sd, 24, 8);

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
          "CMD24 WRITE_BLOCK round-trip: R1=0x00 + data-response 0x05 + "
          "CMD17 readback returns identical 512 bytes",
          r1_wr == 0x00 && data_resp_ok && r1_rd == 0x00 && tok && match,
          "r1_wr=" + std::to_string(r1_wr) +
          " resp=" + std::string(data_resp_ok ? "1" : "0") +
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

// ─── New: SD-15 / mount() does full reset (TASK2-VERIFY5) ───────────────

static void test_sd_15_mount_full_reset() {
    // SD-15 (TASK2-VERIFY5 commit 24a1bc4): SdCardDevice::mount() does a
    // full SPI-protocol state reset, identical to reset(). Pre-fix mount()
    // cleared only state_/initialized_/app_cmd_/cmd_idx_, leaving
    // resp_buf_/resp_idx_/data_idx_/data_crc_count_/multi_block_/
    // multi_block_sector_/persistent_response_byte_/data_block_/cmd_buf_
    // untouched. A runtime mount swap (e.g. user changes --sdcard while
    // a CMD17 SENDING_DATA is mid-transfer) leaves these fields stale.
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-
    // REVIEW.md §3 / §4.1): the original SD-15 design called init_card()
    // between mount(img2) and CMD17. The init_card sequence inevitably
    // hits receive()'s default-state abort branch (sd_card.cpp:201-218 —
    // "new CMD start byte while state_!=IDLE") because CMD8's resp_buf_
    // is left partially-drained after send_cmd_r1's "stop on first non-FF"
    // poll. That abort branch DOES clear multi_block_/multi_block_sector_/
    // pending_write_after_r1_, masking the leak — and CMD17 then re-reads
    // data_block_ from the new file. Net: original test passed both pre-
    // fix and post-fix.
    //
    // Discriminative redesign: the cleanest leaked field that survives
    // the IDLE-state path through receive()/send() is
    // persistent_response_byte_. Post-CMD0 on img1, the field is set to
    // 0x01 (ZEsarUX-style sustained response per cmd0_go_idle at line
    // 410). After mount(img2), state_=IDLE in both pre/post fix; a bare
    // send() in IDLE state returns persistent_response_byte_ verbatim
    // (sd_card.cpp:236-243). No abort branch fires, no command is issued,
    // no write touches data_block_. The IDLE-branch send() is the
    // shortest path that still surfaces the leak.
    //
    //   - Pre-fix: persistent_response_byte_=0x01 (LEAKED across mount).
    //   - Post-fix: mount() calls reset() which sets it to 0xFF.
    //
    // CRITICAL: do NOT call deselect() or reset() between issuing CMD0
    // and the post-mount probe — both would clear persistent_response_
    // byte_=0xFF and mask the leak. The probe is one bare send() call
    // in IDLE state. We also test multi-block leak indirectly by
    // confirming subsequent CMD17 returns the correct img2 content
    // (round-trip integrity preserved across remount).
    std::string img1 = make_image(8);
    std::string img2 = make_image(8);

    // Customise img2 sector 2 byte 4 to 0xCC so pre/post is unambiguous.
    {
        FILE* f = std::fopen(img2.c_str(), "r+b");
        std::fseek(f, 2 * 512 + 4, SEEK_SET);
        std::fputc(0xCC, f);
        std::fclose(f);
    }

    SdCardDevice sd;

    // (1) Mount img1 and issue a single CMD0. cmd0_go_idle() sets
    //     persistent_response_byte_=0x01 (sd_card.cpp:431). state_ ends
    //     in IDLE (RESPONDING drains eagerly when resp_buf_ is fully
    //     read by send_cmd_r1).
    bool m1 = sd.mount(img1);
    sd.reset();          // start from a known clean state
    uint8_t r1_cmd0 = send_cmd_r1(sd, 0, 0);  // R1=0x01 expected

    // (2) Critical: mount(img2) WITHOUT deselect()/reset() between.
    //     Pre-fix: state_=IDLE, but persistent_response_byte_=0x01
    //     leaks through. Post-fix: full reset() restores it to 0xFF.
    bool m2 = sd.mount(img2);

    // (3) Probe: a single bare send() in state_=IDLE returns
    //     persistent_response_byte_ verbatim (sd_card.cpp:243). This
    //     bypasses the abort branch entirely and is the most direct
    //     observation of the leaked field.
    //
    //     Pre-fix: 0x01.  Post-fix: 0xFF.
    uint8_t leaked_persistent = spi_read(sd);

    // (4) Round-trip integrity check: with proper init on img2, CMD17
    //     sector=2 must return img2's content (byte 4 = 0xCC). This
    //     is the original sanity-check intent of SD-15. Pass-fix and
    //     post-fix should both succeed here, but it pins the broader
    //     contract that mount(img2) leaves the card usable.
    init_card(sd);
    uint8_t r1_17 = send_cmd_r1(sd, 17, 2);
    bool tok17 = wait_token(sd);
    uint8_t got[512] = {}; if (tok17) read_block(sd, got);
    sd.deselect();
    sd.unmount();

    // The PRIMARY discriminator is `leaked_persistent==0xFF`. Pre-fix
    // it equals 0x01 (leaked from CMD0 on img1); post-fix it equals
    // 0xFF (reset by mount()'s reset() call).
    bool ok = m1 && m2
           && r1_cmd0 == 0x01
           && leaked_persistent == 0xFF
           && r1_17 == 0x00 && tok17 && got[4] == 0xCC && got[0] == 0x02;

    check("SD-15",
          "mount() does full reset() — persistent_response_byte_ MUST "
          "NOT leak across a runtime mount swap. Probe: after CMD0 on "
          "img1 (which sets persistent_response_byte_=0x01), mount(img2) "
          "must clear it back to 0xFF. A bare send() in IDLE state then "
          "returns 0xFF (post-fix) instead of the leaked 0x01 (pre-fix). "
          "Round-trip integrity also pinned via subsequent CMD17 on img2. "
          "Pre-fix mount() cleared only state_/initialized_/app_cmd_/"
          "cmd_idx_; post-fix calls reset() canonically (TASK2-VERIFY5 "
          "commit 24a1bc4)",
          ok,
          "r1_cmd0=" + std::to_string(r1_cmd0) +
          " leaked=" + std::to_string(leaked_persistent) +
          " r1_17=" + std::to_string(r1_17) +
          " tok17=" + (tok17 ? "1" : "0") +
          " got4=" + std::to_string(got[4]) +
          " got0=" + std::to_string(got[0]));

    std::remove(img1.c_str());
    std::remove(img2.c_str());
}

// ─── New: SD-16 / cmd16 idle-bit reflects initialized_ (TASK2-VERIFY5) ──

static void test_sd_16_cmd16_idle_bit(SdCardDevice& sd) {
    // SD-16 (TASK2-VERIFY5 commit 24a1bc4): cmd16_set_blocklen(arg≠512)
    // R1 idle bit (bit 0) must reflect initialized_ — NOT be hard-coded
    // to 1. SD spec § 4.9.1 R1 layout: bit 0 = "in idle state" (the card
    // is in initialization), bit 2 = "illegal command". Pre-fix returned
    // 0x05 (idle + illegal) unconditionally, mis-reporting a post-init
    // (ACMD41-completed) card as still in idle.
    //
    // Discriminative shape (complements SD-12 above which only checks
    // bit 2): with the card INITIALIZED via init_card(), CMD16 arg=256
    // must return R1=0x04 (illegal-only, idle CLEAR). With the card
    // RESET (uninit), arg=256 returns R1=0x05 (idle + illegal). Both
    // reflect the same cmd16_set_blocklen() branch but pin both legs
    // of the idle-bit derivation.
    sd.reset();
    init_card(sd);
    uint8_t r1_init = send_cmd_r1(sd, 16, 256);   // bad blocklen, init
    sd.deselect();

    sd.reset();   // back to uninit (CMD0 not issued -> initialized_=false)
    uint8_t r1_uninit = send_cmd_r1(sd, 16, 256); // bad blocklen, uninit
    sd.deselect();

    check("SD-16",
          "CMD16 SET_BLOCKLEN arg≠512 R1: initialized card -> 0x04 "
          "(illegal only, idle CLEAR); uninitialized card -> 0x05 "
          "(idle + illegal). Idle bit must derive from initialized_, "
          "not be hard-coded (SD spec § 4.9.1)",
          r1_init == 0x04 && r1_uninit == 0x05,
          "init=" + std::to_string(r1_init) +
          " uninit=" + std::to_string(r1_uninit));
}

// ─── New: SD-17 / CMD24 0xFF gap-byte tolerance (TASK2-VERIFY4) ─────────

static void test_sd_17_cmd24_gap_bytes(SdCardDevice& sd) {
    // SD-17 (TASK2-VERIFY4 commit c7acf9e): CMD24 RECEIVING_DATA must
    // tolerate spec-mandated 0xFF gap bytes between R1 and the 0xFE
    // start-of-data token. SD Physical Layer Simplified Spec 6.00
    // § 7.3.3.2: "Following the command response (R1) and one (or
    // more) bytes of $FF (host SPI clock), the data must be sent
    // following a Data Token byte ($FE for single block)." The card
    // MUST tolerate any number of 0xFF gap bytes before the data token.
    // Pre-fix code absorbed each 0xFF into data_block_, shifting the
    // 512-byte payload and corrupting the write. esxdos / generic
    // FatFs DO send gap bytes per spec; tbblue's xmit_datablock writes
    // the token immediately after R1 and missed the bug.
    //
    // Test shape: build a deterministic 512-byte payload, write it via
    // CMD24 with N=4 leading 0xFF gap bytes after R1 (before the 0xFE
    // token), read back via CMD17, verify identity. Pre-fix the first 4
    // bytes of the readback would be shifted (bytes 0..3 of the payload
    // would be lost, replaced by 0xFF); post-fix the readback matches
    // the payload byte-for-byte.
    sd.reset();
    init_card(sd);

    uint8_t pattern[512];
    for (int i = 0; i < 512; ++i)
        pattern[i] = static_cast<uint8_t>((i * 7 + 0x33) & 0xFF);

    // Use sector=10 to avoid clobbering fixtures that other tests rely on.
    uint8_t r1_wr = send_cmd_r1(sd, 24, 10);

    // The fix: emit GAP bytes (0xFF) BEFORE the 0xFE token. Per spec
    // these are clock-only fillers; pre-fix they leaked into data_block_.
    constexpr int N_GAP = 4;
    for (int i = 0; i < N_GAP; ++i) spi_write(sd, 0xFF);

    spi_write(sd, 0xFE);   // start-of-data token
    for (int i = 0; i < 512; ++i) spi_write(sd, pattern[i]);
    spi_write(sd, 0xFF);   // CRC hi
    spi_write(sd, 0xFF);   // CRC lo

    bool data_resp_ok = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0x05) { data_resp_ok = true; break; }
        if (b != 0xFF && b != 0x00) break;
    }
    sd.deselect();

    // Read back via CMD17.
    sd.reset();
    init_card(sd);
    uint8_t r1_rd = send_cmd_r1(sd, 17, 10);
    bool tok = wait_token(sd);
    uint8_t got[512] = {};
    if (tok) read_block(sd, got);
    sd.deselect();

    bool match = std::memcmp(got, pattern, 512) == 0;

    check("SD-17",
          "CMD24 tolerates leading 0xFF gap bytes between R1 and the "
          "0xFE start-of-data token; readback equals payload byte-for-"
          "byte (SD Phys Layer Spec 6.00 § 7.3.3.2)",
          r1_wr == 0x00 && data_resp_ok && r1_rd == 0x00 && tok && match,
          "r1_wr=" + std::to_string(r1_wr) +
          " resp=" + std::string(data_resp_ok ? "1" : "0") +
          " r1_rd=" + std::to_string(r1_rd) +
          " tok=" + (tok ? "1" : "0") +
          " match=" + (match ? "1" : "0") +
          " got0=" + std::to_string(got[0]) +
          " exp0=" + std::to_string(pattern[0]));

    // Restore fixture content for sector 10 so subsequent tests are
    // unaffected: sector S has byte 0..3 = LE(S) and bytes 4..511 =
    // (S+i) & 0xFF.
    sd.reset();
    init_card(sd);
    uint8_t orig[512];
    orig[0] = 0x0A; orig[1] = 0x00; orig[2] = 0x00; orig[3] = 0x00;
    for (int i = 4; i < 512; ++i)
        orig[i] = static_cast<uint8_t>((10 + i) & 0xFF);
    (void)send_cmd_r1(sd, 24, 10);
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, orig[i]);
    spi_write(sd, 0xFF); spi_write(sd, 0xFF);
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0x05) break;
        if (b != 0xFF && b != 0x00) break;
    }
    sd.deselect();
}

// ─── New: SD-18 / unhandled CMD R1 illegal-cmd bit (TASK2-VERIFY8) ──────

static void test_sd_18_unhandled_cmd(SdCardDevice& sd) {
    // SD-18 (TASK2-VERIFY8 commit 6ebfd2b): the default branch of the
    // CMD switch (unhandled command, e.g. CMD20 / CMD40 / CMD45 /
    // CMD59) must set R1 bit 2 (illegal command) per SD spec § 7.3.2.1.
    // Pre-fix returned only the idle bit (R1=0x00 or 0x01), letting
    // firmware silently ignore unsupported-command failures instead
    // of triggering its illegal-command error path.
    //
    // Use CMD20 — not implemented in jnext (not in the case switch),
    // unambiguously hits the `default:` branch. With the card
    // initialized, R1 bit 0 = 0 (not idle) and bit 2 = 1 (illegal).
    sd.reset();
    init_card(sd);
    uint8_t r1 = send_cmd_r1(sd, 20, 0);
    sd.deselect();

    check("SD-18",
          "Unhandled CMD20 returns R1 with bit 2 (illegal command) set; "
          "bit 0 (idle) clear on initialized card "
          "(SD spec § 7.3.2.1; TASK2-VERIFY8 fix)",
          (r1 & 0x04) != 0 && (r1 & 0x01) == 0,
          "r1=" + std::to_string(r1));
}

// ─── New: SD-19 / unknown ACMD R1 illegal-cmd bit (TASK2-VERIFY8) ──────

static void test_sd_19_unknown_acmd(SdCardDevice& sd) {
    // SD-19 (TASK2-VERIFY8 commit 6ebfd2b): the unknown-ACMD branch
    // (e.g. CMD55+ACMD13/22/23/42/51 are recognised; ACMD7 etc. are
    // not) must set R1 bit 2 (illegal command). The idle bit (bit 0)
    // must reflect initialized_, NOT be hard-coded to 1. Pre-fix
    // hard-coded R1=0x05 (idle + illegal), mis-asserting idle on a
    // post-ACMD41 card.
    //
    // NB: pass-9 commit ff84d3e changed CMD55+non-ACMD41 to FALL THROUGH
    // to the regular CMD switch (per SD spec § 4.3.9.1). So a CMD55
    // followed by a non-ACMD41 command exercises the regular CMD path,
    // not the unknown-ACMD branch. To exercise the unknown-ACMD
    // default we need a CMD that has an ACMD overload that's NOT
    // explicitly recognized in jnext's process_command (CMD41 is the
    // only ACMD jnext implements; everything else falls through). After
    // pass-9 the unknown-ACMD branch is reached only when the regular
    // CMD switch ALSO doesn't match (i.e. the index is not in
    // {0,1,8,12,13,16,17,18,23,24,55,58}). CMD42 fits — no regular
    // entry, no ACMD entry → default ACMD-illegal branch (after
    // pass-9: still hits unknown-ACMD before fall-through, OR reaches
    // the regular default unhandled-CMD branch which ALSO sets bit 2
    // per pass-8). Either way, R1 bit 2 must be set with bit 0 = 0.
    sd.reset();
    init_card(sd);
    uint8_t r1_55 = send_cmd_r1(sd, 55, 0);   // APP_CMD prefix
    uint8_t r1_acmd42 = send_cmd_r1(sd, 42, 0); // unknown ACMD
    sd.deselect();

    check("SD-19",
          "CMD55+ACMD42 (or CMD42 fall-through): R1 bit 2 (illegal cmd) "
          "set; bit 0 (idle) clear on initialized card "
          "(SD spec § 7.3.2.1; TASK2-VERIFY8 fix derives idle from "
          "initialized_)",
          r1_55 == 0x00
              && (r1_acmd42 & 0x04) != 0
              && (r1_acmd42 & 0x01) == 0,
          "r1_55=" + std::to_string(r1_55) +
          " r1_acmd42=" + std::to_string(r1_acmd42));
}

// ─── New: SD-20 / CMD55+non-ACMD falls through (TASK2-VERIFY9) ──────────

static void test_sd_20_cmd55_fallthrough(SdCardDevice& sd) {
    // SD-20 (TASK2-VERIFY9 commit ff84d3e): per SD Physical Layer
    // Simplified Spec § 4.3.9.1 / § 7.3.2.1 / § 4.3.9.5, "If the next
    // command following CMD55 is not an application-specific (ACMD)
    // command, the application-specific flag is cleared and the command
    // is treated as a regular command." Pre-fix returned R1 illegal for
    // any non-ACMD41 sequence, violating spec for the regular CMD bridge
    // case (e.g. CMD55 → CMD17 must return CMD17's R1=0x00 and proceed
    // with a single-block read).
    //
    // Test shape: issue CMD55, then CMD17 sector=2 — expect R1=0x00 and
    // a successful 512-byte read (token + data) matching sector 2's
    // identity bytes.
    sd.reset();
    init_card(sd);

    // Issue CMD55 — recognized; returns R1=0x00 (init), sets app_cmd_=true.
    uint8_t r1_55 = send_cmd_r1(sd, 55, 0);

    // Next command is a REGULAR cmd (CMD17), not an ACMD. Per spec the
    // app-cmd flag must clear and CMD17 runs as normal.
    uint8_t r1_17 = send_cmd_r1(sd, 17, 2);
    bool tok = wait_token(sd);
    uint8_t blk[512] = {}; if (tok) read_block(sd, blk);
    sd.deselect();

    check("SD-20",
          "CMD55 followed by non-ACMD (CMD17) falls through to regular "
          "CMD switch; R1=0x00 + data block matches sector 2 fixture "
          "(SD spec § 4.3.9.1; TASK2-VERIFY9 fix)",
          r1_55 == 0x00 && r1_17 == 0x00 && tok
              && blk[0] == 0x02 && blk[1] == 0x00,
          "r1_55=" + std::to_string(r1_55) +
          " r1_17=" + std::to_string(r1_17) +
          " tok=" + (tok ? "1" : "0") +
          " b0=" + std::to_string(blk[0]));
}

// ─── New: SD-21 / CMD24 past-EOF write-error response (TASK2-VERIFY12) ──

static void test_sd_21_cmd24_past_eof() {
    // SD-21 (V12-DIVMMC-02 + V13-DIVMMC-01 supersede): CMD24 with a sector
    // index past end-of-image must reject IMMEDIATELY at R1 with bit 6
    // (PARAMETER_ERROR) set, and MUST NOT enter the data-write phase.
    //
    // SD Physical Layer Simplified Spec § 7.3.2.1 Table 7-9 — R1 layout:
    //   bit 7 = 0 (start bit)
    //   bit 6 = PARAMETER_ERROR (= "argument was out of the allowed range")
    //   bit 5 = ADDRESS_ERROR
    //   bit 4 = ERASE_SEQUENCE_ERROR
    //   bit 3 = COM_CRC_ERROR
    //   bit 2 = ILLEGAL_COMMAND
    //   bit 1 = ERASE_RESET
    //   bit 0 = IN_IDLE_STATE
    //
    // SD Physical Layer Simplified Spec § 4.3.4 (Data Write Sequence) /
    // § 7.3.3.2: the data phase is conditional on R1=0x00. When the card
    // detects an error at R1, the host MUST NOT proceed to the data phase.
    //
    // Evolution of this row:
    //   * Pre-V12-DIVMMC-02: CMD24 unconditionally queued 0x05 (data accepted)
    //     even when the sector was past end-of-image. R1 was 0x00. Host
    //     believed every past-EOF write succeeded.
    //   * V12-DIVMMC-02 (Pass-12 verify-audit): kept R1=0x00 but flipped the
    //     end-of-data-phase response token from 0x05 to 0x0D (write error).
    //     Spec-permissible BUT the WEAKER variant of two valid behaviours.
    //   * V13-DIVMMC-01 (Pass-13 verify-audit, this fix): align with the
    //     V12-DIVMMC-04 fix that gave CMD17/CMD18 the EARLIER R1-bit-6
    //     rejection. Past-EOF CMD24 now sets R1 bit 6 immediately and the
    //     data phase never starts. Symmetric with CMD17/CMD18 past-EOF.
    //
    // Discriminative shape: build a small 8-sector image (4 KB), CMD24 to
    // sector 100 (= 51200 bytes, well past the 4096-byte boundary).
    // Post-V13: R1=0x40 (bit 6 set). Pre-V13: R1=0x00 (the V12-DIVMMC-02
    // shape — only the data response token reflected the error).
    //
    // Safety: also verify the existing in-bounds CMD24 path still emits
    // R1=0x00 + data-response token 0x05 (regression guard so the V13 fix
    // doesn't flip the sense of in-bounds CMD24).
    const uint32_t n_sectors = 8;
    std::string img = make_image(n_sectors);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);

    // CMD24 sector=100 (past EOF for an 8-sector image).
    // V13-DIVMMC-01: R1 must come back with bit 6 set (PARAMETER_ERROR).
    uint8_t r1_wr = send_cmd_r1(sd, 24, 100);

    // V13-DIVMMC-01: card MUST NOT have entered the data phase. The next
    // bytes the host clocks must be the IDLE 0xFF tail (no data response
    // token). Pre-V13 the card was in RECEIVING_DATA waiting for 0xFE +
    // 512 + CRC; reading would surface 0xFF until the host obediently sent
    // the (wasted) data + CRC, then a 0x0D token. Post-V13 the FSM is back
    // to IDLE the moment R1=0x40 has been emitted on MISO, so reads return
    // 0xFF immediately and stay 0xFF.
    sd.deselect();

    // In-bounds regression guard: CMD24 to sector 0 must still emit
    // R1=0x00 + data-response 0x05.
    sd.reset();
    init_card(sd);
    uint8_t r1_ok = send_cmd_r1(sd, 24, 0);
    spi_write(sd, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd, 0xAA);
    spi_write(sd, 0xFF);
    spi_write(sd, 0xFF);
    uint8_t resp_ok = 0xFF;
    bool got_resp_ok = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b != 0xFF) { resp_ok = b; got_resp_ok = true; break; }
    }
    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && r1_wr == 0x40                                     // V13-DIVMMC-01: R1 bit 6 PARAMETER_ERROR
           && r1_ok == 0x00 && got_resp_ok && resp_ok == 0x05;  // in-bounds regression

    check("SD-21",
          "CMD24 past EOF rejects at R1 with PARAMETER_ERROR (0x40) and "
          "skips the data phase; in-bounds case still returns R1=0x00 + "
          "data-accepted (0x05) "
          "(SD Physical Layer Simplified Spec § 7.3.2.1 Table 7-9 + § 4.3.4)",
          ok,
          "r1_wr=" + std::to_string(r1_wr) +
          " r1_ok=" + std::to_string(r1_ok) +
          " resp_ok=" + std::to_string(resp_ok));

    std::remove(img.c_str());
}

// ─── New: SD-25 / CMD24 past-EOF: data phase fully suppressed (V13-DIVMMC-01) ──
//
// Companion to SD-21 above — discriminative test that the data phase is
// FULLY suppressed after R1=0x40, not merely shortened. SD-21 covers the
// R1 byte itself; SD-25 covers the FSM-state contract: after the R1=0x40
// byte has been emitted the card must be back in IDLE (resp_idx_ exhausted,
// state_=IDLE), not in RECEIVING_DATA waiting for 0xFE + 512 + CRC like
// the pre-fix path.
//
// Pre-V13 the FSM was in RECEIVING_DATA after R1, so even though the
// R1 byte said "0x40 = error", the card would still consume the host's
// (mis-issued) 0xFE + 512 + CRC stream, then emit a 0x0D token. The
// discriminative shape:
//
//   1. Issue CMD24 sector=100 (past-EOF for 8-sector image).
//   2. After draining R1, send another command (CMD13 SEND_STATUS).
//   3. Pre-V13: the card is in RECEIVING_DATA — the CMD13 first byte
//      0x4D would be absorbed as data_block_[0] (no command match),
//      and CMD13 never executes. Reads continue to return 0xFF.
//   4. Post-V13: the card is in IDLE — receive() of 0x4D matches the
//      command-start pattern, RECEIVING_CMD begins, after 5 more bytes
//      process_command() runs CMD13_SEND_STATUS. Reads then return
//      NCR + R1 + status_byte.
static void test_sd_25_cmd24_past_eof_no_data_phase() {
    const uint32_t n_sectors = 8;
    std::string img = make_image(n_sectors);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);

    // CMD24 sector=100 (past EOF). Drain the R1 response.
    uint8_t r1 = send_cmd_r1(sd, 24, 100);

    // Now issue CMD13 (SEND_STATUS) immediately. Post-V13 this works
    // because the FSM is back in IDLE. Pre-V13 the bytes get absorbed
    // as the (mis-issued) data block.
    spi_write(sd, 0x40 | 13);  // command byte
    spi_write(sd, 0x00);       // arg [3]
    spi_write(sd, 0x00);       // arg [2]
    spi_write(sd, 0x00);       // arg [1]
    spi_write(sd, 0x00);       // arg [0]
    spi_write(sd, 0xFF);       // CRC

    // Poll for the CMD13 R2 response. Post-V13: NCR + R1 + status.
    // Pre-V13: nothing — the bytes were eaten as data, CMD13 never ran.
    uint8_t r1_cmd13 = 0xFF;
    uint8_t status_cmd13 = 0xFF;
    bool got_cmd13 = false;
    for (int i = 0; i < 16; ++i) {
        uint8_t b = spi_read(sd);
        if (b != 0xFF) {
            r1_cmd13 = b;
            // Next byte after R1 is the status byte (R2 specific).
            status_cmd13 = spi_read(sd);
            got_cmd13 = true;
            break;
        }
    }

    sd.deselect();
    sd.unmount();
    std::remove(img.c_str());

    // Discriminative shape:
    //   r1            == 0x40  (V13-DIVMMC-01: PARAMETER_ERROR)
    //   got_cmd13     == true  (FSM returned to IDLE after R1)
    //   r1_cmd13      == 0x00  (CMD13 R1: card initialized, no errors)
    //   status_cmd13  == 0x00  (CMD13 R2: no card-status errors)
    bool ok = mounted
           && r1 == 0x40
           && got_cmd13
           && r1_cmd13 == 0x00
           && status_cmd13 == 0x00;

    check("SD-25",
          "CMD24 past EOF leaves FSM in IDLE — a follow-up CMD13 dispatches "
          "cleanly (proves data phase fully suppressed) "
          "(SD Physical Layer Simplified Spec § 4.3.4 + § 7.3.2.3)",
          ok,
          "r1=" + std::to_string(r1) +
          " got_cmd13=" + std::to_string(got_cmd13) +
          " r1_cmd13=" + std::to_string(r1_cmd13) +
          " status_cmd13=" + std::to_string(status_cmd13));
}

// ─── New: SD-22 / CMD8 R7 byte 0 command-version nibble (V12-DIVMMC-03) ──

static void test_sd_22_cmd8_r7_cmdver(SdCardDevice& sd) {
    // SD-22 (V12-DIVMMC-03 reviewer-promoted fix): SD Physical Layer
    // Simplified Spec § 7.3.2.6 R7 4-byte register layout:
    //   bits 31:28 = command version (`0001` = R7 v1)  → byte 0 = 0x10
    //   bits 27:12 = reserved (zero)
    //   bits 11:8  = voltage accepted nibble           → byte 2 low nibble
    //   bits  7:0  = check pattern echo                → byte 3
    //
    // Pre-fix the high byte of the 4-byte register was hardcoded to 0x00
    // (no command-version field). Discriminative shape: issue CMD0 then
    // CMD8 with check pattern 0xAA, drain the NCR / R1, then read 4 bytes
    // of R7 register and verify byte 0 = 0x10.
    sd.reset();
    (void)send_cmd_r1(sd, 0, 0);  // CMD0 GO_IDLE
    // CMD8 SEND_IF_COND, voltage = 0x1, check = 0xAA → arg = 0x000001AA.
    uint8_t r1 = send_cmd_r1(sd, 8, 0x000001AA);
    // After R1, four R7 bytes follow — read them.
    uint8_t r7_byte0 = spi_read(sd);
    uint8_t r7_byte1 = spi_read(sd);
    uint8_t r7_byte2 = spi_read(sd);
    uint8_t r7_byte3 = spi_read(sd);
    sd.deselect();

    bool ok = (r1 == 0x01)
           && (r7_byte0 == 0x10)        // V12-DIVMMC-03 fix: cmd version
           && (r7_byte1 == 0x00)
           && ((r7_byte2 & 0x0F) == 0x01)  // voltage accepted: 0x1
           && (r7_byte3 == 0xAA);           // check pattern echo

    check("SD-22",
          "CMD8 R7 register byte 0 = 0x10 (cmd version 1, "
          "SD Physical Layer Simplified Spec § 7.3.2.6). Pre-fix "
          "hardcoded 0x00 in the cmd-version field.",
          ok,
          "r1=" + std::to_string(r1) +
          " b0=" + std::to_string(r7_byte0) +
          " b1=" + std::to_string(r7_byte1) +
          " b2=" + std::to_string(r7_byte2) +
          " b3=" + std::to_string(r7_byte3));
}

// ─── New: SD-23 / CMD17 past-EOF R1 PARAMETER_ERROR (V12-DIVMMC-04) ────

static void test_sd_23_cmd17_past_eof_r1_paramerror() {
    // SD-23 (V12-DIVMMC-04 reviewer-promoted fix): SD Physical Layer
    // Simplified Spec § 7.3.2.1 (Table 7-9) R1 layout. Bit 6 is
    // PARAMETER_ERROR — "argument was out of the allowed range". When
    // CMD17/CMD18 is issued with a sector beyond end-of-image, the card
    // MUST set R1 bit 6 (in addition to emitting the data error token
    // 0x08). Pre-fix R1=0x00 ("no errors") was the weaker spec-permissible
    // form; the fix sets R1 = 0x40 (PARAMETER_ERROR) for spec strictness.
    //
    // Discriminative shape: small 4-sector image, CMD17 sector=100. R1
    // must have bit 6 set (= 0x40 with no other status flags). Same
    // for CMD18.
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);

    uint8_t r1_cmd17 = send_cmd_r1(sd, 17, 100);
    sd.deselect();

    sd.reset();
    init_card(sd);
    uint8_t r1_cmd18 = send_cmd_r1(sd, 18, 100);
    sd.deselect();

    // Regression guard: in-bounds CMD17 sector=0 must still return R1=0x00.
    sd.reset();
    init_card(sd);
    uint8_t r1_ok = send_cmd_r1(sd, 17, 0);
    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && (r1_cmd17 & 0x40) != 0    // PARAMETER_ERROR set
           && (r1_cmd18 & 0x40) != 0    // PARAMETER_ERROR set
           && r1_ok == 0x00;             // in-bounds: no errors

    check("SD-23",
          "CMD17/CMD18 past EOF set R1 bit 6 PARAMETER_ERROR per "
          "SD Phys Layer Spec § 7.3.2.1 Table 7-9. In-bounds R1=0x00.",
          ok,
          "r1_cmd17=" + std::to_string(r1_cmd17) +
          " r1_cmd18=" + std::to_string(r1_cmd18) +
          " r1_ok=" + std::to_string(r1_ok));

    std::remove(img.c_str());
}

// ─── New: SD-24 / CMD24 stray pre-token bytes ignored (V12-DIVMMC-06) ──

static void test_sd_24_cmd24_pre_token_ignored() {
    // SD-24 (V12-DIVMMC-06 reviewer-promoted fix): per SD Physical Layer
    // Simplified Spec § 7.3.3.2, after R1 the card waits for the 0xFE
    // start-of-block token. ANY pre-token byte (including but not
    // limited to 0xFF gap bytes) must be IGNORED. The pre-fix only
    // skipped 0xFF gap bytes and absorbed any other pre-token byte as
    // data_block_[0], silently shifting the entire payload by 1+.
    //
    // Discriminative shape: CMD24 sector=0; send a stray byte (0x55)
    // BEFORE the 0xFE token, then 0xFE, then a deterministic payload.
    // Read back via CMD17. Pre-fix: data_block_[0]=0x55, payload[0]
    // becomes data_block_[1], …, last byte lost. Readback would NOT
    // match the original payload. Post-fix: stray byte is ignored, the
    // payload is captured cleanly, readback matches.
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);

    uint8_t pattern[512];
    for (int i = 0; i < 512; ++i)
        pattern[i] = static_cast<uint8_t>(i ^ 0x5A);

    uint8_t r1_wr = send_cmd_r1(sd, 24, 0);
    spi_write(sd, 0x55);   // STRAY pre-token byte (NOT 0xFF, NOT 0xFE)
    spi_write(sd, 0xFE);   // start token
    for (int i = 0; i < 512; ++i) spi_write(sd, pattern[i]);
    spi_write(sd, 0xFF);   // CRC hi
    spi_write(sd, 0xFF);   // CRC lo

    // Drain data response.
    bool data_resp_ok = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0x05) { data_resp_ok = true; break; }
        if (b != 0xFF && b != 0x00) break;
    }
    sd.deselect();

    // Readback via CMD17.
    sd.reset();
    init_card(sd);
    uint8_t r1_rd = send_cmd_r1(sd, 17, 0);
    bool tok = wait_token(sd);
    uint8_t got[512] = {};
    if (tok) read_block(sd, got);
    sd.deselect();
    sd.unmount();

    bool match = std::memcmp(got, pattern, 512) == 0;
    bool ok = mounted && r1_wr == 0x00 && data_resp_ok
           && r1_rd == 0x00 && tok && match;

    check("SD-24",
          "CMD24 ignores stray pre-token bytes (other than 0xFE/0xFF) — "
          "data block boundary preserved per SD Phys Layer Spec "
          "§ 7.3.3.2. Pre-fix absorbed stray byte as data_block_[0], "
          "shifting payload.",
          ok,
          "r1_wr=" + std::to_string(r1_wr) +
          " resp=" + std::string(data_resp_ok ? "1" : "0") +
          " match=" + (match ? "1" : "0"));

    std::remove(img.c_str());
}

// ─── New: SD-26 / CMD18 mid-stream past-EOF data error token (V14-DIVMMC-01) ─

static void test_sd_26_cmd18_midstream_past_eof_error_token() {
    // SD-26 (V14-DIVMMC-01 verify-audit fix): per SD Physical Layer
    // Simplified Spec § 7.3.3.3 (Data Error Token format), when the card
    // cannot deliver the requested data block (out-of-range, ECC failure,
    // CC error, generic error), it sends a 1-byte error token in place of
    // the 0xFE start-of-block token. Bit layout (MSB to LSB) 0b0000_OECR
    // where bit 3 = O (OUT_OF_RANGE → mask 0x08), bit 2 = E (card ECC
    // failed), bit 1 = C (CC error), bit 0 = R (geneRic error); the
    // OUT_OF_RANGE-only token is 0x08.
    //
    // Pre-fix CMD18 mid-stream past-EOF silently aborted by emitting 0xFF
    // and transitioning to IDLE — diverging from spec-compliant SD cards
    // that emit 0x08 as a discriminative error token. Symmetric with the
    // V12-DIVMMC-04 + V13-DIVMMC-01 CMD17 / CMD18-initial-block past-EOF
    // fixes which already emit 0x08 in the start-of-stream case.
    //
    // Discriminative shape: 4-sector image; CMD18 starts at sector 3
    // (last valid sector). Stream block 3 OK; the AT-END inter-block
    // boundary is where the past-EOF check fires (sector 4 would be past
    // EOF). Pre-fix: byte after CRC is 0xFF (== generic line-idle).
    // Post-fix: byte after CRC is 0x08 (data error token, OUT_OF_RANGE).
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);

    uint8_t r1 = send_cmd_r1(sd, 18, 3);  // start at LAST valid sector
    bool tok3 = wait_token(sd);
    uint8_t b3[512] = {};
    if (tok3) read_block(sd, b3);

    // After block 3's CRC, the next byte the card emits is the inter-block
    // filler in the SUCCESS path (next sector loaded, 0xFF returned, then
    // 0xFE in resp_buf_ for the next read). In the past-EOF path (sector
    // 4 > 3 last valid), the post-fix card returns 0x08 instead.
    //
    // Sample up to 16 post-CRC bytes; the FIRST non-0xFF byte is what the
    // card has chosen to signal — must be 0x08 (post-fix), not 0xFE
    // (would mean another block was being prepared) and not just 0xFF
    // forever (pre-fix silent abort).
    uint8_t signal_byte = 0xFF;
    bool saw_data_token = false;
    for (int i = 0; i < 16; ++i) {
        uint8_t b = spi_read(sd);
        if (b == 0xFE) { saw_data_token = true; break; }
        if (b != 0xFF) { signal_byte = b; break; }
    }
    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && r1 == 0x00
           && tok3 && b3[0] == 0x03    // sector 3 streamed correctly
           && !saw_data_token          // no spurious 0xFE for sector 4
           && signal_byte == 0x08;     // V14-DIVMMC-01 fix: error token

    check("SD-26",
          "CMD18 mid-stream past-EOF emits data error token 0x08 per "
          "SD Phys Layer Spec § 7.3.3.3 (V14-DIVMMC-01). Pre-fix "
          "silently aborted with 0xFF.",
          ok,
          "r1=" + std::to_string(r1) +
          " tok3=" + (tok3 ? "1" : "0") +
          " b3_0=" + std::to_string(b3[0]) +
          " saw_data_token=" + (saw_data_token ? "1" : "0") +
          " signal=" + std::to_string(signal_byte));

    std::remove(img.c_str());
}

// ─── New: SD-27 / CMD8 R7 R1-byte reflects initialized_ (V14-DIVMMC-02) ─

static void test_sd_27_cmd8_r7_r1_post_init() {
    // SD-27 (V14-DIVMMC-02 verify-audit fix): per SD Physical Layer
    // Simplified Spec § 7.3.2.6, the R7 4-byte register is preceded by
    // an R1-format byte. R1 reflects the live card state (bit 0 = "in
    // idle state"). Pre-fix hard-coded R1=0x01 in CMD8 regardless of
    // `initialized_`, so a CMD8 issued AFTER successful ACMD41 init
    // would still report idle — diverging from spec-compliant cards.
    //
    // Discriminative shape: full init via CMD0 → CMD8 → CMD55 + ACMD41
    // → CMD58 (this proves `initialized_=true` by virtue of subsequent
    // CMD17 returning R1=0x00). Then re-issue CMD8 and read its R1
    // byte. Pre-fix: R1 = 0x01 (idle, wrong). Post-fix: R1 = 0x00
    // (ready, spec-compliant).
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);  // CMD0, CMD8, CMD55+ACMD41, CMD58

    // Confirm init done: CMD17 R1 should be 0x00 (ready).
    uint8_t r1_pre = send_cmd_r1(sd, 17, 0);
    // Drain block 0 data so state machine cleans up.
    (void)wait_token(sd);
    uint8_t scratch[512];
    read_block(sd, scratch);
    sd.deselect();

    // Now re-issue CMD8 in initialized state.
    uint8_t r1_post_init = send_cmd_r1(sd, 8, 0x000001AA);
    // Drain the 4 R7 bytes after R1.
    (void)spi_read(sd);
    (void)spi_read(sd);
    (void)spi_read(sd);
    uint8_t r7_check = spi_read(sd);
    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && r1_pre == 0x00            // init done sanity
           && r1_post_init == 0x00      // V14-DIVMMC-02 fix: post-init R1=0
           && r7_check == 0xAA;         // check pattern still echoes

    check("SD-27",
          "CMD8 R7 byte 0 (R1) reflects `initialized_` per SD Phys "
          "Layer Spec § 7.3.2.6 / R1 layout. Post-init CMD8 returns "
          "R1=0x00 (ready), not the pre-fix hardcoded 0x01 (idle).",
          ok,
          "r1_pre=" + std::to_string(r1_pre) +
          " r1_post_init=" + std::to_string(r1_post_init) +
          " r7_check=" + std::to_string(r7_check));

    std::remove(img.c_str());
}

// ─── New: SD-28 / CMD24 write to RO image emits 0x0D (V15-DIVMMC-01) ────
//
// V15-DIVMMC-01 (Pass-15 verify-audit, 2026-05-10): SD Physical Layer
// Simplified Spec § 7.3.3.3 (Data Response Token) — `sss=110` (= 0x0D)
// signals "data rejected due to write error". When the SD-image file is
// opened in fall-back read-only mode (mount() line 33, e.g. user file with
// no write permission, or RO mount of a system image) OR when the
// underlying disk fails the host-side write (disk full, I/O error, etc.),
// `file_.write()` silently sets the fstream `failbit`. Pre-fix the card
// unconditionally emitted 0x05 (data accepted) regardless of the stream
// state — diverging from every real SD card with a mechanical write-
// protect tab (which would emit 0x0D for any in-bounds write attempt).
//
// Discriminative shape:
//   1. Build an 8-sector temp image, then chmod 0444 (read-only).
//   2. Mount — falls through to RO path, mount() returns true (mounted).
//   3. CMD24 sector=0 (in-bounds, R1 path is OK so data phase starts).
//   4. Send 0xFE + 512 data + 2 CRC. Pre-fix: 0x05 (data accepted, lying).
//      Post-fix: 0x0D (write error — fstream rejected the write).
//   5. Symmetric guard: re-mount the same image with 0644 perms (RW),
//      same CMD24 succeeds with 0x05.
//
// Class-(c) latent — the canonical NextZXOS fixture is opened RW and
// writes succeed; but a forensic / user-supplied RO mount silently
// corrupts the host's expectations of write success. Symmetric with
// SD-21 / SD-25 / SD-26 past-EOF rejections and V12-DIVMMC-02 family.
static void test_sd_28_cmd24_ro_image_write_error() {
    const uint32_t n_sectors = 8;
    std::string img = make_image(n_sectors);

    // root ignores the permission bits this row depends on: it opens a 0444
    // file for writing regardless, so the card mounts RW and answers 0x05.
    // The RO premise simply cannot be constructed as root (CI runs in a
    // container as root; a local run does not), so skip rather than report a
    // failure that says nothing about the code under test.
    if (geteuid() == 0) {
        skip("SD-28", "running as root; cannot construct a read-only image");
        std::remove(img.c_str());
        return;
    }

    // Make the image read-only on the host filesystem.
    if (chmod(img.c_str(), 0444) != 0) {
        // Permission change failed — skip rather than spuriously fail.
        skip("SD-28", "host chmod(0444) failed; cannot construct RO image");
        std::remove(img.c_str());
        return;
    }

    SdCardDevice sd;
    bool mounted = sd.mount(img);  // expected to fall through to RO mode
    sd.reset();
    init_card(sd);

    // CMD24 sector=0 (in-bounds) → R1 should be 0x00 (no error in command).
    uint8_t r1 = send_cmd_r1(sd, 24, 0);
    spi_write(sd, 0xFE);                            // data token
    for (int i = 0; i < 512; ++i) spi_write(sd, 0xAA);  // 512 data bytes
    spi_write(sd, 0xFF);                            // CRC hi
    spi_write(sd, 0xFF);                            // CRC lo

    // Poll for the data response token.
    uint8_t resp = 0xFF;
    bool got_resp = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd);
        if (b != 0xFF) { resp = b; got_resp = true; break; }
    }
    sd.deselect();
    sd.unmount();

    // Restore RW perms so the cleanup `std::remove` works on systems where
    // the file's parent dir requires write access (and to be tidy).
    chmod(img.c_str(), 0644);

    // Symmetric regression guard: re-mount the same image RW, same CMD24
    // must now succeed with 0x05 (proves the test isn't accidentally
    // detecting a different failure mode — e.g. the image being malformed).
    SdCardDevice sd_rw;
    bool mounted_rw = sd_rw.mount(img);
    sd_rw.reset();
    init_card(sd_rw);
    uint8_t r1_rw = send_cmd_r1(sd_rw, 24, 0);
    spi_write(sd_rw, 0xFE);
    for (int i = 0; i < 512; ++i) spi_write(sd_rw, 0xAA);
    spi_write(sd_rw, 0xFF);
    spi_write(sd_rw, 0xFF);
    uint8_t resp_rw = 0xFF;
    bool got_resp_rw = false;
    for (int i = 0; i < 32; ++i) {
        uint8_t b = spi_read(sd_rw);
        if (b != 0xFF) { resp_rw = b; got_resp_rw = true; break; }
    }
    sd_rw.deselect();
    sd_rw.unmount();

    bool ok = mounted                       // RO fall-through still mounts
           && mounted_rw                    // RW remount works
           && r1 == 0x00                    // R1 says command OK (in-bounds)
           && got_resp && resp == 0x0D      // V15-DIVMMC-01: write-error token
           && r1_rw == 0x00 && got_resp_rw && resp_rw == 0x05;  // RW guard

    check("SD-28",
          "CMD24 to RO-mounted image emits data-response 0x0D (write error) "
          "per SD Phys Layer Spec § 7.3.3.3; same image mounted RW emits "
          "0x05 (data accepted) — discriminates the silent-write-loss "
          "(pre-fix) bug.",
          ok,
          "r1=" + std::to_string(r1) +
          " resp=" + std::to_string(resp) +
          " r1_rw=" + std::to_string(r1_rw) +
          " resp_rw=" + std::to_string(resp_rw));

    std::remove(img.c_str());
}

// ─── New: SD-29 / ACMD41 HCS bit reflected in CMD58 OCR CCS (V17-DIVMMC-01) ─
//
// V17-DIVMMC-01 (Pass-17 verify-audit, 2026-05-10): SD Phys Layer Simplified
// Spec § 4.2.3 / § 5.1: ACMD41's argument bit 30 is the Host Capacity Support
// (HCS) flag. When HCS=0 the host requests SDSC-only mode; the card MUST
// respond with CCS=0 (bit 30 of OCR byte 0) in the subsequent CMD58 R3
// response. Pre-fix the emulator IGNORED the HCS bit and unconditionally
// reported CCS=1 — diverging from spec for any host that requests SDSC
// compatibility mode (e.g. legacy MMC/SDSC-only firmware, strict spec
// validators). TBBlue / NextZXOS / FatFs always set HCS=1 so the
// divergence is class-(c) latent on the boot path.
//
// Discriminative shape:
//   1. Init via CMD0 → CMD8 → CMD55 → ACMD41 with HCS=0 (arg=0x00100000).
//   2. CMD58 — read R1 + 4 OCR bytes. Pre-fix: ocr0=0xC0 (bit 30 set,
//      lying about SDHC). Post-fix: ocr0=0x80 (bit 30 clear, SDSC
//      reported).
//   3. Re-init: CMD0 → CMD8 → CMD55 → ACMD41 with HCS=1 (arg=0x40000000).
//   4. CMD58 — read R1 + 4 OCR bytes. Both pre and post-fix: ocr0=0xC0
//      (bit 30 set). This second leg is the symmetric "happy path"
//      guard, ensuring the fix doesn't accidentally regress HCS=1.
static void test_sd_29_acmd41_hcs_reflected_in_ocr() {
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);

    // Leg 1: HCS=0 → expect CCS=0 in OCR byte 0.
    sd.reset();
    (void)send_cmd_r1(sd, 0, 0);          // CMD0 GO_IDLE
    (void)send_cmd_r1(sd, 8, 0x1AA);      // CMD8 SEND_IF_COND
    (void)send_cmd_r1(sd, 55, 0);         // CMD55 APP_CMD
    (void)send_cmd_r1(sd, 41, 0x00100000); // ACMD41 with HCS=0 (bit 30 clear)
    uint8_t r1_hcs0  = send_cmd_r1(sd, 58, 0);
    uint8_t ocr0_hcs0 = spi_read(sd);
    (void)spi_read(sd); (void)spi_read(sd); (void)spi_read(sd);  // drain rest of OCR
    sd.deselect();

    // Leg 2: HCS=1 → expect CCS=1 in OCR byte 0.
    sd.reset();
    (void)send_cmd_r1(sd, 0, 0);
    (void)send_cmd_r1(sd, 8, 0x1AA);
    (void)send_cmd_r1(sd, 55, 0);
    (void)send_cmd_r1(sd, 41, 0x40000000); // ACMD41 with HCS=1 (bit 30 set)
    uint8_t r1_hcs1  = send_cmd_r1(sd, 58, 0);
    uint8_t ocr0_hcs1 = spi_read(sd);
    (void)spi_read(sd); (void)spi_read(sd); (void)spi_read(sd);
    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && r1_hcs0 == 0x00                 // init succeeded
           && (ocr0_hcs0 & 0xC0) == 0x80      // HCS=0 → bit 31 set, bit 30 clear
           && r1_hcs1 == 0x00                 // init succeeded again
           && (ocr0_hcs1 & 0xC0) == 0xC0;     // HCS=1 → both bits set

    check("SD-29",
          "ACMD41 HCS bit (arg bit 30) is reflected in CMD58 OCR CCS bit "
          "(byte 0 bit 6) per SD Phys Layer Spec § 4.2.3 / § 5.1. HCS=0 "
          "→ CCS=0 (SDSC mode); HCS=1 → CCS=1 (SDHC mode). Pre-fix "
          "unconditionally reported CCS=1.",
          ok,
          "r1_hcs0=" + std::to_string(r1_hcs0) +
          " ocr0_hcs0=0x" + [&]{ char b[8]; std::snprintf(b,sizeof(b),"%02X",ocr0_hcs0); return std::string(b); }() +
          " r1_hcs1=" + std::to_string(r1_hcs1) +
          " ocr0_hcs1=0x" + [&]{ char b[8]; std::snprintf(b,sizeof(b),"%02X",ocr0_hcs1); return std::string(b); }());

    std::remove(img.c_str());
}

// V18-DIVMMC-NIT-01: full-duplex stream advance in receive() default branch.
// Per VHDL `serial/spi_master.vhd:104-117` (oshift_r) + `:148-168` (ishift_r /
// miso_dat) + `zxnext.vhd:3270-3298`, every clocked SPI byte exchange captures
// whatever the slave drives on MISO regardless of MOSI content.  While
// state_=RESPONDING/SENDING_DATA/WRITE_RESP, the SD card is actively shifting
// out its response stream — a host write_data() with a non-CMD-start byte
// must (a) return the next response byte on MISO and (b) advance the response
// stream, exactly as if read_data() had been called.  Pre-fix, the default
// branch unconditionally returned 0xFF and left resp_idx_/data_idx_ frozen.
static void test_sd_30_full_duplex_stream_advance() {
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);

    sd.reset();
    init_card(sd);
    // CMD17 sector=1: card emits R1=0x00, then 0xFE token, then 512 data
    // bytes (= pattern (1+i)&0xFF for i>=4 per make_image), then 2 CRC bytes.
    uint8_t r1 = send_cmd_r1(sd, 17, 1);

    // Drain through the 0xFE data token using receive() — these are 0xFF
    // MOSI bytes interleaved with the card's RESPONDING/SENDING-token bytes.
    // We poll receive(0xFF) (write-side) directly to confirm the host sees
    // the response stream on MISO via the full-duplex semantics.
    uint8_t b_token = 0xFF;
    for (int i = 0; i < 16; ++i) {
        b_token = sd.receive(0xFF);
        if (b_token == 0xFE) break;
    }

    // Now in SENDING_DATA state, mid-block.  Read first data byte via send()
    // to anchor a baseline (sector 1, offset 0: byte = 0x01).
    uint8_t data0 = sd.send();

    // Now exercise the fix: call receive(0xFF) — a non-CMD-start byte — while
    // in SENDING_DATA.  Pre-fix returns 0xFF and leaves data_idx_ at 1.  Post-
    // fix delegates to send(), returning the byte at data_idx_=1 (sector 1
    // offset 1 = 0x00 per make_image's 4-byte sector-id header).
    uint8_t rx_during_send = sd.receive(0xFF);

    // Next send() should return the byte at data_idx_=2 (post-fix) — proving
    // the stream advanced.  Pre-fix it returns the byte at data_idx_=1 (= 0x00
    // for sector 1, indistinguishable from rx_during_send's pre-fix 0xFF
    // value via this byte alone), so we ALSO assert rx_during_send != 0xFF
    // and equals 0x00 (the expected post-fix MISO value).
    uint8_t after = sd.send();

    sd.deselect();
    sd.unmount();

    // Per make_image: sector 1 bytes are [0x01, 0x00, 0x00, 0x00, (1+4)&FF=0x05,
    // (1+5)&FF=0x06, ...].
    bool ok = mounted
           && r1 == 0x00
           && b_token == 0xFE          // token captured via full-duplex receive
           && data0 == 0x01            // anchor: sector 1 offset 0
           && rx_during_send == 0x00   // post-fix: MISO carries data_block_[1]
           && after == 0x00;           // post-fix: stream advanced to data_block_[2]

    check("SD-30",
          "receive(non-CMD-byte) in SENDING_DATA / RESPONDING / WRITE_RESP "
          "returns the next MISO byte and advances the response stream "
          "(full-duplex SPI per spi_master.vhd:104-168). Pre-fix returned "
          "0xFF and left resp_idx_/data_idx_ un-advanced.",
          ok,
          "r1=" + std::to_string(r1) +
          " b_token=" + std::to_string(b_token) +
          " data0=" + std::to_string(data0) +
          " rx_during_send=" + std::to_string(rx_during_send) +
          " after=" + std::to_string(after));

    std::remove(img.c_str());
}

// V18-DIVMMC-NIT-01 (b): same fix exercised via RESPONDING state (R1 stream
// for a CMD13 status read).  CMD13 returns a 2-byte R2 response.  After
// CMD13 we read R2[0] via send(), then call receive(0x00) (non-CMD byte) to
// snap the next byte and advance, then read again to confirm advance.
static void test_sd_31_full_duplex_responding_state() {
    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);

    sd.reset();
    init_card(sd);
    // Issue CMD13 — card emits R2 (2 bytes, R1 + status) in RESPONDING state.
    // send_cmd_r1 returns the first non-0xFF byte and leaves the second
    // (status) byte queued.  We then have RESPONDING state with 1 byte left.
    sd.receive(0x40 | 13);                // CMD13 start byte
    sd.receive(0x00); sd.receive(0x00);   // 4 arg bytes
    sd.receive(0x00); sd.receive(0x00);
    sd.receive(0x01);                     // CRC stop bit

    // Poll for R1 via full-duplex receive().  This is the boot-path-relevant
    // sub-case where receive() is the only channel the host uses.  Pre-fix:
    // RESPONDING state's default branch in receive() returns 0xFF without
    // advancing resp_idx_.  Post-fix: send() is delegated to, returning the
    // R1 byte (0x00) and advancing resp_idx_ to point at R2[1] (status byte).
    uint8_t r1_via_receive = 0xFF;
    for (int i = 0; i < 16; ++i) {
        uint8_t b = sd.receive(0xFF);
        if (b != 0xFF) { r1_via_receive = b; break; }
    }
    // Now the R2 status byte should be next on MISO.  Read via send().
    // Per CMD13 handler the status byte is typically 0x00 for a healthy card.
    uint8_t r2_status = sd.send();

    sd.deselect();
    sd.unmount();

    bool ok = mounted
           && r1_via_receive == 0x00   // post-fix: receive() observed R1 on MISO
           && r2_status     == 0x00;   // and stream advanced to next byte

    check("SD-31",
          "receive(non-CMD-byte) in RESPONDING state observes the next "
          "response byte on MISO and advances resp_idx_ per VHDL full-duplex "
          "semantics (spi_master.vhd:104-168). Pre-fix the receive() default "
          "branch returned 0xFF and the R1 byte would never be observable "
          "via the write-side channel.",
          ok,
          "r1_via_receive=" + std::to_string(r1_via_receive) +
          " r2_status=" + std::to_string(r2_status));

    std::remove(img.c_str());
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

static void test_sd_33_cmd10_cid_mdt_year() {
    // SD-33 (V24-DIVMMC-01, Pass-24 convergence pressure-test, 2026-05-11):
    // per SD Physical Layer Simplified Spec v6.00 § 5.2 Table 5-1, the
    // Manufacturing Date (MDT) field is 12 bits at CID bits [19:8] in format
    // `year_offset[11:4] | month[3:0]`. Year offset is from 2000.
    //
    // The CID returned by cmd10_send_cid has its block comment stating
    // "year=2026, month=05". For that intent:
    //   year_offset = 26 = 0x1A; MDT = (0x1A << 4) | 0x5 = 0x1A5
    //   CID[13] (bits [23:16]) = `reserved[3:0] | MDT[11:8]` = 0x00 | 0x1 = 0x01
    //   CID[14] (bits [15:8])  = MDT[7:0] = 0xA5
    //
    // Pre-fix CID[14] was 0x65 -> MDT[7:0] = `0110 0101` -> year_low_nibble =
    // 0x6, month = 0x5. Combined: year_offset = (0x1 << 4) | 0x6 = 0x16 = 22
    // -> encoded year 2022 (off by 4 from intent). Class-(c) cosmetic latent
    // (TBBlue/FatFs do not inspect MDT, but a forensic firmware / mmls-style
    // tool that decodes the CID would log the wrong manufacturing date).
    //
    // Test shape: issue CMD10 SEND_CID and decode the CID response payload.
    // Expected post-fix: CID[14] == 0xA5; year_offset decoded from MDT[11:4]
    // must equal 26 (= 0x1A); month decoded from MDT[3:0] must equal 5.
    std::string img = make_image(4);
    SdCardDevice sd;
    if (!sd.mount(img)) {
        check("SD-33-MOUNT", "image mount", false);
        std::remove(img.c_str());
        return;
    }

    // Initialize the card so cmd10_send_cid doesn't short-circuit on
    // `if (!initialized_) queue_r1(0x01); return;`.
    (void)send_cmd_r1(sd, 0, 0);            // CMD0 GO_IDLE
    (void)send_cmd_r1(sd, 8, 0x1AA);        // CMD8
    (void)send_cmd_r1(sd, 55, 0);
    (void)send_cmd_r1(sd, 41, 0x40000000);  // ACMD41 -> initialized_=true

    // CMD10 SEND_CID — response is NCR + R1(0x00) + Nac gap + 0xFE + 16 CID
    // bytes + 2 CRC bytes (21 bytes total, GH #98).
    uint8_t r1 = send_cmd_r1(sd, 10, 0);
    // GH #98: CMD10 now inserts a ≥1-byte Nac gap (§ 7.5.2) between R1 and
    // the token, so token-poll like a real host instead of assuming the
    // token is the very next byte. The gap contract itself is SD-NAC-05's
    // job; this row only needs the CID payload.
    bool token_found = wait_token(sd);
    uint8_t cid_bytes[16];
    for (int i = 0; i < 16; ++i) {
        cid_bytes[i] = spi_read(sd);
    }
    (void)spi_read(sd);  // CRC high
    (void)spi_read(sd);  // CRC low

    sd.deselect();
    sd.unmount();
    std::remove(img.c_str());

    // Decode MDT per SD spec § 5.2:
    //   MDT[11:8] = CID[13] bits [3:0]
    //   MDT[7:0]  = CID[14]
    // year_offset = MDT[11:4] (8 bits), month = MDT[3:0] (4 bits).
    const uint16_t mdt = ((cid_bytes[13] & 0x0F) << 8) | cid_bytes[14];
    const uint8_t year_offset = static_cast<uint8_t>((mdt >> 4) & 0xFF);
    const uint8_t month       = static_cast<uint8_t>(mdt & 0x0F);
    const int year_full = 2000 + year_offset;

    const bool r1_ok        = (r1 == 0x00);
    const bool token_ok     = token_found;
    const bool cid14_ok     = (cid_bytes[14] == 0xA5);   // discriminative byte
    const bool year_ok      = (year_full == 2026);
    const bool month_ok     = (month == 0x05);

    check("SD-33",
          "CMD10 CID Manufacturing Date encodes year=2026 month=05 per SD "
          "Physical Layer Simplified Spec § 5.2 Table 5-1. Pre-fix CID[14] "
          "was 0x65 encoding year_offset=0x16 = 2022 (off-by-4); post-fix "
          "CID[14] = 0xA5 encoding year_offset=0x1A = 2026.",
          r1_ok && token_ok && cid14_ok && year_ok && month_ok,
          "r1=" + std::to_string(r1) +
          " token_found=" + (token_found ? "1" : "0") +
          " cid[14]=" + std::to_string(cid_bytes[14]) +
          " year=" + std::to_string(year_full) +
          " month=" + std::to_string(month));
}

// ─── Task 26 items 1/2/3 — SPI Ncr, CMD58 no-$FF, real data-block CRC ──

// Independent reference CRC-16 for the test side. Written from scratch
// (not shared with the production sd_crc16) so the assertion is not
// circular. Validated against the published catalogue check value
// CRC-16/XMODEM("123456789") = 0x31C3 in TASK26-CRC-01 below.
static uint16_t ref_crc16_xmodem(const uint8_t* p, size_t n) {
    uint16_t c = 0x0000;
    for (size_t i = 0; i < n; ++i) {
        c ^= static_cast<uint16_t>(p[i]) << 8;
        for (int b = 0; b < 8; ++b)
            c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021)
                             : static_cast<uint16_t>(c << 1);
    }
    return c;
}

// TASK26-NCR-01: exactly 2 leading idle ($FF) bytes precede R1.
// SD Phys Layer Spec § 7.5.4 (Ncr). Discriminative: pre-fix jnext emitted
// a single idle byte, so byte[1] was R1 (0x01 for an idle card), not $FF.
static void test_task26_ncr_two_idle_bytes(SdCardDevice& sd) {
    sd.reset();
    // CMD0 GO_IDLE — R1 = 0x01 via queue_r1() (the shared R1 path).
    spi_write(sd, 0x40 | 0);
    for (int i = 0; i < 4; ++i) spi_write(sd, 0x00);
    spi_write(sd, 0x95);
    const uint8_t b0 = spi_read(sd);
    const uint8_t b1 = spi_read(sd);
    const uint8_t b2 = spi_read(sd);
    sd.deselect();
    check("TASK26-NCR-01",
          "CMD0 response has exactly 2 idle ($FF) bytes before R1 "
          "(SD Phys Layer § 7.5.4 Ncr). Pre-fix emitted 1 idle byte so "
          "byte[1] was R1=0x01, not $FF.",
          b0 == 0xFF && b1 == 0xFF && b2 == 0x01,
          "b0=" + std::to_string(b0) + " b1=" + std::to_string(b1) +
          " b2=" + std::to_string(b2));
}

// TASK26-OCR-01: no OCR payload byte equals the $FF idle sentinel.
// Discriminative: pre-fix the voltage-window byte [23:16] was 0xFF, which
// a skip-$FF firmware reader drops → OCR misaligns.
static void test_task26_cmd58_no_ff_payload(SdCardDevice& sd) {
    sd.reset();
    init_card(sd);
    const uint8_t r1 = send_cmd_r1(sd, 58, 0);   // skips the 2 NCR bytes
    uint8_t ocr[4];
    for (int i = 0; i < 4; ++i) ocr[i] = spi_read(sd);
    sd.deselect();
    const bool no_ff = ocr[0] != 0xFF && ocr[1] != 0xFF &&
                       ocr[2] != 0xFF && ocr[3] != 0xFF;
    // Also confirm OCR semantics intact: power-up (bit31) + CCS (bit30) set
    // for an SDHC host (init_card sends ACMD41 HCS=1).
    const bool ccs_ok = (ocr[0] & 0xC0) == 0xC0;
    check("TASK26-OCR-01",
          "CMD58 OCR payload contains no $FF byte (tbblue.fw skips $FF as "
          "idle and would misalign). Pre-fix OCR[1] (voltage window) = 0xFF.",
          r1 == 0x00 && no_ff && ccs_ok,
          "r1=" + std::to_string(r1) +
          " ocr=" + std::to_string(ocr[0]) + "," + std::to_string(ocr[1]) +
          "," + std::to_string(ocr[2]) + "," + std::to_string(ocr[3]));
}

// TASK26-CRC-01: read-data blocks carry a real CRC-16 (not dummy 0x0000).
// Discriminative: pre-fix jnext emitted 0x00,0x00 for the 2 CRC bytes.
static void test_task26_data_block_crc() {
    // (a) Validate the test-side reference against the published
    //     CRC-16/XMODEM catalogue check value, so the emitted-CRC
    //     assertion below rests on a known-correct oracle.
    const uint8_t chk[] = { '1','2','3','4','5','6','7','8','9' };
    const uint16_t chk_crc = ref_crc16_xmodem(chk, sizeof(chk));
    check("TASK26-CRC-00",
          "reference CRC-16/XMODEM(\"123456789\") == 0x31C3 (SD data-block "
          "CRC variant: poly 0x1021, init 0x0000)",
          chk_crc == 0x31C3,
          "got=0x" + [&]{ char b[8]; std::snprintf(b,sizeof(b),"%04X",chk_crc); return std::string(b); }());

    std::string img = make_image(4);
    SdCardDevice sd;
    bool mounted = sd.mount(img);
    sd.reset();
    init_card(sd);
    const uint8_t r1 = send_cmd_r1(sd, 17, 1);   // read sector 1
    const bool tok = wait_token(sd);
    uint8_t data[512];
    for (int i = 0; i < 512; ++i) data[i] = spi_read(sd);
    const uint8_t crc_hi = spi_read(sd);
    const uint8_t crc_lo = spi_read(sd);
    sd.deselect();
    sd.unmount();
    std::remove(img.c_str());

    const uint16_t emitted  = static_cast<uint16_t>((crc_hi << 8) | crc_lo);
    const uint16_t expected = ref_crc16_xmodem(data, 512);
    check("TASK26-CRC-01",
          "CMD17 data block emits the real CRC-16 (poly 0x1021, init "
          "0x0000) over the 512 data bytes, high byte first. Pre-fix "
          "emitted dummy 0x0000.",
          mounted && r1 == 0x00 && tok && expected != 0x0000 &&
          emitted == expected,
          "emitted=0x" + [&]{ char b[8]; std::snprintf(b,sizeof(b),"%04X",emitted); return std::string(b); }() +
          " expected=0x" + [&]{ char b[8]; std::snprintf(b,sizeof(b),"%04X",expected); return std::string(b); }());
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
    test_nac_gap(sd);                // SD-NAC-01..05 (GH #84, GH #98)
    test_cmd18_end_of_image(sd);
    test_cmd18_cs_deassert_aborts(sd);
    test_cmd18_stream_survives_deselect(sd);
    test_cmd13_status(sd);
    test_cmd16_set_blocklen(sd);
    test_cmd23_block_count(sd);
    test_cmd24_write(sd);
    test_readonly_mount();
    test_mmc_cmd1_init(sd);

    // TASK2-TESTCOV — retroactive regression coverage for DivMMC/SD/SPI
    // pass-3..pass-10 fixes. Each test cites the specific fix commit and
    // VHDL/SD-spec reference.
    test_sd_16_cmd16_idle_bit(sd);   // 24a1bc4 (pass-5)
    test_sd_17_cmd24_gap_bytes(sd);  // c7acf9e (pass-4)
    test_sd_18_unhandled_cmd(sd);    // 6ebfd2b (pass-8)
    test_sd_19_unknown_acmd(sd);     // 6ebfd2b (pass-8)
    test_sd_20_cmd55_fallthrough(sd); // ff84d3e (pass-9)

    // BOOT-SD-01 / BOOT-SD-02 / SD-15 use their own local SdCardDevice +
    // images so they don't disturb the shared `sd` and `img` above.
    test_boot_sd_01();
    test_boot_sd_02();
    test_sd_15_mount_full_reset();   // 24a1bc4 (pass-5)
    test_sd_21_cmd24_past_eof();     // V12-DIVMMC-02 + V13-DIVMMC-01 (pass-13 verify-audit)
    test_sd_22_cmd8_r7_cmdver(sd);   // V12-DIVMMC-03 (pass-12 reviewer-promoted)
    test_sd_23_cmd17_past_eof_r1_paramerror();  // V12-DIVMMC-04 (pass-12 reviewer-promoted)
    test_sd_24_cmd24_pre_token_ignored();        // V12-DIVMMC-06 (pass-12 reviewer-promoted)
    test_sd_25_cmd24_past_eof_no_data_phase();   // V13-DIVMMC-01 (pass-13 verify-audit)
    test_sd_26_cmd18_midstream_past_eof_error_token();  // V14-DIVMMC-01 (pass-14 verify-audit)
    test_sd_27_cmd8_r7_r1_post_init();           // V14-DIVMMC-02 (pass-14 verify-audit)
    test_sd_28_cmd24_ro_image_write_error();     // V15-DIVMMC-01 (pass-15 verify-audit)
    test_sd_29_acmd41_hcs_reflected_in_ocr();    // V17-DIVMMC-01 (pass-17 verify-audit)
    test_sd_30_full_duplex_stream_advance();     // V18-DIVMMC-NIT-01 (pass-18 reviewer fix)
    test_sd_31_full_duplex_responding_state();   // V18-DIVMMC-NIT-01 (pass-18 reviewer fix)
    test_sd_33_cmd10_cid_mdt_year();             // V24-DIVMMC-01 (pass-24 convergence pressure-test)

    // ─── Task 26 SPI/SD conformance batch (2026-07-11) ────────────────
    test_task26_ncr_two_idle_bytes(sd);          // item 1 — Ncr = 2 idle bytes
    test_task26_cmd58_no_ff_payload(sd);         // item 2 — CMD58 no $FF payload
    test_task26_data_block_crc();                // item 3 — real data-block CRC-16

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
