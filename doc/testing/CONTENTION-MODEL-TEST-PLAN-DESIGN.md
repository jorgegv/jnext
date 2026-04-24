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

- **12 plan rows inherited from the ULA Video plan's §11** (`doc/testing/
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
    `is_contended_access()` — this is the target of the existing 12
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
- **Status summary**: when the un-skip pass runs, this plan will open
  with ~**0 rows live** (most rows start as `skip(F: ...)`) and, as the
  emulator work lands, will unlock the per-machine waves until all rows
  are either `check()` or explicitly `WONT`. The 12 existing CON-* rows
  in `test/mmu/mmu_test.cpp` remain the sole already-passing surface;
  this plan treats them as the bare-class slice and owns the
  full-emulator-level rows that they cannot exercise.

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

### Home of the contention logic (open question)

The `TASK-CONTENTION-MODEL-PLAN.md` open question "dedicated class vs.
merge into `MachineTiming`" is resolved by the existing code: a dedicated
`ContentionModel` class already exists and owns the per-bank tables, the
per-machine LUT, and the four VHDL gate inputs. **This plan targets that
class** as the primary object under test. `MachineTiming` stays the source
of truth for `hc_max`/`vc_max`/IRQ position and feeds `hc`/`vc` into
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

### Test runner

A new test binary `contention_test` at `test/contention/contention_test.cpp`
wired into `test/CMakeLists.txt`. The binary mirrors the harness style
used by `test/ctc_interrupts/` and `test/ula/`:

1. **Bare-class rows** (sections §4-§8, §12) — unit-level, construct a
   `ContentionModel`, seed inputs via the public setters, assert against
   `is_contended_access()` / `delay()` / `is_contended_address()`. No
   `Emulator` required. These overlap with the 12 existing CON-* rows
   in `test/mmu/mmu_test.cpp`; this plan owns them here and the MMU plan
   becomes a subset-mirror (both suites exercise the same surface; the
   duplication is intentional and cheap). Aggregator scripts must treat
   the two ID namespaces as disjoint (prefixed `CT-CON-*` here vs.
   `CON-*` there) — see "Section 16: Known cross-suite overlap" below.
2. **Wait-pattern / phase rows** (sections §9-§11) — construct a full
   `Emulator` fixture (same pattern as `test/ula/ula_test.cpp` `UlaBed`
   and `test/ctc_interrupts/` `EmuBed`), step the tick loop to a known
   `(hc, vc)` position via `MachineTiming` observables, issue a memory
   or I/O cycle to a known bank/page, and compare the added T-state
   count against the VHDL-derived expected value. Requires
   `ContentionModel::delay()` to be live on the tick path (currently
   **skip(F)**-blocked; see Current status point 2).
3. **Floating-bus-write rows** (§13) — full `Emulator` fixture; issue a
   contended memory read/write and verify `p3_floating_bus_dat` capture
   per `zxnext.vhd:4498-4509`. Cross-suite dependency noted: the
   floating-bus value readback on port 0xFF is tested in the Floating
   Bus plan; this plan owns the *capture* (write side) only.
4. **Integration smoke** (§14) — full emulator, machine configured to
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

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-GATE-01 | `ContentionModel` default-constructed, `build(ZX48K)`, `mem_active_page=0x0A` | `is_contended_access() == true` (enable=1, mem=1) | `zxnext.vhd:4481,4490` |
| CT-GATE-02 | ZX48K, `mem_active_page=0x0A`, `set_contention_disable(true)` | `is_contended_access() == false` (enable gate off) | `zxnext.vhd:4481` |
| CT-GATE-03 | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(1)` (7 MHz) | `is_contended_access() == false` (enable gate off) | `zxnext.vhd:4481,5817` |
| CT-GATE-04 | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(2)` (14 MHz) | `is_contended_access() == false` | `zxnext.vhd:4481,5817` |
| CT-GATE-05 | ZX48K, `mem_active_page=0x0A`, `set_cpu_speed(3)` (28 MHz) | `is_contended_access() == false` | `zxnext.vhd:4481,5817` |
| CT-GATE-06 | ZX48K, `mem_active_page=0x0A`, `set_pentagon_timing(true)` | `is_contended_access() == false` (discriminative over machine-type switch) | `zxnext.vhd:4481` |
| CT-GATE-07 | ZX48K, all gates off (`disable=false`, `speed=0`, `pentagon=false`), `mem_active_page=0x0A` | `is_contended_access() == true` | `zxnext.vhd:4481,4490` |
| CT-GATE-08 | Reset-default `ContentionModel` (no `build()` call), `mem_active_page=0x0A` | `is_contended_access()` returns the "default type" result per `contention.h:56` (documents power-on behaviour — captures regression if default type changes silently) | `src/memory/contention.h:56`; `zxnext.vhd:5800-5802` |

Note: CT-GATE-01..06 overlap the existing CON-10/11/12a/12b rows in
`test/mmu/mmu_test.cpp`. This plan supersedes them; the MMU rows stay as
a mirror but their authoritative owner is this plan.

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

Bank 5 maps to pages 0x0A (low half) and 0x0B (high half). Bank 0 maps
to 0x00/0x01. Only pages whose high nibble is 0 are eligible.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-M48-01 | 48K, `mem_active_page=0x0A` (bank 5 low half, `bits(3:1)=101`) | `is_contended_access() == true` | `zxnext.vhd:4490` |
| CT-M48-02 | 48K, `mem_active_page=0x0B` (bank 5 high half, `bits(3:1)=101`) | `is_contended_access() == true` | `zxnext.vhd:4490` |
| CT-M48-03 | 48K, `mem_active_page=0x00` (bank 0 low half, `bits(3:1)=000`) | `is_contended_access() == false` | `zxnext.vhd:4490` |
| CT-M48-04 | 48K, `mem_active_page=0x01` (bank 0 high half, `bits(3:1)=000`) | `is_contended_access() == false` | `zxnext.vhd:4490` |
| CT-M48-05 | 48K, `mem_active_page=0x0E` (bank 7 low half, `bits(3:1)=111`) | `is_contended_access() == false` | `zxnext.vhd:4490` |
| CT-M48-06 | 48K, `mem_active_page=0x10` (high nibble `!= 0`) | `is_contended_access() == false` (guard at `zxnext.vhd:4489`) | `zxnext.vhd:4489` |
| CT-M48-07 | 48K, `mem_active_page=0xE0` (ROM / high page) | `is_contended_access() == false` | `zxnext.vhd:4489` |
| CT-M48-08 | 48K, `mem_active_page=0xFF` (floating-bus sentinel) | `is_contended_access() == false` | `zxnext.vhd:4489` |

### §6. Memory contention — 128K

Origin: re-homed ULA rows S11.07 (bank-1 odd), S11.08 (bank-4 even).

VHDL: `zxnext.vhd:4491`:
```
'1' when machine_timing_128 = '1' and mem_active_page(1) = '1' else
```

Odd banks contend: 1, 3, 5, 7 → pages with low nibble bit 1 set, i.e.
bit 1 of the page index.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-M128-01 | 128K, `mem_active_page=0x02` (bank 1, `bit(1)=1`) | `is_contended_access() == true` | `zxnext.vhd:4491` |
| CT-M128-02 | 128K, `mem_active_page=0x03` (bank 1 high, `bit(1)=1`) | `is_contended_access() == true` | `zxnext.vhd:4491` |
| CT-M128-03 | 128K, `mem_active_page=0x04` (bank 2, `bit(1)=0`) | `is_contended_access() == false` | `zxnext.vhd:4491` |
| CT-M128-04 | 128K, `mem_active_page=0x08` (bank 4, `bit(1)=0`) | `is_contended_access() == false` | `zxnext.vhd:4491` |
| CT-M128-05 | 128K, `mem_active_page=0x0A` (bank 5, `bit(1)=1`) | `is_contended_access() == true` | `zxnext.vhd:4491` |
| CT-M128-06 | 128K, `mem_active_page=0x0E` (bank 7, `bit(1)=1`) | `is_contended_access() == true` | `zxnext.vhd:4491` |
| CT-M128-07 | 128K, `mem_active_page=0x00` (bank 0, `bit(1)=0`) | `is_contended_access() == false` | `zxnext.vhd:4491` |
| CT-M128-08 | 128K, `mem_active_page=0x10` (high nibble != 0) | `is_contended_access() == false` | `zxnext.vhd:4489` |

### §7. Memory contention — +3

Origin: re-homed ULA rows S11.09 (bank ≥ 4 WAIT path), S11.10 (bank 0
non-contended).

VHDL: `zxnext.vhd:4492`:
```
'1' when machine_timing_p3 = '1' and mem_active_page(3) = '1' else
```

Banks ≥ 4 contend: banks 4, 5, 6, 7 → pages with bit 3 of the page
index set. Note that +3 uses `WAIT_n` assertion rather than clock
stretching (see §11 for the wait-signal side).

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-MP3-01 | +3, `mem_active_page=0x08` (bank 4, `bit(3)=1`) | `is_contended_access() == true` | `zxnext.vhd:4492` |
| CT-MP3-02 | +3, `mem_active_page=0x0A` (bank 5, `bit(3)=1`) | `is_contended_access() == true` | `zxnext.vhd:4492` |
| CT-MP3-03 | +3, `mem_active_page=0x0C` (bank 6, `bit(3)=1`) | `is_contended_access() == true` | `zxnext.vhd:4492` |
| CT-MP3-04 | +3, `mem_active_page=0x0E` (bank 7, `bit(3)=1`) | `is_contended_access() == true` | `zxnext.vhd:4492` |
| CT-MP3-05 | +3, `mem_active_page=0x00` (bank 0, `bit(3)=0`) | `is_contended_access() == false` | `zxnext.vhd:4492` |
| CT-MP3-06 | +3, `mem_active_page=0x02` (bank 1, `bit(3)=0`) | `is_contended_access() == false` | `zxnext.vhd:4492` |
| CT-MP3-07 | +3, `mem_active_page=0x06` (bank 3, `bit(3)=0`) | `is_contended_access() == false` | `zxnext.vhd:4492` |
| CT-MP3-08 | +3, ROM access (e.g. `mem_active_page >= 0xF0`) | `is_contended_access() == false` (ROM is never contended; guard at :4489) | `zxnext.vhd:4489` |

### §8. I/O port contention

Origin: re-homed ULA rows S11.05 (even port contended), S11.06 (odd
port not contended).

VHDL: `zxnext.vhd:4496`:
```
port_contend <= (not cpu_a(0)) or port_7ffd_active or port_bf3b or port_ff3b;
```

Additional context: `port_7ffd_active` active on 128K/+3 timing only
(`zxnext.vhd:2594`), `port_bf3b`/`port_ff3b` are ULA+ index/data ports
(`zxnext.vhd:2685-2686`).

These rows are bare-class and can be driven via a helper that simulates
the port-address → `port_contend` decode without the full `Emulator`.
Where the helper does not exist (today), rows start as `skip(F:
port_contend decode not exposed)`.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-IO-01 | 48K, `cpu_a=0xFE` (even port, e.g. ULA) | `port_contend == 1` → contended I/O cycle | `zxnext.vhd:4496` |
| CT-IO-02 | 48K, `cpu_a=0xFF` (odd port, non-ULA) | `port_contend == 0` → non-contended | `zxnext.vhd:4496` |
| CT-IO-03 | 48K, `cpu_a=0x00` (even, lowest) | `port_contend == 1` | `zxnext.vhd:4496` |
| CT-IO-04 | 48K, `cpu_a=0x01` (odd, lowest) | `port_contend == 0` | `zxnext.vhd:4496` |
| CT-IO-05 | 128K, `cpu_a=0x7FFD` (odd, but `port_7ffd_active=1`) | `port_contend == 1` (OR-term via `port_7ffd_active`) | `zxnext.vhd:4496,2594` |
| CT-IO-06 | 48K, `cpu_a=0x7FFD` (`port_7ffd_active=0` on 48K timing) | `port_contend == 0` (no OR-term; odd bit gate is `(not cpu_a(0))=0`) | `zxnext.vhd:4496,2594` |
| CT-IO-07 | Any timing, `cpu_a=0xBF3B` (ULA+ index, odd) | `port_contend == 1` via `port_bf3b` OR-term (when `port_ulap_io_en=1`) | `zxnext.vhd:4496,2685` |
| CT-IO-08 | Any timing, `cpu_a=0xFF3B` (ULA+ data, odd) | `port_contend == 1` via `port_ff3b` OR-term (when `port_ulap_io_en=1`) | `zxnext.vhd:4496,2686` |
| CT-IO-09 | Any timing, `cpu_a=0xBF3B`, `port_ulap_io_en=0` | `port_contend == 0` (ULA+ OR-term masked) | `zxnext.vhd:4496,2685` |

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

So wait is asserted when:
- horizontal is inside display window (`hc < 256`, i.e. `hc(8)=0`);
- vertical is inside display window (`vc < 192`, i.e. `border_active_v=0`);
- phase of the 16-cycle group is 3-14 (48K/128K) or 1-14 (+3);
- AND the enable gate is on.

These rows require driving a full `Emulator` to a specific `(hc, vc)`
position, so they start as `skip(F: runtime hc/vc driving)` pending
§10/§11 wiring.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-WIN-01 | 48K, `hc=0`, `vc=0` (top-left of display window) | `wait_s == 0`: at `hc=0`, `hc_adj=0+1=1` → `hc_adj(3:2)="00"` (clause 1 false); on 48K `timing_p3=0` so clause 2 masked. Boundary row: captures regression if the `+1` offset is dropped | `zxula.vhd:582-583` |
| CT-WIN-02 | 48K, `hc=3`, `vc=100` (phase boundary low) | Compute `hc_adj = 4 → (3:2)=01 ≠ 00` → `wait_s=1` | `zxula.vhd:582-583` |
| CT-WIN-03 | 48K, `hc=15`, `vc=100` (end of 16-phase group) | `hc_adj=16(0x10) → (3:2)=00` → `wait_s=0` | `zxula.vhd:582-583` |
| CT-WIN-04 | 48K, `hc=255`, `vc=100` (last display column) | `wait_s == 0`: `i_hc(3:0)="1111"`, `hc_adj=0000` after 4-bit wrap of `15+1`; same truncation as CT-WIN-03. Discriminates the `hc(8)=0` gate from the 4-bit wrap (if the `+1` were computed at 9-bit width without truncation, `hc_adj(3:2)` would be `00` here but `01` at `hc=16` — this row pins the last display-column edge) | `zxula.vhd:178,582-583` |
| CT-WIN-05 | 48K, `hc=256`, `vc=100` (first non-display column, `hc(8)=1`) | `wait_s == 0` (window gate off) | `zxula.vhd:583` |
| CT-WIN-06 | 48K, `hc=100`, `vc=192` (`border_active_v=1`) | `wait_s == 0` (window gate off) | `zxula.vhd:583` |
| CT-WIN-07 | 48K, `hc=100`, `vc=0..191` sweep, bank-5 memory cycle | Added T-states match the VHDL 7-phase stretch pattern {6,5,4,3,2,1,0,0} mod 8 aligned to `hc_adj` (VHDL synchronous variant — the emulator must assert the stretch on the *prior* cycle per `zxula.vhd:579-580` comment) | `zxula.vhd:579-583,587-595` |
| CT-WIN-08 | +3, `hc_adj=1` (phase where `hc_adj(3:1)="000"` AND `p3=1`) | `wait_s == 1` (+3-only extra phase) | `zxula.vhd:582-583` |

Note: CT-WIN-01 intentionally encodes a known-tricky boundary case; the
VHDL-derived expected value is `wait_s=0` at `hc=0` because `hc_adj=1`
has `hc_adj(3:2)="00"`. Any test author resurrecting this row must walk
the arithmetic, not hand-wave it.

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

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-S48-01 | 48K, bank 5 memory read inside display window at a stretched phase | CPU tick count increased by VHDL-derived delay (non-zero, matches LUT at `(hc,vc)`) | `zxula.vhd:587-595` |
| CT-S48-02 | 48K, bank 5 memory read inside display window at a non-stretched phase | Zero added T-states | `zxula.vhd:582-595` |
| CT-S48-03 | 48K, bank 0 memory read (never contended page) | Zero added T-states | `zxula.vhd:595,zxnext.vhd:4490` |
| CT-S48-04 | 128K, bank 1 memory read inside display window at stretched phase | Delay matches LUT | `zxula.vhd:587-595,zxnext.vhd:4491` |
| CT-S48-05 | 128K, bank 4 memory read (even bank, not contended) | Zero added T-states | `zxnext.vhd:4491` |
| CT-S48-06 | 48K, I/O read to port 0xFE (even port, `port_contend=1`) inside display window at a stretched phase | `o_cpu_contend == 1` on the IORQ-going-low cycle (port path: `port_contend=1 and iorq_n=0 and ioreqtw3_n=1`); CPU T-state count increased by the VHDL-derived stretch count | `zxula.vhd:587-595,zxnext.vhd:4496` |
| CT-S48-07 | 48K, I/O read to port 0xFF (odd port, `port_contend=0`) inside display window | Zero added T-states from port-contend path (memory path not exercised) | `zxnext.vhd:4496` |
| CT-S48-08 | 48K, memory read outside display window (`border_active_v=1`) | Zero added T-states (window gate blocks) | `zxula.vhd:583` |

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

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-SP3-01 | +3, bank 4 memory read inside display window at stretched phase | `WAIT_n=0` for N cycles; CPU T-state count increased by LUT delay | `zxula.vhd:600` |
| CT-SP3-02 | +3, bank 7 memory read inside display window at stretched phase | Stall matches LUT | `zxula.vhd:600,zxnext.vhd:4492` |
| CT-SP3-03 | +3, bank 0 memory read (page bit 3 = 0) | Zero added T-states | `zxnext.vhd:4492` |
| CT-SP3-04 | +3, bank 4 memory read outside display window | Zero added T-states (`wait_s=0`) | `zxula.vhd:583,600` |
| CT-SP3-05 | +3, bank 4 memory read with `contention_disable=1` | Zero added T-states (enable gate off) | `zxnext.vhd:4481` |
| CT-SP3-06 | +3, I/O read to port 0xFE inside display window | Zero added T-states from WAIT path (live VHDL `o_cpu_wait_n` is memory-only per `zxula.vhd:600`) | `zxula.vhd:599-600` |
| CT-SP3-07 | +3, I/O read to port 0xFE inside display window, contended bank in MMU slot | Zero added T-states from WAIT path on I/O (same VHDL note) | `zxula.vhd:599-600` |
| CT-SP3-08 | +3, `hc_adj(3:1)=000` extra phase, bank 4 memory read | Stall asserts (+3-only wait-window extension, see §9 row CT-WIN-08) | `zxula.vhd:582-583,600` |

### §12. Pentagon and Next-turbo — never-contended paths

Origin: re-homed ULA rows S11.11 (Pentagon never), S11.12 (CPU speed
> 3.5 MHz disables).

VHDL: `zxnext.vhd:4481` (Pentagon and cpu_speed gates live on the
enable signal — see §4).

Note: Pentagon also does not have a `machine_timing_pentagon='1'` branch
in `mem_contend` (`zxnext.vhd:4489-4493`) — it falls through to the
default `'0'`. CON-12a/b in `test/mmu/mmu_test.cpp` exercises this
machine-type fallthrough separately from the `pentagon_timing` gate.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-PENT-01 | Pentagon, `mem_active_page=0x0A` (would contend on 48K) | `is_contended_access() == false` | `zxnext.vhd:4481,4489-4493` |
| CT-PENT-02 | Pentagon, `mem_active_page=0x03` (would contend on 128K) | `is_contended_access() == false` | `zxnext.vhd:4481,4489-4493` |
| CT-PENT-03 | Pentagon, `mem_active_page=0x08` (would contend on +3) | `is_contended_access() == false` | `zxnext.vhd:4481,4489-4493` |
| CT-PENT-04 | Pentagon, I/O port 0xFE (even) | Zero added T-states (enable gate blocks before `port_contend` decode) | `zxnext.vhd:4481` |
| CT-PENT-05 | Pentagon, full-emulator frame of known contended program, compare T-state count vs. 48K | Frame length matches Pentagon's `71680` T-state budget (`zxula_timing.vhd`) with NO contention added | `zxnext.vhd:4481` |
| CT-TURBO-01 | 48K, `cpu_speed=1` (7 MHz), bank 5 memory read | Zero added T-states | `zxnext.vhd:4481,5817` |
| CT-TURBO-02 | 48K, `cpu_speed=2` (14 MHz), bank 5 memory read | Zero added T-states | `zxnext.vhd:4481,5817` |
| CT-TURBO-03 | 48K, `cpu_speed=3` (28 MHz), bank 5 memory read | Zero added T-states | `zxnext.vhd:4481,5817` |
| CT-TURBO-04 | 48K, NR 0x07 write to `0x01`, then bank 5 memory read | Zero added T-states (NR 0x07 path must flow to `cpu_speed`) | `zxnext.vhd:5787-5790,5817` |
| CT-TURBO-05 | 48K, NR 0x08 write to bit-6=1, then bank 5 memory read | Zero added T-states (NR 0x08 bit 6 path must flow to `eff_nr_08_contention_disable`) | `zxnext.vhd:4481,5823` |

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

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-FB-01 | +3, memory read from bank 4 at any display phase | `p3_floating_bus_dat` observably equals the byte read | `zxnext.vhd:4498-4505` |
| CT-FB-02 | +3, memory write to bank 4 at any display phase | `p3_floating_bus_dat` observably equals the byte written | `zxnext.vhd:4498-4508` |
| CT-FB-03 | +3, memory read from bank 0 (not contended) | `p3_floating_bus_dat` unchanged from prior value | `zxnext.vhd:4498-4501` |
| CT-FB-04 | +3, I/O read (no MREQ) even to a contended page | `p3_floating_bus_dat` unchanged (capture gated on MREQ) | `zxnext.vhd:4501` |

### §14. Integration smoke (runtime drift)

These rows require full `Emulator` + cycle-accurate tick loop +
`ContentionModel::delay()` wired into the CPU's memory cycle. They are
integration-level regression guards.

| ID | Stimulus | Expected | VHDL |
|----|----------|----------|------|
| CT-INT-01 | 48K, HALT-in-loop program sized to 1 frame, contention ON | Frame T-state count matches VHDL-derived total (`69888` + per-cycle stretch sum from LUT) | `zxula.vhd:582-595`, `zxula_timing.vhd` |
| CT-INT-02 | 48K, same program, contention OFF via NR 0x08 bit 6 | Frame T-state count matches uncontended 69888 baseline | `zxnext.vhd:4481,5823` |
| CT-INT-03 | Regression screenshot suite — running 48K contention-sensitive demo | Screenshot matches reference (baseline re-captured at the time contention lands) | — (regression-suite guard) |

### §15. Known-overlap and regression-risk notes

- **Overlap with `test/mmu/mmu_test.cpp` CON-01..CON-12b**: the existing
  MMU rows (Cat-16) mirror the bare-class subset of §4-§7 of this plan.
  They are retained as a cross-check only; the authoritative owner is
  this plan. ID namespaces are disjoint (`CT-*` vs. `CON-*`).
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
| §5  | Memory — 48K | 8 |
| §6  | Memory — 128K | 8 |
| §7  | Memory — +3 | 8 |
| §8  | I/O port contention | 9 |
| §9  | Wait-pattern window | 8 |
| §10 | 48K / 128K clock-stretch | 8 |
| §11 | +3 `WAIT_n` | 8 |
| §12 | Pentagon + Next-turbo | 10 |
| §13 | Floating-bus capture | 4 |
| §14 | Integration smoke | 3 |
| | **Total** | **82** |

Row-to-legacy-S11 mapping (the 12 rows re-homed from
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
| S11.08 (128K bank-4 non-contended) | CT-M128-04, CT-S48-05 |
| S11.09 (+3 bank ≥ 4 `WAIT_n`) | CT-MP3-01, CT-SP3-01 |
| S11.10 (+3 bank 0 non-contended) | CT-MP3-05, CT-SP3-03 |
| S11.11 (Pentagon never-contended) | CT-PENT-01..05 |
| S11.12 (CPU speed > 3.5 MHz disables) | CT-TURBO-01..05 |

## Open questions

- **Class home** (from `TASK-CONTENTION-MODEL-PLAN.md`): plan assumes
  `ContentionModel` stays a dedicated class. If implementation folds
  it into `MachineTiming`, the bare-class rows (§4-§7, §12) re-target
  the `MachineTiming` surface; wait-pattern rows (§9-§11) are unaffected.
- **Per-bank tables location** (from `TASK-CONTENTION-MODEL-PLAN.md`):
  today the per-machine branch lives inside
  `ContentionModel::is_contended_access()` as straight-line code.
  Whether to extract a `PerMachineContentionTable` strategy object is
  an implementation call; the test plan is strategy-agnostic.
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

## File layout

```
test/
  contention/
    contention_test.cpp         # all §4-§14 rows
  CMakeLists.txt                # add contention_test executable

doc/testing/
  CONTENTION-MODEL-TEST-PLAN-DESIGN.md   # this document

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
