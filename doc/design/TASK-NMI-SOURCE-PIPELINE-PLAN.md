# Task 3 — NMI Source Pipeline Plan

Plan authored 2026-04-24. Follows the Task 3 staged plan template established
by `TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md`, `TASK3-AUDIO-SKIP-REDUCTION-PLAN.md`,
`TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`, `TASK3-INPUT-SKIP-REDUCTION-PLAN.md`,
and `TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md` (0→4 phases, parallel agent waves,
independent critic review per agent, VHDL-as-oracle per
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`).

This plan is called out in the current prompt file's
"Post-Input plan follow-ups" backlog as the required predecessor for un-skipping
Copper `ARB-06`, the eight DivMMC `NM-01..08` rows, and the two CTC NMI-adjacent
rows. It is also the logical next item in the Primary Task 3 ordering after the
Audio plan closed (2026-04-24), because the single remaining Copper skip
(`ARB-06`) is blocked on NMI infrastructure.

## Plan decisions (2026-04-24, pre-Phase-0)

User-confirmed resolutions to the open questions at §Risks:

- **Q1 — Stackless NMI (Wave D): CUT.** Implementing NR 0xC0 bit 3 would
  require patching the FUSE Z80 core (no pre-push NMI hook, no RETN
  interception hook) and risks the 1356-row FUSE regression. Only one
  test row benefits (CTC `NR-C0-02`). Wave D is removed from Phase 2;
  NR-C0-02's skip reason is refreshed in Phase 0 to cite
  "requires FUSE Z80 core patch for stackless NMI — deferred until a
  second driver row or user-visible bug appears".
- **Q2 — Task 8 Multiface plan overlap: amend now (minimal).**
  `doc/design/TASK-8-MULTIFACE-PLAN.md` §5 gets edited in Phase 0 so
  Branches A (NMI FSM) and D (NR 0x02 programmatic NMI) are marked
  *superseded by this plan*, and Branch C (NMI-button source) is scoped
  down to MF-button wiring only (DivMMC button wiring lands in this
  plan's Wave B).
- **Q3 — Test-suite layout: new `test/nmi/nmi_test.cpp` suite** (as
  authored).

Row-count targets are therefore **10 must-do un-skips** (not 11):
ARB-06, NM-01..08, DMA-04.

### Copper NR-write path — audit complete (pre-Phase-0)

`src/peripheral/copper.cpp:148` calls `nextreg.write(reg, val)` on the
shared `NextReg` object. `src/port/nextreg.cpp:112-118` dispatches to
the per-register write handler set by `Emulator::reset()`. Therefore
Copper's MOVE-to-NR-0x02 will fire the NR 0x02 write handler that
Wave A installs — **no extra Copper hook is required**. Risk 3
(§Risks) is downgraded to "no action; audit confirmed direct dispatch".

## Summary

Stand up a **central NMI source / arbiter subsystem** that owns the
four-state FSM (`S_NMI_IDLE → S_NMI_FETCH → S_NMI_HOLD → S_NMI_END`) from
`zxnext.vhd:2089-2170`, with the VHDL-faithful priority chain
(MF > DivMMC > ExpBus), configurable gates (NR 0x06 bits 3/4, NR 0x81 bit 5,
port 0xE3 bit 7, `nr_03_config_mode`), and three producer classes (hotkey
buttons, NR 0x02 software NMI, IO-trap stub). Route the resulting
`nmi_divmmc_button` strobe into the existing `DivMmc::set_button_nmi()`
consumer (dormant since commit `b57bb85`), add the four VHDL-specified
`button_nmi` clear paths (reset / automap_reset / RETN / automap_held),
and drive `Z80Cpu::request_nmi()` from the FSM output.

Expected outcome — of the 11 NMI-blocking skips enumerated in the Phase-0
audit, **10 flip to pass** and **1 carries forward as an honest skip** with
a refreshed reason (see §Plan decisions, Q1):

| Suite | Rows | Current | Post-plan |
|---|---|---:|---|
| `copper_test` ARB-06 | 1 | skip | **pass** (Wave A) |
| `divmmc_test` NM-01..08 | 8 | skip | **pass** (Wave B + DivMMC clear-path PR) |
| `ctc_test` DMA-04 | 1 | skip | **pass** (Wave E) |
| `ctc_test` NR-C0-02 | 1 | skip | skip, reason refreshed (Wave D CUT) |

Plus a **new `test/nmi/nmi_test.cpp` suite** (≈45-55 rows) covering the FSM
transitions, arbiter priority, all three producer paths, all gate registers,
and the consumer-feedback hold signals.

Expected aggregate delta: `3281 / 3144 / 0 / 137 → ≈3330 / 3199 / 0 / 131`
across **30 suites** (new `nmi_test` suite; 11 rows flipped to pass; no new
deferrals on the must-do rows).

## Starting state

- `copper_test`: 76 / 75 / 0 / 1 (ARB-06 skip — NMI-blocked).
- `divmmc_test`: 100 / 92 / 0 / 8 (NM-01..08 skip — all NMI-blocked).
- `ctc_test`: 133 / 128 / 0 / 5 (2 NMI-blocked: NR-C0-02, DMA-04).
- Aggregate unit (post-Audio + post-Input-re-audit close, 2026-04-24c):
  29 suites, `3281 / 3144 / 0 / 137`.
- Regression: 34 / 0 / 0. FUSE Z80: 1356 / 1356.
- Branch: `main`, working tree clean.

### NMI infrastructure currently absent

Grep confirms **zero callers** of `Z80Cpu::request_nmi()` (API exists at
`src/cpu/z80_cpu.h:58`, implementation at `src/cpu/z80_cpu.cpp:380-382`) and
**zero callers** of `DivMmc::set_button_nmi()` (setter exists at
`src/peripheral/divmmc.h:143`). There is no `src/peripheral/nmi*`, no NR 0x02
write handler with NMI semantics (`src/core/emulator.cpp:643-652` handles
reset bits only), no NR 0x06 decoding of the two NMI-enable bits
(`emulator.cpp:1408-1432` decodes PSG/audio/speaker only), and no NR 0x81
handler at all. Port 0xE3 bit 7 (CONMEM) is decoded by DivMMC but not exposed
to the absent NMI arbiter.

Stackless NMI storage exists (`Im2Controller::stackless_nmi_` field parsed at
`emulator.cpp:862`) but is never consumed by the FUSE Z80 core.

### Pre-existing DivMMC clear-path gap

`DivMmc::button_nmi_` has no automatic clear logic in the current emulator.
Per `divmmc.vhd:105-116`, the latch must clear on any of:

1. `i_reset = 1` (hard reset).
2. `i_automap_reset = 1` (DivMMC automap reset signal).
3. `i_retn_seen = 1` (Z80 RETN instruction completion).
4. `automap_held = 1` (one-shot; DivMMC consumed the NMI).

Today `button_nmi_` only clears if someone explicitly calls
`set_button_nmi(false)` (no one does). This is the implementation gap behind
DivMMC rows NM-04..07 and is folded into Wave B of this plan rather than a
separate DivMMC skip-reduction plan.

### Triage of the 11 NMI-blocking skips (categorisation)

Cross-referenced against VHDL per `feedback_unobservable_audit_rule.md`:

| Cluster | Rows | Count | Cat | Phase-2 wave |
|---|---|---:|---|---|
| 1 — NR 0x02 software NMI | copper ARB-06 | **1** | F | Wave A (NR 0x02 bits 2/3 routing) |
| 2 — DivMMC button latch produce | divmmc NM-01 | **1** | F | Wave B (FSM drives set_button_nmi) |
| 3 — DivMMC button latch consume | divmmc NM-02, NM-03 | **2** | F | Wave B (existing gate + test stimulus) |
| 4 — DivMMC button latch clear | divmmc NM-04, NM-05, NM-06, NM-07 | **4** | F | Wave B (four clear paths) |
| 5 — DivMMC disable-nmi output | divmmc NM-08 | **1** | F | Wave B (`o_disable_nmi` consumer) |
| 6 — NMI-driven DMA delay | ctc DMA-04 | **1** | F | Wave E (`nmi_activated AND nr_cc_dma_int_en_0_7`) |
| 7 — Stackless NMI | ctc NR-C0-02 | **1** | F → **CUT** | Wave D removed per Q1; skip-reason refresh only |

Total accounting: 1 + 1 + 2 + 4 + 1 + 1 + 1 = **11** (**10 flip, 1 carry**). ✓

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
| `cores/zxnext/src/zxnext.vhd` | 2052-2067 | Stackless NMI (`z80_stackless_nmi`, NMIACK return-address capture into NR 0xC2/0xC3) |
| `cores/zxnext/src/zxnext.vhd` | 2007 | `im2_dma_delay <= im2_dma_int OR (nmi_activated AND nr_cc_dma_int_en_0_7) OR (…)` |
| `cores/zxnext/src/zxnext.vhd` | 3830-3872 | NR 0x02 bit 2/3 CPU + Copper write → `nmi_cpu_02_we / nmi_cu_02_we / nmi_gen_nr_mf / nmi_gen_nr_divmmc / nmi_sw_gen_mf / nmi_sw_gen_divmmc` |
| `cores/zxnext/src/zxnext.vhd` | 1109-1110 | NR 0x06 bit 3 (`button_m1_nmi_en`) / bit 4 (`button_drive_nmi_en`) |
| `cores/zxnext/src/zxnext.vhd` | 1222 | NR 0x81 bit 5 (`expbus_nmi_debounce_disable`) |
| `cores/zxnext/src/zxnext.vhd` | 5891 | NR 0x02 readback (bits 3/2 auto-cleared by FSM) |
| `cores/zxnext/src/zxnext.vhd` | 6230 | NR 0xC0 readback (bit 3 `stackless_nmi`) |
| `cores/zxnext/src/zxnext.vhd` | 6331-6349 | Hotkey F9 (`hotkey_m1`, MF) / F10 (`hotkey_drive`, DivMMC) edge detection |
| `cores/zxnext/src/device/divmmc.vhd` | 103-150 | `button_nmi` latch set / clear / `o_disable_nmi` output |
| `cores/zxnext/src/device/multiface.vhd` | 134-148, 167-191 | MF NMI_ACTIVE + MF_ENABLE state machines (stub-only in this plan) |
| `cores/zxnext/src/zxnext_top_issue4.vhd` | 1773-1774, 1951-1955 | Bus-NMI pin + PS/2 F9/F10 → keyboard hotkey latch |

## Scope

### In scope (MUST)

1. **`NmiSource` / NMI-arbiter subsystem** — new class
   `src/peripheral/nmi_source.{h,cpp}` (single-instance via `Emulator`).
   Owns the four-state FSM, the `nmi_assert_*` combinatorial producers,
   the priority latch (MF > DivMMC > ExpBus), and the `nmi_generate_n` output
   that drives `Z80Cpu::request_nmi()`.
2. **Three producer input APIs** on `NmiSource`:
   - `set_mf_button(bool)` / `strobe_mf_button()` — hotkey F9 or future MF
     peripheral.
   - `set_divmmc_button(bool)` / `strobe_divmmc_button()` — hotkey F10 or
     button press.
   - `nr_02_write(uint8_t v)` — strobes `nmi_sw_gen_mf` (bit 3) and
     `nmi_sw_gen_divmmc` (bit 2) per VHDL:3830-3872 bit decode.
   - `set_expbus_nmi_n(bool)` — ExpBus pin (stubbed `true` / inactive by
     default; wire-only for VHDL-faithfulness).
3. **Gate-register wiring**:
   - NR 0x06 bit 3 (`button_m1_nmi_en`) and bit 4 (`button_drive_nmi_en`)
     decoded in `Emulator` → forwarded via `NmiSource::set_mf_enable(bool)` /
     `set_divmmc_enable(bool)`.
   - NR 0x81 bit 5 (`expbus_nmi_debounce_disable`) → `set_expbus_debounce_disable(bool)`.
   - Port 0xE3 bit 7 (CONMEM) — accessor on DivMmc, read by NmiSource on
     each arbitration step.
   - `nr_03_config_mode` — `Emulator` already has this state; forward via
     `set_config_mode(bool)` to force-clear latches (VHDL:2102-2105).
4. **Consumer-feedback hooks** (stubs for MF, real for DivMMC):
   - `set_divmmc_nmi_hold(bool)` ← `DivMmc::is_nmi_hold()` (new accessor
     derived from `automap_held_ OR button_nmi_` per VHDL `o_disable_nmi`).
   - `set_mf_nmi_hold(bool)` — stubbed `false` in this plan (real value
     arrives with Task 8 Multiface).
   - `set_mf_is_active(bool)` — stubbed `false`; same deferral.
5. **Z80 drive path**: `NmiSource::tick()` computes `nmi_generate_n` per
   VHDL:2164-2170; on falling edge (assertion) calls
   `Z80Cpu::request_nmi()`. FSM advances to `S_NMI_FETCH` when the Z80
   accepts the NMI (detectable via the existing `fuse_z80_nmi()` return);
   advances to `S_NMI_HOLD` / `S_NMI_END` based on `mf_a_0066` detection
   (existing DivMMC PC=0x0066 watcher, refactored to also signal NmiSource)
   and `cpu_wr_n` progression.
6. **DivMMC `button_nmi_` clear paths** — wire the four VHDL-specified clear
   sources currently absent:
   - Reset (exists — `DivMmc::reset()` already clears the latch) → re-verify.
   - `i_automap_reset` — DivMmc internal state; clear `button_nmi_` when the
     automap FSM re-enters the reset state.
   - `i_retn_seen` — new hook `DivMmc::on_retn_seen()` called from the Z80
     RETN instruction path (or from `NmiSource` which already observes
     NMIACK/RETN progression for the FSM).
   - `automap_held = 1` — clear on the rising edge of `automap_held_`.
7. **NR 0x02 write handler + readback** (`emulator.cpp`):
   - Write forwards `v` to `NmiSource::nr_02_write(v)` which handles bits
     2/3 strobes and stores the latched bits for readback.
   - Read returns: `bus_reset << 7 | iotrap << 4 | mf_nmi_pending << 3 |
     divmmc_nmi_pending << 2 | reset_type` per VHDL:5891, where the
     pending bits auto-clear on FSM transition through S_NMI_END.
8. **NMI-activated → DMA-delay wiring** (Wave E, small):
   - `Im2Controller::update_im2_dma_delay()` (landed inert in DMA Feature D
     per `project_feature_d_staged_wiring.md`) gets its `nmi_activated`
     input wired from `NmiSource::is_activated()`. NR 0xCC bit 7
     (`nr_cc_dma_int_en_0_7`) is already decoded.
9. **New `test/nmi/nmi_test.cpp` suite** (≈45-55 rows):
   - Reset defaults (FSM in IDLE, all latches clear).
   - Each producer → each consumer path, with and without its enable gate.
   - Priority arbitration: simultaneous MF + DivMMC → MF wins.
   - FSM state transitions (IDLE→FETCH→HOLD→END→IDLE).
   - `port_e3_reg(7) = 1` blocks MF assertion.
   - `mf_is_active` stub blocks DivMMC assertion.
   - `nr_03_config_mode = 1` force-clears latches.
   - NR 0x02 readback: bit auto-clear after FSM transition.
   - NR 0x06 / NR 0x81 gates observed at the producer layer.
   - ExpBus pin (stubbed) — input-only path verified inert in defaults.
10. **Un-skip the 10 non-stackless rows** (ARB-06 + NM-01..08 + DMA-04).
    Wave D (stackless, NR-C0-02) is a stretch — see §8.

### Out of scope (explicit)

- **Multiface peripheral (Task 8).** MF-side hold / active hooks remain
  stubs in this plan. Task 8 replaces the stubs with a real `Multiface`
  class. The NMI arbiter is designed so Task 8 is a drop-in consumer
  substitution, not a plumbing rewrite.
- **Full PS/2 keyboard hotkey edge detection** (VHDL zxnext.vhd:6331-6349
  shift-chain debounce). We expose `set_mf_button` / `set_divmmc_button`
  bool setters and trust the caller to produce a clean pulse. A later
  GUI / Input-plan follow-up can add real debounce if it ever matters.
- **GUI keybinding for F9 / F10 → NMI button.** The Joystick-GUI backlog
  item will also own per-instance keymapping for MF + DivMMC buttons.
  Headless tests use the bool setters directly.
- **ExpBus NMI pin wire-through.** No physical expansion bus in jnext;
  `set_expbus_nmi_n()` always defaults to inactive. NR 0x81 bit 5 handler
  is wired end-to-end even though there is no producer today, for
  VHDL-faithfulness when expansion bus emulation is ever added.
- **IO-trap (`nmi_gen_iotrap`) — NR 0xD8 FDC-trap path.** Stubbed in this
  plan; producer path is a single stub method `strobe_iotrap()` but there
  is no test row covering it.
- **Real Z80 stackless-NMI execution semantics.** Storage + NR 0xC0
  readback already exist. Wave D only adds the NR 0xC0 readback row and
  the NMIACK-observed return-address capture IF it fits the session
  budget; full Z80N NMIACK wiring may require FUSE-core patch territory.
  CTC `NR-C0-02` is the only row this blocks; it can remain skip with a
  refreshed reason if Wave D is cut.
- **NextZXOS boot impact.** This plan is not aimed at boot. If Task 12
  incidentally benefits because NR 0x02 / NR 0x06 / NR 0x81 now decode
  correctly, that's bonus.

### Explicitly deferred to Task 8

- MF-side `nmi_assert_mf` gating via real `mf_is_active` (MF ROM mapped).
- MF button-hold semantics (MF's own nmi_active FSM).
- MF port decode (I/O ports `port_mf_enable` / `port_mf_disable` per
  model).
- `enNextMf.rom` loading.

## Dependencies

- **Task 7 (DivMMC automap pipeline)** — already landed. This plan relies
  on `DivMmc::automap_held_` and automap-reset hooks already being clocked
  correctly. Any remaining Task-7 cycle-accuracy gaps inherited by this
  plan must be called out in Phase 0 rather than papered over.
- **CTC+Interrupts plan** — already landed (`Im2Controller::update_im2_dma_delay()`
  exists but inert; this plan wires it live in Wave E).
- **UART+I2C + Audio + Input plans** — landed; no hard dependency but
  shared aggregate dashboard must update cleanly.

## Phase 0 — triage, re-home, plan-doc refresh (single agent + critic)

**Actions, all test-code / plan-doc only:**

1. Refresh skip-reason strings on ARB-06, NM-01..08, DMA-04, NR-C0-02 to
   point at the matching Wave of this plan (e.g.
   `"un-skip via task-nmi-wave-b"`). Keep cite of the VHDL source.
2. Re-verify each skip's stimulus against the current source tree:
   - ARB-06 (copper): confirm the Copper MOVE-to-NR-0x02 path reaches
     `Emulator::write_nextreg(0x02, v)`. If Copper's NR-write pipe
     bypasses Emulator (writes NextReg directly), Wave A must also teach
     the Copper its NR 0x02 hand-off.
   - NM-01..08 (divmmc): re-read the current `divmmc_test.cpp` skip
     messages; refresh any that cite "Task 8" to cite "NMI pipeline plan
     Wave B" instead. Task-8 annotation remains for rows that truly
     require the Multiface.
   - DMA-04 (ctc): confirm the test's expected model matches the
     VHDL:2007 expression and that NR 0xCC bit 7 is decoded today
     (audit says yes via Im2Controller).
   - NR-C0-02 (ctc): refresh reason to cite "Wave D stretch — un-skip
     if Wave D lands; otherwise carry with updated reason".
3. **Enumerate new test rows** that will populate `nmi_test.cpp` (≈45-55
   rows). Produce the row table as an appendix in the Phase-0 commit so
   Waves A-E have a concrete target. Each row carries a short VHDL cite.
4. Append NMI sections to the two existing test-plan design docs:
   - `doc/testing/COPPER-TEST-PLAN-DESIGN.md` — note ARB-06 un-skip path.
   - `doc/testing/DIVMMC-SPI-TEST-PLAN-DESIGN.md` — reword NM-01..08
     sections to reference this plan as the un-skip vehicle.
   - Create `doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md` — full row
     table for the new suite, VHDL-cited, matching the style of the
     UART-I2C / AUDIO plan docs.

**Expected Phase 0 delta**: no skip-count change (reason-refreshes and
doc work only). Independent critic (1 agent) reviews the reason-string
refreshes and the new design doc against VHDL.

**Files touched in Phase 0 only**:
- `test/copper/copper_test.cpp` (ARB-06 reason refresh).
- `test/divmmc/divmmc_test.cpp` (NM-01..08 reason refresh).
- `test/ctc/ctc_test.cpp` (DMA-04, NR-C0-02 reason refresh).
- `doc/testing/COPPER-TEST-PLAN-DESIGN.md`.
- `doc/testing/DIVMMC-SPI-TEST-PLAN-DESIGN.md`.
- `doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md` (NEW).

## Phase 1 — NmiSource scaffold + Z80 + Emulator wiring (single agent + critic)

**Single agent, single branch `task-nmi-phase-1-scaffold`. No test rows
flipped yet; this is the implementation seam.**

1. New class `src/peripheral/nmi_source.{h,cpp}` (~250-350 lines):
   - `enum class NmiSrc { None, Mf, DivMmc, ExpBus };`
   - `enum class NmiState { Idle, Fetch, Hold, End };`
   - Members for every VHDL signal in the 2089-2170 block:
     `nmi_mf_`, `nmi_divmmc_`, `nmi_expbus_`, `nmi_state_`,
     `mf_button_`, `divmmc_button_`, `expbus_nmi_n_` (default `true`),
     all gate flags, consumer-feedback flags.
   - API per §Scope item 2-5.
   - `tick(uint32_t master_cycles)` — advances FSM per CPU-clock edge
     (28 MHz internal, Z80 edge synchronous per VHDL:2151-2162).
   - `save_state()` / `load_state()` via project's `StateWriter` /
     `StateReader` — all signals above persisted.
2. `Emulator::nmi_source_` member (single-instance). Construction in
   `Emulator` ctor; `reset()` call forwarded from `Emulator::reset()`.
   `tick()` called from the per-instruction cluster alongside CTC / UART /
   Md6 (mirror `emulator.cpp:2287` pattern).
3. Z80 hook:
   - On the trailing edge of `nmi_generate_n` (i.e. when NmiSource wants
     the Z80 to take an NMI), call `z80_cpu_.request_nmi()`.
   - FUSE Z80 already acks NMI on next instruction boundary and emits
     vector to 0x0066. NmiSource observes the M1 fetch at 0x0066 via the
     existing `Mmu` read-watcher or a new per-step `observe_pc(pc)` hook
     to advance the FSM.
4. DivMmc: add `is_nmi_hold() const` accessor returning
   `automap_held_ OR button_nmi_` per VHDL `o_disable_nmi`. Do NOT wire
   the consumer-feedback yet (landing in Phase 2 Wave B to keep the seam
   minimal in Phase 1).
5. Unit test scaffold: `test/nmi/nmi_test.cpp` with a single `check()`
   row (reset defaults: FSM=IDLE, all latches clear). Add CMake + Makefile
   wiring. Suite reads **1 / 1 / 0 / 0**.

**Critic review**: one independent agent reviews the scaffold against
VHDL:2089-2170 for structural fidelity (signal-for-signal naming,
priority ordering, clear conditions). Not a skip-flip, so lighter-weight
than Phase 2/3 reviews.

**Phase 1 skip-count delta**: 0. Aggregate: +1 suite, +1 pass.

## Phase 2 — parallel waves

**Up to 5 simultaneous agents per `feedback_parallel_agent_budget_20260421.md`.
Each wave in its own branch, independent critic per branch. Wave E can
run in parallel with any other wave.**

### Wave A — NR 0x02 software NMI (1 agent)

- Branch: `task-nmi-wave-a-nr02-sw`.
- Scope: NR 0x02 write handler routes to `NmiSource::nr_02_write(v)`;
  NR 0x02 read returns the VHDL:5891 bit layout with auto-clear.
- Copper integration: audit whether Copper's MOVE-to-NR writes go
  through `Emulator::write_nextreg(…)` or bypass it. If bypass, add the
  hook.
- Row unlocks: **copper ARB-06** (flip from skip to check).
- `nmi_test.cpp` rows added: NR02-* group (≈6 rows).

### Wave B — Hotkey NMI + DivMMC consumer (1 agent)

- Branch: `task-nmi-wave-b-hotkey-divmmc`.
- Scope:
  - FSM produces `nmi_divmmc_button` strobe → route to
    `DivMmc::set_button_nmi(true)`.
  - DivMmc gains the four `button_nmi_` clear paths per VHDL:105-116
    (reset already works; add automap_reset, retn, automap_held).
  - `DivMmc::on_retn_seen()` hook (called from Z80 RETN execution path
    — existing FUSE callback or new jnext-side Z80 watcher).
  - NmiSource consumes `DivMmc::is_nmi_hold()` for the `divmmc_nmi_hold`
    signal in VHDL:2107 / 2118.
  - `NmiSource::set_divmmc_button(true)` producer API.
- Row unlocks: **divmmc NM-01..08** (all 8 flip to check).
- `nmi_test.cpp` rows added: HK-*, DIS-* groups (≈12 rows).

### Wave C — Gate registers (NR 0x06, NR 0x81, port 0xE3) (1 agent)

- Branch: `task-nmi-wave-c-gates`.
- Scope:
  - NR 0x06 handler at `emulator.cpp:1408-1432` extended with bit 3
    (`button_m1_nmi_en`) and bit 4 (`button_drive_nmi_en`) decode →
    forward to `NmiSource::set_mf_enable(bool)` / `set_divmmc_enable(bool)`.
  - NR 0x81 handler (NEW, not yet present per audit) with bit 5
    (`expbus_nmi_debounce_disable`) decode → forward to
    `NmiSource::set_expbus_debounce_disable(bool)`. All other NR 0x81
    bits also decoded for VHDL-faithfulness (stored but inert for bits
    that need Task-8 / expbus work).
  - DivMmc gains `is_conmem() const` accessor (port 0xE3 bit 7).
  - NmiSource reads `divmmc.is_conmem()` per tick for the
    `port_e3_reg(7) = 0` gate at VHDL:2107.
- Row unlocks: **none** (gate registers have their behaviour covered in
  Waves A/B rows). This wave adds the plumbing.
- `nmi_test.cpp` rows added: GATE-* group (≈8 rows — each bit's gate
  observed at the producer).

### Wave D — Stackless NMI — **CUT** (per Q1 decision, pre-Phase-0)

Resolved at plan-decisions block above. Rationale summary: FUSE Z80 core
has no pre-push NMI hook and no RETN interception hook; implementing
stackless semantics requires patching the core, which risks the 1356-row
FUSE opcode regression. Only one test row (CTC `NR-C0-02`) benefits.
The NR 0xC0 bit 3 storage already exists in `Im2Controller` and will
remain dormant; readback is VHDL-faithful today. Phase 0 refreshes
NR-C0-02's skip reason to cite the FUSE-core-patch dependency.

When a second driver arrives (either another test row, or a reproducible
user-visible bug traced to stackless NMI), a dedicated single-wave plan
can pick this up in isolation. Until then: do not include.

### Wave E — NMI-driven DMA-delay wiring (1 agent)

- Branch: `task-nmi-wave-e-dma-delay`.
- Scope:
  - `Im2Controller::update_im2_dma_delay()` (landed inert in DMA Feature
    D) gets its `nmi_activated` input wired from
    `NmiSource::is_activated()` per VHDL:2007.
  - NR 0xCC bit 7 (`nr_cc_dma_int_en_0_7`) already decoded.
  - The delay latch is now live when NMI fires during a DMA
    operation + that bit is set.
- Row unlocks: **ctc DMA-04** (flip to check).
- `nmi_test.cpp` rows added: DMA-* group (≈3 rows — the full VHDL:2007
  OR chain observed).

**Wave merge order**:
1. Phase 1 merges first.
2. Waves A, B, C, E can merge in any order (no code conflicts expected —
   A touches NR 0x02 path, B touches DivMmc + FSM consumer feedback,
   C touches NR 0x06 / 0x81 / port 0xE3, E touches Im2Controller).
3. Wave D merges last (depends on Z80 core hook landed in Phase 1 /
   Wave A).

## Phase 3 — `nmi_test.cpp` row flips + cross-suite un-skips + integration proofs

**Single agent + critic. Merges after all Phase-2 waves.**

1. Flip the ≈10 new `nmi_test.cpp` rows per Phase-0 appendix to
   live `check()` (reset-defaults row from Phase 1 stays, plus
   NR02-/HK-/DIS-/GATE-/DMA- groups from Waves A/B/C/E, plus Wave D's
   STK- group if Wave D landed).
2. Flip cross-suite rows from skip to check:
   - `copper_test` ARB-06 → check.
   - `divmmc_test` NM-01..08 → check.
   - `ctc_test` DMA-04 → check. NR-C0-02 → check iff Wave D landed,
     otherwise skip-reason refreshed only.
3. Integration proof in `divmmc_integration_test.cpp` (or new
   `nmi_integration_test.cpp` if cleaner): press DivMMC button →
   verify Z80 takes NMI → verify PC=0x0066 fetch → verify automap
   fires → verify RETN clears automap + button_nmi_.
4. Update dashboards:
   - `test/SUBSYSTEM-TESTS-STATUS.md` — nmi_test + new live counts.
   - `doc/testing/TRACEABILITY-MATRIX.md` — 11 (or 10) rows flipped.
5. Update `FEATURES.md` / `TODO.md` if significant (user decides).

**Expected Phase 3 delta**:
- `nmi_test`: 1 / 1 / 0 / 0 → ≈ 45-55 / 45-55 / 0 / 0.
- `copper_test`: 76 / 75 / 0 / 1 → 76 / 76 / 0 / 0.
- `divmmc_test`: 100 / 92 / 0 / 8 → 100 / 100 / 0 / 0.
- `ctc_test`: 133 / 128 / 0 / 5 → 133 / 129 / 0 / 4 (Wave D cut) or
  133 / 130 / 0 / 3 (Wave D landed).
- Aggregate: ≈3281 / 3144 / 0 / 137 → ≈3330-3336 / 3199-3210 / 0 /
  127-131, across **30 suites**.
- Regression: 34 / 0 / 0 (no screenshot changes expected).

## Phase 4 — dashboards + independent audit (1 agent)

1. Regenerate dashboards (Makefile target already emits them).
2. Independent Phase-4 audit agent writes
   `doc/testing/audits/task-nmi-phase4.md` with:
   - Plan-drift catalogue.
   - Per-wave critic verdicts.
   - Backlog items surfaced mid-plan.
   - Final aggregate delta.
3. Update `.prompts/<today>.md` Task Completion Status line.

## Acceptance criteria

Plan is DONE when ALL of the following hold:

- [ ] Phase 0 → 4 all merged via author + critic cycles, no self-review.
- [ ] `nmi_test.cpp` suite exists with ≈45-55 live rows (all check, no
      skip).
- [ ] All 10 must-do rows (ARB-06, NM-01..08, DMA-04) flipped to check.
- [ ] NR-C0-02 either flipped (Wave D landed) or skip-reason refreshed
      to cite the correct blocker.
- [ ] Every test row tagged "NMI" in the traceability matrix is either
      un-skipped as PASS or has a written VHDL-cited justification for
      remaining SKIP (category D/E/F/G per the unobservable-audit rule).
- [ ] Regression 34 / 34 / 0. No FUSE Z80 or screenshot test regression.
- [ ] Unit aggregate: +N pass, −N skip (not −N pass).
- [ ] Phase-4 audit merged.

## Risks and open questions

### Risk 1 — FUSE Z80 NMI hook insufficient for stackless mode

FUSE's Z80 core handles NMI via its standard stack-push + vector-to-0x0066.
Stackless NMI (NR 0xC0 bit 3) requires capturing the return address
into NR 0xC2 / NR 0xC3 *instead* of pushing it to the stack. Unless
FUSE exposes a pre-push hook, Wave D cannot implement the full
stackless path without patching the core.
**Mitigation**: explicitly flag Wave D as a stretch. If the pre-push
hook is absent, cut Wave D to reason-refresh only and carry NR-C0-02
as a Task-8-era follow-up.

### Risk 2 — Z80 RETN detection for `button_nmi_` clear + FSM END

The NMI FSM transitions S_NMI_END → S_NMI_IDLE on the completion of
the CPU write that exits the NMI handler (VHDL:2149-2162
`cpu_wr_n=1` → IDLE). This is not quite "RETN seen" — the VHDL models
the Z80's post-handler bus cycle. The jnext-side hook should observe
either (a) the RETN opcode in Z80 instruction path, or (b) the equivalent
post-NMI bus transition.
**Mitigation**: Phase 1 decides between (a) and (b) based on what
FUSE exposes cleanly. Document the choice; Phase 4 audit re-checks
timing fidelity against any VHDL-faithful traces we have.

### Risk 3 — Copper NR-writes bypass Emulator

If the Copper's MOVE-to-NR path writes NextReg internal state directly
(bypassing `Emulator::write_nextreg`), NmiSource will not see Copper
NR 0x02 writes and ARB-06 will fail silently.
**Mitigation**: Phase 0 audits the Copper write path. Wave A adds a
hook if required.

### Risk 4 — DivMmc clear-path regressions

The DivMMC `button_nmi_` latch today is never set by anything (NmiSource
is new). Adding the four clear paths could expose latent assumptions
in the DivMMC automap FSM that only worked because the latch was
permanently clear. All 100 rows of `divmmc_test` must stay green after
Wave B.
**Mitigation**: Wave B critic specifically diffs against `divmmc_test`
output pre- / post- Wave B.

### Risk 5 — MF stubs lie about true MF behaviour

`set_mf_nmi_hold(false)` / `set_mf_is_active(false)` are stubs until
Task 8 lands. The arbiter then has no "MF active" state to arbitrate
against, so MF > DivMMC priority is un-exercised at integration level
(it IS exercised in `nmi_test` via test-only stimulus of the stubs).
**Mitigation**: document the stub clearly; Task 8 Branch B includes
replacing the stubs with real `Multiface::is_nmi_hold() / is_active()`
accessors.

### Risk 6 — Agent worktree stale-base bug

Recurs in every multi-wave session (`feedback_agent_worktree_stale_base.md`).
**Mitigation**: brief every agent at launch to
`git -C <worktree> fetch origin && git -C <worktree> rebase origin/main`
before starting work.

### Open question 1 — Should NmiSource own `nr_03_config_mode`?

`nr_03_config_mode` is an Emulator-level state today. NmiSource needs
it for latch clearing (VHDL:2102). Two options: (a) Emulator pushes the
value to NmiSource via `set_config_mode(bool)` on every NR 0x02 write,
or (b) NmiSource holds a pointer / ref to Emulator and pulls. (a) is
cleaner, matches the UART+I2C / Audio wiring style.

### Open question 2 — Test-suite naming

`test/nmi/nmi_test.cpp` mirrors `test/uart/uart_test.cpp`. Directory
`test/nmi/` is new. Confirm this is the preferred layout vs putting
the rows into `divmmc_test.cpp` (rejected — the arbiter is its own
subsystem and deserves its own suite for future MF / ExpBus growth).

### Open question 3 — Multiface test-plan doc co-existence

Task 8's own plan doc (`TASK-8-MULTIFACE-PLAN.md`) already mentions
Branch A "NMI state machine in Z80Cpu / NMI router". This plan
supersedes that branch; Task 8 Branch A should be rewritten as
"consume NmiSource already landed" before Task 8 is executed. Do we
amend `TASK-8-MULTIFACE-PLAN.md` now (mark Branch A as superseded
by this plan), or leave it for whoever picks up Task 8? Recommend:
add a one-paragraph note to Task 8 Branch A now, referencing this plan.

## Relation to Task 12 (NextZXOS boot)

NMI infrastructure is unlikely to unblock the current boot hang (user
sees "boots to 48K mode" after DivMMC + CTC + Audio + UART + Input
plans landed). The NextZXOS firmware does write NR 0x02 at various
points — if its writes currently trigger unintended paging / NMI (because
NR 0x02 was previously a no-op) we may see a shift in boot fingerprint
after Wave A. Document any new fingerprint in
`doc/issues/NEXTZXOS-BOOT-INVESTIGATION.md` as a post-plan amendment.
