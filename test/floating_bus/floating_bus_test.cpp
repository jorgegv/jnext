// Emulator Floating Bus Compliance Test Suite.
//
// 26 plan rows enumerated in
// doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md (plan landed 2026-04-23) plus
// 5 FB-HARNESS-NN smoke rows (Branch C) and 1 port-conflict neighbour
// (FB-3X, added during Phase 3 row-flips per Branch B reviewer note 2).
// All 26 plan rows are now live `check()` calls — Phase 3 (this commit)
// flipped them against the Branch A/B/C emulator + harness landed today.
//
// Open Q 2 resolved (plan §Open Questions): production
// `Emulator::floating_bus_read` (src/core/emulator.cpp:3090-3197) folds
// raster phase by `tstate_in_line % 8` and returns VRAM at offsets
// {2,3,4,5}. The 16-hc VHDL window is approximated 1:2 — phases 9/B/D/F
// land in the *first* half of each 8T window in the C++ model. Tests
// use `set_raster_position` (raw 3.5 MHz T-states) accordingly.
//
// Run: ./build/test/floating_bus_test

#include "core/clock.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include "video/ula.h"

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

[[maybe_unused]]
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

// ── Phase 3 fixture helpers (Branch C, plan §Phase 2 / §Open Q 2) ─────
//
// These helpers exist so the upcoming Phase 3 unskip work can flip the 26
// FB-NN rows mechanically. They are *test-side only* — no src/ change.
//
// Open Q 2 (plan doc §Open Questions): the production
// `Emulator::floating_bus_read` (src/core/emulator.cpp:3090-3197) folds
// raster phase by `tstate_in_line % 8` and selects pixel/attr at offsets
// {2, 3, 4, 5}. The VHDL oracle (zxula.vhd:319-340) folds by `hc(3:0)`
// over a 16-pixel-clock window and selects pixel/attr at hc phases
// {0x9, 0xB, 0xD, 0xF}. The 16-hc-vs-8T mapping has not yet been pinned
// to VHDL — Phase 3 will resolve it. So `set_raster_position` accepts a
// raw `tstate` parameter (3.5 MHz T-states from the start of the line)
// and a separate `set_raster_position_hc` overload that takes `hc`
// (7 MHz pixel clocks from the start of the line) so Phase 3 can sweep
// either interpretation against the same fixture.
//
// Idiom (per nmi_integration_test.cpp `fresh_cpu_at_c000`): re-init the
// Emulator at the start of every scenario to avoid carry-over of clock,
// CPU, NR, port-dispatch, or MMU state between rows. Helpers below
// expect a freshly-`init`ed Emulator on entry.

// Build a freshly-initialised headless Next emulator (mirrors the idiom
// in test/nmi/nmi_integration_test.cpp + test/ula/ula_integration_test.cpp).
// `type` lets caller pick 48K/128K/+3/Pentagon/Next per FB section.
[[maybe_unused]]
static bool fresh_emulator(Emulator& emu,
                           MachineType type = MachineType::ZXN_ISSUE2) {
    EmulatorConfig cfg;
    cfg.type = type;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Advance the emulator's master clock so that
//   (clock_.get() - frame_cycle_) / cpu_speed_divisor == line * tstates_per_line + tstate
// matching the geometry used by `Emulator::floating_bus_read`
// (src/core/emulator.cpp:3155-3167).
//
// Mechanism: `Clock` exposes only `tick(n)` and `reset()` — there is no
// direct setter for the cycle counter. We therefore compute the master
// cycle delta and feed it to `clock().tick()`. This advances *only* the
// clock counter; subsystems are not stepped, no scheduler events fire,
// and CPU state is unchanged. That is exactly what we want for
// `floating_bus_read` rows, which observe the raster phase as a pure
// function of `clock_.get() - frame_cycle_`.
//
// Pre-condition: caller invoked `fresh_emulator()` so frame_cycle_ == 0
// and clock_.get() == 0 (or at least <= the master-cycle target).
//
// Returns true on success, false if the requested raster position would
// require ticking backwards (i.e. clock has already advanced past the
// target — caller forgot to fresh_emulator()).
[[maybe_unused]]
static bool set_raster_position(Emulator& emu, int line, int tstate) {
    const auto& timing = emu.timing();
    const int divisor  = cpu_speed_divisor(emu.config().cpu_speed);

    // Target: (line * tstates_per_line + tstate) T-states since frame start,
    // converted back to master cycles. tstate is raw 3.5 MHz T-states
    // (Open Q 2: Phase 3 may need to sweep hc instead — see helper below).
    const uint64_t target_master =
        static_cast<uint64_t>(line) * timing.master_cycles_per_line
        + static_cast<uint64_t>(tstate) * static_cast<uint64_t>(divisor);

    const uint64_t now_master = emu.clock().get() - emu.current_frame_cycle();
    if (target_master < now_master) return false;
    emu.clock().tick(target_master - now_master);
    return true;
}

// Sister helper for the hc (7 MHz pixel-clock) interpretation of the
// VHDL `hc(3:0)` phase fold. 1 hc = 4 master cycles (= 0.5 T-state at
// 3.5 MHz CPU). Use this when Phase 3 sweeps the VHDL hc-window model.
// Same mechanism + preconditions as `set_raster_position`.
[[maybe_unused]]
static bool set_raster_position_hc(Emulator& emu, int line, int hc) {
    const auto& timing = emu.timing();
    const uint64_t target_master =
        static_cast<uint64_t>(line) * timing.master_cycles_per_line
        + static_cast<uint64_t>(hc) * 4ULL;

    const uint64_t now_master = emu.clock().get() - emu.current_frame_cycle();
    if (target_master < now_master) return false;
    emu.clock().tick(target_master - now_master);
    return true;
}

// Execute a single `IN A,(0xFF)` instruction at PC=0x8000 and return A.
// Mirrors the integration-tier idiom from test/nmi/nmi_integration_test.cpp:
// poke the opcode bytes into RAM via `mmu().write`, point PC at them,
// `cpu().execute()` one instruction, then read back the A register.
//
// Opcode: DB FF — 11 T-states on a 48K/128K, 2-byte instruction. The
// port operand of `IN A,(n)` puts (A << 8) | n on the address bus per
// Z80 semantics; for floating-bus tests we only care about A8-A15
// inasmuch as port 0xFF is unmapped on 48K/128K so the read falls
// through `port_dispatch.set_default_read` → `Emulator::floating_bus_read`
// (src/core/emulator.cpp:184-186).
[[maybe_unused]]
static uint8_t cpu_in_a_FF(Emulator& emu) {
    emu.mmu().write(0x8000, 0xDB);  // IN A,(n)
    emu.mmu().write(0x8001, 0xFF);  // n = 0xFF
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000;
    // A's high byte ends up on A8-A15; set A=0 so the dispatched port is
    // a clean 0x00FF (caller can override via the helper below if needed).
    regs.AF = static_cast<uint16_t>(regs.AF & 0x00FF);
    emu.cpu().set_registers(regs);
    emu.cpu().execute();
    return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
}

// Execute a single `IN A,(C)` instruction with BC=0x0FFD and return A.
// Used by Branch B's +3 port-0x0FFD rows. The +3 floating-bus surface
// at 0x0FFD is decoded as a 16-bit port (zxnext.vhd:2589 + 4517), so
// `IN A,(n)` (which only carries 8 address bits) cannot reach it — the
// 16-bit `IN r,(C)` form is required.
//
// Opcode: ED 78 — `IN A,(C)`. Port operand = BC.
[[maybe_unused]]
static uint8_t cpu_in_a_0FFD(Emulator& emu) {
    emu.mmu().write(0x8000, 0xED);
    emu.mmu().write(0x8001, 0x78);  // IN A,(C)
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000;
    regs.BC = 0x0FFD;               // B=0x0F (high), C=0xFD (low)
    emu.cpu().set_registers(regs);
    emu.cpu().execute();
    return static_cast<uint8_t>(emu.cpu().get_registers().AF >> 8);
}

// Bypass-the-CPU helper: drives Port::in() directly, useful for
// unit-style rows that want to assert the port-dispatch surface without
// budgeting CPU T-states (which themselves advance the clock and
// therefore the raster phase). Keeps `cpu_in_a_*` for end-to-end rows
// where the CPU edge is part of what we are pinning.
[[maybe_unused]]
static uint8_t read_port_default(Emulator& emu, uint16_t port) {
    return emu.port().in(port);
}

// Compute the VRAM-bank-5 RAM offset for a given pixel line+col matching
// the formula at src/core/emulator.cpp:3182-3189. Returns the raw
// `Ram::write` index for the *pixel* byte at (pixel_line, char_col).
// Bank 5 sits at SRAM offset 10*0x2000 = 0x14000; pixel address scheme
// is the standard ZX Spectrum display-file layout.
static uint32_t vram_pixel_ram_offset(int pixel_line, int char_col) {
    const int y = pixel_line;
    const uint16_t pixel_addr = 0x4000
        | ((y & 0xC0) << 5)
        | ((y & 0x07) << 8)
        | ((y & 0x38) << 2)
        | (char_col * 2);
    return static_cast<uint32_t>(pixel_addr) - 0x4000u + 10u * 0x2000u;
}

// Same for the attribute byte at (pixel_line, char_col).
// attr_addr = 0x5800 + (line/8)*32 + char_col*2 per emulator.cpp:3189.
static uint32_t vram_attr_ram_offset(int pixel_line, int char_col) {
    const uint16_t attr_addr = 0x5800u + (pixel_line / 8) * 32u + char_col * 2u;
    return static_cast<uint32_t>(attr_addr) - 0x4000u + 10u * 0x2000u;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════
// Section 1 — Border-phase read returns 0xFF (48K/128K)
// VHDL: zxula.vhd:312-316 (border holds floating_bus_r at 0xFF);
//       zxula.vhd:414-416,573 (border_active_v + final else arm).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §1
// ══════════════════════════════════════════════════════════════════════

static void test_section1_border(void) {
    set_group("FB-1-Border");

    // FB-01 — re-home of S10.01.
    // 48K, line in V-border; port 0xFF read expected 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // Line 32 sits in the V-border (line < 64 → border early-return
        // arm at emulator.cpp:3169).
        set_raster_position(emu, 32, 64);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-01",
              "48K V-border (line=32) port 0xFF read returns 0xFF "
              "(zxula.vhd:312-316,414,573)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-02 — neighbour. 48K, H-blank inside V-active; expected 0xFF.
    // Per zxula.vhd:316,416 border_active_ula = i_hc(8) OR border_active_v;
    // emulator models H-blank via tstate_in_line >= 128 (emulator.cpp:3169).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // Line 100 = active display; tstate 150 > 128 = H-blank window.
        set_raster_position(emu, 100, 150);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-02",
              "48K H-blank inside V-active (line=100, t=150) port 0xFF=0xFF "
              "(zxula.vhd:316,416,573)",
              v == 0xFF, fmt("v=0x%02X", v));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 2 — Active-display capture phases
// VHDL: zxula.vhd:319-340 (hc(3:0) case → floating_bus_r load phases
//       0x9/0xB/0xD/0xF from i_ula_vram_d; phase 0x1 resets).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §2
//
// Open Q 2 (resolved): production folds raster phase by `tstate_in_line
// % 8` and selects {pixel, attr, pixel+1, attr+1} at offsets {2, 3, 4, 5}.
// Tests use `set_raster_position` with raw 3.5 MHz T-states; phase
// selection is via the chosen `tstate % 8`.
// ══════════════════════════════════════════════════════════════════════

static void test_section2_capture_phases(void) {
    set_group("FB-2-Capture");

    // FB-2A — VHDL hc phase 0x9 ↔ host T%8=2 → pixel byte.
    // Place raster at line=64 (top of active), col=8 (char_col=4 chars in
    // = pixel column 32; well clear of left border at col<32). tstate=34
    // → tstate%8 = 2 → pixel byte arm.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 34;          // tstate%8=2, char_col=4
        const int pixel_line = LINE - 64;           // 36
        const int char_col   = TSTATE / 8;          // 4
        const uint8_t MARKER = 0xA5;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2A",
              "48K active display, T%8=2 → pixel byte from VRAM "
              "(zxula.vhd:325-327)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }

    // FB-2B — VHDL hc phase 0xB ↔ host T%8=3 → attr byte.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 35;          // tstate%8=3, char_col=4
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x5C;
        emu.ram().write(vram_attr_ram_offset(pixel_line, char_col), MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2B",
              "48K active display, T%8=3 → attribute byte from VRAM "
              "(zxula.vhd:329-330)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }

    // FB-2C — VHDL hc phase 0xD ↔ host T%8=4 → pixel+1 byte.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 36;          // tstate%8=4, char_col=4
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x3E;
        // pixel+1 → write at pixel offset + 1
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col) + 1, MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2C",
              "48K active display, T%8=4 → pixel+1 byte from VRAM "
              "(zxula.vhd:332-333)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }

    // FB-2D — VHDL hc phase 0xF ↔ host T%8=5 → attr+1 byte.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 37;          // tstate%8=5, char_col=4
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x77;
        emu.ram().write(vram_attr_ram_offset(pixel_line, char_col) + 1, MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2D",
              "48K active display, T%8=5 → attr+1 byte from VRAM "
              "(zxula.vhd:335-336)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }

    // FB-2E — reset/idle phase. Plan picks T%8=0 (production default arm
    // at emulator.cpp:3196 returns 0xFF for {0,1,6,7}). VHDL calls this
    // hc(3:0)=0x1 (reset). The 8T model collapses 8 of the 16 hc phases
    // into the "idle" arm; FB-2E pins one representative.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 32;          // tstate%8=0
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        // Seed VRAM with a non-FF byte so a bug returning the pixel/attr
        // byte instead of 0xFF would witness as a fail.
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x33);
        emu.ram().write(vram_attr_ram_offset(pixel_line, char_col),  0x33);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2E",
              "48K active display, idle phase (T%8=0) returns 0xFF "
              "(zxula.vhd:321-323,573)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-2F — scanline above active display window (line < 64).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // Line 50 < 64 → above-active border. tstate=20 (in pixel-fetch
        // window if it were active) so the row pins the V-axis gate, not
        // the H-blank gate.
        set_raster_position(emu, 50, 20);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-2F",
              "48K above-active V-border (line=50) returns 0xFF "
              "(zxula.vhd:414-416,573)",
              v == 0xFF, fmt("v=0x%02X", v));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 3 — +3 floating-bus paths: port 0xFF vs port 0x0FFD
// VHDL: zxula.vhd:573 (bit-0 OR i_timing_p3; border fallback arm);
//       zxnext.vhd:4513 (port 0xFF hard-forced to 0xFF on +3);
//       zxnext.vhd:4517 (port_p3_floating_bus_dat + port_7ffd_locked);
//       zxnext.vhd:2589 (port 0x0FFD decode gated by p3_timing_hw_en +
//       port_p3_floating_bus_io_en);
//       zxnext.vhd:4498-4509 (p3_floating_bus_dat latch).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §3
//
// FOLLOW-UP: VHDL derives p3_timing_hw_en from NR 0x03 (zxnext.vhd:5774);
// Branch B uses MachineType — minor drift documented in 2026-04-25
// reviewer note. A Next-base machine that flips NR 0x03 to +3 timing
// would not see port 0x0FFD as a floating-bus surface in our
// implementation; on real hardware it would. Not exercised by Phase 3
// rows; revisit when NR 0x03 runtime machine-type commit lands.
// ══════════════════════════════════════════════════════════════════════

static void test_section3_p3_paths(void) {
    set_group("FB-3-P3Paths");

    // FB-03 — re-home of S10.05, re-scoped. +3 port 0xFF → 0xFF (port
    // 0xFF hard-forced per zxnext.vhd:4513; Branch A wired the gate).
    // Place at active capture phase to prove the per-machine gate (not
    // border early-return) is what forces 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        const int LINE = 100, TSTATE = 34;          // active capture, T%8=2
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        // Seed VRAM so a regression that bypasses the +3 gate would emit
        // this marker instead of 0xFF.
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x42);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-03",
              "+3 port 0xFF in active capture phase hard-forced to 0xFF "
              "(zxnext.vhd:4513)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-03a — +3 port 0x0FFD active-display VRAM-byte arm + bit-0 force.
    // Verify9-memory class-(c) → class-(a) fix: pre-fix the handler
    // unconditionally returned `p3_floating_bus_dat | 0x01` (border arm)
    // in all raster phases. VHDL zxula.vhd:573 active arm fires when
    // `border_active_ula='0' AND floating_bus_en='1'`, returning
    // `floating_bus_r | i_timing_p3` (= VRAM pixel byte | 0x01 on +3).
    // Seed both the latch (0xA4 → would yield 0xA5 under old behaviour)
    // AND the VRAM pixel byte (0x42 → expected 0x43 under VHDL): the
    // handler must now return 0x43, proving the active arm wins.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        // Seed the contended-CPU latch (Mmu::write to slot 1 updates
        // p3_floating_bus_dat_ on contended pages).
        emu.mmu().write(0x4000, 0xA4);
        // Active display + T%8=2 (pixel byte 0). At LINE=100, TSTATE=34 →
        // pixel_line=36, char_col=4 → pixel_addr per VHDL display layout.
        const int LINE = 100, TSTATE = 34;
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x42);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-03a",
              "+3 port 0x0FFD active-display VRAM byte | 0x01 → 0x43 "
              "(zxula.vhd:573 active arm + zxnext.vhd:4517)",
              v == 0x43, fmt("v=0x%02X (latch=0xA4, pixel=0x42)", v));
    }

    // FB-04 — re-home of S10.06, re-scoped. +3 port 0xFF at border → 0xFF.
    // Border early-return AND the per-machine gate should both yield 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        // Seed the +3 latch with a recognisable byte to prove the shadow
        // is NOT exposed via port 0xFF (it lives only on port 0x0FFD).
        emu.mmu().write(0x4000, 0xA5);
        set_raster_position(emu, 32, 64);          // V-border
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-04",
              "+3 port 0xFF at border ignores p3_floating_bus_dat shadow → 0xFF "
              "(zxnext.vhd:4513)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-04a — +3 port 0x0FFD border fallback via p3_floating_bus_dat.
    // Border arm of zxula.vhd:573 substitutes i_p3_floating_bus (the
    // contended-write latch). Branch B hands it back via the same
    // expression as FB-03a.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0xA5);             // bit 0 already set
        set_raster_position(emu, 32, 64);          // V-border
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-04a",
              "+3 port 0x0FFD border fallback via p3_floating_bus_dat → 0xA5 "
              "(zxula.vhd:573 + zxnext.vhd:4498-4509,4517)",
              v == 0xA5, fmt("v=0x%02X", v));
    }

    // FB-04b — GH #112: the bit-0 force at `zxula.vhd:573` is scoped to
    // the ACTIVE-DISPLAY waveform only; the border waveform is raw.
    //
    // :573 is a conditional signal assignment (LRM 10.5.3) — an ordered
    // chain of independent waveforms:
    //     (floating_bus_r(7 downto 1) & (floating_bus_r(0) or i_timing_p3))
    //         when (border_active_ula='0' and floating_bus_en='1')
    //     else i_p3_floating_bus when i_timing_p3='1'
    //     else X"FF";
    // `or i_timing_p3` lives inside the parenthesised concatenation of
    // waveform 1. Waveform 2 is the bare signal `i_p3_floating_bus`,
    // wired 1:1 from `p3_floating_bus_dat` (zxnext.vhd:4478), whose sole
    // driver (:4499-4508) latches cpu_di / cpu_do verbatim — no force.
    //
    // FB-03a already pins waveform 1 and FB-3X/FB-3F/FIX-FB-EFFLOCK-01
    // pin waveform 2, but none of them pins the CONTRAST, which is the
    // entire semantic content of the parenthesisation. FB-04a cannot:
    // it seeds 0xA5, whose bit 0 is already 1, so it reads identically
    // whether or not the force is (wrongly) applied.
    //
    // Method: ONE emulator, both source bytes seeded with 0x42 (bit 0
    // CLEAR, so the force is observable). Read at the border → waveform
    // 2 → 0x42 raw. Tick forward into active display → waveform 1 →
    // 0x43. Same byte in, two different bytes out: the force is present
    // in exactly one arm.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        // Waveform-2 source: the contended-CPU latch (bank 5 is
        // contended on +3 timing → mmu().write updates it).
        emu.mmu().write(0x4000, 0x42);
        // Waveform-1 source: the VRAM pixel byte the ULA fetches at
        // (LINE=100, TSTATE=34) → pixel_line=36, char_col=4.
        const int LINE = 100, TSTATE = 34;
        emu.ram().write(vram_pixel_ram_offset(LINE - 64, TSTATE / 8), 0x42);

        // Border first (set_raster_position only ticks FORWARD).
        set_raster_position(emu, 32, 64);          // V-border
        const uint8_t v_border = read_port_default(emu, 0x0FFD);
        // Then into the active-display capture phase.
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v_active = read_port_default(emu, 0x0FFD);

        check("FB-04b",
              "+3 port 0x0FFD bit-0 force is scoped to the active-display "
              "arm only: same 0x42 source byte reads 0x42 at border (raw "
              "i_p3_floating_bus) and 0x43 in active display "
              "(floating_bus_r(0) or i_timing_p3) "
              "(zxula.vhd:573 + zxnext.vhd:4478, 4499-4508, 4517)",
              v_border == 0x42 && v_active == 0x43,
              fmt("border=0x%02X (want 0x42) active=0x%02X (want 0x43); "
                  "pre-GH#112-fix border would yield 0x43",
                  v_border, v_active));
    }

    // FB-3A — +3 port 0x0FFD with port_7ffd_locked=1 → 0xFF.
    // Lock paging by writing bit 5 of port 0x7FFD via the dispatcher.
    // (Direct mmu_.map_128k_bank() works too but going through the port
    // pinces the integration path.)
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0x42);             // seed latch
        emu.mmu().map_128k_bank(0x20);             // bit 5 → paging_locked
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3A",
              "+3 port 0x0FFD + port_7ffd_locked=1 → 0xFF "
              "(zxnext.vhd:4517)",
              v == 0xFF,
              fmt("v=0x%02X locked=%d", v, emu.mmu().paging_locked() ? 1 : 0));
    }

    // FB-3B — +3 port 0x0FFD with port_p3_floating_bus_io_en=0 (NR 0x82
    // bit 4 cleared) → decode blocked → 0xFF.
    //
    // GH #111 rewrite (owner-approved, 2026-07-26): a blocked decode
    // means `port_p3_float = '0'` (zxnext.vhd:2589 with the
    // port_p3_floating_bus_io_en AND-term, :2403), so the read strobe
    // `port_p3_float_rd <= iord and port_p3_float` (:2716) never
    // asserts, `port_internal_rd_response` stays low (:2803-2806 — no
    // other strobe decodes 0x0FFD), and the cpu_di IORQ mux delivers
    // its unconditional default `cpu_di <= X"FF"` (:1877). The X"00"
    // at :2814 is only the wired-OR contribution into port_rd_dat
    // (:2837), never CPU-visible without its own strobe. The previous
    // 0x00 expectation encoded that misreading.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0x42);             // seed latch (irrelevant)
        // NR 0x82 reset default = 0xFF; clear bit 4 only.
        emu.nextreg().write(0x82, 0xEF);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3B",
              "+3 port 0x0FFD + NR 0x82 b4=0 → decode blocked → 0xFF "
              "(zxnext.vhd:2403, 2589, 2716, 2803-2806, 1877)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-3C — 48K port 0x0FFD → 0xFF (decode blocked by p3_timing_hw_en;
    // no strobe → no internal response → cpu_di default X"FF", GH #111).
    // Note port_7ffd DOES decode 0x0FFD on non-+3 timing (:2593) but is
    // write-only — it contributes no read strobe.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3C",
              "48K port 0x0FFD → 0xFF (p3_timing_hw_en gate blocks decode) "
              "(zxnext.vhd:2589, 2716, 2803-2806, 1877)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-3D — 128K port 0x0FFD → 0xFF (same blocked-decode path as
    // FB-3C, GH #111).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX128K);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3D",
              "128K port 0x0FFD → 0xFF (p3_timing_hw_en gate blocks decode) "
              "(zxnext.vhd:2589, 2716, 2803-2806, 1877)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

    // FB-3E RETIRED 2026-05-04: standalone Pentagon machine type dropped
    // (Wave 0.3 follow-up). FB-3D / FB-3F still cover the same gate path
    // for 128K and Next-base.

    // FB-3F — Next-base port 0x0FFD → DECODED (post-D3F-01 fix).
    //
    // Updated for D3F-01 (port_p3_float gate keys on machine_timing_,
    // not config_.type). Next-base init seeds tim_sel=0x03 (+3 timing,
    // VHDL :1099 default) — so port_p3_float IS decoded on a fresh
    // Next emulator. This closes the pre-existing "FOLLOW-UP" note
    // (see Section 3 prologue). Read at the reset raster position
    // (border) → `zxula.vhd:573` second waveform → the raw latch.
    //
    // GH #112 rewrite (2026-07-26): the expected value was 0x01 — the
    // reset-default latch 0x00 with jnext's spurious border-arm bit-0
    // force applied. The VHDL applies no such force to the second
    // waveform (see the FB-3X / FB-04b commentary), so the latch comes
    // back verbatim. This row was NOT enumerated in GH #112's issue
    // text — the issue named only FIX-FB-EFFLOCK-01 and FB-3X — but it
    // is load-bearing on the same force and the fix breaks it, so it
    // had to be rewritten too. The latch is now SEEDED with 0x42
    // rather than left at its 0x00 reset default: post-fix an unseeded
    // row would assert 0x00, which a hardcoded-zero regression would
    // also satisfy — the row must be able to fail for its own reason.
    // 0x42 also stays distinct from the blocked-decode value 0xFF
    // (GH #111), which is what this row exists to rule out.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().write(0x4000, 0x42);             // latch = 0x42
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3F",
              "Next port 0x0FFD decoded post-D3F-01 (machine_timing_ keyed) "
              "→ raw border-arm p3_floating_bus_dat = 0x42, NOT the "
              "blocked-decode 0xFF "
              "(zxnext.vhd:2589 + :1099 default tim_sel=011; "
              "zxula.vhd:573 second arm)",
              v == 0x42,
              fmt("v=0x%02X (want 0x42; blocked decode would give 0xFF, "
                  "pre-GH#112-fix 0x43)", v));
    }

    // FIX-FB-EFFLOCK-01 — testcov-memory follow-up coverage gap (#4 in the
    // reviewer report): Verify8 A4 (+3 floating-bus port read uses
    // `effective_paging_locked` instead of raw `paging_locked`). The +3
    // 0x0FFD handler at emulator.cpp returns 0xFF when paging is
    // EFFECTIVELY locked. Pre-fix it called `paging_locked()` (raw
    // 7FFD(5) mirror); post-fix calls `effective_paging_locked()` which
    // composes the full VHDL :3769 expression (raw lock AND NOT
    // pentagon_1024_en).
    //
    // `pentagon_1024_en()` depends on `nr_8f_mode_=11` AND `EFF7(2)=0`,
    // independent of machine_type. Even on a +3 emulator, firmware can
    // configure those bits — making the path observable. Sequence:
    //   1. +3 emulator at reset.
    //   2. Lock paging via 7FFD(5) (raw lock asserted).
    //   3. Confirm: read NR readback / direct accessor — paging_locked=true.
    //   4. Enable Pentagon-1024: NR 0x8F = 0x03; EFF7(2)=0 (default).
    //   5. effective_paging_locked() should be FALSE now.
    //   6. NR 0x82 bit 4 must be set (default 0xFF) so port_p3_floating_bus_io_en=1.
    //   7. Read port 0x0FFD — pre-fix returns 0xFF (raw lock asserted),
    //      post-fix returns the floating-bus byte. The pre-fix vs
    //      post-fix split hinges on `effective_paging_locked()` —
    //      observable via the port-read result not being 0xFF.
    //
    // GH #112 rewrite (owner-approved, 2026-07-26): the expected value
    // was 0x43 (`latch | 0x01`). That encoded the border-arm bit-0
    // force jnext used to apply, which the VHDL does NOT have.
    // `zxula.vhd:573` is a conditional signal assignment (LRM 10.5.3);
    // the `or i_timing_p3` term lives inside the parenthesised
    // concatenation of the FIRST waveform (active display) only. The
    // second waveform is the bare signal `i_p3_floating_bus`, driven
    // 1:1 (zxnext.vhd:4478) from `p3_floating_bus_dat`, whose only
    // driver (:4499-4508) latches cpu_di / cpu_do verbatim. The read
    // here happens at the reset raster position (border), so the
    // border arm is selected and the raw latch 0x42 must come back.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        // Seed the contended-CPU latch (Mmu::p3_floating_bus_dat_) with a
        // distinctive non-0xFF byte. The latch updates on contended
        // memory writes — emu.mmu().write(0x4000, 0x42) lands on bank 5
        // (contended on +3 → latch updates) per the verify9 wiring.
        emu.mmu().write(0x4000, 0x42);
        // Lock paging via 7FFD(5).
        emu.mmu().map_128k_bank(0x20);
        const bool raw_locked = emu.mmu().paging_locked();
        // Enable Pentagon-1024: NR 0x8F=0x03 (mode "11"), EFF7(2)=0.
        emu.mmu().write_nr_8f(0x03);
        emu.mmu().write_port_eff7(0x00);  // clear EFF7(2) explicitly
        const bool eff_locked = emu.mmu().effective_paging_locked();
        // Read port 0x0FFD. NR 0x82 reset default 0xFF (bit 4 set →
        // port_p3_floating_bus_io_en=1 — decode active).
        const uint8_t v = read_port_default(emu, 0x0FFD);
        // Pre-fix: gate uses raw paging_locked → returns 0xFF.
        // Post-fix: gate uses effective_paging_locked → returns the raw
        //          latch 0x42 via the border-arm fallback
        //          (set_raster_position not called → border arm).
        check("FIX-FB-EFFLOCK-01",
              "+3 port 0x0FFD with paging-locked but Pentagon-1024 "
              "override drops effective_paging_locked → returns the raw "
              "border-arm latch 0x42 (NOT 0xFF) — VHDL zxnext.vhd:3769, "
              "4517 + zxula.vhd:573 second arm; verify8 A4 / GH #112",
              raw_locked && !eff_locked && v == 0x42,
              fmt("raw_locked=%d eff_locked=%d v=0x%02X "
                  "(exp 1/0/0x42; pre-efflock-fix would yield 0xFF, "
                  "pre-GH#112-fix 0x43)",
                  raw_locked, eff_locked, v));
    }

    // FB-3X — Branch B reviewer note 2: pin that port 0x0FFD reads on +3
    // dispatch to the dedicated 0x0FFD handler and do NOT accidentally
    // fall through to the 0x7FFD bank-switching surface.
    //
    // Method: seed p3_floating_bus_dat with a unique value (0x42) and
    // read 0x0FFD at the reset raster position (border) → the
    // `zxula.vhd:573` SECOND waveform (`i_p3_floating_bus`) is selected
    // and must hand the latch back verbatim: 0x42. The value is a
    // fingerprint of the 0x0FFD handler: the 0x7FFD surface is
    // write-only (zxnext.vhd:2593) and would leave the read at the
    // dispatcher default 0xFF, and no plausible 0x7FFD bank/screen
    // artifact equals 0x42.
    //
    // GH #112 rewrite (owner-approved, 2026-07-26): expected value was
    // 0x43 and the comment cited `zxula.vhd:573`'s bit-0 force as
    // covering the border arm. It does not — :573 is a conditional
    // signal assignment (LRM 10.5.3) and `(floating_bus_r(0) or
    // i_timing_p3)` is parenthesised inside the FIRST waveform only.
    // The border arm is the bare signal `i_p3_floating_bus`
    // (zxnext.vhd:4478 → the raw cpu_di/cpu_do latch at :4499-4508).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0x42);             // latch = 0x42
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3X",
              "+3 port 0x0FFD dispatches to 0x0FFD handler not 0x7FFD "
              "(specificity: mask 0xF003 > 0x8003) → raw border-arm latch "
              "0x42 (zxula.vhd:573 second arm + zxnext.vhd:4478, 4499-4508; "
              "port_dispatch.cpp most-specific-match)",
              v == 0x42, fmt("v=0x%02X (pre-GH#112-fix would yield 0x43)", v));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 4 — Per-machine ULA-vs-0xFF selection (port 0xFF)
// VHDL: zxnext.vhd:4513 (only 48K+128K timings deliver ula_floating_bus
//       onto port 0xFF; +3/Pentagon/Next force 0xFF).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §4
// ══════════════════════════════════════════════════════════════════════

static void test_section4_per_machine(void) {
    set_group("FB-4-MachineSel");

    // FB-4A — 128K active capture → VRAM byte reaches port 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX128K);
        const int LINE = 100, TSTATE = 34;          // T%8=2 (pixel arm)
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x5A;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-4A",
              "128K active capture → ULA floating bus reaches port 0xFF (0x5A) "
              "(zxnext.vhd:4513)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }

    // FB-4B RETIRED 2026-05-04: standalone Pentagon machine type dropped
    // (Wave 0.3 follow-up). FB-4C still covers the same gate path
    // (non-48K/128K timing → port 0xFF hard-forced 0xFF) on Next-base.

    // FB-4C — Next active → port 0xFF hard-forced 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZXN_ISSUE2);
        const int LINE = 100, TSTATE = 34;
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x5A);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-4C",
              "Next-base active capture → port 0xFF hard-forced 0xFF "
              "(zxnext.vhd:4513)",
              v == 0xFF, fmt("v=0x%02X", v));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 5 — Port 0xFF read path wiring (default-read handler)
// VHDL: zxnext.vhd:2713 (port_ff_rd unconditional decode);
//       zxnext.vhd:2813 (read mux: Timex vs ULA vs 0x00).
// Host: src/core/emulator.cpp:194 binds floating_bus_read as the
//       port-dispatch default read handler (port 0xFF is unmapped on
//       48K/128K → falls through).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §5
// ══════════════════════════════════════════════════════════════════════

static void test_section5_port_ff_wiring(void) {
    set_group("FB-5-Wiring");

    // FB-06 — re-home of S10.07. 48K IN A,(0xFF) at border → 0xFF
    // via the full CPU + port-dispatch path (not a direct
    // floating_bus_read() call) so a regression in port_.set_default_read
    // would surface here.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // cpu_in_a_FF executes 11 T-states (DB FF). Fresh clock at line 0
        // (V-border) so the read takes the border early-return arm.
        const uint8_t a = cpu_in_a_FF(emu);
        check("FB-06",
              "48K CPU IN A,(0xFF) at border returns 0xFF via "
              "port_dispatch.set_default_read (zxnext.vhd:2713,2813)",
              a == 0xFF, fmt("a=0x%02X", a));
    }

    // FB-5A — neighbour. 48K IN A,(0xFF) in active capture → VRAM byte.
    // Approach: bypass the (uncertain) per-CPU-cycle port-sample timing
    // of the FUSE Z80 core by saturating the entire pixel-fetch window
    // (T-states 0..127) of one active line with the SAME marker byte.
    // Whichever T%8 phase the IN's port sample lands on, the read either
    // returns the marker (phases 2/3/4/5) or 0xFF (phases 0/1/6/7).
    // We pin raster *after* the IN's leading 7 T-states (DB+FF fetches)
    // so the actual port sample lands inside the pixel-fetch window,
    // then assert the read is the marker.
    //
    // To make the test deterministic regardless of FUSE Z80 sample
    // timing, set the raster position so the port sample ends up at a
    // T%8 ∈ {2,3,4,5} phase. Empirically `set_raster_position(100, 64)`
    // followed by cpu_in_a_FF lands at a stable phase; we verify post
    // hoc that the read returned the seeded marker.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // Saturate ALL pixel + attr bytes for every char_col on line 100
        // with the same marker. Whichever (char_col, phase) the IN samples
        // in the {2,3,4,5} arms, the byte is the same.
        const int LINE = 100;
        const int pixel_line = LINE - 64;
        const uint8_t MARKER = 0xC3;
        for (int cc = 0; cc < 32; ++cc) {
            emu.ram().write(vram_pixel_ram_offset(pixel_line, cc),     MARKER);
            emu.ram().write(vram_pixel_ram_offset(pixel_line, cc) + 1, MARKER);
            emu.ram().write(vram_attr_ram_offset (pixel_line, cc),     MARKER);
            emu.ram().write(vram_attr_ram_offset (pixel_line, cc) + 1, MARKER);
        }
        // Position raster early enough that the IN's leading fetches +
        // contention land the port sample inside the pixel-fetch window
        // (tstate < 128). Choose tstate=20 to leave ample slack against
        // contention adjustments (worst-case ~+12 T per access).
        set_raster_position(emu, LINE, 20);
        const uint8_t a = cpu_in_a_FF(emu);
        // The post-IN raster position tells us which T%8 phase we sampled.
        // Phases {2,3,4,5} → marker; phases {0,1,6,7} → 0xFF (default arm).
        const int post_line  = emu.current_scanline();
        const int post_tcyc  = static_cast<int>(
            (emu.clock().get() - emu.current_frame_cycle())
            / cpu_speed_divisor(emu.config().cpu_speed));
        const int post_tline = post_tcyc % emu.timing().tstates_per_line;
        check("FB-5A",
              "48K CPU IN A,(0xFF) in active line 100 sees VRAM marker 0xC3 "
              "(VRAM saturated for all char_col; the IN's port sample lands "
              "inside the pixel-fetch window; zxnext.vhd:2713,2813; "
              "emulator.cpp:3090-3197)",
              a == MARKER,
              fmt("a=0x%02X expected=0x%02X post_line=%d post_tline=%d phase=%d",
                  a, MARKER, post_line, post_tline, post_tline % 8));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 6 — NR 0x08 Timex override + port_ff_io_en gate
// VHDL: zxnext.vhd:2813 (three-term AND for the Timex arm);
//       zxnext.vhd:5180 (nr_08_port_ff_rd_en <= nr_wr_dat(2));
//       zxnext.vhd:2397 (port_ff_io_en <= internal_port_enable(0));
//       zxnext.vhd:3630 (port_ff_dat_tmx <= port_ff_reg);
//       zxnext.vhd:1118 (nr_08_port_ff_rd_en reset default '0').
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §6
// ══════════════════════════════════════════════════════════════════════

static void test_section6_nr08_override(void) {
    set_group("FB-6-NR08");

    // FB-07 — re-home of S10.08. NR 0x08 bit 2 set + port 0xFF write →
    // subsequent read returns the Timex register (port_ff_reg shadow,
    // mirrored in Ula::screen_mode_reg_).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // NR 0x82 default 0xFF (bit 0 set → port_ff_io_en=1). Set NR 0x08
        // bit 2 to enable the Timex arm, then write port 0xFF = 0x02
        // (mode 010 = HI_COLOUR; canonical so no unknown-mode warn).
        emu.nextreg().write(0x08, 0x14);            // bit 4 (default) + bit 2
        // Write port 0xFF — gated by port_ff_io_en (NR 0x82 b0). Use the
        // dispatcher to pin the full integration path.
        emu.port().out(0x00FF, 0x02);
        set_raster_position(emu, 32, 64);           // border (so floating-bus arm
                                                     // would yield 0xFF)
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-07",
              "48K NR 0x08 b2=1 + port 0xFF write 0x02 → read returns 0x02 "
              "(Timex arm wins; zxnext.vhd:2813,5180,3630)",
              v == 0x02, fmt("v=0x%02X", v));
    }

    // FB-6A — reset state NR 0x08=0 → floating-bus wins (port 0xFF write
    // does not influence read). Pins reset default nr_08_port_ff_rd_en=0
    // (zxnext.vhd:1118).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // NR 0x08 reset default has bit 2 = 0 (only bit 4 set).
        emu.port().out(0x00FF, 0x02);               // write Timex reg (canonical mode 010 = HI_COLOUR; no unknown-mode warn)
        set_raster_position(emu, 32, 64);           // border
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-6A",
              "48K reset state NR 0x08 b2=0 → border read returns 0xFF "
              "(floating-bus arm wins; zxnext.vhd:1118,2813,5180)",
              v == 0xFF,
              fmt("v=0x%02X nr_08_b2=%d", v, (emu.nextreg().cached(0x08) >> 2) & 1));
    }

    // FB-6B — NR 0x08 b2=1 + clear NR 0x82 b0 (port_ff_io_en=0) → Timex
    // AND-term collapses → floating-bus arm takes over. The port 0xFF
    // *write* is also gated by port_ff_io_en (emulator.cpp:1329), so we
    // perform the write first while io_en=1, THEN clear bit 0, then read.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        emu.nextreg().write(0x08, 0x14);            // bit 4 + bit 2
        emu.port().out(0x00FF, 0x02);               // seed Timex reg while io_en=1 (mode 010 = HI_COLOUR; canonical so no unknown-mode warn)
        emu.nextreg().write(0x82, 0xFE);            // clear NR 0x82 b0 only
        set_raster_position(emu, 32, 64);           // border
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-6B",
              "48K NR 0x08 b2=1 + NR 0x82 b0=0 → Timex arm collapses → 0xFF "
              "(zxnext.vhd:2397,2813)",
              v == 0xFF,
              fmt("v=0x%02X nr_82=0x%02X screen_mode=0x%02X",
                  v, emu.nextreg().cached(0x82),
                  emu.ula().get_screen_mode_reg()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 7 — D3F follow-up: port-handler machine_timing gates
// (Task 2 D3 V24-MEM-01 reviewer follow-ups; commit b9ea1c1e)
//
// Three port handlers were flagged as still keying on `config_.type`
// (typ_sel axis) when the VHDL gate keys on `machine_timing_*`
// (tim_sel axis). Same family as V24-MEM-01. Pre-fix a user-written
// NR 0x03 with `tim_sel != typ_sel` would silently mis-decode the
// port; post-fix the two axes are independent.
//
// VHDL anchors:
//   * zxnext.vhd:2589 — port_p3_float gate `p3_timing_hw_en = '1'`
//     (= one-hot machine_timing_p3 mirror per :1283).
//   * zxnext.vhd:2771 — port_fffd_rd alias `port_bffd AND machine_timing_p3`.
//   * zxnext.vhd:4513 — port_ff_dat_ula gate `machine_timing_48 OR
//     machine_timing_128`.
//
// Discrimination idiom (lifted from contention_test.cpp Cat 29):
//   1. init at one CLI machine type
//   2. write NR 0x03 with bit 7 set + tim_sel != typ_sel
//   3. emu.run_frame() to commit the video-frame latch
//   4. issue the port operation
//   5. verify the behaviour tracks tim_sel, not typ_sel
// ══════════════════════════════════════════════════════════════════════

static void test_section7_d3f_followup(void) {
    set_group("FB-7-D3F");

    // FB-D3F-01 — port 0x0FFD gate via machine_timing_p3 (VHDL :2589).
    //
    // Pre-fix gate keyed on `config_.type == ZX_PLUS3`. Discriminator:
    // init as 48K, then write NR 0x03 = 0xB1 to commit tim_sel=+3 with
    // typ_sel=48K. After run_frame() commits the latch, port 0x0FFD
    // should NOT short-circuit to 0x00 just because config_.type is
    // still ZX48K; it should read the floating-bus content because
    // machine_timing_ == TimingPlus3.
    //
    // To make the assertion observable: seed the +3 floating-bus latch
    // via `mmu_.set_p3_floating_bus_dat(0xA4)` and position the raster
    // in the V-border so the active-display arm is silent and the
    // handler falls through to `mmu_.p3_floating_bus_dat()`.
    // Pre-fix returns 0x00 (gate fails); post-fix returns 0xA4.
    //
    // GH #112 rewrite (2026-07-26): the expected value was 0xA5
    // (`latch | 0x01`). That encoded jnext's spurious border-arm bit-0
    // force — `zxula.vhd:573` is a conditional signal assignment (LRM
    // 10.5.3) and `or i_timing_p3` is parenthesised inside the FIRST
    // waveform only; the border waveform is the bare signal
    // `i_p3_floating_bus` (zxnext.vhd:4478 → raw cpu_di/cpu_do latch at
    // :4499-4508). The seed 0xA4 has bit 0 CLEAR, so this row is
    // genuinely discriminative for the force — it was not enumerated in
    // GH #112's issue text but is load-bearing on the same behaviour.
    // Its own subject (the tim_sel gate) is untouched: pre-D3F-01 the
    // gate failed and returned 0x00, which 0xA4 still rules out.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);

        // NR 0x03 = 0xB1: bit7=1 (commit gate), tim_sel=3 (+3 timing,
        // bits 6:4 = 011), bit3=0 (don't toggle dt_lock), typ_sel=1
        // (48K type, bits 2:0 = 001). Different tim_sel vs typ_sel.
        emu.nextreg().write(0x03, 0xB1);
        // Frame edge promotes pending -> effective machine_timing.
        emu.run_frame();

        // Confirm tim_sel == TimingPlus3 actually committed (V24-MEM-01
        // axis independence).
        const MachineTimingMode tim_after = emu.mmu().machine_timing();

        // Seed p3 floating-bus latch with 0xA4. The border-fallback
        // path returns it raw (GH #112): 0xA4.
        emu.mmu().set_p3_floating_bus_dat(0xA4);

        // Place raster at V-border (line < 64) so active-display arm
        // returns false and the handler falls through to the latch.
        set_raster_position(emu, 32, 64);

        // NR 0x82 default 0xFF — bit 4 (port_p3_floating_bus_io_en) is
        // set. effective_paging_locked() is false (no 7FFD(5) write).
        const uint8_t v = read_port_default(emu, 0x0FFD);

        check("FB-D3F-01",
              "Port 0x0FFD gate keys on machine_timing_ (tim_sel) per VHDL "
              ":2589 — init 48K + NR 0x03 = 0xB1 (tim_sel=+3, typ_sel=48) → "
              "after run_frame() port 0x0FFD returns the raw border-arm "
              "latch 0xA4 (post-fix); pre-D3F-01 returned 0x00 "
              "(config_.type != ZX_PLUS3); pre-GH#112 returned 0xA5",
              tim_after == MachineTimingMode::TimingPlus3 && v == 0xA4,
              fmt("v=0x%02X (want 0xA4); tim_after=%d (want %d=TimingPlus3)",
                  v, static_cast<int>(tim_after),
                  static_cast<int>(MachineTimingMode::TimingPlus3)));
    }

    // FB-D3F-02 — port 0xBFFD AY-read alias gate via machine_timing_p3
    // (VHDL :2771 — `port_fffd_rd <= iord and (port_fffd or (port_bffd
    // and machine_timing_p3) or port_bff5)`).
    //
    // Pre-fix gate keyed on `config_.type == ZX_PLUS3`. Discriminator:
    // init as 128K, then write NR 0x03 = 0xB3 to commit tim_sel=+3
    // with typ_sel=128K. After run_frame(), reading port 0xBFFD on a
    // 128K typ_sel machine with +3 tim_sel must alias to AY reg read
    // (= 0x5A sentinel), not return 0xFF (pre-fix path).
    //
    // The AY-data write side is unchanged (still always-decoded on all
    // machines per VHDL :2773 `port_bffd_wr <= iowr and port_bffd`).
    // We use the data write to seed AY reg 0 with a sentinel, then
    // the BFFD read alias retrieves it.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX128K);

        // NR 0x03 = 0xB3: bit7=1, tim_sel=3 (+3), bit3=0, typ_sel=3
        // (PLUS3 — but the machine was inited as 128K, so the typ_sel
        // commit also flips type to +3; we only care about the tim_sel
        // axis for this test). To isolate tim_sel, use typ_sel=2
        // (128K, unchanged). NR 0x03 = 0xB2.
        emu.nextreg().write(0x03, 0xB2);
        emu.run_frame();

        const MachineTimingMode tim_after = emu.mmu().machine_timing();
        const MachineType       typ_after = emu.mmu().machine_type();

        // Select AY reg 0 (tone period lo — readable unmasked, see
        // IO-05 in audio_port_dispatch_test.cpp).
        emu.port().out(0xFFFD, 0x00);
        // Seed reg 0 with sentinel via BFFD write (always decoded).
        emu.port().out(0xBFFD, 0x5A);

        // BFFD read: pre-fix returns 0xFF (config_.type != PLUS3);
        // post-fix returns 0x5A (machine_timing_ == TimingPlus3).
        const uint8_t v = read_port_default(emu, 0xBFFD);

        check("FB-D3F-02",
              "Port 0xBFFD AY-read alias gate keys on machine_timing_ "
              "(tim_sel) per VHDL :2771 — init 128K + NR 0x03 = 0xB2 "
              "(tim_sel=+3, typ_sel=128) → after run_frame() BFFD read "
              "aliases to AY reg 0 = 0x5A (post-fix); pre-fix returned "
              "0xFF (config_.type != ZX_PLUS3)",
              tim_after == MachineTimingMode::TimingPlus3
                  && typ_after == MachineType::ZX128K
                  && v == 0x5A,
              fmt("v=0x%02X (want 0x5A); tim_after=%d (want %d=TimingPlus3); "
                  "typ_after=%d (want %d=ZX128K)",
                  v, static_cast<int>(tim_after),
                  static_cast<int>(MachineTimingMode::TimingPlus3),
                  static_cast<int>(typ_after),
                  static_cast<int>(MachineType::ZX128K)));
    }

    // FB-D3F-03 — port 0xFF ULA-floating-bus gate via machine_timing_48
    // | machine_timing_128 (VHDL :4513).
    //
    // Pre-fix gate keyed on `config_.type == ZX48K || == ZX128K`.
    // Discriminator: init as 48K, then write NR 0x03 = 0xB1 to commit
    // tim_sel=+3 with typ_sel=48K. After run_frame() the gate must
    // collapse (machine_timing_ == TimingPlus3 is neither 48 nor 128)
    // → port 0xFF returns 0xFF even though config_.type is still ZX48K.
    //
    // Seed bank-5 VRAM with a sentinel byte that would be returned at
    // active capture if the gate hadn't collapsed. Pre-fix returns the
    // sentinel | 0x00 (48 timing path → ULA active arm); post-fix
    // returns 0xFF (machine_timing_ check fails).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);

        // Seed bank-5 VRAM at the position the ULA would fetch at
        // active capture phase. Same coords as FB-03 / FB-03a.
        const int LINE = 100, TSTATE = 34;          // active capture, T%8=2
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x42);

        // NR 0x03 = 0xB1: tim_sel=+3, typ_sel=48 (unchanged).
        emu.nextreg().write(0x03, 0xB1);
        emu.run_frame();

        const MachineTimingMode tim_after = emu.mmu().machine_timing();
        const MachineType       typ_after = emu.mmu().machine_type();

        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);

        check("FB-D3F-03",
              "Port 0xFF ULA-arm gate keys on machine_timing_ (tim_sel) "
              "per VHDL :4513 — init 48K + NR 0x03 = 0xB1 (tim_sel=+3, "
              "typ_sel=48) → after run_frame() port 0xFF returns 0xFF "
              "(machine_timing_ neither 48 nor 128); pre-fix returned "
              "the VRAM byte 0x42 (config_.type == ZX48K)",
              tim_after == MachineTimingMode::TimingPlus3
                  && typ_after == MachineType::ZX48K
                  && v == 0xFF,
              fmt("v=0x%02X (want 0xFF); tim_after=%d (want %d=TimingPlus3); "
                  "typ_after=%d (want %d=ZX48K)",
                  v, static_cast<int>(tim_after),
                  static_cast<int>(MachineTimingMode::TimingPlus3),
                  static_cast<int>(typ_after),
                  static_cast<int>(MachineType::ZX48K)));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section 8 — GH #109: floating bus is scoped to the LSB-0xFF decode
// VHDL: zxnext.vhd:2571+2583 (port_ff <= '1' when cpu_a(7:0) = X"FF" —
//       LSB-only, ANY high byte);
//       zxnext.vhd:2803-2806 (port_internal_rd_response OR-list — no
//       strobe matches a genuinely undecoded port);
//       zxnext.vhd:1868-1878 (cpu_di IORQ mux — no internal response +
//       no expansion bus → cpu_di <= X"FF", every machine timing).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §8 (GH #109)
// ══════════════════════════════════════════════════════════════════════

static void test_section8_gh109_scope(void) {
    set_group("FB-8-GH109");

    // FB-109-01 — DISCRIMINATOR (fails pre-fix). 48K machine, active
    // capture phase, VRAM seeded: a read of an UNDECODED odd port
    // (0x40A7 — LSB 0xA7 matches no VHDL decode: not port_fe (A0=1),
    // not port_ff (LSB != 0xFF), not port_fd family (A1:0 = 11), not
    // any LSB in zxnext.vhd:2541-2574) must return 0xFF per the cpu_di
    // default (zxnext.vhd:1877) — the ULA floating bus reaches port
    // 0xFF ONLY (zxnext.vhd:2583+4513). Pre-fix, floating_bus_read()
    // was the dispatch default and returned the seeded VRAM byte here.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 34;          // active capture, T%8=2
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x5A;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x40A7);
        check("FB-109-01",
              "48K active capture: undecoded port 0x40A7 returns 0xFF, not "
              "the ULA floating bus (zxnext.vhd:1877; port_ff scope :2583)",
              v == 0xFF, fmt("v=0x%02X (want 0xFF; VRAM marker=0x%02X)",
                             v, MARKER));
    }

    // FB-109-02 — scope-preservation pin. Same 48K active-capture
    // geometry: a read of port 0x40FF (LSB 0xFF, NON-ZERO high byte)
    // IS the port_ff decode (LSB-only per zxnext.vhd:2571+2583) and
    // must return the ULA floating-bus VRAM byte (zxnext.vhd:2813 ULA
    // arm + :4513 48K gate; zxula.vhd:319-340,573). Pins that GH #109's
    // narrowing moved the mux to a registered LSB-0xFF read handler
    // without shrinking it below its true scope (all 0x??FF ports).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const int LINE = 100, TSTATE = 34;          // active capture, T%8=2
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        const uint8_t MARKER = 0x5A;
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), MARKER);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x40FF);
        check("FB-109-02",
              "48K active capture: port 0x40FF (LSB-only port_ff decode) "
              "returns the VRAM byte (zxnext.vhd:2571+2583,2813,4513)",
              v == MARKER, fmt("v=0x%02X expected=0x%02X", v, MARKER));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section HARNESS — Phase 3 fixture-helper smoke rows (Branch C)
// These rows do NOT belong to the 26 plan rows. They exist to keep the
// helpers compile-tested independently of the FB-NN rows.
// ══════════════════════════════════════════════════════════════════════

static void test_harness_smoke(void) {
    set_group("FB-HARNESS");

    // FB-HARNESS-01 — fresh_emulator + set_raster_position lands the
    // clock at the requested (line, tstate). Verify by reading back
    // current_scanline() / the master-cycle delta.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const bool ok_set = set_raster_position(emu, 100, 50);

        const auto& timing  = emu.timing();
        const int   divisor = cpu_speed_divisor(emu.config().cpu_speed);
        const uint64_t expected_master =
            100ULL * timing.master_cycles_per_line + 50ULL * divisor;
        const uint64_t actual_master =
            emu.clock().get() - emu.current_frame_cycle();
        const int line = emu.current_scanline();

        check("FB-HARNESS-01",
              "set_raster_position(100, 50) lands clock at expected master "
              "cycle and current_scanline()==100",
              ok_set && actual_master == expected_master && line == 100,
              fmt("ok_set=%d actual=%llu expected=%llu line=%d",
                  ok_set,
                  static_cast<unsigned long long>(actual_master),
                  static_cast<unsigned long long>(expected_master),
                  line));
    }

    // FB-HARNESS-02 — set_raster_position_hc uses the 7 MHz pixel-clock
    // domain (1 hc = 4 master cycles). This is the alternate
    // interpretation of the VHDL hc(3:0) phase fold per Open Q 2.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const bool ok_set = set_raster_position_hc(emu, 64, 144);

        const auto& timing = emu.timing();
        const uint64_t expected_master =
            64ULL * timing.master_cycles_per_line + 144ULL * 4ULL;
        const uint64_t actual_master =
            emu.clock().get() - emu.current_frame_cycle();
        const int hc = emu.current_hc();

        check("FB-HARNESS-02",
              "set_raster_position_hc(64, 144) lands clock at expected "
              "master cycle and current_hc()==144",
              ok_set && actual_master == expected_master && hc == 144,
              fmt("ok_set=%d actual=%llu expected=%llu hc=%d",
                  ok_set,
                  static_cast<unsigned long long>(actual_master),
                  static_cast<unsigned long long>(expected_master),
                  hc));
    }

    // FB-HARNESS-03 — cpu_in_a_FF executes a single IN A,(0xFF). On a
    // freshly-init'd 48K at line 0 the production floating_bus_read
    // takes the border early-return (line < 64) and returns 0xFF. The
    // CPU instruction must complete without hanging and PC must advance
    // by 2 (DB FF is a 2-byte instruction).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const uint8_t a  = cpu_in_a_FF(emu);
        const uint16_t pc = emu.cpu().get_registers().PC;

        check("FB-HARNESS-03",
              "cpu_in_a_FF executes IN A,(0xFF) on 48K at line 0 → A=0xFF "
              "(border early-return path) and PC=0x8002",
              a == 0xFF && pc == 0x8002,
              fmt("a=0x%02X pc=0x%04X", a, pc));
    }

    // FB-HARNESS-04 — cpu_in_a_0FFD executes a single IN A,(C) with
    // BC=0x0FFD. On a freshly-init'd Next at line 0 the read falls
    // through to the same default handler as port 0xFF; we don't assert
    // the returned byte (Phase 3 will), only that the helper completes,
    // PC advances 2 bytes, and BC is preserved.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZXN_ISSUE2);
        const uint8_t a  = cpu_in_a_0FFD(emu);
        const auto regs  = emu.cpu().get_registers();

        check("FB-HARNESS-04",
              "cpu_in_a_0FFD executes IN A,(C) with BC=0x0FFD; helper "
              "completes, PC advances 2 bytes, BC preserved",
              regs.PC == 0x8002 && regs.BC == 0x0FFD,
              fmt("a=0x%02X pc=0x%04X bc=0x%04X", a, regs.PC, regs.BC));
    }

    // FB-HARNESS-05 — read_port_default bypasses the CPU and drives
    // Port::in() directly. On a freshly-init'd 48K at line 0, reading
    // port 0xFF must return 0xFF (border early-return).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const uint8_t v = read_port_default(emu, 0x00FF);

        check("FB-HARNESS-05",
              "read_port_default(0x00FF) on fresh 48K returns 0xFF "
              "(border early-return path through port_dispatch default)",
              v == 0xFF,
              fmt("v=0x%02X", v));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Emulator Floating Bus Compliance Tests\n");
    std::printf("======================================\n");
    std::printf("(26 plan rows + 1 port-conflict neighbour (FB-3X) +\n");
    std::printf(" 5 FB-HARNESS-NN smoke rows + later additions incl.\n");
    std::printf(" 2 GH #109 rows (Section 8) + 1 GH #112 row (FB-04b);\n");
    std::printf(" plan:\n");
    std::printf(" doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md)\n\n");

    test_section1_border();
    std::printf("  Section 1 (Border)             — %2d rows\n", 2);

    test_section2_capture_phases();
    std::printf("  Section 2 (Capture phases)     — %2d rows\n", 6);

    test_section3_p3_paths();
    std::printf("  Section 3 (+3 port 0xFF/0x0FFD)— %2d rows\n", 12);

    test_section4_per_machine();
    std::printf("  Section 4 (Per-machine select) — %2d rows\n", 3);

    test_section5_port_ff_wiring();
    std::printf("  Section 5 (Port 0xFF wiring)   — %2d rows\n", 2);

    test_section6_nr08_override();
    std::printf("  Section 6 (NR 0x08 + io_en)    — %2d rows\n", 3);

    test_section7_d3f_followup();
    std::printf("  Section 7 (D3F-followup gates) — %2d rows\n", 3);

    test_section8_gh109_scope();
    std::printf("  Section 8 (GH #109 FF scope)   — %2d rows\n", 2);

    test_harness_smoke();
    std::printf("  Harness smoke (FB-HARNESS-NN)  — %2d rows\n", 5);

    std::printf("\n======================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    if (!g_results.empty()) {
        std::printf("\nPer-group breakdown:\n");
        std::string last;
        int gp = 0, gf = 0;
        for (const auto& r : g_results) {
            if (r.group != last) {
                if (!last.empty())
                    std::printf("  %-22s %d/%d\n",
                                last.c_str(), gp, gp + gf);
                last = r.group;
                gp   = gf = 0;
            }
            if (r.passed) ++gp; else ++gf;
        }
        if (!last.empty())
            std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
    }

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-8s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
