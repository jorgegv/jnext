// UART Subsystem Integration Test — full-machine rows exercising the
// dual-UART dispatch and the joystick-IO-mode UART multiplex END-TO-END
// through the port-dispatch and IoMode handlers (Task 3 UART+I2C
// SKIP-reduction plan, Wave C + Wave D, 2026-04-24).
//
// These plan rows cannot be exercised against the bare Uart class — they
// span:
//   * Port 0x133B / 0x143B / 0x153B / 0x163B dispatch registered in
//     `src/core/emulator.cpp:1452-1464`, which routes a raw byte to the
//     currently-selected UART channel (bit 6 of the select register,
//     per VHDL uart.vhd and zxnext.vhd:3335-3417).
//   * NR 0x0B iomode register (src/input/iomode.h), whose bits 7 & 0
//     drive the top-level UART ↔ joystick-connector RX mux at
//     zxnext.vhd:3340-3341. The iomode→UART routing is bound at the
//     Emulator tier via `Emulator::inject_joy_uart_rx` — tests the
//     same combinational behaviour the real silicon performs.
//
// They live on the integration tier rather than the subsystem tier: the
// subsystem-tier uart_test.cpp covers the per-channel Uart FIFO and
// framing semantics, while this file pins the NR/port-dispatch + iomode
// handoff that lives one level above Uart in emulator.cpp.
//
// Reference plan: doc/design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md §Phase 2
// (Waves C & D).
// Reference structural template: test/input/input_integration_test.cpp,
// test/ula/ula_integration_test.cpp.
//
// Wave C (parallel, 10 rows) & Wave D (this commit, 2 rows: DUAL-05/06).
// When both land the suite reports 12/12/0/0.
//
// Run: ./build/test/uart_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "input/iomode.h"
#include "peripheral/uart.h"

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
std::vector<SkipNote> g_skipped;  // always empty in this suite

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

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Drive the full UART advance so pending TX bytes complete. The
// default Next config runs UART 0 at 115200 @ 28 MHz → ~243 * 10 =
// 2430 master cycles for one 8N1 byte. Tick generously to guarantee
// completion of a single byte regardless of framing changes.
static void tick_uart_byte(Emulator& emu) {
    emu.uart().tick(8000);
}

// ══════════════════════════════════════════════════════════════════════
// Wave C rows — WAVE C APPENDS HERE.
//
// Wave C (parallel work, 10 rows) will insert its static void
// test_*() functions between this banner and the Wave D banner below,
// then register them in main() between the "Wave C rows" comment and
// the "Wave D rows" comment. Last-merger resolves any conflicts per
// CLAUDE.md.
// ══════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════
// Wave D rows — DUAL-05 / DUAL-06
// VHDL: zxnext.vhd:3335-3417 (top-level UART block), :3340-3341 (RX mux
// between joystick-connector and ESP/Pi pins), uart.vhd (per-channel
// FIFO + register multiplex using uart_select_r bit 6).
// Emulator wiring: src/core/emulator.cpp:1452-1464 (port dispatch),
// src/core/emulator.h (inject_joy_uart_rx — the iomode mux model).
// ══════════════════════════════════════════════════════════════════════

// DUAL-05 — channel-0 vs channel-1 TX pin routing.
//
// VHDL uart.vhd gates tx_wr on uart_select_r (bit 6 of the select
// register): writes to port 0x133B land in UART 0's TX FIFO when
// select=0, UART 1's TX FIFO when select=1. Downstream the two TX
// pins (uart0_tx → ESP, uart1_tx → Pi per zxnext.vhd:3343-3344) are
// physically distinct — a byte TX'd on one channel can never surface
// on the other. We verify the channel routing at the FIFO level via
// the Uart's per-channel on_tx_byte callback: a byte written to the
// shared port + currently-selected channel emerges on that channel's
// callback AND nowhere else.
//
// Test procedure (ZX Spectrum Next / VHDL oracle):
//   1. Install independent on_tx_byte sinks on UART 0 and UART 1.
//   2. Select channel 0 (port 0x153B = 0x00), write 0xAA to port 0x133B,
//      tick UART long enough for the byte to finish transmitting.
//      Expect: channel-0 sink saw 0xAA, channel-1 sink saw nothing.
//   3. Select channel 1 (port 0x153B = 0x40), write 0xBB, tick.
//      Expect: channel-1 sink saw 0xBB, channel-0 sink UNCHANGED
//      (still only 0xAA from step 2).
static void test_dual_05_channel_routing(Emulator& emu) {
    set_group("DUAL");
    fresh(emu);

    // Intercept TX byte emissions on each channel. The UART reset path
    // left the loopback-fallback branch in place (on_tx_byte is empty
    // by default); installing a handler preempts loopback for the
    // tested channel.
    //
    // NOTE: Emulator::init already wires uart_.on_tx_interrupt /
    // on_rx_interrupt on the top-level Uart (see emulator.cpp:1658),
    // but the per-channel on_tx_byte slot is NOT otherwise consumed.
    // We access channels via a non-const Uart& and install the sinks
    // directly; this is a test-only override limited to this suite.
    //
    // We can't use Uart::channel() const-accessor; use a raw cast via
    // a const_cast on the Uart reference since channels_ are private.
    // Instead, reach through a helper that touches the public surface:
    // the subsystem-tier uart_test does this via "Uart uart;" locally,
    // but we need the integration-tier Emulator-owned Uart. The
    // cleanest legal access is via the existing public `uart()` getter
    // plus a private-class override: since `UartChannel::on_tx_byte`
    // is a public std::function member (per uart.h:145), and Uart
    // exposes `const UartChannel& channel(int)`, we need a non-const
    // path. Add one via a local const_cast on the reference we already
    // own — well-defined because the underlying object IS non-const
    // (owned by the non-const Emulator we build here).
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

// DUAL-06 — joystick IO-mode UART RX multiplex.
//
// VHDL zxnext.vhd:3340-3341 multiplexes the UART 0 / UART 1 RX lines
// with the joystick-connector UART RX pin under NR 0x0B control:
//
//   uart0_rx <= joy_uart_rx when joy_iomode_uart_en='1'
//               and nr_0b_joy_iomode_0='0' else i_UART0_RX;
//   uart1_rx <= joy_uart_rx when joy_iomode_uart_en='1'
//               and nr_0b_joy_iomode_0='1' else pi_uart_rx;
//
// So when NR 0x0B bit 7 (iomode_en) = 1 AND bit 0 (iomode_0) = 0,
// UART 0's RX is driven from the joystick UART pin instead of the
// ESP pin. When bit 0 = 1, UART 1's RX is so driven. When bit 7 = 0,
// the mux is disabled and joystick UART RX goes nowhere.
//
// jnext models this at the Emulator tier via `Emulator::inject_joy_uart_rx`
// (see emulator.h "Joystick UART-RX injection" block), which consults
// iomode_en / iomode_0 and routes to uart_.inject_rx on the correct
// channel. This test exercises that routing.
//
// Test procedure (ZX Spectrum Next / VHDL oracle):
//   1. NR 0x0B = 0x80 (iomode_en=1, iomode_0=0) → joy→UART 0 route.
//      Drive joystick-UART RX with 0x77. Expect UART 0 RX FIFO to
//      receive the byte; UART 1 RX FIFO unchanged.
//   2. NR 0x0B = 0x81 (iomode_en=1, iomode_0=1) → joy→UART 1 route.
//      Drive joystick-UART RX with 0x55. Expect UART 1 RX FIFO to
//      receive the byte; UART 0 RX FIFO unchanged.
//   3. NR 0x0B = 0x00 (iomode_en=0) — mux disabled.
//      Drive joystick-UART RX with 0x33. Expect NEITHER UART to
//      receive the byte (both RX FIFOs quiescent).
static void test_dual_06_iomode_rx_mux(Emulator& emu) {
    set_group("DUAL");
    fresh(emu);

    // Step 1: iomode_en=1, iomode_0=0 → joy→UART 0 mux.
    // NR 0x0B write goes through the NR dispatch path at
    // emulator.cpp NR 0x0B handler (calls iomode_.set_nr_0b).
    emu.nextreg().write(0x0B, 0x80);              // iomode_en=1, iomode_0=0
    emu.inject_joy_uart_rx(0x77);

    // Read UART 0 RX FIFO via port 0x143B after selecting channel 0.
    emu.port().out(0x153B, 0x00);                 // select channel 0
    const uint8_t u0_step1 = emu.port().in(0x143B);
    // Select channel 1 and peek RX FIFO to make sure it's empty.
    emu.port().out(0x153B, 0x40);
    const bool u1_empty_step1 = emu.uart().channel(1).rx_empty();

    const bool step1_ok = (u0_step1 == 0x77) && u1_empty_step1;

    // Step 2: iomode_en=1, iomode_0=1 → joy→UART 1 mux.
    emu.nextreg().write(0x0B, 0x81);              // iomode_en=1, iomode_0=1
    emu.inject_joy_uart_rx(0x55);

    // UART 1 should have 0x55.
    emu.port().out(0x153B, 0x40);
    const uint8_t u1_step2 = emu.port().in(0x143B);
    // UART 0 should still be empty (earlier 0x77 was consumed in step 1).
    const bool u0_empty_step2 = emu.uart().channel(0).rx_empty();

    const bool step2_ok = (u1_step2 == 0x55) && u0_empty_step2;

    // Step 3: iomode_en=0 — mux disabled; byte is dropped.
    emu.nextreg().write(0x0B, 0x00);              // iomode_en=0
    emu.inject_joy_uart_rx(0x33);

    // Neither UART should have received anything.
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

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("UART Subsystem Integration Tests\n");
    std::printf("================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    // ── Wave C rows — WAVE C APPENDS TEST INVOCATIONS HERE ────────────

    // ── Wave D rows ───────────────────────────────────────────────────
    test_dual_05_channel_routing(emu);
    std::printf("  Group: DUAL (DUAL-05)         — done\n");

    test_dual_06_iomode_rx_mux(emu);
    std::printf("  Group: DUAL (DUAL-06)         — done\n");

    std::printf("\n================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown (live rows only):\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-14s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-14s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-14s %s\n", s.id.c_str(), s.reason.c_str());
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    // Suppress unused-function warning for skip() if no plan row F-skips.
    (void)&skip;

    return g_fail > 0 ? 1 : 0;
}
