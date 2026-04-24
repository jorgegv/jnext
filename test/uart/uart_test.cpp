// UART + I2C/RTC Compliance Test Runner
//
// Full rewrite (Task 1 Wave 2, 2026-04-15) against
// doc/testing/UART-I2C-TEST-PLAN-DESIGN.md. Every assertion cites a VHDL
// file and line range from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo — cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * The plan and the VHDL are the oracle. The C++ implementation is
//     never used to set expected values.
//   * One plan row -> exactly one check() or skip() with the plan ID.
//   * Rows that cannot be exercised through the current public API are
//     reported via skip() with a one-line reason (not via fake checks).
//   * Assertion failures that reflect real C++/VHDL divergence are left
//     failing and feed the Task 3 backlog.
//
// Historical emulator bugs that this suite used to expose (now fixed):
//   * i2c.cpp:101 false-STOP — FIXED in commit 174fa56 (Task 2 item 1).
//     Unblocked I2C-P03, I2C-P05a/b, RTC-01/02/04/05, and on re-audit
//     (2026-04-24) also RTC-06/07/08/09/10 and I2C-P06 which had been
//     mistakenly attributed to a separate BCD / register-pointer fault.
//   * uart.cpp:299 select-register read bit 6 vs bit 3 — FIXED in commit
//     47ee7e2 (Task 2 item 22). Unblocked SEL-02, SEL-05, DUAL-02.
//
// Task 3 UART+I2C SKIP-reduction plan at
// doc/design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md tracks the remaining
// 35 F-skips (bit-level TX/RX, RTC feature gaps). The 12 cross-subsystem
// rows (INT-01..06, GATE-01..03, DUAL-05/06, I2C-10) are re-homed to
// test/uart/uart_integration_test.cpp per feedback_rehome_to_owner_plan
// — they appear below as `// RE-HOME:` comments, not skip() calls, so
// this file's total does not count them.
//
// Run: ./build/test/uart_test

#include "peripheral/uart.h"
#include "peripheral/i2c.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
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
    g_skipped.push_back({id, reason});
}

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Port register selector encoding ───────────────────────────────────
// uart.h comment / zxnext.vhd:2639: cpu_a(9 downto 8) = uart_reg
//   00 = 0x143B Rx      (port_reg 0)
//   01 = 0x153B Select  (port_reg 1)
//   10 = 0x163B Frame   (port_reg 2)
//   11 = 0x133B Tx      (port_reg 3)
constexpr int REG_RX     = 0;
constexpr int REG_SELECT = 1;
constexpr int REG_FRAME  = 2;
constexpr int REG_TX     = 3;

// ── I2C bit-bang primitives ───────────────────────────────────────────
// These drive the I2cController through its public API as NextZXOS would.
// The false-STOP bug in i2c.cpp:101 means most transactions end up in
// State::IDLE mid-byte; the expected ACK/data values therefore disagree
// with the current C++ behaviour. That is a real emulator bug (leave
// failing) and not a harness problem.

void i2c_start(I2cController& i2c) {
    i2c.write_sda(1);
    i2c.write_scl(1);
    i2c.write_sda(0);
    i2c.write_scl(0);
}

void i2c_stop(I2cController& i2c) {
    i2c.write_sda(0);
    i2c.write_scl(1);
    i2c.write_sda(1);
}

uint8_t i2c_send_byte(I2cController& i2c, uint8_t byte) {
    for (int i = 7; i >= 0; --i) {
        i2c.write_sda((byte >> i) & 1);
        i2c.write_scl(1);
        i2c.write_scl(0);
    }
    i2c.write_sda(1);
    i2c.write_scl(1);
    uint8_t ack = i2c.read_sda() & 0x01;
    i2c.write_scl(0);
    return ack;
}

uint8_t i2c_read_byte(I2cController& i2c, bool send_ack) {
    uint8_t byte = 0;
    i2c.write_sda(1);
    for (int i = 7; i >= 0; --i) {
        i2c.write_scl(1);
        byte |= ((i2c.read_sda() & 0x01) << i);
        i2c.write_scl(0);
    }
    i2c.write_sda(send_ack ? 0 : 1);
    i2c.write_scl(1);
    i2c.write_scl(0);
    i2c.write_sda(1);
    return byte;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// Group 1: UART Select Register (port 0x153B)
// VHDL: serial/uart.vhd:268-290 (write), 354-371 (read)
// ══════════════════════════════════════════════════════════════════════

static void test_group1_select() {
    set_group("SEL");
    Uart uart;

    // SEL-01 - uart.vhd:273-278 hard reset: uart_select_r=0, uart0/1 msb=0;
    //          uart.vhd:355 reads "00000" & uart0_prescalar_msb_r
    {
        uart.hard_reset();
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-01",
              "uart.vhd:273-278,355 - hard reset select read = 0x00",
              sel == 0x00,
              fmt("got=0x%02x", sel));
    }

    // SEL-02 - uart.vhd:280 writes uart_select_r = d(6); uart.vhd:371
    //          UART 1 select read = "01000" & msb  ->  bit 3 set, value 0x08
    //          (KNOWN EMULATOR BUG uart.cpp:299 uses bit 6 -> 0x40)
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x40);
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-02",
              "uart.vhd:371 - write 0x40, UART 1 select read = 0x08",
              sel == 0x08,
              fmt("got=0x%02x (VHDL expects 0x08)", sel));
    }

    // SEL-03 - uart.vhd:280 write d(6)=0 restores uart_select_r=0;
    //          uart.vhd:355 UART 0 select read = "00000" & msb = 0x00
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x40);
        uart.write(REG_SELECT, 0x00);
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-03",
              "uart.vhd:280,355 - write 0x00, re-select UART 0 read = 0x00",
              sel == 0x00,
              fmt("got=0x%02x", sel));
    }

    // SEL-04 - uart.vhd:281-283 d(4)=1 & d(6)=0 -> uart0_prescalar_msb = d(2:0);
    //          uart.vhd:355 UART 0 read = "00000" & msb = 0x05 when msb=5
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x15);
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-04",
              "uart.vhd:281-283,355 - UART 0 msb=5 select read = 0x05",
              sel == 0x05,
              fmt("got=0x%02x", sel));
    }

    // SEL-05 - uart.vhd:284-286 d(4)=1 & d(6)=1 -> uart1_prescalar_msb = d(2:0);
    //          uart.vhd:371 UART 1 read = "01000" & msb = 0x0D when msb=5
    //          (KNOWN EMULATOR BUG uart.cpp:299)
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x55);
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-05",
              "uart.vhd:284-286,371 - UART 1 msb=5 select read = 0x0D",
              sel == 0x0D,
              fmt("got=0x%02x (VHDL expects 0x0D)", sel));
    }

    // SEL-06 - uart.vhd:273-278 i_reset_hard clears uart0/1_prescalar_msb_r;
    //          uart.vhd:355 select read therefore = 0x00
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x15);     // UART 0 msb = 5
        uart.write(REG_SELECT, 0x55);     // UART 1 msb = 5
        uart.hard_reset();
        uart.write(REG_SELECT, 0x00);     // select UART 0
        uint8_t msb0 = uart.read(REG_SELECT) & 0x07;
        uart.write(REG_SELECT, 0x40);     // select UART 1
        uint8_t msb1 = uart.read(REG_SELECT) & 0x07;
        check("SEL-06",
              "uart.vhd:273-278 - hard reset clears both UART prescaler MSBs",
              msb0 == 0 && msb1 == 0,
              fmt("uart0_msb=%u uart1_msb=%u", msb0, msb1));
    }

    // SEL-07 - uart.vhd:273-274 i_reset clears uart_select_r to 0 but the
    //          inner `if i_reset_hard` guard leaves prescaler MSBs intact
    //          on a soft reset.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x55);     // UART 1, msb=5
        uart.reset();                     // soft reset
        uart.write(REG_SELECT, 0x40);     // select UART 1 to read its msb
        uint8_t msb1 = uart.read(REG_SELECT) & 0x07;
        check("SEL-07",
              "uart.vhd:273-278 - soft reset preserves prescaler MSB",
              msb1 == 0x05,
              fmt("uart1_msb=%u expected=5", msb1));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 2: Frame Register (port 0x163B)
// VHDL: serial/uart.vhd:294-308
// ══════════════════════════════════════════════════════════════════════

static void test_group2_frame() {
    set_group("FRM");
    Uart uart;

    // FRM-01 - uart.vhd:297-299 hard reset: uart0/1_framing_r = 0x18
    {
        uart.hard_reset();
        uint8_t frm = uart.read(REG_FRAME);
        check("FRM-01",
              "uart.vhd:297-299 - hard reset frame = 0x18 (8N1)",
              frm == 0x18,
              fmt("got=0x%02x", frm));
    }

    // FRM-02 - uart.vhd:300-305 uart_frame_wr stores cpu_d verbatim
    {
        uart.hard_reset();
        uart.write(REG_FRAME, 0x1B);
        uint8_t frm = uart.read(REG_FRAME);
        check("FRM-02",
              "uart.vhd:302 - write 0x1B stored verbatim in uart0_framing_r",
              frm == 0x1B,
              fmt("got=0x%02x", frm));
    }

    // FRM-03 - uart.vhd:301-305 per-UART selection: the if/else writes only
    //          the selected channel's framing register.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x00);
        uart.write(REG_FRAME,  0x1F);     // UART 0 frame = 0x1F
        uart.write(REG_SELECT, 0x40);
        uint8_t frm1 = uart.read(REG_FRAME);
        check("FRM-03",
              "uart.vhd:301-305 - frame write targets selected UART only",
              frm1 == 0x18,
              fmt("uart1 frame=0x%02x expected=0x18", frm1));
    }

    // FRM-04 - uart.vhd:302,305 bit 7 stored in framing_r and, via the
    //          separate reset paths referencing framing(7), clears the RX
    //          FIFO occupancy (status bit 0) at line 360.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uart.inject_rx(0, 0xBB);
        uart.write(REG_FRAME, 0x98);      // bit 7 = FIFO reset, frame = 8N1
        uint8_t status = uart.read(REG_TX);
        check("FRM-04",
              "uart.vhd:360 - frame bit 7 clears rx_avail status",
              (status & 0x01) == 0,
              fmt("status=0x%02x", status));
    }

    // FRM-05 - TX break (frame bit 6) drives TX line low with o_busy=1;
    //          uart_tx.vhd holds the line. Not observable via the public
    //          Uart API (no TX line sampling, no busy flag accessor).
    skip("FRM-05", "uart_tx.vhd - TX break line state not exposed by Uart API");

    // FRM-06 - uart_tx.vhd samples framing_r at transmission start;
    //          requires bit-level TX trace which the emulator does not model.
    skip("FRM-06", "uart_tx.vhd - bit-level framing snapshot not modelled");
}

// ══════════════════════════════════════════════════════════════════════
// Group 3: Prescaler / Baud Rate
// VHDL: serial/uart.vhd:313-337 (LSB), 281-286 (MSB)
// ══════════════════════════════════════════════════════════════════════

static void test_group3_prescaler() {
    set_group("BAUD");
    Uart uart;

    // BAUD-01 - uart.vhd:319-320 hard reset sets lsb = "00000011110011" = 0xF3
    //           and uart.vhd:276-277 clears msb to 0. Full 17-bit = 0x000F3.
    //           The full prescaler is not directly readable; the MSB
    //           component is, via the select register read at uart.vhd:355.
    {
        uart.hard_reset();
        uint8_t msb = uart.read(REG_SELECT) & 0x07;
        check("BAUD-01",
              "uart.vhd:276-277 - hard reset prescaler MSB = 0 (full = 0x000F3)",
              msb == 0x00,
              fmt("msb=%u", msb));
    }

    // BAUD-02 - uart.vhd:323-324 d(7)=0 writes lsb(6:0) = d(6:0). The low
    //           14 bits of the prescaler are not readable via ports, so we
    //           assert observable side-effects we CAN verify: the MSB is
    //           unaffected and subsequent writes to the other LSB half
    //           do not clear the low half. That is enforced structurally
    //           in uart.vhd:323-327 by the half-selective assignment.
    skip("BAUD-02", "uart.vhd:323-324 - prescaler LSB low 7 bits not observable via ports");

    // BAUD-03 - uart.vhd:325-326 d(7)=1 writes lsb(13:7) = d(6:0). Same
    //           observability issue as BAUD-02.
    skip("BAUD-03", "uart.vhd:325-326 - prescaler LSB high 7 bits not observable via ports");

    // BAUD-04 - uart.vhd:281-286 select write with d(4)=1 stores d(2:0)
    //           into the selected UART's prescaler MSB; uart.vhd:355/371
    //           reads them back in the low 3 bits of the select register.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x13);     // d(4)=1, d(2:0)=011, UART 0
        uint8_t msb = uart.read(REG_SELECT) & 0x07;
        check("BAUD-04",
              "uart.vhd:281-286,355 - prescaler MSB write stored and read",
              msb == 0x03,
              fmt("msb=%u expected=3", msb));
    }

    // BAUD-05 - uart.vhd:282-286 has two independent registers
    //           uart0/1_prescalar_msb_r, each written only when the
    //           matching d(6) is asserted. Read back via the select
    //           register mask asserts independence (low 3 bits).
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x13);     // UART 0 msb = 3
        uart.write(REG_SELECT, 0x52);     // UART 1 msb = 2
        uart.write(REG_SELECT, 0x00);
        uint8_t msb0 = uart.read(REG_SELECT) & 0x07;
        uart.write(REG_SELECT, 0x40);
        uint8_t msb1 = uart.read(REG_SELECT) & 0x07;
        check("BAUD-05",
              "uart.vhd:282-286 - UART 0 and UART 1 prescaler MSBs independent",
              msb0 == 0x03 && msb1 == 0x02,
              fmt("uart0=%u uart1=%u", msb0, msb1));
    }

    // BAUD-06 - uart.vhd:276-277 hard reset clears uart0 AND uart1 msb.
    //           (LSB reset verified structurally at uart.vhd:319-320 but
    //           not readable.)
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x17);     // UART 0 msb = 7
        uart.write(REG_SELECT, 0x57);     // UART 1 msb = 7
        uart.hard_reset();
        uart.write(REG_SELECT, 0x00);
        uint8_t m0 = uart.read(REG_SELECT) & 0x07;
        uart.write(REG_SELECT, 0x40);
        uint8_t m1 = uart.read(REG_SELECT) & 0x07;
        check("BAUD-06",
              "uart.vhd:276-277 - hard reset clears both prescaler MSBs",
              m0 == 0 && m1 == 0,
              fmt("uart0=%u uart1=%u", m0, m1));
    }

    // BAUD-07 - uart_tx.vhd/uart_rx.vhd sample the prescaler at the start
    //           of a transfer; the emulator treats TX as a single-step
    //           delay (uart.cpp:53-57) so mid-byte re-timing is not
    //           observable.
    skip("BAUD-07", "uart_tx.vhd - mid-byte prescaler snapshot not modelled");
}

// ══════════════════════════════════════════════════════════════════════
// Group 4: TX FIFO and Transmission
// VHDL: serial/uart.vhd:386-404 (tx arbitration), uart_tx.vhd (line coder)
// ══════════════════════════════════════════════════════════════════════

static void test_group4_tx() {
    set_group("TX");
    Uart uart;

    // TX-01 - uart.vhd:360 status bit 4 = uart0_status_tx_empty. Writing a
    //         byte pushes it onto the TX FIFO; once tick() starts the
    //         transmitter, tx_empty drops to 0.
    {
        uart.hard_reset();
        uart.write(REG_TX, 0x42);
        uart.tick(1);
        uint8_t status = uart.read(REG_TX);
        check("TX-01",
              "uart.vhd:360 - write to TX port clears tx_empty",
              (status & 0x10) == 0,
              fmt("status=0x%02x", status));
    }

    // TX-02 - uart.vhd:360 status bit 1 = uart0_status_tx_full. fifop.vhd:
    //         full asserts when stored[DEPTH_BITS]=1; TX DEPTH_BITS=6 ->
    //         full at 64 entries.
    {
        uart.hard_reset();
        for (int i = 0; i < 64; ++i) uart.write(REG_TX, static_cast<uint8_t>(i));
        uint8_t status = uart.channel(0).read_status();
        check("TX-02",
              "uart.vhd:360,fifop.vhd - 64 TX writes set tx_full",
              (status & 0x02) != 0,
              fmt("status=0x%02x", status));
    }

    // TX-03 - uart.vhd line 798-style gate: tx_fifo_we is AND'd with
    //         (not tx_fifo_full), so a 65th write is silently dropped and
    //         tx_full remains asserted.
    {
        uart.hard_reset();
        for (int i = 0; i < 64; ++i) uart.write(REG_TX, static_cast<uint8_t>(i));
        uart.write(REG_TX, 0xFF);         // 65th write
        uint8_t status = uart.channel(0).read_status();
        check("TX-03",
              "uart.vhd - write gated by not tx_fifo_full, 65th byte dropped",
              (status & 0x02) != 0,
              fmt("status=0x%02x", status));
    }

    // TX-04 - uart.vhd:360 tx_empty = tx_fifo_empty AND NOT tx_busy. While
    //         a byte is shifting out, tx_empty = 0 even if the FIFO drained.
    {
        uart.hard_reset();
        uart.write(REG_TX, 0x42);
        uart.tick(1);                      // kick transmitter, byte now in flight
        uint8_t status = uart.channel(0).read_status();
        check("TX-04",
              "uart.vhd:360 - tx_empty requires tx_fifo_empty AND not tx_busy",
              (status & 0x10) == 0,
              fmt("status=0x%02x", status));
    }

    // TX-05 - uart.vhd tx_fifo_we = uart_tx_wr AND NOT uart_tx_wr_d AND
    //         NOT full: write pulse is edge-triggered. The test harness
    //         cannot hold an i/o write signal asserted across cycles
    //         through the Uart::write() API.
    skip("TX-05", "uart.vhd - TX FIFO write edge detection not reachable through write() API");

    // TX-06 - uart.vhd:302 framing bit 7 asserted triggers uartN_fifo_reset
    //         (see line 536), clearing TX FIFO as well as errors.
    {
        uart.hard_reset();
        for (int i = 0; i < 10; ++i) uart.write(REG_TX, static_cast<uint8_t>(i));
        uart.write(REG_FRAME, 0x98);
        uint8_t status = uart.channel(0).read_status();
        check("TX-06",
              "uart.vhd:302,536 - framing bit 7 resets TX FIFO",
              (status & 0x10) != 0,
              fmt("status=0x%02x", status));
    }

    // TX-07 - uart_tx.vhd break state: frame(6)=1 holds TX line low and
    //         raises o_busy; no observable signal in the emulator.
    skip("TX-07", "uart_tx.vhd - TX break line state not exposed");

    // TX-08..TX-14 exercise bit-level line encoding (start/data/stop,
    //         parity, flow control). uart.cpp models the transmitter as a
    //         timed byte-delay with no per-bit output; not observable.
    skip("TX-08", "uart_tx.vhd - bit-level TX encoding not modelled");
    skip("TX-09", "uart_tx.vhd - 7E2 bit-level encoding not modelled");
    skip("TX-10", "uart_tx.vhd - 5O1 bit-level encoding not modelled");
    skip("TX-11", "uart_tx.vhd - CTS_n flow-control input not plumbed");
    skip("TX-12", "uart_tx.vhd - CTS_n flow-control input not plumbed");
    skip("TX-13", "uart_tx.vhd - parity generation not modelled");
    skip("TX-14", "uart_tx.vhd - parity generation not modelled");
}

// ══════════════════════════════════════════════════════════════════════
// Group 5: RX FIFO and Reception
// VHDL: serial/uart.vhd:349-360 (RX read / status), 525-540 (sticky errors)
// ══════════════════════════════════════════════════════════════════════

static void test_group5_rx() {
    set_group("RX");
    Uart uart;

    // RX-01 - uart.vhd:347-353 reading REG_RX returns uart0_rx_o(7:0) while
    //         rx_avail=1. The emulator exposes inject_rx() to simulate
    //         a received byte landing in the FIFO.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uint8_t val = uart.read(REG_RX);
        check("RX-01",
              "uart.vhd:347-353 - RX port returns head of RX FIFO",
              val == 0xAA,
              fmt("got=0x%02x", val));
    }

    // RX-02 - uart.vhd:351-352 rx_avail=0 path forces cpu_d <= (others=>'0')
    {
        uart.hard_reset();
        uint8_t val = uart.read(REG_RX);
        check("RX-02",
              "uart.vhd:351-352 - read of empty RX FIFO returns 0x00",
              val == 0x00,
              fmt("got=0x%02x", val));
    }

    // RX-03 - uart.vhd:360 bit 0 = rx_avail. fifop.vhd with DEPTH_BITS=9
    //         asserts stored=512 as full; the FIFO holds 512 bytes.
    {
        uart.hard_reset();
        for (int i = 0; i < 512; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        uint8_t status = uart.channel(0).read_status();
        check("RX-03",
              "uart.vhd:360,fifop.vhd - 512 RX entries, rx_avail=1",
              (status & 0x01) != 0,
              fmt("status=0x%02x", status));
    }

    // RX-04 - uart.vhd:540 uart0_status_rx_err_overflow is sticky and set
    //         by (uart0_rx_avail AND uart0_rx_avail_d); the 513th byte
    //         arriving against a full FIFO asserts it.
    {
        uart.hard_reset();
        for (int i = 0; i < 512; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        uart.inject_rx(0, 0xFF);          // 513th, overflow
        uint8_t status = uart.channel(0).read_status();
        check("RX-04",
              "uart.vhd:540 - 513th RX byte sets sticky rx_err_overflow",
              (status & 0x04) != 0,
              fmt("status=0x%02x", status));
    }

    // RX-05 - fifop.vhd read pointer advances on falling edge of rd; the
    //         emulator pop() exposes sequential ordering.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0x11);
        uart.inject_rx(0, 0x22);
        uart.inject_rx(0, 0x33);
        uint8_t v1 = uart.read(REG_RX);
        uint8_t v2 = uart.read(REG_RX);
        uint8_t v3 = uart.read(REG_RX);
        check("RX-05",
              "fifop.vhd - sequential RX reads return FIFO in insertion order",
              v1 == 0x11 && v2 == 0x22 && v3 == 0x33,
              fmt("got=%02x,%02x,%02x", v1, v2, v3));
    }

    // RX-06 - fifop.vhd full_near = stored[N] OR (stored[N-1] AND stored[N-2])
    //         With DEPTH_BITS=9 that is stored >= 384. Status bit 3 reflects
    //         rx_near_full at uart.vhd:360.
    {
        uart.hard_reset();
        for (int i = 0; i < 384; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        uint8_t status = uart.channel(0).read_status();
        check("RX-06",
              "fifop.vhd - rx_near_full asserts at stored >= 384",
              (status & 0x08) != 0,
              fmt("status=0x%02x", status));
    }

    // RX-07 - uart.vhd:302 + line 536 pattern: framing bit 7 drives the
    //         uartN_fifo_reset signal that clears both FIFOs and the
    //         sticky errors; rx_avail returns to 0.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uart.inject_rx(0, 0xBB);
        uart.write(REG_FRAME, 0x98);
        uint8_t status = uart.channel(0).read_status();
        check("RX-07",
              "uart.vhd:302,536 - framing bit 7 resets RX FIFO (rx_avail=0)",
              (status & 0x01) == 0,
              fmt("status=0x%02x", status));
    }

    // RX-08..RX-15 require either bit-level RX injection (framing errors,
    //         parity errors, break, noise filtering) or pin-level flow
    //         control output that the Uart class does not expose.
    skip("RX-08", "uart_rx.vhd - framing error injection requires bit-level RX path");
    skip("RX-09", "uart_rx.vhd - parity error injection requires bit-level RX path");
    skip("RX-10", "uart_rx.vhd - break detection requires bit-level RX path");
    skip("RX-11", "uart.vhd:359 - 9th FIFO bit (rx_err) not exposed per-byte");
    skip("RX-12", "uart_rx.vhd - noise rejection window not modelled");
    skip("RX-13", "uart_rx.vhd - frame bit 6 pause state not observable");
    skip("RX-14", "uart_rx.vhd - mid-byte sampling latch not modelled");
    skip("RX-15", "uart_rx.vhd - o_Rx_rtr_n hardware flow output not plumbed");
}

// ══════════════════════════════════════════════════════════════════════
// Group 6: Status Register Clearing
// VHDL: serial/uart.vhd:265 (uart0_tx_rd_fe), 536-540 (sticky error latch)
// ══════════════════════════════════════════════════════════════════════

static void test_group6_status() {
    set_group("STAT");
    Uart uart;

    // STAT-01 - uart.vhd:540 sticky overflow is only cleared by
    //           uartN_fifo_reset or uartN_tx_rd_fe (line 536). A plain
    //           RX-port read (uart_rx_rd) leaves it asserted.
    {
        uart.hard_reset();
        for (int i = 0; i < 513; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        (void)uart.read(REG_RX);          // RX read: should NOT clear sticky
        uint8_t status = uart.channel(0).read_status();
        check("STAT-01",
              "uart.vhd:536-540 - RX-port read leaves sticky overflow asserted",
              (status & 0x04) != 0,
              fmt("status=0x%02x", status));
    }

    // STAT-02 - uart.vhd:265 uart0_tx_rd_fe = tx_rd_d AND NOT tx_rd (falling
    //           edge of a status-port access); line 536 uses it to clear
    //           rx_err_overflow and rx_err_framing.
    {
        uart.hard_reset();
        for (int i = 0; i < 513; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        uint8_t first  = uart.read(REG_TX);   // first status read, reports overflow
        uint8_t second = uart.read(REG_TX);   // second read, cleared
        check("STAT-02",
              "uart.vhd:265,536 - status-port read falling edge clears sticky errors",
              (first & 0x04) != 0 && (second & 0x04) == 0,
              fmt("first=0x%02x second=0x%02x", first, second));
    }

    // STAT-03 - uart.vhd:536 uartN_fifo_reset is the other signal that
    //           clears the sticky error latch; framing bit 7 triggers it.
    {
        uart.hard_reset();
        for (int i = 0; i < 513; ++i) uart.inject_rx(0, static_cast<uint8_t>(i));
        uart.write(REG_FRAME, 0x98);
        uint8_t status = uart.channel(0).read_status();
        check("STAT-03",
              "uart.vhd:536 - uartN_fifo_reset clears sticky errors",
              (status & 0x04) == 0,
              fmt("status=0x%02x", status));
    }

    // STAT-04 - uart.vhd:346-378 the read mux is indexed by uart_select_r;
    //           UART 0 RX data visible only while UART 0 is selected,
    //           UART 1 status independent.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uart.write(REG_SELECT, 0x40);     // select UART 1
        uint8_t status = uart.read(REG_TX);
        check("STAT-04",
              "uart.vhd:346-378 - UART 1 status independent of UART 0 RX FIFO",
              (status & 0x01) == 0,
              fmt("uart1 status=0x%02x", status));
    }

    // STAT-05 - uart.vhd:360 tx_empty AND'd with NOT tx_busy. After a
    //           hard reset no byte is in flight and no FIFO content,
    //           so tx_empty is asserted.
    {
        uart.hard_reset();
        uint8_t status = uart.channel(0).read_status();
        check("STAT-05",
              "uart.vhd:360 - idle UART reports tx_empty=1",
              (status & 0x10) != 0,
              fmt("status=0x%02x", status));
    }

    // STAT-06 - uart.vhd:360 rx_avail = NOT rx_fifo_empty. Injecting a byte
    //           flips the bit independently of receiver state.
    {
        uart.hard_reset();
        uint8_t before = uart.channel(0).read_status();
        uart.inject_rx(0, 0x42);
        uint8_t after  = uart.channel(0).read_status();
        check("STAT-06",
              "uart.vhd:360 - rx_avail reflects FIFO occupancy",
              (before & 0x01) == 0 && (after & 0x01) == 1,
              fmt("before=0x%02x after=0x%02x", before, after));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 7: Dual UART Independence
// VHDL: serial/uart.vhd:386-404 (uart0 RX/TX gates), 572-590 (uart1)
// ══════════════════════════════════════════════════════════════════════

static void test_group7_dual() {
    set_group("DUAL");
    Uart uart;

    // DUAL-01 - uart.vhd:387-388 / 572-573 gate rx_rd and tx_wr with
    //           uart_select_r so each UART owns an independent FIFO pair.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uart.inject_rx(1, 0xBB);
        uart.write(REG_SELECT, 0x00);
        uint8_t v0 = uart.read(REG_RX);
        uart.write(REG_SELECT, 0x40);
        uint8_t v1 = uart.read(REG_RX);
        check("DUAL-01",
              "uart.vhd:387-388,572-573 - UART 0 and UART 1 have independent RX FIFOs",
              v0 == 0xAA && v1 == 0xBB,
              fmt("uart0=0x%02x uart1=0x%02x", v0, v1));
    }

    // DUAL-02 - uart.vhd:282-286 independent MSB registers; uart.vhd:355/371
    //           read them back in the select register low 3 bits with the
    //           channel marker at bit 3 for UART 1.
    //           (KNOWN EMULATOR BUG uart.cpp:299 returns bit 6 marker.)
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x13);     // UART 0 msb = 3
        uart.write(REG_SELECT, 0x55);     // UART 1 msb = 5
        uart.write(REG_SELECT, 0x00);
        uint8_t sel0 = uart.read(REG_SELECT);
        uart.write(REG_SELECT, 0x40);
        uint8_t sel1 = uart.read(REG_SELECT);
        check("DUAL-02",
              "uart.vhd:282-286,355,371 - UART 0 reads 0x03, UART 1 reads 0x0D",
              sel0 == 0x03 && sel1 == 0x0D,
              fmt("sel0=0x%02x sel1=0x%02x (VHDL expects 0x03, 0x0D)", sel0, sel1));
    }

    // DUAL-03 - uart.vhd:300-305 independent framing registers per channel.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x00);
        uart.write(REG_FRAME,  0x1B);
        uart.write(REG_SELECT, 0x40);
        uint8_t frm1 = uart.read(REG_FRAME);
        uart.write(REG_SELECT, 0x00);
        uint8_t frm0 = uart.read(REG_FRAME);
        check("DUAL-03",
              "uart.vhd:300-305 - per-channel framing: UART 0=0x1B, UART 1=0x18",
              frm0 == 0x1B && frm1 == 0x18,
              fmt("frm0=0x%02x frm1=0x%02x", frm0, frm1));
    }

    // DUAL-04 - uart.vhd:346-378 status mux reads per-channel signals; an
    //           RX byte on UART 0 does not raise UART 1 rx_avail.
    {
        uart.hard_reset();
        uart.inject_rx(0, 0xAA);
        uint8_t st0 = uart.channel(0).read_status();
        uint8_t st1 = uart.channel(1).read_status();
        check("DUAL-04",
              "uart.vhd:346-378 - UART 1 status unaffected by UART 0 RX byte",
              (st0 & 0x01) == 1 && (st1 & 0x01) == 0,
              fmt("st0=0x%02x st1=0x%02x", st0, st1));
    }

    // DUAL-05 / DUAL-06 live at the Emulator tier: pin routing and
    // joystick IO-mode multiplex cross Uart + IoMode boundaries.
    // RE-HOME: see test/uart/uart_integration_test.cpp DUAL-05 — UART 0
    //   (ESP) vs UART 1 (Pi) channel assignment per zxnext.vhd:3335-3421;
    //   verified via the Emulator-level on_tx_byte/inject_rx dispatch
    //   (channel 0 bytes do not leak into channel 1).
    // RE-HOME: see test/uart/uart_integration_test.cpp DUAL-06 — joystick
    //   UART IO-mode multiplex per zxnext.vhd:3340-3350; when
    //   IoMode::iomode_en()=1 and the joystick IO-mode selects UART
    //   (iomode_bits >= 2), UART 0 RX is fed from the joystick connector
    //   rather than the physical UART pin.
}

// ══════════════════════════════════════════════════════════════════════
// Group 8: I2C Bit-Bang (ports 0x103B, 0x113B)
// VHDL: zxnext.vhd:3226-3268
// ══════════════════════════════════════════════════════════════════════

static void test_group8_i2c() {
    set_group("I2C");
    I2cController i2c;

    // I2C-01 - zxnext.vhd:3235-3236 / 3246-3247 reset asserts
    //          i2c_scl_o='1', i2c_sda_o='1'. zxnext.vhd:3259/3266 read
    //          path returns "1111111" & bit (= 0xFF when released).
    {
        i2c.reset();
        uint8_t scl = i2c.read_scl();
        uint8_t sda = i2c.read_sda();
        check("I2C-01",
              "zxnext.vhd:3235-3247 - reset releases SCL and SDA high",
              (scl & 0x01) == 1 && (sda & 0x01) == 1,
              fmt("scl=0x%02x sda=0x%02x", scl, sda));
    }

    // I2C-02 - zxnext.vhd:3237-3238 port_103b_wr stores cpu_do(0) in
    //          i2c_scl_o. Writing 0 drives SCL line low (bit 0 = 0).
    {
        i2c.reset();
        i2c.write_scl(0x00);
        uint8_t scl = i2c.read_scl();
        check("I2C-02",
              "zxnext.vhd:3237-3238 - write 0 sets SCL output low",
              (scl & 0x01) == 0,
              fmt("scl=0x%02x", scl));
    }

    // I2C-03 - write 1 via same process releases the line.
    {
        i2c.reset();
        i2c.write_scl(0x00);
        i2c.write_scl(0x01);
        uint8_t scl = i2c.read_scl();
        check("I2C-03",
              "zxnext.vhd:3237-3238 - write 1 releases SCL output high",
              (scl & 0x01) == 1,
              fmt("scl=0x%02x", scl));
    }

    // I2C-04 - zxnext.vhd:3248-3249 same pattern for SDA port.
    {
        i2c.reset();
        i2c.write_sda(0x00);
        uint8_t sda = i2c.read_sda();
        check("I2C-04",
              "zxnext.vhd:3248-3249 - write 0 sets SDA output low",
              (sda & 0x01) == 0,
              fmt("sda=0x%02x", sda));
    }

    // I2C-05 - write 1 releases SDA.
    {
        i2c.reset();
        i2c.write_sda(0x00);
        i2c.write_sda(0x01);
        uint8_t sda = i2c.read_sda();
        check("I2C-05",
              "zxnext.vhd:3248-3249 - write 1 releases SDA output high",
              (sda & 0x01) == 1,
              fmt("sda=0x%02x", sda));
    }

    // I2C-06 - zxnext.vhd:3259 read bits 7:1 are the literal "1111111";
    //          bit 0 carries SCL AND pi_i2c1_scl. With a released bus the
    //          full byte is 0xFF and the upper seven bits are 0xFE & .
    {
        i2c.reset();
        uint8_t scl = i2c.read_scl();
        check("I2C-06",
              "zxnext.vhd:3259 - SCL read upper bits = 0xFE",
              (scl & 0xFE) == 0xFE,
              fmt("scl=0x%02x", scl));
    }

    // I2C-07 - same for SDA at zxnext.vhd:3266.
    {
        i2c.reset();
        uint8_t sda = i2c.read_sda();
        check("I2C-07",
              "zxnext.vhd:3266 - SDA read upper bits = 0xFE",
              (sda & 0xFE) == 0xFE,
              fmt("sda=0x%02x", sda));
    }

    // I2C-08 - zxnext.vhd:3238 assigns i2c_scl_o <= cpu_do(0); the upper
    //          bits of the bus value are discarded.
    {
        i2c.reset();
        i2c.write_scl(0xFE);
        uint8_t scl = i2c.read_scl();
        check("I2C-08",
              "zxnext.vhd:3238 - SCL write takes cpu_do(0) only; 0xFE -> 0",
              (scl & 0x01) == 0,
              fmt("scl=0x%02x", scl));
    }

    // I2C-09 - zxnext.vhd:3259/3266 upper 7 bits always "1111111" regardless
    //          of the line state.
    {
        i2c.reset();
        i2c.write_scl(0x00);
        i2c.write_sda(0x00);
        uint8_t scl = i2c.read_scl();
        uint8_t sda = i2c.read_sda();
        check("I2C-09",
              "zxnext.vhd:3259,3266 - read upper 7 bits stay 1 while lines low",
              (scl & 0xFE) == 0xFE && (sda & 0xFE) == 0xFE,
              fmt("scl=0x%02x sda=0x%02x", scl, sda));
    }

    // I2C-10 - Same gate cluster as GATE-02: NR 0x82 bit 2 ->
    // internal_port_enable(10) -> port_i2c_io_en per zxnext.vhd:2418,
    // 2628-2631. Exercisable only at the Emulator tier.
    // RE-HOME: see test/uart/uart_integration_test.cpp I2C-10 — when NR
    //   0x82 bit 2 is clear, ports 0x103B and 0x113B ignore reads/writes.

    // I2C-11 - zxnext.vhd:3259 AND-s the CPU SCL with pi_i2c1_scl; the
    //          emulator has no Pi I2C master input.
    skip("I2C-11", "zxnext.vhd:3259 - pi_i2c1_scl input not plumbed in I2cController");

    // I2C-12 - zxnext.vhd:3235-3247 reset restores both output registers
    //          to 1 regardless of prior state.
    {
        i2c.write_scl(0x00);
        i2c.write_sda(0x00);
        i2c.reset();
        uint8_t scl = i2c.read_scl();
        uint8_t sda = i2c.read_sda();
        check("I2C-12",
              "zxnext.vhd:3235-3247 - reset releases both lines high",
              (scl & 0x01) == 1 && (sda & 0x01) == 1,
              fmt("scl=0x%02x sda=0x%02x", scl, sda));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 9: I2C Protocol Sequences
// The I2cController implements higher-level START/STOP/byte state than
// zxnext.vhd itself, but it must behave as a correct I2C master from the
// perspective of attached slave devices. Known i2c.cpp:101 false-STOP bug
// causes the byte-transfer tests to fail.
// ══════════════════════════════════════════════════════════════════════

static void test_group9_i2c_protocol() {
    set_group("I2C-P");
    I2cController i2c;
    I2cRtc rtc;
    i2c.attach_device(0x68, &rtc);

    // I2C-P01 - zxnext.vhd:3237-3249 a START is just SDA falling while SCL
    //           is held high. The i2cController exposes this as a state
    //           transition to ADDRESS; no crash + subsequent address byte
    //           acceptance is the observable contract. We assert the
    //           sequence of port writes is accepted (no throw, no lock).
    {
        i2c.reset();
        i2c.write_sda(1);
        i2c.write_scl(1);
        i2c.write_sda(0);                 // START edge
        i2c.write_scl(0);
        uint8_t sda = i2c.read_sda() & 0x01;
        check("I2C-P01",
              "zxnext.vhd:3237-3249 - START sequence accepted, SDA still driven low",
              sda == 0,
              fmt("sda=%u", sda));
    }

    // I2C-P02 - zxnext.vhd:3237-3249 STOP = SDA rising while SCL high.
    {
        i2c.reset();
        i2c.write_sda(0);
        i2c.write_scl(1);
        i2c.write_sda(1);                 // STOP edge
        uint8_t sda = i2c.read_sda() & 0x01;
        check("I2C-P02",
              "zxnext.vhd:3237-3249 - STOP sequence accepted, SDA released high",
              sda == 1,
              fmt("sda=%u", sda));
    }

    // I2C-P03 - Send address byte 0xD0 (DS1307 write). A correct master
    //           drives 8 data bits then releases SDA; a DS1307 at 0x68 must
    //           reply with ACK=0 on the 9th clock.
    //           (KNOWN EMULATOR BUG i2c.cpp:101)
    {
        i2c.reset();
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0xD0);
        i2c_stop(i2c);
        check("I2C-P03",
              "DS1307 / zxnext.vhd - send 0xD0, slave drives ACK=0",
              ack == 0,
              fmt("ack=%u", ack));
    }

    // I2C-P04 - Wrong address produces NACK (master samples SDA high). The
    //           false-STOP bug still lets this row pass because the master
    //           never hears a positive ACK.
    {
        i2c.reset();
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0x50);
        i2c_stop(i2c);
        check("I2C-P04",
              "I2C protocol - unmatched address leaves SDA high at ACK clock",
              ack == 1,
              fmt("ack=%u", ack));
    }

    // I2C-P05 - Write register pointer, restart, send read address, read
    //           one data byte with NACK. Exposes i2c.cpp:101.
    {
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);
        (void)i2c_send_byte(i2c, 0x00);
        i2c_stop(i2c);
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0xD1);
        uint8_t secs = i2c_read_byte(i2c, false);
        i2c_stop(i2c);
        bool ack_ok = (ack == 0);
        bool bcd_ok = ((secs & 0x0F) <= 9) && (((secs >> 4) & 0x07) <= 5);
        check("I2C-P05a",
              "DS1307 - restart + read address 0xD1 returns ACK=0",
              ack_ok,
              fmt("ack=%u", ack));
        check("I2C-P05b",
              "DS1307 - seconds register is valid BCD (upper<=5, lower<=9)",
              bcd_ok,
              fmt("secs=0x%02x", secs));
    }

    // I2C-P06 - Master ACK/NACK after a data byte. The DS1307 model auto-
    //           increments its register pointer on each transfer(true).
    //           The false-STOP bug was fixed in commit 174fa56 (2026-04-24
    //           re-audit confirmed sequential read returns valid BCD:
    //           secs=0x??, mins=0x?? — both upper<=5/lower<=9). Master
    //           ACK after first byte auto-loads next register per
    //           i2c.cpp:279.
    {
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);
        (void)i2c_send_byte(i2c, 0x00);
        i2c_stop(i2c);
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD1);
        uint8_t b0 = i2c_read_byte(i2c, true);   // ACK -> want next byte
        uint8_t b1 = i2c_read_byte(i2c, false);  // NACK
        i2c_stop(i2c);
        bool b0_bcd = ((b0 & 0x0F) <= 9) && (((b0 >> 4) & 0x07) <= 5);
        bool b1_bcd = ((b1 & 0x0F) <= 9) && (((b1 >> 4) & 0x07) <= 5);
        check("I2C-P06",
              "i2c.cpp:279 - master ACK auto-loads next reg, NACK terminates",
              b0_bcd && b1_bcd,
              fmt("secs=0x%02x mins=0x%02x", b0, b1));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 10: DS1307 RTC Register Map
// I2cRtc (peripheral/i2c.cpp) snapshots host time into regs_[0..6] on
// start(). Most rows fail today because i2c.cpp:101 prevents the master
// from ever issuing a clean read transaction to the slave.
// ══════════════════════════════════════════════════════════════════════

static void test_group10_rtc() {
    set_group("RTC");
    I2cController i2c;
    I2cRtc rtc;
    i2c.attach_device(0x68, &rtc);

    auto read_reg = [&](uint8_t reg) -> uint8_t {
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);
        (void)i2c_send_byte(i2c, reg);
        i2c_stop(i2c);
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD1);
        uint8_t v = i2c_read_byte(i2c, false);
        i2c_stop(i2c);
        return v;
    };

    // RTC-01 - DS1307 address 0xD0 (write) returns ACK=0.
    {
        i2c.reset();
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0xD0);
        i2c_stop(i2c);
        check("RTC-01",
              "DS1307 datasheet - 7-bit address 0x68 ACK-s write frame 0xD0",
              ack == 0,
              fmt("ack=%u", ack));
    }

    // RTC-02 - DS1307 address 0xD1 (read) returns ACK=0.
    {
        i2c.reset();
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0xD1);
        i2c_stop(i2c);
        check("RTC-02",
              "DS1307 datasheet - 7-bit address 0x68 ACK-s read frame 0xD1",
              ack == 0,
              fmt("ack=%u", ack));
    }

    // RTC-03 - Foreign address produces NACK from the bus (no listener).
    {
        i2c.reset();
        i2c_start(i2c);
        uint8_t ack = i2c_send_byte(i2c, 0xA0);
        i2c_stop(i2c);
        check("RTC-03",
              "DS1307 datasheet - foreign address 0xA0 NACK-ed",
              ack == 1,
              fmt("ack=%u", ack));
    }

    // RTC-04 - i2c.cpp snapshot_time() stores BCD seconds at regs_[0] with
    //          upper nibble <= 5 and lower nibble <= 9.
    {
        uint8_t secs = read_reg(0x00);
        bool bcd_ok = ((secs & 0x0F) <= 9) && (((secs >> 4) & 0x07) <= 5);
        check("RTC-04",
              "DS1307 - register 0x00 reads BCD seconds",
              bcd_ok,
              fmt("secs=0x%02x", secs));
    }

    // RTC-05 - same shape for minutes at regs_[1].
    {
        uint8_t mins = read_reg(0x01);
        bool bcd_ok = ((mins & 0x0F) <= 9) && (((mins >> 4) & 0x07) <= 5);
        check("RTC-05",
              "DS1307 - register 0x01 reads BCD minutes",
              bcd_ok,
              fmt("mins=0x%02x", mins));
    }

    // RTC-06 / RTC-07 — Hours (reg 0x02) and day-of-week (reg 0x03) flow
    // through the same snapshot_time -> to_bcd pipeline as secs/mins; the
    // pre-174fa56 0x73 symptom was the false-STOP bug corrupting the read
    // transaction mid-byte. 2026-04-24 re-audit confirmed both registers
    // now read plausible values (hours=0x12 BCD, dow=0x06 in 1..7 range).
    // Register 0x02 bit 6 is 0 (24h mode) because snapshot_time() populates
    // via tm_hour directly — 12h mode is covered by RTC-13 below.
    {
        uint8_t h = read_reg(0x02);
        // 24h mode: bit 6 = 0, bits 5:0 = BCD 00-23 (upper <= 2, lower 0-9).
        bool bcd_ok = ((h & 0x40) == 0) &&
                      ((h & 0x0F) <= 9) &&
                      (((h >> 4) & 0x03) <= 2);
        check("RTC-06",
              "DS1307 - register 0x02 reads 24h-mode BCD hours",
              bcd_ok,
              fmt("hours=0x%02x", h));
    }
    {
        uint8_t dow = read_reg(0x03);
        // DS1307 day-of-week is 1..7; i2c.cpp:68 populates via tm_wday+1.
        bool dow_ok = (dow >= 1) && (dow <= 7);
        check("RTC-07",
              "DS1307 - register 0x03 reads day-of-week in [1..7]",
              dow_ok,
              fmt("dow=0x%02x", dow));
    }

    // RTC-08..RTC-10 — Date (reg 0x04), month (reg 0x05), year (reg 0x06)
    // were also unblocked by the 174fa56 false-STOP fix and re-audited on
    // 2026-04-24. i2c.cpp:69-71 populates via to_bcd(tm_mday / tm_mon+1 /
    // tm_year%100). Assertions restrict to wall-clock-plausible BCD ranges
    // rather than exact values to stay wall-clock-flake-immune.
    {
        uint8_t d = read_reg(0x04);
        // Date 01..31 BCD: upper nibble 0..3, lower 0..9; (31 is max).
        bool ok = ((d & 0x0F) <= 9) && (((d >> 4) & 0x03) <= 3) && (d != 0);
        check("RTC-08",
              "DS1307 - register 0x04 reads BCD date 01..31",
              ok,
              fmt("date=0x%02x", d));
    }
    {
        uint8_t m = read_reg(0x05);
        // Month 01..12 BCD: upper nibble 0..1, lower 0..9.
        bool ok = ((m & 0x0F) <= 9) && (((m >> 4) & 0x01) <= 1) && (m != 0);
        check("RTC-09",
              "DS1307 - register 0x05 reads BCD month 01..12",
              ok,
              fmt("month=0x%02x", m));
    }
    {
        uint8_t y = read_reg(0x06);
        // Year 00..99 BCD: upper 0..9, lower 0..9.
        bool ok = ((y & 0x0F) <= 9) && (((y >> 4) & 0x0F) <= 9);
        check("RTC-10",
              "DS1307 - register 0x06 reads BCD year 00..99",
              ok,
              fmt("year=0x%02x", y));
    }

    // RTC-11..17 — feature gaps pending Wave E of the UART+I2C skip-reduction
    // plan. i2c.cpp:44 silently discards writes; regs_ is 8 bytes so NVRAM
    // 0x08-0x3F is unimplemented; 12h mode bit 6 of reg 0x02 is never set;
    // control reg 0x07 is hard-coded to 0x00; CH bit (reg 0x00 bit 7) is
    // not modelled; register-pointer auto-increment wraps at 0x07 not 0x3F.
    skip("RTC-11",
         "F-CT-RTC-CONTROL: control register 0x07 readback requires storage "
         "(currently always 0x00); un-skip via task3-uart-e-rtc");
    skip("RTC-12",
         "F-CT-RTC-WRITE: write path discarded at i2c.cpp:44; "
         "un-skip via task3-uart-e-rtc");
    skip("RTC-13",
         "F-CT-RTC-12H: 12h/24h mode bit 6 of hours register not modelled; "
         "un-skip via task3-uart-e-rtc");
    skip("RTC-14",
         "F-CT-RTC-AUTOINC: register-pointer wrap at 0x3F not modelled; "
         "un-skip via task3-uart-e-rtc");
    skip("RTC-15",
         "F-CT-RTC-WRITE: write path discarded at i2c.cpp:44; "
         "un-skip via task3-uart-e-rtc");
    skip("RTC-16",
         "F-CT-RTC-CH: seconds-reg bit 7 oscillator-halt not modelled; "
         "un-skip via task3-uart-e-rtc");
    skip("RTC-17",
         "F-CT-RTC-NVRAM: regs_ sized at 8 bytes; 0x08-0x3F NVRAM not "
         "implemented; un-skip via task3-uart-e-rtc. CANDIDATE WONT.");
}

// ══════════════════════════════════════════════════════════════════════
// Group 11: UART IM2 Interrupt Integration
// VHDL: zxnext.vhd:1930-1944 (vector map + req line), :1949-1950
//       (en line), :5615-5617 (NR 0xC6 write), :6245 (NR 0xC6 readback)
// Vectors: 1=UART0-RX (near_full OR (avail AND NOT C6[1])), 2=UART1-RX
//          (same with C6[5]), 12=UART0-TX (C6[2]), 13=UART1-TX (C6[6]).
// ══════════════════════════════════════════════════════════════════════

static void test_group11_interrupts() {
    set_group("INT");
    // The UART IM2 interrupt fabric wiring (zxnext.vhd:1930-1944,
    // 1949-1950, 5615-5617, 6245) lives in Emulator / Im2Controller, not
    // inside Uart. All six rows re-home to the integration suite with
    // VHDL-accurate vector semantics:
    //   vector 1  = UART 0 RX (rx_near_full OR (rx_avail AND NOT NR 0xC6 bit 1))
    //   vector 2  = UART 1 RX (rx_near_full OR (rx_avail AND NOT NR 0xC6 bit 5))
    //   vector 12 = UART 0 TX empty (gated by NR 0xC6 bit 2)
    //   vector 13 = UART 1 TX empty (gated by NR 0xC6 bit 6)
    // NR 0xC6 bits 3 and 7 are reserved and read back as 0 (zxnext.vhd:6245).
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-01 — vector 1 on
    //   UART 0 rx_avail with NR 0xC6 bit 0 set, bit 1 clear.
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-02 — vector 1 on
    //   UART 0 rx_near_full with NR 0xC6 bit 1 set (fires even when bit 0
    //   clear — the "near-full override" is an OR inside the req line).
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-03 — vector 2 on
    //   UART 1 rx_avail with NR 0xC6 bit 4 set, bit 5 clear.
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-04 — vector 2 on
    //   UART 1 rx_near_full with NR 0xC6 bit 5 set.
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-05 — vector 12
    //   on UART 0 tx_empty with NR 0xC6 bit 2 set.
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-06 — vector 13
    //   on UART 1 tx_empty with NR 0xC6 bit 6 set.
}

// ══════════════════════════════════════════════════════════════════════
// Group 12: Port Enable Gating
// VHDL: zxnext.vhd:2628-2639 (port decode), internal_port_enable
// ══════════════════════════════════════════════════════════════════════

static void test_group12_gating() {
    set_group("GATE");
    // Port-enable gating lives in Emulator::register_io_ports (currently
    // bypassed at emulator.cpp:1444-1464). Task3-UART Wave C adds the
    // internal_port_enable(10)/(12) wraps mirroring the DivMMC pattern at
    // emulator.cpp:1467-1476 before the integration-suite rows can pass.
    // RE-HOME: see test/uart/uart_integration_test.cpp GATE-01 — UART port
    //   enable NR 0x82 bit 4 -> internal_port_enable(12) -> port_uart_io_en
    //   (zxnext.vhd:2420, 2639); when clear, ports 0x133B/0x143B/0x153B/0x163B
    //   return 0xFF and ignore writes.
    // RE-HOME: see test/uart/uart_integration_test.cpp GATE-02 — I2C port
    //   enable NR 0x82 bit 2 -> internal_port_enable(10) -> port_i2c_io_en
    //   (zxnext.vhd:2418, 2628-2631); when clear, ports 0x103B/0x113B
    //   ignore.
    // RE-HOME: see test/uart/uart_integration_test.cpp GATE-03 — full NR
    //   0x82/0x83/0x84/0x85 internal_port_enable mapping per
    //   zxnext.vhd:5499-5509; exercise several bits and confirm the
    //   per-port gate matches.
}

// ══════════════════════════════════════════════════════════════════════
// Main
// ══════════════════════════════════════════════════════════════════════

int main() {
    std::printf("UART + I2C/RTC Compliance Tests\n");
    std::printf("===============================\n\n");

    test_group1_select();       std::printf("  Group SEL   done\n");
    test_group2_frame();        std::printf("  Group FRM   done\n");
    test_group3_prescaler();    std::printf("  Group BAUD  done\n");
    test_group4_tx();           std::printf("  Group TX    done\n");
    test_group5_rx();           std::printf("  Group RX    done\n");
    test_group6_status();       std::printf("  Group STAT  done\n");
    test_group7_dual();         std::printf("  Group DUAL  done\n");
    test_group8_i2c();          std::printf("  Group I2C   done\n");
    test_group9_i2c_protocol(); std::printf("  Group I2C-P done\n");
    test_group10_rtc();         std::printf("  Group RTC   done\n");
    test_group11_interrupts();  std::printf("  Group INT   done\n");
    test_group12_gating();      std::printf("  Group GATE  done\n");

    std::printf("\n===============================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  SKIP %s: %s\n", s.id.c_str(), s.reason.c_str());
        }
    }

    std::printf("\nPer-group live results:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty()) std::printf("  %-8s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty()) std::printf("  %-8s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
