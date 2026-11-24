# A.2 Files and directories

Everything JNEXT keeps for you lives under `~/.jnext`.

| Path                                       | What it is                                                                       |
|--------------------------------------------|----------------------------------------------------------------------------------|
| `~/.jnext/jnext.conf`                      | GUI configuration, INI format. Written by **Settings > Preferences** (chapter 4); also holds the debugger window layout. |
| `~/.jnext/sdcard/cspect-next-1gb-fixed.img` | The SD-card image used when `--sdcard` is not given.                             |
| `~/.jnext/sdcard/cspect-next-1gb.img`      | The canonical distribution image the one above is produced from.                  |
| `~/.jnext/sdcard/previous/`                 | The backup an upgrade leaves: the old card, its original image and sidecars, `overlays/` (your old writes files) and `upgrade-report.txt`. Managed by the File menu entries and `--sdcard-go-back` / `--sdcard-delete-backup` ([3.7](../03-first-run/07-upgrading-nextzxos.md)). |
| `~/.jnext/sdcard/overlays.list`            | The `--sdcard-writes` files used with the default card, one path per line, so an upgrade rebuilds them too. |
| `~/.jnext/sdcard/*.distro`, `*.recipe`, `*.sha256`, `*.distro-declined`, `*.newer-seen` | Small files beside the images: which NextZXOS each came from, how the card was made, the original's checksum, the release whose offer you turned down, the newer release a notice was shown for. |
| `~/.jnext/sdcard/upgrade-work/`            | Scratch while an upgrade runs; removed when it ends or at the next start. |
| `~/.jnext/locks/`                          | The locks that keep a second JNEXT off a card or writes file. |
| `~/.jnext/warm-start/`                     | The recorded NextZXOS machine a `.nex` load starts from ([5. Running programs](../05-running-programs/index.md)). One file per machine type, about 100 KB, valid for one SD image (its size and modification time, or the `--sdcard-sha256` digest you gave). Deleting it costs one cold boot. |
| `~/.jnext/snapshots/`                      | Where **File ▸ Quick Save Snapshot** writes and **Quick Load Snapshot** reads the newest `.jns` ([5.9 Snapshots](../05-running-programs/09-snapshots.md#quick-save-and-quick-load)). Created on the first save. Quick Save keeps the newest 20 files it made (`--quick-snapshot-keep`). |
| `~/.jnext/screenshots/`                    | Where **File ▸ Quick Screenshot** writes, unless Preferences says otherwise ([5.3 Display](../05-running-programs/03-display.md)). Created on the first capture. |

In a web page JNEXT keeps only the SD card it made, the warm-start recording and, for a page
that asks with `keepWrites`, the program's saves, in the
browser's storage for the page's origin ([8.4](../08-web-page/04-the-image-and-what-the-browser-keeps.md)).

Beside a program you load, JNEXT also reads the debugger's files when they are
there: `NAME.sld` or `NAME.sld.txt` (a source map) and, for a `.nex`,
`NAME.Memory.txt` or `Memory.txt` (symbols). It never writes them. See
[Loading a source map](../06-debugger/source-debugger/05-loading-a-source-map.md).

Two rules worth remembering:

- **Command-line options always beat saved settings.** A flag on the command
  line wins over the same setting in `jnext.conf`.
- **Headless runs never read the configuration file at all**, so an automated
  run (chapter 7) cannot be perturbed by whatever you last changed in
  Preferences.

Documentation installed with the packages — the man page, `README.md` and the
`ChangeLog` — goes to the usual system locations for your platform.
