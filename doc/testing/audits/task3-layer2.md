# Subsystem Layer 2 row-traceability audit

**Rows audited**: 141 total (89 unit-pass + 6 unit-fail + 2 skip + 44 deferred)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 89   | 0 false-pass + 0 tautology | 0 |
| fail   | 6    | 0 false-fail | 0 |
| skip   | 2    | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

**Summary narrative**:
- All 89 passing rows cite specific VHDL lines and assert the correct behaviour.
- All 6 failing rows (G7-01, G7-02, G7-03, G7-05a, G7-05b, G7-05c) fail due to a **single, known emulator bug**: `src/video/layer2.cpp:52-61` omits the VHDL `+1` bank transform at `layer2.vhd:172`. The test code correctly asserts the VHDL oracle; this is a GOOD-FAIL state (legitimate emulator bug, correctly specified test). Per the plan header (line 39-42), these failures are intentional.
- Both skipped rows cite specific VHDL lines and explain their unreachability honestly.
- 44 rows deferred to integration tier are tracked but not counted in pass/fail (they depend on NextREG dispatch, port 0x123B, compositor, or cycle-accurate timing).

## FALSE-PASS
None.

## FALSE-FAIL
None.

## LAZY-SKIP
None.

## TAUTOLOGY
None.

## PLAN-DRIFT
None.

## UNCLEAR
None.

## GOOD (summary only, no per-row detail)

- **pass rows cleared (GOOD)**: 89
  - G1: 6/6 (reset defaults)
  - G2: 15/15 (resolution modes, address generation)
  - G3: 18/18 (scrolling, wrap branches)
  - G4: 31/31 (clip window, auto-index, inclusivity)
  - G5: 10/10 (palette offset, 4-bit mode, palette select)
  - G6: 7/7 (transparency, RGB comparison, clip gate, disable)
  - G9: 2/2 (negative/boundary cases)

- **fail rows cleared (GOOD-FAIL, real emulator bug with correct VHDL oracle)**: 6
  - G7-01: bank 8 reads from SRAM page 24 (VHDL `+1` transform) — fails because C++ omits `+1`.
  - G7-02: bank 0x18 reads from SRAM page 40 (VHDL `+1` transform) — fails because C++ omits `+1`.
  - G7-03: bank 0x68 reads from SRAM page 120 (VHDL `+1` transform) — fails because C++ omits `+1`.
  - G7-05a, G7-05b, G7-05c: rows 0..191 source from consecutive 16K pages (VHDL bank addr decomposition) — fail because C++ omits `+1` bank transform.
  - All 6 cite `layer2.vhd:172-175` correctly.
  - Task 3 backlog item: fix `src/video/layer2.cpp:52-61` to implement `layer2_bank_eff = ((0 & bank(6:4)) + 1) & bank(3:0)`.

- **skip rows cleared (GOOD-SKIP, honest coverage gap)**: 2
  - G9-04: "covered structurally by G3-12 narrow-scroll path (layer2.vhd:148)" — the wide-scroll branch is observationally equivalent to narrow-scroll under the test harness; cites VHDL line 148 (hc_eff lookahead).
  - G9-06: "hc_eff=hc+1 is a VHDL internal signal; not observable via Layer2 API (layer2.vhd:148)" — documents a VHDL computation that collapses into the address formula and cannot be separately stimulated. Cites line 148.

## Audit methodology notes

1. **Row identification**: Every row in the test code is tied to a plan ID via the first argument to `check("<ID>", ...)` or `skip("<ID>", ...)`. The plan document lists all 94 tested rows plus a deferred set.

2. **VHDL citation discipline**:
   - Passing rows: Each assertion is justified by at least one inline comment citing `layer2.vhd:<line>` or `zxnext.vhd:<line>`. Examples: G1-12b cites `layer2.vhd:175` (layer2_en gate); G2-01 cites `layer2.vhd:160` (address formula); G3-03 cites `layer2.vhd:156-158` (Y scroll wrap condition).
   - Failing rows: All 6 failures cite `layer2.vhd:172-175` (bank transform + enable gate). The test setup in `vhdl_ram_addr()` and `cpp_ram_addr()` (lines 101-124) **documents the disagreement explicitly**: the C++ function omits the `+1` lift. This is a deliberate VHDL-vs-C++ mismatch probe.
   - Skipped rows: G9-04 and G9-06 both cite `layer2.vhd:148` (the hc_eff lookahead) and explain why the behaviour is unobservable from the Layer2 class boundary.

3. **Test harness transparency**:
   - The test code is explicit about the gap: lines 23-30 in `layer2_test.cpp` note "VHDL DISAGREEMENT flagged for Task 3 backlog" and state "Group G7 tests below assert the VHDL identity and will FAIL until the emulator is fixed; that is intentional."
   - Tests that can run under either C++ or VHDL address (`write_both()`, line 131-134) still allow the emulator to pass even if the bank transform is buggy — this confirms the test setup is sound.

4. **False-pass / false-fail / lazy-skip pattern detection**:
   - **No false-pass rows found**: Every passing row's assertion encodes genuine VHDL behaviour; none are vacuously true.
   - **No false-fail rows found**: The 6 failing rows all fail for the same, documented C++ bug. The VHDL oracle they assert is correct.
   - **No lazy-skip rows found**: Both skips explain the unreachability with specific VHDL line citations and are honest gaps (not implementation shortfalls posing as test limitations).

5. **Deferred rows** (44 total, not counted in unit pass/fail):
   - G1-03..G1-08, G1-10..G1-11: NextREG read-back through the NR dispatch layer.
   - G2-04, G2-07, G2-11, G2-12: Cycle-accurate timing and lookahead probes.
   - G3-09, G3-11: Complex scroll arithmetic and 4-bit byte-lane logic.
   - G4-01a..G4-04: Auto-index and NR 0x1C reset (depend on NR dispatch).
   - G5-09: Palette select isolation (compositor responsibility).
   - G6-07..G6-09: Fallback colour, priority gate (compositor+palette).
   - G7-04, G7-06..G7-16: Port 0x123B bit mapping, SRAM out-of-range guard, 5-page wide fill, read-back formatting.
   - G8-01..G8-05: Layer priority (compositor test plan).
   - G9-01, G9-02: NR 0x69 disable path, port 0x123B cold-reset read.
   - These are tracked in the result list but deliberately **not counted in g_pass/g_fail** (see line 1206: "not counted as pass/fail so g_pass/g_fail reflect the unit-testable subset only").

6. **Plan-drift check**: The plan document accurately describes VHDL behaviour. No discrepancies found between the prose description of each row and the actual VHDL citations. The retraction notice (lines 3-42) honestly documents the previous plan's failures and explains the rewrite.

## Audit findings: Zero issues

All 95 unit-testable rows are correctly specified against VHDL. The 6 failures are a known, deliberately-asserted emulator bug. The 2 skips are honest coverage gaps with clear justification. No tautologies, no false-pass/false-fail patterns, no lazy-skip anti-patterns detected.
