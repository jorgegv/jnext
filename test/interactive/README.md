# Interactive DAPR Tests

These tests require user interaction (keyboard input, joystick, audio playback)
and cannot be run as part of the automated regression suite.

Run them manually with:

```bash
./build/jnext --machine-type next --load demo/dapr-nexlib+tests/test00alltests/<nex_file>
```

## Tests

| Test | NEX File | Description |
|------|----------|-------------|
| dapr-keyb | test06keyb.nex | Keyboard input test |
| dapr-joystick | test07joystick.nex | Kempston joystick test |
| dapr-covox | test08covox.nex | DAC/Covox/PCM audio test (press keys to play samples) |
| dapr-videoint | test09videoint.nex | Video interrupt test (animated output) |
| dapr-isometric | test11isometric.nex | Isometric rendering demo (animated) |
| dapr-mathfunc | test12mathfunc.nex | Math functions demo (animated sprite) |
