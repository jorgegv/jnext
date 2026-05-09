# NextZXOS Boot — Pass-10 Convergence Audit — DivMMC + SD + SPI

**Worktree**: `.claude/worktrees/task2-verify10-divmmc-sd-spi`
**Branch**: `task2/verify10-divmmc-sd-spi`
**Author**: blind audit, ultrathink, ZX-Next-emulator skill
**Date**: 2026-05-09
**Mode**: Pass-10 strict-convergence (target = 0 pending bugs of any class)

## Verdict

**Class-(a) found**: **1**
- DA-10A.1: `DivMmc::rom3_active_` not re-synced from MMU after `Emulator::load_state`. Class-(a) — break-on-restore of the ROM3-conditional automap gate. Fixed.

**Class-(b) found**: **0**

**Class-(c) found**: **2** (latent, NOT fixed; documented for future passes)
- DA-10C.1: `SpiMaster::reset()` clears `rx_data_` even on soft reset. VHDL spi_master.vhd:106-117 is hardcoded `i_reset => '0'` at instantiation (zxnext.vhd:3285), so the SPI master's miso_dat FF is never reset by the soft path. JNEXT's symmetric clear is incorrect on soft reset, but practically harmless because `rx_data_` is overwritten on the next `read_data`/`write_data`. Latent.
- DA-10C.2: `SdCardDevice::receive` in `RECEIVING_DATA` state silently absorbs ANY non-0xFE byte (after one initial 0xFF gap-skip) into `data_block_[0]`. Per SD Phys Spec § 7.3.3.2, the card SHOULD reject mid-stream tokens that aren't the data token (e.g. host sends 0xFD Stop-Tran token mid-CMD24 — that's illegal but should not be absorbed as payload). Latent — no firmware path exercises this misuse.

**Class-(d) found**: **1** (architectural; NOT fixed)
- DA-10D.1: SPI master modelled as zero-latency byte pump. VHDL serial/spi_master.vhd is a 16-cycle FSM (state_r 5 bits, two delayed-control FFs for sck/miso synchronization). The byte-level wrapper in JNEXT cannot model the `o_spi_wait_n` cadence accurately for cycle-accurate DMA throttling (zxnext.vhd:1844 `dma_wait_n <= z80_wait_n and spi_wait_n`). `spi_wait_n()` is hardcoded `true` (= no wait). Architectural — same long-term G137 issue documented in earlier passes.

## Pass-10 process

Tenth blind audit. The user's stop criterion: 0 pending bugs of any class across all 4 subsystems. Convergence is honest if there is nothing left to find. If genuinely converged, this pass should yield zero class-(a/b/c). It found one class-(a) (load-state re-sync gap) and two latent class-(c)s — strict convergence not yet achieved on this subsystem.

## Spot-checks performed

### 1. VHDL signal-by-signal coverage — `device/divmmc.vhd`

All signals on the entity port (lines 27-59) verified:

| VHDL signal | C++ counterpart | Status |
|---|---|---|
| `i_cpu_a_15_13` | derived from addr in `is_ram_mapped`/`is_active`/`check_automap` | ✓ |
| `i_cpu_mreq_n` / `i_cpu_m1_n` | `is_m1` parameter (mreq_n implicit on M1) | ✓ |
| `i_en` (= port_divmmc_io_en) | `port_io_enable_` | ✓ |
| `i_automap_reset` (= port_io_en=0 OR nr_0a=0) | `apply_enabled_transition_()` clear | ✓ |
| `i_automap_active` (= sram_pre_override(2)) | `main_path_eligible` | ✓ |
| `i_automap_rom3_active` (composite) | `rom3_path_eligible` | partial (✓ for Next-mode boot path; ignores `sram_romcs` and altrom branch — class-(d) noted in source) |
| `i_retn_seen` | `on_retn` / `on_m1_retn_delay` | ✓ |
| `i_divmmc_button` | `set_button_nmi` | ✓ |
| `i_divmmc_reg` (port_e3_reg(7:6) & "00" & port_e3_reg(3:0)) | `conmem_`/`mapram_`/`bank_` | ✓ |
| `i_automap_*_on_q` (7 signals) | `entry_points_*` / `entry_valid_*` / `entry_timing_*` | ✓ |
| `o_divmmc_rom_en/ram_en/rdonly/ram_bank` | `is_rom_mapped`/`is_ram_mapped`/`is_read_only`/`ram_page_for` | ✓ |
| `o_disable_nmi` (= automap OR button_nmi) | `is_nmi_hold` (= held OR button_nmi) | ✓ |
| `o_automap_held` | `automap_held()` | ✓ |

The `automap` combinational signal (line 148 = held OR (active AND instant_on) OR (rom3_active AND rom3_instant_on)) is computed in `check_automap` (`automap_active_ = automap_held_ || instant_match`). ✓

### 2. VHDL signal-by-signal coverage — `serial/spi_master.vhd`

Entity port (lines 41-57):

| VHDL signal | C++ counterpart | Status |
|---|---|---|
| `i_CLK` (twice sck freq) | (zero-latency model — class-d) | architectural |
| `i_reset` | hardcoded '0' at instantiation (zxnext.vhd:3285) — never reset | matches |
| `i_spi_rd` (= port_eb_rd) | `read_data` trigger | ✓ |
| `i_spi_wr` (= port_eb_wr) | `write_data` trigger | ✓ |
| `i_spi_mosi_dat` | `val` parameter | ✓ |
| `o_spi_miso_dat` | `rx_data_` | ✓ |
| `o_spi_sck` / `o_spi_mosi` / `i_spi_miso` | abstracted as device exchange | byte-level OK |
| `o_spi_wait_n` | `spi_wait_n()` hardcoded true | architectural — class-d |

VHDL `i_reset => '0'` (zxnext.vhd:3285) means the SPI master's internal FFs (state_r, oshift_r, ishift_r, miso_dat) are NEVER reset — only on FPGA core load. `SpiMaster::reset()` clears `cs_` (correct: port_e7_reg DOES reset on `reset` per zxnext.vhd:3308) but ALSO clears `rx_data_` (incorrect on soft reset: VHDL miso_dat is in spi_master_mod, never reset). Latent class-(c) — DA-10C.1.

### 3. VHDL signal-by-signal coverage — `zxnext.vhd` SPI/DivMMC slice

- `port_e7_reg` decode (lines 3308-3326) including all 5 patterns ("10"/SD0, "01"/SD1, 0xFB/RPI0, 0xF7/RPI1, 0x7F/Flash, else 0xFF) — `SpiMaster::write_cs` matches all 5 plus the SD-swap composite for SD lines and the `flash_cs_enable_` gate for 0x7F. ✓ (Pass-8 verify-audit fixes for sd_swap and flash_cs_enable confirmed solid.)
- `nr_0a_sd_swap` (line 3312/3314) — `set_sd_swap` ✓
- `port_divmmc_io_en` / `nr_0a_divmmc_automap_en` (line 4112) — `set_port_io_enable` / `set_nr_0a_4_enable` ✓
- `port_e3_reg(6)` OR-latch (line 4182) — `mapram_ = mapram_ || ((val & 0x40) != 0)` ✓
- `port_e3_reg(6)` clear via `nr_09_we and nr_wr_dat(3)` (line 4184) — `clear_mapram()` called from NR 0x09 handler ✓
- `divmmc_retn_seen` masking by `mf_is_active` (line 4111) — confirmed in NMI plan + Multiface code path; covered by emulator wiring.

### 4. SD Physical Layer Simplified Spec 6.00 § 7.3 compliance

Re-walked the SPI-mode command set:

| CMD | Index | jnext shape | Spec reference | Status |
|---|---|---|---|---|
| CMD0 | 0 | NCR + R1=0x01 | § 7.3.2 R1 | ✓ |
| CMD1 | 1 | NCR + R1=0x00 (init complete) | § 7.3.2 / TBBlue compat | ✓ |
| CMD8 | 8 | NCR + R7 (R1 + 0x00 + 0x00 + 0x01 + check) | § 7.3.2.6 | ✓ voltage echo + check pattern |
| CMD9 | 9 | NCR + R1 + 0xFE + 16 CSD + 2 CRC | § 7.3.3 / 5.3.3 v2 | ✓ SDHC CSD v2 layout |
| CMD10 | 10 | NCR + R1 + 0xFE + 16 CID + 2 CRC | § 5.2 | ✓ |
| CMD12 | 12 | 8 stuff bytes + NCR + R1 | § 7.3.2 / TBBlue stuff-byte poll | ✓ |
| CMD13 | 13 | NCR + R1 + R2 status byte | § 7.3.2.4 | ✓ |
| CMD16 | 16 | NCR + R1 (idle bit conditioned on initialized_) | § 4.9.1 SDHC fixed 512 | ✓ Pass-5 fix |
| CMD17 | 17 | NCR + R1 + 0xFE + 512 + 2 CRC | § 7.3.3 | ✓ |
| CMD18 | 18 | first block CMD17 shape; subsequent 0xFE + 512 + CRC | § 7.3.3 | ✓ |
| CMD23 | 23 | NCR + R1 | § 4.6.2 | ✓ |
| CMD24 | 24 | NCR + R1; host sends 0xFE + 512 + 2 CRC; card sends 0x05 token | § 7.3.3.2 | ✓ Pass-4 0xFF gap fix |
| CMD55 | 55 | NCR + R1 | § 7.3.2 | ✓ Pass-9 fall-through fix for non-ACMD41 |
| CMD58 | 58 | NCR + R1 + 4 OCR bytes (0xC0/0xFF/0x80/0x00 SDHC) | § 5.1 / TBBlue MMC_Init | ✓ |
| ACMD41 | 41 | NCR + R1=0x00 (ready) | § 4.2.3 | ✓ |
| (default unhandled) | — | NCR + R1 with bit 2 (illegal cmd) | § 7.3.2.1 | ✓ Pass-8 fix |

R1 layout (§ 7.3.2.1): `[0]=idle, [1]=erase_reset, [2]=illegal_cmd, [3]=cmd_crc_err, [4]=erase_seq_err, [5]=address_err, [6]=parameter_err, [7]=zero`. JNEXT correctly emits idle (CMD0=0x01) and illegal-cmd (CMD16 with non-512 arg = 0x05; default = 0x05; pre-init CMD17/18 = 0x01).

R7 voltage echo (§ 7.3.2.6): bytes 1..3 are voltage range fields (1 = 2.7-3.6V), byte 4 = check pattern echo. JNEXT: `{ 0xFF, 0x01, 0x00, 0x00, 0x01, check }` — NCR + R1=0x01 + 0x00 + 0x00 + 0x01 + check. ✓

OCR voltage range (§ 5.1): bit 23..15 = voltage windows. Standard SDHC returns `0x40FF8000` (CCS=1) or `0xC0FF8000` (CCS=1 + power-up complete). JNEXT initialized → 0xC0FF8000. ✓

CSD v2 (§ 5.3.3): C_SIZE encodes capacity = (C_SIZE+1)*512KB. JNEXT computes from `file_size_/512KB - 1`. ✓

### 5. Multi-state interaction final pass

Walked: AUTOMAP × Multiface NMI × conmem × mapram × rom-bank-select × NR $BB × NR $D8/$D9/$DA × machine type × cold/soft/hard reset.

- **automap × conmem**: `is_active() = port_io_enable_ && (conmem_ || automap_active_)` per line 123 of divmmc.h. CONMEM bypasses the nr_0a_4_enable gate (matches VHDL: line 4112 only gates the divmmc_automap_reset path; CONMEM in port_e3_reg(7) is gated only by port_divmmc_io_en at zxnext.vhd:4147). ✓
- **automap × Multiface NMI**: `divmmc_retn_seen <= z80_retn_seen_28 and not mf_is_active` (line 4111) — Multiface's NMI handler RETN does NOT clear DivMMC automap. Covered by emulator's NmiSource wiring (separate audit subject).
- **mapram × bank_=3 read-only**: `is_read_only(addr<0x4000 && mapram_ && bank_==3)` ✓ matches divmmc.vhd:100 (rdonly = page0=1 OR (mapram=1 AND ram_bank=X"3")).
- **mapram OR-latch reset behaviour**: `reset()` clears mapram_ (correct: port_e3_reg resets to all-zeros per zxnext.vhd:4177). NR $09 bit 3 also clears it via `clear_mapram()`. ✓
- **machine type × ROM3 select**: `divmmc_.set_rom3_active(mmu_.sram_rom3())` invoked at all relevant fan-out sites in Emulator. Pass-7 fix established this. ✓
- **cold/soft/hard reset**: Soft reset preserves enable-flag state (`enabled_/port_io_enable_/nr_0a_4_enable_`); clears automap latches and NMI button — matches VHDL i_automap_reset path. ✓

### 6. Save/load coverage final pass

`DivMmc::save_state` persists 17 fields. `SpiMaster::save_state` persists 3 fields (cs_, rx_data_, sd_swap_). `SdCardDevice` is intentionally NOT saved (rewind ring skips SD).

Reviewed every public-state field in `divmmc.h`:

| Field | Saved? | Re-synced on load? | Status |
|---|---|---|---|
| `port_io_enable_` | yes | — | ✓ |
| `nr_0a_4_enable_` | yes | — | ✓ |
| `enabled_` | yes (composite) | — | ✓ |
| `conmem_` / `mapram_` / `bank_` / `control_reg_` | yes | — | ✓ |
| `automap_active_` / `automap_hold_` / `automap_held_` | yes | — | ✓ |
| `rom3_active_` | **NO** | **NO (pre-fix)** | **CLASS-(a) — DA-10A.1** |
| `button_nmi_` | yes | — | ✓ |
| `layer2_map_read_` | yes | — | ✓ |
| `retn_pending_clear_` | yes | — | ✓ |
| `entry_points_0_/1_` / `entry_valid_0_` / `entry_timing_0_` | yes | — | ✓ |
| `rom_` (ROM bytes) | not saved (loaded from SD-extracted bytes) | — | ✓ |
| `ram_` (128K) | yes | — | ✓ |

`rom3_active_` is a feeder shadow from MMU (set via `set_rom3_active()` at port-write / NR-commit / machine-type-change sites). Save_state does NOT persist it. Constructor default = false. Pre-fix, a `load_state` restoring a snapshot taken with sram_rom=3 selected would leave rom3_active_ at its constructor default false, breaking the ROM3-conditional automap gate (sram_divmmc_automap_rom3_en, divmmc.vhd:130,148) until the next MMU port write. Class-(a).

`SpiMaster::flash_cs_enable_` is also a feeder shadow but is correctly re-synced from `nextreg_.nr_03_config_mode()` and `nmi_source_.reset_type()` at the end of `Emulator::load_state` (see lines 6309-6311). ✓

## Findings — fixes applied

### DA-10A.1 — Class-(a) — `rom3_active_` not re-synced on load_state

**Root cause**: `DivMmc::save_state`/`load_state` does not persist `rom3_active_`. It is fed only at port-write / NR-commit / machine-type-change sites. After `Emulator::load_state`, no fan-out fires automatically, so `rom3_active_` stays at the constructor default `false`. This breaks the ROM3-conditional automap gate (sram_divmmc_automap_rom3_en, divmmc.vhd:130,148; zxnext.vhd:3138) until the next MMU port write — a window during which all NR $BB ROM3-only entry points (RST $0066 NMI delayed, $04C6 / $04D7 / $0562 / $056A tape traps, $3Dxx wildcard) are blocked.

**Fix**: in `Emulator::load_state` (src/core/emulator.cpp:6180), after `divmmc_.load_state(r)`, add:
```cpp
divmmc_.set_rom3_active(mmu_.sram_rom3());
```
Mirrors the existing `i2c_.set_pi_i2c1_en` / `spi_.set_flash_cs_enable` re-sync pattern used elsewhere in load_state. The same fan-out runs unconditionally at end of `Emulator::init` (line 4273) — the load_state path needed an equivalent.

**VHDL oracle**: zxnext.vhd:2981-3008 (sram_pre_rom3 derivation), :3138 (sram_divmmc_automap_rom3_en composite), :3137 (sram_divmmc_automap_en).

**Class**: (a) — break-on-restore, observable as a missing automap fire after restoring a snapshot in ROM3 mode.

## Findings — class-(c) latent (NOT fixed)

### DA-10C.1 — `SpiMaster::reset()` clears `rx_data_` on soft reset

**Description**: `SpiMaster::reset()` is invoked from `Emulator::init` for both cold init AND soft reset (line 127). Per VHDL `i_reset => '0'` at instantiation (zxnext.vhd:3285), the spi_master_mod's internal FFs (state_r, oshift_r, ishift_r, miso_dat) are NEVER reset by the soft path — only on FPGA core load. JNEXT's `cs_` clear is correct (port_e7_reg DOES reset on soft, zxnext.vhd:3308) but the `rx_data_` (= miso_dat) clear is incorrect on soft reset.

**Why latent**: `rx_data_` is overwritten on the next `read_data`/`write_data` exchange. The supervisor's first SPI access post-reset re-establishes a meaningful byte in flight. Functionally undetectable in practice.

**Why not fixed in pass-10**: low-impact, would require threading a hard/soft hint through `SpiMaster::reset` and weighing the cost of the API churn vs. the benefit. Documented for future passes.

### DA-10C.2 — `SdCardDevice::receive` absorbs unexpected mid-stream tokens

**Description**: In `RECEIVING_DATA` state, after one initial 0xFF gap-skip, ANY non-0xFE byte falls through to `data_block_[data_idx_++] = tx`. Per SD Phys Spec § 7.3.3.2, only 0xFE (single block) or 0xFC (multi-block — not applicable to CMD24) are valid data tokens. Other bytes (e.g. 0xFD Stop-Tran, 0x00, partial pre-token noise) should be rejected or at least NOT absorbed as payload byte 0.

**Why latent**: no firmware in scope (TBBlue, NextZXOS, esxdos, FatFs) sends invalid mid-stream tokens during CMD24. The current behaviour matches "tolerant card" semantics.

**Why not fixed in pass-10**: spec-strict rejection would require a small state-machine refinement and a test. Latent.

## Findings — class-(d) (architectural)

### DA-10D.1 — SPI master cycle-FSM is not modelled

Documented in pass-9 already (G137 long-term). `SpiMaster` is a zero-latency byte pump; `o_spi_wait_n` is hardcoded true. Cycle-accurate DMA-via-SPI throttling (zxnext.vhd:1844) cannot be reproduced without a full state-r FSM rewrite. No firmware in scope hits the throttle pathologically.

## Build + test status

```
LANG=C cmake -B build -DENABLE_QT_UI=ON                      → OK
LANG=C cmake --build build -j$(nproc)                        → OK (clean rebuild)
LANG=C ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
  fuse_z80_tests             ........ Passed
  divmmc_tests               ........ Passed
  sdcard_tests               ........ Passed
  sd_rom_extractor_tests     ........ Passed
  100% tests passed, 0 tests failed out of 4
LANG=C ctest --test-dir build (full)
  100% tests passed, 0 tests failed out of 37
```

No regressions across any subsystem.

## Convergence verdict

**Honest verdict**: NOT YET CONVERGED on this subsystem.

Pass-10 found 1 class-(a) bug (DA-10A.1, fixed) and 2 latent class-(c) bugs (DA-10C.1, DA-10C.2, NOT fixed) plus 1 architectural class-(d) (DA-10D.1, pre-existing).

The class-(a) load-state re-sync gap is now fixed in this pass — but its existence means the previous pass's "zero-pending" verdict was incomplete. This validates the user's stricter convergence criterion: a pass is only complete when blind audit AND save-load coverage AND boundary-input coverage AND multi-state interaction AND VHDL-signal-by-signal coverage all return zero pending bugs of any class.

A pass-11 should re-walk the same matrix and confirm:
- DA-10A.1 fix is correct and tested (no save_state unit tests exist; integration-level fix; future pass should add coverage if practical).
- DA-10C.1 / DA-10C.2 are still acceptable as latent; OR should be retired by spec-strict refinements.
- DA-10D.1 remains class-(d) by architecture.

If pass-11 finds zero new class-(a/b/c), the subsystem converges.

## Files changed

- `src/core/emulator.cpp` — added `divmmc_.set_rom3_active(mmu_.sram_rom3())` re-sync after `divmmc_.load_state(r)` in `Emulator::load_state`. ~14 lines (incl. comment).
