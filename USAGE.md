<!-- GENERATED FILE - DO NOT EDIT. -->
<!-- USAGE.md is rendered from doc/man/jnext.1.md, the single source it -->
<!-- shares with the jnext(1) man page. Edit that, then run `make docs`. -->

# JNEXT — Usage

This is the markdown rendering of the **jnext(1)** man page: same source, so
the two can never disagree. For building jnext from source, see
[BUILD.md](BUILD.md).

---

## Table of contents

- [NAME](#name)
- [SYNOPSIS](#synopsis)
- [DESCRIPTION](#description)
- [OPTIONS](#options)
  - [Machine and program](#machine-and-program)
  - [Recording and playback](#recording-and-playback)
  - [Headless and automation](#headless-and-automation)
  - [Debugging](#debugging)
  - [Misc](#misc)
- [MACHINES](#machines)
- [SD CARD AND ROMS](#sd-card-and-roms)
- [LOADING PROGRAMS](#loading-programs)
- [HEADLESS MODE, SCREENSHOTS AND
  RECORDING](#headless-mode-screenshots-and-recording)
- [LOGGING](#logging)
- [PROFILING](#profiling)
- [THE GUI](#the-gui)
- [KEYBOARD MAPPING](#keyboard-mapping)
- [THE DEBUGGER](#the-debugger)
- [MAGIC BREAKPOINT AND MAGIC PORT](#magic-breakpoint-and-magic-port)
- [EXAMPLES](#examples)
- [FILES](#files)
- [EXIT STATUS](#exit-status)
- [SEE ALSO](#see-also)
- [BUGS](#bugs)
- [COPYRIGHT](#copyright)

---

<!-- SINGLE SOURCE for the jnext CLI/usage reference. It generates BOTH -->

<!-- doc/man/jnext.1 (roff, installed to $mandir/man1) and USAGE.md -->

<!-- (repo-root markdown rendering). Do not edit either output by hand: -->

<!-- edit this file and run `make docs` (needs pandoc; see BUILD.md). -->

<!-- Both outputs are committed, so building from source never needs the -->

<!-- doc toolchain. No version or date is embedded on purpose - it would -->

<!-- go stale every release and drag this file into sync-version.sh; -->

<!-- users get the version from `jnext --version`. -->

<!-- One comment per line on purpose: a blank line inside a single HTML -->

<!-- comment block comes out as a literal newline entity in USAGE.md. -->

## NAME

jnext - real-time ZX Spectrum Next emulator with an integrated debugger

## SYNOPSIS

**jnext** \[*options*\] \[*file*\]

## DESCRIPTION

**jnext** is a real-time, cross-platform ZX Spectrum Next emulator
written in C++17 and derived from the official VHDL sources of the ZX
Spectrum Next FPGA core. It emulates the Next itself as well as the ZX
Spectrum 48K, 128K and +2A/+3 machines, and carries a debugger that
shows CPU, memory, video layers, sprites, audio and NextREG state live.

*file* is a program to load, exactly equivalent to **--load** *FILE*, so
`jnext game.tap` just works. Anything starting with `-` is treated as an
option, never as a filename; giving both a bare filename and **--load**
is an error.

The examples below use the Qt6 GUI build (`build/gui-release/jnext`).
The SDL-only build (`build/sdl-release/jnext`) takes the same options
minus the GUI and debugger ones.

## OPTIONS

### Machine and program

**--machine** *TYPE*  
Machine type: `48k`, `128k`, `plus3`, `next` (default).

**--load** *FILE*  
Load a program. The format is detected from the extension: `.nex`,
`.sna`, `.szx`, `.z80`, `.tap`, `.tzx`, `.wav`, `.rzx`. (`.rzx` is
accepted here and plays back, as **--rzx-play**.)

**--sdcard** *FILE*  
Mount SD-card image *FILE* (`.img`). Optional; see **SD CARD AND ROMS**.

**--sdcard-download-confirm**  
Skip the download prompt for the default-location image and proceed.

**--sdcard-download-force**  
Force re-download and re-patch of the default-location image, to recover
a corrupted one. Ignored when **--sdcard** is given: an explicit path
always wins.

**--sdcard-readonly**  
Open the SD image read-only, so the host file is never modified. The
emulated machine sees a write-protected card: writes are rejected with
the SD write-error token rather than silently discarded. Use it when a
run must not disturb an image other runs share.

**--speed** *PERCENT*  
Emulator throttle: 50 = half, 100 = normal, 200 = 2x, 400 = 4x. Clamped
to 10..1000.

**--joy1-source** *SRC*  
Host source for Joy 1 (port 0x1F): `sdl` (autodetected gamepad, default)
or `keys` (host arrow keys, Space = fire).

**--joy2-source** *SRC*  
Host source for Joy 2 (port 0x37): `sdl` (default) or `keys`. Only one
connector may use `keys`. Interactive (SDL/Qt) frontends only; inert
under **--headless**.

**--tape-realtime**  
Real-time tape loading, at the speed of an actual tape, instead of fast
load.

**--tape-save** *FILE*  
Append blocks SAVEd through the 48K ROM SA-BYTES routine to *FILE*
(`.tap`). Trap-based: it fires when the ROM save routine at 0x04C2 runs
with ROM paged at slot 0. Without this option no SAVE capture happens.

**--esxdos-stub**  
Intercept `RST $08` calls and provide in-memory config I/O plus `.RUN`
sibling-NEX chaining, without booting NextZXOS.

**--rtc** *“YYYY-MM-DD HH:MM:SS”*  
Pin the RTC to a fixed date and time (a frozen clock) instead of
following the host clock, which makes boot screenshots deterministic.
The ISO `YYYY-MM-DDTHH:MM:SS` form is also accepted.

**--silent**  
Disable all sound output (beeper, the three AY/YM chips, DAC/Covox/
Specdrum). No audio device is opened and the emulator skips PSG and
mixer sample synthesis entirely, which can measurably speed up CPU-bound
runs. Tape loading (EAR input) is unaffected.

**--inject** *FILE*  
Load a raw binary into RAM.

**--inject-org** *ADDR*  
Load address for **--inject** (hex, default `8000`).

**--inject-pc** *ADDR*  
Entry point for **--inject** (hex, default: same as **--inject-org**).

**--inject-delay** *N*  
Wait *N* frames before injecting (default 0). Use around 100 if the
binary calls ROM routines that need the system variables set up first.

### Recording and playback

**--record** *FILE*  
Record video and audio to an MP4. Requires **ffmpeg**(1) on the PATH.

**--wav-record** *FILE*  
Record the mixed stereo output to a 44.1 kHz, 16-bit PCM WAV. Works
headless and requires neither an audio device nor ffmpeg.

**--dac-trace** *FILE*  
Record `segment,tstate,channel,value` rows for physical DAC writes. A
cold boot starts a new segment.

**--rzx-play** *FILE*  
Play back an RZX recording.

**--rzx-record** *FILE*  
Record input to an RZX file.

**--rewind-buffer-size** *N*  
Frame-snapshot ring buffer for backwards execution. Opt-in; default 0 =
off.

**--trace**  
Enable the per-instruction trace log (a 10K-entry ring). Also enabled
implicitly by **--rewind-buffer-size** *N* with *N* \> 0, and toggleable
in the debugger.

### Headless and automation

**--headless**  
Run with no display and no audio device, at maximum speed.

**--benchmark** *N*  
Headless only. Run exactly *N* frames uncapped, print one
machine-parseable `BENCH` line (wall seconds, fps, T-states/s,
T-states/frame, CPU speed, host core, build type) plus a human summary
to stdout, then exit. Used by `make bench` (`test/bench/bench.sh`).

**--benchmark-label** *NAME*  
Workload label printed verbatim in the `BENCH` line (default: the loaded
file’s basename, or `boot-<machine>`). No whitespace, since the `BENCH`
line is space-delimited.

**--delayed-screenshot** *FILE*  
Save a PNG screenshot after a delay.

**--delayed-screenshot-time** *N*  
Delay in seconds (default 10).

**--delayed-screenshot-frames** *N*  
Delay in frames. Overrides **--delayed-screenshot-time**.

**--delayed-screenshot-layers** *LIST*  
Layers to compose into the screenshot: a comma-separated list of `ula`,
`layer2`, `sprites`, `tiles`, `all` (default `all`).

**--delayed-automatic-exit** *N*  
Exit the emulator after *N* seconds.

**--delayed-automatic-exit-frames** *N*  
Exit after *N* frames. Overrides **--delayed-automatic-exit**.

**--delayed-snapshot** *FILE*  
Headless only. Save a snapshot after a delay in frames. The format is
chosen by the extension of *FILE*: `.szx`, `.nex`, anything else `.sna`.

**--delayed-snapshot-frames** *N*  
Delay in frames for **--delayed-snapshot** (default 0).

**--delayed-keypress** *SECS* *KEY*  
Press *KEY* after *SECS* seconds. Headless only, repeatable.

**--delayed-keypress-frames** *N* *KEY*  
Press *KEY* after *N* emulated frames. Overrides the seconds form.

**--compositor-trace** *FILE*  
Dump a per-pixel compositor trace (CSV) for one frame.

**--compositor-trace-frame** *N*  
Target frame for **--compositor-trace** (default 250).

*KEY* is case-insensitive and is one of: a single character (`a`-`z`,
`0`-`9`, `.`, `,`, `;`, `:`), one of the symbolic names `ENTER`,
`RETURN`, `SPACE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, or a compound
`sym+`*char* / `caps+`*char* (for example `sym+m` is `.`).

### Debugging

**--magic-breakpoint**  
Enable the magic-breakpoint opcodes (`ED FF` and `DD 01`).

**--magic-port** *PORT*  
Enable the magic debug port at *PORT* (hex, for example `0x00FF`).

**--magic-port-mode** *MODE*  
Magic-port output mode: `hex` (default), `dec`, `ascii`, `line`.

**--profile**  
Enable the CPU T-state profiler. It allocates an mmap’d histogram and
accumulates one entry per executed instruction; on exit the histogram is
written to **--profile-output**.

**--profile-output** *FILE*  
Output path for **--profile** (default `profile.dat`).

**--log-level** *SPEC*  
Set per-subsystem log levels; see **LOGGING**.

### Misc

**--help**, **-h**  
Print the built-in help and exit.

**--version**, **-V**  
Print the version and exit.

## MACHINES

`48k`  
ZX Spectrum 48K - the original rubber-key Spectrum.

`128k`  
ZX Spectrum 128K - AY sound and memory paging.

`plus3`  
ZX Spectrum +2A/+3 - extended paging.

`next`  
ZX Spectrum Next (Issue 2) - full Next hardware and extended features.

The machine type can also be switched at runtime from the GUI (**Machine
\> Machine Type**); switching resets the machine.

## SD CARD AND ROMS

jnext takes its ROMs from an SD-card image, exactly like real ZX
Spectrum Next hardware. There are two parts to it:

- **The FPGA boot ROM** (`nextboot.rom`, 8 KB) is *silicon-baked*:
  embedded in the jnext binary at link time. No flag, no lookup; it
  mirrors the on-FPGA flash IPL of the real machine.
- **Everything else** - 48K / 128K / +3 BASIC, NextZXOS, DivMMC
  firmware, Multiface firmware - is read from the SD-card image at the
  canonical TBBlue paths under `/MACHINES/NEXT/` (`48.rom`, `128.rom`,
  `plus3.rom`, `enNxtmmc.rom`, `enNextMf.rom`).

So even a **--machine** `48k` run needs an SD-card image: that is where
`48.rom` lives.

Point at an image with **--sdcard** *FILE*, or give no **--sdcard** at
all: jnext then falls back to
`~/.jnext/sdcard/cspect-next-1gb-fixed.img` (a FAT32-patched image),
offering to download the canonical distribution image (kept alongside it
as `cspect-next-1gb.img`) and produce that patched copy on first run.
**--sdcard-download-confirm** skips the prompt, which is useful in
scripts; **--sdcard-download-force** re-downloads and re-patches a
corrupted cached image. An explicit **--sdcard** always wins over both.

Note that jnext opens the image **read-write** by default, and writes
the emulated machine makes to the SD card are persisted to the file. Two
runs sharing one image are therefore not independent: a run that boots
NextZXOS mutates the image the next run reads. For a reproducible run,
either pass **--sdcard-readonly**, or give it a private copy
(`cp --reflink=auto` is instant and costs no space on a copy-on-write
filesystem).

The mounted image is also the SD card the emulated machine sees at
runtime, through the SPI/DivMMC path: NextZXOS reads its files from it.

## LOADING PROGRAMS

Use **--load** *FILE*, or simply a bare filename (`jnext game.tap`). The
two are equivalent, and giving both is an error. The format is detected
from the extension:

`.nex`  
v1.0/1.1/1.2; pages, Layer 2 screen and palette from the header.

`.sna`  
48K and 128K snapshots.

`.szx`  
Chunked snapshots, zlib-compressed pages.

`.z80`  
v1/v2/v3, 48K and 128K, RLE-compressed or raw pages.

`.tap`  
Fast load via a ROM trap; `LOAD ""` is auto-typed.

`.tzx`  
Full block support; fast load or real-time.

`.wav`  
RIFF/PCM EAR-bit playback (8/16-bit, mono/stereo).

`.rzx`  
Played back as if **--rzx-play** had been given.

`.tap` and `.tzx` load in real time instead with **--tape-realtime**.

RZX recording uses **--rzx-record** *FILE*. The GUI’s **File \> Load NEX
File…** dialog accepts all of the above.

**--esxdos-stub** handles the common `RST $08` calls used by directly
loaded NEX programs. It provides one in-memory file and
`run sibling.nex` chaining.

Raw binaries go straight into RAM with **--inject** *FILE*
(**--inject-org** load address, **--inject-pc** entry point,
**--inject-delay** a frame delay before injecting).

## HEADLESS MODE, SCREENSHOTS AND RECORDING

**--headless** runs the emulator with no window and no audio device, as
fast as the host allows: the mode built for scripting and CI.

Notes worth knowing:

- **--delayed-screenshot-frames** is the deterministic one. Frame counts
  are reproducible; wall-clock seconds are not.
  **--delayed-automatic-exit-frames** is the same idea for the exit
  bound, and wins over **--delayed-automatic-exit** when both are given.
- **--rtc** makes boot screenshots reproducible by freezing the clock,
  so the NextZXOS date and time on screen never change between runs.
- A screenshot that was asked for and never taken is an error. If
  **--delayed-automatic-exit** fires before the capture comes due, jnext
  logs an error and exits non-zero instead of silently writing nothing.
- **--delayed-screenshot-layers** isolates a layer. An excluded layer is
  composed as if its hardware enable bit were clear, so the remaining
  ones still follow the NR 0x15 priority order and the NR 0x4A fallback
  colour shows through where everything is transparent. Excluding `ula`
  also removes the border: the ULA is what draws it.
- Video recording (**--record** *FILE*, or **File \> Record MPEG4
  Video**) pipes video and audio to ffmpeg, which must be installed.
- WAV recording (**--wav-record** *FILE*) captures the mixed audio
  before the host playback device. It works in headless mode and
  continues through cold boots such as sibling-NEX chaining. It cannot
  be combined with **--silent**, which disables mixer synthesis. If the
  emulator exits abnormally, the file may retain its initial zero-length
  data header even though PCM follows it.
- DAC tracing (**--dac-trace** *FILE*) records guest DAC writes before
  mixing. Channels are numbered 0-3 (A-D) and timestamps are CPU
  T-states.

## LOGGING

**--log-level** *SPEC* sets per-subsystem log levels. A bare level sets
every subsystem; a *name*`=`*level* pair sets one. They can be mixed,
and are applied left to right:

    jnext --log-level warn                      # everything at warn
    jnext --log-level cpu=trace                 # just the CPU
    jnext --log-level warn,emulator=debug       # quiet, except the emulator

Levels are `trace`, `debug`, `info` (default), `warn`, `err`, `critical`
and `off`; `warning`, `error`, `fatal` and `none` are accepted as
aliases.

Subsystems are `cpu`, `memory`, `ula`, `video`, `audio`, `port`,
`nextreg`, `dma`, `copper`, `uart`, `input`, `platform`, `emulator`,
`sdcard`, `divmmc`, `spi`, `ctc`, `i2c` and `multiface`.

## PROFILING

**--profile** enables the CPU T-state profiler: one histogram entry per
executed instruction, written to **--profile-output** (default
`profile.dat`) on exit. Join it against a z88dk `.map` file to get a
per-function heatmap:

    jnext --headless game.nex --profile --profile-output game.dat \
        --delayed-automatic-exit 20
    tools/get-function-heatmap.pl -m game.map game.dat

## THE GUI

The Qt6 build gives a native window with menus, a toolbar and a status
bar (FPS, CPU speed, emulator speed, tape status, machine type), Hi-DPI
pixel-perfect rendering at integer scale, and a CRT scanline filter.

**File**  
Load a program (Ctrl+O - NEX/SNA/SZX/TAP/TZX/WAV/RZX), Mount SD Card
Image, Record MPEG4 Video (Ctrl+F5) / Stop (Ctrl+F6), Play RZX / Record
RZX / Stop RZX, Save Screenshot (Ctrl+S), Save Snapshot (Ctrl+Shift+S),
Quit (Ctrl+Q).

**Machine**  
Reset (Ctrl+R), Machine Type (48K / 128K / +3 / Next), CPU Speed (3.5 /
7 / 14 / 28 MHz - the Next’s own clock), Emulator Speed (0.5x / 1x / 2x
/ 4x / custom % - the host-side throttle).

**Input**  
Joy 1 Source (port 0x1F) and Joy 2 Source (port 0x37), each selecting
what drives that connector; Capture Mouse, which confines the host
pointer so the Kempston mouse can move freely (Ctrl+Alt releases it).

**Tape**  
Open Tape File (Ctrl+T), Eject, Rewind, Fast Load (toggle).

**Debug**  
Magic Breakpoint (toggle).

**View**  
Scale 1x / 2x / 3x, Fullscreen (F11, letterboxed), CRT Filter, Debugger
(Ctrl+D).

**Settings**  
Preferences…, which opens the settings dialog (Startup, Input and Paths
tabs). Its values are saved to the configuration file; command-line
options always take precedence over them.

**Help**  
About.

The toolbar has Reset, Load, Screenshot and an NMI button (Multiface
NMI).

CPU Speed and Emulator Speed are different things: the first changes the
clock the emulated Z80 runs at, which is a real Next feature; the second
changes how fast the emulator runs relative to real time.

## KEYBOARD MAPPING

| PC key                | Spectrum key                            |
|-----------------------|-----------------------------------------|
| Letter/number keys    | The corresponding key                   |
| Ctrl (left/right)     | Caps Shift                              |
| Shift (left/right)    | Symbol Shift                            |
| Backspace             | Delete (Caps Shift + 0)                 |
| Arrow keys            | Cursor keys (Caps Shift + 5/6/7/8)      |
| Enter                 | Enter                                   |
| Space                 | Space                                   |
| Esc                   | Break (Caps Shift + Space)              |
| Tab                   | Extend Mode (Caps Shift + Symbol Shift) |
| Key left of `1`       | True Video (Caps Shift + 3)             |
| Alt + key left of `1` | Inverse Video (Caps Shift + 4)          |
| Alt + E               | Edit (Caps Shift + 1)                   |
| Alt + G               | Graph (Caps Shift + 9)                  |
| Alt + C               | Caps Lock (Caps Shift + 2)              |
| `'`                   | `"` (Symbol Shift + P)                  |
| `;`                   | `;` (Symbol Shift + O)                  |
| `.`                   | `.` (Symbol Shift + M)                  |
| `,`                   | `,` (Symbol Shift + N)                  |

Esc is the Spectrum’s Break key, not a fullscreen shortcut. Fullscreen
is toggled with F11 only (it used to also be exited with Esc).

Alt is a modifier here, never a Spectrum key. Only Alt + E/G/C and Alt +
the key left of `1` are used, because the menu bar claims Alt +
F/M/I/T/D/V/S/H.

Up to two USB gamepads are picked up automatically (hot-plug) and mapped
to the Next’s two joystick ports; the joystick mode (Kempston / Sinclair
/ Cursor / MD) follows NextREG 0x05, as on real hardware.

Either joystick port can instead be driven by the host cursor keys
(arrows for direction, Space for fire), which is useful for the many
games that read the Kempston joystick when you have no gamepad. Choose
the source per connector in the **Input** menu, in **Settings \>
Preferences \> Input**, or with **--joy1-source** `keys` /
**--joy2-source** `keys`; the choice is remembered across sessions. Only
one connector may use the cursor keys at a time, and while it does, the
arrows and Space stop acting as ZX keys.

## THE DEBUGGER

The debugger opens in its own window (**View \> Debugger**, Ctrl+D). It
is driven from the Qt6 UI, so it needs a GUI build (`make gui-release`
or `make gui-debug`); the SDL-only build has no way to open it.

- **CPU registers** - all Z80/Z80N registers, flags (S/Z/H/P/V/N/C),
  halt state, interrupt mode, active ULA screen
- **MMU panel** - the Next 8-slot MMU table (page numbers and type) and
  the 128K bank mappings
- **Disassembly** - Z80 + Z80N, PC highlight, breakpoint gutter,
  follow-PC, run-to-cursor, symbol names from MAP files
- **Memory hex editor** - full 64K, hex and ASCII, inline editing,
  page/bank selector
- **Stack** - SP-relative word view, SP row highlighted
- **Call stack** - CALL/RST/INT/RET tracking, with symbol resolution
- **Breakpoints** - execution, read, write and I/O watchpoints, in one
  panel
- **Watch expressions** - byte, word or long at arbitrary addresses,
  with custom labels
- **Video panels** - All layers (the real composite, through the live
  compositor), ULA (primary and shadow), Layer 2 (active and shadow),
  Sprites, Tilemap, Background (the NR 0x4A fallback colour);
  per-scanline view up to the current raster position, checkerboard for
  transparent pixels
- **Sprite viewer** - all 128 hardware sprites with their attribute
  table
- **Copper disassembly** - decoded WAIT/MOVE, with the current Copper PC
- **NextREG panel** - all 256 registers, named, editable inline
- **Audio panel** - AY register state for the 3 TurboSound chips,
  per-source mute
- **Trace log** - circular instruction trace buffer, exportable to a
  file. Off by default; enable with **--trace**, the debugger’s Enable
  Trace menu item, or implicitly by enabling rewind, which needs it for
  Step Back
- **Symbol table** - Z88DK MAP files; symbols appear inline in the
  disassembly and in watches
- **Backwards execution (rewind)** - frame-snapshot ring buffer, with
  Step Back, Frame Back and a rewind slider. Opt-in, because rewind
  takes a full-machine snapshot every frame: start jnext with
  **--rewind-buffer-size** *N* (for example 500), or toggle it live from
  the debugger’s Debug \> Rewind \> Enable Rewind menu, with no restart
  needed - that allocates 500 frames, or the last size set via Rewind
  Buffer Size… Unchecking the toggle pauses snapshotting but keeps the
  recorded history; set the buffer size to 0 to free the memory

| Key      | Action         |
|----------|----------------|
| F5       | Run / Continue |
| F6       | Step Into      |
| F7       | Step Over      |
| F8       | Step Out       |
| F9       | Pause / Break  |
| Shift+F6 | Frame Back     |
| Shift+F7 | Step Back      |

## MAGIC BREAKPOINT AND MAGIC PORT

The magic breakpoint uses the `ED FF` (ZEsarUX) and `DD 01` (CSpect)
opcodes to pause the debugger when enabled; they act as a NOP otherwise.
Enable it with **--magic-breakpoint** or **Debug \> Magic Breakpoint**.

The magic debug port logs writes to a configurable port to stderr as
hex, decimal, ASCII or line-buffered text. Enable it with
**--magic-port** *PORT* and **--magic-port-mode** *MODE*.

## EXAMPLES

Boot NextZXOS / the TBBlue firmware (the default machine is `next`):

    jnext

The same, with an explicit SD-card image:

    jnext --sdcard ~/.jnext/sdcard/cspect-next-1gb-fixed.img

Run a program; a bare filename is shorthand for **--load**:

    jnext game.nex
    jnext game.tap          # auto-types LOAD ""
    jnext game.sna

Run as a plain ZX Spectrum 48K (the 48K BASIC ROM comes from the SD
image):

    jnext --machine 48k

Run at double speed:

    jnext --speed 200

Inject a raw binary at 0x8000 and run it:

    jnext --inject program.bin --inject-org 8000

Take a headless screenshot, for CI. Name the SD image explicitly: with
no **--sdcard** and no cached image, provisioning prompts on stdin and
declines at EOF, so an unattended run would exit non-zero instead of
capturing anything (**--sdcard-download-confirm** is the alternative).

    jnext --headless --machine 48k \
        --sdcard ~/.jnext/sdcard/cspect-next-1gb-fixed.img \
        --delayed-screenshot /tmp/test.png \
        --delayed-screenshot-frames 200 --delayed-automatic-exit 5

Capture Layer 2 on its own, then the ULA and sprites together:

    jnext --headless game.nex --delayed-screenshot l2.png \
        --delayed-screenshot-layers layer2
    jnext --headless game.nex --delayed-screenshot us.png \
        --delayed-screenshot-layers ula,sprites

## FILES

`~/.jnext/jnext.conf`  
GUI configuration, in INI format. Written by **Settings \>
Preferences**. CLI options always take precedence over saved values, and
headless runs never read it.

`~/.jnext/sdcard/cspect-next-1gb-fixed.img`  
The default SD-card image, used when **--sdcard** is not given.

`~/.jnext/sdcard/cspect-next-1gb.img`  
The canonical distribution image the patched one above is produced from.

## EXIT STATUS

**jnext** exits 0 on success and non-zero on error. In particular it
exits non-zero when a **--delayed-screenshot** was requested but never
taken, rather than silently writing nothing.

## SEE ALSO

**ffmpeg**(1)

The project documentation: `README.md` for an overview and `BUILD.md`
for building from source.

Project home page: <https://github.com/jorgegv/jnext>

## BUGS

Report bugs at <https://github.com/jorgegv/jnext/issues>.

## COPYRIGHT

Copyright (C) ZXjogv <zx@jogv.es>. Licensed under the GNU General Public
License version 3 or later.
