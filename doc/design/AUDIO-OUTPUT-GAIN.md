# Host Audio Output Gains

## Use case

Some programs produce a correct but quiet mix compared with other desktop
audio. Users also need to balance a program's beeper, three TurboSound chips,
and DAC audio independently—for example, to lift quiet level music without
making sampled effects overpower it. These controls must not change any
guest-visible state.

## Interface

All gains accept finite values from -24 through +24 dB:

| Contribution | CLI | `[audio]` key |
|---|---|---|
| Complete mix (master) | `--audio-gain-db` | `gain_db` |
| Beeper (EAR/MIC/tape-EAR) | `--audio-gain-beeper-db` | `gain_beeper_db` |
| TurboSound AY #0 | `--audio-gain-ay0-db` | `gain_ay0_db` |
| TurboSound AY #1 | `--audio-gain-ay1-db` | `gain_ay1_db` |
| TurboSound AY #2 | `--audio-gain-ay2-db` | `gain_ay2_db` |
| DAC family | `--audio-gain-dac-db` | `gain_dac_db` |

The same range is available under **Settings > Preferences > Audio** as six
sliders in 1 dB steps. The symmetric range puts the 0 dB default at the centre.
Every default is 0 dB, preserving existing output exactly. Invalid values are
rejected or fall back to the default. Each explicit CLI value independently
wins over its saved preference for that run.

## Design

The mixer converts dB to a linear multiplier with `10^(DB/20)`. Beeper gain is
applied to its zero-centred contribution. Each AY gain is applied to that
chip's post-mode/post-pan contribution in a wide signed host path *before* the
three chips are summed. `TurboSound::pcm_left/right()` remain the unmodified
12-bit hardware result. This avoids overflow or wrapping when boosted chips
are combined and keeps gain invisible to the guest and debugger.

The DAC is unsigned around its silence midpoint. Its gain therefore scales
`dac_term - DAC_REST_LEVEL` and then restores `DAC_REST_LEVEL`; scaling the raw
unsigned value would create a DC offset at every non-zero gain. The master gain
is applied after the complete mix is converted to signed PCM. Final results
saturate at the signed 16-bit limits.

TurboSound caches each chip's post-mode/post-pan contribution on the same PSG
tick that updates the aggregate hardware sum. Both the all-zero-dB path and
non-default per-chip gains therefore preserve hardware timing: a pan or mode
write cannot become audible early through a host-only control. Other 0 dB
contributions bypass multiplication too.

The historical snapshot schema contains only TurboSound's aggregate left/right
PCM, not its decomposition by chip. Extending that inline schema would shift
every following component and break old snapshots. On load the per-chip cache
is therefore marked invalid; while invalid, the mixer uses the exact serialized
aggregate and temporarily defers per-chip gains. The next PSG tick rebuilds
the cache and restores balancing. This preserves snapshot compatibility and
the pending-write timing edge, at the cost of at most one PSG tick before
host-only AY balancing resumes.

Gains belong to host configuration, not machine state. They are not serialized
or reset by the emulated machine. The shared frontend cold-boot driver
(`emulator_frontend_cold_boot`) carries all six live mixer values into the
rebuilt machine's config, and `Emulator::init()` reapplies them after mixer
reset. Live changes therefore survive cold boots, Machine > Power Reset,
File > Load and sibling-NEX chaining. Video and WAV recording consume the same gained PCM
sent to host playback. Applying Preferences updates the running mix immediately.

## Tests

The original master-gain suite remains unchanged. Additive subsystem suites pin
all defaults, distinct independently expected per-AY transfer ratios, pre-sum
AY saturation, DAC positive/negative centred transfer, DAC silence without DC,
master/subsystem composition, guest-visible AY identity, PSG-tick deferral for
pending pan writes, old-schema snapshot fallback/revalidation, reset behaviour,
emulator initialisation, and live frontend cold-boot carry. Separate additive
GUI suites pin exact documented configuration keys, persistence, valid/invalid
loading and precedence, all six Preferences sliders, Apply values, and live
application to a running emulator. The additive subsystem suite also pins each
audio-gain CLI option to its distinct value and precedence slot.

The functional test validates every new CLI endpoint and option-specific error,
then compares -24/0/+6.0206 dB headless WAV captures. Differencing their peaks
cancels the fixture's constant I2S term and proves the isolated beeper
contribution follows the requested gain.
