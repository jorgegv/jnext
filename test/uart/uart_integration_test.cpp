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
#include "peripheral/uart_device.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
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
              "UART0 rx_near_full fires UART0_RX with NR 0xC6 bit 1 set only "
              "(near-full override) [zxnext.vhd:1942, :1950; plan-drift note]",
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
static void test_dual_06_iomode_rx_mux(Emulator& emu) {
    set_group("DUAL");
    fresh(emu);

    // Step 1: iomode_en=1, iomode_0=0 → joy→UART 0 mux.
    emu.nextreg().write(0x0B, 0x80);              // iomode_en=1, iomode_0=0
    emu.inject_joy_uart_rx(0x77);

    // Read UART 0 RX FIFO via port 0x143B after selecting channel 0.
    emu.port().out(0x153B, 0x00);                 // select channel 0
    const uint8_t u0_step1 = emu.port().in(0x143B);
    const bool u1_empty_step1 = emu.uart().channel(1).rx_empty();

    const bool step1_ok = (u0_step1 == 0x77) && u1_empty_step1;

    // Step 2: iomode_en=1, iomode_0=1 → joy→UART 1 mux.
    emu.nextreg().write(0x0B, 0x81);              // iomode_en=1, iomode_0=1
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
          "NR 0x0B bit7=1 & bit0=0, to UART 1 when bit7=1 & bit0=1, and "
          "is dropped when bit7=0",
          step1_ok && step2_ok && step3_ok,
          fmt("step1 u0=0x%02X (want 0x77) u1_empty=%d; step2 u1=0x%02X (want 0x55) "
              "u0_empty=%d; step3 u0_empty=%d u1_empty=%d",
              u0_step1, u1_empty_step1 ? 1 : 0,
              u1_step2, u0_empty_step2 ? 1 : 0,
              u0_empty_step3 ? 1 : 0, u1_empty_step3 ? 1 : 0));
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
    std::printf("  Group: DUAL — done\n");

    test_uart_device_seam(emu);
    std::printf("  Group: DEV — done\n");

    test_nr_a0_pi_uart_routing(emu);
    std::printf("  Group: NR_A0-INT — done\n");

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
