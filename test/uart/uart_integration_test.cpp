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
#include "core/pi_qemu.h"
#include "core/nextpi_provisioner.h"
#include "core/rzx.h"
#include "core/saveable.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/pi_uart_device.h"
#include "peripheral/joy_uart_source.h"
#include "peripheral/uart_device.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <zlib.h>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include <process.h>
#endif
#include "../row_id.h"

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
    report_row_id(id);
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
    report_row_id(id);
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
// Section JOY — the joystick-connector UART cable (JOY-01..12), GH #251, #254
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

// A tag unique to this process: concurrent runs share the temp directory, and a
// fixed name lets one run replace or delete another's file (or FIFO).
std::string pid_tag() {
#ifdef _WIN32
    return std::to_string(::_getpid());
#else
    return std::to_string(::getpid());
#endif
}

// A `--joy-uart-rx` stream on disk, removed when the row's scope ends. Real
// file I/O rather than a test-only injection hook, so the row covers
// read_joy_uart_source_file() and the config seam a command line actually uses.
class TempSourceFile {
public:
    explicit TempSourceFile(const std::string& tag, const std::vector<uint8_t>& bytes) {
        path_ = (std::filesystem::temp_directory_path()
                 / ("jnext-joy-uart-" + tag + "-" + pid_tag() + ".bin")).string();
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
// `Emulator::rewind_to_frame` deliberately ends PAUSED — a rewind in the GUI
// lands the user in the debugger at the restored instant — so a programmatic
// replay that does not resume silently executes NOTHING and every "the replay
// matched" assertion becomes a comparison of empty frames against empty frames.
// (It used to arm the machine too, by the `active()` bit this also cleared; GH
// #278 WP3 stopped that and WP4c retired the bit.)
bool rewind_and_resume(Emulator& emu, uint32_t frame) {
    if (!emu.rewind_to_frame(frame)) return false;
    emu.debug_state().resume();
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


// ── GH #252 live-cable helpers ────────────────────────────────────────
//
// Real descriptors throughout. A mock endpoint would assert that the pacing
// arithmetic is self-consistent and nothing about whether a FIFO can be opened
// without a peer, which is the whole of the defect GH #252 reports.

// A FIFO pair on disk with the host's two ends held open, removed when the
// row's scope ends.
//
// `attach()` is separate from the constructor because the ORDER matters and is
// itself part of what is under test: jnext opens `<base>.rx` for reading at
// Emulator::init(), and only then can the host open the same path for writing
// at all (O_WRONLY on a FIFO with no reader is the ENXIO the issue reports).
class TempFifoCable {
public:
    explicit TempFifoCable(const std::string& tag) {
        base_ = (std::filesystem::temp_directory_path()
                 / ("jnext-joy-cable-" + tag + "-" + pid_tag())).string();
        remove_files();
    }
    ~TempFifoCable() {
        close_host_rx_writer();
        close_host_tx_reader();
        remove_files();
    }
    TempFifoCable(const TempFifoCable&)            = delete;
    TempFifoCable& operator=(const TempFifoCable&) = delete;

    const std::string& base() const { return base_; }

    /// Open the host's ends, AFTER the emulator has created and opened its own.
    /// `open_tx_reader` false leaves `<base>.tx` with no reader, which is the
    /// state a run starts in before the debugger on the host attaches.
    bool attach(bool open_tx_reader = true) {
#ifdef _WIN32
        (void)open_tx_reader;
        return false;
#else
        rx_w_ = ::open((base_ + ".rx").c_str(), O_WRONLY | O_NONBLOCK);
        if (open_tx_reader && !open_tx_reader_now()) return false;
        return rx_w_ >= 0;
#endif
    }

    bool open_tx_reader_now() {
#ifdef _WIN32
        return false;
#else
        tx_r_ = ::open((base_ + ".tx").c_str(), O_RDONLY | O_NONBLOCK);
        return tx_r_ >= 0;
#endif
    }

    void close_host_tx_reader() {
#ifndef _WIN32
        if (tx_r_ >= 0) { ::close(tx_r_); tx_r_ = -1; }
#endif
    }
    void close_host_rx_writer() {
#ifndef _WIN32
        if (rx_w_ >= 0) { ::close(rx_w_); rx_w_ = -1; }
#endif
    }

    /// Host → Next. Returns how many bytes the pipe took.
    std::size_t send(const std::vector<uint8_t>& bytes) {
#ifdef _WIN32
        (void)bytes; return 0;
#else
        if (rx_w_ < 0 || bytes.empty()) return 0;
        const ssize_t n = ::write(rx_w_, bytes.data(), bytes.size());
        return (n > 0) ? static_cast<std::size_t>(n) : 0;
#endif
    }

    /// Next → host, non-blocking; appends whatever is there right now.
    std::vector<uint8_t> drain() {
        std::vector<uint8_t> got;
#ifndef _WIN32
        if (tx_r_ < 0) return got;
        uint8_t buf[512];
        for (;;) {
            const ssize_t n = ::read(tx_r_, buf, sizeof(buf));
            if (n <= 0) break;
            got.insert(got.end(), buf, buf + n);
        }
#endif
        return got;
    }

private:
    void remove_files() {
        std::error_code ec;
        std::filesystem::remove(base_ + ".rx", ec);
        std::filesystem::remove(base_ + ".tx", ec);
    }
    std::string base_;
    int         rx_w_ = -1;
    int         tx_r_ = -1;
};

// Config for a Next with a LIVE joystick cable attached.
EmulatorConfig link_config(const std::string& base, int connector) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.joy_uart_fifo        = base;
    cfg.joy_uart_connector   = connector;
    return cfg;
}

// Transmit one byte from the guest on `channel` with NR 0x0B held at `nr_0b`,
// and run a whole frame so the byte-level TX engine completes it and the
// cable's once-per-frame poll() runs.
void guest_transmit_frame(Emulator& emu, uint8_t nr_0b, int channel, uint8_t byte) {
    emu.nextreg().write(0x0B, nr_0b);
    emu.port().out(0x153B, static_cast<uint8_t>(channel ? 0x40 : 0x00));
    emu.port().out(0x133B, byte);
    emu.run_frame();
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
            (std::filesystem::temp_directory_path() /
             ("jnext-joy-uart-nonexistent-" + pid_tag() + ".bin"))
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

    // ── JOY-12 — the cable is PACED by the channel NR 0x0B bit 0 routes it to
    // (GH #254). zxnext.vhd:3340-3341 hands `joy_uart_rx` to `uart0_rx` or to
    // `uart1_rx`, and each of those is sampled by its own channel's receiver at
    // its own prescaler (uart.vhd:404 / :589, `i_prescaler` of uart0_rx_mod /
    // uart1_rx_mod; zxnext.vhd:3385 / :3403 wire the two pins in). So the byte
    // rate the guest sees is the SELECTED channel's.
    //
    // Every other JOY row leaves both prescalers at the boot default, where the
    // two channels' byte times are equal and the choice is invisible: hard-coding
    // the pacing channel survived the whole suite. Here channel 1 is programmed
    // 8x slower than channel 0 — port 0x153B with bit 4 set writes its prescaler
    // MSB (uart.vhd:280-287), port 0x143B its LSB halves (uart.vhd:320-331) — and
    // the cable is routed to each channel in turn, so pinning the choice either
    // way fails one of the two runs. The expected count is the elapsed master
    // cycles over the selected channel's byte time, ±1 for where the run's edges
    // fall against it; the unselected channel's rate is 8x away from that.
    {
        TempSourceFile file("pace", cable_stream());

        struct Pace {
            std::size_t read = 0, delivered = 0, dropped = 0;
            uint64_t    cycles = 0;
            uint32_t    t0 = 0, t1 = 0;
        };
        auto pace = [&file](int channel) {
            Pace p;
            Emulator emu;
            emu.init(joy_config(file.path(), /*connector=*/1, /*delay_frames=*/0));

            emu.port().out(0x153B, 0x50);         // select ch 1, write MSB = 0
            emu.port().out(0x143B, 0x18);         // LSB bits 6:0
            emu.port().out(0x143B, 0x80 | 0x0F);  // LSB bits 13:7 -> 0x798 = 1944
            emu.port().out(0x153B, 0x00);         // back to ch 0, MSB untouched

            const uint64_t start = emu.clock().get();
            p.read   = run_and_drain(emu, static_cast<uint8_t>(0xB0 | channel),
                                     channel, 2).size();
            p.cycles = emu.clock().get() - start;
            if (const JoyUartSource* src = emu.joy_uart_source()) {
                p.delivered = src->delivered();
                p.dropped   = src->dropped();
            }
            p.t0 = emu.uart().channel(0).byte_transfer_ticks();
            p.t1 = emu.uart().channel(1).byte_transfer_ticks();
            return p;
        };
        auto paced_by = [](const Pace& p, uint32_t t) {
            const uint64_t want = p.cycles / t;
            return p.read == p.delivered && p.dropped == 0
                && p.read + 1 >= want && p.read <= want + 1;
        };

        const Pace on1 = pace(1);
        const Pace on0 = pace(0);
        const bool programmed = on1.t0 == 2430 && on1.t1 == 19440
                             && on0.t0 == 2430 && on0.t1 == 19440;

        check("JOY-12",
              "zxnext.vhd:3340-3341 — the joystick cable is paced at the byte "
              "time of the channel NR 0x0B bit 0 routes it to (each channel's "
              "receiver samples at its own prescaler, uart.vhd:404,589): with "
              "channel 1 programmed 8x slower than channel 0, routing to "
              "channel 1 delivers at channel 1's rate and routing to channel 0 "
              "at channel 0's",
              programmed && paced_by(on1, on1.t1) && paced_by(on0, on0.t0),
              fmt("byte ticks ch0=%u ch1=%u (want 2430/19440); routed to ch1: "
                  "read %zu (want %llu±1 at ch1, %llu at ch0) delivered=%zu "
                  "dropped=%zu over %llu cycles; routed to ch0: read %zu (want "
                  "%llu±1 at ch0, %llu at ch1) delivered=%zu dropped=%zu over "
                  "%llu cycles",
                  on1.t0, on1.t1, on1.read,
                  static_cast<unsigned long long>(on1.cycles / 19440),
                  static_cast<unsigned long long>(on1.cycles / 2430),
                  on1.delivered, on1.dropped,
                  static_cast<unsigned long long>(on1.cycles), on0.read,
                  static_cast<unsigned long long>(on0.cycles / 2430),
                  static_cast<unsigned long long>(on0.cycles / 19440),
                  on0.delivered, on0.dropped,
                  static_cast<unsigned long long>(on0.cycles)));
    }

    // ══════════════════════════════════════════════════════════════════
    // GH #252 — the RETURN direction, and the live cable.
    //
    // Everything above is one-way: a recorded stream going in. What the
    // hardware also does is send, and jnext had nowhere to send it — the
    // isolated branch of `UartChannel::deliver_tx_byte` dropped the byte
    // because there was no representation for joystick pin 7 at all.
    // ══════════════════════════════════════════════════════════════════

    // ── JOY-13 — a byte the guest transmits while the joystick connector owns
    // the channel COMES OUT ON PIN 7 (zxnext.vhd:3526-3531):
    //
    //   joy_iomode_pin7 <= uart0_tx   when nr_0b_joy_iomode_0 = '0'
    //
    // which leaves the core as `o_JOY_IO_MODE_PIN_7` (zxnext.vhd:1593) and is
    // driven onto the connector by md6_joystick_connector_x2.vhd:116. All three
    // of the old outcomes still have to hold at the same time — the module-facing
    // pin stays idle (zxnext.vhd:3343) so the ESP hears nothing, and there is
    // still no loopback into this channel's own RX FIFO — or the new route would
    // be a fourth copy of the byte rather than its destination.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);

        std::vector<uint8_t> pin7;
        emu.uart().set_joy_uart_tx_sink([&pin7](uint8_t b) { pin7.push_back(b); });

        emu.nextreg().write(0x0B, 0xB0);        // mux on, channel 0, joy 2
        emu.port().out(0x153B, 0x00);
        emu.port().out(0x133B, 0xAA);           // guest transmits
        tick_uart_byte(emu);
        const bool on_pin7      = (pin7.size() == 1) && (pin7[0] == 0xAA);
        const bool device_silent = esp.rx.empty();
        const bool no_loopback   = emu.uart().channel(0).rx_empty();

        // Positive control the other way: with the mux OFF the byte goes to the
        // module on the wire and pin 7 carries nothing, so the row cannot pass
        // for a build that simply sends every transmitted byte everywhere.
        emu.nextreg().write(0x0B, 0x00);
        emu.port().out(0x133B, 0xBB);
        tick_uart_byte(emu);
        const bool device_heard = (esp.rx.size() == 1) && (esp.rx[0] == 0xBB);
        const bool pin7_quiet   = (pin7.size() == 1);

        emu.uart().set_joy_uart_tx_sink(nullptr);
        emu.uart().detach_device(0);

        check("JOY-13",
              "zxnext.vhd:3526-3531,1593 — while the joystick UART mux owns "
              "UART 0 a transmitted byte leaves on joy pin 7, not through the "
              "module-facing pin (:3343) and not looped back into the channel's "
              "own RX FIFO; with the mux off it goes to the module instead",
              on_pin7 && device_silent && no_loopback && device_heard && pin7_quiet,
              fmt("on_pin7=%d device_silent=%d no_loopback=%d device_heard=%d "
                  "pin7_quiet=%d (all want 1); pin7=[%s] device rx=%zu",
                  on_pin7 ? 1 : 0, device_silent ? 1 : 0, no_loopback ? 1 : 0,
                  device_heard ? 1 : 0, pin7_quiet ? 1 : 0,
                  bytes_hex(pin7).c_str(), esp.rx.size()));
    }

    // ── JOY-14 — NR 0x0B bit 0 chooses WHICH channel's TX pin 7 carries, and
    // the VHDL spells the two out separately:
    //
    //   if nr_0b_joy_iomode_0 = '0' then joy_iomode_pin7 <= uart0_tx;
    //   else                             joy_iomode_pin7 <= uart1_tx;   (:3526-3530)
    //
    // JOY-13 pins bit 0 = 0. Without this row the second arm could compare the
    // wrong channel — a copy-paste away in `Uart::set_joy_uart_tx_sink`'s
    // two-line body — and the Pi-side rig would silently transmit nothing.
    {
        Emulator emu;
        build_next_emulator(emu);
        StubUartDevice esp;
        emu.uart().attach_device(0, &esp);      // the ESP stays on UART 0

        std::vector<uint8_t> pin7;
        emu.uart().set_joy_uart_tx_sink([&pin7](uint8_t b) { pin7.push_back(b); });

        emu.nextreg().write(0x0B, 0xB1);        // mux on, channel 1, joy 2
        emu.port().out(0x153B, 0x40);           // select channel 1
        emu.port().out(0x133B, 0x5A);
        tick_uart_byte(emu);
        const bool ch1_on_pin7 = (pin7.size() == 1) && (pin7[0] == 0x5A);

        // ...and UART 0, which the mux did NOT take, still reaches its module.
        emu.port().out(0x153B, 0x00);
        emu.port().out(0x133B, 0xC3);
        tick_uart_byte(emu);
        const bool ch0_to_device = (esp.rx.size() == 1) && (esp.rx[0] == 0xC3);
        const bool pin7_only_ch1 = (pin7.size() == 1);

        emu.uart().set_joy_uart_tx_sink(nullptr);
        emu.uart().detach_device(0);

        check("JOY-14",
              "zxnext.vhd:3526-3530 — with NR 0x0B bit 0 = 1 it is UART 1's TX "
              "that pin 7 carries, while UART 0 keeps its own module-facing pin "
              "and reaches the ESP normally",
              ch1_on_pin7 && ch0_to_device && pin7_only_ch1,
              fmt("ch1_on_pin7=%d ch0_to_device=%d pin7_only_ch1=%d (all want 1); "
                  "pin7=[%s] device rx=[%s]",
                  ch1_on_pin7 ? 1 : 0, ch0_to_device ? 1 : 0,
                  pin7_only_ch1 ? 1 : 0, bytes_hex(pin7).c_str(),
                  bytes_hex(esp.rx).c_str()));
    }

    // ── JOY-15 — THE TWO DIRECTIONS ARE GATED DIFFERENTLY, and that asymmetry
    // is the VHDL's:
    //
    //   * RX is connector-selected. `joy_uart_rx <= ((not nr_0b_joy_iomode(0))
    //     and not i_JOY_LEFT(5)) or (nr_0b_joy_iomode(0) and not
    //     i_JOY_RIGHT(5))` (zxnext.vhd:3538) — bit 4 picks ONE socket's pin to
    //     listen on.
    //   * TX is not. `o_JOY_IO_MODE_PIN_7` (zxnext.vhd:1593) has no connector
    //     selection anywhere in it; the board presents it to whichever socket
    //     `o_joy_select` currently points at, and in io mode that free-runs
    //     (`state <= "1111100" & state_next(1 downto 0)`, o_joy_select <=
    //     state(1) — md6_joystick_connector_x2.vhd:109,117), alternating every
    //     two CLK_28 ticks while one bit at 115200 lasts 243 of them. Both
    //     sockets therefore see the transmitted bit.
    //
    // So a cable in the socket bit 4 does NOT select can still HEAR the Next
    // while being unable to be heard by it. Copying the RX gate onto the TX
    // path — the obvious-looking symmetry — would silently half-break exactly
    // that rig, and nothing else in this suite would notice.
    {
        TempFifoCable cable("asym");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/0));      // cable in joy 1
        const bool attached = cable.attach();

        // NR 0x0B bit 4 = 1 selects joy 2 — the OTHER socket.
        cable.send({0x11, 0x22});
        guest_transmit_frame(emu, 0xB0, 0, 0x99);
        emu.nextreg().write(0x0B, 0xB0);
        emu.run_frame();

        const JoyUartLink* link = emu.joy_uart_link();
        const std::vector<uint8_t> heard_by_host = cable.drain();
        const bool rx_dropped = link && link->delivered() == 0 && link->dropped() > 0;
        const bool rx_fifo_empty = emu.uart().channel(0).rx_empty();
        const bool tx_arrived = (heard_by_host.size() == 1) && (heard_by_host[0] == 0x99);

        check("JOY-15",
              "zxnext.vhd:3538 vs :1593 + md6_joystick_connector_x2.vhd:109,117 "
              "— NR 0x0B bit 4 selects which socket is LISTENED to, while pin 7 "
              "is presented to both sockets in turn, so a cable in the "
              "unselected socket is not heard by the Next yet still hears it",
              attached && rx_dropped && rx_fifo_empty && tx_arrived,
              fmt("attached=%d rx_dropped=%d rx_fifo_empty=%d tx_arrived=%d "
                  "(all want 1); delivered=%zu dropped=%zu; host heard [%s] "
                  "(want 99)",
                  attached ? 1 : 0, rx_dropped ? 1 : 0, rx_fifo_empty ? 1 : 0,
                  tx_arrived ? 1 : 0, link ? link->delivered() : 0,
                  link ? link->dropped() : 0, bytes_hex(heard_by_host).c_str()));
    }

    // ── JOY-16 — THE FEATURE. A live FIFO cable carries both directions of a
    // conversation while the machine runs: the host's bytes cross the
    // zxnext.vhd:3340-3341 RX mux into the channel the guest reads at port
    // 0x143B, and the guest's bytes come back out of the zxnext.vhd:3526-3531
    // TX mux onto a descriptor the host reads.
    //
    // Real descriptors, real `mkfifo`, real `open`. That is the point: the
    // one-way path this replaces failed not in its arithmetic but at `open()`
    // — a FIFO blocked until its writer closed and a socket returned ENXIO —
    // and no amount of in-memory stubbing would have found either.
    {
        TempFifoCable cable("duplex");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));      // cable in joy 2
        const bool attached = cable.attach();

        const std::vector<uint8_t> from_host = {0x4A, 0x4E, 0x58, 0x54};
        cable.send(from_host);

        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);      // mux on, channel 0, joy 2
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                to_guest.push_back(emu.port().in(0x143B));
        }

        const std::vector<uint8_t> from_guest = {0x01, 0x02, 0x03};
        for (uint8_t b : from_guest) guest_transmit_frame(emu, 0xB0, 0, b);
        const std::vector<uint8_t> to_host = cable.drain();

        const JoyUartLink* link = emu.joy_uart_link();

        check("JOY-16",
              "GH #252 — a live joystick-port cable carries both directions at "
              "once: the host's bytes reach port 0x143B through the "
              "zxnext.vhd:3340-3341 RX mux and the guest's reach the host "
              "through the zxnext.vhd:3526-3531 pin-7 TX mux, over real FIFOs "
              "opened while the machine runs",
              attached && to_guest == from_host && to_host == from_guest
                  && link && link->dropped() == 0 && link->unsent() == 0,
              fmt("attached=%d; guest got [%s] (want [4A 4E 58 54]); host got "
                  "[%s] (want [01 02 03]); delivered=%zu dropped=%zu sent=%zu "
                  "unsent=%zu",
                  attached ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str(),
                  link ? link->delivered() : 0, link ? link->dropped() : 0,
                  link ? link->sent() : 0, link ? link->unsent() : 0));
    }

    // ── JOY-17 — the live cable's RX is PACED at the receiving channel's byte
    // time, not handed over as fast as the host supplies it.
    //
    // uart_rx.vhd clocks one frame per `prescaler * frame_bits` CLK_28 ticks
    // (uart.vhd:404 wires the channel's own prescaler into its receiver), and
    // the Next-side RX FIFO is 512 entries with drop-newest overflow. A host
    // that writes 3000 bytes in one go — one DeZog memory-read response — would
    // therefore lose 5/6 of them to the FIFO before the guest's first read if
    // the cable simply dumped what it had. The count below is the elapsed
    // master cycles over the channel's byte time, ±1 for where the run's edges
    // fall, which is three thousand away from "everything at once".
    {
        TempFifoCable cable("pace");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        std::vector<uint8_t> burst(3000);
        for (std::size_t i = 0; i < burst.size(); ++i)
            burst[i] = static_cast<uint8_t>((i * 11 + 3) & 0xFF);
        const std::size_t offered = cable.send(burst);

        const uint64_t start = emu.clock().get();
        std::vector<uint8_t> got;
        for (int f = 0; f < 2; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                got.push_back(emu.port().in(0x143B));
        }
        const uint64_t cycles     = emu.clock().get() - start;
        const uint32_t byte_ticks = emu.uart().channel(0).byte_transfer_ticks();
        const uint64_t want       = cycles / byte_ticks;

        // The prefix must be the stream's, in order — a paced-but-scrambled
        // delivery would satisfy a bare count.
        bool prefix_ok = got.size() <= burst.size();
        for (std::size_t i = 0; prefix_ok && i < got.size(); ++i)
            prefix_ok = (got[i] == burst[i]);

        check("JOY-17",
              "uart.vhd:404 / uart_rx.vhd — the live cable delivers at the "
              "receiving channel's byte time (prescaler * frame_bits), so a "
              "3000-byte host burst arrives over many frames in order rather "
              "than overflowing the 512-entry RX FIFO in one",
              attached && offered >= burst.size() && prefix_ok
                  && got.size() + 1 >= want && got.size() <= want + 1
                  && got.size() < burst.size(),
              fmt("attached=%d offered=%zu (want 3000); got %zu byte(s) (want "
                  "%llu±1 and < 3000); in-order prefix=%d; byte_ticks=%u over "
                  "%llu cycles",
                  attached ? 1 : 0, offered, got.size(),
                  static_cast<unsigned long long>(want), prefix_ok ? 1 : 0,
                  byte_ticks, static_cast<unsigned long long>(cycles)));
    }

    // ── JOY-18 — NOTHING BLOCKS WHEN THERE IS NO FAR END, and a far end that
    // turns up late is picked up.
    //
    // This is the defect GH #252 reports, in its most literal form: the
    // Next→host FIFO cannot be opened at all until something is reading it
    // (`O_WRONLY | O_NONBLOCK` gives ENXIO), so an eager open would either fail
    // the run or — with the blocking form — wedge the emulator before the first
    // frame. The cable opens that side LAZILY and retries it on every flush, so
    // a debugger started after the emulator still gets the traffic that was
    // queued while it was not there.
    {
        TempFifoCable cable("late");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach(/*open_tx_reader=*/false);

        const std::vector<uint8_t> markers = {0x71, 0x72, 0x73};
        for (uint8_t b : markers) guest_transmit_frame(emu, 0xB0, 0, b);

        const JoyUartLink* link = emu.joy_uart_link();
        const bool nothing_sent_yet = link && link->sent() == 0
                                   && link->unsent() == 0 && link->faults() == 0;

        const bool reader_opened = cable.open_tx_reader_now();
        emu.nextreg().write(0x0B, 0xB0);
        emu.run_frame();                        // the frame seam retries the open
        const std::vector<uint8_t> got = cable.drain();

        check("JOY-18",
              "GH #252 — the Next->host FIFO is opened lazily because "
              "O_WRONLY|O_NONBLOCK on a FIFO with no reader is ENXIO: with no "
              "peer the run proceeds and nothing is lost or faulted, and a "
              "reader that attaches later receives what was queued",
              attached && nothing_sent_yet && reader_opened && got == markers,
              fmt("attached=%d nothing_sent_yet=%d reader_opened=%d (all want "
                  "1); got [%s] (want [71 72 73]); sent=%zu unsent=%zu faults=%zu",
                  attached ? 1 : 0, nothing_sent_yet ? 1 : 0,
                  reader_opened ? 1 : 0, bytes_hex(got).c_str(),
                  link ? link->sent() : 0, link ? link->unsent() : 0,
                  link ? link->faults() : 0));
    }

    // ── JOY-19 — THE FAR END DISAPPEARS MID-SESSION, and comes back.
    //
    // Closing the read end of a FIFO makes the next write fail with EPIPE —
    // and, with the default signal disposition, raise SIGPIPE and KILL the
    // process. jnext ignores SIGPIPE when it opens the endpoint, takes the
    // EPIPE, and then DISCARDS what is queued: those bytes are half of a
    // conversation with a process that has exited, and giving the remains of
    // them to whatever connects next would hand that peer a truncated message
    // it cannot recognise as stale. The loss is counted rather than silent, and
    // the cable re-opens for the next peer.
    {
        TempFifoCable cable("bounce");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        guest_transmit_frame(emu, 0xB0, 0, 0xA1);
        const std::vector<uint8_t> before = cable.drain();

        cable.close_host_tx_reader();
        guest_transmit_frame(emu, 0xB0, 0, 0xA2);      // EPIPE: lost with the peer

        const JoyUartLink* link = emu.joy_uart_link();
        const bool survived   = (link != nullptr);
        const bool loss_seen  = link && link->unsent() == 1 && link->sent() == 1;

        const bool reopened = cable.open_tx_reader_now();
        guest_transmit_frame(emu, 0xB0, 0, 0xA3);
        const std::vector<uint8_t> after = cable.drain();

        check("JOY-19",
              "GH #252 — a peer that closes mid-session makes the next write "
              "EPIPE (SIGPIPE is ignored, so the emulator survives it); the "
              "stale queue is discarded and counted rather than delivered to "
              "the next peer, and the cable re-opens for one that reconnects",
              attached && survived && before == std::vector<uint8_t>{0xA1}
                  && loss_seen && reopened && after == std::vector<uint8_t>{0xA3},
              fmt("attached=%d survived=%d loss_seen=%d reopened=%d (all want "
                  "1); before=[%s] (want A1) after=[%s] (want A3); sent=%zu "
                  "unsent=%zu faults=%zu",
                  attached ? 1 : 0, survived ? 1 : 0, loss_seen ? 1 : 0,
                  reopened ? 1 : 0, bytes_hex(before).c_str(),
                  bytes_hex(after).c_str(), link ? link->sent() : 0,
                  link ? link->unsent() : 0, link ? link->faults() : 0));
    }

    // ── JOY-20 — THE REPLAY GATE. A live descriptor is not snapshottable and
    // nothing about this cable rides in the state stream, which is the opposite
    // of the GH #251 recording (JOY-10/11) and for a reason the recording does
    // not have: bytes already handed to the peer cannot be unsent.
    //
    // So while `replay_mode_` holds — rewind fast-forward and RZX playback both
    // re-execute instructions the guest already ran — the cable is INERT, the
    // `EspUartAdapter::set_inert` posture. Two halves, and the second is the one
    // a naive gate gets wrong: a replayed frame must not re-transmit (the peer
    // would see the byte twice), and it must not READ either, because the host
    // bytes it consumed would be gone from the timeline that resumes afterwards.
    {
        TempFifoCable cable("replay");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        guest_transmit_frame(emu, 0xB0, 0, 0xD1);
        const std::vector<uint8_t> live_before = cable.drain();

        emu.set_replay_mode(true);
        cable.send({0xE1, 0xE2});                   // host talks during the replay
        guest_transmit_frame(emu, 0xB0, 0, 0xD2);   // guest re-transmits
        const std::vector<uint8_t> during = cable.drain();
        const JoyUartLink* link = emu.joy_uart_link();
        const bool not_read = link && link->received() == 0;

        emu.set_replay_mode(false);
        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                to_guest.push_back(emu.port().in(0x143B));
        }
        guest_transmit_frame(emu, 0xB0, 0, 0xD3);
        const std::vector<uint8_t> live_after = cable.drain();

        check("JOY-20",
              "GH #252 — the live cable is held inert while replay_mode_ holds "
              "(the EspUartAdapter::set_inert posture): a re-executed frame "
              "neither re-transmits to the peer nor consumes the host bytes the "
              "resumed timeline still needs, and both directions return when "
              "the gate lifts",
              attached && live_before == std::vector<uint8_t>{0xD1}
                  && during.empty() && not_read
                  && to_guest == std::vector<uint8_t>({0xE1, 0xE2})
                  && live_after == std::vector<uint8_t>{0xD3},
              fmt("attached=%d not_read=%d (both want 1); before=[%s] (want D1) "
                  "during=[%s] (want empty) after=[%s] (want D3); guest got [%s] "
                  "(want [E1 E2]); received=%zu",
                  attached ? 1 : 0, not_read ? 1 : 0,
                  bytes_hex(live_before).c_str(), bytes_hex(during).c_str(),
                  bytes_hex(live_after).c_str(), bytes_hex(to_guest).c_str(),
                  link ? link->received() : 0));
    }

    // ── JOY-21 — the PTY form of the same cable, end to end.
    //
    // A pty is what the consumer this issue names can actually open: DeZog's
    // serial remote wants one serial DEVICE, not a pair of pipes, so without
    // this transport the feature reaches a shell script and stops. It is also
    // the form with a different failure surface — one descriptor for both
    // directions, a line discipline that echoes and mangles CR/LF unless the
    // termios is put in raw mode, and a master that survives its slave closing
    // — so exercising it through the FIFO's rows would prove nothing about it.
    //
    // The slave path is read back from the cable's own description, which is
    // also the only way a user learns it.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.joy_uart_pty         = true;
        cfg.joy_uart_connector   = 1;
        emu.init(cfg);

        const JoyUartLink* link = emu.joy_uart_link();
        std::string slave;
        if (link) {
            const std::string& d = link->describe();
            const std::size_t at = d.find("pty ");
            if (at != std::string::npos) slave = d.substr(at + 4);
        }

        int slave_fd = -1;
#ifndef _WIN32
        if (!slave.empty())
            slave_fd = ::open(slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
#endif
        const bool opened = slave_fd >= 0;

        std::vector<uint8_t> to_guest;
        std::vector<uint8_t> to_host;
        if (opened) {
#ifndef _WIN32
            const uint8_t out[] = {0x31, 0x32};
            (void)!::write(slave_fd, out, sizeof(out));
#endif
            for (int f = 0; f < 3; ++f) {
                emu.nextreg().write(0x0B, 0xB0);
                emu.run_frame();
                emu.port().out(0x153B, 0x00);
                while (!emu.uart().channel(0).rx_empty())
                    to_guest.push_back(emu.port().in(0x143B));
            }
            guest_transmit_frame(emu, 0xB0, 0, 0x7E);
            guest_transmit_frame(emu, 0xB0, 0, 0x0D);   // CR: raw mode must not translate it
#ifndef _WIN32
            uint8_t buf[64];
            for (int tries = 0; tries < 4 && to_host.size() < 2; ++tries) {
                const ssize_t n = ::read(slave_fd, buf, sizeof(buf));
                if (n > 0) to_host.insert(to_host.end(), buf, buf + n);
                emu.nextreg().write(0x0B, 0xB0);
                emu.run_frame();
            }
            ::close(slave_fd);
#endif
        }

        check("JOY-21",
              "GH #252 — the pty transport carries the same zxnext.vhd:3340-3341 "
              "/ :3526-3531 mux in both directions over one descriptor, with the "
              "termios in raw mode so a 0x0D is delivered as a byte rather than "
              "translated by the line discipline",
              opened && to_guest == std::vector<uint8_t>({0x31, 0x32})
                  && to_host == std::vector<uint8_t>({0x7E, 0x0D}),
              fmt("slave='%s' opened=%d; guest got [%s] (want [31 32]); host got "
                  "[%s] (want [7E 0D])",
                  slave.c_str(), opened ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str()));
    }

    // ── JOY-22 — the peer-loss discard throws away the RECEIVE queue too, and
    // that half is invisible from outside unless a row puts bytes in it.
    //
    // `flush_tx()` clears BOTH queues when a write reports EPIPE, because both
    // hold half of an exchange with a process that has exited. JOY-19 covers
    // the transmit half — it is the one the counters show. The receive half is
    // host bytes already pulled off the descriptor and not yet clocked into the
    // guest, and nothing about them is visible at the moment they are dropped:
    // deleting `rx_queue_.clear()` left the whole suite green, so that half of
    // the documented decision lived only in a comment.
    //
    // The shape that makes it observable is a burst BIGGER than one frame's
    // worth of pacing. 2000 bytes at the 115200 default is ~230 per frame, so
    // after one frame ~1770 are still queued when the peer goes away — and if
    // they were kept, the frames after it would go on delivering them.
    {
        TempFifoCable cable("rxdiscard");
        Emulator emu;
        emu.init(link_config(cable.base(), /*connector=*/1));
        const bool attached = cable.attach();

        std::vector<uint8_t> burst(2000);
        for (std::size_t i = 0; i < burst.size(); ++i)
            burst[i] = static_cast<uint8_t>((i * 5 + 9) & 0xFF);
        const std::size_t offered = cable.send(burst);

        // One frame: the whole burst comes off the descriptor into the queue,
        // and only a frame's worth of it reaches the guest. The guest also
        // TRANSMITS here, and that is not decoration — the Next->host FIFO is
        // opened lazily, so until a byte has actually gone out jnext holds no
        // write descriptor and a peer closing is indistinguishable from one
        // that never attached (JOY-18's case, where nothing is discarded
        // because nothing was lost). Writing the row without this step made it
        // fail against correct code, which is how the distinction was found.
        std::vector<uint8_t> before;
        guest_transmit_frame(emu, 0xB0, 0, 0xA1);
        emu.port().out(0x153B, 0x00);
        while (!emu.uart().channel(0).rx_empty())
            before.push_back(emu.port().in(0x143B));
        const bool peer_was_attached = cable.drain() == std::vector<uint8_t>{0xA1};

        // NOW the peer goes away, and the guest's next transmitted byte is what
        // discovers it (EPIPE). Everything still queued dies with it.
        cable.close_host_tx_reader();
        guest_transmit_frame(emu, 0xB0, 0, 0xA2);

        const JoyUartLink* link = emu.joy_uart_link();
        const std::size_t delivered_at_loss = link ? link->delivered() : 0;
        // Bytes that came off the descriptor and never reached the sink: the
        // remainder that was in the queue at the instant of the loss. If this
        // is 0 the row is asserting nothing.
        const std::size_t abandoned = link
            ? link->received() - link->delivered() - link->dropped() : 0;

        // ...and now nothing more may arrive, however long the machine runs.
        std::vector<uint8_t> after;
        for (int f = 0; f < 3; ++f) {
            emu.nextreg().write(0x0B, 0xB0);
            emu.run_frame();
            emu.port().out(0x153B, 0x00);
            while (!emu.uart().channel(0).rx_empty())
                after.push_back(emu.port().in(0x143B));
        }

        // `dropped() == 0` throughout, so "nothing arrived" cannot be the mux
        // having stopped routing rather than the queue having been discarded.
        const bool still_routed = link && link->dropped() == 0;

        check("JOY-22",
              "GH #252 — a peer lost mid-session takes the RECEIVE queue with "
              "it as well as the transmit one: host bytes already read off the "
              "descriptor but not yet clocked into the guest are discarded, so "
              "the next peer's session does not begin with the tail of the "
              "previous one's message",
              attached && peer_was_attached && offered >= burst.size()
                  && !before.empty() && abandoned > 0 && after.empty()
                  && link->delivered() == delivered_at_loss && still_routed,
              fmt("attached=%d peer_was_attached=%d offered=%zu (want 2000); "
                  "before the loss the guest read %zu byte(s) (want >0) and %zu "
                  "were left queued (want >0); after it read %zu (want 0); "
                  "delivered %zu -> %zu (want unchanged); dropped=%zu (want 0)",
                  attached ? 1 : 0, peer_was_attached ? 1 : 0, offered,
                  before.size(), abandoned, after.size(), delivered_at_loss,
                  link ? link->delivered() : 0, link ? link->dropped() : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// The LIVE Raspberry Pi serial link on UART 1 (NextPi, --nextpi)
// ══════════════════════════════════════════════════════════════════════
//
// The wire on the Pi GPIO header handed to the host, as a `UartDevice` on
// UART 1. Real FIFOs, for the joystick rows' reason: the host plumbing is the
// part that fails at `open()`, and only real descriptors can find that.
//
// Every row tolerates a missing link (`emu.pi_uart() == nullptr`) and FAILS on
// it rather than crashing, so deleting the `setup_pi_uart()` call from init()
// turns every row red — the demonstration that the rows test the feature.

namespace {

EmulatorConfig pi_config(const std::string& base) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_uart_fifo_rx      = base + ".rx";
    cfg.pi_uart_fifo_tx      = base + ".tx";
    return cfg;
}

// One frame with NR 0xA0 held at `nr_a0` (NR 0x0B at `nr_0b`) and UART 1
// selected, then drain UART 1's RX FIFO through port 0x143B. NR 0xA0 is
// re-written every frame for the reason the JOY rows re-write NR 0x0B: the
// guest's own ROM runs inside run_frame() and owns the register too.
std::vector<uint8_t> pi_frame(Emulator& emu, uint8_t nr_a0, uint8_t nr_0b = 0x00) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, nr_0b);
    emu.run_frame();
    emu.port().out(0x153B, 0x40);
    std::vector<uint8_t> got;
    while (!emu.uart().channel(1).rx_empty())
        got.push_back(emu.port().in(0x143B));
    return got;
}

// The guest transmits `byte` on UART 1 with the registers held as above, and a
// whole frame runs so the TX engine completes it and the link's poll() flushes.
void pi_transmit_frame(Emulator& emu, uint8_t nr_a0, uint8_t byte, uint8_t nr_0b = 0x00) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, nr_0b);
    emu.port().out(0x153B, 0x40);
    emu.port().out(0x133B, byte);
    emu.run_frame();
}

} // namespace

static void test_pi_uart_link() {
    set_group("PI");

    // ── PI-01 — THE FEATURE. With NR 0xA0 = 0x30 (UART 1 on GPIO 14/15, wired
    // for a Pi — what `.pisend` writes) a live link carries both directions:
    // the host's bytes are read by the guest at port 0x143B on UART 1, and the
    // guest's UART 1 transmissions reach the host. UART 0 hears none of it.
    {
        TempFifoCable cable("pi-duplex");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        const std::vector<uint8_t> from_host = {0x53, 0x55, 0x50, 0x3E};   // "SUP>"
        cable.send(from_host);
        std::vector<uint8_t> to_guest;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            to_guest.insert(to_guest.end(), got.begin(), got.end());
        }

        const std::vector<uint8_t> from_guest = {0x0D, 0x03, 0x03};       // CR ^C ^C
        for (uint8_t b : from_guest) pi_transmit_frame(emu, 0x30, b);
        const std::vector<uint8_t> to_host = cable.drain();

        const PiUartDevice* pi = emu.pi_uart();
        const bool uart0_quiet = emu.uart().channel(0).rx_empty();

        check("PI-01",
              "the Raspberry Pi link carries both directions over UART 1 while NR "
              "0xA0 = 0x30 connects it to the Pi GPIO pins (zxnext.vhd:2278-2281): "
              "host bytes reach port 0x143B on UART 1, the guest's UART 1 bytes "
              "reach the host, and UART 0 hears nothing",
              pi && attached && to_guest == from_host && to_host == from_guest
                  && uart0_quiet && pi->link().dropped() == 0
                  && pi->tx_disconnected() == 0,
              fmt("pi=%d attached=%d; guest got [%s] (want [53 55 50 3E]); host got "
                  "[%s] (want [0D 03 03]); uart0_quiet=%d dropped=%zu "
                  "tx_disconnected=%zu",
                  pi ? 1 : 0, attached ? 1 : 0, bytes_hex(to_guest).c_str(),
                  bytes_hex(to_host).c_str(), uart0_quiet ? 1 : 0,
                  pi ? pi->link().dropped() : 0, pi ? pi->tx_disconnected() : 0));
    }

    // ── PI-02 — THE GPIO GATE. Bits 5 and 4 of NR 0xA0 put UART 1 on GPIO 14/15
    // and choose which way round RX and TX are wired; only both set reaches a Pi.
    // With either clear (0x00 = reset, 0x10, 0x20) the Pi's bytes are lost on a
    // pin the Next is not reading and the guest's never reach the Pi — counted,
    // not queued. Opening the gate afterwards proves the link itself was alive.
    {
        TempFifoCable cable("pi-gate");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        bool all_closed_silent = true;
        std::string detail;
        int closed_tx = 0;
        for (uint8_t a0 : {uint8_t{0x00}, uint8_t{0x10}, uint8_t{0x20}}) {
            cable.send({0xE0});
            std::vector<uint8_t> heard;
            for (int f = 0; f < 2; ++f) {
                const std::vector<uint8_t> got = pi_frame(emu, a0);
                heard.insert(heard.end(), got.begin(), got.end());
            }
            pi_transmit_frame(emu, a0, 0xF0);
            ++closed_tx;
            const std::vector<uint8_t> host = cable.drain();
            if (!heard.empty() || !host.empty()) all_closed_silent = false;
            detail += fmt("a0=%02X guest[%s] host[%s]; ", a0, bytes_hex(heard).c_str(),
                          bytes_hex(host).c_str());
        }

        const PiUartDevice* pi = emu.pi_uart();
        const std::size_t dropped_closed = pi ? pi->link().dropped() : 0;
        const std::size_t tx_lost_closed = pi ? pi->tx_disconnected() : 0;

        cable.send({0x31});
        std::vector<uint8_t> open_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            open_heard.insert(open_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0x32);
        const std::vector<uint8_t> open_host = cable.drain();

        check("PI-02",
              "NR 0xA0 bits 5:4 gate the Pi link both ways (zxnext.vhd:2278-2281): "
              "with 0x00, 0x10 or 0x20 the Pi is not heard and does not hear, and "
              "the loss is counted; 0x30 then carries traffic again",
              pi && attached && all_closed_silent && dropped_closed == 3
                  && tx_lost_closed == static_cast<std::size_t>(closed_tx)
                  && open_heard == std::vector<uint8_t>({0x31})
                  && open_host == std::vector<uint8_t>({0x32}),
              fmt("pi=%d attached=%d; %sdropped=%zu (want 3) tx_disconnected=%zu "
                  "(want 3); open: guest [%s] (want 31) host [%s] (want 32)",
                  pi ? 1 : 0, attached ? 1 : 0, detail.c_str(), dropped_closed,
                  tx_lost_closed, bytes_hex(open_heard).c_str(),
                  bytes_hex(open_host).c_str()));
    }

    // ── PI-03 — NO LOOPBACK. An unattached UART 1 loops its TX back into its own
    // RX FIFO (uart.h); with the Pi attached the byte goes to the Pi and the
    // guest must not read its own transmission back.
    {
        TempFifoCable cable("pi-noloop");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        pi_transmit_frame(emu, 0x30, 0x55);
        const std::vector<uint8_t> echoed = pi_frame(emu, 0x30);
        const std::vector<uint8_t> to_host = cable.drain();

        check("PI-03",
              "with the Pi link attached, a UART 1 transmission goes to the Pi "
              "only — UART 1's unattached loopback is off, so the guest does not "
              "read its own byte back",
              emu.pi_uart() && attached && echoed.empty()
                  && to_host == std::vector<uint8_t>({0x55}),
              fmt("pi=%d attached=%d; guest read back [%s] (want empty); host got "
                  "[%s] (want 55)",
                  emu.pi_uart() ? 1 : 0, attached ? 1 : 0,
                  bytes_hex(echoed).c_str(), bytes_hex(to_host).c_str()));
    }

    // ── PI-04 — THE JOYSTICK MUX STILL WINS. NR 0x0B = 0xB1 (UART mode, bits 7
    // and 5, channel bit 0 = UART 1) gives UART 1's RX to the joystick
    // connector (zxnext.vhd:3340-3341: `uart1_rx <= joy_uart_rx`, not
    // `pi_uart_rx`) and its TX to pin 7 (:3526-3531), so the Pi is neither heard
    // nor spoken to even with NR 0xA0 = 0x30. With NR 0x0B cleared it is again.
    {
        TempFifoCable cable("pi-joymux");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        cable.send({0xA1});
        std::vector<uint8_t> muxed_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30, 0xB1);
            muxed_heard.insert(muxed_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xA2, 0xB1);
        const std::vector<uint8_t> muxed_host = cable.drain();

        cable.send({0xA3});
        std::vector<uint8_t> free_heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30, 0x00);
            free_heard.insert(free_heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xA4, 0x00);
        const std::vector<uint8_t> free_host = cable.drain();

        check("PI-04",
              "while NR 0x0B routes UART 1 to the joystick connector "
              "(zxnext.vhd:3340-3341, :3526-3531) the Pi link is isolated in both "
              "directions; with the mux off it carries traffic again",
              emu.pi_uart() && attached && muxed_heard.empty() && muxed_host.empty()
                  && free_heard == std::vector<uint8_t>({0xA3})
                  && free_host == std::vector<uint8_t>({0xA4}),
              fmt("pi=%d attached=%d; muxed: guest [%s] host [%s] (want both "
                  "empty); free: guest [%s] (want A3) host [%s] (want A4)",
                  emu.pi_uart() ? 1 : 0, attached ? 1 : 0,
                  bytes_hex(muxed_heard).c_str(), bytes_hex(muxed_host).c_str(),
                  bytes_hex(free_heard).c_str(), bytes_hex(free_host).c_str()));
    }

    // ── PI-05 — A SOFT RESET DOES NOT UNPLUG THE PI. It resets the Next-side
    // UART and NR 0xA0 (to 0x00, zxnext.vhd:5080), not the Pi on the far end:
    // the same link object stays attached and carries traffic once the guest
    // routes UART 1 to the Pi again.
    {
        TempFifoCable cable("pi-softreset");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();
        const PiUartDevice* before = emu.pi_uart();

        emu.soft_reset();
        const PiUartDevice* after = emu.pi_uart();
        const bool same_device = before && before == after
                                 && emu.uart().device(1) == static_cast<const UartDevice*>(after);
        const uint8_t a0_after_reset = static_cast<uint8_t>(emu.nextreg().read(0xA0));

        cable.send({0x5A});
        std::vector<uint8_t> heard;
        for (int f = 0; f < 2; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0x5B);
        const std::vector<uint8_t> to_host = cable.drain();

        check("PI-05",
              "a soft reset keeps the Pi link attached to UART 1 (the Pi does not "
              "see a Next-side reset) while NR 0xA0 returns to 0x00 "
              "(zxnext.vhd:5080); traffic flows again once it is set",
              attached && same_device && a0_after_reset == 0x00
                  && heard == std::vector<uint8_t>({0x5A})
                  && to_host == std::vector<uint8_t>({0x5B}),
              fmt("attached=%d same_device=%d nr_a0=%02X (want 00); guest [%s] "
                  "(want 5A) host [%s] (want 5B)",
                  attached ? 1 : 0, same_device ? 1 : 0, a0_after_reset,
                  bytes_hex(heard).c_str(), bytes_hex(to_host).c_str()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// NextPi under QEMU, launched by jnext (--nextpi)
// ══════════════════════════════════════════════════════════════════════
//
// No real QEMU and no NextPi image: a shell-script stand-in takes QEMU's
// place. It answers on the `-chardev pipe` exactly as QEMU's pipe chardev does
// — both FIFO ends opened read-write, so neither side ever sees EOF — prints
// "SUP> " like the NextPi Supervisor, and appends whatever the Next sends to a
// file. What is under test is jnext's half: the directory checks, the overlay,
// the FIFOs, the spawn, the wiring into UART 1, and the teardown.

namespace {

#ifndef _WIN32
bool write_script(const std::filesystem::path& path, const std::string& body) {
    std::ofstream f(path);
    f << body;
    f.close();
    return ::chmod(path.c_str(), 0755) == 0;
}
#endif

/// A provisioned NextPi directory's layout, with empty stand-in files, plus
/// a stand-in `qemu-system-arm` and `qemu-img` in `bin/`.
class FakeNextPi {
public:
    explicit FakeNextPi(const std::string& tag) {
        root_ = std::filesystem::temp_directory_path() / ("jnext-fake-nextpi-" + tag + "-" + pid_tag());
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_ / "dist" / "boot", ec);
        std::filesystem::create_directories(root_ / "bin", ec);
        for (const char* f : {"dist/nextpi.img", "dist/boot/kernel.img",
                              "dist/boot/bcm2708-rpi-zero.dtb"})
            std::ofstream(root_ / f).put('\0');
#ifndef _WIN32
        // What the stand-in records — its pid, environment, open descriptors
        // and arguments — is what the rows check about the child jnext made.
        // The pid goes LAST: `child_pid()` returning is the rows' signal that
        // the other records are complete. Written first, a row could read
        // `env`, `fds` or `args` after the redirection had created the file
        // and before the command had filled it.
        const std::string body =
                  "here=$(dirname \"$0\")\n"
                  "env > \"$here/env\"\n"
                  "ls /dev/fd > \"$here/fds\" 2>/dev/null\n"
                  // fd 3 cannot be read off that listing: `ls` opens the
                  // directory itself, as fd 3 when 3 is free. The shell's own
                  // redirection opens nothing.
                  "if ( : <&3 ) 2>/dev/null; then echo open; else echo closed; fi > \"$here/fd3\"\n"
                  "for a in \"$@\"; do case \"$a\" in pipe,*path=*) base=\"${a##*path=}\" ;; esac; done\n"
                  "printf '%s\\n' \"$@\" > \"$here/args\"\n"
                  // Asked to send its sound to the mixer (QEMU's wav back-end on
                  // a FIFO), the stand-in plays bin/tone.wav into it, if any.
                  "for a in \"$@\"; do case \"$a\" in wav,*path=*) au=\"${a#*path=}\"; au=\"${au%%,*}\" ;; esac; done\n"
                  "if [ -n \"$au\" ] && [ -f \"$here/tone.wav\" ]; then cat \"$here/tone.wav\" > \"$au\" & fi\n"
                  "echo $$ > \"$here/pid\"\n"
                  "exec 3<>\"$base.in\" 4<>\"$base.out\"\n"
                  "printf 'SUP> ' >&4\n"
                  "exec cat <&3 >> \"$here/received\"\n";
        ok_ = write_script(root_ / "bin" / "qemu-system-arm", "#!/bin/sh\n" + body)
           // A QEMU that ignores SIGTERM (it is inherited across exec).
           && write_script(root_ / "bin" / "qemu-stubborn", "#!/bin/sh\ntrap '' TERM\n" + body)
           && write_script(root_ / "bin" / "qemu-img",
                  "#!/bin/sh\n"
                  "for a in \"$@\"; do overlay=\"$prev\"; prev=\"$a\"; done\n"
                  ": > \"$overlay\"\n")
           && write_script(root_ / "bin" / "qemu-fails", "#!/bin/sh\nexit 3\n")
           // Outlives the start check, then exits on its own with status 7.
           && write_script(root_ / "bin" / "qemu-exits-7", "#!/bin/sh\nsleep 1\nexit 7\n");
#endif
    }
    ~FakeNextPi() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    FakeNextPi(const FakeNextPi&)            = delete;
    FakeNextPi& operator=(const FakeNextPi&) = delete;

    bool ok() const { return ok_; }
    std::string dir() const { return (root_ / "dist").string(); }
    std::string bin(const char* name) const { return (root_ / "bin" / name).string(); }

    /// What the stand-in has received from the Next so far.
    std::string received() const {
        std::ifstream f(root_ / "bin" / "received");
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    /// Poll for `want` to have arrived, up to two seconds of wall clock — the
    /// stand-in is a separate process, so its writes land when they land.
    bool wait_received(const std::string& want) const {
        for (int i = 0; i < 40; ++i) {
            if (received() == want) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    std::string args() const { return file("args"); }
    /// One of the files the stand-in writes in bin/ ("pid", "env", "fds", ...).
    std::string file(const char* name) const {
        std::ifstream f(root_ / "bin" / name);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    /// The stand-in's own pid, once it has started (0 if it never did).
    int child_pid() const {
        for (int i = 0; i < 100; ++i) {
            const std::string p = file("pid");
            if (!p.empty() && p.back() == '\n') return std::atoi(p.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return 0;
    }

private:
    std::filesystem::path root_;
    bool                  ok_ = false;
};

EmulatorConfig pi_qemu_config(const PiQemu& qemu) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_uart_fifo_rx      = qemu.rx_path();
    cfg.pi_uart_fifo_tx      = qemu.tx_path();
    return cfg;
}

bool process_alive(int pid) {
#ifndef _WIN32
    if (pid <= 0 || ::kill(pid, 0) != 0) return false;
#ifdef __linux__
    // A zombie is not running, but kill(pid, 0) still succeeds on it. The
    // stand-in becomes one when stop() SIGKILLs it together with its watchdog
    // parent: it is re-parented to PID 1, and a container's PID 1 (CI's
    // fedora:44 job) need not reap it. Field 3 of /proc/<pid>/stat is the
    // state; the name before it is parenthesised and may contain spaces.
    // Read with read(2), not a stream: the process may be reaped between the
    // open and the read, which then fails with ESRCH, and libstdc++'s filebuf
    // turns that failure into an exception that escapes istreambuf_iterator.
    std::string stat;
    const int fd = ::open(("/proc/" + std::to_string(pid) + "/stat").c_str(), O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        const ssize_t n = ::read(fd, buf, sizeof buf);
        ::close(fd);
        if (n > 0) stat.assign(buf, static_cast<std::size_t>(n));
    }
    const std::size_t rp = stat.rfind(')');
    if (rp != std::string::npos && rp + 2 < stat.size() && stat[rp + 2] == 'Z') return false;
#endif
    return true;
#else
    (void)pid;
    return false;
#endif
}

bool contains(const std::vector<std::string>& v, const std::string& a, const std::string& b) {
    for (std::size_t i = 0; i + 1 < v.size(); ++i)
        if (v[i] == a && v[i + 1] == b) return true;
    return false;
}

} // namespace

static void test_pi_qemu() {
    set_group("PI");

    // ── PI-06 — the QEMU command line: a Raspberry Pi (QEMU raspi0) booting the
    // directory's kernel and device tree from the overlay, with its console
    // UART on the pipe chardev whose FIFOs jnext opens, no monitor and no
    // display. Paths with commas are escaped the QEMU way (doubled).
    {
        PiQemu::Spec spec;
        spec.dir = "/n";
        const std::vector<std::string> a = PiQemu::build_args(spec, "/run/uart");
        const bool shape =
            contains(a, "-M", "raspi0")
            && contains(a, "-kernel", "/n/boot/kernel.img")
            && contains(a, "-dtb", "/n/boot/bcm2708-rpi-zero.dtb")
            && contains(a, "-drive", "file=/n/overlay.qcow2,if=sd,format=qcow2")
            && contains(a, "-chardev", "pipe,id=pi,path=/run/uart")
            && contains(a, "-serial", "chardev:pi")
            && contains(a, "-monitor", "none")
            && contains(a, "-display", "none")
            && contains(a, "-device", "usb-audio,audiodev=snd0,buffer=16384");
        const std::string wav  = PiQemu::audiodev_arg("wav:/x,y.wav");
        const std::string none = PiQemu::audiodev_arg("none");
        const std::string dflt = PiQemu::audiodev_arg("host");
#ifdef __APPLE__
        const bool dflt_ok = dflt.rfind("coreaudio,id=snd0", 0) == 0;
#else
        const bool dflt_ok = dflt == "pa,id=snd0";
#endif
        check("PI-06",
              "jnext builds a raspi0 command line booting the NextPi "
              "directory's kernel, device tree and overlay, with the console UART "
              "on the pipe chardev jnext opens; -audiodev is `host`: the platform's "
              "default output, a named driver, or wav:FILE",
              shape && wav == "wav,id=snd0,path=/x,,y.wav" && none == "none,id=snd0" && dflt_ok,
              fmt("shape=%d wav='%s' (want wav,id=snd0,path=/x,,y.wav) none='%s' "
                  "default='%s'",
                  shape ? 1 : 0, wav.c_str(), none.c_str(), dflt.c_str()));
    }

    // ── PI-07 — the refusals, each one a usage error before the machine boots
    // and none leaving a process behind: an incomplete NextPi directory, a
    // QEMU that is not installed, and a QEMU that dies at once.
    {
        FakeNextPi fake("refuse");
        std::string err_dir, err_bin, err_dies;

        PiQemu::Spec empty_dir;
        empty_dir.dir = (std::filesystem::temp_directory_path() / ("jnext-no-nextpi-" + pid_tag())).string();
        PiQemu a;
        const bool dir_refused = !a.start(empty_dir, err_dir)
                                 && err_dir.find("incomplete") != std::string::npos;

        PiQemu::Spec no_qemu;
        no_qemu.dir         = fake.dir();
        no_qemu.qemu_binary = fake.bin("no-such-qemu");
        PiQemu b;
        const bool bin_refused = !b.start(no_qemu, err_bin)
                                 && err_bin.find("not found") != std::string::npos && !b.running();

        PiQemu::Spec dies;
        dies.dir         = fake.dir();
        dies.qemu_binary = fake.bin("qemu-fails");
        PiQemu c;
        const bool dies_refused = !c.start(dies, err_dies)
                                  && err_dies.find("exited at once") != std::string::npos
                                  && !c.running();

        check("PI-07",
              "starting NextPi is refused before boot, leaving nothing running, when the "
              "NextPi directory is incomplete, when QEMU is not installed, "
              "and when QEMU exits at once",
              fake.ok() && dir_refused && bin_refused && dies_refused,
              fmt("fixture=%d dir='%s' binary='%s' dies='%s'", fake.ok() ? 1 : 0,
                  err_dir.c_str(), err_bin.c_str(), err_dies.c_str()));
    }

    // ── PI-08 — THE FEATURE, end to end with the stand-in. jnext creates the
    // overlay (via qemu-img) and the FIFOs, spawns "QEMU", and the Emulator
    // built from the resulting config hears the Pi's "SUP> " on UART 1 and is
    // heard by it, NR 0xA0 = 0x30 as `.pisend` sets it. Destroying the launcher
    // stops the process and removes the FIFOs.
    {
        FakeNextPi fake("e2e");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";

        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fake.ok() && qemu->start(spec, error);
        const bool overlay = std::filesystem::exists(std::filesystem::path(fake.dir()) / "overlay.qcow2");
        const int  pid     = qemu->pid();
        const std::filesystem::path fifo_dir = std::filesystem::path(qemu->rx_path()).parent_path();

        std::vector<uint8_t> heard;
        bool sent = false;
        if (started) {
            Emulator emu;
            emu.init(pi_qemu_config(*qemu));
            for (int f = 0; f < 4 && heard.size() < 5; ++f) {
                const std::vector<uint8_t> got = pi_frame(emu, 0x30);
                heard.insert(heard.end(), got.begin(), got.end());
            }
            pi_transmit_frame(emu, 0x30, 0x0D);
            sent = fake.wait_received("\r");
        }
        const bool args_ok = fake.args().find("pipe,id=pi,path=") != std::string::npos;

        // SIGTERM ends QEMU at once; the 3 s SIGKILL fallback is for a QEMU
        // that ignores it, and must not be what stops a well-behaved one.
        const auto t0 = std::chrono::steady_clock::now();
        qemu.reset();
        const long stop_ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const bool stopped     = !process_alive(pid) && stop_ms < 2000;
        const bool fifos_gone  = !fifo_dir.empty() && !std::filesystem::exists(fifo_dir);

        check("PI-08",
              "NextPi end to end: jnext creates the overlay and the FIFOs, "
              "starts QEMU on its pipe chardev, the guest reads the Pi's SUP> on "
              "UART 1 and the Pi receives the guest's byte; stopping the launcher "
              "ends the process with SIGTERM (not the SIGKILL fallback) and removes "
              "the FIFOs",
              started && overlay && args_ok
                  && heard == std::vector<uint8_t>({'S', 'U', 'P', '>', ' '}) && sent
                  && stopped && fifos_gone,
              fmt("started=%d (%s) overlay=%d args=%d guest heard [%s] (want 53 55 50 "
                  "3E 20) pi received=%d stopped=%d (%ld ms, want < 2000) fifos_gone=%d",
                  started ? 1 : 0, error.c_str(), overlay ? 1 : 0, args_ok ? 1 : 0,
                  bytes_hex(heard).c_str(), sent ? 1 : 0, stopped ? 1 : 0, stop_ms,
                  fifos_gone ? 1 : 0));
    }

    // ── PI-09 — A HARD RESET DOES NOT REBOOT THE PI. The launcher belongs to
    // the process, so a second Emulator built from the same config (what a
    // cold boot does) re-opens the same FIFOs and reaches the SAME Pi process,
    // which never saw the first Emulator go away.
    {
        FakeNextPi fake("coldboot");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error);
        const int  pid     = qemu.pid();

        bool first = false, second = false;
        if (started) {
            {
                Emulator emu;
                emu.init(pi_qemu_config(qemu));
                pi_transmit_frame(emu, 0x30, 'A');
                first = fake.wait_received("A");
            }
            Emulator emu;
            emu.init(pi_qemu_config(qemu));
            pi_transmit_frame(emu, 0x30, 'B');
            second = fake.wait_received("AB");
        }

        check("PI-09",
              "a rebuilt Emulator (a hard reset) reconnects to the same running Pi "
              "through the same FIFOs; the Pi received both machines' bytes in order",
              started && first && second && qemu.pid() == pid && process_alive(pid),
              fmt("started=%d (%s) first=%d second=%d received='%s' (want AB) "
                  "same_pid=%d alive=%d",
                  started ? 1 : 0, error.c_str(), first ? 1 : 0, second ? 1 : 0,
                  fake.received().c_str(), qemu.pid() == pid ? 1 : 0,
                  process_alive(pid) ? 1 : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// The NextPi directory, prepared on first use (nextpi_provisioner)
// ══════════════════════════════════════════════════════════════════════
//
// Offline: the "mirror" is a temporary directory and the download seam copies
// from it, as sdcard_provisioner's tests do. The archive is built here — a
// gzip-compressed tar in GNU format whose image entry is a tiny MBR disk with a
// hand-made FAT32 boot partition holding kernel.img and a long-named
// bcm2708-rpi-zero.dtb — so the whole real code path runs: checksum, tar
// stream, base-256 / pax sizes and names, the FAT32 reader, the install swap.

namespace {

void put16(std::vector<uint8_t>& b, std::size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v); b[at + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, std::size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

/// A 64-sector disk: MBR, one FAT32 (type 0x0C) partition at LBA 8 with one
/// sector per cluster, kernel.img (8.3) and bcm2708-rpi-zero.dtb (VFAT long
/// name) in its root directory. Small files only (one cluster each).
std::vector<uint8_t> fake_nextpi_disk(const std::string& kernel, const std::string& dtb) {
    std::vector<uint8_t> d(64 * 512, 0);
    const std::size_t part = 8 * 512;
    d[0x1BE + 4] = 0x0C;                 // FAT32 LBA
    put32(d, 0x1BE + 8, 8);              // first sector
    put32(d, 0x1BE + 12, 56);            // size
    d[510] = 0x55; d[511] = 0xAA;
    put16(d, part + 11, 512);            // bytes per sector
    d[part + 13] = 1;                    // sectors per cluster
    put16(d, part + 14, 1);              // reserved sectors
    d[part + 16] = 1;                    // FATs
    put32(d, part + 32, 56);             // total sectors
    put32(d, part + 36, 1);              // sectors per FAT
    put32(d, part + 44, 2);              // root cluster
    std::memcpy(&d[part + 82], "FAT32   ", 8);
    d[part + 510] = 0x55; d[part + 511] = 0xAA;
    const std::size_t fat = part + 512;  // LBA 9
    put32(d, fat + 0, 0x0FFFFFF8);
    put32(d, fat + 4, 0x0FFFFFFF);
    for (int cl = 2; cl <= 4; ++cl) put32(d, fat + 4 * cl, 0x0FFFFFFF);   // root, kernel, dtb
    const auto cluster = [&](int cl) { return static_cast<std::size_t>((10 + cl - 2) * 512); };

    std::size_t e = cluster(2);
    auto sfn = [&](const char* name11, int cl, std::size_t size) {
        std::memcpy(&d[e], name11, 11);
        d[e + 11] = 0x20;
        put16(d, e + 26, static_cast<uint16_t>(cl));
        put32(d, e + 28, static_cast<uint32_t>(size));
        e += 32;
    };
    sfn("KERNEL  IMG", 3, kernel.size());
    // "bcm2708-rpi-zero.dtb" = 20 chars: two LFN slots, highest first.
    const std::string lfn = "bcm2708-rpi-zero.dtb";
    for (int slot = 2; slot >= 1; --slot) {
        d[e] = static_cast<uint8_t>(slot | (slot == 2 ? 0x40 : 0));
        d[e + 11] = 0x0F;
        static const int offs[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
        for (int k = 0; k < 13; ++k) {
            const std::size_t idx = static_cast<std::size_t>((slot - 1) * 13 + k);
            const uint16_t ch = idx < lfn.size() ? static_cast<uint8_t>(lfn[idx])
                              : idx == lfn.size() ? 0x0000 : 0xFFFF;
            put16(d, e + offs[k], ch);
        }
        e += 32;
    }
    sfn("BCM270~1DTB", 4, dtb.size());
    std::memcpy(&d[cluster(3)], kernel.data(), kernel.size());
    std::memcpy(&d[cluster(4)], dtb.data(), dtb.size());
    return d;
}

/// A tar header: name, size, type. `base256` writes the size in GNU's
/// base-256 form (how a >8 GB image, like NextPi's, is recorded).
std::string tar_header(const std::string& name, uint64_t size, char type, bool base256 = false) {
    std::string h(512, '\0');
    std::memcpy(&h[0], name.data(), std::min<std::size_t>(name.size(), 100));
    std::snprintf(&h[100], 8, "%07o", 0644);
    if (base256) {
        h[124] = static_cast<char>(0x80);
        for (int i = 0; i < 8; ++i) h[135 - i] = static_cast<char>(size >> (8 * i));
    } else {
        std::snprintf(&h[124], 12, "%011llo", static_cast<unsigned long long>(size));
    }
    h[156] = type;
    std::memcpy(&h[257], "ustar  ", 8);                      // GNU magic, like NextPi's
    std::memset(&h[148], ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(&h[148], 8, "%06o", sum);
    return h;
}
std::string tar_entry(const std::string& name, const std::string& data, char type = '0',
                      bool base256 = false) {
    std::string out = tar_header(name, data.size(), type, base256) + data;
    out.resize((out.size() + 511) / 512 * 512, '\0');
    return out;
}
std::string pax_record(const std::string& key, const std::string& value) {
    const std::string body = " " + key + "=" + value + "\n";
    std::size_t len = body.size() + 1;
    while (std::to_string(len).size() + body.size() != len) ++len;
    return std::to_string(len) + body;
}

/// NextPi-<release>.tar.gz as the mirror serves it: a directory, an md5 file,
/// a pax-named and base-256-sized image, written with zlib.
bool write_fake_release(const std::string& path, const std::string& release,
                        const std::vector<uint8_t>& disk) {
    const std::string top = "NextPi-" + release + "/";
    std::string tar = tar_entry(top, "", '5');
    tar += tar_entry(top + "NextPi2-test.img.md5", "0123 x\n");
    tar += tar_entry(top + "PaxHeaders/img", pax_record("path", top + "NextPi2-test.img"), 'x');
    tar += tar_entry("ignored-short-name", std::string(disk.begin(), disk.end()), '0', true);
    tar += std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}

/// A mirror directory plus a download seam that serves from it (and the
/// listing page for "<mirror>/"), counting what it was asked for.
struct FakeMirror {
    std::filesystem::path dir;
    std::string           listing;
    std::vector<std::string> fetched;
    bool                  offline = false;

    explicit FakeMirror(const std::string& tag) {
        dir = std::filesystem::temp_directory_path() / ("jnext-nextpi-mirror-" + tag + "-" + pid_tag());
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }
    ~FakeMirror() { std::error_code ec; std::filesystem::remove_all(dir, ec); }

    std::string url() const { return "https://mirror.test/NextPi2"; }

    bool add_release(const std::string& release, const std::vector<uint8_t>& disk) {
        const std::string archive = (dir / ("NextPi-" + release + ".tar.gz")).string();
        if (!write_fake_release(archive, release, disk)) return false;
        std::ofstream(archive + ".md5") << "MD5 (NextPi-" << release << ".tar.gz) = "
                                        << nextpi::md5_file(archive) << "\n";
        listing += "<a href=\"NextPi-" + release + ".tar.gz\">NextPi-" + release + ".tar.gz</a>\n"
                   "<a href=\"NextPi-" + release + ".tar.gz.md5\">md5</a>\n";
        return true;
    }

    sdcard::DownloadFn download() {
        return [this](const std::string& u, const std::string& dest, const sdcard::ProgressFn& progress,
                      std::string& err) {
            fetched.push_back(u);
            if (offline) { err = "Could not resolve hostname"; return false; }
            if (u == url() + "/") { std::ofstream(dest) << listing; return true; }
            const std::string name = u.substr(u.find_last_of('/') + 1);
            std::error_code ec;
            std::filesystem::copy_file(dir / name, dest,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { err = "HTTP status 404"; return false; }
            if (progress) progress(1, 1);
            return true;
        };
    }
};

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

nextpi::ProvisionOptions nextpi_options(FakeMirror& mirror, const std::string& dir,
                                        const std::string& release, int& confirms, bool answer = true) {
    nextpi::ProvisionOptions o;
    o.dir          = dir;
    o.release      = release;
    o.mirror       = mirror.url();
    o.download     = mirror.download();
    o.confirm      = [&confirms, answer](const std::string&) { ++confirms; return answer; };
    o.space_needed = 0;
    return o;
}

} // namespace

static void test_nextpi_provisioner() {
    set_group("PI");
    namespace fs = std::filesystem;
    const std::vector<uint8_t> disk_a = fake_nextpi_disk("kernel image a", "device tree a");
    const std::vector<uint8_t> disk_b = fake_nextpi_disk("kernel image b", "device tree b");

    // ── PI-10 — releases: the mirror listing yields each NextPi-<name>.tar.gz
    // once (not its .md5), and names compare as `sort -V` does, so "latest"
    // picks 1_100 over 1_93D and 1_93D over 1_93C.
    {
        const std::vector<std::string> names = nextpi::parse_release_listing(
            "<a href=\"NextPi-1_93C.tar.gz\">x</a> <a href=\"NextPi-1_93C.tar.gz.md5\">m</a>"
            "<a href=\"NextPi-1_93D.tar.gz\">NextPi-1_93D.tar.gz</a> <a href=\"NextPi-1_100.tar.gz\">");
        const bool listing = names == std::vector<std::string>({"1_93C", "1_93D", "1_100"});
        const bool order = nextpi::compare_release("1_100", "1_93D") > 0
                        && nextpi::compare_release("1_93D", "1_93C") > 0
                        && nextpi::compare_release("1_93D", "1_93D") == 0
                        && nextpi::compare_release("1_9", "1_10") < 0;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-10",
              "NextPi's release list is read from the mirror's NextPi-<name>.tar.gz "
              "links, once each, and release names order numerically (1_100 after 1_93D)",
              listing && order, fmt("listed [%s] order=%d", joined.c_str(), order ? 1 : 0));
    }

    // ── PI-11 — the tar stream: the image entry is found by suffix through a pax
    // path record, its size read from a GNU base-256 field, and written out
    // byte for byte; an archive without such an entry is an error.
    {
        FakeMirror mirror("tar");
        const fs::path archive = mirror.dir / "a.tar.gz";
        const fs::path out = mirror.dir / "out.img";
        std::string err, err_missing;
        const bool wrote = write_fake_release(archive.string(), "T", disk_a);
        const bool ok = wrote && nextpi::extract_tar_gz_entry(archive.string(), ".img", out.string(), {}, err);
        const bool same = ok && slurp(out) == std::string(disk_a.begin(), disk_a.end());
        const bool missing = !nextpi::extract_tar_gz_entry(archive.string(), ".iso", out.string(), {}, err_missing)
                           && err_missing.find(".iso") != std::string::npos;
        check("PI-11",
              "the NextPi archive reader finds the image through a pax path record, "
              "reads its GNU base-256 size, and writes it byte for byte; a missing "
              "entry is an error",
              ok && same && missing,
              fmt("ok=%d (%s) same=%d missing='%s'", ok ? 1 : 0, err.c_str(), same ? 1 : 0,
                  err_missing.c_str()));
    }

    // ── PI-12 — FIRST USE: asked once, the checksum and archive fetched, the
    // image and both boot files installed, the release recorded, the archive
    // deleted. The next run finds it ready: no question, no network.
    {
        FakeMirror mirror("first");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool installed = r1.status == sdcard::ProvisionStatus::Ok && r1.release == "1_93D"
            && slurp(dir / "nextpi.img") == std::string(disk_a.begin(), disk_a.end())
            && slurp(dir / "boot" / "kernel.img") == "kernel image a"
            && slurp(dir / "boot" / "bcm2708-rpi-zero.dtb") == "device tree a"
            && nextpi::prepared_release(dir.string()) == "1_93D"
            && !fs::exists(dir / "NextPi-1_93D.tar.gz");
        const std::size_t fetched_first = mirror.fetched.size();
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool second_quiet = r2.status == sdcard::ProvisionStatus::Ok && confirms == 1
                                && mirror.fetched.size() == fetched_first;
        check("PI-12",
              "first use of NextPi asks once, downloads the default release with "
              "its MD5, installs the image and the two boot files from its FAT32 "
              "partition and deletes the archive; the next run asks nothing and "
              "fetches nothing",
              fixture && installed && fetched_first == 2 && second_quiet,
              fmt("fixture=%d installed=%d (%s) fetched=%zu (want 2) second_quiet=%d confirms=%d",
                  fixture ? 1 : 0, installed ? 1 : 0, r1.error.c_str(), fetched_first,
                  second_quiet ? 1 : 0, confirms));
    }

    // ── PI-13 — the refusals: declining downloads nothing and installs nothing;
    // an archive whose MD5 does not match is deleted and nothing is installed.
    {
        FakeMirror mirror("refuse");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir_no = mirror.dir / "declined";
        int confirms = 0;
        const nextpi::ProvisionResult declined =
            nextpi::provision(nextpi_options(mirror, dir_no.string(), "", confirms, false));
        const bool declined_ok = declined.status == sdcard::ProvisionStatus::Declined
                               && mirror.fetched.empty() && nextpi::prepared_release(dir_no.string()).empty();

        std::ofstream(mirror.dir / "NextPi-1_93D.tar.gz.md5") << "00000000000000000000000000000000\n";
        const fs::path dir_bad = mirror.dir / "corrupt";
        const nextpi::ProvisionResult corrupt =
            nextpi::provision(nextpi_options(mirror, dir_bad.string(), "", confirms));
        const bool corrupt_ok = corrupt.status == sdcard::ProvisionStatus::Failed
                              && corrupt.error.find("MD5") != std::string::npos
                              && !fs::exists(dir_bad / "NextPi-1_93D.tar.gz")
                              && nextpi::prepared_release(dir_bad.string()).empty();
        check("PI-13",
              "declining the NextPi download fetches and installs nothing; a download "
              "whose MD5 does not match is deleted and nothing is installed",
              fixture && declined_ok && corrupt_ok,
              fmt("fixture=%d declined=%d corrupt=%d ('%s')", fixture ? 1 : 0,
                  declined_ok ? 1 : 0, corrupt_ok ? 1 : 0, corrupt.error.c_str()));
    }

    // ── PI-14 — CHANGING RELEASE, and "latest". A directory holding another
    // release is replaced after asking, and its overlay (made over the old
    // image) discarded. "latest" picks the newest listed release; with the
    // mirror unreachable it keeps the installed one, with a warning, asking
    // nothing.
    {
        FakeMirror mirror("change");
        const bool fixture = mirror.add_release("1_93D", disk_a) && mirror.add_release("1_100", disk_b);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "1_93D", confirms));
        std::ofstream(dir / "overlay.qcow2") << "old overlay";
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool upgraded = r1.status == sdcard::ProvisionStatus::Ok
            && r2.status == sdcard::ProvisionStatus::Ok && r2.release == "1_100" && confirms == 2
            && slurp(dir / "boot" / "kernel.img") == "kernel image b" && !fs::exists(dir / "overlay.qcow2");
        mirror.offline = true;
        const nextpi::ProvisionResult r3 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool offline_kept = r3.status == sdcard::ProvisionStatus::Ok && r3.release == "1_100"
                                && !r3.warning.empty() && confirms == 2;
        check("PI-14",
              "the NextPi provisioner replaces a directory holding another release after asking, "
              "discarding its overlay; \"latest\" installs the newest listed release, and "
              "with the mirror unreachable keeps the installed one with a warning",
              fixture && upgraded && offline_kept,
              fmt("fixture=%d upgraded=%d (%s / %s, confirms=%d) offline_kept=%d ('%s')",
                  fixture ? 1 : 0, upgraded ? 1 : 0, r1.error.c_str(), r2.release.c_str(),
                  confirms, offline_kept ? 1 : 0, r3.warning.c_str()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// NextPi — behaviours the first review found untested (PR #310)
// ══════════════════════════════════════════════════════════════════════

namespace {

/// Wait up to `ms` for `pid` to be gone (reaped or never ours: kill fails).
bool gone_within(int pid, int ms) {
    for (int waited = 0; waited <= ms; waited += 20) {
        if (!process_alive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

/// A tar.gz of `entries` (already-built tar blocks) plus the end marker.
bool write_tar_gz(const std::string& path, const std::string& tar_body) {
    const std::string tar = tar_body + std::string(1024, '\0');
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz) return false;
    const bool ok = gzwrite(gz, tar.data(), static_cast<unsigned>(tar.size())) == static_cast<int>(tar.size());
    return gzclose(gz) == Z_OK && ok;
}

} // namespace

static void test_nextpi_review_rows() {
    set_group("PI");
    namespace fs = std::filesystem;

    // ── PI-15 — THE REPLAY GATE, the JOY-20 posture for the Pi. While a rewind
    // or an RZX playback re-executes frames, Emulator::service_pi_uart_frame
    // holds the link inert: a replayed transmission does not reach the Pi a
    // second time, and the Pi's bytes are not read (they stay in the pipe for
    // the timeline that resumes). Afterwards both flow again.
    {
        TempFifoCable cable("pi-replay");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        pi_transmit_frame(emu, 0x30, 0xD1);
        const std::vector<uint8_t> live_before = cable.drain();

        emu.set_replay_mode(true);
        cable.send({0xE1, 0xE2});
        pi_transmit_frame(emu, 0x30, 0xD2);
        const std::vector<uint8_t> during = cable.drain();
        const PiUartDevice* pi = emu.pi_uart();
        const bool not_read = pi && pi->link().received() == 0;

        emu.set_replay_mode(false);
        std::vector<uint8_t> heard;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xD3);
        const std::vector<uint8_t> live_after = cable.drain();
        check("PI-15",
              "during a rewind/RZX replay the NextPi link is inert: the guest's replayed byte "
              "does not reach the Pi and the Pi's bytes are not consumed; afterwards they "
              "arrive and the guest is heard again",
              attached && live_before == std::vector<uint8_t>({0xD1}) && during.empty() && not_read &&
                  heard == std::vector<uint8_t>({0xE1, 0xE2}) && live_after == std::vector<uint8_t>({0xD3}),
              fmt("before=[%s] during=[%s] (want empty) not_read=%d heard=[%s] (want E1 E2) after=[%s]",
                  bytes_hex(live_before).c_str(), bytes_hex(during).c_str(), not_read ? 1 : 0,
                  bytes_hex(heard).c_str(), bytes_hex(live_after).c_str()));
    }

    // ── PI-16 — the warm-start recording machine (Emulator::warm_start_boot_config)
    // must not open the NextPi FIFOs: a second reader would steal the Pi's bytes.
    {
        EmulatorConfig live;
        live.type            = MachineType::ZXN_ISSUE2;
        live.sd_card_image   = "card.img";
        live.pi_uart_fifo_rx = "/tmp/x/uart.out";
        live.pi_uart_fifo_tx = "/tmp/x/uart.in";
        const EmulatorConfig b = Emulator::warm_start_boot_config(live);
        check("PI-16",
              "the warm-start recording boot gets no NextPi link (both FIFO paths cleared); the "
              "machine itself is kept",
              b.pi_uart_fifo_rx.empty() && b.pi_uart_fifo_tx.empty() && b.sd_card_image == "card.img",
              fmt("rx='%s' tx='%s'", b.pi_uart_fifo_rx.c_str(), b.pi_uart_fifo_tx.c_str()));
    }

    // ── PI-17 — SIGKILL ESCALATION: a QEMU that ignores SIGTERM is killed —
    // with its watchdog — once the grace period is over, and stop() returns.
    {
        FakeNextPi fake("stubborn");
        PiQemu::Spec spec;
        spec.dir           = fake.dir();
        spec.qemu_binary   = fake.bin("qemu-stubborn");
        spec.audio         = "none";
        spec.stop_grace_ms = 300;
        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fake.ok() && qemu->start(spec, error);
        const int child = fake.child_pid();
        const auto t0 = std::chrono::steady_clock::now();
        qemu.reset();
        const long ms = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count());
        const bool killed = child > 0 && gone_within(child, 1000);
        check("PI-17",
              "a QEMU that ignores SIGTERM is SIGKILLed with its watchdog once the stop grace "
              "period is over, and stop() returns",
              started && killed && ms >= 300 && ms < 3000,
              fmt("started=%d (%s) child=%d killed=%d stop took %ld ms (want 300..3000)",
                  started ? 1 : 0, error.c_str(), child, killed ? 1 : 0, ms));
    }

    // ── PI-18 / PI-19 — THE CHILD. Its locale is C whatever jnext's is (set
    // in the child only), and it inherits none of jnext's descriptors: a file
    // jnext holds open at fd 57 without close-on-exec is not open in QEMU.
    {
        FakeNextPi fake("child");
        const char* old_lang = std::getenv("LANG");
        const char* old_lc   = std::getenv("LC_ALL");
        const std::string saved_lang = old_lang ? old_lang : "";
        const std::string saved_lc   = old_lc ? old_lc : "";
        ::setenv("LANG", "es_ES.UTF-8", 1);
        ::setenv("LC_ALL", "es_ES.UTF-8", 1);
        const int leak = ::open(fake.dir().c_str(), O_RDONLY);
        const bool leak_ok = leak >= 0 && ::dup2(leak, 57) == 57;
        if (leak >= 0 && leak != 57) ::close(leak);

        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error) && fake.child_pid() > 0;
        const bool jnext_locale_kept = std::string(std::getenv("LANG")) == "es_ES.UTF-8";
        if (old_lang) ::setenv("LANG", saved_lang.c_str(), 1); else ::unsetenv("LANG");
        if (old_lc) ::setenv("LC_ALL", saved_lc.c_str(), 1); else ::unsetenv("LC_ALL");
        if (leak_ok) ::close(57);

        // Exactly LANG and LC_ALL are the promise, each present once and C.
        // Other variables may legitimately carry jnext's locale (a GNOME
        // session exports GDM_LANG=es_ES.UTF-8), so they are not searched.
        const std::string env = "\n" + fake.file("env");
        auto count = [&env](const char* line) {
            std::size_t n = 0;
            for (std::size_t at = env.find(line); at != std::string::npos; at = env.find(line, at + 1)) ++n;
            return n;
        };
        const bool env_c = count("\nLANG=") == 1 && count("\nLANG=C\n") == 1 &&
                           count("\nLC_ALL=") == 1 && count("\nLC_ALL=C\n") == 1;
        check("PI-18",
              "QEMU runs with LANG=C and LC_ALL=C set in its own environment, whatever jnext's "
              "locale is, and jnext's own environment is left alone",
              started && env_c && jnext_locale_kept,
              fmt("started=%d (%s) env_c=%d jnext_kept=%d", started ? 1 : 0, error.c_str(),
                  env_c ? 1 : 0, jnext_locale_kept ? 1 : 0));

        // fd 3 is the watchdog's end of its pipe, which the watchdog closes
        // for QEMU (`3<&-`): QEMU gets none of jnext's descriptors, that pipe
        // included (R3-3).
        const std::string fds = "\n" + fake.file("fds");
        const bool listed = !fake.file("fds").empty();
        const std::string fd3 = fake.file("fd3");
        check("PI-19",
              "QEMU inherits none of jnext's descriptors: a file jnext holds at fd 57 without "
              "close-on-exec is not open in the child, nor is the watchdog's pipe at fd 3",
              started && leak_ok && listed && fds.find("\n57\n") == std::string::npos && fd3 == "closed\n",
              fmt("started=%d leak_ok=%d listed=%d fd 3 %s child fds:%s", started ? 1 : 0, leak_ok ? 1 : 0,
                  listed ? 1 : 0, fd3.c_str(), fake.file("fds").c_str()));
    }

    // ── PI-20 — A KILLED jnext DOES NOT ORPHAN QEMU. A process holding a
    // running PiQemu is SIGKILLed (no destructor runs); its watchdog sees the
    // pipe close and stops QEMU, which would otherwise keep the overlay locked.
    {
        FakeNextPi fake("orphan");
        int child_qemu = 0;
        bool held = false;
#ifndef _WIN32
        const pid_t holder = ::fork();
        if (holder == 0) {
            PiQemu::Spec spec;
            spec.dir         = fake.dir();
            spec.qemu_binary = fake.bin("qemu-system-arm");
            spec.audio       = "none";
            auto* q = new PiQemu();                 // never destroyed: SIGKILLed first
            std::string e;
            if (!q->start(spec, e)) ::_exit(2);
            for (;;) ::pause();
        }
        if (holder > 0) {
            child_qemu = fake.ok() ? fake.child_pid() : 0;
            held = child_qemu > 0 && process_alive(child_qemu);
            ::kill(holder, SIGKILL);
            int st = 0;
            ::waitpid(holder, &st, 0);
        }
#endif
        const bool stopped = child_qemu > 0 && gone_within(child_qemu, 3000);
        check("PI-20",
              "when the process running NextPi is SIGKILLed, the watchdog stops QEMU instead of "
              "leaving it orphaned with the overlay locked",
              held && stopped,
              fmt("qemu pid=%d running while held=%d stopped after the kill=%d", child_qemu,
                  held ? 1 : 0, stopped ? 1 : 0));
    }

    // ── PI-21 — the free-space check: with less free space than unpacking needs,
    // the download is refused before anything is fetched.
    {
        FakeMirror mirror("space");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, (mirror.dir / "np").string(), "", confirms);
        o.space_needed = std::numeric_limits<uint64_t>::max();
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PI-21",
              "NextPi is not downloaded when the directory lacks the free space unpacking needs: "
              "refused with the amounts, nothing fetched",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("free") != std::string::npos && mirror.fetched.empty(),
              fmt("status=%d error='%s' fetched=%zu", static_cast<int>(r.status), r.error.c_str(),
                  mirror.fetched.size()));
    }

    // ── PI-22 — long names: a GNU 'L' record, and a POSIX ustar prefix, each
    // supply the directory part a suffix match needs (neither short name has it).
    {
        FakeMirror mirror("names");
        const std::string data = "image bytes";
        std::string gnu = tar_entry("././@LongLink", "NextPi-T/very/deep/name.img", 'L');
        gnu += tar_entry("short-name", data);
        std::string ustar_hdr = tar_header("x.img", data.size(), '0');
        std::memcpy(&ustar_hdr[257], "ustar\0" "00", 8);           // POSIX, not GNU
        std::memcpy(&ustar_hdr[345], "NextPi-T/dir", 12);         // the prefix field
        std::memset(&ustar_hdr[148], ' ', 8);
        unsigned sum = 0;
        for (unsigned char c : ustar_hdr) sum += c;
        std::snprintf(&ustar_hdr[148], 8, "%06o", sum);
        std::string ustar = ustar_hdr + data;
        ustar.resize((ustar.size() + 511) / 512 * 512, '\0');
        const fs::path a_gnu = mirror.dir / "gnu.tar.gz", a_ustar = mirror.dir / "ustar.tar.gz";
        const fs::path out = mirror.dir / "out";
        std::string e1, e2;
        const bool ok_gnu = write_tar_gz(a_gnu.string(), gnu) &&
            nextpi::extract_tar_gz_entry(a_gnu.string(), "deep/name.img", out.string(), {}, e1) &&
            slurp(out) == data;
        const bool ok_ustar = write_tar_gz(a_ustar.string(), ustar) &&
            nextpi::extract_tar_gz_entry(a_ustar.string(), "dir/x.img", out.string(), {}, e2) &&
            slurp(out) == data;
        check("PI-22",
              "the NextPi archive reader takes an entry's full name from a GNU long-name record "
              "and from a POSIX ustar prefix",
              ok_gnu && ok_ustar, fmt("gnu=%d (%s) ustar=%d (%s)", ok_gnu ? 1 : 0, e1.c_str(),
                                      ok_ustar ? 1 : 0, e2.c_str()));
    }

    // ── PI-23 — A MALFORMED ARCHIVE IS AN ERROR, NOT A CRASH. Each bound has
    // its own case, and each case must fail with THAT bound's message: a
    // long-name record claiming ~2^64 bytes and an entry of 2^40+1 hit the
    // entry-size bound, an `x` header of 2^40 the metadata bound, and a pax
    // `size=` record of 2^40+1 the pax size bound. No exception escapes. (The
    // fixture's tag must not contain any of the words searched for: it is in
    // every archive path, so in every message.)
    {
        FakeMirror mirror("bounds");
        const fs::path out = mirror.dir / "out";
        auto try_one = [&](const char* name, const std::string& body, const char* want,
                           std::string& why) {
            const fs::path a = mirror.dir / name;
            if (!write_tar_gz(a.string(), body)) { why = "fixture"; return false; }
            try {
                return !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find(want) != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        std::string huge_l = tar_header("././@LongLink", 0, 'L');
        huge_l[124] = static_cast<char>(0x80);
        for (int i = 125; i < 136; ++i) huge_l[i] = static_cast<char>(0xFF);
        huge_l[135] = static_cast<char>(0x00);
        std::string big_x = tar_header("PaxHeaders/x", 1ull << 40, 'x', true);
        std::string big_file = tar_header("big.img", (1ull << 40) + 1, '0', true);
        std::string pax_size = tar_entry("PaxHeaders/big.img", pax_record("size", "1099511627777"), 'x') +
                               tar_entry("big.img", "");
        std::string w1, w2, w3, w4;
        const bool l_ok = try_one("l.tar.gz", huge_l, "an entry claims", w1);
        const bool x_ok = try_one("x.tar.gz", big_x, "name/metadata record", w2);
        const bool f_ok = try_one("f.tar.gz", big_file, "an entry claims", w3);
        const bool p_ok = try_one("p.tar.gz", pax_size, "a pax size of", w4);
        check("PI-23",
              "a NextPi archive with an absurd long-name, pax header, entry or pax size= record "
              "fails with that bound's \"malformed\" message instead of throwing (no crash after a "
              "6 GB download)",
              l_ok && x_ok && f_ok && p_ok,
              fmt("L: %d (%s) x: %d (%s) entry: %d (%s) pax size: %d (%s)", l_ok ? 1 : 0, w1.c_str(),
                  x_ok ? 1 : 0, w2.c_str(), f_ok ? 1 : 0, w3.c_str(), p_ok ? 1 : 0, w4.c_str()));
    }

    // ── PI-24 — a FAILED UPGRADE KEEPS THE OLD RELEASE. 1_93D is installed;
    // "latest" finds 1_100 whose archive (with a valid MD5) has no image: the
    // upgrade fails, and 1_93D is still prepared, marker and files intact.
    {
        FakeMirror mirror("keep");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("kernel old", "dtb old"));
        const fs::path broken = mirror.dir / "NextPi-1_100.tar.gz";
        const bool broken_ok = write_tar_gz(broken.string(), tar_entry("NextPi-1_100/readme.txt", "no image"));
        std::ofstream(broken.string() + ".md5") << nextpi::md5_file(broken.string()) << "\n";
        mirror.listing += "<a href=\"NextPi-1_100.tar.gz\">x</a>\n";
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool kept = r1.status == sdcard::ProvisionStatus::Ok && r2.status == sdcard::ProvisionStatus::Failed &&
                          nextpi::prepared_release(dir.string()) == "1_93D" &&
                          slurp(dir / "boot" / "kernel.img") == "kernel old" &&
                          !fs::exists(dir / "nextpi.img.part") && !fs::exists(dir / "boot.part");
        check("PI-24",
              "a NextPi upgrade that fails while unpacking leaves the installed release prepared "
              "and intact, with no partial files",
              fixture && broken_ok && kept,
              fmt("fixture=%d r1=%d r2=%d ('%s') prepared='%s'", fixture && broken_ok ? 1 : 0,
                  static_cast<int>(r1.status), static_cast<int>(r2.status), r2.error.c_str(),
                  nextpi::prepared_release(dir.string()).c_str()));
    }

    // ── PI-25 — the release name from Preferences goes into a file name and a
    // URL, so anything but a mirror-style name is refused before either.
    {
        FakeMirror mirror("names-check");
        int confirms = 0;
        bool all_refused = true;
        std::string detail;
        for (const char* bad : {"../escape", "a/b", "a b", ".hidden", "x?y=1"}) {
            const nextpi::ProvisionResult r =
                nextpi::provision(nextpi_options(mirror, (mirror.dir / "np").string(), bad, confirms));
            if (r.status != sdcard::ProvisionStatus::Failed || r.error.find("release name") == std::string::npos)
                all_refused = false;
            detail += std::string(bad) + "->" + std::to_string(static_cast<int>(r.status)) + " ";
        }
        const bool good = nextpi::valid_release_name("1_93D") && nextpi::valid_release_name("2.0-beta");
        check("PI-25",
              "a NextPi release name with a path separator, space, leading dot or URL syntax is "
              "refused before anything is asked or fetched; real names pass",
              all_refused && good && confirms == 0 && mirror.fetched.empty(),
              fmt("%sconfirms=%d fetched=%zu good=%d", detail.c_str(), confirms, mirror.fetched.size(),
                  good ? 1 : 0));
    }

    // ── PI-26 — a FAILED START CLEANS UP what it made: the overlay it created
    // is removed (it would otherwise be reused, half-made), qemu.log is kept
    // because the error points at it.
    {
        FakeNextPi fake("cleanup");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-fails");
        PiQemu q;
        std::string error;
        const bool refused = fake.ok() && !q.start(spec, error);
        const bool overlay_gone = !fs::exists(fs::path(fake.dir()) / "overlay.qcow2");
        const bool log_kept = fs::exists(fs::path(fake.dir()) / "qemu.log") &&
                              error.find("qemu.log") != std::string::npos;
        check("PI-26",
              "a NextPi start that fails removes the overlay it created and keeps qemu.log, "
              "which its error names",
              refused && overlay_gone && log_kept,
              fmt("refused=%d (%s) overlay_gone=%d log_kept=%d", refused ? 1 : 0, error.c_str(),
                  overlay_gone ? 1 : 0, log_kept ? 1 : 0));
    }

    // ── PI-27 — THE START POLICY (main.cpp, through nextpi::start_outcome):
    // running is running; declining starts jnext without NextPi either way; a
    // failure is an error only when --nextpi asked for it, and from Preferences
    // a warning that lets jnext start.
    {
        using nextpi::StartOutcome;
        const auto Ok = sdcard::ProvisionStatus::Ok, No = sdcard::ProvisionStatus::Declined,
                   Bad = sdcard::ProvisionStatus::Failed;
        const bool table =
            nextpi::start_outcome(true, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(false, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(true, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(false, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(true, Bad, false) == StartOutcome::Exit &&
            nextpi::start_outcome(true, Ok, false) == StartOutcome::Exit &&
            nextpi::start_outcome(false, Bad, false) == StartOutcome::WarnAndContinue &&
            nextpi::start_outcome(false, Ok, false) == StartOutcome::WarnAndContinue;
        check("PI-27",
              "NextPi's start policy: a failure exits only when --nextpi asked for it (from "
              "Preferences it warns and continues); declining continues either way",
              table, "");
    }
}

// ══════════════════════════════════════════════════════════════════════
// NextPi — behaviours the second review found untested (PR #310, R2)
// ══════════════════════════════════════════════════════════════════════

namespace {

/// The descriptors this process has open below 1024.
std::set<int> open_fds() {
    std::set<int> fds;
#ifndef _WIN32
    for (int fd = 0; fd < 1024; ++fd)
        if (::fcntl(fd, F_GETFD) >= 0) fds.insert(fd);
#endif
    return fds;
}

} // namespace

static void test_nextpi_review2_rows() {
    set_group("PI");
    namespace fs = std::filesystem;

    // ── PI-28 — THE INSTALL IS CRASH-SAFE (R2-3). The `release` marker is what
    // makes a directory "prepared", so it is removed BEFORE anything is
    // replaced and written LAST, through release.part and a rename. Two
    // installs are made to fail part-way, with obstacles that stop root too:
    //   a. the image cannot be put in place (a directory holds its name): the
    //      old marker must already be gone;
    //   b. the marker cannot be written (a directory holds release.part): the
    //      new release is in place, but the directory must not claim it.
    // Either way the next start re-provisions instead of booting a mixture.
    {
        FakeMirror mirror("swap");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;

        const fs::path dir_a = mirror.dir / "a";
        std::error_code ec;
        fs::create_directories(dir_a / "nextpi.img" / "in-the-way", ec);
        std::ofstream(dir_a / "release") << "1_92\n";
        const nextpi::ProvisionResult ra = nextpi::provision(nextpi_options(mirror, dir_a.string(), "", confirms));
        const bool a_ok = ra.status == sdcard::ProvisionStatus::Failed &&
                          ra.error.find("cannot install") != std::string::npos &&
                          !fs::exists(dir_a / "release");

        const fs::path dir_b = mirror.dir / "b";
        fs::create_directories(dir_b / "release.part" / "in-the-way", ec);
        const nextpi::ProvisionResult rb = nextpi::provision(nextpi_options(mirror, dir_b.string(), "", confirms));
        const bool b_ok = rb.status == sdcard::ProvisionStatus::Failed &&
                          fs::is_regular_file(dir_b / "nextpi.img") && !fs::exists(dir_b / "release") &&
                          nextpi::prepared_release(dir_b.string()).empty();
        check("PI-28",
              "a NextPi install that fails part-way leaves no release marker: it is removed before "
              "the image is replaced and written last through release.part, so the directory is "
              "never taken as prepared",
              fixture && a_ok && b_ok,
              fmt("fixture=%d a=%d ('%s', marker %s) b=%d ('%s', marker %s)", fixture ? 1 : 0,
                  a_ok ? 1 : 0, ra.error.c_str(), fs::exists(dir_a / "release") ? "kept" : "gone",
                  b_ok ? 1 : 0, rb.error.c_str(), fs::exists(dir_b / "release") ? "written" : "absent"));
    }

    // ── PI-29 — NAMES FROM THE MIRROR ARE CHECKED (R2-4). For "latest" the
    // release name comes from the mirror's listing, not from the user, and the
    // listing is the only place it is checked: a leading dot or dash, or more
    // than 64 characters, is not a release. The 65-digit name would win
    // "latest" numerically, so the provisioner must never fetch it.
    {
        const std::string long_name(65, '9');
        const std::string html = "<a href=\"NextPi-.hidden.tar.gz\">x</a> <a href=\"NextPi--rf.tar.gz\">x</a> "
                                 "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a> "
                                 "<a href=\"NextPi-1_93D.tar.gz\">x</a>";
        const std::vector<std::string> names = nextpi::parse_release_listing(html);
        const bool listed_ok = names == std::vector<std::string>({"1_93D"});

        FakeMirror mirror("listing");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        mirror.listing += "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a>\n";
        int confirms = 0;
        const fs::path dir = mirror.dir / "np";
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        bool fetched_bad = false;
        for (const std::string& u : mirror.fetched)
            if (u.find(long_name) != std::string::npos) fetched_bad = true;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-29",
              "release names from the mirror's listing are checked like typed ones: a leading dot "
              "or dash or a 65-character name is not listed, and \"latest\" never fetches one",
              fixture && listed_ok && r.status == sdcard::ProvisionStatus::Ok && r.release == "1_93D" &&
                  !fetched_bad,
              fmt("listed [%s] (want 1_93D) latest=%s status=%d fetched_bad=%d", joined.c_str(),
                  r.release.c_str(), static_cast<int>(r.status), fetched_bad ? 1 : 0));
    }

    // ── PI-30 — NO TEMPORARY DIRECTORY (R2-6). With $TMPDIR naming no
    // directory, start() fails with an error that says so — it used to throw
    // std::filesystem_error out of jnext (SIGABRT) — and leaves nothing behind.
    {
        FakeNextPi fake("tmpdir");
        const char* old = std::getenv("TMPDIR");
        const std::string saved = old ? old : "";
        ::setenv("TMPDIR", "/nonexistent-jnext-tmpdir", 1);
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        bool refused = false, threw = false;
        std::string error;
        {
            PiQemu q;
            try {
                refused = !q.start(spec, error) && !q.running();
            } catch (const std::exception& ex) {
                threw = true;
                error = ex.what();
            }
        }
        if (old) ::setenv("TMPDIR", saved.c_str(), 1); else ::unsetenv("TMPDIR");
        check("PI-30",
              "with $TMPDIR naming no directory, starting NextPi fails with an error naming the "
              "temporary directory instead of throwing, and nothing is left running",
              fake.ok() && refused && !threw && error.find("temporary directory") != std::string::npos &&
                  !fs::exists(fs::path(fake.dir()) / "overlay.qcow2"),
              fmt("refused=%d threw=%d error='%s'", refused ? 1 : 0, threw ? 1 : 0, error.c_str()));
    }

    // ── PI-31 — A PAX RECORD CANNOT REACH OUTSIDE ITS HEADER (R2-8). Its length
    // is checked against what is left; 2^64-1 used to wrap `pos + len` and be
    // taken. Such a header is malformed, whatever the records after it say.
    {
        FakeMirror mirror("paxlen");
        const fs::path a = mirror.dir / "wrap.tar.gz", out = mirror.dir / "out";
        const std::string body = tar_entry("PaxHeaders/x", "18446744073709551615 path=EVIL.img\n", 'x') +
                                 tar_entry("data.bin", "payload");
        std::string why;
        bool refused = false;
        try {
            refused = write_tar_gz(a.string(), body) &&
                      !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                      why.find("pax record overruns") != std::string::npos;
        } catch (const std::exception& ex) {
            why = std::string("threw ") + ex.what();
        }
        check("PI-31",
              "a pax record whose length runs past its header (18446744073709551615) makes the "
              "archive malformed instead of wrapping and renaming the next entry",
              refused, fmt("error='%s'", why.c_str()));
    }

    // ── PI-39..42 — EACH WELL-FORMEDNESS CHECK OF THE PAX PARSER (R3-2). A pax
    // record is "<len> <key>=<value>\n", <len> counting the whole record. Each
    // archive below breaks exactly one rule, after a valid first record, and
    // must be refused as malformed; with that one check gone it would be read.
    {
        FakeMirror mirror("paxshape");
        const fs::path out = mirror.dir / "out";
        // `pad_newline_at` puts a '\n' at that offset of the header's data
        // block, in the zero padding past its end.
        auto refused = [&](const char* name, const std::string& records, std::string& why,
                           std::size_t pad_newline_at = 0) {
            const fs::path a = mirror.dir / name;
            std::string x = tar_entry("PaxHeaders/x", records, 'x');
            if (pad_newline_at) x[512 + pad_newline_at] = '\n';
            const std::string body = x + tar_entry("data.bin", "payload");
            try {
                return write_tar_gz(a.string(), body) &&
                       !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find("pax record overruns") != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        const std::string valid = pax_record("comment", "ok");

        // PI-39 — a record claiming more than what is left of the header: 99
        // bytes, and 2^64-1 (which wrapped `pos + len` in the original code).
        // The wrapped case is also refused by the space rule: a wrapped end
        // lies before `pos`, and so before the record's own space. And one
        // claiming 30 whose 30th byte, in the padding past the header, is a
        // '\n': only the length check stands between it and being read.
        std::string w1, w2, w6;
        const bool short_ok = refused("long.tar.gz", valid + "99 path=EVIL.img\n", w1);
        const bool wrap_ok  = refused("wrap.tar.gz", valid + "18446744073709551615 path=EVIL.img\n", w2);
        const bool pad_ok   = refused("pad.tar.gz", valid + "30 path=EVIL.img\n", w6, valid.size() + 29);
        check("PI-39",
              "a pax record after a valid one that claims more than is left of the header (99, "
              "2^64-1, or 30 with a '\\n' in the padding where it would end) makes the archive "
              "malformed",
              short_ok && wrap_ok && pad_ok,
              fmt("99: %d (%s) 2^64-1: %d (%s) padded: %d (%s)", short_ok ? 1 : 0, w1.c_str(),
                  wrap_ok ? 1 : 0, w2.c_str(), pad_ok ? 1 : 0, w6.c_str()));

        // PI-40 — the right length, but the record does not end on '\n'.
        std::string w3;
        const bool nl_ok = refused("nl.tar.gz", valid + "19 path=NextPi.imgX", w3);
        check("PI-40", "a pax record that does not end on '\\n' makes the archive malformed", nl_ok,
              fmt("error='%s'", w3.c_str()));

        // PI-41 — the length ends before the record's first space: "3\n\n" is
        // three bytes ending on '\n', and the space found is the next record's.
        std::string w4;
        const bool sp_ok = refused("space.tar.gz", valid + "3\n\n" + pax_record("path", "x.img"), w4);
        check("PI-41",
              "a pax record whose length ends before its first space (the space found belongs to the "
              "next record) makes the archive malformed",
              sp_ok, fmt("error='%s'", w4.c_str()));

        // PI-42 — no space at all in what is left.
        std::string w5;
        const bool nosp_ok = refused("nospace.tar.gz", valid + "4ab\n", w5);
        check("PI-42", "a pax record with no space after its length makes the archive malformed", nosp_ok,
              fmt("error='%s'", w5.c_str()));
    }

    // ── PI-43 — THE MARKER CANNOT BE REMOVED (R3-4). Removing `release` is the
    // install's first step; when it fails (here a non-empty directory holds
    // the name) the install stops there, before the image is put in place.
    {
        FakeMirror mirror("marker");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir / "release" / "in-the-way", ec);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PI-43",
              "a NextPi install whose release marker cannot be removed fails at once, before the "
              "image is put in place",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("cannot install") != std::string::npos && !fs::exists(dir / "nextpi.img"),
              fmt("status=%d error='%s' image %s", static_cast<int>(r.status), r.error.c_str(),
                  fs::exists(dir / "nextpi.img") ? "installed" : "absent"));
    }

    // ── PI-44 — THE NAME LENGTH LIMIT, AT ITS BOUNDARY (R3-5): a release name
    // of 64 characters is accepted, one of 65 refused.
    {
        const bool at_64 = nextpi::valid_release_name(std::string(64, 'a'));
        const bool at_65 = nextpi::valid_release_name(std::string(65, 'a'));
        check("PI-44", "a NextPi release name of 64 characters is valid and one of 65 is not",
              at_64 && !at_65, fmt("64: %d 65: %d", at_64 ? 1 : 0, at_65 ? 1 : 0));
    }

    // ── PI-32 — A FAILED START KEEPS THE USER'S OVERLAY (R2-9). Only an overlay
    // the failing start created is removed: one that was there before holds
    // everything NextPi saved, and must survive.
    {
        FakeNextPi fake("keep-overlay");
        const fs::path overlay = fs::path(fake.dir()) / "overlay.qcow2";
        std::ofstream(overlay) << "NextPi's saved state";
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-fails");
        spec.audio       = "none";
        PiQemu q;
        std::string error;
        const bool refused = fake.ok() && !q.start(spec, error);
        check("PI-32",
              "a NextPi start that fails keeps an overlay that was already there (the user's saved "
              "NextPi state): only one the failing start created is removed",
              refused && slurp(overlay) == "NextPi's saved state",
              fmt("refused=%d overlay='%s'", refused ? 1 : 0, slurp(overlay).c_str()));
    }

    // ── PI-33 — THE CHILD ENVIRONMENT ITSELF (R2-10). PiQemu::child_environment
    // drops every LANG and LC_ALL entry before adding its own, so the child
    // gets each exactly once. (A shell in between, as PI-18 sees it, would
    // hide a duplicate; qemu-img is spawned directly and would not.)
    {
        const char* env[] = {"PATH=/bin", "LANG=es_ES.UTF-8", "LC_ALL=es_ES.UTF-8", "LANGUAGE=es",
                             "LC_ALL=fr_FR", nullptr};
        const std::vector<std::string> got = PiQemu::child_environment(env);
        const std::vector<std::string> want = {"PATH=/bin", "LANGUAGE=es", "LANG=C", "LC_ALL=C"};
        std::string joined;
        for (const std::string& v : got) joined += v + " ";
        check("PI-33",
              "a spawned NextPi child's environment is the parent's without any LANG or LC_ALL "
              "entry, then LANG=C and LC_ALL=C, each exactly once (LANGUAGE is not LANG)",
              got == want, fmt("got [%s]", joined.c_str()));
    }

    // ── PI-34 — THE WATCHDOG PASSES QEMU'S EXIT STATUS ON (R2-10). QEMU runs
    // under the watchdog shell; a QEMU that exits on its own with status 7
    // must show up as status 7, not as the shell's own 0.
    {
        FakeNextPi fake("status");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-exits-7");
        spec.audio       = "none";
        PiQemu q;
        std::string error;
        const bool started = fake.ok() && q.start(spec, error);
        int status = -1;
        for (int i = 0; i < 150 && (status = q.exit_status()) == -1; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
#ifndef _WIN32
        const bool seven = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 7;
#else
        const bool seven = false;
#endif
        check("PI-34",
              "when QEMU exits on its own, the status jnext reaps is QEMU's (7), passed on by the "
              "watchdog shell",
              started && seven, fmt("started=%d (%s) wait status=%d", started ? 1 : 0, error.c_str(), status));
    }

    // ── PI-35 — THE WATCHDOG PIPE IS CLOSE-ON-EXEC (R2-10). Every descriptor a
    // start leaves open in jnext — the watchdog pipe's write end — has
    // FD_CLOEXEC, so no other child jnext spawns (ffmpeg, a second QEMU) can
    // inherit it and keep the watchdog from ever seeing jnext go away.
    {
        FakeNextPi fake("cloexec");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        const std::set<int> before = open_fds();
        PiQemu q;
        std::string error;
        const bool started = fake.ok() && q.start(spec, error);
        std::string fresh;
        bool all_cloexec = true;
        int count = 0;
#ifndef _WIN32
        for (int fd : open_fds()) {
            if (before.count(fd)) continue;
            ++count;
            const bool ce = (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
            all_cloexec = all_cloexec && ce;
            fresh += std::to_string(fd) + (ce ? "(cloexec) " : "(INHERITABLE) ");
        }
#endif
        check("PI-35",
              "every descriptor a NextPi start leaves open in jnext (the watchdog pipe's write "
              "end) is close-on-exec, so no other child can inherit it",
              started && count >= 1 && all_cloexec,
              fmt("started=%d (%s) new fds: %s", started ? 1 : 0, error.c_str(), fresh.c_str()));
    }

    // ── PI-36 — THE REPLAY GATE, RZX HALF (R2-11). PI-15 replays a rewind; an
    // RZX playback must hold the link inert the same way: the guest's byte
    // does not reach the Pi, the Pi's bytes are not read; afterwards both flow.
    {
        TempFifoCable cable("pi-rzx");
        Emulator emu;
        emu.init(pi_config(cable.base()));
        const bool attached = cable.attach();

        RzxRecording rec;
        rec.frames.resize(50);
        emu.rzx_player().start(std::move(rec));
        cable.send({0xE1});
        pi_transmit_frame(emu, 0x30, 0xD2);
        const bool playing = emu.rzx_player().is_playing();
        const std::vector<uint8_t> during = cable.drain();
        const PiUartDevice* pi = emu.pi_uart();
        const bool not_read = pi && pi->link().received() == 0;
        emu.rzx_player().stop();

        std::vector<uint8_t> heard;
        for (int f = 0; f < 3; ++f) {
            const std::vector<uint8_t> got = pi_frame(emu, 0x30);
            heard.insert(heard.end(), got.begin(), got.end());
        }
        pi_transmit_frame(emu, 0x30, 0xD3);
        const std::vector<uint8_t> after = cable.drain();
        check("PI-36",
              "during an RZX playback the NextPi link is inert: the guest's byte does not reach the "
              "Pi and the Pi's bytes are not consumed; after it they arrive and the guest is heard",
              attached && playing && during.empty() && not_read &&
                  heard == std::vector<uint8_t>({0xE1}) && after == std::vector<uint8_t>({0xD3}),
              fmt("playing=%d during=[%s] (want empty) not_read=%d heard=[%s] (want E1) after=[%s]",
                  playing ? 1 : 0, bytes_hex(during).c_str(), not_read ? 1 : 0, bytes_hex(heard).c_str(),
                  bytes_hex(after).c_str()));
    }

    // ── PI-37 / PI-45 — THE CLOSE-ON-EXEC FALLBACK (R2-12, R3-6), for systems
    // where posix_spawn cannot close descriptors itself. It marks every open
    // descriptor from 3 up (fd 57) except the one to keep (fd 58), and leaves
    // stdin, stdout and stderr alone. PI-37 runs it as used, from the list of
    // open descriptors; PI-45 runs its last resort, the number walk, and
    // checks how far that walk goes for the limits sysconf can report.
    {
#ifndef _WIN32
        auto run = [](const std::vector<std::string>& lists, std::string& detail) {
            int p[2];
            if (::pipe(p) != 0) { detail = "pipe failed"; return false; }
            const bool fixture = ::dup2(p[0], 57) == 57 && ::dup2(p[1], 58) == 58;
            ::close(p[0]);
            ::close(p[1]);
            // fd 3, the walk's lower bound (R4-1), and the last number the
            // number walk reaches (R4-2), each holding a pipe end without
            // close-on-exec. Whatever this process had at fd 3 is set aside
            // and put back.
            const int saved3 = ::fcntl(3, F_DUPFD_CLOEXEC, 100);
            const int flags3 = ::fcntl(3, F_GETFD);
            // The walk's last number is usable only below RLIMIT_NOFILE: raise
            // the soft limit to it where the hard limit allows (on Linux
            // sysconf reports that limit itself, so `top` is exactly the last
            // number walked; macOS reports a larger one and caps the raise).
            const long walk = PiQemu::fd_walk_limit(::sysconf(_SC_OPEN_MAX));
            rlimit saved_nofile{};
            ::getrlimit(RLIMIT_NOFILE, &saved_nofile);
            rlimit raised = saved_nofile;
            if (raised.rlim_cur < static_cast<rlim_t>(walk)) {
                raised.rlim_cur = std::min<rlim_t>(static_cast<rlim_t>(walk), raised.rlim_max);
                if (::setrlimit(RLIMIT_NOFILE, &raised) != 0) ::getrlimit(RLIMIT_NOFILE, &raised);
            }
            // macOS also caps descriptors at kern.maxfilesperproc whatever
            // the rlimit says, so step down to the highest number that works.
            int top = static_cast<int>(std::min<rlim_t>(static_cast<rlim_t>(walk), raised.rlim_cur)) - 1;
            while (top > 1024 && ::dup2(58, top) != top) --top;
            const bool edges = ::dup2(58, 3) == 3 && ::dup2(58, top) == top;
            if (edges) {
                ::fcntl(3, F_SETFD, 0);
                ::fcntl(top, F_SETFD, 0);
            }
            int std_flags[3];
            for (int fd = 0; fd < 3; ++fd) {
                std_flags[fd] = ::fcntl(fd, F_GETFD);
                if (std_flags[fd] >= 0) ::fcntl(fd, F_SETFD, std_flags[fd] & ~FD_CLOEXEC);
            }
            bool marked = false, kept = false, std_alone = true, fd3 = false, at_top = false;
            if (fixture && edges) {
                PiQemu::mark_close_on_exec_except(58, lists);
                marked = (::fcntl(57, F_GETFD) & FD_CLOEXEC) != 0;
                kept   = (::fcntl(58, F_GETFD) & FD_CLOEXEC) == 0;
                fd3    = (::fcntl(3, F_GETFD) & FD_CLOEXEC) != 0;
                at_top = (::fcntl(top, F_GETFD) & FD_CLOEXEC) != 0;
                for (int fd = 0; fd < 3; ++fd)
                    if (std_flags[fd] >= 0 && (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0) std_alone = false;
            }
            for (int fd = 0; fd < 3; ++fd)
                if (std_flags[fd] >= 0) ::fcntl(fd, F_SETFD, std_flags[fd]);
            ::close(top);
            ::setrlimit(RLIMIT_NOFILE, &saved_nofile);
            if (saved3 >= 0) {
                ::dup2(saved3, 3);
                ::fcntl(3, F_SETFD, flags3);   // dup2 cleared close-on-exec; restore what fd 3 had
                ::close(saved3);
            } else {
                ::close(3);
            }
            ::close(57);
            ::close(58);
            detail = fmt("fixture=%d edges=%d 57 marked=%d 58 kept=%d fd 3 marked=%d fd %d (top) marked=%d "
                         "0-2 untouched=%d", fixture ? 1 : 0, edges ? 1 : 0, marked ? 1 : 0, kept ? 1 : 0,
                         fd3 ? 1 : 0, top, at_top ? 1 : 0, std_alone ? 1 : 0);
            return fixture && edges && marked && kept && fd3 && at_top && std_alone;
        };
        std::string listed_detail, walked_detail;
        const bool listed = run({"/proc/self/fd", "/dev/fd"}, listed_detail);
        const bool walked = run({}, walked_detail);
#else
        const bool listed = false, walked = false;
        const std::string listed_detail, walked_detail;
#endif
        check("PI-37",
              "the close-on-exec fallback marks every open descriptor from 3 up (fd 3 itself, fd 57 "
              "and the walk's last number) except the one to keep (fd 58), and leaves stdin, stdout "
              "and stderr alone",
              listed, listed_detail);
        const bool limits = PiQemu::fd_walk_limit(-1) == 65536 && PiQemu::fd_walk_limit(0) == 65536 &&
                            PiQemu::fd_walk_limit(1024) == 1024 &&
                            PiQemu::fd_walk_limit(65536) == 65536 &&
                            PiQemu::fd_walk_limit(1000000000L) == 65536;
        check("PI-45",
              "the fallback's last resort, the number walk, marks the same descriptors, fd 3 and its "
              "last number included; it goes up to sysconf's limit capped at 65536, and to 65536 when "
              "the limit is indeterminate (-1) or 0",
              walked && limits, walked_detail + fmt(" limits=%d (-1 -> %ld, 1024 -> %ld, 10^9 -> %ld)",
                                                    limits ? 1 : 0, PiQemu::fd_walk_limit(-1),
                                                    PiQemu::fd_walk_limit(1024),
                                                    PiQemu::fd_walk_limit(1000000000L)));
    }

    // ── PI-46 — THE LIST IS WHAT IS USED (R4-3). Below 65536 the number walk
    // would mark the same descriptors, so the marks alone cannot tell that
    // the list was read: the walk reports which source it used. By default
    // /proc/self/fd where it exists (Linux), else /dev/fd (macOS); /dev/fd
    // when the first list cannot be read; the number walk ("") with none.
    {
        std::string detail;
        bool ok = false;
#ifndef _WIN32
        int p[2];
        if (::pipe(p) == 0 && ::dup2(p[0], 57) == 57) {
            ::close(p[0]);
            auto marked57 = [] { return (::fcntl(57, F_GETFD) & FD_CLOEXEC) != 0; };
            auto unmark57 = [] { ::fcntl(57, F_SETFD, ::fcntl(57, F_GETFD) & ~FD_CLOEXEC); };
            std::error_code ec;
            const std::string want_default =
                std::filesystem::is_directory("/proc/self/fd", ec) ? "/proc/self/fd" : "/dev/fd";
            const std::string by_default = PiQemu::mark_close_on_exec_except(-1);
            const bool m1 = marked57();
            unmark57();
            const std::string second = PiQemu::mark_close_on_exec_except(-1, {"/nonexistent-jnext-fd-list", "/dev/fd"});
            const bool m2 = marked57();
            unmark57();
            const std::string none = PiQemu::mark_close_on_exec_except(-1, {});
            const bool m3 = marked57();
            ok = by_default == want_default && m1 && second == "/dev/fd" && m2 && none.empty() && m3;
            detail = fmt("default='%s' (want '%s') marked=%d; second='%s' marked=%d; none='%s' marked=%d",
                         by_default.c_str(), want_default.c_str(), m1 ? 1 : 0, second.c_str(), m2 ? 1 : 0,
                         none.c_str(), m3 ? 1 : 0);
            ::close(57);
            ::close(p[1]);
        }
#endif
        check("PI-46",
              "the close-on-exec fallback reads /proc/self/fd where it exists, else /dev/fd, falls "
              "back to /dev/fd when the first list cannot be read, and walks the numbers only with "
              "no list; each marks fd 57",
              ok, detail);
    }

    // ── PI-47 — THE LIST LEAVES OUT ITS OWN DIRECTORY (R4-5). Reading /dev/fd
    // (or /proc/self/fd) opens a descriptor that the listing then names; it is
    // closed before anyone acts on the list, and its number may be reused by
    // then, so it must not be in the list. The directory gets the lowest free
    // number, which the row learns first; fd 57 is open and must be listed.
    // And the directory is CLOSED again (R5-1): the lowest free number is the
    // same after each read, and after each list-reading marking walk. A
    // directory left open would be a descriptor per start that, being left
    // off the list, nothing marks close-on-exec — so it would reach QEMU.
    {
        std::string detail;
        bool ok = false;
#ifndef _WIN32
        int p[2];
        if (::pipe(p) == 0 && ::dup2(p[0], 57) == 57) {
            ::close(p[0]);
            ok = true;
            auto lowest_free = [] {
                const int fd = ::open("/dev/null", O_RDONLY);
                ::close(fd);
                return fd;
            };
            for (const char* list : {"/dev/fd", "/proc/self/fd"}) {
                const int probe = lowest_free();   // the number the directory will get
                std::vector<int> fds;
                const bool read = PiQemu::open_descriptors(list, fds);
                if (!read && std::string(list) == "/proc/self/fd") continue;   // no /proc (macOS)
                const int after_read = lowest_free();
                PiQemu::mark_close_on_exec_except(-1, {list});
                const int after_mark = lowest_free();
                ::fcntl(57, F_SETFD, 0);
                const bool own_left_out = std::find(fds.begin(), fds.end(), probe) == fds.end();
                const bool has_57 = std::find(fds.begin(), fds.end(), 57) != fds.end();
                const bool closed = after_read == probe && after_mark == probe;
                ok = ok && read && own_left_out && has_57 && closed;
                detail += fmt("%s: read=%d own fd %d left out=%d 57 listed=%d lowest free before/after "
                              "read/after mark %d/%d/%d; ", list, read ? 1 : 0, probe, own_left_out ? 1 : 0,
                              has_57 ? 1 : 0, probe, after_read, after_mark);
            }
            ::close(57);
            ::close(p[1]);
        }
#endif
        check("PI-47",
              "the open-descriptor list read from /dev/fd (and /proc/self/fd where it exists) names "
              "the open descriptors (fd 57) but not the directory's own, which is closed again: no "
              "descriptor is left open by reading the list or by the walk that marks from it",
              ok, detail);
    }

    // ── PI-38 — WHETHER TO START NEXTPI (R2-5), main.cpp's decision through
    // nextpi::start_request. The command line wins either way; without it a
    // GUI session follows Preferences and a headless one starts nothing.
    // Only --nextpi makes a failure an error (asked_on_cli; PI-27).
    {
        using nextpi::start_request;
        auto is = [](nextpi::StartRequest r, bool wanted, bool asked) {
            return r.wanted == wanted && r.asked_on_cli == asked;
        };
        const bool table =
            is(start_request(true, true, true, false), true, true) &&     // GUI --nextpi, pref off
            is(start_request(true, true, false, true), false, false) &&   // GUI --no-nextpi, pref on
            is(start_request(true, false, false, true), true, false) &&   // GUI, pref on
            is(start_request(true, false, false, false), false, false) && // GUI, pref off
            is(start_request(false, true, true, false), true, true) &&    // headless --nextpi
            is(start_request(false, false, false, true), false, false) && // headless: no Preferences
            is(start_request(false, true, false, true), false, false);    // headless --no-nextpi
        check("PI-38",
              "whether NextPi starts: --nextpi and --no-nextpi win, otherwise a GUI session follows "
              "the [nextpi] preference and a headless one starts nothing; only --nextpi makes a "
              "failure an error",
              table, "");
    }
}

// ══════════════════════════════════════════════════════════════════════
// NextPi's sound into the Next's mixer (PiAudio → I2s), end to end
// ══════════════════════════════════════════════════════════════════════

namespace {

/// A WAV file as QEMU's wav back-end writes it (44-byte header, s16 stereo,
/// 44.1 kHz) holding `frames` frames of a square wave flipping every `half`
/// frames: left +amp/-amp and right -r_amp/+r_amp. A right amplitude different
/// from the left one lets a row tell the two channels apart.
void write_tone(const std::string& path, int frames, int16_t amp, int half, int16_t r_amp) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); };
    f.write("RIFF", 4); u32(0); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
    u32(44100); u32(44100 * 4); u16(4); u16(16); f.write("data", 4); u32(0);
    for (int i = 0; i < frames; ++i) {
        const bool low = (i / half) % 2;
        u16(static_cast<uint16_t>(low ? static_cast<int16_t>(-amp) : amp));
        u16(static_cast<uint16_t>(low ? r_amp : static_cast<int16_t>(-r_amp)));
    }
}

/// The mixer's output over some frames: how many samples, and each channel's
/// lowest and highest value.
struct MixerLevels {
    int n = 0;
    int lo_l = 32767, hi_l = -32768, lo_r = 32767, hi_r = -32768;
};

/// Run `frames` frames with NR 0xA2 held at `a2` and return the levels of the
/// mixer's output over them.
MixerLevels mixer_levels(Emulator& emu, uint8_t a2, int frames) {
    MixerLevels m;
    std::vector<int16_t> buf(2 * 4096);
    for (int f = 0; f < frames; ++f) {
        emu.nextreg().write(0xA2, a2);
        emu.run_frame();
        int n;
        while ((n = emu.mixer().read_samples(buf.data(), 4096)) > 0) {
            m.n += n;
            for (int i = 0; i < n; ++i) {
                m.lo_l = std::min<int>(m.lo_l, buf[2 * i]);     m.hi_l = std::max<int>(m.hi_l, buf[2 * i]);
                m.lo_r = std::min<int>(m.lo_r, buf[2 * i + 1]); m.hi_r = std::max<int>(m.hi_r, buf[2 * i + 1]);
            }
        }
    }
    return m;
}

/// The left and right peak-to-peak of the mixer's output (see mixer_levels).
std::pair<int, int> mixer_swing(Emulator& emu, uint8_t a2, int frames) {
    const MixerLevels m = mixer_levels(emu, a2, frames);
    return {m.hi_l - m.lo_l, m.hi_r - m.lo_r};
}

} // namespace

static void test_nextpi_audio() {
    set_group("PI");

    // ── PI-48 — THE FEATURE: the Pi's sound reaches the Next's mixer. With the
    // default audio setting PiQemu asks QEMU for its wav back-end on a FIFO
    // (44.1 kHz s16 stereo) and reads it; the emulator latches it into I2s,
    // where NR 0xA2 gates it exactly as on the board (zxnext.vhd:2358-2359):
    // with 0x00 the output is silent, with 0xC0 (both channels enabled — what
    // .pisend sets, plus its other bits) the Pi's square wave is in the mix.
    // The channels carry different amplitudes so a swap shows, and the levels
    // are EXACT, so an error of one 10-bit step shows too: left ±16384 is
    // to_i2s 768/256, i.e. ±256 from the 0x200 rest, which the mixer outputs as
    // exactly +1024/-1024 (MX-06/07's x4); right ±8192 is 640/384, exactly
    // +512/-512. Each output sample holds one Pi frame for its whole interval,
    // so the extremes are those values; only the sample in which the gate
    // opens is a blend, and it lies between them.
    {
        FakeNextPi fake("audio");
        write_tone(fake.bin("tone.wav"), 11025, 16384, 50, 8192);   // 250 ms
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error) && qemu.audio() != nullptr;
        // Up to 5 s: the stand-in is a shell started by a shell, which a busy
        // host can take well over a second to get going.
        for (int i = 0; i < 500 && started && qemu.audio()->available() < 11025; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Read the args only now: the stand-in writes them asynchronously, and
        // before it plays the tone, so audio having arrived means they are in.
        const std::string args = fake.args();
        const bool args_ok = args.find("wav,id=snd0,path=") != std::string::npos &&
                             args.find(",out.frequency=44100,out.channels=2,out.format=s16") != std::string::npos;
        std::pair<int, int> closed{-1, -1};
        MixerLevels open;
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            closed = mixer_swing(emu, 0x00, 3);      // gate shut: Pi frames consumed, silent
            open   = mixer_levels(emu, 0xC0, 5);     // gate open: the tone
        }
        check("PI-48",
              "NextPi's sound reaches the Next's mixer over I2S: QEMU writes it to a FIFO "
              "PiQemu reads, the emulator latches it per sample, and NR 0xA2 gates it "
              "(0x00 silent, 0xC0 the Pi's square wave, each channel on its own side at its "
              "exact level: left +-1024 and right +-512) (i2s.vhd:177-180, zxnext.vhd:2358-2359)",
              started && args_ok && closed.first <= 16 && closed.second <= 16 && open.n > 0 &&
                  open.lo_l == -1024 && open.hi_l == 1024 && open.lo_r == -512 && open.hi_r == 512,
              fmt("started=%d (%s) args=%d swing closed L=%d R=%d (want <=16) open n=%d L %d..%d "
                  "R %d..%d (want L -1024..1024, R -512..512)", started ? 1 : 0, error.c_str(),
                  args_ok ? 1 : 0, closed.first, closed.second, open.n, open.lo_l, open.hi_l,
                  open.lo_r, open.hi_r));
    }

    // ── PI-49 — neither a rewind replay nor an RZX playback consumes the Pi's
    // stream (it belongs to the live session); live again, the emulator draws
    // from it at exactly one frame per mixer output sample, the rate QEMU is
    // asked for (a backlog of 8000 is below the trim, so nothing else drops). A replay runs no audio path at all (run_frame does not advance
    // audio while replay_mode_ holds), so there is no output to inspect: the
    // row asserts exactly that, zero samples. An RZX playback does produce
    // output, and it must be silent: the I2S rest value 0x200 in BOTH channels
    // (i2s.vhd:177-180), which the mixer centres on to output exactly 0
    // (Mixer::MIX_REST_LEVEL), so every sample of each channel must be 0 — a
    // swing alone could not show it (a channel stuck at another value is flat
    // too). RZX playback is driven
    // through the player's own API, as esp_wiring_test does: a recording of a
    // few empty frames keeps is_playing() true without overriding any input.
    {
        FakeNextPi fake("audio-replay");
        write_tone(fake.bin("tone.wav"), 8000, 16384, 50, 16384);
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error) && qemu.audio() != nullptr;
        for (int i = 0; i < 500 && started && qemu.audio()->available() < 8000; ++i)   // as PI-48
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        uint32_t before = 0, after_replay = 0, after_rzx = 0, after_live = 0;
        MixerLevels replay, rzx, live;
        // The reset value, with and without a Pi: 0x200 in both channels
        // (i2s_receive.vhd:129-130 resets to 0, i2s.vhd:177-180 inverts bit 12).
        bool rest_no_pi = false, rest_pi = false;
        if (started) {
            EmulatorConfig plain = pi_qemu_config(qemu);   // no pi_audio
            Emulator bare;
            bare.init(plain);
            rest_no_pi = bare.i2s().left() == 0x200 && bare.i2s().right() == 0x200;
        }
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            rest_pi = emu.i2s().left() == 0x200 && emu.i2s().right() == 0x200;
            before = qemu.audio()->available();
            emu.set_replay_mode(true);
            replay = mixer_levels(emu, 0xC0, 2);
            after_replay = qemu.audio()->available();
            emu.set_replay_mode(false);
            RzxRecording rec;
            rec.frames.resize(4);
            emu.rzx_player().start(std::move(rec));
            rzx = mixer_levels(emu, 0xC0, 2);
            after_rzx = qemu.audio()->available();
            emu.rzx_player().stop();
            live = mixer_levels(emu, 0xC0, 1);
            after_live = qemu.audio()->available();
        }
        const bool rzx_silent = rzx.n > 0 && rzx.lo_l == 0 && rzx.hi_l == 0 && rzx.lo_r == 0 &&
                                rzx.hi_r == 0;
        check("PI-49",
              "neither a rewind replay nor an RZX playback consumes the Pi's sound; a replay "
              "produces no audio at all, and an RZX playback leaves the I2S input silent (0x200 "
              "in both channels, so every output sample is 0); live again, the emulator draws "
              "exactly one Pi frame per mixer output sample; the I2S input resets to 0x200 with "
              "or without a Pi (i2s_receive.vhd:129-130, i2s.vhd:177-180)",
              started && rest_no_pi && rest_pi && before == 8000 && after_replay == before && after_rzx == before &&
                  replay.n == 0 && rzx_silent && live.n > 0 &&
                  before - after_live == static_cast<uint32_t>(live.n),
              fmt("started=%d (%s) available before=%u after replay=%u after rzx=%u after live=%u "
                  "replay samples=%d (want 0); rzx n=%d L %d..%d R %d..%d (want n>0, all 0); "
                  "live samples=%d consumed=%u (want equal); reset 0x200 without a Pi=%d, with=%d",
                  started ? 1 : 0, error.c_str(), before, after_replay, after_rzx, after_live,
                  replay.n, rzx.n, rzx.lo_l, rzx.hi_l, rzx.lo_r, rzx.hi_r, live.n,
                  before - after_live, rest_no_pi ? 1 : 0, rest_pi ? 1 : 0));
    }

    // ── PI-50 — the warm-start recording machine does not get the Pi's sound:
    // it would be a second consumer of one stream.
    {
        PiAudio audio;
        EmulatorConfig live;
        live.sd_card_image = "card.img";
        live.pi_audio      = &audio;
        const EmulatorConfig b = Emulator::warm_start_boot_config(live);
        check("PI-50",
              "the warm-start recording boot gets no NextPi audio reader (one consumer of the "
              "Pi's sound; jnext-only, no VHDL counterpart)",
              b.pi_audio == nullptr && b.sd_card_image == "card.img", "");
    }

    // ── PI-51 — the audio FIFO cannot be made: start() fails with PiAudio's
    // error rather than starting a QEMU whose sound goes nowhere. $TMPDIR is
    // nested so deep that the UART FIFO `uart.out` still fits in PATH_MAX but
    // `uart.audio`, two characters longer, does not (ENAMETOOLONG).
    {
        namespace fs = std::filesystem;
        FakeNextPi fake("audio-fifo");
        // Canonical: the kernel's limit applies after symlinks resolve, and
        // macOS's temporary directory sits behind one (/var -> /private/var).
        std::error_code ec;
        const fs::path deep_root = fs::canonical(fs::temp_directory_path(), ec) /
                                   ("jnext-pi51-" + std::to_string(::getpid()));
        // strlen(<tmp>/jnext-pi-XXXXXX/uart.out) == PATH_MAX - 1, the longest
        // path the kernel takes; ".audio" makes it PATH_MAX + 1.
        const std::size_t want = static_cast<std::size_t>(PATH_MAX) - 1 -
                                 std::strlen("/jnext-pi-XXXXXX/uart.out");
        std::string deep = deep_root.string();
        while (deep.size() + 1 < want) {               // each step adds "/" + a name
            const std::size_t rest = want - deep.size() - 1;
            // Names of at most 200 chars; never leave exactly 1 char to fill.
            const std::size_t take = rest <= 200 ? rest : (rest == 201 ? 199 : 200);
            deep += "/" + std::string(take, 'd');
        }
        fs::create_directories(deep, ec);
        const bool deep_ok = !ec && deep.size() == want;
        const char* old = std::getenv("TMPDIR");
        const std::string saved = old ? old : "";
        ::setenv("TMPDIR", deep.c_str(), 1);
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        bool refused = false;
        std::string error;
        {
            PiQemu q;
            refused = !q.start(spec, error) && !q.running() && q.audio() == nullptr;
        }
        if (old) ::setenv("TMPDIR", saved.c_str(), 1); else ::unsetenv("TMPDIR");
        const bool spawned = fs::exists(fs::path(fake.dir()) / "bin" / "pid");
        fs::remove_all(deep_root, ec);
        check("PI-51",
              "when the Pi's audio FIFO cannot be created, starting NextPi fails with that error "
              "and starts no QEMU (jnext-only, no VHDL counterpart)",
              fake.ok() && deep_ok && refused && !spawned &&
                  error.find("uart.audio") != std::string::npos,
              fmt("deep=%d (%zu/%zu) refused=%d spawned=%d error='%s'", deep_ok ? 1 : 0, deep.size(),
                  want, refused ? 1 : 0, spawned ? 1 : 0, error.c_str()));
    }

    // ── PI-52 — the mixer's -audiodev value escapes the FIFO path: QEMU's
    // option syntax splits on ',' (a literal comma is written ',,'), and the
    // path comes from $TMPDIR, which may hold one. Pure.
    {
        const std::string arg = PiQemu::mixer_audiodev_arg("/t,mp/uart");
        const std::string want =
            "wav,id=snd0,path=/t,,mp/uart.audio,out.frequency=44100,out.channels=2,out.format=s16";
        check("PI-52",
              "the -audiodev value that sends the Pi's sound to the mixer escapes a comma in the "
              "FIFO path, and asks for 44.1 kHz s16 stereo (jnext-only, no VHDL counterpart)",
              arg == want, fmt("got '%s' want '%s'", arg.c_str(), want.c_str()));
    }

    // ── PI-54 — `host` reaches QEMU as the platform's default output, and the
    // empty setting still means the mixer. Pure: build_args only.
    {
        auto audiodev = [](const std::vector<std::string>& a) {
            for (std::size_t i = 0; i + 1 < a.size(); ++i)
                if (a[i] == "-audiodev") return a[i + 1];
            return std::string();
        };
        PiQemu::Spec spec;
        spec.dir   = "/n";
        spec.audio = "host";
        const std::string host = audiodev(PiQemu::build_args(spec, "/run/uart"));
        spec.audio.clear();
        const std::string mixer = audiodev(PiQemu::build_args(spec, "/run/uart"));
#ifdef __APPLE__
        const bool host_ok = host.rfind("coreaudio,id=snd0,", 0) == 0;
#else
        const bool host_ok = host == "pa,id=snd0";
#endif
        check("PI-54",
              "the Pi audio setting `host` gives QEMU the platform's default output (pa; "
              "coreaudio on macOS), and the empty setting the mixer's wav FIFO (jnext-only, no "
              "VHDL counterpart)",
              host_ok && mixer == PiQemu::mixer_audiodev_arg("/run/uart"),
              fmt("host='%s' empty='%s'", host.c_str(), mixer.c_str()));
    }

    // ── PI-53 — a non-default audio setting keeps the Pi's sound away from the
    // mixer: with Spec::audio = "none" QEMU gets that driver (not the wav
    // FIFO), PiQemu makes no PiAudio and creates no audio FIFO.
    {
        namespace fs = std::filesystem;
        FakeNextPi fake("audio-none");
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        spec.audio       = "none";
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error);
        const bool no_reader = qemu.audio() == nullptr;
        const bool recorded = started && fake.child_pid() > 0;   // args written before the pid
        const std::string args = fake.args();
        const bool driver = args.find("none,id=snd0") != std::string::npos &&
                            args.find("wav,id=snd0,path=") == std::string::npos;
        std::error_code ec;
        const bool no_fifo = started &&
            !fs::exists(fs::path(qemu.rx_path()).parent_path() / "uart.audio", ec);
        check("PI-53",
              "with the Pi's audio set to a QEMU driver (\"none\"), QEMU is given that driver, "
              "not the mixer's wav FIFO, and jnext makes no audio reader or FIFO (jnext-only, no "
              "VHDL counterpart)",
              started && no_reader && recorded && driver && no_fifo,
              fmt("started=%d (%s) reader=%s args recorded=%d driver=%d fifo=%s",
                  started ? 1 : 0, error.c_str(), no_reader ? "none" : "MADE", recorded ? 1 : 0,
                  driver ? 1 : 0, no_fifo ? "none" : "MADE"));
    }

    // ── PI-55 — the guest's view of the Pi's sample (NR 0x2C/0x2D/0x2E,
    // zxnext.vhd:6006-6015) is live in a live run and the rest value 0x200
    // whenever the run must be reproducible: a rewind replay, an RZX playback
    // or an RZX recording, none of which can reproduce a host-timed sample. A
    // restored snapshot also puts the latch back at rest. The tone is left
    // ±16384 (10-bit 768/256, NR byte 0xC0/0x40) and right ±8192 (640/384,
    // 0xA0/0x60); the rest value reads 0x80, and NR 0x2D's low bits 0.
    {
        namespace fs = std::filesystem;
        FakeNextPi fake("audio-nr2c");
        write_tone(fake.bin("tone.wav"), 8000, 16384, 50, 8192);
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        PiQemu qemu;
        std::string error;
        const bool started = fake.ok() && qemu.start(spec, error) && qemu.audio() != nullptr;
        for (int i = 0; i < 500 && started && qemu.audio()->available() < 8000; ++i)   // as PI-48
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        struct Read { uint8_t l = 0, d = 0xFF, r = 0; };
        Read live, replay, rzx_play, rzx_rec;
        bool rest_after_load = false;
        const std::string rzx_path =
            (fs::temp_directory_path() / ("jnext-pi55-" + std::to_string(::getpid()) + ".rzx")).string();
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            auto read = [&] {
                Read v;
                v.l = emu.nextreg().read(0x2C);
                v.d = emu.nextreg().read(0x2D);
                v.r = emu.nextreg().read(0x2E);
                return v;
            };
            mixer_swing(emu, 0xC0, 1);          // live: a Pi frame is latched
            live = read();
            emu.set_replay_mode(true);
            replay = read();
            emu.set_replay_mode(false);
            RzxRecording rec;
            rec.frames.resize(4);
            emu.rzx_player().start(std::move(rec));
            rzx_play = read();
            emu.rzx_player().stop();
            // The recorder itself: Emulator::start_rzx_recording() refuses on
            // a Next (GH #274), and the gate reads only is_recording().
            if (emu.rzx_recorder().start(rzx_path)) {
                rzx_rec = read();
                emu.rzx_recorder().stop();
            }
            StateWriter measure;
            emu.save_state(measure);
            std::vector<uint8_t> snap(measure.position());
            StateWriter w(snap.data(), snap.size());
            emu.save_state(w);
            StateReader r(snap.data(), snap.size());
            emu.load_state(r);
            rest_after_load = emu.i2s().left() == 0x200 && emu.i2s().right() == 0x200;
        }
        std::error_code ec;
        fs::remove(rzx_path, ec);
        auto is_rest = [](const Read& v) { return v.l == 0x80 && v.d == 0x00 && v.r == 0x80; };
        const bool live_ok = (live.l == 0xC0 || live.l == 0x40) && (live.r == 0xA0 || live.r == 0x60);
        check("PI-55",
              "the guest reads the Pi's live sample on NR 0x2C/0x2E in a live run, and the rest "
              "value 0x200 (0x80, NR 0x2D 0) in a rewind replay, an RZX playback and an RZX "
              "recording; a restored snapshot puts the latch at rest (zxnext.vhd:6006-6015)",
              started && live_ok && is_rest(replay) && is_rest(rzx_play) && is_rest(rzx_rec) &&
                  rest_after_load,
              fmt("started=%d (%s) live 2C=%02X 2E=%02X; replay %02X/%02X/%02X; rzx play "
                  "%02X/%02X/%02X; rzx rec %02X/%02X/%02X (want 80/00/80); rest after load=%d",
                  started ? 1 : 0, error.c_str(), live.l, live.r, replay.l, replay.d, replay.r,
                  rzx_play.l, rzx_play.d, rzx_play.r, rzx_rec.l, rzx_rec.d, rzx_rec.r,
                  rest_after_load ? 1 : 0));
    }

    // ── PI-56..59 — NR 0xA2 bit 0: the Pi's audio as the EAR input, read on
    // port 0xFE bit 6 (zxnext.vhd:2361-2373, :3459; zxnext_top_issue2.vhd:
    // 663-677). The stand-in plays a square wave with BOTH channels in phase at
    // ±24576 (10-bit 0x380 / 0x080: t = 11 / 00), flipping every 20 samples
    // (0.45 ms, well inside the ~1.17 ms relaxation), so the comparator
    // toggles with it.
    auto ear_bit = [](Emulator& emu) { return (emu.port().read(0xFFFE) >> 6) & 1; };
    auto start_pi = [](FakeNextPi& fake, PiQemu& qemu, int frames, int half, std::string& error) {
        write_tone(fake.bin("tone.wav"), frames, 24576, half, -24576);   // R in phase with L
        PiQemu::Spec spec;
        spec.dir         = fake.dir();
        spec.qemu_binary = fake.bin("qemu-system-arm");
        const bool started = fake.ok() && qemu.start(spec, error) && qemu.audio() != nullptr;
        for (int i = 0; i < 500 && started && qemu.audio()->available() < static_cast<uint32_t>(
                                                   std::min(frames, 40000)); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return started;
    };

    // Step instructions and read port 0xFE bit 6 every 100 of them, against
    // the comparator: the stand-in's whole tone is buffered at once, so the
    // first pop trims it to ~4410 frames (100 ms; MX-32) and the sampling
    // stays inside them. `invert`: the tape jack is high, so bit 6 = !ear.
    struct EarReads { int reads = 0, mismatches = 0, ones = 0, zeros = 0; };
    auto sample_ear = [&](Emulator& emu, int instructions, bool invert) {
        EarReads r;
        for (int i = 1; i <= instructions; ++i) {
            emu.execute_single_instruction();
            if (i % 100) continue;
            const int bit = ear_bit(emu);
            const int want = (emu.i2s().fe_ear() ? 1 : 0) ^ (invert ? 1 : 0);
            ++r.reads;
            if (bit != want) ++r.mismatches;
            (bit ? r.ones : r.zeros)++;
        }
        return r;
    };

    // PI-56 — the CPU reads the comparator: with NR 0xA2 = 0xC3 (stereo +
    // EAR; bit 1 is reserved and set), port 0xFE bit 6 equals pi_fe_ear at
    // every read and takes both values. With bit 0 cleared it reads 0, and
    // during a rewind replay it reads 0 even while the comparator is 1 (a
    // host-timed level, like the sample on NR 0x2C).
    {
        FakeNextPi fake("audio-ear");
        PiQemu qemu;
        std::string error;
        const bool started = start_pi(fake, qemu, 40000, 20, error);
        EarReads on;
        int off_ones = -1;
        bool replay_ok = false;
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            emu.nextreg().write(0xA2, 0xC3);
            on = sample_ear(emu, 20000, false);
            for (int i = 0; i < 20000 && !emu.i2s().fe_ear(); ++i) emu.execute_single_instruction();
            if (emu.i2s().fe_ear() && ear_bit(emu) == 1) {
                emu.set_replay_mode(true);
                replay_ok = ear_bit(emu) == 0;
                emu.set_replay_mode(false);
            }
            emu.nextreg().write(0xA2, 0xC2);                  // bit 0 clear
            off_ones = 0;
            for (int i = 1; i <= 5000; ++i) {
                emu.execute_single_instruction();
                if (i % 100 == 0) off_ones += ear_bit(emu);
            }
        }
        check("PI-56",
              "with NR 0xA2 bit 0 set, port 0xFE bit 6 reads the Pi's EAR comparator, toggling "
              "with a Pi square wave; with bit 0 clear, and during a rewind replay, it reads 0 "
              "(zxnext.vhd:2361-2373, :3459)",
              started && on.reads == 200 && on.mismatches == 0 && on.ones > 0 && on.zeros > 0 &&
                  off_ones == 0 && replay_ok,
              fmt("started=%d (%s) reads=%d mismatches=%d ones=%d zeros=%d bit0-clear ones=%d "
                  "replay=%d", started ? 1 : 0, error.c_str(), on.reads, on.mismatches, on.ones,
                  on.zeros, off_ones, replay_ok ? 1 : 0));
    }

    // PI-57 — XOR with a playing tape: a WAV tape holding a steady high level
    // is the EAR jack, and bit 6 is jack XOR pi_fe_ear, i.e. the inverse of
    // the comparator at every read (zxnext_top_issue2.vhd:673).
    {
        namespace fs = std::filesystem;
        FakeNextPi fake("audio-ear-tape");
        PiQemu qemu;
        std::string error;
        const bool started = start_pi(fake, qemu, 40000, 20, error);
        const std::string wav = (fs::path(fake.dir()) / "high.wav").string();
        {
            std::ofstream f(wav, std::ios::binary);
            auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>(v >> (8 * i))); };
            auto u16 = [&](uint16_t v) { f.put(static_cast<char>(v)); f.put(static_cast<char>(v >> 8)); };
            const uint32_t n = 44100 * 3;                     // 3 s of 8-bit mono, all high
            f.write("RIFF", 4); u32(36 + n); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
            u32(44100); u32(44100); u16(1); u16(8); f.write("data", 4); u32(n);
            for (uint32_t i = 0; i < n; ++i) f.put(static_cast<char>(0xF0));
        }
        EarReads r;
        bool loaded = false;
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            loaded = emu.load_wav(wav) && emu.wav_tape().is_playing();
            emu.nextreg().write(0xA2, 0xC3);
            if (loaded) r = sample_ear(emu, 20000, true);
        }
        check("PI-57",
              "with NR 0xA2 bit 0 set and a tape playing a steady high level, port 0xFE bit 6 is "
              "the tape XOR the Pi's EAR comparator (zxnext_top_issue2.vhd:673)",
              started && loaded && r.reads == 200 && r.mismatches == 0 && r.ones > 0 && r.zeros > 0,
              fmt("started=%d (%s) tape=%d reads=%d mismatches=%d ones=%d zeros=%d", started ? 1 : 0,
                  error.c_str(), loaded ? 1 : 0, r.reads, r.mismatches, r.ones, r.zeros));
    }

    // PI-58 — the relaxation: the stand-in plays a steady high level, so the
    // comparator rises once and holds. Stepping instructions from the edge,
    // bit 6 reads 1 at once and 30000 master cycles later, and has relaxed to
    // the issue-2 level (0, issue-2 off) 34000 cycles after it: ear_relax
    // holds a level for 64 ticks of the 512-cycle membrane enable (32768).
    {
        FakeNextPi fake("audio-ear-relax");
        PiQemu qemu;
        std::string error;
        const bool started = start_pi(fake, qemu, 40000, 1 << 30, error);   // never flips
        bool saw_edge = false;
        int at_edge = -1, at_30000 = -1, at_34000 = -1;
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            Emulator emu;
            emu.init(cfg);
            emu.nextreg().write(0xA2, 0xC3);
            for (int i = 0; i < 2000000 && !emu.i2s().fe_ear(); ++i) emu.execute_single_instruction();
            saw_edge = emu.i2s().fe_ear();
            if (saw_edge) {
                const uint64_t t0 = emu.clock().get();
                at_edge = ear_bit(emu);
                while (emu.clock().get() < t0 + 30000) emu.execute_single_instruction();
                at_30000 = ear_bit(emu);
                while (emu.clock().get() < t0 + 34000) emu.execute_single_instruction();
                at_34000 = ear_bit(emu);
            }
        }
        check("PI-58",
              "a steady Pi EAR level reads 1 on port 0xFE bit 6 from its edge, still 1 30000 "
              "master cycles later, and relaxed to the issue-2 level (0) 34000 cycles after it "
              "(ear_relax, zxnext_top_issue2.vhd:663-677: 64 x 512 cycles)",
              started && saw_edge && at_edge == 1 && at_30000 == 1 && at_34000 == 0,
              fmt("started=%d (%s) edge=%d bit6 at edge=%d +30000=%d +34000=%d", started ? 1 : 0,
                  error.c_str(), saw_edge ? 1 : 0, at_edge, at_30000, at_34000));
    }

    // PI-59 — --silent: no mixing, but the Pi's samples are still latched,
    // so the EAR path works as in PI-56 (--silent leaves EAR input working).
    {
        FakeNextPi fake("audio-ear-silent");
        PiQemu qemu;
        std::string error;
        const bool started = start_pi(fake, qemu, 40000, 20, error);
        EarReads r;
        if (started) {
            EmulatorConfig cfg = pi_qemu_config(qemu);
            cfg.pi_audio = qemu.audio();
            cfg.silent   = true;
            Emulator emu;
            emu.init(cfg);
            emu.nextreg().write(0xA2, 0xC3);
            r = sample_ear(emu, 20000, false);
        }
        check("PI-59",
              "with --silent the Pi's samples are still latched and NR 0xA2 bit 0's EAR path "
              "still toggles port 0xFE bit 6 (jnext-only: --silent has no VHDL counterpart)",
              started && r.reads == 200 && r.mismatches == 0 && r.ones > 0 && r.zeros > 0,
              fmt("started=%d (%s) reads=%d mismatches=%d ones=%d zeros=%d", started ? 1 : 0,
                  error.c_str(), r.reads, r.mismatches, r.ones, r.zeros));
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

// ══════════════════════════════════════════════════════════════════════
// GH #265 — UART status read at the IN's latch point
// ══════════════════════════════════════════════════════════════════════
//
// port_uart_dat is reloaded from uart_do on every CLK_CPU falling edge
// (zxnext.vhd:3418-3423), and the IN latches the reload made 2.5 T-states
// into its I/O cycle (t80na.vhd:214-222, t80n.vhd:1781-1782): for IN A,(C)
// the UART as it stands after edge start + 83. The UART used to be ticked
// only between instructions, so the IN saw it at its instruction's start.
static void test_gh265_status_read() {
    set_group("GH265-UART");

    // UART-RD-GH265-01 — TX empty (0x133B bit 4) of a byte written at edge
    // 0 outside any instruction. The byte engine starts it on edge 1 and
    // finishes it byte_transfer_ticks() = 243 x 10 = 2430 edges later, on
    // 2431 (the model's byte time; uart.vhd's prescaled bit clock). An IN
    // starting at 2400 loads on 2483: empty. One starting at 2300 loads on
    // 2383: still busy. Pre-fix the IN at 2400 saw the transmitter at
    // 2400: busy. The margins (48 / 52 edges) keep the row about the latch
    // point, not about the model's byte time to the edge.
    auto status_at = [](uint64_t s) -> uint8_t {
        Emulator emu;
        build_next_emulator(emu);
        emu.port().out(0x133B, 0x55);                   // TX, loops back
        emu.mmu().write(0x8000, 0xED);
        emu.mmu().write(0x8001, 0x78);                  // IN A,(C)
        auto r = emu.cpu().get_registers();
        r.PC = 0x8000; r.BC = 0x133B;
        emu.cpu().set_registers(r);
        emu.uart().set_time(0);
        emu.uart().tick(static_cast<uint32_t>(s));      // devices stand at the clock
        emu.clock().tick(s - emu.clock().get());
        emu.cpu().execute();
        return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
    };
    const uint8_t late  = status_at(2400);
    const uint8_t early = status_at(2300);
    check("UART-RD-GH265-01",
          "UART status IN latches the transmitter as of the port_uart_dat "
          "reload 83 cycles in, not the instruction start "
          "(zxnext.vhd:3418-3423; t80na.vhd:214-222)",
          (late & 0x10) != 0 && (early & 0x10) == 0,
          "s=2400: 0x" + std::to_string(late) + " s=2300: 0x"
          + std::to_string(early) + " (want bit 4 set, clear)");

    // UART-WR-GH265-01 — a TX write inside an instruction reaches the UART
    // on its commit edge, not at the instruction's start. OUT (C),A to
    // 0x133B starting at 5000: IORQ+WR on 5072 (t80na.vhd:148-150), taken
    // on 5073; the byte starts on 5074 and ends 2430 edges later, on 7504.
    // An IN whose load edge is 7470 sees it still busy, one at 7600 sees
    // it done. Pre-fix the write landed at 5000 and the byte ended on 7431:
    // done at 7470 already. (Margins as UART-RD-GH265-01.)
    auto after_write = [](uint64_t sn) -> uint8_t {
        Emulator emu;
        build_next_emulator(emu);
        emu.mmu().write(0x8000, 0xED);
        emu.mmu().write(0x8001, 0x79);                  // OUT (C),A
        emu.mmu().write(0x8002, 0xED);
        emu.mmu().write(0x8003, 0x78);                  // IN A,(C)
        auto r = emu.cpu().get_registers();
        r.PC = 0x8000; r.BC = 0x133B;
        r.AF = static_cast<uint16_t>(0x5500 | (r.AF & 0x00FF));
        emu.cpu().set_registers(r);
        emu.uart().set_time(0);
        emu.uart().tick(5000);
        emu.clock().tick(5000 - emu.clock().get());
        emu.execute_single_instruction();               // OUT: 5000..5096
        const uint64_t s = sn - 83;
        emu.uart().set_time(emu.clock().get());
        emu.uart().tick(static_cast<uint32_t>(s - emu.clock().get()));
        emu.clock().tick(s - emu.clock().get());
        emu.cpu().execute();                            // IN A,(C)
        return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
    };
    const uint8_t busy = after_write(7470);
    const uint8_t done = after_write(7600);
    check("UART-WR-GH265-01",
          "UART TX write taken on the edge after IORQ+WR, 73 cycles into "
          "OUT (C),A (t80na.vhd:148-150; zxnext.vhd:3418-3423)",
          (busy & 0x10) == 0 && (done & 0x10) != 0,
          "Sn=7470: 0x" + std::to_string(busy) + " Sn=7600: 0x"
          + std::to_string(done) + " (want bit 4 clear, set)");
}

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

    test_pi_uart_link();
    test_pi_qemu();
    test_nextpi_provisioner();
    test_nextpi_review_rows();
    test_nextpi_review2_rows();
    test_nextpi_audio();
    std::printf("  Group: PI — done\n");

    test_nr_a0_pi_uart_routing(emu);
    std::printf("  Group: NR_A0-INT — done\n");

    test_esp_backend();
    std::printf("  Group: ESP — done\n");

    test_gh265_status_read();
    std::printf("  Group: GH265-UART — done\n");

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
