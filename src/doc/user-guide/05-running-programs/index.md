# 5. Running programs

The short version: give JNEXT a file.

```
jnext game.nex
jnext game.tap
jnext game.sna
```

The format is recognised from the extension — `.nex`, `.jns`, `.sna`, `.szx`,
`.z80`, `.tap`, `.tzx`, `.wav` and `.rzx` are all understood. (`.jns` is
JNEXT's own whole-machine snapshot; see [5.9 Snapshots](09-snapshots.md).) A bare filename is
exactly the same as `--load FILE`; you cannot use both at once.

A NEX **V1.3** file can declare a buffer for an argument line, and `--nex-args`
fills it:

```
jnext --load game.nex --nex-args "level 3"
```

The program finds that text zero-terminated at the address the file declares,
exactly as it would when launched with arguments from NextZXOS; `DE` then holds
that address plus the buffer's size, which is where the Next's V1.3 loader
leaves it (not the address itself, as the format's notes say). A line longer than the
buffer the file declares is truncated to fit. Only V1.3 files have the buffer,
so with anything else the option warns and is ignored.

Note that NEX **V1.3** is an experimental format and is not supported in any
way. Loading a V1.3 file needs an explicit opt-in: on the command line, add
`--experimental-nex-v1.3` (without it the load is refused with an error); in
the window, JNEXT shows a warning dialog and only proceeds if you confirm.
V1.0–V1.2 files load normally, no opt-in involved.

Some NEX files are larger than the program their header describes. When the
header asks for the file to stay open, the program can read the extra data
from its own file, and JNEXT lets it. When it does not, JNEXT does what the
Next's own loader does: it loads the declared part, ignores the rest and runs
the program. More than 16 KB of ignored data is reported with a warning.

In the window, **File > Load NEX File…** (Alt+O) opens the same loader and
accepts every one of those formats despite its name. Tapes have their own
entry, **Tape > Open Tape File…** (Alt+T), covered in
[5.5](05-recording-and-playback.md). A file that cannot be loaded — missing,
truncated, or not the format its extension says — gets a warning dialog naming
it, from either menu and from **File > Play RZX Recording…**; the log says
what was wrong with it.

![JNEXT running, with NextZXOS booted](../img/gui-main-window.png)

The status bar along the bottom tracks the session: frame rate, the emulated
CPU clock, the emulator speed, tape state, and the current machine.

Everything else in this chapter is about shaping that session.

## Programs that need NextZXOS

Loading a file directly (with `--load`, a bare file name, or **File > Load NEX
File…**) skips NextZXOS: the program starts on a machine with nothing else
running. That is all most games and demos need. On a real Next, though, every
NEX program is started by NextZXOS, and some programs also use it while they
run: to ask which drive they are on, to save a high score, or to read their
levels, music or settings from files next to them.

For a NEX file loaded directly, JNEXT stands in for the NextZXOS services such
programs most often ask for:

- the current drive, which is always `C:`;
- one small file kept in memory, so a high score or a settings file can be
  saved and read back during the session (it is not written to disk, and it is
  gone when JNEXT closes);
- `run` of another NEX file in the same folder, for programs that come in
  several parts.

Everything else is refused with an error. In practice:

- A program that only checks its drive or saves a high score runs normally.
- A program that needs data files from disk (levels, music, a configuration
  file) stops with its own error message, or runs with parts missing. NXtel,
  for example, says it cannot read `NXTEL.CFG`. JNEXT cannot give a directly
  loaded program files from your computer.

For those, do what a real Next does: boot NextZXOS from the SD card image and
start the program from the **Browser**, with its files next to it on the card.
Many programs, games included, are already on the image JNEXT uses by default.
To add your own, copy the program and its files into the image with a tool that
can write to FAT32 disk images, such as `mcopy` from mtools.

(A NEX file whose header asks to keep its own file open can also read that
file, and read files next to it, when loaded directly. It cannot write them.)

## The machine a NEX file starts on

Loading a NEX file directly still starts it on a Next that the firmware made.
There is no option for this and nothing to turn on:

```
jnext game.nex
```

The first time you load a NEX file with a given SD card image, JNEXT cold-boots
that image the way it does with no arguments — the FPGA boot ROM, then
`TBBLUE.FW`, then NextZXOS — and saves the resulting machine under
`~/.jnext/warm-start/`. That takes about three and a half seconds, and JNEXT
prints a line saying it is doing it and another when it is done. Every load
after that restores the saved machine instead of booting.

What your program gets is a Next with NextZXOS resident and its ROM paged in —
the environment a NEX file is written for, because on real hardware every NEX
file is launched by NextZXOS. The alternative, which is what JNEXT did before
version 1.0.11, is a machine JNEXT assembles: its settings are JNEXT's idea of
what the firmware would have left rather than what it actually left, and some
of them are states real hardware never reaches.

This does not replace booting NextZXOS from the card. The program still starts
directly, so the section above still applies: files next to it on your computer
are not visible to it. What changes is the machine underneath.

### When it cannot be done

JNEXT never falls back quietly. If it cannot record or restore the machine it
prints an error naming the reason and starts the program on the assembled
machine instead. The reasons are:

- no SD card image is mounted, so there is no firmware to boot;
- the card has no firmware on it, or the boot does not end with NextZXOS
  resident;
- the saved machine will not load back, or is not NextZXOS-resident once it
  has been through a file.

The 48K, 128K and +3 are not one of those cases: they run no firmware at all,
so there is nothing to record and nothing is reported. The same goes for tapes
and snapshots, which are loaded by mechanisms a NextZXOS-resident machine does
not have — a snapshot replaces the whole machine, and a tape is read through a
48K ROM routine that is not paged in. Those load exactly as they always have.

### Where it is kept, and refreshing it

The saved machine is recorded from *your* image, on your computer, and is never
shipped with JNEXT: it contains NextZXOS's own ROM, and JNEXT does not
redistribute firmware — which is also why it downloads the SD card image rather
than including one.

It is one file per machine type, a little over 100 KB, keyed by the contents of
the SD card image, the machine type and the save format. Change any of those —
replace the image, switch machine, install a JNEXT update that changes the
format — and it re-records itself on the next load. Checking that key means
reading the whole card image each time a NEX file is loaded, which costs about
half a second on a 1 GB image; it is what stops a recording of one card being
served to another.

If you update `TBBLUE.FW` or NextZXOS on the card in a way you want re-recorded
straight away, `--warm-start-regenerate` forces it:

```
jnext --warm-start-regenerate game.nex
```

---
