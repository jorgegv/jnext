# 8. Known issues

The live list is the issue tracker, and it is the only list:

**<https://github.com/jorgegv/jnext/issues>**

Nothing is duplicated here, deliberately. A known-issues chapter in a released
document starts going stale the moment it is written — a bug gets fixed, a
workaround stops being needed, a "planned" feature ships — and a page that
confidently describes a problem you no longer have is worse than no page at
all. The tracker is where the current state is authoritative.

## Reporting something

If what you are seeing is not already there, please open an issue. What helps
most:

- the JNEXT version (`jnext --version`) and your OS;
- the exact command line, or the steps in the GUI;
- the program you were running, if it can be shared;
- for a rendering problem, a screenshot — `--headless` with
  `--delayed-screenshot` and `--delayed-screenshot-frames` (chapter 7) gives a
  capture anyone can reproduce exactly;
- for a performance problem, `--log-level platform=debug`, which reports the
  per-second frame cadence.

## Capturing a log to attach

Add `--log-file` and JNEXT writes its log to a file instead of the console:

```console
$ jnext game.nex --log-level nextreg=debug --log-file trace.log
```

That is easier than it sounds to get wrong without it. JNEXT logs to standard
error, so `> trace.log` captures nothing and you need `2> trace.log`; on
PowerShell, redirected output from a native program is re-encoded on the way to
the file. `--log-file` sidesteps both — the program writes where you told it to.

The file replaces the console rather than adding to it, and is emptied at the
start of every run, so what you attach describes the run you just did and
nothing else. If it cannot be opened JNEXT says so and stops, rather than
quietly logging to the console you redirected it away from and leaving you with
an empty file.

A trace at `debug` or `trace` level grows quickly — a few megabytes a minute is
normal — so aim the level at the subsystem you are asking about rather than
turning everything up.

If you want the console log without ANSI colour, set `NO_COLOR` to any non-empty
value; JNEXT follows the [no-color.org](https://no-color.org/) convention, and
an empty value counts as unset. A `--log-file` is never coloured either way, so
you do not need `NO_COLOR` to make a captured log readable.
