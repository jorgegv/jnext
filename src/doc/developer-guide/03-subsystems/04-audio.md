# 3.4 Audio

Audio splits in two, and keeping the halves apart matters more here than anywhere
else in jnext. Everything in `src/audio/` up to and including the 13-bit sum
models the FPGA's audio path and is checkable against `ym2149.vhd`,
`soundrive.vhd` and `audio_mixer.vhd`. Everything after it — the ring buffer, the
SDL device, and above all the *pacing* — is host policy with no VHDL counterpart.
A change on the first side is a hardware-fidelity question; on the second it is a
"does it sound right on this machine" question.

## The emulated hardware

**AY / YM2149** — `src/audio/ay_chip.*`. One `AyChip` is a full YM2149: 16
registers, three 12-bit tone counters, the 17-bit noise LFSR, the envelope
generator with its 16 shapes, and both the AY and YM volume tables taken from the
VHDL. It is advanced by `tick()` at the PSG clock-enable rate, 28 MHz / 16 =
1.75 MHz, and exposes three 0-255 channel levels.

**TurboSound** — `src/audio/turbosound.*` owns three of them plus everything
around them: which chip port 0xFFFD's control writes select, per-chip panning,
the ABC/ACB stereo mode (NR 0x08 b5), the per-chip mono flags (NR 0x09 b7:5), the
TurboSound enable (NR 0x08 b1), and AY-vs-YM mode (NR 0x06 b0). It recomputes a
12-bit stereo pair on every PSG tick, and the mixer only ever reads that latched
pair. The two resets are distinct: `reset()` is power-on, while
`reset_ay_only()` models the VHDL `audio_ay_reset` fired by an NR 0x06
`psg_mode = 11` write, which must *not* clear the NR-driven
enable/stereo/mono inputs.

**DAC** — `src/audio/dac.*` is four 8-bit Soundrive channels; A+B sum to left,
C+D to right, giving a 9-bit value per side. Each channel resets to 0x80, which
is its *silence*, not zero. The port decode does not live here — it is in
`Emulator::init` in `src/core/emulator.cpp`, because each port is gated by
NR 0x84 bits exactly as the VHDL decodes them:

| Ports | Channels | Gate (NR 0x84) |
|---|---|---|
| 0x1F, 0x0F, 0x4F, 0x5F | A, B, C, D (Soundrive mode 1) | b1 |
| 0xF1, 0xF3, 0xF9, 0xFB | A, B, C, D (Soundrive mode 2) | b2 |
| 0x3F, 0x5F | A, D (Profi Covox) | b3 |
| 0x0F, 0x4F | B, C (Covox) | b4 |
| 0xB3 | B **and** C (GS Covox) | b6 |
| 0xDF | A **and** D (Specdrum, mono) | b7 |
| NR 0x2C / 0x2D / 0x2E | B / A+D / C | — |

Every write is additionally gated on NR 0x08 b3; with the DAC disabled the
Soundrive module is held in reset. The mono ports writing *two* channels is real
hardware behaviour and a routine source of confusion — a program that expects
0xDF to be "channel D" gets A as well.

**Beeper** — `src/audio/beeper.*` is deliberately tiny: three booleans (EAR and
MIC from port 0xFE bits 4 and 3, plus a tape-EAR input from real-time tape
playback). No accumulator, no filtering; that is the mixer's job.

**Pi I2S** — `src/audio/i2s.*` is a latched 10-bit sample pair plus the NR 0xA2
control byte, with no protocol emulation. The term exists because the mixer sum
includes it, and its *idle* value (0x200, offset binary) is load-bearing.

## The mixer

`src/audio/mixer.*` reproduces `audio_mixer.vhd`'s 13-bit unsigned sum exactly:
EAR contributes 512, MIC 128, the AY pair is zero-extended, the DAC pair is
shifted left by two, the I2S pair is zero-extended. `Mixer::mix` is a pure
function of the sources with no state.

Two things around that sum are jnext's own, and both were bug fixes:

- **Box filtering, not point sampling.** `accumulate()` integrates the source
  levels over a span of emulated master cycles and `emit_sample()` divides by the
  span. Output samples are ~635 master cycles apart and a beeper engine toggles
  far faster, so point-sampling discards most toggles and folds their energy back
  into the audible band as a whistle. `Emulator::advance_audio` walks each
  instruction's span in chunks that stop at output-sample boundaries, making the
  weighting exact at instruction granularity.
- **AC coupling.** `Mixer::MIX_REST_LEVEL` is the sum a fully silent machine
  produces (DAC midpoints plus the I2S idle level); `emit_sample()` subtracts it,
  so digital silence really is zero, exactly as the hardware's output capacitor
  blocks DC. Getting this constant wrong does not sound like a DC offset — it
  makes every buffer seam a step of that height, heard as a click train.

Output is stereo `int16_t` at 44 100 Hz into a 4096-pair ring buffer (~4 frames)
that drops the oldest sample on overrun. Two optional callbacks tap the same
samples, for `--record` video muxing and for `--wav-record` (`AudioRecorder`).
Per-source gains and the debugger's mute mask are host settings, not machine
state, and are excluded from `reset()` and from snapshots.

## Getting samples to the host

`src/platform/sdl_audio.*` is the only place SDL audio appears.
`push_from_mixer()` drains the ring buffer, converts through an `SDL_AudioStream`
and queues to the device. It refuses to push when the queue is already past
`QUEUE_MAX_MS`, and *never* clears it — discarding queued or freshly-read samples
punches a hole in the stream, which is a click.

Headless opens no device; the mixer still synthesises and the ring buffer simply
self-limits. `--silent` makes `Emulator::tick_devices_after_instruction` skip PSG
ticking and mixer synthesis entirely — register writes still land, only the
oscillator work is skipped.

## Pacing — pure host policy

This has no hardware counterpart and decides whether jnext sounds broken. The
mixer emits exactly 44 100 samples per *emulated* second, but a frontend driven
by a 20 ms timer runs 50.00 frames per *real* second (see
[A frame end to end](../02-architecture/03-a-frame-end-to-end.md)), and no
machine's frame is exactly 20 ms. That small permanent mismatch drains the device
queue to empty within ~20 s, after which SDL pads with zeros several times a
second, forever.

The fix is to pace on the sound card instead. `src/platform/audio_pacing.h` holds
the whole policy as pure constexpr functions: `frames_for_tick()` returns 0, 1 or
2 emulated frames for this host tick from the device queue depth, holding it in a
40-90 ms band. It acts on an EMA of the readings, not raw ones (the device
consumes in ~23 ms chunks, whose sawtooth would clip the band edges constantly),
feeds each intervention's effect forward so filter lag cannot over-correct, and
keeps two raw-reading emergency guards outside the smoothing. The header records
the alternatives modelled and rejected; read it before changing a threshold.

When the host genuinely cannot keep real time, no pacing can conjure the missing
samples. `SdlAudio` then pads the queue up to `QUEUE_FLOOR_MS` by repeating the
last emitted level — a DC hold has no discontinuity, so a starved host stutters
instead of clicking. Because that inserts samples the emulator never produced, it
is armed only when pacing is in charge: not at `--speed` other than 1x, and not
during a tape fastload, where emulated time is deliberately decoupled from real
time.

Ordering and lifetime around all this live in `src/platform/frame_sequencer.h`,
used by the Qt frontend and deliberately open-coded by the SDL one. It exists
because the *policy* was well tested while the *wiring* was not: constructing a
fresh pacing `BandState` per tick instead of persisting it passed the entire
automated gate, and was caught only by watching a live cadence log. See
[Testing](../04-testing/index.md).
