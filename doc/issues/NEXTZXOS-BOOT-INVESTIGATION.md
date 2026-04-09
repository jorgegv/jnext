# NextZXOS Boot Investigation

## Current Status (2026-04-01)

**Branch:** `fix-nextzxos-boot`

The firmware boot sequence completes successfully — all ROMs load, display shows
"Firmware v1.44.db / Core v3.02.00", machine type 0xb3 is set, and soft reset
triggers. However, **ROM data does not reach the correct RAM pages** after soft
reset. The CPU at 0x0000 finds boot loader residue instead of the Spectrum ROM.

Root cause is fully analyzed. The closest fix attempt (config page write
recording + soft_reset replay) got 96K of ROM data correctly recorded and
replayed, but had remaining issues with DivMMC automap state after soft_reset.

---

## Boot Sequence Overview

```
Boot ROM phase:
  CMD12 → CMD0 → CMD8 → ACMD41 → CMD58 (SD init)
  → reads TBBLUE.FW from SD (~55 sectors)
  → NR 0x07=0x03 (28 MHz), NR 0x03=0xB0 (disable boot ROM)
  → JP 0x6000 (start firmware)

Post-boot firmware (at 0x6000):
  CMD12 → CMD0 → CMD8 → ACMD41 → CMD58 (SD reinit via diskio.c)
  → f_mount → f_open config.ini → reads config ✓
  → f_open menu.def → reads menu config ✓
  → check_coreversion() → returns (HWID_EMULATORS=0x08) ✓
  → display_bootscreen() → reads TBBLUE.FW screens data ✓
  → load_keymap() → writes keymap to config page bank 0 ✓
  → load_roms() → writes ESXMMC, Multiface, Spectrum ROM via config pages ✓
  → init_registers() → sets NR 0x05, 0x06, 0x08, 0x09, 0x0A, etc. ✓
  → NR 0x03=0xB3 (machine type +3, config_mode OFF) ✓
  → NR 0x02=0x01 (soft reset) ✓

After soft reset:
  CPU at PC=0x0000 → reads from MMU slots 0-1 (RAM pages 0x00, 0x01)
  → pages contain boot loader residue, NOT Spectrum ROM → garbage execution
```

---

## SPI Model (rewritten 2026-03-29)

### ZesarUX Comparison

Reference: `/home/jorgegv/src/spectrum/zesarux/src/storage/mmc.c`

The original model used VHDL-accurate full-duplex SPI with pipeline delay. This
caused response byte offsets, leftover data corrupting subsequent commands, and
CMD8 R7 response issues.

The rewritten model uses **independent read/write paths** matching ZesarUX:
- `receive(tx)` — command/data write path
- `send()` — response read path
- `deselect()` — CS high resets all protocol state to IDLE

### Response Formats (verified against firmware diskio.c)

```
CMD0:  NCR(0xFF) + R1(0x01)
CMD8:  NCR(0xFF) + R1(0x01) + R7(0x00, 0x00, 0x01, check_pattern)
CMD12: 8×0xFF (stuff bytes) + NCR(0xFF) + R1
CMD17: NCR(0xFF) + R1(0x00) + 0xFE (data token) + 512 data + 2 CRC
CMD18: NCR(0xFF) + R1(0x00) + [0xFE + 512 data + 2 CRC] per block + 0xFF gap
CMD58: NCR(0xFF) + R1 + OCR(4 bytes: 0xC0=CCS+powered, 0xFF, 0x80, 0x00)
ACMD41: NCR(0xFF) + R1(0x00)
```

### Firmware SPI Protocol (from diskio.c)

```c
send_cmd():
  select()              // CS low (0xFE to port 0xE7) + 1 dummy read
  xmit_mmc(buf, 6)     // send 6-byte command
  if (CMD12) rcvr_mmc(buf, 8)  // skip 8 stuff bytes
  wait_response()       // read up to 250 bytes until non-0xFF
  return R1

disk_read():
  if (count == 1): CMD17 + rcvr_datablock(buf, 512)
  else: CMD18 + loop { rcvr_datablock(buf, 512); buf += 512 } + CMD12
  deselect()

rcvr_datablock():
  loop up to 5000: read byte, break if != 0xFF
  check byte == 0xFE (data token)
  rcvr_mmc(buf, 512)   // read 512 data bytes
  rcvr_mmc(d, 2)       // discard 2 CRC bytes
```

---

## Firmware Source Architecture

Source: `https://gitlab.com/thesmog358/tbblue/-/tree/master/src/firmware` (GPLv3)
Local clone: `/tmp/tbblue-firmware/src/firmware/`

### Two Independent SD Card Drivers

| Component    | Boot Loader (`loader/`)              | Post-Boot Firmware (`app/`)            |
|--------------|--------------------------------------|----------------------------------------|
| **SD init**  | `mmc.s` (Z80 asm)                   | `diskio.c` (FatFs diskio layer, C)     |
| **FAT**      | `fat.c` (hand-rolled FAT16/32)      | `ff.c` (Chan's FatFs library)          |
| **Entry**    | `main.c` → `MMC_Init()` → loads FW  | `boot.c` → `f_mount()` → `f_open()`   |
| **Runs at**  | 0x0000 (boot ROM overlay)            | 0x6000 (loaded into RAM)               |

### Firmware Build Configuration

From `app/Makefile`:
```
CC = sdcc
CFLAGS = -mz80 --opt-code-size
LDFLAGS = -mz80 --code-loc 0x6010 --data-loc 0 --no-std-crt0
ROFLAGS = -D_FS_READONLY=1 -D_FS_MINIMIZE=1
```

CRT0 (`app/src/crt0.s`):
```asm
.org 0x6000
ld sp, #0xFFFF      ; stack at top of memory (slot 7, page 0x01)
di
call gsinit         ; zero BSS, copy initializers
call _main
jp _exit
```

### Firmware Memory Layout

- **0x0000-0x1FFF**: `_DATA` + `_INITIALIZED` + `_BSS` (firmware globals: ~8K)
  - Includes: FATFS (~552 bytes), FIL, line[256], fwMap[512], scratch[512], etc.
  - On real hardware: config page RAM (bank 0, pages 0x00/0x01)
  - In emulator with automap ON: DivMMC ROM (0x0000-0x1FFF) + DivMMC RAM (0x2000-0x3FFF)
- **0x4000-0x5FFF**: ULA screen RAM (bank 5, pages 0x0A/0x0B)
- **0x6000-0x600F**: CRT0 startup code
- **0x6010+**: `_CODE` (firmware functions)
- **0xFFFF downward**: Stack (in slot 7 = page 0x01)

### Key Functions

- `boot.c:main()` — firmware entry point, calls all phases
- `boot.c:check_coreversion()` — skips flash check when `mach_id == HWID_EMULATORS` (0x08)
- `boot.c:display_bootscreen()` — reads screen data from TBBLUE.FW into config page
- `boot.c:load_keymap()` — reads keymap file to config page bank 0
- `boot.c:load_roms()` → `loadFile()` — reads ROM files to config page banks
- `boot.c:init_registers()` — writes NR 0x05/0x06/0x08/0x09/0x0A (enables DivMMC automap)
- `fwfile.c:fwOpenAndSeek()` — opens TBBLUE.FW, reads 512-byte block map
- `fwfile.c:fwRead()` — reads data from TBBLUE.FW via f_read
- `config.c:load_config()` — reads config.ini and menu.def
- `videomagic.c:videoTestActive()` — checks magic string at config page bank 2
- `misc.c:getCoreBoot()` — checks magic string at config page bank 6

### loadFile() Pattern

```c
void loadFile(unsigned char destpage, unsigned char numpages, unsigned int blocklen) {
    REG_NUM = REG_RAMPAGE;       // NR 0x04
    while (numpages--) {
        REG_VAL = destpage++;    // select config page bank
        res = f_read(&Fil, (unsigned char *)0, blocklen, &bl);  // read into 0x0000
    }
}
```

This writes ROM data to address 0x0000 via the config page mechanism. Each
`destpage` maps a different 16K RAM bank to slots 0-1.

### TBBLUE.FW Block Map

From the SD image (sector 193377, first 512 bytes):

| Block | ID                | Start (sectors) | Length (sectors) | Size     |
|-------|-------------------|-----------------|------------------|----------|
| 0     | FW_BLK_BOOT       | 0               | 224              | 112K     |
| 1     | FW_BLK_OLD_EDITOR | 146             | 511              | 256K     |
| 2     | FW_BLK_UPDATER    | 292             | 2                | 1K       |
| 3     | FW_BLK_OLD_CORES  | 128             | 16               | 8K       |
| 4     | FW_BLK_EDITOR     | 287             | 259              | 132K     |
| 5     | FW_BLK_CORES      | 252             | 483              | 247K     |
| 6     | FW_BLK_TESTCARD   | 365             | 438              | 224K     |
| 7     | FW_BLK_RESET      | 219             | 398              | 204K     |
| 8     | FW_BLK_SCREENS    | 220             | 73               | 37K      |

Constants: `FW_L2_PAL_SIZE=227`, `RAMPAGE_ROMSPECCY=0x00`, `RAMPAGE_ROMDIVMMC=0x04`,
`RAMPAGE_ROMMF=0x05`, `RAMPAGE_ALTROM0=0x06`.

---

## VHDL DivMMC Automap Behavior

Source: `device/divmmc.vhd`, `zxnext.vhd`

### Key Defaults at Power-On

- `nr_0a_divmmc_automap_en = '0'` → **automap DISABLED**
- `nr_03_config_mode = '1'` → config mode ON
- `nr_83_internal_port_enable = all '1'` → port 0xE3 enabled
- `nr_b8_divmmc_ep_0 = 0x83` → RST 0x00, 0x08, 0x38 enabled
- `nr_b9_divmmc_ep_valid_0 = 0x01` → only RST 0x0000 valid by default

### Automap Activation/Deactivation

**Activation** (M1 cycle only):
- RST entry points: gated by `entry_points_0` AND `entry_valid_0`
- NMI (0x0066): bits 0-1 of `entry_points_1`
- Tape traps: bits 2-5 of `entry_points_1`
- Delayed vs instant: controlled by `entry_timing_0`

**Deactivation:**
- RETN instruction (`i_retn_seen` clears `automap_hold` and `automap_held`)
- Delayed off: PC at 0x1FF8-0x1FFF when NR 0xBB bit 6 = 1
- Automap reset: forced when `port_divmmc_io_en='0'` OR `nr_0a_divmmc_automap_en='0'`
- Hard/soft reset clears `automap_hold`/`automap_held`

### Memory Priority (zxnext.vhd lines 3081-3133)

```
1. DivMMC ROM (divmmc_rom_en AND override bit 2) → rdonly=1, writes DISCARDED
2. DivMMC RAM (divmmc_ram_en AND override bit 2) → rdonly varies
3. Layer 2 write-over
4. ROMCS (expansion bus)
5. Alt ROM
6. Default: config page (if config_mode) or normal MMU
```

DivMMC ALWAYS has higher priority than config page. Writes to DivMMC ROM area
(0x0000-0x1FFF when mapped) are silently lost — NOT redirected to config page.

### Config Mode Address Decode (zxnext.vhd ~line 3044)

```vhdl
elsif nr_03_config_mode = '1' then
   sram_pre_A21_A13 <= nr_04_romram_bank & cpu_a(13);
   sram_pre_override <= "110";   -- DivMMC & Layer 2 CAN still override
```

Even in config mode, DivMMC can override. On real hardware this is irrelevant
because automap is OFF at boot. In the emulator, automap is ON, causing conflict.

---

## The Root Cause: DivMMC / Config Page Conflict

### On Real Hardware (automap OFF)

1. Boot ROM loads firmware, disables itself
2. Firmware runs at 0x6000; globals at 0x0000 are in config page RAM (bank 0)
3. Config mode ON; automap OFF → no DivMMC interference
4. FatFs reads/writes globals consistently through config page RAM
5. `loadFile()` switches config page banks, writes ROM data to 0x0000
6. ROM data overwrites firmware globals (including FATFS at ~0x03C8)
7. FatFs f_read clips reads at cluster boundary (~13 sectors per call)
8. After first cluster read, FATFS is corrupted by ROM data at 0x0000
9. **On real hardware this works because:** (analysis uncertain — see below)
10. Firmware enables automap via NR 0x0A, triggers soft reset
11. Post-reset: DivMMC ROM runs briefly (esxdos init → RETN), then Spectrum ROM

### In Emulator (automap ON)

1. Boot ROM loads firmware, disables itself
2. Automap triggers at PC=0x0000 → DivMMC active
3. DivMMC ROM at 0x0000-0x1FFF (reads: esxdos; writes: DISCARDED)
4. DivMMC RAM at 0x2000-0x3FFF (reads/writes: consistent)
5. Firmware globals that happen to be in 0x2000+ work via DivMMC RAM
6. esxdos ROM byte at offset 0x003A = 0x00 → FatFs sees "not mounted" → mounts OK
7. `loadFile()` writes ROM data: slot 0 (0x0000-0x1FFF) → discarded; slot 1 (0x2000-0x3FFF) → DivMMC RAM
8. Config page RAM (pages 0x00/0x01) never receives the ROM data
9. After soft reset, pages 0x00/0x01 have boot loader residue → garbage execution

### The Chicken-and-Egg

- **Automap ON**: firmware completes all phases; ROM data intercepted by DivMMC
- **Automap OFF**: config page works correctly; FatFs self-corrupts because
  f_read's destination buffer at 0x0000 overlaps FATFS structure

### FatFs Self-Corruption Detail

Chan's FatFs `f_read()` (ff.c ~line 3555):
```c
if (csect + cc > fs->csize) {   /* Clip at cluster boundary */
    cc = fs->csize - csect;
}
disk_read(fs->drv, rbuff, sect, cc);
```

FatFs reads at most one cluster at a time. With cluster size 16 and 13 sectors
remaining in the current cluster, disk_read gets 13 sectors. Data is written to
the destination buffer starting at 0x0000. After ~1 sector (512 bytes), the
FATFS structure at ~0x03C8 is overwritten. When f_read loops back for the next
cluster, `get_fat()` reads corrupted data → `FR_DISK_ERR` or `FR_INT_ERR`.

The firmware source confirms `_FS_MINIMIZE=1` (read-only, no multi-cluster
optimization). No fast-seek or cluster-map features.

### Unresolved Question

How does this work on real hardware? The same FatFs code, same cluster size,
same self-corruption should occur. Possible explanations (not yet verified):

1. The pre-built firmware binary may use different `--data-loc` than the source
   Makefile shows, placing FATFS above the write range
2. The real hardware's SD image may have TBBLUE.FW at a cluster boundary,
   giving more sectors in the first cluster (enough to complete the full read)
3. The firmware binary on the SD image may be a different version than the
   source code, with different memory layout or FatFs configuration
4. There may be a VHDL mechanism that provides a "shadow" write path during
   config mode that we haven't identified

---

## Approaches Tried

### Successfully Fixed

| Fix | Description | Commit |
|-----|-------------|--------|
| Split SPI model | Independent read/write paths matching ZesarUX | 5bef014 |
| CS deselect | Reset protocol state on CS change | 5bef014 |
| CMD1 support | SEND_OP_COND for boot ROM MMC init | 5bef014 |
| NCR byte | 0xFF prepended to R1 responses | 971aaee |
| CMD12 stuff bytes | 8×0xFF before R1 for firmware compatibility | 7723008 |
| Machine ID | NextREG 0x00 = 0x08 (HWID_EMULATORS) | 93dc7bc |
| Boot ROM SRAM | Writable boot ROM for loader variables | 6b947ec |
| Config mode sync | NR 0x03 syncs config_mode to DivMMC | 6b947ec |
| FAT32 image | Fixed SD image for correct BPB/FAT32 | 93dc7bc |
| CMD18 multi-block | Multi-block read with inter-block gaps | 93dc7bc |

### Attempted But Insufficient (2026-04-01)

| # | Approach | Result |
|---|----------|--------|
| 1 | DivMmc `config_mode_=true` initially | DivMMC inactive; fwRead fails (FATFS overwrite) |
| 2 | Write-only bypass (skip DivMMC writes when config_mode) | Read/write asymmetry breaks globals |
| 3 | Shadow writes (dual to DivMMC + config page) | Config page slots still ROM (read-only) early in boot |
| 4 | Default config page on boot ROM disable | Config page set up; fwRead still fails (FATFS overwrite) |
| 5 | Config page write recording + soft_reset replay | **96K replayed correctly**; screen corrupted because DivMMC automap not reset on soft_reset |
| 6 | Automap OFF (matching VHDL default) | "Error reading TBBLUE.FW data!" (FatFs self-corruption) |
| 7 | DivMmc `config_mode_=false` + automap OFF | Same as #6 |

### Previous Attempts (2026-03-30, from memory)

| # | Approach | Result |
|---|----------|--------|
| A | config_mode_ starting true (matching VHDL) | Broke boot ROM (DivMMC ROM not mapped) |
| B | Skip DivMMC for all writes < 0x4000 when config_mode | Broke boot ROM (DivMMC RAM buffers) |
| C | Skip DivMMC when config_mode AND boot_rom_en=false | Broke boot ROM (config_mode starts true) |
| D | DivMMC automap disabled by default | Firmware hung after 3 config page writes |
| E | divmmc_write returns false for slot 0 when config_mode | Same as C |
| F | entry_valid_0_ mask in check_automap | Caused boot failures |
| G | RETN detection (ED 45) clearing automap | Never triggered during boot |

---

## Closest Fix: Approach #5 (Write Recording + Replay)

This approach got the furthest. Implementation:

1. When NR 0x04 is written during config mode, start recording all writes to
   0x0000-0x3FFF in a side buffer keyed by config page bank
2. DivMMC automap stays ON → firmware uses DivMMC RAM for globals → FatFs works
3. Config page writes are intercepted by DivMMC (as before) but ALSO recorded
4. At soft_reset, replay all recorded writes to the correct RAM pages

**Result:** 96K recorded across 6 banks (0-5), successfully replayed.

**Remaining issues to fix:**
- DivMMC `automap_active_` must be cleared during soft_reset (VHDL resets
  `automap_hold`/`automap_held` on soft reset). Without this, DivMMC ROM
  overrides the replayed Spectrum ROM at 0x0000.
- After DivMMC reset, CPU at PC=0x0000 triggers fresh automap (if
  `automap_enabled_` was set by firmware via NR 0x0A). DivMMC ROM runs esxdos
  init → RETN → automap off → Spectrum ROM executes.
- Need to verify replayed data is correct (first bytes should be
  `F3 AF 11 FF FF C3 CB 11` for the 48K ROM).
- Bank 5 replay overwrites ULA screen RAM — acceptable since Spectrum ROM
  reinitializes the screen.

---

## SD Card Image Details

```
Image: roms/nextzxos-1gb-fat32fix.img (1GB)
Partition: FAT32, starts at sector 63
BPB: 512 B/sector, 16 sect/cluster, 32 reserved, 2 FATs, 2048 sect/FAT
Root cluster: 2, data start: sector 4191
TBBLUE.FW: cluster 11826 (sector 193375), offset 2 sectors
  Clusters 11826-11835 (contiguous), total 160 sectors = 80K
```

---

## Key Emulator Files

| File | Role |
|------|------|
| `src/core/emulator.cpp` | NR 0x02 (reset), NR 0x03 (machine type/config), NR 0x04 (config page), NR 0x0A (automap enable), soft_reset() |
| `src/memory/mmu.h` | `read()`/`write()` hot paths, boot ROM overlay, DivMMC overlay, config page, L2 write-over |
| `src/memory/mmu.cpp` | `set_config_page()`, `divmmc_read/write()` helpers |
| `src/peripheral/divmmc.h` | `is_active()`, automap state, config_mode_, entry points |
| `src/peripheral/divmmc.cpp` | `check_automap()`, `on_retn()`, `write()` (slot 0 read-only) |
| `src/peripheral/sd_card.cpp` | SD card SPI model, all CMD handlers |
| `src/peripheral/spi.cpp` | SPI master, CS handling, read/write dispatch |
| `src/port/nextreg.cpp` | NextREG defaults (machine ID 0x08 at line 9) |

---

## Firmware Source Key Files

| File | Role |
|------|------|
| `app/src/boot.c` | `main()`, `loadFile()`, `load_roms()`, `display_bootscreen()`, `init_registers()` |
| `app/src/fwfile.c` | `fwOpenAndSeek()`, `fwRead()`, `fwSeek()` — TBBLUE.FW block access |
| `app/src/config.c` | `load_config()` — reads config.ini, menu.def |
| `app/src/ff/diskio.c` | `disk_initialize()`, `disk_read()`, `send_cmd()`, `rcvr_datablock()` |
| `app/src/ff/ff.c` | Chan's FatFs — `f_read()` at line 3500, cluster boundary clip at ~3555 |
| `app/src/videomagic.c` | `videoTestActive()` — reads magic from config page bank 2 |
| `app/src/misc.c` | `getCoreBoot()` — reads magic from config page bank 6, `disable_bootrom()` |
| `app/src/crt0.s` | CRT0: SP=0xFFFF, gsinit, call main |
| `app/Makefile` | Build flags: `--data-loc 0 --code-loc 0x6010 --no-std-crt0` |
| `hardware.h` | HWID_EMULATORS=0x08, RAMPAGE constants, register definitions |
| `loader/src/main.c` | Boot loader: `mem = 0x6000`, reads TBBLUE.FW blocks, `jp 0x6000` |

---

## Commits

| Hash | Date | Description |
|------|------|-------------|
| 5bef014 | 2026-03-29 | SPI model rewrite: split read/write, CS deselect, CMD1 |
| 971aaee | 2026-03-29 | NCR busy byte before R1 |
| 7723008 | 2026-03-29 | CMD12 stuff bytes (8×0xFF) |
| 93dc7bc | 2026-03-30 | Machine ID, FAT32 image, CMD18, soft reset |
| 6b947ec | 2026-03-31 | Writable boot ROM SRAM, config_mode sync, automap control |
| 9e4243b | 2026-04-01 | Delayed screenshot + automatic exit CLI options |
