# JNEXT — Usage

Everything JNEXT can do from the command line and from the GUI. For building
it, see [BUILD.md](BUILD.md).

The examples use the Qt6 GUI build (`build/gui-release/jnext`); the SDL-only
build (`build/release/jnext`) takes the same options minus the GUI and
debugger ones.

---

## Table of contents

- [Quick start](#quick-start)
- [Machines](#machines)
- [SD card and ROMs](#sd-card-and-roms)
- [Loading programs](#loading-programs)
- [Command-line reference](#command-line-reference)
- [Headless mode, screenshots and recording](#headless-mode-screenshots-and-recording)
- [Logging](#logging)
- [Profiling](#profiling)
- [The GUI](#the-gui)
- [Keyboard mapping](#keyboard-mapping)
- [The debugger](#the-debugger)
- [Magic breakpoint and magic port](#magic-breakpoint-and-magic-port)

---

## Quick start

```sh
# Boot NextZXOS / TBBlue firmware (default machine: Next)
./build/gui-release/jnext

# Same, with an explicit SD-card image
./build/gui-release/jnext --sdcard roms/nextzxos-1gb-fat32fix.img

# Run a program: a bare filename is shorthand for --load
./build/gui-release/jnext game.nex
./build/gui-release/jnext game.tap          # auto-types LOAD ""
./build/gui-release/jnext game.sna

# Run as a plain ZX Spectrum 48K (48K BASIC ROM comes from the SD image)
./build/gui-release/jnext --machine 48k

# Double speed
./build/gui-release/jnext --speed 200

# Inject a raw binary at 0x8000 and run it
./build/gui-release/jnext --inject program.bin --inject-org 8000

# Headless screenshot, for CI
./build/gui-release/jnext --headless --machine 48k \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

## Machines

`--machine TYPE` (default: `next`):

| Type    | Machine                    | Notes                                      |
|---------|----------------------------|--------------------------------------------|
| `48k`   | ZX Spectrum 48K            | Original rubber-key Spectrum               |
| `128k`  | ZX Spectrum 128K           | AY sound and memory paging                 |
| `plus3` | ZX Spectrum +2A/+3         | Extended paging                            |
| `next`  | ZX Spectrum Next (Issue 2) | Full Next hardware and extended features   |

The machine type can also be switched at runtime from the GUI
(**Machine > Machine Type**); switching resets the machine.

## SD card and ROMs

JNEXT takes its ROMs from an SD-card image, exactly like real ZX Spectrum Next
hardware. There are two parts to it:

- **The FPGA boot ROM** (`nextboot.rom`, 8 KB) is *silicon-baked*: embedded in
  the jnext binary at link time. No flag, no lookup — it mirrors the on-FPGA
  flash IPL of the real machine.
- **Everything else** — 48K / 128K / +3 BASIC, NextZXOS, DivMMC firmware,
  Multiface firmware — is read from the SD-card image at the canonical TBBlue
  paths under `/MACHINES/NEXT/` (`48.rom`, `128.rom`, `plus3.rom`,
  `enNxtmmc.rom`, `enNextMf.rom`).

So even a `--machine 48k` run needs an SD-card image: that is where `48.rom`
lives.

Point at an image with `--sdcard FILE`, or give no `--sdcard` at all: JNEXT
then falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (a
FAT32-patched image), offering to download the canonical distribution image
(kept alongside it as `cspect-next-1gb.img`) and produce that patched copy on
first run. `--sdcard-download-confirm` skips the prompt (useful in scripts);
`--sdcard-download-force` re-downloads and re-patches a corrupted cached image.
An explicit `--sdcard` always wins over both.

The repository's canonical NextZXOS test image is
`roms/nextzxos-1gb-fat32fix.img` — the FAT32-corrected variant of
`roms/nextzxos-1gb.img`, whose 32 KB clusters leave it below the FAT32 minimum
cluster count, which the TBBlue firmware's FatFs (correctly) refuses to mount.
Use the `-fat32fix` one.

The mounted image is also the SD card the emulated machine sees at runtime,
through the SPI/DivMMC path — NextZXOS reads its files from it.

## Loading programs

`--load FILE`, or simply a bare filename (`jnext game.tap`) — the two are
equivalent, and giving both is an error. The format is detected from the
extension:

| Format | Notes                                                                   |
|--------|-------------------------------------------------------------------------|
| `.nex` | v1.0/1.1/1.2; pages, Layer 2 screen and palette from the header          |
| `.sna` | 48K and 128K snapshots                                                   |
| `.szx` | Chunked snapshots, zlib-compressed pages                                 |
| `.tap` | Fast load via ROM trap (or real-time with `--tape-realtime`); `LOAD ""` is auto-typed |
| `.tzx` | Full block support; fast load or real-time                              |
| `.wav` | RIFF/PCM EAR-bit playback (8/16-bit, mono/stereo)                        |

RZX recordings are not loaded with `--load`: use `--rzx-play FILE` (playback)
and `--rzx-record FILE` (recording). The GUI's **File > Load NEX File...**
dialog does accept `.rzx` as well as all of the above.

`--esxdos-stub` handles the common `RST $08` calls used by directly loaded NEX
programs. It provides one in-memory file and `run sibling.nex` chaining.

Raw binaries can be injected straight into RAM with `--inject FILE`
(`--inject-org` load address, `--inject-pc` entry point, `--inject-delay` a
frame delay before injecting — use ~100 frames if the binary calls ROM
routines that need the system variables set up first).

## Command-line reference

```
jnext [options] [file]
```

`[file]` is a program to load — equivalent to `--load FILE`. Anything starting
with `-` is treated as an option, never as a filename.

### Machine and program

| Option | Description |
|--------|-------------|
| `--machine TYPE` | `48k`, `128k`, `plus3`, `next` (default) |
| `--load FILE` | Load a program (`.nex`, `.sna`, `.szx`, `.tap`, `.tzx`, `.wav`; format from the extension) |
| `--sdcard FILE` | Mount SD-card image `FILE` (`.img`). Optional — see [SD card and ROMs](#sd-card-and-roms) |
| `--sdcard-download-confirm` | Skip the download prompt for the default-location image and proceed |
| `--sdcard-download-force` | Force re-download + re-patch of the default-location image (ignored when `--sdcard` is given) |
| `--speed PERCENT` | Emulator throttle: 50 = half, 100 = normal, 200 = 2×, 400 = 4× (clamped to 10..1000) |
| `--tape-realtime` | Real-time tape loading instead of fast load |
| `--esxdos-stub` | Provide basic `RST $08` file I/O and sibling-NEX chaining |
| `--rtc "YYYY-MM-DD HH:MM:SS"` | Pin the RTC to a fixed date/time (frozen clock) instead of following the host clock. The ISO `YYYY-MM-DDTHH:MM:SS` form is also accepted |
| `--inject FILE` | Load a raw binary into RAM |
| `--inject-org ADDR` | Load address for `--inject` (hex, default `8000`) |
| `--inject-pc ADDR` | Entry point for `--inject` (hex, default: same as `--inject-org`) |
| `--inject-delay N` | Wait N frames before injecting (default 0) |

### Recording and playback

| Option | Description |
|--------|-------------|
| `--record FILE` | Record video + audio to an MP4 (requires `ffmpeg` on the PATH) |
| `--rzx-play FILE` | Play back an RZX recording |
| `--rzx-record FILE` | Record input to an RZX file |
| `--rewind-buffer-size N` | Frame-snapshot ring buffer for backwards execution (opt-in; default 0 = off) |
| `--trace` | Enable the per-instruction trace log (also enabled implicitly by `--rewind-buffer-size N`, and toggleable in the debugger) |

### Headless and automation

| Option | Description |
|--------|-------------|
| `--headless` | Run with no display and no audio, at maximum speed |
| `--benchmark N` | Headless-only: run exactly N frames uncapped, print one machine-parseable `BENCH` line (wall s, fps, T-states/s, T-states/frame, CPU speed, host core, build type) plus a human summary to stdout, then exit. Used by `make bench` (`test/bench/bench.sh`) |
| `--benchmark-label NAME` | Workload label printed verbatim in the `BENCH` line (default: loaded file's basename, or `boot-<machine>`). No whitespace |
| `--delayed-screenshot FILE` | Save a PNG screenshot after a delay |
| `--delayed-screenshot-time N` | Delay in seconds (default 10) |
| `--delayed-screenshot-frames N` | Delay in frames (overrides `--delayed-screenshot-time`) |
| `--delayed-screenshot-layers LIST` | Layers to compose into the screenshot: comma-separated `ula`, `layer2`, `sprites`, `tiles`, `all` (default `all`) |
| `--delayed-automatic-exit N` | Exit the emulator after N seconds |
| `--delayed-automatic-exit-frames N` | Exit after N frames (overrides `--delayed-automatic-exit`) |
| `--delayed-keypress SECS KEY` | Press KEY after SECS seconds (repeatable) |
| `--delayed-keypress-frames N KEY` | Press KEY after N emulated frames |
| `--compositor-trace FILE` | Dump a per-pixel compositor trace (CSV) for one frame |
| `--compositor-trace-frame N` | Target frame for `--compositor-trace` (default 250) |

`KEY` is a single character, or the symbolic `ENTER` / `RETURN` / `SPACE`
(case-insensitive).

### Debugging

| Option | Description |
|--------|-------------|
| `--magic-breakpoint` | Enable the magic-breakpoint opcodes (`ED FF` / `DD 01`) |
| `--magic-port PORT` | Enable the magic debug port at PORT (hex, e.g. `0x00FF`) |
| `--magic-port-mode MODE` | Magic-port output mode: `hex` (default), `dec`, `ascii`, `line` |
| `--profile` | Enable the CPU T-state profiler |
| `--profile-output FILE` | Output path for `--profile` (default `profile.dat`) |
| `--log-level SPEC` | Set log levels — see [Logging](#logging) |

### Misc

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Print the built-in help and exit |
| `--version`, `-V` | Print the version and exit |

## Headless mode, screenshots and recording

`--headless` runs the emulator with no window and no audio device, as fast as
the host allows: the mode built for scripting and CI.

```sh
./build/gui-release/jnext --headless --machine 48k \
    --sdcard roms/nextzxos-1gb-fat32fix.img \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-frames 200 --delayed-automatic-exit 5
```

Notes worth knowing:

- **`--delayed-screenshot-frames` is the deterministic one.** Frame counts are
  reproducible; wall-clock seconds are not.
  `--delayed-automatic-exit-frames` is the same idea for the exit bound, and
  wins over `--delayed-automatic-exit` when both are given.
- **`--rtc` makes boot screenshots reproducible** by freezing the clock, so
  the NextZXOS date/time on screen never changes between runs.
- **A screenshot that was asked for and never taken is an error.** If
  `--delayed-automatic-exit` fires before the capture comes due, jnext logs an
  error and exits **non-zero** instead of silently writing nothing.
- **`--delayed-screenshot-layers` isolates a layer.** An excluded layer is
  composed as if its hardware enable bit were clear, so the remaining ones
  still follow the NR 0x15 priority order and the NR 0x4A fallback colour
  shows through where everything is transparent. Excluding `ula` also removes
  the border — the ULA is what draws it.

  ```sh
  # Capture Layer 2 on its own; then ULA + sprites together
  jnext --headless game.nex --delayed-screenshot l2.png  --delayed-screenshot-layers layer2
  jnext --headless game.nex --delayed-screenshot us.png  --delayed-screenshot-layers ula,sprites
  ```

- **Video recording** (`--record FILE`, or **File > Record MPEG4 Video**) pipes
  video and audio to `ffmpeg`, which must be installed.

## Logging

`--log-level SPEC` sets per-subsystem log levels. A **bare level** sets every
subsystem; a **`name=level`** pair sets one; they can be mixed, and are applied
left to right:

```sh
jnext --log-level warn                      # everything at warn
jnext --log-level cpu=trace                 # just the CPU
jnext --log-level warn,emulator=debug       # quiet, except the emulator
```

Levels: `trace`, `debug`, `info` (default), `warn`, `err`, `critical`, `off`
(`warning`/`error`/`fatal`/`none` are accepted as aliases).

Subsystems: `cpu`, `memory`, `ula`, `video`, `audio`, `port`, `nextreg`, `dma`,
`copper`, `uart`, `input`, `platform`, `emulator`, `sdcard`, `divmmc`, `spi`,
`ctc`, `i2c`, `multiface`.

## Profiling

`--profile` enables the CPU T-state profiler: one histogram entry per executed
instruction, written to `--profile-output` (default `profile.dat`) on exit.
Join it against a z88dk `.map` file to get a per-function heatmap:

```sh
jnext --headless game.nex --profile --profile-output game.dat --delayed-automatic-exit 20
tools/get-function-heatmap.pl -m game.map game.dat
```

## The GUI

The Qt6 build gives a native window with menus, a toolbar and a status bar
(FPS, CPU speed, machine type, tape status, rewind state), Hi-DPI
pixel-perfect rendering at integer scale, and a CRT scanline filter.

| Menu | Contents |
|------|----------|
| **File** | Load a program (Ctrl+O — NEX/SNA/SZX/TAP/TZX/WAV/RZX), Mount SD Card Image, Record MPEG4 Video (Ctrl+F5) / Stop (Ctrl+F6), Play RZX / Record RZX / Stop RZX, Save Screenshot (Ctrl+S), Save Snapshot (Ctrl+Shift+S), Quit (Ctrl+Q) |
| **Machine** | Reset (Ctrl+R), Machine Type (48K / 128K / +3 / Next), **CPU Speed** (3.5 / 7 / 14 / 28 MHz — the Next's own clock), **Emulator Speed** (0.5× / 1× / 2× / 4× / custom % — the host-side throttle) |
| **Tape** | Open Tape File (Ctrl+T), Eject, Rewind, Fast Load (toggle) |
| **Debug** | Magic Breakpoint (toggle) |
| **View** | Scale 2× / 3× / 4×, Fullscreen (F11, letterboxed), CRT Filter, Debugger (Ctrl+D) |
| **Help** | About |

The toolbar has Reset, Load, Screenshot and an **NMI** button (Multiface NMI).

CPU Speed and Emulator Speed are different things: the first changes the clock
the emulated Z80 runs at (a real Next feature), the second changes how fast the
emulator runs relative to real time.

## Keyboard mapping

| PC key              | Spectrum key             |
|---------------------|--------------------------|
| Letter/number keys  | The corresponding key    |
| Ctrl (left/right)   | Caps Shift               |
| Shift (left/right)  | Symbol Shift             |
| Backspace           | Delete (Caps Shift + 0)  |
| Arrow keys          | Cursor keys (Caps Shift + 5/6/7/8) |
| Enter               | Enter                    |
| Space               | Space                    |

Up to two USB gamepads are picked up automatically (hot-plug) and mapped to the
Next's MD6 joystick ports; the joystick mode (Kempston / Sinclair / Cursor /
MD) follows NextREG 0x05, as on real hardware.

## The debugger

The debugger opens in its own window (**View > Debugger**, Ctrl+D). It is
driven from the Qt6 UI, so it needs a GUI build (`make gui-release` /
`make gui-debug`) — the SDL-only build has no way to open it.

- **CPU registers** — all Z80/Z80N registers, flags (S/Z/H/P/V/N/C), halt
  state, interrupt mode, active ULA screen
- **MMU panel** — the Next 8-slot MMU table (page numbers and type) and the
  128K bank mappings
- **Disassembly** — Z80 + Z80N, PC highlight, breakpoint gutter, follow-PC,
  run-to-cursor, symbol names from MAP files
- **Memory hex editor** — full 64K, hex + ASCII, inline editing, page/bank
  selector
- **Stack** — SP-relative word view, SP row highlighted
- **Call stack** — CALL/RST/INT/RET tracking, with symbol resolution
- **Breakpoints** — execution, read, write and I/O watchpoints, in one panel
- **Watch expressions** — byte, word or long at arbitrary addresses, with
  custom labels
- **Video panels** — All layers (the real composite, through the live
  compositor), ULA (primary + shadow), Layer 2 (active + shadow), Sprites,
  Tilemap, Background (the NR 0x4A fallback colour); per-scanline view up to
  the current raster position, checkerboard for transparent pixels
- **Sprite viewer** — all 128 hardware sprites with their attribute table
- **Copper disassembly** — decoded WAIT/MOVE, with the current Copper PC
- **NextREG panel** — all 256 registers, named, editable inline
- **Audio panel** — AY register state for the 3 TurboSound chips, per-source
  mute
- **Trace log** — circular instruction trace buffer, exportable to a file.
  Off by default; enable with `--trace`, the debugger's Enable Trace menu
  item, or implicitly by enabling rewind (rewind needs it for Step Back)
- **Symbol table** — Z88DK MAP files; symbols appear inline in the disassembly
  and in watches
- **Backwards execution (rewind)** — frame-snapshot ring buffer, with Step
  Back, Frame Back and a rewind slider. Opt-in (default off, because rewind
  takes a full-machine snapshot every frame): start jnext with
  `--rewind-buffer-size N` (e.g. 500), or toggle it live from the debugger's
  Debug > Rewind > Enable Rewind menu (no restart needed; allocates 500
  frames, or the last size set via Rewind Buffer Size...). Unchecking the
  toggle pauses snapshotting but keeps the recorded history; set the buffer
  size to 0 to free the memory

| Key | Action |
|-----|--------|
| F5 | Run / Continue |
| F6 | Step Into |
| F7 | Step Over |
| F8 | Step Out |
| F9 | Pause / Break |
| Shift+F6 | Frame Back |
| Shift+F7 | Step Back |

## Magic breakpoint and magic port

- **Magic breakpoint** — the `ED FF` (ZEsarUX) and `DD 01` (CSpect) opcodes
  pause the debugger when enabled, and act as a NOP otherwise. Enable with
  `--magic-breakpoint` or **Debug > Magic Breakpoint**.
- **Magic debug port** — writes to a configurable port are logged to stderr as
  hex, decimal, ASCII or line-buffered text. Enable with
  `--magic-port PORT --magic-port-mode MODE`.
