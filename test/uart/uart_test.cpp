// UART + I2C/RTC Compliance Test Runner
//
// Full rewrite (Task 1 Wave 2, 2026-04-15) against
// doc/testing/UART-I2C-TEST-PLAN-DESIGN.md. Every assertion cites a VHDL
// file and line range from the authoritative FPGA source at
//   cores/zxnext/src/
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
//   * uart.cpp:299 select-register read bit 6 vs bit 3 — 47ee7e2 (Task 2
//     item 22) was WRONG IN BOTH DIRECTIONS and is reverted by GH #253. The
//     emulator had it right: uart.vhd:371's "01000" is bits 7 DOWNTO 3 of an
//     8-bit o_cpu_d, so the marker is bit 6 (0x40), as ports.txt:370 states and
//     as the write path already used. 47ee7e2 read the literal as bits 4..0,
//     changed the emulator to 0x08, and rewrote UART-SEL-02, SEL-05 and DUAL-02
//     to match — so those three rows "unblocked" by agreeing with the defect
//     they were derived from, and the suite certified it until 2026-08.
//     Expected values come from the VHDL, never from the C++ (see the ground
//     rules above); this is what it costs when a row is derived from a
//     misreading instead.
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
#include "core/emulator_config.h"   // Task 28 — parse_rtc_datetime

#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
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

// ── Bit-level TX helpers (Wave A) ─────────────────────────────────────
// Configure a channel for bit-level stepping with a chosen frame byte and
// prescaler. `prescaler` is the full 17-bit value; we only use the 14-bit
// LSB half (msb=0) so it must be <= 0x3FFF. A large-enough prescaler keeps
// the TX line stable across two observation ticks per bit, which matches
// VHDL behaviour where tx_shift only updates on tx_timer_expired.
void bitlevel_setup(UartChannel& ch, uint8_t frame, uint16_t prescaler) {
    ch.hard_reset();
    // Clear the default LSB (243) so we can overwrite cleanly.
    // Default lsb = 0x00F3: lower 7 = 0x73, upper 7 = 0x01.
    ch.write_prescaler_lsb(0x80);              // clear upper 7 bits of lsb
    ch.write_prescaler_lsb(0x00);              // clear lower 7 bits of lsb
    // Now lsb = 0. Load new prescaler (14 bits).
    uint8_t lo = static_cast<uint8_t>(prescaler & 0x7F);
    uint8_t hi = static_cast<uint8_t>((prescaler >> 7) & 0x7F);
    ch.write_prescaler_lsb(lo);                // lower 7 bits (bit7=0)
    ch.write_prescaler_lsb(0x80 | hi);         // upper 7 bits (bit7=1)
    ch.write_prescaler_msb(0);                 // MSB=0
    ch.write_frame(frame);
    ch.set_bitlevel_mode(true);
}

// Advance TX engine until it enters `target` state (return true) or until
// max_ticks elapses (return false). Must be called while state != target,
// otherwise returns false immediately (0 ticks).
using TxState = UartChannel::TxState;
bool tick_until_tx_state(UartChannel& ch, TxState target, size_t max_ticks) {
    for (size_t i = 0; i < max_ticks; ++i) {
        ch.tick_one_bit_clock();
        if (ch.tx_state_live() == target) return true;
    }
    return false;
}

// Walk one whole bit-time (prescaler ticks) and sample the TX line at the
// midpoint (half-prescaler in). This matches how a real UART receiver
// samples at mid-bit and is stable against the tx_shift post-shift line
// quirk (bit becomes visible *after* the tx_timer_expired tick).
bool sample_mid_bit(UartChannel& ch, uint16_t prescaler) {
    // Advance prescaler/2 ticks, sample, then advance the remainder.
    uint16_t half = static_cast<uint16_t>(prescaler / 2);
    for (uint16_t i = 0; i < half; ++i) ch.tick_one_bit_clock();
    bool sample = ch.tx_line_out();
    for (uint16_t i = half; i < prescaler; ++i) ch.tick_one_bit_clock();
    return sample;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// Group 1: UART Select Register (port 0x153B)
// VHDL: serial/uart.vhd:268-290 (write), 354-371 (read)
// ══════════════════════════════════════════════════════════════════════

static void test_group1_select() {
    set_group("SEL");
    Uart uart;

    // UART-SEL-01 - uart.vhd:273-278 hard reset: uart_select_r=0, uart0/1 msb=0;
    //          uart.vhd:355 reads "00000" & uart0_prescalar_msb_r
    {
        uart.hard_reset();
        uint8_t sel = uart.read(REG_SELECT);
        check("UART-SEL-01",
              "uart.vhd:273-278,355 - hard reset select read = 0x00",
              sel == 0x00,
              fmt("got=0x%02x", sel));
    }

    // UART-SEL-02 - uart.vhd:280 writes uart_select_r = d(6); uart.vhd:371
    //          UART 1 select read = "01000" & msb. The literal is bits 7 DOWNTO
    //          3 of an 8-bit o_cpu_d (msb is the low 3), so the set bit is bit 6
    //          -> 0x40, matching ports.txt:370 and the write side. GH #253: this
    //          row asserted 0x08 for four months, having read the literal as
    //          bits 4..0, and made the emulator agree with it.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x40);
        uint8_t sel = uart.read(REG_SELECT);
        check("UART-SEL-02",
              "uart.vhd:371 - write 0x40, UART 1 select read = 0x40",
              sel == 0x40,
              fmt("got=0x%02x (VHDL expects 0x40)", sel));
    }

    // UART-SEL-03 - uart.vhd:280 write d(6)=0 restores uart_select_r=0;
    //          uart.vhd:355 UART 0 select read = "00000" & msb = 0x00
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x40);
        uart.write(REG_SELECT, 0x00);
        uint8_t sel = uart.read(REG_SELECT);
        check("UART-SEL-03",
              "uart.vhd:280,355 - write 0x00, re-select UART 0 read = 0x00",
              sel == 0x00,
              fmt("got=0x%02x", sel));
    }

    // UART-SEL-04 - uart.vhd:281-283 d(4)=1 & d(6)=0 -> uart0_prescalar_msb = d(2:0);
    //          uart.vhd:355 UART 0 read = "00000" & msb = 0x05 when msb=5
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x15);
        uint8_t sel = uart.read(REG_SELECT);
        check("UART-SEL-04",
              "uart.vhd:281-283,355 - UART 0 msb=5 select read = 0x05",
              sel == 0x05,
              fmt("got=0x%02x", sel));
    }

    // SEL-05 - uart.vhd:284-286 d(4)=1 & d(6)=1 -> uart1_prescalar_msb = d(2:0);
    //          uart.vhd:371 UART 1 read = "01000" & msb = 0x45 when msb=5
    //          (bit 6 marker | msb 5). See UART-SEL-02 on the bit numbering.
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x55);
        uint8_t sel = uart.read(REG_SELECT);
        check("SEL-05",
              "uart.vhd:284-286,371 - UART 1 msb=5 select read = 0x45",
              sel == 0x45,
              fmt("got=0x%02x (VHDL expects 0x45)", sel));
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

    // SEL-08 - the read-back marker and the write-side select are the SAME bit,
    //          so reading 0x153B and writing the value straight back leaves the
    //          selected channel alone. uart.vhd:280 latches uart_select_r from
    //          d(6); uart.vhd:355/371 report it in bit 6 of o_cpu_d.
    //
    //          This is the round trip a debug stub performs — read the select to
    //          see where the debuggee left it, borrow the port, put it back — and
    //          it is the shape in which GH #253 was actually reported
    //          (dezogif_ng #42). It is the row the other SEL rows could not be:
    //          they compare against a constant, so a wrong constant makes them
    //          agree with a wrong emulator. This one compares the register
    //          against ITSELF across the round trip and holds whatever the
    //          marker bit turns out to be — under the 0x08 defect the read
    //          returns a value whose bit 6 is clear, so writing it back silently
    //          switches a UART 1 session to UART 0 and the next 0x143B read
    //          takes bytes from the wrong channel.
    {
        uart.hard_reset();

        uart.write(REG_SELECT, 0x40);            // select UART 1
        const uint8_t sel1_before = uart.read(REG_SELECT);
        uart.write(REG_SELECT, sel1_before);     // ...and hand it straight back
        const uint8_t sel1_after  = uart.read(REG_SELECT);

        // Same for UART 0, so the row is not merely "writing anything keeps
        // channel 1" — the round trip must be a no-op in both directions.
        uart.write(REG_SELECT, 0x00);
        const uint8_t sel0_before = uart.read(REG_SELECT);
        uart.write(REG_SELECT, sel0_before);
        const uint8_t sel0_after  = uart.read(REG_SELECT);

        check("SEL-08",
              "uart.vhd:280,355,371 - the select read-back is the bit the write "
              "path consumes, so read-then-write-back does not change channel",
              sel1_after == sel1_before && sel0_after == sel0_before,
              fmt("UART1 before=0x%02x after=0x%02x; UART0 before=0x%02x "
                  "after=0x%02x (each pair must match)",
                  sel1_before, sel1_after, sel0_before, sel0_after));
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

    // FRM-05 - uart_tx.vhd:234 `o_busy = 1 when state /= S_IDLE OR i_frame(7)=1
    //          OR i_frame(6)=1`. With no FIFO content, state stays S_IDLE,
    //          so only the frame-bit gates should drive busy. Verify both
    //          frame(7) (reset-hold) and frame(6) (break-hold) raise o_busy
    //          regardless of FIFO state.
    {
        UartChannel& ch = uart.channel(0);

        // (a) frame(7)=1 (reset held): o_busy=1, regardless of FIFO.
        bitlevel_setup(ch, 0x98, 4);           // 0x98 = reset | 8N1
        ch.tick_one_bit_clock();               // let snapshot settle
        bool busy_reset = ch.tx_busy_bitlevel();

        // (b) frame(6)=1 (break held), FIFO empty: o_busy=1.
        bitlevel_setup(ch, 0x58, 4);           // 0x58 = break | 8N1
        ch.tick_one_bit_clock();
        bool busy_break = ch.tx_busy_bitlevel();

        check("FRM-05",
              "uart_tx.vhd:234 - o_busy raised by frame(7) reset OR frame(6) break",
              busy_reset && busy_break,
              fmt("busy_reset=%d busy_break=%d", busy_reset, busy_break));
    }

    // FRM-06 - uart_tx.vhd:107-114 samples i_frame(2), i_frame(0) and
    //          i_prescaler at S_IDLE on each falling edge; once the TX
    //          engine leaves S_IDLE those snapshots are held for the whole
    //          frame, so mid-byte frame writes must not disturb the
    //          in-flight bit pattern. Send 0xAA, step into S_BITS, rewrite
    //          the framing register, then step to completion and verify
    //          tx_shift_ drained down to 0x00 (all 8 bits of 0xAA shifted
    //          through) regardless of the mid-flight frame overwrite.
    {
        UartChannel& ch = uart.channel(0);
        bitlevel_setup(ch, 0x18, 4);           // 8N1
        ch.write_tx(0xAA);
        // Enter S_BITS (via IDLE -> START -> BITS).
        bool entered = tick_until_tx_state(ch, TxState::S_BITS, 50);
        // Mid-byte: write a different framing value (still 8N1 but with
        // flow/odd-parity bits set). VHDL snapshot must ignore this.
        ch.write_frame(0x1F);                  // 8O1+parity, flow enable
        // Continue ticking to run out the frame. Generous budget:
        // 8 bits * 4 prescaler + stop bits = ~40 ticks.
        bool drained = tick_until_tx_state(ch, TxState::S_IDLE, 200);
        // Back in S_IDLE: tx_shift_ should have fully shifted the 0xAA
        // down to 0x00 — confirms the original 8-data-bit frame ran to
        // completion despite the mid-byte rewrite.
        uint8_t final_shift = ch.tx_shift_reg_live();
        check("FRM-06",
              "uart_tx.vhd:107-114 - frame snapshot at S_IDLE honoured across mid-byte write",
              entered && drained && final_shift == 0x00,
              fmt("entered=%d drained=%d final_shift=0x%02x", entered, drained, final_shift));
    }
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

    // BAUD-02 / BAUD-03 — D-UNOBSERVABLE per feedback_unobservable_audit_rule.
    // uart.vhd:323-326 writes lsb(6:0) when d(7)=0 and lsb(13:7) when d(7)=1
    // via half-selective assignment. The low 14 bits of the prescaler drive
    // the internal baud divider only — there is no VHDL read path exposing
    // them to any port. BAUD-01/04/05/06 cover the MSB (which IS observable
    // via the select register). Indirect coverage of the LSB: BAUD-07 observes
    // the post-prescaler TX empty latency, which only matches when the LSB
    // write path is intact; the half-selective write semantics are structurally
    // enforced by uart.vhd:323-327. No test row emitted — these carry no
    // observable assertion under the VHDL contract.

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

    // BAUD-07 - uart_tx.vhd:86,111 tx_prescaler snapshot is taken from
    //           i_prescaler at S_IDLE on the falling edge, then held for
    //           the whole frame. Mid-byte prescaler writes must not change
    //           the in-flight timer cadence. Start sending 0xAA, enter
    //           S_BITS, double the prescaler, and verify the frame still
    //           completes — if the snapshot were honoured the byte runs
    //           at the ORIGINAL prescaler; if it were ignored the engine
    //           would either drag out the remaining bits (tick budget
    //           runs out) or complete faster than a byte running at the
    //           new prescaler.
    {
        UartChannel& ch = uart.channel(0);
        bitlevel_setup(ch, 0x18, 4);           // 8N1, prescaler=4
        ch.write_tx(0xAA);
        // Enter S_BITS first.
        bool entered = tick_until_tx_state(ch, TxState::S_BITS, 50);
        // Mid-byte: double the prescaler to 8. If honoured, frame runs at
        // the original p=4 cadence (~40 ticks total remaining).
        ch.write_prescaler_lsb(0x80);          // clear upper 7 bits
        ch.write_prescaler_lsb(0x00);          // clear lower 7
        ch.write_prescaler_lsb(0x08);          // lower 7 = 8
        // With snapshot honoured, the frame must complete in a budget
        // consistent with the ORIGINAL prescaler (~40 ticks). Give it 50
        // ticks (enough for p=4 but not enough for p=8 worth of ~80).
        bool drained_quick = tick_until_tx_state(ch, TxState::S_IDLE, 50);
        check("BAUD-07",
              "uart_tx.vhd:86,111 - prescaler snapshot at S_IDLE honoured across mid-byte write",
              entered && drained_quick,
              fmt("entered=%d drained_quick=%d", entered, drained_quick));
    }
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

    // TX-05 - uart.vhd:529 `uart0_tx_fifo_we <= uart0_tx_wr and (not
    //         uart0_tx_wr_d) and not uart0_tx_fifo_full` — the FIFO is
    //         written exactly once per CPU I/O strobe (edge-detected).
    //         Verify the observable VHDL contract at the CPU/emu boundary:
    //         N distinct `write()` calls enqueue exactly N bytes (not N-1,
    //         not N+1) — i.e., each call is its own edge, and bytes are
    //         not double-counted. We transmit via the byte-level `tick()`
    //         path and confirm the loopback delivers exactly N bytes back
    //         to the RX FIFO in order.
    {
        uart.hard_reset();
        const uint8_t pattern[] = {0x10, 0x20, 0x30, 0x40};
        for (uint8_t b : pattern) uart.write(REG_TX, b);
        // Drain the FIFO through the byte-level transmitter (loopback →
        // RX FIFO). Each byte takes `frame_bits * prescaler` ticks at
        // default 8N1 + 243 = 10*243=2430; 4 bytes * 2500 ticks budget.
        uart.tick(12000);
        // Read back from RX FIFO (loopback): should be exactly 4 bytes
        // in original order. A spurious double-push from non-edge-
        // detected write() would show up here as extra bytes.
        uint8_t rx[5] = {0,0,0,0,0};
        for (int i = 0; i < 5; ++i) rx[i] = uart.read(REG_RX);
        uint8_t after_last = uart.channel(0).read_status();
        bool seq_ok = rx[0] == 0x10 && rx[1] == 0x20 && rx[2] == 0x30 && rx[3] == 0x40;
        bool no_extra = rx[4] == 0x00 && (after_last & 0x01) == 0;  // rx_avail=0
        check("TX-05",
              "uart.vhd:529 - 4 CPU writes enqueue exactly 4 FIFO entries (edge-triggered)",
              seq_ok && no_extra,
              fmt("rx=[%02x,%02x,%02x,%02x,%02x] status=0x%02x",
                  rx[0], rx[1], rx[2], rx[3], rx[4], after_last));
    }

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

    // TX-07 - uart_tx.vhd:239 `S_IDLE => o_Tx <= not i_frame(6)`. When the
    //         break bit is asserted (frame(6)=1) the TX line goes LOW and
    //         stays there while the engine is parked in S_IDLE (no byte
    //         pending). Flip bit-level mode on, set frame(6), tick the
    //         engine and verify tx_line_out() == false.
    {
        UartChannel& ch = uart.channel(0);
        bitlevel_setup(ch, 0x58, 4);           // break | 8N1, FIFO empty
        // Let the engine settle: a few ticks of S_IDLE + break gate.
        for (int i = 0; i < 4; ++i) ch.tick_one_bit_clock();
        bool line_low = !ch.tx_line_out();
        check("TX-07",
              "uart_tx.vhd:239 - S_IDLE with frame(6)=1 holds o_Tx low",
              line_low,
              fmt("line=%d expected=0", ch.tx_line_out()));
    }

    // TX-08 - uart_tx.vhd:236-245 bit-level TX line encoding for 8N1.
    //         Send 0x55 (binary 01010101). LSB-first per uart_tx.vhd:124
    //         shift direction. Expected TX line sequence:
    //           S_IDLE  = 1 (idle high)
    //           S_START = 0 (start bit)
    //           S_BITS  = bit 0..7 of 0x55 LSB-first = 1,0,1,0,1,0,1,0
    //           S_STOP_1 = 1 (stop bit)
    //           S_IDLE  = 1
    //         Sample each bit at mid-prescaler to match a receiver's
    //         mid-bit sample point.
    {
        UartChannel& ch = uart.channel(0);
        const uint16_t P = 8;                  // prescaler=8 for clean mid-bit sampling
        bitlevel_setup(ch, 0x18, P);           // 8N1
        ch.write_tx(0x55);
        // Wait until engine enters S_START (idle -> start transition).
        bool in_start = tick_until_tx_state(ch, TxState::S_START, 20);
        bool start_low = sample_mid_bit(ch, P) == false;
        // Data bits, LSB-first for 0x55: 1,0,1,0,1,0,1,0
        const bool expected[8] = {true, false, true, false, true, false, true, false};
        bool data_ok = true;
        bool data[8] = {};
        for (int i = 0; i < 8; ++i) {
            data[i] = sample_mid_bit(ch, P);
            if (data[i] != expected[i]) data_ok = false;
        }
        // Stop bit: sample at mid-point. With 1 stop bit we're in S_STOP_1.
        bool stop_high = sample_mid_bit(ch, P) == true;
        check("TX-08",
              "uart_tx.vhd:236-245 - 8N1 bit pattern for 0x55 LSB-first",
              in_start && start_low && data_ok && stop_high,
              fmt("start=%d data=[%d%d%d%d%d%d%d%d] stop=%d",
                  start_low ? 0 : 1,
                  data[0], data[1], data[2], data[3],
                  data[4], data[5], data[6], data[7],
                  stop_high ? 1 : 0));
    }

    // TX-09 - uart_tx.vhd:152,216-225 7E2 framing (7 data, even parity, 2
    //         stops). Frame encoding: #bits="10", parity_en=1, odd=0,
    //         stop=1. => 0x15.  For 0x7F (7 ones) with even parity
    //         (init=frame(1)=0), final parity = XOR(bit0..bit6) = 1, then
    //         two stop bits at 1.
    {
        UartChannel& ch = uart.channel(0);
        const uint16_t P = 8;
        bitlevel_setup(ch, 0x15, P);           // 7E2
        ch.write_tx(0x7F);
        bool in_start = tick_until_tx_state(ch, TxState::S_START, 20);
        bool start_low = sample_mid_bit(ch, P) == false;
        bool data_ok = true;
        for (int i = 0; i < 7; ++i) {
            if (sample_mid_bit(ch, P) != true) { data_ok = false; break; }
        }
        bool parity_high = sample_mid_bit(ch, P) == true;
        bool stop1_high = sample_mid_bit(ch, P) == true;
        bool stop2_high = sample_mid_bit(ch, P) == true;
        check("TX-09",
              "uart_tx.vhd:152,216-225 - 7E2: 0x7F yields even-parity=1, 2 stop bits high",
              in_start && start_low && data_ok && parity_high && stop1_high && stop2_high,
              fmt("start=%d data_ok=%d parity=%d stop1=%d stop2=%d",
                  start_low ? 0 : 1, data_ok ? 1 : 0,
                  parity_high ? 1 : 0, stop1_high ? 1 : 0, stop2_high ? 1 : 0));
    }

    // TX-10 - uart_tx.vhd 5O1 framing (5 data, odd parity, 1 stop). Frame
    //         encoding: #bits="00", parity_en=1, odd=1, stop=0 => 0x06.
    //         For 0x0F (lower-5 = 01111, 4 ones) with odd parity
    //         (init=frame(1)=1), final = 1 XOR 4 ones = 1.
    {
        UartChannel& ch = uart.channel(0);
        const uint16_t P = 8;
        bitlevel_setup(ch, 0x06, P);           // 5O1
        ch.write_tx(0x0F);
        bool in_start = tick_until_tx_state(ch, TxState::S_START, 20);
        bool start_low = sample_mid_bit(ch, P) == false;
        // Data bits LSB-first of 0x0F&0x1F: 1,1,1,1,0
        const bool expected[5] = {true, true, true, true, false};
        bool data_ok = true;
        for (int i = 0; i < 5; ++i) {
            if (sample_mid_bit(ch, P) != expected[i]) { data_ok = false; break; }
        }
        bool parity_high = sample_mid_bit(ch, P) == true;
        bool stop_high = sample_mid_bit(ch, P) == true;
        check("TX-10",
              "uart_tx.vhd - 5O1: 0x0F lower-5 yields odd-parity=1, 1 stop bit",
              in_start && start_low && data_ok && parity_high && stop_high,
              fmt("start=%d data_ok=%d parity=%d stop=%d",
                  start_low ? 0 : 1, data_ok ? 1 : 0,
                  parity_high ? 1 : 0, stop_high ? 1 : 0));
    }

    // TX-11 - uart_tx.vhd:180-192 CTS flow control hold: with frame(5)=1
    //         (flow enable) and i_cts_n=1 (not clear to send), the engine
    //         transitions IDLE -> S_RTR and stays there waiting. The TX
    //         line stays idle-high throughout.
    {
        UartChannel& ch = uart.channel(0);
        bitlevel_setup(ch, 0x38, 4);           // 0x38 = flow_en | 8N1
        ch.set_cts_n(true);                    // NOT clear to send
        ch.write_tx(0x55);
        // Advance many ticks; engine must park in S_RTR.
        for (int i = 0; i < 100; ++i) ch.tick_one_bit_clock();
        bool in_rtr = (ch.tx_state_live() == TxState::S_RTR);
        bool line_idle = ch.tx_line_out() == true;
        check("TX-11",
              "uart_tx.vhd:180-192 - flow_en + CTS#=1 parks engine in S_RTR, line idle",
              in_rtr && line_idle,
              fmt("state=%d line=%d (expected S_RTR=%d, line=1)",
                  (int)ch.tx_state_live(), ch.tx_line_out(), (int)TxState::S_RTR));
    }

    // TX-12 - uart_tx.vhd:187-192 CTS release: from S_RTR, asserting
    //         i_cts_n=0 (clear to send) lets the engine advance to
    //         S_START on the next tick.
    {
        UartChannel& ch = uart.channel(0);
        bitlevel_setup(ch, 0x38, 4);           // flow_en | 8N1
        ch.set_cts_n(true);
        ch.write_tx(0x55);
        // Park in S_RTR.
        for (int i = 0; i < 20; ++i) ch.tick_one_bit_clock();
        bool in_rtr = (ch.tx_state_live() == TxState::S_RTR);
        // Release CTS.
        ch.set_cts_n(false);
        // One tick to reach S_START.
        bool reached_start = tick_until_tx_state(ch, TxState::S_START, 10);
        check("TX-12",
              "uart_tx.vhd:187-192 - CTS# release (0) advances S_RTR -> S_START",
              in_rtr && reached_start,
              fmt("was_rtr=%d reached_start=%d", in_rtr, reached_start));
    }

    // TX-13 - uart_tx.vhd:153,156 even parity calculation. Init =
    //         frame(1) = 0; XOR each data LSB *pre-shift*. Send 0x0F (4
    //         ones) → parity = 0; send 0x1F (5 ones) → parity = 1.
    {
        UartChannel& ch = uart.channel(0);
        const uint16_t P = 8;
        auto sample_parity_for = [&](uint8_t byte) -> bool {
            bitlevel_setup(ch, 0x1C, P);       // 8E1: 8 data | parity_en | even | 1 stop
            ch.write_tx(byte);
            tick_until_tx_state(ch, TxState::S_START, 20);
            sample_mid_bit(ch, P);             // start bit
            for (int i = 0; i < 8; ++i) sample_mid_bit(ch, P);
            return sample_mid_bit(ch, P);      // parity bit
        };
        bool p_0F = sample_parity_for(0x0F);   // 4 ones → parity 0
        bool p_1F = sample_parity_for(0x1F);   // 5 ones → parity 1
        check("TX-13",
              "uart_tx.vhd:153,156 - even parity: 0x0F→0, 0x1F→1 (pre-shift LSB XOR)",
              !p_0F && p_1F,
              fmt("p(0x0F)=%d (want 0), p(0x1F)=%d (want 1)", p_0F ? 1 : 0, p_1F ? 1 : 0));
    }

    // TX-14 - uart_tx.vhd:153,156 odd parity: init = frame(1) = 1.
    //         0x0F (4 ones) → 1; 0x1F (5 ones) → 0.
    {
        UartChannel& ch = uart.channel(0);
        const uint16_t P = 8;
        auto sample_parity_for = [&](uint8_t byte) -> bool {
            bitlevel_setup(ch, 0x1E, P);       // 8O1: 8 data | parity_en | odd | 1 stop
            ch.write_tx(byte);
            tick_until_tx_state(ch, TxState::S_START, 20);
            sample_mid_bit(ch, P);             // start bit
            for (int i = 0; i < 8; ++i) sample_mid_bit(ch, P);
            return sample_mid_bit(ch, P);      // parity bit
        };
        bool p_0F = sample_parity_for(0x0F);   // init=1 XOR 4 ones → 1
        bool p_1F = sample_parity_for(0x1F);   // init=1 XOR 5 ones → 0
        check("TX-14",
              "uart_tx.vhd:153,156 - odd parity: 0x0F→1, 0x1F→0 (pre-shift LSB XOR, init=1)",
              p_0F && !p_1F,
              fmt("p(0x0F)=%d (want 1), p(0x1F)=%d (want 0)", p_0F ? 1 : 0, p_1F ? 1 : 0));
    }
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

    // RX-08..RX-15 — Wave B flips: bit-level RX engine lives in Phase-1
    //         scaffold (uart.{h,cpp}). Use a small prescaler (=8) to keep the
    //         bit-driven tests fast. Helper drives a byte bit-at-a-time with
    //         optional framing/parity-err injection; it also holds each bit
    //         long enough for the noise-rejection debouncer (uart_rx.vhd:119-131)
    //         to propagate (counter saturates after ~5 stable samples).

    // Bit-level helpers: use a small-but-generous prescaler (=32) so the
    // noise-rejection debouncer (~5 ticks) is well below half a bit period
    // (16 ticks), keeping mid-bit sampling robust.
    constexpr int BIT_PRESCALER = 32;

    // Helper: configure UART 0 for a small prescaler, enable bit-level mode,
    // drive a byte at bit level with the given framing/parity bits.
    // Returns the channel reference for post-drive state inspection.
    auto configure_bitlevel = [&](uint8_t framing_reg) -> UartChannel& {
        uart.hard_reset();
        // Prescaler: MSB stays 0; write BOTH LSB halves so the default
        // 115200-baud value doesn't leak into the upper 7 bits. VHDL
        // uart.vhd:323-326: bit 7 clear writes lsb(6:0); bit 7 set writes
        // lsb(13:7). Explicitly zero the upper half first.
        uart.write(REG_FRAME, framing_reg);
        uart.write(REG_RX, 0x80);              // upper 7 bits = 0
        uart.write(REG_RX, BIT_PRESCALER);     // lower 7 bits = BIT_PRESCALER
        UartChannel& ch = uart.channel(0);
        ch.set_bitlevel_mode(true);
        // Settle: line idle-high long enough for debouncer to saturate + the
        // S_PAUSE → S_IDLE transition (and, if framing_reg & 0x40, remain in
        // S_PAUSE). A full bit-period worth of ticks is overkill but safe.
        ch.drive_rx_line(true);
        for (int i = 0; i < BIT_PRESCALER; ++i) ch.tick_one_bit_clock();
        return ch;
    };

    // Helper: drive a single bit (value v) for `bit_ticks` CLK_28 edges.
    auto drive_bit = [](UartChannel& ch, bool v, int bit_ticks) {
        ch.drive_rx_line(v);
        for (int i = 0; i < bit_ticks; ++i) ch.tick_one_bit_clock();
    };

    // Helper: drive a full frame — start bit (0) + 8 data LSB-first + stop bit.
    // If `stop_low` is true, hold the stop bit at 0 (triggers S_ERROR per
    // uart_rx.vhd:263-266 — framing error).
    auto drive_frame = [&](UartChannel& ch, uint8_t data, bool stop_low) {
        drive_bit(ch, false, BIT_PRESCALER);                         // start
        for (int b = 0; b < 8; ++b)
            drive_bit(ch, (data >> b) & 1, BIT_PRESCALER);           // data LSB first
        drive_bit(ch, stop_low ? false : true, BIT_PRESCALER);       // stop
    };

    // RX-08 / RX-09 — bit-level engine reaches S_ERROR correctly, but the
    //         Phase-1 scaffold does NOT propagate the one-cycle
    //         `rx_err_framing` / `rx_err_parity` pulses (uart_rx.vhd:312-313)
    //         into the sticky `err_framing_` flag (uart.vhd:541). The latches
    //         `rx_byte_framing_err_` / `rx_byte_parity_err_` are set at the
    //         state transition but only consumed when a byte commits at
    //         STOP_* → S_IDLE; on the error path (→ S_ERROR) they never land
    //         in sticky status bit 6. The rest of Wave B's RX-08..15 stimulus
    //         works; only the sticky-err propagation is missing. Reported
    //         back to the session manager — NOT fixed in this worktree per
    //         prompt instruction (scaffold extension forbidden).
    // RX-08 — Framing error: stop bit held low triggers S_STOP_1 → S_ERROR
    //         (uart_rx.vhd:263-266). Sticky err_framing (status bit 6) must
    //         OR in per uart.vhd:541.
    {
        UartChannel& ch = configure_bitlevel(0x18);  // 8N1
        drive_frame(ch, 0x55, /*stop_low=*/true);
        // Settle for engine to reach S_ERROR + propagate sticky flag.
        for (int i = 0; i < BIT_PRESCALER; ++i) ch.tick_one_bit_clock();
        uint8_t status = ch.read_status();
        check("RX-08",
              "uart.vhd:541 - framing error sets sticky status bit 6 (err_framing)",
              (status & 0x40) != 0,
              fmt("status=0x%02x (bit 6 = err_framing)", status));
    }

    // RX-09 — Parity error: data + mismatched parity bit triggers S_PARITY
    //         → S_ERROR (uart_rx.vhd:257). Sticky err_framing sets per
    //         uart.vhd:541 (same sticky covers parity + framing errors).
    {
        UartChannel& ch = configure_bitlevel(0x1C);  // 8E1 (parity_en, even)
        // Drive byte 0x0F (4 ones, even parity should be 0) but send
        // parity bit = 1 (wrong) to trigger mismatch.
        drive_bit(ch, false, BIT_PRESCALER);                       // start
        for (int b = 0; b < 8; ++b)
            drive_bit(ch, (0x0F >> b) & 1, BIT_PRESCALER);         // data LSB-first
        drive_bit(ch, true, BIT_PRESCALER);                        // wrong parity
        drive_bit(ch, true, BIT_PRESCALER);                        // stop
        for (int i = 0; i < BIT_PRESCALER; ++i) ch.tick_one_bit_clock();
        uint8_t status = ch.read_status();
        check("RX-09",
              "uart.vhd:541 - parity error sets sticky status bit 6 (err_framing shared)",
              (status & 0x40) != 0,
              fmt("status=0x%02x (bit 6 = err_framing)", status));
    }

    // RX-10 - uart_rx.vhd:314 o_err_break = '1' when state=S_ERROR and
    //         rx_shift="00000000". Drive start + all-zero data + stop-low to
    //         reach S_ERROR with rx_shift holding 0x00 (see uart_rx.vhd:160-162:
    //         state_next=S_ERROR forces rx_shift <= 0x00 via the shift process).
    {
        UartChannel& ch = configure_bitlevel(0x18);
        drive_bit(ch, false, BIT_PRESCALER);                         // start
        for (int b = 0; b < 8; ++b)
            drive_bit(ch, false, BIT_PRESCALER);                     // all-zero data
        drive_bit(ch, false, BIT_PRESCALER);                         // stop = 0 → S_ERROR
        for (int i = 0; i < BIT_PRESCALER; ++i) ch.tick_one_bit_clock();
        bool in_error = ch.rx_state_live() == UartChannel::RxState::S_ERROR;
        bool is_break = ch.rx_break();
        uint8_t shift = ch.rx_shift_reg_live();
        check("RX-10",
              "uart_rx.vhd:314 - all-zero frame + stop-low → S_ERROR with rx_shift=0x00 → rx_break()",
              in_error && is_break && shift == 0x00,
              fmt("state=%u break=%d shift=0x%02x", (unsigned)ch.rx_state_live(), is_break, shift));
    }

    // RX-11 - uart.vhd:359 status bit 5 = `uart0_rx_o(8) AND rx_avail` —
    //         the per-byte framing flag stored in the 9th FIFO bit, NOT the
    //         sticky err_framing_ flag. Inject one error byte, then one clean
    //         byte, and sample bit 5 across a pop — it must follow the FIFO
    //         head, not the sticky flag.
    {
        uart.hard_reset();
        UartChannel& ch = uart.channel(0);
        ch.inject_rx_bit_frame(0xAA, /*framing_err=*/true,  /*parity_err=*/false);
        ch.inject_rx_bit_frame(0xBB, /*framing_err=*/false, /*parity_err=*/false);

        uint8_t status1 = ch.read_status();               // head = 0xAA (err)
        uint8_t head1   = uart.read(REG_RX);              // pop 0xAA, clears errs
        uint8_t status2 = ch.read_status();               // head = 0xBB (clean)
        uint8_t head2   = uart.read(REG_RX);              // pop 0xBB

        bool bit5_first_set  = (status1 & 0x20) != 0;
        bool bit5_second_clr = (status2 & 0x20) == 0;
        check("RX-11",
              "uart.vhd:359 - status bit 5 follows FIFO head 9th bit (per-byte) not sticky err_framing_",
              bit5_first_set && bit5_second_clr && head1 == 0xAA && head2 == 0xBB,
              fmt("status1=0x%02x status2=0x%02x head1=0x%02x head2=0x%02x",
                  status1, status2, head1, head2));
    }

    // RX-12 - uart_rx.vhd:119-131 + misc/debounce.vhd: noise-rejection debouncer
    //         filters pulses shorter than 2^NOISE_REJECTION_BITS (=4) stable
    //         samples. A 2-tick low pulse on Rx must NOT propagate to the RX
    //         engine; state stays in S_IDLE.
    {
        UartChannel& ch = configure_bitlevel(0x18);
        // Guard: engine settled in S_IDLE after configure_bitlevel.
        bool was_idle = ch.rx_state_live() == UartChannel::RxState::S_IDLE;
        // Drive a 2-tick low pulse; debounce counter resets on each flip and
        // never saturates, so rx_debounced_ stays high → engine stays in S_IDLE.
        drive_bit(ch, false, 2);
        drive_bit(ch, true, 12);
        bool still_idle = ch.rx_state_live() == UartChannel::RxState::S_IDLE;
        check("RX-12",
              "uart_rx.vhd:119-131 - short <4-tick Rx pulse filtered by noise rejection; S_IDLE preserved",
              was_idle && still_idle,
              fmt("was_idle=%d still_idle=%d final_state=%u",
                  was_idle, still_idle, (unsigned)ch.rx_state_live()));
    }

    // RX-13 - uart_rx.vhd:231-232 in S_IDLE with i_frame(6)=1 → S_PAUSE. Set
    //         framing bit 6 (TX break, frame-pause) and drive a start bit;
    //         the engine must NOT leave S_PAUSE / must not receive the byte.
    {
        UartChannel& ch = configure_bitlevel(0x58);       // bit 6 = pause/break
        // After configure_bitlevel the engine started in S_PAUSE and with
        // frame(6)=1 it must stay there regardless of Rx line.
        drive_frame(ch, 0x55, /*stop_low=*/false);
        bool still_pause = ch.rx_state_live() == UartChannel::RxState::S_PAUSE;
        // No byte should land in the FIFO.
        uint8_t status = ch.read_status();
        check("RX-13",
              "uart_rx.vhd:231-232 - frame(6)=1 keeps engine in S_PAUSE; no RX byte received",
              still_pause && (status & 0x01) == 0,
              fmt("state=%u status=0x%02x", (unsigned)ch.rx_state_live(), status));
    }

    // RX-14 - uart_rx.vhd:144-154 frame/prescaler snapshot at falling edge
    //         while state=S_IDLE. Once reception starts the snapshot is
    //         frozen until the next S_IDLE entry: changing framing mid-byte
    //         MUST NOT alter the in-flight byte's framing.
    //         Start a byte (framing_=0x18 = 8 data bits), advance a few data
    //         bits, write a radically different framing (=0x10 = 5 data bits);
    //         the byte must still complete with 8 data bits and land in the
    //         FIFO. If the snapshot wasn't honoured the engine would mis-frame.
    {
        UartChannel& ch = configure_bitlevel(0x18);
        drive_bit(ch, false, BIT_PRESCALER);                         // start (snapshot latched)
        drive_bit(ch, 1, BIT_PRESCALER);                             // data bit 0 = 1
        drive_bit(ch, 0, BIT_PRESCALER);                             // data bit 1 = 0
        // Mid-byte framing change — snapshot should insulate in-flight byte.
        // Note: writing framing here does NOT set bit 7 (no reset) so the
        // FIFO + in-flight state persist; only the rx_frame_* snapshot reads
        // have frozen at the S_IDLE tick, so the engine continues with 8 data
        // bits rather than switching to 5.
        uart.write(REG_FRAME, 0x10);                                 // would imply 5 data bits
        drive_bit(ch, 1, BIT_PRESCALER);                             // data bit 2
        drive_bit(ch, 0, BIT_PRESCALER);                             // data bit 3
        drive_bit(ch, 1, BIT_PRESCALER);                             // data bit 4
        drive_bit(ch, 0, BIT_PRESCALER);                             // data bit 5
        drive_bit(ch, 1, BIT_PRESCALER);                             // data bit 6
        drive_bit(ch, 0, BIT_PRESCALER);                             // data bit 7 → 0x55
        drive_bit(ch, true, BIT_PRESCALER);                          // stop bit
        for (int i = 0; i < BIT_PRESCALER; ++i) ch.tick_one_bit_clock();
        uint8_t status = ch.read_status();
        uint8_t byte   = uart.read(REG_RX);
        check("RX-14",
              "uart_rx.vhd:144-154 - frame-snapshot at S_IDLE insulates in-flight byte from mid-byte framing change",
              (status & 0x01) != 0 && byte == 0x55,
              fmt("status=0x%02x byte=0x%02x (expected avail=1, byte=0x55)", status, byte));
    }

    // RX-15 - uart.vhd:442-446 o_Rx_rtr_n = framing(5) AND rx_fifo_almost_full.
    //         Almost_full (FifoBuffer) = count >= Capacity - 2 = 510 entries.
    //         With hw-flow bit (frame 0x20) set and FIFO at 510, rx_rtr_n()
    //         must be asserted (request-to-receive deasserted = host busy);
    //         draining one byte drops below the threshold and rx_rtr_n()
    //         clears.
    {
        uart.hard_reset();
        UartChannel& ch = uart.channel(0);
        uart.write(REG_FRAME, 0x38);                      // 8N1 + hw-flow (bit 5)
        // Fill the 512-entry FIFO to exactly almost_full threshold (=510).
        for (int i = 0; i < 510; ++i) ch.inject_rx(static_cast<uint8_t>(i));
        bool asserted = ch.rx_rtr_n();
        // Drain one byte — count drops to 509, below almost_full threshold.
        (void)uart.read(REG_RX);
        bool cleared = !ch.rx_rtr_n();
        check("RX-15",
              "uart.vhd:442-446 - rx_rtr_n = framing(5) AND rx_fifo_almost_full; toggles across drain",
              asserted && cleared,
              fmt("at-510=%d after-drain-to-509=%d", asserted, !cleared));
    }
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
    //           channel marker at bit 6 for UART 1. See UART-SEL-02 on the bit
    //           numbering (GH #253).
    {
        uart.hard_reset();
        uart.write(REG_SELECT, 0x13);     // UART 0 msb = 3
        uart.write(REG_SELECT, 0x55);     // UART 1 msb = 5
        uart.write(REG_SELECT, 0x00);
        uint8_t sel0 = uart.read(REG_SELECT);
        uart.write(REG_SELECT, 0x40);
        uint8_t sel1 = uart.read(REG_SELECT);
        check("DUAL-02",
              "uart.vhd:282-286,355,371 - UART 0 reads 0x03, UART 1 reads 0x45",
              sel0 == 0x03 && sel1 == 0x45,
              fmt("sel0=0x%02x sel1=0x%02x (VHDL expects 0x03, 0x45)", sel0, sel1));
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

    // I2C-11 - zxnext.vhd:3259 AND-s the CPU SCL with pi_i2c1_scl. Pulling
    //          pi_i2c1_scl low must force read_scl() bit 0 to 0 even while
    //          the local master has released the line; releasing the Pi
    //          bridge restores the local driver's view.
    //
    // G138 NOTE: per VHDL zxnext.vhd:2318, pi_i2c1_scl is forced HIGH at
    // the wired-AND boundary unless NR 0xA0 bit 3 (pi_i2c1_en) is set.
    // Enable the gate explicitly so set_pi_i2c1() reaches the AND. I2C-13
    // below verifies the gated-off case.
    {
        i2c.reset();
        i2c.set_pi_i2c1_en(true);          // NR 0xA0 bit 3 = 1 (open the gate)
        i2c.write_scl(0x01);              // local master releases SCL high
        i2c.set_pi_i2c1(false, true);     // Pi pulls SCL low
        uint8_t scl_low = i2c.read_scl() & 0x01;
        i2c.set_pi_i2c1(true, true);      // Pi releases SCL
        uint8_t scl_hi  = i2c.read_scl() & 0x01;
        check("I2C-11",
              "zxnext.vhd:3259 - pi_i2c1_scl AND-gates the SCL read path "
              "(with NR 0xA0 bit 3 enabling the Pi bridge per G138)",
              scl_low == 0 && scl_hi == 1,
              fmt("pi_low=%u pi_high=%u", scl_low, scl_hi));
    }

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

    // I2C-13 — NR 0xA0 bit 3 (pi_i2c1_en) gates I2C1 GPIO mux.
    // VHDL zxnext.vhd:2280: pi_i2c1_en <= nr_a0_pi_peripheral_en(3);
    // VHDL zxnext.vhd:2317-2318:
    //   pi_i2c1_sda <= i_GPIO(2) when pi_i2c1_en='1' else '1';
    //   pi_i2c1_scl <= i_GPIO(3) when pi_i2c1_en='1' else '1';
    // When the gate is OFF (NR 0xA0 bit 3 = 0, the reset default), pulling
    // the Pi bridge wires low MUST NOT propagate to the host's port 0x103B
    // /0x113B reads — the AND-boundary substitutes '1' for the Pi inputs.
    // When the gate is ON, the wired-AND is observable (mirrors I2C-11).
    {
        i2c.reset();
        i2c.write_scl(0x01);                  // local master releases SCL
        i2c.write_sda(0x01);                  // local master releases SDA

        // Gate OFF (default after reset). Pi tries to pull both lines low.
        i2c.set_pi_i2c1_en(false);
        i2c.set_pi_i2c1(false, false);        // pi_i2c1_scl=0, pi_i2c1_sda=0
        const uint8_t scl_off = i2c.read_scl() & 0x01;
        const uint8_t sda_off = i2c.read_sda() & 0x01;

        // Now open the gate; the same Pi-low values must propagate.
        i2c.set_pi_i2c1_en(true);
        const uint8_t scl_on = i2c.read_scl() & 0x01;
        const uint8_t sda_on = i2c.read_sda() & 0x01;

        check("I2C-13",
              "zxnext.vhd:2280, 2317-2318 - NR 0xA0 bit 3 gates pi_i2c1_scl/sda; "
              "when off the Pi-low is masked to 1 at the wired-AND boundary",
              scl_off == 1 && sda_off == 1 && scl_on == 0 && sda_on == 0,
              fmt("off: scl=%u sda=%u (want 1/1); on: scl=%u sda=%u (want 0/0)",
                  scl_off, sda_off, scl_on, sda_on));
    }

    // WONT I2C-14 — 24LC256 EEPROM at 0x50 (G139). Writing a real
    // 24LCxx EEPROM device class (page-write timing, 32 KiB array, ack
    // polling, address-byte ACK at 0xA0/0xA1) is a non-trivial peripheral
    // implementation, and the only consumer in scope is tbblue.fw config
    // persistence. Until a tbblue.fw boot path actually requires the
    // EEPROM contents (most CSpect-targeted apps do not touch 0x50), the
    // NACK behaviour from "no device attached" is firmware-faithful.
    // Revisit-trigger: tbblue.fw boot needs the 24LC256 contents.
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

    // Helpers for multi-byte DS1307 transactions. write_reg sends the
    // register pointer + one data byte. write_seq sends a register
    // pointer followed by N data bytes (exercises auto-increment).
    // read_seq reads N bytes back from a seeked pointer.
    auto write_reg = [&](uint8_t reg, uint8_t value) {
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);   // DS1307 write frame
        (void)i2c_send_byte(i2c, reg);
        (void)i2c_send_byte(i2c, value);
        i2c_stop(i2c);
    };
    auto write_seq = [&](uint8_t reg, std::initializer_list<uint8_t> values) {
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);
        (void)i2c_send_byte(i2c, reg);
        for (uint8_t v : values) (void)i2c_send_byte(i2c, v);
        i2c_stop(i2c);
    };
    auto read_seq_from = [&](uint8_t reg, std::size_t n) {
        std::vector<uint8_t> out;
        i2c.reset();
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD0);
        (void)i2c_send_byte(i2c, reg);
        i2c_stop(i2c);
        i2c_start(i2c);
        (void)i2c_send_byte(i2c, 0xD1);
        for (std::size_t i = 0; i < n; ++i) {
            bool last = (i + 1 == n);
            // ACK all bytes except the final one (master NACK).
            out.push_back(i2c_read_byte(i2c, !last));
        }
        i2c_stop(i2c);
        return out;
    };

    // RTC-11 - Control register (reg 0x07) round-trip. DS1307 datasheet
    //          §Control Register: bits SQWE and OUT are writable; the
    //          emulator does not model the SQW pin so the register must
    //          round-trip verbatim (no side effects).
    {
        rtc.set_use_real_time(false);
        write_reg(0x07, 0x90);            // SQWE=1, OUT=1, RS=00
        uint8_t ctrl = rtc.peek_register(0x07);
        check("RTC-11",
              "DS1307 control register (0x07) round-trips written value",
              ctrl == 0x90,
              fmt("reg=0x%02x expected=0x90", ctrl));
        rtc.set_use_real_time(true);
    }

    // RTC-12 - Write single register + read back. With real-time snapshot
    //          frozen, writing 0x34 to reg 0x00 and reading back must
    //          return the exact value. Validates i2c.cpp write path
    //          (previously discarded writes silently).
    {
        rtc.set_use_real_time(false);
        rtc.poke_register(0x00, 0x00);    // clear any residue
        write_reg(0x00, 0x34);            // store 0x34 in seconds (valid BCD)
        uint8_t v = read_seq_from(0x00, 1).at(0);
        check("RTC-12",
              "DS1307 - write/read single register round-trip",
              v == 0x34,
              fmt("read=0x%02x expected=0x34", v));
        rtc.set_use_real_time(true);
    }

    // RTC-13 - 12-hour mode (DS1307 datasheet §Hours Register). bit 6 = 1
    //          selects 12h mode; bit 5 = PM flag; bits 4:0 = BCD hours.
    //          Writing 0x61 (12h|PM|hour=01) must round-trip.
    {
        rtc.set_use_real_time(false);
        write_reg(0x02, 0x61);            // 12h mode, PM, hour = 01
        uint8_t v = read_seq_from(0x02, 1).at(0);
        bool mode_ok = (v & 0x40) != 0;
        bool pm_ok   = (v & 0x20) != 0;
        bool bcd_ok  = (v & 0x1F) == 0x01;
        check("RTC-13",
              "DS1307 12h mode - bit 6 (12h) + bit 5 (PM) + BCD hours round-trip",
              mode_ok && pm_ok && bcd_ok,
              fmt("reg=0x%02x expected=0x61", v));
        rtc.set_use_real_time(true);
    }

    // RTC-14 - Auto-increment pointer wrap 0x3F → 0x00 (DS1307 datasheet
    //          §Address Autoincrement). Write 0x42 to reg 0x3F; the
    //          pointer then advances to 0x00 inside the same
    //          transaction — but the next transaction starts a fresh
    //          seek, so we verify by issuing a single-transaction
    //          "write pointer=0x3F, then two data bytes" and reading
    //          0x3F back + 0x00 back.
    {
        rtc.set_use_real_time(false);
        rtc.poke_register(0x00, 0xAA);
        rtc.poke_register(0x3F, 0xAA);
        // Write into 0x3F, then let auto-increment spill into 0x00.
        write_seq(0x3F, {0x42, 0x55});
        uint8_t at_3f = rtc.peek_register(0x3F);
        uint8_t at_00 = rtc.peek_register(0x00);
        check("RTC-14",
              "DS1307 - auto-increment wraps 0x3F → 0x00",
              at_3f == 0x42 && at_00 == 0x55,
              fmt("reg[0x3F]=0x%02x reg[0x00]=0x%02x", at_3f, at_00));
        rtc.set_use_real_time(true);
    }

    // RTC-15 - Sequential write into NVRAM (regs 0x08-0x3F, no validation).
    //          Writing three consecutive bytes must store them at
    //          0x08 / 0x09 / 0x0A via pointer auto-increment.
    {
        rtc.poke_register(0x08, 0x00);
        rtc.poke_register(0x09, 0x00);
        rtc.poke_register(0x0A, 0x00);
        write_seq(0x08, {0x11, 0x22, 0x33});
        uint8_t a = rtc.peek_register(0x08);
        uint8_t b = rtc.peek_register(0x09);
        uint8_t c = rtc.peek_register(0x0A);
        check("RTC-15",
              "DS1307 - sequential write with pointer auto-increment",
              a == 0x11 && b == 0x22 && c == 0x33,
              fmt("0x08=0x%02x 0x09=0x%02x 0x0A=0x%02x", a, b, c));
    }

    // RTC-16 - CH bit / oscillator halt (DS1307 datasheet §Clock Halt).
    //          Setting reg 0x00 bit 7 halts the oscillator; snapshot_time()
    //          must leave regs_[0..6] frozen across start()/stop()
    //          transitions.
    {
        rtc.set_use_real_time(true);
        // Halt the clock.
        write_reg(0x00, 0x80);            // CH=1, seconds=0
        uint8_t seconds_halt = rtc.peek_register(0x00);
        // Force a fresh snapshot (start() would call snapshot_time()).
        rtc.start();
        rtc.stop();
        uint8_t seconds_after = rtc.peek_register(0x00);
        bool halt_preserved = (seconds_halt == 0x80) && (seconds_after == 0x80);

        // Release the halt and verify host time flows again.
        write_reg(0x00, 0x00);            // CH=0
        rtc.start();                      // triggers snapshot
        rtc.stop();
        uint8_t seconds_run = rtc.peek_register(0x00);
        bool bcd_ok = ((seconds_run & 0x0F) <= 9) && (((seconds_run >> 4) & 0x07) <= 5);

        check("RTC-16",
              "DS1307 CH bit halts oscillator; clearing it resumes host-time snapshot",
              halt_preserved && bcd_ok && (seconds_run & 0x80) == 0,
              fmt("halt=0x%02x→0x%02x run=0x%02x", seconds_halt, seconds_after, seconds_run));
    }

    // RTC-17 - NVRAM 0x08-0x3F round-trip (DS1307 datasheet §RAM). All
    //          56 bytes must store arbitrary values byte-accurately.
    {
        // Fill 0x08..0x3F with a pattern via auto-increment.
        std::initializer_list<uint8_t> pattern = {
            0x55, 0xAA, 0x00, 0xFF, 0x5A, 0xA5, 0x01, 0x80,
            0x02, 0x40, 0x04, 0x20, 0x08, 0x10, 0x11, 0x22,
            0x33, 0x44, 0x66, 0x77, 0x88, 0x99, 0xAB, 0xCD,
            0xEF, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE,
            0xF0, 0x0F, 0x70, 0x07, 0xB0, 0x0B, 0xD0, 0x0D,
            0xE0, 0x0E, 0xC1, 0x1C, 0xC2, 0x2C, 0xC3, 0x3C,
            0xC4, 0x4C, 0xC5, 0x5C, 0xC6, 0x6C, 0xC7, 0x7C,
        };
        write_seq(0x08, pattern);
        bool ok = true;
        uint8_t reg = 0x08;
        std::string mismatch;
        for (uint8_t expected : pattern) {
            uint8_t got = rtc.peek_register(reg);
            if (got != expected) {
                ok = false;
                mismatch = fmt("reg 0x%02x: got=0x%02x expected=0x%02x", reg, got, expected);
                break;
            }
            ++reg;
        }
        check("RTC-17",
              "DS1307 NVRAM 0x08-0x3F byte-accurate round-trip",
              ok,
              mismatch);
    }

    // RTC-18 — 12h-mode hours snapshot preserves bit 6 / AM-PM.
    // DS1307 datasheet §Hours Register: bit 6 = mode (0 = 24h, 1 = 12h);
    // when 12h, bit 5 = AM/PM (0 = AM, 1 = PM) and bits 4:0 = BCD hours
    // 1..12. After the host writes 12h-mode (which sets `mode_12h_`), a
    // subsequent snapshot_time() (driven by start()) MUST re-encode the
    // host's 24h tm_hour into 12h BCD with the mode bit + AM/PM bit set,
    // not blindly write to_bcd(tm_hour) (which silently drops the host
    // back to 24h on every transaction).
    //
    // We can't modify host time, so we test deterministically: freeze
    // real-time off, write 0x40 (mode=12h, hour=00 BCD nonsense — datasheet
    // says hours start at 1 in 12h, but we just want the mode flag latched),
    // then re-enable real-time and verify the next snapshot encodes
    // bits 5:0 ≤ 12 BCD AND bit 6 = 1. As a stronger second probe we then
    // poke a fresh DS1307 fixture with mode_12h_ already set + use_real_time
    // ON, trigger a snapshot, and inspect bit 6 + the BCD value.
    {
        I2cRtc rtc12;
        I2cController i2c12;
        i2c12.attach_device(0x68, &rtc12);

        // Step 1 — set 12h-mode via I2C write (bit 6 of reg 0x02 = 1).
        rtc12.set_use_real_time(false);
        i2c12.reset();
        i2c_start(i2c12);
        (void)i2c_send_byte(i2c12, 0xD0);                    // DS1307 write
        (void)i2c_send_byte(i2c12, 0x02);                    // pointer = hours
        (void)i2c_send_byte(i2c12, 0x40);                    // mode=12h, hour=0
        i2c_stop(i2c12);
        const bool mode_set = rtc12.mode_12h();

        // Step 2 — re-enable host snapshot, force a fresh start() so
        // snapshot_time() runs, then read the hours register back and check
        // it preserved the 12h mode bit + encoded the host tm_hour as BCD
        // 1..12 (with optional PM flag).
        rtc12.set_use_real_time(true);
        rtc12.start();                                       // triggers snapshot_time()
        const uint8_t hr_after = rtc12.peek_register(0x02);
        const bool   mode_preserved   = (hr_after & 0x40) != 0;
        const uint8_t hr_bcd          = hr_after & 0x1F;     // bits 4:0
        const uint8_t tens            = (hr_bcd >> 4) & 0x01;
        const uint8_t units           = hr_bcd & 0x0F;
        const int    h12              = tens * 10 + units;
        const bool   bcd_in_12h_range = (h12 >= 1 && h12 <= 12);

        check("RTC-18",
              "DS1307 12h-mode hours snapshot preserves bit 6 + encodes BCD 1..12 "
              "with AM/PM bit (G161 — i2c.cpp::snapshot_time branch on mode_12h_)",
              mode_set && mode_preserved && bcd_in_12h_range,
              fmt("mode_set=%d hr=0x%02x mode_bit=%d bcd_hour=%d (want mode=1, hour 1..12)",
                  mode_set ? 1 : 0,
                  hr_after,
                  mode_preserved ? 1 : 0,
                  h12));
    }

    // RTC-19 — Task 28 fixed-time mode (--rtc). set_fixed_time() pins the
    // clock: snapshot_time() must encode the pinned std::tm into
    // regs_[0..6] as BCD instead of the host clock. 2026-07-10 is a
    // Friday → tm_wday=5 → DS1307 day-of-week register = 6.
    {
        I2cRtc rtcf;
        std::tm t{};
        const bool parsed = parse_rtc_datetime("2026-07-10 12:34:56", t);
        rtcf.set_fixed_time(t);
        rtcf.start();                                   // triggers snapshot_time()
        const bool regs_ok =
            rtcf.peek_register(0x00) == 0x56 &&         // seconds
            rtcf.peek_register(0x01) == 0x34 &&         // minutes
            rtcf.peek_register(0x02) == 0x12 &&         // hours (24h)
            rtcf.peek_register(0x03) == 0x06 &&         // day-of-week (Fri=6)
            rtcf.peek_register(0x04) == 0x10 &&         // date
            rtcf.peek_register(0x05) == 0x07 &&         // month
            rtcf.peek_register(0x06) == 0x26;           // year
        check("RTC-19",
              "Task 28 fixed-time mode — snapshot encodes the pinned datetime as BCD, "
              "not the host clock (i2c.cpp::snapshot_time fixed_tm_ branch)",
              parsed && regs_ok,
              fmt("parsed=%d regs=%02x %02x %02x %02x %02x %02x %02x "
                  "(want 56 34 12 06 10 07 26)",
                  parsed ? 1 : 0,
                  rtcf.peek_register(0x00), rtcf.peek_register(0x01),
                  rtcf.peek_register(0x02), rtcf.peek_register(0x03),
                  rtcf.peek_register(0x04), rtcf.peek_register(0x05),
                  rtcf.peek_register(0x06)));
    }

    // RTC-20 — Task 28 fixed-time survives reset(). The DS1307 is
    // battery-backed and NextZXOS soft-resets mid-boot; I2cRtc::reset()
    // must preserve has_fixed_time_/fixed_tm_ so the post-reset snapshot
    // still encodes the pinned datetime (not the host clock).
    {
        I2cRtc rtcf;
        std::tm t{};
        (void)parse_rtc_datetime("2026-07-10 12:34:56", t);
        rtcf.set_fixed_time(t);
        rtcf.reset();                                   // soft-reset equivalent
        const bool still_fixed = rtcf.has_fixed_time();
        rtcf.start();                                   // re-snapshot after reset
        const bool regs_ok =
            rtcf.peek_register(0x00) == 0x56 &&
            rtcf.peek_register(0x02) == 0x12 &&
            rtcf.peek_register(0x04) == 0x10 &&
            rtcf.peek_register(0x06) == 0x26;
        check("RTC-20",
              "Task 28 fixed-time survives reset() — battery-backed DS1307; "
              "NextZXOS mid-boot soft reset must not fall back to host clock",
              still_fixed && regs_ok,
              fmt("still_fixed=%d sec=0x%02x hr=0x%02x date=0x%02x yr=0x%02x "
                  "(want 1, 56, 12, 10, 26)",
                  still_fixed ? 1 : 0,
                  rtcf.peek_register(0x00), rtcf.peek_register(0x02),
                  rtcf.peek_register(0x04), rtcf.peek_register(0x06)));
    }

    // RTC-21 — Task 28 fixed-time honours 12h mode. With the clock pinned
    // to 15:00 and mode_12h_ set (reg 0x02 bit 6 via I2C write), the next
    // snapshot must encode hours as 12h BCD: 3 PM → 0x40|0x20|0x03 = 0x63
    // (DS1307 datasheet §Hours Register).
    {
        I2cRtc rtcf;
        I2cController i2cf;
        i2cf.attach_device(0x68, &rtcf);
        std::tm t{};
        (void)parse_rtc_datetime("2026-07-10 15:00:00", t);
        rtcf.set_fixed_time(t);
        i2c_start(i2cf);
        (void)i2c_send_byte(i2cf, 0xD0);                // DS1307 write
        (void)i2c_send_byte(i2cf, 0x02);                // pointer = hours
        (void)i2c_send_byte(i2cf, 0x40);                // mode=12h
        i2c_stop(i2cf);
        rtcf.start();                                   // re-snapshot in 12h mode
        const uint8_t hr = rtcf.peek_register(0x02);
        check("RTC-21",
              "Task 28 fixed-time + 12h mode — pinned 15:00 encodes as 3 PM "
              "(0x63: mode bit 6 + PM bit 5 + BCD 03)",
              hr == 0x63,
              fmt("hr=0x%02x (want 0x63)", hr));
    }

    // RTC-22 — Task 28 parse_rtc_datetime() contract: accepts both the
    // space and ISO-8601 'T' separators (identical result); rejects
    // garbage, trailing characters, out-of-range fields, and invalid
    // calendar dates (Feb 30, Feb 29 on non-leap years); is timezone/
    // DST-independent (a datetime inside a host DST gap, e.g.
    // 2026-03-29 02:30 in CET, must parse — review finding, 2026-07-10);
    // computes tm_wday without mktime (2026-03-29 is a Sunday, Feb 29
    // 2024 is a Thursday).
    {
        std::tm a{}, b{}, gap{}, leap{}, dummy{};
        const bool space_ok = parse_rtc_datetime("2026-07-10 12:34:56", a);
        const bool iso_ok   = parse_rtc_datetime("2026-07-10T12:34:56", b);
        const bool same =
            a.tm_year == b.tm_year && a.tm_mon == b.tm_mon &&
            a.tm_mday == b.tm_mday && a.tm_hour == b.tm_hour &&
            a.tm_min == b.tm_min && a.tm_sec == b.tm_sec &&
            a.tm_wday == b.tm_wday;
        // DST-gap datetime: must parse regardless of the host TZ, with
        // the correct weekday (Sunday = 0).
        const bool gap_ok = parse_rtc_datetime("2026-03-29 02:30:00", gap)
                            && gap.tm_wday == 0;
        // Leap-year handling: Feb 29 valid in 2024 (Thursday = 4),
        // invalid in 2026 (and 1900, a non-leap century year).
        const bool leap_ok = parse_rtc_datetime("2024-02-29 00:00:00", leap)
                             && leap.tm_wday == 4;
        const bool rej_nonleap  = !parse_rtc_datetime("2026-02-29 00:00:00", dummy);
        const bool rej_garbage  = !parse_rtc_datetime("bogus", dummy);
        const bool rej_trailing = !parse_rtc_datetime("2026-07-10 12:34:56garbage", dummy);
        const bool rej_range    = !parse_rtc_datetime("2026-13-01 00:00:00", dummy);
        const bool rej_rollover = !parse_rtc_datetime("2026-02-30 10:00:00", dummy);
        check("RTC-22",
              "Task 28 parse_rtc_datetime — space/'T' forms equivalent; TZ/DST-"
              "independent (DST-gap datetime accepted); leap years handled; "
              "garbage / trailing chars / out-of-range / invalid dates rejected",
              space_ok && iso_ok && same && gap_ok && leap_ok && rej_nonleap &&
                  rej_garbage && rej_trailing && rej_range && rej_rollover,
              fmt("space=%d iso=%d same=%d gap=%d(wday=%d) leap=%d(wday=%d) rej: "
                  "nonleap=%d garbage=%d trailing=%d range=%d rollover=%d",
                  space_ok ? 1 : 0, iso_ok ? 1 : 0, same ? 1 : 0,
                  gap_ok ? 1 : 0, gap.tm_wday, leap_ok ? 1 : 0, leap.tm_wday,
                  rej_nonleap ? 1 : 0, rej_garbage ? 1 : 0, rej_trailing ? 1 : 0,
                  rej_range ? 1 : 0, rej_rollover ? 1 : 0));
    }
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

    // INT-07 — UART RX request-mask asymmetry. VHDL zxnext.vhd:1941-1944
    // request-line shape is
    //   uartN_rx_near_full OR (uartN_rx_avail AND NOT nr_c6(*))
    // The asymmetry lives in the Emulator on_rx_interrupt callback (the
    // bare Uart channel emits per-byte on_rx_available unconditionally —
    // the mask is an emulator-tier concern paired with NR 0xC6 storage).
    // RE-HOME: see test/uart/uart_integration_test.cpp INT-07 — verifies
    // that with NR 0xC6 bit 1 set + bit 0 clear, a single per-byte avail
    // does NOT latch UART0_RX status until the FIFO crosses the 3/4
    // near-full threshold. Closes G134.
}

// ══════════════════════════════════════════════════════════════════════
// Group 13: NR 0xA0 Pi peripheral enable (G135) — RE-HOMED
// VHDL: zxnext.vhd:1241, 2278-2281, 5080, 5560-5561, 6188-6189
// ══════════════════════════════════════════════════════════════════════

static void test_group13_nr_a0() {
    set_group("NR-A0");
    // NR_A0-01 / NR_A0-02 / NR_A0-03 are NR-write-handler + bit fan-out
    // assertions — they live on the integration tier (Emulator + NextReg
    // + I2cController) and not against the bare Uart/I2c peripherals.
    // RE-HOME: see test/uart/uart_integration_test.cpp NR_A0-INT —
    //   NR_A0-01 (write/read handler + reset 0x00 + read mask 0x39),
    //   NR_A0-02 (bit 4 pi_uart_en accessor / not yet wired into UART1
    //              GPIO mux — still WONT pending the GPIO mux model),
    //   NR_A0-03 (bit 3 pi_i2c1_en gates I2C1 read-side wired-AND).
    // Closes G135 for the directly-observable subset (write handler +
    // read mask + I2C bit-3 fan-out); UART1 GPIO mux remains a deferred
    // back-end wiring item (no test wants it yet).
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
// Group 14: ESP-01 / Wi-Fi UART bridge (G39)
// VHDL: zxnext.vhd:3335-3421. ESP-side AT command set lives off-chip.
// Distinct from G135 (Pi UART) and G72 (joystick-pin-7 / UART-mode
// injectors). All existing UART rows exercise the dual-UART core; they
// do not exercise an attached ESP module.
// ══════════════════════════════════════════════════════════════════════

static void test_group14_esp() {
    set_group("ESP");

    // ESP-01..04 were WONT here — "a self-contained networking feature"
    // nobody had built. GH #25 built it, so the revisit-trigger fired and the
    // four rows are LIVE, re-homed to the suite that can reach the real
    // thing. They need a whole `Emulator` (NR 0xC6 / NR 0xCA for the IM2
    // vector, NR 0x02 for the reset line, and `setup_esp` to construct the
    // ThreadedEsp + EspGatedTransport a user's `--esp` builds), none of which
    // this bare-`Uart` suite has.
    //
    // RE-HOME: see test/uart/uart_integration_test.cpp ESP-01 — guest TX on UART 0
    //   egresses to the real emulated ESP-01, which answers, instead of to
    //   the channel loopback (zxnext.vhd:1611-1612, :3381).
    // RE-HOME: see test/uart/uart_integration_test.cpp ESP-02 — the ESP's reply
    //   lands in the UART 0 RX FIFO and raises the UART0_RX IM2 vector under
    //   the NR 0xC6 request mask (zxnext.vhd:1941-1944).
    // RE-HOME: see test/uart/uart_integration_test.cpp ESP-03 — NR 0x02 bit 7
    //   (o_RESET_PERIPHERAL) latches and reads back, and in v1.0 drives NO
    //   device reset (zxnext.vhd:5119, :1579; design doc §4.2 and §10 —
    //   nextsync's recovery path is a v1.1 extension point).
    // RE-HOME: see test/uart/uart_integration_test.cpp ESP-04 — with no backend
    //   attached, UART 0 keeps the loopback these 116 rows observe.
    //
    // The socket half — a real TCP connect, AT+CIPSEND and +IPD framing
    // against a live peer — is the `esp-loopback-func` regression row.
    (void)0;
}

// ══════════════════════════════════════════════════════════════════════
// Group 15: Task 27 C1 — byte-level tick() accumulator seam
// ══════════════════════════════════════════════════════════════════════
//
// UartChannel::tick(N) was rewritten from a per-master-cycle countdown
// loop to closed-form arithmetic that iterates once per byte boundary.
// These rows pin the seam with exact byte-boundary positions: at the
// default hard-reset configuration the byte time is prescaler 243
// (uart.vhd:318-320 hard-reset value, 115200 @ 28 MHz) x 10 frame bits
// (8N1: start + 8 data + 1 stop; frame hard-reset 0x18, uart.vhd:297-299)
// = 2430 master cycles, and back-to-back FIFO bytes are exactly one
// byte time apart.

static void test_group15_c1_accumulator() {
    set_group("TX-C1");
    constexpr uint32_t T = 243 * 10;   // byte time in 28 MHz master cycles

    // TX-C1-ACC-01 — a single span crossing a byte boundary emits the
    // next byte inside the span; the following boundary is exact to the
    // single cycle; on_tx_empty fires exactly at the last byte's end.
    {
        UartChannel ch;
        ch.hard_reset();
        int tx = 0, empty = 0;
        ch.on_tx_byte  = [&](uint8_t) { ++tx; };
        ch.on_tx_empty = [&]() { ++empty; };
        ch.write_tx(0x11);
        ch.write_tx(0x22);
        ch.write_tx(0x33);

        ch.tick(1);                    // byte 1 starts (cycle 0)
        const bool started_one = (tx == 1);
        ch.tick(T + T / 2);            // one span across the boundary at T
        const bool two_after_span = (tx == 2);
        ch.tick(T / 2 - 1);            // one cycle short of boundary at 2T
        const bool still_two = (tx == 2);
        ch.tick(1);                    // exactly cycle 2T: byte 3 starts
        const bool three_at_edge = (tx == 3 && empty == 0);
        ch.tick(T - 1);                // one short of byte 3's end at 3T
        const bool no_early_empty = (empty == 0);
        ch.tick(1);                    // cycle 3T: done, FIFO drained
        const uint8_t status = ch.read_status();
        const bool drained = (tx == 3 && empty == 1
                              && !ch.tx_busy() && (status & 0x10) != 0);

        check("TX-C1-ACC-01",
              "single tick span across a byte boundary: bytes exactly "
              "prescaler*frame_bits=2430 cycles apart [uart.vhd:297-299,"
              "318-320]; boundaries exact to one cycle; tx_empty at end",
              started_one && two_after_span && still_two && three_at_edge
                  && no_early_empty && drained,
              fmt("tx=%d empty=%d started=%d span2=%d still2=%d edge3=%d "
                  "noearly=%d status=0x%02x",
                  tx, empty, started_one, two_after_span, still_two,
                  three_at_edge, no_early_empty, status));
    }

    // TX-C1-ACC-02 — one span draining several FIFO bytes: 4 queued
    // bytes all start within a single tick(4T) call (starts at 0, T,
    // 2T, 3T); the last completion lands exactly one cycle later.
    {
        UartChannel ch;
        ch.hard_reset();
        int tx = 0, empty = 0;
        ch.on_tx_byte  = [&](uint8_t) { ++tx; };
        ch.on_tx_empty = [&]() { ++empty; };
        for (uint8_t b : {0x10, 0x20, 0x30, 0x40}) ch.write_tx(b);

        ch.tick(4 * T);                // cycles 0..4T-1
        const bool four_started = (tx == 4 && empty == 0 && ch.tx_busy());
        ch.tick(1);                    // cycle 4T: byte 4 completes
        const bool done = (tx == 4 && empty == 1 && !ch.tx_busy());

        check("TX-C1-ACC-02",
              "one tick(4*2430) span drains 4 FIFO bytes back-to-back "
              "(starts at 0/T/2T/3T); last completion exactly at 4T "
              "[uart.vhd:297-299,318-320]",
              four_started && done,
              fmt("tx=%d empty=%d four_started=%d done=%d",
                  tx, empty, four_started, done));
    }
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
    test_group13_nr_a0();       std::printf("  Group NR-A0 done\n");
    test_group14_esp();         std::printf("  Group ESP   done\n");
    test_group15_c1_accumulator(); std::printf("  Group TX-C1 done\n");

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
