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
and AY-vs-YM mode (NR 0x06 b0). All three chips advance on every PSG tick, whether or not
TurboSound is on or the chip is selected, as in the VHDL (every ym2149 runs on the
ungated `clock_en_i`). The enable and the selection decide only which chips reach the
mix: with TurboSound off, the selected one. The latched 12-bit stereo pair is
recomputed only when a chip output or a mix input changed, and the mixer
only ever reads that latched pair.

There are four entry points, and the differences are load-bearing. `power_on()`
(construction and a hard reset, which on hardware reconfigures the FPGA) puts every
chip at its declared initial values. `reset()` is the VHDL `reset` pulse, which is what
a soft reset is: RESET_H on the chips clears the address, the register file (R7 =
0xFF) and the outputs, and nothing else, so the tone, noise and envelope generators
and the divider keep running; it also clears the NR-driven enable, stereo and mono
inputs, which `Emulator::init` re-derives from the NextReg shadows.
`reset_ay_only()` is the same RESET_H effect without touching those NR-driven inputs,
because in hardware they are external signals into the chip rather than state inside
it. `set_reset_held()` is the NR 0x06 `psg_mode = 11` level (`audio_ay_reset` is a
level in the VHDL, not a pulse): while it is set, port writes and the select/pan
command are ignored and the outputs stay 0, but the generators keep running. The
emulator applies `reset_ay_only()` when the hold starts and re-derives the level from
NR 0x06 after `init()` and after a state load; it is not serialised. The PSG
clock-enable phase (`psg_accum_`) is a free-running divider and also survives a soft
reset. At power-on the PSG mode is YM, and `init()` derives AY or YM from the NR 0x06
value like the other NR 0x06 shadows.

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
| 0xFB | A **and** D (Pentagon/ATM, mono) | b5, effective only while b2 is off (`zxnext.vhd:2749`) |
| 0xB3 | B **and** C (GS Covox) | b6 |
| 0xDF | A **and** D (Specdrum, mono) | b7 |
| NR 0x2C / 0x2D / 0x2E | B / A+D / C | — |

Every write is additionally gated on NR 0x08 b3; with the DAC disabled the
Soundrive module is held in reset. The family decode comes first: a port whose
NR 0x84 family bit is off is not a DAC port at all, whatever NR 0x08 says. The mono ports writing *two* channels is real
hardware behaviour and a routine source of confusion — a program that expects
0xDF to be "channel D" gets A as well.

**Dropped writes.** Every audio write the machine discards is counted by the
reason the VHDL gives for discarding it, so a debugger can say why a program is
silent. `AudioDropReason` (`src/debug/events.h`) has eight reasons, and a write
gets the first that applies: decode before gate, because an undecoded write is not
a write to that device. The AY port gate (NR 0x84 b0) comes before the reset hold
(NR 0x06 `psg_mode = 11`), which comes before the 0xFFFD value and the register
address. The decision stays where it is made: `TurboSound::reg_addr()` and
`reg_write()` return an `AyPortStatus`, and `AyChip::write_data()` returns false for
an address of 16-31; `Emulator::note_ay_status_()` maps the status to a reason, and
each DAC handler's gate goes through `Emulator::dac_gate_()`, family first, then the
DAC enable. The 0xF1 and 0xF9 handlers count a closed Soundrive 2 family and then
decline, so the byte still reaches the paging decode (`zxnext.vhd:3024`); the 0xFB
handler counts its neither-decode case. The 0xDFFD write is not counted: its
handler wins dispatch while NR 0x82 b2 is set, so the AY side never sees it.
`Emulator::note_audio_drop_()` records the count and the last port, value, PC, frame
tag and source; the source is `Debugger` outside guest execution, else the live
write-source bracket the DMA and the Copper set around their port writes. The
counters are host state: outside `describe_state()`, `init()` and `load_state()`,
carried over a cold boot by `emulator_cold_boot()` beside the mute mask, cleared
only by `Debugger::clear_audio_drops()`, and not counted while a rewind replays
frames. `Debugger::audio_state()` publishes them as `AudioState::drops`.

**The write tap.** Every decoded audio write can also be logged, for the debugger's Log
tab and `--audio-log`. `Emulator` holds one `AudioTapSink*` (`src/debug/audio_tap.h`,
the core-to-backend seam, implemented by the debugger backend) and one bool,
`audio_tap_active_`; the backend switches it on while any client asks for the log or a live `AudioWrite`
subscription exists (`Emulator::set_audio_tap()`, called by `publish_audio_tap()`). The sites are the three AY handlers (0xFFFD, 0xBFFD and
0xBFF5), the 14 DAC channel writes in the 11 DAC handlers (`Emulator::dac_write_()`
wraps each), the three NextREG 0x2C-0x2E mirror handlers, the 0xFE handler and
`note_audio_drop_()`, so the counter and the log come from one call. Each site tests
the one bool and nothing else, and there is no test on the per-instruction path.
Rule R1: the tap observes and never writes. The sites differ in order: the AY handlers (0xFFFD, 0xBFFD, 0xBFF5) read
what the entry needs (the chip, the address, the previous byte) before the write and record after it, while the DAC
writes (`dac_write_()`), the NR 0x2C-0x2E mirrors and the 0xFE handler record BEFORE it (the previous value is
still in place, and `tap_beeper_` must run before `beeper_.set_ear()`). Either way the hot path's own writes stay
where they are, and the log cannot drift from them.
The sink has two consumers, the log ring and the `AudioWrite` event latch (`09-2`), and the core knows of neither.
The cold bodies are in `src/core/emulator_audio_tap.cpp`, marked cold and out of line,
so adding one cannot move inlining in `emulator.cpp`. They return at once while a rewind
replays frames (`replay_mode_`). A plain register select is not logged; an applied
0xFFFD write with bit 7 set is exactly the chip-select or pan command, because a bit-7
value that is not the command returns `SelectIgnored`. An entry's timestamp comes from
`Emulator::stamp_at_()`, the arithmetic the event latch stamper also uses, so the log
agrees with the event system by construction; the stamp is the start of the instruction
slot that raised the write, which is also why a rewind landing removes the entries at its
own cycle. The landing removes everything from that cycle on, markers included: the rewind ring is this
session's own history on one clock, and a soft reset, a rewind and a state load taken from it all keep that
clock (loading a checkpoint of this session puts it back to the checkpoint's cycle), so every marker is
crossed. A state-load marker AT the landing cycle is the landing state itself and stays, just before the
new `Rewind` marker. A state loaded from another session has a clock that cannot be ordered against this
one, so across such a load the cycle order, and the result, is best-effort. The `Rewind` marker is always
recorded, and only the same marker twice at one cycle is one row, so a reset right after a rewind keeps its
own row. Markers come from `debug_after_machine_transition_()` (every reset and every
state load pass through it; a ring restore for a rewind is left out by
`DebugState::restoring_from_rewind()`) and from the two rewind landings. A hard reset
rebuilds the `Emulator`, so the backend installs the sink again in
`reapply_after_machine_rebuild()`. The ring, the seq counter and the verbs live in the
backend (`src/debug/debugger_audio.cpp`).

**Pacing engines.** `Debugger::ctc_state()` and `Debugger::dma_state()` (INS-25) report what paces a
sample player: the CTC's four channels and the DMA's prescaler. Both read through a `DebugView` that
is defined inline in `CtcChannel` and `Dma` (`ctc.h`, `dma.h`): no data member, so no layout change, and
no core translation unit calls it, so no code is emitted in a hot path; an unused debugger pays nothing.
The CTC rate is the ZC/TO rate: `MASTER_CLOCK_HZ / (prescaler x TC)` for a running timer (TC 0 counts
256; `ctc_chan.vhd:132-170`), the source's rate divided by TC for a running counter. A counter's
source is the previous channel, channel 0 being fed by channel 3 (`zxnext.vhd:4598`, `Ctc::handle_zc_to`
arms `(channel + 1) & 3`); `Debugger::ctc_state()` walks that chain back at most four hops, a stopped
channel or a ring of counters ends it with no rate, and the division is rounded once, at the end, in
milli-hertz. A channel is `running` only in S_RUN or S_RUN_TC, the states in which `t_count` counts. The
DMA rate is the NOMINAL pacing rate `MASTER_CLOCK_HZ / (32 x prescaler)`, the same at every CPU speed (the
DMA timer adds 8, 4, 2 or 1 per CPU clock at 3.5, 7, 14 and 28 MHz, one per master cycle, `dma.vhd:250-255`):
the gate (`dma.vhd:424`) is a minimum spacing from the read, and the state machine's own cycles come
on top, so a real stream is slower than the nominal rate.

**The sample stream.** `dac_stream_stats()` (`src/debug/dac_stream.h`, a published header with no core
dependency) turns the audio log into one row per DAC writer: the measured rate, the interval minimum,
mean, maximum and lower median, the population standard deviation and the gaps. Its rules are small and
each is pinned by a row. An entry counts if it is an applied DAC or mirror write after the newest marker
of the vector (a reset restarts the player, a rewind truncated the log) inside the window `(end - window,
end]`; a clock younger than the window has no start. The log has one entry per DAC channel, so a write to
0xDF, 0xB3, the mono 0xFB or NextREG 0x2D is two entries at one cycle: consecutive counted entries with
equal cycle, source, PC and port are ONE sample. A CPU writer is keyed by its PC; DMA, Copper and debugger
entries by source alone, because their PC is the slot's, not theirs. A gap is an interval strictly above
1.5 times the lower median: one skipped sample is two periods, one period is one, so 1.5 sits between them
whatever the interrupt acceptance jitter does to a single interval. `nearest_pacing()` pairs a CPU writer
with the running CTC channel whose rate is nearest and a DMA writer with the DMA. The DMA's own spacing
is at least the prescaler's (`dma.vhd`, the timer cleared at TRANSFERING_READ_1 and the gate at WAITING_CYCLES), so its measured rate is at most the nominal one.

**Frame grouping and the tracker.** `src/debug/audio_frames.h` is a pure, published helper that
groups the log's entries, oldest first, into one `AudioFrame` per frame that holds counted AY
writes (`AudioFrameAccumulator::feed()`). A counted write is an applied `AyRegister` entry (no
`Dropped` flag, chip 0-2, register 0-15); any other non-marker entry only proves the open frame
ended. A frame keeps the last value written to each of R0-R13, the value at its start (`before`:
the `prev` of the frame's first write to the register, which is exact because the AY stores the whole
written byte; for a register not written in the frame, the value carried from the previous frame, or
unknown after a marker), a mask of the registers written and of those
written twice, the first and last raster stamps and the PC range of the CPU's writes, and the number
of empty frames before it. A marker, or a frame number that goes backwards, completes the open frame
and forgets the timeline. The Audio panel's Tracker (`src/debugger/audio_tracker_view.*`) is its
consumer. The Tracker mirrors the log by the Log
view's own rule, one function (`fetch_audio_log()`: append the entries after the newest one held, or
resync when that entry is gone) and groups only what is new, keeping at most `kMaxRows` rows. Nothing
in the emulation path changes for it: it reads what the write tap already records.

The player timing strip under the Tracker's table is the header's second consumer.
`audio_timing_stats()` takes completed frames, oldest first, and a window of frame numbers ending at the
newest, and returns min / avg / max of the first and last write's line and T-state and of the write
count, the union of the CPU PC ranges, and the frame numbers with no write. It is total on any input:
a frame number that does not go up starts a new run and only the frames after the last break count,
and the frame numbers before the oldest frame used, minus its `empty_before`, are not counted as empty
because the log says nothing about them (its ring, a marker, the row cap). The view feeds it
`AudioTrackerModel::timing_frames()`, the model's own rows: the open frame's tail is left out, a reset
or state load row ends the walk and a rewind row does not. The strip is recomputed by
`AudioTrackerView::update_timing()` at the end of `refresh()`, so it costs nothing while another
sub-tab is current, and again when the window box or the current row changes. The window box is a
view control with no setting. The dot plot, `AudioJitterPlot`, scales Y to the window's own lines.

**Beeper** — `src/audio/beeper.*`. The beeper is the 48K Spectrum's entire sound
system: a single bit in port 0xFE that the CPU toggles, with everything from
clicks to three-channel music produced by how fast it toggles. jnext's model is
deliberately tiny — three booleans, being EAR and MIC from port 0xFE bits 4 and
3 plus a tape-EAR input from real-time tape playback, and one derived value,
`mic_final`, the board's `beep_mic_final` (`zxnext.vhd:6503`), which is what
the mixer's MIC term reads. `mic_final` is not in the saved state: the
emulator refreshes it from `Emulator::tape_out_level` at the events that change
it. There is no accumulator and no filtering here, because that is the mixer's
job.

**Pi I2S** — `src/audio/i2s.*`. A Next can take digital audio in from a
Raspberry Pi attached to its interface, and mix it with everything else. jnext
models the destination, not the wire: a latched 10-bit sample pair plus the
NR 0xA2 control byte, with no I2S protocol emulation. Its *idle* value (0x200,
offset binary) is load-bearing. The samples come from **NextPi**: QEMU's `wav`
audio back-end writes the emulated Pi's sound into a FIFO (a named pipe on
Windows, `pi_audio_win.cpp`), `src/audio/pi_audio.*` reads it on a thread into a ring, and the emulator latches one frame per mixer
output sample into `I2s` (`Emulator::feed_pi_audio`), or 0x200 when none is
ready. `PiAudio` absorbs the drift between QEMU's host clock and the emulated
one: it prebuffers 50 ms, plays silence on an underrun and prebuffers again, and
trims a backlog past 300 ms back to 100 ms. NR 0xA2 bit 0 sends the Pi's audio
to EAR instead: `I2s::fe_ear()` is the hardware's comparator with hysteresis
(`zxnext.vhd:2677-2690`), and `Emulator::audio_ear_in` folds it into the port
0xFE bit 6 read and the MIC output through the board's `ear_relax`. While it
does (`I2s::ear_routed`), `PiAudio` runs in tape mode: a 250 ms prebuffer, and
drift taken out one frame at a time, because a tape loader cannot survive the
refill's gap or a trim. The chapter on peripherals describes how NextPi itself
runs.

## The mixer

`src/audio/mixer.*` reproduces `audio_mixer.vhd`'s 13-bit unsigned sum exactly:
EAR contributes 512, MIC 128, the AY pair is zero-extended, the DAC pair is
shifted left by two, and the I2S pair is zero-extended. `Mixer::mix` is a pure
function of the sources, holding no state of its own.

The hardware mixer has no tape input. A tape playing in real time, and the Pi's
audio when NR 0xA2 sends it to EAR, reach the mix only inside the MIC term:
`beep_mic_final` is the board's EAR input XOR the port 0xFE MIC bit (on the
issue 3 keyboard setting), so a tape swings the mix by 128, a quarter of a
beeper EAR step, and a tape XOR a set MIC bit is heard inverted. The EAR input
itself is the jack through the board's `ear_relax`: a level held with no edge
for 64 x 512 master cycles relaxes to the issue 2 MIC level. jnext applies that
to both sources of the XOR: the Pi's comparator edges and the playing tape's own
level changes (`tape_edge_master_`, stamped where the tape block sees the level
move). A tape pause holds its last level for a second, so the relaxation is what
a loader sees after the last pulse of a block. The stamp is not saved: after a
restore (`load_state`, a `.jns` deck reattach) `derive_tape_edge_` rebuilds it
from the deck's position (a `.jns` load calls it again after the deck is
reattached, as `load_state` runs before that). `TapLoader::ticks_since_edge` is exact for every phase
(the pulse or pause length minus `pulse_left`) except the very first pilot pulse of
a tape started from idle, which has no edge, so a restore inside it is approximate
(it matters only on issue 2 with MIC set); `WavLoader::ticks_since_edge`
looks back along the recording (to twice the hold, all the relaxation needs);
`TzxLoader::ticks_since_edge` is exact in the pause (the 1 ms hold and the pause's
remainder, lengths the player holds) and 0 in a pulse phase, because the ZOT
player does not keep the length of the pulse it has just started: the level is
then taken as freshly changed, which keeps it as the board has it until the
relaxation time, at most one pulse late for a pulse longer than that. A pause of 1 ms
or less after a block that ends high forces the level low with no visible edge, so
there the age counts from the pause.
`Emulator::tape_out_level`
is the one model of that signal (the MIC jack of a tape save reads the same
value), and it is refreshed into the beeper at the events that change it: a port
0xFE write, an NR 0x08 or NR 0xA2 write, a state load, a replay-mode change, and a
level change of a playing tape, in the instruction of the tape block that shows
it (one compare per instruction while a tape plays, nothing when none does).
`Emulator::mic_stale_` covers what has no event of its own, at each
output-sample boundary: a tape that started or stopped, and the Pi's relaxation
hold running out (also inside a HALT batch). A tape edge is exact to the
instruction, as EAR is, because the mixer integrates the term across the sample.
A state load also re-derives the NR 0x06
shadows (speaker-exclusive gate, NMI button enables, PS/2 mode, hotkey enables)
from the restored register.

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
snapshots, and the cold boot of a hard reset carries both across (the gains in
the boot config, the mask in `emulator_cold_boot()`).

The mask must not reach a snapshot, and the TurboSound sum is state, so `TurboSound` keeps two
sums: the hardware one (every active chip; what a snapshot or a rewind slot stores, under the keys
`pcm_l` and `pcm_r`) and the audible one (`pcm_left()`, `pcm_right()`, `chip_pcm()`), which leaves out the
chips the mask mutes. A load sets the audible pair equal to the loaded hardware pair, and the first PSG
tick rebuilds both. A state saved while a chip is muted is therefore byte-identical to one saved unmuted.

The beeper bit of the mask zeroes the mixer's `ear` and `mic` terms, and because a playing tape reaches the mix
only through `mic_i` (above), it silences the tape and the Pi's audio routed to EAR as well; the Audio panel's Tape EAR lamp still shows the raw input
(`BeeperState::tape_ear`), so a muted tape is visibly playing.

**The source tap.** The debugger's Scopes view needs the level of each source before the mixer sums
them, which the mixer does not keep. `AudioSourceTap` (`src/audio/audio_source_tap.h`, standard headers
only) holds sixteen accumulators and a 8192-sample ring per source; the Debugger owns it, the Emulator
holds a raw pointer (`set_audio_scope_tap()`), and it is not machine state: not saved, not reset, never
read by the Z80. `Mixer::integrate_span_tapped()` is `integrate_span()`'s loop, chunk for chunk, with
`tap_accumulate_()` beside the audible `accumulate_impl_()`: the sixteen terms are the hardware ones
(`Dac::channel()`, `AyChip::output_a/b/c()`, `TurboSound::hw_pcm_left()` and the beeper's EAR and `mic_final()` levels: the
playing tape and the Pi's audio routed to EAR have no term of their own, they arrive inside `beep_mic_final`
(v1.3.62), so they show in the Beeper source as its MIC 128), computed beside `mix()` and never inside it, so no mute and no host gain reaches them.
Each pushed sample is the mixer's own `rounded_average_x4` of a term over one output period, so the
Left and Right traces equal the mixer's output plus its resting level exactly. The sync rule: a tap
sample is pushed only when the tap saw the mixer's whole sample (compared by cycle count), which covers a
tap enabled mid-sample, a reset, a state load and a switch between the tapped and untapped paths.

Nothing may be added to the hot audio path when no tap is installed (a debugger feature that is unused
costs nothing). A test per instruction slice was rejected, and so was a second per-slot accumulate
entry point (a second caller of a hot function stopped the compiler inlining it, 1 to 7 % of every
program). Instead the device cluster's `replay_mode_` test became a test of one byte,
`Emulator::audio_divert_` (bit 0 a replay, bit 1 a tap installed): the same load, test and branch, so
zero costs nothing extra, and a non-zero byte goes to `audio_divert_span_()`, which is cold and out of
line and calls `integrate_span_tapped()` (a replay mixes nothing). `refresh_audio_divert_()` runs at
every writer of `replay_mode_` and of the tap pointer.

The HALT and raster-poll batches do not feed the tap: a test in their replay loop cost one instruction
per replayed segment (0.1 % of whole programs, measured), and a twin of the replay cost more elsewhere.
While a tap is installed `batch_window_limit_()` refuses every window (it tests `audio_divert_`, which
it already did for a rewind replay as `replay_mode_`), so those programs run slot by slot, the path the
batches are held equivalent to, and the tap sees exactly the batch-off samples. Batching resumes when
the tap is dropped.

**The WAV stems.** The same tap carries seven more terms while a stem recording runs
(`AudioStemTap`, hung on `AudioSourceTap::stems`): each AY chip's left and right, the DAC's, the beeper,
the Pi's and the mix, in the mixer's 13-bit levels times master cycles, accumulated in
`tap_accumulate_()` only when the pointer is set. They are the hardware terms of `audio_mixer.vhd`
(`pcm = ear + mic + ay + dac + i2s`, one unsaturated register), so every one is before the debugger mutes
and the host gains: `TurboSound::hw_chip_pcm()`, `Dac::channel()` (not the audible copy), the beeper's `exc_i`-gated
EAR and `mic_final()`, `pi_audio_L/R()` (or its rest level), and the mix. At each output-sample boundary
a frame is pushed, one value per stem and channel, each the mixer's own x4 round-to-nearest average less
the source's rest level (AY 0, DAC 4096, beeper 0, Pi 2048; the four add up to the mixer's `MIX_REST_LEVEL * 4` of
6144, so the AC coupling splits exactly), so the mix stem equals `--wav-record` at 0 dB. The stems have
their own sample counter and the same whole-sample rule as the scope.

*Where the sum is taken, and its bound.* The identity is on the hardware 13-bit mix, time-averaged by
the mixer's box filter and scaled by four; everything after it (output gain, per-source gains, mutes,
the int16 clamp) is host-side and absent from the stems. Nothing in between saturates or filters
(the VHDL sum is at most 5998 of 8191, the AY sum never wraps). Each file is one rounding of an exact
average x_i = 4 acc_i / cycles, and the mix is one rounding of their sum M. With round(x) = floor(x + 1/2)
the error r = x - round(x) is in [-1/2, 1/2), so the sum of the six source files minus the mix file
is r_M - (r_1 + ... + r_6), an integer in (-3.5, 3.5): at most 3 counts. It is zero when every
4 acc_i is a multiple of the cycle count, which holds whenever each term is constant over the sample.

*The per-chip latch.* A chip's stem must be the `psgN_L/R_pan` register whose sum the mixer heard
(`turbosound.vhd:323-335`), but `TurboSound` keeps only the sum, and recomputing a chip's value on the fly
disagrees with the latched sum between a pan, select, mono, ACB or enable write and the next PSG tick (up to 16 master
cycles; at 28 MHz an `OUT` is shorter than that). So `set_hw_chip_latch(true)` makes the cold
recompute (`compute_stereo_mix_channels_()`) store each chip's hardware pair beside the sum. The test that
sends the recompute out of line is the one byte `mix_divert_` (bit 0 a channel mute, bit 1 the latch), which
replaced the `channel_mute_mask_ != 0` test of the same position: no instruction was added to the hot loop.
Before the first latched recompute (just after the latch is switched on, or after a state load) `hw_chip_pcm()`
computes the pair from the current inputs, which is exact unless an input change is pending, and that can
only be for one PSG tick. The latch is not machine state: not serialised, and `reset()` only re-validates it.

*Alignment.* A frame is pushed for every sample the mixer emits, from the first sample that starts after the
recording starts. `Emulator::init()` discards the mixer's partial sample, so it also calls
`AudioSourceTap::clear_partial()`, and so does every `set_audio_scope_tap()` (which also switches the latch
to match `stems`): otherwise the next sample would not be whole for one of the two and one stem frame would be
lost per reset. A cold boot builds a new `Emulator` whose tap is installed by the backend's
reconstruct re-application, before its first frame. Stem recording installs the tap, so it refuses the HALT and
raster-poll batches like a scope does.

`StemRecorder` (`src/debug/stem_recorder.*`, internal to the backend) owns the `AudioStemTap` and seven
`AudioRecorder`s; the `Debugger` holds it as a recording, not a client request (`stems_record_start()` and
`stems_record_stop()`, INS-24), so tabs and windows do not end it and `--wav-record-stems` reaches the
same path through `app.debugger()`.

The envelope generator takes one step right after every R13 write, as the VHDL does: the write pulses
`env_reset`, which reloads the level and also forces `env_ena` (`ym2149.vhd:340-342` and `ym2149.vhd:393-409`), so the first
step comes with the write instead of one envelope period later. `AyChip::load_envelope()` applies the reload
and then runs `step_shape_()`, the half of `update_envelope()` that follows the period counter.

A second host mask, the 13-bit per-channel one (`AudioChannelMute`, set through
`Emulator::set_audio_channel_mute_mask()`), follows the same rules and composes with the first. Its nine AY
bits go to `TurboSound`, which zeroes the muted channel's input BEFORE the chip's A/B/C mux when it builds
the audible pair, so ACB, mono and pan stay exact; the hardware pair never sees them. Its four DAC bits go
to the `Dac`, which keeps an audible copy of its four registers (a muted channel reads its idle 0x80),
updated on each write and on a mute change; `pcm_left()` and `pcm_right()` sum that copy, so the Mixer's
`mix()` is the same code muted or not, and the registers (what the debugger reads and a snapshot stores)
never see the mute. A state load rebuilds the copy. The cold boot carries both masks.

## Getting samples to the host

`src/platform/sdl_audio.*` is the only place SDL audio appears. The device is
opened in *callback* mode: SDL's audio thread pulls samples from a
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
audible click train.

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
smoothing. The estimate is also kept within one device chunk of each reading: the feed-forward credits a catch-up with a frame of audio that a host
too slow to keep real time never actually delivers, and unbounded the estimate
ran away until ordinary low readings tripped the "ahead of the card" arm and
skipped frames on an almost empty queue. The chunk is the opened device's
buffer — `SdlAudio` reads it back with `SDL_GetAudioDeviceFormat()` and logs
`Audio device buffer: N sample frames at F Hz -> pacing estimate envelope E ms`
— rounded up and never below `ESTIMATE_ENVELOPE_MS` (24 ms, SDL3's default
1024-frame buffer at 44.1 kHz).
The header records the alternatives that were modelled and rejected, so read it
before changing a threshold.

When the host genuinely cannot keep real time, no pacing policy can conjure the
missing samples, and two layers of last-level hold take over. The
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
