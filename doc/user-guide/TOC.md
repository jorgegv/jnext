# JNEXT User Guide — proposed table of contents

**Status: TOC agreed 2026-07-20. Contents not written yet.**

This file is the agreed skeleton for the user manual ([issue
#28](https://github.com/jorgegv/jnext/issues/28)). Nothing under
`doc/user-guide/` is built or published yet: there is deliberately no
`mkdocs.yml` and there are no stub pages, so `BUILD.md`'s statement that the
manual does not exist stays true until the chapters are actually written.

The guide is **task-oriented** — how to do a thing — not a flag reference.
The complete option reference is `jnext(1)` / `USAGE.md`, which are generated
from a single source; the Reference chapter links there rather than restating
options, so the manual can never drift from the binary.

Toolchain: [mkdocs-material](https://squidfunk.github.io/mkdocs-material/),
YAML + Markdown. Documentation-only dev requirement, never invoked by a code
build.

---

## 1. Introduction

What JNEXT is, what it emulates, and how faithful it aims to be (the VHDL core
as the authoritative spec). Machine coverage at a glance. Where to get help and
report bugs.

## 2. Installing

Packages first (rpm, deb, Flatpak, Windows zip, macOS dmg) — the recommended
route for users. Building from source is covered by `BUILD.md`, linked, not
duplicated.

## 3. First run

Launching, the SD-card image and where it comes from (self-provisioning, the
`~/.jnext/sdcard` fallback), the first NextZXOS boot, and what a working
first screen looks like.

## 4. Configuration

Preferences, `~/.jnext/jnext.conf`, and the precedence rule that CLI flags
always win over saved settings. Placed before "Running programs" so a reader
sets the machine up once, then uses it.

## 5. Running programs

The core chapter. Loading and running software, with everything that shapes a
session as subsections.

- **5.1 Choosing a machine** — 48K / 128K / +3 / Pentagon / Next, and what
  changes with each (timing, contention, available hardware).
- **5.2 Input** — keyboard mapping and the extended keys, joysticks and
  gamepads, the Kempston mouse and pointer capture.
- **5.3 Display** — scaling, fullscreen, the CRT filter, layers, and
  screenshots.
- **5.4 Sound** — AY / TurboSound, the beeper, DAC, volume and stereo, and
  what to do about audio glitches.
- **5.5 Recording and playback** — video recording, RZX record/replay, and
  tape (TAP/TZX/WAV) real-time versus fast loading.

## 6. The debugger

One chapter, not a separate manual — with a subsection per panel and per
function, so it can be used as a reference while debugging.

**Panels** (one subsection each, matching `src/debugger/`):

- CPU registers
- MMU
- Disassembly
- Memory
- Stack
- Call stack
- Watches
- Breakpoints
- Video (with its ULA / Layer 2 / Sprites / Tilemap views)
- Sprites
- Copper
- NextREG
- Audio

**Functions** (one subsection each):

- Run, pause, and the step family (into / over / out)
- Run to cursor, run to end of frame, run to end of scanline
- Breakpoints: execute, read, write, I/O
- Watches, and watch sizes (byte / word / long)
- Symbols from Z88DK MAP files
- The trace log
- Backward execution (rewind)
- The magic breakpoint and the magic port

## 7. Automation and CI

Kept deliberately, and justified: the same machinery that automates JNEXT's own
test suite automates testing of *your* programs. Headless mode, deterministic
screenshots (by seconds, by frames), fixing the RTC, key injection, exit codes
and the never-silently-missed-capture contract, and wiring all of it into a
build or CI pipeline.

## 8. Reference

Pointers, not restatements: `jnext(1)` for the full option reference, file and
directory locations, and links to the ZX Spectrum Next hardware documentation.

---

## Open questions for a later pass

- Whether chapter 6 outgrows the guide and wants to become its own document
  once every panel is written up.
- Whether chapter 7 should carry a complete worked example (a small program
  plus the CI job that screenshot-tests it) or stay a description of the
  mechanisms.
