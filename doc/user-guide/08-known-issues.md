# 8. Known issues

The live list is the issue tracker:

**<https://github.com/jorgegv/jnext/issues>**

That is where anything you hit should be reported, and where the current state
of everything below is authoritative. This chapter writes up the few that a
user is most likely to run into, so you can recognise them without having to
search.

## 8.1 Judder when the host cannot keep up

**What you see.** Motion is not smooth. The picture advances in a slightly
lurching, uneven way — as if frames were being dropped in pairs — while the
sound stays perfectly clean. It shows up on demanding titles (heavy Layer 2 +
sprite + copper work) on modest hardware, and it gets noticeably better if you
run the same title with `--silent`.

**Why it happens.** A ZX Spectrum Next frame has a fixed real-time budget —
about 17 ms. If emulating one frame on your machine costs close to, or more
than, that budget, JNEXT cannot produce frames as fast as they are due. When
that happens it currently **prioritises continuous audio**: it keeps the sound
card fed, which means catching up by emulating two frames in the time slot for
one. The first of each pair is superseded before it is ever painted. The
result is a reduced number of *unique* frames on screen, with pairs skipped
between them — and that alternating skip is the judder you see.

Two things follow from this that are worth knowing, because they save you
chasing the wrong culprit:

- **It is not your graphics card or window system.** In the reported case the
  window system painted every single frame it was handed; the frames simply
  were not produced.
- **Audio is what pushes it over the edge.** Mixing sound costs real time on
  top of emulating the machine. A title that fits the budget in silence can sit
  at 100% of it with sound on.

**What you can do.**

- Run with `--silent`, or turn sound off, if you can live without it. This is
  the single biggest lever.
- Close other work. The margin here is small, so a background compile or a
  busy browser is enough to tip it.
- Reduce what the host has to draw: smaller window scale, no CRT filter,
  windowed rather than fullscreen.
- Check whether your machine is simply at its limit. `--benchmark N` runs *N*
  frames uncapped and prints the achieved rate; if a title only just clears
  real time flat-out, it has no headroom left for audio.

**What to expect.** On marginal hardware this is a limit, not a bug that will
be tuned away — one emulated frame costs what it costs. What *is* being
changed is the choice JNEXT makes when it cannot keep up: today it always
prefers smooth audio over smooth video, and a user-selectable policy (prefer
video instead, running slower than real time with audio artifacts, the way
some other emulators degrade) is planned.

Tracked as [issue #9](https://github.com/jorgegv/jnext/issues/9); the
selectable policy is [issue #35](https://github.com/jorgegv/jnext/issues/35).

## 8.2 Continuous buzz on Soundrive/DAC playback

**What you see.** Software that plays samples through the Soundrive/Specdrum
8-bit DAC produces a steady background buzz underneath the expected sound.

**What is known.** It persists with interrupts disabled, at 28 MHz, and with a
tightly timed pure-assembly playback loop — and it reproduces in ZEsarUX as
well as JNEXT. So it is not yet clear whether both emulators share a fault,
the test program itself is at fault, or driving a DAC from Z80 code with no
hardware sample clock is inherently prone to it.

**Impact.** Low. Very little Next software uses the DAC; AY/TurboSound and the
beeper are unaffected.

Tracked as [issue #38](https://github.com/jorgegv/jnext/issues/38).

## 8.3 The debugger window does not follow the emulator window

**What you see.** Move the main emulator window and the debugger window stays
where it was, so the two drift apart and have to be re-arranged by hand.

**What works today.** The debugger's position *is* remembered between
sessions, so it reopens where you last left it. It just does not track the
emulator window while that window is being moved.

**Workaround.** Place both windows once; the layout survives a restart.

Tracked as [issue #39](https://github.com/jorgegv/jnext/issues/39).

## 8.4 Reporting something else

If what you are seeing is not here, please open an issue. What helps most:

- the JNEXT version (`jnext --version`) and your OS;
- the exact command line, or the steps in the GUI;
- the program you were running, if it can be shared;
- for a rendering problem, a screenshot — `--headless` with
  `--delayed-screenshot` and `--delayed-screenshot-frames` (chapter 7) gives a
  capture anyone can reproduce exactly;
- for a performance problem, `--log-level platform=debug`, which reports the
  per-second frame cadence.
