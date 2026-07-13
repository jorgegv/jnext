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
#include "cpu/z80_cpu.h"
#include "memory/contention.h"
#include "memory/mmu.h"

#include <cstdint>
#include <cstddef>

// Construct a fresh Emulator at the given machine type. Returns true on
// success; false if init() failed (typically a missing ROM file). The
// caller-supplied EmulatorConfig is otherwise default — ROMs come from
// the standard FUSE directory (/usr/share/fuse), which the rest of the
// jnext test harness already depends on.
inline bool make_emu(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
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

// (Task 54: the former `expected_wait_pattern(hc)` helper was REMOVED.
// It mirrored the emulator's wrong-period `pattern[hc & 7]` table —
// FUSE's per-T-STATE pattern indexed with PIXEL ticks — so the CT-WIN-07
// sweep that consumed it was green with both sides wrong. The sweep now
// carries its own INLINE VHDL-derived literal table in
// contention_test.cpp, deliberately not shared with production code.)

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

// ── Phase-3 integration-smoke helpers ─────────────────────────────────
// These drive the FUSE Z80 core (already wired with ContentionModel
// per-cycle via z80_set_contention_runtime() in Emulator::init()) by
// poking a small program into RAM and stepping `cpu().execute()` until
// HALT. The cumulative T-state delta between contention-ON and
// contention-OFF runs IS the integrated contention stretch the runtime
// added — the integration-smoke metric the §14 plan rows ask for.
//
// We deliberately avoid `Emulator::run_frame()` because it resets the
// FUSE tstates counter to 0 at the start of every frame — making
// cross-frame totals impossible to read directly. Stepping the CPU
// instruction-by-instruction keeps `*fuse_z80_tstates_ptr()`
// monotonically accumulating from the value we seed at run-start.
//
// The contention runtime is independent of the run_frame() loop — once
// `Emulator::init()` installs it via `z80_set_contention_runtime()`,
// every `cpu_.execute()` MREQ/IORQ goes through `contention_tick()`
// regardless of whether we are inside a frame or not.

// Layout of the integration-smoke program: 100 contended reads of
// (HL=0x4000, bank 5 on 48K — contended) from code at 0x8000 (bank 2
// on 48K — uncontended), terminated by HALT.
//
// 0x8000  21 00 40   LD HL, 0x4000   ; 10 T (M1 4 + ND 3 + ND 3)
// 0x8003  06 64      LD B,  100      ; 7 T  (M1 4 + ND 3)
// 0x8005  7E         LD A, (HL)      ; 7 T  (M1 4 + MR 3)  ← contended access
// 0x8006  10 FD      DJNZ -3         ; 13/8 T (M1 5 + ND 3 + 5/0)
// 0x8008  76         HALT            ; 4 T  (M1 4)
//
// Uncontended baseline derivation (B = 100 iterations):
//   setup     : 10 + 7  = 17 T
//   loop body : 100 * (LD A,(HL)=7 + DJNZ taken=13)
//             - 1 * (DJNZ taken=13 - DJNZ not-taken=8)   ; last DJNZ falls through
//             = 100*20 - 5 = 1995 T
//   halt      : 4 T
//   ──────────────────────
//   total     : 17 + 1995 + 4 = 2016 T
//
// (Note the LD A,(HL) read of 0x4000 is the only contended access per
//  iteration. M1 fetches at 0x8005..0x8007 are bank 2 — uncontended.)
struct IntSmokeProgram {
    static constexpr uint16_t kEntry        = 0x8000;
    static constexpr uint16_t kStackInit    = 0xBF00;     // top of bank 2
    static constexpr uint8_t  kIterations   = 100;
    // Un-contended baseline T-states for kIterations=100. Derivation
    // above. If kIterations changes, recompute.
    static constexpr uint32_t kBaselineT    = 2016;
    static constexpr uint16_t kHaltAddr     = 0x8008;
};

// Inject the integration-smoke program into RAM at IntSmokeProgram::
// kEntry and prepare CPU state to run it. Caller must have already
// init()ed the Emulator at the desired MachineType.
inline void install_int_smoke_program(Emulator& emu) {
    constexpr uint16_t E = IntSmokeProgram::kEntry;
    // LD HL, 0x4000
    emu.mmu().write(E + 0, 0x21);
    emu.mmu().write(E + 1, 0x00);
    emu.mmu().write(E + 2, 0x40);
    // LD B, 100
    emu.mmu().write(E + 3, 0x06);
    emu.mmu().write(E + 4, IntSmokeProgram::kIterations);
    // LD A, (HL)
    emu.mmu().write(E + 5, 0x7E);
    // DJNZ -3 (back to LD A,(HL))
    emu.mmu().write(E + 6, 0x10);
    emu.mmu().write(E + 7, 0xFD);
    // HALT
    emu.mmu().write(E + 8, 0x76);

    auto regs = emu.cpu().get_registers();
    regs.PC     = E;
    regs.SP     = IntSmokeProgram::kStackInit;
    regs.IFF1   = 0;        // disable interrupts so the frame INT cannot
    regs.IFF2   = 0;        // perturb the T-state count
    regs.halted = false;
    emu.cpu().set_registers(regs);
}

// Step the CPU until HALT (PC parked on the HALT opcode and regs.halted
// asserted by the FUSE core), or until `max_steps` is exceeded as a
// safety cap. Returns the cumulative T-states consumed across all
// stepped instructions — read from the FUSE tstates counter delta.
//
// The FUSE tstates counter is global; we snapshot it at entry, never
// reset it, and return the (end - start) delta. This sidesteps the
// frame-start reset that `Emulator::run_frame()` applies.
// Task 50 — park the FUSE T-state counter on the FIRST ACTIVE DISPLAY LINE
// before running a timing program.
//
// Contention only exists inside the active display (VHDL zxula.vhd:414-416:
// `border_active_v <= i_vc(8) or (i_vc(7) and i_vc(6))` over the ULA's
// display-relative i_vc). The FUSE counter is frame-relative and starts at 0
// — raw vc 0, which is the TOP BORDER, 64 lines above the display on a 48K.
//
// Every timing-smoke row below used to run from tstates=0 and still measured
// a contention stretch. That was only possible because the CPU seam fed the
// gate the RAW frame vc, so the window sat 64 lines too high and the top
// border contended. Those rows were, in effect, asserting the bug. With the
// seam corrected they must execute where contention genuinely applies, or
// they measure a legitimate zero. See the T50-* rows in contention_test.cpp.
//
// `display_origin().vc` is c_min_vactive (48K/128K/+3: 64; Pentagon: 80), so
// this is machine-correct, not a hardcoded 14336.
// The anchor is the ULA COUNTER ORIGIN — the (hc_ula, vc_ula) = (0, 0)
// instant, i.e. the tick at which the ULA opens its fetch window
// (ula_min_hactive = c_min_hactive - 12, zxula_timing.vhd:423) on the first
// active display line (c_min_vactive). It is deliberately NOT a tuned
// constant: these programs read on a FIXED cadence (the smoke program's
// 20 T loop = 40 pixel ticks ≡ 0 mod 8), so their contention phase is LOCKED
// by wherever they start — whatever wait-pattern bin the first read lands in,
// every read lands in. Picking the start offset by trying values until the
// suite goes green would be tuning the test to the answer; anchoring on the
// hardware's own window origin is the one choice that is defensible without
// looking at the result.
inline void seek_to_display_window(Emulator& emu) {
    const auto& t = emu.video_timing();
    // 1 T-state = 2 pixel ticks, so tstates_per_line = (hc_max + 1) / 2, and
    // the hc origin (a pixel-tick count) is likewise halved to reach T-states.
    const uint32_t ts_per_line = static_cast<uint32_t>(t.hc_max() + 1) / 2u;
    const uint32_t ts_into_line =
        static_cast<uint32_t>(t.ula_prefetch_origin_hc()) / 2u;
    *fuse_z80_tstates_ptr() =
        static_cast<uint32_t>(t.display_origin().vc) * ts_per_line + ts_into_line;
}

inline uint32_t run_int_smoke_program(Emulator& emu,
                                      std::size_t max_steps = 100000) {
    seek_to_display_window(emu);
    const uint32_t t_start = *fuse_z80_tstates_ptr();
    for (std::size_t i = 0; i < max_steps; ++i) {
        emu.cpu().execute();
        const auto regs = emu.cpu().get_registers();
        // FUSE asserts halted on the first M1 of HALT; one iteration
        // beyond that adds another 4 T-states of HALT-NOP. Stop the
        // moment we see halted to keep totals deterministic.
        if (regs.halted && regs.PC == IntSmokeProgram::kHaltAddr) {
            break;
        }
    }
    const uint32_t t_end = *fuse_z80_tstates_ptr();
    return t_end - t_start;
}
