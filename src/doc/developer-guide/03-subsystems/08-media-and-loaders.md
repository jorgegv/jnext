# 3.8 Media and loaders

This is the subsystem that gets code into the machine and gets state, pictures
and sound back out of it. Almost none of it is emulated hardware. A real
Spectrum has a tape port and an SD card; it has no notion of a `.sna` file, and
nothing inside it can write a PNG. These loaders exist because a *developer*
needs a way in and a way out, so they are host-side conveniences that reach
directly into the emulator's RAM, registers and MMU rather than going through
any bus the guest can see.

Everything under this heading lives in `src/core/`, is driven from the frontend
(a CLI flag at startup, a menu item later) and drives `Emulator` — never the
reverse. Only two things here are hardware the guest could tell apart from the
real article: the real-time tape signal, which arrives on the EAR line exactly
as an analogue tape would, and the SD card's SPI transport, which is a device
the guest's own driver talks to. Both are called out below.

## What jnext can read and write

| Format | In | Out | Where | Notes |
|---|---|---|---|---|
| `.nex` | yes | yes | `nex_loader.*`, `nex_saver.*` | Next-native. V1.0–V1.3 (V1.3 gated, see below) |
| `.sna` | yes | yes | `sna_loader.*`, `sna_saver.*` | Reads and writes both forms; the form follows the machine; refuses a Next, a +3 the format cannot describe, and any machine whose 0x4000-0xFFFF window it cannot name (extended paging) |
| `.szx` | yes | yes | `szx_loader.*`, `szx_saver.*` | Writes only 48K/128K/+3; refuses Next |
| `.z80` | yes | — | `z80_loader.*` | v1/v2/v3, 48K and 128K |
| `.tap` | yes | yes | `tap_loader.*`, `tap_saver.*` | Save is a ROM `SA-BYTES` trap |
| `.tzx` | yes | — | `tzx_loader.*` | Wraps ZOT (`third_party/zot`) |
| `.wav` | yes | — | `wav_loader.*` | Real-time only, no fast path |
| `.rzx` | yes | yes | `rzx_player.*`, `rzx_recorder.*` | Input recording, not state |
| raw binary | yes | — | `Emulator::inject_binary` | `--inject` + `--inject-org`/`--inject-pc` |

Format selection is by **file extension**: for `--load` in `src/main.cpp`
around line 890, and again in the GUI's file dialogs and in the headless
snapshot writer. There is no content sniffing, and an extension jnext does not
recognise is a hard error naming the supported set rather than a guess at what
the file might be.

The snapshot and program loaders all follow the same two-phase shape.
`load(path)` parses and validates into member state; `apply(emu)` writes that
state into the machine. Splitting the two means a malformed file fails before
the machine has been touched, leaving the running session intact. Several
loaders additionally define their parse and decompression logic **inline in the
header** — `z80_loader.h`, `tap_loader.h`, `nex_loader.h`, `tap_saver.h` — for
one specific reason: it lets test tiers that cannot link `jnext_core` exercise
the real parser rather than a copy of it. See
[chapter 4](../04-testing/index.md).

A loader that puts the CPU straight into an interrupt mode (`.sna`, `.z80`,
`.szx`, and a NEX entering at IM 1) executes no `IM` instruction, and the latch
behind NR `0xC0` bits 2:1 is fed only by the ones the CPU decodes
(`im2_control.vhd:218-229`). So `Emulator::load_sna()`/`load_z80()`/`load_szx()`
and `NexLoader::apply()` seed it with `Im2Controller::set_im_mode()` from the
restored CPU.

## Tapes: two completely different mechanisms

Loading from tape is the one place where the emulator offers you a choice
between fidelity and speed, so it is worth being clear about what the choice
actually is. On real hardware a game takes minutes to load, the border stripes
as it goes, and the loader chatters through the speaker. jnext can reproduce
all of that, or it can skip it entirely and have the program in memory almost
instantly. Those are two independent code paths that share nothing.

**Fast load is a ROM trap.** When ROM is paged into slot 0 and PC reaches the
48K ROM's `LD-BYTES` entry at `0x0556`, the run loop hands the next block
straight to memory and skips the routine altogether — no tape signal is ever
generated. `.tap` and `.tzx` both support this. `.wav` cannot, because a WAV is
just audio samples with no block structure to extract. The gate is a PC match,
which has one consequence worth knowing: a custom or turbo loader that never
enters the ROM routine is never intercepted, and real time is then the only way
it will load.

Saving works the same way in reverse. `--tape-save` arms `TapSaver`, which
traps `SA-BYTES` at `0x04C2` and appends a TAP block. That trap is gated on a
**ROM identity check** as well as on the PC, because other ROMs legitimately
execute code at `0x04C2` — a plain PC gate fired ten times during an ordinary
NextZXOS boot.

**Real-time playback** (`--tape-realtime`) is the honest one. The loader drives
the EAR bit per T-state and the ROM's own loading routine decodes it, so the
border stripes and the timing are produced by the same code that produces them
on hardware. All three tape formats support it. An `IN` from port 0xFE sees the
tape level of the T-state its `port_fe_dat_0` reload falls in — in the I/O
cycle's third clock, 2.5 T-states in after its contention stretches
(`zxnext.vhd:3455-3464`), so T-state 9 of an uncontended `IN A,(n)` — not the
level at the instruction's start (GH #265). The EAR signal is also routed
into the audio mixer, which is why you hear the loader as well as see it.

Getting BASIC to *start* loading is a third mechanism again. The phantom typist
in `src/input/phantom_typist.h` types `LOAD ""` for you once it can prove the
ROM's input loop is running — see [3.7 Input](07-input.md) for how it decides
that. It is armed by `load_tap()` only; `.tzx` and `.wav` still fall back to a
fixed 100-frame delay.

`TapLoader::parse_blocks()` refuses a `.tap` whose blocks do not tile the
file exactly — a block whose declared length runs past the end, or one stray
byte where a length field should start — the two cases FUSE's libspectrum TAP
reader rejects. A bad checksum or flag byte inside a complete block is not a
container error: the tape loads, and the ROM reports "R Tape loading error"
when it reads that block. `TzxLoader::validate()` does the same for a `.tzx`
against libspectrum's TZX reader: the `ZXTape!` signature, then every block
walked with that reader's length rules, so a block that runs past the end or a
header with no block after it refuses the whole tape. ZOT's own `tzx_load()`
checks none of this — it took any file of two bytes or more, and one without
the signature as TAP data — so the check runs first and ZOT only ever sees a
validated TZX.

Where jnext deliberately accepts more than libspectrum is the block IDs that
library does not implement and refuses outright. The TZX specification gives
each of `$16`, `$17`, `$18`, `$26`, `$27`, `$34` and `$40` a length formula, and
every later ID one through its General Extension Rule (a DWORD length after
the ID), so such a block is valid when its length fields fit the file.
`spec_skip_body()` holds those formulas; the validator, the fast-load scanner
and ZOT (a local patch in `third_party/zot/tzx.c`) all skip these blocks by
them, and `$19`, which ZOT cannot play either, the same way — the blocks after
still load. `load()` warns about each skipped block that carried content.

## NEX

`.nex` is the Next's native program container, and the most involved loader
here: four header versions, optional Layer 2 / LoRes / tilemode loading
screens, palettes, a Copper block, bank ordering and a CRC-32C.

Its **oracle is Ped7g's `nexload2.asm`**, not the distribution's own
`nexload.asm`. The distro loader refuses V1.3 outright, which makes nexload2
the only executable specification for that version. `nex_loader.cpp` cites it
line by line, and where the two loaders genuinely disagree — the palette-block
rule, the V1.3 delay model, the loading bar — the difference is documented as a
deliberate divergence rather than silently reconciled.

V1.3 is an **experimental format and not officially supported** (GH #228): the
user-facing entry points enforce V1.2 conformance. `Emulator::load_nex()`
refuses a header version above V1.2 unless
`EmulatorConfig::allow_experimental_nex_v13` is set — the CLI sets it with
`--experimental-nex-v1.3` (and refuses up front, exit 1, without it), the GUI
sets it per load after its warning dialog's Proceed. `NexLoader` itself stays
fully V1.3-capable — the gate lives at the entry seam, not in the loader — so
the V1.3 test corpus drives `NexLoader::load()`/`apply()` directly. The pure
policy predicate is `nex_version_needs_v13_optin()`; the pre-load version
query the entry points share is `NexLoader::probe_version()`.

`NexSaver` writes V1.2, and its class comment enumerates what the format simply
cannot carry: no register file beyond PC and SP, no NextREG state, no MMU slots
0–5. Read that list before treating a NEX round trip as a snapshot, because it
is not one.

A directly loaded NEX can also keep its own file handle open and stream from
itself. `extended_nex_host.*` presents the host file to the guest as a
synthetic block-addressed SD extent, so NextZXOS's file APIs work against it.

Whatever follows the header-described banks is never read into host memory:
`NexLoader::load()` reads only up to `payload_offset()`. What happens to those
trailing bytes depends on the header's `file_handle`. Any non-zero value
(`keeps_file_open()`) keeps the file open; the value only picks where the
handle goes: 1 to `$3FFF` in `BC` (`B`=0, `C`=handle), `$4000` and above
written to that address (`delivers_handle_in_bc()`; `nexload2.asm:397-407`,
`nexload.asm:560-570` and `:606-609`). `Emulator::load_nex()` then opens the
host file behind the handle — the extended-NEX host bridge in the stand-in
below — at `payload_offset()`, just after the last bank, where both loaders
hand their own handle over (GH #267), and the program streams them. With 0, both loaders read the declared
banks and close the file, with no size check at all (`nexload2.asm:390-395`,
`nexload.asm:547-551`), so the bytes are dead. jnext matches that: the program
loads and runs, and more than 16 KB of dead bytes logs a warning (GH #250,
found on Spectron2084). That warning replaces the refusal issue #10 added,
which had turned such a file away. No host bridge is opened for such a file,
so the stand-in cannot reach the bytes either: its `F_OPEN` then knows only
the in-memory file.

When no handle goes in `BC` (`file_handle` 0, or `$4000` and above), the two
loaders disagree on what `BC` holds at entry, and `load_nex()` follows the one
that really runs the file: `$00FF` ("no handle") for V1.3 (`nexload2.asm:407`),
`$0000` for V1.0–V1.2 (`nexload.asm:582-585`).

The rest of the entry state is `NexLoader::apply()`'s, and the comment there
tabulates it. Both loaders jump through the NextZXOS DivMMC ROM's `RST $20`,
which leaves `AF=$0044`, `HL` = PC, and the entry PC in the word just below
`SP` (its `push hl : ret`; `load_nex()` writes it). `DE` and `IX` are what each loader's
own last instructions leave: for V1.0–V1.2, `DE=$6Fxx` from the bank loop and
`IX` = the address of the last block read; for V1.3, `DE` = the CLI buffer
address plus its size (not the address, whatever the format's notes say) and
`IX` = 0, 1 or `$C000`. `IY`, `I`, `IM` and the alternate set belong to
NextZXOS, which neither loader touches; jnext uses the values measured under
the distro image's NextZXOS (`IY=$5C3A`, `I=$09`, IM 1), found identical for
every launch path tried. Interrupts are off. NR `0xC0`'s interrupt-mode field
is seeded to match, since no `IM` instruction runs.

The NextREGs follow each loader the same way: NR `0x06` and `0x08` are
read-modify-writes with each loader's own masks (so the internal-speaker and
stereo bits a user set survive where that loader keeps them); the V1.0–V1.2
loader sets 14 MHz before anything else, so a file with DONTRESETNEXTREGS set
starts at 14 MHz, not at the speed it found; and both loaders act on the
expansion-bus byte at header offset 142 whatever the file's version. What
NextZXOS itself leaves in registers no loader writes (its config, its NMI
setup) jnext does not reproduce; the list is in `apply()`.

## The warm start: applying a NEX to a machine the firmware made

The warm start (GH #234, the structural fix for GH #72) replaces the synthetic
machine at the top of `Emulator::load_nex()` with a recording of a real boot.
It is Next-only and `.nex`-only, and it is **not an option**: there is no
enable flag, because a default of "start the program on a machine hardware
cannot produce" is the wrong default. It shipped in v1.0.6 as opt-in
`--warm-start` while the reference movement it causes was still unmeasured;
that flag was removed once the movement had been measured and accepted (two
rows, both animation phase). `--warm-start-regenerate` survives as the debug /
refresh lever.

Everything that can go wrong falls back to exactly the machine `--load` has
always assembled, and says so on `error` first — the only decline that is
quiet is the non-Next one, which is not a decline at all (a 48K has no
firmware to record, so nothing was refused and nothing was asked for).

**Why it exists.** `Emulator::init()` skips the boot-ROM overlay whenever a
load file is present — the overlay at `$0000-$1FFF` would clobber the
program's own reset vector — so `nextboot.rom`, `TBBLUE.FW` and NextZXOS never
run, and every `reset()` default in every subsystem becomes directly
observable to the loaded program. Some of those defaults are states hardware
cannot reach: GH #226's dead F4/F9/F10 came from `nr_03_config_mode` sitting
at its power-on `'1'` for a whole session because nothing wrote NR `$03`.
Fixing such defaults one at a time is a treadmill, because the reference —
what the firmware leaves behind — exists only as a running machine.

**The mechanism.** `Emulator::ensure_warm_start_state()` looks for a cached
recording and, on a miss, calls `record_warm_start_state()`, which

1. constructs a **separate `Emulator`**,
2. `init()`s it with `load_file` cleared (which arms the boot-ROM overlay),
3. runs `kWarmStartBootFrames` (500) frames,
4. checks `nextzxos_resident()`, and
5. serialises it with `save_state()` — the same `Saveable` stream the rewind
   buffer uses.

`init_for_load_from_file()` then `init()`s the live machine normally and
`load_state()`s the recording on top, because the stream carries the emulated
machine but not the host wiring `init()` installs (the port-dispatch lambdas,
the contention LUT, the ROM buffer, the SD card, the mixer's gains) — exactly
as a rewind lands on a running machine.

**A recording must be a cold boot, and re-`init()`ing is not one.** The first
implementation re-`init()`ed the live emulator and produced a machine that ran
500 frames and drew nothing. `nr_03_config_mode` has no reset clause in the
VHDL (`zxnext.vhd:1102`) and `NextReg::reset()` faithfully preserves it, so
the firmware-less commit the *first* `init()` had already made left config mode
clear — and with it clear, `TBBLUE.FW`'s ROM streaming through the NR `$04`
window (`zxnext.vhd:3044-3050`) never reaches the ROM area. Measured: four RAM
pages changed in 500 frames, none of them the screen. Hence the separate
`Emulator`, which is at power-on state by construction, the same way
`emulator_cold_boot()` is for F1.

**The capture criterion** is `Emulator::nextzxos_resident()`, deliberately not
a pinned PC (a NextZXOS update would then break capture silently). It asks
three questions: the boot-ROM overlay is off, `nr_03_config_mode` is clear,
and a NextZXOS marker string is present in SRAM ROM pages 0-7. The third is
load-bearing — a firmware-less machine answers the first two exactly as a
booted one does.

**The cache** is `src/core/warm_start_cache.{h,cpp}`: one file per machine
type under `<config-dir>/warm-start`, with a 96-byte header carrying the
identity that invalidates it — the SD image's SHA-256, the machine type, a
state-format version, and the exact *uncompressed* stream length. Any mismatch
discards the file and re-boots. The length is not a convenience:
`Ram::load_state` reads a count-prefixed blob straight into the live RAM
buffer, so refusing a stream whose length is not exactly what this build
writes is what keeps a foreign recording from writing past it. It is
**generated locally and never vendored** — a post-NextZXOS snapshot holds
NextZXOS and DivMMC ROM content in RAM.

**The header is plain; the payload is deflated.** The stream is ~2.29 MB, of
which RAM is 91.5 % and 89.5 % of the whole file is zero bytes; zlib takes it
to ~126 KB. zlib rather than zstd because zlib is already a required
dependency (`find_package(ZLIB REQUIRED)`; `rzx.h`, `szx_loader.cpp` and
`sdcard_provisioner.cpp` all use it). The header is **not** compressed,
because it carries the identity the loader validates *before* it is willing
to touch the payload: compressing it would mean inflating megabytes of a file
not yet shown to be this build's, this machine's or this card's, and sizing
the output buffer from a number read out of that same unvalidated stream.
zlib's `uncompress()` also wants the exact output size up front — which is
`plain_bytes`, in the plain header. There are therefore TWO lengths on disk:
`plain_bytes` at offset 16 (identity, compared against this build) and
`stored_bytes` at offset 88 (bytes on disk, compared against the file size and
nothing else). The `Identity` field is named `plain_bytes` for that reason —
the rename is what forced every former use site through the compiler when the
second length appeared. The magic's trailing digit is the FILE-layout
generation and went `JNEXTWS1` -> `JNEXTWS2` here; `kFormatVersion` did not
move, because the state stream inside did not change.

Three refusal branches came with the payload and are pinned by `WSC-Z-02`,
`WSC-Z-03` and `WSC-Z-05`: a stream that will not inflate, one that inflates
*short* of the declared plain length (zlib alone is happy with that — it
reached `Z_STREAM_END` with room to spare — so the equality is checked
explicitly), and a header declaring an empty payload.

**The handover leaves 48 BASIC paged.** `nexload.asm` never writes 0x7FFD or
0x1FFD; its last instruction is `rst $20` (`nexload.asm:587`), NextZXOS's
"leave this dot command and jump to HL", and that OS handover is what selects
ROM 3. Measured through the real chain in jnext — boot NextZXOS, *Command
Line*, `.nexload` — the machine sits at `sram_rom = 0` (NextZXOS's own ROM)
for the whole menu and command line and is at `7FFD=0x10, 1FFD=0x06,
sram_rom = 3` from the program's first instruction. The synthetic machine gets
this right by having only the 48K image in its SRAM ROM pages; the recording,
taken at the NextZXOS menu, does not, so `init_for_load_from_file()` sets the
two ROM-bank bits after the restore. It is deliberately NOT in
`NexLoader::apply()`, which also runs on the synthetic path. Without it every
program that reads the character set out of ROM renders noise —
`magic-bp-demo` and `magic-port-demo` did, and row `G` of `warm-start-func` is
the regression.

**What it surfaced.** `NexLoader::apply()` did not model MMU0-5 at all; it
relied on `init()`'s reset defaults happening to hold bank 5 at `$4000` and
bank 2 at `$8000`. Both reference loaders set them explicitly
(`nexload.asm:280-284` unconditionally, `:406-407` gated;
`nexload2.asm:913-918` in its gated reset table), and on a NextZXOS-resident
machine the defaults do not hold — a measured MMU3 = page 17 sent every write
to `$6000-$7FFF` into Layer 2's bank 8. `apply()` now writes them, which is a
no-op on the synthetic path and pinned by `NEXMMU-01..07`.

**The cost.** A miss costs a 500-frame boot, ~3.5 s headless. A hit costs the
SD image's SHA-256 — ~0.5 s for a 1 GB image warm in the page cache, ~1.2 s
cold — because the recording's identity is the image's *contents*. The cheap
alternatives (a size+mtime key, a hashed prefix, a named subset of files) all
accept a cached recording without reading the image, and any of them can serve
a recording of a different card while reporting success; that is the worst
failure this mechanism can have, because the machine it produces looks booted.
Within a process the digest is paid once: the restored stream stays in
`warm_start_state_` for the session, so a GUI **File > Load NEX File…** after
a CLI `--load` costs nothing.

**Tests.** `test/warm_start/warm_start_test.cpp` (`WSC-*` for the cache file,
its four invalidation keys and the compressed payload; `WSR-*` for the
residency criterion, the announced fallbacks and `WSR-DEF-01`, which proves the
path is reached with no flag set) plus the `warm-start-func` regression row for
the end-to-end record → cache → restore round trip, which needs a real SD image
and so cannot be a unit test.

## The esxDOS stand-in for directly loaded programs

On hardware a NEX is always started by NextZXOS's `nexload`, so the program
can call the esxDOS / NextZXOS API: `RST $08` followed by a one-byte call
number, reached through the DivMMC automap (see
[3.6 Peripherals](06-peripherals.md#divmmc)). A direct load skips NextZXOS, and
`$0008` then holds the 48K ROM's ERROR-1 restart, so an unanswered call crashes
the program. Warhawk's first call, `M_GETSETDRV`, ended in a DI + HALT at
`$1303` that way (GH #250). jnext therefore answers these calls on the host,
the way CSpect (`esxDOS.dll`) and ZEsarUX (`esxdos_handler`) always do. Neither
emulator is an oracle for this: both fake every `RST $08`, DivMMC or not.

**Where it lives.** `Z80Cpu::execute()` (`src/cpu/z80_cpu.cpp`) checks every
arrival at `$0008`. When the byte before the pushed return address is `$CF`
(`RST $08`) and the call number after it is `$80` or above, it calls
`Z80Cpu::on_esxdos_call`. If that returns true the CPU resumes after the call
number, charged a flat 50 T-states; otherwise `$0008` runs as usual. The
handler is the `handle_esxdos` lambda in `Emulator::init()`
(`src/core/emulator.cpp`), wrapped in `esxdos_bridge_handler_` for tracing.
`init()` attaches it for `--esxdos-stub`, for esxdos tracing, and when the
command line loads a `.nex`. `Emulator::load_nex()` attaches it too, for a NEX
loaded from the GUI after start-up.

**When it answers.** Three things arm it:

- `direct_nex_esxdos_` — set by `Emulator::load_nex()` for every NEX it loads,
  cleared by `init()`, so by a soft reset, and gone after a hard reset, which
  reconstructs the emulator. `load_sna()`, `load_szx()` and `load_z80()`
  re-run `init()` before applying the file, and RZX playback does the same for
  its embedded snapshot (`load_snapshot_from_memory()`), so a snapshot clears
  it. `load_tap()`, `load_tzx()` and `load_wav()` deliberately do not: they
  attach tape media to the running machine, so a NEX still running keeps its
  stand-in.
- `EmulatorConfig::esxdos_stub` (`--esxdos-stub`), for the whole session.
- The extended-NEX host bridge (`extended_nex_host_`), open when the NEX
  header's `file_handle` is non-zero. Only `load_nex()` opens it, so it never
  exists without `direct_nex_esxdos_`; `init()` closes it, so both resets and
  every re-initialising load do.

**The ROM gate.** Whatever armed it, the handler answers nothing unless NR
`$50` reads `$FF`, i.e. ROM is paged in at `$0000`. That is the hardware's own
precondition: the DivMMC automap that takes `$0008` to NextZXOS needs
`sram_divmmc_automap_rom3_en` (`zxnext.vhd:3138`), which needs
`sram_pre_override(0)`, which only the ROM branch of the slot-0 decode sets
(`zxnext.vhd:3060-3066`). A program with its own RAM at `$0000` keeps its own
`RST $08` handler.

**What it answers.**

| Call | Directly loaded NEX | `--esxdos-stub` only |
|---|---|---|
| `$85`-`$87` `DISK_FILEMAP` / `STRMSTART` / `STRMEND` | with the host bridge, block streaming from the NEX file; without it, the catch-all error below | falls through |
| `$88` `M_DOSVERSION` | `BC`=`'NX'`; `DE`=`$0194`, or `$0202` with the host bridge | same, `$0194` |
| `$89` `M_GETSETDRV` | get or set `C:` returns `A`=`$10`; any other drive, carry set, `A`=`$0B` | falls through |
| `$8F` `M_EXECCMD` | `run NAME.nex` for a plain name in the same directory: chain-loads it through `nex_load_request_` | same |
| `$9A` `F_OPEN` | host bridge: the NEX's own file or a read-only sibling regular file (no symlinks, no paths) on host handles 2/3, other names refused unless `--esxdos-stub` is also given; otherwise the in-memory file | in-memory file |
| `$9B` / `$9D` `F_CLOSE` / `F_READ` | host handles 2/3, else the in-memory file | in-memory file |
| `$9E` `F_WRITE` | the in-memory file only; there is no host-handle branch, so a write to host handle 2 or 3 fails (carry set, `A`=5) | in-memory file |
| `$9F` / `$A0` / `$A1` `F_SEEK` / `F_FGETPOS` / `F_FSTAT` | host handles; otherwise `F_SEEK` fails (`A`=5) and the other two get the catch-all error | `F_SEEK` fails (`A`=5); the other two fall through |
| any other `$80`-`$B1` | carry set, `A`=`$02` (`esx_enonsense`) | falls through |

With `--esxdos-stub-root` the file and directory calls are served from a real
host directory instead; see the section below.
| `$B2` and above | falls through | falls through |

The in-memory file (`esxdos_stub_file_`) is one anonymous buffer: opening for
write creates it empty under the given name, and opening for read succeeds only
under that same name. `emulator_cold_boot()` carries it across a power reset,
and a `.RUN` chain-load is a cold boot, so a chain of NEX files shares it.

**The oracle.** The `M_GETSETDRV` replies, the `esx_enonsense` reply to
unassigned numbers (`$80`, `$8A`, `$96`, `$97`) and the fact that `$B2` and
`$E0` never return (NextZXOS raises a BASIC error report) were measured on
real NextZXOS firmware: the distro SD image booted natively in jnext
(tbblue.fw, then NextZXOS), a probe NEX started with `.nexload`, and the
results read back through the magic port. That is the real firmware on
jnext's emulated hardware, not a physical Next. The error numbers are named
from `esxapi.def`. The catch-all also refuses calls NextZXOS does implement,
such as `F_GETCWD` (`$A8`), `F_CHDIR` (`$A9`), `F_OPENDIR` (`$A3`) and
`M_GETDATE` (`$8E`). For those it is jnext's choice, not NextZXOS's reply.

**Why `--esxdos-stub` answers less.** It can be given with NextZXOS booted,
and there `M_GETSETDRV` and the catch-all would answer in front of NextZXOS's
own esxDOS: with them, `.ls` refused `M_P3DOS` and `M_GETHANDLE` and left a
blank screen. So both apply only to a directly loaded NEX. The calls the stub
does answer still shadow NextZXOS's (a long-standing limitation): with
NextZXOS booted and the flag given, `.ls` reports "No such file or dir".

**Tracing.** `--log-level esxdos=trace` logs every call with its arguments on
the way in and its result on the way out, including calls that were not
answered. The names come from `src/core/esxdos_trace.h`, transcribed from
`esxapi.def` and z88dk's `esxdos.def`.

**Tests.** `test/esxdos_stub/esxdos_stub_test.cpp`: `ESX-01..10` cover the
in-memory file and `.RUN` chaining, `ESXT-01..36` the call-name table, hook
installation and the `$0008` trigger, and `ESXN-01..18` GH #250. Most `ESXN`
rows load a NEX built at test time through `load_nex()` and run it with
`run_frame()`; the bare `--esxdos-stub` rows put the program in RAM directly
instead, because `load_nex()` would arm the direct-load answers, and the
`$B1`/`$B2` rows call the handler directly. They cover the answers above, the
disarm on reset and soft
reset, the ROM gate for all three sources, the `$B1`/`$B2` boundary, and a bare
`--esxdos-stub` leaving `$89`, `$8D` and `$94` to the code at `$0008`. The host
bridge has its own suite, `test/core/extended_nex_test.cpp` (`XNEX-*`), and
the regression rows `extended-nex-stream-func` and `esxdos-chain-*-func` run
real programs.

## A host directory: `--esxdos-stub-root` (GH #31)

`--esxdos-stub-root DIR` replaces the single in-memory buffer with a real host
directory. It implies `--esxdos-stub`, and `--esxdos-stub-writable` is the
separate opt-in for writes. `EsxdosHostFs` (`src/core/esxdos_hostfs.*`) owns
the sandbox, the handle table and the 8.3/timestamp synthesis; the dispatcher
block sits in `handle_esxdos` between the extended-NEX bridge and the legacy
in-memory switch.

**Handle spaces.** 1 is the in-memory file, 2 and 3 are `ExtendedNexHost`, and
`EsxdosHostFs` owns `$04..$0B` for files and `$84..$87` for directories. The
dispatcher claims a handle-keyed call only for handles in its own range, so the
three back ends never contend; path-keyed calls (`F_OPEN`, `F_STAT`,
`F_OPENDIR`, `F_CHDIR`, `F_GETCWD`, `F_GETFREE`, `M_GETDATE`) it claims
outright, since with a root there is exactly one volume. The one interaction
worth knowing: with a root configured, the extended-NEX bridge's *sibling*
lookup is skipped, so only the NEX's own filename still resolves beside the
NEX — otherwise one name could mean two files.

**Reading the filespec.** Two readers, and the distinction is load-bearing. A
dot command's COMMAND TAIL is "terminated by `$00`, `$0d` or `':'`"
(`readdir.asm:78-79`), so `M_EXECCMD` uses `read_cmdline`. A FILESPEC is
NUL-terminated and nothing else — every esxDOS header block says so in those
words — so the four path-keyed calls plus the two pre-existing `F_OPEN` sites
use `read_filespec`. Applying the command-tail terminator set to a filespec
truncates exactly the drive-qualified form the API is built around: F_OPEN is
documented as "A=drive specifier (overridden if filespec includes a drive)",
`esxapi.def:129-130` assigns `'*'` and `'$'` as drive letters, and NextZXOS's
ROM carries literals like `"c:/nextzxos/autoexec.1st"`. That bug shipped into
review: one shared helper split on `':'`, so `"c:/hello.txt"` reached
`resolve()` as the single byte `"c"` and every drive-qualified open returned
`ENOENT`. `HFS-84..88` pin all four calls through the dispatcher and `HFS-89`
pins that `M_EXECCMD` still splits.

**A bad root is fatal.** `EsxdosHostFs::validate_root()` is called from
`main.cpp` beside the `--rzx-record` start-up check, and a root that does not
exist or is not a directory exits non-zero. It has to: parsing the flag also
sets `esxdos_stub`, so continuing would quietly serve the in-memory file and a
typo in a CI script would go green with the feature off. `configure()` calls
the same function, so the two cannot disagree.

**Containment.** Primary confinement is LEXICAL and consults nothing: the guest
path is split, `.` dropped, `..` popped, and a pop that would leave the root
refused with `esx_epath`. An escaping path is therefore never constructed. A
guest absolute path is absolute *within* the root. Both `/` and `\` separate
components, on every host: FAT forbids a backslash inside a name, and treating
it as an ordinary character would leave `std::filesystem::path` re-splitting it
on a Windows build but not a POSIX one, so the lexical walk would enforce
different things per platform. The drive qualifiers `*:`,
`$:` and `c:` (`esxapi.def:129-130`) all map to the root. On top of that, every
existing component is checked with `symlink_status()` and refused if it is a
link, the result is canonicalised and re-checked against the canonical root,
and an open re-checks once more afterwards. Those layers are genuinely
independent, and that has a consequence worth stating plainly: **no row can
prove any single symlink check is load-bearing**, because neutralising one does
not change the answer. A reviewer measured it — all seven neutralised at once
still gave a green suite. What the rows pin instead is that no call surface has
lost *all* its protection, once per surface, using a link whose target is
INSIDE the root (the only case containment cannot catch): `HFS-12` stat,
`HFS-90` open, `HFS-91` opendir, `HFS-92` chdir. For `open` and `stat` the
discriminating mutation is `symlink_status` → `status`, because their
`is_regular_file` gate implicitly refuses a link too; for `opendir` and `chdir`
deleting the explicit check is enough. The full note is in
`esxdos_hostfs.cpp`. The residual TOCTOU window is documented in the
`.cpp`: closing it needs an `openat(O_NOFOLLOW)` walk with no portable form
across the Linux, macOS and MinGW builds.

**Rewind.** An open `std::fstream` is not snapshottable, so `save_state` writes
only `(handle, is_dir, mode, position, path)` per open handle plus the guest
CWD, and `load_state` reopens from that. Reads therefore rewind exactly.
Writes cannot — the host side effect already happened — which is the whole
reason writes are behind a second flag.

**Refusals rather than approximations.** `esx_mode_use_wildcards`,
`esx_mode_sf_enable` and `esx_mode_use_header` return `esx_enosys`: each
changes what the entry stream contains, and a caller that asked for a filtered
or sorted listing and silently got neither is worse off than one told no. The
two modes real software was measured to use (`.ls` asks for
`esx_mode_lfn_only` and `esx_mode_short_only`, no other bits) are served.
`M_P3DOS` is not answered: it bridges into NextZXOS ROM entry points including
`IDE_SECTOR_READ`, and a directory has no sectors.

**SCOPE, and the measurement behind it.** This serves programs that reach the
filesystem through `RST $08` — a directly loaded NEX, and dot commands. It does
NOT serve NextZXOS's Browser, file selector, BASIC or loader. That was the
load-bearing assumption of the whole design, and it was settled experimentally
before any of this was built (2026-09-23), not inferred:

| Run (same build, same image, `--log-level esxdos=trace`) | calls in `$85..$B1` |
|---|---|
| Browser opened at `C:/`, descended into `C:/DEMOS/`, scrolled | **0** |
| `.ls` from the NextZXOS command line | **76** |

The `.ls` run is the positive control — it is `F_OPENDIR` ×2, `F_READDIR` ×35,
`F_GETCWD` ×2, `F_READ` ×6, `M_P3DOS` ×22 and more — so the instrument plainly
can see directory traffic. The Browser, listing a directory on screen, produced
none of it. NextZXOS carries its own SD/SPI driver and FAT code in
`enNextZX.rom` and never asks `$0008`. Reaching it would need a synthetic FAT32
volume at the block layer, which the project declined
(`doc/design/TASK89-ESXDOS-HOST-FILESYSTEM.md` §0.1 decision 5). The boundary is permanent, and the user-facing docs say so in
`--help`, the man page and the user guide because the flag's name invites the
opposite assumption.

**Tests.** `HFS-01..95` in `test/esxdos_stub/esxdos_hostfs_rows.cpp` (linked
into `esxdos_stub_test`): the sandbox and the 8.3/timestamp synthesis against
the class, every register convention through the real dispatcher. The
regression row `esxdos-hostfs-func` runs a real guest program that opens, seeks
to offset 6, reads and prints a real host file through `RST $08`, then probes
the sandbox from inside the guest — with a control run, no root, that must not
produce the content.

**Known gaps.** Without `--esxdos-stub-root` a directly loaded program still
cannot reach host files other than the kept-open NEX and its siblings, and the
in-memory file still has no `F_SEEK`. `M_DOSVERSION` reports 1.94 without the
host bridge. `M_GETERR` is not implemented in either back end.

## RZX

RZX records a *session*, and it does so by recording inputs rather than
pictures. The file holds an embedded snapshot of the machine at the start plus,
for every frame that follows, the instruction count and every value the program
read back from an `IN`. Playing it back re-runs the original code: jnext feeds
the recorded values through the port-dispatch hook at the points the program
asks for them, so the program takes exactly the path it took when the recording
was made. The result is a very small file that reproduces a run exactly instead
of showing you a video of it — which is also why an RZX is a good bug report,
and why it only works at all if the emulator executes the same instructions in
the same order both times.

`rzx.h` holds the format, `rzx_player.*` and `rzx_recorder.*` the two
directions. The snapshot a recording embeds is the richest format the machine
has (GH #274): a **`.jns` on a Next** (`Emulator::save_jns()` into a buffer — no
temporary file, which is deliberate), an SZX on the 128K and +3 (`SzxSaver` —
all eight banks and the paging ports), and an SNA on a 48K (`SnaSaver::save()`,
where the CPU view IS the machine).

The Next arm used to be a 48K SNA, and that was a real defect rather than a
documented limitation: it carries registers, banks 5/2/0 and the border and
nothing a Next adds, so a recording replayed correctly only when the program
redrew its whole display during the replayed frames. `test02layer2.nex` — which
draws a Layer 2 image and then leaves it — replayed 70183 pixels away from its
own recording, and `rzx-machine-func` did not see it because its fixture was a
program that redraws. That fixture is now `test02layer2.nex` for exactly that
reason.

`Emulator::load_snapshot_from_memory()` gained the matching `jns` branch, and it
is the one place that loads a `.jns` with the **SD identity override** on — an
owner decision of 2026-09-26, recorded as such at the call site because a
deliberate Tier-1 override should not read as convenience. A `.jns` file refuses a
different card, but an RZX is a portable artifact and the card is not part of what
a recording claims to carry, and the 48K SNA this replaces carried no card
identity at all. The override warns and names both cards; `--snapshot-mode strict`
still governs the provenance checks.

How much that override actually carries was measured rather than assumed, and the
answer is a useful boundary for judging RZX fidelity generally:

| Path the card could reach a replay by | Reached? |
|---|---|
| Guest reads through the CPU | **No.** `PortDispatch::in()` serves every `IN` from the log with no port filter, so SD data through 0xE7/0xEB comes from the recording whatever card is mounted |
| Guest reads through the DMA | **Yes.** `dma_.read_io` is wired to `port_.read()`, the pre-override path, so a DMA port read is neither recorded nor replayed — on any machine, for any snapshot type. A pre-existing RZX limitation |
| The ROMs | **Yes**, they are extracted from the card at init — but that path is CHECKED: the `.jns` records ROM digests, which warn, and refuse under `--snapshot-mode strict` |

So the override is belt-and-braces for CPU-driven SD access and load-bearing only
for the DMA case.
A command-line recording starts once the `--load`/`--inject` is in the
machine (`emulator_start_rzx_record_when_loaded()`), so that snapshot is the
loaded program. The tape ROM traps stand down while RZX records or plays —
see the trap block in `run_frame()` — and a fast-load tape already attached is
switched to real-time loading (`Emulator::rzx_suspend_tape_traps()`). On playback the embedded snapshot — SNA, SZX or Z80 — is parsed straight
from the file's bytes by `Emulator::load_snapshot_from_memory()`, using the
`load_from_buffer()` entry each of those loaders has beside its file-path
`load()`; nothing is written to a temporary file. Every way of starting a
playback reaches `load_rzx()` on a freshly initialised machine whose
`EmulatorConfig::load_file` is the recording — `--rzx-play` sets it as
`--load` does, and the GUI's Play RZX item goes through
`MainWindow::handle_load_path()` and the frontend cold boot like File > Load NEX File… —
because that field changes the machine `init()` builds (on the Next it decides
the boot-ROM overlay), and so how the recording replays. Any other snapshot type fails
the load: input replayed against a machine it was not recorded on reproduces
nothing.

For the same reason a playback builds the machine the recording was made on.
The recorder names it in the RZX creator block's custom data
(`rzx::set_recorded_machine()`, `machine=48k` — a `--machine` value), because
the snapshot cannot: a Next recording embeds a 48K SNA. `rzx::recorded_machine()`
reads that marker first and otherwise judges a recording by its snapshot
(`rzx::snapshot_machine()`: SNA size, SZX machine ID, `.z80` version and
hardware mode); a pre-marker jnext recording with an SNA names no machine.
`emulator_boot_machine()` turns that into the machine a boot builds, and every
boot that plays a recording asks it: `main.cpp` for the command line (unless
`--machine` was given explicitly — `load_rzx()` then warns on a mismatch) and
`emulator_cold_boot()` for every cold boot, which is the GUI's route. The
shared cold-boot driver hands the booted type back to the frontend
(`ColdBootHooks::keep_machine`), so the recording's machine stays selected for
later boots, a hard reset included, and `MainWindow::set_emulator()` shows it.

A recording cannot replay a reset the host performs, so none is ever carried
across one. The power-on cold boot, the host's F4 soft reset and starting a
playback all go through `Emulator::end_rzx_at_reset()`, which **writes** the
running recording and ends it there, with a warning — the choice FUSE makes
on a menu reset or on opening a file. (A reset the program asks for itself,
through NR 0x02, replays like any other instruction and ends nothing.) jnext's
reader and writer handle one snapshot and one input sequence per file, so
continuing across the reset with a second snapshot block — which the RZX format
allows — is not an option they offer. A file from elsewhere that does continue
from a second snapshot (FUSE writes one for an inserted snapshot) is played up
to it: `rzx::parse()` stops there and counts the rest in
`RzxRecording::later_snapshots`, and `load_rzx()` warns.

## Media out

| What | Flag / UI | Code |
|---|---|---|
| PNG or `.SCR` screenshot | `--delayed-screenshot`, File ▸ Save Screenshot, File ▸ Quick Screenshot | `src/platform/screenshot.*` |
| WAV of the mixer | `--wav-record` | `src/audio/audio_recorder.*` |
| DAC activity CSV | `--dac-trace` | `src/audio/dac_trace_recorder.*` |
| MP4 with audio | `--record`, File ▸ Record MPEG4 Video | `src/core/video_recorder.*` |

**Two formats, and the extension is the only selector.** There is no format
flag and no format argument anywhere: `screenshot_format_for_path()` reads the
filename's last extension, and `save_screenshot()` is the single dispatch every
caller goes through — the three frontends' `--delayed-screenshot` handlers and
both GUI menu entries. Adding a third format means adding it there, once.

PNG screenshots are always written at double height — each framebuffer row is
emitted twice — so that a 640×256 frame becomes a 640×512 PNG with square
pixels and the aspect ratio a viewer expects.
`--delayed-screenshot-layers` narrows a capture by clearing layer enables in
the renderer rather than by masking the result, so the layers that remain still
follow NR 0x15 priority; see [3.3 Video](03-video.md).

`.SCR` is not a rendering at all. `Ula::screen_dump()` copies the ULA's own
storage through `fetch_vram_bank()` — the same function the pixel and attribute
fetches use — so the file is the bytes the display is reading, not a conversion
of the composited picture. Bank and layout come from the live register state
exactly as the renderer derives them: the port 0x7FFD shadow bit alone chooses
bank 5 or bank 7 (`zxnext.vhd:6649-6656`), and port 0xFF bits 2:0 choose the
window, masked back to the standard mode while shadow is on
(`zxula.vhd:191`). Standard and Timex-alt give 6912 bytes; hi-colour and
hi-res give 12288, the two 6144-byte planes the ULA fetches. Nothing else in
the machine is representable, so a Layer 2 or tilemap program captured this way
yields whatever is left in the ULA screen memory — a documented limitation of
the format, not of the dump.

`--delayed-screenshot-layers` is *refused* with a `.scr` target rather than
ignored (`src/main.cpp`), for the same reason it is refused without a
screenshot at all: an option that silently does nothing is worse than an error.

Both writers treat a failed `fclose()` as a failure. That is not defensive
noise: a screenshot is small enough to sit entirely in stdio's buffer, so on a
full disk the only write(2) happens at the close, and ignoring its result is
precisely how a truncated file gets reported as saved.

**Quick Screenshot** (File ▸ Quick Screenshot, Alt+K) is the same write with no
dialog: `auto_screenshot_path()` builds `jnext-YYYYMMDD-HHMMSS<ext>` in the
configured directory, creating it if needed and appending `-02`, `-03`… while
the name is taken. Its directory and format are separate config fields from the
Save-Screenshot dialog's remembered directory, deliberately — that one follows
the dialog around, and a no-dialog capture whose destination moves is
unfindable.

Video recording needs **FFmpeg on the host**. jnext writes raw ARGB frames and
raw stereo PCM to temporary files during the run, so the hot loop pays no
encoding cost, and shells out once at stop, trying `libx264`, `mpeg4` and
`libopenh264` in that order. The command line is built for two dialects — POSIX
`sh` and Windows `CreateProcess` — and both are compiled everywhere, so both
stay unit-testable on any host.

## The SD card is two independent things

jnext talks to the same `.img` file in two entirely different ways, at two
different times, for two different consumers. The split looks redundant until
you notice that the two consumers want opposite things.

**`src/core/sd_rom_extractor.*` is a host-side FAT32 reader.** Before the
machine can start, jnext needs the ROM images that on real hardware live as
files on the card, and there is no guest yet to ask for them — so the emulator
opens the `.img` *as a filesystem* and pulls them out by path:
`/MACHINES/NEXT/48.rom`, `enNxtmmc.rom`, `enNextMf.rom`. It is read-only,
handles MBR + FAT32-LBA only, and does short-name lookup only, which is all
that job needs.

**`src/peripheral/sd_card.cpp` is emulated hardware.** Once the machine is
running, the guest wants a card, not a filesystem: NextZXOS carries its own
FAT driver and expects to do the filesystem work itself, over SPI, one 512-byte
block at a time. That class implements an SD card in SPI mode behind the
`SpiDevice` interface and serves those blocks over ports `0xE7`/`0xEB` — see
[3.6 Peripherals](06-peripherals.md). It knows nothing about FAT32.

The two never talk to each other: one serves the host at init time, the other
the guest at run time. The card is opened read-write and guest writes persist,
so booting the machine mutates the image; `--sdcard-readonly` makes the guest
see a write-protected card instead.

Provisioning is a third, separate concern. `sdcard_provisioner.*` locates or
downloads the canonical image and re-clusters a copy via `fat32_image.*`,
because the shipped 1 GB image has too few clusters to be a spec-valid FAT32
and the Next firmware's own FatFs — correctly — rejects it.
