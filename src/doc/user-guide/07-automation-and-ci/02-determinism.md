# 7.2 Determinism

A screenshot is only useful as a test if the same input always produces the
same image. Three things make that true.

**Count frames, not seconds.** `--delayed-screenshot-time` counts *emulated*
seconds, not the host's, so a slow CI runner and a fast desktop agree on it.
What it depends on is the refresh rate the program selects: a frame at 50 Hz
counts 1/50 s and one at 60 Hz counts 1/60 s, so the frame it lands on moves
if the program switches. `--delayed-screenshot-frames` names one exact frame.
The same applies to the exit bound: `--delayed-automatic-exit-frames`
overrides `--delayed-automatic-exit` when both are given. **Use the frame forms
for anything you intend to compare.**

**Pin the clock.** Anything that draws the date or time — the NextZXOS menu,
most obviously — changes between runs. `--rtc` freezes it:

```console
$ jnext --headless --rtc "2026-01-01 00:00:00" ...
```

**Inject keys by frame.** `--delayed-keypress-frames N KEY` presses a key
after exactly *N* emulated frames, and repeats:

```console
$ jnext --headless --load myprog.nex \
      --delayed-keypress-frames 100 space \
      --delayed-keypress-frames 160 enter \
      --delayed-screenshot level2.png --delayed-screenshot-frames 250 \
      --delayed-automatic-exit-frames 300
```

*KEY* is case-insensitive: a single character, or `ENTER`, `RETURN`, `SPACE`,
`UP`, `DOWN`, `LEFT`, `RIGHT`, or a `sym+`/`caps+` compound (`sym+m` is `.`).
Keys are held briefly and released, so leave a gap of at least ~15 frames
between presses.

`--delayed-screenshot-layers` narrows a capture to `ula`, `layer2`, `sprites`
or `tiles`, which makes a failure say *which* layer changed rather than just
"the frame changed". Excluding `ula` also removes the border — the ULA is what
draws it.
