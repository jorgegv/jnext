# Tape SAVE to TZX and WAV (GH #89)

Phase 1 (Task 57) shipped `--tape-save FILE`: a ROM trap at SA-BYTES (0x04C2)
appends standard TAP blocks. This plan covers what the issue still asks for:
capturing the tape-out signal of any saver (Phase 2, TZX), a WAV writer fed by
the same capture (Phase 3), and a Tape-menu affordance.

## 1. The tape-out signal (VHDL)

The MIC jack is driven from one signal:

- `zxnext_top_issue2.vhd:1402-1408` (`-- tape save`): `mic_port_o <= zxn_audio_mic`,
  which is `o_AUDIO_MIC` of the machine (`zxnext_top_issue2.vhd:2421`; issue 4 is the
  same at `zxnext_top_issue4.vhd:1274`, `:2311`).
- `zxnext.vhd:1638`: `o_AUDIO_MIC <= beep_mic_final`.
- `zxnext.vhd:6503`: `beep_mic_final <= i_AUDIO_EAR xor (port_fe_mic and nr_08_keyboard_issue2) xor port_fe_mic`.
- `zxnext.vhd:3588-3599`: `port_fe_mic <= port_fe_reg(3)`, the latch written by any
  `OUT` to an even port (`port_fe_wr`, `zxnext.vhd:2711`).

`(m and i2) xor m` is `m and not i2`, so

    tape_out = i_AUDIO_EAR xor (port_fe_mic and not nr_08_keyboard_issue2)

jnext already models `i_AUDIO_EAR` for the port 0xFE read (`emulator.cpp`, the ULA
handler): the playing tape's level, else `port_fe_mic` in issue-2 keyboard mode, else 0.
With no tape playing both keyboard modes therefore give `tape_out = port_fe_mic`. With a
tape playing in real time the input is echoed to the output, exactly as on the board;
jnext captures that echo too, from the same model, rather than diverging from the VHDL.
A fast-loaded (trapped) tape produces no edges, so it echoes nothing.

## 2. Format by extension

| `--tape-save` file | what is written | ROM saves (SA-BYTES) | other savers |
|--------------------|-----------------|----------------------|--------------|
| `*.tzx` | TZX 1.20 | trap, instant, block 0x10 | captured from tape-out |
| `*.wav` | WAV, PCM 8-bit mono 44100 Hz | trap, instant, standard pulses synthesised | captured from tape-out |
| anything else (`*.tap`) | TAP, unchanged | trap, instant | not captured (TAP cannot hold them) |

The extension test is case-insensitive. `.tap` behaviour is untouched, and so is every
other extension. Before this change `--tape-save x.tzx` wrote TAP bytes into a file a TZX
reader rejects, so giving it TZX content fixes a wrong output rather than changing a
working one. No new CLI flag.

All three formats append, as TAP always has: an existing `.tzx` must start with the
TZX signature and gets blocks appended; an existing `.wav` must carry exactly the header
jnext writes (canonical 44-byte PCM header, same rate, width and channel count) and gets
samples appended with the RIFF and data sizes patched. Anything else is refused at
arming time with an error, and saving stays off. An empty or missing file starts a new
tape.

## 3. Trap and capture together, without double writing

The trap is kept for `.tzx` and `.wav` so ROM saves stay instant, as they are today.
It cannot double-write: it fires on the first instruction of SA-BYTES and returns to the
caller without running the routine, so the ROM's own pulses never reach tape-out. The
trap also skips SA/LD-RET, the border restore, so no stray edge follows. Ordering is
kept by sending both sources through one recorder as a single time-ordered stream: a
trapped block is an event at the trap's emulated time, exactly like an edge.

Because the trap is still armed, the RZX refusal (`--tape-save` cannot be combined with
RZX recording or playback) applies to every format, unchanged.

## 4. Timing source

Edge times are the emulator's master clock (`Clock::get()`, 28 MHz cycles). Tape-out is
sampled only where it can change: at every port 0xFE write, at the write's bus request
edge (`Emulator::io_request_edge()`, the CLK_28 edge that latches `port_fe_reg`,
zxnext.vhd:3588-3594), and, while a tape plays in real time, at the end of every
instruction (the echoed input). It is emulated time, never wall clock, so a run is
deterministic. Master cycles are independent of the CPU speed (NR 0x07): a CPU T-state
is 8, 4, 2 or 1 master cycles, so a saver running at 7 MHz writes pulses half as long in
TZX terms, which is what a tape recorder on the real board would see.

TZX timings are in 3.5 MHz T-states (TZX 1.20 §"Introduction": 1 T = 1/3500000 s), so a
duration in master cycles is divided by 8, rounded to nearest. The WAV sample index of a
tape position `p` (master cycles) is `p * 44100 / 28000000`, computed on the running total
so rounding never accumulates. Both conversions use the nominal 28 MHz the Next's
3.5 MHz T-state is derived from; machine timing (48K/128K/Pentagon/Next frame lengths)
only changes where edges fall, not the conversion.

The clock can go backwards (a rewind, a reset, a snapshot load). A backwards step is
treated as a 1-second gap, so the stream never contains a negative or zero pulse. While
the rewind fast-forward replays frames (`replay_mode()`), neither the trap nor the
capture records anything: those frames already reached the tape once.

**Cost when nothing is saving** (review round 1, B1). Sampling at the end of every slot,
as first written, cost +0.23 % host instructions on every program. Now the port 0xFE
write handler tests one precomputed flag (`tape_capture_live_` = recorder armed and no
replay running, refreshed by start/stop and every replay-mode change), the per-instruction
sample sits inside the tape players' own `is_playing()` branches, and a TZX / WAV save
arms the TAP saver's flag too, so the SA-BYTES trap test is the one TAP alone already
paid. Measured with `perf stat` on a `gui-release` build, nothing armed: host
instructions equal main's within run-to-run noise (report, round 1). When the flag turns
on it takes the current level as the baseline, so a change made while it was off is not
an edge.

## 5. Segmentation and encoding (TZX)

The edge stream is cut into segments at any interval longer than 65535 T-states
(18.7 ms): no data pulse is that long, and 65535 is the largest pulse a TZX block can
hold. Each segment is decoded left to right:

1. **Data block.** At least 256 consecutive pulses equal within 1/8 (pilot), two
   shorter pulses (sync), then pulse pairs whose halves are equal; the pair lengths fall
   into two classes, short (0) and long (1), with long at least 1.5 × short (one class
   only at the ROM's own lengths). Bits are MSB first. After whole bytes, one more edge
   is the block's end, not a bit: the ROM itself adds it (SA/LD-RET restores the border
   about 855 T after the last bit). A last pulse with no matching second half is read as
   a bit only when it completes a byte.
   - All timings within 1/16 of the ROM's (pilot 2168, sync 667/735, 855/1710, pilot
     count 8063 for a flag byte below 0x80 and 3223 otherwise, within 1 %) and a whole
     number of bytes: **block 0x10**.
   - Otherwise **block 0x11** with the measured averages, the exact pilot count and the
     used bits of the last byte.
2. **Anything else** (no pilot, a pilot too short, a pattern that breaks): **block 0x13**
   pulse sequences of at most 255 pulses each, with every duration exact to the T-state.
   Nothing is dropped. 0x13 was chosen over 0x12 (it loses nothing 0x12 would keep, and
   a run of equal pulses is rare outside a pilot) and over 0x15 (direct recording samples
   the level and quantises every edge; 0x13 keeps the edge times exactly).

**Pauses.** A block's pause is the time from its last edge to the next event (the next
segment's first edge, a trapped block, or stopping the save), in milliseconds, capped at
65535 (the field's maximum). 0x10/0x11 carry it in their own field. An unrecognised run
that ends a segment has an open last edge: it is written as a 1 ms pulse (TZX's own rule
for finishing the last edge), followed by a 0x20 pause for the rest (at least 1 ms, since
0 in 0x20 means "stop the tape").

**What reaches the disk when** (review round 1, B3). A block's last pause is only known
when the next event arrives, so it is written provisionally and patched in place: a
trapped ROM block is written at once with pause 0; a segment is written once it has
been silent for longer than 65535 T (checked once per frame), with its last pause up to
that moment. The next event (or stopping) rewrites that one WORD. The file is flushed
every frame. A run that is killed (Ctrl-C, a crash) therefore keeps every trapped block
and every segment that had gone quiet; it loses only a MIC segment still in progress and
the true length of the last pause. A segment that reaches 2^20 edges without a pause is
decoded at that point, so a saver that never stops cannot grow memory without bound.

A trapped ROM block becomes a 0x10 block with the exact bytes; its pause is measured
the same way (the ROM's own one-second gap between header and data runs outside the trap,
so it appears as real silence).

## 6. WAV

WAV is the raw signal: no decoding. The writer keeps a tape position in master cycles;
each edge advances it by the time since the previous event (capped at 65.535 s, the same
cap as the TZX pause), writes samples at the old level up to the new position and
toggles. A trapped ROM block is synthesised at the current position as the standard
pulse train (pilot 8063/3223 × 2168, sync 667 + 735, 855/855 or 1710/1710 per bit, MSB
first, closing edge), which moves the position forward by its length while emulated time
stands still. Levels are 0x40 (low) and 0xC0 (high), so any threshold reader splits them
at the midpoint. Nothing is written before the first event. Stopping writes the held level
for the time since the last event (same cap) and patches the RIFF and data sizes. The
sizes are also refreshed after every trapped block and, when samples were added, once per
frame, and the file is flushed, so a killed run leaves a WAV whose header matches its
samples up to the last edge. The RIFF pad byte for an odd data size is not written: the
file is appended to in later sessions, and every reader tried (FUSE/libspectrum,
libaudiofile, sox) accepts it without.

## 7. GUI and frontends

Tape menu, below Fast Load, after a separator:

- **Start &Saving...** opens a save dialog (TAP/TZX/WAV filters; no overwrite prompt,
  since saving appends) and arms the chosen file exactly as `--tape-save` would. It is
  refused, with a message, while an RZX recording or playback runs, mirroring the RZX
  refusal in the other direction. Enabled only while not saving.
- **Stop Sa&ving** finishes the file (pending block, WAV header) and disarms. Enabled
  only while saving.

No keyboard shortcut, like the other Tape items (Eject, Rewind, Fast Load): a host Alt
shortcut takes a key from the guest, and saving is not a frequent action. A
`--tape-save` on the command line starts the GUI in the saving state, with Stop enabled.
The status tip names the file being written.

The SDL frontend and headless mode have no tape menu: `--tape-save` is their interface,
and the file is finished when the emulator is destroyed at exit.

## 8. Structure

- `src/core/tape_recorder.{h,cpp}`: `TapeRecorder`, the capture plus the TZX and WAV
  writers. The encoders (blocks 0x10/0x11/0x13/0x20, the segment decoder, the WAV header)
  are static functions so unit rows test them byte-exact without an emulator.
- `Emulator` arms `TapSaver` for TAP and `TapeRecorder` for TZX/WAV
  (`start_tape_save()` / `stop_tape_save()` / `tape_save_active()`), samples
  `tape_out_level()` at port 0xFE writes and while a tape plays, polls the recorder once
  per frame, and closes it in the destructor and before re-arming.
  `emulator_cold_boot()` carries the live `tape_save_file` across a power-on reset, so a
  save started from the menu survives it. The warm-start recording machine gets none:
  `Emulator::warm_start_boot_config()` clears every host output (tape save, compositor
  trace, magic port, joystick-port cable), so each file has one writer.
- `TapSaver::handle_sa_bytes_trap` hands the block to the recorder when one is armed.

## 9. Known limits

- When a tape playing in real time stops, the echo's last level change is recorded at
  the next port 0xFE write rather than at the moment it stopped.
- The level before the first edge, and the absolute polarity, are not recorded; Spectrum
  loaders are edge-triggered.
- The issue-2 EAR relaxation (`symmetric_relaxation`, ~1.15 ms) is modelled only in its
  steady state, as it is for the port read.
