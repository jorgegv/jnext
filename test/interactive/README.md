# Interactive DAPR Tests

These tests require user interaction (keyboard input, joystick, audio playback)
and cannot be run as part of the automated regression suite.

Run them manually with:

```bash
./build/jnext --machine next --load demo/dapr-nexlib+tests/test00alltests/<nex_file>
```

## Tests

| Test | NEX File | Description |
|------|----------|-------------|
| dapr-keyb | test06keyb.nex | Keyboard input test — also automated: see below |
| dapr-joystick | test07joystick.nex | Kempston joystick test — also automated: see below |
| dapr-covox | test08covox.nex | DAC/Covox/PCM audio test (press keys to play samples) |
| dapr-videoint | test09videoint.nex | Video interrupt test (animated output) |
| dapr-isometric | test11isometric.nex | Isometric rendering demo (animated) |
| dapr-mathfunc | test12mathfunc.nex | Math functions demo (animated sprite) |

## Replayed from recordings (GH #20)

`dapr-keyb` and `dapr-joystick` also run unattended: each was recorded once in
the GUI with `--record-script` (`test/scripts/dsl/record-dapr.sh`, which drives
the window with xdotool under Xvfb) and is replayed headless by the functional
rows `script-replay-keyb-func` and `script-replay-joystick-func`, which compare
the replay's captures with the recording's pixel for pixel. To re-record:

```bash
test/scripts/dsl/record-dapr.sh keyb
test/scripts/dsl/record-dapr.sh joystick
```
