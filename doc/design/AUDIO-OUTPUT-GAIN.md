# Host Audio Output Gain

## Use case

Some programs produce a correct but quiet mix compared with other desktop
audio. Users need to raise emulator playback volume without changing AY, DAC,
beeper, or other guest-visible state.

## Interface

`--audio-gain-db DB` accepts finite values from -24 through +24 dB. The same
range is available under **Settings > Preferences > Audio** as a slider in
1 dB steps; the range is symmetric so the 0 dB default sits at the centre. The
value is persisted as `audio/gain_db` in `~/.jnext/jnext.conf`. The default is
0 dB, preserving existing output exactly. Invalid and out-of-range values are
rejected or fall back to the default. An explicit CLI value wins over the
saved preference for that run. Deeper attenuation than -24 dB is a mute in
practice, and muting is already covered by `--silent` and **Start muted**.

## Design

The mixer converts dB to a linear multiplier with `10^(DB/20)`. It applies the
multiplier to the signed stereo PCM after the hardware sources are mixed and
the resting DC level is removed. Results saturate at the signed 16-bit limits.

Gain belongs to host configuration, not machine state. It is not serialized or
reset by the emulated machine. The shared frontend cold-boot driver
(`emulator_frontend_cold_boot`) carries the live mixer value into the rebuilt
machine's config exactly as it carries the joystick sources, and
`Emulator::init()` reapplies it after mixer reset, so a live Preferences
change survives cold boots, Machine > Reset, File > Load and sibling-NEX
chaining, not just the value the process started with. Video and WAV recording
consume the same gained PCM sent to host playback. Applying the Preferences
control updates the running mix immediately.

## Tests

The tests pin 0 dB identity, positive and negative transfer ratios, saturation,
silence, reset behavior, cold-boot propagation (including a live change carried
across the frontend cold-boot driver), configuration round-tripping,
CLI precedence, the Preferences control, and live application. The functional
test validates the CLI range and compares two headless WAV captures, proving
that +6.0206 dB doubles the emitted PCM peak.
