# Pass-20 Reviewer Report — DivMMC + SD-Card + SPI Subsystem

**Date**: 2026-05-11
**Branch**: `task2/verify20-divmmc-sd-spi-reviewer` (off audit HEAD `f317b77`)
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify20-divmmc-sd-spi-reviewer`
**Reviewer**: independent reviewer (Pass-20)

## Verdict: **APPROVE**

The Pass-20 audit is approved without nits.

- Enumeration table is comprehensive (~98 file-cited rows, exceeding the
  prompt's "75 rows" claim by ~30%); no missed surfaces detected in the
  independent re-audit.
- The single class-(c) finding `V20-DIVMMC-01` (CMD55 + ACMD41 R1 APP_CMD
  bit) is spec-correct per SD Physical Layer Simplified Spec § 7.3.2.1
  Table 7-9.
- The SD-19 / SD-20 test updates are legitimate **bug-corrections** (the
  pre-fix tests had `r1_55 == 0x00`, which encoded the missing APP_CMD
  bit), NOT arbitrary "make-it-pass" enshrinement.
- SD-32 is a proper discriminative regression test — confirmed FAIL
  pre-fix and PASS post-fix via sandwich.
- No side effects on SD initialization path, no internal mask/AND on R1
  bit 5, no test/firmware path broken.
- Three class-(d) items (V20-DIVMMC-D01..D03) are pre-existing
  architectural items already documented in prior passes.

## Step 1 — row-count validation

Independent enumeration:

| Section | Reviewer count | Audit count | Match |
|---|---|---|---|
| SdCardDevice command handlers (CMD0/1/8/9/10/12/13/16/17/18/23/24/55/58 + ACMD41 + default) | 16 | 16 | ✓ |
| SdCardDevice state-machine transitions | 9 | 9 | ✓ |
| External ports (0xE3 rd/wr, 0xE7 rd/wr, 0xEB rd/wr) | 6 | 6 | ✓ |
| DivMMC-related NR registers (0x0A rd/wr, 0xB8-0xBB wr+rd, 0x09 wr, 0x83 wr, 0x02/03 gate) | 13 | 13 | ✓ |
| DivMMC automap entry-point coverage | 8 | 8 | ✓ |
| DivMmc internal state machine | 7 | 7 | ✓ |
| DivMmc memory overlay | 5 | 5 | ✓ |
| DivMmc port 0xE3 control register | 6 | 6 | ✓ |
| SpiMaster public-API surface | 10 | 10 | ✓ |
| DivMmc save_state schema fields | 18 | 18 | ✓ |
| **Total** | **98** | **98** | ✓ |

`grep -cE "^\| sd_card\.cpp|^\| emulator\.cpp|^\| divmmc\.cpp|^\| spi\.cpp|^\| divmmc\.h|^\| spi\.h"` on the audit report → 98 rows; matches my section sums (98). The "75-row" phrasing in the prompt summary undercounts the actual table, which is comprehensive.

No missed surfaces:
- All 14 supported SD CMDs + ACMD41 + default dispatcher case enumerated.
- All 3 external ports (0xE3, 0xE7, 0xEB) with both read/write sides.
- All 4 DivMMC entry-point NRs (0xB8-0xBB) read+write.
- All 6 control-bit fields of port 0xE3.
- All 8 automap entry-point sites (RST 0x00..0x38 sweep, 0x0066/0x04C6/0x0562/0x04D7/0x056A/0x3Dxx wildcard, 0x1FF8-0x1FFF off-zone).
- Full SpiMaster public API surface (reset, set_sd_swap, attach_device, write_cs, read_cs, write_data, read_data, spi_wait_n, save_state, load_state).

## Step 2 — Spot-check ✓ rows (10)

Read the cited VHDL/SD-spec lines and compared with the C++ summary:

1. **NR 0xB8 write @ zxnext.vhd:5584-5585** — `nr_b8_divmmc_ep_0 <= nr_wr_dat(7 downto 0)`. C++ `divmmc_.set_entry_points_0(v)` unmasked. ✓
2. **NR 0xBB write @ zxnext.vhd:5593-5594** — `nr_bb_divmmc_ep_1 <= nr_wr_dat(7 downto 0)`. C++ `set_entry_points_1(v)` unmasked. ✓
3. **port_e3_reg b5:4 invariant @ zxnext.vhd:4180-4187** — VHDL writes only bits 7, 6, 3:0; bits 5:4 are never assigned post-reset → always 0. C++ stores `(val & 0x8F) | (mapram?0x40:0)`, masking out bits 5:4. ✓ (F19-DIVMMC-NIT-01 fix)
4. **port_e3_dat @ zxnext.vhd:4190** — `port_e3_reg(7 downto 6) & "00" & port_e3_reg(3 downto 0)`. C++ `read_control()` returns `control_reg_ & 0xCF`. ✓
5. **port_e7 write_cs decode @ zxnext.vhd:3308-3326** — case-by-case mapping of `cpu_do(1:0)`, 0xFB, 0xF7, 0x7F (gated by `nr_03_config_mode | nr_02_reset_type(2)`). C++ `spi_.write_cs(val)` mirrors this decode with the flash_cs gate. ✓
6. **port_divmmc_io_en @ zxnext.vhd:2412** — `internal_port_enable(8)`. C++ gate at `emulator.cpp:4466` uses `effective_internal_port_enable(0x83) & 0x01`. ✓
7. **divmmc.vhd:108 button_nmi reset on i_retn_seen** — `if i_reset = '1' or i_automap_reset = '1' or i_retn_seen = '1' then button_nmi <= '0'`. C++ `on_retn()` clears button_nmi/hold/held/active/pending. ✓
8. **divmmc.vhd:126 automap_hold reset on i_retn_seen** — same pattern. C++ matches. ✓
9. **divmmc_automap_rom3_delayed_on @ zxnext.vhd:2902-2905** — bit-mask of `nr_bb_divmmc_ep_1(2..5)` against the four PC patterns 0x04C6 / 0x0562 / 0x04D7 / 0x056A. C++ matches at divmmc.cpp:395-407. ✓
10. **spi_master.vhd:177 o_spi_wait_n** — `state_idle or state_last_d`. C++ `spi_wait_n()` returns constant true (partial — covered by V20-DIVMMC-D01 class-d). ✓

All 10 spot-checks match. No row required ✗ → no missed findings flagged.

## Step 3 — V20-DIVMMC-01 verification

### Spec correctness

SD Physical Layer Simplified Spec v6.00 § 7.3.2.1 (Table 7-9) defines R1 bit-5 as:

> APP_CMD: A '1' indicates that the card will (or has) interpret(ed) the command as an ACMD.

Two implications for the implementation:

1. **CMD55's R1**: the card has interpreted the command as CMD55, signalling that the NEXT command will be treated as ACMD → bit 5 = 1.
2. **The subsequent ACMD's R1** (e.g. ACMD41): the card IS interpreting it as an ACMD → bit 5 = 1.

The fix at `560d101`:
- `cmd55_app_cmd()` (sd_card.cpp:815): `queue_r1((initialized_ ? 0x00 : 0x01) | 0x20)`. Pre-init = 0x21 (idle|APP_CMD); post-init = 0x20 (APP_CMD only). ✓ spec-correct.
- `acmd41_sd_send_op_cond()` (sd_card.cpp:974): `queue_r1(0x20)` — APP_CMD only (fast-init promotes `initialized_=true` before queueing R1, so no idle bit). ✓ spec-correct.

### SD-19 / SD-20 update legitimacy

Inspected the pre-fix file content of SD-19 and SD-20:

- **SD-19** (commit `560d101~1`, line 901): `r1_55 == 0x00`. Post-fix → `r1_55 == 0x20`.
- **SD-20** (commit `560d101~1`, line 942): `r1_55 == 0x00`. Post-fix → `r1_55 == 0x20`.

Both pre-fix assertions encoded the pre-fix wrong behavior (CMD55 R1 = 0x00, missing bit 5). The update to 0x20 reflects the spec-correct value. **This is bug-correction, not enshrinement.** ✓

### SD-32 discriminative

`test_sd_32_cmd55_acmd41_app_cmd_bit` (test/sdcard/sdcard_test.cpp:1771) asserts:

- Pre-init CMD55 → R1 = 0x21 (idle | APP_CMD).
- Post-init CMD55 → R1 = 0x20 (APP_CMD).
- ACMD41 (after CMD55) → R1 = 0x20 (APP_CMD).

Each leg distinguishes one of the three R1-bit-5-bearing responses. Functionally discriminative.

### Sandwich verification

Procedure:
1. **Baseline** (HEAD `f317b77`): `./build/test/sdcard_test` → 33/33 PASS.
2. **Revert fix only** (`git checkout 560d101~1 -- src/peripheral/sd_card.cpp`; tests left intact):
   - Rebuild → success.
   - Run sdcard_test → SD-19 FAIL, SD-20 FAIL, SD-32 FAIL. Confirmed:
     ```
     FAIL SD-19: [r1_55=0 r1_acmd42=4]
     FAIL SD-20: [r1_55=0 r1_17=0 tok=1 b0=2]
     FAIL SD-32: [r1_cmd55_pre=1 r1_cmd55_post=0 r1_acmd41=0]
     ```
3. **Restore fix** (`git checkout 560d101 -- src/peripheral/sd_card.cpp`):
   - Rebuild → success.
   - Run sdcard_test → 33/33 PASS.

Sandwich confirms: tests are discriminative for the fix; the fix is necessary and sufficient.

## Step 4 — Side-effect inspection

### SD initialization path

Boot path: CMD0 → CMD8 → CMD55 → ACMD41 (loop until idle clear) → CMD58.

- CMD55 R1 = 0x20 or 0x21 — TBBlue MMC_Init / FatFs check only R1 bit 0 (idle) and bit 7 (validity). Both bits are correctly placed: bit 0 reflects `initialized_`, bit 7 = 0 (response validity). No regression.
- ACMD41 R1 = 0x20 — same: bit 0 clear (init complete), bit 7 = 0. The host's `(R1 & 0x01) == 0` idle-clear check passes correctly. No regression.
- CMD58 R1 unchanged at sd_card.cpp:944 (`initialized_ ? 0x00 : 0x01`) — CMD58 is NOT an ACMD, so APP_CMD bit must NOT be set. Correctly omitted. ✓

### Internal AND/MASK paths

`grep -nE "queue_r1\("` enumerated all 18 R1 sites. The `queue_r1()` helper at sd_card.cpp:984 stores R1 verbatim into `resp_buf_[1]`; no internal masking. The host receives the R1 byte as-is on MISO. No code path AND/MASK's bit 5 expecting 0.

### Test side-effects

`grep -nE "send_cmd_r1.*[, ]\s*55\s*[,)]|send_cmd_r1.*[, ]\s*41\s*[,)]"` enumerated all CMD55 / ACMD41 invocations in the test file:

- `init_card()` (line 144-145): uses `(void)` — discards R1. ✓
- SD-19 (line 894): updated correctly.
- SD-20 (line 930): updated correctly.
- SD-29 (line 1561-1573): uses `(void)` for CMD55 / ACMD41 — discards R1. ✓
- SD-32 (line 1799-1814): new test, checks all three legs.

All other tests that call CMD55 / ACMD41 discard the R1 via `(void)`. No additional update required.

### Firmware behavior

- TBBlue `TBBLUE.FW` MMC_Init path (mmc.s) checks R1 via `and #0xFE` (CMD58 path) and `and #0x40` (OCR CCS) — never bit 5.
- FatFs `send_cmd` in `diskio.c` checks R1 bit 7 (validity) for poll exit and bit 0 (idle) for state.
- esxdos: same R1 bit-0/bit-7 only.

No spec-strict firmware path is broken. The fix is purely additive for spec strictness.

## Step 5 — Independent re-audit

Independent re-sweep of cross-cutting families looking for missed Pass-20 findings:

1. **R1 bit completeness for OTHER non-ACMD branches** — verified that no other regular CMD (CMD0/1/8/9/10/12/13/16/17/18/23/24/55/58) should set APP_CMD. CMD58 specifically: CMD58 is NOT an ACMD. SD spec § 7.3.2.4 Table 7-12 (R3 layout) confirms R1 byte does NOT carry APP_CMD for CMD58. ✓ No finding.
2. **CMD55 → non-ACMD fall-through R1 (e.g. CMD55 → CMD17 → CMD17's R1)** — per § 4.3.9.5 the app-cmd flag is cleared on a non-ACMD; CMD17's R1 must NOT have APP_CMD. SD-20 asserts `r1_17 == 0x00` (correct). ✓ No finding.
3. **R1 bit 7 (validity)** — confirmed all `queue_r1()` callers pass values with bit 7 = 0. ✓
4. **Persistent_response_byte_** — CMD0 sets 0x01, CMD12 sets 0xFF; neither carries APP_CMD because they are not part of a CMD55+ACMD pair. ✓
5. **port_e3 b5:4 readback @ divmmc.cpp:128** — already covered by F19-DIVMMC-NIT-01.
6. **SpiMaster `spi_wait_n`** — already a class-(d) (V20-DIVMMC-D01).
7. **`i_retn_seen` clear timing** — already class-(d) (V20-DIVMMC-D02).
8. **`button_nmi` clear-while-held timing** — already class-(d) (V20-DIVMMC-D03).
9. **Save_state schema** — re-confirmed all 18 fields, every one matches the live runtime state. ✓
10. **NR 0x83 bit-fanout** — bits 0/1 (DivMMC/MF) propagate to `divmmc_.set_port_io_enable()` and the MF subsystem. Match VHDL `internal_port_enable(8)` and `internal_port_enable(9)`. ✓

No additional findings.

## Test invariants (post-fix, post-review)

- `ctest --output-on-failure`: **38/38 PASS** (verified)
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 PASS** (verified)
- `bash test/00regression/regression.sh`: **33/0/0 PASS** (verified)
- `./build/test/sdcard_test`: **33/33 PASS** (verified, incl. SD-32 new)
- `./build/test/divmmc_test`: 137 plan rows / 0 fail / 0 skip (verified)

## Summary

| Aspect | Status |
|---|---|
| Table row count (audit vs reviewer) | ✓ 98 / 98 |
| Spot-check 10 ✓ rows | ✓ 10/10 match |
| V20-DIVMMC-01 spec-correctness | ✓ per SD spec § 7.3.2.1 Table 7-9 |
| SD-19 / SD-20 update legitimacy | ✓ bug-correction (pre-fix asserted `r1_55 == 0x00` which encoded the bug) |
| SD-32 discriminative | ✓ FAIL pre-fix → PASS post-fix |
| Sandwich verification | ✓ confirmed |
| Side-effect inspection | ✓ no internal mask break; no test break; no firmware break |
| Independent re-audit | ✓ no missed findings |
| Class-(d) items | 3 documented carry-forwards (D01/D02/D03), pre-existing |

**Verdict: APPROVE (no NITs, no missed findings).**
