# 3.8 Media and loaders

This is the subsystem that gets code into the machine and gets state, pictures
and sound back out of it. Almost none of it is emulated hardware. A real
Spectrum has a tape port and an SD card; it has no notion of a `.sna` file, and
nothing inside it can write a PNG. These loaders exist because a *developer*
needs a way in and a way out, so they are host-side conveniences that reach
directly into the emulator's RAM, registers and MMU rather than going through
any bus the guest can see.

Everything under this heading except the POK trainers (below) lives in
`src/core/`, is driven from the frontend (a CLI flag at startup, a menu item
later) and drives `Emulator` — never the reverse. Only two things here are hardware the guest could tell apart from the
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
| `.tzx` | yes | yes | `tzx_loader.*`, `tape_recorder.*` | Read: wraps ZOT (`third_party/zot`). Write: tape-out capture decoded into 0x10/0x11/0x13/0x20 |
| `.wav` | yes | yes | `wav_loader.*`, `tape_recorder.*` | Read: real-time only, no fast path. Write: the tape-out signal, 8-bit mono 44100 Hz |
| `.rzx` | yes | yes | `rzx_player.*`, `rzx_recorder.*` | Input recording, not state |
| `.zip` | yes | — | `core/program_archive.*`, `save/zip_interchange.*` | Not a format: a container resolved to one program file at the two entry points (see ZIP archives) |
| raw binary | yes | — | `Emulator::inject_binary` | `--inject` + `--inject-org`/`--inject-pc` |
| `.pok` | yes | — | `platform/pok_trainers.*`, `gui/pok_dialog.*` | Not a loader: a list of trainers applied to the machine that is already running, through the debugger backend; Qt window only (see POK trainers) |

Format selection is by **file extension**: for `--load` in `src/main.cpp`
around line 890, and again in the GUI's file dialogs and in the headless
snapshot writer. There is no content sniffing of programs, and an extension jnext does not
recognise is a hard error naming the supported set rather than a guess at what
the file might be. The one exception is an archive (below), which is recognised
by `.zip` or by its `PK` magic and resolved to a program file first.

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

## Which machine a snapshot loads as

A `.sna`, `.szx` or `.z80` loads on the machine it was **saved** on (GH #323),
as a `.jns` always has. There is ONE table that maps a snapshot to a machine,
`snapshot_machine()` (declared in `src/core/snapshot_machine.h`, defined in `src/core/snapshot_machine.cpp`): a `.sna` by
size (49179 = 48K, longer = the 128K form, which libspectrum calls a Pentagon
but jnext's own saver writes from the 128K and +3), an `.szx` by machine ID, a
`.z80` by version and hardware mode. Each entry cites libspectrum 1.5.0 in the
source. It also returns the **refusals**: a machine jnext does not emulate
(Timex, Scorpion, SE, Didaktik, Pentagon 512/1024), an unknown `.z80` header
length (not 23, 54 or 55) or hardware mode, and a snapshot taken with the TR-DOS,
Interface 1, MGT or Multiface ROM paged in, each with a distinct reason naming
the machine. `rzx::recorded_machine()` (for an RZX's embedded snapshot),
`emulator_boot_machine()` and the three loaders all read this table;
`rzx::snapshot_machine()` is only a thin view of it.

The switch has two layers, and both are needed:

1. **Frontend cold boot** (File > Load, `--load`, Quick Load).
   `emulator_boot_machine()` returns the snapshot's machine, the cold boot builds
   it, and `ColdBootHooks::keep_machine` records it, so a later reset boots that
   machine. `emulator_startup_machine()` lets a snapshot beat an explicit
   `--machine` (`main.cpp` warns). Qt's `handle_load_path()` asks
   `snapshot_file_loadable()` before the cold boot, so a refused file costs no
   session.
2. **Core, in place** (every route, including debugger loads, which never
   reconstruct). `Emulator::load_sna/szx/z80` parse the file, ask the table
   (`switch_to_snapshot_machine_()`; a refusal returns false BEFORE the machine is
   touched), then `switch_machine_for_load_()` ends a running RZX recording (its
   file would name the wrong machine) and sets `config_.type`, and the usual
   in-place `init(config_)` builds it. `load_jns` uses the same helper. The
   frontends follow through `emulator_apply_load_keeping()`, whose `keep` callback
   stores the running machine as the frontend's own. `load_snapshot_from_memory()`
   (an RZX's embedded snapshot) does NOT switch: RZX has its own policy (the
   frontend boots the recorded machine and an explicit `--machine` wins).

What the loaders restore on the switched-to machine: the `.z80` +3 port `0x1FFD`
(byte 86; written BEFORE `0x7FFD`, because the `0x7FFD` lock bit closes `0x1FFD`,
`zxnext.vhd:3718`, and the `.szx` does the same), the AY registers of the 128K,
+3 and Pentagon (`.z80` bytes 38-54, the `.szx` `AY` chunk, through the AY
ports, `Emulator::restore_ay_registers()`), and NTSC 48K (`.szx` ID 15) as the 48K with NR `0x05` bit 2 (60 Hz).
The `.szx` `SCLD` chunk (Timex port `0xFF`) is deliberately ignored: libspectrum writes it only for Timex/SE machines (`szx.c:2782-2785`) and the machine table refuses all of those, so it never arrives on a machine jnext loads. The tests are
`snapshot_machine_test` (SNM-01..33, SNM-29 withdrawn), the QS-25..27/SNM-40..41 rows of
`emulator_boot_test`, PA-24 and the `snapshot-machine-func` regression row.

## Tapes: two completely different mechanisms

Loading from tape is the one place where the emulator offers you a choice
between fidelity and speed, so it is worth being clear about what the choice
actually is. On real hardware a game takes minutes to load, the border stripes
as it goes, and the loader chatters through the speaker. jnext can reproduce
all of that, or it can skip it entirely and have the program in memory almost
instantly. Those are two independent code paths that share nothing.

**Fast load is a ROM trap.** The trap sits at `LD-BREAK`, `0x056B`, the `RET NZ`
of the 48K ROM's `LD-BYTES` routine, not at the routine's entry `0x0556`: every
entry converges there, including the `0x0562` one a custom block loader uses after
doing the routine's prologue itself (FUSE traps the same spot). It fires only
with ROM paged into slot 0, the Z flag set (the ROM's `RET NZ` is then not about
to return) and a **ROM identity check** on the bytes at `0x056B` and `0x05DF`
(`tape_trap::ld_bytes_rom_present`, like the save trap's). It then hands the next
block to memory without generating any tape signal. `.tap` and `.tzx` both
support this; `.wav` cannot, because a WAV is just audio samples with no block
structure to extract.

Both loaders call one helper, `tape_trap::rom_ld_bytes` (`src/core/tape_trap.*`),
which does what the ROM does from `LD-START` to its return: the flag comes from
A' and LOAD/VERIFY from the carry of F' (the entry's `EX AF,AF'` has run); a flag
mismatch consumes the block and returns carry clear; LOAD stores bytes, VERIFY
compares them; the carry reports the parity of flag, bytes and the byte after the
last one (the checksum), and a DE longer than the block times out as the ROM does.
The CPU is left at the routine's own `RET` (`0x05E2`), so the ROM pops whatever the
caller pushed: `SA/LD-RET` for a `0x0556` entry (border restored, `EI`), the
caller's return address for a `0x0562` one. The registers it leaves are those of
the ROM run against jnext's own real-time player, except the ROM's edge-tracking
byte C, whose value after the sync depends on the pilot phase the ROM caught and
which the trap pins to `0x21`. A custom or turbo loader that never enters the ROM
routine is never intercepted, and real time is then the only way it will load.

**A fast load also runs the machine uncapped**, so the BASIC between the blocks
does not cost real time: `Emulator::fastload_active()` drops the frame pacing (the
Qt frontend runs up to 200 frames per tick, the SDL one skips its delay) and the
audio. Like FUSE's `timer_fastloading_active()`, it holds only while something is
loading, never because a tape is merely attached (GH #408): while the phantom
typist waits, for at most `PhantomTypist::FASTLOAD_WAIT_FRAMES` (1000) frames, or,
for a fast tape with blocks left, while the keyboard auto-type is typing its
`LOAD ""` or within `FASTLOAD_TAPE_IDLE_FRAMES` (50) frames of the last block the
trap served. Before that bound, a tape nobody read (opened at a NextZXOS screen,
or a second program on it) kept the sprint for the whole session: the window
answered once a burst, and one key tap lasted a burst, long enough for the ROM to
auto-repeat it dozens of times. Both bounds are measured (`tap_loader_test`
TAPF-09..12): the ROM's own work between trapped blocks is 0-1 frames, and a
loader `PAUSE` past the window runs only its tail at 1x, the next block still
trapped at once; the typist's first fire at power-on is frame 63-92 on the
48K/128K/+3, 92 on a Next started with `--load`, and 311 on a cold Next, which
runs the FPGA bootloader and tbblue firmware first (`init()` overlays
`nextboot.rom` only when no `load_file` is set). A reset, Tape > Eject, a newly attached tape (`.tap`, `.tzx`
or `.wav`) and any state restore (a rewind, a debugger state load, a `.jns`)
each close the window.

**Automatic play/stop (GH #395)** is a host deck feature with no hardware
counterpart (no Spectrum has tape motor control). The signal is
`TapePollDetector` (`src/core/tape_poll_detector.h`): every CPU `IN` of the ULA
port while `cpu_.executing()` (the debugger's port reads are not counted),
stamped with `tape_sample_tstates()`. 32 reads in a row, each at most 1000
T-states after the previous, with the last no more than 1000 T-states old, is
"polling". 1000 T-states is over the shortest standard pulse (855), and 32 is
over the ROM's keyboard scan (bursts of 8 reads 59 T apart, then about 69000
T-states of nothing). There is no PC range (it would serve only ROM loaders
and differ by ROM) and no register rule: FUSE's "B changes by one" start rule
cannot see DeciLoad, which polls one address with B constant, and FUSE's own
detect-loader fails the `xevious-deciload` tape.

- **Real time.** The deck starts at attach as ever. It holds only at a gap
  boundary, the instant a block and its non-zero pause have finished, and only
  when nobody polls. It never stops mid-block and never at a zero-pause join. It
  resumes at the first counted read once polling: the next block from its first
  pulse. TAP: `TapLoader::set_gap_hold` / `resume`. TZX: ZOT's `hold_request`,
  `held`, `tzx_resume()`; a stop block (0x20 pause 0, 0x2A) holds instead of
  ending the tape, and 0x2A stops only on a 48K machine. WAV has no gaps.
- **Fast mode.** Nothing happens: a polling program never starts a tape that
  is not playing, because in fast mode the `0x056B` trap serves the loads and a
  "press any key" wait (a tight `IN` loop, indistinguishable at the port from a
  pure-EAR loader such as DeciLoad) would play blocks away that a later ROM load
  needs. A custom loader in fast mode behaves as it always did.
- **State.** The detector and the hold are host-deck state, not serialised, and
  reset on every state load (tape position is excluded from rewind by design;
  a `.jns` does carry it, see below). Off (`tape_auto_play`, `--no-tape-auto-play`) is the
  deck as it was: no hold, stop blocks stop for good. The one exception is
  independent of the setting: a 0x2A block now stops only on a 48K machine (the
  TZX specification).

**Deck position (GH #402).** Each loader can say where it is and be put back
there, for a `.jns`: `deck_state()` returns a plain struct (TAP: block, phase,
pulse and byte counters; TZX: the fast-load cursor and the whole ZOT player,
file pointers as byte offsets and the next edge as a distance from "now"; WAV:
playing and the start as a distance from "now"), and `restore_deck()` checks
every value against the loaded bytes before it writes any (the checks are the
read sites of `tzx.c`), returning false with a reason otherwise. `sha256()` is
the digest of the bytes `load()` read. `handover_to_fast_at_gap()` and
`start_realtime_from_fast()` convert a restored deck to the session's mode
without cutting a block. See [save state and rewind](../02-architecture/05-save-state-and-rewind.md).

Saving works the same way in reverse. `--tape-save` (or Tape ▸ Start Saving,
both through `Emulator::start_tape_save()`) arms one of two savers by the
file's extension, and in both the ROM's `SA-BYTES` at `0x04C2` is trapped. That
trap is gated on a **ROM identity check** as well as on the PC, because other
ROMs legitimately execute code at `0x04C2` — a plain PC gate fired ten times
during an ordinary NextZXOS boot.

- **`.tap`** (any name not `.tzx`/`.wav`): `TapSaver` appends the trapped block.
  Nothing else is captured; a TAP cannot hold an arbitrary signal.
- **`.tzx` / `.wav`**: `TapeRecorder` (`src/core/tape_recorder.*`) records the
  **tape-out signal** as timed edges, and the trap hands it trapped blocks as
  events in the same time-ordered stream. Tape-out is the MIC jack, `o_AUDIO_MIC
  <= beep_mic_final` (`zxnext.vhd:1638`), with `beep_mic_final = i_AUDIO_EAR xor
  (port_fe_mic and nr_08_keyboard_issue2) xor port_fe_mic` (`zxnext.vhd:6503`);
  `Emulator::tape_out_level()` computes it from the same `i_AUDIO_EAR` model the
  port 0xFE read uses, so with no tape playing it is port 0xFE bit 3, and a tape
  playing in real time is echoed. It is sampled at each port 0xFE write (a CPU
  OUT at its bus request edge, a DMA transfer at its place in the burst), while
  a tape plays at the end of every instruction, and once at the end of every
  frame, against `Clock::get()`: emulated 28 MHz master cycles, so CPU speed
  (NR 0x07) changes the pulse lengths exactly as on the board, and a trapped
  block cannot be double-written, because the trap skips the routine whose
  pulses would otherwise reach the line. The TZX writer cuts the stream into
  segments at gaps over 65535 T-states and decodes each: pilot (≥256 equal
  pulses) + sync + two-class bit pairs becomes 0x10 at the ROM's timings and
  0x11 otherwise; everything else becomes exact 0x13 pulses and a 0x20 pause.
  The WAV writer needs no decoding; it synthesises the ROM's pulse train for a
  trapped block. Nothing is recorded while a rewind replays frames. Unarmed, all
  of this costs one flag test per port 0xFE write and nothing per instruction:
  a TZX / WAV save arms the TAP saver's flag too, so the trap test is unchanged.
  The file survives a kill: trapped blocks are written at once and segments once
  they go quiet, the last pause WORD patched in place later, the WAV header
  refreshed, and the file flushed every frame.
  `doc/design/TAPE-SAVE-PLAN.md` has the rules and their reasons.

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
when it reads that block. Those anomalies are warnings, and `parse_blocks()` reports
only the first `kMaxWarnings` (20) plus one count of the rest: a 256 MiB tape of
zeros is 134 million zero-length blocks (TAPC-13). A tape is also refused above
`kMaxBlocks` (1 Mi) blocks: a `TapBlock` costs 40 bytes before its payload, so a
file of 4-byte blocks took ~12x its size in memory (TAPC-15).
`TzxLoader::validate()` does the same for a `.tzx`
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
identity that invalidates it — the SD image's size and mtime, the machine
type, a state-format version, and the exact *uncompressed* stream length. Any mismatch
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
two ROM-bank bits after the restore. Those are direct MMU calls, so they skip
what the 0x7FFD / 0x1FFD port handlers push after paging, and of that the
DivMMC's ROM 3 feeder (`DivMmc::rom3_active_`, VHDL `sram_rom3` into
`sram_divmmc_automap_rom3_en`, `zxnext.vhd:2981-3008, 3138`) is the one input
the two bits change; the handover re-syncs it (GH #301). Without that,
`load_state()` left it at the recording's ROM 0, and since NextZXOS leaves NR
0xB9 = 0x00 (RST $08 valid only with ROM 3) every ROM3-gated automap entry
point stayed off until the program's first 0x7FFD / 0x1FFD write. It is deliberately NOT in
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

**The cost.** A miss costs a 500-frame boot, ~3.5 s headless. A hit costs one
`stat` of the SD image plus inflating the ~100 KB cache. The key is the image's
(size, mtime) read after the recording, plus a process-wide write generation
(`SdCardDevice::host_write_generation()`, bumped by every CMD24 jnext commits):
an own write drops the in-memory state, and a memo of the last validation
catches an own write that left the mtime unchanged. GH #396 (owner, 2026-10-22)
replaced the whole-image SHA-256 this used to pay (0.6-1.2 s per GB on the UI
thread at every cold boot that loads a program). **Accepted residual:** an
image replaced or edited from outside with the same size and the same mtime
(`cp -p`, `touch -r`) serves a stale recording; `--warm-start-regenerate` is the
escape hatch. The file magic is `JNEXTWS3` (layout generation).

**Tests.** `test/warm_start/warm_start_test.cpp` (`WSC-*` for the cache file,
its four invalidation keys and the compressed payload; `WSR-*` for the
residency criterion, the announced fallbacks and `WSR-DEF-01`, which proves the
path is reached with no flag set; `WSR-ROM3-01/02` for the ROM 3 feeder and
the RST $08 automap it gates) plus the `warm-start-func` regression row for
the end-to-end record → cache → restore round trip, which needs a real SD image
and so cannot be a unit test.

## The esxDOS stand-in for directly loaded programs

On hardware a NEX is always started by NextZXOS's `nexload`, so the program
can call the esxDOS / NextZXOS API: `RST $08` followed by a one-byte call
number, reached through the DivMMC automap (see
[3.6 Peripherals](06-peripherals.md#divmmc)). A direct load skips `nexload`.
Before the warm start it skipped NextZXOS altogether, and `$0008` held the 48K
ROM's ERROR-1 restart, so an unanswered call crashed the program: Warhawk's
first call, `M_GETSETDRV`, ended in a DI + HALT at `$1303` that way (GH #250).
jnext therefore answers these calls on the host,
the way CSpect (`esxDOS.dll`) and ZEsarUX (`esxdos_handler`) always do. Neither
emulator is an oracle for this: both fake every `RST $08`, DivMMC or not.
Since the warm start a NextZXOS is usually resident behind the program, and
jnext's answers still come first: that is the documented design (jnext(1),
"esxDOS calls from a directly loaded NEX"), because the NEX is a host file
NextZXOS cannot see. A program that needs NextZXOS's own file calls is started
from NextZXOS, or run with `--esxdos-stub-root` (GH #301).

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

**Exact answers, or refusals.** A listing mode is served only where the answer
is exactly NextZXOS's, and refused with `esx_enosys` everywhere else: a caller
that asked for a filtered or sorted listing and silently got neither is worse
off than one told no. What NextZXOS answers was measured (GH #301) with a probe
NEX launched from its Browser, listing one directory in every mode:

- `esx_mode_sf_enable` with no exclude bit and no `esx_sf_sort_enable` in C
  (C = `$00`, and `$07`: the sort-key and reverse bits are inert without
  sort_enable) is the plain listing, entry for entry. NXModPlayer opens every
  listing after its first with `$B0` (sf_enable, wildcards, LFN; C = 0).
  Exclusion and sorting are refused.
- The wildcard that filters is the one passed to **F_READDIR**, not
  F_OPENDIR's: opened `*.TXT` and read `*.*` the listing is complete, and the
  reverse is filtered. NextZXOS matches it against the 8.3 name (`*` lists only
  names with no extension), so `*.*` matches every entry. So F_OPENDIR accepts
  the wildcard bit, and `readdir()` serves `*.*` and refuses any other pattern.
  The bit travels in the rewind snapshot's mode byte.
- F_OPENDIR returns C = 0 ("sort operation not completed") whatever C it was
  given.
- `esx_mode_use_header` stays refused: a host file has no header.

"Exactly" governs WHICH listing modes are served, i.e. which entries a listing
returns. Entry order (host-name sort, not FAT directory order), name spelling
(the host's case: NextZXOS lists a lower-case 8.3 FAT name as `A.TXT`, the stub
`a.txt`) and the attribute bits (read-only on a read-only mount, hidden for a
leading-dot name) follow the host and cannot be reproduced from it; lookup is
case-insensitive, so no path call is affected.

`F_GETCWD` answers in NextZXOS's form, `C:/` then each component's 8.3 name and
a `/` (`C:/AAA/T/SUBDIR~1/` was measured for an LFN of "sub dir long"), and
with A = `$FF` for the filespec in DE. Since the guest then builds paths out of
8.3 names, `resolve()` accepts an entry's 8.3 name (in any case) as well as its
host name; the 8.3 synthesis follows FAT's basis-name rule, including stripping
leading dots first (`.git` is `GIT~1`, `.hidden` is `HIDDEN~1`, as measured);
`list_host_dir()` is the one place a listing and its short names are made, so
F_READDIR, F_GETCWD and `resolve()` cannot disagree on one. The error codes for
bad paths are NextZXOS's measured ones too: a missing or non-directory
component before the last is `esx_enotdir`, as is a missing directory for
F_OPENDIR and F_CHDIR; a missing last component of F_OPEN / F_STAT is
`esx_enoent`; F_OPEN and F_STAT of a directory are `esx_einval`.
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

**Tests.** `HFS-01..122` in `test/esxdos_stub/esxdos_hostfs_rows.cpp` (linked
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
directions. The snapshot a recording embeds is an SZX on the 128K and +3
(`SzxSaver` — all eight banks and the paging ports) and an SNA on the 48K
(`SnaSaver::save()`, where the CPU view IS the machine).

**RECORDING IS REFUSED ON A NEXT** — `Emulator::rzx_refused_by_machine()`, owner
decision 2026-09-26 (GH #274), and the derivation with its measurements lives at
that definition. In short, an RZX holds a classic-Spectrum snapshot AND a
per-frame log of IN VALUES WITHOUT THE PORTS THEY CAME FROM, and a Next fails
both halves. The snapshot: none of SNA, SZX or Z80 describes a Next, so a 48K SNA
of one — which jnext embedded until this change — holds no NextREGs, Layer 2,
tilemap, sprites or Copper, and a recording replayed correctly only when the
program happened to redraw its display every frame. The log: because it stores
values and not ports, the snapshot and the log must agree on the guest's exact
sequence of port reads, and nothing can detect it when they do not. Embedding a
`.jns` — the one format that CAN represent a Next — was built and measured
against that: the replayed guest made two reads the recording never captured
(0x00E3, 0x243B), every value was consumed two positions early, and the guest
wedged in a HALT with IFF1 clear.

**Two facts from that experiment are worth keeping, because nothing in the tree
demonstrates them any more.** First, a `.jns` restore is faithful enough to RUN
and not bit-exact enough to REPLAY: at the first `run_frame` the CPU state
matched the recording exactly and the same snapshot animates correctly under
`--load`, yet from that identical PC the first port read differed. Do not assume
`.jns` is replay-grade. Second, the DMA was outside the log: `dma_.read_io` was
wired to `port_.read()`, which has no RZX hook, so a DMA port read was neither
recorded nor replayed. GH #283 moved it to `PortDispatch::guest_read()`, the
RZX-aware read the CPU's `IN` also takes (`in()` forwards to it). `read()` stays
hook-free on purpose: it is also every tool's read — the debugger's `port_in()`
— and a tool's read is not input. `port_in()` is refused (`RefusedRzx`) while an
RZX records or plays, as `port_out()` is: it dispatches the live handler, and a
port read's side effects (a UART RX pop, the SPI shift, a Multiface strobe) are
a change to the machine no recording can carry. On the playback branch
`guest_read()` still raises the I/O watchpoint and latches the CAP-EVT `Port`
event, with the replayed value, for the CPU's read and the DMA's alike. The DMA keeps its own timing; the CPU's bus
cycle (`fuse_z80_readport()`) is around `in()`, not in `guest_read()`.

Replay then agrees with the recording only while the DMA makes the same reads in
the same order, and it does, given the same machine: whether a slot is a DMA
burst or a CPU instruction (`step_one_instruction()`), how many bytes a burst
moves (`Dma::execute_burst()`: block length, prescaler, `dma_delay`, the bus
handshake) and which port each read hits are all functions of emulated state,
which the replay rebuilds from the snapshot and the logged values. jnext's
player delimits frames by emulator frame, not by the fetch counter, and the
counter counts only CPU instructions, so DMA reads land in the frame they
happened in on both sides. The gap is the snapshot: SNA and SZX hold no DMA
registers and no NextREGs, and playback's `init()` resets the DMA. A transfer
programmed before the recording started therefore replays from a reset DMA and
goes out of step — documented for users in the RZX section of the user guide.
`rzx-dma-func` (fixture `demo/rzx_dma_demo`) and `dma_test` GH283-01..12 pin it.

Playing a Next recording an older jnext wrote is NOT refused: `load_rzx()` warns,
names what the embedded snapshot cannot restore, and plays. The file is a fait
accompli and there is something to see — a program living in the 48K view replays
correctly, which is why this went unnoticed.

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
the snapshot cannot always: a pre-GH #274 Next recording embeds a 48K SNA, which
names no machine. `rzx::recorded_machine()`
reads that marker first and otherwise judges a recording by its snapshot
(`snapshot_machine()`, the same single table the snapshot loaders use: SNA size,
SZX machine ID, `.z80` version and hardware mode); a pre-marker jnext recording with an SNA names no machine.
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

## POK trainers

A `.pok` file (the World of Spectrum format: `N` name, `M`/`Z` pokes of
`bank address value original`, `Y` end) is a list of cheats for one game. It is
the one input here that is not loaded into a fresh machine: it edits the machine
that is running, so it is written as a **client of the debugger backend**
(`jnext::pok`, `src/platform/pok_trainers.*`, namespace `jnext::pok`) and holds no
`Emulator*`. Every read is `Debugger::peek` and every write is
`Debugger::poke(CLIENT_NONE, MemSpace::page(p), ...)`, which is what gives the
trainers, with no code of their own, the RZX refusal (a recording would diverge),
the rewind wall (`note_mutation()`: Step Back across a trainer is refused, as for
any debugger edit), the beam-replay logging of a page store (`log_page_store()`)
and a `MUTATE` line per byte. A second write path (a direct `Mmu` or `Ram` store)
would lose each of those; do not add one.

The module has no Qt, so `pok_trainers_test` runs in `make unit-test`,
`unit-test-sdl` and `unit-test-win`. The Qt side is `gui/pok_dialog.*` (a view
of a `Session`) and `MainWindow::open_pok_file()`, the seam both Machine ▸ POK
Trainers… and File ▸ Load of a `.pok` end in; `pok_dialog_test` drives the real
menu action, file picker, dialog and value prompt. `--load x.pok` keeps its
"unsupported file extension" refusal: a `.pok` is not a program. The runs that
have no window (headless, and the SDL-only window) take one through two options,
`--delayed-pok-frames N FILE` and `--pok-trainers LIST` (below); a script
(`set mem[a]`, `set phys[p, o]`, `save_snapshot`) still does what the options do
not, and the recipe is in the user guide (5.11).

**From the command line.** Two rows in `cli_options.h`, not one, because the
table's arity is at most 2 (CLI-TBL-03) and because "when, and which file" and
"which trainers" are different things; `main.cpp` refuses either without the
other, or either twice. The parse arms only store. Below the loop `main()`
reads and checks everything that can be checked before the machine exists, so a
typo in a CI script fails at once with status 1: `parse_picks()` turns the list
(`1,3=9,7`: a trainer number from 1, optionally `=VALUE`) into `Pick`s,
`make_cli_request()` loads the file and checks the picks against it (the trainer
exists and is valid, a value is given exactly for a trainer that leaves one
open) into a `CliRequest`. The Qt window has the dialog, so `main()` refuses the
options there (`#ifdef ENABLE_QT_UI`, not headless). `configure_and_run` hands
the request to `HeadlessApp::set_delayed_pok()` or `SdlApp::set_delayed_pok()`;
`QtApp` has none, and `if constexpr` leaves the call out of its instantiation.

Each of the two loops holds a `jnext::pok::CliJob`: the request and a
`cli::Delay::frames(N)`. `poll()` runs once per tick, after that tick's `--load`,
`--inject` and `--rzx-record` handling and before its frame, so `--load x.sna`
with N = 0 applies the trainers right after the snapshot is in; otherwise they go
in on the poll after N frames have run. When due it calls `run_cli_request()`: a fresh `Session` over
the loop owner's debugger backend (the same write path, so the same RZX refusal,
rewind wall and `MUTATE` lines), `apply()` for each pick in the order written,
stopping at the first refusal with the trainer's number and name in the reason;
a trainer that `state()` already reports as found in memory is logged and left.
A refusal makes `poll()` return false, which the loop turns into
`exit_code_ = 1`, and a job still pending when the automatic exit fires is
listed in `auto_exit_finds_no_deferred_work()`, so neither can end in a quiet
exit 0. Nothing is removed from here: a run is one-shot. The rows are POK-60..64
(the list, the request, the run, the refusal, the job's counting) and the
regression row `delayed-pok-func`, which reads the trainers back from a `.scr`
(the ULA screen memory, byte for byte) in both loops, with the controls that a
frame earlier or later flips, the `--load` order, the banked line, the usage
errors and the Qt refusal.

**Where a poke lands.** Only 0xC000-0xFFFF is banked. Port 0x7FFD writes MMU6
and MMU7 and nothing else (`zxnext.vhd:4679-4680`; MMU2-5 keep pages 0A 0B 04 05,
`:4613-4616`), so a Multiface-level POKE with a bank number can only reach that
bank at 0xC000 and above. A banked poke (bank 0-7) at address ≥ 0xC000 is NR page
`2*bank + ((address >> 13) & 1)`, offset `address & 0x1FFF`, whatever is paged in
(a 48K only has bank 0 there: port 0x7FFD is not decoded, NR 0x82 = 0xC0). Every
other poke (bank 8-15, "ignore the bank", or an address below 0xC000) is the RAM
page `Debugger::mmu_slots()` reports for the address when the trainer is applied,
not `poke(Cpu)`, which would follow a Layer 2 write-over mapping that reads do not.
Fuse and ZEsarUX index `RAM[bank][address & 0x3FFF]` for every address; that
differs from this only for a banked line below 0xC000, a few lines in thousands
of real files, all in trainers that also poke the banked window, and it is not
the author's reading. On the real 128K Starglider (loaded from its tape, snapshot
at the title), "Infinite Missiles (Bank#0)" is `M 0 44796 2 0` / `Z 0 60367 0 0`.
The second line lands in bank 0, on the `DEC A` that counts the missile counter
down (it becomes a NOP). The first is the CPU address 0xAEFC (bank 2), the operand
of the `LD A,1` that resets the same counter; read as byte 0x2EFC of bank 0, as
Fuse and ZEsarUX read it, it changes an unrelated byte of bank 0 instead and the
reset stays 1 (a bank diff of two snapshots, with and without the trainer, shows
exactly those two bytes).
The decision, the evidence and what would reopen it are in
`doc/analysis/POK-BANKED-POKES-BELOW-C000.md`.

**Undo and state.** `Session::apply` resolves every poke to a location, reads
every original **before** writing anything (so a refusal leaves memory untouched
and a trainer that pokes one address twice restores the byte from before the
trainer), writes the last value of each location, and keeps `{location, original,
final}` per trainer. `remove` writes those originals back to those locations,
whatever is paged in by then. `state()` is **derived from memory, never stored**:
Applied needs a record whose bytes all still hold; AppliedFound is a trainer with
no value left open whose bytes are all in memory with no record (removable only
if every poke gives a non-zero `original`). A record that stops verifying is kept,
because removing an overlapping trainer can make it true again. When the program
has overwritten part of a trainer it reads Not applied and Remove is refused, so
Apply is the way on; it replaces the record, but a location that still holds what
the older record wrote keeps the older record's `original` (matched by page and
offset), because that byte is still our poke, and a location the program changed
takes the program's byte. Without that, Remove would leave the poke in place for
good. A program put in through the backend, which never reaches `set_emulator()`,
drops them too: the `Session` compares `Debugger::program_loads()` and forgets the
records when it moved (POK-65..67, once per move, for every trainer). The counter moves
for a ZRCP `smartload` (`Debugger::load()`, whether the loader succeeded or not), a ZRCP
`load-binary` that landed a byte, and a NEW DZRP or GDB session (`note_program_replaced()`
from `cmd_init` and the GDB `on_connect`: neither protocol has a load verb, the program
arrives as memory writes after the connection; DZRP-LOAD-01, GDB-SES-08, ZRCP-LOAD-06).
Plain memory writes into a session already open (CMD_WRITE_MEM/BANK, `M`/`X`,
`write-memory`) do not move it, so the records survive those.
`forget_applied()` drops them for the other routes: `MainWindow::set_emulator()` calls
it, and `QtApp` calls `set_emulator()` after every machine rebuild
(`boot_machine()`'s `rewire_host` hook), which covers menu load, Quick Load,
Power Reset, a guest NR 0x02 reset and a machine-type change.

**File > Load.** `handle_load_path()` sends a `.pok` to `open_pok_file()` before
any cold boot. The same function now also refuses, before the cold boot, a file
that would route to the NEX loader (a `.nex`, or an extension nothing owns) but
has no `Next` prologue (`NexLoader::probe_version()`), which `NexLoader::load()`
would refuse anyway: before, the session was reset and then the load failed. The
reason follows the case: a `.nex` is "not a NEX file" and nothing more, and only
an extension nothing owns is told the list of formats and where a `.pok` goes.

The suites: `pok_trainers_test` (POK-01..25 and POK-60 the parser, the list and
`load_pok_file`, POK-30..57 and POK-61..64 the session and the command-line
request, each bank-to-page row citing `zxnext.vhd` or the other emulators'
indexing and asserting bytes the test placed itself; the rows that sweep a
trainer's locations one at a time, POK-50..52, are there so that a rule meant for
every location cannot pass by checking the first) and `pok_dialog_test`
(PKD-01..17); File > Load's refusal is `preferences_apply_test` PA-24e/f/g.

## ZIP archives

`game.tap.zip` loads (GH #392) by being turned into **an extracted file with the
member's own extension at the two places a user names a program**: the
command-line fold in `main.cpp` (right after the bare filename becomes
`load_file`, before the RZX checks) and `MainWindow::handle_load_path()`. Every
decision below that, from the extension tables to the warm start, the RZX and
snapshot rules, the V1.3 gate, `Debugger::load_program_sidecars` and the NEX
loader that keeps its file open (`ExtendedNexHost::reopen()`, `run X.nex`
siblings resolved beside it), then sees an ordinary path and is unchanged.
Loading from memory per format was rejected: nine loader APIs, and a NEX needs a
path anyway.

**Two readers, on purpose.** `save/zip_archive.*` is the `.jns` container: it
accepts only the subset its own writer emits (flag 0, no extra field, no data
descriptor, lower-case grammar paths) and must stay that strict, because two
readers disagreeing about the bytes is the failure it exists to avoid.
Real archives break every one of those rules (Info-ZIP: flag 0x0002, a 24-byte
extra field on every entry, directory entries; streaming writers: flag 0x0008
and zero sizes in the local header), so `save/zip_interchange.*` is a separate
read-only reader: sizes and CRC from the CENTRAL directory, the data offset from
the LOCAL header's own name and extra lengths, stored and deflate only, and a
refusal naming each of: not an archive, ZIP64, multi-disk, a damaged directory
(archive level), encryption, another method, a declared size over 256 MiB
(checked before anything is allocated), a CRC mismatch (member level). The only
code the two share is `zip::inflate_exact()`; the EOCD and central-directory
walks are not shared, because a shared walk has to be the weaker of the two.
`ZIPI-25` pins that the strict reader still refuses an Info-ZIP archive.

`core/program_archive.*` is the layer on top. `is_program_ext()` is the ONE table
of loadable extensions and `emulator_load_routes_to_nex()` is defined over it, so
they cannot drift. `is_archive_path()` is false for a program extension or
`.pok` (a `.jns` IS a zip), true for any `.zip`, and for anything else true when
the file starts `PK\3\4` or `PK\5\6`. `ProgramArchive` lists the members that
are programs (not directories, `__MACOSX/` or `._` AppleDouble junk), refuses two
with one name, and extracts to `<config-dir>/archive-load/<process>/<n>/<basename>`
after refusing an absolute, drive-lettered or `..` name (splitting on both `/` and
`\`, APPNOTE 4.4.17) and sanitising the basename. Several programs are refused
on the command line (listed) and asked for in the window (`QInputDialog`;
refused with the list when unattended, and the choice mapped back by INDEX, never
by re-encoding the text). Duplicate names are found with a hash set (a `find()` per
member was quadratic: 12 s for 65534 entries). A member name is untrusted bytes:
every message that carries one goes through `display_name()`, which escapes C0, DEL,
C1 and anything outside well-formed UTF-8 as `\xNN`, so no consumer can print an
escape sequence. An archive file over 256 MiB is refused too, before it is read. `extraction_name()` also prefixes `_` to a Windows device name (`CON.TAP`, `NUL.Z80`, `COM1.NEX`), on every OS, because Windows 10 refuses to create those files.

**Lifetime.** One `ScratchDir` per process (`process_scratch()`, a function-local
static, so it is destroyed on any return from `main()`, after the app objects and
their open NEX handle, which matters on Windows): a random directory per process,
one numbered slot per extraction (two archives both holding `GAME.TAP` never
collide, and a refused second load never removes the file the running program has
open). Process directories older than 7 days are swept on first use: leftovers
of a crash, never another instance's live session. In the window, a bundled
`.pok` (`matching_pok()`: same stem, else the only one) becomes the POK session
without showing the dialog (`start_pok_session(.., show=false)`).

The suites: `program_archive_test` (ZIPI-01..25 the reader, ZIPL-01..22 the layer;
fixtures from foreign writers in `test/fixtures/zip` plus archives built in code
with `test/zip_fixture.h`), `load_error_test` ZIPG-01..09 (the window) and the
`zip-load-func` regression row.

## Media out

| What | Flag / UI | Code |
|---|---|---|
| PNG or `.SCR` screenshot | `--delayed-screenshot`, File ▸ Save Screenshot, File ▸ Quick Screenshot | `src/core/screenshot.*` |
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

**Quick Save / Quick Load** (File ▸ Quick Save Snapshot Alt+Shift+F5, Quick Load
Snapshot Alt+Shift+F9; the same two chords in the SDL-only build) follow the same
shape for `.jns`. `src/core/quick_snapshot.*` holds the folder
(`default_quick_snapshot_dir()` = `sdcard::default_state_dir()` + `/snapshots`,
so `$JNEXT_CONFIG_DIR` and the SD image's own state-dir resolution apply — on
Windows it inherits the HOME → USERPROFILE fix tracked in GH #316, and until
then a missing HOME puts it beside the SD image under `./.jnext`), the name
(`auto_timestamped_path()`, the one function `auto_screenshot_path()` also
uses) and the choice of file (`latest_quick_snapshot()`: greatest modification
time among `*.jns` regular files directly in the folder, ties to the greater
name stem so `…-02` beats the bare name; local-time names are not ordered across
a DST change, which is why the name is not the key). Quick Load goes through
`MainWindow::handle_load_path()` / `SdlApp::cold_boot()`, i.e. a full cold boot
and then `load_jns_file()`, never an in-place load. Two things make that route
safe for a `.jns`: `Emulator::probe_jns_file()` runs the pre-touch half of
`load_jns` (`check_jns()`) BEFORE the cold boot, so a file that will be refused
costs no session; and `emulator_boot_machine()` reads the `.jns` manifest's
machine the way it reads an RZX's, so the cold boot builds the snapshot's
machine and the frontend's `keep_machine` hook records it (otherwise a later
reset would boot the old machine); the command-line startup does the same through `emulator_startup_machine()`, where a `.jns` wins even over an explicit `--machine`. After ANY snapshot load the frontends' `emulator_apply_load()` calls `Keyboard::release_all_keys()`: the membrane matrix in a `.jns` records the keys the host held at save time (the Shift of the Alt+Shift+F5 chord), whose release would never arrive, so the guest starts with no key down (the matrix, the extended-key register that the arrows use, and the shift scan history; the core load keeps them, the round-trip contract: QS-28..30). Debugger-initiated loads (DZRP, ZRCP, GDB) go through `emulator_apply_load()` too, so they release held keys as well. `prune_quick_snapshots()` never deletes a symbolic link. Quick Load failures (no snapshot, unreadable newest file) are a status-bar line in Qt and a log line in SDL, never a dialog; both actions act once per key press (`QuickChordTracker`, `setAutoRepeat(false)`). `.sna`, `.szx` and `.z80` switch
machine the same way (GH #323; see "Which machine a snapshot loads as" below), and
QS-25..27 pin that route. Retention: `prune_quick_snapshots()` runs after
each Quick Save and deletes only names matching `is_quick_snapshot_name()`;
the count is `EmulatorConfig::quick_snapshot_keep` (CLI) or
`AppConfigData::quick_snapshot_keep` (`[snapshot] quick_keep`, Preferences),
the CLI winning (`quick_snapshot_keep_cli`). The debugger keymap refuses
Alt+Shift+F5/F9 by name (`validate_combo`).
The SDL chord predicate is `platform/quick_snapshot_chord.h`; the Qt chords are
the two QAction shortcuts in `main_window.cpp`.
Both frontends hand the request to the key `Router` (`on_host_action()`, GH #386)
instead of acting in the key callback, so a chord takes its turn behind the keys
typed before it, keys typed during or after it reach the loaded machine, and
`SdlApp::service_quick_requests()` / `QtApp::TickEffects::post_frames()` call
`action_done()` however the action ended (see the input subsystem chapter).
There is no whole-card read on this path (GH #396, owner 2026-10-22): a save,
probe or load reads the MBR and the BPB sector (the Tier-1 identity) and nothing
else; `.jns` has no Tier-2 `content_stamp` any more (old files carry one, it is
ignored) and no mid-transfer refusal. jnext assumes the card is as the snapshot
left it. `sd_no_digest_linux_test` (`NODIG-01..04`) pins it by bytes read.

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
`/MACHINES/NEXT/48.rom`, `128.rom`, `plus3.rom`, `enNxtmmc.rom`,
`enNextMf.rom` and `enAltZX.rom`. It is read-only,
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
and the Next firmware's own FatFs — correctly — rejects it. The same pass
writes a default `/MACHINES/NEXT/config.ini` and doubles the distribution's
16 KB `zx81.rom` to the 32 KB the firmware boot menu loads (GH #284, as
upstream tbblue did after the 24.11 release), only when it is that exact file.
That recipe is versioned (`kFixedImageRecipe`, recorded in a `.recipe` sidecar
next to the image). jnext never rebuilds an existing image because its recipe
is old: the image holds the user's own files.

Downloads are resumable (GH #358), by one contract for every caller of
`DownloadFn` (the SD distro zip, the NextPi archive, its `.md5`, the mirror
listing). The default backends (libcurl, WinHTTP) write `<dest>.part`, beside a
resume record `<dest>.part.resume` (requested URL, validator, full size), and
rename it over `dest` only when the byte count matches the advertised total, so
`dest` exists only when complete and a transfer cut short is a failure on both
OSes. A failure or cancel keeps the partial only if it holds bytes and the
server sent a strong ETag (else Last-Modified); otherwise nothing is left. The
next call asks for the rest (`Range`, `If-Range` with the validator verbatim)
and decides before the first body byte: 206 with exactly the expected
`Content-Range` appends; 200 (range ignored or representation changed) starts
again from 0; 416 or a wrong 206 discards the partial and retries once as a
plain GET. libcurl uses `CURLOPT_RANGE` with an explicit `If-Range` header, not
`CURLOPT_RESUME_FROM_LARGE`, which turns a server's 200 into error 33 and loses
the transfer. The file names, the record format and the decision table live once
in `PartialDownload` (`sdcard_provisioner.cpp`); both backends only feed it
headers and body. Residual corruption (a server that lies about its validator) is
caught after the download: the NextPi MD5, the zip entry CRC. One classifier
(`partial_state`) names the five states of a `.part`: a Complete one (whole file,
only the final rename failed) is finished with no request and never refetched; an
Oversize or Stale one is discarded; a Resumable one is continued, appended at an
explicit 64-bit offset.

A dropped network must neither freeze the window nor take minutes to be noticed
(GH #358). The same contract on `DownloadFn` says how both backends behave while a
transfer is under way. They call the `ProgressFn` at least every `tick_ms` (200),
whatever the network does: connecting, waiting for headers, a silent body. A
`false` from it ends the attempt within a tick (`cancelled by user`, the partial
kept by the rule above). An attempt that gets no body byte for `stall_timeout_ms`
(30 000, counted from the start of the attempt and then from the last byte) ends
with `stalled: no data received for 30 s (after X of Y bytes)`, the partial kept
the same way, and a slow transfer that is alive is never cut. The download runs on
the GUI thread, before `QtApp` exists, so the window lives exactly as often as a
backend calls `SdcardGuiProvisioner::progress`, which pumps the event loop: the
cadence is the backends' business, not a worker thread's. The rule exists once.
`StallWatch` answers "how long since the byte count last changed", `download_step`
is the per-tick decision both backends take (report, honour a cancel, test the
watch), `DownloadTimings` carries the two numbers and the WinHTTP read size (tests
pass short ones to `http_download`; `default_http_download` forwards the production
values), and the dialog uses the same watch for its notice: once 5 s
(`kStallNoticeMs`) pass with no byte its label gains `Stalled: no data received for N s`, the seconds counting up
and the note gone when bytes arrive. The dialog's watch starts when it opens, so a
slow answer to the confirm box is not a stall.

The libcurl twin runs the transfer on the multi interface: `curl_multi_perform`,
`download_step`, then `curl_multi_poll` for one tick. With the easy interface the
progress callback is frequent while data flows but slows to about one call a second
when nothing arrives (libcurl documents it; 1.00 s gaps measured through a 15 s
stall), so the window repainted at about 1 Hz through a stall and nothing named the
stall; `CURLOPT_LOW_SPEED_*` is gone because its moving average overshot (120 s set,
126 s measured) and a second mechanism would be a second message. `curl_multi_poll` needs
libcurl 7.66, which every packaged target exceeds. A `file://` URL (a test seam,
and the Flatpak build) is read inside one `curl_multi_perform` call and has no
ticks. The WinHTTP twin is asynchronous (`WINHTTP_FLAG_ASYNC`) because the contract
cannot be met synchronously: a synchronous `WinHttpReadData` blocks its thread until
data arrives or its receive timeout fires, and Microsoft documents closing a
synchronous request's handle from another thread as a race. With the cable pulled
the GUI thread sat inside that read for its 120 s, and Windows ghosted the window as
"Not Responding". In async mode the thread waits on an event for the next completion,
one tick at a time, and a cancel or a stall is "close the request handle and return".
The per-request state (event, latest completion, read buffer, the extra headers
`WinHttpSendRequest` still points at) is heap-allocated and belongs to the request
handle from the moment the status callback is installed: the callback frees it on
`WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING`, the last callback a handle gets, so an
abort returns at once while WinHTTP may still be completing a read into that buffer,
and nothing touches the state after `WinHttpCloseHandle`. The callback only stores
atomics and signals (no logging, no globals) and is declared `CALLBACK`, which the
i686 builds need. Reads are sized by `WinHttpQueryDataAvailable`, so every byte that
arrived reaches the `.part`, and a read never asks for more than the buffer holds
(`DownloadTimings::read_bytes`, 64 KiB); the receive timeout is only a backstop 30 s
behind the stall timeout.

Under wine the rows (`PROV-STALL-*`, `PIT-43`) show the tick cadence, the stall, the
cancel and the resume on the WinHTTP backend; they cannot show four things. Windows'
own hung-window detection (five seconds without a message pump) and the Qt dialog on
top of WinHTTP: the Qt suites run on Linux only, so the pump is proven there
(`SDD-01..03`) and meets the WinHTTP cadence in `progress()`, which every platform
shares. A physical cable pull (media sense, TCP retransmission, RST or ICMP): the
loopback server models "connection open, nothing arrives", which is what a receiver
sees until TCP gives up. WinHTTP's teardown of a pending asynchronous read: wine keeps
it until data, EOF, the server's close or the backstop (measured 58 s), so a row that
abandons a connection builds its own server and lets its destructor close it; the
caller's result and timing are the same, only the abandoned socket lives longer. And
TLS under asynchronous WinHTTP, and the read-fill semantics and error codes of real
Windows (wine fills the buffer, reports a read timeout as 10060 and never reports
more than 8 KiB available: measured over the whole provisioner suite, 8192 at most):
the rows use plain `http` on loopback, and https to the real mirrors is exercised
only by use. How much real Windows reports available is therefore not observable
here; the clamp of a read to the buffer binds under wine only because `PROV-STALL-10`
lowers `read_bytes` to 1000, and the buffer is the one limit that does not depend on
that number. The error side is pinned the same way: `PROV-STALL-09` makes a server
close without answering and a port refuse the connection, one per stage of the
asynchronous sequence, and requires the transport's own reason at once, never a
stall.

What a failed fetch says is the callers' business, once (GH #385). Every failure of
a fetch is built by `sdcard::fetch_failure(what, url, reason, next)`: `<what>:
<reason>`, then `  URL: <url>` (exactly the string handed to the `DownloadFn`, one
variable per fetch, so it can be pasted into a browser), then `  <what happens
next>` (the part is kept, nothing was kept, the download was removed); nine call
sites, two in `sdcard_provisioner.cpp` and seven in `nextpi_provisioner.cpp`, and
nothing else builds one. The backends hand up a reason in plain words and no
framing: `net_reason(kind, raw)` gives each `NetFailure` kind its one wording and
appends the backend's own code and text in parentheses, `http_status_reason(status)`
does the same for an HTTP error status (RFC 9110 section 15), and each twin defines
`transport_reason(code)` (like `http_download`), which sorts its codes into the
kinds: a `CURLcode` for libcurl, an `ERROR_WINHTTP_*` for WinHTTP, wine's
`WSAETIMEDOUT` as a timeout. So the two say the same words for the same failure.
libcurl's `CURLOPT_FAILONERROR` turns any HTTP status of 400 or more into one
generic code, so that twin reads the status off the transfer, and a 404 reads the
same as on WinHTTP (where `PartialDownload::begin` says it, through the same
function). A proxy that will not open the tunnel (its answer to `CONNECT` is not a
2xx) leaves the transfer with no response status at all, so that twin also reads
`CURLINFO_HTTP_CONNECTCODE` and says `could not connect to the server (the proxy
answered HTTP 403 to the connection request)`, whatever code libcurl gave the
failure (8.18: 22 for a 4xx or 5xx, 56 for a 3xx). WinHTTP takes its proxy from the
machine's settings, never from the environment, so the case exists only on libcurl.
libcurl also gives its codes 1 and 3 (unsupported protocol, malformed URL) to a reply
it cannot use after connecting (not HTTP, or a redirect to a scheme or URL it cannot
follow); only with no connection made (`CURLINFO_NUM_CONNECTS` 0) is that the
address's fault, so only then is it worded as one. The stall reason, `cancelled by
user` and the other reasons that were already plain are unchanged, as are the
declared residuals (`curl_easy_init failed`, which the user cannot act on).
`PROV-REASON-01..05` pin the wordings, `04` on both transports (it fails on both
with the old text) and `05` the transport's own code for an address it cannot use;
`PROV-REASON-06` and `07` put a refusing proxy, and a server whose reply libcurl
cannot use, in front of the real libcurl backend, which is why they live in the
POSIX-only sibling suite `sdcard_provisioner_posix_test`; `PROV-MSG-*` and
`PIT-44..50` the messages, `PROV-SANDBOX-02` and `PROV-STALL-09` moved to the new
text with their invariants intact.

Where the message goes. `main.cpp` logs a failed SD-card provisioning in every
frontend (headless and the SDL-only build printed it to stderr and nowhere else) and
shows it in a dialog in the Qt GUI; a failed NextPi start is reported in a dialog
in the Qt GUI whether it came from Preferences (jnext starts without it) or from
`--nextpi` (it also prints to stderr and exits 1); and the warning that `latest`
could not read the mirror's list is a dialog too, not only a log line. A declined
release change is the user's own answer and is logged, not a warning.
`SdcardGuiProvisioner::warn` shows a `QMessageBox` built by `warning_box`, whose
text is selectable by mouse whatever the widget style (the "Windows" style's
default is links only, so the address could not be copied). On the offscreen
platform nothing is shown (nobody could dismiss it); the title and the text go to
the `platform` logger at debug level, and that line is what `sdcard-download-fail-func`
and `nextpi-func` facts 10-12 read of a dialog.

The short fetches. The NextPi release list of `latest` and the two `.md5` fetches
are tiny, so they passed no `ProgressFn`, and on a dead network the window showed
nothing for the 30 s the backend waits. `ShortFetchFn` (`nextpi::ProvisionOptions::
short_fetch`) wraps exactly those three, with a label and the address each, and hands
the fetch the `ProgressFn` it should give the `DownloadFn`; the archive download is
not wrapped and keeps `opts.progress` (`PIT-51`). The Qt build's `SdcardGuiProvisioner::short_fetch`
pumps the event loop on every call, shows nothing for the first
`kShortFetchShowMs` (1000 ms, so a quick fetch does not flash a dialog), and then
shows the same dialog as the archive, built and refreshed by the same code (title,
empty bar, `Stalled` notice from 5 s, Cancel); it is gone when the fetch returns.
Its Cancel ends that fetch only: the `cancelled_` latch of `progress()` is not
touched, so the archive download that follows is not aborted. All of it counts on
the helper's `now_()`, so `SDD-06..08` run on a clock the test owns, and `SDD-09`
runs the whole thing through the real backend and a silent server on the production
clock. `nextpi-func` fact 12 shows it in the real binary against a mirror that
answers after 3 s.

The console. The frontends with no dialog (`--headless`, the SDL-only build) say the
same things as text, on stderr and in the log, in the dialog's words (GH #385). One
class decides: `sdcard::ConsoleWait` is made for one transfer and ticked by its
`ProgressFn`, which the backends call at least every 200 ms whatever the network does.
When no byte has arrived for `kStallNoticeMs` (5 s) it says `stall_note()`, the
dialog's own text, once, on a line of its own on stderr and as a warning in the log,
and again if bytes come and a new stall begins. With a `slow_line` it also says that
line once, as information, when the transfer has gone on for `kShortFetchShowMs`; a
quicker transfer says nothing at all. The limit is the dialog's, defined once in the
core. A `ConsoleWait` counts from its first tick, not from its construction: the
prompt before a download can sit open for minutes. `nextpi::CliShortFetch` is the
console's `ShortFetchFn`: one `ConsoleWait` per fetch, its line `<label> <address>`,
logged on the `uart` logger like the rest of the module. `sdcard::CliProgress` is the
terminal's progress display of a download, the `\rDownloading ...  45%` line that
`cli_progress` and the NextPi twin drew, now one class with the heading and the
no-length rule as arguments (the SD-card image counts MiB, NextPi says nothing) and
the stall note, which starts a new line when a progress line was left unfinished.
`make_cli_progress()` makes the SD-card image's and `nextpi::make_cli_progress()`
NextPi's; `main.cpp` wires them, and `CliShortFetch`, where there is no dialog. The
clock, the stream and the log's copy are parameters, so `PROV-CON-01..11` and
`PIT-52..56` run all of it on a fake clock and read the stream back as a file; the
real binary is shown by `sdcard-download-fail-func` fact 5 (both frontends without
a dialog, each with its own wiring in `main.cpp`) and `nextpi-func` fact 13, against
a server that answers after 8 s. What no row can show is the NextPi archive's own
display being wired in `main.cpp`: its download needs 22 GB free, which the
regression suite cannot assume, so that one assignment is covered only by the
classes it uses.
