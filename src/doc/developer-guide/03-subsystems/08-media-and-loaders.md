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
| `.sna` | yes | yes | `sna_loader.*`, `sna_saver.*` | Reads 48K and 128K; writes 48K only |
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
on hardware. All three tape formats support it. The EAR signal is also routed
into the audio mixer, which is why you hear the loader as well as see it.

Getting BASIC to *start* loading is a third mechanism again. The phantom typist
in `src/input/phantom_typist.h` types `LOAD ""` for you once it can prove the
ROM's input loop is running — see [3.7 Input](07-input.md) for how it decides
that. It is armed by `load_tap()` only; `.tzx` and `.wav` still fall back to a
fixed 100-frame delay.

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
below — and the program streams them. With 0, both loaders read the declared
banks and close the file, with no size check at all (`nexload2.asm:390-395`,
`nexload.asm:547-551`), so the bytes are dead. jnext matches that: the program
loads and runs, and more than 16 KB of dead bytes logs a warning (GH #250,
found on Spectron2084). That warning replaces the refusal issue #10 added,
which had turned such a file away. No host bridge is opened for such a file,
so the stand-in cannot reach the bytes either: its `F_OPEN` then knows only
the in-memory file.

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
  cleared by `reset()` and `soft_reset()`. `load_sna()`, `load_szx()` and
  `load_z80()` reset, and RZX playback loads its embedded snapshot through the
  same loaders (`load_snapshot_from_memory()`), so a snapshot clears it. `load_tap()`, `load_tzx()` and `load_wav()` deliberately do not:
  they attach tape media to the running machine, so a NEX still running keeps
  its stand-in.
- `EmulatorConfig::esxdos_stub` (`--esxdos-stub`), for the whole session.
- The extended-NEX host bridge (`extended_nex_host_`), open when the NEX
  header's `file_handle` is non-zero. Only `load_nex()` opens it, so it never
  exists without `direct_nex_esxdos_`; reset and soft reset close it.

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

**Known gaps.** A directly loaded program cannot reach host files other than
the kept-open NEX and its siblings; a host directory for it is
[#31](https://github.com/jorgegv/jnext/issues/31), not implemented. The
kept-open handle starts at file position 0 rather than after the last bank, as
`nexload` leaves it
([#267](https://github.com/jorgegv/jnext/issues/267)). There is one
in-memory file, with no `F_SEEK`. `M_DOSVERSION` reports 1.94 without the host
bridge.

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
directions. jnext embeds a 48K SNA, which is the reason `SnaSaver` exists at
all. On playback the embedded snapshot — SNA, SZX or Z80 — is parsed straight
from the file's bytes by `Emulator::load_snapshot_from_memory()`, using the
`load_from_buffer()` entry each of those loaders has beside its file-path
`load()`; nothing is written to a temporary file. Any other snapshot type fails
the load: input replayed against a machine it was not recorded on reproduces
nothing.

A recording cannot replay a reset the host performs, so none is ever carried
across one. The power-on cold boot, the host's F4 soft reset and starting a
playback all go through `Emulator::end_rzx_at_reset()`, which **writes** the
running recording and ends it there, with a warning — the choice FUSE makes
on a menu reset or on opening a file. (A reset the program asks for itself,
through NR 0x02, replays like any other instruction and ends nothing.) jnext's
reader and writer handle one snapshot and one input sequence per file, so
continuing across the reset with a second snapshot block — which the RZX format
allows — is not an option they offer.

## Media out

| What | Flag / UI | Code |
|---|---|---|
| PNG screenshot | `--delayed-screenshot`, File ▸ Save Screenshot | `src/platform/screenshot.*` |
| WAV of the mixer | `--wav-record` | `src/audio/audio_recorder.*` |
| DAC activity CSV | `--dac-trace` | `src/audio/dac_trace_recorder.*` |
| MP4 with audio | `--record`, File ▸ Record MPEG4 Video | `src/core/video_recorder.*` |

Screenshots are always written at double height — each framebuffer row is
emitted twice — so that a 640×256 frame becomes a 640×512 PNG with square
pixels and the aspect ratio a viewer expects.
`--delayed-screenshot-layers` narrows a capture by clearing layer enables in
the renderer rather than by masking the result, so the layers that remain still
follow NR 0x15 priority; see [3.3 Video](03-video.md).

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
