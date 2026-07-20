# If something goes wrong

**The download failed, or the image looks broken.** Ask JNEXT to fetch and
prepare it again:

```sh
jnext --sdcard-download-force
```

This only affects the image in `~/.jnext/sdcard/`; it is ignored when you pass
an explicit `--sdcard`.

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
