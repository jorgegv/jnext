# Task — Contention Model SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 12 rows re-homed from `test/ula/ula_test.cpp` §11 that test
memory + I/O contention behaviour. Contention is a machine-wide
subsystem (affects CPU T-state counting across 48K / 128K / +3 /
Pentagon / Next timing regimes). A dedicated `ContentionModel` class
already exists at `src/memory/contention.h`; what is missing is the
full wait-pattern emission on the CPU tick loop plus the I/O-port
decode path.

> **See also**: `doc/testing/CONTENTION-TEST-PLAN-DESIGN.md` for the
> 68-row VHDL-derived compliance test plan (Phase A/B/C phasing,
> trimmed of duplicate facets per critic audit 2026-04-24).

## Starting state

- **`ContentionModel` class exists** at `src/memory/contention.h`
  with the following public API:
  - `void build(MachineType type)` — populates the per-machine
    `lut_[vc][hc]` wait-pattern table and seeds the default
    contended slot (slot 1 for 48K/128K/+3). Also resets all four
    gate inputs to VHDL power-on defaults.
  - `uint8_t delay(uint16_t hc, uint16_t vc) const` — returns the
    number of wait T-states for a memory access at `(hc, vc)`.
    **Declared but not yet wired to any runtime caller.**
  - `bool is_contended_address(uint16_t addr) const` — slot-table
    lookup (`0x0000/0x4000/0x8000/0xC000` → slot 0/1/2/3).
  - `void set_contended_slot(int slot, bool v)` — per-slot contend
    flag setter.
  - Four VHDL-faithful gate-input setters/getters:
    `set_mem_active_page / cpu_speed / pentagon_timing /
    contention_disable` (plus their const getters). Used by
    `is_contended_access()`.
  - `bool is_contended_access() const` — combined enable-gate +
    `mem_contend` decode per `zxnext.vhd:4481,4489-4493`. **Live
    and exercised today** by the 13 CON-* rows in
    `test/mmu/mmu_test.cpp` Cat-16.
- **13 CON-* rows live + green** in `test/mmu/mmu_test.cpp` Cat-16
  (CON-01..CON-09 plus CON-10/11/12a/12b). Will migrate to
  `test/contention/contention_test.cpp` per the C2-move commitment
  in the test-plan-design document (the mmu suite will briefly
  reopen 148/0/0 → 135/0/13 at migration, then re-close).
- **No `test/contention/` suite exists yet.** Canonical landing
  site: `test/contention/contention_test.cpp` wired into
  `test/CMakeLists.txt`, mirroring `test/ctc/` and
  `test/ctc_interrupts/`.
- **Runtime tick-loop integration absent.** `ContentionModel::delay()`
  has no caller; the jnext CPU (`src/cpu/z80_cpu.cpp:33-122`) uses
  FUSE's built-in `ula_contention[]` table — 48K-pattern-only, keyed
  off `tstates` mod frame, not off VHDL `hc`/`vc`. This is the
  central gap that blocks the Phase-B test rows in the test plan.
- **12 ULA plan rows re-homed** with `// RE-HOME:` comments in
  `test/ula/ula_test.cpp` §11 pointing at this plan (2026-04-23).

## Rows inherited from ULA plan

(Now owned by `doc/testing/CONTENTION-TEST-PLAN-DESIGN.md`. Listed
here for provenance.)

| Row ID | Machine | Scope | VHDL cite |
|---|---|---|---|
| S11.01 | 48K | Bank-5 contention phase | `zxula.vhd:582-583` |
| S11.02 | 48K | Bank-0 non-contended | `zxnext.vhd:4489` |
| S11.03 | 48K | `hc_adj(3:2)="00"` non-contended phase | `zxula.vhd:582` |
| S11.04 | 48K | `vc>=192` `border_active_v` | `zxula.vhd:582` |
| S11.05 | 48K | Even-port I/O contention | `zxnext.vhd:4496` |
| S11.06 | 48K | Odd-port I/O | `zxnext.vhd:4496` |
| S11.07 | 128K | Bank-1 odd-bank contention | `zxnext.vhd:4491` |
| S11.08 | 128K | Bank-4 non-contended | `zxnext.vhd:4491` |
| S11.09 | +3 | Bank≥4 `WAIT_n` contention | `zxula.vhd:600` |
| S11.10 | +3 | Bank-0 non-contended | `zxnext.vhd:4493` |
| S11.11 | Pentagon | Never-contended | `zxnext.vhd:4481` |
| S11.12 | Next (turbo) | CPU speed >3.5 MHz disables contention | `zxnext.vhd:4481` |

## Remaining gaps (what still needs to land)

1. **`ContentionModel::delay()` tick-loop wiring** — the LUT is
   populated at `build()` time but no runtime site calls
   `delay(hc, vc)`. The CPU memory-cycle path must call into it and
   add the returned T-state count to the tick budget. This unblocks
   every Phase-B row in the test plan (§9-§11 + most of §12-§13).
2. **Wait-pattern per-phase coverage** — the test plan's §9 includes
   per-phase rows (CT-WIN-02 through CT-WIN-10) that pin the
   `hc_adj(3:2)` window, the `hc(8)=0`/`border_active_v=0` gates, and
   the 4-bit wrap at `zxula.vhd:178,582`. All require runtime `(hc,
   vc)` driving via `MachineTiming` observables.
3. **NR 0x07 / NR 0x08 live dispatch paths** — verify that writes
   through the NextREG port dispatcher land on
   `ContentionModel::set_cpu_speed()` and
   `ContentionModel::set_contention_disable()`, respecting the
   `hc(8)` commit edge at `zxnext.vhd:5822-5823` for NR 0x08. Test
   plan row CT-TURBO-06 pins the commit-edge behaviour.
4. **+3 `WAIT_n` runtime path** — the VHDL distinguishes +3
   (`o_cpu_wait_n`) from 48K/128K (`o_cpu_contend`). Emulator must
   emit the stall via the correct path for +3. Test plan §11 covers
   this; all 8 rows are currently Phase B.
5. **Pentagon-timing gate at the enable signal vs. the memory-
   contend switch** — both live in the class today and are covered
   by `is_contended_access()`; no new work needed at the bare-class
   tier. Runtime parity with the FUSE path (tables are currently
   zeroed for Pentagon) will need a smoke test once `delay()` goes
   live.

## Phase 0 — design (resolved)

- **"Should contention live as a dedicated class or merge into
  `MachineTiming`?"** — **resolved: dedicated class already exists**
  at `src/memory/contention.h`. No merge-into-`MachineTiming` path
  is contemplated.
- **"Per-bank contention tables location"** — **resolved: live in
  `ContentionModel`**. Whether to extract a
  `PerMachineContentionTable` strategy object is an implementation
  call; the test plan is strategy-agnostic.

## Approach (phased)

- **Phase 0 — VHDL audit of `ContentionModel`** (prerequisite to
  unlocking the test plan's Phase A rows). Walk `zxnext.vhd:4481-4520
  + 5787-5828` against `src/memory/contention.{h,cpp}` and confirm
  every input signal is modelled. Known gap: `expbus_en` /
  `expbus_speed` substitution at `zxnext.vhd:5816-5820` — jnext has
  no NextBUS emulation today, document as **WONT** per
  `feedback_wont_taxonomy.md`. Any further divergences either get a
  new setter on `ContentionModel` or a `// WONT` comment in source.
- **Phase 1 — scaffold `test/contention/contention_test.cpp`** wired
  into `test/CMakeLists.txt`. Absorb the 13 CON-* rows from
  `test/mmu/mmu_test.cpp` (the C2-move) and implement every Phase-A
  row from the test plan: §4 GATE, §5-§7 memory-contend per-machine,
  §8 bare-class port-contend subset, §12 CT-PENT-01 + CT-TURBO-01.
  Target: ~28 Phase-A rows live and green.
- **Phase 2 — `delay()` tick-loop wiring**. Add
  `ContentionModel::contention_tick(mreq_n, iorq_n, rd_n, wr_n,
  addr, hc, vc)` (or equivalent per implementation choice) and call
  it from the Z80 tick path. Decide on FUSE-table retirement vs.
  coexistence. Unlocks test plan §9-§11 + most of §12-§13 (~36
  Phase-B rows).
- **Phase 3 — NextREG dispatch paths**. Ensure NR 0x07 / NR 0x08
  writes land on the class setters, with the `hc(8)` commit edge
  for NR 0x08. Unlocks CT-TURBO-04/05/06.
- **Phase 4 — integration smoke + regression refresh**. Full-frame
  T-state integration (CT-INT-01/02), Pentagon full-frame
  (CT-PENT-05), and screenshot-regression re-baseline. Contention
  landing changes frame length on 48K/128K/+3 — the Phase-4 audit
  MUST re-run `bash test/regression.sh` and refresh any shifted
  references.
- **Phase 5 — un-skip + close**. Flip the 12 ULA re-home comments
  (and their Phase-B/C cousins in the test plan) back to `check()`
  where the row lands green; file a dashboard refresh.

## Scope + risk

- ~20-40 hours of work given the per-machine complexity and the
  need to re-run all screenshot-regression references after the CPU
  timing model changes.
- **Blocks nothing critical today** — jnext works without full
  VHDL-faithful contention for NextZXOS boot, game loading, and most
  demos. Contention-sensitive demos (classic 48K timing tricks) are
  a cosmetic correctness gap.
- **Primary risk**: screenshot regressions on landing `delay()`
  wiring. Mitigation: treat Phase 4 re-baseline as mandatory and
  document every refreshed reference in the merge note.

## Status: **Phase 1 (Phase-A) DONE 2026-04-26** — 28/68 rows live.
Phase 2 (Phase-B `delay()` tick-loop wiring, 36 rows) and Phase 3
(Phase-C integration smoke, 4 rows) PENDING.

### Phase 1 closure (2026-04-26)

3 cherry-picked branches landed on main:
- `95b21f6` — Branch A: VHDL audit of `ContentionModel` against
  `zxnext.vhd:4481-4520 + 5787-5828`. Added
  `port_contend(uint16_t cpu_a, bool port_ulap_io_en) const` accessor
  for the bare-class §8 port-contend decode. Documented `expbus_en`/
  `expbus_speed` as WONT (no NextBUS in jnext) and `port_7ffd_active`
  as PHASE-B (needs full Emulator).
- `89544a9` — Branch B: flipped 21 §4 + §5 + §6 + §7 + §12-Phase-A rows
  using existing `is_contended_access()` API.
- `49e20cd` — Branch B: C2-move — deleted 13 Cat-16 memory-contention
  rows from `test/mmu/mmu_test.cpp` (the legacy identifiers no longer
  exist anywhere in the tree per plan §15).
- `f4d5f4f` — Branch C: flipped 7 §8 IO port-contend rows
  (CT-IO-01..04, 07..09) using Branch A's `port_contend()`.
- `5a2bbf0` — followup: scrub legacy identifier literals from the
  migration-map comment per Branch B reviewer Nit 1.

Final: contention_test **28/0/40**. mmu_test **137/0/0** (was 150).
Aggregate `make unit-test` **3326/3286/0/40** across 32 suites.
Regression 34/0/0.

Phase-A = 28 rows breakdown: §4 (8) + §5 (5) + §6 (3) + §7 (3) +
§8 Phase-A subset (7) + §12 Phase-A subset (2).

### Phase 2/3 deferral rationale

Phase 2 wires `ContentionModel::delay()` into the Z80 tick path. Per
plan §15: "any emulator change that lands `ContentionModel::delay()`
on the tick path will change frame length on 48K/128K/+3 machines.
Reference screenshots captured before that work will need
regeneration." Mixing this with Phase 1 in the same session would
have made the screenshot rebaseline harder to audit. Schedule Phase 2
as its own session with the `bash test/generate-references.sh` sweep
included in scope. Phase 3 follows Phase 2.

See `doc/testing/CONTENTION-TEST-PLAN-DESIGN.md` for the full
row-level test plan (68 rows across 11 sections, with Phase A/B/C
tags).
