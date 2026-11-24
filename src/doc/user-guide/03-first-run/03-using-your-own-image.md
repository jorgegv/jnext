# Using your own image

If you already have a NextZXOS SD-card image, point JNEXT at it:

```sh
jnext --sdcard /path/to/my-image.img
```

An explicit `--sdcard` always wins: it is used as-is, and no download is ever
offered or performed. In the graphical version you can set a permanent default
in Preferences (chapter 4), and change the card while JNEXT runs (below).

The image is opened **read-write**, and whatever the emulated machine writes to
the card is kept. Booting is not one of those things: reaching the NextZXOS
welcome screen and the menu writes nothing at all, and leaves the image
byte-identical. What changes it is *file* work done through NextZXOS — saving
from NextBASIC, a DOS command such as `.mkdir`, the Browser copying or deleting
something, or a program writing a file. Those changes persist into later runs,
exactly as they would on a real card, so two runs sharing one image are not
independent once one of them has written.

Add `--sdcard-readonly` when a run must not disturb the image: the emulated
machine then sees a write-protected card and the host file is never touched.

To keep the image pristine and still let the machine save, use
`--sdcard-writes FILE`: the image is opened read-only and what the machine writes
goes to *FILE*, which is laid over the image every time the card is mounted, so the
saves survive the next run. *FILE* belongs to that image (its `--sdcard-sha256`, else
its size and modification time) and is refused for another one; delete it, or add
`--sdcard-writes-reset`, to start from the image again. It holds only the sectors the
machine wrote, so it stays small, plus the one file that JNEXT's esxDOS stand-in keeps
for a NEX loaded directly (see "Running programs"): that file is stored when the
program closes it and is there at the next run. `--sdcard-file-add` works with it too:
the file you add goes into *FILE* and the image stays untouched.

## Changing the card while JNEXT runs

**File > Insert SD Card Image** puts a different card in the running machine,
and **File > Eject SD Card** takes it out. Nothing is reset, so you keep what
you were doing — a RAMdisk, say, while you copy files from one card to another.

The Next has no way to notice a card being pulled, so NextZXOS has to be told:
type **REMOUNT**. It answers *Remove/insert SD and press Y*; change the card from
the File menu, then press Y. NextZXOS reads the new card and lists its drives
again.

A few things to know:

- The inserted card is write-protected if JNEXT was started with
  `--sdcard-readonly`, like the first one. It does not become your saved
  default; that is Preferences' job.
- The machine keeps the ROMs it started with — a real Next loads them once, at
  power-on. A **hard reset**, or loading a program, reads them from the card that
  is in. After an **eject**, those put the last inserted card back, since JNEXT
  cannot start a machine without one.
- The card cannot be changed while an RZX recording is being made or played, or
  while a directly loaded `.nex` keeps its own file open. JNEXT says why.
- Changing the card clears the debugger's rewind history and drops the warm-start
  recording (the next NEX load records again from the new card).
- A `.jns` snapshot saved with one card refuses to load once a *different* card
  is in (chapter 5, Snapshots). A copy of the same card is the same card to it.

## A second SD card (D:)

The Next has two SD card sockets, and JNEXT can have a card in each. NextZXOS gives
every FAT partition of every card a drive letter, the first card first: a first card
with one partition is **C:**, and the first partition of the second card is **D:**.
It is a handy place for your own programs or for a disk you want to keep apart from
NextZXOS. The slot is optional: with nothing in it JNEXT behaves exactly as if the
slot did not exist.

The second card is an image of its own: a partition table with one FAT partition, for
example a copy of a real card made with `dd`. It must not be the file you boot from;
JNEXT refuses to put the same image in both slots.

Put it in with `--sdcard2 FILE` (with `--sdcard2-readonly` to write-protect it, or
`--sdcard2-writes FILE` to keep what NextZXOS writes in a side file and leave the image
alone), or save the path in **Preferences > Paths > Second SD card image**. A saved path
that no longer exists is logged and ignored; a path you give on the command line that
cannot be used stops JNEXT with a message.

NextZXOS finds its drives when it starts, so the card has to be in the slot before the
machine boots or is hard-reset. In the running machine use **File > Insert Second SD
Card Image** and **File > Eject Second SD Card**, then **REMOUNT** as for the first card,
or reset. After an **eject** of the second card a hard reset keeps the slot empty, and
after an **insert** it keeps the new card. (The first card is different: it stays, because
JNEXT reads its ROMs from it.)

To see what is on it, type `CD "d:"` and then `.ls`. Do not type `.nexload d:/game.nex`:
the colon separates BASIC statements, so NextZXOS sees only `d`. Change into the drive
first, or quote the path.

ROMs and the firmware always come from the first card. `--sdcard-file-add` works on any
FAT32 image, so `jnext --sdcard card2.img --sdcard-file-add game.nex` puts a file on
a second-card image.

`--sdcard-file-extract` is the other way round, and reads any card the Next mounts, FAT12, FAT16
or FAT32, with or without a partition table: `jnext --sdcard FILE --sdcard-file-extract /DIR/NAME`
copies the file into the current directory.

## Moving to a new NextZXOS by hand

An image you name yourself is never upgraded for you. To move your files to a new NextZXOS:

1. Get the new distribution and make its card (`tools/fix-sdcard-image.sh`, or let JNEXT fetch it
   into a separate `JNEXT_CONFIG_DIR`).
2. Export your changes from the old card:
   `jnext --sdcard OLD.img --sdcard-export-base ORIGINAL.img --sdcard-export-changes ~/my-files`,
   where `ORIGINAL.img` is the image the old card was made from. If you keep your changes in a
   writes file, `--sdcard-writes FILE` replaces `--sdcard-export-base`.
3. Open `~/my-files/CHANGES.txt` and delete the lines of files you do not want on the new card (for
   example a `config.ini` written for the old NextZXOS).
4. Import them: `jnext --sdcard NEW.img --sdcard-import-changes ~/my-files`. Add `--sdcard-writes FILE2`
   to keep them in a writes file instead.

A file the new card already has is replaced by yours: that is the opposite of the automatic upgrade,
where the new NextZXOS's version wins. The report says which files were written and which replaced.
Files you deleted are not moved. See [3.7](07-upgrading-nextzxos.md) for the other rescue commands.
