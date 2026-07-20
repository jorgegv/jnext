# 4. Configuration

JNEXT starts with sensible defaults, so you can skip this chapter entirely and
still run programs. Come back when you get tired of passing the same options
every time.

Everything here is about the **graphical** JNEXT. Automated, headless runs
never read your settings — see [chapter 7](07-automation-and-ci.md) for why that
matters.

## Preferences

**Settings > Preferences…** opens a dialog with three tabs.

![The Preferences dialog, Startup tab](img/preferences-startup.png)

**Startup** holds what the emulator should look like when it launches:

| Setting | Choices | Notes |
|---|---|---|
| Machine type | 48K, 128K, +3, Next | See [5.1](05-running-programs.md#51-choosing-a-machine) |
| CPU speed | 3.5, 7, 14, 28 MHz | The emulated Next's own clock |
| Emulator speed | 10%–1000% | How fast the emulator runs against real time |
| Window scale | 1x, 2x, 3x | |
| CRT scanline filter | on / off | |
| Start muted | on / off | Takes effect on next launch only |
| Tape fast-load by default | on / off | See [5.5](05-running-programs.md#55-recording-and-playback) |

**Input** picks what drives each of the Next's two joystick connectors — an
autodetected USB gamepad, or the host cursor keys with Space as fire. Only one
connector can use the cursor keys at a time, and the dialog enforces that for
you. Details in [5.2](05-running-programs.md#52-input).

**Paths** remembers three directories so the file dialogs open somewhere
useful: the last directory you loaded a program from, a default SD-card image,
and where screenshots go.

### Apply, OK and Cancel

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

## The configuration file

Preferences are stored as plain text in:

```
~/.jnext/jnext.conf
```

It is an INI file, so you can read and edit it with any text editor while
JNEXT is closed. It lives next to the other things JNEXT keeps per-user, such
as the downloaded SD-card image in `~/.jnext/sdcard/`. The debugger window
keeps its own layout separately in `~/.jnext/Debugger.conf`.

Deleting the file resets everything to defaults — that is the supported way to
start over. A missing or corrupt entry falls back to its default rather than
stopping JNEXT from starting.

## What wins: the command line

**Command-line options always beat saved settings.** For any given setting:

1. If you passed it on the command line, that value is used.
2. Otherwise the saved preference is used.
3. Otherwise the built-in default is used.

So if your preferences say "Next" but you run `jnext --machine 48k`, you get a
48K — for that run only, with nothing overwritten. This makes it safe to keep
comfortable defaults for everyday use and still override them for a one-off,
and it is why scripts should pass explicit options rather than depend on
whatever a particular machine happens to have saved.

Headless runs ignore the configuration file completely, so an automated test
produces the same result on any machine.

The full list of options is in the **jnext(1)** man page, also available as
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
