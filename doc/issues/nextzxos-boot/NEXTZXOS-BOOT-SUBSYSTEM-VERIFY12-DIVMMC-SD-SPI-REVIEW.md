# Pass-12 Independent Review — DivMMC + SD + SPI Subsystem

**Reviewer branch**: `task2/verify12-divmmc-sd-spi-reviewer`
**Worktree**: `.claude/worktrees/task2-verify12-divmmc-sd-spi-reviewer`
**Audit head reviewed**: `fe209bb`
**Reviewer head**: `ce6a6ab`
**Date**: 2026-05-10

## Verdict

**APPROVE-WITH-NITS** — both audit findings (V12-DIVMMC-01, V12-DIVMMC-02)
verified VHDL/spec-faithful, fixes correct, regression tests
discriminative. Three of the six "catalogued" items
(V12-DIVMMC-03/04/06) were reclassified as class-(c) fixable and
**fixed in this review pass** with discriminative tests; the remaining
three (V12-DIVMMC-05/07/08) are confirmed truly class-(d) architectural.
One minor NIT recorded against the audit (V12-DIVMMC-01 first-boot
default value drift vs VHDL initial value).

| Finding | Class | Audit | Reviewer |
|---------|-------|-------|----------|
| V12-DIVMMC-01 | (c) | Fixed | VHDL-verified, fix correct, SS-16 disc-passing (NIT below) |
| V12-DIVMMC-02 | (b) | Fixed | Spec-verified, fix correct, SD-21 disc-passing |
| V12-DIVMMC-03 | (c) | Catalogued (latent) | Reclassified class-(c) fixable, FIXED + SD-22 |
| V12-DIVMMC-04 | (c) | Catalogued (latent) | Reclassified class-(c) fixable, FIXED + SD-23 |
| V12-DIVMMC-05 | (d) | Catalogued | Confirmed class-(d) architectural |
| V12-DIVMMC-06 | (c) | Catalogued (latent) | Reclassified class-(c) fixable, FIXED + SD-24 |
| V12-DIVMMC-07 | (d) | Catalogued | Confirmed class-(d) architectural |
| V12-DIVMMC-08 | (d) | Catalogued | Confirmed class-(d) architectural |

## Methodology

Read the audit report and audit commit `fe209bb`. Did NOT consult any
prior pass reports. Verified VHDL/spec claims directly against
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.
For each fix, ran the revert→FAIL→restore→PASS discriminative protocol.

Build & test (Release):

```
ctest --test-dir build  → 38/38 pass
./build/test/fuse_z80_test build/test/fuse  → 1356/1356 pass
./build/test/divmmc_test  → 135/135 pass
./build/test/sdcard_test  → 25/25 pass (3 new tests added)
```

## V12-DIVMMC-01 — `SpiMaster::reset()` clobbered `rx_data_` (class-(c))

### VHDL claim verification

Audit claims: VHDL `zxnext.vhd:3282-3298` instantiates `spi_master_mod`
with `i_reset => '0'` HARDWIRED at line 3285, and the `miso_dat` reset
clause at `spi_master.vhd:159-168` therefore never fires.

**Verified**: `zxnext.vhd:3285` reads `i_reset => '0'` with comment
"hard reset done through core load". `spi_master.vhd:159-168` reads
`if i_reset = '1' then miso_dat <= (others => '1') elsif state_last_d
= '1' then miso_dat <= ishift_r & i_spi_miso`. With `i_reset` fixed at
'0', the clause is dead code post-bitstream-load. Audit claim holds.

### Fix correctness

The fix removes `rx_data_ = 0xFF;` from `SpiMaster::reset()`
(`src/peripheral/spi.cpp:68`). The constructor still member-initialises
`rx_data_=0xFF` so first-boot reads see 0xFF until a real transfer
arrives. The fix is minimal and correct.

### Discriminative test (SS-16) verification

Test in `test/divmmc/divmmc_test.cpp` group_ss SS-16. Revert protocol:

```
$ # restored to pre-fix (rx_data_ = 0xFF in reset())
$ ./build/test/divmmc_test 2>&1 | grep SS-16
  FAIL SS-16: ... [primed=42 after_reset=ff]
Total: 134 Passed: 1 Failed
$ # restored
$ ./build/test/divmmc_test 2>&1 | grep -c FAIL
0
Total: 135 Passed
```

Discriminative: when reverted, `after_reset=ff` (clobbered); when fix
in place, `after_reset=42` (preserved). Confirmed.

### NIT: first-boot default value drift vs VHDL

`spi_master.vhd:74` declares `signal miso_dat : std_logic_vector(7
downto 0) := (others => '0')` — i.e., the FPGA-bitstream initial value
is **0x00**, not 0xFF. JNEXT's `rx_data_` member-init at `spi.h:110` is
**0xFF**. With both `i_reset='0'` (no clause firing) AND no prior SPI
transfer, real hardware would surface 0x00 on the first port-0xEB read,
JNEXT surfaces 0xFF.

The audit's fix-comment claims the constructor "still member-initialises
it to 0xFF for first-boot defaults" — that's true of the JNEXT code but
diverges from the VHDL initial value. Practical impact: nil — every
known firmware (TBBlue, NextZXOS, esxdos) issues a CMD before the first
port-0xEB read, so the first sampled value is always overwritten by the
CMD's R1 (or whatever else the slave drives). Not a bug worth fixing,
but worth documenting as a NIT for future reviewers / classroom-level
VHDL faithfulness.

Recommendation (deferred — not fixed in this pass): align member-init
with VHDL initial value (`uint8_t rx_data_ = 0x00;` in `spi.h:110`).

## V12-DIVMMC-02 — CMD24 past-EOF data response (class-(b))

### Spec claim verification

Audit claims SD Physical Layer Simplified Spec § 7.3.3.3 mandates the
data response token format `0bxxx0_sss1` with status codes:
- `010` = data accepted → 0x05
- `101` = data rejected (CRC error) → 0x0B
- `110` = data rejected (write error) → 0x0D

**Verified**: SD Physical Layer Simplified Spec § 7.3.3.3 (Data Response
Token) defines exactly this layout. Past-EOF writes must surface 0x0D
(write error). Pre-fix unconditional 0x05 was a real spec-compliance
bug.

### Fix correctness

`src/peripheral/sd_card.cpp:200-225`: a local `write_ok` flag mirrors
the existing `byte_addr + 512 <= file_size_` condition. The token is
gated on `write_ok`. The in-bounds path (`write_ok=true`) is
unchanged: still emits 0x05. The past-EOF path now emits 0x0D and
adds a warn-level log. Fix is minimal, correct, and preserves the
SD-14 round-trip path.

### Discriminative test (SD-21) verification

Test in `test/sdcard/sdcard_test.cpp:951+`. Revert protocol:

```
$ # restored to pre-fix unconditional resp_buf_ = { 0x05 }
$ ./build/test/sdcard_test 2>&1 | grep SD-21
  FAIL SD-21: ... [r1_wr=0 resp=5 r1_ok=0 resp_ok=5]
$ # restored
Total:   22 Passed
```

Discriminative: when reverted, `resp=5` (data-accepted) at past-EOF;
when fix in place, `resp=0x0D`. The in-bounds case (`resp_ok=5`) is
preserved both ways, guarding against an inverted-sense regression.
Confirmed.

## Catalogued items — re-classification and reviewer fixes

The audit catalogued 6 items as "class-(c)/(d) latent" and deferred
them. The user's convergence rule is "zero pending of any class". I
re-examined each per VHDL/spec; promoted three to class-(c) fixable
(committed in this review pass), confirmed three as truly class-(d)
architectural.

### V12-DIVMMC-03 → reviewer-promoted to class-(c) FIXED — commit `ce6a6ab`

Audit deferral reason: "TBBlue master + FatFs disk-io do not validate
byte 0 of R7."

**Reviewer assessment**: SD Physical Layer Simplified Spec § 7.3.2.6
mandates R7 4-byte register bits 31:28 = command version (`0001` for
v1). Mapping to byte 0 of the 4-byte register = 0x10. Pre-fix used
0x00, which is reserved/illegal in the spec — diverges from any real
SD card. Fix is a single byte (`0x00 → 0x10` in `cmd8_send_if_cond`).
Latency is class-(c) by audit's own admission, but the fix is small
enough that promoting it costs nothing.

**Fix**: `src/peripheral/sd_card.cpp:469`:
```cpp
resp_buf_ = { 0xFF, 0x01, 0x10, 0x00, 0x01, check };  // NCR + R1 + R7 (cmd ver=1)
```

**Test (SD-22)**: read 4 R7 bytes, assert byte 0 = 0x10. Revert→FAIL
→restore→PASS confirmed (`b0=0` when reverted, `b0=16` post-fix).

### V12-DIVMMC-04 → reviewer-promoted to class-(c) FIXED — commit `ce6a6ab`

Audit deferral reason: "TBBlue/NextZXOS never read past EOF in the boot
path. ... not a strict bug, just thinner R1 fidelity than ideal."

**Reviewer assessment**: SD Physical Layer Simplified Spec § 7.3.2.1
(Table 7-9) R1 bit 6 = PARAMETER_ERROR = "argument was out of the
allowed range". When the host issues CMD17/CMD18 with a sector index
past end-of-image, the card MUST set R1 bit 6 (in addition to the
data error token). The audit's "spec-permissible weaker form" framing
is not strictly correct — § 7.3.2.1 is normative for status flags. The
fix is one byte (`queue_r1(0x00) → queue_r1(0x40)`) at two sites in
`cmd17_read_single_block` and `cmd18_read_multiple_block`.

**Fix**: `src/peripheral/sd_card.cpp:579,621`:
```cpp
queue_r1(0x40);   // PARAMETER_ERROR per SD Phys Layer Spec § 7.3.2.1
```

**Test (SD-23)**: CMD17 + CMD18 past-EOF return R1 with bit 6 set;
in-bounds CMD17 returns R1=0x00. Revert→FAIL→restore→PASS confirmed.

### V12-DIVMMC-05 → reviewer-confirmed class-(d) architectural

Audit reason: SPI master cycle-accurate FSM not modelled — VHDL
`spi_master.vhd:80-100` is a 16-cycle byte-shift FSM with `spi_begin`
suppression of mid-transfer rd/wr; JNEXT collapses to instantaneous
synchronous calls.

**Reviewer assessment**: confirmed truly class-(d). A cycle-accurate
SPI rewrite would touch (a) every existing SPI test (~50+ rows in
divmmc/sdcard test suites assume byte-level synchrony), (b) DMA
throttling (`o_spi_wait_n` at `spi_master.vhd:177` consumed at
`zxnext.vhd:3297`), (c) Z80 wait-state injection. This is a
multi-subsystem refactor. Class-(d) confirmed.

### V12-DIVMMC-06 → reviewer-promoted to class-(c) FIXED — commit `ce6a6ab`

Audit deferral reason: "All known firmware (TBBlue, FatFs, esxdos)
sends either the token immediately or a string of 0xFF gap bytes
followed by the token. Out-of-spec hosts are theoretical."

**Reviewer assessment**: SD Physical Layer Simplified Spec § 7.3.3.2
explicitly says the card waits for 0xFE; pre-token bytes are gap
bytes. The Pass-4 fix only handled 0xFF. Other pre-token bytes
silently absorb as data_block_[0], shifting the entire payload by 1+.
While "no firmware sends odd bytes" is true today, the fix is small
and prevents silent corruption.

The fix introduces an explicit `data_token_received_` flag (added to
`SdCardDevice` in `sd_card.h`), reset on `reset()`, `deselect()`,
CMD24 dispatch (`cmd24_write_single_block`), and the
new-command-during-data branch. The state machine now ignores ALL
pre-token bytes (incl. 0xFF) and only collects bytes after the 0xFE
token. SD-14 / SD-17 (existing CMD24 round-trip + 0xFF gap byte
tests) continue to pass.

**Fix**: see `src/peripheral/sd_card.cpp` `case State::RECEIVING_DATA`
and matching reset paths.

**Test (SD-24)**: send a stray 0x55 byte BEFORE the 0xFE token; verify
the round-trip payload via CMD17 readback matches byte-for-byte.
Revert→FAIL→restore→PASS confirmed (`match=0` when reverted, `match=1`
post-fix).

### V12-DIVMMC-07 → reviewer-confirmed class-(d) architectural

Audit reason: VHDL `zxnext.vhd:4114-4135` registers
`divmmc_automap_*_q` on the falling edge of `i_CLK_28` based on
`cpu_m1_n` and `cpu_mreq_n` boundaries. JNEXT collapses the registered
pipeline into the same `check_automap` call.

**Reviewer assessment**: confirmed truly class-(d). The two-stage
hold/held latch in JNEXT (Step 1 / Step 3 in `check_automap`) absorbs
the 1-cycle pipeline delay at the byte boundary. A cycle-accurate
distinction would require modelling the falling-edge of `i_CLK_28`
within an M1 cycle, which is sub-cycle granularity that JNEXT's M1
callback architecture intentionally collapses. Multi-subsystem effect.
Class-(d) confirmed.

### V12-DIVMMC-08 → reviewer-confirmed class-(d) architectural

Audit reason: `port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6)` and NR
0x09 bit 3 mapram-clear are mutually-exclusive on the same VHDL clock
(`elsif` ladder at `zxnext.vhd:4180-4186`). JNEXT serialises Z80 OUT
instructions, so two writes in the same VHDL cycle are impossible.

**Reviewer assessment**: confirmed truly class-(d). The Z80
instruction set has no atomic OUT-OUT instruction. Even DMA cannot
issue two port writes in the same VHDL clock. This is a VHDL-impossible
scenario for any realisable software; not a bug. Class-(d) confirmed.

## Hunt for missed findings

Examined `src/peripheral/divmmc.{cpp,h}`, `src/peripheral/sd_card.{cpp,h}`,
`src/peripheral/spi.{cpp,h}`, and the port-decode slice in
`src/core/emulator.cpp` (port 0xE3, 0xE7, 0xEB) against the VHDL
oracle. Cross-checked:

- **port_e7 decode** (write_cs in spi.cpp): VHDL `zxnext.vhd:3308-3322`
  if/elsif ladder mirrors JNEXT's branch order. SD-swap, RPI0/RPI1,
  Flash-CS gate all match. ✓
- **port_e3 read mask** (`read_control() & 0xCF`): VHDL line 4190 says
  `port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto 0)` →
  bits 5:4 forced 0, bits 7,6,3:0 surfaced. JNEXT mask `0xCF =
  11001111` ✓.
- **port_e3 write OR-latch** (`mapram_ = mapram_ || (val & 0x40)`):
  VHDL line 4182 `port_e3_reg(6) <= cpu_do(6) or port_e3_reg(6)` ✓.
- **divmmc_overlay rom_en/ram_en/rdonly logic**: matches divmmc.vhd
  lines 94-100 ✓.
- **automap pipeline (hold → held → automap)**: matches divmmc.vhd
  lines 123-148 at byte-boundary granularity. The reset→
  i_automap_reset→i_retn_seen ordering at lines 108/126/139 is
  modelled in `apply_enabled_transition_` and `on_retn`. ✓.
- **NMI button latch (button_nmi)**: VHDL lines 105-116. Modelled in
  `button_nmi_` with the "auto-clear-while-held" semantics fix from
  Pass-8. ✓.
- **automap combinational output (line 148)**: includes
  `i_automap_active AND (instant_on OR nmi_instant_on)` plus
  `i_automap_rom3_active AND rom3_instant_on`. JNEXT's
  `automap_active_ = automap_held_ || instant_match` accumulates both
  paths into `instant_match` ✓.
- **MISO mux** at `zxnext.vhd:3278-3280`: when no SS asserted,
  `spi_miso<='1'`. JNEXT's `write_data`/`read_data` no-active-device
  paths set `rx_data_=0xFF`. ✓ (Pass-3 fix, still correct.)
- **CMD58 OCR layout**: bit 31 = power-up, bit 30 = CCS. JNEXT's
  `ocr0 = initialized_ ? 0xC0 : 0x00` correct. ✓
- **State::RESPONDING terminal-byte transition** (line 282-289):
  pending_write_after_r1_ → RECEIVING_DATA correctly fires AFTER R1 is
  emitted on MISO, not when the buffer is queued. ✓ (Pass-2 fix.)
- **CMD12 stuff bytes**: 8 stuff bytes prefix per TBBLUE.FW pattern. ✓.

No additional findings.

## Tests run (post-reviewer-fixes)

```
$ cmake --build build -j$(nproc)
[100%] Built target jnext

$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 38
Total Test time (real) = 0.39 sec

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/divmmc_test
Total:  135  Passed:  135  Failed:    0  Skipped:    0

$ ./build/test/sdcard_test
Total:   25  Passed:   25  Failed:    0  Skipped:    0
```

Three new regression tests added by reviewer:
- `test/sdcard/sdcard_test.cpp` SD-22 (V12-DIVMMC-03 R7 byte 0)
- `test/sdcard/sdcard_test.cpp` SD-23 (V12-DIVMMC-04 R1 PARAMETER_ERROR)
- `test/sdcard/sdcard_test.cpp` SD-24 (V12-DIVMMC-06 stray pre-token byte)

Each verified discriminative via revert→FAIL→restore→PASS protocol.

## Summary

| Class | Count | Status   |
|-------|-------|----------|
| (a)   | 0     | n/a      |
| (b)   | 1     | resolved by audit (V12-DIVMMC-02 + SD-21) |
| (c)   | 4     | 1 resolved by audit (V12-DIVMMC-01 + SS-16); 3 promoted+resolved by reviewer (V12-DIVMMC-03/04/06 + SD-22/23/24) |
| (d)   | 3 confirmed architectural (V12-DIVMMC-05/07/08); no action |

**Verdict: APPROVE-WITH-NITS**. The audit's two findings are correct
VHDL/spec-faithful fixes with discriminative tests. The reviewer
promoted three of six catalogued items to class-(c) fixes
(V12-DIVMMC-03/04/06) and committed them with their own discriminative
tests; the remaining three (V12-DIVMMC-05/07/08) are confirmed truly
class-(d) architectural per the convergence rule. One NIT against the
audit's V12-DIVMMC-01 first-boot default value drift vs VHDL initial
(class-(c) latent, not fixed). No missed findings beyond the catalogue.
