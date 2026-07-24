# 5.4 Sound

JNEXT emulates everything the Next can make noise with, and none of it needs
setting up:

- **Beeper** — the original single-bit speaker.
- **AY / TurboSound** — the AY sound chip that gave the 128K its music, up to
  three of them (TurboSound) on the Next, with stereo panning.
- **DAC** — 8-bit sample playback (Specdrum, Soundrive and Covox compatible).

Which of these a program can actually use is up to the program and the machine
it was written for. Stereo separation and channel balance are likewise set by
the software, not by you — they are properties of the emulated hardware.

**Settings > Preferences > Audio > Output gain** can trim or boost JNEXT's
final stereo mix from -24 dB to +24 dB, and `--audio-gain-db DB` provides the
same control for a single run. The setting applies live and affects playback
and WAV/video recordings without changing the emulated machine's audio state.
Complete mute remains available through
**Settings > Preferences > Startup > Start muted** or `--silent`; muting also
skips sound synthesis entirely, which speeds up runs that do not need audio.
Tape loading still works while muted.

## If the sound stutters or clicks

JNEXT paces itself against your sound card, so audio should stay clean
indefinitely. Glitches almost always mean the **host cannot keep up**.

Check the two frame-rate figures in the status bar:

```
FPS: 50.0 emu / 50.0 shown
```

`emu` is how fast the machine is being simulated; `shown` is how many of those
frames reached the screen. If `emu` sits below about 50, your host is behind,
and the audio will suffer with it. Things that help, roughly in order:

1. Close whatever else is busy on the machine.
2. Turn off the CRT filter and drop the window scale.
3. Reduce the emulated CPU speed (**Machine > CPU Speed**) if the software
   tolerates it — 28 MHz is many times the work of 3.5 MHz.
4. Use `--silent` if you do not need sound at all.

`shown` being lower than `emu` on its own is not a fault: it is expected when
you run faster than 1x, and during brief audio catch-up.

---
