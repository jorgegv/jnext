# Upgrading NextZXOS on your card

JNEXT keeps your SD card in one place and knows which NextZXOS it came from. When a
newer JNEXT downloads a newer NextZXOS than your card has, you can move to it without
losing what you saved.

## Where your card lives

| Platform | Folder |
|---|---|
| Linux, macOS | `~/.jnext/sdcard/` |
| Windows | `%USERPROFILE%\.jnext\sdcard\` |

`cspect-next-1gb-fixed.img` is **your card**: NextZXOS runs from it and everything you
save lands on it. `cspect-next-1gb.img` is the original that JNEXT downloaded and made your
card from. Every other file and folder there belongs to JNEXT (the small sidecar files
beside the two images, `previous`, `upgrade-work`, `overlays.list`, `locks`, and
`test-suite` on a developer's machine): do not edit or delete them by hand.

## The offer

Once for each new NextZXOS release, before the machine starts, the windowed JNEXT asks
whether to upgrade. The box names the new and the old version, the size of the download,
the disk space needed while it runs, and says that your old card is kept as a backup.

- **Upgrade (keep my files)** downloads the new NextZXOS and builds your new card from it.
- **Not now** (or closing the box) goes on with your card and asks again next time.
- **Keep my card (don't ask again)** goes on with your card and stops the offer for this
  release; the next release asks once more.

Without a window (`--headless`, or `--sdcard-file-add`) JNEXT never asks. It writes one line
to the log:

```
sdcard: NextZXOS 26.11a is available; this card is NextZXOS 24.11. Run jnext --sdcard-upgrade yes to upgrade it (your files are kept, and the old card is kept as a backup).
```

You can change your mind later: **File > Upgrade SD Card...** in the window, or
`--sdcard-upgrade yes` (`--sdcard-upgrade ask` asks again after a "don't ask again").

## What an upgrade keeps

- The files you added or changed are copied to the new card, with their attributes and dates.
- Where the new NextZXOS changed the same file you changed, **its version wins** (typically
  files under `/SYS` and `/NEXTZXOS`).
- A file you deleted that the new NextZXOS has comes back.
- Your `config.ini` is kept, because JNEXT's own default for it did not change.
- A name that ends in a space or a dot cannot be written by JNEXT: it is listed as not
  carried and stays on the old card.

When it is done the window shows what was carried and what was replaced. The same list is
in the log and in `previous/upgrade-report.txt`.

If you use `--sdcard-writes FILE`, the same command line keeps working: JNEXT rebuilds the
file over the new card, and so every other writes file it has used with your default card.
`--sdcard-writes-reset` still discards.

## The backup

Your old card, the original it came from and the old contents of your writes files are kept
in `previous` next to the new card. The File menu has three entries:

| Entry | When it is available | Command line |
|---|---|---|
| **Upgrade SD Card...** | the card in use is your default card and an older NextZXOS | `--sdcard-upgrade yes` |
| **Go Back to the Previous SD Card** | a backup exists | `--sdcard-go-back` |
| **Delete the Previous SD Card Backup (size)...** | a backup exists | `--sdcard-delete-backup` |

Going back first checks that every writes file of the old card can be put back where it was (a missing folder is made); if one cannot, nothing is changed and you are told which. The report of the upgrade stays with the backup it describes. Going back **swaps** the two: the card in use becomes the backup and the machine restarts on
the old one. Delete gives the backup's disk space back (the entry names the size); it
cannot be undone. The command-line forms exit 0 when done, 2 when there is no backup, 3 when
they failed with nothing changed and 4 when another JNEXT has the card open.

## When your files cannot be kept

An upgrade compares your card with the card as it was first made. If the original image is
gone or damaged, or the card was re-formatted, it cannot tell your files from NextZXOS's. The
offer then says so, and its button reads **Fresh start (keep my card as a backup)**: a new
card with nothing carried over, your old card and writes files in the backup.
`--sdcard-upgrade fresh` does the same without the box, and `--sdcard-upgrade yes` refuses
with the reason.

## If something goes wrong

- A download or an upgrade that fails or is interrupted leaves your card exactly as it was.
- If JNEXT is killed while it switches cards, the next start finishes the switch.
- The upgrade needs room for the download, the new card, a rebuilt copy of the old one and
  your writes files: about three card images. With less free space it refuses before it
  downloads anything.

## One card, one jnext

Two JNEXTs on one card would each cache what the other changes. The second one is refused:

```
the SD card image /home/me/.jnext/sdcard/cspect-next-1gb-fixed.img is in use by another jnext (process 4242); give each jnext its own card (--sdcard FILE)
```

To run two, give each its own card image with `--sdcard FILE`. The same goes for a writes file. A hard link to a card counts as a
separate card.

The exception is `--sdcard-file-add`: when the JNEXT that has the card serves ZRCP
(`--zrcp-port`), the file is copied through it instead of being refused
([5. Running programs](../05-running-programs/index.md)). When it does not, the message ends with the
way out:

```
... is in use by another jnext (process 4242), which is not listening on ZRCP. Restart it with --zrcp-port PORT and run this command again: the file will be copied through it.
```

Both JNEXTs must run as the same user with the same configuration directory: a lock is a
file in `~/.jnext/locks/` (`$JNEXT_CONFIG_DIR/locks/`), and a JNEXT that does not see it
cannot tell the card is in use.

## An older JNEXT and a newer card

A JNEXT never changes a card of a newer NextZXOS than its own. It says so in the log, and in a
box once for each version, and uses the card as it is. (JNEXT releases from before this
feature may offer to replace such a card: answer **Keep my card**.)

## Getting your files out

These commands work on any card and exit without starting the machine.

- `--sdcard-writes-flatten OUT` writes one plain image of the card with your writes file
  applied, which any FAT tool can open.
- `--sdcard-export-changes DIR` copies the files you added or changed into `DIR` as ordinary
  files and writes `CHANGES.txt` beside them: one line for each, saying whether it was added,
  changed, a directory or removed, with its attributes. The default card is compared with the
  card as JNEXT made it; for another card give the image it was made from with
  `--sdcard-export-base`, or the writes file with `--sdcard-writes`.
- `--sdcard-import-changes DIR` puts such a folder onto a card (or into a writes file). A
  file the card already has is replaced by yours, and nothing is deleted.

Export, import and the upgrade's carrying over of your files, like `--sdcard-file-add`, work on the
first FAT32 partition of the card. A card of your own with a FAT12 or FAT16 volume, or no partition
table, still runs, but these commands cannot reach its files.

Moving to a new NextZXOS by hand is an export and an import ([3.3](03-using-your-own-image.md)).

## Messages

| You see | What it means, and what to do |
|---|---|
| `... is in use by another jnext (process N); give each jnext its own card` | Another JNEXT has the card or its writes file open. Close it or give this one its own card. |
| `... is in use by another jnext (process N), which is not listening on ZRCP. Restart it with --zrcp-port PORT ...` | You asked for `--sdcard-file-add` on a card a running JNEXT has open, and that JNEXT cannot take the copy. Restart it with `--zrcp-port PORT` and run the command again. |
| `the files on this card cannot be carried over (...); --sdcard-upgrade fresh starts a new card and keeps this one as the backup` | The reason is in the brackets. Use `fresh`, or restore the original image. |
| `The upgrade to NextZXOS X failed, so the current SD card image stays in use and nothing was changed.` | The reason follows. Try again with `--sdcard-upgrade yes`. |
| `not enough free disk space for the upgrade` | Free the disk space it names. Nothing was downloaded. |
| `an interrupted SD card upgrade could not be finished` | Look at the reason; the old set is in `previous`. |
| `It holds writes for your previous SD card (NextZXOS X, kept as a backup): go back to it (--sdcard-go-back), or discard the writes with --sdcard-writes-reset.` | That writes file belongs to the card you upgraded away from. |
| `this SD card is NextZXOS X, newer than this jnext's NextZXOS Y; it is used as it is` | Nothing to do; update JNEXT when you can. |
| `there is no previous SD card to go back to` | No backup exists (exit 2). |
| `there is no previous SD card backup to delete` | `--sdcard-delete-backup` found no backup (exit 2). Nothing to do. |
| `cannot delete ...` | The backup could not be removed; the reason follows (exit 3). Close whatever holds the files and try again. |
| `an earlier go-back is unfinished: start jnext once to finish it` | A go back was interrupted. Start JNEXT normally once; it finishes the switch (exit 3). |
| `cannot restore the writes file '...' (its folder cannot be written to); nothing was changed` | The backup holds a writes file whose folder cannot be written to, so the go back refused with everything as it was (exit 3). Make the folder writable and try again. |
| `sdcard: could not restore the writes file ... its copy is kept in ...` | In the log. The folder became unwritable during the go back; the copy is in `previous/overlays` under the name shown. Copy it where you want it. |
| `Your files cannot be carried over (...); jnext --sdcard-upgrade fresh makes a new card and keeps this one as the backup.` | In the log, when a newer NextZXOS is available and the card cannot be compared with its original. Use `--sdcard-upgrade fresh`, or restore the original image. |
| `nothing to compare against: this card was not made by jnext ...` | `--sdcard-export-changes` has no original to compare with (exit 3). Give the image the card was made from with `--sdcard-export-base FILE`, or the writes file with `--sdcard-writes FILE`. |
| `'...' already exists; give a new file name` | `--sdcard-writes-flatten` will not overwrite a file (exit 3). Name a new file. |
| `'...' is not empty; export into a new or empty directory` | `--sdcard-export-changes` will not mix with other files (exit 3). Use a new or empty directory. |
| `... is not a directory made by --sdcard-export-changes (no CHANGES.txt)` | `--sdcard-import-changes` needs the folder an export made (exit 2). Point it at that folder. |
| `... is not a jnext changes list (expected '...')` | The `CHANGES.txt` is not one JNEXT wrote (exit 2). Use the file an export made, unedited. |
