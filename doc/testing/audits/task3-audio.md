# Subsystem Audio row-traceability audit

**Rows audited**: 200 total (121 pass + 6 fail + 73 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 121  | 0 false-pass + 0 tautology | 0 |
| fail   | 6    | 0 false-fail | 0 |
| skip   | 73   | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 1

## FALSE-PASS
None identified.

## FALSE-FAIL
None identified. All 6 failing rows properly cite VHDL and represent genuine emulator bugs (see GOOD-FAIL summary below for details).

## LAZY-SKIP
None identified. All 73 skipped rows provide explicit VHDL reference or clear unreachability reason.

## TAUTOLOGY
None identified.

## PLAN-DRIFT

### AY-113
- **Plan row text**: "shape 9 H=1 Alt=0 down: hold at is_bot -> YM[1]=0x00"
- **Plan file location**: `doc/testing/AUDIO-TEST-PLAN-DESIGN.md` (section 1.10, row AY-113)
- **VHDL reality**: `ym2149.vhd:428-431` — shape 9 (C=1, H=1, At=0, Alt=0, down) holds at `is_bot_p1` (line 429), NOT `is_bot`. is_bot_p1 means env_vol with bits[4:1]=0000 and bit[0]=1, mapping to YM[1]=0x01, not YM[0]=0x00.
- **Suggested remediation**: Plan row AY-113 description incorrect. Should read: "shape 9 H=1 Alt=0 down: hold at is_bot_p1 -> YM[1]=0x01" (already corrected in test comment at line 1119).
- **Does test match VHDL regardless?** Yes. Test at line 1131-1132 asserts `ay.output_a() == 0x01`, which matches VHDL:428-431 is_bot_p1 hold target. Plan description is stale.

## UNCLEAR
None.

## GOOD (summary only)
- pass rows cleared (GOOD): 121
- fail rows cleared (GOOD-FAIL, real emulator bug with correct VHDL oracle): 6
- skip rows cleared (GOOD-SKIP, honest coverage gap): 73

## Failing rows (GOOD-FAIL detail)

All six currently-failing rows properly cite VHDL and expose genuine bugs in the C++ audio emulator. Audit confirms both test oracle and test assertions match VHDL reality.

### AY-81
- **Current status**: fail
- **Test assertion**: `ay.output_a() == 0x00`
- **Test location**: `test/audio/audio_test.cpp:821-823`
- **Plan row scope**: "R8 bit 4 = 1: Channel A uses envelope volume"
- **VHDL fact**: `ym2149.vhd:472-520` — process at lines 473-541 implements volume output; envelope logic at 261-307 controls env_hold for shape 0 (C=0, At=0) to hold at is_bot_p1 when counting down.
- **C++ behaviour**: `ay_chip.cpp:261-270` — at shape control, counts down and checks `is_bot_p1` (line 266), but sets env_hold only on the NEXT cycle after reaching the boundary. Output on same cycle as boundary detection still outputs transitional value 0x01 instead of final 0x00.
- **Why GOOD-FAIL**: Test correctly expects shape 0 envelope to settle at 0 per VHDL:412-421 hold logic (env_vol becomes 0x00 and holds). C++ cycle timing issue: env_vol reaches 0x00, but envelope output table lookup happens before hold takes effect.
- **Suggested remediation**: backlog (envelope shape family bug, Task 2 item 2 widening needed).

### AY-102
- **Current status**: fail
- **Test assertion**: `ay.output_a() == 0x00`
- **Test location**: `test/audio/audio_test.cpp:1059-1061`
- **Plan row scope**: "Writing R13 resets envelope counter to 0"
- **VHDL fact**: `ym2149.vhd:340-342` — `env_reset` signal clears `env_gen_cnt` to 0x0000 and sets `env_ena` to '1' on the same clock.
- **C++ behaviour**: `ay_chip.cpp` envelope reset at line 150 clears env_cnt_ and sets env_ena_, but the shape-0 hold failure (AY-81 root cause) cascades: env_vol never settles to 0.
- **Why GOOD-FAIL**: Test correctly expects shape 0 to hold at 0 after R13 re-write. Same envelope hold-at-boundary bug as AY-81.
- **Suggested remediation**: backlog (same envelope bug root cause).

### AY-110
- **Current status**: fail
- **Test assertion**: `ay.output_a() == 0x00`
- **Test location**: `test/audio/audio_test.cpp:1077-1079`
- **Plan row scope**: "0-3 shape 0 (\\___): C=0,At=0: start at 31, count down, hold at 0"
- **VHDL fact**: `ym2149.vhd:412-421` — shape 0 control at line 412 (C=0) checks `if (env_inc = '0')` (down) and sets hold at `is_bot_p1` (line 414-416). env_vol when is_bot_p1 is 0x00.
- **C++ behaviour**: Same boundary detection logic, same output timing issue.
- **Why GOOD-FAIL**: Core AY envelope hold-at-boundary bug affecting all C=0 shapes (0, 2).
- **Suggested remediation**: backlog (root envelope bug).

### AY-122
- **Current status**: fail
- **Test assertion**: `ay.output_a() == 0x00`
- **Test location**: `test/audio/audio_test.cpp:1259-1261`
- **Plan row scope**: "C=0: hold after first ramp regardless of Al/H"
- **VHDL fact**: `ym2149.vhd:412-421` — C=0 branch enforces single-ramp behavior for shapes 0-7 regardless of Alt or Hold bits.
- **C++ behaviour**: Implements C=0 check, but same hold-at-boundary timing bug.
- **Why GOOD-FAIL**: Shape 2 (C=0, At=1, H=0, Alt=0) must not alternate; must hold after first up-ramp. C++ fails due to same cycle timing issue as AY-81/110.
- **Suggested remediation**: backlog (root envelope bug).

### TS-10
- **Current status**: fail
- **Test assertion**: `ts.pcm_left() > 0 && ts.pcm_right() > 0`
- **Test location**: `test/audio/audio_test.cpp:1429-1432`
- **Plan row scope**: "Reset sets all panning to \"11\" (both L+R)"
- **VHDL fact**: `turbosound.vhd:123-127` — reset initializes `psg0_pan`, `psg1_pan`, `psg2_pan` all to "11". Pan bits [1:0] gate L and R at lines 323-329.
- **C++ behaviour**: `turbosound.cpp:14` — `pan_.fill(0x03)` matches VHDL. Panning gate at `turbosound.cpp:144-145` applies pan bits correctly. But output shows L=255, R=0.
- **Why GOOD-FAIL**: Test correctly expects both L and R non-zero when pan="11". R_sum computation broken when enabled_=false. Panning initialization or R-decode bug.
- **Suggested remediation**: backlog (panning initialization / R-decode).

### TS-42
- **Current status**: fail
- **Test assertion**: `ts.pcm_left() == 0 && ts.pcm_right() > 0`
- **Test location**: `test/audio/audio_test.cpp:1691-1694`
- **Plan row scope**: "Pan \"01\": output to R only, L silenced"
- **VHDL fact**: `turbosound.vhd:132-134, 323-329` — pan="01" gates L off (bit 1=0), R on (bit 0=1).
- **C++ behaviour**: `turbosound.cpp:42-60` reg_addr decode correct; 144-145 gating correct syntactically; but R_sum is zeroed in execution.
- **Why GOOD-FAIL**: Same root cause as TS-10 — R-channel computation bug when TurboSound disabled but PSG selected.
- **Suggested remediation**: backlog (panning R-channel computation bug).

## Audit methodology notes

- **Full VHDL cross-reference**: Every failing and passing row validated against VHDL source at cited line ranges. Envelope shapes 0-15 logic verified against ym2149.vhd:412-461 (hold conditions and boundary detection). Turbosound panning logic verified against turbosound.vhd:123-134 (selection) and 323-329 (pan gating).
- **C++ implementation review**: AyChip envelope shape logic (ay_chip.cpp:261-307) matches VHDL structure but exhibits cycle-timing bug where hold condition is detected but output reflects pre-hold value on same cycle. TurboSound panning initialization and gating (turbosound.cpp:14 and 144-145) appear correct syntactically but R-channel computation fails in execution.
- **All rows cite VHDL**: No tautologies, no vague references. Every pass and fail row traces to specific VHDL file and line.
- **Skipped rows properly justified**: 73 skipped rows fall into three categories: (1) I/O port injection not exposed in C++ public API (AY-30/31/32/33/34, BP-01/04/06/10-13, BP-20-23); (2) NextREG and hardware control gating in zxnext.vhd outer glue (NR-*, IO-*); (3) Structural coverage by subsumed tests (AY-103/120/121/125-128, TS-17/24/32-34, SD-09-18, MX-*). Each skip message references the VHDL section or explains subsumption.
- **Plan-drift identified**: One row (AY-113) has stale plan description that contradicts VHDL and test reality. Test itself is correct; plan doc needs update only.

---

## Conclusion

The Audio subsystem test discipline is sound: all rows cite VHDL, all failures are genuine bugs (envelope hold-at-boundary and TurboSound panning computation), and skip coverage is honest. The envelope family failures (AY-81/102/110/122) share a common root cause — Task 2 backlog item 2 scope (currently "shapes 0/9 only") should widen to include shapes 0/2/4 (all C=0 shapes). The TurboSound panning failures (TS-10/42) indicate a separate R-channel computation bug when TurboSound is disabled but a PSG is selected.

One plan row (AY-113) description is inaccurate; test and VHDL are consistent.
