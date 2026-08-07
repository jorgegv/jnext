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
all.

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
