# NextZXOS Boot Subsystem — Pass-4 Blind Verification: DivMMC + SD-card + SPI

Date: 2026-05-09
Branch: `task2/verify4-divmmc-sd-spi`
Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify4-divmmc-sd-spi`
Auditor: Pass-4 (blind — did not read prior NEXTZXOS-BOOT-SUBSYSTEM-* reports).

## Verdict

**1 class-(a) bug found and fixed.** Audit is converging.

After three prior passes (which closed the obvious AUTOMAP-trigger / NR $BB / port $E3-$E7-$EB / button-strobe-gating / SPI default-$FF issues), this pass-4 audit focused on the methodology that was deliberately under-explored: SD-card device lifecycle coherence, CRC paths, soft-reset cross-product, boundary block addresses, and a differential VHDL spot-check. Twelve discrepancies were classified; only ONE is class-(a).

## Methodology

### 1. SD-card lifecycle coherence table

For each of `mount()`, `unmount()`, `reset()`, `deselect()`, listed which of the 16 state-touching fields each function clears. Result: cross-mount and cross-reset paths leave several fields stale (e.g. `multi_block_`, `multi_block_sector_`, `data_block_`, `cmd_buf_`, `app_cmd_`). All such cases are **defended by the only caller path** (Emulator::init calls `reset()` before `mount()` on hard reset, and skips both on soft reset), so they degrade to class-(b) — robust-but-fragile, not exploitable today.

### 2. CRC validation

C++ ignores all CRCs (CMD0's mandatory $95, data block CRC16, write CRC). VHDL has no CRC enforcement at the bus level — CRC is a device-side responsibility. tbblue.fw and standard SD masters always send correct CRCs anyway. Class-(c) — non-issue.

### 3. Soft-reset interaction matrix

- `nr_0a_sd_swap` (VHDL :1125): default '0', no reset path → C++ correctly preserves `sd_swap_` across soft reset. ✓
- `nr_0a_divmmc_automap_en` (VHDL :1126): default '0', no reset path → C++ correctly preserves `nr_0a_4_enable_` across soft reset. ✓
- `port_e3_reg` (VHDL :4177): cleared on hard reset → C++ `DivMmc::reset()` clears `conmem_/mapram_/bank_`. ✓
- `port_e7_reg` (VHDL :3308): cleared to all-1s on hard reset → C++ `SpiMaster::reset()` sets `cs_=0xFF`. ✓
- `spi_master.miso_dat`: VHDL `i_reset='0'` always (line 3285) → never reset in real hardware. C++ resets `rx_data_=0xFF` on reset. **Class-(b)** discrepancy — VHDL-non-faithful but stale-byte exposure window doesn't exist (first port-EB read after reset is preceded by a CMD0 write, which updates rx_data_).
- `sd_card_` external device: VHDL = not in reset domain. C++ `Emulator::init` correctly skips `sd_card_.reset()` on soft reset (G46(b) fix at emulator.cpp:159-161). ✓
- DivMMC FFs (button_nmi, automap_hold/held): VHDL `i_reset='1'` clears all (divmmc.vhd:108,126,139). C++ `DivMmc::reset()` clears all. ✓

### 4. Boundary block addresses

- Block 0: handled correctly (CMD17/CMD18 read sector=0, file at offset 0).
- Last valid block (= image_size/512 - 1): handled correctly (`byte_addr + 512 == file_size_` allows read).
- Block at image_size/512 (off-end): triggers past-end branch, queues `r1=0x00 + error token 0x08`. **Class-(b)** — R1 should set bit 5 (PARAMETER_ERROR) per spec, but C++ returns R1=0x00. tbblue's `rcvr_datablock` recognizes the 0x08 error token and bails, so observable behavior is correct.
- Block 0xFFFFFFFF (max): byte_addr fits in uint64_t, `+ 512` overflow-safe, falls through to past-end branch. ✓

### 5. CRC return on data block read

C++ sends two `0x00` CRC bytes after data. Real SD cards send a real CCITT CRC16. Most firmware ignores it. Class-(c).

### 6. Multi-CMD55 / ACMD41 init dance retry

C++ `acmd41_sd_send_op_cond()` immediately sets `initialized_=true` and returns R1=0x00. Real cards sometimes return R1=0x01 for several iterations. tbblue.fw's polling loop tolerates fast-init. Class-(c).

### 7. Port $EF mirror

VHDL has no $EF mirror for $E3/$E7/$EB. The original audit guidance was wrong about this. Skip.

### 8. SPI clock dividers

NR $86/$87/$88 are bus-port enables, NOT SPI dividers. No SPI clock divider state in either VHDL or C++. The original audit guidance was wrong. Skip.

### 9. Differential VHDL spot-check (10 signals)

| VHDL signal | C++ counterpart | status |
|-------------|-----------------|--------|
| `divmmc_automap_held` (zxnext.vhd:935) | `DivMmc::is_nmi_hold()` | covered |
| `divmmc_retn_seen` (zxnext.vhd:4111) | `DivMmc::on_m1_retn_delay()` | covered |
| `divmmc_nmi_hold` (zxnext.vhd:934) | `DivMmc::is_nmi_hold()` | covered |
| `port_e3_dat` (zxnext.vhd:4190) | `DivMmc::read_control()` (mask 0xCF) | covered |
| `divmmc_automap_*_q` latches (zxnext.vhd:4114-4135) | per-M1 sequence in `check_automap()` | covered (byte-granular) |
| `port_e7_lsb` / `port_eb_lsb` | port-mask 0xFF | covered |
| `spi_ss_flash_n` | not modeled (no flash) | class-(c) |
| `spi_ss_rpi0_n` / `spi_ss_rpi1_n` | not modeled (no Pi) | class-(c) |
| `spi_wait_n` | `SpiMaster::spi_wait_n()` always true | covered |
| `nmi_assert_divmmc` | upstream NmiSource | scope |

### 10. Re-examined Pass-2 class-(b) — `rom3_active_` vs `sram_rom3()`

The C++ comment at `divmmc.cpp:319-323` acknowledges altrom-locked path is partially modeled. NextZXOS boot path keeps `NR 0x8C bit 7=0` (altrom_en=0), so `(altrom_en AND alt_128_n) OR (rom3_sel AND NOT altrom_en)` reduces to `rom3_sel`. Class-(b) threshold not met for boot. No fix.

## Findings

### Bug 1 — CMD24 RECEIVING_DATA mishandles spec-mandated 0xFF gap bytes (CLASS-A) — FIXED

**Location**: `src/peripheral/sd_card.cpp:130-138` (pre-fix).

**Symptom**: After CMD24 R1, if the host writes any 0xFF "host idle clock" bytes BEFORE the 0xFE start-of-data token, the C++ erroneously absorbs each 0xFF into `data_block_[data_idx_++]`, shifting the entire 512-byte payload and corrupting the disk write.

**SD spec authority**: SD Physical Layer Simplified Spec 6.00 § 7.3.3.2:

> Following the command response (R1) and one (or more) bytes of $FF (host SPI clock), the data must be sent following a Data Token byte ($FE for single block, $FC for multi-block).

The card MUST tolerate any number of 0xFF gap bytes pre-token.

**Pre-fix code path** (sd_card.cpp:130-138):
```cpp
case State::RECEIVING_DATA:
    if (tx == 0xFE && data_idx_ == 0 && data_crc_count_ == 0) {
        break;  // token received
    }
    if (data_idx_ < 512) {
        data_block_[data_idx_++] = tx;  // BUG: 0xFF gap byte absorbed as data
        break;
    }
```

**Why prior passes missed it**: tbblue.fw's `xmit_datablock` (src/firmware/app/src/ff/diskio.c:179-202) writes the 0xFE token IMMEDIATELY after R1 — no gap bytes. So the boot path doesn't trigger the bug. Other firmware (esxdos `F_WRITE`, generic FatFs that DOES send gap bytes per spec, custom drivers) WOULD trigger it. Pass-4's lifecycle/boundary methodology surfaced the path that prior passes missed.

**Fix** (sd_card.cpp:139-156): explicitly skip 0xFF bytes pre-token while `data_idx_==0 && data_crc_count_==0`:

```cpp
if (data_idx_ == 0 && data_crc_count_ == 0 && tx == 0xFF) {
    break;  // gap byte — wait for 0xFE token
}
```

**Verification**: full test suite (`fuse_z80`, `divmmc_tests`, `sdcard_tests`, `sd_rom_extractor_tests`) all pass post-fix.

### Other findings (class-(b) / class-(c), not fixed)

| # | Location | Issue | Class | Reason not fixed |
|---|----------|-------|-------|------------------|
| 2 | `sd_card.cpp:27-53` `mount()` | doesn't fully clear all per-session state from prior mount | (b) | only caller (Emulator::init line 3960) is preceded by `reset()` (line 159-161) on hard-reset and is skipped entirely on soft-reset |
| 3 | `sd_card.cpp:55-64` `unmount()` | clears only 5 of 16 fields | (b) | file-closed gate (`if (!file_.is_open()) return 0xFF`) prevents stale-state exposure |
| 4 | `spi.cpp:26-34` `SpiMaster::reset()` | clears `devices_` array AND `rx_data_` | (b) | `Emulator::init` re-attaches devices at line 3670 unconditionally; rx_data_ stale-byte exposure has no read window before next CMD |
| 5 | `divmmc.cpp:29-49` `DivMmc::reset()` | doesn't clear `rom3_active_` | (b) | `Emulator::init` re-syncs at line 3824 unconditionally |
| 6 | `spi.cpp:69-78` port_e7 decode | doesn't recognize $7F (flash) under config_mode/reset_type(2) | (b) | flash chip not modeled in jnext; port-readback divergence only matters for FW-update path |
| 7 | `sd_card.cpp:331-334` unhandled CMD response | doesn't set R1 ILLEGAL_COMMAND bit (0x04) | (b) | spec divergence; tbblue.fw doesn't check this bit on unknown commands |
| 8 | `sd_card.cpp:301-313` ACMD-not-41 fallback | returns illegal-command R1 instead of executing as regular CMD | (b) | spec divergence; firmware always sends CMD55+ACMD41 in pairs |
| 9 | `divmmc.cpp:286-291` button_nmi clear | rising-edge-of-held in C++ vs steady-state in VHDL | (c) | one-cycle window only fires when button pressed AGAIN while held=1 — narrow race |
| 10 | `sd_card.cpp:123-128` RECEIVING_CMD | no timeout for stalled command stream | (c) | spec doesn't mandate timeout for SPI-mode |
| 11 | `sd_card.cpp:247-249` data block CRC | sends $00,$00 instead of real CRC16 | (c) | most firmware ignores CRC |
| 12 | `sd_card.cpp:653-656` ACMD41 first-call | immediately sets initialized_=true | (c) | tbblue.fw tolerates fast-init |

## Convergence assessment

**Audit is CONVERGING but not fully converged.** Pass-4 found exactly one new class-(a) bug (the 0xFF-gap-byte data corruption in CMD24), confirming that prior passes missed at least one realistic spec-violation. The methodology rotation (lifecycle / boundary / cross-product) was successful — none of the prior passes exercised SD-card-protocol gap-byte handling.

A hypothetical pass-5 would likely focus on:
- The `rom3_active_` vs `sram_rom3()` class-(b) item (audit whether NextZXOS ever sets `NR 0x8C bit 7` mid-boot, which would re-elevate it to class-(a))
- Fully VHDL-faithful gating of `automap_active_` by the composite `enabled_` (currently masked by side-effect in `apply_enabled_transition_`, but not literally identical to VHDL's `(NOT i_automap_reset) AND ...`)
- Cycle-accurate SPI master timing (currently zero-latency byte wrapper) — orthogonal concern, would require G137 long-term FSM rewrite.

The remaining class-(b)/(c) findings are not exploitable in tbblue's NextZXOS boot path. Diminishing returns are clear.

## Open questions

1. **Does any path WRITE to port 0xEB during CMD17/CMD18 read?** If so, those bytes go through `receive()` not `send()`. Currently, jnext's `receive()` in SENDING_DATA falls to the default branch which only checks for new-CMD start byte. Stray writes during read are silently dropped. Spec-OK per § 7.3.3.1 — when host clocks 0xFF during a card response, that's the standard "host idle".

2. **Should `mount()` invoke `reset()` for defense-in-depth?** Currently `mount()` is only called from a known-clean state (post-reset). If a future code path adds runtime SD-image swap (e.g., via Qt menu), `mount()` would need to be self-contained. Recommend adding `reset()` call inside `mount()` when that path lands.

3. **VHDL line 3285 hardcodes `i_reset => '0'` for the `spi_master` entity.** This means the FPGA's spi_master internal state (state_r, oshift_r, ishift_r, miso_dat) is NEVER reset in real hardware after the initial FPGA load. Should jnext's `SpiMaster::reset()` reflect this and NOT clear `rx_data_`? Probably yes for true VHDL faithfulness, but the current behavior is harmless. Defer.

## Test Status

```
$ ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
fuse_z80_tests ...................   Passed
divmmc_tests .....................   Passed
sdcard_tests .....................   Passed
sd_rom_extractor_tests ...........   Passed

100% tests passed, 0 tests failed out of 4
```

## Files modified

- `src/peripheral/sd_card.cpp`: bug-1 fix (0xFF gap byte handling in RECEIVING_DATA).
- `doc/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY4-DIVMMC-SD-SPI.md`: this report (NEW).

## Branch HEAD

To be set after commit.
