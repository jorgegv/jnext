// Phase-2 fixture helpers for contention_test.cpp.
//
// These helpers cover the §9-§13 Phase-B rows of the contention test plan
// (doc/testing/CONTENTION-TEST-PLAN-DESIGN.md).
//
// Coverage strategy in this branch (Branch C of the 2026-04-25 wave):
//   * §9 (CT-WIN-*) and §10/§11 stretch-LUT rows are validated against
//     the bare `ContentionModel::delay()` + per-machine LUT semantics.
//     The LUT is the VHDL `wait_s` × per-phase pattern and is the same
//     state Branch A's runtime tick will dereference on every CPU MREQ.
//     Testing the LUT IS testing the per-cycle stretch behaviour — the
//     A-wave only changes WHO calls it, not what it returns.
//   * §10/§11 zero-contention rows (bank 0 reads, etc.) are validated
//     against the gate-and-decode path through `is_contended_access()`.
//   * §12 NR-write rows (CT-TURBO-04/05, CT-PENT-04) are validated by
//     driving the production NR-write handlers via `emu.nextreg().write()`
//     and observing the public-side effects (clock speed, NR readback).
//     The NR 0x07 / NR 0x08 → ContentionModel dispatch is already wired
//     at src/core/emulator.cpp:303-308, 1644 (predates Branch B), so
//     these rows pass on `main` when their semantics are verified via
//     the public API (e.g. `clock().cpu_speed()`).
//   * §12 CT-TURBO-06 (hc(8) commit-edge) is the one true Branch-B row
//     in this scope: it requires the NR 0x08 write to latch on the next
//     hc(8) rising edge, not combinatorially. Until Branch B lands the
//     write commits immediately and the row's pre-/post-edge contention
//     discrimination collapses. Documented here as a check() that will
//     FAIL standalone post-A merge until B's commit-gate lands.
//   * §13 (CT-FB-*) is validated via `Mmu::p3_floating_bus_dat()` — the
//     latch and the slot-contended push from Emulator into Mmu have
//     been on `main` since the Floating-Bus plan landed.
//   * §8 CT-IO-05/06 (128K `port_7ffd_active`): the bare-class
//     `port_contend()` already comments this term as Phase-B and drops
//     it. Until Branch A/B land the runtime port_7ffd_active drive,
//     these two rows assert the bare-class contract directly (under-
//     reports for 128K cpu_a=0x7FFD because the term is dropped on
//     purpose) and a separate full-Emulator probe is added once the
//     wiring is observable.
//
// The helpers do NOT reach into private Emulator state — `contention_`
// is private. Where the test plan asks for "full Emulator" observation
// of contention, we use the next-best public proxy:
//   * `emu.clock().cpu_speed()`          for NR 0x07 round-trip.
//   * `emu.nextreg().read(0x08) bit 6`   for NR 0x08 round-trip.
//   * `emu.mmu().p3_floating_bus_dat()`  for §13 latch capture.
//   * `emu.mmu().slot_contended()`       for the slot-contended push.

#pragma once

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "memory/contention.h"

#include <cstdint>

// Construct a fresh Emulator at the given machine type. Returns true on
// success; false if init() failed (typically a missing ROM file). The
// caller-supplied EmulatorConfig is otherwise default — ROMs come from
// the standard FUSE directory (/usr/share/fuse), which the rest of the
// jnext test harness already depends on.
inline bool make_emu(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// The bare-class equivalent: a `ContentionModel` configured for the
// given MachineType, with `mem_active_page` seeded so the contention
// gate fires when the per-machine memory-decode predicate is true. The
// caller can override page / cpu_speed / contention_disable as needed.
inline ContentionModel make_cm(MachineType type, uint8_t mem_active_page = 0) {
    ContentionModel cm;
    cm.build(type);
    cm.set_mem_active_page(mem_active_page);
    return cm;
}

// Per-phase wait-pattern LUT element from VHDL zxula.vhd:587-595.
// `pattern[hc & 7] = {6,5,4,3,2,1,0,0}` is the 7-cycle clock-stretch
// table emitted on contended cycles inside the wait-window.
inline uint8_t expected_wait_pattern(int hc) {
    static constexpr uint8_t pattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    return pattern[hc & 7];
}

// True when the row's `(hc, vc)` pair is inside the VHDL display window
// AND the hc_adj phase is in a stretched bin (see CONTENTION-TEST-PLAN-
// DESIGN.md §9). Range matches the build()-loop in
// src/memory/contention.cpp post Branch-A fix:
//   for vc in 0..191, hc in 0..255:
//     hc_adj = (hc & 0xF) + 1
//     contend = (hc_adj & 0xC) != 0
//     if (is_p3) contend |= (hc_adj & 0xE) == 0
// vc range is per VHDL `border_active_v = vc(8) | (vc(7) & vc(6))`
// (zxula.vhd:414) — contention fires for vc in [0, 191].
inline bool expect_lut_nonzero(MachineType type, int hc, int vc) {
    if (vc < 0 || vc > 191) return false;
    if (hc < 0 || hc > 255) return false;
    const int hc_adj = (hc & 0xF) + 1;
    bool contend = (hc_adj & 0xC) != 0;
    if (type == MachineType::ZX_PLUS3) {
        contend = contend || ((hc_adj & 0xE) == 0);
    }
    return contend;
}
