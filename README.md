# JNEXT -- ZX Spectrum Next Emulator

**A developer's emulator for the ZX Spectrum Next, derived directly from the official FPGA VHDL sources.**

JNEXT is a real-time, cross-platform software emulator of the ZX Spectrum Next computer, written in C++17. It uses the official ZX Next FPGA core VHDL sources as the authoritative hardware reference, translating gate-level behavior into accurate software emulation.

**Status:** Beta -- actively developed. Issues and pull requests are welcome.

**GitHub:** [https://github.com/jorgegv/jnext](https://github.com/jorgegv/jnext)

---

## About this project

JNEXT was fully developed by Claude (Anthropic's AI), with human guidance and supervision from Jorge Gonzalez Villalonga. The complete prompt history, design documents, daily task files, and development documentation are available in the repository. This makes JNEXT not just an emulator, but also a practical case study in developing a large, complex piece of software using AI-assisted programming with Claude.

---

## Emulated machines

| Machine                    | Description                               |
|----------------------------|-------------------------------------------|
| ZX Spectrum 48K            | Original rubber-key Spectrum              |
| ZX Spectrum 128K           | 128K with AY sound and memory paging      |
| ZX Spectrum +3             | Amstrad +3 with extended paging           |
| Pentagon 128               | Russian Pentagon clone                    |
| ZX Spectrum Next (Issue 2) | Full Next hardware with extended features |

## Emulated hardware

- **Z80N CPU** -- Standard Z80 plus Next extended instructions (Z80N)
- **ULA** with memory contention and per-scanline border rendering
- **Layer 2** -- 256x192, 320x256, and 640x256 modes
- **Hardware sprites** -- 128 sprites with scaling and anchoring
- **Tilemap** engine
- **Copper** co-processor
- **AY-3-8912 sound** -- TurboSound with 3 AY chips
- **DAC audio** (4-channel)
- **Beeper** with real-time EAR audio
- **DMA** (Z80 DMA compatible)
- **DivMMC** with SD card interface
- **UART**
- **CTC** (Counter/Timer Circuit)
- **MMU** -- 8-slot memory management unit
- **Kempston and Sinclair joystick** emulation
- **Keyboard** with compound key mapping (arrows, delete)

## Features

### Qt6 GUI

JNEXT includes a full-featured Qt6 graphical interface:

- **File loading** -- NEX, SNA, SZX, TAP, TZX, and WAV format support via File menu or toolbar
- **Machine type selection** -- Switch between 48K, 128K, +3, Pentagon, and Next on the fly
- **CPU speed control** -- 3.5 MHz, 7 MHz, 14 MHz, 28 MHz
- **Tape controls** -- Open, eject, rewind; fast load or real-time playback modes
- **SD card mounting** -- Mount `.img` disk images for DivMMC
- **Reset** -- Soft reset via menu, toolbar, or Ctrl+R
- **CRT filter toggle** -- Simulated CRT scanline effect
- **Fullscreen** -- True fullscreen with aspect-ratio-correct letterboxing (F11)
- **Scalable display** -- 2x, 3x, 4x integer scaling with Hi-DPI / pixel-perfect rendering
- **FPS and status bar** -- Real-time FPS, CPU speed, tape status, and machine type indicators

### SDL2 minimal interface

For quick testing or lightweight use, JNEXT also runs with a plain SDL2 window (when built without Qt6):

| Key   | Action                            |
|-------|-----------------------------------|
| F2    | Cycle window scale (1x / 2x / 3x) |
| F11   | Toggle fullscreen                 |

### Debugger

The integrated debugger is a major feature, opening in a separate window alongside the emulator. It provides:

- **CPU register panel** -- All Z80 registers (AF, BC, DE, HL, IX, IY, SP, PC, I, R, alternate set) and individual flag display (S, Z, H, P/V, N, C), with halt and interrupt mode indicators
- **Disassembly view** -- Scrollable disassembly with address navigation, current-PC highlighting, breakpoint gutter, follow-PC mode, and run-to-cursor (via context menu)
- **Memory hex editor** -- Full 64K memory view with hex and ASCII columns, inline byte editing, page/bank selector, and address navigation
- **Breakpoint management** -- Execution breakpoints (click gutter or via panel), memory read/write/read-write watchpoints, clear-all, add/edit/remove dialog
- **Stepping controls** -- Single step (F6), step over (F7), step out (F8), run/continue (F5), pause/break (F9)
- **Watch expressions** -- Monitor byte, word, or long values at arbitrary addresses with custom labels
- **Sprite viewer** -- Table view of all 128 hardware sprites with attributes
- **Video panel** -- Raster position, layer visibility, layer priority, and ULA palette swatch
- **Copper disassembly** -- Decoded copper instructions with current PC indicator
- **NextREG viewer** -- All 256 NextREG registers with names and editable values
- **Audio panel** -- AY register state for all 3 TurboSound chips, mute controls for AY/DAC/beeper, stereo mode display
- **Trace log** -- Enable/disable instruction tracing (F2), clear, and export to text file
- **Symbol table support** -- Load Z88DK or simple-format MAP files for symbolic address display
- **Breakpoint panel** -- Unified view of all execution and data breakpoints with type indicators

Debugger keyboard shortcuts:

| Key   | Action         |
|-------|----------------|
| F5    | Run / Continue |
| F6    | Single Step    |
| F7    | Step Over      |
| F8    | Step Out       |
| F9    | Pause / Break  |
| F2    | Toggle Trace   |
| F3    | Export Trace   |

### Command-line interface for automated testing

JNEXT supports headless operation for CI pipelines and automated testing:

```
./build/jnext [options]
```

| Option                        | Description                                                  |
|-------------------------------|--------------------------------------------------------------|
| `--headless`                  | Run without display or audio, at maximum speed               |
| `--machine-type TYPE`         | `48k`, `128k`, `plus3`, `pentagon`, `next` (default)         |
| `--load FILE`                 | Load a program file (NEX or TAP, auto-detected by extension) |
| `--tape-realtime`             | Use real-time tape loading speed instead of fast load        |
| `--delayed-screenshot FILE`   | Save a PNG screenshot after a delay                          |
| `--delayed-screenshot-time N` | Delay in seconds before screenshot (default: 10)             |
| `--delayed-automatic-exit N`  | Exit the emulator after N seconds                            |
| `--sd-card FILE`              | Mount an SD card image (.img)                                |
| `--roms-directory DIR`        | Directory containing ROM files (default: `/usr/share/fuse`)  |
| `--log-level SPEC`            | Per-subsystem log levels (e.g. `cpu=trace,video=warn`)       |

A full automated regression test suite is included, running the FUSE Z80 opcode tests (1340/1356 pass, 98.8%) and screenshot comparison tests in headless mode:

```bash
bash test/regression.sh
```

---

## Building

### Requirements

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16 or later
- SDL2 development libraries
- Qt6 development libraries (for the full GUI; optional)

**Linux (Fedora/RHEL):**
```sh
sudo dnf install SDL2-devel cmake gcc-c++ qt6-qtbase-devel
```

**Linux (Debian/Ubuntu):**
```sh
sudo apt install libsdl2-dev cmake g++ qt6-base-dev
```

**macOS:**
```sh
brew install sdl2 cmake qt@6
```

### Build steps

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext
cmake -B build -DENABLE_QT_UI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The executable is `build/jnext`.

To build without the Qt6 GUI (SDL2-only):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### CMake options

| Option            | Default | Description                         |
|-------------------|---------|-------------------------------------|
| `ENABLE_QT_UI`    | OFF     | Build the Qt6 native GUI            |
| `ENABLE_DEBUGGER` | ON      | Include the debugger (requires Qt6) |
| `ENABLE_TESTS`    | ON      | Build the test suite                |
| `CYCLE_ACCURATE`  | OFF     | 28 MHz cycle-accurate mode          |
| `STATIC_BUILD`    | OFF     | Link statically                     |

### ROM files

JNEXT does not ship ROM files. By default, it loads ROMs from `/usr/share/fuse/` (installed by the FUSE emulator package):

| Machine  | ROM files                           |
|----------|-------------------------------------|
| 48K      | `48.rom`                            |
| 128K     | `128-0.rom`, `128-1.rom`            |
| +3       | `plus3-0.rom` through `plus3-3.rom` |
| Pentagon | `128p-0.rom`, `128p-1.rom`          |

Override the ROM directory with `--roms-directory DIR`.

---

## Quick start

```sh
# Run with the default ZX Next machine type
./build/jnext

# Run as ZX Spectrum 48K
./build/jnext --machine-type 48k

# Load and run a NEX file
./build/jnext --load game.nex

# Load a TAP file (will auto-type LOAD "")
./build/jnext --load game.tap

# Headless screenshot for testing
./build/jnext --headless --machine-type 48k \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

### Keyboard mapping

| PC Key             | Spectrum Key               |
|--------------------|----------------------------|
| Letter/number keys | Corresponding Spectrum key |
| Left Ctrl          | Caps Shift                 |
| Left/Right Shift   | Symbol Shift               |
| Backspace          | Delete (Caps Shift + 0)    |
| Arrow keys         | Cursor keys (5/6/7/8)      |
| Enter              | Enter                      |

---

## Libraries and third-party software

| Library                                    | License | Description                                                                          |
|--------------------------------------------|---------|--------------------------------------------------------------------------------------|
| [SDL2](https://www.libsdl.org/)            | zlib    | Cross-platform multimedia library                                                    |
| [Qt6](https://www.qt.io/)                  | LGPLv3  | GUI framework                                                                        |
| [spdlog](https://github.com/gabime/spdlog) | MIT     | Fast C++ logging library (vendored as git submodule)                                 |
| FUSE Z80 core                              | GPLv2   | Z80 CPU core adapted from the [FUSE](http://fuse-emulator.sourceforge.net/) emulator |
| [ZOT](https://github.com/antirez/zot)      | MIT     | TZX/TAP tape player library by antirez (vendored in third_party/zot/)                |

## References and acknowledgments

- **ZX Spectrum Next FPGA core** -- The official VHDL sources serve as the authoritative hardware specification for this emulator.
- **[FUSE](http://fuse-emulator.sourceforge.net/)** -- The Z80 CPU core is adapted from FUSE. ROM files are loaded from the FUSE package installation.
- **[ZesarUX](https://github.com/chernandezba/zesarux)** -- Used as a behavioral reference during development.

---

## License

Copyright (C) 2026 Jorge Gonzalez Villalonga

JNEXT is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

JNEXT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [LICENSE](LICENSE) file for the full license text.
