# 3.8 Media and loaders

Everything under this heading lives in `src/core/`, and almost none of it is
emulated hardware. A real Spectrum has a tape port and an SD card; it has no
notion of a `.sna` file. These loaders exist because a *developer* needs to get
code into the machine and state out of it, so they are host-side conveniences
that reach directly into the emulator's RAM, registers and MMU. Only two things
here are hardware the guest could tell apart from the real thing — the
real-time tape signal and the SD card's SPI transport — and both are called out
below.

## What jnext can read and write

| Format | In | Out | Where | Notes |
|---|---|---|---|---|
| `.nex` | yes | yes | `nex_loader.*`, `nex_saver.*` | Next-native. V1.0–V1.3 |
| `.sna` | yes | yes | `sna_loader.*`, `sna_saver.*` | Reads 48K and 128K; writes 48K only |
| `.szx` | yes | yes | `szx_loader.*`, `szx_saver.*` | Writes only 48K/128K/+3; refuses Next |
| `.z80` | yes | — | `z80_loader.*` | v1/v2/v3, 48K and 128K |
| `.tap` | yes | yes | `tap_loader.*`, `tap_saver.*` | Save is a ROM `SA-BYTES` trap |
| `.tzx` | yes | — | `tzx_loader.*` | Wraps ZOT (`third_party/zot`) |
| `.wav` | yes | — | `wav_loader.*` | Real-time only, no fast path |
| `.rzx` | yes | yes | `rzx_player.*`, `rzx_recorder.*` | Input recording, not state |
| raw binary | yes | — | `Emulator::inject_binary` | `--inject` + `--inject-org`/`--inject-pc` |

Format selection is by **file extension**: for `--load` in `src/main.cpp`
around line 890, and again in the GUI's file dialogs and the headless snapshot
writer. There is no content sniffing — an extension jnext does not recognise is
a hard error naming the supported set, rather than a guess.

The snapshot and program loaders follow the same two-phase shape —
`load(path)` parses and validates into member state, `apply(emu)` writes it
into the machine — so a malformed file fails before the machine has been
touched. Several of them additionally define their parse and decompression
logic **inline in the header** (`z80_loader.h`, `tap_loader.h`, `nex_loader.h`,
`tap_saver.h`), for one reason: it lets test tiers that cannot link
`jnext_core` exercise the real parser. See [chapter 4](../04-testing/index.md).

## Tapes: two completely different mechanisms

A tape can reach the guest in one of two ways, and they share nothing.

**Fast load** is a ROM trap. When ROM is paged into slot 0 and PC reaches the
48K ROM's `LD-BYTES` entry at `0x0556`, the run loop hands the next block
straight to memory and skips the routine — no tape signal is ever generated.
`.tap` and `.tzx` both support this; `.wav` cannot, because a WAV is audio
samples with no block structure to extract. Saving works the same way in
reverse: `--tape-save` arms `TapSaver`, which traps `SA-BYTES` at `0x04C2` and
appends a TAP block. That trap is gated on a **ROM identity check** as well as
the PC, because other ROMs legitimately execute at `0x04C2` — a plain PC gate
fired ten times during an ordinary NextZXOS boot.

**Real-time playback** (`--tape-realtime`) is the honest one: the loader drives
the EAR bit per T-state and the ROM's own loading routine decodes it, so the
border stripes and the loading noise are real. All three tape formats support
it. The EAR signal is also routed into the audio mixer, which is why you hear
it.

Getting BASIC to *start* loading is a third mechanism again:
`src/input/phantom_typist.h` waits for the ROM to poll all eight keyboard
half-rows — proof BASIC's input loop is running — before injecting `LOAD ""`.
It is armed by `load_tap()` only; `.tzx` and `.wav` still use a fixed 100-frame
delay.

## NEX

`.nex` is the Next's native program container and the most involved loader
here: four header versions, optional Layer 2 / LoRes / tilemode loading
screens, palettes, a Copper block, bank ordering and a CRC-32C.

Its **oracle is Ped7g's `nexload2.asm`**, not the distribution's own
`nexload.asm` — the distro loader refuses V1.3 outright, so nexload2 is the only
executable specification for that version. `nex_loader.cpp` cites it line by
line, and the places where the two loaders genuinely disagree (the palette-block
rule, the V1.3 delay model, the loading bar) are documented as deliberate
divergences rather than silently reconciled.

`NexSaver` writes V1.2, and its class comment enumerates what the format cannot
carry — no register file beyond PC and SP, no NextREG state, no MMU slots 0–5.
Read that list before treating a NEX round trip as a snapshot.

A directly loaded NEX can also keep its own file handle open and stream from
itself; `extended_nex_host.*` presents the host file to the guest as a synthetic
block-addressed SD extent so NextZXOS's file APIs work against it.

## RZX

RZX records *inputs*, not state: an embedded snapshot plus, per frame, the
instruction count and every value returned by an `IN`. Playback feeds those
values back through the port dispatch hook so the program takes the same path.
`rzx.h` holds the format; `rzx_player.*` and `rzx_recorder.*` the two
directions. jnext embeds a 48K SNA, which is why `SnaSaver` exists at all.

## Media out

| What | Flag / UI | Code |
|---|---|---|
| PNG screenshot | `--delayed-screenshot`, File ▸ Save Screenshot | `src/platform/screenshot.*` |
| WAV of the mixer | `--wav-record` | `src/audio/audio_recorder.*` |
| DAC activity CSV | `--dac-trace` | `src/audio/dac_trace_recorder.*` |
| MP4 with audio | `--record`, File ▸ Record MPEG4 Video | `src/core/video_recorder.*` |

Screenshots are always written at double height — each framebuffer row emitted
twice — so a 640×256 frame becomes a 640×512 PNG with square pixels.
`--delayed-screenshot-layers` narrows a capture by clearing layer enables in the
renderer, so the remaining layers still follow NR 0x15 priority (see
[3.3 Video](03-video.md)).

Video recording needs **FFmpeg on the host**. jnext writes raw ARGB frames and
raw stereo PCM to temporary files during the run — no encoding cost in the hot
loop — then shells out once at stop, trying `libx264`, `mpeg4`, `libopenh264`
in that order. The command line is built for two dialects (POSIX `sh` and
Windows `CreateProcess`), both compiled everywhere so both are unit-testable.

## The SD card is two independent things

**`src/core/sd_rom_extractor.*` is a host-side FAT32 reader.** At startup the
emulator opens the `.img` file *as a filesystem* and pulls named ROMs out of it
by path — `/MACHINES/NEXT/48.rom`, `enNxtmmc.rom`, `enNextMf.rom`. It is
read-only, MBR + FAT32-LBA only, and short-name lookup only. This mirrors real
hardware, where those ROM images also live on the card.

**`src/peripheral/sd_card.cpp` is emulated hardware.** It implements an SD card
in SPI mode behind the `SpiDevice` interface and serves 512-byte blocks to the
guest over ports `0xE7`/`0xEB` (see [3.6 Peripherals](06-peripherals.md)). It
knows nothing about FAT32; NextZXOS's own driver does the filesystem work.

They point at the same file and never talk to each other: one serves the host at
init time, the other the guest at run time. The card is opened read-write and
guest writes persist, so a boot mutates the image — `--sdcard-readonly` makes
the guest see a write-protected card instead.

Provisioning is a third, separate concern: `sdcard_provisioner.*` locates or
downloads the canonical image and re-clusters a copy via `fat32_image.*`,
because the shipped 1 GB image has too few clusters to be a spec-valid FAT32
and the Next firmware's own FatFs correctly rejects it.
