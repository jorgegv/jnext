# Degradation policy when the host cannot emulate in real time

Issue [#35](https://github.com/jorgegv/jnext/issues/35), on top of issue
[#9](https://github.com/jorgegv/jnext/issues/9).

## Use case

A Next frame has a fixed real-time budget: 20.259 ms at 50 Hz, 17.198 ms at
60 Hz. On a host where emulating one frame costs close to or more than that
budget, JNEXT cannot produce frames as fast as they fall due, and something has
to give. Today it always gives up video: `audio_pacing::frames_for_tick()` runs
two emulator frames in one frame slot whenever the sound-card queue runs low,
and since a tick presents once, after its frames, the first frame of the pair is
overwritten in the framebuffer before any paint can serve it. Sound stays clean
and motion turns lurching, with pairs of frames missing.

That is a defensible default and a bad law. FUSE and several other emulators
make the opposite choice: present every frame, run slower than real time, let
the sound stutter. Both are correct behaviours for different users and different
programs, so the choice belongs to the user.

The reporter in #9 (danboid, Beast on an i7-7700HQ) sits at ~100% of the Next-60
budget with audio on and explicitly expects slow-motion degradation rather than
frame-pair skipping. The instrumented cadence on that machine was ~55 frames/s
emulated, ~35 presented, ~20 superseded, `lost` ~0, ~20 audio doubles/s: the
window system painted everything it was handed, and the missing frames were the
pacing policy's own doing.

## Interface

| | |
|---|---|
| CLI | `--when-slow-prefer audio\|video` |
| `~/.jnext/jnext.conf` | `[startup]` `when_slow_prefer = audio\|video` |
| GUI | **Settings > Preferences > Startup > When the host is too slow** |
| Default | `audio`, i.e. the behaviour that existed before this option |

`audio` keeps the sound card fed and drops video frames to do it. `video` never
drops a frame for the sound card: the frame the pacer asked for runs in the next
slot instead, where it gets its own present, and the machine runs slower than
real time. The audio queue then runs down to `QUEUE_FLOOR_MS` and SdlAudio's
existing underrun guard holds the last sample level to bridge the gap, which is
heard as stutter and a dropped pitch.

The CLI value wins over the saved preference for that run, as with every other
option that has both forms. The Preferences control applies live: the sequencer
reads the policy on its next tick, and no schedule or filter state has to be
re-anchored for it. The setting is inert on a host with headroom, inert under
`--headless` (uncapped, no audio device), and inert whenever audio pacing itself
is off (`--silent`, `--speed` other than 1x, or a fastload burst in flight).

## Design

### One decision changes, and it is applied outside the band controller

The pacer is a closed loop with a modelled response: an EMA over the queue
readings, action feed-forward, and two raw-reading envelope guards, all
calibrated against a chunked model in `audio_pacing_test` (AP-13..AP-16). This
option must not be able to perturb that loop, so it is not a mode flag inside
`frames_for_tick()`. The band decides as it always did, and a separate pure
step maps its answer to what the tick actually does:

```c++
audio_pacing::TickPlan plan_for(int paced_frames, WhenSlowPrefer prefer);
```

`TickPlan` carries the frame count and one flag, `next_tick_asap`. Under
`Audio` the plan is the pacer's answer unchanged. Under `Video` a catch-up
(`paced_frames >= 2`) becomes one frame plus `next_tick_asap`.

Only the catch-up is policy-sensitive, because it is the only outcome that costs
a frame. A 0-frame tick (queue high, or the `QUEUE_MAX_MS` envelope guard)
delays a frame rather than dropping one: the picture holds for one period and
the frame still arrives. Declining it under `Video` would gain no video and let
the queue run past `QUEUE_MAX_MS`, where SdlAudio drops the push outright and
the hole is an audible click, so both policies keep it.

The band's feed-forward has already been applied by `frames_for_tick()` before
`plan_for()` sees the result, and that is right for both policies: video mode
still delivers the extra frame's samples within roughly the same period, so the
queue really does move by one intervention and the smoothed estimate must know
it. Video mode changes when the extra frame runs and whether it is presented,
not whether the intervention happens.

### The catch-up becomes a pull-in, not a refusal

A first design simply declined the double. It is worse in both regimes.

On a host with headroom, the catch-up exists to correct the drift between the
host clock and the sound card's crystal, and it fires a few times a minute. A
mode that refuses it lets the queue walk down to the floor and pad routinely,
so choosing `video` would degrade the audio of a machine that was never slow.

On a host without headroom, refusing the double leaves the frame the sound card
needed unemulated, which is a slower machine for no video gain, since the frame
was going to be presented either way.

So `Video` still runs the frame; it runs it in the next tick, which is pulled in
to the event loop's earliest slot. Each frontend realises that with a mechanism
it already has:

- QtApp re-anchors the frame deadline at now (`frame_deadline::Scheduler::resync_now`),
  so the repeating timer fires at its 1 ms floor;
- SdlApp skips its end-of-iteration `SDL_Delay`, the same fast path fastload uses.

Two frames therefore reach the queue in roughly one period under either policy.
The difference is that the second frame gets its own tick, its own compositor
pass and its own present.

### Why the pull-in re-anchors instead of leaving the deadline behind

`resync_now()` sets the deadline to now rather than advancing it by a period.
Advancing would leave the deadline in the past, and every later tick would
inherit that lateness until it crossed `STALL_PERIODS`, at which point the
existing stall arm resyncs to `now + period` and the machine sits idle for a
whole frame period.

That is not hypothetical on the hosts this option is for. Simulated at the
Next-60 period with a tick costing 1.67 periods (~60% of real time), advancing
inserts a full idle period on every OTHER tick and delivers 26.5 frames/s;
re-anchoring never idles and delivers 33.4. The idle is a periodic hitch, the
same class of artifact the video preference exists to remove, and it costs a
fifth of the frames the host could actually have produced. Re-anchoring keeps
the machine running flat out for as long as it is behind, and the ordinary grid
resumes the moment the queue recovers into the band.

`resync_now()` deliberately does not increment the stall-resync counter.
`resyncs()` is the diagnostic for "the host went away", and a deliberate,
requested re-anchor is not that.

### What the audio does under `video`, and what was rejected

Nothing new. When the emulator cannot produce samples fast enough, the queue
reaches `QUEUE_FLOOR_MS` and `underrun_pad_samples()` tops it up with copies of
the last emitted sample. A DC hold has no discontinuity, so it stutters rather
than clicks. It exists as a rescue and under this policy on a slow host it
becomes routine, which is the cost the user selected.

Two alternatives named in the issue were rejected:

- **Resampling** the emulator's output to the device rate would keep the audio
  continuous at the price of pitch, and it needs a resampler that the project
  does not have. That is a new dependency and a much larger change, for an
  artifact profile that is not obviously better than the hold.
- **Letting the queue drain to empty** hands the shortfall to SDL, which pads
  with zeros: a step from the signal level to 0 and back, several times a
  second. That is issue #7 reintroduced on purpose, and it is strictly worse
  than the hold it would replace.

A third, rate-limiting the doubles rather than replacing them, was rejected as
an arbitrary constant: it needs a threshold nobody can derive, and it makes the
policy's behaviour depend on how bad the host is rather than on what the user
asked for.

### Where the state lives

The policy is a frontend knob, like `--speed` and `--tape-realtime`, and is not
carried in `EmulatorConfig`: the emulated machine has no opinion about which of
picture and sound the host sacrifices, and `src/core` has no other dependency on
`src/platform`. QtApp holds it in `frame_sequencer::Sequencer` next to the band
state it qualifies; SdlApp holds it beside its own `BandState`.

Diagnostics keep the two policies distinguishable. The cadence log line grows a
third counter:

```
audio-catchup: 0 doubles, 0 skips, 19 pull-ins
```

Without it a video-preferring session under load reports zero interventions of
any kind, which reads as an idle pacer when it is in fact intervening on every
tick. `superseded` in the same line is the other half of the evidence: it should
fall to ~0 under `video` while `emulated` and `presented` converge.

## Tests

Every existing row of the band model still exercises the identical
`frames_for_tick()`, unchanged and uncalled by the new code path's decision
logic, which is the point of keeping `plan_for()` separate.

31 new rows:

- `audio_pacing_test` AP-17a..g: `plan_for()` maps every pacer outcome under
  both policies; the catch-up is the only one that differs; `Video` sets
  `next_tick_asap` exactly when it declines a double; and the band's estimate
  moves identically whichever policy is in force.
- `frame_deadline_test` FD-13a..f: `resync_now()` anchors at now, returns
  the 1 ms floor, does not count as a stall resync, and stops the lateness
  accumulation that otherwise idles the machine on every other tick at 60% of
  real time.
- `frame_sequencer_test` FS-SLOW-01..09: on a starving device an `Audio` tick
  runs two frames, composites one and supersedes one; a `Video` tick runs one,
  composites it, and ends by asking for the next tick immediately. The three
  intervention counters separate. On a healthy device the two policies produce
  byte-identical tick sequences, and `Video` still skips a tick when the device
  is ahead.
- `app_config_test` AC-58/59/60: the preference defaults to `Audio`,
  round-trips through the INI file, and falls back to the default on an unknown
  value.
- `preferences_apply_test` PA-15a..d / PA-16a..b: the control exists, starts
  from the persisted value, returns the edited value, does NOT wipe the setting
  when the dialog is applied untouched (`collect()` builds a fresh object, so a
  persisted field without a control is destroyed on OK), and Apply forwards the
  policy to the frontend even when the user declines a machine-type restart.
- `cli_options_test` covers the flag by construction: the table is diffed
  against the man page in both directions.

Discriminative evidence, three mutations against the finished branch:

| Mutation | Rows that fail |
|---|---|
| `plan_for()` ignores the policy | AP-17b/e/f, FS-SLOW-02/04/05/06 |
| the sequencer advances the deadline instead of `resync_now()` | FS-SLOW-06 |
| `resync_now()` waits a period and counts as a stall | FD-13a/b/c/e/f |
