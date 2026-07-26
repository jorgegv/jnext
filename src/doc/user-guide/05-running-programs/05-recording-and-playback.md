# 5.5 Recording and playback

## Tapes: fast or real time

Tape files (`.tap`, `.tzx`) load two ways. Either way `LOAD ""` is typed in
for you when the tape is attached.

**Fast load** is the default and is what you want almost always: JNEXT
short-circuits the ROM's loading routine and the program appears in seconds.

**Real time** replays the tape as audio, at the speed of an actual cassette —
several minutes, loading stripes, screeching and all. Use it for authenticity,
or for the occasional program with a custom loader that fast-load cannot
follow. Turn it on with **Tape > Fast Load** (uncheck it) or `--tape-realtime`.

`.wav` files are always real time; there is no ROM routine to short-circuit.

**Tape > Open Tape File…** (Ctrl+T) loads a tape, and **Eject** and **Rewind**
do what they say. The status bar shows the tape name and, while loading, the
block position.

## Video

**File > Record MPEG4 Video…** (Ctrl+F5) starts recording video with audio to
an MP4; **Stop** (Ctrl+F6) ends it. From the command line, `--record FILE`.

This needs **ffmpeg** installed and on your PATH — JNEXT feeds it the frames.
If it is missing, `--record` fails immediately: JNEXT says so and exits
non-zero without emulating anything, rather than running a session you would
find out afterwards had never been recorded.

To capture only the audio, `--wav-record FILE` writes a standard WAV. It needs
neither ffmpeg nor a sound card, so it works in automated runs.

## RZX: recording what you did

An RZX file records your *input* rather than the screen, alongside a snapshot
of the machine. Replaying it reproduces the session exactly, keystroke for
keystroke — a walkthrough, a bug report, or a speedrun that stays honest.

- **File > Record RZX…**, or `--rzx-record FILE`; **Stop RZX** to finish.
- **File > Play RZX Recording…**, or `--rzx-play FILE`. Loading a `.rzx` with
  `--load` plays it too.

Because it stores input rather than pixels, an RZX is tiny compared with a
video — but it only replays correctly in an emulator that models the machine
the same way.

---

Every option mentioned in this chapter, and the ones that are not, are listed
in full in the manual page — `man jnext`, or [`USAGE.md`](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
