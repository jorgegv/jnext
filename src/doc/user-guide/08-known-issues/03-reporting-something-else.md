# 8.3 Reporting something else

If what you are seeing is not here, please open an issue. What helps most:

- the JNEXT version (`jnext --version`) and your OS;
- the exact command line, or the steps in the GUI;
- the program you were running, if it can be shared;
- for a rendering problem, a screenshot — `--headless` with
  `--delayed-screenshot` and `--delayed-screenshot-frames` (chapter 7) gives a
  capture anyone can reproduce exactly;
- for a performance problem, `--log-level platform=debug`, which reports the
  per-second frame cadence.
