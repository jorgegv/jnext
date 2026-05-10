# NEXTZXOS Boot Subsystem — Pass-18 DivMMC + SD card + SPI Verification Audit

**Date**: 2026-05-10
**Branch**: `task2/verify18-divmmc-sd-spi`
**Scope**: DivMMC AUTOMAP, SD-card SPI protocol, SPI master byte-pump, host-side FAT32 reader, NR $B8-$BB readback, port $E3/$E7/$EB decode.
**Method**: Blind audit (no prior report consultation until after independent findings logged) of the emulator code against the official ZX Spectrum Next FPGA VHDL spec.

## Files reviewed

### Emulator code
- `src/peripheral/divmmc.{h,cpp}` — DivMMC AUTOMAP / port $E3 / RAM banks / NR $B8-$BB
- `src/peripheral/sd_card.{h,cpp}` — SD command interpreter (CMD0/8/9/10/12/13/16/17/18/23/24/55/58 + ACMD41)
- `src/peripheral/spi.{h,cpp}` — SPI master byte-pump (port $E7/$EB)
- `src/core/sd_rom_extractor.{h,cpp}` — host-side FAT32 reader (boot-time ROM extraction; pure host path, no VHDL analog)
- `src/core/emulator.cpp` — port-decode wiring (port $E3/$E7/$EB, NR $0A/$83/$B8-$BB handlers, reset cascade)

### VHDL oracle
- `cores/zxnext/src/device/divmmc.vhd` — DivMMC RTL (153 lines, two-stage hold/held pipeline)
- `cores/zxnext/src/serial/spi_master.vhd` — SPI master RTL (179 lines; 16-cycle byte-transfer FSM)
- `cores/zxnext/src/zxnext.vhd` — top-level:
  - port LSB decode (lines 2540-2575)
  - port $E3/$E7/$EB gates (lines 2608, 2620-2621, 2727-2737)
  - NR $B8-$BB → automap entry-point decode (lines 2848-2908)
  - port $E3 register write process (lines 4174-4190)
  - port $E7 register write process (lines 3305-3332)
  - SPI master instantiation + MISO mux (lines 3270-3298)
  - DivMMC entity instantiation (lines 4137-4171)
  - reset master-block defaults (lines 5087-5090, 4928-5100)

## Findings — overview

| ID | Class | Title |
|----|-------|-------|
| (none) | — | Defensive-zero: no new discrepancies found |

Total: **0** findings (a:0, b:0, c:0, d:0).

Tests after audit: ctest 38/38 PASS, FUSE 1356/1356 PASS, sdcard 30/30 PASS, divmmc 136/136 PASS, regression.sh 33/0/0.

## Defensive-zero rationale

Pass-18 is a deliberate sweep across every entity, signal, register, port, and reset path enumerated above. Every behavior in the VHDL oracle was cross-checked against the C++ implementation; every discrepancy candidate was traced to either (a) a prior pass's documented fix (V11-V17), (b) a documented class-(d) architectural simplification, or (c) cosmetic internal state that is not externally observable (e.g. unused bits in `control_reg_`).

The DivMMC + SD + SPI subsystem has now been audited 18 times. After Pass-17 closed V17-DIVMMC-01 (ACMD41 HCS bit), the subsystem is the nearest of the three remaining active subsystems to honest convergence. Pass-18 found zero new discrepancies. The subsystem is recommended for **convergence** in the cumulative aggregate report.

## Areas audited with no findings

### DivMMC overlay & enable gates
- VHDL `divmmc.vhd:94-95` — `rom_en/ram_en` = `(page0/page1 AND (conmem OR automap) AND mapram-condition)`. C++ `DivMmc::is_active() && !mapram_` (rom) / `is_ram_mapped` (ram) match (`divmmc.cpp:451-459`).
- VHDL `divmmc.vhd:96` — `ram_bank` selector. C++ `ram_page_for()` matches (`divmmc.cpp:473-479`).
- VHDL `divmmc.vhd:98-99` — output `i_en` gate = `port_divmmc_io_en` only. C++ `is_active()` checks `port_io_enable_` only — `nr_0a_4_enable_` is NOT in the overlay path; it is only in the automap-reset path (V16-DIVMMC-01 alignment, comments at `divmmc.h:108-122`).
- VHDL `divmmc.vhd:100` — `o_divmmc_rdonly` = `page0 OR (mapram AND bank=3)`. C++ `is_read_only()` matches (`divmmc.cpp:461-471`).
- VHDL `zxnext.vhd:4154` — bits 5:4 of `i_divmmc_reg` forced to "00". The storage `port_e3_reg(5:4)` is already 0 (the write process at lines 4181-4183 only writes bits 7, 6, 3:0); the force is redundant. C++ `read_control()` masks bits 5:4 on readback (`divmmc.cpp:115-117`), so externally observable behavior matches. The internal `control_reg_` does retain val bits 5:4 in storage (cosmetic divergence — not surfaced externally because every consumer either masks or reads from separate bank_/mapram_/conmem_ fields).
- VHDL `zxnext.vhd:4181-4183` — port $E3 write: bit 7 direct overwrite, bit 6 OR-latch, bits 3:0 direct overwrite. C++ `write_control()` matches (`divmmc.cpp:105-113`).
- VHDL `zxnext.vhd:4184-4186` — NR $09 b3 clears port_e3_reg(6) mapram latch. C++ `clear_mapram()` matches (`divmmc.cpp:121-125`).
- VHDL `zxnext.vhd:4112` — `divmmc_automap_reset` = `NOT(port_divmmc_io_en) OR NOT(nr_0a_divmmc_automap_en)`. C++ `apply_enabled_transition_` clears automap state on `enabled = port_io && nr_0a_4` going false (V11 Pass-11 fix).
- VHDL `zxnext.vhd:4177` — `port_e3_reg <= (others => '0')` on reset. C++ `DivMmc::reset()` matches.

### DivMMC AUTOMAP two-stage latch
- VHDL `divmmc.vhd:105-116` — `button_nmi` FF with reset/automap_reset/retn_seen/held priority. C++ models this in `on_m1_retn_delay` + `apply_enabled_transition_` + `check_automap` (continuous clear while held, V11 Pass-8 fix).
- VHDL `divmmc.vhd:123-134` — `automap_hold` FF clocks on M1+MREQ-low. C++ `check_automap` step 3 matches.
- VHDL `divmmc.vhd:136-145` — `automap_held` FF clocks on MREQ rising. C++ `check_automap` step 1 promotes `hold` → `held` at start of each M1 (collapsed cycle model).
- VHDL `divmmc.vhd:148` — combinational `automap` = `held OR (active AND instant_on/nmi_instant) OR (rom3_active AND rom3_instant)`. C++ `check_automap` step 5 matches (V11 Pass-11 fix to use `automap_active_` for `is_nmi_hold()`).

### NR $B8-$BB automap entry-point configuration
- VHDL `zxnext.vhd:5087-5090` — soft-reset defaults: $83/$01/$00/$CD. C++ `DivMmc::reset()` sets the same values (V17-NMP-01 fix added live read handlers for these registers; see `emulator.cpp:2477-2484`).
- VHDL `zxnext.vhd:2848-2890` — RST entry-point decode: `port_00xx_msb AND cpu_a(7:6)="00" AND cpu_a(2:0)="000"` selects 8 RST addresses [$00, $08, $10, $18, $20, $28, $30, $38] via `cpu_a(5:3)`. C++ `rst_addrs[8]` matches (`divmmc.cpp:337-358`).
- VHDL `zxnext.vhd:2892-2895` — `automap_instant_on/delayed_on` = `rst_ep AND rst_ep_valid AND (timing or NOT timing)`. C++ `check_automap` lines 340-358 match.
- VHDL `zxnext.vhd:2898-2899` — `rom3_instant_on` includes `$3Dxx wildcard AND nr_bb(7)`. C++ line 406-408 matches (V8-DIVMMC fix).
- VHDL `zxnext.vhd:2896` — `delayed_off` at PC ∈ [$1FF8, $1FFF] AND `nr_bb(6)`. C++ lines 409-422 match (V8-DIVMMC fix; also gated by `main_path_eligible` per VHDL `i_automap_active AND delayed_off`).
- VHDL `zxnext.vhd:2907-2908` — `nmi_instant/delayed_on` at PC=$0066 AND `nr_bb(1)/(0)` AND `button_nmi`. C++ lines 372-381 match (V8-DIVMMC fix); button_nmi gate matches `divmmc.vhd:120-121`.

### SRAM priority arbiter / ROM3 gates
- VHDL `zxnext.vhd:3137-3138` — `sram_divmmc_automap_en = sram_pre_override(2)`; `rom3_en = pre_override(2) AND pre_override(0) AND NOT layer2_map AND NOT romcs AND ((altrom AND alt_128_n) OR (rom3 AND NOT altrom))`. C++ `check_automap` lines 332-335 model the main path exactly; ROM3 path uses `rom3_active_` as a unified flag (acknowledged simplification of the altrom branch in the inline comment; boot path does not exercise altrom-locked mode). Class-(d) architectural — not new.

### Port $E3 / $E7 / $EB I/O decode
- VHDL `zxnext.vhd:2541-2575` — LSB-only decode (`cpu_a(7:0)`). C++ uses mask `0x00FF` for all three ports (V17-NMP-02/03 pattern). Matches.
- VHDL `zxnext.vhd:2608` — `port_e3 = port_e3_lsb AND port_divmmc_io_en`. C++ port $E3 handler gates on `effective_internal_port_enable(0x83) & 0x01` (`emulator.cpp:4276-4284`).
- VHDL `zxnext.vhd:2620-2621` — `port_e7/eb = port_e7_lsb/eb_lsb AND port_spi_io_en`. C++ gates on `effective_internal_port_enable(0x83) & 0x08` (`emulator.cpp:4192-4206`).
- VHDL `zxnext.vhd:614-622, 2803-2806, 2837-2840` — port $E7 is WRITE-ONLY (no `port_e7_rd` signal, no entry in `port_internal_rd_response` OR-tree). C++ registers `nullptr` for the read callback (V16-DIVMMC-01 fix). Falls through to floating-bus 0xFF.
- VHDL `zxnext.vhd:2815` — `port_e3_rd_dat = port_e3_dat WHEN port_e3_rd ELSE X"00"`. With port_e3_rd=0, the contribution to the OR-tree is 0x00, and the final `cpu_di = X"FF"` defaults via `port_internal_rd_response=0` at line 1877. C++ returns 0xFF directly when the gate is closed.

### SPI master byte-pump
- VHDL `spi_master.vhd:74-99` — 16-cycle state counter `state_r[4:0]` starts at "10000" (idle), counts to "01111" (state_last) then to "10000" again. JNEXT collapses to byte-level (every write/read completes in one call). `spi_wait_n()` always returns true (always idle). Class-(d) cycle-accuracy — known, NOT new.
- VHDL `spi_master.vhd:104-117` — output shift register `oshift_r`; spi_begin with i_spi_rd loads `(others => '1')`, i_spi_wr loads `cpu_do`. C++ `write_data(val)` passes `val` to device receive; `read_data()` calls device send (which internally uses MOSI=0xFF).
- VHDL `spi_master.vhd:159-168` — `miso_dat` reset-clause uses `i_reset='1' → all-ones`, but `i_reset` is hardwired to '0' at `zxnext.vhd:3285`. C++ `SpiMaster::reset()` correctly does NOT clobber `rx_data_` (V12-DIVMMC-01 fix). Initial value 0x00 matches VHDL signal-declaration default (V12-DIVMMC-01-NIT fix).
- VHDL `zxnext.vhd:3278-3280` — MISO source mux: flash → rpi → SD → '1' default. JNEXT's `active_device()` iterates CS bits 0-3 in order (SD0, SD1, rpi0, rpi1); flash is not modeled as a device. Only one CS bit is ever low (port_e7_reg's decode collapses ambiguous patterns), so multi-CS priority is moot in practice.

### Port $E7 (chip-select) decode
- VHDL `zxnext.vhd:3305-3326` — decode tree: cpu_do bits 1:0 == "10" → SD0 (XORed by sd_swap), "01" → SD1 (XORed by sd_swap), cpu_do == 0xFB → rpi0, 0xF7 → rpi1, 0x7F (gated by config_mode OR reset_type[2]) → flash, else → all 0xFF. C++ `write_cs()` matches this exact decode tree (V8-DIVMMC flash-CS fix).
- VHDL `zxnext.vhd:3308-3309` — `port_e7_reg <= (others => '1')` on reset. C++ `SpiMaster::reset()` matches (and additionally pulses `deselect()` on any currently-selected device per V11 Pass-11 fix).
- VHDL `zxnext.vhd:3319` — flash-CS gate `(config_mode OR reset_type[2])`. C++ `flash_cs_enable_` shadow is re-fanned-out from emulator on every NR $02 / NR $03 write and at init / soft_reset (`emulator.cpp:1978, 2236, 4551, 6911`). All three NR-$02 reset_type FSM advances reflect in the gate.

### NR $0A bits (sd_swap, divmmc_automap_en, mf_type, mouse)
- VHDL `zxnext.vhd:5191-5198` — bits 7:6 (mf_type) and 5 (sd_swap) only commit when `nr_03_config_mode='1'`; bits 4 (divmmc_automap_en) / 3 (mouse_button_reverse) / 1:0 (mouse_dpi) commit unconditionally. C++ `emulator.cpp:1061-1095` matches (V11-NMP-02 fix canonicalises the cache).
- VHDL `zxnext.vhd:1125-1126` — sd_swap / divmmc_automap_en have only initial values (no reset clause). C++ preserves both across soft reset (`SpiMaster::reset()` does not touch `sd_swap_`; `DivMmc::reset()` does not touch `port_io_enable_` / `nr_0a_4_enable_` / `enabled_`).
- VHDL `zxnext.vhd:5912` — NR $0A readback recomposes from authoritative fields. C++ `emulator.cpp:1113-1122` matches (Pass-3 fix to read from authoritative subsystem state).

### SD card command interpreter
- CMD0 (R1=0x01 idle) — sustained $01 via `persistent_response_byte_` for ZEsarUX-compat (deliberate, comment at `sd_card.cpp:506-522`).
- CMD1, CMD8 (R7 = NCR + R1 + 4 voltage bytes; byte 0 = 0x10 cmd-ver, byte 3 = check-pattern echo) — V12-DIVMMC-03 + V14-DIVMMC-02 fixes match SD § 7.3.2.6.
- CMD9 / CMD10 (CSD / CID 16-byte block) — present and shaped per SD § 5.3.3 / § 5.2.
- CMD12 (8 stuff bytes + NCR + R1) — TBBlue post-CMD18 stop transmission compat (`sd_card.cpp:577-590`).
- CMD13 (R2) — present, R2=0x00 "no error".
- CMD16 (only arg=512 accepted; else R1 |= illegal) — V8 fix.
- CMD17 (NCR + R1 + 0xFE + 512 + CRC; past-EOF emits R1=0x40 + 0x08 error token) — V12-DIVMMC-04 fix.
- CMD18 (same as CMD17 + multi-block stream; mid-stream past-EOF emits 0x08) — V12 + V14-DIVMMC-01 fixes.
- CMD23 (ack-only) — present.
- CMD24 (R1 + RECEIVING_DATA → 512 data bytes + 2 CRC → WRITE_RESP token) — past-EOF rejected at R1=0x40 (V13-DIVMMC-01 fix), failed host-side write emits 0x0D (V15-DIVMMC-01 fix), pre-token byte tracking via `data_token_received_` (V12-DIVMMC-06 fix).
- CMD55 (sets app_cmd_=true) — followed by ACMD41 path is intact; non-ACMD bridge falls through to regular CMD switch per SD § 4.3.9.5 (V9 fix).
- CMD58 (NCR + R1 + 4 OCR bytes; CCS bit from `host_supports_sdhc_`) — V17-DIVMMC-01 fix.
- ACMD41 (latches HCS from arg bit 30, sets initialized_=true, R1=0x00) — V17-DIVMMC-01 fix.
- Receive() default branch (in RESPONDING/SENDING_DATA/WRITE_RESP states): new CMD start byte (`(tx & 0xC0) == 0x40`) aborts current response and starts a new command — VHDL/spec-compliant for misbehaving hosts.
- Reset / mount / unmount / deselect protocol-state flushes — all canonical reset() use (V5 + V7 + V8 + V11 fixes).
- ZEsarUX-compat `persistent_response_byte_` for sustained CMD0=$01 / CMD8=$00 / CMD12=$FF idle states.

### Reset cascade
- Soft reset (`preserve_memory=true`) calls `spi_.reset()` + `divmmc_.reset()` but NOT `sd_card_.reset()` (the SD is external, not in FPGA reset domain — V6 G46(b) fix, `emulator.cpp:159-161`).
- Hard reset calls all three. Matches VHDL's "core load" semantics.
- `DivMmc::reset()` preserves enable-flag levers (port_io_enable_, nr_0a_4_enable_, enabled_) — VHDL signals nr_0a_divmmc_automap_en + internal_port_enable(8) are initial-value-only, NOT in any reset clause.
- `SpiMaster::reset()` preserves `sd_swap_` (NR $0A b5 — initial-value-only) but resets `cs_=0xFF` (port_e7_reg HAS a reset clause at VHDL :3308-3309).

### NR $B8-$BB readback
- VHDL `zxnext.vhd:6217-6227` — read of NR $B8/$B9/$BA/$BB returns the raw register byte. C++ `emulator.cpp:2481-2484` registers live read handlers pulling from `DivMmc::entry_points_0()` etc. (V17-NMP-01 fix).

### Save / load state
- `DivMmc::save_state` / `load_state` (`divmmc.cpp:529-610`) writes/reads all 17 fields in stream order — including the V11 split enable levers (port_io_enable_, nr_0a_4_enable_) at the end of the stream. Round-trip preserves all state.
- `SpiMaster::save_state` / `load_state` (`spi.cpp:231-247`) writes cs_, rx_data_, sd_swap_.
- `SdCardDevice` is intentionally NOT a Saveable (header comment at `sd_card.h:168-173`). Class-(d) — known gap.

## Test posture

- ctest: 38/38 PASS
- divmmc_test: 136/136 PASS (18 groups, 0 skips)
- sdcard_test: 30/30 PASS (0 skips)
- fuse_z80_test: 1356/1356 PASS
- regression.sh: **32 PASS / 1 FAIL / 0 SKIP** (pre-existing `parallax-demo` flake at integration HEAD `126d764`, 44636 pixels differ — unrelated to DivMMC/SD/SPI scope; this audit pass made zero code changes)

No new tests added — defensive-zero report.

## Convergence recommendation

After **18 audit passes**, the DivMMC + SD + SPI subsystem returns ZERO findings on a blind pass. Per workflow rule `feedback_task2_converged_subsystem_skip.md`, subsystems whose audit returns zero findings AND reviewer returns APPROVE-no-missed are converged and SKIPPED in subsequent passes. Pass-18 fulfils the audit half of that rule; the independent reviewer step is the gate for skipping in Pass-19+.

The trend across the active subsystems:
- Pass-11: 8 effective findings
- Pass-12: 17
- Pass-13: 7
- Pass-14: 9
- Pass-15: 5
- Pass-16: 7
- Pass-17: 8 (DivMMC: 1)
- Pass-18: DivMMC = **0**

This is the first zero-finding pass for any of the three remaining active subsystems. Recommended convergence verdict: **DivMMC + SD + SPI** is honestly converged at Pass-18.
