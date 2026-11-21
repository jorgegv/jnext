# Letting JNEXT fetch one

You do not have to find an image yourself. On the first run, with no SD card
configured, JNEXT offers to download the official ZX Spectrum Next
distribution image:

```
No SD-card image was found at ~/.jnext/.

Download the NextZXOS official distribution image and install it there? [y/N]
```

In the graphical version the same question appears as a dialog titled **jnext -
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
    - `/MACHINES/NEXT/zx81.rom` is made 32 KB long, the same 16 KB ROM twice,
      when the distribution ships it as 16 KB (NextZXOS 24.11 did; 26.11 and 26.11a already
      ship 32 KB and is left alone). The firmware's boot menu loads the ZX81
      entry as 32 KB and stops on `zx81.rom...error reading!` otherwise. The
      ZX Spectrum Next team made the same change after the 24.11 distribution
      was released. JNEXT makes it only when the file is exactly the one that
      distribution ships. A working copy prepared by an earlier version of
      JNEXT is not fixed automatically; see
      [If something goes wrong](06-if-something-goes-wrong.md).
3. Boot from the working copy.

Both files are kept in `~/.jnext/sdcard/`, and together they take about 2 GB of
disk space. The download happens once; every later run reuses it and starts
immediately. If the download stops (network trouble, Cancel, closing JNEXT), the
part already fetched is kept as `sn-emulator.zip.part` in `~/.jnext/sdcard`, and
the next start continues from there (the question then says so).

If no data arrives for a few seconds, the progress dialog says the download has stalled and for
how long; after 30 seconds without data JNEXT stops waiting and says why. Cancel works at any
time. Without the dialog (`--headless`, or a build with no Qt window) the same notice is printed
on the terminal and written to the log.

When a download fails, JNEXT says which address it was fetching, what went wrong in plain words
(with the error code after it, for anyone who has to look it up), and whether the part already
fetched was kept. The address can be selected and copied from the message: try it in a web
browser to see whether the file can be reached at all.

Answering no leaves JNEXT with nothing to boot from, so it explains the problem
and exits.

## NextZXOS 26.11a and the FPGA core

The distribution JNEXT downloads is NextZXOS 26.11a (NextZXOS 2.10a). It is made for
FPGA core 3.03.03 and ships no core update file, while JNEXT emulates core 3.02.03,
the one it follows. NextZXOS 2.10a boots and runs on it, and the firmware shows core
3.02.03 on its splash screen. Whatever core 3.03.03 adds over 3.02.03 is not emulated.

## When a newer distribution is available

JNEXT remembers which distribution each image came from. When a version of JNEXT
downloads a newer distribution than the one your image came from, it tells you so
and, in the graphical version, asks what to do:

- **Download and replace** downloads the new distribution and replaces your image
  with it. **Everything you saved on the old image is lost**: your programs, your
  files and NextZXOS's settings. Nothing is changed until the new image is
  complete, so a download or a preparation that fails leaves your image exactly as
  it was.
- **Keep my card** (also what closing the question means) goes on with your image
  and asks again the next time.
- Ticking **Do not ask me again** with Keep stops the question for this
  distribution. The question comes back once for the next one.

Without a window (`--headless`, or `--sdcard-file-add`) JNEXT never asks. It keeps
your image and writes one line to the log saying how to upgrade. You can answer
without the dialog with `--sdcard-upgrade`: `yes` replaces the image, `no` keeps
it for this run, `never` keeps it and records "do not ask again", and `ask`
forgets that record and asks.

An image you name yourself (`--sdcard FILE`, or an image path in Preferences) is
never offered an upgrade, and `--sdcard-upgrade` is refused with `--sdcard`. Each
distribution's image also has its own FAT32 volume serial.
