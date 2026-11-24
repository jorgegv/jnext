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

- **Upgrade (keep my files)** downloads the new distribution and builds a new image
  from it. The files you saved on the old image (your programs, your files and
  NextZXOS's settings) are carried over to the new one; where NextZXOS itself
  changed a file you also changed, the new version wins, and you get a list of
  what was carried and what was replaced. Your old image is kept untouched as a
  backup. Nothing is changed until the new image is complete, so a download or a
  preparation that fails leaves your image exactly as it was. If your image can no
  longer be compared with the one it came from (the original image is gone, or the
  image was rebuilt), the files cannot be carried over: the new image starts
  fresh, and your old one, with everything on it, is kept.
- **Not now** (also what closing the question means) goes on with your image and
  asks again the next time.
- **Keep my card (don't ask again)** stops the question for this distribution. The
  question comes back once for the next one.

### The backup of your old image

After an upgrade your old image is in the `previous` folder next to the new one, together with the old contents of
your writes files and a report of what was carried over. The File menu has three entries for it: **Upgrade SD
Card...** (enabled while the card in use is the default one and an older distribution), **Go Back to the
Previous SD Card** (swaps the two: the card in use becomes the backup and the machine restarts on the old one;
the distribution you left is not offered again) and **Delete the Previous SD Card Backup (size)...**, which gives
the disk space back. The same can be done without a window with `--sdcard-go-back` and
`--sdcard-delete-backup`. A file whose name ends in a space or a dot cannot be written by JNEXT: an upgrade lists
it as not carried and leaves it on the old image.

Without a window (`--headless`, or `--sdcard-file-add`) JNEXT never asks. It keeps
your image and writes one line to the log saying how to upgrade. You can answer
without the dialog with `--sdcard-upgrade`: `yes` upgrades the image and carries your files over (refused when they cannot be
carried over), `fresh` makes a new image and carries nothing over, `no` keeps
it for this run, `never` keeps it and records "do not ask again", and `ask`
forgets that record and asks.

An image you name yourself (`--sdcard FILE`, or an image path in Preferences) is
never offered an upgrade, and `--sdcard-upgrade` is refused with `--sdcard`. Each
distribution's image also has its own FAT32 volume serial.
