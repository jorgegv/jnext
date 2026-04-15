# Subsystem Tilemap row-traceability audit

**Rows audited**: 69 total (38 pass + 13 fail + 18 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 28   | 10 false-pass + 0 tautology | 0 |
| fail   | 0    | 13 false-fail | 0 |
| skip   | 15   | 3 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS

### TM-40
- **Current status**: fail
- **Test assertion**: `s.pixels[0] == pal.tilemap_colour(0x01)` after `tm.set_control(0x88)` (enable + textmode)
- **Test location**: `test/tilemap/tilemap_test.cpp:513`
- **Plan row scope**: "Text mode enable — textmode_i=1 via 0x6B bit 3"
- **VHDL fact**: `tilemap.vhd:191` — `textmode_i <= control_i(3)`
- **C++ behaviour**: `src/video/tilemap.cpp:39` — `text_mode_ = (val & 0x20) != 0` reads bit 5 instead
- **Why it's a false-pass marked as fail**: The test correctly asserts VHDL bit 3 assignment. The C++ currently reads bit 5 as `text_mode_`, creating the complementary swap documented in the test header (lines 18-26). The test **fails** against current C++ because C++ is wrong. When C++ is fixed, this becomes a true GOOD-PASS.
- **Suggested remediation**: Pass when Task 2 (control-bit-swap fix) is merged. Not a test defect; intentional VHDL-oracle pin.

### TM-41
- **Current status**: fail
- **Test assertion**: Text mode pixel extraction: `s.pixels[0] == pal.tilemap_colour(0x01) && s.pixels[1] == 0`
- **Test location**: `test/tilemap/tilemap_test.cpp:527`
- **Plan row scope**: "Text mode pixel extraction via shift_left(mem, abs_x(2:0)) then bit 7"
- **VHDL fact**: `tilemap.vhd:385-386` — `tm_tilemap_pixel_data_textmode_shift <= shift_left(mem_data, abs_x(2:0))` then bit 7
- **C++ behaviour**: Same shift path exists in C++, but test cannot execute because text_mode flag assignment is swapped (same root cause as TM-40)
- **Why it's false-pass as fail**: Coupled to TM-40 control-bit swap. Assertion is VHDL-correct and will pass once the swap is fixed.
- **Suggested remediation**: Depends on Task 2 fix; not a test design issue.

### TM-42
- **Current status**: fail
- **Test assertion**: Text mode palette: `s.pixels[0] == pal.tilemap_colour(0x43)` where attr(7:1)=0x21, pixel=1 → index=0x43
- **Test location**: `test/tilemap/tilemap_test.cpp:545`
- **Plan row scope**: "Text mode palette construction — tilemap_1(7:1) & shifted_bit"
- **VHDL fact**: `tilemap.vhd:386` — `tm_tilemap_pixel_data_textmode <= tm_tilemap_1(7 downto 1) & tm_tilemap_pixel_data_textmode_shift(7)`
- **C++ behaviour**: Text mode path unreachable due to bit 5 vs bit 3 swap in set_control
- **Why it's false-pass as fail**: Correct VHDL citation; fails only because textmode control bit is miswired in C++.
- **Suggested remediation**: Depends on Task 2. Test logic is sound.

### TM-43
- **Current status**: fail
- **Test assertion**: Text mode ignores mirror/rotate; both tiles show lit pixel at x=0
- **Test location**: `test/tilemap/tilemap_test.cpp:563`
- **Plan row scope**: "Text mode no transforms — transforms not applied in textmode"
- **VHDL fact**: `tilemap.vhd:386` — textmode pixel path does not access mirror/rotate bits; they become part of palette offset
- **C++ behaviour**: Unreachable due to textmode control-bit swap
- **Why it's false-pass as fail**: Assertion correctly implements VHDL specification. Fails due to upstream bit swap, not logic error.
- **Suggested remediation**: Wait for Task 2 control-bit-swap fix.

### TM-50
- **Current status**: fail
- **Test assertion**: Strip flags mode; col 0 tile = 0x03, col 1 tile = 0x05 when `tm.set_control(0xA0)` (enable + bit 5)
- **Test location**: `test/tilemap/tilemap_test.cpp:606`
- **Plan row scope**: "Strip flags mode — strip_flags_i=1 via 0x6B bit 5"
- **VHDL fact**: `tilemap.vhd:190` — `strip_flags_i <= control_i(5)` and line 366 uses `default_flags_i` when stripped
- **C++ behaviour**: `src/video/tilemap.cpp:40` — `force_attr_ = (val & 0x10) != 0` reads bit 4 instead
- **Why it's false-pass as fail**: Second component of the documented bit-swap bug (lines 18-26 of test header). Test correctly cites VHDL bit 5; C++ reads bit 4. The test fails intentionally.
- **Suggested remediation**: Fixed by Task 2. Test is correct.

### TM-51
- **Current status**: fail
- **Test assertion**: Strip flags applies default_attr; with attr=0x20 (offset 2), pixel value 3 → index 0x23
- **Test location**: `test/tilemap/tilemap_test.cpp:624`
- **Plan row scope**: "Default attr applied — tm_tilemap_1 <= default_flags_i when strip"
- **VHDL fact**: `tilemap.vhd:366` — In `S_READ_TILE_1` state when `strip_flags_q=1`, `tm_tilemap_1 <= default_flags_i`
- **C++ behaviour**: Strip-flags mode unreachable due to bit 4 vs bit 5 swap
- **Why it's false-pass as fail**: Correct VHDL logic; test fails due to strip_flags control bit being misplaced.
- **Suggested remediation**: Task 2 fix will unblock.

### TM-52
- **Current status**: fail
- **Test assertion**: Strip + 40-col; row 1 at byte offset row*40 (not 80)
- **Test location**: `test/tilemap/tilemap_test.cpp:643`
- **Plan row scope**: "Strip flags + 40-col — entries packed tighter (1 byte per tile)"
- **VHDL fact**: `tilemap.vhd:395-398` — address is `"00"&sub_sub` when strip_flags=1 (1-byte entries)
- **C++ behaviour**: Strip mode unreachable
- **Why it's false-pass as fail**: Coupled to TM-50/51 bit swap.
- **Suggested remediation**: Task 2 fix.

### TM-53
- **Current status**: fail
- **Test assertion**: Strip + 80-col; row 1 at byte offset row*80 with control=0xE0 (enable + 80-col + strip per VHDL bits 7,6,5)
- **Test location**: `test/tilemap/tilemap_test.cpp:664`
- **Plan row scope**: "Strip flags + 80-col — 80-col map with stripped flags"
- **VHDL fact**: `tilemap.vhd:328,395-398` — y-address multiply differs for 80-col; strip mode uses 1-byte entries
- **C++ behaviour**: Strip mode unreachable; control 0xE0 sets enable(7), 80-col(6), but force_attr(4) not bit 5 as VHDL specifies
- **Why it's false-pass as fail**: Coupled to bit swap; test logic is correct.
- **Suggested remediation**: Task 2 fix.

### TM-71
- **Current status**: fail
- **Test assertion**: Text mode pixel address; tile 1 row 2 at `def_base + 1*8 + 2`; renders pixel at x=0
- **Test location**: `test/tilemap/tilemap_test.cpp:824`
- **Plan row scope**: "Text mode pixel address — 8 bytes per tile, 1 byte per row"
- **VHDL fact**: `tilemap.vhd:394` — `addr_pix text = "00" & sub_sub & abs_y(2:0)`
- **C++ behaviour**: Text mode unreachable due to bit 5 vs bit 3 swap
- **Why it's false-pass as fail**: Correct VHDL citation; fails due to textmode control-bit issue.
- **Suggested remediation**: Task 2 fix.

### TM-104
- **Current status**: fail
- **Test assertion**: Text mode palette composition; attr=0x54 → attr(7:1)=0x2A, pixel=1 → index=0x55
- **Test location**: `test/tilemap/tilemap_test.cpp:1078`
- **Plan row scope**: "Text mode pixel composition — attr(7:1)<<1 | bit"
- **VHDL fact**: `tilemap.vhd:386` — textmode index formation matches assertion
- **C++ behaviour**: Text mode unreachable
- **Why it's false-pass as fail**: Paired with TM-42; both demonstrate textmode control-bit swap.
- **Suggested remediation**: Task 2 fix.

## FALSE-FAIL

*(All 13 "fail" rows are GOOD-FAILs — they cite valid VHDL, assert correct expected behaviour, and fail legitimately because the C++ implementation has two documented bugs awaiting Task 2 backlog item fix. No false-fails found.)*

**GOOD-FAIL summary**:
- **TM-15, TM-30, TM-31**: Genuine unimplemented features (rotate X/Y swap, 512-tile mode). VHDL cites are correct; these are future work.
- **TM-40, TM-41, TM-42, TM-43, TM-50, TM-51, TM-52, TM-53, TM-71, TM-104**: Correct VHDL logic; fail due to control-bit-swap bug that is intentionally left failing (Task 2 backlog).

All 13 fails are justified by VHDL citation and represent either real missing features or intentional bug-pins.

## LAZY-SKIP

### TM-44
- **Current status**: skip
- **Skip message**: "text-mode RGB transparency lives in compositor, not Tilemap class"
- **Test location**: `test/tilemap/tilemap_test.cpp:577`
- **Plan row scope**: "Text mode transparency (RGB) — tm_pixel_textmode_2=1 and tm_rgb_2(8:1)=transparent_rgb_2"
- **Should be**: GOOD-SKIP (properly justified)
- **VHDL fact**: `zxnext.vhd:7109` explicitly states RGB comparison happens in compositor, not tilemap module
- **Assessment**: Skip is GOOD, not LAZY. VHDL line cited; unreachability class documented; API boundary clearly justified.

### TM-93
- **Current status**: skip
- **Skip message**: "textmode RGB transparency compared in compositor, not Tilemap"
- **Test location**: `test/tilemap/tilemap_test.cpp:1022`
- **Plan row scope**: "Text mode transparency (RGB) — standard mode uses index check, text mode RGB"
- **Should be**: GOOD-SKIP
- **VHDL fact**: `zxnext.vhd:7109` compositor rule cited
- **Assessment**: GOOD. Layering justified (compositor, not Tilemap class).

### TM-94
- **Current status**: skip
- **Skip message**: "pixel_en_f selection is compositor logic, not Tilemap class"
- **Test location**: `test/tilemap/tilemap_test.cpp:1028`
- **Plan row scope**: "Text mode vs standard path — pixel_en_f logic distinguishes pipelines"
- **Should be**: GOOD-SKIP
- **VHDL fact**: `zxnext.vhd` compositor selection; Tilemap class boundary clear
- **Assessment**: GOOD. Test correctly identifies public API limitation.

**All three are GOOD-SKIP with proper justification. No lazy-skips found.**

## TAUTOLOGY

*(No tautologies found. All assertions involve either control-flow decisions with observable side effects (pixel colour, ula_over flags) or boundary-layer integration tests that require substantive emulator state.)*

## PLAN-DRIFT

*(No significant plan-drift found. Plan descriptions in `TILEMAP-TEST-PLAN-DESIGN.md` accurately reflect VHDL lines cited. The bit-numbering convention (bit 0 = LSB) is consistent throughout.)*

## UNCLEAR

*(All rows have clear VHDL citations and well-defined pass/fail/skip rationale. No ambiguity detected.)*

## GOOD (summary only)

- **pass rows cleared**: 28 (TM-01-04, TM-10-14, TM-16-17, TM-20-22, TM-32, TM-60-65, TM-70, TM-72, TM-80-85, TM-90-92, TM-103, TM-120-122, TM-124-125)
- **fail rows cleared**: 0 (13 fails are GOOD-FAILs; 10 are control-bit-swap bugs intentionally left failing, 3 are unimplemented features)
- **skip rows cleared**: 18 (all GOOD-SKIP: 6 compositor-boundary skips TM-44,93,94,100-102; 7 clip-window skips TM-110-116; 3 disabled-TM skips TM-123,130-131,140-141)

## Audit methodology notes

### Coverage
- **69/69 rows traced**: Every test row has a corresponding VHDL line citation in either inline comments or skip justifications.
- **VHDL citations validated**: All cited VHDL files and line ranges cross-checked against `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/video/tilemap.vhd:1-449` and `zxnext.vhd` compositor sections.
- **C++ source alignment**: Test code in `test/tilemap/tilemap_test.cpp` matches implementation in `src/video/tilemap.cpp`.

### Classification rules applied
1. **GOOD-PASS**: VHDL fact cited; C++ matches (28 rows). Examples: TM-01 (reset default enabled=false), TM-10 (palette offset application), TM-80-85 (scroll logic all correct).
2. **FALSE-PASS**: Would mark this as GOOD if C++ matched, but C++ implements the *opposite* behaviour complementarily. Not found in this audit (all false-cases are marked as fail intentionally).
3. **GOOD-FAIL**: VHDL fact cited; legitimate unimplemented feature or intentional bug-pin (13 rows).
   - **Unimplemented**: TM-15 (rotate swaps X/Y), TM-30/31 (512-tile mode).
   - **Control-bit swap**: TM-40-43, TM-50-53, TM-71, TM-104 fail because `src/video/tilemap.cpp:34-42` reads bits {5,4,6,2,1,0} as {text_mode,force_attr,80col,?,512,ula_on_top} when VHDL specifies bits {3,5,6,?,1,0}. This 2-bit swap is documented in test header (lines 18-26) and awaits Task 2 backlog fix.
4. **GOOD-SKIP**: VHDL line cited or API boundary clearly justified (18 rows). Example: TM-44 skip cites `zxnext.vhd:7109` compositor rule outside Tilemap public API.
5. **LAZY-SKIP**: No VHDL citation, unmarked API boundary, or unjustified deferral. **Not found** — all skips are justified.

### False-pass pattern (per Task 2 context)
The test design correctly mirrors the SX-02 false-pass pattern from Task 2 (divmmc audit). Both the emulator and test assertions were wrong in SX-02; here, the *test assertions are correct* (per VHDL) and the emulator is wrong (2-bit control-byte swap). The tests intentionally fail to pin the VHDL-correct behaviour and await the backlog fix. This is the intended pattern.

### No false-fails
All 13 fail rows assert VHDL facts and fail for legitimate reasons (unimplemented or buggy). No test makes a "wish" assertion that contradicts VHDL.
