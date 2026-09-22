# 5. Running programs

The short version: give JNEXT a file.

```
jnext game.nex
jnext game.tap
jnext game.sna
```

The format is recognised from the extension — `.nex`, `.sna`, `.szx`, `.z80`,
`.tap`, `.tzx`, `.wav` and `.rzx` are all understood. A bare filename is
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

## Starting from a real NextZXOS: `--warm-start`

There is a middle road between the two. `--warm-start` boots the firmware for
real — once — and then starts your program on the machine that boot produced:

```
jnext --warm-start game.nex
```

The first time you use it with a given SD card image, JNEXT cold-boots it the
way it does with no arguments (the FPGA boot ROM, then `TBBLUE.FW`, then
NextZXOS), and saves the resulting machine to `~/.jnext/warm-start/`. That
takes a few seconds and prints a line saying so. Every run after that restores
the saved machine instead of booting, so it costs nothing.

What your program gets is a Next with NextZXOS resident and its ROM paged in —
the environment a NEX file is written for, because on real hardware every NEX
is launched by NextZXOS. Without it, the program meets a machine JNEXT
assembles, whose settings are JNEXT's idea of what the firmware would have
left rather than what it actually left.

The saved machine is recorded from *your* image and is never shipped with
JNEXT: it contains NextZXOS's own ROM. It re-records itself when the image
changes, when you pick a different machine, or when a JNEXT update changes the
format — `--warm-start-regenerate` forces it.

Two limits. It is the ZX Spectrum Next only: the 48K, 128K and +3 run no
firmware, so there is nothing to record, and the option says so and does
nothing. And it applies to `.nex` files; tapes and snapshots load as they
always have.

It is off by default, because it changes what every loaded program sees.

---
