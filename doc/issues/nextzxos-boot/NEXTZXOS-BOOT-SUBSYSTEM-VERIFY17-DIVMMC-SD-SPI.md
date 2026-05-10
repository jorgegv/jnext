# NEXTZXOS Boot Subsystem — Pass-17 DivMMC + SD card + SPI Verification Audit

**Date**: 2026-05-10
**Branch**: `task2/verify17-divmmc-sd-spi`
**Scope**: DivMMC AUTOMAP, SD-card SPI protocol, SPI master byte-pump, host-side FAT32 reader.
**Method**: Blind audit (no prior report consultation) of the emulator code against the official ZX Spectrum Next FPGA VHDL spec.

## Files reviewed

### Emulator code
- `src/peripheral/divmmc.{h,cpp}` — DivMMC AUTOMAP / port $E3 / RAM banks
- `src/peripheral/sd_card.{h,cpp}` — SD command interpreter
- `src/peripheral/spi.{h,cpp}` — SPI master byte-pump
- `src/core/sd_rom_extractor.{h,cpp}` — host-side FAT32 reader
- `src/core/emulator.cpp` — port-decode wiring (port $E3, $E7, $EB, NR $0A/$83/$B8-$BB)

### VHDL oracle
- `cores/zxnext/src/device/divmmc.vhd` — DivMMC RTL (153 lines)
- `cores/zxnext/src/serial/spi_master.vhd` — SPI master RTL (179 lines)
- `cores/zxnext/src/zxnext.vhd` — top-level: port-decode (lines 2540-2700), DivMMC wiring (lines 2848-2908, 4108-4190), SPI/CS wiring (lines 3270-3332)

## Findings — overview

| ID | Class | Title |
|----|-------|-------|
| V17-DIVMMC-01 | (c) | ACMD41 ignores HCS bit, CMD58 OCR CCS always reports SDHC |

Total: 1 finding (a:0, b:0, c:1, d:0).

Tests after fix: ctest 38/38 PASS, FUSE 1356/1356 PASS, sdcard 30/30 PASS, divmmc 136/136 PASS.

## V17-DIVMMC-01 — ACMD41 HCS bit not reflected in CMD58 OCR CCS bit

**Class**: (c) latent — TBBlue / NextZXOS / FatFs always set HCS=1 on the boot path.

**VHDL evidence**: SD Phys Layer Simplified Spec § 4.2.3 / § 5.1 (the SD card protocol is external to the FPGA core; spec is the oracle for SPI-mode SD card behavior).

The ACMD41 command's argument bit 30 is the **Host Capacity Support** flag (HCS):
- HCS=1: host supports SDHC/SDXC; the card may set CCS=1 in OCR.
- HCS=0: host supports only SDSC; the card MUST set CCS=0 in OCR even if it is internally SDHC.

The card surfaces CCS in CMD58's R3 response (OCR byte 0 bit 6).

**Pre-fix code** (`src/peripheral/sd_card.cpp` :903-907 + :883-901):
```cpp
void SdCardDevice::acmd41_sd_send_op_cond() {
    initialized_ = true;
    queue_r1(0x00);
}

void SdCardDevice::cmd58_read_ocr() {
    uint8_t ocr0 = initialized_ ? 0xC0 : 0x00;  // bit 30 (CCS) ALWAYS set
    resp_buf_ = { 0xFF, ..., ocr0, 0xFF, 0x80, 0x00 };
    ...
}
```

The emulator unconditionally reported CCS=1 regardless of the host's HCS request.

**Observable impact**: A strict-spec host that issues ACMD41 with HCS=0 (requesting SDSC compatibility mode) would observe CMD58 OCR with CCS=1 — diverging from real SDHC cards which report CCS=0 in this case (or stay in idle and never complete init). TBBlue / NextZXOS / FatFs always set HCS=1, so the divergence is class-(c) latent for the boot path. Strict spec validators or legacy MMC/SDSC-only Z80 firmware would see the divergence.

**Fix**: introduce a new `host_supports_sdhc_` field in `SdCardDevice`. Set it from `cmd_arg() & 0x40000000` in `acmd41_sd_send_op_cond()`. Source CCS in `cmd58_read_ocr()` from this field instead of the unconditional 0x40.

**Code change**:
- `src/peripheral/sd_card.h`: add `bool host_supports_sdhc_ = false;`, clear in `reset()`.
- `src/peripheral/sd_card.cpp` `acmd41_sd_send_op_cond()`: latch HCS from arg bit 30.
- `src/peripheral/sd_card.cpp` `cmd58_read_ocr()`: compute `ocr0 = (initialized_ ? 0x80 : 0x00) | (host_supports_sdhc_ ? 0x40 : 0x00)`.

**Discriminative test**: `test_sd_29_acmd41_hcs_reflected_in_ocr` in `test/sdcard/sdcard_test.cpp`.
- Leg 1: Init via CMD0 → CMD8 → CMD55 → ACMD41 with arg=0x00100000 (HCS=0). Then CMD58. Expect OCR byte 0 = 0x80 (bit 31 set, bit 30 clear). Pre-fix returns 0xC0.
- Leg 2 (symmetric guard): Re-init with HCS=1. Expect OCR byte 0 = 0xC0 (bit 31 + bit 30 set). Both pre and post-fix pass this leg.

Pre-fix run output: `FAIL SD-29: ... ocr0_hcs0=0xC0 (expected 0x80)`. Post-fix run output: PASS.

**Commit**: 5551397.

## Areas audited with no findings

The following areas were exhaustively reviewed against VHDL with no discrepancies found:

### DivMMC (`divmmc.vhd` + `zxnext.vhd:2848-2908, 4108-4190`)

- **Port $E3 control register** (write/read, bit 6 OR-latch, bits 5:4 forced to 0 on read, bank field [3:0]) — emulator's `write_control` / `read_control` matches VHDL `port_e3_reg` semantics including the sticky-OR on bit 6 and the `& 0xCF` read mask.
- **NR 0x09 bit 3 mapram clear** — `clear_mapram()` matches VHDL :4184-4185.
- **AUTOMAP entry-point decode** — RST $00-$38 (NR $B8 + NR $B9 valid + NR $BA timing), NMI $0066 (NR $BB bits 1:0), tape traps $04C6/$0562/$04D7/$056A (NR $BB bits 2-5), $3Dxx wildcard (NR $BB bit 7), off-range $1FF8-$1FFF (NR $BB bit 6) — all matched against VHDL :2848-2908.
- **AUTOMAP main vs ROM3 path gating** — `sram_pre_override(2)` and `sram_pre_override(0)` gates per VHDL :3137-3138 (DivMmc::check_automap signature).
- **Two-stage automap latch** (hold→held promotion on MREQ rising edge) — divmmc.vhd:123-148 modeled as step1 (held := hold), step2 (decode), step3 (hold update), step4 (active = held OR instant_match).
- **`button_nmi` cleared while held=1** — divmmc.vhd:112-113 modeled (Pass-8 fix carried).
- **NR $0A bit 4 / port_io enable interaction** — DA-08 / NA-03 split levers correctly clear automap latches on enabled→disabled edge per VHDL :4112 i_automap_reset.
- **RETN delayed clear** — G46(a) one-M1-cycle delay register matches VHDL :108,126,139 i_retn_seen pulse.
- **DivMMC ROM/RAM read-only semantics** — slot 0 always RO, slot 1 RO only when (mapram=1 AND bank=3) — matches VHDL :100.
- **`ram_bank` forced to 3 for slot 0** — matches VHDL :96.

### SD card protocol (`src/peripheral/sd_card.cpp`)

- **CMD0 / CMD8 / CMD55 / ACMD41 init sequence** — full R7 with byte 0 = 0x10 (cmd version 1) and R1 reflecting `initialized_` (V12-DIVMMC-03 + V14-DIVMMC-02 carried).
- **CMD17 single-block read** — past-EOF returns R1=0x40 PARAMETER_ERROR + 0x08 data error token (V12-DIVMMC-04 carried).
- **CMD18 multi-block read** — initial-block past-EOF + mid-stream past-EOF both emit 0x08 (V14-DIVMMC-01 carried). CMD12 / CS-deassert abort streams correctly.
- **CMD24 single-block write** — past-EOF rejected at R1 (V13-DIVMMC-01); RO-mounted image emits 0x0D (V15-DIVMMC-01); 0xFE token waiting (V12-DIVMMC-06).
- **CMD16 SET_BLOCKLEN** — 512 accepted, anything else illegal-command (V12-DIVMMC-04 + V14-DIVMMC-02 carried).
- **CMD9 / CMD10 SEND_CSD/CID** — 16-byte responses with valid CSDv2 layout from `file_size_`.
- **CMD58 READ_OCR** — fixed by V17-DIVMMC-01.
- **deselect / reset / mount / unmount cleanup** — full state reset including all FSM flags (Pass-5/Pass-8 carries).

### SPI master (`src/peripheral/spi.cpp`)

- **port_e7 decode** — VHDL :3311-3322 SD-swap math (cpu_do(1:0)="10" → 0xFE/0xFD per swap; cpu_do(1:0)="01" → 0xFD/0xFE per swap; $FB → RPi0; $F7 → RPi1; $7F → Flash gated by config_mode | reset_type b2; else $FF) all match.
- **port_e7 write-only** — V16-DIVMMC-01 fix carried (port_e7_rd does not exist in VHDL; emulator passes nullptr for read handler).
- **port_eb full-duplex pipeline** — Read returns prev miso_dat, captures new = dev->send() with MOSI=$FF (matches VHDL :109-110 oshift_r := all-1s on i_spi_rd). Write captures new = dev->receive(val).
- **`miso_dat` / `rx_data_` initial value** — VHDL declares `:= (others => '0')` (line 74); V12-DIVMMC-01 fix carried.
- **`reset()` does not clobber `rx_data_`** — V12-DIVMMC-01 fix; matches VHDL `i_reset` hardwired to '0' at zxnext.vhd:3285.
- **`reset()` calls deselect on previously-selected devices** — Pass-11 fix carried.
- **`flash_cs_enable` composite gate** — set externally on NR 0x03 / NR 0x02 changes + init() + load_state. V8 fix carried.
- **NR 0x83 b3 (port_spi_io_en) gate on port $E7 / $EB** — emulator gates both read and write per `effective_internal_port_enable(0x83) & 0x08`.
- **NR 0x83 b0 (port_divmmc_io_en) gate on port $E3** — `effective_internal_port_enable(0x83) & 0x01`.
- **Internal port enable AND-mask** when expbus_eff_en=1 (V16-NMP-02 carried).

### Host-side FAT32 reader (`src/core/sd_rom_extractor.cpp`)

- **MBR + BPB + FAT chain walk** — only bug found previously (V16-DIVMMC-02 directory cycle DOS hardening) is carried.
- **8.3 short-name lookup** — case-insensitive, 0x05→0xE5 alias, LFN/volume label skip — all spec-correct.
- **File chain walk bound** — `max_chain_len = (file_size / bytes_per_cluster) + 2` defends against pathological cycles.

## Class-(d) — listed only, not fixed

### V17-DIVMMC-02 (class-d, listed) — Cycle-accurate SPI master timing

VHDL `spi_master.vhd:82` specifies `spi_begin = '1' WHEN (state_last = '1' OR state_idle = '1') AND (i_spi_rd = '1' OR i_spi_wr = '1') ELSE '0'`. Mid-transfer requests (when neither state_last nor state_idle is high) are silently DROPPED. The DMA engine throttles via `spi_wait_n` to ensure 16-cycle separation between transfers.

The emulator's `SpiMaster` is a synchronous byte wrapper: every `write_data` / `read_data` call completes a full transfer in one logical step. Back-to-back accesses without 16-cycle gaps would silently drop transfers in VHDL but ALL succeed in jnext. `spi_wait_n()` always returns true.

**Class-(d)**: cycle-accurate FSM rewrite of the SpiMaster (multi-cycle state machine with pending_request handling and proper wait_n assertion) — this is the same scope as the existing G137 long-term plan. **DO NOT FIX**.

**Impact**: latent on the boot path (firmware uses proper delays / DMA-with-wait). Real Z80 software stress-testing against tight EB I/O timing would diverge.

## Test summary

Before fix:
- ctest: 38/38 PASS
- FUSE: 1356/1356 PASS
- sdcard: 29/29 PASS
- divmmc: 136/136 PASS

After fix:
- ctest: 38/38 PASS
- FUSE: 1356/1356 PASS
- sdcard: 30/30 PASS (new SD-29 added)
- divmmc: 136/136 PASS

Discriminative regression test SD-29 added in `test/sdcard/sdcard_test.cpp`. Pre-fix run: SD-29 FAIL (`ocr0_hcs0=0xC0`, expected 0x80). Post-fix run: SD-29 PASS.

## Conclusion

The DivMMC + SD + SPI subsystem is in good shape after Pass-16 fixes — only one class-(c) finding emerged in this exhaustive blind audit. The finding is fixed with a discriminative test. One class-(d) item is listed for future cycle-accurate SPI master work (G137).

**Subsystem trajectory**: Pass-11 had 8 effective findings, Pass-12 had 17, Pass-13 had 7, Pass-14 had 9, Pass-15 had 5, Pass-16 had 7. Pass-17 has 1. The descending trend continues; convergence is approaching but not yet asserted.
