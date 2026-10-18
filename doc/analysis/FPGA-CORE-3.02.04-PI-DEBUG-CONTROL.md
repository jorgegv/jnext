# FPGA core 3.02.04 and the Pi debug control

Analysis of 2026-10-18. It covers what the official FPGA repository holds beyond the core jnext
follows, and what the "Pi debug control" (the Raspberry Pi reading and writing the Next's memory)
would take in jnext. Nothing here is decided. External links are in
[REFERENCES.md](../REFERENCES.md).

Tracked in GitHub issues #393 (the Pi debug control, blocked on QEMU) and #394 (choosing the
emulated core version and board; no use for it yet).

## 1. Where the new work is

The local clone is `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA`. It was fetched on
2026-10-18; the fetch before that was on 2026-08-15.

| Ref | Commit | Date | Core |
|---|---|---|---|
| `master` (= `origin/master`) | b047011 | 2025-11-06 | 3.02.03. jnext follows this one, and its VHDL citations refer to it |
| `origin/30204` | dc8d764 | 2026-05-28 | 3.02.04 (bitstreams built under `cores/zxnext/tbumaker/`) |
| `origin/30205` | 15e206d | 2026-09-25 | 3.02.05, in progress; contains all of 30204 |

Neither branch is merged into `master`. Beyond 30204, 30205 adds only a version bump, smaller
anti-brick ("AB") builds (DMA, Multiface and DivMMC left out), a whitespace commit and a merge of
`master`. The last two are the only commits since the 2026-08-15 fetch.

### Boards

| Kickstarter | Board | FPGA | Evidence |
|---|---|---|---|
| KS1 | issue 2 | Spartan 6 | `src/cpu/readme.txt` ("too large for synthesis on KS1 machines") |
| KS2 | issue 4 | Artix 7 | commit 293f594 "KS2 Final Issue 4 3.02.00, Issue 2 3.01.10" |
| (none) | issue 3 | Spartan 6 | commit a289cdc "Remove issue 3 build as the S6 KS2 board has been abandoned" |
| KS3 | issue 5 | Artix 7 | the "KS3 ..." commits that came with the issue 5 builds (3.02.03 on) |

- **3.02.04 is built for every board.** `tbumaker/` has issue 2 bitstreams (`zxnext-2-30204-hdmi.bin`, `-vga.bin`, `-ab.bin`) next to the issue 4 and issue 5 ones. Two commits maintain the issue 2 build: "Allow separate vga and hdmi builds in tbu for issue 2" and "Corrections for issue 2 build". **3.02.05 has no bitstreams yet.**
- **The new modules are build-time options, and issue 2 gets none of them.** `zxnext.vhd`'s generic `g_option` selects the CPU variant, each Pi debug command group, the tzx controls and SRAM port C.
  - `zxnext_top_issue2.vhd` sets `g_option := X"0000"`, and the ISE project does not override it.
  - `zxnext_top_issue4.vhd` and `zxnext_top_issue5.vhd` set `X"FFF2"`: the Z80N with the debug interface, every Pi debug command, the tzx controls and SRAM port C.
- **On an issue 2 Next, 3.02.04 therefore has:**
  - no Pi debug control (NextREG 0xA0 bit 1 reads back as 0);
  - no NMI+QWERT tape hotkeys;
  - the plain Z80N. `src/cpu/readme.txt` says the complete memptr/XY core is too large for KS1, and the debug-wedge core is likely usable only on KS2/KS3.
- **What every board gets:** the always-built changes, namely the PS/2 keys, the MD pad identification, and the new boot ROM with the SD card choice.
- **For jnext:** jnext emulates an issue 2 board (`ZXN_ISSUE2`). Emulating the Pi debug control would mean emulating an issue 4/5 feature, including what NextREG 0x0F reports.

## 2. What 3.02.04 changes

From `cores/zxnext/changelog.md` on `origin/30204`, checked against the VHDL diff `master..origin/30205`:

| Area | Change | For jnext |
|---|---|---|
| Pi | A hardware SPI slave that gives the Pi direct access to the Next's CPU, memory, NextREGs and BRAM while the machine runs (section 3) | the feature of interest; needs a Pi-side SPI path (section 4) |
| TZX | NMI + Q/W/E/R/T become pulsed "tape" hotkeys. In the VHDL they feed only the Pi debug control's STATUS (`zxnext.vhd` on 30205, line 3753: `i_hotkeys => tzx_hotkeys`), so a tape player on the Pi can act on them | only with the Pi debug control |
| PS/2 keyboard | Expansion bus enable/disable moves from F11/F12 to F5/F6, as on the membrane; F12 becomes another module reset, for keyboards without PAUSE/BREAK | small key-mapping change |
| Joysticks | Six-button Mega Drive pads also identify as three-button, which fixes Atari paddles being detected wrongly | small joystick-detection change |
| Z80 | Complete memptr and XY flags as a build-time option (`src/cpu/t80n-complete/`), not active in the shipped core; debug-wedge variants (`t80n-debug/`, `t80n-complete-debug/`) for the larger FPGAs | none: the shipped core is unchanged |
| Boot | User-controlled boot from the external or the internal SD card, settings kept in flash, new `src/rom/bootrom.vhd`. The boot ROM now overlays only 0x0000-0x1FFF (`cpu_a(15 downto 13) = "000"`, commit 13f1f1e), so config memory shows through at 0x2000-0x3FFF. NextREG 0x0A bit 5, the sd0/sd1 swap added in 3.02.03, now changes only when bit 2 is written as 1 (commit 6395c28: older firmware flipped it by accident in config mode) | the boot ROM range and the NextREG 0x0A lock, if adopted |
| Build | Separate HDMI and VGA builds for issue 2, and a VGA test pattern (`src/video/vga/video_test_pattern.vhd`) | none |

Bug fixes in the commits that the changelog does not list:
- SD card initialization no longer locks up some brands when more than one card shares the SPI bus (01676e2).
- Flash SPI traffic no longer shows up on the SD card's SPI lines (0c06f07).
- The SPI master's wait signal to the DMA is corrected (515ec76, `src/serial/spi_master.vhd`: a new `spi_active` latch). This affects DMA transfers through the SPI data port.
- The rest are board-level fixes for issue 5 (PLL start-up, SD pin I/O standard).

For comparison, 3.02.03 (on `master`, the version jnext emulates and reports in NextREG 0x01/0x0E) added issue 5 bitstreams, the NextREG 0x0A bit 5 SD swap and NextREG 0x81 bit 3 (+3 FDC control signals, issue 5 only). jnext implements both registers.

`nextreg.txt` also corrects two notes: NextREG 0x7F user storage resets on hard reset only, and the
"(may change)" on the ULA half-pixel scroll is gone.

## 3. The Pi debug control

**Files** (on `origin/30204`):
- `src/device/pi_debug_control/` holds `pi_debug_control.vhd`, a variant with optional features cut by generics, and the protocol in `readme.txt`.
- `src/serial/spi_slave.vhd` is the SPI slave itself.
- `src/device/arbiter_dma.vhd` arbitrates the bus.
- `src/cpu/readme.txt` lists the CPU register ids.

**Enable:** NextREG 0xA0 bit 1 puts SPI on GPIO 7-11 in **slave** mode (Pi to Next; hard reset 0).
- It overrides bit 0, the existing master mode (Next to Pi).
- If slave mode is not built in, the bit reads back as 0.
- The pins are the Pi's SPI0: CE1 = GPIO 7, CE0 = GPIO 8, MISO = 9, MOSI = 10, SCLK = 11.

**Protocol** (`readme.txt`):
- A valid command byte has upper nibble `0xA`. The Pi sends `00` whenever it has nothing to send, so a lost sync puts the module in an error state.
- The Pi resets the module by driving GPIO 8 (CE0) high. That does not change the CPU's pause state.
- The Pi should send VERSION first.

| Command | Bytes after the command (Pi side) | Notes |
|---|---|---|
| `VERSION` | read junk, read VERSION, read CAPABILITIES | capabilities bits: hotkeys, cpu_reg_rw, bram_rw, pause/resume/step, sram_rw, nextreg_w, nextreg_r, system |
| `STATUS` | read junk, read STATUS, read HOTKEYS | status: had reset, busrq state (00 idle, 01 stopping the CPU, 10 paused, 11 releasing for a step), DMA active and CPU halted (both active low), had error. Hotkeys: Q W E R T |
| `SYSTEM` | write COMMAND | soft reset, hard reset, DivMMC NMI, Multiface NMI, a CTC channel 3 interrupt. In the VHDL the resets and NMIs are ORed into the function-key hotkeys |
| `CPU_PAUSE`, `CPU_RESUME`, `CPU_SSTEP` | none | pause takes the bus (BUSRQ); single step releases it for one instruction, then pauses again; poll STATUS |
| `SRAM_R`, `SRAM_W` | ADDR[20:16], ADDR[15:8], ADDR[7:0], LEN[15:8], LEN[7:0], then data (a read starts with one junk byte) | any of the 2 MB, 21-bit physical address |
| `BRAM_R`, `BRAM_W` | ADDR[15:8], ADDR[7:0], LEN[15:8], LEN[7:0], then data | ADDR[15] selects bank 5 (16K) or bank 7 (8K); the address wraps at the bank size |
| `NEXTREG_R`, `NEXTREG_W` | NEXTREG, then the value | |
| `CPU_REG_R`, `CPU_REG_W` | CPU_REG, then the value | the CPU must be paused. Ids: 00-07 A F B C D E H L, 08-0F the shadow set, 10-13 IX/IY halves, 14-17 SP and PC halves, 18 I, 19 R, 1A/1B interrupt mode, HALT and IFF flags (on 1B only HALT, IFF2 and IFF1 are writable) |

**Getting each other's attention:**
- The Pi gets the Next's attention with a SYSTEM interrupt, flags written into memory or NextREG 0x7F, or forced register changes.
- The Next gets the Pi's attention with the QWERT hotkeys, a HALT (seen in STATUS), or flags in memory or NextREG 0x7F.

## 4. The Pi side in jnext: QEMU `raspi0`

jnext runs NextPi under `qemu-system-arm -M raspi0`, with the image's `boot/kernel.img` and
`boot/bcm2708-rpi-zero.dtb` and the serial link on a chardev (`src/core/pi_qemu.cpp:107-115`).
What that offers for SPI0 (measured on the development host's QEMU 10.2.2 unless stated):

- **The SPI0 master exists.** QEMU has a BCM2835 SPI controller, `bcm2835-spi` (`hw/ssi/bcm2835_spi.c`), added in QEMU 9.0 (2024).
  - `info qtree` on `raspi0` shows it at 0x20204000, with an SSI bus named `spi` and nothing on it.
  - The SoC's SPI *slave* block (`bcm2835-spis`) and `bcm2835-i2s` are unimplemented stubs. That doesn't matter here: the Pi is the master.
- **The model is polled and interrupt-driven only.** DMA is unimplemented: it logs "DMA not supported". LoSSI is unimplemented too. The CS field is stored but drives no chip-select lines.
- **The NextPi device tree disables SPI0.**
  - In `bcm2708-rpi-zero.dtb`, `/soc/spi@7e204000` has `status = "disabled"`, two `spidev` children (`spidev@0`, `spidev@1`) and `tx`/`rx` DMA channels.
  - On a real Pi, `dtparam=spi=on` (the `spi` entry in `__overrides__`) enables it, and the Pi firmware applies that. QEMU loads the DTB directly and runs no firmware, so under jnext SPI0 stays disabled.
  - Enabling it means passing QEMU a DTB copy with `status = "okay"`. Because the model has no DMA, that copy also needs no `dmas`: the mainline Linux driver uses DMA for longer transfers (96 bytes and up in mainline; to verify against the NextPi kernel).
- **The missing piece: nothing on the bus can reach jnext.**
  - QEMU 10.2.2 offers 137 devices for an SSI bus: 136 SPI flash chips and the `ssd0323` display controller. None forwards traffic to another process.
  - A Pi-to-jnext SPI link therefore needs a QEMU SSI peripheral whose `transfer_raw` sends each byte over a chardev and returns jnext's reply. That means an upstream QEMU contribution, or a patched QEMU build shipped for every platform (the Windows launcher uses the user's own `qemu-system-arm.exe`).
  - QEMU's SSI transfer is synchronous, so a byte-at-a-time link costs one round trip per byte unless the device batches.
- **Unknown:** whether any NextPi software uses the debug control yet.

## 5. What jnext would need (not decided)

1. **The Next side**:
   - the SPI slave and its command state machine;
   - CPU pause and step through BUSRQ;
   - access to SRAM, BRAM, NextREGs and registers;
   - the SYSTEM resets, NMIs and interrupt;
   - STATUS and the QWERT hotkeys.

   The debugger backend already provides each of these operations.
2. **The Pi side**, which is the blocker (section 4): a chardev-backed SSI device in QEMU, plus a DTB copy with SPI0 enabled and its DMA channels removed.
3. **Smaller 3.02.04 items** that do not need the Pi: the PS/2 F5/F6/F12 change and the MD six-button identification.
4. **Version policy:** all of this exists only on the `30204` branch. Adopting any of it means citing that branch instead of `master` for those features.
