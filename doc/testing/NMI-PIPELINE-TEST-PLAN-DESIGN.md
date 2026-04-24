# NMI Source Pipeline Compliance Test Plan

VHDL-derived compliance test plan for the **NMI source / arbiter**
subsystem of the JNEXT ZX Spectrum Next emulator. All specifications are
extracted directly from the FPGA VHDL source, per the VHDL-as-oracle rule
in [`UNIT-TEST-PLAN-EXECUTION.md`](UNIT-TEST-PLAN-EXECUTION.md).

Plan authored 2026-04-24 during Phase 0 of
[`TASK-NMI-SOURCE-PIPELINE-PLAN.md`](../design/TASK-NMI-SOURCE-PIPELINE-PLAN.md).

## Purpose

The ZX Spectrum Next has three NMI producer paths (Multiface button,
DivMMC button, ExpBus pin), two software-NMI strobes via NR 0x02
(CPU write and Copper MOVE), a 4-state FSM that gates `/NMI` assertion
on the Z80, and a fixed priority arbiter (MF > DivMMC > ExpBus). The
mechanism is used by Multiface, DivMMC entry-point automap, Copper
`ARB-06`, and the NR 0xCC bit-7 NMI-driven DMA-delay path.

This test plan ensures the emulator faithfully reproduces the VHDL
behaviour of the central NMI source subsystem (new class
`NmiSource` per the plan doc). It covers:

- Reset defaults for the FSM and all latches.
- Each of the three producer paths with its enable gate.
- FSM state transitions (IDLE → FETCH → HOLD → END → IDLE).
- Arbiter priority and consumer-feedback hold signals.
- The four gate registers (NR 0x06 bits 3/4, NR 0x81 bit 5, port 0xE3
  bit 7, `nr_03_config_mode`).
- NR 0x02 read / write bit layout and auto-clear on FSM END.
- NMI-activated feed into `Im2Controller::update_im2_dma_delay`
  (VHDL:2007).
- Z80 /NMI assertion and PC vectoring to 0x0066.

## Scope pointer

This plan is the design document for the **new `test/nmi/nmi_test.cpp`
suite** introduced by
[`TASK-NMI-SOURCE-PIPELINE-PLAN.md`](../design/TASK-NMI-SOURCE-PIPELINE-PLAN.md).

It also serves as the design reference for the cross-suite un-skips in
`copper_test.cpp` (ARB-06), `divmmc_test.cpp` (NM-01..08), and
`ctc_test.cpp` (DMA-04) that the plan flips in Phase 3.

## Current status

(blank — suite does not yet exist; will fill in Phase 1 when the
scaffold row lands at 1/1/0/0.)

## VHDL authority

Treat these as the authoritative spec. All emulator behaviour is
VHDL-faithful unless explicitly justified otherwise.

| File | Lines | Content |
|---|---|---|
| `cores/zxnext/src/zxnext.vhd` | 2089-2093 | `nmi_assert_mf / _divmmc / _expbus` combinatorial producers + enable gates |
| `cores/zxnext/src/zxnext.vhd` | 2095-2116 | Latch priority and clear paths (`nmi_mf / _divmmc / _expbus` registers) |
| `cores/zxnext/src/zxnext.vhd` | 2118 | `nmi_hold` selector (mf → divmmc → expbus) |
| `cores/zxnext/src/zxnext.vhd` | 2120-2162 | `nmi_state_t` FSM (IDLE / FETCH / HOLD / END) and transitions |
| `cores/zxnext/src/zxnext.vhd` | 2164-2170 | `nmi_generate_n` (Z80 /NMI drive), `nmi_mf_button` / `nmi_divmmc_button` strobes, `nmi_accept_cause` |
| `cores/zxnext/src/zxnext.vhd` | 1841 | `z80_nmi_n <= nmi_generate_n` (Z80 direct drive) |
| `cores/zxnext/src/zxnext.vhd` | 2052-2067 | Stackless NMI (out of scope, Wave D cut per plan Q1) |
| `cores/zxnext/src/zxnext.vhd` | 2007 | `im2_dma_delay <= im2_dma_int OR (nmi_activated AND nr_cc_dma_int_en_0_7) OR (…)` |
| `cores/zxnext/src/zxnext.vhd` | 3830-3872 | NR 0x02 bit 2/3 CPU + Copper write → `nmi_cpu_02_we / nmi_cu_02_we / nmi_gen_nr_mf / nmi_gen_nr_divmmc / nmi_sw_gen_mf / nmi_sw_gen_divmmc` |
| `cores/zxnext/src/zxnext.vhd` | 1109-1110 | NR 0x06 bit 3 (`button_m1_nmi_en`) / bit 4 (`button_drive_nmi_en`) |
| `cores/zxnext/src/zxnext.vhd` | 1222 | NR 0x81 bit 5 (`expbus_nmi_debounce_disable`) |
| `cores/zxnext/src/zxnext.vhd` | 5891 | NR 0x02 readback (bits 3/2 auto-cleared by FSM) |
| `cores/zxnext/src/zxnext.vhd` | 6230 | NR 0xC0 readback (bit 3 `stackless_nmi`, Wave D cut) |
| `cores/zxnext/src/zxnext.vhd` | 6331-6349 | Hotkey F9 (`hotkey_m1`, MF) / F10 (`hotkey_drive`, DivMMC) edge detection |
| `cores/zxnext/src/device/divmmc.vhd` | 103-150 | `button_nmi` latch set / clear / `o_disable_nmi` output |
| `cores/zxnext/src/device/multiface.vhd` | 134-148, 167-191 | MF NMI_ACTIVE + MF_ENABLE state machines (stub-only in this plan) |
| `cores/zxnext/src/zxnext_top_issue4.vhd` | 1773-1774, 1951-1955 | Bus-NMI pin + PS/2 F9/F10 → keyboard hotkey latch |

## Architecture

### Producers (three paths)

Per `zxnext.vhd:2089-2093`:

```
nmi_assert_expbus <= '1' when expbus_eff_en = '1' and expbus_eff_disable_mem = '0' and i_BUS_NMI_n = '0' else '0';
nmi_assert_mf     <= '1' when (hotkey_m1    = '1' or nmi_sw_gen_mf     = '1') and nr_06_button_m1_nmi_en    = '1' else '0';
nmi_assert_divmmc <= '1' when (hotkey_drive = '1' or nmi_sw_gen_divmmc = '1') and nr_06_button_drive_nmi_en = '1' else '0';
```

Each producer has an OR'd hotkey-or-software input gated by its NR 0x06
enable. The ExpBus path has its own expbus-effective gates (no NR 0x06
involvement).

The test suite models each producer independently and in combination.

### Arbiter (fixed priority MF > DivMMC > ExpBus)

Per `zxnext.vhd:2095-2118`, three latches are set combinatorially from
the assert signals but cleared together on FSM `S_NMI_END`:

- `nmi_mf` set iff `nmi_assert_mf = 1`, cleared on END or reset.
- `nmi_divmmc` set iff `nmi_assert_divmmc = 1` AND `NOT nmi_mf`
  (MF priority).
- `nmi_expbus` set iff `nmi_assert_expbus = 1` AND `NOT (nmi_mf OR
  nmi_divmmc)` (ExpBus lowest priority).

`nmi_hold` is selected on the matching consumer-feedback signal:
`mf_nmi_hold` if MF is the active cause, `divmmc_nmi_hold` otherwise.

### FSM (4 states)

Per `zxnext.vhd:2120-2162`:

| State | Entry condition | Exit transition |
|---|---|---|
| `S_NMI_IDLE` | reset / END complete / config_mode | `nmi_activated = 1` → `S_NMI_FETCH` |
| `S_NMI_FETCH` | FSM took request | M1 fetch at 0x0066 → `S_NMI_HOLD` |
| `S_NMI_HOLD` | vector fetched | `nmi_hold = 0` → `S_NMI_END` |
| `S_NMI_END` | consumer done | `cpu_wr_n` rising edge → `S_NMI_IDLE` (latches clear here) |

`nr_03_config_mode = 1` force-clears latches from any state
(VHDL:2102-2105).

### Z80 drive

Per `zxnext.vhd:1841, 2164-2170`: `z80_nmi_n <= nmi_generate_n`
where `nmi_generate_n = '0'` while FSM is in `S_NMI_FETCH` or later
(i.e. `/NMI` asserted from request-taken to handler-done).

### Gate registers

| Register | Bit | Signal | VHDL cite | Effect |
|---|---|---|---|---|
| NR 0x06 | 3 | `button_m1_nmi_en` | zxnext.vhd:1110 | gate MF producer |
| NR 0x06 | 4 | `button_drive_nmi_en` | zxnext.vhd:1109 | gate DivMMC producer |
| NR 0x81 | 5 | `expbus_nmi_debounce_disable` | zxnext.vhd:1222 | disable debounce on ExpBus pin |
| port 0xE3 | 7 | CONMEM | divmmc.vhd | blocks MF assertion (external gate) |
| NR 0x03 | — | `nr_03_config_mode` | force-clear latches | all latches → 0 while config |

### Consumer-feedback hold signals

- `divmmc_nmi_hold` ← `DivMmc::is_nmi_hold() = automap_held_ OR button_nmi_`
  per VHDL `o_disable_nmi` (`divmmc.vhd:150`).
- `mf_nmi_hold` — stubbed `false` in this plan (Multiface lands in Task 8).
- `mf_is_active` — stubbed `false` in this plan (blocks DivMMC assertion
  when true; Task 8 wires real Multiface state).

### Software NMI via NR 0x02

Per `zxnext.vhd:3830-3872`:

- Bit 3: strobe MF NMI request (`nmi_gen_nr_mf`).
- Bit 2: strobe DivMMC NMI request (`nmi_gen_nr_divmmc`).
- Copper MOVE to NR 0x02 uses a separate write-enable
  (`nmi_cu_02_we`) but lands in the same request latches; Copper
  writes in jnext route through `Emulator::write_nextreg` →
  `NextReg::write`, so no additional Copper hook is required
  (plan doc §"Copper NR-write path — audit complete").
- NR 0x02 readback at `zxnext.vhd:5891`:
  `bus_reset << 7 | iotrap << 4 | mf_pending << 3 | divmmc_pending << 2 | reset_type`.
  The `mf_pending` / `divmmc_pending` bits auto-clear when the FSM
  transitions through `S_NMI_END`.

### NMI-activated → DMA delay

Per `zxnext.vhd:2007`:

```
im2_dma_delay <= im2_dma_int OR
                 (nmi_activated AND nr_cc_dma_int_en_0_7) OR
                 ...
```

`nmi_activated` is true while any of the three request latches is set
(i.e. the FSM has taken a request and has not yet completed END).

## Test Case Catalogue

Row counts below sum to **49** across 11 groups. Each row carries a
short VHDL cite. Row IDs follow the naming pattern `GROUP-NN`.

### Group RST — Reset defaults (3 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| RST-01 | FSM in `S_NMI_IDLE` after reset | zxnext.vhd:2120, 2149 | reset; read FSM state accessor |
| RST-02 | All three request latches clear after reset | zxnext.vhd:2095-2105 | reset; read `nmi_mf` / `_divmmc` / `_expbus` accessors |
| RST-03 | Gate flags at VHDL power-on values (MF-en = 0, DivMMC-en = 0, expbus-debounce = 0) | zxnext.vhd:1109-1110, 1222 | reset; read gate accessors |

### Group NR02 — NR 0x02 software NMI (Wave A) (6 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| NR02-01 | NR 0x02 write bit 3 → MF request pending | zxnext.vhd:3830-3838 | write 0x08 to NR 0x02; observe `mf_pending` |
| NR02-02 | NR 0x02 write bit 2 → DivMMC request pending | zxnext.vhd:3830-3838 | write 0x04 to NR 0x02; observe `divmmc_pending` |
| NR02-03 | NR 0x02 write bits 3+2 together → MF wins priority | zxnext.vhd:2097-2105, 3830-3838 | write 0x0C; expect MF cause, not DivMMC |
| NR02-04 | NR 0x02 readback bit layout | zxnext.vhd:5891 | write 0x0C; read NR 0x02; expect bits 3/2 = 1/1 pre-END |
| NR02-05 | NR 0x02 readback bits 3/2 auto-clear on FSM `S_NMI_END` | zxnext.vhd:5891, 2149-2162 | drive FSM through END; read NR 0x02; expect bits 3/2 = 0 |
| NR02-06 | Copper MOVE to NR 0x02 reaches same request latches | zxnext.vhd:3830-3872, copper.cpp:148 | strobe NmiSource via Emulator::write_nextreg(0x02, 0x04); observe `divmmc_pending` |

### Group HK — Hotkey producers (Wave B) (5 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| HK-01 | `set_mf_button(true)` edge → FSM takes MF request | zxnext.vhd:2089, 6331 | strobe `set_mf_button`; FSM advances IDLE → FETCH |
| HK-02 | `set_divmmc_button(true)` edge → FSM takes DivMMC request | zxnext.vhd:2090, 6349 | strobe `set_divmmc_button`; FSM advances IDLE → FETCH |
| HK-03 | NR 0x06 bit 3 = 0 blocks MF producer | zxnext.vhd:1109, 2089 | set NR 0x06 bit 3 = 0; strobe MF button; expect no FSM advance |
| HK-04 | NR 0x06 bit 4 = 0 blocks DivMMC producer | zxnext.vhd:1110, 2090 | set NR 0x06 bit 4 = 0; strobe DivMMC button; expect no FSM advance |
| HK-05 | Simultaneous MF + DivMMC button press → MF wins | zxnext.vhd:2097-2105 | strobe both; observe `accept_cause = MF` |

### Group DIS — DivMMC consumer (Wave B) (4 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| DIS-01 | FSM IDLE→FETCH for DivMMC path calls `DivMmc::set_button_nmi(true)` | zxnext.vhd:2170, divmmc.vhd:108-111 | strobe DivMMC producer; observe `DivMmc::button_nmi()` flips to 1 |
| DIS-02 | `DivMmc::is_nmi_hold()` feedback drives `divmmc_nmi_hold` at arbiter | divmmc.vhd:150, zxnext.vhd:2118 | set DivMmc automap_held_; assert `divmmc_nmi_hold` reads true at NmiSource |
| DIS-03 | `o_disable_nmi = automap_held OR button_nmi` observable on DivMmc | divmmc.vhd:150 | set both flags independently; accessor returns correct OR |
| DIS-04 | FSM HOLD → END on `divmmc_nmi_hold = 0` | zxnext.vhd:2135-2148 | drive FSM to HOLD; clear hold; expect transition to END |

### Group CLR — DivMMC clear paths (Wave B) (4 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| CLR-01 | reset clears `button_nmi_` | divmmc.vhd:108 | set button_nmi; reset; expect cleared (baseline; already works) |
| CLR-02 | `automap_reset` clears `button_nmi_` (new hook) | divmmc.vhd:108 | set button_nmi; strobe automap_reset; expect cleared |
| CLR-03 | `on_retn_seen()` clears `button_nmi_` (new hook) | divmmc.vhd:108 | set button_nmi; call on_retn_seen; expect cleared |
| CLR-04 | `automap_held = 1` one-shot clears `button_nmi_` | divmmc.vhd:112-113 | set button_nmi; raise automap_held edge; expect cleared |

### Group GATE — Gate registers (Wave C) (8 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| GATE-01 | NR 0x06 bit 3 decode sets `set_mf_enable` | zxnext.vhd:1110 | write NR 0x06; observe NmiSource MF-enable accessor |
| GATE-02 | NR 0x06 bit 4 decode sets `set_divmmc_enable` | zxnext.vhd:1109 | write NR 0x06; observe DivMMC-enable accessor |
| GATE-03 | NR 0x81 bit 5 decode sets `set_expbus_debounce_disable` | zxnext.vhd:1222 | write NR 0x81; observe ExpBus-debounce accessor |
| GATE-04 | port 0xE3 bit 7 (CONMEM) blocks MF assertion | divmmc.vhd, zxnext.vhd:2107 | set DivMmc CONMEM; strobe MF button; expect no FSM advance |
| GATE-05 | `nr_03_config_mode = 1` force-clears all three latches | zxnext.vhd:2102-2105 | set MF + DivMMC + ExpBus; raise config_mode; expect all cleared |
| GATE-06 | `nr_03_config_mode = 1` holds latches clear (set attempts ignored) | zxnext.vhd:2102-2105 | hold config_mode; strobe producers; expect no latch set |
| GATE-07 | NR 0x81 bit 5 = 1 lets ExpBus fire without debounce | zxnext.vhd:1222, 2091 | set bit 5 = 1; pulse ExpBus pin; expect immediate assert |
| GATE-08 | NR 0x81 bit 5 = 0 requires debounced ExpBus (stubbed — see EXPBUS-03) | zxnext.vhd:1222, 2091 | set bit 5 = 0; pulse ExpBus pin; expect delayed assert |

### Group FSM — FSM transitions (Wave A/B) (6 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| FSM-01 | IDLE → FETCH on `nmi_activated` rising | zxnext.vhd:2126-2134 | strobe any producer; FSM advances to FETCH |
| FSM-02 | FETCH → HOLD on M1 fetch at 0x0066 | zxnext.vhd:2135-2138 | strobe producer; feed `observe_pc(0x0066)`; FSM advances to HOLD |
| FSM-03 | HOLD → END on `nmi_hold = 0` | zxnext.vhd:2139-2148 | drive to HOLD; clear consumer hold; advances to END |
| FSM-04 | END → IDLE on `cpu_wr_n` rising edge | zxnext.vhd:2149-2162 | drive to END; pulse cpu_wr_n; returns to IDLE |
| FSM-05 | END clears all three request latches | zxnext.vhd:2102-2105, 2149-2162 | drive through END; all three latches = 0 |
| FSM-06 | `nr_03_config_mode = 1` force-clears FSM to IDLE from any state | zxnext.vhd:2102-2105 | drive FSM to FETCH / HOLD / END; raise config_mode; expect IDLE |

### Group ARB — Priority arbitration (4 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| ARB-01 | Simultaneous MF + DivMMC assert → MF latches, DivMMC does not | zxnext.vhd:2097-2105 | strobe both; `nmi_mf` = 1, `nmi_divmmc` = 0 |
| ARB-02 | Simultaneous MF + ExpBus → MF latches, ExpBus does not | zxnext.vhd:2097-2105 | strobe both; `nmi_mf` = 1, `nmi_expbus` = 0 |
| ARB-03 | Simultaneous DivMMC + ExpBus (no MF) → DivMMC wins | zxnext.vhd:2097-2105 | strobe both; `nmi_divmmc` = 1, `nmi_expbus` = 0 |
| ARB-04 | `mf_is_active = true` (stub) blocks DivMMC latch even with DivMMC request | zxnext.vhd:2097-2098 | set stub; strobe DivMMC; `nmi_divmmc` = 0 |

### Group EXPBUS — ExpBus pin (Wave C — stubbed default inactive) (3 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| EXPBUS-01 | Default `expbus_nmi_n = 1` (inactive); no assert | zxnext.vhd:2091 | reset; expect `nmi_assert_expbus = 0` |
| EXPBUS-02 | `expbus_nmi_n = 0` with `expbus_nmi_debounce_disable = 1` → immediate assert | zxnext.vhd:2091, 1222 | pulse pin; NR 0x81 bit 5 = 1; expect latch set |
| EXPBUS-03 | `expbus_nmi_n = 0` without debounce-disable → delayed assert (debounce path stubbed) | zxnext.vhd:2091 | pulse pin; NR 0x81 bit 5 = 0; assert after debounce window |

### Group DMA — NMI-activated → im2_dma_delay (Wave E) (3 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| DMA-01 | `is_activated()` = true while any latch set | zxnext.vhd:2007 | strobe producer; FSM in FETCH; expect `is_activated() = 1` |
| DMA-02 | `im2_dma_delay` latches on `is_activated() AND nr_cc_dma_int_en_0_7` | zxnext.vhd:2007 | NR 0xCC bit 7 = 1; strobe NMI; expect `dma_delay` asserts |
| DMA-03 | NR 0xCC bit 7 = 0 blocks the NMI-activated contribution | zxnext.vhd:2007 | NR 0xCC bit 7 = 0; strobe NMI; expect `dma_delay` unchanged by NMI |

### Group Z80 — Z80 drive + integration (Wave A/B) (3 rows)

| ID | Description | VHDL cite | Stimulus summary |
|---|---|---|---|
| Z80-01 | FSM producing `/NMI` edge calls `Z80Cpu::request_nmi()` | zxnext.vhd:1841, 2164-2170 | strobe producer; observe Z80 request_nmi counter flips |
| Z80-02 | Z80 accepts NMI, PC vectors to 0x0066 | zxnext.vhd:2135-2138, Z80 standard behaviour | strobe producer; step Z80; expect PC = 0x0066 on fetch |
| Z80-03 | Reset clears both NmiSource state and Z80 NMI line | zxnext.vhd:2120, 2149 | drive FSM mid-handler; reset; expect IDLE + Z80 NMI line released |

### Row count summary

| Group | Rows |
|---|---:|
| RST | 3 |
| NR02 | 6 |
| HK | 5 |
| DIS | 4 |
| CLR | 4 |
| GATE | 8 |
| FSM | 6 |
| ARB | 4 |
| EXPBUS | 3 |
| DMA | 3 |
| Z80 | 3 |
| **Total** | **49** |

## Re-homed rows

(none)

This plan un-skips the NMI-blocked rows in their owning suites
(`copper_test` ARB-06, `divmmc_test` NM-01..08, `ctc_test` DMA-04)
rather than re-homing them. The NMI arbiter is its own subsystem with
its own suite; the owning suites retain the rows that exercise the
subsystem's contract with the arbiter (e.g. `DivMmc::button_nmi`
visible side-effects).

## Open questions

The full list lives in
[`TASK-NMI-SOURCE-PIPELINE-PLAN.md`](../design/TASK-NMI-SOURCE-PIPELINE-PLAN.md)
§Risks and open questions. Below is the plan-time status of each,
copied for convenience:

- **Q1 — Stackless NMI (Wave D): RESOLVED — CUT.** Implementing
  NR 0xC0 bit 3 would require patching the FUSE Z80 core (no pre-push
  NMI hook, no RETN interception hook) and risks the 1356-row FUSE
  regression. Only one test row benefits (CTC `NR-C0-02`). Wave D is
  removed from Phase 2; NR-C0-02's skip reason is refreshed in Phase 0
  to cite the FUSE-core-patch dependency. Revisit if a second driver row
  or reproducible user-visible bug appears.
- **Q2 — Task 8 Multiface plan overlap: RESOLVED — amend now.**
  `doc/design/TASK-8-MULTIFACE-PLAN.md` §5 is edited in Phase 0 so
  Branches A (NMI FSM) and D (NR 0x02 programmatic NMI) are marked
  *superseded by this plan*, and Branch C (NMI-button source) is scoped
  down to MF-button wiring only (DivMMC button wiring lands in this
  plan's Wave B).
- **Q3 — Test-suite layout: RESOLVED — new `test/nmi/nmi_test.cpp`
  suite** (as authored). Directory `test/nmi/` is new. Rejected
  alternative: putting the rows into `divmmc_test.cpp` — the arbiter is
  its own subsystem and deserves its own suite for future MF / ExpBus
  growth.

## File Layout

```
test/
  nmi/
    nmi_test.cpp           # NEW — created in Phase 1 with 1 row scaffold
  CMakeLists.txt           # Updated in Phase 1: new test executable + CTest
doc/testing/
  NMI-PIPELINE-TEST-PLAN-DESIGN.md   # This document
doc/design/
  TASK-NMI-SOURCE-PIPELINE-PLAN.md   # Skip-reduction plan (phases + waves)
```

## How to Run

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
./build/test/nmi_test
bash test/regression.sh
```

## Summary

| Group | Tests | Coverage |
|-------|------:|----------|
| RST — Reset defaults | 3 | FSM + latch + gate power-on state |
| NR02 — NR 0x02 software NMI | 6 | Wave A routing, readback, auto-clear |
| HK — Hotkey producers | 5 | Wave B edge capture + NR 0x06 gating |
| DIS — DivMMC consumer | 4 | Wave B set_button_nmi + is_nmi_hold |
| CLR — DivMMC clear paths | 4 | Reset / automap_reset / RETN / automap_held |
| GATE — Gate registers | 8 | NR 0x06, NR 0x81, port 0xE3, config_mode |
| FSM — State transitions | 6 | IDLE / FETCH / HOLD / END transitions |
| ARB — Priority arbitration | 4 | MF > DivMMC > ExpBus + mf_is_active |
| EXPBUS — ExpBus pin | 3 | Stubbed default inactive + debounce-disable |
| DMA — NMI-activated → im2_dma_delay | 3 | Wave E wiring + NR 0xCC bit 7 gate |
| Z80 — Z80 drive + integration | 3 | request_nmi, PC=0x0066, reset-clear |
| **Total** | **49** | |
