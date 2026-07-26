# Using your own image

If you already have a NextZXOS SD-card image, point JNEXT at it:

```sh
jnext --sdcard /path/to/my-image.img
```

An explicit `--sdcard` always wins: it is used as-is, and no download is ever
offered or performed. In the graphical version you can also mount one from
**File > Mount SD Card Image**, and set a permanent default in Preferences
(chapter 4).

The image is opened **read-write**, and what the emulated machine writes to the
card is kept — booting NextZXOS changes the file, so two runs sharing one image
are not independent. Add `--sdcard-readonly` when a run must not disturb the
image: the emulated machine then sees a write-protected card and the host file
is never touched.
