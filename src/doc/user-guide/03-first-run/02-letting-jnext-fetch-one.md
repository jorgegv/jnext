# Letting JNEXT fetch one

You do not have to find an image yourself. On the first run, with no SD card
configured, JNEXT offers to download the official ZX Spectrum Next
distribution image:

```
No SD-card image was found at ~/.jnext/.

Download the NextZXOS official distribution image and install it there? [y/N]
```

In the graphical version the same question appears as a dialog titled **jnext —
SD card image**. Answer yes and JNEXT will:

1. Download the official distribution archive and unpack the SD-card image from
   it, showing progress as it goes.
2. Prepare a working copy of that image, keeping the downloaded original
   untouched. The copy differs from the original in three ways:
    - its FAT32 partition is re-formatted with smaller clusters and the same
      files. The original has too few clusters to be valid FAT32, and the
      Next's firmware refuses such a card, as the real machine does;
    - it gets a default `/MACHINES/NEXT/config.ini`, the firmware's settings
      file;
    - `/MACHINES/NEXT/zx81.rom` is made 32 KB long: the same 16 KB ROM, twice.
      The distribution ships it as 16 KB, but the firmware's boot menu loads
      the ZX81 entry as 32 KB and stops on `zx81.rom...error reading!`. The
      ZX Spectrum Next team made the same change after that distribution was
      released. JNEXT makes it only when the file is exactly the one the
      distribution ships.
3. Boot from the working copy.

Both files are kept in `~/.jnext/sdcard/`, and together they take about 2 GB of
disk space. The download happens once; every later run reuses it and starts
immediately.

Answering no leaves JNEXT with nothing to boot from, so it explains the problem
and exits.
