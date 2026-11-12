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
program closes it and is there at the next run.

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
