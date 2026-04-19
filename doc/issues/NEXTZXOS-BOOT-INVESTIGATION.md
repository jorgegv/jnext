# NextZXOS Boot Investigation — Investigation Journal

This document is the chronological journal of the NextZXOS boot effort on JNext.
All relevant findings, hypotheses, attempts, and conclusions are kept here, one
entry per session.  New investigation notes go at the **bottom** (most recent
first was rejected to keep the narrative readable).

- **Goal:** boot the official NextZXOS / tbblue firmware from an SD image all
  the way to the NextZXOS welcome screen / BASIC prompt.
- **Authoritative hardware spec:** `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
  and friends.
- **TBBLUE firmware (GPLv3):** <https://gitlab.com/thesmog358/tbblue/-/tree/master/src/firmware>
- **Canonical SD image used for testing:** `roms/nextzxos-1gb-fat32fix.img`
  (the `-fat32fix` variant uses 8 KB clusters / 261 877 data clusters — valid
  FAT32 per FatFs. The original `roms/nextzxos-1gb.img` has 32 KB clusters /
  32 758 data clusters, below the FAT spec minimum of 65 525 → rejected by
  tbblue.fw's FatFs. See 2026-04-18 Stage C.)
- **Typical boot command:**
  ```
  ./build/jnext --machine next \
      --boot-rom roms/nextboot.rom \
      --divmmc-rom roms/enNxtmmc.rom \
      --sd-card   roms/nextzxos-1gb-fat32fix.img
  ```

Related documents:

- `doc/design/FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` — design plan for bypassing
  tbblue.fw by initialising NextZXOS from the emulator directly (Task 13).
- `doc/analysis/WAY-FORWARD-2026-03-31.md` — project state + strategy overview
  written when the investigation first hit a wall.

---

## Boot sequence overview (reference)

```
Boot ROM phase (nextboot.rom):
  CMD12 → CMD0 → CMD8 → ACMD41 → CMD58 (SD init)
  → reads TBBLUE.FW from SD (~55 sectors)
  → NR 0x07=0x03 (28 MHz), NR 0x03=0xB0 (disable boot ROM)
  → JP 0x6000 (start tbblue.fw)

tbblue.fw (at 0x6000, SDCC-built, crt0 sets SP=0xFFFF):
  CMD12 → CMD0 → CMD8 → ACMD41 → CMD58 (SD reinit via diskio.c)
  → f_mount → f_open config.ini → reads config
  → f_open menu.def → reads menu config
  → check_coreversion() → returns (HWID_EMULATORS=0x08)
  → display_bootscreen() → reads TBBLUE.FW screens data
  → load_keymap() → writes keymap to config page bank 0
  → load_roms() → writes DivMMC, MF, +3 ROMs (banks 0..3) via config pages
  → init_registers() → sets NR 0x05, 0x06, 0x08, 0x09, 0x0A, NR 0x82-0x85
  → NR 0x03=0xB3 (machine type +3, config_mode OFF)
  → NR 0x02=0x01 (soft reset)

Post soft-reset:
  enNextZX.rom at 0x0000 = F3 C3 EF 00 = DI; JP 0x00EF
  → NR 0x07=3, NR 0x03=0xB0, NR 0xC0=0x08 (stackless NMI)
  → NR 0x82-0x85=0xFF; NR 0x80/0x81/0x8A/0x8F=0
  → LDIR clear of 0x5800-0x5AFF (attr area, L=0)
  → 112-bank RAM test (NR 0x56 sweep 0x00..0xDE)
  → Peripheral init (NR 0x05/0x06/0x08/0x0A/0xD8/0x8E/0xB8-0xBB/0xC0)
  → LD SP, 0x5BFF ; RST 0x20 ; IM 1
  → Should hand control to NextZXOS…
```

---

## Key architectural facts (authoritative)

### VHDL `nr_03_config_mode` state machine

`zxnext.vhd:1102` — power-on default `= '1'`. State machine at
`zxnext.vhd:5137-5151`:

- NR 0x03 write bits `[2:0] = 111` → `config_mode <= '1'`
- NR 0x03 write bits `[2:0] = 001..100` → `config_mode <= '0'` (machine type commits)
- NR 0x03 write bits `[2:0] = 000` → no change
- Machine-type select is **ignored unless `config_mode = '1'`**

Signal gates (non-exhaustive): `zxnext.vhd:2102, 3044, 3319, 5167, 5192, 5209,
5682, 5700, 6370`.

### VHDL MMU page mapping (the +0x20 shift)

`zxnext.vhd:2964` — `mmu_A21_A13` formula:

```
mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7:5))) & mem_active_page(4:0);
```

Equivalent to `sram = (page + 0x20) mod 256` with two dual-port exceptions
(`zxnext.vhd:2961-2962`): bank 5 (pages 0x0A/0x0B) and bank 7 lower (page 0x0E)
bypass the shift.

**Implication:** in Next mode the MMU stores *logical* pages (what firmware
writes to NR 0x50-0x57 / port 0x7FFD), and applies the shift at every SRAM
access site. Writes via config_mode (`NR 0x04`) use an **unshifted** path per
`zxnext.vhd:3045`.

### VHDL DivMMC automap (summary)

Source: `device/divmmc.vhd`, `zxnext.vhd`.

**Power-on defaults:**
- `nr_0a_divmmc_automap_en = '0'` → automap **disabled**
- `nr_03_config_mode = '1'` → config mode ON
- `nr_83_internal_port_enable = all '1'` → port 0xE3 enabled
- `nr_b8_divmmc_ep_0 = 0x83`, `nr_b9_divmmc_ep_valid_0 = 0x01` → only RST 0x0000 valid
- `nr_ba_ep_timing_0 = 0x00` → all entry points delayed

**Activation** (6 pathways, M1 cycle only): RST entry points gated by `entry_points_0
AND entry_valid_0`; NMI (0x0066) via `entry_points_1` bits 0-1; ROM3 substitution /
tape traps via `entry_points_1` bits 2-5; instant vs delayed selected by
`entry_timing_0`; delayed = activates on NEXT M1+MREQ, instant = same cycle.

**Deactivation:**
- RETN instruction (`i_retn_seen` clears `automap_hold` and `automap_held`)
- Delayed off: PC at 0x1FF8-0x1FFF when NR 0xBB bit 6 = 1
- Automap reset: forced when `port_divmmc_io_en='0'` OR `nr_0a_divmmc_automap_en='0'`
- Hard/soft reset clears `automap_hold`/`automap_held`

**Hold logic (`divmmc.vhd:123-148`):**
```
On M1+MREQ cycle:
  automap_hold = (triggers) OR (automap_held AND NOT (active AND delayed_off))
When MREQ deasserts:
  automap_held = automap_hold
Final:
  automap = (NOT automap_reset) AND (automap_held OR instant_triggers)
```
Automap persists when execution leaves 0x0000-0x3FFF — only the specific
deactivation conditions clear it.

**Memory priority (`zxnext.vhd:3081-3133`):**
```
1. DivMMC ROM (divmmc_rom_en AND override bit 2) → rdonly=1, writes DISCARDED
2. DivMMC RAM (divmmc_ram_en AND override bit 2) → rdonly varies
3. Layer 2 write-over
4. ROMCS (expansion bus)
5. Alt ROM
6. Default: config page (if config_mode) or normal MMU
```

DivMMC has priority over config page. Writes to DivMMC ROM area are **silently
lost**, not redirected.

### VHDL SPI master (pipeline delay)

`spi_master.vhd:82` — `spi_begin <= '1' when ... (i_spi_rd = '1' or i_spi_wr = '1')`.
Both reads and writes trigger a transfer.  Reads return the **previous** byte;
the new transfer result lands in `miso_dat` at `state_last_d`.

Emulator equivalent in `spi.cpp::read_data()`:
```
uint8_t prev = rx_data_;
rx_data_ = dev->exchange(0xFF);
return prev;
```
Getting this wrong causes either infinite polling or missed responses.

### TBBLUE firmware layout (app/)

From `app/Makefile`: `sdcc --code-loc 0x6010 --data-loc 0 --no-std-crt0`.

- **0x0000-0x1FFF** `_DATA + _INITIALIZED + _BSS`: ~8 KB of firmware globals
  including **FATFS** (~552 bytes), `FIL`, `line[256]`, `fwMap[512]`,
  `scratch[512]`. On real HW this is config-page RAM (bank 0, pages 0x00/0x01).
  In emulator-with-automap, DivMMC ROM covers 0x0000-0x1FFF and DivMMC RAM
  covers 0x2000-0x3FFF.
- **0x4000-0x5FFF** ULA screen RAM (bank 5, pages 0x0A/0x0B).
- **0x6000** CRT0 `LD SP,0xFFFF; DI; CALL gsinit; CALL _main; JP _exit`.
- **0x6010+** `_CODE`.
- **Stack** descends from 0xFFFF (slot 7 = page 0x01).

Other build flags: `ROFLAGS = -D_FS_READONLY=1 -D_FS_MINIMIZE=1`.

### TBBLUE.FW block map (from SD image, sector 193377, first 512 bytes)

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

Constants: `FW_L2_PAL_SIZE=227`, `RAMPAGE_ROMSPECCY=0x00`,
`RAMPAGE_ROMDIVMMC=0x04`, `RAMPAGE_ROMMF=0x05`, `RAMPAGE_ALTROM0=0x06`,
`HWID_EMULATORS=0x08` (our emulator reports this from NR 0x00).

---

## Journal

### 2026-03-22 — Initial task: boot from SD image

Baseline task: "Test NextZXOS boot from SD image" (Task 11 of the 2026-03-22
prompt). Investigation not yet started; acts as scope marker.

### 2026-03-29 — SPI model rewrite (matching ZesarUX)

Reference: `/home/jorgegv/src/spectrum/zesarux/src/storage/mmc.c`.

The original SPI used VHDL-accurate full-duplex with pipeline delay; that caused
response byte offsets, leftover data corrupting subsequent commands, and CMD8
R7 response issues.  The rewrite uses **independent read/write paths** like
ZesarUX:

- `receive(tx)` — command/data write path
- `send()` — response read path
- `deselect()` — CS high resets all protocol state to IDLE

Response formats, verified against firmware `diskio.c`:
```
CMD0:  NCR(0xFF) + R1(0x01)
CMD8:  NCR(0xFF) + R1(0x01) + R7(0x00, 0x00, 0x01, check_pattern)
CMD12: 8×0xFF (stuff bytes) + NCR(0xFF) + R1
CMD17: NCR(0xFF) + R1(0x00) + 0xFE (data token) + 512 data + 2 CRC
CMD18: NCR(0xFF) + R1(0x00) + [0xFE + 512 data + 2 CRC] per block + 0xFF gap
CMD58: NCR(0xFF) + R1 + OCR(4 bytes: 0xC0=CCS+powered, 0xFF, 0x80, 0x00)
ACMD41: NCR(0xFF) + R1(0x00)
```

Firmware `send_cmd()` sequence (from `diskio.c`): `select()` (CS low + 1 dummy
read); `xmit_mmc(buf, 6)`; if CMD12 `rcvr_mmc(buf, 8)` skip stuff bytes;
`wait_response()` reads up to 250 bytes until non-0xFF; return R1.

Boot ROM now works fully.  **Blocker at session end:** post-boot firmware reads
MBR+BPB correctly but retries 3× — issue is NOT SPI (data verified byte-perfect).
Likely BPB processing logic or a missing emulator feature.

**Commits:** `5bef014` (SPI rewrite, split read/write, CS deselect, CMD1),
`971aaee` (NCR busy byte before R1), `7723008` (CMD12 stuff bytes).

### 2026-03-31 — Debugging features + first big investigation

Added CLI options `--delayed-screenshot`, `--delayed-screenshot-time`,
`--delayed-automatic-exit` to allow headless automated testing. Comprehensive
analysis written at `doc/analysis/WAY-FORWARD-2026-03-31.md`.

Task 2 status: "NextZXOS boot — root cause fully analyzed, closest approach was
config page write recording + soft_reset replay (96K across 6 banks replayed,
but DivMMC automap reset needed on soft_reset)."

Commits: `9e4243b` (CLI options).

### 2026-04-01 — Root cause analysis: DivMMC / config page conflict

**Branch:** `fix-nextzxos-boot` (not merged at the time).

**Status:** firmware boot sequence completes — all ROMs load, display shows
"Firmware v1.44.db / Core v3.02.00", machine type 0xB3 set, soft reset
triggers.  But ROM data does not reach the correct RAM pages after soft reset.
The CPU at 0x0000 finds boot-loader residue instead of Spectrum ROM.

**What works:**
- Boot ROM loads TBBLUE.FW from SD image
- Post-boot FatFs mounts FAT32, reads `config.ini`, `menu.def`
- Firmware loads all ROMs (keymap, ESXMMC, Multiface, Spectrum)
- Firmware displays "Firmware v1.44.db / Core v3.02.00"
- Machine type 0xB3 (+3) set, soft reset triggered
- Machine ID 0x08 (HWID_EMULATORS) correctly returned

**The fundamental conflict:**
1. Firmware globals (FATFS, FIL, line[], etc.) live at 0x0000-0x1FFF
2. Config-page writes (ROM loading) also target 0x0000-0x3FFF
3. On real VHDL hardware: automap OFF → no DivMMC → config page provides consistent storage
4. In emulator: automap ON → DivMMC ROM at 0x0000-0x1FFF, DivMMC RAM at 0x2000-0x3FFF

**Why automap MUST stay ON in the emulator:**
- With automap OFF, config-page RAM is at 0x0000-0x3FFF (correct)
- But FatFs `f_read` writes ROM data to 0x0000, overwriting its own FATFS struct at ~0x03C8
- FatFs reads one CLUSTER at a time (clip at cluster boundary in `f_read`, `ff.c ~3555`)
- After first cluster read (13-14 sectors), FATFS is corrupted → can't follow chain
- Result: "Error reading TBBLUE.FW data!" from `fwfile.c:fwRead`

**Why automap ON "works" (but data goes wrong place):**
- DivMMC ROM at 0x0000-0x1FFF: reads return esxdos code, writes discarded
- DivMMC RAM at 0x2000-0x3FFF: consistent read/write for FatFs globals
- esxdos byte at offset 0x003A happens to be 0x00 → FatFs sees "not mounted" → fresh mount
- FatFs works because its critical structures (FATFS, FIL) are in 0x2000+ DivMMC RAM range
- Config page writes intercepted by DivMMC: slot 0 discarded, slot 1 to DivMMC RAM

**The chicken-and-egg:**
- Automap ON: firmware completes but config-page writes are intercepted by DivMMC
- Automap OFF: config-page writes land correctly but FatFs self-corrupts during `f_read`

**FatFs self-corruption detail** (`ff.c ~3555`):
```c
if (csect + cc > fs->csize) {    /* Clip at cluster boundary */
    cc = fs->csize - csect;
}
disk_read(fs->drv, rbuff, sect, cc);
```
With cluster size 16 and 13 sectors remaining in the current cluster, `disk_read`
gets 13 sectors. After ~1 sector (512 bytes) the FATFS struct at ~0x03C8 is
overwritten. `_FS_MINIMIZE=1` means no fast-seek or cluster-map.

**Approaches tried and failed this session:**

| # | Approach | Result |
|---|----------|--------|
| 1 | DivMmc `config_mode_=true` initially | DivMMC inactive; fwRead fails (FATFS overwrite) |
| 2 | Write-only bypass (skip DivMMC writes when config_mode) | Read/write asymmetry breaks globals |
| 3 | Shadow writes (dual to DivMMC + config page) | Config page slots still ROM (read-only) early in boot |
| 4 | Default config page on boot-ROM disable | Config page set up; fwRead still fails |
| 5 | Config page write recording + soft_reset replay | **96K replayed correctly**; screen corrupted because DivMMC automap not reset on soft_reset |
| 6 | Automap OFF (matching VHDL default) | "Error reading TBBLUE.FW data!" (FatFs self-corruption) |
| 7 | DivMmc `config_mode_=false` + automap OFF | Same as #6 |

**Previous-session attempts (2026-03-30, from memory):**

| # | Approach | Result |
|---|----------|--------|
| A | config_mode_ starting true (matching VHDL) | Broke boot ROM (DivMMC ROM not mapped) |
| B | Skip DivMMC for all writes < 0x4000 when config_mode | Broke boot ROM (DivMMC RAM buffers) |
| C | Skip DivMMC when config_mode AND boot_rom_en=false | Broke boot ROM |
| D | DivMMC automap disabled by default | Firmware hung after 3 config page writes |
| E | divmmc_write returns false for slot 0 when config_mode | Same as C |
| F | entry_valid_0_ mask in check_automap | Caused boot failures |
| G | RETN detection (ED 45) clearing automap | Never triggered during boot |

**Closest fix (Approach #5):** record all config-mode writes to 0x0000-0x3FFF
while automap is ON; replay them to correct RAM pages at soft_reset. 96K
recorded across 6 banks (0-5), successfully replayed. Remaining issues:
- DivMMC `automap_active_` must be cleared during soft_reset (VHDL resets
  `automap_hold`/`automap_held` on soft reset). Without this, DivMMC ROM
  overrides the replayed Spectrum ROM at 0x0000.
- After DivMMC reset, CPU at PC=0x0000 triggers fresh automap. DivMMC ROM runs
  esxdos init → RETN → automap off → Spectrum ROM executes.
- Need to verify replayed data is correct (first bytes should be
  `F3 AF 11 FF FF C3 CB 11` for the 48K ROM).
- Bank 5 replay overwrites ULA screen RAM — acceptable since Spectrum ROM
  reinitialises the screen.

**Unresolved question:** how does this work on real hardware? The same FatFs
code, same cluster size, same self-corruption should occur. Possible
explanations (not verified):
1. The pre-built firmware binary may use different `--data-loc` than the source
   Makefile shows, placing FATFS above the write range.
2. The real-hardware SD image may have TBBLUE.FW at a cluster boundary, giving
   more sectors in the first cluster (enough to complete the full read).
3. The firmware binary on the SD image may be a different version with
   different memory layout or FatFs configuration.
4. There may be a VHDL mechanism that provides a "shadow" write path during
   config mode that we haven't identified.

**SD card image details:**
```
Image: roms/nextzxos-1gb-fat32fix.img (1 GB)
Partition: FAT32, starts at sector 63
BPB: 512 B/sector, 16 sect/cluster, 32 reserved, 2 FATs, 2048 sect/FAT
Root cluster: 2, data start: sector 4191
TBBLUE.FW: cluster 11826 (sector 193375), offset 2 sectors
  Clusters 11826-11835 (contiguous), 160 sectors = 80 KB
```

**Key emulator files at this point:**

| File | Role |
|------|------|
| `src/core/emulator.cpp` | NR 0x02 (reset), NR 0x03 (machine type/config), NR 0x04 (config page), NR 0x0A (automap enable), soft_reset() |
| `src/memory/mmu.h` | `read()`/`write()` hot paths, boot-ROM overlay, DivMMC overlay, config page, L2 write-over |
| `src/memory/mmu.cpp` | `set_config_page()`, `divmmc_read/write()` helpers |
| `src/peripheral/divmmc.h` | `is_active()`, automap state, config_mode_, entry points |
| `src/peripheral/divmmc.cpp` | `check_automap()`, `on_retn()`, `write()` (slot 0 read-only) |
| `src/peripheral/sd_card.cpp` | SD card SPI model, all CMD handlers |
| `src/peripheral/spi.cpp` | SPI master, CS handling |
| `src/port/nextreg.cpp` | NextREG defaults (machine ID 0x08 at line 9) |

### 2026-04-18 — Task 9 Stage A: `nr_03_config_mode` state machine

Context: previous memory claimed "boot runs to soft reset; 7 failed fix
attempts."  Post DivMMC+SPI Phases 1-4 (session 2026-04-17g) + machine-ID 0x08
fix, boot now reaches tbblue.fw's embedded **Config tool** and displays
"ZX Spectrum Next Configuration" + "Error opening 'menu.ini/.def'!".
CSpect shows the welcome page from `/nextzxos/autoexec.1st` instead — different
flow.

**Proved correct (not the bug):**

1. **SD byte delivery** — for every observed `CMD17 READ_SINGLE_BLOCK`, the
   first 8 bytes our emulator sends match the raw image byte-for-byte
   (sectors 0 / 63 / 610 / 197922 verified).
2. **SPI pipeline byte count** — each CMD17 is 6 writes + 518 reads (1 NCR +
   1 R1 + 1 token 0xFE + 512 data + 2 CRC + 1 trailing 0xFF). Matches SD SPI
   spec.
3. **Z80 reads all 512 bytes per sector** — not a short-read.
4. **Machine ID** — `src/port/nextreg.cpp:18` returns 0x08 (HWID_EMULATORS).

**SD activity pattern observed:**
- **Pass 1** (nextboot.rom + tbblue.fw first-stage loader): full SD init
  (CMD0/8/55/ACMD41/58), CMD17 sector=0 (MBR), CMD17 sector=63 (VBR), CMD17
  sector=610 × 2 (root directory cluster), then CMD17 sector=197922..197976
  (55 consecutive sectors of `/TBBLUE.FW` — ~28 KB of first-stage code).
- **Pass 2/3/4** (after tbblue.fw first-stage takes over): full SD re-init,
  then CMD17 sector=0, CMD17 sector=63, stop. Never reads FAT, never reads
  `/machines/next/`, never reads `menu.def`.

The divergence lives in tbblue.fw's first-stage code: re-inits SD correctly but
refuses to proceed past VBR validation.  Most likely: an FPGA/NR-state gate
(probably `nr_03_config_mode`) holds it there because we don't clear
config_mode.

**Plan A → B → C:**

- **Stage A (in progress this session):** model `nr_03_config_mode` as a proper
  state machine. Add `bool nr_03_config_mode_ = true;` to `NextReg`; install a
  write_handler for NR 0x03 implementing the `001-100 → 0`, `111 → 1`,
  `000 → no change` transition rules.  Expose getter.  Initially don't wire
  the gated behaviours — verify boot change from state tracking alone.
- **Stage B (if A insufficient):** accept tbblue.fw Config tool may be
  faithful real-HW first-boot behaviour.  Add CSpect-style fast-boot flag or
  auto-clear config_mode in headless mode.
- **Stage C (if still stuck):** extract `/TBBLUE.FW`, disassemble the
  first-stage Z80 code, trace the "normal boot" vs "Config tool" decision
  point.

Commits: `e73df39` (auto-regen version.h), `0077d3e` (rename --machine-type
→ --machine), `396a63a` (NR 0x00 = 0x08 = HWID_EMULATORS).

### 2026-04-18 — Task 9 Stage C findings: two stacked bugs

After Stage A+B didn't unblock, Stage C reading tbblue.fw source narrowed the
failure to two **stacked** bugs.

**Bug 1 — SD image format (not a jnext bug).**  `roms/nextzxos-1gb.img`
(the supplied official-looking image) is 1024 MB with `SecPerClus = 64`
(32 KB clusters). That yields `(2097089 - 547) / 64 = 32758` data clusters.
FAT spec defines FAT32 as `clusters > MAX_FAT16 = 0xFFF5 (65525)`; the image
has fewer, so tbblue.fw's FatFs (`ff.c`) categorises it as FAT16 at
`ff.c:3145`, then the FAT16 branch at `ff.c:3159` returns `FR_NO_FILESYSTEM`
because `n_rootdir = 0` (the VBR has FAT32 layout, not FAT16). Three
consecutive `f_open` calls in `load_config()` (`config.ini`, `menu.ini`,
`menu.def`) each re-trigger `find_volume` → same rejection → `display_error("Error
opening 'menu.ini/.def'!")` and `for(;;)` halt. CSpect must use its own FS
driver that tolerates this image.

**Bug 2 — missing CMD18 (READ_MULTIPLE_BLOCK).** Once the image is reformatted
with valid FAT32 parameters (512 MB, 4 KB clusters, 130 557 clusters),
tbblue.fw's mount succeeds.  Boot progresses further — 76 CMD17 reads reaching
`/TBBLUE.FW` content at sectors 4120+, plus directory traversal reads. Then
`sd_card.cpp` logs `unhandled CMD18 arg=0x0000110b` and tbblue.fw shows
"Error reading TBBLUE.FW data!".  Our `SdCardDevice::process_command` had no
case for CMD18 and fell through to `queue_r1(initialized_ ? 0x00 : 0x01)` which
responds with R1=0x00 but **no data** — FatFs `rcvr_datablock` waits for the
0xFE token and eventually times out.

**Why the earlier byte-for-byte SD check didn't surface Bug 1:** the byte
delivery was correct; the CONTENT (the VBR's BPB) described a filesystem that
FatFs rejected. No byte-level validation can catch this — the bytes are valid,
it's the semantics that violate spec.

**Empirical reproduction** (build a valid FAT32 image from files in
`/tmp/nextzxos-extract/`):
```bash
rm -f /tmp/test-fat32.img
dd if=/dev/zero of=/tmp/test-fat32.img bs=1M count=512
(echo o; echo n; echo p; echo 1; echo 2048; echo ''; echo t; echo c; echo w) \
    | fdisk /tmp/test-fat32.img
mformat -i /tmp/test-fat32.img@@$((2048*512)) -F -c 8 -T $((512*2048 - 2048)) \
        -h 255 -s 63 ::
mcopy -i ...@@$((2048*512)) .../TBBLUE.FW  "::TBBLUE.FW"
mcopy -i ...@@$((2048*512)) .../TBBLUE.TBU "::TBBLUE.TBU"
mmd   -i ...@@$((2048*512)) "::machines" "::machines/next"
mcopy -i ...@@$((2048*512)) .../menu.def    "::machines/next/menu.def"
```

Source references (tbblue repo at `/home/jorgegv/src/spectrum/tbblue`):
- `src/firmware/app/src/ff/ff.c:3145` — `if (nclst <= MAX_FAT16) fmt = FS_FAT16;`
- `src/firmware/app/src/ff/ff.c:3159` — `if (fs->n_rootdir == 0) return FR_NO_FILESYSTEM;`
- `src/firmware/app/src/ff/ff.c:379-381` — `MAX_FAT12/16/32` constants.
- `src/firmware/app/src/config.c:345-354` — `load_config()` trying menu.ini then menu.def.
- `src/firmware/app/src/misc.c:31-46` — `display_error()` is fatal.
- `src/firmware/app/src/ff/diskio.c:152-170` — `rcvr_datablock()` polls for 0xFE.

**Useful BPB constants (from the under-clustered image):**
- MBR partition 0: `bootable=0x80, type=0x0C, StLba=63`.
- VBR BPB: `BytsPerSec=512, SecPerClus=64, RsvdSecCnt=33, NumFATs=2,
  FATSz32=257, TotSec32=2097089, RootClus32=2`.
- For valid FAT32 on 1 GB: `SecPerClus` must be ≤ 16 (8 KB clusters).

**Follow-up decided:** implement CMD18 (READ_MULTIPLE_BLOCK) in
`src/peripheral/sd_card.cpp` (R1=0x00, then repeat `[0xFE + 512 bytes + 2 CRC]`
until host sends CMD12 STOP_TRANSMISSION).  The canonical test image becomes
`roms/nextzxos-1gb-fat32fix.img` (valid FAT32).  The under-clustered image
stays as `roms/nextzxos-1gb.img` for historical reference — do not relax our
SD driver to accept it (firmware-faithful is the right posture).

Commits: `93dc7bc` (machine ID, FAT32 image, CMD18, soft reset — this batch).

### 2026-04-18 — Task 11: NextZXOS architectural prerequisites

Goal: implement the 4 VHDL prerequisites needed before tbblue.fw can soft-reset
into enNextZX.rom. Merged in 3 branches / 6 commits `f3c42ff..88a1c15`.

- **Branch 1 — NR 0x04 + config_mode SRAM routing** (merged `f3c42ff..7d0b0fa`):
  - VHDL `zxnext.vhd:3044-3050` config-mode memory routing for 0x0000-0x3FFF ROM slots.
  - NR 0x04 (`romram_bank`) handler added to NextReg + mirrored into Mmu.
  - Reviewer corrected priority: Boot ROM > MF > MMU-RAM > DivMMC > Layer 2 >
    config_mode > sram_rom (VHDL arbiter `zxnext.vhd:3084-3132`).

- **Branch 2 — ROM-in-SRAM serving** (merged `75d87fb..00fe3ab`):
  - VHDL `zxnext.vhd:3052` — Next ROM lives in SRAM pages 0..7, no separate
    ROM chip.
  - `Emulator::init()` seeds `ram_` pages 0..7 from `rom_` at hard reset;
    `Mmu::set_rom_in_sram(true)` re-points ROM-slot `read_ptr_` at `ram_`.

- **Branch 3 — soft-reset preserves SRAM + boot_rom_en** (merged
  `eb78d43..88a1c15`):
  - `Emulator::init(cfg, preserve_memory)` overload gates
    ram/rom/boot-reload/SRAM-seed.
  - New `Emulator::soft_reset()` preserves RAM and restores `boot_rom_en`
    across `Mmu::reset`.
  - NR 0x02 handler: bit 0 → soft, bit 1 → hard (hard wins), bit 7 → no-op.
    VHDL cited at `zxnext.vhd:1101,5109-5111,5122` for `bootrom_en`.

**Tests:** Unit 2668/0/565 (+19 across all 3 branches: CFG-01..11, PRI-07,
SR-01..07). Regression 34/0/0 throughout.

**Empirical result:** tbblue.fw still stalls at **3 NR 0x04 writes**
(`0x02, 0x06, 0x00`) before reaching `load_roms()` or `RESET_SOFT`.  My fixes
are never exercised because firmware hangs upstream.  Boot symptom unchanged
vs. pre-Branch-1.  Do NOT revisit Task 11 mechanics when debugging further —
the VHDL-cited paths are correct.

### 2026-04-18 — Task 12: MMU slot 6/7 aliased ROM-in-SRAM (ROOT CAUSE FOUND)

Task 12 was framed as "firmware stalls at 3 NR 0x04 writes upstream of
RESET_SOFT". Turned out to be a one-line MMU bug, not firmware-interaction.

**Trace fingerprint (pre-fix, on canonical `roms/nextzxos-1gb-fat32fix.img`):**
- NR 0x03 = 0xB0 → boot ROM disabled, config_mode=1
- NR 0x03 = 0x00 → config_mode stays 1 (000 = no change)
- NR 0x02 = 0x80 → RESET_ESPBUS only (no-op for us)
- NR 0x2B palette init (24 writes)
- NR 0x04 = 0x02, then NR 0x11/0x05/0x09, then NR 0x04 = 0x06, then NR 0x04 = 0x00
- Silence for 80+ seconds. No more NR writes. No SD CMD17/18 reads.

The (0x02, 0x06, 0x00) NR 0x04 pattern is menu/config-parse paging, NOT
`load_roms()`.  Expected `load_roms()` pattern for 128K mode: NR 0x04 = 0x04
(DivMMC), 0x05 (MF), 0x00, 0x01 (Speccy).

**Root cause:** `Mmu::map_128k_bank(port_7ffd)` mapped port_7ffd bank N to
SRAM pages `N*2` and `N*2+1`.  In Next mode with `rom_in_sram_=true`
(Task 11 Branch 2), SRAM pages 0-7 hold ROM-in-SRAM (Spectrum ROM).  So
bank 0 (port_7ffd=0 reset default) put slots 6/7 on SRAM pages 0/1 —
**aliased with the ROM area**.

tbblue.fw's FATFS global lives at RAM 0xCA94-0xCCC2 (slot 6).  When
`display_bootscreen()` called `fwRead((uchar*)0x0000, 0x2800)`, the 10 KB
write into 0x0000-0x27FF went via config_mode NR 0x04 = 0x00 routing to
SRAM page 0.  **Same physical page as FATFS.**  FATFS was silently overwritten
at offsets 0xA94-0xCC2 → `get_fat()` returned garbage → `f_read` aborted →
`display_error("Error reading TBBLUE.FW data!")`.

**VHDL citation:** `zxnext.vhd:2964` —
```
mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7:5))) & mem_active_page(4:0);
```
+0x20 shift on the MSBs. For `mem_active_page = 0`: SRAM page 0x20
(RAMPAGE_RAMSPECCY base), NOT page 0.

**Fix (commit `5896384`, 2 hunks):**
- `src/memory/mmu.cpp::map_128k_bank`: when `rom_in_sram_=true`, `base = 0x20`;
  legacy 0 otherwise. Non-Next paths unchanged.
- `src/core/emulator.cpp::init()`: after `mmu_.set_rom_in_sram(true)`, call
  `mmu_.map_128k_bank(0)` to refresh slots 6/7 from their RESET_PAGES seed
  (RESET_PAGES still has raw 0x00, 0x01 — tbblue.fw never writes port_7FFD
  during its prologue, so without this refresh the buggy values persist from
  reset).

**Empirical post-fix:** firmware reaches `load_roms()` and displays
"Loading ROM: enNextZX.rom... OK!". Task 12's RESET_SOFT-stall fingerprint is
eliminated. NextZXOS boot still doesn't complete — separate bug downstream.

**Latent issue (not fixed):** `src/core/nex_loader.cpp` and other `set_page()`
callers still use raw `bank*2` (no +0x20 shift). Works today for NEX content
that avoids bank 0, but is technically wrong under the same VHDL rule.

Commits: `5896384` (Task 12 map_128k_bank fix), `4dbceb7` (Task 12 root-cause
notes + Task 13 bypass-tbblue-fw plan).

### 2026-04-18 — Task 12c deep analysis: the +0x20 shift is architectural

After Task 12 fixed slot 6/7, NextZXOS stalled at the next layer. Investigation
showed that **slot 4/5 alias** with SRAM pages 0x04/0x05 (DIVMMC/MF ROM pages)
is the same pattern as Task 12 but at a different slot.

**Empirical PC trace this session:**
- Firmware runs clean through spacebar wait loop (65535 iters of
  `videoTestActive` calling `B796 → B974 strncmp`).
- After wait loop: `vdp_clear`, `load_keymap` (keymap NR 0x28-0x2B writes),
  `load_roms`.
- `load_roms` sequence: NR 0x04 = 0x04 (DivMMC), 0x05 (MF), 0x00, 0x01,
  0x02 (Speccy banks 0/1/2).
- **Immediately after NR 0x04 = 0x02 write, firmware's slot-4 content is
  clobbered** — at RAM 0x9F8B the bytes are now `c6 05 cd 65 07` (ROM), not
  the app code.
- Next instruction fetch lands in ROM garbage; flow eventually reaches
  PC 0x9F8B → `CALL 0x0765` which jumps into Speccy ROM area. Emulator
  oscillates in 0x0000-0x1EA3 (Speccy ROM execution from slot 0/1 via
  config_mode NR 0x04 = 0x02). No NR 0x02 RESET_SOFT, no further firmware
  progress.

**Places that need the +0x20 shift in Next mode (audit):**
1. **`Mmu::slots_[6]/[7]`** — FIXED by Task 12.
2. **`Mmu::slots_[2]/[3]`** (bank 5 VRAM 0x4000-0x7FFF) — NOT FIXED. RESET_PAGES
   seeds 0x0A/0x0B. In Next mode should be 0x2A/0x2B.
3. **`Mmu::slots_[4]/[5]`** (bank 2 at 0x8000-0xBFFF) — NOT FIXED. RESET_PAGES
   seeds 0x04/0x05. In Next mode should be 0x24/0x25. **This is today's stall.**
4. **ULA bank-5 VRAM fetch** — hardcoded physical pages 0x0A/0x0B in
   `src/video/ula.cpp`. In Next mode should read 0x2A/0x2B — only if slot 2/3
   writes also go there. Currently internally consistent; moving them together
   is required.
5. **Tilemap bank-5/7 fetch** — same pattern as ULA.
6. **Sprite SRAM fetch** — not audited; subagent flagged likely correct.
7. **Layer 2** — has its own separate +1 bank transform (different concept).
8. **`nex_loader.cpp:342-347` `entry_bank * 2`** — latent bug in Next mode.
9. **`Emulator::init` ROM-in-SRAM seed** (`emulator.cpp:1268-1274`): copies
   `rom_.page_ptr(p) → ram_.page_ptr(p)` for p=0..7 — correct (matches VHDL
   ROM-in-SRAM layout).

**Why the narrow slot-4/5-only attempt broke things (Task 12b revert):**
- (a) ULA still reads bank-5 VRAM from SRAM 0x0A/0x0B → screen rendering
  diverged.
- (b) The app code tbblue.fw's ROM layer wrote into slot-3 RAM (0x6000-0x7FFF)
  at emulator boot time landed at SRAM page 0x0B (via `ram_.write(0x6000+i,
  val)` during nextboot.rom execution which did NOT go through shifted slot
  mapping). After shift, slot 3 pointed to 0x2B → empty → CPU ran garbage from
  0x6000 and tbblue.fw's `main()` kept restarting from crt0.

**Conclusion:** fix is architectural, not local. Our MMU stores physical SRAM
indices where VHDL stores logical MMU pages. Centralise the shift.

**Independent reviewer recommendation:** (a) introduce `Mmu::to_sram_page`
helper; (b) apply at every `ram_` access site in Next mode; (c) retire
Task 12's per-site `speccy_base`.

No commits this day beyond the revert restoring clean state. Test state
preserved: 2676/0/557 unit, 34/0/0 regression.

### 2026-04-19 — Task 12c: architectural fix landed

Three iterations before the fix was correct:

- **First attempt.** Apply shift in `rebuild_ptr` + retire `speccy_base`.
  Unit tests pass. Regression: 4 Layer 2 tests fail (dapr-l2empty, etc.).
  Root cause: Layer 2 renderer reads raw SRAM banks while MMU writes go
  through the shift → divergence.
- **Narrow-fix attempt.** Only shift slots 4/5 in `set_rom_in_sram`. Broke 20
  regression tests because NEX loaders write via `mmu.set_page` which would
  need to shift too, but Layer 2 renderer wouldn't — same mismatch, wider.
- **Final fix.** Universal helper + thread `rom_in_sram` flag into Layer 2
  renderer's `compute_ram_addr` AND into the port-0x123B L2 write-over path in
  `mmu.h`. Both paths now apply `to_sram_page`. 2676/0/557 + 34/0/0 clean.

**Helper rule (final):**
```cpp
uint8_t Mmu::to_sram_page(uint8_t logical) const {
    if (!rom_in_sram_) return logical;
    if (logical == 0x0A || logical == 0x0B || logical == 0x0E) return logical;
    return static_cast<uint8_t>(logical + 0x20);
}
```
Matches VHDL `zxnext.vhd:2964` with bank-5/7-lower dual-port exceptions from
`zxnext.vhd:2961-2962`.

**Second commit** (`6920a63`) removed the `>= 0x20` short-circuit after
discovering NextZXOS post-RESET_SOFT writes NR 0x56/0x57 with logical 0x20+
values that VHDL shifts to 0x40+. Full VHDL formula now applied (wraps at
0x100; pages ≥ 0xE0 wrap to SRAM 0x00..0x1F).

**Commits:**
- `9737640` — helper + Layer 2 shift + retire `speccy_base`.
- `6920a63` — apply shift across full page range.
- `7abbe51` — merge `task12c-centralise-shift-helper`.

**Empirical result post-fix:**
- `load_roms` completes ALL 4 +3 ROM banks (previously stalled at bank 2).
- `init_registers` runs — NR 0x05/0x06/0x08/0x09/0x0A + NR 0x82-0x85 all
  written.
- NR 0x03 = 0xB3 (machine-type +3 commit) writes; NR 0x02 = 0x01 (RESET_SOFT)
  fires.
- `Emulator::soft_reset()` runs.
- CPU resumes at 0x0000. enNextZX.rom content at 0x0000 = `F3 C3 EF 00` =
  `DI; JP 0x00EF`.
- At 0x00EF: NR 0x07=3, NR 0x03=0xB0, NR 0xC0=0x08 (stackless NMI),
  NR 0x82-0x85=0xFF, NR 0x80/0x81/0x8A/0x8F=0, NR 0x06 RMW, LDIR clear of
  0x5800-0x5AFF.
- RAM test PASS 1 at 0x0130 (112 iterations, NR 0x56 sweep 0x00..0xDE).
- RAM test PASS 2 at 0x018E (112 iterations verifying 0xBB marker).
- Post-test init: NR 0xD8=0x01, NR 0x05=0x5A, NR 0x08=0x4E, NR 0x06=0xAB/0xA8,
  NR 0x0A=0x10, various NR 0x8E writes, NR 0xB8-0xBB, NR 0xC0.
- At 0x01D1: `LD SP, 0x5BFF`. At 0x01D4: `RST 0x20`. At 0x01DB: `IM 1`.
- CPU eventually settles into DivMMC-automapped IM1 handler (PC samples
  oscillate in 0x0038..0x006E + 0x1FF9, SP=0x26E9).

**Visible state:** blue vertical stripes on display, 8-pixel period, across
the full 256×192 area; border black.  VRAM content (via `debug_dump_vram`
helper, reverted after the session):
```
attr [0x5800..0x581F]: 00 39 00 39 00 39 00 39 00 39 00 39 ...
pixel [0x4000..0x401F]: 00 39 00 39 00 39 00 39 ...
```
Attribute 0x39 = `00 111 001` = blue ink on white paper. With pixel bytes 0x39
in those cells, 5 blue pixels out of 8 → observed stripe pattern. Attributes
were supposedly cleared at enNextZX.rom:0x012A (LDIR with L=0) but something
later rewrites them.

**Ruled-out hypotheses (session 2026-04-19):**
- **SP leak (0xFFFF → 0x26E9 post-reset) is NOT a leak.** `enNxtmmc.rom:0x0052`
  does `LD SP, 0x26ED` — DivMMC IM1 handler's private stack base. SP
  fluctuating between 0x26E7..0x26ED is normal.
- **Stackless NMI (NR 0xC0 bit 3)** is unhandled in our emulator, but no
  caller of `cpu_.request_nmi()` exists anywhere, so no NMIs fire to leak
  stack. Per VHDL `zxnext.vhd:5597-5599`: bit 7:5 `nr_c0_im2_vector`, bit 3
  `nr_c0_stackless_nmi`, bit 0 `nr_c0_int_mode_pulse_0_im2_1`. 0x08 sets bit 3
  only; bit 0 = 0 → pulse mode (IM1-style). So **IM2 hardware mode is NOT in
  play** (earlier misreading corrected by user).
- **DivMMC automap** is working correctly on IM1. `enNxtmmc.rom` at 0x38 is
  `C3 E5 00` (`JP 0x00E5`); at 0x42 is `C3 F9 1F` (`JP 0x1FF9`). CPU runs
  `enNxtmmc.rom`'s handler at 0x00E5, not `enNextZX.rom`'s 0x0038. Handler
  ends cleanly with `EI; RET`.

**Likely remaining issue (top suspect, not yet verified):** `enNextZX.rom` at
0x01D4 does `RST 0x20`.  In +3 BASIC `RST 0x20` is the
"NEXT-BYTE-FROM-ROM-TABLE" subroutine — it expects the caller's return address
to point to a ROM table, reads the byte, advances PC, returns.  If ROM content
at 0x0000 isn't exactly enNextZX bytes (ROM-in-SRAM preservation across
soft_reset, or interaction with DivMMC automap), `RST 0x20` reads garbage and
BASIC goes off the rails → the 0x00/0x39 VRAM pattern may be BASIC
interpreter error output.

**Other hypotheses for the stripes (ordered by likelihood):**
2. **Peripheral gap.** NR 0x8E (MMU advanced) gets 6 writes — if our handler
   drops meaningful state, subsequent reads could be wrong. NR 0xB8-0xBB are
   mystery registers.
3. **DivMMC state mishandled.** Automap has been tested for single-M1 entry
   but RAM test + peripheral init path may push it through edge cases not
   covered (8 of 17 DivMMC SKIPs are Task 8 Multiface deps; 1 is Layer 2
   read-map feeder).
4. **Unintended L2 write-over.** Port 0x123B sets `l2_write_enable_`. If
   post-reset code accidentally enables L2 write-over for segments covering
   0x4000-0x7FFF, CPU writes would go to L2 RAM at shifted SRAM pages — not
   bank 5. Need to check whether `enNextZX.rom` touches port 0x123B post-reset.

**Sites NOT yet audited (deferred follow-up):**
- Tilemap bank-5/7 base fetch — stays physical 0x0A/0x0E (bank-5/7 dual-port
  match).
- Sprite SRAM fetch.
- Copper memory access.
- DMA RAM access.
- NEX-loader `entry_bank * 2` — now correct by accident because `set_page` +
  `rebuild_ptr` apply the helper, but NEX loader's `write_to_page` TEMP_SLOT=7
  path also consistent.
- SNA / SZX loaders — follow NEX pattern; likely fine.
- NR 0xC0 bit 3 (stackless NMI) — unhandled. Not relevant today, will matter
  once Multiface peripheral lands.

**Tests:** 2676/0/557 unit, 34/0/0 regression — unchanged across Task 12c
commits.

**Decision at EOD:** rather than chase the post-reset `RST 0x20` path with
another speculative round, address the Memory/MMU (77 skip) and NextREG
(48 skip) gaps first.  Rationale: momentum from Task 12c; NR 0xC0 bit 3 is a
known unhandled bit that may surface as a SKIP; any MMU/NextREG gaps
affecting NextZXOS will surface during the work; measurable progress every
session vs high-variance debug.  After both subsystems are clean, re-attempt
NextZXOS boot.

---

### 2026-04-20 — Phase 2 boot regression, Option C, NR 0x8E bit-3 gate

**Starting state.** Phase 2 A/A.1/B/C/D1/D2/E MMU+NextREG SKIP-reduction
ladder had landed earlier in the day (7+ merges, tests 2759/0/499). Session
opened with the user's instruction "try boot, I think it will fail. Analyze
and come back with suggestions." The concern was well-founded: boot rendered
**black screen** instead of the 2026-04-19 blue-stripes baseline.

**Bisection → offending commit `e42d3df`** — Phase 2 C's `feat(core): wire
NR 0x8E/0x8F to Mmu` (22 lines in `emulator.cpp` routing the two NextREG
handlers to `Mmu::write_nr_8e` / `Mmu::write_nr_8f`). Before this commit
those NextREG writes were stubs; after it they call `apply_legacy_paging_()`
every time, which is where the regression surfaces.

#### Fix 1 — Option C (merge `60ee5fe`)

Pre-existing latent bug: `apply_legacy_paging_()` and `map_plus3_bank()`
wrote the derived physical ROM page into `nr_mmu_[0]`/`nr_mmu_[1]` instead
of VHDL's `0xFF` sentinel ("legacy ROM paging slot"). A comment at
`src/memory/mmu.cpp:253-258` admitted the deviation — "for test/debugger
observability". While no caller wrote to NR 0x8E/0x8F, the deviation was
invisible. After `e42d3df`, firmware writes trigger `apply_legacy_paging_`
and the deviation compounds downstream.

**My initial framing was wrong.** I proposed a guard-on-sentinel fix (skip
the legacy rebuild when `nr_mmu_[i] != 0xFF`, to preserve explicit NR
0x50/0x51 overrides across NR 0x8F writes). An author agent STOPPED mid-
implementation and flagged the error: `zxnext.vhd:4607-4700` is a **clocked
sequential process** (rising-edge `if/elsif` chain), NOT a combinatorial
priority mux. When `port_memory_change_dly` fires — which happens on every
port 7FFD/1FFD/DFFD/EFF7 write AND on every NR 0x8E/NR 0x8F write per
`zxnext.vhd:3813` — the process unconditionally executes `MMU0 <= X"FF";
MMU1 <= X"FF";` (`zxnext.vhd:4642-4645` in the non-special / non-EFF7.3
branch). Prior NR 0x50/0x51 explicit overrides are **stomped on the very
next port/mode write** by VHDL itself, last-writer-wins. I verified against
the VHDL directly before accepting the author's framing.

**Actual Option C fix (commits `5001376` + `d4507fc`):**
- `apply_legacy_paging_` else-branch and `map_plus3_bank` normal-paging
  else-branch now write `nr_mmu_[0] = 0xFF; nr_mmu_[1] = 0xFF;` — matches
  VHDL:4642-4645.
- New accessor `Mmu::get_effective_page(slot)` in `src/memory/mmu.h`:
  returns `nr_mmu_[slot]` when explicit (`!= 0xFF`), else `slots_[slot]`
  (the resolved mapping cache). Bridges the VHDL register-visible value
  to the physical-page observability that tests/debugger want.
- Test-code sweep: `port_test` rows `REG-09` and `NR82-03` that asserted
  physical ROM page after a 0x1FFD write via `get_page(0)` now use
  `get_effective_page(0)` — identical semantics, VHDL-correct accessor.
  Both flipped FAIL → PASS. `mmu_test` and `nextreg_integration_test`
  needed no updates (no row was asserting the clobbered value directly).

**Post-Option-C boot test: still black.** VHDL-faithful for MMU0/1 but
doesn't recover blue stripes. A second, independent regression mechanism
was suspected.

#### Fix 2 — NR 0x8E bit-3 ram-slots gate (merge `0cdf1bf`)

Diagnostic: temporarily stub `apply_legacy_paging_()` inside `write_nr_8e`
and `write_nr_8f` to no-ops. **Boot recovers to blue stripes.** So the
regression is entirely in the rebuild-on-mode-change path. Next sub-step:
re-enable `write_nr_8f` only → blue stripes. So **`write_nr_8e` alone is
the culprit.** Firmware trace during ~8 s boot window: 6 NR 0x8E writes
(values 0x02, 0x08, 0x03, 0x00, 0x01, 0x02) and 1 NR 0x8F write (0x00).

VHDL divergence located at `zxnext.vhd:3814`:
```
port_memory_ram_change_dly <= not (nr_8e_we and not nr_wr_dat(3));
```
And the MMU6/7 update at `zxnext.vhd:4677`:
```
elsif port_1ffd_special_old = '1' or port_memory_ram_change_dly = '1' then
    MMU6 <= port_7ffd_bank & '0';
    MMU7 <= port_7ffd_bank & '1';
end if;
```
So `port_memory_ram_change_dly` is `'0'` exactly when
`(nr_8e_we='1' AND nr_wr_dat(3)='0')`. On those NR 0x8E writes MMU6/7
rebuild is **suppressed**. Every other trigger (port 7FFD/1FFD/DFFD/EFF7,
NR 0x8F, NR 0x8E with bit 3 set) yields `ram_change_dly='1'` and rebuilds
normally. **MMU0/1 rebuild is separate and unconditional** — still driven
by `port_memory_change_dly` per VHDL:4619-4646.

Our `apply_legacy_paging_()` unconditionally rebuilt **both** halves on
every trigger. 5 of the 6 firmware NR 0x8E writes have bit 3 = 0; all 5
were stomping MMU6/7 away from whatever firmware had set them to.

**Fix (commits `c59ec7f` + `0920224` + `e67ba75`):**
- **Refactor.** Split `apply_legacy_paging_()` into
  `apply_legacy_rom_slots_()` (slots 0/1, VHDL:4619-4646) and
  `apply_legacy_ram_slots_()` (slots 6/7, VHDL:4677-4680). Convenience
  wrapper preserves ram-then-rom order for the default both-halves
  callers (port writes, NR 0x8F, soft reset, `map_plus3_bank`).
- **Gate.** `write_nr_8e` now does:
  ```cpp
  if (v & 0x08) apply_legacy_ram_slots_();
  apply_legacy_rom_slots_();
  ```
  Bit 3 = `nr_wr_dat(3)` per the VHDL gate.
- **New integration tests** in `test/nextreg/nextreg_integration_test.cpp`:
  - `N8E-RAM-PRESERVE-0` — NR 0x56 = 0x20 override survives NR 0x8E = 0x00
    (bit 3 = 0) write. Asserts slot 6 still 0x20 after the mode write.
  - `N8E-RAM-REBUILD-1` — bank 3 via `map_128k_bank(0x03)`, NR 0x56 = 0x20
    override, then NR 0x8E = 0x08 (bit 3 = 1, bits 6:4 = 0 → forces 7FFD
    bank = 0). Asserts slot 6 == 0x00 after (rebuild happened).
- **Plan doc rows** in `doc/testing/NEXTREG-TEST-PLAN-DESIGN.md` for both,
  citing VHDL:3814 and :4677.

**Rebase wrinkle.** The author agent's worktree forked from stale
`6a38dd1` (pre-Option-C base). Round-1 critic caught that the refactored
`apply_legacy_rom_slots_()` and `map_plus3_bank` bodies had silently
reverted Option C's `0xFF` sentinel back to the old `rom_bank*2` physical
page. Rebasing onto `main` resolved cleanly (no conflicts — git's
three-way merge adopted main's sentinel into the refactored code). Round-2
critic confirmed the sentinel was preserved across both sites post-rebase.
Lesson (logged): always pass author agents the exact base SHA in the brief
so no ambiguity about `main` state.

#### Test state post-both-merges
- **Unit aggregate:** 2761 / 0 / 499 (+2 from the new NR 0x8E tests; was
  2759 at session start).
- **Regression:** 34 / 0 / 0.
- Per binary: `mmu_test` 155/142/0/13 (unchanged), `nextreg_integration`
  68/55/0/13 (+2 pass), `port_test` 83/82/0/1 (+1 from Option C),
  `fuse_z80` 1356/1356.

#### Boot state post-both-merges: **still black**

Both fixes are VHDL-faithful and test-clean but the boot outcome is
identical to the pre-fix regression. Further diagnostic (temporarily
commenting out individual calls in `write_nr_8e`):

| Stub configuration | Boot outcome |
|---|---|
| All `apply_legacy_*` calls stubbed | **Blue stripes** (2026-04-19 baseline) |
| Only `apply_legacy_rom_slots_()` stubbed (ram gate left in place) | **Black** |
| Landed fix (ram gated on bit 3, rom unconditional) | **Black** |

The ram-gate suppresses 5 of the 6 firmware NR 0x8E writes, but the single
`v=0x08` (bit 3 = 1) write is enough to break boot on its own. That write
forces `port_7ffd_(2:0) ← 0` (VHDL-faithful per `zxnext.vhd:3662-3670`) and
then rebuilds MMU6/7 to bank 0 (pages 0/1). Real hardware would do exactly
the same on that write, so **firmware is designed to handle this** — meaning
either:
1. Our `write_nr_8e` port-update decoding diverges from VHDL somewhere
   before the rebuild (the 7FFD / 1FFD / DFFD update logic at
   `src/memory/mmu.cpp` lines ~371-398).
2. There is another VHDL-faithfulness gap elsewhere — most likely in
   `apply_legacy_rom_slots_` which derives `rom_bank` solely from
   `port_7ffd_` bit 4 + `port_1ffd_` bit 2 and **ignores NR 0x8C altrom**.
   If firmware writes NR 0x8C before the v=0x08 NR 0x8E, our ROM slot
   derivation is missing that input.
3. Something firmware touches between the soft-reset and the v=0x08 write
   leaves emulator state in a place where VHDL's MMU6/7 ← bank-0 rebuild
   is harmless but ours isn't.

**Next session plan (user-agreed):** task (c) — dig into the v=0x08
regression with instrumentation. Concrete starting points:
1. Bit-by-bit verify every line of `write_nr_8e` port-update decoding
   against `zxnext.vhd:3662-3670` (port_7ffd_reg), `:3696-3704`
   (port_dffd_reg), `:3726-3734` (port_1ffd_reg). The critic confirmed the
   *gate* structure but did not independently check every bit-assignment.
2. Audit `apply_legacy_rom_slots_` for NR 0x8C altrom / NR 0x8E altrom
   interactions. VHDL `mmu_A21_A13` for slots 0/1 consults altrom
   state; our derivation doesn't.
3. Instrument: log `slots_[0..7]`, `nr_mmu_[0..7]`, `port_7ffd_`,
   `port_1ffd_`, `port_dffd_reg_`, `port_eff7_reg_3_`, `nr_8c_reg_`, SP,
   PC immediately before and after the v=0x08 write. Compare to what
   VHDL would compute for each signal.
4. Cross-reference with CSpect behaviour if accessible — CSpect tolerates
   some things our emulator doesn't (see 2026-04-18 Stage C lesson), and
   real-hardware traces (if available) are ground truth.

**Explicitly out of scope for task (c):** re-litigating Option C or the
NR 0x8E bit-3 gate. Both are VHDL-faithful, tested, and independently
reviewed.

#### Architectural insight logged
VHDL MMU register-file updates (`zxnext.vhd:4607-4700`) are **clocked
sequential** — the `elsif` ladder pattern means last-write-in-time wins,
not a priority mux. When reading VHDL register-file processes, don't
assume priority semantics from the syntactic ordering of branches. This
framing error cost the first Option C iteration.

#### Queued follow-up (not blocking boot work)
Debugger UI UX regression from Option C (critic noted, not blocking):
`src/debugger/mmu_panel.cpp:112` (`MmuPanel::refresh`) and
`src/debugger/memory_panel.cpp:197` (`MemoryPanel::update_page_selector`)
display `mmu.get_page(slot)` which now shows `0xFF` sentinel for slots
0/1 in legacy ROM mode instead of the derived physical page. Swap both
to `get_effective_page`. Also `src/debugger/memory_panel.cpp:133` has a
dead `uint8_t page` assignment — delete the line rather than swap (the
local `page` variable is never read after the assignment).

---

## Commit index (all dates)

| Hash | Date | Description |
|------|------|-------------|
| 5bef014 | 2026-03-29 | SPI model rewrite: split read/write, CS deselect, CMD1 |
| 971aaee | 2026-03-29 | NCR busy byte before R1 |
| 7723008 | 2026-03-29 | CMD12 stuff bytes (8×0xFF) |
| 93dc7bc | 2026-03-30 | Machine ID 0x08, FAT32 image, CMD18, soft reset |
| 6b947ec | 2026-03-31 | Writable boot ROM SRAM, config_mode sync, automap control |
| 9e4243b | 2026-04-01 | Delayed screenshot + automatic exit CLI options |
| e73df39 | 2026-04-18 | Build: auto-regenerate version.h when version.yaml changes |
| 0077d3e | 2026-04-18 | CLI: rename --machine-type to --machine |
| 396a63a | 2026-04-18 | nextreg: NR 0x00 machine ID = 0x08 (HWID_EMULATORS) |
| f3c42ff…7d0b0fa | 2026-04-18 | Task 11 Branch 1 — NR 0x04 + config_mode SRAM routing |
| 75d87fb…00fe3ab | 2026-04-18 | Task 11 Branch 2 — ROM-in-SRAM serving |
| eb78d43…88a1c15 | 2026-04-18 | Task 11 Branch 3 — soft_reset preserves SRAM + boot_rom_en |
| 5896384 | 2026-04-18 | fix(mmu): Task 12 — +0x20 shift for 128K bank in Next mode |
| 4dbceb7 | 2026-04-18 | doc: Task 12 root-cause fix notes + Task 13 bypass plan |
| 9737640 | 2026-04-19 | fix(mmu): Task 12c — centralise +0x20 shift in `to_sram_page` helper |
| 6920a63 | 2026-04-19 | fix(mmu): Task 12c — apply `to_sram_page` across full page range |
| 7abbe51 | 2026-04-19 | Merge branch 'task12c-centralise-shift-helper' |
| f15a58c | 2026-04-19 | doc(prompt): add Task 12c status block |
| e42d3df | 2026-04-19 | feat(core): wire NR 0x8E/0x8F to Mmu — introduces black-screen boot regression |
| 5001376 | 2026-04-20 | fix(mmu): apply_legacy_paging_ writes 0xFF sentinel (VHDL-faithful) — Option C |
| d4507fc | 2026-04-20 | test(port): switch ROM-page-after-port-write asserts to get_effective_page |
| 60ee5fe | 2026-04-20 | Merge branch 'fix/mmu-legacy-paging-sentinel' — Option C landed |
| c59ec7f | 2026-04-20 | refactor(mmu): split apply_legacy_paging_ into rom / ram halves |
| 0920224 | 2026-04-20 | fix(mmu): NR 0x8E bit-3=0 suppresses MMU6/7 rebuild (VHDL:3814) |
| e67ba75 | 2026-04-20 | test(nextreg): N8E-RAM-PRESERVE-0 / N8E-RAM-REBUILD-1 for NR 0x8E bit-3 gate |
| 0cdf1bf | 2026-04-20 | Merge branch 'fix/mmu-nr8e-ram-gate' — NR 0x8E ram-slots gate landed |
