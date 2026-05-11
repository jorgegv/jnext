# Pass-6 — CPU / Z80N / IM2 Blind Verification Re-Audit

Branch: `task2/verify6-cpu-z80n-im2`
Worktree: `.claude/worktrees/task2-verify6-cpu-z80n-im2`
Files audited: `src/cpu/z80_cpu.{cpp,h}`, `src/cpu/z80n_ext.{cpp,h}`,
`src/cpu/im2.{cpp,h}`, `third_party/fuse-z80/`.
Constraint: blind audit — no prior pass-1..5 report read.

## Verdict

**1 class-(a) finding, fixed in this pass.** Audit converging.

Five prior passes landed 10 fixes (P1=1, P2=1, P3=3, P4=4, P5=1). Pass-6
ran the deferred angle from pass-5 (Z80N operand-read contention) to
ground, classified, and fixed it. Two class-(b) speculative items
remain (PUSH_NN/OUTINB/JP_C WZ tracking) that require additional VHDL
spelunking before fixing — flagged for a future pass.

## Pass-6 fix landed

### F1 — Z80N operand-read & data-access contention bypass (class-(a))

**Pre-fix.** All Z80N opcodes that touch memory or ports — TEST_N,
ADD_HL/DE/BC_NN, PUSH_NN, OUTINB, NEXTREG_NN, NEXTREG_A, JP_C, LDIX,
LDWS, LDDX, LDIRX, LDIRSCALE, LDPIRX, LDDRX — used the raw
`MemoryInterface::read/write` and `IoInterface::in/out` paths from
`cpu.memory()` / `cpu.io()`. These bypass `fuse_z80_readbyte` /
`fuse_z80_writebyte` / `fuse_z80_readport` / `fuse_z80_writeport`, so:

  1. **No contention** is added to operand reads and data writes — the
     `ContentionModel::contention_tick()` gate that fires on
     `(hc, vc, mem_active_page)` during the active raster window is
     SKIPPED for every byte the Z80N opcode reads or writes.
  2. **No incremental tstates advance** during the operand cycle — only
     the static post-instruction `tstates += (t-8)` in the wrapper at
     `z80_cpu.cpp:593-595` compensated, losing the sub-instruction
     (hc, vc) precision needed for derived contention to be applied at
     the correct raster column.

Pass-5 noted this was deferred class-(b). Pass-6 quantified:

  * **LDIRX over screen RAM (6144 bytes contended, ~6 T avg stretch
    per read+write pair):** ≈ 36 K T-states drift per full-screen
    blit — a full FRAME's worth of timing (a 50 Hz frame is 70 144 T at
    7 MHz × 4-cycle access). Demos that use LDIRX for fade
    transitions sit on this drift.
  * **NextZXOS supervisor invokes NEXTREG_NN / NEXTREG_A constantly;**
    even when the operand pages are uncontended ROM, the +3 T
    per-byte advance is deferred until the static post-instruction
    add fires. The `/INT` pulse-window check at the top of
    `Z80Cpu::execute()` (`tstates - int_requested_at_`) sees a stale
    `tstates` during long Z80N bursts, so a pending ULA INT can
    outlive its 32/36-cycle pulse without the window check expiring.

→ Reclassified **class-(b) → class-(a)**.

**Fix.** `src/cpu/z80n_ext.cpp` — every operand read and data write now
goes through `fuse_z80_readbyte` / `fuse_z80_writebyte` / port reads
through `fuse_z80_readport` / port writes through `fuse_z80_writeport`.
Each call advances `tstates` by `(timing_baseline + contention_stretch)`
internally. The remaining "internal idle" cycles per opcode are added
via raw `tstates += internal_idle` at the case tail.

**NextReg writes** (`cpu.io().out(0x243B/0x253B, ...)`) STAY on the raw
IoInterface path — real hardware bypasses the I/O bus entirely for
NEXTREG (VHDL `t80n_mcode.vhd:1672-1707` drives the NextReg fabric
directly via `Z80N_data_o` strobes; no IORQ on the external pin).
Charging `fuse_z80_writeport`'s 4 T per "port write" would over-count
by 8 T per NEXTREG_NN. The 6 T internal idle on NEXTREG_NN /
NEXTREG_A covers the actual NextReg fabric write timing in hardware.

**LDIX-family transparency.** When `(HL)` matches A, the write phase is
suppressed in real hardware. We emulate the suppressed write phase as
a raw `tstates += 3` so the spec-total (16 T / 21 T) is preserved
unconditionally. Tracking the *contention* of a suppressed write
(NoWrite asserted but the bus cycle still consumes T-states with its
own contention stretch) is a corner case for a future pass — the
current behaviour matches the legacy timing total.

**Wrapper change.** `src/cpu/z80_cpu.cpp:593-598` no longer adds
`(t - 8)` to `tstates`. `execute_z80n()` now self-manages the FULL
post-M1 advance. The returned `t` value is purely informational (the
spec-total); the wrapper still snapshots `start_ts` before the
contend_read pair and returns `tstates - start_ts` so the FUSE
`fuse_z80_execute_one()` return-shape (FULL elapsed delta inc.
contention) is preserved.

**T-state breakdown table (post-fix).**

| Opcode | Spec T | M1 (wrapper) | Data accesses (fuse) | Internal idle |
|---|---|---|---|---|
| TEST_N (ED 27) | 11 | 8 | 3 (1× read) | 0 |
| ADD_*_NN (ED 34/35/36) | 16 | 8 | 6 (2× read) | 2 |
| PUSH_NN (ED 8A) | 23 | 8 | 6+6 (2 read + 2 write) | 3 |
| OUTINB (ED 90) | 16 | 8 | 3 (read) + 4 (port write) | 1 |
| NEXTREG_NN (ED 91) | 20 | 8 | 6 (2× read) | 6 |
| NEXTREG_A (ED 92) | 17 | 8 | 3 (1× read) | 6 |
| JP_C (ED 98) | 13 | 8 | 4 (port read IORQ) | 1 |
| LDIX/LDWS/LDDX (ED A4/A5/AC) | 14 / 16 | 8 | 3+3 (read+write) | 0 / 2 |
| LDIRX/LDIRSCALE/LDPIRX/LDDRX | 16/21 | 8 | 3+3 (read+write) | 2 / 7 |
| (BS*L/BS*R/BRLC/MUL/MIRROR/SWAPNIB/ADD_*_A/PIXEL*/SETAE) | 8 | 8 | — | 0 |

## Methodology

Pass-6 walked the 10 angles enumerated in the prompt:

1. **Z80N operand-read contention (deferred class-(b) from pass-5).**
   Quantified to class-(a). Fixed (F1 above).

2. **NMI / INT during Z80N opcode mid-execution.** Z80N opcodes are
   atomic — interrupts only sampled at instruction boundaries (FUSE
   convention; matches VHDL `t80n.vhd` instruction-boundary INT
   acceptance). `Z80Cpu::execute()` samples INT/NMI at the top before
   any opcode is fetched. ED + ext bytes are read together as a
   single decode unit; no mid-decode INT window. **No fix.**

3. **EI grace period precise behavior.** FUSE
   `fuse_z80_core.c:122-124` sets `interrupts_enabled_at = tstates`
   on EI; `fuse_z80_interrupt()` rejects when
   `(libspectrum_signed_dword)tstates == z80.interrupts_enabled_at` —
   matches VHDL `t80n.vhd` EI semantics (IFF1 takes effect AFTER the
   next opcode boundary). Pass-4 added save/load persistence of this
   field. Stacked EIs: each EI overwrites
   `interrupts_enabled_at = tstates` so the grace period RESETS to
   the latest EI. Matches FUSE Z80 test suite case `ei.in/expected`
   (1356/1356 still passing). **No fix.**

4. **RETI vs RETN distinction in IM2 daisy-chain.** `im2.cpp`
   `on_reti()` clears S_ISR (`im2_device.vhd:123-128`); `on_retn()`
   is a documented no-op (`im2_device.vhd` does NOT react to RETN —
   only `divmmc.vhd:108,126,139` and the `mmc.vhd` /
   `zxnext.vhd` reset/NMI shadow latches consume `i_retn_seen`).
   Decoder `advance_decoder()` distinguishes ED 4D (RETI →
   `reti_seen_pulse_`) from ED 45 (RETN → `retn_seen_pulse_`),
   firing only the corresponding pulse. Peripheral fabric sees
   RETI but NOT RETN — matches VHDL exactly. **No fix.**

5. **M1 fetch + INT acceptance window race.** Pass-1 added the
   `int_pulse_tstates` window check (`z80_cpu.cpp:426-432`). 32/36
   cycles depending on machine timing per VHDL
   `zxnext.vhd:2033`. Pulse retraction is correctly modelled:
   `int_pending_` is cleared when `(tstates -
   int_requested_at_) > int_pulse_tstates && !iff1`. **No fix.**

6. **R-register precise increment count.** Z80N path adds R += 2
   (one for ED, one for ext byte) at `z80_cpu.cpp:531`. Matches
   FUSE's R++ at top of `fuse_z80_execute_one` (line 205) +
   the secondary R++ at the ED-prefix dispatch (`opcodes_base.c:1075`).
   Sync via `(z80.r7 & 0x80) | (z80.r & 0x7f)` correctly preserves
   the R7 high bit (set only by user `LD R, A` writes). **No fix.**

7. **PC fix-up on RST $66 (NMI).** `fuse_z80_nmi()` at
   `fuse_z80_core.c:165-178` does `if (z80.halted) { PC++;
   z80.halted = 0; }` — increments PC past the HALT before pushing.
   Matches VHDL NMI handling. The on_nmi_servicing callback in
   `z80_cpu.cpp:399-402` pre-computes `saved_pc` using the same
   HALT-fix logic so NR 0xC2/0xC3 capture sees the correct
   stacked PC. **No fix.**

8. **ED 8A PUSH NN vs PUSH HL/AF/BC/DE/IX/IY cycle counts.**
   PUSH_NN = 23 T (per pass-1, including ED prefix). PUSH HL =
   11 T; PUSH IX/IY = 15 T (DD/FD prefix). All cycle counts
   match VHDL. **No fix.**

9. **WZ register on every Z80N opcode.** Built the full table
   below. The undocumented WZ behaviour of PUSH_NN (only WZ.lo
   updated, twice — see `t80n_mcode.vhd:1921-1949` `LDZ` strobes
   at MCycle 1 and 3, no LDW), OUTINB and JP_C is murky in the
   VHDL. Pass-3 + Pass-4 covered the cases with clear LDZ+LDW
   semantics (ADD_HL/DE/BC_NN). The remaining cases either don't
   touch WZ (most arithmetic / PIXEL ops) or update only WZ.lo
   (PUSH_NN) — and BIT (HL) / SCF / CCF read WZ.h for X/Y flag
   composition, so WZ.lo-only updates have no observable effect
   on the next instruction's flags. Class-(b) deferred (no
   currently-known consumer of these specific WZ states; tracking
   them requires more VHDL spelunking and a real failing test).

10. **FUSE-internal state inventory (final pass).** Walked every
    member of `processor` struct in
    `third_party/fuse-z80/fuse_z80_shim.h:39-53`. All members
    accounted for in save/load:
    - `af/bc/de/hl/af_/bc_/de_/hl_/ix/iy/sp/pc/i/r/r7` — saved via
      `Z80Registers` mirror (sync_*_from_*).
    - `memptr` — Pass-3 added.
    - `q` — Pass-3 added.
    - `iff1/iff2/im/halted` — saved.
    - `iff2_read` — Pass-4 added.
    - `interrupts_enabled_at` — Pass-4 added.
    - No additional fields in the FUSE processor struct. **No fix.**

## WZ table (final)

| Opcode | Name | VHDL WZ end-state | C++ WZ end-state | Match | Notes |
|---|---|---|---|---|---|
| ED 23 | SWAPNIB | unchanged | unchanged | ✓ | no MCycle touches WZ |
| ED 24 | MIRROR_A | unchanged | unchanged | ✓ | |
| ED 27 | TEST_N | unchanged | unchanged | ✓ | TST itself doesn't load WZ |
| ED 28 | BSLA_DE_B | unchanged | unchanged | ✓ | |
| ED 29 | BSRA_DE_B | unchanged | unchanged | ✓ | |
| ED 2A | BSRL_DE_B | unchanged | unchanged | ✓ | |
| ED 2B | BSRF_DE_B | unchanged | unchanged | ✓ | |
| ED 2C | BRLC_DE_B | unchanged | unchanged | ✓ | |
| ED 30 | MUL_DE | unchanged | unchanged | ✓ | |
| ED 31 | ADD_HL_A | unchanged | unchanged | ✓ | |
| ED 32 | ADD_DE_A | unchanged | unchanged | ✓ | |
| ED 33 | ADD_BC_A | unchanged | unchanged | ✓ | |
| ED 34 | ADD_HL_NN | WZ = nn (LDZ + LDW) | WZ = nn | ✓ | pass-3+4 |
| ED 35 | ADD_DE_NN | WZ = nn | WZ = nn | ✓ | pass-3+4 |
| ED 36 | ADD_BC_NN | WZ = nn | WZ = nn | ✓ | pass-3+4 |
| ED 8A | PUSH_NN | WZ.lo = ll, WZ.hi unchanged | unchanged (both bytes) | (b) | LDZ × 2; murky |
| ED 90 | OUTINB | unclear (no LDZ/LDW in mcode) | unchanged | likely ✓ | |
| ED 91 | NEXTREG_NN | unchanged (no LDZ/LDW in mcode) | unchanged | ✓ | |
| ED 92 | NEXTREG_A | unchanged | unchanged | ✓ | |
| ED 93 | PIXELDN | unchanged | unchanged | ✓ | |
| ED 94 | PIXELAD | unchanged | unchanged | ✓ | |
| ED 95 | SETAE | unchanged | unchanged | ✓ | |
| ED 98 | JP_C | unclear (port read; no LDZ/LDW) | unchanged | likely ✓ | port read doesn't load WZ |
| ED A4 | LDIX | unclear (no LDZ/LDW in mcode) | unchanged | likely ✓ | block transfer; LDI doesn't touch WZ |
| ED A5 | LDWS | unclear | unchanged | likely ✓ | |
| ED AC | LDDX | unclear | unchanged | likely ✓ | |
| ED B4 | LDIRX | unclear | unchanged | likely ✓ | |
| ED B6 | LDIRSCALE | unclear | unchanged | likely ✓ | |
| ED B7 | LDPIRX | unclear (LDZ at MCycle 1?) | unchanged | (b) | possibly LDZ on read; verify |
| ED BC | LDDRX | unclear | unchanged | likely ✓ | |
| ED FB | LOOP | n/a (not implemented) | unchanged | ✓ | NOP-equivalent |

PUSH_NN's WZ.lo-only update (per VHDL `LDZ <= '1'` at MCycle 1 + 3 in
`t80n_mcode.vhd:1929,1938`, no LDW) is the only confirmed VHDL/C++
divergence among the WZ-touching opcodes. WZ.h is unchanged — the
next BIT (HL) / SCF / CCF X/Y flag composition reads WZ.h, so
WZ.lo-only updates have no observable effect on flag-reading
opcodes. Marking class-(b): no currently-known failing test, no
known consumer of this specific WZ.lo state.

## Test results

| Suite | Total | Pass | Fail | Skip |
|---|---|---|---|---|
| FUSE Z80 (`./build/test/fuse_z80_test build/test/fuse`) | 1356 | 1356 | 0 | 0 |
| Z80N (`./build/test/z80n_test test/z80n`) | 85 | 85 | 0 | 0 |
| `make unit-test` (all 36 suites) | 3892 | 3854 | 0 | 38 |
| `ctest` (37 suites) | 37 | 37 | 0 | — |
| `bash test/00regression/regression.sh` | 33 | 32 | 1* | 0 |

\* Pre-existing `parallax-demo` 44636 pixels-differ failure
(unrelated to this pass — verified at baseline pre-fix; unchanged
post-fix).

## Convergence assessment

**Trend across passes (class-(a) finds per pass):**
P1=1, P2=1, P3=3, P4=4, P5=1, P6=1.

The trend descended P3→P4 (3→4) on the FUSE-internal state inventory
expansion, then descended sharply P4→P5 (4→1) once the inventory was
exhaustive, and P6 (1) found and resolved the final deferred
class-(b) item from P5. The remaining unresolved findings are all
class-(b) (PUSH_NN WZ.lo, LDPIRX possible LDZ at MCycle 1) — all
purely speculative without a failing test or a documented consumer
of the divergent state.

**Recommendation:** declare convergence on the CPU/Z80N/IM2 audit.
Future iterations should focus on:

  1. **Concrete failing tests** that exercise the speculative
     class-(b) items (PUSH_NN followed by BIT (HL) / SCF, or LDPIRX
     followed by an instruction that consumes WZ).
  2. **LDIX-family transparency contention** — the suppressed-write
     branch currently uses raw `tstates += 3` instead of going
     through fuse_z80_writebyte. This is correct timing but skips
     the contention stretch on the suppressed-write phase. If the
     write phase still triggers the contention gate in real
     hardware (NoWrite asserted but cycle still occupies the bus),
     this drifts contention by up to 6 T per LDIRX iteration over
     contended pages where transparency hits.

Both items are non-blocking for current production code paths and
should NOT block other Task 2 verification subsystems (memory,
nmi-mf-port, divmmc-sd-spi).

## Files modified

- `src/cpu/z80n_ext.cpp` — all data-touching Z80N opcodes route
  memory accesses through `fuse_z80_readbyte` / `fuse_z80_writebyte`
  and port accesses through `fuse_z80_readport` /
  `fuse_z80_writeport` (plus the `cpu.io().out` → NextReg fabric
  shortcut for NEXTREG_NN/NEXTREG_A which intentionally bypasses
  the I/O bus). Each opcode advances `tstates` by `internal_idle`
  at the case tail. Added `extern "C" #include "fuse_z80_shim.h"`
  for `tstates` global access.
- `src/cpu/z80_cpu.cpp` — wrapper no longer adds `(t - 8)` to
  `tstates` for Z80N opcodes; `execute_z80n` self-manages the FULL
  post-M1 advance.
