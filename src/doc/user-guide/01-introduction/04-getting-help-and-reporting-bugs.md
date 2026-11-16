# Getting help and reporting bugs

Bugs, questions and feature requests all go to the issue tracker:

**<https://github.com/jorgegv/jnext/issues>**

## Known issues

The tracker is also the list of known issues, and it is the only list. This
guide does not repeat it, deliberately: a list of problems in a released
document goes stale the moment it is written. A bug gets fixed, a workaround
stops being needed, a "planned" feature ships, and a page that describes a
problem you no longer have is worse than no page at all. The tracker is where
the current state is authoritative.

Search it before you report: what you are seeing may already be there.

## Reporting something

If it is not there, please open an issue. What helps most:

- the JNEXT version (`jnext --version`, or the emulator window's title bar) and
  your operating system;
- the exact command line, or the steps in the GUI;
- the program you were running, if it can be shared, and what you expected to
  see instead;
- for a rendering problem, a screenshot: `--headless` with
  `--delayed-screenshot` and `--delayed-screenshot-frames` (chapter 7) gives a
  capture anyone can reproduce exactly;
- for a performance problem, `--log-level platform=debug`, which reports the
  per-second frame cadence.

## Capturing a log to attach

Add `--log-file` and JNEXT writes its log to a file instead of the console:

```console
$ jnext game.nex --log-level nextreg=debug --log-file trace.log
```

Why use it rather than a shell redirect:

- JNEXT logs to standard error, so `> trace.log` captures nothing and you need
  `2> trace.log`.
- On PowerShell, redirected output from a native program is re-encoded on the
  way to the file.
- `--log-file` sidesteps both: the program writes where you told it to.

How the file behaves:

- It replaces the console rather than adding to it.
- It is emptied at the start of every run, so what you attach describes the run
  you just did and nothing else.
- If it cannot be opened JNEXT says so and stops, rather than quietly logging to
  the console you redirected it away from and leaving you with an empty file.
- A trace at `debug` or `trace` level grows quickly (a few megabytes a minute is
  normal), so aim the level at the subsystem you are asking about rather than
  turning everything up.

For the console log without ANSI colour, set `NO_COLOR` to any non-empty value;
JNEXT follows the [no-color.org](https://no-color.org/) convention, and an empty
value counts as unset. A `--log-file` is never coloured either way, so you do
not need `NO_COLOR` to make a captured log readable.

## Licence

JNEXT is free software, licensed under the GNU General Public License version 3
or later.
