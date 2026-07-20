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
