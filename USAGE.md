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
  - [Networking (ESP-01 WiFi)](#networking-esp-01-wifi)
  - [Recording and playback](#recording-and-playback)
  - [Headless and automation](#headless-and-automation)
  - [Debugging](#debugging)
  - [Misc](#misc)
- [MACHINES](#machines)
- [SD CARD AND ROMS](#sd-card-and-roms)
- [LOADING PROGRAMS](#loading-programs)
- [HEADLESS MODE, SCREENSHOTS AND
  RECORDING](#headless-mode-screenshots-and-recording)
- [NETWORKING (ESP-01 WiFi)](#networking-esp-01-wifi-1)
  - [It is off by default, and that is the
    point](#it-is-off-by-default-and-that-is-the-point)
  - [What is always refused, whatever you
    allow](#what-is-always-refused-whatever-you-allow)
  - [Narrowing it further](#narrowing-it-further)
  - [Making it permanent](#making-it-permanent)
  - [Letting a program listen (server
    mode)](#letting-a-program-listen-server-mode)
  - [Nothing about your host leaks into the
    program](#nothing-about-your-host-leaks-into-the-program)
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
- [AUTHORS](#authors)
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
option, never as a filename. Name the program one way or the other, not
both at once: `jnext --load game.tap game.tap` is rejected, and so is
naming two files.

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

**--nex-args** *LINE*  
Argument line for a NEX **V1.3** program. *LINE* is placed, verbatim and
zero-terminated, in the CLI buffer the file’s header declares (its
address and size), and `DE` points at it when the program starts. A line
as long as or longer than that buffer is truncated to the buffer’s size
with no terminator - which is what the reference V1.3 loader does. Quote
*LINE* to pass more than one word:
`jnext --load game.nex --nex-args "level 3"`. Only V1.3 declares a CLI
buffer, so the option is inert - with a warning - for a `V1.0`-`V1.2`
file, or a V1.3 file whose header declares no buffer.

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

### Networking (ESP-01 WiFi)

**--esp**  
Enable the emulated ESP-01 WiFi module on UART 0. **Off by default** -
see **NETWORKING** below for what turning it on gives the running
program.

**--no-esp**  
Force the ESP off for this run, overriding a saved GUI preference that
enables it. Given both, whichever comes last on the command line wins.

**--esp-allow** *HOST*  
Only let the running program connect to *HOST*. Repeatable: give the
option once per host. *HOST* is a single name - a comma is **rejected**
rather than taken as a separator or as part of the name, because a name
that silently matched nothing would look like a restriction that is in
force when it is not. Matching is exact and case-insensitive, with no
wildcards, and an IP address must be listed as itself. With no
**--esp-allow** at all the program may name any host (subject to the
always-refused addresses in **NETWORKING**). Requires **--esp**.

**--esp-listen-address** *ADDR*  
Bind the running program’s `AT+CIPSERVER` to *ADDR*, default
`127.0.0.1`. *ADDR* is a numeric IP address, never a name - an address
resolved through DNS could change under you. The default means a program
that opens a server is reachable only from this machine; widening it
(`0.0.0.0`) exposes that program to your network, which is why it has to
be asked for. Nothing listens until the program itself sends
`AT+CIPSERVER`. Requires **--esp**.

### Recording and playback

**--record** *FILE*  
Record video and audio to an MP4. Requires **ffmpeg**(1) on the PATH. If
the recording cannot be produced (encoder failure, no usable output),
**jnext** reports the error and exits non-zero. If the recording cannot
even be started (no usable **ffmpeg**), **jnext** exits non-zero
immediately, without running the emulation.

**--wav-record** *FILE*  
Record the mixed stereo output to a 44.1 kHz, 16-bit PCM WAV. Works
headless and requires neither an audio device nor ffmpeg.

**--dac-trace** *FILE*  
Record `segment,tstate,channel,value` rows for physical DAC writes. A
cold boot starts a new segment.

**--audio-gain-db** *DB*  
Set master host audio gain from -24 dB to +24 dB (default 0 dB). The
gain is applied after the emulated hardware mix; overflow saturates at
the 16-bit PCM limits.

**--audio-gain-beeper-db** *DB*  
Set host gain for beeper EAR/MIC/tape-EAR audio from -24 dB to +24 dB.

**--audio-gain-ay0-db** *DB*  
Set host gain for TurboSound AY chip 0 from -24 dB to +24 dB.

**--audio-gain-ay1-db** *DB*  
Set host gain for TurboSound AY chip 1 from -24 dB to +24 dB.

**--audio-gain-ay2-db** *DB*  
Set host gain for TurboSound AY chip 2 from -24 dB to +24 dB.

**--audio-gain-dac-db** *DB*  
Set host gain for Specdrum, Soundrive and Covox DAC audio from -24 dB to
+24 dB.

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
Press *KEY* after *N* emulated frames. This is the frames-unit spelling
of **--delayed-keypress**, not an override of it: both forms queue into
the same list, so giving both schedules two keypresses.

**--delayed-nmi** *SECS* *BUTTON*  
Press an NMI *BUTTON* after *SECS* seconds. Headless only, repeatable.
*BUTTON* is case-insensitive and names which button to press, spelled as
the label on a real Next’s case. Of its three buttons, two raise an NMI:
`nmi` (aliases `mf`, `m1`) is the **NMI** button, wired to the
Multiface; `drive` (alias `divmmc`) is the **DRIVE** button, wired to
the DivMMC. **RESET** is not an NMI button and is not accepted here. The
press goes through the same path as the host F9 / F10 hotkeys, so it is
subject to the same enable gates — NextREG 0x06 bit 3 for the Multiface,
bit 4 plus NextREG 0x83 bit 0 for the DivMMC — and a press with its gate
closed does nothing, exactly as on hardware. One press generates one
NMI, not a repeating one.

**--delayed-nmi-frames** *N* *BUTTON*  
Press *BUTTON* after *N* emulated frames. This is the frames-unit
spelling of **--delayed-nmi**, not an override of it: both forms queue
into the same list, so giving both schedules two presses.

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

**--persistent-breakpoints**  
Keep breakpoints and watchpoints armed while the debugger window is
**closed**. Without this option — the default — closing the window
disarms them, and nothing stops. With it, a hit pauses the machine and
reopens the debugger window at the breakpoint. With the window already
open, behaviour is unchanged. The breakpoint check then runs on every
instruction for the whole run, which is what “persistent” costs; it is
accepted but has no effect in **--headless**, in the SDL-only build and
in builds without the debugger, since only the debugger can set a
breakpoint.

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
two are equivalent, so use one or the other: combining them
(`jnext --load game.tap game.tap`) is rejected, as is naming two files.
The format is detected from the extension:

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
- Master and per-subsystem host gains are available under **Settings \>
  Preferences \> Audio**, or for one run with the **--audio-gain-**
  family. Beeper, each TurboSound AY chip and the DAC family can be
  balanced independently before the master gain. They change playback
  and recording volume without changing guest-visible audio state and
  survive cold boots. Each explicit CLI value overrides its
  corresponding saved preference.

## NETWORKING (ESP-01 WiFi)

A real ZX Spectrum Next has a header for an ESP-01 WiFi module on UART
0. **--esp** emulates one, so software written for it - NXtel, nextsync,
`newt`, the NextZXOS `.HTTP` and `.zxdb-dl` dot commands - can reach the
network through your host.

Both **TCP and UDP** are emulated. UDP is what `newt` uses to read the
time from an internet SNTP server, so `.newt sntp 0 pool.ntp.org` works;
the optional local-port argument of `AT+CIPSTART="UDP",...` is honoured.
UDP “modes” 1 and 2, which let the far end of a link become whoever last
sent a packet, are answered `ERROR` rather than accepted and ignored.
**Server mode** and **multiplexed connections** are emulated too - see
*Letting a program listen* below. TLS and transparent mode are not.

### It is off by default, and that is the point

An enabled ESP gives the running program an **outbound TCP and UDP pipe
out of the emulator**, and the program can already read the SD-card
image, which jnext opens read-write. A program you did not write is a
program you are trusting when you turn this on. So jnext does not turn
it on for you, and there is no way for a loaded program to turn it on
for itself.

When it *is* on, a windowed run shows an **ESP cell in the status bar**
for as long as the run lasts. Its presence means the guest can reach the
network; it names the host of the most recent connection, and shows an
allowlist refusal or a fault in red. There is no way to have the ESP
enabled in a window without that cell being there. **--headless** builds
no window and so has no status bar: there the `esp01` log is the only
report, which is why it reports every connection at the default level.

An address the policy below refuses is reported as a *failure* rather
than an allowlist refusal, so it is not red; the reason
(`address refused by policy: …`) is in the cell’s tooltip and in the
log. See [issue \#161](https://github.com/jorgegv/jnext/issues/161).

### What is always refused, whatever you allow

These are refused by address, before any connection is attempted, and
**--esp-allow** cannot re-enable them:

- loopback - `127.0.0.0/8` and `::1`, i.e. daemons on your own machine;
- link-local - `169.254.0.0/16` and `fe80::/10`;
- cloud metadata endpoints - `169.254.169.254`, `100.100.100.200` and
  `fd00:ec2::254`;
- the unspecified addresses `0.0.0.0/8` and `::`;
- multicast and reserved space - `224.0.0.0/4`, `240.0.0.0/4`,
  `255.255.255.255`, `ff00::/8`.

IPv6 spellings of an IPv4 address (`::ffff:127.0.0.1`, NAT64, 6to4) are
resolved to what they actually reach and judged on that, so they cannot
be used to get around the list.

**Your own LAN is reachable.** RFC1918 addresses (`192.168.x.x`,
`10.x.x.x`, `172.16-31.x.x`), carrier-grade NAT (`100.64.0.0/10`) and
IPv6 unique-local (`fc00::/7`) are deliberately *not* refused - reaching
a machine on your own network is the main thing people use this for, and
nextsync in particular connects to a PC on the LAN. The two
cloud-metadata addresses above sit inside those ranges and are carved
back out.

### Narrowing it further

**--esp-allow** restricts the program to hosts you name:

    jnext --esp --esp-allow nx.nxtel.org NXtel.nex

Every connection to anything else is refused, the program gets the same
`ERROR` it would get from a real module that could not connect, and the
refusal is reported in the log and in the status bar.

### Making it permanent

**Settings \> Preferences \> Network** holds the same two settings for
people who run one of these programs regularly: an enable checkbox and
the allowed-host list, one host per line (a comma separates too,
matching the config file’s own format). Blank entries and repeats are
dropped, and the list is greyed out while the module is off, because it
restricts nothing on its own.

The module is built when a machine starts, so this page cannot switch it
on or off underneath a running program: the change takes effect on the
next hard reset (**F1**, or **Machine \> Power Reset**) and on the next
launch. The CLI still wins for a single run in both directions -
**--esp** over a saved *off*, and **--no-esp** over a saved *on*.

### Letting a program listen (server mode)

A program can also **accept** incoming connections: `AT+CIPMUX=1`
followed by `AT+CIPSERVER=1,<port>` makes jnext listen on that port and
hand the program each connection that arrives. This is what a debug stub
running on the emulated Next needs, because a debugger on your PC dials
out and never listens.

Everything above is about *outbound* connections - which addresses the
program may dial. A listening socket points the other way: it exposes
the **program** to whatever can reach the port, and the allowlist has
nothing to say about it, because the program does not choose who
connects. So the boundary is the bind address instead:

- by default the port is bound to `127.0.0.1`, so only this machine can
  reach it - which is all the intended use needs;
- **--esp-listen-address** *ADDR* widens that, and `0.0.0.0` means any
  machine that can reach yours can talk to the running program. There is
  no authentication of any kind in front of it;
- nothing listens until the program asks. There is no way to open a port
  from the command line, and no port survives a Power Reset;
- if the port cannot be bound - already in use, or an address that is
  not yours - the program is told `ERROR`. jnext never falls back to a
  different port or a wider address.

Up to four programs can be connected at once, numbered from 1; number 0
stays reserved for the program’s own outbound connection, so opening a
server never costs it the ability to dial out. `AT+CIPMUX` cannot be
changed while anything is connected.

Still not emulated: TLS and transparent mode ([issue
\#154](https://github.com/jorgegv/jnext/issues/154)).

### Nothing about your host leaks into the program

The module reports a fixed synthetic identity: SSID `JNextWifiHost`, a
`02:00:00:...` MAC and BSSID, and a `192.168.1.50` station address. None
of it is read from your machine, so a program cannot learn your real
SSID, MAC or local addresses by asking. The emulated module is not a
radio and never scans.

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
`sdcard`, `divmmc`, `spi`, `ctc`, `i2c`, `multiface`, `esp01` and
`esxdos`.

`esp01` is the emulated ESP-01 WiFi module (**--esp**). At the default
`info` level it reports only the connections opened, refused, failed and
closed - the events **NETWORKING** describes as never silent. A refusal
or a failure is a warning, so it survives **--log-level** *warn* too.
`debug` adds every AT command received and every response emitted, and
`trace` adds the raw byte traffic in both directions. Turning it down to
`off` silences the connection reports too, which is a deliberate choice
on the user’s part and not something jnext undoes.

`esxdos` is a tracer rather than a subsystem: at `trace` level every
`RST $08` esxdos call is logged with its arguments and its result,
including calls jnext does not service. It does not need
**--esxdos-stub** — the interesting case is a NextZXOS-expecting program
running without the stub, where those calls are otherwise invisible.

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

Menu shortcuts use **Alt**, never plain **Ctrl**: Ctrl is the Spectrum’s
Symbol Shift, so a Ctrl shortcut would eat a key the guest needs (see
**THE KEYBOARD** below).

**File**  
Load a program (Alt+O - NEX/SNA/SZX/TAP/TZX/WAV/RZX), Mount SD Card
Image, Record MPEG4 Video (Ctrl+F5) / Stop (Ctrl+F6), Play RZX / Record
RZX / Stop RZX, Save Screenshot (Alt+S), Save Snapshot (Alt+Shift+S),
Quit (Alt+Q).

**Machine**  
Power Reset (Alt+R - power off/on cold boot, full boot chain), Soft
Reset (F4 - the front-panel reset button: back to NextZXOS without
re-running the boot chain; does nothing while the firmware still holds
config mode, or after a direct **--load** where the firmware never ran),
Machine Type (48K / 128K / +3 / Next), CPU Speed (3.5 / 7 / 14 / 28
MHz - the Next’s own clock), Emulator Speed (0.5x / 1x / 2x / 4x /
custom % - the host-side throttle).

**Input**  
Joy 1 Source (port 0x1F) and Joy 2 Source (port 0x37), each selecting
what drives that connector; Capture Mouse, which confines the host
pointer so the Kempston mouse can move freely (Ctrl+Alt releases it).

**Tape**  
Open Tape File (Alt+T), Eject, Rewind, Fast Load (toggle).

**Debug**  
Magic Breakpoint (toggle).

**View**  
Scale 1x / 2x / 3x, Fullscreen (F11, letterboxed), CRT Filter, Debugger
(Alt+D).

**Settings**  
Preferences…, which opens the settings dialog (Startup, Input, Audio and
Paths tabs). Its values are saved to the configuration file;
command-line options always take precedence over them.

**Help**  
About.

The toolbar has Power Reset, Soft Reset, Load, Screenshot, an NMI button
(Multiface NMI) and, in a debugger-enabled build, a bug button that
opens the debugger.

CPU Speed and Emulator Speed are different things: the first changes the
clock the emulated Z80 runs at, which is a real Next feature; the second
changes how fast the emulator runs relative to real time.

## KEYBOARD MAPPING

| PC key                | Spectrum key                            |
|-----------------------|-----------------------------------------|
| Letter/number keys    | The corresponding key                   |
| Shift (left/right)    | Caps Shift                              |
| Ctrl (left/right)     | Symbol Shift                            |
| Backspace             | Delete (Caps Shift + 0)                 |
| Arrow keys            | Cursor keys (Caps Shift + 5/6/7/8)      |
| Enter                 | Enter                                   |
| Space                 | Space                                   |
| Esc                   | Break (Caps Shift + Space)              |
| Tab                   | Extend Mode (Caps Shift + Symbol Shift) |
| Caps Lock             | Caps Lock (Caps Shift + 2)              |
| Key left of `1`       | True Video (Caps Shift + 3)             |
| `\`                   | Inverse Video (Caps Shift + 4)          |
| Alt + key left of `1` | Inverse Video (Caps Shift + 4)          |
| Alt + E               | Edit (Caps Shift + 1)                   |
| Alt + G               | Graph (Caps Shift + 9)                  |
| Alt + C               | Caps Lock (Caps Shift + 2)              |
| `'`                   | `"` (Symbol Shift + P)                  |
| `;`                   | `;` (Symbol Shift + O)                  |
| `.`                   | `.` (Symbol Shift + M)                  |
| `,`                   | `,` (Symbol Shift + N)                  |
| `/`                   | `/` (Symbol Shift + V)                  |
| `-`                   | `-` (Symbol Shift + J)                  |
| `=`                   | `=` (Symbol Shift + L)                  |

Shift is Caps Shift and Ctrl is Symbol Shift, matching a real Spectrum
Next with a PC keyboard attached. Earlier releases had the two swapped.

Holding Shift while pressing a digit or one of the symbol keys above
reaches the guest as Caps Shift plus that key: Shift+1 is Edit, Shift+0
is Delete, Shift+5 to Shift+8 are the four cursor keys, Shift+9 is
Graph, and Shift+`;` is Caps Shift + Symbol Shift + O. jnext keys off
the physical key rather than the character your host layout produces,
the same way the Next’s own PS/2 keymap treats the shift keys as an
independent overlay. Earlier releases dropped all nineteen of those
keystrokes before the Spectrum saw them.

Esc is the Spectrum’s Break key, not a fullscreen shortcut. Fullscreen
is toggled with F11 only (it used to also be exited with Esc).

The function keys are host controls, not Spectrum keys. F1 is Power
Reset (the same as Alt+R), F4 is Soft Reset, F9 fires the Multiface NMI,
F10 fires the DivMMC NMI, F2 cycles the window scale and F11 toggles
fullscreen. With the debugger open F9 belongs to the debugger (Pause /
Break) instead.

Ctrl is left alone for the guest. No Ctrl+letter chord of any kind is a
jnext shortcut, so the Symbol Shift sequences NextBASIC needs
constantly - Ctrl+O `;`, Ctrl+D `STEP`, Ctrl+R `<`, Ctrl+T `>`, Ctrl+Q
`<=`, Ctrl+S `|` - all reach the program you are running, and so does
Ctrl+Shift+S (Caps Shift + Symbol Shift + S), which was the last one to
move. Earlier releases bound those to menu commands and the program
never saw them. The only Ctrl chords jnext still takes are Ctrl+F5 and
Ctrl+F6 (start and stop video recording), and function keys have no
Spectrum meaning to lose.

Alt is the opposite: it is a host modifier, never a Spectrum key. jnext
claims Alt + Q/O/S/R/T/D and Alt+Shift+S (menu shortcuts) plus Alt +
F/M/I/A/B/V/N/H (menu bar), which leaves the guest only Alt + E/G/C
(EDIT, GRAPH, CAPS LOCK) and Alt + the key left of `1` (INV VIDEO). Real
Next hardware instead maps Left Alt to EXTEND MODE and Right Alt to
GRAPH; jnext deliberately does not, following the FUSE/ZEsarUX
convention that puts EXTEND MODE on Tab.

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

The debugger opens in its own window (**Debug \> Debugger** or **View \>
Debugger** - the same entry in both menus - or Alt+D). It is driven from
the Qt6 UI, so it needs a GUI build (`make gui-release` or
`make gui-debug`); the SDL-only build has no way to open it.

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
  panel. They are armed only while the debugger window is open; start
  jnext with **--persistent-breakpoints** to keep them armed with it
  closed, in which case a hit reopens the window at the breakpoint
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

| Key      | Action             |
|----------|--------------------|
| F2       | Enable Trace       |
| F3       | Export Trace       |
| F5       | Run / Continue     |
| F6       | Single Step (into) |
| F7       | Step Over          |
| F8       | Step Out           |
| F9       | Pause / Break      |
| Shift+F6 | Frame Back         |
| Shift+F7 | Step Back          |

The window may be resized below what the panels need: the panel area
then scrolls rather than clipping, and the control toolbar along the
bottom stays visible, with any buttons that no longer fit reachable from
its overflow menu. Its size is remembered between sessions and clamped
to the screen it reopens on.

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
Preferences**. Host gains are stored under `[audio]` as `gain_db`
(master), `gain_beeper_db`, `gain_ay0_db`, `gain_ay1_db`, `gain_ay2_db`,
and `gain_dac_db`. The emulated ESP-01 is stored under `[esp]` as
`enabled` (`true`/`false`, the persistent form of **--esp**) and
`allowed_hosts` (a comma-separated list, the persistent form of
**--esp-allow**), both edited under **Settings \> Preferences \>
Network**. Whenever `enabled` is set the status bar carries the ESP cell
**NETWORKING** describes, so neither a config file nor that page can put
the guest on the network without saying so. **--no-esp** overrides it
for one run. CLI options always take precedence over saved values, and
headless runs never read it.

`~/.jnext/sdcard/cspect-next-1gb-fixed.img`  
The default SD-card image, used when **--sdcard** is not given.

`~/.jnext/sdcard/cspect-next-1gb.img`  
The canonical distribution image the patched one above is produced from.

## EXIT STATUS

**jnext** exits 0 on success and non-zero on error. In particular it
exits non-zero when a **--delayed-screenshot** was requested but never
taken, rather than silently writing nothing, and likewise when a
**--record** recording fails to materialize as a usable output file.

## SEE ALSO

**ffmpeg**(1)

The project documentation: `README.md` for an overview and `BUILD.md`
for building from source.

Project home page: <https://github.com/jorgegv/jnext>

## BUGS

Report bugs at <https://github.com/jorgegv/jnext/issues>.

## AUTHORS

Main author: Jorge Gonzalez, aka ZXjogv <zx@jogv.es>.

Code contributors: dcrespo3d, jon263.

Testers and bug reporters: danboid, Duefectu, janko-jj, Utodev,
WoolyChewbakker.

If you are named here and would prefer not to be, or are not named and
would like to be, say so at the issue tracker below. `CREDITS.md` in the
source distribution carries the same list, plus the third-party
libraries and the reference projects jnext is built and verified
against.

## COPYRIGHT

Copyright (C) ZXjogv <zx@jogv.es>. Licensed under the GNU General Public
License version 3 or later.
