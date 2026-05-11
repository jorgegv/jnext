# Pass-20 Verify-Audit Report — DivMMC + SD-Card + SPI Subsystem

**Date**: 2026-05-11  
**Branch**: `task2/verify20-divmmc-sd-spi` (off integration HEAD `deb7bdf`)  
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify20-divmmc-sd-spi`  
**Auditor**: blind audit agent (Pass-20)

## Findings summary

| ID | Class | Status | Description |
|----|-------|--------|-------------|
| V20-DIVMMC-01 | (c) latent | FIXED + test | CMD55 + ACMD41 R1 missing APP_CMD bit (bit 5) per SD Phys Layer Spec § 7.3.2.1 |
| V20-DIVMMC-D01 | (d) deferred | Pre-existing | SPI master cycle-accurate FSM not modelled (16-cycle/byte, `spi_wait_n` always-asserted) — G137 |
| V20-DIVMMC-D02 | (d) deferred | Pre-existing | `i_retn_seen` clears in divmmc.vhd:108/126/139 happen on the i_CLK_28 cycle after pulse; C++ `on_m1_retn_delay` collapses to next-M1 (~3 i_CLK_28 later) — functionally equivalent for boot (overlay still drops before returned-to instruction) |
| V20-DIVMMC-D03 | (d) deferred | Pre-existing | `button_nmi`-cleared-while-held=1 has a 1-clock latency in VHDL (held=0 in pre-edge sample) vs same-call in C++ — class-d, no boot impact |

Pass-20 yielded **1** class-(c) functional finding + **3** class-(d) architectural items already documented. Compared with Pass-19 (1 NIT + 1 class-d) the audit converges further on the boot-relevant surface — only one fresh genuine spec divergence (SD-spec, not VHDL).

## Tests

- `ctest --output-on-failure`: 38/38 PASS
- `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS
- `bash test/00regression/regression.sh`: 33/0/0 PASS
- `./build/test/sdcard_test`: 33/33 PASS (1 new test: SD-32)
- `./build/test/divmmc_test`: 137 plan rows, 0 fail, 0 skip

## Enumeration table — every surface in scope (Pass-20)

Granularity: one row per `SdCardDevice` command, per external port (read/write), per DivMmc NR (read/write), per save_state field, per SpiMaster public-API surface, per automap trigger PC.

### SdCardDevice command handlers

| Surface (file:line) | C++ behavior summary | VHDL/SD-spec oracle (file:line) | Match | Notes |
|---|---|---|---|---|
| sd_card.cpp:523 cmd0_go_idle | R1=0x01 (idle); init=false; persistent=0x01 | SD spec § 7.3.2.1 / § 4.2.1 | ✓ | sustained $01 mimics ZEsarUX for TBBlue compat |
| sd_card.cpp:517 cmd1_send_op_cond | R1=0x00 (ready); init=true | SD spec § 7.3.2.1 / § 4.2 | ✓ | MMC-legacy path; SDSC fast-init |
| sd_card.cpp:547 cmd8_send_if_cond | R7 = NCR + R1(idle/0) + 0x10 (cmd ver) + 0x00 + 0x01 (volt acc) + check echo | SD spec § 7.3.2.6 | ✓ | V12-DIVMMC-03 + V14-DIVMMC-02 fixes |
| sd_card.cpp:814 cmd9_send_csd | NCR + R1 + 0xFE + 16-byte CSD v2.0 + 2 CRC | SD spec § 5.3.3 | ✓ | C_SIZE derived from file_size_ |
| sd_card.cpp:871 cmd10_send_cid | NCR + R1 + 0xFE + 16-byte CID + 2 CRC | SD spec § 5.2 | ✓ | Synthetic CID (manufacturer 0x03, "SDJNEXT") |
| sd_card.cpp:586 cmd12_stop_transmission | 8 stuff bytes + NCR + R1; clears multi_block; persistent=0xFF | SD spec § 7.3.1.1 + TBBlue firmware | ✓ | persistent_response_byte_=0xFF avoids fake-busy |
| sd_card.cpp:614 cmd13_send_status | R2 = NCR + R1 + 0x00 (status byte) | SD spec § 7.3.2.2 | ✓ | Status byte = "no errors" |
| sd_card.cpp:625 cmd16_set_blocklen | arg=512 → R1(idle); arg≠512 → R1(idle|illegal) | SD spec § 4.9.1 / § 7.3.2.1 | ✓ | Pass-5 V5 fix |
| sd_card.cpp:656 cmd17_read_single_block | Past-EOF → R1 bit 6 + 0x08 token; in-range → NCR+R1+0xFE+512+CRC | SD spec § 7.3.2.1 + § 7.3.3.3 | ✓ | V12-DIVMMC-04 fix |
| sd_card.cpp:698 cmd18_read_multiple_block | Initial = CMD17-shape; mid-stream re-prime per block; past-EOF emits 0x08 | SD spec § 7.3.3 | ✓ | V14-DIVMMC-01 mid-stream past-EOF fix |
| sd_card.cpp:648 cmd23_set_block_count | R1(idle); hint ignored | SD spec § 7.3.1.1 | ✓ | Optional pre-erase hint |
| sd_card.cpp:744 cmd24_write_single_block | Past-EOF → R1 bit 6 (no data phase); ok → R1=0 + bridge to RECEIVING_DATA; data path emits 0x05/0x0D token after CRC | SD spec § 7.3.2.1 + § 7.3.3.3 + § 4.3.4 | ✓ | V13-DIVMMC-01 + V15-DIVMMC-01 fixes |
| sd_card.cpp:801 cmd55_app_cmd | R1=0x20 \| (initialized?0:1) — bit 5 (APP_CMD) set per V20-DIVMMC-01; app_cmd_=true | SD spec § 7.3.2.1 Table 7-9 | ✓ | **V20-DIVMMC-01 FIX** — pre-fix omitted bit 5 |
| sd_card.cpp:904 cmd58_read_ocr | NCR + R1 + 4-byte OCR (bit 31 init, bit 30 = HCS-gated CCS) | SD spec § 5.1 / § 4.2.3 | ✓ | V17-DIVMMC-01 HCS reflection |
| sd_card.cpp:939 acmd41_sd_send_op_cond | R1=0x20 — bit 5 (APP_CMD) set per V20-DIVMMC-01; HCS bit captured; init=true | SD spec § 4.2.3 / § 7.3.2.1 Table 7-9 | ✓ | **V20-DIVMMC-01 FIX** — pre-fix omitted bit 5 |
| sd_card.cpp:485 process_command default | Unhandled CMD → R1 (idle\|illegal) | SD spec § 7.3.2.1 bit 2 | ✓ | Pass-8 fix |

### SdCardDevice state-machine transitions

| State transition (file:line) | C++ behavior | SD-spec oracle | Match | Notes |
|---|---|---|---|---|
| sd_card.cpp:130 IDLE → RECEIVING_CMD on 0x40-0x7F | Capture cmd start; reset persistent | SD spec § 7.3.1.1 (cmd frame `01_cccccc`) | ✓ |  |
| sd_card.cpp:142 RECEIVING_CMD → process_command at 6 bytes | After 5 args + CRC | SD spec § 7.3.1.1 | ✓ | CRC ignored |
| sd_card.cpp:149 RECEIVING_DATA wait for 0xFE | Pre-token bytes (incl. 0xFF) ignored | SD spec § 7.3.3.2 | ✓ | V12-DIVMMC-06 flag-based fix |
| sd_card.cpp:182 RECEIVING_DATA → WRITE_RESP after CRC | Write data + emit 0x05/0x0D | SD spec § 7.3.3.3 | ✓ | V15-DIVMMC-01 RO-image fix |
| sd_card.cpp:265 default → RECEIVING_CMD on new CMD | Abort prior response; reset state | SD spec abort semantics | ✓ | ZEsarUX compat |
| sd_card.cpp:311 default → send() pass-through | Full-duplex stream advance | VHDL spi_master.vhd:104-168 | ✓ | V18-DIVMMC-NIT-01 fix |
| sd_card.cpp:84 deselect() → IDLE + state reset | Drops all transient state; preserves init | ZEsarUX mmc_cs | ✓ | Hardware CS-deassert behavior |
| sd_card.cpp:340 RESPONDING → IDLE / RECEIVING_DATA | After resp_buf_ drains; bridge to write phase via pending_write_after_r1_ | SD spec § 7.2.4 / § 7.3.3.1 | ✓ | CMD24 R1-then-data bridge |
| sd_card.cpp:383 SENDING_DATA multi-block re-prime | Load next sector; emit 0xFF gap + new 0xFE | SD spec § 4.3.4.1 | ✓ | Used by TBBlue FW boot |

### External ports

| Port (read/write) (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| emulator.cpp:4464 port 0xE3 write | Gated on NR 0x83 b0; divmmc_.write_control(v) | zxnext.vhd:2412/2608/4180-4187 | ✓ |  |
| emulator.cpp:4465 port 0xE3 read | Gated on NR 0x83 b0; divmmc_.read_control() | zxnext.vhd:2727+2815+4190 | ✓ |  |
| emulator.cpp:4380 port 0xE7 write | Gated on NR 0x83 b3; spi_.write_cs(val) | zxnext.vhd:2735+3308-3326 | ✓ | V16-DIVMMC-01 |
| emulator.cpp:4381 port 0xE7 read | nullptr → default 0xFF | zxnext.vhd:614-622 (no port_e7_rd) | ✓ | V16-DIVMMC-01 write-only |
| emulator.cpp:4386 port 0xEB write | Gated on NR 0x83 b3; spi_.write_data(val) | zxnext.vhd:2737+3287-3298 | ✓ |  |
| emulator.cpp:4387 port 0xEB read | Gated on NR 0x83 b3; spi_.read_data() | zxnext.vhd:2736+3287-3298 | ✓ |  |

### DivMMC-related NR registers

| NR (read/write) (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| emulator.cpp:1080 NR 0x0A write | Bits 7:6 gated by config_mode; b5 sd_swap; b4 nr_0a_4_enable; b3 mouse_rev; b1:0 dpi | zxnext.vhd:5191-5198 | ✓ | V11-NMP-02 cache-leak mask |
| emulator.cpp:1132 NR 0x0A read | Composed from authoritative subsystem state; bit 2 forced 0 | zxnext.vhd:5912 | ✓ | Pass-3 mf_type fix |
| emulator.cpp:2577 NR 0xB8 write | divmmc_.set_entry_points_0(v); unmasked | zxnext.vhd:5584-5585 | ✓ |  |
| emulator.cpp:2578 NR 0xB9 write | divmmc_.set_entry_valid_0(v); unmasked | zxnext.vhd:5587-5588 | ✓ |  |
| emulator.cpp:2579 NR 0xBA write | divmmc_.set_entry_timing_0(v); unmasked | zxnext.vhd:5590-5591 | ✓ |  |
| emulator.cpp:2580 NR 0xBB write | divmmc_.set_entry_points_1(v); unmasked | zxnext.vhd:5593-5594 | ✓ |  |
| emulator.cpp:2581 NR 0xB8 read | divmmc_.entry_points_0() | zxnext.vhd:6217-6218 | ✓ | V17-NMP-01 fix |
| emulator.cpp:2582 NR 0xB9 read | divmmc_.entry_valid_0() | zxnext.vhd:6220-6221 | ✓ | V17-NMP-01 fix |
| emulator.cpp:2583 NR 0xBA read | divmmc_.entry_timing_0() | zxnext.vhd:6223-6224 | ✓ | V17-NMP-01 fix |
| emulator.cpp:2584 NR 0xBB read | divmmc_.entry_points_1() | zxnext.vhd:6226-6227 | ✓ | V17-NMP-01 fix |
| emulator.cpp:4182 NR 0x09 write | bit 3 → divmmc_.clear_mapram() | zxnext.vhd:4184-4185 | ✓ | E3-05 fix |
| emulator.cpp:2598 NR 0x83 write | Propagate effective_port_enable for DivMMC b0 / MF b1 | zxnext.vhd:1227,2392,2412 | ✓ | V16-NMP-02 fan-out |
| emulator.cpp:7170 NR 0x03/0x02 → flash_cs gate | spi_.set_flash_cs_enable(cfg_mode \|\| rt[2]) | zxnext.vhd:3319 | ✓ | Pass-8 fix |

### DivMMC automap entry-point coverage

| Entry-point PC (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| divmmc.cpp:350-371 RST 0x00..0x38 | Loop over entry_points_0_; per-bit valid → main/rom3 path; per-bit timing → instant/delayed | zxnext.vhd:2848-2890 + :2892-2894 + :2901 | ✓ |  |
| divmmc.cpp:385 PC=0x0066 + button_nmi | NR 0xBB b1 → instant_match (main path); b0 → delayed_match | zxnext.vhd:2907-2908 + divmmc.vhd:120 | ✓ |  |
| divmmc.cpp:395 PC=0x04C6 | NR 0xBB b2 → delayed (rom3 path) | zxnext.vhd:2902 | ✓ |  |
| divmmc.cpp:401 PC=0x0562 | NR 0xBB b3 → delayed (rom3 path) | zxnext.vhd:2903 | ✓ |  |
| divmmc.cpp:404 PC=0x04D7 | NR 0xBB b4 → delayed (rom3 path) | zxnext.vhd:2904 | ✓ |  |
| divmmc.cpp:407 PC=0x056A | NR 0xBB b5 → delayed (rom3 path) | zxnext.vhd:2905 | ✓ |  |
| divmmc.cpp:419 PC=0x3Dxx wildcard | NR 0xBB b7 → instant (rom3 path) | zxnext.vhd:2898-2899 | ✓ | port_3dxx_msb |
| divmmc.cpp:422 PC=0x1FF8-0x1FFF off | NR 0xBB b6 → off_match (main-path gated) | zxnext.vhd:2896 | ✓ | port_1fxx_msb + a(7:3)="11111" |

### DivMmc internal state machine

| State (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| divmmc.cpp:287 step 1 held = hold | Latch hold→held (simulates MREQ rising) | divmmc.vhd:141 | ✓ | Per-M1 collapse of VHDL FF cascade |
| divmmc.cpp:307 button_nmi clear while held=1 | Clear button_nmi each M1 when held=1 | divmmc.vhd:112-113 | ✓ (1-cycle latency divergence) | **V20-DIVMMC-D03 class-d** — VHDL clears one i_CLK_28 cycle later via FF-sample-before-update semantics |
| divmmc.cpp:441 step 3 hold update | hold = instant_match \|\| delayed_match \|\| (held && !off) | divmmc.vhd:128-131 | ✓ |  |
| divmmc.cpp:448 step 4 active output | active = held \|\| instant_match (combinational) | divmmc.vhd:148 | ✓ | NMI nmi_instant_on subsumed under instant_match |
| divmmc.cpp:199 on_retn() immediate clear | Clear hold/held/active/button_nmi/pending | divmmc.vhd:108,126,139 | ✓ | Test-side direct clear |
| divmmc.cpp:225 on_m1_retn_delay | Latched pending → next M1 clears | divmmc.vhd:108,126,139 (i_retn_seen pulse, clocked) | ✓ (1-M1 vs ~1-clk in VHDL) | **V20-DIVMMC-D02 class-d** — drop-on-next-M1 is functionally equivalent for boot |
| divmmc.cpp:142 apply_enabled_transition_ | On true→false edge clear all latches incl. button_nmi | divmmc.vhd:108,126,139 (i_automap_reset path) | ✓ |  |

### DivMmc memory overlay

| Surface (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| divmmc.cpp:462 is_ram_mapped | (active && page0 && mapram) \|\| (active && page1) | divmmc.vhd:95 | ✓ |  |
| divmmc.cpp:474 is_read_only | page0 → always; page1 + mapram + bank=3 → also | divmmc.vhd:100 | ✓ |  |
| divmmc.cpp:494 read | page0 + mapram → RAM page 3; page0 → ROM; page1 → bank | divmmc.vhd:94-101 | ✓ |  |
| divmmc.cpp:513 write | page0 → no-op (rdonly); page1+mapram+bank=3 → no-op | divmmc.vhd:100 | ✓ |  |
| divmmc.cpp:123 is_active | port_io_enable_ && (conmem \|\| automap_active) | divmmc.vhd:94-95 + zxnext.vhd:4147 | ✓ | port_io-only gate (not nr_0a_4) |

### DivMmc port 0xE3 control register

| Field (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| divmmc.cpp:106 conmem (b7) | Direct copy from val | zxnext.vhd:4181 | ✓ |  |
| divmmc.cpp:107 mapram (b6) | OR-latch: mapram_ \|\|= val.b6 | zxnext.vhd:4182 | ✓ |  |
| divmmc.cpp:108 bank (b3:0) | Direct copy from val.b3:0 | zxnext.vhd:4183 | ✓ |  |
| divmmc.cpp:122 stored byte | (val & 0x8F) \| (mapram?0x40:0) — bits 5:4 always 0 | zxnext.vhd:4180-4187 (b5:4 never written) | ✓ | F19-DIVMMC-NIT-01 fix |
| divmmc.cpp:128 read_control | control_reg_ & 0xCF — b5:4 masked | zxnext.vhd:4190 | ✓ |  |
| divmmc.cpp:134 clear_mapram | mapram_=false; control_reg_ &= ~0x40 | zxnext.vhd:4184-4185 | ✓ | NR 0x09 b3 |

### SpiMaster public-API surface

| API (file:line) | C++ behavior | VHDL oracle | Match | Notes |
|---|---|---|---|---|
| spi.cpp:26 reset | Walk active CS lines, pulse deselect(); cs_=0xFF; preserve rx_data_ + sd_swap_ + devices_ | spi_master.vhd:91-100 + zxnext.vhd:3308-3326 | ✓ | Pass-7/11/12 cumulative fixes |
| spi.cpp:104 set_sd_swap | Store NR 0x0A b5 | zxnext.vhd:5193-5194 | ✓ |  |
| spi.cpp:111 attach_device | Bind cs_id slot | n/a (hardware wire) | ✓ | Non-owning slot |
| spi.cpp:120 write_cs | Decode val per cpu_do shape; gate 0x7F on flash_cs_enable_; notify deselects | zxnext.vhd:3308-3326 + :3319 | ✓ | V8 Flash-CS gate; V11 reset deselect |
| spi.cpp:172 read_cs | Return cs_ | (no VHDL port_e7_rd; emulator nullptr) | ✓ | Internal accessor only |
| spi.cpp:176 write_data | Active dev → rx = dev.receive(val); inactive → rx=0xFF | spi_master.vhd:104-117 + zxnext.vhd:3278-3280 | ✓ | V3 Audit no-slave fix |
| spi.cpp:197 read_data | Return prev rx (pipeline delay); new transfer → rx = dev.send() or 0xFF | spi_master.vhd:159-168 (miso_dat) + :177 | ✓ | V3 Audit no-slave; previous-transfer return |
| spi.h:103 spi_wait_n | Always returns true (no wait) | spi_master.vhd:177 | partial (class-d) | **V20-DIVMMC-D01** byte-level model; no DMA consumer |
| spi.cpp:231 save_state | Serialise cs_ + rx_data_ + sd_swap_; flash_cs is derived | n/a | ✓ | Derived state recomputed on load |
| spi.cpp:242 load_state | Restore cs_ + rx_data_ + sd_swap_ | n/a | ✓ |  |

### DivMmc save_state schema fields

| Field (file:line) | C++ behavior | VHDL invariant | Match | Notes |
|---|---|---|---|---|
| divmmc.cpp:552 enabled_ | Composite enable | n/a | ✓ |  |
| divmmc.cpp:553 conmem_ | Bit 7 of port 0xE3 | zxnext.vhd:4181 | ✓ |  |
| divmmc.cpp:554 mapram_ | Bit 6 (OR-latched) | zxnext.vhd:4182 | ✓ |  |
| divmmc.cpp:555 bank_ | Bits 3:0 of port 0xE3 | zxnext.vhd:4183 | ✓ |  |
| divmmc.cpp:556 control_reg_ raw | Stored byte with bits 5:4 zeroed | zxnext.vhd:4180-4187 | ✓ | F19-DIVMMC-NIT-01 |
| divmmc.cpp:557 automap_active_ | Combinational output snapshot | divmmc.vhd:148 | ✓ |  |
| divmmc.cpp:558 entry_points_0_ | NR 0xB8 latch | zxnext.vhd:5585 | ✓ |  |
| divmmc.cpp:559 entry_valid_0_ | NR 0xB9 latch | zxnext.vhd:5588 | ✓ |  |
| divmmc.cpp:560 entry_timing_0_ | NR 0xBA latch | zxnext.vhd:5591 | ✓ |  |
| divmmc.cpp:561 entry_points_1_ | NR 0xBB latch | zxnext.vhd:5594 | ✓ |  |
| divmmc.cpp:563 automap_hold_ | Two-stage latch | divmmc.vhd:123-134 | ✓ |  |
| divmmc.cpp:564 automap_held_ | Two-stage latch | divmmc.vhd:136-145 | ✓ |  |
| divmmc.cpp:566 button_nmi_ | NMI-button latch | divmmc.vhd:108-114 | ✓ |  |
| divmmc.cpp:569 layer2_map_read_ | L2 read-map gate | zxnext.vhd:3138 | ✓ |  |
| divmmc.cpp:573 retn_pending_clear_ | G46(a) delayed-clear shadow | divmmc.vhd:108,126,139 (i_retn_seen) | ✓ | Snapshot append-only |
| divmmc.cpp:574 ram_ (128 KB) | Full DivMMC RAM | n/a (hardware wire) | ✓ |  |
| divmmc.cpp:583 port_io_enable_ | NR 0x83 b0 lever | zxnext.vhd:2412 | ✓ | NA-03 split |
| divmmc.cpp:584 nr_0a_4_enable_ | NR 0x0A b4 lever | zxnext.vhd:5196 | ✓ | NA-03 split |

## Detailed finding: V20-DIVMMC-01

### Issue

Per SD Physical Layer Simplified Spec v6.00 § 7.3.2.1 (Table 7-9), R1 bit 5
= APP_CMD: "A '1' indicates that the card will (or has) interpret(ed) the
command as an ACMD." Two response paths must carry this bit:

1. **CMD55's R1**: the card has interpreted the command as CMD55, signalling
   that the NEXT command will be treated as ACMD → bit 5 = 1 in CMD55's R1.
2. **The subsequent ACMD's R1**: the card IS interpreting it as an ACMD
   (it followed CMD55) → bit 5 = 1 in the ACMD's R1.

Pre-fix:
- `cmd55_app_cmd()` (sd_card.cpp:801): `queue_r1(initialized_ ? 0x00 : 0x01)`
  — bit 5 NOT set.
- `acmd41_sd_send_op_cond()` (sd_card.cpp:939): `queue_r1(0x00)` — bit 5
  NOT set.

### Impact

Class-(c) latent. TBBlue's MMC_Init loop + FatFs's `send_cmd` only check
R1 bit 0 (idle) and bit 7 (response validity / non-0xFF), never bit 5. The
JNEXT boot path therefore works despite the divergence. But a strict
spec-compliant host (forensic firmware / test rig / future Z80 SD library
that audits the ACMD signature) would observe the missing bit.

The reason for class-(c) rather than -(b) is purely empirical: no firmware
we model is known to exercise the bit. The fix is spec-faithful and aligns
the emulated card with every real SDHC card on the market.

### Fix (commit `560d101`)

`src/peripheral/sd_card.cpp`:
- `cmd55_app_cmd()`: `queue_r1(static_cast<uint8_t>((initialized_ ? 0x00 : 0x01) | 0x20))`
  — OR-in 0x20 (APP_CMD).
- `acmd41_sd_send_op_cond()`: `queue_r1(0x20)` — APP_CMD only (no idle:
  ACMD41 fast-init promotes `initialized_=true` before queueing R1, so the
  idle bit is correctly clear; the APP_CMD bit is the only marker).

### Discriminative test (commit `560d101`)

`test/sdcard/sdcard_test.cpp` — new function `test_sd_32_cmd55_acmd41_app_cmd_bit`:

| Step | Expected R1 | Reason |
|---|---|---|
| pre-init CMD55 | 0x21 | idle (0x01) + APP_CMD (0x20) |
| post-init CMD55 | 0x20 | init complete (no idle) + APP_CMD |
| ACMD41 (after CMD55) | 0x20 | APP_CMD + (post fast-init: no idle) |

The test fails pre-fix (R1=0x00/0x01 for CMD55, 0x00 for ACMD41) and
passes post-fix. Existing tests SD-19 and SD-20 were also updated to
reflect the new spec-correct R1=0x20 for CMD55 (both assertions previously
expected 0x00 — i.e. encoded the pre-fix non-spec behavior).

### Test results post-fix

- `./build/test/sdcard_test`: 33/33 PASS (32 prior + 1 new SD-32)
- `ctest --output-on-failure`: 38/38 PASS
- `./build/test/fuse_z80_test`: 1356/1356 PASS
- `bash test/00regression/regression.sh`: 33/0/0 PASS

## Class-(d) pre-existing items (not promoted in Pass-20)

### V20-DIVMMC-D01 — SPI master cycle-accurate FSM not modelled

`SpiMaster::spi_wait_n()` returns constant true (no wait). The VHDL
`spi_master.vhd:177` derives `o_spi_wait_n` from a 5-bit state counter
(`state_idle OR state_last_d`), enforcing a 16-cycle-per-byte cadence.
JNEXT's byte-level model collapses every transfer to one synchronous
exchange. DMA-via-SPI bursts (zxnext.vhd:3297) would see no wait — the
DMA controller currently doesn't consume `spi_wait_n` either, so the
gap is functional but not observable on the boot path. Long-term G137.

### V20-DIVMMC-D02 — RETN clear timing (1-clk in VHDL vs next-M1 in C++)

VHDL `divmmc.vhd:108/126/139` clears `button_nmi` / `automap_hold` /
`automap_held` on the i_CLK_28 rising edge **after** `i_retn_seen` pulses
— typically 1 clk after the pulse, BEFORE the next M1 fetch begins.

C++ `on_m1_retn_delay` (divmmc.cpp:225) buffers the clear until the
NEXT M1 fetch (~3 i_CLK_28 later). The functional invariant ("overlay
survives RETN's own ED 45 fetch and drops on the returned-to instruction's
first M1") IS satisfied, because in both VHDL and C++ the next M1 sees
the cleared state.

Architectural — would require sub-M1 clock-step modelling.

### V20-DIVMMC-D03 — button_nmi clear-while-held timing 1-clk shift

VHDL `divmmc.vhd:112-113` samples `automap_held` in the pre-edge value
during the button_nmi process, so the clear lags the held=0→1 transition
by one i_CLK_28. C++ collapses this into the same `check_automap` call.

Both VHDL and C++ converge on `button_nmi=0` after one M1 of held=1 —
the shift is in the EXACT clock cycle of the clear, which is sub-M1.
No boot impact (Drive-button double-press race only).

## Workflow notes

- Pass-19 fix-of-reviewer (F19-DIVMMC-NIT-01) closed: `port_e3_reg(5:4)`
  storage invariant — bits 5:4 always zero post-write. Re-verified in
  this audit (Pass-20).
- Pass-19 reviewer F19-DIVMMC-D01 (cycle-accurate `spi_wait_n`) carried
  forward as V20-DIVMMC-D01 — same root.
- No regressions in this Pass.
- The audit is functionally close to convergence on this subsystem:
  one Pass-20 finding (spec-only, class-c) plus three carried class-d
  architectural items.

## Final HEAD SHA

`560d101` (single Pass-20 fix commit + this report committed atomically).
