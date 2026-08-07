# 3.4 Audio

A ZX Spectrum Next makes sound from four kinds of source, summed into one stereo
pair: three AY-3-8910-compatible sound chips, four 8-bit sample DACs, the
original Spectrum's one-bit beeper, and a digital audio input from the Raspberry
Pi interface. jnext models all four and sums them exactly as the FPGA's mixer
does — and then has to do something the hardware never has to do, which is hand
a stream of samples to a host sound card whose clock is not the emulated
machine's clock.

That last step is why audio splits in two, and why keeping the halves apart
matters more here than anywhere else in jnext. Everything in `src/audio/` up to
and including the 13-bit sum models the FPGA's audio path and is checkable
against `ym2149.vhd`, `soundrive.vhd` and `audio_mixer.vhd`. Everything after
it — the ring buffer, the SDL device, and above all the *pacing* — is host
policy with no VHDL counterpart. A change on the first side is a
hardware-fidelity question; a change on the second is a "does it sound right on
this machine" question, and the two are settled by completely different kinds of
evidence.

Nothing in the emulated half knows the host exists. The sources are advanced
from the instruction loop (see
[A frame, end to end](../02-architecture/03-a-frame-end-to-end.md)) and the
mixer writes into a ring buffer; if no one ever drains it, the machine runs on
exactly as before, silently. That is what makes `--headless` and `--silent`
cheap rather than special cases.

## The emulated hardware

**AY / YM2149** — `src/audio/ay_chip.*`. The AY-3-8910 is the 128K Spectrum's
sound chip, and almost all Spectrum music is written for it: three square-wave
tone channels, one noise generator, a shared envelope generator, and sixteen
registers reached through a select/data port pair. One `AyChip` is a full
YM2149 — the tone counters, the 17-bit noise LFSR, the envelope generator with
its sixteen shapes, and both the AY and YM volume tables taken from the VHDL. It
is advanced by `tick()` at the PSG clock-enable rate, 28 MHz / 16 = 1.75 MHz,
and exposes three 0-255 channel levels.

**TurboSound** — TurboSound is the Next's name for carrying three of those chips
at once, all sharing the same pair of ports: writing a control value to port
0xFFFD chooses which chip subsequent register writes reach, so a program has
nine tone channels available where a 128K has three, plus per-chip stereo
placement. `src/audio/turbosound.*` owns the three chips and everything around
them — the chip selection, per-chip panning, the ABC/ACB stereo mode (NR 0x08
b5), the per-chip mono flags (NR 0x09 b7:5), the TurboSound enable (NR 0x08 b1)
and AY-vs-YM mode (NR 0x06 b0). It recomputes a 12-bit stereo pair on every PSG
tick, and the mixer only ever reads that latched pair.

The two resets are distinct and the difference is load-bearing. `reset()` is
power-on and clears everything; `reset_ay_only()` models the VHDL
`audio_ay_reset` fired by an NR 0x06 `psg_mode = 11` write, which must *not*
clear the NR-driven enable, stereo and mono inputs, because in hardware those
are external signals into the chip rather than state inside it.

**DAC** — `src/audio/dac.*`. Where an AY synthesises a waveform, a DAC simply
plays back: write a byte, get a voltage, and the program is responsible for
feeding it fast enough. Several third-party Spectrum peripherals did exactly
that — Specdrum, Soundrive, Covox — each answering on its own port addresses,
and the Next decodes all of them onto the same four 8-bit channels. A+B sum to
left, C+D to right, giving a 9-bit value per side; each channel resets to 0x80,
which is its *silence*, not zero.

The port decode does not live in `dac.*`. It is in `Emulator::init` in
`src/core/emulator.cpp`, because each port is gated by NR 0x84 bits exactly as
the VHDL decodes them:

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

**Beeper** — `src/audio/beeper.*`. The beeper is the 48K Spectrum's entire sound
system: a single bit in port 0xFE that the CPU toggles, with everything from
clicks to three-channel music produced by how fast it toggles. jnext's model is
deliberately tiny — three booleans, being EAR and MIC from port 0xFE bits 4 and
3 plus a tape-EAR input from real-time tape playback. There is no accumulator
and no filtering here, because that is the mixer's job.

**Pi I2S** — `src/audio/i2s.*`. A Next can take digital audio in from a
Raspberry Pi attached to its interface, and mix it with everything else. jnext
models the destination but not the journey: a latched 10-bit sample pair plus the
NR 0xA2 control byte, with no protocol emulation. The module exists because the
mixer sum includes it, and its *idle* value (0x200, offset binary) is
load-bearing.

## The mixer

`src/audio/mixer.*` reproduces `audio_mixer.vhd`'s 13-bit unsigned sum exactly:
EAR contributes 512, MIC 128, the AY pair is zero-extended, the DAC pair is
shifted left by two, and the I2S pair is zero-extended. `Mixer::mix` is a pure
function of the sources, holding no state of its own.

Two things around that sum are jnext's own rather than the hardware's, and both
arrived as bug fixes:

- **Box filtering, not point sampling.** `accumulate()` integrates the source
  levels over a span of emulated master cycles and `emit_sample()` divides by
  the span. Output samples are roughly 635 master cycles apart while a beeper
  engine toggles far faster, so point-sampling would discard most toggles and
  fold their energy back into the audible band as a whistle.
  `Emulator::advance_audio` walks each instruction's span in chunks that stop at
  output-sample boundaries, which makes the weighting exact at instruction
  granularity.
- **AC coupling.** `Mixer::MIX_REST_LEVEL` is the sum a fully silent machine
  produces — the DAC midpoints plus the I2S idle level — and `emit_sample()`
  subtracts it, so digital silence really is zero, exactly as the hardware's
  output capacitor blocks DC. Getting this constant wrong does not sound like a
  DC offset. It makes every buffer seam a step of that height, heard as a click
  train.

Output is stereo `int16_t` at 44 100 Hz into a 4096-pair ring buffer, about four
frames' worth, which drops the oldest sample on overrun. Two optional callbacks
tap the same samples, one for `--record` video muxing and one for `--wav-record`
(`AudioRecorder`). Per-source gains and the debugger's mute mask are host
settings rather than machine state, so they are excluded from `reset()` and from
snapshots.

## Getting samples to the host

`src/platform/sdl_audio.*` is the only place SDL audio appears. The device is
opened in *callback* mode (GH #208): SDL's audio thread pulls samples from a
fixed-size ring buffer owned by `SdlAudio`, and `push_from_mixer()` drains the
mixer into that ring once per frontend tick. It refuses to push when the ring
is already past `QUEUE_MAX_MS`, and it *never* clears the ring — discarding
samples that are queued or freshly read punches a hole in the stream, and a
hole in the stream is a click.

The callback's shortfall policy is the pure header
`src/platform/audio_fill.h`: whatever the ring cannot cover is filled by
repeating the last *real* sample pair delivered — a DC hold, never zeros —
so the device can never play content jnext did not choose, no matter what the
GUI thread is doing. Before this, the queue-push model let SDL inject zeros
whenever the queue ran dry, which over any nonzero programme content is an
audible click train (issues #7/#208).

Headless opens no device at all; the mixer still synthesises and the ring buffer
simply self-limits. `--silent` goes further and makes
`Emulator::tick_devices_after_instruction` skip PSG ticking and mixer synthesis
entirely. Register writes still land, so the machine state stays correct — only
the oscillator work is skipped.

## Pacing — pure host policy

This has no hardware counterpart at all, and it decides whether jnext sounds
broken. The mixer emits exactly 44 100 samples per *emulated* second, but a
frontend driven by a 20 ms timer runs 50.00 frames per *real* second (see
[A frame, end to end](../02-architecture/03-a-frame-end-to-end.md)), and no
machine's frame is exactly 20 ms. That small permanent mismatch drains the
device queue to empty within about twenty seconds, after which every shortfall
plays content the emulator never produced — under the original push model,
SDL-injected zeros several times a second, forever.

The fix is to pace on the sound card instead of on the wall clock.
`src/platform/audio_pacing.h` holds the whole policy as pure constexpr
functions: `frames_for_tick()` decides from the device queue depth whether this
host tick runs 0, 1 or 2 emulated frames, holding the depth in a 40-90 ms band.
It acts on an EMA of the readings rather than raw ones, because the device
consumes in roughly 23 ms chunks and that sawtooth would clip the band edges
constantly; it feeds each intervention's effect forward so filter lag cannot
over-correct; and it keeps two emergency guards on the raw readings, outside the
smoothing. The header records the alternatives that were modelled and rejected,
so read it before changing a threshold.

When the host genuinely cannot keep real time, no pacing policy can conjure the
missing samples, and two layers of last-level hold take over (GH #208). The
tick-side layer: `SdlAudio` pads the ring up to `QUEUE_FLOOR_MS` by repeating
the last emitted level, because a DC hold has no discontinuity in it — a
starved host stutters instead of clicking. Since that inserts samples the
emulator never produced, it is armed only when pacing is in charge: not at a
`--speed` other than 1x, and not during a tape fastload, where emulated time is
deliberately decoupled from real time. But that pad only runs when a tick runs,
so it cannot bridge a tick that never arrives — a GUI stall, a degraded timer,
a host throttled below real time. The device-boundary layer covers exactly
that: the SDL audio callback holds the last real pair for any shortfall
(`audio_fill.h`), on SDL's audio thread, independent of tick cadence. Fill
activity is reported on the periodic `cadence:` log line as
`audio-fill: N ms (M events)` — on a healthy host it never appears.

The ordering and lifetime around all of this live in
`src/platform/frame_sequencer.h`, used by the Qt frontend and deliberately
open-coded by the SDL one. It exists because the *policy* was well tested while
the *wiring* was not: constructing a fresh pacing `BandState` every tick instead
of persisting it passed the entire automated gate, and was caught only by
watching a live cadence log. See [Testing](../04-testing/index.md).
