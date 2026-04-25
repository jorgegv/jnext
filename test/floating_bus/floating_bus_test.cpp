// Emulator Floating Bus Compliance Test Suite — SCAFFOLDING ONLY.
//
// This suite hosts the 26 rows enumerated in
// doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md (plan landed 2026-04-23).
// Of those 26: 5 are re-homed from the ULA Phase-4 closure
// (`doc/design/TASK-FLOATING-BUS-PLAN.md`) — FB-01 (=S10.01), FB-03 (=S10.05),
// FB-04 (=S10.06), FB-06 (=S10.07), FB-07 (=S10.08) — and 21 are
// VHDL-justified neighbours uncovered by the Phase-0 audit prep.
//
// Status at file creation: **0 live rows, 26 skip() rows.**
// No implementation exists today. Per the plan §Current status:
//   * No `test/floating_bus/` suite exists. This file creates the home.
//   * The production target `Emulator::floating_bus_read`
//     (src/core/emulator.cpp:2651-2700) has not been audited against
//     VHDL zxula.vhd:308-345/573 + zxnext.vhd:2589/2713/2813/4513/4517.
//   * The existing C++ returns VRAM-derived bytes on ALL machine types
//     in violation of zxnext.vhd:4513 (Pentagon/Next/+3 must hard-force
//     port 0xFF → 0xFF). Rows FB-4A/4B/4C (and the FB-03/04 re-homes
//     with their corrected expected values) are known to witness this
//     bug once unskipped.
//
// Skip reasons below cite the plan §Current status Phase-0-audit-pending
// rationale per row group so that the honest pass-rate signal
// (UNIT-TEST-PLAN-EXECUTION.md §2) stays intact while the audit/fix
// work is scheduled.
//
// Structural template mirrored from test/ula/ula_integration_test.cpp
// and test/ctc_interrupts/ctc_interrupts_test.cpp. Same check()/skip()
// helpers, same per-group breakdown, same final summary line.
//
// Run: ./build/test/floating_bus_test

#include "core/clock.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "port/port_dispatch.h"

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
// `Emulator::floating_bus_read` (src/core/emulator.cpp:3031-3080) folds
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
// (src/core/emulator.cpp:3039-3048).
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
    // 48K, line in V-border; port 0xFF read expected 0xFF
    // (zxula.vhd:312-316,414,573).
    skip("FB-01",
         "Phase 0 audit pending (plan §Current status) — "
         "48K V-border port 0xFF=0xFF; floating_bus_read not yet "
         "VHDL-audited for raster-phase fidelity "
         "(zxula.vhd:312-316,414,573)");

    // FB-02 — neighbour. 48K, H-blank inside V-active; expected 0xFF
    // (zxula.vhd:316,416,573; host src/core/emulator.cpp:2671).
    skip("FB-02",
         "Phase 0 audit pending — 48K H-blank-in-V-active returns 0xFF; "
         "fixture needs tstate_in_line sweep helper not yet built "
         "(zxula.vhd:316,416,573)");
}

// ══════════════════════════════════════════════════════════════════════
// Section 2 — Active-display capture phases
// VHDL: zxula.vhd:319-340 (hc(3:0) case → floating_bus_r load phases
//       0x9/0xB/0xD/0xF from i_ula_vram_d; phase 0x1 resets).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §2
// Open question 2 in the plan flags the 16-hc → 8T-model mapping as
// unverified; un-skipping these rows may witness a real emulator bug.
// ══════════════════════════════════════════════════════════════════════

static void test_section2_capture_phases(void) {
    set_group("FB-2-Capture");

    // FB-2A — neighbour. 48K active, VHDL hc phase 0x9 (pixel fetch).
    skip("FB-2A",
         "Phase 0 audit pending — 16-hc vs 8T-model mapping unverified "
         "(plan §Open Q 2); hc=0x9 → pixel byte "
         "(zxula.vhd:325-327)");

    // FB-2B — neighbour. 48K active, VHDL hc phase 0xB (attr fetch).
    skip("FB-2B",
         "Phase 0 audit pending — hc=0xB → attr byte; 8T-model mapping "
         "not yet locked down "
         "(zxula.vhd:329-330)");

    // FB-2C — neighbour. 48K active, VHDL hc phase 0xD (pixel+1 fetch).
    skip("FB-2C",
         "Phase 0 audit pending — hc=0xD → pixel+1 byte "
         "(zxula.vhd:332-333)");

    // FB-2D — neighbour. 48K active, VHDL hc phase 0xF (attr+1 fetch).
    skip("FB-2D",
         "Phase 0 audit pending — hc=0xF → attr+1 byte "
         "(zxula.vhd:335-336)");

    // FB-2E — neighbour. 48K active, reset phase (VHDL hc=0x1).
    skip("FB-2E",
         "Phase 0 audit pending — reset/idle phase returns 0xFF "
         "(zxula.vhd:321-323,573)");

    // FB-2F — neighbour. 48K above-active (vc < min_vactive).
    skip("FB-2F",
         "Phase 0 audit pending — scanline above vactive returns 0xFF "
         "(zxula.vhd:414-416,573)");
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
// ══════════════════════════════════════════════════════════════════════

static void test_section3_p3_paths(void) {
    set_group("FB-3-P3Paths");

    // FB-03 — re-home of S10.05, re-scoped. +3 port 0xFF → 0xFF
    // (port 0xFF hard-forced per zxnext.vhd:4513; known emulator bug —
    // current floating_bus_read returns VRAM bytes universally).
    skip("FB-03",
         "Phase 0 audit pending — +3 port 0xFF hard-forced to 0xFF; "
         "current Emulator::floating_bus_read violates machine gate "
         "and will witness this as FAIL once unskipped (known bug per "
         "plan §Current status) (zxnext.vhd:4513)");

    // FB-03a — neighbour. +3 port 0x0FFD active-display bit-0 force.
    skip("FB-03a",
         "Phase 0 audit pending — +3 port 0x0FFD not wired as "
         "floating-bus surface today; needs new Port handler + +3 "
         "raster-phase fixture (zxula.vhd:573 + zxnext.vhd:4517)");

    // FB-04 — re-home of S10.06, re-scoped. +3 port 0xFF at border → 0xFF.
    skip("FB-04",
         "Phase 0 audit pending — +3 port 0xFF at border hard-forced "
         "to 0xFF regardless of p3_floating_bus_dat shadow; current "
         "code returns shadow byte — known bug (zxnext.vhd:4513)");

    // FB-04a — neighbour. +3 port 0x0FFD border fallback via
    // p3_floating_bus_dat (last contended CPU r/w byte).
    skip("FB-04a",
         "Phase 0 audit pending — +3 port 0x0FFD border fallback needs "
         "p3_floating_bus_dat contended-access latch + Port 0x0FFD "
         "handler; neither exists today "
         "(zxula.vhd:573 + zxnext.vhd:4498-4509,4517)");

    // FB-3A — neighbour. +3 port 0x0FFD, port_7ffd_locked=1 → 0xFF.
    skip("FB-3A",
         "Phase 0 audit pending — +3 port 0x0FFD + port_7ffd_locked "
         "gate not wired; no Port 0x0FFD floating-bus handler "
         "(zxnext.vhd:4517)");

    // FB-3B — neighbour. +3 port 0x0FFD,
    // port_p3_floating_bus_io_en=0 → decode blocked.
    skip("FB-3B",
         "Phase 0 audit pending — port_p3_floating_bus_io_en gate "
         "(NR 0x82 bit 4) decode-side; needs new +3 port 0x0FFD decode "
         "(zxnext.vhd:2403, 2589, 2814)");

    // FB-3C — neighbour. 48K port 0x0FFD → 0x00 (decode blocked by
    // p3_timing_hw_en).
    skip("FB-3C",
         "Phase 0 audit pending — 48K port 0x0FFD not a floating-bus "
         "surface; p3_timing_hw_en gate absent from current "
         "Emulator (zxnext.vhd:2589, 2814)");

    // FB-3D — neighbour. 128K port 0x0FFD → 0x00 same reason as FB-3C.
    skip("FB-3D",
         "Phase 0 audit pending — 128K port 0x0FFD not floating-bus "
         "surface; p3_timing_hw_en gate absent "
         "(zxnext.vhd:2589, 2814)");

    // FB-3E — neighbour. Pentagon port 0x0FFD → 0x00.
    skip("FB-3E",
         "Phase 0 audit pending — Pentagon port 0x0FFD not "
         "floating-bus surface; p3_timing_hw_en gate absent "
         "(zxnext.vhd:2589, 2814)");

    // FB-3F — neighbour. Next port 0x0FFD → 0x00.
    skip("FB-3F",
         "Phase 0 audit pending — Next-base port 0x0FFD not "
         "floating-bus surface; p3_timing_hw_en gate absent "
         "(zxnext.vhd:2589, 2814)");
}

// ══════════════════════════════════════════════════════════════════════
// Section 4 — Per-machine ULA-vs-0xFF selection (port 0xFF)
// VHDL: zxnext.vhd:4513 (only 48K+128K timings deliver ula_floating_bus
//       onto port 0xFF; +3/Pentagon/Next force 0xFF).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §4
// ══════════════════════════════════════════════════════════════════════

static void test_section4_per_machine(void) {
    set_group("FB-4-MachineSel");

    // FB-4A — neighbour. 128K active capture → VRAM byte reaches port 0xFF.
    skip("FB-4A",
         "Phase 0 audit pending — 128K path verification blocked on "
         "raster-phase fixture; known bug in floating_bus_read may "
         "still flow through correctly here (zxnext.vhd:4513)");

    // FB-4B — neighbour. Pentagon active → port 0xFF hard-forced 0xFF.
    skip("FB-4B",
         "Phase 0 audit pending — Pentagon port 0xFF must return 0xFF "
         "regardless of ULA; current floating_bus_read emits VRAM "
         "byte universally (known bug per plan §Current status) "
         "(zxnext.vhd:4513)");

    // FB-4C — neighbour. Next active → port 0xFF hard-forced 0xFF.
    skip("FB-4C",
         "Phase 0 audit pending — Next-base port 0xFF must return "
         "0xFF; current floating_bus_read emits VRAM byte (known "
         "bug per plan §Current status) (zxnext.vhd:4513)");
}

// ══════════════════════════════════════════════════════════════════════
// Section 5 — Port 0xFF read path wiring (default-read handler)
// VHDL: zxnext.vhd:2713 (port_ff_rd unconditional decode);
//       zxnext.vhd:2813 (read mux: Timex vs ULA vs 0x00).
// Host: src/core/emulator.cpp:173 binds floating_bus_read as the
//       port-dispatch default read handler (port 0xFF is unmapped on
//       48K/128K → falls through).
// Plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md §5
// ══════════════════════════════════════════════════════════════════════

static void test_section5_port_ff_wiring(void) {
    set_group("FB-5-Wiring");

    // FB-06 — re-home of S10.07. 48K IN A,(0xFF) at border → 0xFF
    // via port-dispatch through floating_bus_read.
    skip("FB-06",
         "Phase 0 audit pending — end-to-end CPU IN A,(0xFF) fixture "
         "not yet built (needs port().in() integration path); "
         "verifies port_.set_default_read binding "
         "(zxnext.vhd:2713, 2813; emulator.cpp:173)");

    // FB-5A — neighbour. 48K IN A,(0xFF) in active capture → VRAM byte.
    skip("FB-5A",
         "Phase 0 audit pending — active-display-capture branch of "
         "the wiring needs raster-phase fixture (see FB-2A..FB-2F); "
         "same port-dispatch path as FB-06 "
         "(zxnext.vhd:2713, 2813; emulator.cpp:2651-2700)");
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
    // subsequent IN A,(0xFF) reads back the Timex register.
    skip("FB-07",
         "Phase 0 audit pending — NR 0x08 bit 2 Timex override arm "
         "not yet modelled in floating_bus_read; requires "
         "port_ff_reg shadow + mux "
         "(zxnext.vhd:2813, 5180, 3630)");

    // FB-6A — neighbour. Reset state NR 0x08=0 → floating-bus wins.
    skip("FB-6A",
         "Phase 0 audit pending — cold-boot reset default "
         "nr_08_port_ff_rd_en=0 pinned by this row; exercises the "
         "same mux as FB-07 (zxnext.vhd:1118, 2813, 5180)");

    // FB-6B — neighbour. NR 0x08 bit 2 set + port_ff_io_en cleared →
    // Timex arm AND-term collapses → floating-bus wins again.
    skip("FB-6B",
         "Phase 0 audit pending — port_ff_io_en leg of the 3-term "
         "AND in the read-mux needs NR 0x82 bit 0 plumbing through "
         "floating_bus_read (zxnext.vhd:2397, 2813)");
}

// ══════════════════════════════════════════════════════════════════════
// Section HARNESS — Phase 3 fixture-helper smoke rows (Branch C)
// These rows do NOT belong to the 26 plan rows. They exist to keep the
// helpers compile-tested and to give the suite a non-zero live-row count
// so the `Total: ... Passed: ...` summary line stays meaningful.
// Phase 3 will be free to delete this section once it has live FB-NN
// rows that exercise the same helpers in anger.
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
    std::printf("(scaffolding — 26 plan rows skip() until Phase 0 audit;\n");
    std::printf(" plan: doc/testing/FLOATING-BUS-TEST-PLAN-DESIGN.md;\n");
    std::printf(" plus 5 FB-HARNESS-NN smoke rows for Branch C helpers)\n\n");

    test_section1_border();
    std::printf("  Section 1 (Border)             — %2d rows\n", 2);

    test_section2_capture_phases();
    std::printf("  Section 2 (Capture phases)     — %2d rows\n", 6);

    test_section3_p3_paths();
    std::printf("  Section 3 (+3 port 0xFF/0x0FFD)— %2d rows\n", 10);

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

    // Per-group breakdown (live rows only — empty until rows unskip).
    if (!g_results.empty()) {
        std::printf("\nPer-group breakdown (live rows only):\n");
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
