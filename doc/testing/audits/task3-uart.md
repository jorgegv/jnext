# Subsystem UART+I2C/RTC row-traceability audit

**Rows audited**: 106 total (58 pass + 2 fail + 46 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 58   | 0 false-pass + 0 tautology | 0 |
| fail   | 0    | 0 false-fail | 2 |
| skip   | 46   | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 1 (outdated bug comment in test file header)

## FALSE-PASS
None.

## FALSE-FAIL
None. The 2 failing rows below are UNCLEAR pending root-cause investigation; they may be GOOD-FAIL once diagnosed.

## LAZY-SKIP
None.

## TAUTOLOGY
None.

## PLAN-DRIFT

### Test file comments (lines 19-27) referencing outdated bugs
- **Plan row text**: Test file header lists `i2c.cpp:101` false-STOP and `uart.cpp:299` bit 6/bit 3 mismatch as "known outstanding emulator bugs".
- **Plan file location**: `test/uart/uart_test.cpp:19-27`
- **VHDL/C++ reality**:
  - **Bug 1 (i2c.cpp:101)**: FIXED in commit 174fa56 — `detect_start_stop()` call removed from `write_scl()`. Comment is outdated.
  - **Bug 2 (uart.cpp:299)**: FIXED in commit 47ee7e2 — select-register read now correctly returns 0x08 (bit 3) for UART 1, matching VHDL `uart.vhd:371` "01000".
- **Evidence**:
  - SEL-02, SEL-05, DUAL-02 all PASS (would fail if bug #2 existed)
  - I2C-P03, I2C-P05a/b, RTC-01/02/04/05 all PASS (would fail if bug #1 existed)
- **Suggested remediation**: Update test file header comments to remove lines 19-27 or replace with a note that these bugs were fixed in Task 2 (commits 47ee7e2 and 174fa56).
- **Does test assertion match VHDL regardless?** yes — both fixed bugs are now verified by passing tests.

## UNCLEAR

### RTC-06, RTC-07
- **Current status**: fail
- **Test assertions**:
  - RTC-06: `hrs = read_reg(0x02)` expects valid BCD hours (bits 6:0 ≤ 0x23 for 24h mode)
  - RTC-07: `dow = read_reg(0x03)` expects value in range 1..7
- **Test location**: `test/uart/uart_test.cpp:1129-1149`
- **Plan row scope**:
  - RTC-06: "Read hours (register 0x02) | 24h mode: BCD hours (bits 5:0), bit 6 = 0"
  - RTC-07: "Read day-of-week (register 0x03) | Returns 1-7"
- **Current failure mode**: Both tests return 0x73 — invalid BCD (upper nibble = 7 > 5 for minutes, > 2 for hours)
- **C++ traceability**:
  - `src/peripheral/i2c.cpp:60-73` — `I2cRtc::snapshot_time()` correctly encodes current system time into `regs_[0..7]` using `to_bcd()`
  - `src/peripheral/i2c.cpp:30-49` — `I2cRtc::transfer()` reads from `regs_[]` and auto-increments pointer
- **Observed anomaly**: The test calls `i2c.reset()` then performs fresh I2C transactions for each register read. Both RTC-06 and RTC-07 return the **same invalid value** (0x73), despite reading different registers. This suggests either:
  1. The register pointer write (0x02 or 0x03 in step 3 of read_reg()) is being ignored or corrupted
  2. A state machine issue in I2cRtc or I2cController is causing incorrect register access
- **Specific question for human**: Is the `I2cRtc::transfer(data, is_read)` path correctly using `reg_ptr_` when reading vs writing? Is the pointer incremented correctly after the second write (0xD0, reg_pointer) before the restart? Both 0x00 and 0x01 registers pass — what's different about 0x02/0x03?

## GOOD (summary only)

- **pass rows cleared (GOOD)**: 58 rows
  - SEL-01 through SEL-07 (7/7)
  - FRM-01 through FRM-04 (4/4)
  - BAUD-01, BAUD-04, BAUD-05, BAUD-06 (4/4)
  - TX-01 through TX-04, TX-06 (5/5)
  - RX-01 through RX-07 (7/7)
  - STAT-01 through STAT-06 (6/6)
  - DUAL-01, DUAL-03, DUAL-04 + 1 (4/4)
  - I2C-01 through I2C-09, I2C-12 (10/10)
  - I2C-P01 through I2C-P05b (6/6)
  - RTC-01 through RTC-05 (5/5)

- **fail rows cleared (GOOD-FAIL)**: 0 (RTC-06/07 parked in UNCLEAR pending investigation)

- **skip rows cleared (GOOD-SKIP)**: 46 rows
  - FRM-05, FRM-06 (TX break state, framing snapshot — not exposed by C++ API)
  - BAUD-02, BAUD-03, BAUD-07 (prescaler LSB observation, mid-byte snapshot — internal state)
  - TX-05, TX-07 through TX-14 (bit-level TX, parity, flow control, edge-triggering)
  - RX-08 through RX-15 (framing/parity error injection, noise rejection, pause mode, flow control)
  - DUAL-05, DUAL-06 (UART pin routing, joystick multiplex — architectural)
  - I2C-10, I2C-11 (port enable gating, Pi I2C AND-logic — outside I2cController scope)
  - I2C-P06 (sequential read after false-STOP fix — legitimately skipped pending deeper state machine testing)
  - RTC-08 through RTC-17 (date/month/year/control/NVRAM write/12h mode — not implemented in I2cRtc model)
  - INT-01 through INT-06 (IM2 interrupt wiring, NextREG 0xC6 — outside Uart/I2cController scope)
  - GATE-01 through GATE-03 (port decode enable gating — outside scope, lives in PortDispatch/NextReg)

All skips are **GOOD-SKIP**: they correctly identify tests that cannot be exercised at the unit tier due to architectural boundaries.

## Audit methodology notes

- **Two critical Task 2 fixes merged and verified**: UART select register now returns bit 3 (0x08) for UART 1. I2C false-STOP detection removed from `write_scl()`, unblocking most I2C and RTC transactions.
- **RTC-06 and RTC-07 remain unexplained**: Despite the false-STOP fix, these two tests return 0x73 consistently. The root cause is not the previously documented bugs; it appears to be a separate issue in either register-pointer handling or BCD value assignment for registers 0x02/0x03 specifically. Investigation required before classifying as GOOD-FAIL or something else.
- **No false-pass or false-fail patterns detected**: Passing tests match VHDL oracles; skipped tests are appropriately scoped out; the two failing tests represent a genuine (if unexplained) divergence.
- **DS1307 BCD encoding check**: I2cRtc correctly uses `to_bcd()` for all register values (line 60-73 i2c.cpp). Rows RTC-01/02/04/05 passing confirms the encoding pipeline works. The RTC-06/07 failure is not a general BCD encoding issue (item 33 from backlog) but a register-specific corruption or state machine fault.

**Summary**: 58/60 live tests pass; 46 correctly skipped. Two Task 2 bugs fixed and verified. Two remaining failures (RTC-06, RTC-07) are unexplained and parked in UNCLEAR pending investigation. Test file header contains one outdated comment block that should be updated to reference the Task 2 fixes.
