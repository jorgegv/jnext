# NEXTZXOS Boot Subsystem — Pass-17 DivMMC + SD card + SPI Independent Review

**Date**: 2026-05-10
**Reviewer branch**: `task2/verify17-divmmc-sd-spi-reviewer`
**Audit branch**: `task2/verify17-divmmc-sd-spi` (HEAD `1864a20`)
**Scope**: independent verification of V17-DIVMMC-01 (ACMD41 HCS bit reflected
in CMD58 OCR CCS) + blind re-audit of `divmmc.{h,cpp}`, `sd_card.{h,cpp}`,
`spi.{h,cpp}`, `sd_rom_extractor.{h,cpp}`, related port-decode wiring in
`emulator.cpp`, and the VHDL oracle in `cores/zxnext/src/{device/divmmc.vhd,
serial/spi_master.vhd, zxnext.vhd}`.

## 1. Verification of audit's fix (V17-DIVMMC-01)

### Spec match

SD Physical Layer Simplified Spec § 4.2.3 / § 5.1: ACMD41's argument bit 30 is
the Host Capacity Support flag (HCS). The card's CCS bit (OCR bit 30, surfaced
in CMD58 R3 byte 0 bit 6) MUST reflect HCS — `HCS=0 → CCS=0` (SDSC mode);
`HCS=1 → CCS=1` (SDHC mode). The fix correctly latches HCS in
`acmd41_sd_send_op_cond()` from `cmd_arg() & 0x40000000` into a new
`host_supports_sdhc_` field, then sources CCS in `cmd58_read_ocr()` from this
field. `reset()` clears it. `mount()` and `unmount()` route through `reset()`.

The post-fix OCR composition (`bit 31 = power-up done if initialized_; bit 30 =
host_supports_sdhc_`) matches spec faithfully. byte 1 = 0xFF (voltages
2.8-3.6 V), byte 2 = 0x80 (2.7-2.8 V), byte 3 = 0x00 — all already in place
pre-fix and unchanged.

### Discriminative-test verification (independent revert)

I reverted the fix to the pre-fix shape on disk (`uint8_t ocr0 = initialized_
? 0xC0 : 0x00`), keeping SD-29 (the discriminative test) intact. Rebuilt and
ran sdcard_test:

```
FAIL SD-29: ... [r1_hcs0=0 ocr0_hcs0=0xC0 r1_hcs1=0 ocr0_hcs1=0xC0]
Total:   30  Passed:   29  Failed:    1
```

The pre-fix code emits `ocr0_hcs0=0xC0` after ACMD41 with HCS=0; SD-29 expects
`(ocr0_hcs0 & 0xC0) == 0x80`. Confirmed FAIL.

Re-applied the fix. SD-29 now PASSes:

```
Total:   30  Passed:   30  Failed:    0
```

The test is genuinely discriminative — not a tautological assertion. The
HCS=1 leg in SD-29 is the symmetric "happy path" guard that confirms the fix
doesn't regress the existing TBBlue boot path.

### Test-suite verification

Release build, 38/38 ctest, 1356/1356 FUSE, 30/30 sdcard, 136/136 divmmc. All
PASS. Build is clean (no warnings).

### Verdict on the fix

**Correct.** Spec-faithful, surgical (40 added lines of code + 70 lines of
test), preserves all prior behaviour, and the test is properly discriminative.

## 2. Independent re-audit — areas exhaustively reviewed

### DivMMC overlay (`divmmc.{h,cpp}` + zxnext.vhd:2848-2908,4108-4190)

- **Port $E3 control register** — bit 7 conmem (immediate write per VHDL :4180-4181), bit 6 mapram (sticky-OR per :4182-4183), bits 3:0 bank field (:4185), bits 5:4 forced to '0' on read (`& 0xCF`, matching VHDL :4190 `port_e3_dat <= port_e3_reg(7:6) & "00" & port_e3_reg(3:0)`). No discrepancy.
- **NR $09 bit 3 mapram clear** — `clear_mapram()` matches VHDL :4184-4185.
- **AUTOMAP entry-point decode** — RST $00-$38 (NR $B8 + NR $B9 valid + NR $BA timing), NMI $0066 (NR $BB bits 1:0), tape traps $04C6/$0562/$04D7/$056A (NR $BB bits 2-5), $3Dxx wildcard (NR $BB bit 7), off-range $1FF8-$1FFF (NR $BB bit 6) — verified against VHDL :2848-2908. The C++ collapses VHDL's bitwise indexed mux into an explicit per-RST branch, but the address match (PC ≡ rst_addr) is mathematically equivalent to VHDL's `port_00xx_msb=1 AND a(7:6)=00 AND a(2:0)=000 AND a(5:3)=N`.
- **Path-eligibility gates** — `main_path_eligible = sram_pre_override_2`, `rom3_path_eligible = sram_pre_override_2 AND sram_pre_override_0 AND !layer2_map_read AND rom3_active` matches the VHDL composite at :3137-3138.
- **Two-stage automap latch** (hold→held promotion on MREQ rising edge) — the in-place model (latch hold→held; decode; update hold; compute combinational active) accurately models divmmc.vhd:123-148.
- **`button_nmi` cleared while held=1** — divmmc.vhd:112-113 clauses modelled (Pass-8 fix carried; verified inline at line 294-299).
- **NR $0A bit 4 / port_io enable interaction** — DA-08 / NA-03 split levers correctly clear automap latches on enabled→disabled edge per VHDL :4112 i_automap_reset path.
- **RETN delayed clear** — G46(a) one-M1-cycle delay register matches VHDL :108,126,139 i_retn_seen pulse.
- **DivMMC ROM/RAM read-only semantics** — slot 0 always RO, slot 1 RO only when (mapram=1 AND bank=3) — matches VHDL :100. (The `is_read_only()` accessor is reachable via the test plan only — runtime uses `write()`'s internal RO check at lines 510 and 522.)
- **`ram_bank` forced to 3 for slot 0** — matches VHDL :96.
- **CONMEM-only path bypassing nr_0a_4_enable** — `is_active()` correctly gates only on `port_io_enable_`, NOT the composite `enabled_`, matching VHDL where `i_en` (= port_divmmc_io_en) is the sole gate on `o_divmmc_rom_en`/`o_divmmc_ram_en` and the `nr_0a_4` lever drives only the automap-reset path.
- **load_state re-pushes** — `rom3_active_` re-pushed from `mmu_.sram_rom3()`; `layer2_map_read_` and `button_nmi_` directly serialised. No missed seam.

### SD card protocol (`src/peripheral/sd_card.{h,cpp}`)

- **Full SPI command interpreter state machine** (IDLE / RECEIVING_CMD / RESPONDING / SENDING_DATA / RECEIVING_DATA / WRITE_RESP) modelled correctly, all transitions verified.
- **CMD0 / CMD8 / CMD55 / ACMD41 init sequence** — full R7 with byte 0 = 0x10 (cmd version 1) and R1 reflecting `initialized_` (V12-DIVMMC-03 + V14-DIVMMC-02 carried).
- **CMD17 single-block read** — past-EOF returns R1=0x40 PARAMETER_ERROR + 0x08 data error token (V12-DIVMMC-04 carried).
- **CMD18 multi-block read** — initial-block past-EOF + mid-stream past-EOF both emit 0x08 (V14-DIVMMC-01 carried). CMD12 / CS-deassert abort streams correctly.
- **CMD24 single-block write** — past-EOF rejected at R1 (V13-DIVMMC-01); RO-mounted image emits 0x0D (V15-DIVMMC-01); 0xFE token waiting (V12-DIVMMC-06).
- **CMD16 SET_BLOCKLEN** — 512 accepted, anything else illegal-command (V12-DIVMMC-04 + V14-DIVMMC-02 carried).
- **CMD9 / CMD10 SEND_CSD/CID** — 16-byte responses with valid CSDv2 layout from `file_size_`.
- **CMD58 READ_OCR** — fixed by V17-DIVMMC-01 (this pass).
- **CMD55 → non-ACMD fallthrough** — V14 spec-faithful for non-ACMD41 commands.
- **CMD12 stuff-byte preamble** — TBBlue firmware-specific (8 stuff bytes before NCR+R1) carried.
- **CMD13 SEND_STATUS R2** — 2-byte response (R1 + status byte) carried.
- **Default unhandled CMD → R1 illegal** (Pass-8 fix carried).
- **deselect / reset / mount / unmount cleanup** — full state reset including all FSM flags. The new `host_supports_sdhc_` field added to V17 fix is correctly cleared in `reset()` (and propagated to `mount()`/`unmount()` via reset() calls). Across deselect, `host_supports_sdhc_` persists — symmetric with `initialized_`, matching real-card semantics where init state survives CS toggling and only ACMD41 / mount-cycle re-latches HCS.
- **Mid-response abort on new-CMD start byte** — the `default:` branch in `receive()` correctly resets `multi_block_*` / `pending_write_after_r1_` / `data_token_received_` / `persistent_response_byte_` when a 0x40-0x7F byte arrives during RESPONDING/SENDING_DATA/WRITE_RESP. `app_cmd_` is intentionally NOT reset here so a CMD55 → CMDxx sequence still routes the next CMD as ACMD via process_command(); deselect() always clears `app_cmd_` for the safe boundary. Robust.
- **CRC reception** — 2 trailing CRC bytes ignored on CMD24 RECEIVING_DATA (spec § 7.2.2 CRC optional in SPI mode unless CMD59 enables it).

### SPI master (`src/peripheral/spi.{h,cpp}`)

- **port_e7 decode** — VHDL :3311-3322 SD-swap math (cpu_do(1:0)="10" → 0xFE/0xFD per swap; cpu_do(1:0)="01" → 0xFD/0xFE per swap; $FB → RPi0; $F7 → RPi1; $7F → Flash gated by config_mode | reset_type b2; else $FF) all match.
- **port_e7 write-only** — V16-DIVMMC-01 fix carried (port_e7_rd does not exist in VHDL; emulator passes nullptr for read handler at emulator.cpp:4151-4156).
- **port_eb full-duplex pipeline** — Read returns prev miso_dat, captures new = dev->send() with MOSI=$FF (matches VHDL :109-110 oshift_r := all-1s on i_spi_rd). Write captures new = dev->receive(val).
- **`miso_dat` / `rx_data_` initial value 0x00** — VHDL declares `:= (others => '0')` (line 74); V12-DIVMMC-01 fix carried (zxnext.vhd hardwires `i_reset => '0'` at :3285, so spi_master.vhd's reset clauses never fire — the bitstream-load default 0x00 persists forever).
- **`reset()` does not clobber `rx_data_`** — V12-DIVMMC-01 fix; matches VHDL `i_reset` hardwired to '0' at zxnext.vhd:3285.
- **`reset()` calls deselect on previously-selected devices** — Pass-11 fix carried.
- **`flash_cs_enable` composite gate** — set externally on NR 0x03 / NR 0x02 changes + init() + load_state. V8 fix carried. load_state re-syncs the gate from the canonical loaded NR state at emulator.cpp:6870.
- **NR 0x83 b3 (port_spi_io_en) gate on port $E7 / $EB** — emulator gates both read and write per `effective_internal_port_enable(0x83) & 0x08` (lines 4154,4159,4163).
- **NR 0x83 b0 (port_divmmc_io_en) gate on port $E3** — `effective_internal_port_enable(0x83) & 0x01` (lines 4237,4241).
- **Internal port enable AND-mask** when expbus_eff_en=1 (V16-NMP-02 carried — used for port_divmmc_io_en + port_spi_io_en gates).

### Host-side FAT32 reader (`src/core/sd_rom_extractor.{h,cpp}`)

- **MBR + BPB + FAT chain walk** — V16-DIVMMC-02 directory cycle DOS hardening carried (`max_chain_len` for both directory and file chain walks).
- **8.3 short-name lookup** — case-insensitive, 0x05→0xE5 alias, LFN/volume label skip — all spec-correct.
- **File chain walk bound** — `max_chain_len = (file_size / bytes_per_cluster) + 2` defends against pathological cycles.
- **Out-of-image seek/read** — `read_sectors()` returns false on short read; `fat_next()` returns 0 on short read; both propagate as "not found" or "premature EOC".

### Class-(d) item (listed only)

V17-DIVMMC-02 cycle-accurate SPI master FSM — confirmed listed not fixed.
This is the same scope as the existing G137 plan. **Class-(d) — DO NOT FIX**.

## 3. Missed findings

**None.**

I exhaustively re-audited the four files in scope plus the relevant
`emulator.cpp` port-decode and `mmu.cpp` feeder sites, and cross-referenced
every observable behaviour against the VHDL oracle. The audit's single
class-(c) finding (V17-DIVMMC-01) is the only spec-divergent behaviour I
found in this subsystem at Pass-17.

Cross-cutting recurring families verified absent:
- NR cache-leak — none (all NR accessors that need fan-out fan out at write).
- multi-writer fan-out — none (NR $B8/$B9/$BA/$BB write handlers fan out).
- WO-NR readback — none (port $E7 made write-only at V16; no NR in scope is WO).
- default-FF — none (NR shadows in scope all default to spec-correct values).
- past-EOF — covered for CMD17 / CMD18 / CMD24 (V12-04 / V13-01 / V14-01 / V15-01).
- load_state shadow re-push — `rom3_active_` re-pushed; `flash_cs_enable_` re-pushed; `layer2_map_read_` / `button_nmi_` directly persisted; `host_supports_sdhc_` is direct (latched by ACMD41), no shadow seam.
- port-decode AND-mask — V16-NMP-02 effective_internal_port_enable helper used at all $E3 / $E7 / $EB gating sites.

## 4. Convergence status

The audit returned **1 finding** (1c). Per the converged-subsystem-skip rule
(2026-05-10) a subsystem is converged only when audit returns ZERO findings
AND reviewer returns APPROVE-no-missed. **DivMMC + SD + SPI is NOT yet
converged**; it stays in Pass-18 scope.

**Subsystem trajectory** (effective findings, audit-only counts):
Pass-11 = 8, Pass-12 = 17, Pass-13 = 7, Pass-14 = 9, Pass-15 = 5, Pass-16 = 7,
Pass-17 = 1. The descending trend is clear; one more pass with zero findings
will mark convergence.

## 5. Verdict

**APPROVE.**

- Audit's V17-DIVMMC-01 fix verified (revert-then-test confirmed
  discriminative; reapply confirmed PASS).
- All test suites pass on Release build (ctest 38/38, FUSE 1356/1356,
  sdcard 30/30, divmmc 136/136).
- No missed findings of class-a / b / c.
- One class-(d) item (V17-DIVMMC-02) confirmed listed-only per G137 scope.
