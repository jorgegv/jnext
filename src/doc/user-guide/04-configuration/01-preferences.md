# Preferences

**Settings > Preferences…** opens a dialog with four tabs.

![The Preferences dialog, Startup tab](../img/preferences-startup.png)

**Startup** holds what the emulator should look like when it launches:

| Setting | Choices | Notes |
|---|---|---|
| Machine type | 48K, 128K, +3, Next | See [5.1](../05-running-programs/01-choosing-a-machine.md) |
| CPU speed | 3.5, 7, 14, 28 MHz | The emulated Next's own clock |
| Emulator speed | 10%–1000% | How fast the emulator runs against real time |
| Window scale | 1x, 2x, 3x | |
| CRT scanline filter | on / off | |
| Start muted | on / off | Takes effect on next launch only |
| Tape fast-load by default | on / off | See [5.5](../05-running-programs/05-recording-and-playback.md) |

**Input** picks what drives each of the Next's two joystick connectors — an
autodetected USB gamepad, or the host cursor keys with Space as fire. Only one
connector can use the cursor keys at a time, and the dialog enforces that for
you. Details in [5.2](../05-running-programs/02-input.md).

**Audio** controls the final host output gain with a slider from -24 dB to
+24 dB, centred on the 0 dB default. 0 dB leaves the emulated hardware mix
unchanged. It applies immediately to playback and WAV/video recording; a large
positive boost can clip.

**Paths** remembers three directories so the file dialogs open somewhere
useful: the last directory you loaded a program from, a default SD-card image,
and where screenshots go.

## Apply, OK and Cancel

**Apply** pushes the settings to the *running* machine — change the window
scale or a joystick source and it happens immediately, with nothing lost.
**OK** does the same and closes the dialog; **Cancel** discards.

Two settings cannot work that way, and both say so:

- **Machine type** is a power cycle. If you change it, JNEXT asks whether to
  restart now and warns that anything running will be lost. Answer **No** and
  the machine keeps running — your choice is still saved and takes effect the
  next time JNEXT starts. Every *other* setting in the dialog applies either
  way, so declining the restart never throws away the change you actually came
  to make.
- **Start muted** only takes effect at launch, because the audio device is
  opened once when JNEXT starts.

> The **Machine > Machine Type** menu behaves differently on purpose: it
> restarts straight away without asking. Picking "48K" from a menu called
> Machine *is* an explicit request to become a 48K; the same field buried on a
> Preferences tab is not.
