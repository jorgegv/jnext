# What wins: the command line

**Command-line options always beat saved settings.** For any given setting:

1. If you passed it on the command line, that value is used.
2. Otherwise the saved preference is used.
3. Otherwise the built-in default is used.

So if your preferences say "Next" but you run `jnext --machine 48k`, you get a
48K — for that run only, with nothing overwritten. This makes it safe to keep
comfortable defaults for everyday use and still override them for a one-off,
and it is why scripts should pass explicit options rather than depend on
whatever a particular machine happens to have saved.

## Any setting, from the command line

Every setting of the configuration file can be given for one run with
`--set section.key=value`, using the file's own section and key names and
values. Repeat it for more:

```
jnext --set audio.gain_db=-6 --set startup.silent=false --set paths.last_load_dir=/home/me/games
```

Three settings also have flags of their own, because they have menus of their
own: `--cpu-speed` (`3.5`, `7`, `14` or `28` MHz), `--scale` (1 to 3) and
`--crt` (`off`, `scanlines`, `soft`, `tv` or `pc`). The keys, and the flag
beside each key that has one, are listed by `jnext --help` and in
**jnext(1)** under *Settings for --set*.

- **The later one wins.** A flag and `--set` are the same setting, so
  `--cpu-speed 28 --set startup.cpu_speed=0` starts at 3.5 MHz, in either order.
- **Nothing is written back.** A value from the command line governs that run
  only; `jnext.conf` keeps what it had, even when something else makes JNEXT
  save the file (loading a program adds it to File > Recent Files, say).
- **The window takes over.** Once the run is going, a change you make in it
  (Preferences OK or Apply, a menu, a file dialog that moves the last
  directory) replaces the command line's value from then on. Preferences shows
  the *saved* values, not the command line's.
- **A bad value stops the start-up**, with a message naming the key and the
  value and exit status 1. A key the running frontend has no use for (a window
  setting with `--headless`, say) only produces a warning, so one command line
  can serve the window, the SDL-only build and headless runs.
- **The CPU speed is a starting value.** On the Next machine NextZXOS sets its
  own speed while it boots, as the real firmware does.
- **A few things only `--set` can say**: `startup.silent=false` plays sound for
  one run although the file says muted; `startup.tape_fast_load=true` forces
  fast loading; `esp.allowed_hosts=` (empty) allows any host over a saved list.
  An empty controller (`--joy1-device ""` or `--set input.joy1_device=`) picks
  Automatic over a saved controller.

Headless runs ignore the configuration file completely, so an automated test
produces the same result on any machine.

The full list of options is in the **jnext(1)** man page, also available as
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
