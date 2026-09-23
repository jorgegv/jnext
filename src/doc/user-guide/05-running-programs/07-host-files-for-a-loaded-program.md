# 5.7 Host files for a directly loaded program

A program that reads data files normally means a slow loop while you develop
it: rebuild, copy the files into the SD-card image, boot, navigate, run. The
`--esxdos-stub-root` option removes the copying step for programs JNEXT loads
directly.

```sh
jnext --esxdos-stub-root ./assets --load game.nex
```

`./assets` now *is* the filesystem the program sees. `F_OPEN`, `F_READ`,
`F_SEEK`, `F_READDIR` and the rest of the esxDOS file and directory calls work
on the real files in that directory, including files in subdirectories. Edit a
file on the host, re-run, and the program reads the new version — nothing to
copy.

It is read-only unless you also pass `--esxdos-stub-writable`.

## What sees it, and what does not

This is the part worth reading twice, because the name invites a reasonable
assumption that is wrong.

**Sees the directory:**

- a NEX loaded directly, with `--load`, a bare filename, or **File > Load NEX
  File…**
- dot commands

**Does not see the directory:**

- the NextZXOS **Browser**
- the NextZXOS file selector
- BASIC's `LOAD`
- anything else NextZXOS does for itself

`--esxdos-stub-root` is **not** "use this directory as the SD card". If you
boot NextZXOS and go looking for your files in the Browser, you will find an
empty-looking card and nothing else — not because something is broken, but
because NextZXOS never asks JNEXT for them.

The reason is in how a real Next is built. JNEXT serves these files by
intercepting `RST $08`, the esxDOS system-call entry point. Programs and dot
commands use it. NextZXOS does not: it carries its own SD-card driver and its
own FAT code inside its ROM, and reads the card directly. There is no moment
during a Browser listing at which JNEXT could answer, so there is nothing to
add. This is measured behaviour, and it is a permanent boundary rather than a
missing feature.

**Files that NextZXOS must see still go into the SD-card image.** That workflow
has not changed and still works.

## Do not use it with a NextZXOS session

`--esxdos-stub-root` implies `--esxdos-stub`, and inherits its long-standing
limitation: with NextZXOS booted, JNEXT answers the esxDOS file calls *before*
NextZXOS's own esxDOS sees them, so NextZXOS's file commands stop working —
type `.ls` and you get `No such file or dir`.

So the two are alternatives, not a combination. Use `--esxdos-stub-root` for a
program JNEXT loads directly. Boot NextZXOS without it, and put the files in
the SD-card image.

## Paths, as the program sees them

Inside the directory, paths behave like FAT paths rather than host paths:

- `/` separates components, and a leading `/` means the top of *your*
  directory — `/data/level1.bin` is `<your directory>/data/level1.bin`, never
  the host's `/data`
- lookup ignores case, so a program asking for `LEVEL1.BIN` finds `level1.bin`
- the esxDOS drive prefixes `*:`, `$:` and `c:` all mean the same directory
- long names get a synthesised 8.3 short name, the way FAT does, for programs
  that ask for short names

Anything that would leave the directory is refused, and so is any symbolic
link — links are not listed either, so a program is never offered a name it
would then be refused.

## Things it will not do

Some calls are refused outright rather than answered approximately, because a
wrong answer is worse than a clear refusal:

- wildcard, sorted and filtered directory listings
- the `+3DOS` header modes of `F_OPEN` and `F_OPENDIR` — a host file has no
  `+3DOS` header and inventing one would feed the program eight bytes of
  fiction
- `M_P3DOS`, which calls NextZXOS ROM routines directly, including raw sector
  reads. A directory has no sectors to give.

## Writing, and rewind

`--esxdos-stub-writable` is a second, separate flag on purpose. A guest write
lands on a real file on your disk straight away, and JNEXT's rewind cannot take
it back — the emulator's timeline goes backwards, your filesystem does not.

Reads are fully rewindable: an open file's position travels in the rewind
snapshot and is restored with it, so rewinding and re-running re-reads exactly
the same bytes.

## Timestamps

Directory entries and `F_STAT` report the host file's own modification time.
`M_GETDATE`, which asks what time it is *now*, answers from the emulated clock
instead — so it follows `--rtc` and stays deterministic in a scripted run.
