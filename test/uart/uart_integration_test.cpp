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
    cfg.roms_directory = "/usr/share/fuse";
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

// Wave D adds DUAL-05/06 here (dual-UART routing + joystick IO-mode mux).
// The fresh Wave C commit leaves a clean anchor — Wave D's merger appends
// test_dual_routing() + the main() wire-up below.

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
