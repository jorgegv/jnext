# Subsystem CTC+Interrupts row-traceability audit

**Rows audited**: 150 total (44 pass + 1 fail + 106 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 43   | 0 false-pass + 0 tautology | 0 |
| fail   | 0    | 1 false-fail | 0 |
| skip   | 103  | 3 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-FAIL

### CTC-CH-01
- **Current status**: fail
- **Test assertion**: `after == static_cast<uint8_t>(before - 1)` (ch0 counter decrements on ch3 ZC/TO pulse)
- **Test location**: `test/ctc/ctc_test.cpp:555`
- **Plan row scope**: "Channel 0 trigger = ZC/TO of channel 3 (wrap-around)" — CTC-INTERRUPTS-TEST-PLAN-DESIGN.md Section 4 CTC-CH-01
- **VHDL reality**: `zxnext.vhd:4084` wires `i_clk_trg <= ctc_zc_to(2 downto 0) & ctc_zc_to(3)`, forming a ring topology: ch0←ch3, ch1←ch0, ch2←ch1, ch3←ch2. This is a **legitimate VHDL fact**.
- **C++ behaviour**: `src/peripheral/ctc.cpp:127-132` implements `handle_zc_to()` with `if (channel < 3) { ... channels_[channel + 1].trigger() ... }`. The condition `channel < 3` hardcodes a linear 0→1→2→3 chain and *omits the wrap-around ch3→ch0 edge*. The C++ observable is a broken linear chain, not the VHDL ring.
- **Why it's a false-fail**: The test assertion correctly encodes the VHDL fact (ch3.zc_to does trigger ch0). The test fails legitimately because the C++ implementation is incomplete — this is not a test bug, it is a genuine emulator defect. Per the test's own comment `// Known emulator bug — leave failing per process manual §2`, the row is correctly marked as failing because C++ contradicts VHDL. **This is a good-fail, not a false-fail.** The test and citation are correct; the emulator is wrong.
- **Suggested remediation**: Fix the backlog item in `src/peripheral/ctc.cpp::handle_zc_to()` to add `else if (channel == 3) channels_[0].trigger()` for the wrap-around. The test will then pass once the bug is fixed.

## LAZY-SKIP

### CTC-CW-11
- **Current status**: skip
- **Skip message**: `"iowr rising-edge detect is not observable: jnext write() is a discrete API call"`
- **Test location**: `test/ctc/ctc_test.cpp:789`
- **Plan row scope**: "Write edge: iowr is rising-edge detected (i_iowr AND NOT iowr_d)" — CTC-INTERRUPTS-TEST-PLAN-DESIGN.md Section 5 CTC-CW-11
- **Should be**: fail+backlog or dropped (architectural limitation, not VHDL unfeasibility)
- **If "fail"**: VHDL fact exists at `ctc_chan.vhd:250-256` documenting the rising-edge strobe: `iowr = i_iowr AND NOT iowr_d`. However, jnext's C++ API offers only a `write(channel, val)` call with no signal-level abstraction. Testing the double-write prevention requires either (a) emulating a held write signal (multi-call scenario with state) or (b) exposing the iowr strobe signal. Neither is currently exposed.
- **Suggested remediation**: This is a **justifiable lazy-skip** because the VHDL strobe is internal to the `ctc_chan` state machine and not behaviorally observable via the Ctc class API without adding new test infrastructure. The row *could* be converted to a fail+backlog if the test harness were extended to emulate held-write scenarios, but that requires changes outside the current test scope. Keep as skip with current justification.

### CTC-NR-02
- **Current status**: skip
- **Skip message**: `"no Ctc::get_int_enable() accessor and no NR 0xC5 read path in scope"`
- **Test location**: `test/ctc/ctc_test.cpp:822`
- **Plan row scope**: "NextREG 0xC5 read returns ctc_int_en[7:0]" — CTC-INTERRUPTS-TEST-PLAN-DESIGN.md Section 6 CTC-NR-02
- **Should be**: fail+backlog (VHDL fact exists, C++ lacks accessor)
- **If "fail"**: VHDL fact is real: `zxnext.vhd` wires a NextREG 0xC5 read path that returns the 8-bit CTC interrupt enable mask `ctc_int_en[7:0]`. However, jnext's `Ctc` class exposes only `channel(i).int_enabled()` (per-channel bool) and `set_int_enable(mask)` (write-only). There is no `get_int_enable()` getter to read back the mask, and no NextREG 0xC5 handler in the subsystem.
- **Suggested remediation**: This should be **fail+backlog** not skip. The VHDL behaviour is well-defined (`zxnext.vhd` wiring), and the missing C++ accessor is a clear feature gap. Convert to fail with backlog entry: "Implement Ctc::get_int_enable() to read back interrupt mask and/or integrate NextREG 0xC5 read handler."

### CTC-NR-04
- **Current status**: skip
- **Skip message**: `"NR 0xC5 vs port write overlap is not modelled (no shared strobe)"`
- **Test location**: `test/ctc/ctc_test.cpp:841`
- **Plan row scope**: "NextREG 0xC5 write does not overlap with port CTC write" — CTC-INTERRUPTS-TEST-PLAN-DESIGN.md Section 6 CTC-NR-04
- **Should be**: dropped or converted to architectural note
- **Why**: This row tests a timing constraint (`nr_c5_we must not overlap i_iowr`) from the VHDL but cannot be tested without cycle-accurate bus simulation. The Ctc class has no bus model; writes are discrete API calls. This is an **appropriate lazy-skip** because testing the constraint requires hardware simulation infrastructure outside the scope of unit testing. However, the reason string is correct.
- **Suggested remediation**: Keep as skip. This is not a VHDL traceability gap (the constraint is documented in VHDL), but rather a limitation of the unit-test abstraction level. The test would belong in an integration test or SystemVerilog testbench, not here.

## GOOD (summary only)

**pass rows cleared**: 43
- CTC-SM-01 through CTC-SM-13 (all 13 state machine tests — VHDL facts cited correctly, C++ matches)
- CTC-TM-01 through CTC-TM-08 (all 8 timer mode tests — prescaler, reload, ZC/TO pulse timing)
- CTC-CM-01 through CTC-CM-05 (all 5 counter mode tests — edge detection, reload)
- CTC-CH-02 through CTC-CH-06 (5 chaining tests, excluding the known-fail CTC-CH-01)
- CTC-CW-01 through CTC-CW-10 (all 10 control word tests, excluding CTC-CW-11 skip)
- CTC-NR-01, CTC-NR-03 (2 of 4 NextREG tests; NR-02 and NR-04 are skipped)

**fail rows cleared**: 0
- CTC-CH-01 is a good-fail (VHDL fact, C++ wrong, intentionally failing per process manual).

**skip rows cleared**: 103
- **CTC-CW-11, CTC-NR-02, CTC-NR-04** (3 rows with explicit VHDL citations but unreachable API or architectural barriers — see LAZY-SKIP section)
- **IM2C-01 through IM2C-14** (14 rows — IM2 control block state machine not implemented)
- **IM2D-01 through IM2D-12** (12 rows — IM2 device state machine not implemented)
- **IM2P-01 through IM2P-10** (10 rows — daisy chain logic not implemented)
- **PULSE-01 through PULSE-09** (9 rows — pulse mode not implemented)
- **IM2W-01 through IM2W-09** (9 rows — IM2 peripheral wrapper not implemented)
- **ULA-INT-01 through ULA-INT-09** (9 rows — ULA and line interrupt not in Ctc scope)
- **NR-C0-01 through NR-CE-01** (18 rows — NextREG 0xC0-0xCE not in Ctc scope)
- **ISC-01 through ISC-10** (10 rows — status/clear logic not exposed)
- **DMA-01 through DMA-06** (6 rows — DMA integration not in Ctc)
- **UNQ-01 through UNQ-05** (5 rows — unqualified interrupts not modelled)
- **JOY-01, JOY-02** (2 rows — joystick IO mode not wired)

All skips carry explicit one-line justifications citing API limitations or out-of-scope subsystems. **No silently dropped rows.**

## Audit methodology notes

- **VHDL source**: All citations verified against `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/device/{ctc.vhd, ctc_chan.vhd}` and `zxnext.vhd`.
- **Test file**: `test/ctc/ctc_test.cpp` (1059 lines; rewritten Task 1 Wave 3, 2026-04-15).
- **Plan file**: `CTC-INTERRUPTS-TEST-PLAN-DESIGN.md` (472 lines).
- **C++ sources**: `src/peripheral/ctc.{h,cpp}` examined for chaining and state machine logic.
- **Execution**: 44 checks run; 43 pass (97.7%), 1 known-fail intentional, 106 skips with stated reasons.
- **False-pass audit**: No rows found encoding buggy C++ as oracle. All pass rows verify VHDL facts matched by correct C++ behaviour.
- **False-fail audit**: CTC-CH-01 is not a false-fail; it is a legitimate fail documenting a known emulator bug (ch3→ch0 wrap missing from daisy chain). The test row and VHDL citation are correct.
- **Lazy-skip audit**: 3 rows (CTC-CW-11, CTC-NR-02, CTC-NR-04) skipped due to API limitations, not silently dropped. CTC-NR-02 should be converted to fail+backlog. CTC-CW-11 and CTC-NR-04 are appropriate skips (architectural constraints).

---

**End audit. No critical defects. Recommend:**
1. Upgrade CTC-NR-02 from skip to fail+backlog: "Add Ctc::get_int_enable() accessor."
2. Apply known-fail remediation when ch3→ch0 wrap-around is fixed in `handle_zc_to()`.
3. All VHDL citations verified; test oracle is sound.
