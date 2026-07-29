# The configuration file

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

The emulated ESP-01 is stored in the `[esp]` section: `enabled` (`true`/`false`)
and `allowed_hosts` (a comma-separated list). They are the same two settings as
the Preferences **Network** tab, and the persistent form of `--esp` and
`--esp-allow`. Whenever `enabled` is true the status bar carries an ESP cell, so
editing this file cannot put a program on the network without saying so, and
`--no-esp` turns it off again for one run.

Host audio gains are stored in the `[audio]` section: `gain_db` is the master,
with `gain_beeper_db`, `gain_ay0_db`, `gain_ay1_db`, `gain_ay2_db`, and
`gain_dac_db` for the individual sources. Each accepts -24 through +24; an
invalid value falls back to 0 dB. An explicit matching `--audio-gain-...-db`
value overrides the saved setting for that run.
