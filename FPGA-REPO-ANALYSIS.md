# FPGA Repository Analysis

## Repository Purpose

This is documentation for the repository that is at path `../ZX_Spectrum_Next_FPGA` which is a clone of the official ZX Spectrum Next FPGA core repository. The primary goal in this context is to use the VHDL source as a reference specification for writing a software emulator in C/C++. The VHDL is authoritative: when in doubt about hardware behavior, consult the VHDL source. Paths mentioned in this document refer to the original FPGA source repo.

This repository shall contain the generated emulator code.

## Build System

The FPGA build requires **Xilinx ISE 14.7** (Issue 2 / Spartan 6) or **Vivado** (Issues 4 & 5 / Artix 7). These are GUI-based toolchains with no standard Makefile-driven build. There is no automated test suite.

**ISE build (Issue 2):**
- Open `cores/zxnext/synth-zxnext/zxnext-issue2.xise` in ISE
- Right-click "Generate Programming File" → Re-Run All
- Output: `synth-zxnext/work/zxnext_top_issue2.bit`

**Firmware update file (TBBLUE.TBU):**
```sh
cd cores/zxnext/tbumaker
./maketbu   # prompts for version number (e.g. 30203 for v3.02.03)
```

**C utilities (in `cores/zxnext/utils/` and `cores/zxnext/mcsmaker/`):**
```sh
gcc -o bin2coe bin2coe.c
gcc -o bin2txt bin2txt.c
g++ -o romgen romgen.cpp
gcc -o maketbu maketbu.c
```

## VHDL Source Structure

All source lives under `cores/zxnext/src/`:

| Directory               | Contents                                                 |
|-------------------------|----------------------------------------------------------|
| `zxnext.vhd`            | **Main machine entity** — instantiates all subsystems    |
| `zxnext_top_issue2.vhd` | Spartan 6 top-level (pin connections, housekeeping)      |
| `zxnext_top_issue4.vhd` | Artix 7 Issue 4 top-level                                |
| `zxnext_top_issue5.vhd` | Artix 7 Issue 5 top-level (latest, XADC + XDNA)          |
| `cpu/`                  | T80N Z80 + Z80N extensions (ALU, microcode, pack)        |
| `video/`                | ULA, Layer 2, sprites, tilemap, HDMI, VGA, scan doubler  |
| `audio/`                | AY-3-8910 (YM2149), DAC, I2S, PWM, mixer                 |
| `device/`               | Copper, CTC, DivMMC, DMA, IM2, MultiSpeaker, peripherals |
| `input/`                | PS/2 keyboard, membrane matrix, MD joystick              |
| `serial/`               | UART, SPI master, I2C, FIFO                              |
| `ram/`                  | Dual-port BRAM wrappers (DPRAM, TDPRAM, SDPRAM)          |
| `rom/`                  | Boot ROMs                                                |
| `misc/`                 | Debounce, synchronizers, flash boot                      |
| `pll/`                  | Clock generation (separate PLL per board variant)        |

## Machine Architecture (for Emulator Development)

### Clock Domains
- **28 MHz** — main system clock; machine logic runs here
- **14 MHz** — hi-res pixel clock (video generation)
- **7 MHz** — standard pixel clock
- **CPU clock** — derived from 28 MHz, software-selectable: 3.5 / 7 / 14 / 28 MHz
- **PSG enable** — 28 MHz clock-enable signal for AY chips

### CPU
- **T80N** (Z80 compatible + Z80N extensions): `cpu/T80N.vhd`, `cpu/T80_ALU.vhd`
- 4 software-selectable speeds; wait states inserted for memory contention
- Bus shared with ZXN-DMA (daisy-chain arbitration)
- DMA has Z80-DMA compatibility mode and a burst mode for sampled audio

### Memory Map (CPU view)
- 8×8K MMU slots covering the 64K address space (native ZX Next banking)
- Traditional 128K banking also supported via ports `0x7FFD` / `0x1FFD` / `0xDFFD`
- 768K RAM (unexpanded) or 1792K (expanded), organized as 16K banks / 8K pages
- 64K ROM (4 slots, +3-compatible) + 32K user-programmable Alt ROM
- External 2 MB SRAM on physical pins; accessed by both CPU and Layer 2 video

### Video Subsystem (layer priority, bottom to top)
1. **ULA** (`video/ula.vhd`) — 48K/128K/+3/Pentagon-compatible, attributes, floating bus, contention; Timex hi-colour/hi-res modes
2. **LoRes** — 128×96 4-bit or 8-bit; shares ULA layer slot
3. **Layer 2** — 256×192 / 320×256 / 640×256 8-bit or 4-bit bitmap; DMA-accessible; hardware scroll
4. **Tilemap** — 40×32 or 80×32 grid of 8×8 glyphs, 4-bit colour; hardware scroll
5. **Sprites** — up to 128 sprites, 16×16 px, 8-bit or 4-bit colour, 1×/2×/4×/8× scale, rotate/mirror, sprite linking

Layer priority is programmable via NextREG `0x15`.

### NextREG Interface (control plane)
- Port `0x243B` — register select
- Port `0x253B` — register data
- Key registers for emulator: `0x00` (machine ID), `0x01` (core version), `0x02` (reset), `0x03` (machine type + video timing), `0x07` (CPU speed), `0x15` (layer priority), `0x50`-`0x57` (palette), `0x80`-`0x85` (port decode/expansion bus)
- Full register reference: `cores/zxnext/nextreg.txt`

### Audio
- 3× AY-3-8910 / YM2149 (turbosound): `audio/ym2149.vhd`
- 4× 8-bit DACs (stereo L+R pairs; compatible with Specdrum / Soundrive peripherals)
- Beeper + tape signal
- Raspberry Pi I2S input (can be mixed or routed to EAR for tape loading)
- HDMI audio output + optional internal speaker (PWM)

### Peripherals & I/O
- **DivMMC** (`device/divmmc.vhd`) — SD card + ROM banking; port `0xE3`; MAPRAM feature
- **CTC** (`device/ctc.vhd`) — 4 counter/timer channels; ports `0x183B`–`0x1B3B`
- **UART** — 2 channels (ESP WiFi, Raspberry Pi GPIO); ports `0x133B`–`0x163B`
- **SPI** (`serial/spi_master.vhd`) — SD/flash/Pi; ports `0xE7` (CS), `0xEB` (data)
- **I2C** — RTC, HDMI, Pi; ports `0x103B`/`0x113B`
- **Copper** (`device/copper.vhd`) — display-synchronized co-processor; modifies NextREG state at precise raster positions
- **IM2** (`device/im2.vhd`) — 14-vector hardware IM2 interrupt controller with daisy-chain priority

### Input
- Membrane keyboard matrix (8×5 read via ULA port `0xFE`)
- PS/2 keyboard (`input/ps2_keyboard.vhd`) with programmable function-key mapping
- PS/2 mouse (Kempston protocol)
- 2× joystick (Sinclair, Kempston, MD 6-button modes)

### Standard I/O Ports Summary
| Port              | Function                                            |
|-------------------|-----------------------------------------------------|
| `0xFE`            | ULA (keyboard rows, border colour, beeper, EAR/MIC) |
| `0xFF`            | Timex video mode                                    |
| `0x7FFD`          | 128K RAM bank select                                |
| `0x1FFD`          | +3 extended paging                                  |
| `0xDFFD`          | Extended bank bits (bits 3:0 high bank)             |
| `0xBFFD`/`0xFFFD` | AY register data / select                           |
| `0x303B`          | Sprite control                                      |
| `0x57`/`0x5B`     | Sprite attributes / patterns                        |
| `0x123B`          | Layer 2 control                                     |
| `0x243B`/`0x253B` | NextREG select / data                               |
| `0xE3`            | DivMMC control                                      |
| `0xE7`/`0xEB`     | SPI chip-select / data                              |

Full port map: `cores/zxnext/ports.txt`

## Key Documentation Files

| File                        | Description                            |
|-----------------------------|----------------------------------------|
| `cores/zxnext/ports.txt`    | Complete I/O port map                  |
| `cores/zxnext/nextreg.txt`  | NextREG register definitions           |
| `cores/zxnext/changelog.md` | Full version history v2.0.0 → v3.02.03 |
| `cores/zxnext/README.md`    | Build instructions and machine specs   |

## Emulator Development Notes

- **VHDL is the spec**: when hardware behavior is ambiguous, trace through the VHDL signal paths — `zxnext.vhd` is the integration point; subsystem files are the ground truth for timing.
- **Contention model**: ULA contention is implemented in `video/ula.vhd`; the CPU wait-state injection is in `zxnext.vhd`. The contention pattern depends on which machine type is selected (48K vs 128K vs Pentagon).
- **Video timing**: Pixel counters and display timing are in `video/ula.vhd`. The Copper co-processor hooks into these counters — emulate it as a list of (hpos, vpos, nextreg, value) events executed during raster scan.
- **MMU banking**: MMU slot→page mapping is held in 8 registers (`0x50`–`0x57` in the NextREG space of `zxnext.vhd`). The traditional 128K banking ports modify the same underlying page map.
- **Interrupt timing**: IM2 vectors are in `device/im2.vhd`; interrupts fire at specific raster line positions relative to the ULA frame counter.
- **Board variants**: For emulator purposes, target Issue 2 behavior (Spartan 6) as the reference unless targeting Issue 5 features (XADC, XDNA).
