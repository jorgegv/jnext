# 8.1 Judder when the host cannot keep up

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

- Choose the other trade. Set **When the host is too slow** to *Smooth picture*
  under **Settings > Preferences > Startup**, or start JNEXT with
  `--when-slow-prefer video`. Every frame is then shown and the machine runs
  slower than real time instead, so motion is smooth but slowed, and the sound
  stutters and drops in pitch. It is the way FUSE and several other emulators
  degrade, and which of the two is preferable is a matter of taste and of what
  you are running. On a host with headroom neither setting changes anything.
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
be tuned away — one emulated frame costs what it costs. What JNEXT gives you
is the choice of which way it degrades: smooth sound at the cost of the
picture (the default), or the whole picture at the cost of the sound and of
running slower than real time.

Tracked as [issue #9](https://github.com/jorgegv/jnext/issues/9); the
selectable policy is [issue #35](https://github.com/jorgegv/jnext/issues/35).
