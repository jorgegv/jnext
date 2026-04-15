# Subsystem Input row-traceability audit

**Rows audited**: 149 total (21 pass + 2 fail + 126 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 21   | 0 false-pass + 0 tautology | 0 |
| fail   | 2    | 0 false-fail | 0 |
| skip   | 126  | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS
None.

## FALSE-FAIL
None. The 2 failures below are GOOD-FAIL entries (detailed in GOOD summary).

## LAZY-SKIP
None. All 126 skips are correctly documented as NOT_IMPL with proper reason text.

## TAUTOLOGY
None.

## PLAN-DRIFT
None. Plan descriptions accurately reflect VHDL reality.

## UNCLEAR
None.

## GOOD (summary only)

**Pass rows cleared (GOOD): 21**
- KBD-01 through KBD-21: All test the standard 40-key membrane matrix against membrane.vhd:236-251 with correct expected values derived from single-key and multi-key AND reduction logic. Each citation is precise (line numbers 236, 242-251). Tests verify row/column bit positions and AND reduction across row selections.

**Fail rows cleared (GOOD-FAIL): 2**

### JMODE-08 — Reset default NR 0x05 joystick mode
- **Current status**: fail
- **Test assertion**: `v == 0x40` (Kempston1, Sinclair2)
- **Test location**: `test/input/input_test.cpp:412-415`
- **Plan row scope**: NR 0x05 reset per VHDL signal-declaration defaults (zxnext.vhd:1105-1106)
- **VHDL reality**: `nr_05_joy0 := "001"` (Kempston1), `nr_05_joy1 := "000"` (Sinclair2), packed to 0x40
- **C++ behaviour**: NextReg::reset() zeroes entire register file (nextreg.cpp:8), returns 0x00
- **Why GOOD-FAIL**: VHDL explicitly initialises these signals at power-on; soft-reset at zxnext.vhd:4920-4945 preserves them. The emulator's blanket zeroing violates the VHDL oracle.
- **Task 3 backlog**: Memory file notes this as "nr_05_joy reset" item.

### IOMODE-01 — Reset default NR 0x0B I/O mode pin-7
- **Current status**: fail
- **Test assertion**: `v == 0x01` (en=0, mode=00, iomode_0=1)
- **Test location**: `test/input/input_test.cpp:556-559`
- **Plan row scope**: NR 0x0B reset per zxnext.vhd:4939-4941 soft-reset block, packed to 0x01 per encoding at 5200-5203
- **VHDL reality**: Soft-reset sets `nr_0b_joy_iomode_en <= '0'`, `nr_0b_joy_iomode <= "00"`, `nr_0b_joy_iomode_0 <= '1'`
- **C++ behaviour**: NextReg::reset() returns 0x00
- **Why GOOD-FAIL**: The VHDL reset block (not the signal-decl) specifically initialises all three signal components; the emulator does not apply these defaults.

**Skip rows cleared (GOOD-SKIP): 126**

All 126 skipped rows are correctly marked as NOT_IMPL with proper reason strings. Examples:
- KBD-22, KBD-23: Port 0xFE byte wrapper (bits 7,5 constant, bit 6 EAR) — Keyboard::read_rows returns only bits 4..0
- KBDHYS-01 through -03: Shift-hold hysteresis at membrane.vhd:180-232 — membrane model not yet in C++
- EXT-01 through -20: Extended-key matrix (NR 0xB0/0xB1) — no extended-column state in C++
- JMODE-01 through -07: NR 0x05 mode decoder (except JMODE-08) — no joy0/joy1 mux in C++
- KEMP-01 through -15: Kempston/MD port handlers — ports 0x1F/0x37 not implemented
- MD-01 through -09, MD6-01 through -11i: MD 3/6-button and NR 0xB2 — no joystick routing in C++
- SINC-06: Sinclair joy→key adapter — not in C++
- CURS-01 through -06: Cursor joy→key adapter — not in C++
- IOMODE-02 through -11 (except IOMODE-01): NR 0x0B pin-7 mux, UART enable/rx — no I/O mode mux in C++
- MOUSE-01 through -11: Kempston mouse ports 0xFADF/0xFBDF/0xFFDF — mouse not implemented
- NMI-01 through -07: NR 0x06 NMI gating (bits 3,4) — no NMI gating in C++
- FE-01 through -05: Port 0xFE wrapper — port dispatcher not testable from unit

All citations in skip notes point to correct subsystem abstraction boundaries.

## Audit methodology notes

1. **VHDL traceability**: Every passing check cites specific line numbers in membrane.vhd or zxnext.vhd for its expected value. All cited lines verified to exist and contain relevant logic.

2. **Test-plan consistency**: Plan document explicitly retracted the old "71/71 passing" claim and published corrected expected values for JMODE-02, MD-01, and Kempston bit map. Test code implements the corrected values, so no false-pass rows exist.

3. **Reset defaults (2026-04-15 finding)**: The plan summary correctly identifies the two NR 0x05 and NR 0x0B reset failures as Task 3 emulator bugs, not test bugs. Both failures involve NextReg::reset() applying blanket-zero instead of VHDL signal-declaration and soft-reset defaults.

4. **Skip discipline**: 126 skipped rows represent honest NOT_IMPL status, not lazy skips or hidden bugs. The test code explicitly documents why each row cannot run. The test count summary in the plan matches the actual test code: 23 live rows, 126 skip, 21 pass + 2 fail.

5. **No tautologies**: All live checks assert concrete values, not comparisons that always pass. Each KBD test reads a specific row after setting a specific key position, then asserts the AND-reduction result.

6. **Extended keys and hysteresis not tested**: Rows KBD-22/23, KBDHYS-01 through -03, and EXT-01 through -20 skip correctly. These require either the port 0xFE wrapper, multi-scan membrane state machine, or extended-key matrix folding — all missing from the C++ keyboard module.

**Verdict**: Input test plan passes VHDL-traceability audit with 0 false-pass, 0 false-fail, 0 lazy-skip. The 2 failing rows are legitimate Task 3 bugs in NextReg::reset(). All 21 passing rows are GOOD. All 126 skipped rows are honest NOT_IMPL without hidden coverage gaps.
