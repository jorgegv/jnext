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
