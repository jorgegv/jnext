# Task 3 — Input Subsystem SKIP-Reduction Audit (2026-04-22)

Closure audit for the Task 3 Input subsystem skip-reduction plan executed
2026-04-21 and 2026-04-22. Mirrors the structure of
[task3-ctc-phase5.md](task3-ctc-phase5.md).

Plan doc: [doc/design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md](../../design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md).

## Headline numbers

|                       | Before plan (2026-04-21) | After plan (2026-04-22) | Δ          |
|-----------------------|-------------------------:|------------------------:|-----------:|
| `input_test` total    |                  149     |                139      |   −10      |
| `input_test` pass     |                   23     |                133      |  +110      |
| `input_test` fail     |                    0     |                  0      |    0       |
| `input_test` skip     |                  126     |                  6      |  −120      |
| `input_integration_test`      |                  N/A     |              7/5/0/2    |  +5 pass   |
| Aggregate unit suites |                   21     |                 22      |   +1       |
| Aggregate pass        |                 2876     |               3001      |  +125      |
| Aggregate skip        |                  336     |                218      |  −118      |
| Regression            |                34/0/0    |              34/0/0     | unchanged  |
| FUSE Z80              |             1356/1356    |          1356/1356      | unchanged  |

## Phase summary

| Phase | Action | Critic verdict | Commits |
|---|---|---|---|
| 0 | Triage + comment sweep (10 rows removed: 3 G + 7 re-home) | APPROVE | `c5c9d53` + merge `3de305d` |
| 1 | API scaffold (5 new classes; Emulator + Keyboard wiring) | APPROVE-WITH-FIXES (commit) | `62969ae`, `ca0fe4f`, merge `6ae7bbf`, polarity fix `d30a263` |
| 2 Wave 1 | Agents A/D/F/G/H (8+19+3+20+8 = 58 rows) | 5× APPROVE / APPROVE-WITH-FIXES | 5 author commits + 5 merges (`76b1ae6` → `eeda898`) |
| 2 Wave 2 | Agents B/C/E/I (24+17+4+7 = 52 rows) | 4× APPROVE / APPROVE-WITH-FIXES | 4 author commits + bundled merge |
| 3 | Un-skip flips (30 rows: KBDHYS+JMODE+MD6) + new integration suite | APPROVE | `05cf019` + merge |
| 4 | Dashboard + matrix + plan-doc + audit refresh (this commit) | (in-line) | this commit |

## Per-cluster outcome

| Cluster | Plan rows | Status | Phase-2 agent |
|---|---:|---|---|
| 1 — KBDHYS | 3 | un-skip | F |
| 2 — EXT (NR 0xB0/B1) | 20 | un-skip (G flipped early) | G |
| 3 — JMODE (NR 0x05 decoder) | 8 | un-skip | A |
| 4 — KEMP/MD3 (port 0x1F/0x37) | 24 | un-skip (B flipped early) | B |
| 5 — MD6 (FSM + NR 0xB2) | 19 | un-skip | D |
| 6 — SINC/CURS (joy→key) | 17 | un-skip (C flipped early; **plan-text swap caught**) | C |
| 7a — IOMODE pin-7 modes 00/01 | 4 | un-skip (E flipped early) | E |
| 7b — IOMODE UART (modes 10/11) | 6 | F-skip — blocked on UART+I2C plan | — |
| 8 — Mouse | 8 | un-skip (H flipped early) | H |
| 9 — NMI gate (NR 0x06 bits 3/4) | 7 | un-skip (I flipped early) | I |
| G — host-adapter (MOUSE-09/10/11) | 3 | comment-only | — (Phase 0) |
| Re-home — port 0xFE assembly | 7 | new `input_integration_test` (5 pass, 2 F-skip) | — (Phase 0/3) |
| **Total** | **126** | | |

## Architectural delta (`src/`)

- **5 NEW classes in `src/input/`**:
  - `Joystick` (mode decoder, port-1F/37 composer, set_mode_direct test bypass)
  - `KempstonMouse` (port-FADF/FBDF/FFDF composer, NR 0x0A integration)
  - `Md6ConnectorX2` (single-instance time-multiplexed FSM, NR 0xB2 byte composer, test-only phase-walk accessors)
  - `MembraneStick` (Sinclair 1/2 + Cursor joy→key adapter, COE-derived keymap)
  - `IoMode` (NR 0x0B pin-7 mux for modes 00/01)
- **Keyboard extended**: `ex_matrix_` (uint16_t, active-high), `shift_hist_[2]`, `set_extended_key`, `tick_scan`, `cancel_extended_entries`, `nr_b0_byte`, `nr_b1_byte`. ExtKey enum (16 IDs).
- **Emulator wiring**: 5 new members (single `md6_`, single `membrane_stick_`), 5 accessors, 4 test-only NMI inject accessors (`inject_hotkey_m1/drive`, `inject_sw_nmi_mf/divmmc`), 2 test-only NMI gate accessors (`nmi_assert_mf/divmmc`).
- **NR handler additions**: write 0x05 (joy mode), 0x0B (IoMode), extension of 0x0A (mouse fields), extension of 0x06 (NMI gate bits). Read handlers for 0xB0/0xB1/0xB2.
- **CTC bug fix (Wave-2 Agent E)**: added `Ctc::on_zc_to` callback (separate from `on_interrupt`) — fires unconditionally regardless of CTC channel IRQ-enable state. Wave-1 had wired the IOMode toggle through `on_interrupt`, but VHDL `joy_iomode_pin7` toggles on raw `ctc_zc_to(3)` regardless of IRQ. Latent bug fixed before it could surface.
- **Phase-1 polarity fix**: Wave-1 scaffold returned 0xFF from `nr_b0_byte`/`nr_b1_byte` (active-low membrane convention), but NR 0xB0/B1 are active-high per VHDL `:6206-6212`. Caught by post-merge regression (dapr-tilemap_00 demo read NR 0xB0 and saw ghost-presses of all 8 extended keys, breaking 64917 px of the screenshot). Fixed in commit `d30a263` to return 0x00.

## Test-plan-vs-VHDL discrepancies caught

1. **Sinclair 1 / Sinclair 2 keymap swap (Agent C)**: plan §3.7 had S1/S2 mappings swapped vs. `ram/init/keyjoy_64_6.coe`. Verified independently against VHDL mode-comment table and FUSE `peripherals/joystick.c`. Test rows updated to match COE oracle. Plan-doc edit queued.
2. **EXT-18 / EXT-19 fold-target rows (Agent G)**: plan claimed `,` folds row 5 and LEFT folds row 7; VHDL `membrane.vhd:236-240` shows `,` folds row 7 and LEFT folds row 3. Tests assert no fold into the plan-named row (VHDL-faithful). Plan-text follow-up queued.
3. **MD-09 (open question)**: plan flagged dual-MD1 as ambiguous. Agent B picked OR semantics per VHDL `:3499` literal — both bit-7:6 lanes pass, result = 0xC0. Documented inline.
4. **EAR=0 stipulation in KBD-22/23 + FE-01/02/03 (Phase 3)**: cannot drive EAR=0 from test harness (no test-only EAR setter, no MIC/EAR analog plumbing). Tests reframed to assert the actual VHDL-faithful idle invariant (bits 7/5 always 1, bit 6 = EAR idle high, cols mirror `membrane.vhd:251`). Documented inline.

## Remaining `input_test` skips (6 rows, all F-blocked)

| Row | Description | Blocking subsystem |
|---|---|---|
| IOMODE-05 | NR 0x0B=0xA0 → pin7 = uart0_tx | UART+I2C plan |
| IOMODE-06 | NR 0x0B=0xA1 → pin7 = uart1_tx | UART+I2C plan |
| IOMODE-07 | NR 0x0B=0xA0 + JOY_LEFT(5)=0 → joy_uart_rx asserted | UART+I2C plan |
| IOMODE-08 | NR 0x0B=0xA1 + JOY_RIGHT(5)=0 → joy_uart_rx asserted | UART+I2C plan |
| IOMODE-09 | NR 0x0B=0xA0 → joy_uart_en = 1 | UART+I2C plan |
| IOMODE-10 | NR 0x0B=0x80 → joy_uart_en = 0 | UART+I2C plan |

These will naturally un-block when the UART+I2C subsystem plan lands.

## Remaining `input_integration_test` skips (2 rows)

- **FE-04** — issue-2 MIC XOR EAR composition. Needs MIC/EAR analog
  signals routed into the port-0xFE read path. Out of scope for the
  Input plan; touches the audio subsystem.
- **FE-05** — expansion-bus AND with `port_fe_bus(0)`. jnext does not
  model an external expansion bus.

Both F-skipped honestly with VHDL line citations.

## Backlog items raised by this plan

1. **Plan §3.7 update** — fix Sinclair 1/2 keymap swap in
   `INPUT-TEST-PLAN-DESIGN.md` to match COE/VHDL/FUSE consensus.
2. **Plan EXT-18/19 fold-row labels** — fix to match VHDL
   `membrane.vhd:236-240`.
3. **MembraneStick runtime wiring** — `compose_into_row` is currently
   only called by tests. To make Sinclair/Cursor joystick games actually
   work at runtime, hook into `Keyboard::read_rows` (or the equivalent
   port-0xFE handler path) and feed gamepad input into
   `Joystick::set_joy_left/right` from the SDL event loop.
4. **NMI source pipeline** — Agent I implemented only the NR 0x06
   AND-gate. The full NMI pipeline (rising-edge detection, NMI-stackless,
   Multiface return-stack) remains the fragmented NMI subsystem. See
   `memory/project_nmi_fragmented_status.md`.
5. **MD6 production-tick wiring** — `Md6ConnectorX2::tick(cycles)` is
   not called from any production code path; tests use the test-only
   `step_fsm_once_for_test` accessor. To make MD6 6-button pads work at
   runtime, wire `tick(master_cycles)` into the Emulator frame loop.

## Commits landed (Task 3 Input plan)

```
~30 commits across Phase 0..4 (commit count not finalised pending Phase 4).
Phase 0:  c5c9d53 → 3de305d
Phase 1:  62969ae, ca0fe4f → 6ae7bbf, fix d30a263
Wave 1:   76b1ae6 → 70053f7 → 9868f60 → ccf508c → eeda898 (5 merges)
Wave 2:   bundled merge bringing in B/C/E/I author commits
Phase 3:  05cf019 → (Phase 3 merge)
Phase 4:  this commit + plan-doc + matrix + dashboard refresh
```

## Sign-off

All Phase-2 agents approved by independent critic before merge. All
Phase-3 flips approved by independent critic. All test status verified
on main: unit 22/0/0 suites (3219/3001/0/218), regression 34/0/0, FUSE
1356/1356.

The Input subsystem is now production-grade for keyboard, extended keys,
mouse, NR 0x0B IOMode (modes 00/01), NR 0x06 NMI gating, and joystick
mode decoding + port-1F/37 composition. Sinclair/Cursor joy→key folding
is implemented but not yet wired into the runtime read path (test-only).
MD6 6-button FSM is implemented but the production tick is not yet
wired. Both runtime-wiring tasks are tracked above for a future commit.
