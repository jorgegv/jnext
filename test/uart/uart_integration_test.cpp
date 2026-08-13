// UART Integration Test — full-machine rows re-homed from
// test/uart/uart_test.cpp (Phase 3 Wave C of the TASK3-UART-I2C skip-
// reduction plan, 2026-04-24).
//
// These 10 plan rows cannot be exercised against the bare Uart/I2c
// peripherals — they span NextReg (NR 0xC6 / NR 0x83) + Im2Controller
// (UART0/1 RX/TX priority slots) + the Emulator port-dispatch layer
// that wires 0x103B / 0x113B / 0x133B-0x163B and gates them on
// `internal_port_enable(10)` (I2C) / `internal_port_enable(12)` (UART).
// They live on the integration tier, observable via the same port path
// a real Z80 uses (OUT 0x243B,reg; IN 0x253B; OUT 0x103B/...; IN 0x...).
//
// Reference plan: doc/design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md
//                 §Phase 2 Wave C + §Phase 3.
// Reference structural template: test/ctc_interrupts/ctc_interrupts_test.cpp,
//                                test/ula/ula_integration_test.cpp.
//
// Run: ./build/test/uart_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"
#include "peripheral/joy_uart_source.h"
#include "peripheral/uart_device.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
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
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

// printf-style detail formatter for check() callers that need runtime values.
static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

std::string detail_eq(uint8_t got, uint8_t expected) {
    return "got=" + hex2(got) + " expected=" + hex2(expected);
}

// Space-separated hex render of a byte stream, for JOY-* failure details.
std::string bytes_hex(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (uint8_t b : bytes) {
        if (!out.empty()) out += ' ';
        out += hex2(b);
    }
    return out;
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Fresh-state idiom: re-initialise the emulator before each scenario so
// cross-test state (scheduler queues, latched interrupt status, etc.)
// cannot leak between scopes.
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Read NextREG register through the real port path (OUT 0x243B,reg;
// IN 0x253B). Mirrors the idiom used by ctc_interrupts_test.cpp.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// Write NextREG register through the real port path.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Latch any pending Im2 int_req edges (inject_rx / on_tx_empty call
// raise_req(); the wrapper edge detect runs inside Im2Controller::tick()
// which is driven by Emulator::run_frame in the normal flow).
//
// run_frame() also ticks the UART (master_cycles per instruction), which
// drains TX-FIFO bytes through byte_transfer_ticks() (~2430 cycles at the
// default 115200-baud/28MHz prescaler). A single full frame is ~567k
// master cycles — comfortably more than enough to drain a few-byte TX
// FIFO and fire `on_tx_empty`.
static void settle(Emulator& emu) {
    emu.run_frame();
}

// ══════════════════════════════════════════════════════════════════════
// Section INT — UART IM2 interrupt vectors (INT-01..06)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:1930-1944, :1949-1950:
//   Priority slot 1 = UART0 RX : uart0_rx_near_full OR (uart0_rx_avail AND NOT nr_c6(1))
//                     int_en   : nr_c6(1) OR nr_c6(0)
//   Priority slot 2 = UART1 RX : uart1_rx_near_full OR (uart1_rx_avail AND NOT nr_c6(5))
//                     int_en   : nr_c6(5) OR nr_c6(4)
//   Priority slot 12= UART0 TX : uart0_tx_empty
//                     int_en   : nr_c6(2)
//   Priority slot 13= UART1 TX : uart1_tx_empty
//                     int_en   : nr_c6(6)
//
// Observable: after ticking, NR 0xCA packs UART int_status per
// zxnext.vhd:6253-6254 and src/cpu/im2.cpp:313-323:
//   bit 6 = UART1_TX, bits 5:4 = UART1_RX (duplicated), bit 2 = UART0_TX,
//   bits 1:0 = UART0_RX (duplicated), bits 7/3 = literal 0.
// ══════════════════════════════════════════════════════════════════════

static void test_uart_im2_interrupts(Emulator& emu) {
    set_group("UART-INT");

    // INT-01 — UART 0 rx_avail → NR 0xCA bit 0 (UART0_RX status).
    // NR 0xC6 bit 0 set (avail enable) + bit 1 clear. After inject_rx on
    // channel 0, run one frame to let Im2Controller::tick latch the
    // rising edge on UART0_RX int_req. NR 0xCA bits 1:0 must be set.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x01);          // UART0 RX avail enable
        emu.uart().inject_rx(0, 0x42);
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-01",
              "UART0 rx_avail fires UART0_RX (vector 1) with NR 0xC6 bit 0 set "
              "[zxnext.vhd:1941-1944, :1949-1950; im2.cpp:313-323]",
              (ca & 0x03) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bits 1:0 set)");
    }

    // INT-02 — UART 0 rx_near_full path also fires UART0_RX status even
    // when only NR 0xC6 bit 1 is set (bit 0 clear). Per VHDL:1950 int_en
    // composition, `nr_c6(1) OR nr_c6(0)` → int_en = 1 when either bit
    // set; the VHDL int_req masks the avail path (VHDL:1943) but the
    // near-full path is unconditional. Fill the 512-byte RX FIFO past the
    // 3/4 near-full threshold (384 bytes).
    //
    // PLAN DRIFT NOTE (2026-04-24): the current jnext UART model fires
    // `on_rx_available` on EVERY `inject_rx` (uart.cpp:205-207) without
    // the VHDL avail-vs-near-full distinction. With nr_c6(1) set alone,
    // int_en = 1 and the first inject already latches UART0_RX status.
    // This row asserts the observable "near-full enable path produces a
    // UART0_RX status bit" — the subtler VHDL mask (avail blocked when
    // bit 0 clear AND bit 1 set) is not modelled. Wave B (RX bit-level
    // engine) owns the near-full separation fix; flagged for that plan.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x02);          // UART0 RX near-full enable only
        for (int i = 0; i < 400; ++i)       // past 3/4 threshold of 512
            emu.uart().inject_rx(0, static_cast<uint8_t>(i & 0xFF));
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-02",
              // :1943 is the UART0 term of im2_int_req; :1942 immediately
              // above is UART1's, which this row does not exercise
              // (zxnext.vhd:1941-1944 is the whole concatenation). GH #151.
              "UART0 rx_near_full fires UART0_RX with NR 0xC6 bit 1 set only "
              "(near-full override) [zxnext.vhd:1943, :1950; plan-drift note]",
              (ca & 0x03) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bits 1:0 set)");
    }

    // INT-03 — UART 1 rx_avail → NR 0xCA bits 5:4 (UART1_RX status).
    // NR 0xC6 bit 4 set (avail enable ch1) + bit 5 clear.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x10);          // UART1 RX avail enable
        emu.uart().inject_rx(1, 0x7E);
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-03",
              "UART1 rx_avail fires UART1_RX (vector 2) with NR 0xC6 bit 4 set "
              "[zxnext.vhd:1941-1944, :1949-1950]",
              (ca & 0x30) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bits 5:4 set)");
    }

    // INT-04 — UART 1 rx_near_full → UART1_RX status. NR 0xC6 bit 5 set,
    // bit 4 clear. Fill channel 1 RX past 3/4 threshold.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x20);          // UART1 RX near-full enable only
        for (int i = 0; i < 400; ++i)
            emu.uart().inject_rx(1, static_cast<uint8_t>(i & 0xFF));
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-04",
              "UART1 rx_near_full fires UART1_RX with NR 0xC6 bit 5 set only "
              "[zxnext.vhd:1942, :1950]",
              (ca & 0x30) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bits 5:4 set)");
    }

    // INT-05 — UART 0 tx_empty → NR 0xCA bit 2 (UART0_TX status).
    // Enable NR 0xC6 bit 2 (UART0 TX enable). Write a byte via the TX
    // port path; let run_frame() tick uart_ to drain tx_fifo (~2430 master
    // cycles at default baud) so `on_tx_empty` fires → `on_tx_interrupt`
    // → `im2_.raise_req(UART0_TX)`. NR 0xCA bit 2 must be set.
    //
    // Loopback note: the default UartChannel::on_tx_byte is empty, so
    // drained TX bytes loop back into the RX FIFO. That also raises
    // UART0_RX int_status on edge (VHDL:160 gates neither int_en nor
    // int_unq — see im2.cpp:685-687). We filter only bit 2 here; the
    // loopback noise lands in bits 1:0, which is not asserted.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x04);          // UART0 TX enable (bit 2)
        // Select channel 0 via the select port, then write a TX byte.
        emu.port().out(0x153B, 0x00);       // ch0 selected (bit 6 = 0)
        emu.port().out(0x133B, 0xA5);       // write TX byte
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-05",
              "UART0 tx_empty fires UART0_TX (vector 12) with NR 0xC6 bit 2 set "
              "[zxnext.vhd:1941, :1949]",
              (ca & 0x04) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bit 2 set)");
    }

    // INT-06 — UART 1 tx_empty → NR 0xCA bit 6 (UART1_TX status).
    // Enable NR 0xC6 bit 6 (UART1 TX enable). Select channel 1, write TX.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x40);          // UART1 TX enable (bit 6)
        emu.port().out(0x153B, 0x40);       // ch1 selected (bit 6 = 1)
        emu.port().out(0x133B, 0x5A);       // write TX byte on channel 1
        settle(emu);
        const uint8_t ca = nr_read(emu, 0xCA);
        check("INT-06",
              "UART1 tx_empty fires UART1_TX (vector 13) with NR 0xC6 bit 6 set "
              "[zxnext.vhd:1941, :1949]",
              (ca & 0x40) != 0,
              "NR 0xCA=" + hex2(ca) + " (expected bit 6 set)");
    }

    // INT-07 — UART RX request-mask asymmetry (G134). VHDL zxnext.vhd:1941-1944
    // request shape is uart0_rx_near_full OR (uart0_rx_avail AND NOT
    // nr_c6_int_en_2_210(1)). With NR 0xC6 bit 1 set + bit 0 clear:
    //   - int_en for UART0_RX = bit1 OR bit0 = 1 (latch is armed)
    //   - per-byte rx_avail is suppressed by AND-NOT-bit1 → a single
    //     inject_rx must NOT fire vector 1.
    //   - rx_near_full path is OR'd in unconditionally → once the FIFO
    //     crosses the 3/4 (= 384 / 512) threshold, the request fires and
    //     UART0_RX status bits 1:0 in NR 0xCA latch.
    //
    // This is the asymmetry between int_en (bit1 OR bit0) and int_req
    // (near_full OR (avail AND NOT bit1)). Pre-G134 the emulator raised on
    // every byte regardless of the mask — see the PLAN DRIFT NOTE on INT-02
    // above; the proper near-full-only behaviour is now active.
    {
        fresh(emu);
        nr_write(emu, 0xC6, 0x02);          // bit 1 set, bit 0 clear

        // Single byte first — must NOT raise UART0_RX status because the
        // per-byte avail is masked by NR 0xC6 bit 1.
        emu.uart().inject_rx(0, 0x42);
        settle(emu);
        const uint8_t ca_one = nr_read(emu, 0xCA);
        const bool one_byte_silent = (ca_one & 0x03) == 0;

        // Now cross the 3/4 near-full threshold (384 of 512). 399 more
        // injects (1 already in FIFO → 400 total) puts us firmly past.
        for (int i = 0; i < 399; ++i) {
            emu.uart().inject_rx(0, static_cast<uint8_t>(i & 0xFF));
        }
        settle(emu);
        const uint8_t ca_full = nr_read(emu, 0xCA);
        const bool near_full_fires = (ca_full & 0x03) != 0;

        check("INT-07",
              "UART RX request shape is near_full OR (avail AND NOT NR 0xC6 bit 1) — "
              "single per-byte avail must NOT fire when bit 1 is set, near-full does "
              "[zxnext.vhd:1941-1944, G134]",
              one_byte_silent && near_full_fires,
              fmt("after 1 byte: NR_CA=0x%02X (expect bits1:0 clear); after 400: NR_CA=0x%02X "
                  "(expect bits1:0 set)", ca_one, ca_full));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section GATE — Port-enable gates (GATE-01..03 + I2C-10)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2392-2420, :5499-5509:
//   internal_port_enable[31:0] = NR 0x85 : NR 0x84 : NR 0x83 : NR 0x82
//                                (bits 31:24 : 23:16 : 15:8  : 7:0)
//   bit 8  (NR 0x83 bit 0) → port_divmmc_io_en
//   bit 10 (NR 0x83 bit 2) → port_i2c_io_en   (0x103B / 0x113B)
//   bit 12 (NR 0x83 bit 4) → port_uart_io_en  (0x133B-0x163B)
//
// NOTE: the RE-HOME comments in test/uart/uart_test.cpp (lines 1294-1305)
// and the plan doc (§Cluster 6, 7) cite "NR 0x82 bit 4" / "NR 0x82 bit 2".
// Per VHDL:2392 the low byte of internal_port_enable comes from NR 0x82
// and the NEXT byte (bits 8..15) from NR 0x83. Bits 10 and 12 therefore
// map to NR 0x83 — confirmed by the DivMMC gate at emulator.cpp:1467-1476
// which also uses NR 0x83 (bit 0 = bit 8 = port_divmmc_io_en). Plan-doc
// text is imprecise; this suite cites the VHDL-correct mapping.
//
// At reset, NR 0x82/0x83/0x84 load 0xFF (zxnext.vhd:5052-5057 via
// src/port/nextreg.cpp:38-45), so the gates are open by default and
// existing tests that hit these ports do not regress.
// ══════════════════════════════════════════════════════════════════════

static void test_port_enable_gates(Emulator& emu) {
    set_group("GATE");

    // GATE-01 — UART port enable. Clear NR 0x83 bit 4; reads of
    // 0x133B / 0x143B / 0x153B / 0x163B must return 0xFF, writes must be
    // silently ignored (TX FIFO unchanged). Re-enabling restores live
    // behaviour (the TX status register at 0x133B has bit 4 = tx_empty =
    // 1 at reset, so the read flips from 0xFF → a status byte with at
    // least bit 4 set).
    {
        fresh(emu);
        // Gate off: NR 0x83 bits cleared except bit 4 masked out.
        // Keep NR 0x83 bit 0 (DivMMC) and bit 2 (I2C) set for parity with
        // reset; only clear bit 4 (UART). Reset default = 0xFF.
        nr_write(emu, 0x83, 0xFF & ~0x10);

        const uint8_t r_133b_off = emu.port().in(0x133B);
        const uint8_t r_143b_off = emu.port().in(0x143B);
        const uint8_t r_153b_off = emu.port().in(0x153B);
        const uint8_t r_163b_off = emu.port().in(0x163B);

        // Writes should be ignored: write a TX byte while gated → FIFO
        // stays empty → after re-enabling, reading the TX status (bit 4 =
        // tx_empty) should still show tx_empty = 1 (nothing was queued).
        emu.port().out(0x153B, 0x00);       // ch0 select (also gated → no-op)
        emu.port().out(0x133B, 0xAA);       // TX write (gated → ignored)

        // Re-enable the UART gate.
        nr_write(emu, 0x83, 0xFF);
        const uint8_t r_133b_on = emu.port().in(0x133B);

        const bool reads_0xff =
            r_133b_off == 0xFF && r_143b_off == 0xFF &&
            r_153b_off == 0xFF && r_163b_off == 0xFF;
        // After re-enable, the TX status has bit 4 = tx_empty = 1
        // (the gated OUT dropped the byte; FIFO is empty).
        const bool tx_empty_after = (r_133b_on & 0x10) != 0;

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "gated reads: 133B=0x%02X 143B=0x%02X 153B=0x%02X 163B=0x%02X; "
                      "post-enable 133B=0x%02X (tx_empty bit 4 = %d)",
                      r_133b_off, r_143b_off, r_153b_off, r_163b_off,
                      r_133b_on, (r_133b_on & 0x10) >> 4);
        check("GATE-01",
              "UART port enable gate: NR 0x83 bit 4 → ports 0x133B-0x163B; when "
              "closed reads=0xFF + writes ignored "
              "[zxnext.vhd:2420, :2392; emulator.cpp register_io_ports]",
              reads_0xff && tx_empty_after, detail);
    }

    // GATE-02 — I2C port enable. Clear NR 0x83 bit 2; reads of 0x103B /
    // 0x113B must return 0xFF, writes must be silently ignored.
    //
    // Observable-write side: the I2C bus lines at reset are released
    // (SCL=1, SDA=1), so a released read returns 0xFF for both ports
    // anyway. We verify the gate-drop by toggling write (write 0 = pull
    // SCL/SDA low) then reading. With gate CLOSED the write is dropped
    // and the re-opened read still reads the released state (0xFF-ish).
    // With gate OPEN the write would land and the follow-on read would
    // differ. This double-check distinguishes "gate off" from "gate
    // always-off" (the simpler read=0xFF check could be tautological
    // because the bus is released at reset).
    {
        fresh(emu);
        nr_write(emu, 0x83, 0xFF & ~0x04);  // gate OFF

        const uint8_t r_103b_off = emu.port().in(0x103B);
        const uint8_t r_113b_off = emu.port().in(0x113B);

        // Attempt to pull SCL/SDA low while gated — writes should drop.
        emu.port().out(0x103B, 0x00);       // would write_scl(0)
        emu.port().out(0x113B, 0x00);       // would write_sda(0)

        // Re-open the gate; reads should now reflect live bus state.
        nr_write(emu, 0x83, 0xFF);
        const uint8_t r_103b_on = emu.port().in(0x103B);
        const uint8_t r_113b_on = emu.port().in(0x113B);

        const bool reads_0xff = r_103b_off == 0xFF && r_113b_off == 0xFF;
        // If the gated OUTs had landed, SCL/SDA would still be pulled low
        // after re-opening. VHDL read_scl/sda return the AND of the bus
        // lines (i2c_controller composition); released = bit 0 = 1. We
        // only assert that the reads post-enable are non-zero (i.e. the
        // bus was NOT silently driven low by the ignored writes).
        const bool writes_dropped = (r_103b_on & 0x01) != 0 && (r_113b_on & 0x01) != 0;

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "gated reads: 103B=0x%02X 113B=0x%02X; "
                      "post-enable 103B=0x%02X 113B=0x%02X "
                      "(bit 0 should be 1 if writes were dropped)",
                      r_103b_off, r_113b_off, r_103b_on, r_113b_on);
        check("GATE-02",
              "I2C port enable gate: NR 0x83 bit 2 → ports 0x103B/0x113B; when "
              "closed reads=0xFF + writes ignored "
              "[zxnext.vhd:2418, :2392]",
              reads_0xff && writes_dropped, detail);
    }

    // GATE-03 — NR 0x83 bit-to-port mapping spot check per zxnext.vhd:
    // 2397-2466 (internal_port_enable concat at :2392 + the per-port
    // fan-out at :2397 onward). Exercise THREE documented positions:
    //   (a) NR 0x83 bit 0 (bit 8 of internal_port_enable) → DivMMC 0xE3
    //       (zxnext.vhd:2412) — mirrors emulator.cpp:1467-1476.
    //   (b) NR 0x83 bit 2 (bit 10) → I2C 0x103B (zxnext.vhd:2418).
    //   (c) NR 0x83 bit 4 (bit 12) → UART 0x143B (zxnext.vhd:2420).
    //
    // Unique probes per port (reset-state values, all gates open):
    //   0xE3    → DivMMC::read_control = 0x00 (control reg default);
    //              when gated the port returns 0xFF → a clean 0x00 ↔ 0xFF
    //              flip is directly observable.
    //   0x103B  → I2cController::read_scl = 0xFF (bus released = 1, upper
    //              7 bits always 1). GATED also returns 0xFF → NOT directly
    //              distinguishable from open via a bare read. This matches
    //              VHDL — real software cannot tell gated-I2C from idle
    //              I2C by reading alone. We probe the gate by WRITE-then-
    //              READ instead: a gated OUT 0x103B is dropped; an un-
    //              gated one would drive scl_ low. We re-open the gate
    //              afterwards and read back to confirm the write landed
    //              (scl_ still pulled low) vs was dropped (scl_ stayed 1).
    //   0x143B  → Uart::read(0) = read_rx returns 0 on empty FIFO; when
    //              gated the port returns 0xFF → clean 0x00 ↔ 0xFF flip.
    //
    // The invariant tested is INDEPENDENCE of the three bits: clearing
    // one bit affects only the matching port, not the other two.
    {
        fresh(emu);

        // Sanity: all gates OPEN at reset. E3=0x00, 0x143B=0x00.
        const uint8_t e3_base    = emu.port().in(0x00E3);
        const uint8_t uart_base  = emu.port().in(0x143B);
        (void)e3_base; (void)uart_base;  // logged in detail below

        // (a) NR 0x83 bit 0 off → only DivMMC gated.
        nr_write(emu, 0x83, 0xFF & ~0x01);
        const uint8_t e3_a   = emu.port().in(0x00E3);    // expect 0xFF (gated)
        const uint8_t uart_a = emu.port().in(0x143B);    // expect 0x00 (still open)
        // I2C indirectly: attempt to pull SCL low via OUT 0x103B,0; this
        // lands (gate open) and leaves scl_=0. Re-open DivMMC, read back.
        emu.port().out(0x103B, 0x00);
        nr_write(emu, 0x83, 0xFF);                       // restore for clean read
        const uint8_t i2c_a_land = emu.port().in(0x103B);  // expect 0xFE (scl_=0)

        // (b) NR 0x83 bit 2 off → only I2C gated.
        fresh(emu);
        nr_write(emu, 0x83, 0xFF & ~0x04);
        const uint8_t e3_b   = emu.port().in(0x00E3);    // expect 0x00 (still open)
        const uint8_t uart_b = emu.port().in(0x143B);    // expect 0x00 (still open)
        emu.port().out(0x103B, 0x00);                    // GATED write → dropped
        nr_write(emu, 0x83, 0xFF);                       // restore
        const uint8_t i2c_b_land = emu.port().in(0x103B);  // expect 0xFF (scl_ still 1)

        // (c) NR 0x83 bit 4 off → only UART gated.
        fresh(emu);
        nr_write(emu, 0x83, 0xFF & ~0x10);
        const uint8_t e3_c   = emu.port().in(0x00E3);    // expect 0x00 (still open)
        const uint8_t uart_c = emu.port().in(0x143B);    // expect 0xFF (gated)
        // I2C still open: writing SCL should land.
        emu.port().out(0x103B, 0x00);
        nr_write(emu, 0x83, 0xFF);
        const uint8_t i2c_c_land = emu.port().in(0x103B);  // expect 0xFE (scl_=0)

        const bool a_ok =
            e3_a       == 0xFF &&   // DivMMC gated
            uart_a     == 0x00 &&   // UART still open (empty RX = 0)
            i2c_a_land == 0xFE;     // I2C write landed → scl_ pulled low

        const bool b_ok =
            e3_b       == 0x00 &&   // DivMMC open (ctrl reg = 0)
            uart_b     == 0x00 &&   // UART open
            i2c_b_land == 0xFF;     // I2C write dropped → scl_ stayed high

        const bool c_ok =
            e3_c       == 0x00 &&   // DivMMC open
            uart_c     == 0xFF &&   // UART gated
            i2c_c_land == 0xFE;     // I2C still open → write landed

        char detail[256];
        std::snprintf(detail, sizeof(detail),
                      "(a~b0) E3=0x%02X UART=0x%02X I2C-land=0x%02X; "
                      "(b~b2) E3=0x%02X UART=0x%02X I2C-land=0x%02X; "
                      "(c~b4) E3=0x%02X UART=0x%02X I2C-land=0x%02X",
                      e3_a, uart_a, i2c_a_land,
                      e3_b, uart_b, i2c_b_land,
                      e3_c, uart_c, i2c_c_land);
        check("GATE-03",
              "NR 0x83 bits 0/2/4 independently gate DivMMC/I2C/UART "
              "[zxnext.vhd:2412, :2418, :2420, :2392; :5499-5509]",
              a_ok && b_ok && c_ok, detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section I2C — I2C port-enable gate detail row (I2C-10)
// ══════════════════════════════════════════════════════════════════════

static void test_i2c_port_gate(Emulator& emu) {
    set_group("I2C");

    // I2C-10 — internal_port_enable(10) routing. This is the same
    // underlying mechanism as GATE-02 (NR 0x83 bit 2 → port_i2c_io_en);
    // the row exists in the UART/I2C plan as a distinct requirement row
    // for I2C port gating. Assert the identity at the emulator fixture
    // tier: with the gate CLOSED, both I2C ports return 0xFF.
    //
    // VHDL: zxnext.vhd:2418 — port_i2c_io_en <= internal_port_enable(10).
    //       zxnext.vhd:2392 — internal_port_enable bits 15:8 = NR 0x83.
    //       Therefore internal_port_enable(10) = nr_83_internal_port_enable(2).
    {
        fresh(emu);
        nr_write(emu, 0x83, 0xFF & ~0x04);  // close I2C gate
        const uint8_t scl = emu.port().in(0x103B);
        const uint8_t sda = emu.port().in(0x113B);

        check("I2C-10",
              "internal_port_enable(10) gates 0x103B/0x113B (same mechanism as "
              "GATE-02) [zxnext.vhd:2418, :2392]",
              scl == 0xFF && sda == 0xFF,
              "gated 103B=" + hex2(scl) + " 113B=" + hex2(sda));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Wave D rows — DUAL-05/06 (dual-UART routing + joystick IO-mode mux).
// ══════════════════════════════════════════════════════════════════════

// Drive the full UART advance so pending TX bytes complete. The
// default Next config runs UART 0 at 115200 @ 28 MHz → ~243 * 10 =
// 2430 master cycles for one 8N1 byte. Tick generously to guarantee
// completion of a single byte regardless of framing changes.
static void tick_uart_byte(Emulator& emu) {
    emu.uart().tick(8000);
}

// DUAL-05 — UART 0 (ESP) vs UART 1 (Pi) channel routing via select reg.
static void test_dual_05_channel_routing(Emulator& emu) {
    set_group("DUAL");
    fresh(emu);

    std::vector<uint8_t> sink0;
    std::vector<uint8_t> sink1;
    auto& ch0 = const_cast<UartChannel&>(emu.uart().channel(0));
    auto& ch1 = const_cast<UartChannel&>(emu.uart().channel(1));
    ch0.on_tx_byte = [&sink0](uint8_t b) { sink0.push_back(b); };
    ch1.on_tx_byte = [&sink1](uint8_t b) { sink1.push_back(b); };

    // Step 1: select channel 0, send 0xAA.
    emu.port().out(0x153B, 0x00);        // select = 0 (ESP / UART 0)
    emu.port().out(0x133B, 0xAA);        // TX byte on currently-selected channel
    tick_uart_byte(emu);

    const bool step1_ok =
        (sink0.size() == 1) && (sink0[0] == 0xAA) && sink1.empty();

    // Step 2: select channel 1, send 0xBB.
    emu.port().out(0x153B, 0x40);        // select = 1 (Pi / UART 1)
    emu.port().out(0x133B, 0xBB);
    tick_uart_byte(emu);

    const bool step2_ok =
        (sink1.size() == 1) && (sink1[0] == 0xBB)
        && (sink0.size() == 1) && (sink0[0] == 0xAA);

    // Uninstall before `sink0`/`sink1` go out of scope. `emu` outlives this
    // function and `fresh()` does NOT clear channel callbacks (it calls
    // Emulator::init → Uart::reset, which resets FIFOs and timers only), so
    // a lambda left installed here keeps a reference to a destroyed stack
    // vector and any later row that transmits on these channels invokes it.
    // That was latent for as long as nothing after DUAL-05 transmitted;
    // the DEV rows below do, which surfaced it as a std::bad_alloc.
    ch0.on_tx_byte = nullptr;
    ch1.on_tx_byte = nullptr;

    check("DUAL-05",
          "uart.vhd gates tx_wr on uart_select_r bit 6; zxnext.vhd:3343-3344 "
          "routes UART 0 TX → ESP pin, UART 1 TX → Pi pin. Selecting a "
          "channel via port 0x153B directs port 0x133B TX writes to that "
          "channel ONLY — cross-talk between channels is impossible",
          step1_ok && step2_ok,
          fmt("step1 ch0=%zu ch1=%zu (want 1/0); step2 ch0=%zu ch1=%zu (want 1/1); "
              "sink0[0]=0x%02X (want 0xAA); sink1[0]=0x%02X (want 0xBB)",
              sink0.size(), sink1.size(),
              sink0.size(), sink1.size(),
              sink0.empty() ? 0 : sink0[0],
              sink1.empty() ? 0 : sink1[0]));
}

// DUAL-06 — joystick IO-mode UART RX multiplex (VHDL zxnext.vhd:3340-3341).
//
// GH #251 — the NR 0x0B values here were 0x80 / 0x81, i.e. bit 7 alone, which
// is what `Emulator::inject_joy_uart_rx` used to gate on. The real enable is
// `joy_iomode_uart_en <= nr_0b_joy_iomode_en AND nr_0b_joy_iomode(1)`
// (zxnext.vhd:3537) — bit 7 AND bit 5 — so this row asserted a mux that hardware
// leaves disconnected, and could not have failed if the gate were wrong. The
// values are now 0xA0 / 0xA1 (mode "10"), and DUAL-07 below covers the case the
// old values actually described.
static void test_dual_06_iomode_rx_mux(Emulator& emu) {
    set_group("DUAL");
    fresh(emu);

    // Step 1: mux enabled (bit7+bit5), iomode_0=0 → joy→UART 0.
    emu.nextreg().write(0x0B, 0xA0);              // en=1, mode="10", iomode_0=0
    emu.inject_joy_uart_rx(0x77);

    // Read UART 0 RX FIFO via port 0x143B after selecting channel 0.
    emu.port().out(0x153B, 0x00);                 // select channel 0
    const uint8_t u0_step1 = emu.port().in(0x143B);
    const bool u1_empty_step1 = emu.uart().channel(1).rx_empty();

    const bool step1_ok = (u0_step1 == 0x77) && u1_empty_step1;

    // Step 2: mux enabled, iomode_0=1 → joy→UART 1.
    emu.nextreg().write(0x0B, 0xA1);              // en=1, mode="10", iomode_0=1
    emu.inject_joy_uart_rx(0x55);

    emu.port().out(0x153B, 0x40);
    const uint8_t u1_step2 = emu.port().in(0x143B);
    const bool u0_empty_step2 = emu.uart().channel(0).rx_empty();

    const bool step2_ok = (u1_step2 == 0x55) && u0_empty_step2;

    // Step 3: iomode_en=0 — mux disabled; byte is dropped.
    emu.nextreg().write(0x0B, 0x00);              // iomode_en=0
    emu.inject_joy_uart_rx(0x33);

    const bool u0_empty_step3 = emu.uart().channel(0).rx_empty();
    const bool u1_empty_step3 = emu.uart().channel(1).rx_empty();

    const bool step3_ok = u0_empty_step3 && u1_empty_step3;

    check("DUAL-06",
          "zxnext.vhd:3340-3341 — joystick-UART RX routes to UART 0 when "
          "NR 0x0B joy_iomode_uart_en=1 & bit0=0, to UART 1 when it is 1 & "
          "bit0=1, and is dropped when the enable is clear",
          step1_ok && step2_ok && step3_ok,
          fmt("step1 u0=0x%02X (want 0x77) u1_empty=%d; step2 u1=0x%02X (want 0x55) "
              "u0_empty=%d; step3 u0_empty=%d u1_empty=%d",
              u0_step1, u1_empty_step1 ? 1 : 0,
              u1_step2, u0_empty_step2 ? 1 : 0,
              u0_empty_step3 ? 1 : 0, u1_empty_step3 ? 1 : 0));
}

// DUAL-07 — the joystick-UART enable is bit 7 AND bit 5, not bit 7 alone
// (GH #251). VHDL zxnext.vhd:3537:
//   joy_iomode_uart_en <= '1' when nr_0b_joy_iomode_en = '1'
//                              and nr_0b_joy_iomode(1) = '1' else '0';
// `nr_0b_joy_iomode(1)` is NR 0x0B bit 5, so the two pin-7 modes that do NOT
// carry a UART — "00" static and "01" CTC-toggled (zxnext.vhd:3515-3524) — must
// route nothing, even with the enable bit set. The pre-fix gate read bit 7
// alone and accepted both.
static void test_dual_07_uart_en_needs_bit5(Emulator& emu) {
    set_group("DUAL");

    // Mode "00" (static pin 7): enable set, bit 5 clear → no UART routing.
    fresh(emu);
    emu.nextreg().write(0x0B, 0x80);              // en=1, mode="00", bit0=0
    emu.inject_joy_uart_rx(0x11);
    const bool mode00_ch0_empty = emu.uart().channel(0).rx_empty();
    emu.nextreg().write(0x0B, 0x81);              // en=1, mode="00", bit0=1
    emu.inject_joy_uart_rx(0x22);
    const bool mode00_ch1_empty = emu.uart().channel(1).rx_empty();

    // Mode "01" (pin 7 toggled by CTC ch3): same — bit 5 is still clear.
    fresh(emu);
    emu.nextreg().write(0x0B, 0x90);              // en=1, mode="01", bit0=0
    emu.inject_joy_uart_rx(0x33);
    const bool mode01_ch0_empty = emu.uart().channel(0).rx_empty();
    emu.nextreg().write(0x0B, 0x91);              // en=1, mode="01", bit0=1
    emu.inject_joy_uart_rx(0x44);
    const bool mode01_ch1_empty = emu.uart().channel(1).rx_empty();

    // Positive control on the SAME emulator, so the negatives above cannot be
    // passing merely because injection is broken: flip bit 5 on and the byte
    // must land. Mode "11" as well as "10", since bit 4 selects the CONNECTOR
    // (zxnext.vhd:3538) and must not affect the enable.
    emu.nextreg().write(0x0B, 0xB0);              // en=1, mode="11", bit0=0
    emu.inject_joy_uart_rx(0x55);
    emu.port().out(0x153B, 0x00);
    const uint8_t mode11_ch0 = emu.port().in(0x143B);

    check("DUAL-07",
          "zxnext.vhd:3537 — joy_iomode_uart_en is NR 0x0B bit 7 AND bit 5, so "
          "pin-7 modes \"00\" (static) and \"01\" (CTC-toggled) route no UART RX "
          "even with bit 7 set, while mode \"11\" does (GH #251)",
          mode00_ch0_empty && mode00_ch1_empty
              && mode01_ch0_empty && mode01_ch1_empty
              && mode11_ch0 == 0x55,
          fmt("mode00 ch0_empty=%d ch1_empty=%d; mode01 ch0_empty=%d ch1_empty=%d "
              "(all want 1); mode11 ch0=0x%02X (want 0x55)",
              mode00_ch0_empty ? 1 : 0, mode00_ch1_empty ? 1 : 0,
              mode01_ch0_empty ? 1 : 0, mode01_ch1_empty ? 1 : 0,
              mode11_ch0));
}

// ══════════════════════════════════════════════════════════════════════
// Section DEV — UartDevice attach/detach seam (DEV-01..04)
// ══════════════════════════════════════════════════════════════════════
//
// Pins src/peripheral/uart_device.h + Uart::attach_device/detach_device.
// The seam is the backend socket for the ESP-01 on UART 0 (zxnext.vhd:1611,
// :3381 — UART 0 is the ESP, UART 1 the Pi header) and must be behaviour-
// preserving when nothing is attached: an unattached channel keeps looping
// TX back into its own RX FIFO, which is what the 116 pre-existing UART rows
// observe.
//
// Every row drives the guest side through the REAL port path (OUT 0x153B to
// select the channel, OUT 0x133B to transmit, IN 0x143B to receive) so the
// seam is exercised exactly as a Z80 program would reach it.
//
// Lifetime discipline in this file: the stubs are stack-allocated and `emu`
// outlives every scope, so each scope MUST detach before its stub dies —
// a leaked attachment would leave a dangling UartDevice* for later rows.
// ══════════════════════════════════════════════════════════════════════

namespace {

// Minimal UartDevice backend: records everything the guest sends, and can
// push bytes the other way on demand. `send_to_guest` is protected on the
// base class, so this stub also proves the intended subclass access path.
class StubUartDevice : public UartDevice {
public:
    void receive(uint8_t byte) override { rx.push_back(byte); }
    void poll() override { ++polls; }

    /// Push one byte toward the guest (models a socket delivering data).
    void send(uint8_t byte) { send_to_guest(byte); }

    std::vector<uint8_t> rx;
    int                  polls = 0;
};

} // namespace

static void test_uart_device_seam(Emulator& emu) {
    set_group("DEV");

    // DEV-01 — attaching a device diverts guest TX to it AND suppresses the
    // loopback. Both halves matter: the first proves the device is wired in,
    // the second proves it REPLACED the default loopback rather than running
    // alongside it (a channel with an ESP on the wire does not also echo the
    // guest's own bytes back at it).
    {
        fresh(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        emu.port().out(0x153B, 0x00);       // select channel 0 (ESP)
        emu.port().out(0x133B, 0xAA);       // guest transmits
        tick_uart_byte(emu);

        const bool device_saw   = (esp.rx.size() == 1) && (esp.rx[0] == 0xAA);
        const bool no_loopback  = emu.uart().channel(0).rx_empty();

        emu.uart().detach_device(0);

        check("DEV-01",
              "Uart::attach_device diverts channel TX to UartDevice::receive and "
              "suppresses the default loopback [uart.cpp deliver_tx_byte; "
              "zxnext.vhd:1611, :3381 UART 0 = ESP]",
              device_saw && no_loopback,
              fmt("device rx=%zu first=0x%02X (want 1/0xAA); ch0 RX empty=%d (want 1)",
                  esp.rx.size(), esp.rx.empty() ? 0 : esp.rx[0],
                  no_loopback ? 1 : 0));
    }

    // DEV-02 — the device's guest-bound sink lands in the real RX FIFO and
    // drives the real IM2 UART0_RX vector, under the NR 0xC6 request mask.
    //
    // The mask is the same asymmetry INT-07 pins for inject_rx (VHDL
    // zxnext.vhd:1941-1944): the request is
    //   near_full OR (avail AND NOT nr_c6(1))
    // so with bit 0 set the per-byte avail fires, and with bit 1 set instead
    // it is suppressed. Asserting BOTH directions is what makes this row
    // discriminative — a sink that bypassed inject_rx could still set the
    // status bit in the positive half, but could not reproduce the mask.
    //
    // NOTE: int_status latches independently of the NR 0xC6 int_EN bits
    // (see the loopback note on INT-05), so the negative control here is the
    // request mask (bit 1), NOT "no enable bits set".
    {
        fresh(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        // Positive: bit 0 set, bit 1 clear → per-byte avail fires.
        nr_write(emu, 0xC6, 0x01);
        esp.send(0x5A);                     // device → guest
        settle(emu);
        const uint8_t ca_avail = nr_read(emu, 0xCA);

        // The byte must be readable by the guest at the RX port.
        emu.port().out(0x153B, 0x00);       // select channel 0
        const uint8_t got = emu.port().in(0x143B);

        emu.uart().detach_device(0);

        // Negative: fresh machine, bit 1 set + bit 0 clear → a single
        // injected byte must NOT raise the UART0_RX status.
        fresh(emu);
        StubUartDevice esp2;
        emu.uart().attach_device(0, &esp2);
        nr_write(emu, 0xC6, 0x02);
        esp2.send(0x5A);
        settle(emu);
        const uint8_t ca_masked = nr_read(emu, 0xCA);
        emu.uart().detach_device(0);

        check("DEV-02",
              "UartDevice::send_to_guest injects through Uart::inject_rx: the guest "
              "reads the byte at 0x143B and IM2 UART0_RX follows the NR 0xC6 request "
              "mask near_full OR (avail AND NOT bit1) [zxnext.vhd:1941-1944, :1949-1950]",
              got == 0x5A && (ca_avail & 0x03) != 0 && (ca_masked & 0x03) == 0,
              fmt("guest read=0x%02X (want 0x5A); NR_CA with C6=0x01: 0x%02X "
                  "(want bits1:0 set); with C6=0x02: 0x%02X (want bits1:0 clear)",
                  got, ca_avail, ca_masked));
    }

    // DEV-03 — detach restores loopback AND kills the device's sink. This is
    // what makes the seam behaviour-preserving by construction: a detached
    // channel is indistinguishable from one that never had a device, and a
    // detached device can no longer inject into a channel it does not own
    // (the sink captures the Uart, so a live stale sink is also the dangling-
    // reference hazard documented in uart_device.h).
    {
        fresh(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);
        emu.uart().detach_device(0);

        const size_t rx_before = esp.rx.size();

        emu.port().out(0x153B, 0x00);
        emu.port().out(0x133B, 0xBB);       // guest transmits post-detach
        tick_uart_byte(emu);

        const bool device_silent = (esp.rx.size() == rx_before);

        // Loopback restored: the transmitted byte came back round.
        const uint8_t looped = emu.port().in(0x143B);

        // Sink cleared: a detached device cannot push into the guest.
        esp.send(0x99);
        const bool sink_dead = emu.uart().channel(0).rx_empty();

        check("DEV-03",
              "Uart::detach_device restores loopback and clears the device's RxSink "
              "— a detached channel behaves exactly like one that never had a device "
              "[uart_device.h lifetime contract]",
              device_silent && looped == 0xBB && sink_dead,
              fmt("device rx grew=%d (want 0); loopback read=0x%02X (want 0xBB); "
                  "post-detach send left RX empty=%d (want 1)",
                  device_silent ? 0 : 1, looped, sink_dead ? 1 : 0));
    }

    // DEV-05 — an attached device takes precedence over `on_tx_byte`, and the
    // observer is SILENTLY SUPPRESSED rather than also fired. This pins the
    // policy documented at uart.cpp deliver_tx_byte: exactly one consumer
    // sees any given byte. It matters because `on_tx_byte` is public and
    // still assigned by other rows in this suite (DUAL-05) and by uart_test —
    // an implementation that fired both would double-deliver every ESP byte,
    // and one that preferred the callback would strand the device entirely.
    {
        fresh(emu);
        StubUartDevice esp;
        std::vector<uint8_t> observer;

        auto& ch0 = emu.uart().channel(0);
        ch0.on_tx_byte = [&observer](uint8_t b) { observer.push_back(b); };
        emu.uart().attach_device(0, &esp);   // attach with the hook ALREADY set

        emu.port().out(0x153B, 0x00);        // select channel 0
        emu.port().out(0x133B, 0xC3);
        tick_uart_byte(emu);

        const bool device_got   = (esp.rx.size() == 1) && (esp.rx[0] == 0xC3);
        const bool observer_mute = observer.empty();

        emu.uart().detach_device(0);
        ch0.on_tx_byte = nullptr;            // never outlive `observer`

        check("DEV-05",
              "An attached UartDevice takes precedence over on_tx_byte: the device "
              "receives the byte and the observer hook is suppressed, so exactly one "
              "consumer sees it [uart.cpp deliver_tx_byte]",
              device_got && observer_mute,
              fmt("device rx=%zu first=0x%02X (want 1/0xC3); observer rx=%zu (want 0)",
                  esp.rx.size(), esp.rx.empty() ? 0 : esp.rx[0], observer.size()));
    }

    // DEV-04 — attachment is per-channel. A device on UART 0 (ESP) must not
    // see UART 1 (Pi) traffic or vice versa: zxnext.vhd:3343-3344 routes
    // UART 0 TX to the ESP pin and UART 1 TX to the Pi pin, and uart.vhd
    // gates tx_wr on uart_select_r bit 6. This is DUAL-05's invariant
    // re-asserted across the device seam rather than the callback.
    {
        fresh(emu);
        StubUartDevice esp;    // UART 0
        StubUartDevice pi;     // UART 1
        emu.uart().attach_device(0, &esp);
        emu.uart().attach_device(1, &pi);

        emu.port().out(0x153B, 0x00);       // select ch0
        emu.port().out(0x133B, 0xAA);
        tick_uart_byte(emu);

        emu.port().out(0x153B, 0x40);       // select ch1
        emu.port().out(0x133B, 0xBB);
        tick_uart_byte(emu);

        const bool esp_ok = (esp.rx.size() == 1) && (esp.rx[0] == 0xAA);
        const bool pi_ok  = (pi.rx.size()  == 1) && (pi.rx[0]  == 0xBB);

        emu.uart().detach_device(0);
        emu.uart().detach_device(1);

        check("DEV-04",
              "UartDevice attachment is per-channel: UART 0 (ESP) and UART 1 (Pi) "
              "backends each see only their own channel's TX "
              "[zxnext.vhd:3343-3344; uart.vhd tx_wr gated on uart_select_r bit 6]",
              esp_ok && pi_ok,
              fmt("ch0 device rx=%zu first=0x%02X (want 1/0xAA); "
                  "ch1 device rx=%zu first=0x%02X (want 1/0xBB)",
                  esp.rx.size(), esp.rx.empty() ? 0 : esp.rx[0],
                  pi.rx.size(),  pi.rx.empty()  ? 0 : pi.rx[0]));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section ESP — the REAL emulated ESP-01 on UART 0 (ESP-01..04)
// ══════════════════════════════════════════════════════════════════════
//
// These four IDs sat `missing`/`missing` in the traceability matrix and as
// deliberate `// WONT` comments in test/uart/uart_test.cpp, from the era when
// jnext had no ESP at all ("a self-contained networking feature nobody had
// built"). GH #25 built it, so the revisit-trigger fired.
//
// They are NOT duplicates of DEV-01..05 above. Those drive the hand-written
// `StubUartDevice`, which proves the SEAM. A stub cannot tell you whether the
// thing `--esp` actually constructs — a `ThreadedEsp` wrapping a real
// `AtEngine` behind an `EspGatedTransport`, built by `Emulator::setup_esp` —
// is on the wire and answering. Every row below uses that one, reached the
// way a user reaches it.
//
// Each row builds its OWN Emulator rather than using `fresh(emu)`: the ESP is
// constructed from `EmulatorConfig::esp_enabled`, and `setup_esp` deliberately
// builds only once per Emulator (a soft reset must not drop a live TCP
// connection — design doc §4.3), so the shared fixture cannot express both
// arms.
//
// HERMETIC, and not by luck: `AT` and `ATE1` never touch the transport, so
// nothing here resolves a name or opens a socket. The socket half — a real
// connect, `AT+CIPSEND` and `+IPD` framing against a live TCP peer — is the
// `esp-loopback-func` regression row.

namespace {

/// Bytes rendered with CR/LF as escapes, so a failing byte-stream row reads
/// on one line and a stray terminator cannot hide in the output.
std::string visible(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\r')      out += "\\r";
        else if (c == '\n') out += "\\n";
        else                out += c;
    }
    return out;
}

EmulatorConfig esp_config(bool enabled) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.esp_enabled = enabled;
    return cfg;
}

void uart0_send(Emulator& emu, const std::string& line) {
    emu.port().out(0x153B, 0x00);           // select channel 0 (the ESP)
    for (char c : line) emu.port().out(0x133B, static_cast<uint8_t>(c));
}

/// What a drain saw, and how long it had to look. The frame/millisecond
/// figures exist so a FAILING row can say whether it gave up after the full
/// wait — a genuinely broken egress path — rather than leaving a reader to
/// guess (GH #186).
struct Drained {
    std::string bytes;
    int         frames = 0;
    double      ms     = 0.0;
};

/// Settle window, in EMULATED frames, kept after the wanted bytes appear.
///
/// This is the ORIGINAL fixed budget, and it stays fixed, because the job it
/// does is an emulated-time one: the reply is paced onto the wire at baud
/// (2430 master cycles per byte at the 115200 reset default) and the adapter's
/// hot-path tick gate is only re-evaluated in the once-per-frame `poll()`, so
/// bytes keep dribbling in for a frame or two after the first one lands.
/// Holding the window open for this many frames PAST the match is what keeps
/// the rows able to catch a reply with unwanted bytes trailing it — the
/// property the old fixed budget got for free, and the one a naive
/// stop-on-match retry would have silently dropped.
constexpr int ESP_SETTLE_FRAMES = 4;

/// Upper bound on the wait, in HOST milliseconds — the unit that matters,
/// because the thing being waited for is a HOST THREAD (GH #186).
///
/// The ESP runs on `esp::ThreadedEsp`'s worker: the guest's TX bytes go into
/// an inbound queue and only that thread parses them and queues the answer.
/// The row's old budget was `ESP_SETTLE_FRAMES` of emulated time, which
/// headless at max speed is ~5 ms of WALL CLOCK — measured 1.3 ms/frame — so
/// the row silently required the host to schedule a freshly-woken thread
/// inside 5 ms. Measured cliff: injecting a worker wake latency of 6 ms
/// reproduces "RX drained 0 bytes" exactly; 4 ms does not. Six milliseconds is
/// an ordinary CFS wake latency on a loaded box, which is why this failed
/// three times overnight on branches that touch neither the ESP nor the UART.
///
/// The wait is bounded and generous — 1000x the measured cliff — and it is NOT
/// a way to let a broken path pass: the stop condition is that the drained
/// bytes EQUAL what the row wants, so a loopback (`AT\r\n` where `\r\nOK\r\n`
/// is wanted) or an absent backend never satisfies it and the row still FAILS,
/// after burning the full wait. Mutation-verified.
constexpr int ESP_WAIT_MS = 5000;

/// Backstop so a pathologically cheap frame cannot spin here forever; at the
/// measured 1.3 ms/frame the millisecond deadline is what actually bites.
constexpr int ESP_MAX_FRAMES = 20000;

/// Harvest everything that reaches the RX FIFO, until `want` has arrived (plus
/// the settle window above), or the bounds give up.
///
/// `want` empty means "nothing is expected": the drain then runs exactly
/// `ESP_SETTLE_FRAMES` frames, which is the original behaviour, because there
/// is nothing to wait FOR and a row that expects silence must not sit for the
/// full timeout to learn it.
///
/// Draining across frames is what a real program does too.
Drained uart0_drain(Emulator& emu, const std::string& want) {
    Drained  d;
    const auto start    = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(ESP_WAIT_MS);
    int matched_at = -1;

    for (int i = 0; i < ESP_MAX_FRAMES; ++i) {
        emu.run_frame();
        emu.port().out(0x153B, 0x00);
        while (emu.port().in(0x133B) & 0x01)          // status b0 = RX available
            d.bytes += static_cast<char>(emu.port().in(0x143B));
        d.frames = i + 1;

        if (matched_at < 0 && !want.empty() && d.bytes == want) matched_at = i;

        const bool settled = (matched_at >= 0)
                                 ? (i - matched_at >= ESP_SETTLE_FRAMES)
                                 : (want.empty() && d.frames >= ESP_SETTLE_FRAMES);
        if (settled) break;
        if (std::chrono::steady_clock::now() >= deadline) break;   // gave up
    }
    d.ms = std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start).count();
    return d;
}

/// The wait as a row's failure text renders it.
std::string waited(const Drained& d) {
    return fmt("after %d frames / %.0f ms", d.frames, d.ms);
}

} // namespace

static void test_esp_backend() {
    set_group("ESP");

    // ESP-01 — guest TX egresses to the real ESP, which answers. Two halves,
    // and the second is the one a wiring mistake trips: an unattached channel
    // loops the guest's own bytes into its own RX FIFO (uart.cpp
    // deliver_tx_byte), so "AT\r\n" coming back would look like traffic while
    // proving the ESP is NOT there. Echo is off by default (design doc
    // simplification 2), so the ESP never sends the guest's bytes back.
    {
        Emulator emu;
        emu.init(esp_config(true));
        uart0_send(emu, "AT\r\n");
        const Drained reply = uart0_drain(emu, "\r\nOK\r\n");

        check("ESP-01",
              "guest TX on UART 0 egresses to the REAL emulated ESP-01, which parses "
              "the AT line and answers — not to the channel's loopback "
              "[zxnext.vhd:1611-1612, :3381 UART 0 = ESP]",
              reply.bytes == "\r\nOK\r\n",
              fmt("RX drained %zu bytes %s: '%s' (want '\\r\\nOK\\r\\n'; 'AT\\r\\n' would "
                  "mean loopback, empty would mean nothing is attached)",
                  reply.bytes.size(), waited(reply).c_str(), visible(reply.bytes).c_str()));
    }

    // ESP-02 — the same reply reaches the guest through the real RX FIFO AND
    // drives the real IM2 UART0_RX vector under the NR 0xC6 request mask
    //   near_full OR (avail AND NOT nr_c6(1))
    // (zxnext.vhd:1941-1944). Asserting BOTH directions is what makes it
    // discriminative: a delivery path that bypassed `Uart::inject_rx` could
    // still hand the guest the bytes but could not reproduce the mask. Six
    // bytes never reach the 3/4 near-full mark, so bit 1 really does suppress.
    {
        Emulator emu;
        emu.init(esp_config(true));
        nr_write(emu, 0xC6, 0x01);              // per-byte avail contributes
        uart0_send(emu, "AT\r\n");
        const Drained got_avail = uart0_drain(emu, "\r\nOK\r\n");
        const uint8_t ca_avail  = nr_read(emu, 0xCA);

        Emulator emu2;
        emu2.init(esp_config(true));
        nr_write(emu2, 0xC6, 0x02);             // near-full only; avail suppressed
        uart0_send(emu2, "AT\r\n");
        const Drained got_masked = uart0_drain(emu2, "\r\nOK\r\n");
        const uint8_t ca_masked  = nr_read(emu2, 0xCA);

        check("ESP-02",
              "the ESP's reply lands in the UART 0 RX FIFO and raises the UART0_RX "
              "IM2 vector under the NR 0xC6 request mask near_full OR (avail AND NOT "
              "bit1) [zxnext.vhd:1941-1944, :1949-1950]",
              got_avail.bytes == "\r\nOK\r\n" && (ca_avail & 0x03) != 0 &&
                  got_masked.bytes == "\r\nOK\r\n" && (ca_masked & 0x03) == 0,
              fmt("C6=0x01: rx='%s' %s NR_CA=0x%02X (want reply + bits1:0 set); "
                  "C6=0x02: rx='%s' %s NR_CA=0x%02X (want reply + bits1:0 clear)",
                  visible(got_avail.bytes).c_str(), waited(got_avail).c_str(), ca_avail,
                  visible(got_masked.bytes).c_str(), waited(got_masked).c_str(), ca_masked));
    }

    // ESP-03 — RESTATED to what v1.0 actually built, deliberately.
    //
    // The row used to claim NR 0x02 bit 7 "resets the attached ESP-01, as
    // nextsync's recovery path does". The hardware line is real —
    // zxnext.vhd:5119 latches `nr_02_bus_reset <= nr_wr_dat(7)`, zxnext.vhd:1579
    // drives `o_RESET_PERIPHERAL` from it, and nextreg.txt:48 reads verbatim
    // "Assert and hold reset to the expansion bus and the esp wifi" — and jnext
    // latches the bit and reads it back correctly. What jnext does NOT do is
    // route it to the device. That is a v1.1 extension point (design doc §4.2
    // and §10), left out on the stated grounds that it is a RECOVERY path
    // ("degraded, not blocking") and that the hook belongs on the NR 0x02
    // WRITE, never on `UartChannel::reset`, which resets only the Next-side
    // state machine (§4.3).
    //
    // So the row pins the fact rather than the aspiration — and it is not a
    // tautology: `ATE1` leaves observable state inside the AT engine (echo on),
    // and a real hard reset would restore the power-on default of echo OFF
    // (simplification 2). Wire the reset up and this row goes red, which is
    // exactly right: the design record has to change with it.
    {
        Emulator emu;
        emu.init(esp_config(true));

        // WAIT for the ATE1 acknowledgement, do not merely allow time for it:
        // everything below asserts that the engine kept the state this command
        // set, so a run in which the command had not been PARSED yet would be
        // asserting nothing (GH #186).
        uart0_send(emu, "ATE1\r\n");             // echo ON: engine state to lose
        const Drained ack = uart0_drain(emu, "\r\nOK\r\n");

        nr_write(emu, 0x02, 0x80);               // assert the ESP / expbus reset
        const uint8_t nr02 = nr_read(emu, 0x02);
        uart0_drain(emu, "");                    // expect silence: settle only

        // With echo on, the reply to `AT` is the echoed command AND the OK.
        uart0_send(emu, "AT\r\n");
        const Drained after = uart0_drain(emu, "AT\r\n\r\nOK\r\n");

        check("ESP-03",
              "NR 0x02 bit 7 (o_RESET_PERIPHERAL) latches and reads back, and in v1.0 "
              "drives NO device reset — the attached ESP keeps its state across it; "
              "nextsync's recovery path is a v1.1 extension point (design doc §4.2) "
              "[zxnext.vhd:5119, :1579; nextreg.txt:48]",
              (nr02 & 0x80) != 0 && ack.bytes == "\r\nOK\r\n" &&
                  after.bytes.find("AT") != std::string::npos,
              fmt("NR 0x02 readback=0x%02X (want b7 set); ATE1 ack='%s' %s (want "
                  "'\\r\\nOK\\r\\n', else the echo was never turned on); reply after "
                  "the reset='%s' %s (want the ATE1 echo still on — a bare "
                  "'\\r\\nOK\\r\\n' would mean the engine had been reset)",
                  nr02, visible(ack.bytes).c_str(), waited(ack).c_str(),
                  visible(after.bytes).c_str(), waited(after).c_str()));
    }

    // ESP-04 — the converse, and the reason ESP-01's second half is worth
    // asserting: with nothing attached, UART 0 keeps the loopback the 116
    // pre-ESP UART rows observe. This is what "--esp is default off" means at
    // the wire, rather than at the config struct.
    {
        Emulator emu;
        emu.init(esp_config(false));
        uart0_send(emu, "AT\r\n");
        const Drained reply = uart0_drain(emu, "AT\r\n");

        check("ESP-04",
              "with no ESP backend attached, UART 0 keeps its loopback: the guest's "
              "own bytes come back and nothing answers them",
              emu.uart().device(0) == nullptr && reply.bytes == "AT\r\n",
              fmt("device=%p rx='%s' %s (want null + 'AT\\r\\n')",
                  static_cast<const void*>(emu.uart().device(0)),
                  visible(reply.bytes).c_str(), waited(reply).c_str()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section JOY — the joystick-connector UART cable (JOY-01..11), GH #251
// ══════════════════════════════════════════════════════════════════════
//
// Two things that had no coverage at all before GH #251:
//
//   * The MUX IS EXCLUSIVE. VHDL zxnext.vhd:3340-3346 — when the joystick
//     connector owns a channel, that channel's `*_rx` selects `joy_uart_rx`
//     (so the module's own RX pin is not selected) and its module-facing TX
//     pin is held at '1'. The ESP path in jnext ignored NR 0x0B entirely, so a
//     bench built on `--esp` would have passed with the mux wired arbitrarily
//     wrongly — which is why no bench was built until the routing was
//     enforceable.
//
//   * A HOST SERIAL SOURCE CAN REACH THE PIN. `Emulator::inject_joy_uart_rx`
//     had exactly two callers, both in this file, so nothing outside the test
//     tree could put a byte on the joystick UART — the whole of the issue.
//
// The rows drive the guest side through the real port path and read the source
// side through the real `--joy-uart-rx` config field, file and all.
// ══════════════════════════════════════════════════════════════════════

namespace {

// A `--joy-uart-rx` stream on disk, removed when the row's scope ends. Real
// file I/O rather than a test-only injection hook, so the row covers
// read_joy_uart_source_file() and the config seam a command line actually uses.
class TempSourceFile {
public:
    explicit TempSourceFile(const std::string& tag, const std::vector<uint8_t>& bytes) {
        path_ = (std::filesystem::temp_directory_path()
                 / ("jnext-joy-uart-" + tag + ".bin")).string();
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    ~TempSourceFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempSourceFile(const TempSourceFile&)            = delete;
    TempSourceFile& operator=(const TempSourceFile&) = delete;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// One step of a scheduled cable run: the cursor as it stands at the frame
// START, then the frame, then every byte the guest actually read out of
// channel 0 during it. Compared whole, so a replay that merely lands on the
// right counters while delivering different BYTES is still a failure.
struct CableFrame {
    std::size_t          delivered = 0;
    std::size_t          dropped   = 0;
    std::vector<uint8_t> got;

    bool operator==(const CableFrame& o) const {
        return delivered == o.delivered && dropped == o.dropped && got == o.got;
    }
};

// Run one step with the mux held at `nr_0b`, draining channel 0 afterwards.
//
// The caller indexes its schedule by its OWN step counter, never by
// `Emulator::frame_num()`: a rewind to step N restores the machine to the top
// of step N but leaves `frame_num()` reading N+1, because run_frame() bumps the
// counter before the snapshot is taken. A schedule keyed off it would therefore
// drift by one frame across the rewind and the replay would no longer be a
// replay of the same run.
CableFrame run_cable_frame(Emulator& emu, uint8_t nr_0b) {
    CableFrame f;
    if (const JoyUartSource* src = emu.joy_uart_source()) {
        f.delivered = src->delivered();
        f.dropped   = src->dropped();
    }
    emu.nextreg().write(0x0B, nr_0b);
    emu.run_frame();
    emu.port().out(0x153B, 0x00);
    while (!emu.uart().channel(0).rx_empty())
        f.got.push_back(emu.port().in(0x143B));
    return f;
}

// Rewind to `frame` and leave the machine RUNNABLE.
//
// `Emulator::rewind_to_frame` deliberately ends with
// `debug_state_.set_active(true); debug_state_.pause()` — a rewind in the GUI
// lands the user in the debugger at the restored instant. `run_frame()` returns
// immediately while that holds (emulator.cpp:7813-7816), so a programmatic
// replay that does not clear it silently executes NOTHING and every "the replay
// matched" assertion becomes a comparison of empty frames against empty frames.
bool rewind_and_resume(Emulator& emu, uint32_t frame) {
    if (!emu.rewind_to_frame(frame)) return false;
    emu.debug_state().resume();
    emu.debug_state().set_active(false);
    return true;
}

// Index of the first step at which two runs disagree, or -1 if they agree over
// `count` steps starting at `base` in `forward`.
int first_divergent_step(const std::vector<CableFrame>& forward, int base,
                         const std::vector<CableFrame>& replay) {
    for (std::size_t j = 0; j < replay.size(); ++j) {
        const std::size_t f = static_cast<std::size_t>(base) + j;
        if (f >= forward.size() || !(replay[j] == forward[f]))
            return static_cast<int>(base + j);
    }
    return -1;
}

// A stream long enough to outlive the whole run — an exhausted cursor cannot
// move, and every assertion below would pass trivially against one.
std::vector<uint8_t> cable_stream() {
    std::vector<uint8_t> s(8192);
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<uint8_t>((i * 7 + 1) & 0xFF);
    return s;
}

// Config for a Next with a joystick serial cable attached.
EmulatorConfig joy_config(const std::string& file, int connector,
                          uint32_t delay_frames) {
    EmulatorConfig cfg;
    cfg.type                     = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames     = 0;
    cfg.joy_uart_rx_file         = file;
    cfg.joy_uart_connector       = connector;
    cfg.joy_uart_rx_delay_frames = delay_frames;
    return cfg;
}

// Run frames with the guest's NR 0x0B held at `nr_0b`, draining `channel`'s RX
// FIFO through the real port path after each one. The NR is re-asserted every
// frame because the boot firmware is running underneath and this row must
// observe the mux it asked for, not whatever the ROM last left behind.
std::vector<uint8_t> run_and_drain(Emulator& emu, uint8_t nr_0b, int channel,
                                   int frames) {
    std::vector<uint8_t> got;
    for (int f = 0; f < frames; ++f) {
        emu.nextreg().write(0x0B, nr_0b);
        emu.run_frame();
        emu.port().out(0x153B, static_cast<uint8_t>(channel ? 0x40 : 0x00));
        while (!emu.uart().channel(channel).rx_empty())
            got.push_back(emu.port().in(0x143B));
    }
    return got;
}

} // namespace

static void test_joy_uart_cable() {
    set_group("JOY");

    // ── JOY-01 — the attached backend cannot be HEARD while the mux owns the
    // channel (zxnext.vhd:3340: `uart0_rx <= joy_uart_rx`, so `i_UART0_RX` is
    // not selected). Both directions asserted on one emulator, so the negative
    // half cannot be passing because injection is simply broken.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        emu.nextreg().write(0x0B, 0xA0);        // mux on, channel 0
        esp.send(0x11);
        const bool muted = emu.uart().channel(0).rx_empty();

        emu.nextreg().write(0x0B, 0x00);        // mux off
        esp.send(0x22);
        emu.port().out(0x153B, 0x00);
        const uint8_t heard = emu.port().in(0x143B);

        emu.uart().detach_device(0);

        check("JOY-01",
              "zxnext.vhd:3340 — while the joystick UART mux owns UART 0, "
              "`uart0_rx` selects `joy_uart_rx` and the ESP's own RX pin is not "
              "selected, so its bytes are lost; with the mux off they arrive",
              muted && heard == 0x22,
              fmt("muted=%d (want 1); heard=0x%02X (want 0x22)",
                  muted ? 1 : 0, heard));
    }

    // ── JOY-02 — nor SPOKEN TO (zxnext.vhd:3343: `uart0_tx_esp <= '1'`), and —
    // the half that matters — the byte is NOT looped back into this channel's
    // own RX FIFO either. Looping it back would corrupt the very stream the
    // cable is feeding in, so gating only the RX direction would be worse than
    // gating neither.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        emu.nextreg().write(0x0B, 0xA0);        // mux on, channel 0
        emu.port().out(0x153B, 0x00);
        emu.port().out(0x133B, 0xAA);           // guest transmits
        tick_uart_byte(emu);
        const bool device_silent = esp.rx.empty();
        const bool no_loopback   = emu.uart().channel(0).rx_empty();

        emu.nextreg().write(0x0B, 0x00);        // mux off — positive control
        emu.port().out(0x133B, 0xBB);
        tick_uart_byte(emu);
        const bool device_heard = (esp.rx.size() == 1) && (esp.rx[0] == 0xBB);

        emu.uart().detach_device(0);

        check("JOY-02",
              "zxnext.vhd:3343 — while the joystick UART mux owns UART 0 the "
              "module-facing TX pin is held idle, so a transmitted byte reaches "
              "neither the ESP nor a loopback into the channel's own RX FIFO; "
              "with the mux off the ESP receives normally",
              device_silent && no_loopback && device_heard,
              fmt("device_silent=%d no_loopback=%d device_heard=%d (all want 1); "
                  "device rx=%zu", device_silent ? 1 : 0, no_loopback ? 1 : 0,
                  device_heard ? 1 : 0, esp.rx.size()));
    }

    // ── JOY-03 — the mux takes ONE channel, chosen by NR 0x0B bit 0
    // (zxnext.vhd:3340-3341). With the cable routed to UART 1, the ESP on
    // UART 0 must be untouched in both directions: this is what makes JOY-01/02
    // a routing assertion rather than a blanket "devices go quiet under
    // NR 0x0B" one.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        emu.nextreg().write(0x0B, 0xA1);        // mux on, channel 1 — not ch 0
        esp.send(0x33);
        emu.port().out(0x153B, 0x00);
        const uint8_t heard = emu.port().in(0x143B);
        emu.port().out(0x133B, 0xCC);
        tick_uart_byte(emu);
        const bool device_heard = (esp.rx.size() == 1) && (esp.rx[0] == 0xCC);

        emu.uart().detach_device(0);

        check("JOY-03",
              "zxnext.vhd:3340-3341 — NR 0x0B bit 0 selects WHICH channel the "
              "joystick connector takes, so with bit 0 = 1 the UART 1 pins are "
              "shadowed and the ESP on UART 0 keeps both directions",
              heard == 0x33 && device_heard,
              fmt("ESP→guest=0x%02X (want 0x33); guest→ESP heard=%d rx=%zu (want 1/1)",
                  heard, device_heard ? 1 : 0, esp.rx.size()));
    }

    // ── JOY-04 — a `--joy-uart-rx` file reaches the guest, end to end: config
    // field → JoyUartSource → the connector/enable gates → the channel's RX
    // FIFO → the port the Z80 reads. This is the capability the issue asked for
    // and the one nothing outside this test tree could reach before.
    {
        const std::vector<uint8_t> stream{0xD5, 0x2A, 0x00, 0x7F};
        TempSourceFile file("deliver", stream);
        Emulator emu;
        emu.init(joy_config(file.path(), /*connector=*/1, /*delay_frames=*/0));

        // Guest selects UART mode on connector 2 (mode "11" → bit 4 = 1) and
        // channel 0 (bit 0 = 0).
        const std::vector<uint8_t> got = run_and_drain(emu, 0xB0, 0, 2);

        check("JOY-04",
              "GH #251 — a serial source attached with --joy-uart-rx arrives on "
              "the channel NR 0x0B bit 0 selects, through the same "
              "zxnext.vhd:3340-3341 mux the guest reads at port 0x143B",
              got == stream,
              fmt("got %zu byte(s) [%s] (want %zu [0xD5 0x2A 0x00 0x7F]); "
                  "delivered=%zu dropped=%zu",
                  got.size(), bytes_hex(got).c_str(), stream.size(),
                  emu.joy_uart_source() ? emu.joy_uart_source()->delivered() : 0,
                  emu.joy_uart_source() ? emu.joy_uart_source()->dropped() : 0));
    }

    // ── JOY-05 — the CONNECTOR field is honoured (zxnext.vhd:3538). The cable
    // is in joy 2, and the guest selects mode "10" — connector joy 1 — so the
    // FPGA is reading a pin with nothing on it and every byte is lost. This is
    // the question the issue says the VHDL cannot answer for real hardware and
    // that upstream dezogif answers "port 2 only"; without it, bit 4 would be
    // ignored and jnext would be more permissive than the silicon.
    {
        const std::vector<uint8_t> stream{0x61, 0x62, 0x63};
        TempSourceFile file("connector", stream);
        Emulator emu;
        emu.init(joy_config(file.path(), /*connector=*/1, /*delay_frames=*/0));

        // Mode "10" = connector joy 1, and the cable is in joy 2.
        const std::vector<uint8_t> wrong = run_and_drain(emu, 0xA0, 0, 2);
        const std::size_t dropped =
            emu.joy_uart_source() ? emu.joy_uart_source()->dropped() : 0;
        const std::size_t delivered =
            emu.joy_uart_source() ? emu.joy_uart_source()->delivered() : 0;

        check("JOY-05",
              "zxnext.vhd:3538 — `joy_uart_rx` is read from i_JOY_LEFT(5) or "
              "i_JOY_RIGHT(5) according to NR 0x0B bit 4, so a cable in the "
              "other socket is a pin the machine is not looking at and its "
              "bytes are lost rather than delivered",
              wrong.empty() && delivered == 0 && dropped == stream.size(),
              fmt("received %zu byte(s) [%s] (want 0); delivered=%zu (want 0) "
                  "dropped=%zu (want %zu)",
                  wrong.size(), bytes_hex(wrong).c_str(), delivered,
                  dropped, stream.size()));
    }

    // ── JOY-06 — the start delay holds the stream for exactly N whole frames,
    // which is what makes "a byte lands while the debuggee is already running"
    // reachable at all: with no delay the stream is spent before a guest has
    // set NR 0x0B up. Both edges asserted — silent through frame N, arrived by
    // frame N+1 — because only the pair distinguishes a working delay from a
    // source that never sends.
    {
        const std::vector<uint8_t> stream{0x9E};
        TempSourceFile file("delay", stream);
        Emulator emu;
        emu.init(joy_config(file.path(), /*connector=*/1, /*delay_frames=*/3));

        const std::vector<uint8_t> during_hold = run_and_drain(emu, 0xB0, 0, 3);
        const std::vector<uint8_t> after_hold  = run_and_drain(emu, 0xB0, 0, 1);

        check("JOY-06",
              "GH #251 — --joy-uart-rx-delay-frames N holds the stream for N "
              "COMPLETE frames (counted at the once-per-frame end seam) and "
              "releases it in the (N+1)th",
              during_hold.empty() && after_hold == stream,
              fmt("frames 1-3 got %zu byte(s) (want 0); frame 4 got %zu [%s] "
                  "(want 1 [0x9E])",
                  during_hold.size(), after_hold.size(),
                  bytes_hex(after_hold).c_str()));
    }

    // ── JOY-07 — delivery is PACED at the receiving channel's baud, not dumped
    // in one go. It matters for a reason a 4-byte stream hides: the RX FIFO is
    // 512 bytes with drop-newest overflow (uart_device.h), so an unpaced source
    // longer than that would silently lose its tail, and a guest that reads one
    // byte per NMI poll — the dezogif shape — would see a burst that no cable
    // could produce. Driven on the bare class, where one byte time can be
    // stepped exactly.
    {
        JoyUartSource src({0xA1, 0xA2, 0xA3}, /*connector=*/0, /*delay_frames=*/0);
        std::vector<uint8_t> got;
        src.set_byte_sink([&got](uint8_t b) { got.push_back(b); });

        const uint32_t byte_ticks = 2430;   // 115200 8N1 at 28 MHz, as the Next boots

        src.tick(byte_ticks - 1, byte_ticks);
        const bool none_yet = got.empty();
        src.tick(1, byte_ticks);            // completes the first byte exactly
        const bool one_now = (got.size() == 1) && (got[0] == 0xA1);

        // A span covering two further byte times delivers exactly two, and the
        // remainder is CARRIED: a source that discarded it would drift by one
        // instruction per byte.
        src.tick(2 * byte_ticks, byte_ticks);
        const bool three_now = (got.size() == 3);
        const bool exhausted = src.exhausted();

        // Nothing more, ever — the stream is finite and does not repeat.
        src.tick(10 * byte_ticks, byte_ticks);
        const bool still_three = (got.size() == 3);

        check("JOY-07",
              "GH #251 — JoyUartSource paces delivery at one byte per "
              "UartChannel::byte_transfer_ticks() (the same clock the ESP RX "
              "path uses), and stops when the stream is exhausted",
              none_yet && one_now && three_now && exhausted && still_three,
              fmt("none_yet=%d one_now=%d three_now=%d exhausted=%d still_three=%d "
                  "(all want 1); got %zu [%s]",
                  none_yet ? 1 : 0, one_now ? 1 : 0, three_now ? 1 : 0,
                  exhausted ? 1 : 0, still_three ? 1 : 0,
                  got.size(), bytes_hex(got).c_str()));
    }

    // ── JOY-08 — an unreadable or EMPTY source file is refused, not accepted as
    // "a source that sends nothing". An empty stream can only ever produce a run
    // that looks like a pass for a path no byte took, which is the failure this
    // whole feature exists to make impossible.
    {
        TempSourceFile empty_file("empty", {});
        std::vector<uint8_t> bytes;
        std::string          error_empty;
        const bool empty_refused =
            !read_joy_uart_source_file(empty_file.path(), bytes, error_empty);

        std::string error_missing;
        const std::string missing =
            (std::filesystem::temp_directory_path() / "jnext-joy-uart-nonexistent.bin")
                .string();
        std::error_code ec;
        std::filesystem::remove(missing, ec);
        const bool missing_refused =
            !read_joy_uart_source_file(missing, bytes, error_missing);

        // And the positive control, so the two refusals above are not merely a
        // reader that always fails.
        TempSourceFile good_file("good", {0x01, 0x02});
        std::string error_good;
        const bool good_read =
            read_joy_uart_source_file(good_file.path(), bytes, error_good)
            && bytes == std::vector<uint8_t>{0x01, 0x02};

        check("JOY-08",
              "GH #251 — read_joy_uart_source_file refuses a missing file AND an "
              "empty one (a source that sends nothing tests nothing), and reads a "
              "real one whole",
              empty_refused && missing_refused && good_read,
              fmt("empty_refused=%d ('%s'); missing_refused=%d ('%s'); good_read=%d "
                  "(all want 1)",
                  empty_refused ? 1 : 0, error_empty.c_str(),
                  missing_refused ? 1 : 0, error_missing.c_str(),
                  good_read ? 1 : 0));
    }

    // ── JOY-09 — the SAME isolation on UART 1 (the Pi header), which
    // zxnext.vhd:3341/3344 spells out separately from UART 0:
    //
    //   uart1_rx    <= joy_uart_rx;   -- pi_uart_rx is NOT selected
    //   uart1_tx_pi <= '1';           -- the module-facing TX pin idles
    //
    // JOY-01/02 pin channel 0 and JOY-03 pins that channel 0 is UNAFFECTED when
    // the mux takes channel 1 — but nothing asserted that channel 1 is affected
    // when it does, so `Uart::set_joy_uart_channel_probe` could have compared
    // the wrong channel number in its second lambda and stayed green. That is a
    // copy-paste away in a two-line function and it is exactly the routing the
    // Pi-header half of the mux depends on.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice pi;
        emu.uart().attach_device(1, &pi);

        emu.nextreg().write(0x0B, 0xA1);        // mux on, channel 1
        pi.send(0x44);
        const bool muted = emu.uart().channel(1).rx_empty();

        emu.port().out(0x153B, 0x40);           // select channel 1
        emu.port().out(0x133B, 0x55);           // guest transmits
        tick_uart_byte(emu);
        const bool device_silent = pi.rx.empty();
        const bool no_loopback   = emu.uart().channel(1).rx_empty();

        // Positive control on the same emulator and the same device, so the
        // three negatives cannot be passing because a channel-1 backend is
        // simply never wired to anything.
        emu.nextreg().write(0x0B, 0x00);        // mux off
        pi.send(0x66);
        emu.port().out(0x153B, 0x40);
        const uint8_t heard = emu.port().in(0x143B);
        emu.port().out(0x133B, 0x77);
        tick_uart_byte(emu);
        const bool device_heard = (pi.rx.size() == 1) && (pi.rx[0] == 0x77);

        emu.uart().detach_device(1);

        check("JOY-09",
              "zxnext.vhd:3341,3344 — the joystick UART mux isolates UART 1 "
              "exactly as it isolates UART 0: with NR 0x0B bit 0 = 1 the Pi "
              "backend is neither heard nor spoken to and nothing loops back, "
              "and with the mux off both directions return",
              muted && device_silent && no_loopback
                  && heard == 0x66 && device_heard,
              fmt("muted=%d device_silent=%d no_loopback=%d device_heard=%d "
                  "(all want 1); heard=0x%02X (want 0x66); device rx=%zu",
                  muted ? 1 : 0, device_silent ? 1 : 0, no_loopback ? 1 : 0,
                  device_heard ? 1 : 0, heard, pi.rx.size()));
    }

    // ── JOY-10 — a REWIND rewinds the cable too (GH #251 review round 1).
    //
    // `RewindBuffer` restores the machine from a frame-start snapshot and then
    // RE-EXECUTES the intervening frames. The cable is not part of the machine
    // the snapshot captures unless it is written into the state stream, so
    // without `JoyUartSource::save_state/load_state` the cursor stayed at the
    // newest frame while everything around it went back — and the replayed
    // frames then fed the guest a byte stream that is NOT the one they were
    // reproducing. Measured pre-fix as `delivered()` ending HIGHER after the
    // rewind than it had been at the frame the rewind landed on.
    //
    // Asserted as the WHOLE TRAJECTORY, not as the cursor at the instant of
    // restore: the replay re-runs to the frame it started from and every step
    // must deliver the same BYTES in the same frame as the original run. Round 2
    // measured why the weaker form is not enough — with the mux held at one
    // value for the whole run, `dropped_` never leaves 0, `pos_ == delivered_`
    // holds throughout, and swapping that pair in `save_state` survived the
    // entire suite. So does swapping `frames_`/`timer_`, which nothing observed
    // past the restore instant at all. Both are covered here and in JOY-11.
    {
        const std::vector<uint8_t> stream = cable_stream();
        TempSourceFile file("rewind", stream);

        // delay_frames = 2, so steps 0-1 are the silent hold and the stream is
        // live from step 2 — which also makes `frames_` a live field rather than
        // a constant zero (JOY-11 rewinds INTO the hold to pin it).
        EmulatorConfig cfg = joy_config(file.path(), /*connector=*/1,
                                        /*delay_frames=*/2);
        cfg.rewind_buffer_frames = 16;
        Emulator emu;
        emu.init(cfg);

        // Every fourth step selects mode "10" — connector joy 1 — with the cable
        // in joy 2, so that step drops a whole frame's worth. THIS is what
        // decouples `dropped_` from zero and `pos_` from `delivered_`.
        auto sched = [](int step) -> uint8_t {
            return (step % 4 == 3) ? 0xA0 : 0xB0;
        };

        constexpr int kSteps  = 12;
        // Step 5 is the rewind point: the hold covered 0-1, steps 2 and 4
        // delivered, step 3 dropped — so all three counters are non-zero there
        // AND mutually distinct, which is what makes a mis-ordered save_state
        // observable rather than numerically invisible.
        constexpr int kRewind = 5;

        std::vector<CableFrame> forward;
        for (int i = 0; i < kSteps; ++i)
            forward.push_back(run_cable_frame(emu, sched(i)));

        auto* rb = emu.rewind_buffer();
        const bool in_range = rb
            && rb->oldest_frame_num() <= static_cast<uint32_t>(kRewind)
            && rb->newest_frame_num() >= static_cast<uint32_t>(kRewind);
        const bool rewound = in_range
            && rewind_and_resume(emu, static_cast<uint32_t>(kRewind));

        const JoyUartSource* src = emu.joy_uart_source();
        const bool cursor_restored = rewound && src
            && src->delivered() == forward[kRewind].delivered
            && src->dropped()   == forward[kRewind].dropped;

        // The three counters must actually differ from each other AT the rewind
        // point, or the equality above holds for a save_state that wrote them in
        // any order.
        const bool counters_distinct =
            forward[kRewind].delivered > 0
            && forward[kRewind].dropped > 0
            && forward[kRewind].delivered != forward[kRewind].dropped;

        std::vector<CableFrame> replay;
        for (int i = kRewind; rewound && i < kSteps; ++i)
            replay.push_back(run_cable_frame(emu, sched(i)));

        const int diverged = rewound
            ? first_divergent_step(forward, kRewind, replay) : kRewind;
        const bool full_replay = rewound
            && replay.size() == static_cast<std::size_t>(kSteps - kRewind);

        // And the replay has to have CARRIED bytes, or an all-empty trajectory
        // matches an all-empty one.
        std::size_t replayed_bytes = 0;
        for (const auto& f : replay) replayed_bytes += f.got.size();

        check("JOY-10",
              "GH #251 — JoyUartSource's cursor rides in the emulator state "
              "stream, so a rewind puts the cable back where it was and the "
              "replayed frames deliver byte-for-byte what they delivered the "
              "first time, across a schedule that both delivers and drops",
              rewound && cursor_restored && counters_distinct && full_replay
                  && diverged < 0 && replayed_bytes > 0,
              fmt("rewound=%d cursor_restored=%d counters_distinct=%d "
                  "full_replay=%d (all want 1); first divergent step=%d "
                  "(want -1); at step %d delivered=%zu/%zu dropped=%zu/%zu; "
                  "replayed %zu byte(s) over %zu step(s)",
                  rewound ? 1 : 0, cursor_restored ? 1 : 0,
                  counters_distinct ? 1 : 0, full_replay ? 1 : 0, diverged,
                  kRewind, src ? src->delivered() : 0,
                  forward[kRewind].delivered, src ? src->dropped() : 0,
                  forward[kRewind].dropped, replayed_bytes, replay.size()));
    }

    // ── JOY-11 — the START DELAY survives a rewind too: rewinding INTO the hold
    // must resume it with the frames already served still counted, so the stream
    // is released in the same step as the original run (GH #251 review round 2).
    //
    // This is the row that makes `frames_` a tested field. JOY-10 rewinds past
    // the hold, where `frames_` has saturated at `delay_frames_` and any wrong
    // value ≥ it behaves identically — round 2 measured a `frames_`/`timer_`
    // swap in `save_state` surviving the whole suite for exactly that reason.
    // Rewinding to step 1, with the hold half served, gives the field a value
    // that is neither 0 nor its saturation point, and a wrong restore moves the
    // release edge to a different frame.
    {
        const std::vector<uint8_t> stream = cable_stream();
        TempSourceFile file("rewind-hold", stream);

        EmulatorConfig cfg = joy_config(file.path(), /*connector=*/1,
                                        /*delay_frames=*/4);
        cfg.rewind_buffer_frames = 16;
        Emulator emu;
        emu.init(cfg);

        constexpr int kSteps  = 9;
        constexpr int kRewind = 1;      // inside the hold: frames_ == 1 there

        std::vector<CableFrame> forward;
        for (int i = 0; i < kSteps; ++i)
            forward.push_back(run_cable_frame(emu, 0xB0));

        // The hold has to be where this row says it is, or "the release edge did
        // not move" is a claim about nothing: silent through step 3, live in
        // step 4.
        const bool hold_shape = forward[3].got.empty() && !forward[4].got.empty();

        auto* rb = emu.rewind_buffer();
        const bool in_range = rb
            && rb->oldest_frame_num() <= static_cast<uint32_t>(kRewind)
            && rb->newest_frame_num() >= static_cast<uint32_t>(kRewind);
        const bool rewound = in_range
            && rewind_and_resume(emu, static_cast<uint32_t>(kRewind));

        std::vector<CableFrame> replay;
        for (int i = kRewind; rewound && i < kSteps; ++i)
            replay.push_back(run_cable_frame(emu, 0xB0));

        const int diverged = rewound
            ? first_divergent_step(forward, kRewind, replay) : kRewind;
        const bool full_replay = rewound
            && replay.size() == static_cast<std::size_t>(kSteps - kRewind);

        check("JOY-11",
              "GH #251 — a rewind INTO --joy-uart-rx-delay-frames' hold restores "
              "how much of the hold had been served, so the replay stays silent "
              "for the rest of it and releases the stream in the same frame as "
              "the run it reproduces",
              rewound && hold_shape && full_replay && diverged < 0,
              fmt("rewound=%d hold_shape=%d full_replay=%d (all want 1); "
                  "first divergent step=%d (want -1); forward step3=%zu byte(s) "
                  "(want 0) step4=%zu (want >0)",
                  rewound ? 1 : 0, hold_shape ? 1 : 0, full_replay ? 1 : 0,
                  diverged, forward[3].got.size(), forward[4].got.size()));
    }
}

static void test_nr_a0_pi_uart_routing(Emulator& emu) {
    set_group("NR_A0-INT");
    // VHDL zxnext.vhd:1241, 2278-2281, 5080, 5560-5561, 6188-6189.
    // NR 0xA0 = "Pi peripheral enable byte". Bit fan-out:
    //   bit 5 = pi_uart_rxtx (UART1 cross on Pi GPIO mux)
    //   bit 4 = pi_uart_en   (UART1 GPIO mux enable)
    //   bit 3 = pi_i2c1_en   (I2C1 GPIO 2/3 mux enable)
    //   bit 0 = pi_spi0_en   (SPI0 GPIO mux enable)
    // Reset 0x00. Read mask 0x39 ("00" & b5..b3 & "00" & b0).

    // NR_A0-01 — write/read handler round-trip + reset default + read mask.
    // VHDL :5080 reset = 0x00; VHDL :5560-5561 stores raw byte on write;
    // VHDL :6188-6189 read returns "00" & nr_a0(5..3) & "00" & nr_a0(0).
    {
        fresh(emu);
        const uint8_t reset_val = nr_read(emu, 0xA0);

        // Probe the read mask: write 0xFF, expect 0x39 (bits 5/4/3/0).
        nr_write(emu, 0xA0, 0xFF);
        const uint8_t mask_full = nr_read(emu, 0xA0);

        // Probe a sparse pattern: 0xA5 = 1010 0101 → masked = 0x21
        // (bits 5 + 0 set). Verifies the dropped bits 7,6,2,1.
        nr_write(emu, 0xA0, 0xA5);
        const uint8_t mask_a5 = nr_read(emu, 0xA0);

        check("NR_A0-01",
              "NR 0xA0 write/read handler: reset 0x00 + mask 0x39 per "
              "zxnext.vhd:5080, :6188-6189",
              reset_val == 0x00 && mask_full == 0x39 && mask_a5 == 0x21,
              fmt("reset=0x%02X (want 0x00); 0xFF→0x%02X (want 0x39); "
                  "0xA5→0x%02X (want 0x21)", reset_val, mask_full, mask_a5));
    }

    // NR_A0-02 — bit fan-out accessors (b5 pi_uart_rxtx, b4 pi_uart_en,
    // b3 pi_i2c1_en, b0 pi_spi0_en) all reflect the stored raw byte.
    // Walk a 4-bit pattern across the 4 live bits.
    {
        fresh(emu);
        // Write 0x39 (all four live bits set) and verify each accessor.
        nr_write(emu, 0xA0, 0x39);
        const bool b5 = emu.pi_uart_rxtx();
        const bool b4 = emu.pi_uart_en();
        const bool b3 = emu.pi_i2c1_en();
        const bool b0 = emu.pi_spi0_en();
        const bool all_set = b5 && b4 && b3 && b0;

        // Now write 0x00 and verify all clear.
        nr_write(emu, 0xA0, 0x00);
        const bool b5z = emu.pi_uart_rxtx();
        const bool b4z = emu.pi_uart_en();
        const bool b3z = emu.pi_i2c1_en();
        const bool b0z = emu.pi_spi0_en();
        const bool all_clear = !b5z && !b4z && !b3z && !b0z;

        check("NR_A0-02",
              "NR 0xA0 bit fan-out: pi_uart_rxtx (b5), pi_uart_en (b4), "
              "pi_i2c1_en (b3), pi_spi0_en (b0) per zxnext.vhd:2278-2281",
              all_set && all_clear,
              fmt("set: b5=%d b4=%d b3=%d b0=%d (want 1111); "
                  "clear: b5=%d b4=%d b3=%d b0=%d (want 0000)",
                  b5 ? 1 : 0, b4 ? 1 : 0, b3 ? 1 : 0, b0 ? 1 : 0,
                  b5z ? 1 : 0, b4z ? 1 : 0, b3z ? 1 : 0, b0z ? 1 : 0));
    }

    // NR_A0-03 — bit 3 pi_i2c1_en gates the I2C1 wired-AND read path.
    // VHDL zxnext.vhd:2317-2318:
    //   pi_i2c1_sda <= i_GPIO(2) when pi_i2c1_en='1' else '1';
    //   pi_i2c1_scl <= i_GPIO(3) when pi_i2c1_en='1' else '1';
    // i.e. the Pi-side bridge inputs are forced HIGH at the AND boundary
    // when the gate is OFF (NR 0xA0 bit 3 = 0, the reset default), so a
    // Pi pulling SCL/SDA low must NOT propagate to port 0x103B/0x113B.
    {
        fresh(emu);
        // Drive pi_i2c1_scl/sda LOW from the bridge side.
        emu.i2c().set_pi_i2c1(false, false);

        // Reset state — NR 0xA0 = 0x00 → gate OFF. Pi-low must not show.
        const uint8_t scl_off = emu.port().in(0x103B) & 0x01;
        const uint8_t sda_off = emu.port().in(0x113B) & 0x01;

        // Open the gate via NR 0xA0 bit 3.
        nr_write(emu, 0xA0, 0x08);
        const uint8_t scl_on = emu.port().in(0x103B) & 0x01;
        const uint8_t sda_on = emu.port().in(0x113B) & 0x01;

        check("NR_A0-03",
              "NR 0xA0 bit 3 (pi_i2c1_en) gates I2C1 wired-AND read path per "
              "zxnext.vhd:2280, 2317-2318 (G135 + G138)",
              scl_off == 1 && sda_off == 1 && scl_on == 0 && sda_on == 0,
              fmt("gate off: scl=%u sda=%u (want 1/1); on: scl=%u sda=%u (want 0/0)",
                  scl_off, sda_off, scl_on, sda_on));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("UART + I2C Integration Tests\n");
    std::printf("===============================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_uart_im2_interrupts(emu);
    std::printf("  Group: UART-INT — done\n");

    test_port_enable_gates(emu);
    std::printf("  Group: GATE — done\n");

    test_i2c_port_gate(emu);
    std::printf("  Group: I2C — done\n");

    test_dual_05_channel_routing(emu);
    test_dual_06_iomode_rx_mux(emu);
    test_dual_07_uart_en_needs_bit5(emu);
    std::printf("  Group: DUAL — done\n");

    test_uart_device_seam(emu);
    std::printf("  Group: DEV — done\n");

    test_joy_uart_cable();
    std::printf("  Group: JOY — done\n");

    test_nr_a0_pi_uart_routing(emu);
    std::printf("  Group: NR_A0-INT — done\n");

    test_esp_backend();
    std::printf("  Group: ESP — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
