# Using your own image

If you already have a NextZXOS SD-card image, point JNEXT at it:

```sh
jnext --sdcard /path/to/my-image.img
```

An explicit `--sdcard` always wins: it is used as-is, and no download is ever
offered or performed. In the graphical version you can also mount one from
**File > Mount SD Card Image**, and set a permanent default in Preferences
(chapter 4).

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
