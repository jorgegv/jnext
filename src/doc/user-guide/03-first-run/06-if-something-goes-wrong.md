# If something goes wrong

**The download failed, or the image looks broken.** Ask JNEXT to fetch and
prepare it again:

```sh
jnext --sdcard-download-force
```

This only affects the image in `~/.jnext/sdcard/`; it is ignored when you pass
an explicit `--sdcard`. It downloads the distribution again and builds a new
working copy from it, so **any files you saved on the card are lost**.

**The boot menu's ZX81 entry stops on `zx81.rom...error reading!`.** Your
working copy was prepared by an earlier version of JNEXT, before it doubled
that ROM (see [Letting JNEXT fetch one](02-letting-jnext-fetch-one.md)).
Existing working copies are not fixed automatically, because they hold your
files. `jnext --sdcard-download-force` fixes it: it downloads the image again
and replaces your working copy, **including anything you saved on it**. The
firmware remembers the ZX81 choice on the card, so until then every boot goes
straight to that error: press SPACE while the firmware starts and pick another
entry.

An image you pass with `--sdcard` has the same problem if it was made from that
distribution, and `--sdcard-download-force` never touches it. Run
`tools/fix-sdcard-image.sh` from JNEXT's source repository on it instead (it
needs mtools), for example `tools/fix-sdcard-image.sh my.img my-fixed.img` to
keep the original. It makes the same change to `zx81.rom`, keeps your files,
and resets the firmware settings in `/MACHINES/NEXT/config.ini` to the
defaults.

**You are scripting JNEXT and it stops to ask a question.** Use
`--sdcard-download-confirm` to accept the download without prompting, or pass
`--sdcard` so there is nothing to ask about. An unattended run that is asked a
question it cannot answer will decline and exit rather than hang.

**It boots to a black screen or an error.** Check that the image you supplied
is a NextZXOS SD-card image and not, say, a raw ROM file. If you built the
image yourself, note that JNEXT enforces the FAT32 specification as strictly as
the real firmware does, and will refuse a card the real machine would also
refuse.

Anything else: chapter 8, *Known issues*, and
<https://github.com/jorgegv/jnext/issues>.
