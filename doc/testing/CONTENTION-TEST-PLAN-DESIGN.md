# Contention Model Compliance Test Plan

VHDL-derived compliance test plan for the Contention Model subsystem of the
JNEXT ZX Spectrum Next emulator. Scope: CPU memory + I/O contention timing
across the five supported timing regimes (48K, 128K, +3, Pentagon, Next
turbo). All expected behaviour is derived exclusively from the VHDL source
(`zxula.vhd`, `zxnext.vhd`), not from the current C++ implementation.

## Purpose

Contention is the machine-wide mechanism by which the ULA stalls the Z80 to
arbitrate access to the shared display memory and to the I/O bus. It is a
per-machine, per-pixel-phase, per-bank (memory) / per-port-bit-pattern
(I/O) function that adds T-states to CPU memory- and I/O-cycle costs inside
the display window of the frame. Correct contention emulation is a
prerequisite for cycle-accurate reproduction of classic 48K / 128K / +3
demos, frame-locked audio players, and any software that times off `HALT`
+ `int` arrival.

On the ZX Spectrum Next the contention rule set is extended with two extra
gates that did not exist on classic hardware:

- **NR 0x08 bit 6** (`eff_nr_08_contention_disable`, `zxnext.vhd:4481` and
  `zxnext.vhd:5802`) — a software-settable global disable.
- **NR 0x07 bits 1:0** (`cpu_speed`, `zxnext.vhd:4481` and
  `zxnext.vhd:5801`) — any CPU speed other than 3.5 MHz disables
  contention entirely.

These two combine with the machine-timing select to feed the single ULA
enable signal `i_contention_en`. The per-bank memory-contention rules
(`mem_contend`) and the I/O port-contention rules (`port_contend`) then
decide per cycle whether the ULA-derived wait-state pattern is applied to
the Z80 clock.

This test suite validates the Contention Model against the authoritative
VHDL sources. All expected values are derived from VHDL; the C++
implementation is the thing under test.

## Current status

**CLOSED 2026-04-26 — 68/68 rows live (68/68/0/0).** All three phases
landed in a single session. See `doc/design/TASK-CONTENTION-MODEL-PLAN.md`
§"Phase 1 closure", §"Phase 2 closure" and §"Phase 3 closure" for
write-ups.

### Historical opening state

- **13 plan rows inherited from the ULA Video plan's §11** (`doc/testing/
  ULA-VIDEO-TEST-PLAN-DESIGN.md` §11, re-homed 2026-04-23 during ULA Phase
  0 closure). The source-level skip comments in `test/ula/ula_test.cpp`
  point at this plan (`test/ula/ula_test.cpp:1240`).
- **Existing C++ surface**: a `ContentionModel` class is present at
  `src/memory/contention.h` and is partially VHDL-faithful today. It
  exposes:
  - A build-time per-machine wait-pattern LUT (`lut_[vc][hc]`,
    populated by `build(MachineType)`).
  - The four VHDL-faithful enable-gate inputs (`mem_active_page`,
    `cpu_speed`, `pentagon_timing`, `contention_disable`) combined via
    `is_contended_access()` — this is the target of the existing 13
    `CON-*` rows in `test/mmu/mmu_test.cpp:2092+` (Cat-16 of the MMU
    suite, all green since Task-3 Wave 1, 2026-04-21).
  - A `set_contended_slot()` convenience API for the 4×16K CPU map.
- **What is still missing** (and therefore why many rows in this plan
  start life as `skip()` with an `F:` reason):
  1. The full ULA wait-state pattern emission (`hc_adj(3:2)/="00"`
     window, `border_active_v` gate, `hc(8)='0'` gate) is **not**
     exercised by the existing `ContentionModel` public surface — the
     LUT is populated but not queried on the real CPU tick path. The
     jnext CPU (`src/cpu/z80_cpu.cpp:33-122`) instead uses FUSE's
     built-in `ula_contention[]` table, which is 48K-pattern-only and
     keyed off `tstates` modulo the frame, not off VHDL `hc`/`vc`.
  2. `ContentionModel::delay(hc, vc)` is declared but no runtime site
     calls it. The VHDL-faithful wait-cycle emission is therefore
     **simulated** by the FUSE tables, which approximate but do not
     match the synchronous VHDL model (VHDL contends cycles 3-14 of a
     16-cycle group; original Spectrum / FUSE contends 4-15 — see
     `zxula.vhd:579-580` comment).
  3. **+3 path** (`zxula.vhd:600`, `WAIT_n` instead of clock stretching)
     is not exercised by the FUSE table path at all; jnext's +3 machine
     runs the 48K contention pattern, which is close but not cycle-
     identical to the +3 `hc_adj(3:1)="000"` extra-phase.
  4. **Pentagon** never-contend and **Next-turbo** cpu-speed-disable are
     implicitly correct because the FUSE tables are zeroed for those
     machines (`src/cpu/z80_cpu.cpp:442`) — but the code path is "no
     table", not "ran the gate and got false"; the two are behaviourally
     equivalent today but will diverge the moment per-cycle hc/vc
     driving is wired in.
- **No dedicated test suite** exists. The canonical landing site is
  `test/contention/contention_test.cpp` (to be created as a new unit-test
  binary wired into `test/CMakeLists.txt`, mirroring `test/ctc/` and
  `test/ctc_interrupts/`).
- **Phase A prerequisite — independent VHDL audit of
  `src/memory/contention.{h,cpp}`** against `zxnext.vhd:4481-4520 +
  5787-5828` to confirm the class's input set is complete for Phase-A
  row coverage. Known gap: the `expbus_en` / `expbus_speed` substitution
  at `zxnext.vhd:5816-5820` replaces `cpu_speed` with `expbus_speed`
  whenever the NextBUS expansion is active. jnext has no NextBUS
  emulation today, so this branch is unreachable; document it as
  **WONT** per `feedback_wont_taxonomy.md` during the bare-class audit.
  Any further divergences the audit surfaces must be either closed with
  new setters on `ContentionModel` or converted to `// WONT` comments
  before §4 Phase-A rows are un-skipped.
- **Status summary**: when the un-skip pass runs, this plan will open
  with Phase-A rows (those that target the bare `ContentionModel` class
  surface already present in `src/memory/contention.h`) live, and all
  Phase-B / Phase-C rows `skip(F: ...)` pending `delay()`-path wiring on
  the tick loop. The 13 existing CON-* rows in `test/mmu/mmu_test.cpp`
  migrate into this plan's suite on the C2-move (see "Mirror vs move"
  below). As the emulator work lands, each Phase-B wave unlocks its
  rows until all are either `check()` or explicitly `WONT`.

See `doc/testing/UNIT-TEST-PLAN-EXECUTION.md` for the VHDL-as-oracle rule,
the pass/fail/skip distinction, and the 1:1:1 emulator-fix-plus-unskip
process that governs every row in this plan.

## Scope

| Area | VHDL source | Sections |
|------|-------------|---------:|
| Enable gate (NR 0x07 / NR 0x08 / machine type / Pentagon) | `zxnext.vhd:4481`, `5800-5823` | §4 |
| Memory contention decode (`mem_contend`) | `zxnext.vhd:4489-4493` | §5, §6, §7 |
| I/O port contention decode (`port_contend`) | `zxnext.vhd:4496` | §8 |
| Wait-pattern window (`hc_adj(3:2)`, `border_active_v`, `hc(8)`) | `zxula.vhd:582-583` | §9 |
| 48K / 128K path (clock stretch via `o_cpu_contend`) | `zxula.vhd:587-595` | §10 |
| +3 path (`WAIT_n` via `o_cpu_wait_n`) | `zxula.vhd:599-600` | §11 |
| Pentagon never-contend | `zxnext.vhd:4481` | §12 |
| Next-turbo (cpu_speed != "00") disables | `zxnext.vhd:4481`, `5801`, `5817` | §12 |
| `p3_floating_bus_dat` write-through on contended mem access | `zxnext.vhd:4498-4509` | §13 |
| Runtime-integration smoke (frame-count drift under contention) | integration | §14 |

**Not in scope** (re-homed or owned elsewhere):

- Frame interrupt timing — owned by `doc/design/TASK-VIDEOTIMING-EXPANSION-PLAN.md`.
- Floating-bus readback on port 0xFF — owned by `doc/design/TASK-FLOATING-BUS-PLAN.md`.
- MMU bank-select state transitions — owned by `doc/testing/MEMORY-MMU-TEST-PLAN-DESIGN.md`.

## Architecture / test approach

### Home of the contention logic

Resolved: a dedicated `ContentionModel` class already exists at
`src/memory/contention.h` and owns the per-bank tables, the per-machine
LUT, and the four VHDL gate inputs. **This plan targets that class** as
the primary object under test. `MachineTiming` stays the source of truth
for `hc_max`/`vc_max`/IRQ position and will feed `hc`/`vc` into
`ContentionModel::delay()` at runtime, but does not own contention state
itself.

**Assumption (subject to refinement during implementation):** the future
wiring will add a `contention_tick(mreq_n, iorq_n, rd_n, wr_n, addr,
hc, vc)` entry point on `ContentionModel` that mirrors the VHDL signal
set in `zxula.vhd:587-600`, and the Z80 tick path will call it on each
memory/I/O cycle. If implementation chooses a different shape — for
example, folding the enable gate into the existing FUSE `ula_contention[]`
table population path rather than calling the model per cycle — row
wording is preserved but the stimulus harness (the section "Stimulus
helpers" below) adapts.

### Mirror vs move — commitment to C2-move

The 13 CON-* rows currently living in `test/mmu/mmu_test.cpp` Cat-16
will migrate to the new `test/contention/contention_test.cpp` with
`// RE-HOME:` comments per `feedback_rehome_to_owner_plan.md`. The mmu
suite briefly reopens (148/0/0 → 135/0/13 at migration) and re-closes
once `contention_test.cpp` lands. **No mirror; single source of truth
per row.** The `CT-*` ID namespace used in this plan supersedes the
`CON-*` namespace in the MMU suite for these rows; once migration
completes, `CON-*` identifiers no longer exist anywhere in the tree.

### Test runner

A new test binary `contention_test` at `test/contention/contention_test.cpp`
wired into `test/CMakeLists.txt`. The binary mirrors the harness style
used by `test/ctc_interrupts/` and `test/ula/`:

1. **Bare-class rows** (sections §4-§7, §12 Phase-A subset) —
   unit-level, construct a `ContentionModel`, seed inputs via the public
   setters, assert against `is_contended_access()` /
   `is_contended_address()`. No `Emulator` required. These rows absorb
   the 13 existing CON-* rows from `test/mmu/mmu_test.cpp` at migration
   time (see "Mirror vs move" above).
2. **Mixed-tier I/O rows** (§8) — §8 is **mixed-tier** (bare-class +
   full-Emulator). Most rows (CT-IO-01..04, CT-IO-07..09) are
   bare-class on the `port_contend` decode; CT-IO-05/06 require
   observable `port_7ffd_active` which in jnext today is only
   truthfully driven through the full `Emulator` (machine-timing
   select plus NR/port dispatch) — they are Phase B, not Phase A.
3. **Wait-pattern / phase rows** (sections §9-§11) — construct a full
   `Emulator` fixture (same pattern as `test/ula/ula_test.cpp` `UlaBed`
   and `test/ctc_interrupts/` `EmuBed`), step the tick loop to a known
   `(hc, vc)` position via `MachineTiming` observables, issue a memory
   or I/O cycle to a known bank/page, and compare the added T-state
   count against the VHDL-derived expected value. Requires
   `ContentionModel::delay()` to be live on the tick path (currently
   **skip(F)**-blocked; see Current status point 2).
4. **Floating-bus-write rows** (§13) — full `Emulator` fixture; issue a
   contended memory read/write and verify `p3_floating_bus_dat` capture
   per `zxnext.vhd:4498-4509`. Cross-suite dependency noted: the
   floating-bus value readback on port 0xFF is tested in the Floating
   Bus plan; this plan owns the *capture* (write side) only.
5. **Integration smoke** (§14) — full emulator, machine configured to
   48K with contention on, run N frames of a known `HALT`-loop program
   and confirm the frame-count drift matches the VHDL-expected delta
   vs. a non-contention run. Primarily a regression guardrail; any
   drift from the screenshot-regression reference after contention
   lands is the signal that frame timing shifted (flagged in §15).

### Stimulus helpers (harness contract)

Each row's stimulus is one of the following canonical forms; the runner
exposes builder helpers so row bodies stay terse:

- `make_cm(MachineType)` — fresh `ContentionModel`, `build()` called, all
  other gate inputs at VHDL power-on defaults (`mem_active_page=0`,
  `cpu_speed=0`, `pentagon_timing` seeded from type, `contention_disable=0`).
- `make_emu_at(MachineType, hc, vc)` — fresh `Emulator`, machine-type
  set, tick-advanced until `MachineTiming.hc() == hc && vc() == vc`.
  Used by §9-§11 rows.
- `mem_cycle(emu, addr, rd_or_wr)` — one Z80 MREQ cycle at the given
  address; returns the T-state count actually consumed.
- `io_cycle(emu, port, rd_or_wr)` — one Z80 IORQ cycle at the given
  port; returns the T-state count actually consumed.

## Test catalog

### §4. Enable gate — `i_contention_en` composition

VHDL: `zxnext.vhd:4481`:
```
i_contention_en <= (not eff_nr_08_contention_disable)
                 and (not machine_timing_pentagon)
                 and (not cpu_speed(1)) and (not cpu_speed(0));
```

Reset defaults: `cpu_speed <= "00"` (`zxnext.vhd:5801`),
`eff_nr_08_contention_disable <= '0'` (`zxnext.vhd:5802`).

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-GATE-01 | A | `ContentionModel` default-constructed, `build(ZX48K)`, `mem_active_page=0x0A` | `is_contended_access() == true` (enable=1, mem=1) | `zxnext.vhd:4481,4490` |
| CT-GATE-02 | A | ZX48K, `mem_active_page=0x0A`, `set_contention_disable(true)` | `is_contended_access() == false` (enable gate off) | `zxnext.vhd:4481` |
| CT-GATE-03 | A | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(1)` (7 MHz) | `is_contended_access() == false` (enable gate off) | `zxnext.vhd:4481,5817` |
| CT-GATE-04 | A | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(2)` (14 MHz) | `is_contended_access() == false` | `zxnext.vhd:4481,5817` |
| CT-GATE-05 | A | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(3)` (28 MHz) | `is_contended_access() == false` | `zxnext.vhd:4481,5817` |
| CT-GATE-06 | A | ZX48K, `mem_active_page=0x0A`, `set_pentagon_timing(true)` | `is_contended_access() == false` (discriminative over machine-type switch) | `zxnext.vhd:4481` |
| CT-GATE-07 | A | ZX48K, all gates off (`disable=false`, `speed=0`, `pentagon=false`), `mem_active_page=0x0A` | `is_contended_access() == true` | `zxnext.vhd:4481,4490` |
| CT-GATE-08 | A | Reset-default `ContentionModel` (no `build()` call), `mem_active_page=0x0A` | `is_contended_access() == false` — default-constructed `type_ = MachineType::ZXN_ISSUE2` (`src/memory/contention.h:56`); `is_contended_access()` switch falls through to `return false` at `src/memory/contention.cpp:87-90`. Regression guard: if the default type ever changes to `ZX48K`, this row flips to `true` and flags the silent behavioural shift | `src/memory/contention.h:56`; `src/memory/contention.cpp:87-90`; `zxnext.vhd:5800-5802` |

Cross-reference to CON-* rows in `test/mmu/mmu_test.cpp`:

- CON-10 (NR 0x08 bit 6 disables) ≈ **CT-GATE-02**
- CON-11 (any non-zero `cpu_speed` disables) ≈ **CT-GATE-03 / 04 / 05**
  (CON-11 sweeps all three non-zero speeds in a single MMU row;
  separated here for granular reporting)
- CON-12b (discriminative `pentagon_timing` gate) ≈ **CT-GATE-06**

CON-12a (Pentagon machine-type switch fallthrough) does NOT overlap any
CT-GATE row — it exercises the machine-type branch in
`is_contended_access()` rather than the `pentagon_timing_` gate. Its
owner is **CT-PENT-01** in §12 (the combined gate + switch check); see
§12 note.

### §5. Memory contention — 48K

Origin: re-homed ULA rows S11.01 (bank-5 phase), S11.02 (bank-0
non-contended), S11.03 (phase=0 non-contention), S11.04 (`vc>=192`
border).

VHDL: `zxnext.vhd:4489-4490`:
```
mem_contend <= '0' when mem_active_page(7 downto 4) /= "0000" else
               '1' when machine_timing_48 = '1'
                     and mem_active_page(3 downto 1) = "101" else
               ...
```

The VHDL predicate has four distinct facets worth covering:
`(3:1)="101"` true branch, `(3:1)="101"` false branch (clean zero),
`(3:1)="101"` false branch (other non-zero to discriminate `101`),
and the `(7:4)/="0000"` high-nibble guard. The pre-critic draft
over-sampled: CT-M48-01/02 both asserted `(3:1)=101` with different
bit-0 parities (bit 0 is not a predicate input); CT-M48-03/04 both
asserted `(3:1)=000`; CT-M48-06/07/08 all exercised the same
`(7:4)/="0000"` guard at different high-nibble values. Trimmed to
one representative row per facet, plus a boundary/sentinel row.

| ID | Phase | Stimulus | Expected | VHDL | Facet |
|----|-------|----------|----------|------|-------|
| CT-M48-01 | A | 48K, `mem_active_page=0x0A` (bank 5 low half, `bits(3:1)=101`) | `is_contended_access() == true` | `zxnext.vhd:4490` | `(3:1)="101"` true |
| CT-M48-03 | A | 48K, `mem_active_page=0x00` (bank 0 low half, `bits(3:1)=000`) | `is_contended_access() == false` | `zxnext.vhd:4490` | `(3:1)="101"` false (clean zero) |
| CT-M48-05 | A | 48K, `mem_active_page=0x0E` (bank 7 low half, `bits(3:1)=111`) | `is_contended_access() == false` | `zxnext.vhd:4490` | `(3:1)="101"` false (other non-zero — discriminates `101` vs. any `!="101"` pattern) |
| CT-M48-06 | A | 48K, `mem_active_page=0x10` (high nibble `!= 0`) | `is_contended_access() == false` (guard at `zxnext.vhd:4489`) | `zxnext.vhd:4489` | `(7:4)/="0000"` guard |
| CT-M48-08 | A | 48K, `mem_active_page=0xFF` (floating-bus / out-of-range sentinel) | `is_contended_access() == false` | `zxnext.vhd:4489` | guard + sentinel boundary |

Rows removed vs. the pre-critic draft:

- **CT-M48-02** (`0x0B`) — duplicate of CT-M48-01; both exercise the
  same `(3:1)="101"` true branch. Bit 0 is not a predicate input.
- **CT-M48-04** (`0x01`) — duplicate of CT-M48-03; same `(3:1)="000"`
  false branch.
- **CT-M48-07** (`0xE0`) — duplicate of CT-M48-06; both exercise the
  same `(7:4)/="0000"` guard. CT-M48-08 survives as a representative
  sentinel-boundary value (`0xFF`).

### §6. Memory contention — 128K

Origin: re-homed ULA rows S11.07 (bank-1 odd), S11.08 (bank-4 even).

VHDL: `zxnext.vhd:4491`:
```
'1' when machine_timing_128 = '1' and mem_active_page(1) = '1' else
```

The VHDL predicate has three distinct facets: `bit(1)=1` true branch,
`bit(1)=0` false branch, and the `(7:4)/="0000"` high-nibble guard
(inherited from line 4489 — shared across all three timing modes).

| ID | Phase | Stimulus | Expected | VHDL | Facet |
|----|-------|----------|----------|------|-------|
| CT-M128-01 | A | 128K, `mem_active_page=0x02` (bank 1, `bit(1)=1`) | `is_contended_access() == true` | `zxnext.vhd:4491` | `bit(1)=1` true |
| CT-M128-03 | A | 128K, `mem_active_page=0x04` (bank 2, `bit(1)=0`) | `is_contended_access() == false` | `zxnext.vhd:4491` | `bit(1)=0` false |
| CT-M128-08 | A | 128K, `mem_active_page=0x10` (high nibble `!= 0`) | `is_contended_access() == false` | `zxnext.vhd:4489` | `(7:4)/="0000"` guard |

Rows removed vs. the pre-critic draft:

- **CT-M128-02** (`0x03`), **CT-M128-05** (`0x0A`), **CT-M128-06**
  (`0x0E`) — duplicates of CT-M128-01; all exercise the same `bit(1)=1`
  true branch. Bit 1 is the only predicate input.
- **CT-M128-04** (`0x08`), **CT-M128-07** (`0x00`) — duplicates of
  CT-M128-03; same `bit(1)=0` false branch.

### §7. Memory contention — +3

Origin: re-homed ULA rows S11.09 (bank ≥ 4 WAIT path), S11.10 (bank 0
non-contended).

VHDL: `zxnext.vhd:4492`:
```
'1' when machine_timing_p3 = '1' and mem_active_page(3) = '1' else
```

The VHDL predicate has three distinct facets: `bit(3)=1` true branch,
`bit(3)=0` false branch, and the `(7:4)/="0000"` high-nibble guard
(ROM pages on +3 have high-nibble non-zero and therefore never
contend). Note that +3 uses `WAIT_n` assertion rather than clock
stretching (see §11 for the wait-signal side).

| ID | Phase | Stimulus | Expected | VHDL | Facet |
|----|-------|----------|----------|------|-------|
| CT-MP3-01 | A | +3, `mem_active_page=0x08` (bank 4, `bit(3)=1`) | `is_contended_access() == true` | `zxnext.vhd:4492` | `bit(3)=1` true |
| CT-MP3-05 | A | +3, `mem_active_page=0x00` (bank 0, `bit(3)=0`) | `is_contended_access() == false` | `zxnext.vhd:4492` | `bit(3)=0` false |
| CT-MP3-08 | A | +3, ROM access (e.g. `mem_active_page >= 0xF0`) | `is_contended_access() == false` (ROM is never contended; guard at :4489) | `zxnext.vhd:4489` | `(7:4)/="0000"` guard |

Rows removed vs. the pre-critic draft:

- **CT-MP3-02** (`0x0A`), **CT-MP3-03** (`0x0C`), **CT-MP3-04**
  (`0x0E`) — duplicates of CT-MP3-01; all exercise the same `bit(3)=1`
  true branch.
- **CT-MP3-06** (`0x02`), **CT-MP3-07** (`0x06`) — duplicates of
  CT-MP3-05; same `bit(3)=0` false branch.

### §8. I/O port contention (mixed-tier: bare-class + full-Emulator)

Origin: re-homed ULA rows S11.05 (even port contended), S11.06 (odd
port not contended).

VHDL: `zxnext.vhd:4496`:
```
port_contend <= (not cpu_a(0)) or port_7ffd_active or port_bf3b or port_ff3b;
```

Additional context: `port_7ffd_active` active on 128K/+3 timing only
(`zxnext.vhd:2594`), `port_bf3b`/`port_ff3b` are ULA+ index/data ports
(`zxnext.vhd:2685-2686`).

This section is **mixed-tier** (critic NIT N1). Most rows
(CT-IO-01..04, CT-IO-07..09) are bare-class on the `port_contend`
decode and can be driven via a helper that simulates the
port-address → `port_contend` decode without the full `Emulator`
(Phase A). CT-IO-05/06 require observable `port_7ffd_active` which in
jnext today is only truthfully driven through the full `Emulator`
(machine-timing select plus NR/port dispatch) — they are Phase B.

Where the bare-class helper does not exist yet, rows start as
`skip(F: port_contend decode not exposed)`.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-IO-01 | A | 48K, `cpu_a=0xFE` (even port, e.g. ULA) | `port_contend == 1` → contended I/O cycle | `zxnext.vhd:4496` |
| CT-IO-02 | A | 48K, `cpu_a=0xFF` (odd port, non-ULA) | `port_contend == 0` → non-contended | `zxnext.vhd:4496` |
| CT-IO-03 | A | 48K, `cpu_a=0x00` (even, lowest) | `port_contend == 1` | `zxnext.vhd:4496` |
| CT-IO-04 | A | 48K, `cpu_a=0x01` (odd, lowest) | `port_contend == 0` | `zxnext.vhd:4496` |
| CT-IO-05 | B | 128K, `cpu_a=0x7FFD` (odd, but `port_7ffd_active=1`) | `port_contend == 1` (OR-term via `port_7ffd_active`) | `zxnext.vhd:4496,2594` |
| CT-IO-06 | B | 48K, `cpu_a=0x7FFD` (`port_7ffd_active=0` on 48K timing) | `port_contend == 0` (no OR-term; odd bit gate is `(not cpu_a(0))=0`) | `zxnext.vhd:4496,2594` |
| CT-IO-07 | A | Any timing, `cpu_a=0xBF3B` (ULA+ index, odd) | `port_contend == 1` via `port_bf3b` OR-term (when `port_ulap_io_en=1`) | `zxnext.vhd:4496,2685` |
| CT-IO-08 | A | Any timing, `cpu_a=0xFF3B` (ULA+ data, odd) | `port_contend == 1` via `port_ff3b` OR-term (when `port_ulap_io_en=1`) | `zxnext.vhd:4496,2686` |
| CT-IO-09 | A | Any timing, `cpu_a=0xBF3B`, `port_ulap_io_en=0` | `port_contend == 0` (ULA+ OR-term masked) | `zxnext.vhd:4496,2685` |

### §9. Wait-pattern window — `hc`/`vc`/phase gates

VHDL: `zxula.vhd:582-583`:
```
hc_adj <= i_hc(3 downto 0) + 1;
wait_s <= '1' when ((hc_adj(3 downto 2) /= "00")
                or (hc_adj(3 downto 1) = "000" and i_timing_p3 = '1'))
               and i_hc(8) = '0'
               and border_active_v = '0'
               and i_contention_en = '1' else '0';
```

Note: `hc_adj` is `std_logic_vector(3 downto 0)` at `zxula.vhd:178` —
a 4-bit value. The `+1` at line 582 therefore wraps modulo 16; any
test author computing `hc_adj = i_hc(3:0) + 1` in arithmetic must
apply the 4-bit truncation. CT-WIN-03, CT-WIN-04 and CT-WIN-09 jointly
pin this wrap boundary.

So wait is asserted when:
- horizontal is inside display window (`hc < 256`, i.e. `hc(8)=0`);
- vertical is inside display window (`vc < 192`, i.e. `border_active_v=0`);
- phase of the 16-cycle group is 3-14 (48K/128K) or 1-14 (+3);
- AND the enable gate is on.

These rows require driving a full `Emulator` to a specific `(hc, vc)`
position, so they start as `skip(F: runtime hc/vc driving)` pending
§10/§11 wiring.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-WIN-01 | B | 48K, `hc=0`, `vc=0` (top-left of display window) | `wait_s == 0`: at `hc=0`, `hc_adj=0+1=1` → `hc_adj(3:2)="00"` (clause 1 false); on 48K `timing_p3=0` so clause 2 masked. **By design**: the `+1` offset at `zxula.vhd:582` yields `wait_s=0` for `i_hc(3:0) ∈ {0,1,2}` (`hc_adj ∈ {1,2,3}` → `hc_adj(3:2)="00"`), giving a 3-cycle non-contended prelude so fresh MREQ at the start of each 16-cycle group does not contend. Row captures a regression if the `+1` offset is ever dropped or altered | `zxula.vhd:582-583` |
| CT-WIN-02 | B | 48K, `hc=3`, `vc=100` (phase boundary low) | Compute `hc_adj = 3+1 = 4` → `(3:2)=01` ≠ 00 → `wait_s=1` | `zxula.vhd:582-583` |
| CT-WIN-03 | B | 48K, `hc=15`, `vc=100` (end of 16-phase group) | `hc_adj=0000` after 4-bit wrap of `15+1` → `(3:2)=00` → `wait_s=0`. Discriminates the 4-bit wrap from an un-truncated 9-bit `+1` | `zxula.vhd:178,582-583` |
| CT-WIN-04 | B | 48K, `hc=255`, `vc=100` (last display column) | `wait_s == 0`: `i_hc(3:0)="1111"`, `hc_adj=0000` after 4-bit wrap of `15+1`; same truncation as CT-WIN-03 but at the opposite end of the display window, confirming the wrap is independent of the high bits of `hc` | `zxula.vhd:178,582-583` |
| CT-WIN-05 | B | 48K, `hc=256`, `vc=100` (first non-display column, `hc(8)=1`) | `wait_s == 0` (window gate off) | `zxula.vhd:583` |
| CT-WIN-06 | B | 48K, `hc=100`, `vc=192` (`border_active_v=1`) | `wait_s == 0` (window gate off) | `zxula.vhd:583` |
| CT-WIN-07 | B | 48K, `hc=100`, `vc=0..191` sweep, bank-5 memory cycle. Per-phase expectation: for `hc(3:0) ∈ {0,1,2}` added delay = 0 (`hc_adj(3:2)="00"`); for `hc(3:0) ∈ {3..14}` added delay = `pattern[hc & 7]` from `{6,5,4,3,2,1,0,0}`; for `hc(3:0) = 15` added delay = 0 (wrap) | Added T-states match the VHDL 7-phase stretch pattern exactly per the per-phase expectation above (VHDL synchronous variant — the emulator must assert the stretch on the *prior* cycle per `zxula.vhd:579-580` comment) | `zxula.vhd:579-583,587-595` |
| CT-WIN-08 | B | +3, `hc_adj=1` (phase where `hc_adj(3:1)="000"` AND `p3=1`) | `wait_s == 1` (+3-only extra phase) | `zxula.vhd:582-583` |
| CT-WIN-09 | B | 48K, `hc=16`, `vc=100` — `i_hc(3:0)=0000` → `hc_adj=0001`; `(3:2)="00"` | `wait_s == 0`. **Pairs with CT-WIN-04** to pin the 4-bit vs. 9-bit wrap discrimination: if `hc_adj` were computed 9-bit (uppermost bit preserved), this row would behave identically to CT-WIN-09 for both `hc=16` and `hc=0` because the low 4 bits are equal — confirming that only the low 4 bits matter, even across `hc_adj`'s decimal-16 "carry" position | `zxula.vhd:178,582-583` |
| CT-WIN-10 | B | 48K, `hc=7`, `vc=100` — `hc_adj=8`, `(3:2)=10`, `(3:1)=100`; 48K so clause-2 masked; `hc_adj(3)=1` | `wait_s == 1`; added delay = `pattern[hc & 7] = pattern[7] = 0`. Exercises the pattern-LUT bit-3 input branch; together with CT-WIN-02 (pattern[3]=3) and CT-WIN-03 (pattern[7]=0 in the wrap boundary case) distributes coverage across pattern-address bits | `zxula.vhd:582-583,587-595` |

### §10. 48K / 128K — clock-stretch path

VHDL: `zxula.vhd:587-595`:
```
process (i_CLK_CPU) ...
  mreq23_n    <= i_cpu_mreq_n;
  ioreqtw3_n  <= i_cpu_iorq_n or not i_contention_port;
...
o_cpu_contend <= '1' when
    ((i_contention_memory = '1' and mreq23_n = '1' and ioreqtw3_n = '1') or
     (i_contention_port = '1' and i_cpu_iorq_n = '0' and ioreqtw3_n = '1'))
    and i_timing_p3 = '0' and wait_s = '1' else '0';
```

So on 48K/128K the stretch fires only when:
- memory path: `contention_memory=1` AND MREQ went high last cycle AND
  IORQ (with port-contend enable) is high;
- OR port path: `contention_port=1` AND IORQ just went low AND was high
  last cycle;
- AND NOT +3 timing;
- AND `wait_s=1`.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-S48-01 | B | 48K, bank 5 memory read inside display window at a stretched phase | CPU tick count increased by VHDL-derived delay (non-zero, matches LUT at `(hc,vc)`) | `zxula.vhd:587-595` |
| CT-S48-02 | B | 48K, bank 5 memory read inside display window at a non-stretched phase | Zero added T-states | `zxula.vhd:582-595` |
| CT-S48-03 | B | 48K, bank 0 memory read (never contended page) | Zero added T-states | `zxula.vhd:595,zxnext.vhd:4490` |
| CT-S48-04 | B | 128K, bank 1 memory read inside display window at stretched phase | Delay matches LUT | `zxula.vhd:587-595,zxnext.vhd:4491` |
| CT-S48-05 | B | 128K, bank 4 memory read (even bank, not contended) | Zero added T-states | `zxnext.vhd:4491` |
| CT-S48-06 | B | 48K, I/O read to port 0xFE (even port, `port_contend=1`) inside display window at a stretched phase | `o_cpu_contend == 1` on the IORQ-going-low cycle (port path: `port_contend=1 and iorq_n=0 and ioreqtw3_n=1`); CPU T-state count increased by the VHDL-derived stretch count | `zxula.vhd:587-595,zxnext.vhd:4496` |
| CT-S48-07 | B | 48K, I/O read to port 0xFF (odd port, `port_contend=0`) inside display window | Zero added T-states from port-contend path (memory path not exercised) | `zxnext.vhd:4496` |
| CT-S48-08 | B | 48K, memory read outside display window (`border_active_v=1`) | Zero added T-states (window gate blocks) | `zxula.vhd:583` |

### §11. +3 — `WAIT_n` path

VHDL: `zxula.vhd:599-600`:
```
-- commented original combined memory+port form:
-- o_cpu_wait_n <= '0' when ((mreq=0 and contention_memory=1) or
--                           (iorq=0 and contention_port=1))
--              and timing_p3=1 and wait_s=1 else '1';
-- active form (memory-only):
o_cpu_wait_n <= '0' when (i_cpu_mreq_n = '0' and i_contention_memory = '1')
             and i_timing_p3 = '1' and wait_s = '1' else '1';
```

So on +3 (current VHDL): only memory cycles to contended banks stall the
CPU via `WAIT_n`. I/O-contention wait generation on +3 is **disabled**
in the live VHDL (the port clause is commented out at `zxula.vhd:599`);
this plan treats that as the spec, so CT-SP3-06/07 below test absence,
not presence, of the I/O-side WAIT.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-SP3-01 | B | +3, bank 4 memory read inside display window at stretched phase | `WAIT_n=0` for N cycles; CPU T-state count increased by LUT delay | `zxula.vhd:600` |
| CT-SP3-02 | B | +3, bank 7 memory read inside display window at stretched phase | Stall matches LUT | `zxula.vhd:600,zxnext.vhd:4492` |
| CT-SP3-03 | B | +3, bank 0 memory read (page bit 3 = 0) | Zero added T-states | `zxnext.vhd:4492` |
| CT-SP3-04 | B | +3, bank 4 memory read outside display window | Zero added T-states (`wait_s=0`) | `zxula.vhd:583,600` |
| CT-SP3-05 | B | +3, bank 4 memory read with `contention_disable=1` | Zero added T-states (enable gate off) | `zxnext.vhd:4481` |
| CT-SP3-06 | B | +3, I/O read to port 0xFE inside display window | Zero added T-states from WAIT path (live VHDL `o_cpu_wait_n` is memory-only per `zxula.vhd:600`) | `zxula.vhd:599-600` |
| CT-SP3-07 | B | +3, I/O read to port 0xFE inside display window, contended bank in MMU slot | Zero added T-states from WAIT path on I/O (same VHDL note) | `zxula.vhd:599-600` |
| CT-SP3-08 | B | +3, `hc_adj(3:1)=000` extra phase, bank 4 memory read | Stall asserts (+3-only wait-window extension, see §9 row CT-WIN-08) | `zxula.vhd:582-583,600` |

### §12. Pentagon and Next-turbo — never-contended paths

Origin: re-homed ULA rows S11.11 (Pentagon never), S11.12 (CPU speed
> 3.5 MHz disables).

VHDL: `zxnext.vhd:4481` (Pentagon and cpu_speed gates live on the
enable signal — see §4).

Note: Pentagon also does not have a `machine_timing_pentagon='1'` branch
in `mem_contend` (`zxnext.vhd:4489-4493`) — it falls through to the
default `'0'`. The MMU suite's CON-12a covers this switch-fallthrough
path; after the C2-move it is absorbed by CT-PENT-01 below (which runs
on an actual `MachineType::PENTAGON` and therefore exercises both the
enable gate and the mem-contend switch fallthrough together).

Trim rationale (critic BLOCKING #2): CT-TURBO-01/02/03 in the
pre-critic draft all exercised the single VHDL predicate `(not
cpu_speed(1)) and (not cpu_speed(0))` at `zxnext.vhd:4481`. The gate
bit-OR-reduces to "any non-zero speed disables"; speed value is not a
further predicate input. Collapsed to one row (CT-TURBO-01) driving
speed=1. CT-PENT-01/02/03 likewise all exercised the same
`machine_timing_pentagon=1` gate on different bank indices, but bank
index is not a predicate input once Pentagon is selected — the enable
gate masks `mem_contend` decode upstream. Collapsed to one row
(CT-PENT-01).

Post-trim §12 keeps: Pentagon gate (CT-PENT-01), Pentagon I/O
emulator-level (CT-PENT-04), Pentagon full-frame integration
(CT-PENT-05), bare-class turbo gate (CT-TURBO-01), NR 0x07 writer
(CT-TURBO-04), NR 0x08 writer (CT-TURBO-05), and `hc(8)` commit-edge
(CT-TURBO-06).

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-PENT-01 | A | Pentagon, `mem_active_page=0x0A` (would contend on 48K). Also covers the `MachineType::PENTAGON` switch-fallthrough at `src/memory/contention.cpp:87-90` (returns `false` regardless of page), i.e. the MMU suite's CON-12a | `is_contended_access() == false` | `zxnext.vhd:4481,4489-4493` |
| CT-PENT-04 | B | Pentagon, full `Emulator`, I/O port 0xFE (even) | Zero added T-states (enable gate blocks before `port_contend` decode) | `zxnext.vhd:4481` |
| CT-PENT-05 | C | Pentagon, full-emulator frame of known contended program, compare T-state count vs. 48K | Frame length matches Pentagon's `71680` T-state budget (`zxula_timing.vhd`) with NO contention added | `zxnext.vhd:4481` |
| CT-TURBO-01 | A | 48K, `cpu_speed=1` (7 MHz) on bare `ContentionModel`, `mem_active_page=0x0A` (bank 5) | `is_contended_access() == false` (enable gate off) | `zxnext.vhd:4481,5817` |
| CT-TURBO-04 | B | 48K, full `Emulator`, NR 0x07 write to `0x01`, then bank 5 memory read | Zero added T-states (NR 0x07 path must flow to `cpu_speed`) | `zxnext.vhd:5787-5790,5817` |
| CT-TURBO-05 | B | 48K, full `Emulator`, NR 0x08 write to bit-6=1, then bank 5 memory read | Zero added T-states (NR 0x08 bit 6 path must flow to `eff_nr_08_contention_disable`) | `zxnext.vhd:4481,5823` |
| CT-TURBO-06 | B | 48K, full `Emulator`, write NR 0x08 with bit 6 set **mid-scanline** (just after an `hc(8)=0→1` edge); issue a bank-5 memory read BEFORE the next `hc(8)` rising edge, then another AFTER it | Pre-commit read still contended (old `eff_nr_08_contention_disable` value); post-commit read uncontended. Pins the `if hc(8)='1'` commit gate at `zxnext.vhd:5822-5823` — mid-line NR 0x08 writes latch only on the next `hc(8)` rising edge, not combinatorially | `zxnext.vhd:5822-5823` |

Rows removed vs. the pre-critic draft:

- **CT-TURBO-02** (speed=2), **CT-TURBO-03** (speed=3) — duplicates of
  CT-TURBO-01; the VHDL gate is a bit-OR of the two speed bits, so
  any non-zero value suffices.
- **CT-PENT-02** (bank 3 on Pentagon), **CT-PENT-03** (bank 4 on
  Pentagon) — duplicates of CT-PENT-01; all three exercise the same
  `machine_timing_pentagon=1` gate, and bank index is not a predicate
  input once the enable gate masks `mem_contend`.

### §13. `p3_floating_bus_dat` capture on contended memory access

VHDL: `zxnext.vhd:4498-4509`:
```
process (i_CLK_CPU)
  if mem_contend = '1' and cpu_mreq_n = '0' then
    if cpu_rd_n = '0' then
      p3_floating_bus_dat <= cpu_di;
    elsif cpu_wr_n = '0' then
      p3_floating_bus_dat <= cpu_do;
    end if;
  end if;
end process;
```

This is the *capture* side of the +3 floating bus. The *read-back* side
(port 0xFF returns this value under +3 timing) is owned by the Floating
Bus plan (`doc/design/TASK-FLOATING-BUS-PLAN.md`). This plan owns the
capture only to prove that the contention decode correctly gates the
latch-write enable.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-FB-01 | B | +3, memory read from bank 4 at any display phase | `p3_floating_bus_dat` observably equals the byte read | `zxnext.vhd:4498-4505` |
| CT-FB-02 | B | +3, memory write to bank 4 at any display phase | `p3_floating_bus_dat` observably equals the byte written | `zxnext.vhd:4498-4508` |
| CT-FB-03 | B | +3, **prior-state step**: first run one contended bank-4 read/write to pre-seed `p3_floating_bus_dat` with a known byte `X`. Then issue a +3 memory read from bank 0 (not contended). | `p3_floating_bus_dat` remains at `X` — the latch is gated by `mem_contend`, so a non-contended access does NOT update it. Without the pre-seed step the row degenerates (read-back of an uninitialised value is compiler-undefined) | `zxnext.vhd:4498-4501` |
| CT-FB-04 | B | +3, I/O read (no MREQ) even to a contended page | `p3_floating_bus_dat` unchanged (capture gated on MREQ) | `zxnext.vhd:4501` |

### §14. Integration smoke (runtime drift)

These rows require full `Emulator` + cycle-accurate tick loop +
`ContentionModel::delay()` wired into the CPU's memory cycle. They are
integration-level regression guards.

| ID | Phase | Stimulus | Expected | VHDL |
|----|-------|----------|----------|------|
| CT-INT-01 | C | 48K, HALT-in-loop program sized to 1 frame, contention ON | Frame T-state count matches VHDL-derived total (`69888` + per-cycle stretch sum from LUT) | `zxula.vhd:582-595`, `zxula_timing.vhd` |
| CT-INT-02 | C | 48K, same program, contention OFF via NR 0x08 bit 6 | Frame T-state count matches uncontended 69888 baseline | `zxnext.vhd:4481,5823` |
| CT-INT-03 | C | Regression screenshot suite — running 48K contention-sensitive demo | Screenshot matches reference (baseline re-captured at the time contention lands) | — (regression-suite guard) |

### §15. Known-overlap and regression-risk notes

- **Migration of `test/mmu/mmu_test.cpp` CON-01..CON-12b**: the existing
  13 MMU rows (Cat-16) will migrate into this plan's suite per the
  C2-move commitment (see "Mirror vs move" above). Until migration
  lands, the two ID namespaces (`CT-*` here vs. `CON-*` in MMU) remain
  disjoint; after migration, `CON-*` identifiers no longer exist in the
  tree.
- **Screenshot regression risk**: any emulator change that lands
  `ContentionModel::delay()` on the tick path will change frame length
  on 48K/128K/+3 machines. Reference screenshots captured before that
  work will need regeneration (per `bash test/generate-references.sh`).
  The Phase-4 audit for the implementation wave must include a full
  `bash test/regression.sh` sweep with baseline refresh, and CHANGELOG
  + DEVELOPMENT-SESSIONS notes.
- **FUSE-table overlap**: jnext currently uses FUSE's
  `ula_contention[]` table path (`src/cpu/z80_cpu.cpp:33-122`). Landing
  VHDL-faithful contention requires deciding whether to retire that
  path entirely or keep it as a fallback for non-`ZXN_ISSUE2` machine
  builds. This is an implementation-plan decision, not a test-plan
  decision, but it must be surfaced in the Phase-0 review.

## Integration test suggestions

1. **Frame-timing smoke** — run a known contention-sensitive 48K
   program (e.g. `demo/rainbow.tap` or similar) for N frames in
   headless mode and compare T-state totals against an uncontended
   baseline. Acceptance criterion: total delta within 1% of the
   VHDL-derived expected stretch sum.
2. **Screenshot re-baseline** — the Phase-4 implementation audit
   MUST re-run `bash test/regression.sh` and refresh any references
   that shift due to frame-length changes. Flag all refreshes in the
   implementation's merge note.
3. **NextZXOS boot** — verify that enabling contention does not break
   NextZXOS boot (contention is NOT a boot-path gate: firmware writes
   NR 0x07 / NR 0x08 early and runs subsequent code at non-3.5 MHz
   speeds, so contention is typically disabled for most boot code
   paths; but the interaction must be watched for regressions).

## Nominal test count summary

| Section | Area | Tests |
|---------|------|------:|
| §4  | Enable gate | 8 |
| §5  | Memory — 48K | 5 |
| §6  | Memory — 128K | 3 |
| §7  | Memory — +3 | 3 |
| §8  | I/O port contention | 9 |
| §9  | Wait-pattern window | 10 |
| §10 | 48K / 128K clock-stretch | 8 |
| §11 | +3 `WAIT_n` | 8 |
| §12 | Pentagon + Next-turbo | 7 |
| §13 | Floating-bus capture | 4 |
| §14 | Integration smoke | 3 |
| | **Total** | **68** |

### Row phasing summary (C1 phase tags)

| Phase | Rows | Status |
|-------|-----:|--------|
| A | 28 | land green in first commit after the ContentionModel Phase-A VHDL audit; absorb the 13 CON-* rows at migration |
| B | 36 | skip(F) pending `delay()` tick-loop wiring |
| C | 4 | skip(F) pending Phase B; integration smoke / full-frame |

Phase A breakdown (28 rows):

- §4 CT-GATE-01..08 — 8 rows
- §5 CT-M48-01 / 03 / 05 / 06 / 08 — 5 rows
- §6 CT-M128-01 / 03 / 08 — 3 rows
- §7 CT-MP3-01 / 05 / 08 — 3 rows
- §8 CT-IO-01 / 02 / 03 / 04 / 07 / 08 / 09 — 7 rows
- §12 CT-PENT-01, CT-TURBO-01 — 2 rows

Phase B breakdown (36 rows):

- §8 CT-IO-05, CT-IO-06 — 2 rows
- §9 CT-WIN-01..10 — 10 rows
- §10 CT-S48-01..08 — 8 rows
- §11 CT-SP3-01..08 — 8 rows
- §12 CT-PENT-04, CT-TURBO-04, CT-TURBO-05, CT-TURBO-06 — 4 rows
- §13 CT-FB-01..04 — 4 rows

Phase C breakdown (4 rows):

- §12 CT-PENT-05 — 1 row
- §14 CT-INT-01..03 — 3 rows

Phase sum: 28 + 36 + 4 = **68 rows** ✓ (matches the section total
above).

Row-to-legacy-S11 mapping (the 12 legacy ULA S11 rows re-homed from
`doc/testing/ULA-VIDEO-TEST-PLAN-DESIGN.md` §11):

| Legacy ULA row | Landed here |
|---|---|
| S11.01 (48K bank-5 contention phase) | CT-M48-01, CT-S48-01 |
| S11.02 (48K bank-0 non-contended) | CT-M48-03, CT-S48-03 |
| S11.03 (48K `hc_adj(3:2)=00` non-contended phase) | CT-WIN-03, CT-S48-02 |
| S11.04 (48K `vc>=192` `border_active_v`) | CT-WIN-06, CT-S48-08 |
| S11.05 (48K even-port I/O) | CT-IO-01, CT-S48-06 |
| S11.06 (48K odd-port I/O) | CT-IO-02, CT-S48-07 |
| S11.07 (128K bank-1 odd) | CT-M128-01, CT-S48-04 |
| S11.08 (128K bank-4 non-contended) | CT-M128-03, CT-S48-05 |
| S11.09 (+3 bank ≥ 4 `WAIT_n`) | CT-MP3-01, CT-SP3-01 |
| S11.10 (+3 bank 0 non-contended) | CT-MP3-05, CT-SP3-03 |
| S11.11 (Pentagon never-contended) | CT-PENT-01 (+ CT-PENT-04/05 derivatives) |
| S11.12 (CPU speed > 3.5 MHz disables) | CT-TURBO-01 (+ CT-TURBO-04/05/06 derivatives) |

## Cross-reference — MMU `CON-*` rows (13 total) to `CT-*` rows

Accurate overlap table (critic BLOCKING #4 fix). Left column is the MMU
row ID in `test/mmu/mmu_test.cpp:2092+`; right column is the `CT-*` row
in this plan that owns the equivalent check after the C2-move completes.

| MMU row | Semantic overlap with this plan |
|---------|---------------------------------|
| CON-01 (48K page `0x0A` → contend) | CT-M48-01 |
| CON-02 (48K page `0x0B` → contend) | **folded into CT-M48-01** (duplicate facet; see §5 trim rationale) |
| CON-03 (48K page `0x00` → not contend) | CT-M48-03 |
| CON-04 (48K page `0x0E` → not contend) | CT-M48-05 |
| CON-05 (128K page `0x03` → contend, odd bank) | CT-M128-01 |
| CON-06 (128K page `0x04` → not contend, even bank) | CT-M128-03 |
| CON-07 (+3 page `0x08` → contend, bank 4) | CT-MP3-01 |
| CON-08 (+3 page `0x06` → not contend, bank 3) | CT-MP3-05 |
| CON-09 (48K page `0x10` → high-nibble guard blocks) | CT-M48-06 |
| CON-10 (NR 0x08 bit 6 contention disable) | CT-GATE-02 |
| CON-11 (any non-zero `cpu_speed` disables) | CT-GATE-03 / CT-GATE-04 / CT-GATE-05 |
| CON-12a (Pentagon machine-type switch fallthrough) | CT-PENT-01 (combined gate + switch check) |
| CON-12b (discriminative `pentagon_timing` gate) | CT-GATE-06 |

Count: 13 CON-* rows → 12 unique `CT-*` targets (CON-02 folds into
CT-M48-01 as an intentional duplicate-facet trim). CON-11 in the MMU
suite sweeps all three non-zero `cpu_speed` values in a single row;
in this plan those are separated across CT-GATE-03/04/05 for granular
reporting.

## Open questions

- **Per-bank tables location**: today the per-machine branch lives
  inside `ContentionModel::is_contended_access()` as straight-line
  code. Whether to extract a `PerMachineContentionTable` strategy
  object is an implementation call; the test plan is strategy-agnostic.
- **FUSE table retirement**: landing a VHDL-faithful per-cycle call
  likely obsoletes `ula_contention[]` in `src/cpu/z80_cpu.cpp:33-122`.
  Whether to keep the FUSE path as a fallback for non-Next machine
  builds (there are none in jnext today; all 48K/128K/+3/Pentagon
  builds share the `ZXN_ISSUE2` code path) is the call to resolve in
  the implementation plan's Phase 0.
- **+3 I/O contention regression guard**: the live VHDL at
  `zxula.vhd:599` disables port-side `WAIT_n` generation on +3 (the
  `or (iorq=0 and port=1)` clause is commented out). CT-SP3-06/07
  encode that explicit `skip-or-zero-delay` expectation. If a future
  VHDL update re-enables the clause, those two rows flip expected
  values and need a plan amendment.
- **Class-home / `MachineTiming` merge** — closed: `ContentionModel`
  already exists as a dedicated class at `src/memory/contention.h`
  and this plan targets it directly. No merge-into-`MachineTiming`
  path is contemplated.

## File layout

```
test/
  contention/
    contention_test.cpp         # all §4-§14 rows
  CMakeLists.txt                # add contention_test executable

doc/testing/
  CONTENTION-TEST-PLAN-DESIGN.md   # this document (renamed from
                                    # CONTENTION-MODEL-TEST-PLAN-DESIGN.md
                                    # per critic NIT N5)

doc/design/
  TASK-CONTENTION-MODEL-PLAN.md          # implementation plan (authored 2026-04-23)
```

## CMake integration (forecast)

```cmake
# Contention Model compliance tests
add_executable(contention_test
    contention/contention_test.cpp
)
target_link_libraries(contention_test PRIVATE jnext_core)
add_test(NAME contention_test COMMAND contention_test)
```

## How to run (forecast)

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run contention tests standalone
./build/test/contention_test

# Run full regression (includes contention_test once landed)
bash test/regression.sh
```
