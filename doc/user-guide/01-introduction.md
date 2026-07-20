# 1. Introduction

JNEXT is a ZX Spectrum Next emulator. It runs Next software — and ordinary
ZX Spectrum software — in real time on Linux, Windows and macOS, with sound,
gamepads, tape and snapshot loading, video recording, and a debugger for the
people who write that software.

This guide is task-oriented: it shows you how to do things. The complete list
of command-line options lives in the manual page (`man jnext`, or
[USAGE.md](../../USAGE.md)); both are generated from a single source, so they
never disagree with each other.

## What it emulates

| Area | What you get |
|------|--------------|
| Machines | ZX Spectrum 48K, 128K, +2A/+3, and ZX Spectrum Next (Issue 2) |
| CPU | Z80N — the Next's Z80 with its extra instructions |
| Video | ULA (including the Timex modes), Layer 2, 128 hardware sprites, tilemap, Copper, and the layer compositor |
| Audio | Three AY-3-8910 chips (TurboSound), the DAC, and the beeper |
| Storage and peripherals | DivMMC and SD card, Multiface, DMA, UART, CTC, SPI / I²C / real-time clock |
| Input | Keyboard, USB gamepads, Kempston mouse |
| File formats | NEX, SNA, SZX, Z80, TAP, TZX, WAV, RZX |

The Next boots the real NextZXOS operating system, through the real boot chain,
from a real SD-card image — exactly as the hardware does. Every ROM the machine
needs comes off that SD card rather than out of the emulator. [Chapter
3](03-first-run.md) covers how you get one.

## How faithful it is

JNEXT is built from the official ZX Spectrum Next FPGA sources — the VHDL that
describes the actual hardware. Where a machine's behaviour is ambiguous or
undocumented, the answer is taken from that source rather than guessed at, and
pinned there by a test. In practice this means the emulator's answer is the
silicon's answer.

Two honest caveats:

- **Extreme cycle-exactness is not a goal.** "Good enough to develop games on"
  is. A handful of demos that depend on sub-scanline timing may not be
  pixel-perfect.
- **JNEXT is beta.** Rough edges exist and are listed rather than hidden — see
  chapter 8, *Known issues*, and the issue tracker below.

## Machines at a glance

Choose the machine with `--machine`, or from the **Machine > Machine Type**
menu. Switching resets the machine.

| `--machine` | Machine | Notes |
|-------------|---------|-------|
| `48k` | ZX Spectrum 48K | The original rubber-key Spectrum |
| `128k` | ZX Spectrum 128K | AY sound and memory paging |
| `plus3` | ZX Spectrum +2A/+3 | Extended paging |
| `next` | ZX Spectrum Next | The default. Full Next hardware |

Even a 48K session needs an SD-card image, because that is where the 48K BASIC
ROM lives.

Chapter 5, *Running programs*, explains what actually changes between them.

## Getting help and reporting bugs

Bugs, questions and feature requests all go to the issue tracker:

**<https://github.com/jorgegv/jnext/issues>**

When reporting a problem, include your JNEXT version (`jnext --version`), your
operating system, and — if a specific program misbehaves — which program and
what you expected to see instead.

JNEXT is free software, licensed under the GNU General Public License version 3
or later.
