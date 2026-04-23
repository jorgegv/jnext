# Task 3 — Input Subsystem SKIP-Reduction Plan (ultraplan)

## Context

**Why this change.** Task 3 of the jnext SKIP-reduction programme has driven DMA, Tilemap, Sprites, Layer 2, NextREG (bare + integration), MMU, IO Port Dispatch, DivMMC+SPI, and CTC+Interrupts to near-zero skips. The remaining large subsystems are **Input (126), ULA (75), Audio (73), UART+I2C (48)**. The user has prioritised **Input** next — single biggest remaining block at 126 skips (~85% of the `input_test.cpp` suite), and groundwork from CTC+Interrupts (joy_iomode_pin7, NR 0x20 dispatch) is already landed.

**Current state.** `input_test.cpp` reports `149 total / 23 pass / 0 fail / 126 skip`. Aggregate unit 3212/2876/0/336 (21 suites), regression 34/0/0, FUSE 1356/1356. Working tree clean on main.

**Intended outcome.** Drive `input_test.cpp` to `6 skip / 0 fail` (~96% coverage), with 7 integration-tier port-0xFE-assembly rows re-homed to new `test/input/input_integration_test.cpp`, 3 host-adapter MOUSE rows converted to `// G:` comments, and only the 6 UART-blocked IOMODE rows left as justified `F-skip` until UART+I2C plan lands.

**Plan shape.** 5 phases (0-4), 2 waves of parallel agents (5-agent budget from `feedback_parallel_agent_budget_20260421.md`), with independent critic review **inline per agent** (same flow as CTC: each Phase-2 agent's work is reviewed and merged before the next dependent agent starts).

---

## Skip landscape (pre-triaged by Explore-agent audit)

**126 skipped rows classify as**:

| Cluster | Rows | Count | Size | Depends on |
|---|---|---|---|---|
| **1 — Shift hysteresis** | KBDHYS-01/02/03 | 3 | S | — |
| **2 — Extended-key matrix (NR 0xB0/0xB1)** | EXT-01..20 | 20 | M | — |
| **3 — Joystick mode decoder (NR 0x05)** ⚑ | JMODE-01/02/02r/03/04/05/06/07 | **8** | S | gates 4, 6 (hard); 5, 7 soft via stubbed `set_mode_direct()` |
| **4 — Kempston 1/2 + MD3 ports (0x1F/0x37)** | KEMP-01..15 + MD-01..09 | 24 | M | cluster 3 (hard) |
| **5 — MD6 state machine + NR 0xB2** | MD6-01..11i | 19 | L | cluster 3 only for MD6-10 (negative assertion); other 18 rows drive FSM directly with joy-bit stimulus and are mode-blind |
| **6 — Sinclair/Cursor/UserDef joy→key adapters** | SINC-06, SINC1-01..05, SINC2-01..05, CURS-01..06 | 17 | M | cluster 3 (hard); also depends on `keyjoy_64_6.coe` default keymap |
| **7 — NR 0x0B I/O-mode pin-7 mux** | IOMODE-02/03/04/11 | 4 | S | independent — pin-7 mux is mode-blind |
| **7-UART-blocked** | IOMODE-05/06/07/08/09/10 | 6 | — | F-skip blocked on UART+I2C plan |
| **8 — Kempston mouse (0xFADF/FBDF/FFDF)** | MOUSE-01..08 | 8 | S | — |
| **9 — NMI hotkey + SW NMI gating (NR 0x06)** | NMI-01..07 | 7 | S | — |
| **G — Host-adapter (→ comment)** | MOUSE-09/10/11 | 3 | — | — |
| **Re-home → integration test** | KBD-22/23, FE-01..05 | 7 | — | — |

**Accounting**: 3 + 20 + 8 + 24 + 19 + 17 + 4 + 6 + 8 + 7 + 3 + 7 = **126**. ✓

**Critical-path nuance** (post-critic): JMODE genuinely blocks clusters 4 and 6. Clusters 5 (MD6) and 7 (IOMODE-02/03/04/11) can run in parallel with JMODE if Phase 1 scaffolds a `joystick_.set_mode_direct(joy0, joy1)` test-harness setter (same pattern CTC used for `joy_iomode_pin7`).

**Wave layout adopted (rebalanced per critic Issue #4)**:
- Wave 1 (5 agents, ~58 rows): A (JMODE 8), D (MD6 19, via stubbed decoder), F (KBDHYS 3), G (EXT 20), H (MOUSE 8)
- Wave 2 (4 agents, ~52 rows): B (KEMP/MD3 24), C (SINC/CURS 17), E (IOMODE 4), I (NMI 7)

---

## Architectural expansion — `src/input/`

Current `src/input/` contains only `keyboard.{h,cpp}`. Phase 1 scaffolds the rest:

| New/touched file | Purpose | Key public surface |
|---|---|---|
| `src/input/joystick.{h,cpp}` **NEW** | Joystick state + NR 0x05 mode decoder + port-1F/37 read composer | `set_nr_05(byte)` (real decoder), `set_mode_direct(joy0_mode, joy1_mode)` (**test-harness stub for Wave-1 D/E parallelism**), `read_port_1f()`, `read_port_37()`, accessors for mode bits |
| `src/input/mouse.{h,cpp}` **NEW** | KempstonMouse X/Y/buttons/wheel + port-FADF/FBDF/FFDF | `inject_delta(dx,dy)`, `set_buttons(mask)`, three-port reads, DPI scaling from NR 0x0A |
| `src/input/md6_connector_x2.{h,cpp}` **NEW** (**single instance, per critic Issue #5**) | MD6 dual-connector time-multiplexed FSM per `md6_joystick_connector_x2.vhd:66-193` (9-bit state counter, 8 live sub-phases). Owns the full 12-bit L+R `JOY_LEFT`/`JOY_RIGHT` vectors (per critic Issue #6) and exposes byte slices to Joystick. | `tick()`, `nr_b2_byte()`, `joy_left_word() / joy_right_word()` (12-bit reads for Joystick's port-1F/37 composer), `raw_connector_inputs()` setter |
| `src/input/membrane_stick.{h,cpp}` **NEW** (**new class per critic Issue #7**) | Sinclair 1/2 + Cursor + user-defined joy→key adapter per `membrane_stick.vhd:117-198` + default keymap from `ram/init/keyjoy_64_6.coe:1-66`. Independent from `Keyboard`. | `set_mode(joy_type)`, `inject_joystick_state(side, 12-bit, mode)`, `compose_into_membrane(keyboard_matrix&)` |
| `src/input/iomode.{h,cpp}` **NEW** | NR 0x0B pin-7 mux: static iomode_0 / CTC-toggled (modes 00/01 only; UART modes stay stubbed — F-skip) | `set_nr_0b(byte)`, `tick_ctc_zc3()`, `pin7()` |
| `src/input/keyboard.{h,cpp}` **EXTEND** | Shift hysteresis state + 16-bit `ex_matrix_` word + extended-column folding. **No joy-row injection** — that lives in `membrane_stick`. | Keep `read_rows` semantics; add `set_extended_key(id, pressed)`, `tick_scan()` for hysteresis |
| `src/port/nextreg.cpp` **WIRE** | Install ONE write_handler per NR that fans out to multiple owners (per critic Issue #10): NR 0x06 fans out to Joystick + NMI + existing audio/CPU-speed bits. Install `set_write_handler` for NR 0x05 / 0x06 / 0x0A / 0x0B; install `set_read_handler` for NR 0xB0 / 0xB1 / 0xB2. Uses existing hook API. Avoids the shadow-store bug per `project_systemic_nextreg_shadow_store.md`. | |
| `src/core/emulator.{h,cpp}` **WIRE** | Add members `Joystick joystick_`, `KempstonMouse mouse_`, `Md6ConnectorX2 md6_` (**single**), `MembraneStick membrane_stick_`, `IoMode iomode_`. Replace 0-stub port handlers at `emulator.cpp:1381-1401`. Reset + save/load additions. Add **test-only** `inject_hotkey_m1/drive` + `inject_sw_nmi_*` accessors (per critic Issue #8: group in a clearly-fenced `// === TEST-ONLY ACCESSORS ===` block). NMI-inject signals wire into existing `z80_cpu_.trigger_nmi()` path per the fragmented-NMI landscape (per critic Issue #11). | — |
| `test/input/input_integration_test.cpp` **NEW** | 7 re-home rows (KBD-22/23, FE-01..05) against full Emulator + port_dispatch | Mirror `test/ctc_interrupts/ctc_interrupts_test.cpp` |
| `test/CMakeLists.txt` | Register the new integration test target | — |

**SDL/Qt UI** wiring of gamepad + mouse events is **out of scope** for the test-plan phase — stubbed feeders in test harness are sufficient to drive the plan rows. UI wiring can follow in a separate commit post-merge.

---

## Phase plan (mirrors CTC)

### Phase 0 — Triage + comment sweep (self, ~1h)

**Actions:**
1. Convert MOUSE-09/10/11 (3 G-rows) to `// G: <reason>` comments. **Explicit**: the `skip(...)` calls are **removed**; only the commented-out describing-block remains. The 3 rows no longer appear in the test count.
2. Re-home KBD-22/23 + FE-01..05 (7 rows) to `// RE-HOME: see test/input/input_integration_test.cpp` comments. Same treatment — `skip()` calls removed; row disappears from this test file and reappears in the new integration suite.
3. Refresh skip-reason strings on all 116 remaining skips to reference the relevant Phase-2 branch (e.g. `"Un-skip via task3-input-a-joymode"`). For the 6 UART-blocked IOMODE rows (IOMODE-05/06/07/08/09/10) use `"F: blocked on UART+I2C subsystem plan"`.
4. **Critic**: 1 agent reviews comment dispositions against VHDL.

**Delta after Phase 0:** `139 / 23 pass / 0 fail / 116 skip` (10 rows removed from test file: 3 G → comment, 7 re-home → moved to new integration suite).

---

### Phase 1 — API scaffold (one agent, ~3h)

**Branch**: `task3-input-0-scaffold`. Compile-only stubs — every new method returns default/zero; existing 23 passes must remain green.

**Deliverables:**
- Create `Joystick`, `KempstonMouse`, `Md6ConnectorX2`, `MembraneStick`, `IoMode` classes with headers + empty impls.
- **Critical for Wave-1 parallelism**: `Joystick::set_mode_direct(joy0, joy1)` test-harness setter that bypasses NR 0x05 — lets Agents D and E run in Wave 1 alongside A.
- Extend `Keyboard` with `ex_matrix_` (uint16_t) + `shift_hist_` + `tick_scan()` stubs (no `inject_joy_row` — that lives in `MembraneStick`).
- Add Emulator members (single `Md6ConnectorX2`, single `MembraneStick`) + install NR write_handlers / read_handlers (all stubbed). Single fan-out write-handler for shared NRs (NR 0x06).
- Replace zero-stub port handlers in `emulator.cpp:1381-1401` with handlers that delegate to the stubs.
- Add test-only Emulator inject accessors in a fenced `// === TEST-ONLY ACCESSORS ===` block.
- Update `CMakeLists.txt`.

**Critic**: 1 agent. APPROVE gate before Phase 2 starts.

---

### Phase 2 — Parallel agent waves + inline critic review (5-budget, 2 waves, ~2 days)

**Per-agent flow** (same cadence as CTC+Interrupts execution):
1. Main session creates worktree and launches agent with brief.
2. Agent returns with src/ diff summary (no build/test/commit inside worktree).
3. Main session launches an independent **critic agent** (different instance) to review the diff against VHDL + plan row coverage.
4. Critic returns APPROVE / APPROVE-WITH-FIXES / REJECT. Author applies fixes on same branch.
5. Main session runs `LANG=C make -C build unit-test` + `bash test/regression.sh` from the branch.
6. Main session commits + merges `--no-ff` into main (for Wave 1 independent agents) or into `task3-input-a-joymode`'s merge base (for Wave 2 agents that depend on A).
7. Next agent in dependency chain unblocks.

**Wave 1 (5 parallel agents)** — kicks off after Phase 1 merge. All can run independently because the scaffold's `set_mode_direct()` test-setter lets D run without A's real decoder.

| Agent | Branch | Cluster | Rows | VHDL anchors |
|---|---|---|---|---|
| **A** JOY-MODE (⚑ critical-path for B+C in Wave 2) | `task3-input-a-joymode` | Cluster 3 (JMODE, 8 rows) | 8 | `zxnext.vhd:5157-5158, 3429-3438, 1105-1106` |
| **D** MD6-FSM (uses `set_mode_direct` stub) | `task3-input-d-md6fsm` | Cluster 5 (MD6, 19 rows; full FSM) | 19 | `md6_joystick_connector_x2.vhd:66-193`, `zxnext.vhd:6215, 3441-3442`. **Single instance** owning both connectors per VHDL time-multiplex (critic Issue #5). |
| **F** SHIFT-HYS | `task3-input-f-shifthys` | Cluster 1 (KBDHYS, 3 rows) | 3 | `membrane.vhd:180-232, 183-186` |
| **G** EXT-MATRIX | `task3-input-g-extmatrix` | Cluster 2 (EXT, 20 rows) | 20 | `membrane.vhd:150-175, 236-240`, `zxnext.vhd:6206-6212` |
| **H** MOUSE | `task3-input-h-mouse` | Cluster 8 (MOUSE, 8 rows) | 8 | `zxnext.vhd:2668-2670, 3543-3561, 104`; NR 0x0A `zxnext.vhd:5191-5198` |

**Wave 2 (4 parallel agents)** — kicks off after Agent A merges (B and C need the real NR 0x05 decoder for handler tests).

| Agent | Branch | Cluster | Rows | VHDL anchors |
|---|---|---|---|---|
| **B** KEMP-MD3 | `task3-input-b-kempmd3` | Cluster 4 (KEMP + MD, 24 rows) | 24 | `zxnext.vhd:3441-3442, 3475-3506, 2454, 2674` |
| **C** SINC-CURS | `task3-input-c-sinccurs` | Cluster 6 (SINC + CURS, 17 rows) | 17 | **`membrane_stick.vhd:117-198`** + **`ram/init/keyjoy_64_6.coe:1-66`** (default keymap is the authoritative oracle — critic Issue #2 / #9). Class: `MembraneStick` (NEW, not folded into `Keyboard`). |
| **E** IO-MODE | `task3-input-e-iomode` | Cluster 7 (IOMODE, 4 rows: IOMODE-02/03/04/11) | 4 | `zxnext.vhd:3510-3524, 5200-5203`. S-size. IOMODE-05/06/07/08/09/10 stay F-skip blocked on UART+I2C plan. |
| **I** NMI-GATE | `task3-input-i-nmigate` | Cluster 9 (NMI, 7 rows) | 7 | `zxnext.vhd:2090-2091, 5161-5170`. NMI signals wire into `z80_cpu_.trigger_nmi()`. |

**Per-agent brief template** (~15 lines):
- Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/<branch>`
- Files to touch: enumerated
- VHDL lines (authoritative): enumerated
- Required public API: signatures with VHDL citations
- Rows unblocked: listed with test IDs
- Out-of-scope: enumerated (no build, no test, no commit — main session does that)

**Critic agent brief** (one per Phase-2 agent, same type expertise but different instance):
- Read the author's diff + the relevant VHDL line ranges + the plan row list.
- Sample 10+ assertions; verify each VHDL citation matches hardware.
- For every `skip()` / `// WONT` that remains, trace stimulus through VHDL to confirm it's genuinely unreachable.
- Lint-sweep: no tautologies, no C++-implementation-as-oracle.
- Verdict: APPROVE / APPROVE-WITH-FIXES / REJECT.
- Author applies APPROVE-WITH-FIXES items on the same branch before main session merges.

**10 total Phase-2 review rounds**: 9 agent-critic pairs (A/B/C/D/E/F/G/H/I) + 1 for the Phase-1 scaffold.

---

### Phase 3 — Un-skip + integration tests (self, ~1 day)

**Actions:**
1. In `test/input/input_test.cpp`, flip `skip()` → `check()` for every row unblocked by a merged Phase-2 branch. Each new `check()` cites VHDL `file.vhd:line` in its label.
2. Create `test/input/input_integration_test.cpp` with the 7 re-home rows — full Emulator + port_dispatch stimulus.
3. Update `test/CMakeLists.txt`.
4. Run `LANG=C make -C build unit-test` + `bash test/regression.sh` to verify no regression.
5. Launch 1 critic for the integration test file (diff review against VHDL port-0xFE assembly logic).

**Expected delta:** `139 / 133 / 0 / 6` in `input_test.cpp` (-10 rows removed in Phase 0; 110 skip→check flips in Phase 3; 6 UART-blocked skips remain) + new `input_integration_test` at `7/7/0/0`.

---

### Phase 4 — Merge + dashboard (self, ~2h)

Merge order (corrected per critic Issue #13): Wave 1 first (A, D, F, G, H — independent of each other and of Wave 2), then Wave 2 (B, C, E, I) — all `--no-ff`. After each merge, re-run unit + regression. Refresh:
- `doc/testing/INPUT-TEST-PLAN-DESIGN.md` current-status block
- `test/SUBSYSTEM-TESTS-STATUS.md` dashboard
- `doc/testing/TRACEABILITY-MATRIX.md`
- `doc/design/EMULATOR-DESIGN-PLAN.md` aggregate
- New audit `doc/testing/audits/task3-input-phase5.md`

Final commit: `task3(input): skip 126 → 6 via joystick/mouse/MD6/MembraneStick/IOMode/ExtKeys fabric expansion`.

---

## Risk table

| # | Risk | Mitigation |
|---|---|---|
| **R1** | Systemic NR shadow-store bug bites NR 0xB0/0xB1/0xB2 (raw byte pre-handler stored, masked handler output lost on readback) | Use `set_read_handler` (per-register composed-byte pattern) — the same fix that worked for NR 0x12/0x13 + CTC 0x20. Don't touch the systemic `NextReg::write` — out of scope. |
| **R2** | Extending Keyboard to 8×7 matrix breaks 21 existing passes | Phase 1 scaffold preserves `read_rows` semantics (still returns 5-bit row AND). Extended columns are a *new* field `ex_matrix_` read only via NR 0xB0/0xB1 handler, never via port 0xFE. |
| **R3** | MD6 state machine complexity (L-size, 19 rows, 8-phase sequencer) bloats wave 2 critical path | Full FSM per user decision. Agent D gets dedicated worktree + generous scope. If critic rejects on depth, fall back to functional subset in follow-up commit. |
| **R4** | IOMode UART-mode rows require UART plumbing not yet in place | Resolved: 6 UART rows stay `F-skip` blocked on UART+I2C subsystem plan. Agent E scope limited to pin-7 modes 00/01. |
| **R5** | NMI cluster tests AND gates but hotkey sources are from fragmented NMI subsystem | Resolved: add test-only Emulator accessors `inject_hotkey_m1/drive` + `inject_sw_nmi_mf/divmmc`. Gate logic is VHDL-faithful regardless of source. |

---

## Resolved design decisions (user input 2026-04-21)

1. **IOMode module placement** — **separate file** `src/input/iomode.{h,cpp}`. (Recommendation adopted by default.)
2. **MD6 depth** — **full cycle-accurate FSM** per `md6_joystick_connector_x2.vhd:66-193`. All 19 rows (MD6-01..11i) target un-skip. Agent D is L-size.
3. **IOMODE UART depth** — **mark IOMODE-05/06/07/08/09/10 as `F skip`**, blocked on future UART+I2C subsystem plan. Agent E scope = pin-7 static + CTC-toggled (modes 00/01, 4 rows: IOMODE-02/03/04/11). S-size.
4. **Integration suite location** — **new** `test/input/input_integration_test.cpp` (7 rows). Mirrors CTC+Interrupts.
5. **Extended-key storage** — **separate `ext_matrix_` field** (uint16_t) inside Keyboard. Preserves `read_rows` semantics for the 21 passing tests. (Recommendation adopted by default.)
6. **NMI gate testing** — **yes**, add test-only Emulator accessors: `inject_hotkey_m1(bool)`, `inject_hotkey_drive(bool)`, `inject_sw_nmi_mf(bool)`, `inject_sw_nmi_divmmc(bool)`. Documented as test-only. All 7 NMI rows target un-skip.
7. **PS/2 keyboard** — **no stub** in this plan. No skipped row depends on it.

---

## Acceptance criteria (end of Phase 5)

Given resolved decisions (UART F-skip, NMI inject accessors land, full MD6 FSM):

- **`input_test`**: `139 / 133 / 0 / 6` (down from 149/23/0/126). Only the 6 UART-blocked IOMODE rows remain as F-skips.
- **`input_integration_test`**: `7 / 7 / 0 / 0` (new suite covering KBD-22/23 + FE-01..05)
- **Row accounting**: Phase 0 removes 10 rows from `input_test.cpp` (3 G-comments + 7 re-home); Phase 3 flips 110 skip→check in remaining clusters.
- Aggregate unit: 3212/2876/0/336 → **~3209 / ~2993 / 0 / ~226** (+117 pass, −110 skip project-wide; net −120 in skip column since the 10 Phase-0 removals also reduce total).
- Regression: **34 / 0 / 0** unchanged
- FUSE: **1356 / 1356** unchanged
- Independent critic APPROVE on all 11 review rounds (1 Phase-1 scaffold + 9 Phase-2 agents + 1 Phase-3 integration test), reviewed **inline** as each piece of work lands
- All docs refreshed + audit file created

---

## Critical files to modify

**New:**
- `src/input/joystick.{h,cpp}`
- `src/input/mouse.{h,cpp}`
- `src/input/md6_connector_x2.{h,cpp}` (single instance per critic Issue #5)
- `src/input/membrane_stick.{h,cpp}` (new class per critic Issue #7)
- `src/input/iomode.{h,cpp}`
- `test/input/input_integration_test.cpp`
- `doc/testing/audits/task3-input-phase4.md`

**Extended:**
- `src/input/keyboard.{h,cpp}`
- `src/port/nextreg.{h,cpp}`
- `src/core/emulator.{h,cpp}`
- `test/input/input_test.cpp` (un-skips + comment migrations)
- `test/CMakeLists.txt`

**Refreshed (Phase 5):**
- `doc/testing/INPUT-TEST-PLAN-DESIGN.md`
- `doc/testing/TRACEABILITY-MATRIX.md`
- `doc/design/EMULATOR-DESIGN-PLAN.md`
- `test/SUBSYSTEM-TESTS-STATUS.md`

**New plan doc** (Phase 0 deliverable — authoritative source for execution):
- `doc/design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md` — the full plan, authored from this one after approval.

---

## Verification

End-to-end verification across the full plan:

1. **Phase 0 gate**: `LANG=C make -C build unit-test 2>&1 | grep input_test` shows 139 total / 23 pass / 0 fail / 116 skip (10 rows removed to comments + integration-suite re-home). Critic APPROVE on Phase-0 diff.
2. **Phase 1 gate**: build green + `input_test` unchanged (139/23/0/116); critic APPROVE.
3. **Per-agent Phase 2 gate**: critic APPROVE before merge; unit + regression green on branch; APPROVE-WITH-FIXES items applied on same branch before merge.
4. **Phase 3 gate**: `LANG=C make -C build unit-test` and `bash test/regression.sh` both green; `input_test` matches projected delta per merged branches; integration-test critic APPROVE.
5. **Phase 4 gate**: main-branch unit + regression + FUSE all green; all docs refreshed; audit committed.

---

## Execution timeline estimate

- Phase 0: 1h (self + critic)
- Phase 1: 3h (1 agent + 1 critic + merge)
- Phase 2 Wave 1: 1 day (5 parallel agents, each with inline critic + branch build + merge)
- Phase 2 Wave 2: 1 day (4 parallel agents, gated on A merge; each with inline critic + merge)
- Phase 3: 4h (self + 1 critic on integration test)
- Phase 4: 2h (self — merge final bundle + dashboard refresh)

**Total**: ≈ 2.5 days of active session time. Can be done in one long sitting like the CTC plan was (37 commits in a single session), but spanning two sessions is more realistic given MD6 FSM depth.

---

## Execution status (live, 2026-04-22)

### Phase 0 — DONE (2026-04-21)

Commits on main: `c5c9d53` + merge `3de305d`. Test-file delta: `149/23/0/126 → 139/23/0/116`. Independent critic APPROVE.

### Phase 1 — DONE (2026-04-21)

Commits on main: `62969ae` (5 scaffold classes), `ca0fe4f` (Emulator + Keyboard wiring), merge `6ae7bbf`, fix `d30a263` (NR 0xB0/0xB1 active-high polarity correction — caught a real bug where the dapr-tilemap_00 demo read NR 0xB0 and saw ghost-presses of all 8 extended keys, scrolling the tilemap demo by 64917 px). Independent critic APPROVE-WITH-FIXES (fixes = commit the work, applied; polarity bug found by main-session regression).

End state: unit 21/21 PASS (3212/2886/0/326), regression 34/0/0, FUSE 1356/1356.

### Phase 2 Wave 1 — agents complete, critics complete, merges pending

| Agent | Cluster | Rows | Branch | Build | Unit | Regression | Critic verdict |
|---|---|---|---|---|---|---|---|
| **A** | JMODE | 8 | `task3-input-a-joymode` | clean | 139/23/0/116 (Phase 3 flips) | 34/0/0 | APPROVE-WITH-FIXES (trivial — branch-drift settings.json/prompt revert) |
| **D** | MD6 FSM | 19 | `task3-input-d-md6fsm` | clean | 139/23/0/116 (Phase 3 flips) | 34/0/0 | APPROVE-WITH-FIXES (must commit first; doc nits) |
| **F** | KBDHYS | 3 | `task3-input-f-shifthys` | clean | 139/23/0/116 (Phase 3 flips) | 34/0/0 | APPROVE |
| **G** | EXT | 20 | `task3-input-g-extmatrix` | clean | **139/43/0/96** (20 EXT flipped, all PASS) | 34/0/0 | APPROVE — caught plan typo on EXT-18/19 fold rows |
| **H** | MOUSE | 8 | `task3-input-h-mouse` | clean | **139/31/0/108** (8 MOUSE flipped, all PASS) | 34/0/0 | APPROVE |

Notes on Wave-1 deviations:
- **G + H** went ahead of schedule by also flipping their `skip()` rows to `check()` in `test/input/input_test.cpp`. This is technically Phase 3's job, but the early flips (with all PASS) provide strong correctness evidence.
- **G** caught a plan-text inconsistency: plan rows EXT-18/19 name fold-target rows that don't match `membrane.vhd:236-240`. Agent G's tests assert the VHDL-faithful behavior (no fold into the plan-named row) and document the resolution inline. Plan text may need revising.
- **D** uses test-only accessors (`set_state_for_test`, `step_fsm_once_for_test`, `set_latched_*_for_test`, `set_six_button_*_for_test`) so the FSM phase rows can be stimulated deterministically without driving 512-tick sequences.

Pending action: stage-and-commit Agent D's work, then merge Wave 1 (A → D → F → G → H, or any order — they're independent of each other).

### Phase 2 Wave 2 — pending

Kicks off after Wave 1's Agent A merges (Agents B and C depend on A's NR 0x05 decoder). Agents B/C/E/I will run in parallel.

### Phase 3, Phase 4 — pending

---

## Phase 4 — DONE (2026-04-22)

Final docs/dashboard refresh + audit.

**Landed:**
- `test/SUBSYSTEM-TESTS-STATUS.md` — Input row updated, new `Input (integration)` row added, total recomputed (3219/3001/0/218).
- `doc/testing/TRACEABILITY-MATRIX.md` — Input row updated; new `Input (int)` row.
- `doc/testing/INPUT-TEST-PLAN-DESIGN.md` — Current Status block at top, plan-text inconsistency notes for §3.7 (SINC1/2 swap) and EXT-18/19.
- `doc/design/EMULATOR-DESIGN-PLAN.md` — Phase-9 task-list line for Input updated to `[~]` with full plan summary.
- `doc/testing/audits/task3-input-phase4.md` — NEW closure audit (this plan's complete record).
- `doc/design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md` — this file, with final-state stanza.

## End-of-plan summary

|                       | Plan baseline | Plan target | Achieved |
|-----------------------|--------------:|------------:|---------:|
| `input_test` total    |          149  |       139   |     139  |
| `input_test` pass     |           23  |       133   |     133  |
| `input_test` skip     |          126  |         6   |       6  |
| `input_integration_test`      |         N/A   |    7/7/0/0  |  7/5/0/2 |
| Aggregate skip Δ      |          —    |       −120  |    −118  |

Reasons for the −2 vs −120 plan target on aggregate:
- 2 FE rows (FE-04 issue-2 MIC^EAR, FE-05 expansion-bus AND) honestly F-skipped in the integration suite — outside the Input subsystem boundary; not implementable without audio analog plumbing or expansion-bus modelling work.

Total agent-team effort: 1 scaffold agent, 9 implementation agents (5 Wave 1 + 4 Wave 2), 1 Phase-3 agent, 11 independent critic reviews. ~30 commits on main. Two real bugs found and fixed during execution:
1. NR 0xB0/0xB1 polarity — Phase 1 scaffold returned 0xFF, VHDL is active-high. Caught by post-merge regression failing the dapr-tilemap_00 demo. Fixed in `d30a263`.
2. CTC `on_zc_to` vs `on_interrupt` — Wave 1 Agent H wired the IOMode toggle through IRQ-gated `on_interrupt`; VHDL toggles on raw ZC. Caught by Agent E during Wave 2; fixed by adding a separate `on_zc_to` callback in `src/peripheral/ctc.{h,cpp}`.

Plan-doc inconsistencies caught (queued for follow-up edits):
1. §3.7 SINC1/SINC2 keymap swap — three independent oracles agree (COE, VHDL mode-comment table, FUSE).
2. EXT-18/19 fold-row labels — VHDL `membrane.vhd:236-240` puts `,` in row 7 (not 5) and LEFT in row 3 (not 7).

**Status: CLOSED (plan executed, all phases landed, all critics APPROVE).**
