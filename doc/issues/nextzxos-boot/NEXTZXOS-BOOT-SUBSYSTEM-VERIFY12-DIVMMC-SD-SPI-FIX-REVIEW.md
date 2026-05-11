# Pass-12 Fix-of-Reviewer Review — DivMMC + SD + SPI Subsystem

**Branch**: `task2/verify12-divmmc-sd-spi-fix-reviewer`
**Worktree**: `.claude/worktrees/task2-verify12-divmmc-sd-spi-fix-reviewer`
**Forked off**: `task2/verify12-divmmc-sd-spi` at HEAD `4debd01`
**Audit head**: `fe209bb`
**Reviewer head (APPROVE-WITH-NITS)**: `c663b3d`
**Reviewer-promoted fixes commit**: `ce6a6ab`
**Fix-of-reviewer commit (V12-DIVMMC-01-NIT)**: `93af708`
**Fix-of-reviewer doc**: `4debd01`
**Date**: 2026-05-10

## Verdict

**APPROVE** — V12-DIVMMC-01-NIT fix is VHDL-faithful and discriminative;
the four pre-existing test rows that were realigned (SX-03, SX-04, ML-05,
MX-04) were genuinely enshrining the buggy member-init (their original
introduction in commit `cdea45b` cited `0xFF` reset claims grounded in
the synchronous-reset clauses at `spi_master.vhd:151-152` and
`spi_master.vhd:162-166` — but those clauses never fire because
`zxnext.vhd:3285` hardwires `i_reset => '0'`, so the actual VHDL first-
boot behaviour is governed by the signal-declaration init at
`spi_master.vhd:74` (`miso_dat ... := (others => '0')`) = 0x00). The
reviewer-promoted V12-DIVMMC-03/04/06 fixes are confirmed correct and
discriminative. The class-(d) claims for V12-DIVMMC-05/07/08 are
justified per VHDL evidence.

## Methodology

Read the audit report (`fe209bb`), prior reviewer's review report
(`c663b3d`), and the reviewer-promoted-fix + fix-of-reviewer commits.
VHDL/spec verified directly. For each fix, ran the
revert→FAIL→restore→PASS protocol. Did NOT consult any prior pass
reports unrelated to divmmc.

## V12-DIVMMC-01-NIT verification (PRIMARY)

### VHDL anchor — confirmed

`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/serial/spi_master.vhd:74`:

```vhdl
signal miso_dat : std_logic_vector(7 downto 0) := (others => '0');
```

i.e. signal-declaration initial value = **0x00** (FPGA-bitstream-load
default). Synchronous-reset clause at `spi_master.vhd:159-168`
(`miso_dat <= (others => '1')` on `i_reset='1'`) NEVER fires because
`zxnext.vhd:3285` hardwires `i_reset => '0'` ("hard reset done through
core load"). VHDL-correct first-boot = 0x00.

### Member-init fix — verified

`src/peripheral/spi.h:110-125` post-fix:

```cpp
uint8_t rx_data_ = 0x00;     // last byte received from device (VHDL miso_dat init = 0x00)
```

with a 15-line explanatory comment (`spi.h:110-124`) citing both
`spi_master.vhd:74` (signal-init) and `zxnext.vhd:3285` (i_reset
hardwired). Pre-fix was `0xFF`. Diff is single-byte member-init plus
documentation. Minimal, correct.

### Discriminative test SS-17 — confirmed

`test/divmmc/divmmc_test.cpp` `group_ss` SS-17: instantiates a fresh
`SpiMaster` with NO attached device and NO prior `write_data()`, reads
port 0xEB once, asserts result == 0x00. Run protocol:

```
$ git checkout 93af708~1 -- src/peripheral/spi.h    # revert
$ cmake --build build -j$(nproc) --target divmmc_test
$ ./build/test/divmmc_test 2>&1 | grep -E "FAIL SS-17"
  FAIL SS-17: ... [first_read=ff exp=00]
$ git checkout 93af708 -- src/peripheral/spi.h      # restore
$ cmake --build build -j$(nproc) --target divmmc_test
$ ./build/test/divmmc_test 2>&1 | grep -E "Total:"
Total:  136  Passed:  136  Failed:    0  Skipped:    0
```

Discriminative confirmed.

### Pre-existing test row realignments — verified genuine

The four pre-existing rows updated by the fix were confirmed to have
been written against the wrong "miso_dat reset = 0xFF" claim. Their
original introduction in commit `cdea45b` (Phase-2 rewrite of
divmmc_test.cpp) read, for example:

- **SX-03**: `first == 0xFF, "got=%02x exp=FF (pipeline delay not modelled)"`,
  citing `VHDL spi_master.vhd:162-166`.
- **SX-04**: `v == 0xFF, "First read after reset (no device) returns 0xFF (VHDL spi_master.vhd:162)"`.
- **ML-05**: `v == 0xFF, "First read after reset reflects ishift_r reset to 0xFF (VHDL spi_master.vhd:151-152)"`.
- **MX-04**: read once then asserted `v == 0xFF` via the C++ short-
  circuit (no-active-device branch in `read_data()` returns the prior
  `rx_data_` and refreshes to 0xFF).

In every case the cited VHDL line is a synchronous-reset clause that
never fires — the original test author trusted the reset clause at face
value, missing the `i_reset => '0'` hardwiring at `zxnext.vhd:3285`.
The pre-fix tests all passed because the C++ member-init was also 0xFF
— a coincidence, not a deliberate VHDL-grounded reading. The fix
realigns them to the actual VHDL signal-init at `spi_master.vhd:74`,
preserving discriminativity:

| Row | Pre-revert | Post-fix |
|-----|------------|----------|
| SS-17 | FAIL (0xFF) | PASS (0x00) |
| SX-03 | FAIL (0xFF) | PASS (0x00) |
| SX-04 | FAIL (0xFF) | PASS (0x00) |
| ML-05 | FAIL (0xFF) | PASS (0x00) |

(Pre-revert run produced exactly `Total: 136 Passed: 132 Failed: 4`,
matching the four rows above.)

MX-04 was not in the FAIL list because the fix added a `(void)m.read_data();`
prime call before the assertion, exercising the proper VHDL pipeline
(state_last_d latch from `zxnext.vhd:3280`) instead of the C++ short-
circuit. Post-prime, the no-device case latches 0xFF into miso_dat
through the proper path, so the `v == 0xFF` assertion still holds —
this realignment is VHDL-faithful, not coincidence.

## V12-DIVMMC-03 reviewer-promoted fix — verified

VHDL/spec anchor: SD Physical Layer Simplified Spec § 7.3.2.6 — R7
4-byte register bits 31:28 = command version (`0001` for v1). Maps to
byte 0 = 0x10. Pre-fix used 0x00 (reserved/illegal).

Fix at `src/peripheral/sd_card.cpp` (cmd8_send_if_cond):

```cpp
resp_buf_ = { 0xFF, 0x01, 0x10, 0x00, 0x01, check };  // NCR + R1 + R7 (cmd ver=1)
```

SD-22 disc test: revert→FAIL→restore→PASS confirmed:

```
$ git checkout ce6a6ab~1 -- src/peripheral/sd_card.cpp src/peripheral/sd_card.h
$ ./build/test/sdcard_test 2>&1 | grep -E "FAIL SD-22"
  FAIL SD-22: ... [r1=1 b0=0 b1=0 b2=1 b3=170]
$ git checkout 4debd01 -- src/peripheral/sd_card.cpp src/peripheral/sd_card.h
$ ./build/test/sdcard_test 2>&1 | grep -E "Total:"
Total:   25  Passed:   25  Failed:    0  Skipped:    0
```

## V12-DIVMMC-04 reviewer-promoted fix — verified

VHDL/spec anchor: SD Physical Layer Simplified Spec § 7.3.2.1 (Table
7-9) — R1 bit 6 = PARAMETER_ERROR for "argument out of allowed range".
CMD17/CMD18 past-EOF must surface R1 bit 6 in addition to the data
error token.

Fix at `src/peripheral/sd_card.cpp` (cmd17_read_single_block,
cmd18_read_multiple_block): `queue_r1(0x00) → queue_r1(0x40)`.

SD-23 disc test: revert→FAIL→restore→PASS confirmed:

```
  FAIL SD-23: ... [r1_cmd17=0 r1_cmd18=0 r1_ok=0]
... after restore: 25/25 PASS
```

In-bounds CMD17 R1=0x00 still asserted (regression guard against
inverted-sense fix). Verified.

## V12-DIVMMC-06 reviewer-promoted fix — verified

VHDL/spec anchor: SD Physical Layer Simplified Spec § 7.3.3.2 — card
waits for 0xFE start-of-block token; pre-token bytes are gap bytes.
Pass-4 fix only handled 0xFF; any other pre-token byte was absorbed as
data_block_[0] (silent payload shift).

Fix at `src/peripheral/sd_card.cpp`:
- new `data_token_received_` flag (declared in `sd_card.h`).
- reset on `reset()`, `deselect()`, `cmd24_write_single_block()`
  dispatch, and new-CMD-during-data branch.
- `RECEIVING_DATA` case now matches on `!data_token_received_ && tx ==
  0xFE` to flag transition; all other `!data_token_received_` bytes
  (incl. 0xFF) are silently skipped.

SD-24 disc test: sends 0x55 stray pre-token, then 0xFE, then 512 bytes,
issues CMD17 readback and asserts byte-wise match. Revert→FAIL→
restore→PASS confirmed:

```
  FAIL SD-24: ... [r1_wr=0 resp=1 match=0]
... after restore: 25/25 PASS
```

## V12-DIVMMC-05/07/08 class-(d) claims — justified

**V12-DIVMMC-05** (SPI 16-cycle FSM not modelled): VHDL
`spi_master.vhd:82-100` is a 5-bit state-counter FSM driven by
`spi_begin = '1' when (state_last = '1' or state_idle = '1') and
(i_spi_rd = '1' or i_spi_wr = '1') else '0';` (line 82). Mid-transfer
rd/wr is suppressed (`spi_begin` only fires when state is last or
idle). JNEXT collapses every `write_data`/`read_data` to a synchronous
call. A cycle-accurate rewrite would touch (a) 50+ existing SPI test
rows, (b) DMA throttling via `o_spi_wait_n` (`spi_master.vhd:177` →
consumed at `zxnext.vhd:3297`), (c) Z80 wait-state injection. Genuine
multi-subsystem refactor. Class-(d) confirmed.

**V12-DIVMMC-07** (registered _q pipeline absent): VHDL
`zxnext.vhd:4114-4135` registers `divmmc_automap_*_q` on
`falling_edge(i_CLK_28)` based on `cpu_m1_n` and `cpu_mreq_n` boundaries
— it samples the combinational `divmmc_automap_*_on` signals at the
MREQ rising edge, then drives the `divmmc_mod` inputs from the
registered values one cycle later (lines 4126-4132 + 4156-4162).
JNEXT's `check_automap` collapses this. The hold/held two-stage latch
in JNEXT (Step 1 / Step 3) absorbs the 1-cycle delay at the byte
boundary. Sub-cycle granularity within an M1 is not exposed by JNEXT's
M1-callback architecture. Class-(d) confirmed.

**V12-DIVMMC-08** (port_e3 write OR-latch and NR 0x09 bit 3 mapram-
clear are mutually-exclusive same-cycle): VHDL `zxnext.vhd:4180-4186`
elsif ladder gives port_e3 priority over NR 0x09 bit-3 clear in the
same VHDL clock — but the Z80 has no atomic OUT-OUT instruction. Two
different-port writes in the same VHDL cycle are impossible for any
realisable Z80/DMA software. VHDL-impossible scenario. Class-(d)
confirmed.

## Tests run (post-fix-of-reviewer, Release build)

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
$ cmake --build build -j$(nproc)
[100%] Linking CXX executable jnext

$ ctest --test-dir build --output-on-failure
38/38 Test #38: contention_tests ... Passed
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/divmmc_test
Total:  136  Passed:  136  Failed:    0  Skipped:    0

$ ./build/test/sdcard_test
Total:   25  Passed:   25  Failed:    0  Skipped:    0
```

Zero FAILs.

## Hunt for issues introduced by the fix-of-reviewer

Cross-checked the fix against VHDL and existing test coverage:

- The MX-04 realignment (added `(void)m.read_data();` prime) is sound:
  with no active device the prior `rx_data_` (now 0x00 by default) is
  returned, AND `rx_data_` is refreshed to 0xFF for the next read per
  the no-device branch in `read_data()` (which mirrors VHDL
  `zxnext.vhd:3280` default-else `spi_miso <= '1'`). The second read
  observes the latched 0xFF. The test thus exercises the proper VHDL
  state_last_d path rather than the C++ short-circuit. ✓
- The fix does not change `SpiMaster::reset()` (V12-DIVMMC-01 already
  removed the `rx_data_ = 0xFF;` clobber). ✓
- The constructor calls `reset()` per Pass-3 history; the member-init
  0x00 carries through correctly (verified by SS-17 fresh-construct
  observation). ✓
- No regression in 132 other divmmc rows or 25 sdcard rows.
- No regression in FUSE Z80 (1356/1356) or ctest (38/38).

No issues.

## Summary

| Item | Class | Status |
|------|-------|--------|
| V12-DIVMMC-01 (audit) | (c) | Verified by prior reviewer; SS-16 disc-passing |
| V12-DIVMMC-02 (audit) | (b) | Verified by prior reviewer; SD-21 disc-passing |
| V12-DIVMMC-03 (reviewer-promoted) | (c) | Verified here; SD-22 disc-passing |
| V12-DIVMMC-04 (reviewer-promoted) | (c) | Verified here; SD-23 disc-passing |
| V12-DIVMMC-06 (reviewer-promoted) | (c) | Verified here; SD-24 disc-passing |
| V12-DIVMMC-01-NIT (fix-of-reviewer) | (c) | Verified here; SS-17 disc-passing; SX-03/SX-04/ML-05/MX-04 realignments confirmed VHDL-faithful (originally enshrined the bug per commit `cdea45b`) |
| V12-DIVMMC-05 | (d) | Class-(d) confirmed (VHDL `spi_master.vhd:82-100` 16-cycle FSM, multi-subsystem rewrite required) |
| V12-DIVMMC-07 | (d) | Class-(d) confirmed (VHDL `zxnext.vhd:4114-4135` falling-edge registered pipeline; sub-cycle granularity not exposed) |
| V12-DIVMMC-08 | (d) | Class-(d) confirmed (VHDL-impossible same-cycle Z80 OUT-OUT) |

**Verdict: APPROVE**. The V12-DIVMMC-01-NIT fix is VHDL-faithful, the
SS-17 disc test is properly discriminative, and the four pre-existing
realigned rows (SX-03, SX-04, ML-05, MX-04) were genuinely enshrining
the buggy `0xFF` member-init (their original introduction in commit
`cdea45b` cited synchronous-reset clauses that never fire — same root-
cause confusion as V12-DIVMMC-01 itself). The reviewer-promoted
V12-DIVMMC-03/04/06 fixes are correct and discriminative. The class-
(d) claims for V12-DIVMMC-05/07/08 are justified per VHDL evidence —
each is a genuine architectural rewrite, not "we don't want to do it".
All test suites pass (Release build). Zero FAILs across ctest 38/38,
FUSE 1356/1356, divmmc 136/136, sdcard 25/25.
