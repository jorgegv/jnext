# JNEXT — ZX Spectrum Next Emulator

**A developer's emulator for the ZX Spectrum Next**

JNEXT is a real-time software emulator of the ZX Spectrum Next computer, written in C++17. It uses the official ZX Next FPGA core VHDL sources as the authoritative hardware reference. The main goal is to have a _good enough_ emulator to allow comfortable game development for the Next, and a good debugger with lots of introspection features.

Specifically, _extremely faithful emulation_ of the ZX Next is **not the main goal** of this emulator. But indeed having the VHDL sources available makes it easier to know the expected behaviour.

**Current Status: ALPHA** — features still being actively developed. Issues and pull requests will be welcome once the repo is in a somewhat stable state. For now, I recommend to just wait until a mostly working emulator is published.

See the [JNEXT full development plan](doc/design/EMULATOR-DESIGN-PLAN.md) for the real status of the project, the features that have already been implemented, upcoming features and others that are further in the roadmap.

![JNEXT screenshot](doc/SCREENSHOT.png)

Current status of the screenshot-based regression tests: [CURRENT-REGRESSION-STATE](doc/CURRENT-REGRESSION-STATE.md)

**UPDATE 10/7/26:** NextZXOS boot achieved!

![JNEXT NextZXOS boot screenshot](doc/JNEXT-NEXTZXOS-BOOT.png)


---

## About this project

JNEXT is being fully developed by Claude (Anthropic's AI), with human guidance and supervision. The complete prompt history, design documents, daily task files, and development documentation are available in the repository. This makes JNEXT not just an emulator, but also a practical case study in developing a large, complex piece of software using AI-assisted programming.

---

## Emulated machines

| Machine                    | Description                               |
|----------------------------|-------------------------------------------|
| ZX Spectrum 48K            | Original rubber-key Spectrum              |
| ZX Spectrum 128K           | 128K with AY sound and memory paging      |
| ZX Spectrum +3             | Amstrad +3 with extended paging           |
| ZX Spectrum Next (Issue 2) | Full Next hardware with all extended features |

## Emulated hardware

JNEXT emulates the full ZX Spectrum Next hardware feature set: Z80N CPU,
ULA (48K + Timex modes), Layer 2, hardware sprites, tilemap, Copper,
layer compositor, AY-3-8910 × 3 (TurboSound), DAC, Beeper, DMA, DivMMC,
UART, CTC, SPI / I2C / RTC, IM1/IM2 interrupts, joystick, and keyboard.

For the authoritative, always-current inventory of what is implemented,
what has known gaps, and what is out of scope, see:

- **[Known functionality gaps and proposed plan](doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md)**
  — every subsystem-level gap, sorted by display-impact priority,
  with affected subsystems, user-visible impact, and effort estimate.
- **[Subsystem unit-test status](test/SUBSYSTEM-TESTS-STATUS.md)**
  — live per-subsystem dashboard (Pass / Fail / Skip rows) reflecting
  exactly what is verified against the ZX Next FPGA VHDL as oracle.

---

## Features

### Qt6 GUI

- File loading — NEX, SNA, SZX, TAP, TZX, WAV via File menu or toolbar
- Machine type selection — 48K, 128K, +3, Next
- CPU speed control — 0.5×, 1×, 2×, 4×, or custom percentage
- Tape controls — Open, eject, rewind; fast load or real-time playback
- SD card mounting — Mount `.img` disk images for DivMMC
- PNG screenshot — File > Save Screenshot or Ctrl+S
- Video recording — Record to MP4 via FFmpeg (File > Start/Stop Recording)
- RZX playback and recording — File menu
- CRT scanline filter — View menu toggle
- Fullscreen — True fullscreen with letterbox aspect ratio (F11)
- Scalable display — 2×, 3×, 4× integer scaling, Hi-DPI pixel-perfect rendering
- Status bar — FPS, CPU speed, machine type, tape status, rewind state

### Debugger

The integrated debugger opens in a separate window and provides full introspection into the running emulator:

- **CPU registers** — All Z80/Z80N registers, flags (S/Z/H/P/V/N/C), halt, interrupt mode, ULA active screen
- **MMU panel** — Next 8-slot MMU table with page numbers and type; 128K bank mappings
- **Disassembly** — Scrollable Z80+Z80N disassembly, PC highlighting, breakpoint gutter, follow-PC mode, run-to-cursor, symbol names from MAP files
- **Memory hex editor** — Full 64K view, hex+ASCII, inline editing, page/bank selector
- **Stack panel** — SP-relative word view, SP row highlighted
- **Call stack** — CALL/RST/INT/RET tracking with symbol resolution
- **Breakpoints** — Execution, read, write, and I/O watchpoints; unified panel view
- **Watch expressions** — Byte, word, or long at arbitrary addresses with custom labels
- **Video subpanels** — ULA (primary+shadow), Layer 2 (active+shadow), Sprites, Tilemap; per-scanline view up to current raster position; checkerboard for transparent pixels
- **Sprite viewer** — All 128 hardware sprites with full attribute table
- **Copper disassembly** — Decoded WAIT/MOVE instructions with current PC indicator
- **NextREG panel** — All 256 registers with names, hex values, editable inline
- **Audio panel** — AY register state for all 3 TurboSound chips, per-source mute controls
- **Trace log** — Circular instruction trace buffer, export to file
- **Symbol table** — Load Z88DK MAP files; symbols shown inline in disassembly and watches
- **Backwards execution (rewind)** — Frame snapshot ring buffer; Step Back, Frame Back, rewind slider; configurable buffer size

Debugger keyboard shortcuts:

| Key        | Action           |
|------------|------------------|
| F5         | Run / Continue   |
| F6         | Step Into        |
| F7         | Step Over        |
| F8         | Step Out         |
| F9         | Pause / Break    |
| Shift+F6   | Frame Back       |
| Shift+F7   | Step Back        |

### Magic breakpoint and magic port

- **Magic breakpoint** — `ED FF` (ZEsarUX) or `DD 01` (CSpect) opcodes trigger debugger pause when enabled; act as NOP otherwise. Enable via `--magic-breakpoint` or Debug menu.
- **Magic debug port** — Writes to a configurable port are logged to stderr in hex, decimal, ASCII, or line-buffered mode. Enable via `--magic-port PORT --magic-port-mode MODE`.

### Command-line interface

```
./build/gui-release/jnext [options]
```

| Option                        | Description                                                          |
|-------------------------------|----------------------------------------------------------------------|
| `--machine TYPE`         | `48k`, `128k`, `plus3`, `next` (default)                             |
| `--load FILE`                 | Load NEX, SNA, SZX, TAP, TZX, or WAV (auto-detected by extension)   |
| `--sdcard FILE`              | Mount SD card image (.img); canonical source for all ROMs. Optional — if omitted, jnext uses `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (the patched image), offering to download the canonical distribution (kept as `cspect-next-1gb.img`) and produce that patched copy |
| `--sdcard-download-confirm`   | Skip the download prompt and provision the default image automatically |
| `--sdcard-download-force`     | Force re-download + re-patch of the default-location image (ignored when `--sdcard` is given) |
| `--speed PERCENT`             | Emulator speed: 50=half, 100=normal, 200=2×, 400=4×                 |
| `--headless`                  | Run without display or audio, at maximum speed                       |
| `--tape-realtime`             | Real-time tape loading instead of fast load                          |
| `--esxdos-stub`               | Stub RST $08 esxdos calls so NEX games boot without NextZXOS         |
| `--rtc "YYYY-MM-DD HH:MM:SS"` | Pin the RTC to a fixed date/time (frozen clock; ISO `T` form also accepted) |
| `--inject FILE`               | Load raw binary into RAM (see `--inject-org`, `--inject-pc`)         |
| `--inject-org ADDR`           | Load address for `--inject` (hex, default: 8000)                     |
| `--inject-pc ADDR`            | Entry point for `--inject` (hex, default: same as `--inject-org`)    |
| `--inject-delay N`            | Wait N frames before injecting (default: 0)                          |
| `--record FILE`               | Record video+audio to MP4 via FFmpeg                                 |
| `--rzx-play FILE`             | Play back an RZX recording                                           |
| `--rzx-record FILE`           | Record to RZX                                                        |
| `--rewind-buffer-size N`      | Frame snapshot ring buffer size (default: 500, 0=off)                |
| `--magic-breakpoint`          | Enable magic breakpoint opcodes (ED FF / DD 01)                      |
| `--magic-port PORT`           | Enable magic debug port at PORT (hex, e.g. 0x00FF)                   |
| `--magic-port-mode MODE`      | Magic port output mode: `hex`, `dec`, `ascii`, `line`                |
| `--delayed-screenshot FILE`   | Save a PNG screenshot after a delay                                  |
| `--delayed-screenshot-time N` | Delay in seconds before screenshot (default: 10)                     |
| `--delayed-screenshot-frames N` | Delay in frames before screenshot (overrides `--delayed-screenshot-time`) |
| `--delayed-automatic-exit N`  | Exit after N seconds                                                 |
| `--delayed-keypress SECS KEY` | Headless: press KEY after SECS seconds (repeatable; KEY is a char or ENTER/SPACE) |
| `--delayed-keypress-frames N KEY` | Headless: press KEY after N frames (frame-based form)            |
| `--log-level SPEC`            | Per-subsystem log levels, e.g. `cpu=trace,video=warn`                |
| `--version`                   | Print version and exit                                               |

---

## Building

### Requirements (Linux)

**Fedora / RHEL:**
```sh
sudo dnf install SDL2-devel cmake gcc-c++ qt6-qtbase-devel libpng-devel zlib-devel libcurl-devel openssl-devel
```

**Debian / Ubuntu:**
```sh
sudo apt install libsdl2-dev cmake g++ qt6-base-dev libpng-dev zlib1g-dev libcurl4-openssl-dev libssl-dev
```

_Windows and macOS builds are pending._

### Build steps

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext

# Full Qt6 GUI build (recommended)
make gui-release

# SDL-only build (no GUI, no debugger)
make release
```

Executables are placed in `build/gui-release/jnext` and `build/release/jnext` respectively.

### Available make targets

| Target             | Description                                      |
|--------------------|--------------------------------------------------|
| `make gui-release` | Build Qt6 GUI release (optimised)                |
| `make gui-debug`   | Build Qt6 GUI debug (sanitisers + debug symbols) |
| `make release`     | Build SDL-only release                           |
| `make debug`       | Build SDL-only debug                             |
| `make gui-clean`   | Remove GUI build directories                     |
| `make clean`       | Remove all build directories                     |
| `make regression`  | Run the full automated regression test suite     |
| `make version`     | Show current version                             |
| `make bump`        | Bump minor version, commit, and tag              |
| `make bump-patch`  | Bump patch version, commit, and tag              |
| `make bump-major`  | Bump major version, commit, and tag              |

### ROM files

Wave 0.3 (2026-05-04) made the user-supplied SD-card image the canonical
source for every ROM jnext needs at runtime, mirroring real ZX Spectrum
Next hardware:

- **FPGA boot ROM** (`nextboot.rom`, 8 KB) is silicon-baked: embedded into
  the jnext binary at link time. No flag, no SD lookup.
- **All other ROMs** (DivMMC, NextZXOS, 48K/128K/+3 BASIC, Multiface) are
  read from the `--sdcard` image at canonical TBBlue paths
  (`/MACHINES/NEXT/...`).

The TBBlue distribution image is the standard reference: see
[CLAUDE.md](CLAUDE.md) for the canonical fixture path
(`roms/nextzxos-1gb-fat32fix.img`).

---

## Quick start

jnext needs an SD-card image: it is the canonical source for all peripheral
and machine ROMs (same as real hardware). Point at one with `--sdcard FILE`,
or omit it and let jnext fall back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img`
(the patched image), offering to download the canonical distribution image
(kept as `cspect-next-1gb.img`) and produce that FAT32-patched copy there.

```sh
# Boot NextZXOS / TBBlue firmware (default Next machine)
./build/gui-release/jnext --sdcard roms/nextzxos-1gb-fat32fix.img

# Run as ZX Spectrum 48K (uses /MACHINES/NEXT/48.rom from SD)
./build/gui-release/jnext --machine 48k --sdcard roms/nextzxos-1gb-fat32fix.img

# Load and run a NEX file (boot-ROM overlay skipped automatically)
./build/gui-release/jnext --load game.nex --sdcard roms/nextzxos-1gb-fat32fix.img

# Load a TAP file (will auto-type LOAD "")
./build/gui-release/jnext --load game.tap --sdcard roms/nextzxos-1gb-fat32fix.img

# Load a snapshot
./build/gui-release/jnext --load game.sna --sdcard roms/nextzxos-1gb-fat32fix.img

# Run at double speed
./build/gui-release/jnext --speed 200 --sdcard roms/nextzxos-1gb-fat32fix.img

# Inject a raw binary at address 0x8000 and run
./build/gui-release/jnext --inject program.bin --inject-org 8000 \
    --sdcard roms/nextzxos-1gb-fat32fix.img

# Headless screenshot for CI testing
./build/gui-release/jnext --headless --machine 48k \
    --sdcard roms/nextzxos-1gb-fat32fix.img \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

### Keyboard mapping

| PC Key            | Spectrum key            |
|-------------------|-------------------------|
| Letter/number keys | Corresponding key      |
| Left Ctrl         | Caps Shift              |
| Left/Right Shift  | Symbol Shift            |
| Backspace         | Delete (Caps Shift + 0) |
| Arrow keys        | Cursor keys (5/6/7/8)   |
| Enter             | Enter                   |

---

## Automated testing

A full regression test suite runs the FUSE Z80 opcode tests (1356/1356 pass, 100%) and screenshot comparison tests in headless mode:

```sh
make regression
```

---

## Libraries and third-party software

| Library                                    | License | Description                                                              |
|--------------------------------------------|---------|--------------------------------------------------------------------------|
| [SDL2](https://www.libsdl.org/)            | zlib    | Cross-platform multimedia library (audio + input)                        |
| [Qt6](https://www.qt.io/)                  | LGPLv3  | GUI framework                                                            |
| [libcurl](https://curl.se/libcurl/)        | curl    | HTTP(S) download of the NextZXOS distribution SD-card image              |
| [OpenSSL](https://www.openssl.org/)        | Apache-2.0 | SHA-256 integrity hash of the cached SD-card image (libcrypto)        |
| [FatFs](http://elm-chan.org/fsw/ff/)       | BSD-1   | Host-side FAT32 formatting of the downloaded SD image (vendored, `third_party/fatfs/`) |
| [spdlog](https://github.com/gabime/spdlog) | MIT     | Fast C++ logging library (vendored as git submodule)                     |
| FUSE Z80 core                              | GPLv2   | Z80 CPU core adapted from the [FUSE](http://fuse-emulator.sourceforge.net/) emulator |
| [ZOT](https://github.com/antirez/zot)      | MIT     | TZX/TAP tape player library by antirez (vendored in `third_party/zot/`)  |

---

## References and acknowledgments

- **[ZX Spectrum Next FPGA core](https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA/-/tree/master)** — The official VHDL sources serve as the authoritative hardware specification for this emulator.
- **[TBBlue Firmware sources](https://gitlab.com/thesmog358/tbblue)** — The official firmware sources are useful for problem tracing and debugging.
- **[FUSE](http://fuse-emulator.sourceforge.net/)** — The Z80 CPU core is adapted from FUSE. ROM files are loaded from the FUSE package installation.
- **[ZesarUX](https://github.com/chernandezba/zesarux)** — Multiple Sinclair system emulator with Next support, used as a behavioural reference during development.
- **[CSpect](https://mdf200.itch.io/cspect)** — Another ZX Emulator with Next support, also used as a behavioural reference during development.
- **[ZX Next Wiki](https://wiki.specnext.dev/Main_Page)** - The official reference point for Next development.

---

## License

Copyright (C) 2026 Jorge Gonzalez Villalonga

JNEXT is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

JNEXT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [LICENSE](LICENSE) file for the full license text.
