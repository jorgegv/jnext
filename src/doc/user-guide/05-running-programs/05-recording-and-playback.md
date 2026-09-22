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

**Tape > Open Tape File…** (Alt+T) loads a tape, and **Eject** and **Rewind**
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

Leaving JNEXT with a recording still running finishes the file rather than
abandoning it — **File > Quit**, Alt+Q and closing the window all stop and
finalise it.

## RZX: recording what you did

An RZX file records your *input* rather than the screen, alongside a snapshot
of the machine. Replaying it reproduces the session exactly, keystroke for
keystroke — a walkthrough, a bug report, or a speedrun that stays honest.

- **File > Record RZX…**, or `--rzx-record FILE`; **Stop RZX** to finish.
  From the command line the recording starts with the machine — or, when you
  also `--load` a program, as soon as it is loaded, so the recording holds it
  — and the file is written when JNEXT exits.
- **File > Play RZX Recording…**, or `--rzx-play FILE`. Opening a `.rzx` with
  **File > Open**, loading it with `--load`, or naming it as the file to run
  (`jnext session.rzx`) plays it too. Every one of these starts the machine
  afresh, the way loading a program does, so a recording replays the same
  whichever you use. A file that cannot be played is refused before anything
  is reset, and the Play RZX item needs the `.rzx` extension.

The command-line options work the same whichever way you run JNEXT: the normal
window, the SDL-only build, or `--headless`. A recording that fails to load is
reported, and JNEXT then exits with a non-zero status, exactly as it does for a
program that fails to load. So does a recording that cannot be written: a
`--rzx-record` file that cannot be created stops JNEXT before the machine
starts, and one that cannot be saved (a full disk) makes it exit non-zero.

In the window, the File menu says so instead: a recording that cannot be
started or saved, and a file that cannot be played, each put up a message.
Only one recording runs at a time — **Record RZX…** while one is running tells
you to stop it first — and nothing is recorded while a recording plays back.

A reset ends a recording. A hard reset (the Reset button, F1, or the program's
own), loading another program, changing the machine type, F4, or starting to
play an RZX saves the recording at that point and stops it, with a note on the
status bar: a recording replays your input, and a reset is not input, so it
could not be replayed. Start a new recording afterwards if you want the rest.

While a recording runs, tapes load in real time, at their true speed: a fast
load skips the ROM's own loader, and a recording can only replay what the
machine actually did.

An RZX made elsewhere that carries more than one snapshot — some emulators add
one mid-recording — plays up to the second snapshot, and JNEXT says so.

Because it stores input rather than pixels, an RZX is tiny compared with a
video — but it only replays correctly in an emulator that models the machine
the same way. It also only replays what its snapshot holds: on the 128K and +3
that is the whole machine, but on the Next it is the classic 48K part only, so
a program that uses the Next's own graphics (Layer 2, the tilemap, sprites,
palettes) may not replay correctly.

---

Every option mentioned in this chapter, and the ones that are not, are listed
in full in the manual page — `man jnext`, or [`USAGE.md`](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
