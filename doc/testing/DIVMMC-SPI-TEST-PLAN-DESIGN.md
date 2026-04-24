# DivMMC + SPI Compliance Test Plan

VHDL-derived compliance test plan for the DivMMC and SPI subsystems of the
JNEXT emulator. All specifications are extracted directly from the FPGA VHDL
sources (divmmc.vhd, spi_master.vhd, zxnext.vhd).

## Purpose

DivMMC provides memory-mapped overlay (ROM and RAM banks) for the SD card
interface, with an automap mechanism that automatically activates/deactivates
based on instruction fetches at specific addresses. SPI provides the byte
exchange protocol for SD card communication. Both subsystems are critical for
NextZXOS boot and SD card access.

## Current status

Rewrite in Phase 2 per-row idiom merged on main 2026-04-15 (`task1-wave2-divmmc`).
Updated 2026-04-17 (commit `d4ea4e1`):

- **123 plan rows** mapped 1:1 to test IDs.
- **67/67 live pass (100%)**, 0 fail, 56 skip.
- **Previously failing, now fixed**:
  - **SX-03**: SPI pipeline delay implemented. `read_data()` returns previous `rx_data_`.
  - **SX-05**: `write_data()` captures MISO via `receive()` (changed from void to uint8_t).
  - **ML-05**: Pipeline delay fix covers ishift_r reset — first read returns 0xFF.
  - **SS-10**: Test bug fixed — was using 0x12 which matches VHDL SD card branch; changed to 0x00.
  - E3-04, E3-07, E3-08, EP-02/03/11, NR-01/02/05, SS-09/SS-11: fixed in prior sessions.
- **Skips**: 56 rows. Genuinely unreachable — NMI lifecycle (NM-01..08), RETN hook (DA-06, IN-03), instant-vs-delayed pipeline (TM-01..05), `automap_reset` vs `set_enabled` distinction (DA-08, NA-03), SRAM address ladder (SM-01..07), MISO priority ladder (MX-01/02/05), SPI state counter / SCK / MOSI pin (SX-06..10, ST-01..08), NR 0x09 bit 3 clear mapram (E3-05).

## Architecture

### Test harness

A dedicated test harness (`divmmc_spi_test.cpp`) exercises the DivMMC and SPI
subsystems through direct port I/O and memory fetch simulation. The harness
needs:

1. **CPU emulation** -- ability to trigger M1 fetches at specific addresses to
   exercise automap activation/deactivation.
2. **Port I/O** -- read/write ports 0xE3, 0xE7, 0xEB.
3. **Memory inspection** -- verify which memory bank is mapped at 0x0000-0x1FFF
   and 0x2000-0x3FFF after automap events.
4. **SPI mock** -- simulate SD card MISO responses to verify the byte exchange
   pipeline.

### Test data format

Each test case specifies:
- Test name
- Initial state (port registers, NextREG values, automap state)
- Action (M1 fetch at address, port write, port read)
- Expected state (mapped banks, port read values, automap held flag)

## File Layout

```
test/
  divmmc_spi/
    divmmc_spi_test.cpp    # Test runner
  CMakeLists.txt           # Updated: new executable + CTest registration
doc/design/
  DIVMMC-SPI-TEST-PLAN-DESIGN.md   # This document
```

## Test Case Catalog

### 1. Port 0xE3 -- DivMMC Control Register

Port 0xE3 controls conmem, mapram, and bank selection. VHDL reference:
`zxnext.vhd` lines 4173-4190.

| ID   | Test | Notes |
|------|------|-------|
| E3-01 | Reset clears port 0xE3 to 0x00 | All bits zero on reset |
| E3-02 | Write 0x80: conmem=1, mapram=0, bank=0 | Bit 7 directly writable |
| E3-03 | Write 0x40: mapram latches ON permanently | Bit 6 is OR-latched: `cpu_do(6) OR port_e3_reg(6)` |
| E3-04 | Write 0x00 after mapram set: mapram stays 1 | mapram cannot be cleared by port write |
| E3-05 | mapram cleared by NextREG 0x09 bit 3 | `nr_09_we` with `nr_wr_dat(3)=1` clears bit 6 |
| E3-06 | Write bank 0x0F: bits 3:0 select bank 0-15 | 16 banks of 8K = 128K DivMMC RAM |
| E3-07 | Read port 0xE3 returns `{conmem, mapram, 00, bank[3:0]}` | Bits 5:4 always read as 0 |
| E3-08 | Bits 5:4 of write are ignored | Only bits 7, 6, 3:0 are stored |

### 2. DivMMC Memory Paging -- conmem Mode

When conmem=1 (bit 7 of port 0xE3), DivMMC memory is forcibly mapped
regardless of automap state. VHDL reference: `divmmc.vhd` lines 88-101.

| ID   | Test | Notes |
|------|------|-------|
| CM-01 | conmem=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM | `rom_en` asserted, read-only |
| CM-02 | conmem=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N | Bank from port_e3_reg(3:0) |
| CM-03 | conmem=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3 | ROM replaced by RAM bank 3 |
| CM-04 | conmem=1, mapram=1: 0x2000-0x3FFF = DivMMC RAM bank N | Bank from port_e3_reg(3:0) |
| CM-05 | conmem=1: 0x0000-0x1FFF is read-only | `rdonly=1` when page0=1 |
| CM-06 | conmem=1, mapram=1, bank=3: 0x2000-0x3FFF is read-only | `rdonly=1` when mapram=1 AND bank=3 |
| CM-07 | conmem=1, mapram=1, bank!=3: 0x2000-0x3FFF is writable | `rdonly=0` for other banks |
| CM-08 | conmem=0, automap=0: no DivMMC mapping | Neither rom_en nor ram_en asserted |
| CM-09 | DivMMC paging requires `port_divmmc_io_en=1` | `o_divmmc_rom_en = rom_en AND i_en` |

### 3. DivMMC Memory Paging -- automap Mode

Automap produces identical paging to conmem but is triggered automatically by
instruction fetches. VHDL reference: `divmmc.vhd` lines 94-96, 148.

| ID   | Test | Notes |
|------|------|-------|
| AM-01 | automap=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM | Same as conmem |
| AM-02 | automap=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N | Same as conmem |
| AM-03 | automap=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3 | Same as conmem |
| AM-04 | automap active, then deactivated: normal ROM restored | Paging removed |

### 4. Automap Entry Points -- RST Addresses (0x00xx region)

The RST addresses 0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030,
0x0038 are decoded from `cpu_a[15:13]=000`, `cpu_a[7:6]=00`,
`cpu_a[2:0]=000`, with `cpu_a[5:3]` selecting which entry point (0-7).
VHDL reference: `zxnext.vhd` lines 2848-2890.

Each entry point is controlled by three NextREG bits:
- **NR 0xB8** (`nr_b8_divmmc_ep_0`): entry point enable (bit N)
- **NR 0xB9** (`nr_b9_divmmc_ep_valid_0`): valid flag (bit N) -- determines
  automap vs rom3 path
- **NR 0xBA** (`nr_ba_divmmc_ep_timing_0`): timing flag (bit N) -- instant vs
  delayed

The combination determines the automap signal:

| valid | timing | Result |
|-------|--------|--------|
| 1     | 1      | `automap_instant_on` (immediate activation) |
| 1     | 0      | `automap_delayed_on` (activation after M1 cycle) |
| 0     | 1      | `automap_rom3_instant_on` (immediate, ROM3-conditional) |
| 0     | 0      | `automap_rom3_delayed_on` (delayed, ROM3-conditional) |

Default NextREG values on reset:
- NR 0xB8 = 0x83 (entry points 0, 1, 7 enabled = addresses 0x0000, 0x0008, 0x0038)
- NR 0xB9 = 0x01 (only EP 0 valid = 0x0000 is automap, others are rom3)
- NR 0xBA = 0x00 (all delayed timing)
- NR 0xBB = 0xCD (see section 5)

| ID   | Test | Notes |
|------|------|-------|
| EP-01 | M1 fetch at 0x0000: automap_delayed_on activates | EP0: B8[0]=1, B9[0]=1, BA[0]=0 -> valid+delayed |
| EP-02 | M1 fetch at 0x0008: automap_rom3_delayed_on | EP1: B8[1]=1, B9[1]=0, BA[1]=0 -> rom3+delayed |
| EP-03 | M1 fetch at 0x0038: automap_rom3_delayed_on | EP7: B8[7]=1, B9[7]=0, BA[7]=0 -> rom3+delayed |
| EP-04 | M1 fetch at 0x0010: no automap (EP2 disabled) | B8[2]=0, no trigger |
| EP-05 | M1 fetch at 0x0018: no automap (EP3 disabled) | B8[3]=0 |
| EP-06 | M1 fetch at 0x0020: no automap (EP4 disabled) | B8[4]=0 |
| EP-07 | M1 fetch at 0x0028: no automap (EP5 disabled) | B8[5]=0 |
| EP-08 | M1 fetch at 0x0030: no automap (EP6 disabled) | B8[6]=0 |
| EP-09 | Set NR 0xBA[0]=1: 0x0000 becomes instant_on | Change timing to instant |
| EP-10 | Set NR 0xB9[1]=1: 0x0008 becomes automap (not rom3) | Change valid flag |
| EP-11 | Set NR 0xB8=0xFF: all 8 RST addresses trigger | Enable all entry points |
| EP-12 | Automap only triggers on M1+MREQ (instruction fetch) | Data reads at 0x0000 do NOT trigger automap |

### 5. Automap Entry Points -- Non-RST Addresses

Additional entry points are controlled by NR 0xBB bits. VHDL reference:
`zxnext.vhd` lines 2896-2908.

| NR 0xBB bit | Address | Signal | Type |
|-------------|---------|--------|------|
| bit 0 | 0x0066 (NMI) | `automap_nmi_delayed_on` | NMI delayed, requires `button_nmi` |
| bit 1 | 0x0066 (NMI) | `automap_nmi_instant_on` | NMI instant, requires `button_nmi` |
| bit 2 | 0x04C6 | `automap_rom3_delayed_on` | ROM3-conditional delayed |
| bit 3 | 0x0562 | `automap_rom3_delayed_on` | ROM3-conditional delayed |
| bit 4 | 0x04D7 | `automap_rom3_delayed_on` | ROM3-conditional delayed |
| bit 5 | 0x056A | `automap_rom3_delayed_on` | ROM3-conditional delayed |
| bit 6 | 0x1FF8-0x1FFF | `automap_delayed_off` | Deactivation range |
| bit 7 | 0x3Dxx | `automap_rom3_instant_on` | ROM3 instant (any 0x3Dxx) |

Default NR 0xBB = 0xCD = 11001101:
- bit 0 = 1: NMI 0x0066 delayed ON (with button_nmi)
- bit 1 = 0: NMI 0x0066 instant OFF
- bit 2 = 1: 0x04C6 enabled
- bit 3 = 1: 0x0562 enabled
- bit 4 = 0: 0x04D7 disabled
- bit 5 = 0: 0x056A disabled
- bit 6 = 1: 0x1FF8-0x1FFF deactivation enabled
- bit 7 = 1: 0x3Dxx enabled

| ID   | Test | Notes |
|------|------|-------|
| NR-01 | M1 at 0x04C6 with BB[2]=1: automap_rom3_delayed_on | ROM3-conditional |
| NR-02 | M1 at 0x0562 with BB[3]=1: automap_rom3_delayed_on | ROM3-conditional |
| NR-03 | M1 at 0x04D7 with BB[4]=0: no trigger (default) | Disabled by default |
| NR-04 | M1 at 0x056A with BB[5]=0: no trigger (default) | Disabled by default |
| NR-05 | Set BB[4]=1, M1 at 0x04D7: triggers rom3_delayed_on | After enabling |
| NR-06 | M1 at 0x3D00 with BB[7]=1: automap_rom3_instant_on | Any address 0x3Dxx |
| NR-07 | M1 at 0x3DFF with BB[7]=1: automap_rom3_instant_on | Upper bound of range |
| NR-08 | Set BB[7]=0, M1 at 0x3D00: no trigger | After disabling |

### 6. Automap Deactivation

Deactivation occurs at addresses 0x1FF8-0x1FFF when NR 0xBB bit 6 is set.
VHDL: `port_1fxx_msb=1 AND cpu_a[7:3]=11111 AND nr_bb[6]=1`.

| ID   | Test | Notes |
|------|------|-------|
| DA-01 | M1 at 0x1FF8 with automap held: automap deactivates | Lower bound of deactivation range |
| DA-02 | M1 at 0x1FFF with automap held: automap deactivates | Upper bound |
| DA-03 | M1 at 0x1FF7: no deactivation | Just below range |
| DA-04 | M1 at 0x2000: no deactivation | Above range |
| DA-05 | Set BB[6]=0: deactivation range disabled | No deactivation at 0x1FF8 |
| DA-06 | RETN instruction seen: automap deactivates | `i_retn_seen` clears automap_hold and automap_held |
| DA-07 | Reset clears automap state | `i_reset` clears automap_hold and automap_held |
| DA-08 | `automap_reset` clears automap state | When `port_divmmc_io_en=0` or `nr_0a_divmmc_automap_en=0` |

### 7. Automap Timing -- Instant vs Delayed

The distinction between instant and delayed automap affects when the mapping
takes effect relative to the triggering M1 fetch. VHDL reference:
`divmmc.vhd` lines 123-148.

| ID   | Test | Notes |
|------|------|-------|
| TM-01 | Instant on: DivMMC mapped during the triggering fetch | `automap` includes instant_on directly (no M1 gate) |
| TM-02 | Delayed on: DivMMC mapped on NEXT fetch after trigger | `automap_hold` set during M1, `automap_held` latched on MREQ rising |
| TM-03 | automap_held latches on MREQ_n rising edge | `automap_held <= automap_hold` when `cpu_mreq_n=1` |
| TM-04 | automap_hold updates only during M1+MREQ | `cpu_mreq_n=0 AND cpu_m1_n=0` |
| TM-05 | Held automap persists across non-deactivating fetches | `automap_held AND NOT delayed_off` keeps hold |

### 8. Automap ROM3-Conditional Activation

ROM3-conditional entry points only trigger automap when ROM3 is actually
present in the memory map. VHDL reference: `zxnext.vhd` lines 3137-3138.

`sram_divmmc_automap_rom3_en` requires:
- `sram_pre_override(2)` = 1 (DivMMC enabled)
- `sram_pre_override(0)` = 1 (ROM present)
- Layer 2 mapping NOT active
- ROMCS NOT active
- Either: altrom enabled with 128K ROM, OR actual ROM3 selected

| ID   | Test | Notes |
|------|------|-------|
| R3-01 | M1 at 0x0008 with ROM3 active: automap triggers | rom3_en=1 |
| R3-02 | M1 at 0x0008 with ROM0 active: no automap | rom3_en=0 (wrong ROM) |
| R3-03 | M1 at 0x0008 with Layer 2 mapped: no automap | Layer 2 overrides |
| R3-04 | `automap_active` (non-ROM3 path) always enabled when DivMMC on | `sram_divmmc_automap_en = sram_pre_override(2)` |

### 9. NMI and DivMMC Button

The NMI button triggers automap at 0x0066 only when `button_nmi` is set.
VHDL reference: `divmmc.vhd` lines 105-116, 120-121.

| ID   | Test | Notes |
|------|------|-------|
| NM-01 | DivMMC button press sets `button_nmi` | `i_divmmc_button=1` sets flag |
| NM-02 | M1 at 0x0066 with button_nmi: automap_nmi triggers | `automap_nmi_*_on = i_automap_nmi_*_on AND button_nmi` |
| NM-03 | M1 at 0x0066 without button_nmi: no NMI automap | button_nmi=0 blocks nmi_on signals |
| NM-04 | button_nmi cleared by reset | `i_reset=1` clears button_nmi |
| NM-05 | button_nmi cleared by automap_reset | `i_automap_reset=1` clears |
| NM-06 | button_nmi cleared by RETN | `i_retn_seen=1` clears |
| NM-07 | button_nmi cleared when automap_held becomes 1 | Ensures one-shot behaviour |
| NM-08 | `o_disable_nmi` = automap OR button_nmi | NMI suppressed while DivMMC active |

### 10. NextREG 0x0A -- DivMMC Automap Enable

NR 0x0A bit 4 controls global automap enable. VHDL reference: `zxnext.vhd`
line 4112.

| ID   | Test | Notes |
|------|------|-------|
| NA-01 | NR 0x0A[4]=0 (default): automap_reset asserted | `divmmc_automap_reset=1` |
| NA-02 | NR 0x0A[4]=1: automap_reset deasserted | Automap can function |
| NA-03 | port_divmmc_io_en=0: automap_reset asserted | Even if NR 0x0A[4]=1 |

### 11. DivMMC SRAM Address Mapping

DivMMC ROM and RAM occupy specific SRAM address ranges. VHDL reference:
`zxnext.vhd` lines 3084-3097.

| ID   | Test | Notes |
|------|------|-------|
| SM-01 | DivMMC ROM maps to SRAM address 0x010000-0x011FFF | `sram_A21_A13 = "000001000"` |
| SM-02 | DivMMC RAM bank 0 maps to SRAM 0x020000 | `sram_A21_A13 = "000010000"` |
| SM-03 | DivMMC RAM bank 3 maps to SRAM 0x026000 | `sram_A21_A13 = "000010011"` |
| SM-04 | DivMMC RAM bank 15 maps to SRAM 0x03E000 | `sram_A21_A13 = "000011111"` |
| SM-05 | DivMMC has priority over Layer 2 mapping | Checked before L2 in priority chain |
| SM-06 | DivMMC has priority over ROMCS | Checked before ROMCS |
| SM-07 | ROMCS maps to DivMMC banks 14 and 15 | `sram_A21_A13 = "00001111" & A13(0)` |

### 12. Port 0xE7 -- SPI Chip Select (Slave Select)

Port 0xE7 controls which SPI device is selected. VHDL reference: `zxnext.vhd`
lines 3300-3332.

Bit mapping: `{flash_n, -, -, -, rpi1_n, rpi0_n, sd1_n, sd0_n}`.
Active-low: a 0 bit means the device is selected.

| ID   | Test | Notes |
|------|------|-------|
| SS-01 | Reset: port_e7_reg = 0xFF (all deselected) | All SS lines high |
| SS-02 | Write 0x01 (sd_swap=0): selects SD1 | `port_e7 = "111111" & NOT swap & swap` = 0xFE when swap=0 -> sd0 selected... |
| SS-03 | Write 0x02 (sd_swap=0): selects SD0 | `port_e7 = "111111" & swap=0 & NOT swap=1` -> bit pattern depends on swap |
| SS-04 | Write 0x01 with sd_swap=1: selects SD0 (swapped) | SD0/SD1 swap via NR 0x0A[5] |
| SS-05 | Write 0x02 with sd_swap=1: selects SD1 (swapped) | Reverse of default |
| SS-06 | Write 0xFB: selects RPI0 (bit 2 = 0) | Exact match required |
| SS-07 | Write 0xF7: selects RPI1 (bit 3 = 0) | Exact match required |
| SS-08 | Write 0x7F in config mode: selects Flash | Only allowed in config mode or reset type bit 2 |
| SS-09 | Write 0x7F outside config mode: all deselected (0xFF) | Flash select blocked |
| SS-10 | Write any other value: all deselected (0xFF) | Default case |
| SS-11 | Only one device selected at a time | Hardware enforces single selection |

Detailed SD swap logic:
- Write value `cpu_do[1:0] = "10"`: `port_e7 = "111111" & NOT(sd_swap) & sd_swap`
- Write value `cpu_do[1:0] = "01"`: `port_e7 = "111111" & sd_swap & NOT(sd_swap)`
- When sd_swap=0: write 0x02 -> bit1=1,bit0=0 (SD1 selected); write 0x01 -> bit1=0,bit0=1 (wait, inverted)

Actually the VHDL is clearer: `cpu_do(1:0)="10"` means the software wrote with
bit1=1 (meaning "select SD1 logically"), and the hardware produces
`NOT(swap) & swap`. With swap=0: bits = 1,0 -> SD1_n=0 (SD1 selected).
With swap=1: bits = 0,1 -> SD0_n=0 (SD0 selected instead).

### 13. Port 0xEB -- SPI Data Exchange

Port 0xEB triggers SPI byte exchange. VHDL reference: `spi_master.vhd`.

**Protocol**: SPI mode 0 (CPOL=0, CPHA=0). Full-duplex 8-bit exchange.

| ID   | Test | Notes |
|------|------|-------|
| SX-01 | Write to port 0xEB: sends byte via MOSI | Loads output shift register with written byte |
| SX-02 | Read from port 0xEB triggers exactly one SPI exchange cycle | `state_r` starts on rd (VHDL spi_master.vhd:109-110). MISO return value is NOT checked here — that is SX-03/SX-05/ML-05 territory because `miso_dat` is latched via the pipeline register at `state_last_d`. |
| SX-03 | Read returns PREVIOUS exchange result | `miso_dat` latched at end of previous transfer |
| SX-04 | First read after reset returns 0xFF | `miso_dat` initialized to all 1s |
| SX-05 | Write 0xAA then read: read returns MISO from write cycle | Pipeline: read gets result of preceding exchange |
| SX-06 | SPI transfer is 16 clock cycles (8 bits x 2 edges) | `state_r` counts 0x00 to 0x0F, then idle |
| SX-07 | SCK output matches state_r[0] | `o_spi_sck = state_r(0)` |
| SX-08 | MOSI outputs MSB first | `o_spi_mosi = oshift_r(7)`, shifts left |
| SX-09 | MISO sampled on rising SCK edge (delayed by 1 cycle) | `state_r0_d` delays sampling for synchronization |
| SX-10 | Back-to-back transfers: new transfer starts on last state | `spi_begin` when `state_last=1 AND (rd OR wr)` |

### 14. SPI State Machine

The SPI master uses a 5-bit state counter. VHDL reference: `spi_master.vhd`
lines 86-100.

| ID   | Test | Notes |
|------|------|-------|
| ST-01 | Reset: state = "10000" (idle) | `state_r(4)` = idle flag |
| ST-02 | Transfer start: state goes to "00000" | `spi_begin` clears all bits |
| ST-03 | State increments each clock until 0x0F | 16 states for 8-bit transfer |
| ST-04 | After state 0x0F, returns to idle ("10000") | Next increment wraps to idle |
| ST-05 | `spi_wait_n = 0` during active transfer | `state_idle OR state_last_d` |
| ST-06 | `spi_wait_n = 1` when idle or on last cycle | DMA wait signal |
| ST-07 | Transfer can begin from idle OR from last state | Allows pipelined transfers |
| ST-08 | Read/write during mid-transfer: ignored | `spi_begin=0` when not idle/last |

### 15. SPI MISO Data Latch

The input data register uses a pipeline with one-cycle delay for
synchronization. VHDL reference: `spi_master.vhd` lines 121-168.

| ID   | Test | Notes |
|------|------|-------|
| ML-01 | MISO bits shifted in on delayed rising SCK | `state_r0_d=1` triggers shift |
| ML-02 | Full byte latched into `miso_dat` on `state_last_d` | One cycle after state reaches 0x0F |
| ML-03 | `miso_dat` holds value until next transfer completes | Stable between transfers |
| ML-04 | Input and output shift registers are independent | Can have different data simultaneously |
| ML-05 | Reset sets `ishift_r` to all 1s | Safe default |
| ML-06 | 16 cycles minimum between read/write operations | Comment in VHDL: "separated by 16 cycles" |

### 16. SPI MISO Source Multiplexing

When multiple devices could provide MISO, the source is selected based on which
SS line is active. VHDL reference: `zxnext.vhd` lines 3278-3280.

| ID   | Test | Notes |
|------|------|-------|
| MX-01 | Flash selected: MISO from flash | `spi_ss_flash_n=0` highest priority |
| MX-02 | RPI selected: MISO from RPI | Second priority |
| MX-03 | SD selected: MISO from SD | Third priority |
| MX-04 | No device selected: MISO reads as 1 | Default pull-up |
| MX-05 | Priority: Flash > RPI > SD > default | Cascaded if-else |

### 17. Integration -- DivMMC + SPI Typical Sequences

End-to-end scenarios combining DivMMC paging and SPI card access.

| ID   | Test | Notes |
|------|------|-------|
| IN-01 | Boot sequence: automap at 0x0000, DivMMC ROM mapped | Default NR values trigger automap |
| IN-02 | SD card init: select SD0, exchange bytes, deselect | Port 0xE7 -> 0xEB write/read -> 0xE7 |
| IN-03 | RETN after NMI handler: automap deactivated, normal ROM | Full NMI->handler->RETN cycle |
| IN-04 | Automap at 0x0008 (RST 8): ROM3 conditional | Only triggers when ROM3 is the active ROM |
| IN-05 | Rapid SPI exchanges: back-to-back without idle gap | Pipeline capability |
| IN-06 | conmem override during automap: conmem takes priority | Both produce mapping, conmem is explicit |
| IN-07 | DivMMC disabled via NR 0x0A[4]=0: no automap, SPI still works | SPI is independent of automap |

## Special Handling

### Automap requires M1+MREQ

Automap entry point detection only occurs during instruction fetches
(`cpu_mreq_n=0 AND cpu_m1_n=0`). Data reads from the same addresses do NOT
trigger automap. The test harness must distinguish between M1 fetches and
ordinary memory reads.

### Pipeline delay in SPI reads

A read from port 0xEB returns the MISO data from the PREVIOUS SPI exchange,
not the current one. The first read after reset returns 0xFF. This is because
the read itself triggers a new exchange (sending 0xFF), and the result of that
exchange only becomes available after the next read/write. Tests must account
for this one-exchange pipeline delay.

### mapram latch behaviour

The mapram bit (port 0xE3 bit 6) can only be SET by writing to port 0xE3. It
cannot be cleared by a subsequent write. The only way to clear it is via
NextREG 0x09 bit 3. This is a deliberate hardware design to prevent accidental
unmap of the DivMMC RAM overlay once ESXDOS has set it.

### ROM3-conditional entry points

Entry points configured as ROM3-conditional (valid=0 in NR 0xB9) only trigger
automap when ROM3 is the currently selected ROM page. This is determined by
`sram_divmmc_automap_rom3_en` which checks multiple conditions including:
DivMMC enabled, ROM present, no Layer 2 mapping, no ROMCS, and either altrom
with 128K ROM or actual ROM3 selected.

### SPI clock domain

The SPI master runs at `i_CLK_CPU` which is twice the SPI SCK frequency.
This means each SCK half-period is one `i_CLK_CPU` cycle. A full 8-bit
transfer takes 16 `i_CLK_CPU` cycles. The emulator does not need to model
clock-cycle-exact SPI timing but must respect the pipeline delay semantics.

## CMake Integration

In `test/CMakeLists.txt`:

```cmake
# DivMMC + SPI compliance test
add_executable(divmmc_spi_test divmmc_spi/divmmc_spi_test.cpp)
target_link_libraries(divmmc_spi_test PRIVATE jnext_core)

# Register with CTest
add_test(NAME divmmc_spi_test
         COMMAND divmmc_spi_test)
```

## Regression Integration

The `test/regression.sh` script gains a DivMMC/SPI test phase:

```
=== JNEXT Regression Test Suite ===

[fuse-z80] Running FUSE Z80 opcode tests...
  PASS: 1356/1356 opcodes passed

[z80n]    Running Z80N extended opcode tests...
  PASS: 110/110 opcodes passed

[divmmc-spi] Running DivMMC + SPI compliance tests...
  PASS: XX/XX tests passed

Running screenshot tests...
  ...
```

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run DivMMC/SPI tests standalone
./build/test/divmmc_spi_test

# Run full regression suite (includes DivMMC/SPI)
bash test/regression.sh
```

## Summary

| Section | Tests | Coverage |
|---------|------:|----------|
| Port 0xE3 control register | 8 | Register read/write, bit behaviour |
| conmem mode paging | 9 | ROM/RAM mapping, read-only, enable |
| automap mode paging | 4 | Overlay activation/deactivation |
| RST entry points | 12 | All 8 RST addresses, NR B8/B9/BA |
| Non-RST entry points | 8 | 0x04C6, 0x0562, 0x04D7, 0x056A, 0x3Dxx, NR BB |
| Deactivation | 8 | 0x1FF8-0x1FFF range, RETN, reset |
| Instant vs delayed timing | 5 | Timing semantics |
| ROM3-conditional | 4 | ROM3 presence checks |
| NMI / button | 8 | button_nmi lifecycle, disable_nmi |
| NR 0x0A automap enable | 3 | Global enable/disable |
| SRAM address mapping | 7 | Physical address ranges |
| Port 0xE7 chip select | 11 | SS register, sd_swap, flash protection |
| Port 0xEB SPI exchange | 10 | Full-duplex protocol |
| SPI state machine | 8 | State counter, wait signal |
| SPI MISO latch | 6 | Pipeline delay, synchronization |
| MISO multiplexing | 5 | Device priority |
| Integration scenarios | 7 | End-to-end sequences |
| **Total** | **~123** | |

## NMI integration (NM-01..08 un-skip path)

The eight `NM-01..08` rows in `divmmc_test.cpp` are the DivMMC
subsystem's contract with the central NMI source / arbiter. They were
previously annotated "Deferred to Task 8 (Multiface)"; they are now
owned by the
[NMI source pipeline plan](../design/TASK-NMI-SOURCE-PIPELINE-PLAN.md),
whose Wave B lands the producer that sets `DivMmc::button_nmi_`, the
consumer-side feedback `is_nmi_hold()`, and the four VHDL-specified
clear paths per `divmmc.vhd:103-150`.

### Row → Wave B sub-task mapping

| ID | Sub-task | VHDL cite |
|---|---|---|
| NM-01 | FSM drives `set_button_nmi(true)` on DivMMC path | divmmc.vhd:108-111 |
| NM-02 | button_nmi produced end-to-end, automap gate observed | divmmc.vhd:120-121 |
| NM-03 | no-button baseline vs button-pressed stimulus at 0x0066 | divmmc.vhd:120 |
| NM-04 | reset clear path (already present; verify end-to-end) | divmmc.vhd:108 |
| NM-05 | `automap_reset` clear path (new hook) | divmmc.vhd:108 |
| NM-06 | `on_retn_seen()` clear path (new Z80 RETN hook) | divmmc.vhd:108 |
| NM-07 | `automap_held = 1` one-shot clear path | divmmc.vhd:112-113 |
| NM-08 | `o_disable_nmi` output accessor (`automap_held OR button_nmi`) | divmmc.vhd:150 |

### Clear-path fidelity (Wave B)

Per `divmmc.vhd:105-116`, `button_nmi_` must clear on any of the
following four signals:

1. `i_reset = 1` — hard reset. Already clears via `DivMmc::reset()`
   (pre-plan baseline). **NM-04 verifies this end-to-end now that the
   latch can actually be set via NmiSource.**
2. `i_automap_reset = 1` — the DivMMC automap FSM re-entering the
   reset state. **NM-05 exercises a new internal hook added in Wave B.**
3. `i_retn_seen = 1` — the Z80 RETN instruction completion. **NM-06
   exercises a new `DivMmc::on_retn_seen()` hook wired from the Z80
   RETN execution path.**
4. `automap_held = 1` — one-shot on the rising edge when the DivMMC
   consumer has consumed the NMI. **NM-07 exercises the edge-trigger
   added in Wave B.**

### `o_disable_nmi` accessor (NM-08)

A new `DivMmc::is_nmi_hold() const` accessor is added in Wave B and
returns `automap_held_ OR button_nmi_` per the VHDL `o_disable_nmi`
output signal (`divmmc.vhd:150`). This value is read by `NmiSource` on
each tick for the `divmmc_nmi_hold` signal at `zxnext.vhd:2118`, and
is what drives the FSM transition from HOLD to END on the DivMMC path.

### Task 8 cross-references

Any pre-existing "deferred to Task 8 (Multiface)" annotations on these
eight rows are superseded by the NMI plan. Task 8 Branch A (NMI state
machine) and Branch D (NR 0x02 programmatic NMI) are marked *superseded
by this plan* in `doc/design/TASK-8-MULTIFACE-PLAN.md` §5 (Phase 0 edit
per plan Q2). Task 8 Branch C is scoped down to MF-button wiring only;
the DivMMC-button wiring lives in this plan's Wave B.
