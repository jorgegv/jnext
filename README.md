# JNEXT — a ZX Spectrum Next emulator built from the FPGA sources

**JNEXT is a real-time ZX Spectrum Next emulator written in C++17, whose
hardware behaviour comes from the official ZX Next FPGA core VHDL — not from
guesswork, and not from other emulators.** When a behaviour is unclear, the
VHDL is the oracle: it decides, and a unit test citing it locks the answer
down.

It is built for **developers**: a comfortable machine to write Next games on,
with a debugger that shows you everything the hardware is doing.

![JNEXT screenshot](doc/SCREENSHOT.png)

## Why this one

- **The VHDL is the specification.** Ambiguous behaviour is resolved against
  the FPGA core sources, and pinned there by unit tests.
- **A debugger that sees the whole machine.** Z80/Z80N disassembly, memory,
  the 8-slot MMU, all 256 NextREGs, 128 sprites, the Copper, the tilemap, the
  layer compositor, the AY chips — plus **backwards execution**: step back,
  frame back, scrub a rewind slider.
- **It boots the real thing.** NextZXOS cold-boots through the authentic chain
  (FPGA boot ROM → TBBLUE.FW → NextZXOS) off an SD-card image, like real
  hardware — all machine and peripheral ROMs come from the SD card, not from
  the emulator.
- **Headless and scriptable.** Deterministic PNG screenshots (frame-accurate,
  per-layer, with a pinnable RTC), keypress injection, a T-state profiler, RZX
  and MP4 recording: enough to put a Next program under CI.

![JNEXT booting NextZXOS](doc/JNEXT-NEXTZXOS-BOOT.png)

## What it emulates

Four machines — **48K, 128K, +3 and ZX Spectrum Next (Issue 2)** — and the
Next hardware set: Z80N CPU, ULA (48K + Timex modes), Layer 2, 128 hardware
sprites, tilemap, Copper, the layer compositor, 3 × AY-3-8910 (TurboSound),
DAC, beeper, DMA, DivMMC, Multiface, UART, CTC, SPI / I²C / RTC, IM1/IM2
interrupts, keyboard and USB gamepads. It loads NEX, SNA, SZX, TAP, TZX, WAV
and RZX.

**Status: ALPHA**, and honest about it. Extreme cycle-exactness is explicitly
*not* a goal — "good enough to develop games on" is. Some things are still
rough or missing; they are listed, not hidden:

- [TODO.md](TODO.md) — known issues and pending features
- [doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md](doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md)
  — every subsystem-level gap, sorted by display impact
- [test/SUBSYSTEM-TESTS-STATUS.md](test/SUBSYSTEM-TESTS-STATUS.md) — live
  per-subsystem unit-test dashboard: exactly what is verified against the VHDL

Linux only for now; Windows and macOS ports are pending.

## Quick start

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext
make gui-release            # Qt6 GUI build → build/gui-release/jnext

# Boot NextZXOS (jnext offers to download an SD-card image on first run)
./build/gui-release/jnext

# Run a program — a bare filename is all it takes
./build/gui-release/jnext game.tap
```

Build details in **[BUILD.md](BUILD.md)**; everything you can do with it in
**[USAGE.md](USAGE.md)**.

## Documentation

| Document | What's in it |
|----------|--------------|
| [BUILD.md](BUILD.md) | Prerequisites, build targets, CMake options, running the tests |
| [USAGE.md](USAGE.md) | Every CLI option, SD card and ROMs, the GUI, the debugger, keyboard map |
| [FEATURES.md](FEATURES.md) | Full feature list |
| [TODO.md](TODO.md) | Pending features and known issues |
| [ChangeLog](ChangeLog) | What changed in each release |
| [CREDITS.md](CREDITS.md) | Third-party libraries, references and acknowledgments |
| [doc/design/EMULATOR-DESIGN-PLAN.md](doc/design/EMULATOR-DESIGN-PLAN.md) | The development plan: real status, implemented features, roadmap |
| [doc/CURRENT-REGRESSION-STATE.md](doc/CURRENT-REGRESSION-STATE.md) | Current state of the screenshot regression suite |

Issues and pull requests will be welcome once the repository is in a somewhat
stable state; for now, waiting for a mostly working emulator is the better
plan.

## About this project

JNEXT is being fully developed by Claude (Anthropic's AI), with human guidance
and supervision. The complete prompt history, design documents, daily task
files and development documentation are in the repository — which makes JNEXT
not just an emulator, but a practical case study in building a large, complex
piece of software with AI-assisted programming.

## License

Copyright (C) 2026 Jorge Gonzalez Villalonga

JNEXT is free software: you can redistribute it and/or modify it under the
terms of the **GNU General Public License** as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

JNEXT is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the [LICENSE](LICENSE) file for the full license
text.
