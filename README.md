# JNEXT — ZX Spectrum Next Emulator

A real-time, cross-platform software emulator of the **ZX Spectrum Next** computer, written in C++17. JNEXT aims to accurately reproduce the behavior of the ZX Spectrum Next hardware, based on the official FPGA core VHDL sources as the authoritative reference.

## Status

JNEXT is in early development. The emulator currently boots to 48K BASIC with ULA video rendering and keyboard input. Extended video modes (Layer 2, sprites, tilemap), audio, and peripherals are planned for future phases.

## Building

### Requirements

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16 or later
- SDL2 development libraries

**Linux (Fedora/RHEL):**
```sh
sudo dnf install SDL2-devel cmake gcc-c++
```

**Linux (Debian/Ubuntu):**
```sh
sudo apt install libsdl2-dev cmake g++
```

**macOS:**
```sh
brew install sdl2 cmake
```

### Build steps

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext
make release
```

The executable is `build/release/zxnext`.

### Makefile targets

| Target               | Description                                            |
|----------------------|--------------------------------------------------------|
| `make debug`         | Configure and build in Debug mode (with debug symbols) |
| `make debug-run`     | Build (if needed) and run the emulator in Debug mode   |
| `make debug-clean`   | Remove the debug build directory                       |
| `make release`       | Configure and build in Release mode (optimized)        |
| `make release-run`   | Build (if needed) and run the emulator in Release mode |
| `make release-clean` | Remove the release build directory                     |
| `make clean`         | Remove all build directories                           |

### ROM files

JNEXT does not include ROM files. Place a 48K Spectrum ROM as `roms/48.rom` in the project directory. The emulator will warn and continue without it if the file is not found.

## Usage

```
./build/jnext [options]
```

### Command-line options

| Option             | Description                                                                         |
|--------------------|-------------------------------------------------------------------------------------|
| `--log-level SPEC` | Set per-subsystem log levels. SPEC is a comma-separated list of `name=level` pairs. |

### Log levels

Available levels: `trace`, `debug`, `info`, `warn`, `err`, `critical`, `off`

Available subsystems: `cpu`, `memory`, `ula`, `video`, `audio`, `port`, `nextreg`, `dma`, `copper`, `uart`, `input`, `platform`, `emulator`

**Examples:**
```sh
# Trace CPU activity, warn-only for video
./build/jnext --log-level cpu=trace,video=warn

# Debug port I/O
./build/jnext --log-level port=debug

# Silence all logging
./build/jnext --log-level emulator=off,platform=off
```

### Keyboard shortcuts

| Key   | Action                            |
|-------|-----------------------------------|
| `F2`  | Cycle window scale (1x / 2x / 3x) |
| `F11` | Toggle fullscreen                 |

### Keyboard mapping

The ZX Spectrum keyboard is mapped to PC keys as follows:

| PC Key             | Spectrum Key               |
|--------------------|----------------------------|
| Letter/number keys | Corresponding Spectrum key |
| Left Ctrl          | Caps Shift                 |
| Left/Right Shift   | Symbol Shift               |
| Backspace          | Delete (Caps Shift + 0)    |
| Enter              | Enter                      |

## License

Copyright (C) 2025-2026 Jorge Gonzalez Villalonga

JNEXT is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

JNEXT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [LICENSE](LICENSE) file for the full license text.
