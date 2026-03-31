# Way Forward — JNext ZX Spectrum Next Emulator

**Date:** 2026-03-31
**Author:** Claude (analysis requested by project owner)

---

## 1. Understanding of Goals and Current State

### Project Goals

JNext aims to be a **faithful, cross-platform software emulator** of the ZX Spectrum
Next, written in C++17, using the official FPGA VHDL sources as the authoritative
hardware specification. The design philosophy has been: since we have the VHDL, we can
emulate everything down to a very low level, and since the Next can behave like all
Spectrum models, we can use the NextZXOS firmware to emulate all ZX models from a single
hardware emulation target.

### What Has Been Accomplished

In approximately **two weeks** of development (2026-03-18 to 2026-03-31), the project
has completed **Phases 1 through 7.5** — an extraordinary amount of work:

| Phase | Scope                                                            | Status   |
|-------|------------------------------------------------------------------|----------|
| 1     | Skeleton + CPU (boots to 48K BASIC)                              | COMPLETE |
| 2     | Full ULA Video (pixel-perfect rendering)                         | COMPLETE |
| 2.5   | Logging & Debugging infrastructure                               | COMPLETE |
| 3     | Extended Video (Layer 2, Sprites, Tilemap, Copper, Compositor)   | COMPLETE |
| 3.5   | Program Loading (NEX files + raw binary injection)               | COMPLETE |
| 4     | Audio (3×AY-3-8910, DAC, Beeper, Mixer)                          | COMPLETE |
| 5     | Peripherals (DivMMC, CTC, SPI, I2C, UART, DMA, full NextREG)     | COMPLETE |
| 6     | Qt6 Native UI (pixel-perfect, fullscreen, CRT filter)            | COMPLETE |
| 7     | Full Debugger (8 panels, breakpoints, watches, trace, MAP files) | COMPLETE |
| 7.5   | Debugger Enhancements                                            | COMPLETE |

The codebase consists of ~18,000 lines of C++17 across ~119 source files, organized into
11 subsystems. All major hardware subsystems have been verified against the VHDL sources.
12 working demo programs exercise video, audio, and peripherals end-to-end.

### The Current Blocker

Phase 8's milestone is **NextZXOS booting from an SD card image**. The boot ROM works
perfectly (loads TBBLUE.FW into RAM), but the post-boot firmware fails during BPB
(BIOS Parameter Block) processing and retries 3 times before giving up. A full day was
spent rewriting the SPI model and debugging the protocol without resolving this.

---

## 2. Analysis of Concerns

### Concern 1: "Is the low-level VHDL-faithful approach the right one?"

**Short answer: Yes, for the core emulation. The question is really about where the
boundary of "core emulation" should be.**

The VHDL-faithful approach has been enormously successful for the CPU, video pipeline,
audio, and memory subsystem. These are the heart of the emulator and they work correctly
precisely *because* they follow the VHDL closely. The approach breaks down at the
**system boundary** — the SD card — because:

1. **An SD card is not part of the Next hardware.** The VHDL describes the SPI *master
   controller*, not the SD card itself. The card is an external device with its own
   complex protocol (SD Physical Layer Specification is 300+ pages). The VHDL tells us
   nothing about how the card should *respond*.

2. **UPDATE: The firmware source IS available (GPLv3).** The
   [tbblue GitLab repo](https://gitlab.com/thesmog358/tbblue/-/tree/master/src/firmware)
   contains the **complete firmware source code** under GPLv3, authored by Garry Lancaster,
   Fabio Belavenuto & Victor Trucco. This changes the debugging picture entirely — see
   Section 3a below for detailed analysis.

3. **The problem space is underspecified.** Unlike ULA timing (where the VHDL gives
   exact cycle counts), the SD card protocol has many optional behaviors, vendor-specific
   quirks, and timing dependencies that real hardware handles implicitly. However,
   having the source code (point 2) greatly narrows the unknown space.

The lesson is not that the approach is wrong, but that **the SD card peripheral
should be treated differently from the core hardware**. It's an external device, not an
FPGA module, and emulating it requires matching firmware expectations, not VHDL behavior.

### Concern 2: "Should we emulate peripherals at a higher level?"

**Analysis of how other emulators handle this:**

| Emulator    | SD Card Approach                                                            | Level  | Boots NextZXOS? |
|-------------|-----------------------------------------------------------------------------|--------|-----------------|
| **ZesarUX** | Low-level SPI byte protocol, but pragmatic (shortcuts, hardcoded responses) | Medium | **Yes** |
| **CSpect**  | Higher-level ROM firmware interception, serves data at command level        | High   | **Yes** (official) |
| **FUSE**    | No SD/MMC at all; +3 disk via DSK format at controller level                | High   | N/A |

Both CSpect and ZesarUX successfully boot NextZXOS from SD card images, proving it is
achievable. CSpect (the official Next emulator by Mike Dailly) requires `enNextZX.ROM`
and `enNxtMMC.ROM` alongside the SD image, and uses `-mmc=sdcard.img`. ZesarUX uses
the same images with its `-mmc` flag. The [System/Next distribution](https://www.specnext.com/latestdistro/)
provides a specific 1GB image optimized for emulators.

ZesarUX proves that low-level SPI emulation *can* work, but their implementation has
pragmatic shortcuts (e.g., `MACHINE_IS_TBBLUE` conditionals, hardcoded OCR values,
default return value of 0 for unknown states). CSpect takes a higher-level approach
entirely.

**The key insight:** There is no shame in pragmatic shortcuts for external devices. The
emulator's value lies in accurate *hardware* emulation (CPU timing, video pipeline, audio
mixing), not in faithful simulation of a commodity SD card.

### Concern 3: "Can we reuse open-source libraries?"

There are no ready-to-use C++ libraries for SD card SPI emulation in a retro-computing
context. Libraries like ulibSD and ZipCPU's SDSPI target embedded/FPGA contexts and
would require significant adaptation. The most practical "library" is ZesarUX's own
mmc.c, which we've already used as a reference.

However, with the firmware source now available, the most valuable reference is the
firmware's own `diskio.c` — we can match our SD card responses exactly to what it
expects.

### Concern 4: "Is emulating all ZX models via NextZXOS firmware realistic?"

**This goal has a fundamental architectural problem.** The Next *can* run in various
compatibility modes, but these modes are configured by NextZXOS firmware at boot time.
Without NextZXOS booting, you can't access these modes at all. More importantly:

- The **48K mode already works** via the standard 48K ROM (no NextZXOS needed)
- **128K/+3 modes** would need the corresponding ROMs loaded into the correct MMU pages
  and appropriate NextREG timing configuration — this can be done without NextZXOS
- **Pentagon timing** is just a contention LUT change

In other words: the hardware emulation already supports all models via NextREG
configuration. NextZXOS is one *way* to configure the machine, but not the *only* way.
The emulator could provide a menu to select machine type and load the appropriate ROMs
directly, without involving NextZXOS at all.

### Concern 5: "Have I been too ambitious?"

**No.** The project's progress proves the approach is sound. Phases 1-7.5 in two weeks
is remarkable. The SD card blocker is frustrating but it's a **local problem**, not a
systemic failure of the design. The risk is letting one stubborn peripheral issue
overshadow the enormous amount that already works.

---

## 3. Remaining Gaps in the SD Card Implementation

Detailed comparison with ZesarUX reveals these specific differences that likely explain
the boot failure:

| Gap                      | Detail                                                                      | Likely Impact                                                               |
|--------------------------|-----------------------------------------------------------------------------|-----------------------------------------------------------------------------|
| **Default return value** | ZesarUX returns 0 for unknown commands; JNext returns 0xFF in IDLE          | Firmware may interpret extra bytes after sector read differently            |
| **OCR format (CMD58)**   | ZesarUX uses `{5, 0, 0, 0, 0}`; JNext sets CCS bit differently (0xC0)       | Affects card type detection (SDHC vs SD); firmware may take wrong init path |
| **TBBLUE conditional**   | ZesarUX has `MACHINE_IS_TBBLUE` conditionals in mmc_read()                  | Different behavior for Next vs other machines; we may be hitting wrong path |
| **NextREG 0x05**         | Board revision defaults to 0x00 in JNext; real hardware has non-zero nibble | Firmware may skip critical init paths based on board revision               |
| **FS Info sector**       | Firmware may attempt to read sector 64 (FS Info) which hasn't been observed | Silent data mismatch could cause validation failure                         |

**Risk assessment:** With the firmware source now available, this drops to **medium
complexity** — the unknowns are now knowns.

---

## 3a. Firmware Source Analysis (NEW — source found at gitlab.com/thesmog358/tbblue)

The complete firmware source (GPLv3) is at:
`https://gitlab.com/thesmog358/tbblue/-/tree/master/src/firmware`

### Architecture: Two Separate SD Card Drivers

The firmware has **two completely independent SD card implementations**:

| Component | Boot Loader (`loader/`) | Post-Boot Firmware (`app/`) |
|-----------|------------------------|---------------------------|
| **SD init** | `mmc.s` (hand-written Z80 asm) | `diskio.c` (FatFs diskio layer, C) |
| **FAT driver** | `fat.c` (minimal hand-rolled) | `ff.c` (Chan's FatFs, full library) |
| **Entry point** | `main.c` → `MMC_Init()` → `FindDrive()` → loads TBBLUE.FW | `boot.c` → `f_mount()` → `f_open()` via FatFs |
| **Runs at** | 0x0000 (boot ROM) | 0x6000 (loaded into RAM) |

**This is why our boot ROM works but post-boot fails.** The two drivers have different
SPI protocol expectations. We matched the boot loader's `mmc.s` protocol, but the
post-boot `diskio.c` has its own protocol.

### Post-Boot `diskio.c` — Exact SPI Protocol

The `disk_initialize()` function (`app/src/ff/diskio.c:275`):

```
1. send_cmd(CMD12, 0)          — cancel any multi-block read
2. send_cmd(CMD0, 0) == 1      — expect R1=0x01 (idle)
3. send_cmd(CMD8, 0x1AA) == 1  — expect R1=0x01
4. rcvr_mmc(buf, 4)            — read 4 bytes of R7 (expect buf[2]=0x01, buf[3]=0xAA)
5. Loop: send_cmd(ACMD41, 1<<30) until response == 0
6. send_cmd(CMD58, 0) == 0     — expect R1=0x00
7. rcvr_mmc(buf, 4)            — read 4 bytes OCR (check buf[0] bit 6 = CCS)
```

**Critical protocol details in `send_cmd()`** (`diskio.c:210`):

```c
select();                        // SD_CONTROL = 0xFE, then read 1 dummy byte
xmit_mmc(buf, 6);               // send 6-byte command
if (cmd == CMD12) rcvr_mmc(buf, 8);  // skip 8 stuff bytes for CMD12
d = wait_response();             // read up to 250 bytes until non-0xFF
return d;
```

**Key difference: `select()` is called EVERY command**, which does CS low + 1 dummy
read. And after `disk_read()` completes, `deselect()` is called (CS high + 1 dummy
read). This select/deselect-per-command pattern may differ from what our emulator does.

### The Error Path

In `boot.c:main()` → `load_config()` (`config.c:294`):
```
1. f_mount(&FatFs, "", 0)               — lazy mount, no disk access
2. f_open(&Fil, CONFIG_FILE, FA_READ)   — triggers disk_initialize() + FAT mount
3. (config.ini read, or silently skipped if missing)
4. f_open(&Fil, MENU_FILE, FA_READ)     — tries "machines/next/menu.ini"
5. f_open(&Fil, MENU_DEFAULT_FILE, FA_READ) — tries "machines/next/menu.def"
6. BOTH fail → display_error("Error opening 'menu.ini/.def'!")
```

The failure means either `disk_initialize()` failed, FatFs couldn't parse the FAT32
BPB, or directory traversal couldn't find the files. Since we confirmed BPB data is
byte-perfect, the most likely cause is a **protocol-level mismatch in
`disk_initialize()`** or in `send_cmd()`/`wait_response()` behavior.

### Specific Items to Check Against Our Implementation

1. **`select()` pattern**: CS low + 1 dummy byte read before EVERY command. Does our
   emulator handle this dummy read correctly?

2. **CMD8 R7 response**: After R1, firmware reads exactly 4 bytes via `rcvr_mmc()`.
   These must be: `{0x00, 0x00, 0x01, 0xAA}`. If our emulator returns them differently
   (e.g., shifted or with extra 0xFF padding), CMD8 will fail.

3. **CMD58 OCR response**: After R1=0x00, firmware reads exactly 4 bytes. For SDHC:
   `buf[0]` bit 6 must be set (CCS=1). Our OCR must have `0x40` in the first byte.

4. **`wait_response()` returning 0**: Reads up to 250 bytes looking for non-0xFF. If
   our emulator returns 0xFF for too many bytes (or never returns non-0xFF), the
   function returns 0xFF and the command "fails". A return of 0 from `wait_response()`
   means the card never responded, but a return of any value != 0xFF is accepted.

5. **`deselect()`/`select()` dummy bytes**: These dummy reads on CS transitions are
   standard SD SPI protocol — they clock the card to release the data line. Our
   emulator must return 0xFF for these dummy reads.

6. **`rcvr_datablock()` token wait**: Waits up to 5000 iterations for non-0xFF, then
   checks for 0xFE. If we return the data token too early (before the firmware reads
   it), the firmware will miss it.

7. **`HWID_EMULATORS` check in `boot.c:183`**: The firmware checks
   `if (mach_id == HWID_EMULATORS) return;` in `check_coreversion()`. Our NextREG 0x00
   (machine ID) must return `HWID_EMULATORS` (`0x08`) to skip the core version check,
   which would otherwise try to read flash memory (which we don't emulate).
   **CONFIRMED BUG: our emulator returns `0x0A` (HWID_ZXNEXT) instead of `0x08`
   (HWID_EMULATORS).** This means the firmware enters `check_coreversion()` and tries
   to call `readFlash()`, which will fail/hang since we don't emulate FPGA flash.
   This alone could explain the boot failure.

### Impact on Debugging Effort

With the source available, the debugging approach changes from "trial and error against
a black box" to "systematic comparison against known code." Each of the 7 items above
can be verified in a single debugging session. **Estimated effort drops to 1 day.**

---

## 4. Conclusions

1. **The project is a success.** The core emulation is solid, verified against VHDL,
   and functionally complete for running ZX Next software.

2. **The SD card is a peripheral problem, not a design problem.** It should not cause
   us to question the overall architecture.

3. **The firmware source is available (GPLv3).** This transforms the debugging task
   from reverse-engineering a black box to systematic code comparison. The source is
   at `https://gitlab.com/thesmog358/tbblue/-/tree/master/src/firmware`.

4. **A likely root cause has been identified.** Our emulator reports Machine ID `0x0A`
   (real ZX Next hardware) instead of `0x08` (HWID_EMULATORS). This causes the
   firmware to attempt a flash memory read that we don't emulate, likely causing a
   hang before it ever reaches the "Error opening menu.ini/.def" path.

5. **NextZXOS boot is now within close reach.** With the source available and a likely
   root cause identified (Machine ID), this is no longer a "maybe days of debugging"
   problem — it's a "fix known issues and verify" problem.

6. **The "all models via firmware" goal is achievable without NextZXOS** by implementing
   machine-type selection directly in the emulator (ROM loading + NextREG configuration).
   But with NextZXOS boot close to working, this becomes a natural bonus.

7. **The SD card problem is solvable** — the firmware source provides the exact code
   paths, and we have identified a confirmed bug (Machine ID) plus 6 more items to
   verify systematically.

---

## 5. Way Forward and Recommendations

### Recommended Strategy: Fix NextZXOS Boot First, Then Ship v1.0

The discovery of the firmware source code fundamentally changes the recommendation.
Previously, fixing NextZXOS was estimated at 2-3 days of trial-and-error debugging
against a black box. Now, with the source available and a confirmed bug already found,
**fixing NextZXOS boot is likely a 1-day task** — and it would make v1.0 dramatically
more impressive.

#### Phase A: Fix NextZXOS Boot (~1 day)

1. **Fix Machine ID (confirmed bug):** Change NextREG 0x00 from `0x0A`
   (HWID_ZXNEXT) to `0x08` (HWID_EMULATORS). This skips `check_coreversion()` and
   the flash read that we don't emulate. This alone may fix the boot.

2. **Verify the 6 remaining items from Section 3a against `diskio.c`:**
   - `select()`/`deselect()` dummy byte handling
   - CMD8 R7 response format (4 bytes: `{0x00, 0x00, 0x01, 0xAA}`)
   - CMD58 OCR format (4 bytes, bit 6 of byte 0 = CCS for SDHC)
   - `wait_response()` behavior (250 retries for non-0xFF)
   - `rcvr_datablock()` token wait (5000 retries, expects 0xFE)
   - CMD12 stuff bytes (8 bytes skipped in `send_cmd()`)

3. **Test with SD image.** If boot works, verify config.ini loading, menu.def
   parsing, ROM loading, and personality selection.

4. **If still failing after all 7 items**, use the debugger to set a breakpoint at
   the `display_error` call sites in `config.c` and trace back through FatFs. With
   the source, we know exactly which functions to instrument.

#### Phase B: Complete Phase 8 (~3-5 days)

5. **Run FUSE Z80 opcode test suite** — validates CPU accuracy
6. **Performance profiling** and optimization
7. **Layer 2 additional modes** (320×256, 640×256) if time permits
8. **Add machine-type selection** in the emulator menu:
   - 48K / 128K / +3 / Pentagon modes
   - Load correct ROM files and configure NextREGs automatically
   - This works both with and without NextZXOS

#### Phase C: Ship v1.0 (~1 week)

9. **Complete Phase 9** (CI, testing, release packaging):
   - Linux release first
   - FUSE opcode validation as CI gate
   - Basic documentation (README, usage)

10. **Ship v1.0** with:
    - NextZXOS boot from SD image (full Next experience)
    - 48K BASIC (built-in, no SD needed)
    - 128K/+3 modes (via ROM selection or NextZXOS personality)
    - NEX file loading
    - Full debugger with 8 panels
    - All video/audio/peripherals working

#### Future (v1.1+)

11. **Snapshot support** (.sna, .z80, .szx formats)
12. **Tape loading** (.tap, .tzx)
13. **Network play** / ESP emulation via UART bridge
14. **Full cycle-accurate mode** (28 MHz reference, build flag)

### Alternative Strategy: Ship Without NextZXOS

If the NextZXOS fix takes longer than expected (unlikely now, but possible), the
original recommendation still holds: ship v1.0 with NEX loading as the primary
program interface and defer NextZXOS to v1.1. The emulator is already fully functional
for running Next software without it.

---

## 6. Additional Issues Identified

### Issue 1: Test Coverage

The emulator has 12 demo programs but no automated test suite. Phase 9 plans this, but
the current "it works when I run the demo" approach creates risk for regressions.
**Recommendation:** Prioritize FUSE opcode tests and at least one visual regression test
(48K BASIC prompt screenshot comparison) before v1.0.

### Issue 2: Multi-Platform Builds

The emulator currently builds on Linux only. Windows and macOS builds are in Phase 9 but
are non-trivial (static Qt6 + SDL2 linking). **Recommendation:** Ship Linux v1.0 first;
add Windows/macOS in v1.0.1 or v1.1.

### Issue 3: ROM Distribution

The emulator requires ROM files that cannot be redistributed. The current approach
(user provides ROMs, emulator loads from `roms/` directory) is correct, but the UX needs
work. **Recommendation:** Clear error messages when ROMs are missing, with instructions
on where to obtain them legally.

### Issue 4: The DAC Buzz

The persistent DAC audio buzz (deferred in Phase 6) affects both JNext and ZesarUX. This
suggests it's either a fundamental limitation of software DAC emulation or a demo code
issue, not an emulator bug. **Recommendation:** Keep deferred; document as known
limitation.

### Issue 5: Architecture Documentation

The codebase is clean and well-organized, but the EMULATOR-DESIGN-PLAN.md is now
partially outdated (references `zxnext_*` targets, some Phase 8/9 tasks need
updating). **Recommendation:** Update before v1.0 release as part of Phase 9 docs.

---

*This document represents an honest assessment of the project state and a pragmatic
path forward. The discovery of the firmware source code during this analysis
transformed the outlook from "debugging a black box" to "fixing known issues against
readable source." The Machine ID bug (`0x0A` vs `0x08`) is a strong candidate for the
root cause, and the 6 additional verification items are all tractable with the source
in hand. The emulator is far more complete and capable than its current blocker
suggests — and that blocker now has a clear path to resolution.*
