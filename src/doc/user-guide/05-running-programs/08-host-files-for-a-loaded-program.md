# 5.8 Host files for a directly loaded program

A program that reads data files normally means a slow loop while you develop
it: rebuild, copy the files into the SD-card image, boot, navigate, run. The
`--esxdos-stub` and `--esxdos-stub-root` options remove the copying step for
programs JNEXT loads directly. The short form serves the directory the NEX is
in:

```sh
jnext --esxdos-stub --load mygame/game.nex
```

The program can now open `mygame/data.bin` by name. If the data lives somewhere
else, name the directory:

```sh
jnext --esxdos-stub-root ./assets --load game.nex
```

`./assets` now *is* the filesystem the program sees (and it wins over the
NEX's own directory if you give both). `F_OPEN`, `F_READ`,
`F_SEEK`, `F_READDIR` and the rest of the esxDOS file and directory calls work
on the real files in that directory, including files in subdirectories. Edit a
file on the host, re-run, and the program reads the new version — nothing to
copy.

It is read-only unless you also pass `--esxdos-stub-writable`. With that flag
the program can also delete (`F_UNLINK`, wildcards too), rename (`F_RENAME`),
make and remove directories (`F_MKDIR`, `F_RMDIR`), truncate files
(`F_TRUNCATE`, `F_FTRUNCATE`) and change their read-only bit (`F_CHMOD`), and
ask for the text of an error code (`M_GETERR`): the calls a program uses to
manage its save slots, its data directory or a log, answered the way NextZXOS
answers them.

A few things about the short form, `--esxdos-stub` with no directory:

- It replaces the single in-memory file a directly loaded NEX otherwise gets,
  so a program that saves a file (a high-score table, say) is refused instead
  of saving into memory. Load the NEX without `--esxdos-stub` to keep the
  in-memory file, or add `--esxdos-stub-writable` to let it write beside the
  NEX.
- It lasts until the next reset, soft reset or load. A NEX loaded afterwards
  (from **File > Load NEX File…**, a recent file, a `run NAME.nex` call or the
  debugger) is served its own directory.
- A NEX taken out of a `.zip` has no directory on disk and gets none; neither
  do snapshots and tapes.
- For a NEX reached through a symbolic link, the directory is the link's own.
- A saved `.jns` snapshot does not remember it.

If the directory does not exist, JNEXT says so and stops, rather than starting
up with the option quietly doing nothing.

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

- `/` and `\` both separate components, and a leading separator means the top
  of *your* directory — `/data/level1.bin` is
  `<your directory>/data/level1.bin`, never the host's `/data`. (A host file
  whose name contains a backslash is therefore unreachable. FAT cannot name one
  either, so no program that expects a FAT card can ask for it.)
- lookup ignores case, so a program asking for `LEVEL1.BIN` finds `level1.bin`
- the esxDOS drive prefixes `*:`, `$:` and `c:` all mean the same directory
- long names get a synthesised 8.3 short name, the way FAT does, for programs
  that ask for short names, and a path can use either name. A short name keeps
  naming the same file for as long as it exists, so deleting `LONGNA~1.TXT`
  does not make `LONGNA~2.TXT` mean another file, as on a FAT card
- a name your host answers to that the directory does not list (an NTFS short
  name, a differently normalised spelling of an accented name) is refused,
  never handed to another file
- a trailing dot is dropped from a name, as FAT does (`save.` is `save`)
- the current directory reads back the way NextZXOS reports it: `C:/`, then
  each directory's 8.3 name followed by `/`, such as `C:/MYGAME~1/DATA/`

Anything that would leave the directory is refused (`esx_enotdir`, as
NextZXOS answers a path that does not exist), and so is any symbolic link —
links are not listed either, so a program is never offered a name it would then
be refused.

## Things it will not do

Some calls are refused outright rather than answered approximately, because a
wrong answer is worse than a clear refusal:

- sorted and filtered directory listings, and wildcard listings other than
  `*.*`. A listing request that NextZXOS answers with the plain listing, such
  as `*.*` or a sort/filter request with nothing selected, gets the plain
  listing here too. The entries are the same ones NextZXOS would list, but
  their order, the spelling of their names (your files' own case) and their
  read-only and hidden flags follow your host files, not a FAT card
- the `+3DOS` header modes of `F_OPEN` and `F_OPENDIR` — a host file has no
  `+3DOS` header and inventing one would feed the program eight bytes of
  fiction
- `M_P3DOS`, which calls NextZXOS ROM routines directly, including raw sector
  reads. A directory has no sectors to give.
- `F_CHMOD` of the hidden and system attributes, of the archive attribute
  cleared, and of a directory's attributes: a host file or directory has no
  such attribute for the next `F_STAT` or `F_READDIR` to show, so the call is
  refused with `esx_enosys` instead of answering success and showing something
  else. Setting or clearing the read-only bit works, and means the host write
  permission
- a name that ends in a space (a card keeps it, a host may strip it). A name
  with `"`, `<`, `>` or `|` in it is refused with `esx_einval`, as on the card,
  and so is a wildcard in a name that is being created
- `F_RENAME` never replaces a file that is already there (`esx_eexist`), as on
  the card

A few answers differ in small ways from the card, on purpose: an `F_OPEN` mode with neither the read nor the write bit is `esx_einval`; creating a file through the 8.3 short name of an existing long-named file truncates that file and keeps its long name (the card rewrites the entry and loses it); a renamed current
directory shows its new name in `F_GETCWD`; `F_TRUNCATE` on a write-protected
folder is refused (the card answers success and loses the write); and
`F_OPEN` for writing on a write-protected folder is `esx_erdonly`.

## Writing, and rewind

`--esxdos-stub-writable` is a second, separate flag on purpose. A guest write
lands on a real file on your disk straight away, and JNEXT's rewind cannot take
it back — the emulator's timeline goes backwards, your filesystem does not.

Reads are fully rewindable: an open file's position travels in the rewind
snapshot and is restored with it, so rewinding and re-running re-reads exactly
the same bytes. Deletions, renames, new and removed directories, truncations and
read-only changes are writes too, and are not undone: a handle that a rewind
finds pointing at a file that has since been deleted or renamed is closed, not
reopened.

Without `--esxdos-stub-writable` the folder answers like a write-protected
card: every call that would change something is refused with `esx_edevicebusy`,
after the checks that do not write (a missing name is still `esx_enoent`, an
existing directory `esx_eexist`), and nothing on the host changes.

## Timestamps

Directory entries and `F_STAT` report the host file's own modification time.
`M_GETDATE`, which asks what time it is *now*, answers from the emulated clock
instead — so it follows `--rtc` and stays deterministic in a scripted run.
