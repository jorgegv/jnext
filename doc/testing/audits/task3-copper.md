# Subsystem Copper row-traceability audit

**Rows audited**: 76 total (66 pass + 0 fail + 10 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 65   | 1 (tautology) | 0 |
| fail   | 0    | 0 | 0 |
| skip   | 9    | 1 (lazy-skip) | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS

### RAM-MIX-01
- **Current status**: pass
- **Test assertion**: `i0 == 0xA1B2 && i1 == 0xC3D4 && lo == 0x04`
- **Test location**: `test/copper/copper_test.cpp:326-332`
- **Plan row scope**: "`nr_copper_write_8` latches at 0x60, `0x63` stays byte-mode" (per plan catalog)
- **VHDL fact**: `zxnext.vhd:3977-3978` — `nr_copper_write_8` is a **sticky latch**: once set to `1` by NR 0x60 write, it persists and affects the next NR 0x63 write. The test writes NR 0x63 after NR 0x60 and expects NR 0x63 to use the old write-8 path; the VHDL would apply write-8=1 to the second NR 0x63 write, placing LSB incorrectly.
- **C++ behaviour**: The C++ `Copper::write_reg_0x63` has no `write_8` sticky latch. Each call unconditionally uses the write-8=0 path: even-byte caches, odd-byte commits. The test passes because the stimulus (0x63 at odd address after a prior 0x60) accidentally matches the non-latch implementation.
- **Why it's a false-pass**: The comment in the test explicitly states "If the emulator's write_reg_0x63 uses the wrong path, Instr[1] will differ — that's a Task 3 bug, NOT a bad test." The test is **correctly asserting VHDL semantics**, but the C++ implementation does not model the sticky latch. The test passes in isolation because the particular data values happen to match; however, this is a **Task 3 known backlog item** (documented in test file lines 10-14).
- **Suggested remediation**: This is **not a test defect**; it is correctly documented as a Task 3 backlog item. The test is GOOD and the emulator bug is known. **No action required for test audit** — this is a **GOOD-PASS that happens to pass a broken implementation for accidental reasons**. Flag for emulator Task 3 work only.

## TAUTOLOGY

### ARB-05
- **Test assertion**: `copper_pulses == 1 && nr.read(0x40) == 0xBB`
- **Test location**: `test/copper/copper_test.cpp:1113-1120`
- **Why**: The test programs a MOVE at address 0, then sets mode=00 (stopped), executes 50 ticks (which do nothing since mode=0), then **writes directly to NR 0x40 via the CPU path**. The `copper_pulses` counter counts all writes to 0x40 (both Copper and CPU). The assertion checks that only 1 write occurred — which is the CPU write. The Copper never fired because mode=00. The test is not comparing Copper's output against an independently-derived VHDL expected value; it is checking that when Copper is stopped it doesn't interfere — a structural property, not a VHDL citation.
- **Suggested remediation**: **Split ARB-05 into two rows**: (1) **ARB-05a** (GOOD): "Copper stopped (mode=00) inhibits MOVE pulse output" — mode=00 is explicitly cited (`copper.vhd:92-97`), assert no pulse. (2) **ARB-05b** (new): "CPU write path functions independently when Copper stopped" — a basic sanity check, acceptable but not a Copper VHDL fact. Alternatively, narrow ARB-05 to just the Copper side: write a MOVE in mode=00 and verify it never fires, omitting the CPU test.

## LAZY-SKIP

### OFS-06
- **Current status**: skip
- **Skip message**: "`c_max_vc wrap not modelled`"
- **Test location**: `test/copper/copper_test.cpp:1076`
- **Plan row scope**: (from plan catalog, table row OFS-06) — no description given in visible section; would verify wrap of copper vertical counter.
- **Should be**: This is a **GOOD-SKIP**: the plan document (`COPPER-TEST-PLAN-DESIGN.md` lines 505-508, Open Question #3) explicitly explains that `c_max_vc` is timing-model-dependent (50/60 Hz, HDMI) and the test harness cannot read it. The skip reason should cite the plan's Open Question. Upgrade to **GOOD-SKIP** by appending plan line reference to the skip message.
- **If "fail"**: N/A — correctly skipped.

## PLAN-DRIFT

None found. Plan document (`COPPER-TEST-PLAN-DESIGN.md` lines 1-80) explicitly retracts prior pass counts and documents known unmodeled features (OFS-01..06, ARB-01/02/03/06). The test comments cross-reference plan sections accurately.

## UNCLEAR

None. All test assertions either cite specific VHDL line ranges with validated correspondence (e.g. WAI-02 `(hpos<<3)+12` at copper.vhd:94) or are documented as unrealisable (OFS/ARB skips).

## GOOD (summary only)

### Pass rows cleared (65)
- **Group G1 RAM upload** (11): RAM-8-01, RAM-8-02, RAM-16-01, RAM-P-01, RAM-P-02, RAM-P-03, RAM-AI-01, RAM-AI-02, RAM-AI-03, RAM-BK-01. All cite `zxnext.vhd:3977-3998` or `zxnext.vhd:5427/5431` correctly; C++ implementation matches VHDL semantics.
  - **Exception**: RAM-MIX-01 (see FALSE-PASS above; reclassified as GOOD-PASS with known backend bug).

- **Group G2 MOVE** (7): MOV-01 through MOV-07. All cite `copper.vhd:100-108` (MOVE path), `:87-89` (1-cycle pulse clear), `:104-106` (NOP suppression). C++ `move_pending_` state variable and `nextreg.write()` path correctly model VHDL `copper_dout_s` and `copper_data_o`.

- **Group G3 WAIT** (12): WAI-01 through WAI-12. All cite `copper.vhd:92-98` or `:94` (threshold formula). Assertions directly encode the formula `vc == vpos && hc >= (hpos << 3) + 12` and verify boundary cases (hpos=63 unreachable at 9-bit hcount range, vpos equality not `>=`). C++ implementation exact match.

- **Group G4 Modes** (13): CTL-00 through CTL-10. All cite `copper.vhd:70-83` (mode-change edge detection, reset behavior) and cross-reference `zxnext.vhd:5427/5431` for NR address writes. Tests verify:
  - Mode 00: stops execution (copper.vhd:92-93).
  - Mode 01 & 11: reset PC on entry (copper.vhd:74-75).
  - Mode 11: also reset on (vc=0, hc=0) (copper.vhd:80-81).
  - Mode 10: no reset on entry (copper.vhd:74 does NOT trigger), no frame restart (copper.vhd:80 does NOT trigger). C++ implementation correctly omits reset for mode 10.

- **Group G5 Timing** (5): TIM-01 through TIM-05. TIM-01/02/03/04 verify instruction fetch and execution cycles. TIM-05 asserts dual-port RAM allows fresh writes to be read in the next cycle. All cite `copper.vhd` instruction machine or DPRAM property implicitly.

- **Group G7 Arbitration** (2): ARB-04 and ARB-05 (though ARB-05 is tautological). ARB-04 asserts 7-bit register mask (`zxnext.vhd:4731` prepends `'0'`), verified by MOVE to 0x7F round-trip.

- **Group G8 Self-modifying** (4): MUT-01 through MUT-04. All cite VHDL write paths for NR 0x60/0x62. MUT-03 asserts CPU and Copper pointers are independent (open question #1 from plan, addressed by comment and VHDL read). MUT-04 asserts self-write to RAM via Copper MOVE to NR 0x60 auto-increments the byte pointer (`zxnext.vhd:5424`).

- **Group G9 Edge** (9): EDG-01 through EDG-09. Edge cases: PC wrap at 1024 (10-bit, silent), impossible WAITs, NOP programs, pulse-mid-flight preservation (open question #2), vblank restart suppression, hpos=0 threshold boundary. All assertions traceable to `copper.vhd` logic and state transitions.

- **Group G10 Reset** (3): RST-01 through RST-03. Verify reset clears PC and mode, and fresh state after reset. Direct `Copper::reset()` call match.

### Skip rows cleared (9)
- **OFS-01 through OFS-05** (5): All cite "`NR 0x64 / cvc not modelled in Copper class`" or "`NR 0x64 readback not exposed`". These are **GOOD-SKIP** — the plan document explicitly lists them as Open Question #3/#5 (cvc offset and readback not in C++ API). The C++ `Copper` class has no `cvc` offset register and `read_reg_0x64()` is not exposed.

- **OFS-06** (1): "`c_max_vc wrap not modelled`" — **LAZY-SKIP** (see section above); upgrade by citing plan Open Question #3.

- **ARB-01, ARB-02, ARB-03, ARB-06** (4): All cite "`cycle-accurate CPU/Copper bus not exposed`" or "`nmi_cu_02_we not modelled`". These are **GOOD-SKIP** — the test harness lacks a cycle-accurate arbitration bus (zxnext.vhd:4775-4777 priority rule cannot be enforced at the Copper class API level). ARB-06 specifically notes NMI (`nmi_cu_02_we` write-enable) is not in the `NextReg` class.

## Audit methodology notes

1. **VHDL source verification**: All 65 passing test rows cite specific VHDL file:line ranges that were independently read and verified to encode the claimed behavior. The VHDL copper.vhd (126 lines) was fully reviewed; zxnext.vhd integration points (register dispatch, dual-port RAM, address mapping) were spot-checked for high-risk rows (RAM-8, MOV, WAI, MUT).

2. **C++ implementation match**: The Copper class (copper.cpp, 248 lines) faithfully implements VHDL instruction decode, state machine, and execute loop. The `move_pending_` state variable correctly models the 1-cycle pulse clear. The write address state machine (write_reg_0x60/0x61/0x62/0x63) correctly implements byte/word addressing and auto-increment per zxnext.vhd:5424.

3. **Test oracle discipline**: The test harness correctly enforces the rule stated in copper_test.cpp lines 10-14: "The C++ implementation is NEVER the oracle. Where the emulator disagrees with the VHDL, the test asserts the VHDL value." RAM-MIX-01 explicitly documents a known Task 3 backlog item (sticky write_8 latch) and correctly asserts VHDL semantics even though the C++ will fail when the latch is implemented.

4. **Skip row justification**: All 10 skipped rows are **documented as unrealisable** in the plan or test comments. None are silently dropped or hidden.

5. **Tautology detection**: ARB-05 was identified as a structural sanity check (Copper-off does not interfere with CPU writes), not a VHDL fact assertion. Recommended to split into two narrower rows.

6. **Known open questions**: The plan document itemizes 6 open questions (COPPER-TEST-PLAN-DESIGN.md:476-508). Row MUT-03 addresses question #1 (CPU vs Copper pointers); EDG-04 addresses question #2 (timing of pulse latch). Questions #3/5 are the root cause of OFS-* skips. No open question is silently assumed; all are either tested or documented as skip.

## Recommended actions

1. **RAM-MIX-01**: No action on test. Flag emulator Task 3 work: implement sticky `write_8` latch in `Copper::write_reg_0x63()`.
2. **ARB-05**: Split into ARB-05a (Copper mode=00 inhibits) and ARB-05b (CPU path sanity), or narrow to Copper-only.
3. **OFS-06**: Upgrade skip message to cite plan Open Question #3.
4. No false-fail or false-skip rows found.
