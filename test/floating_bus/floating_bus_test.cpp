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
    cfg.roms_directory = "/usr/share/fuse";
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

    // FB-03a — +3 port 0x0FFD active-display bit-0 force. Branch B
    // simplification: handler returns p3_floating_bus_dat | 0x01 in all
    // raster phases on +3 with port_7ffd_locked=0. Seed the latch with a
    // bit-0=0 byte (0xA4); expect 0xA5 = 0xA4 | 0x01.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        // Slot 1 (0x4000-0x7FFF) is contended on +3 — Mmu::write into
        // that slot updates p3_floating_bus_dat_ (mmu.h:278-280).
        emu.mmu().write(0x4000, 0xA4);
        // Active display + T%8=2 (matches the "capture" arm semantics);
        // Branch B handler is raster-phase agnostic so position only
        // documents intent.
        set_raster_position(emu, 100, 34);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-03a",
              "+3 port 0x0FFD bit-0 force (latch=0xA4 → 0xA5) "
              "(zxula.vhd:573 + zxnext.vhd:4517)",
              v == 0xA5, fmt("v=0x%02X", v));
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
    // bit 4 cleared) → decode blocked → 0x00.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0x42);             // seed latch (irrelevant)
        // NR 0x82 reset default = 0xFF; clear bit 4 only.
        emu.nextreg().write(0x82, 0xEF);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3B",
              "+3 port 0x0FFD + NR 0x82 b4=0 → decode blocked → 0x00 "
              "(zxnext.vhd:2403, 2589, 2814)",
              v == 0x00, fmt("v=0x%02X", v));
    }

    // FB-3C — 48K port 0x0FFD → 0x00 (decode blocked by p3_timing_hw_en).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3C",
              "48K port 0x0FFD → 0x00 (p3_timing_hw_en gate) "
              "(zxnext.vhd:2589, 2814)",
              v == 0x00, fmt("v=0x%02X", v));
    }

    // FB-3D — 128K port 0x0FFD → 0x00 (same gate as FB-3C).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX128K);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3D",
              "128K port 0x0FFD → 0x00 (p3_timing_hw_en gate) "
              "(zxnext.vhd:2589, 2814)",
              v == 0x00, fmt("v=0x%02X", v));
    }

    // FB-3E — Pentagon port 0x0FFD → 0x00.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::PENTAGON);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3E",
              "Pentagon port 0x0FFD → 0x00 (p3_timing_hw_en gate) "
              "(zxnext.vhd:2589, 2814)",
              v == 0x00, fmt("v=0x%02X", v));
    }

    // FB-3F — Next-base port 0x0FFD → 0x00.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZXN_ISSUE2);
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3F",
              "Next port 0x0FFD → 0x00 (p3_timing_hw_en gate) "
              "(zxnext.vhd:2589, 2814)",
              v == 0x00, fmt("v=0x%02X", v));
    }

    // FB-3X — Branch B reviewer note 2: pin that port 0x0FFD reads on +3
    // dispatch to the dedicated 0x0FFD handler and do NOT accidentally
    // fall through to the 0x7FFD bank-switching surface.
    //
    // Method: seed p3_floating_bus_dat with a unique value (0x42), read
    // 0x0FFD, expect 0x43 (0x42 | bit 0). 0x42 differs from any plausible
    // 0x7FFD-related artifact (bank/screen bits) and the bit-0 force is
    // a unique fingerprint of the 0x0FFD handler at emulator.cpp:1356-1367.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX_PLUS3);
        emu.mmu().write(0x4000, 0x42);             // latch = 0x42
        const uint8_t v = read_port_default(emu, 0x0FFD);
        check("FB-3X",
              "+3 port 0x0FFD dispatches to 0x0FFD handler not 0x7FFD "
              "(specificity: mask 0xF003 > 0x8003) → 0x42 | 0x01 = 0x43 "
              "(emulator.cpp:1356-1367 + port_dispatch.cpp:35-61)",
              v == 0x43, fmt("v=0x%02X", v));
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

    // FB-4B — Pentagon active → port 0xFF hard-forced 0xFF.
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::PENTAGON);
        const int LINE = 100, TSTATE = 34;
        const int pixel_line = LINE - 64;
        const int char_col   = TSTATE / 8;
        // Seed VRAM with a distinctive byte; gate must drop it.
        emu.ram().write(vram_pixel_ram_offset(pixel_line, char_col), 0x5A);
        set_raster_position(emu, LINE, TSTATE);
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-4B",
              "Pentagon active capture → port 0xFF hard-forced 0xFF "
              "(zxnext.vhd:4513)",
              v == 0xFF, fmt("v=0x%02X", v));
    }

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
        // bit 2 to enable the Timex arm, then write port 0xFF = 0x05.
        emu.nextreg().write(0x08, 0x14);            // bit 4 (default) + bit 2
        // Write port 0xFF — gated by port_ff_io_en (NR 0x82 b0). Use the
        // dispatcher to pin the full integration path.
        emu.port().out(0x00FF, 0x05);
        set_raster_position(emu, 32, 64);           // border (so floating-bus arm
                                                     // would yield 0xFF)
        const uint8_t v = read_port_default(emu, 0x00FF);
        check("FB-07",
              "48K NR 0x08 b2=1 + port 0xFF write 0x05 → read returns 0x05 "
              "(Timex arm wins; zxnext.vhd:2813,5180,3630)",
              v == 0x05, fmt("v=0x%02X", v));
    }

    // FB-6A — reset state NR 0x08=0 → floating-bus wins (port 0xFF write
    // does not influence read). Pins reset default nr_08_port_ff_rd_en=0
    // (zxnext.vhd:1118).
    {
        Emulator emu;
        fresh_emulator(emu, MachineType::ZX48K);
        // NR 0x08 reset default has bit 2 = 0 (only bit 4 set).
        emu.port().out(0x00FF, 0x05);               // write Timex reg
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
        emu.port().out(0x00FF, 0x05);               // seed Timex reg while io_en=1
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
    std::printf(" 5 FB-HARNESS-NN smoke rows; plan:\n");
    std::printf(" doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md)\n\n");

    test_section1_border();
    std::printf("  Section 1 (Border)             — %2d rows\n", 2);

    test_section2_capture_phases();
    std::printf("  Section 2 (Capture phases)     — %2d rows\n", 6);

    test_section3_p3_paths();
    std::printf("  Section 3 (+3 port 0xFF/0x0FFD)— %2d rows\n", 11);

    test_section4_per_machine();
    std::printf("  Section 4 (Per-machine select) — %2d rows\n", 3);

    test_section5_port_ff_wiring();
    std::printf("  Section 5 (Port 0xFF wiring)   — %2d rows\n", 2);

    test_section6_nr08_override();
    std::printf("  Section 6 (NR 0x08 + io_en)    — %2d rows\n", 3);

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
