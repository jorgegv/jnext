# 9.2 Files and directories

Everything JNEXT keeps for you lives under `~/.jnext`.

| Path                                       | What it is                                                                       |
|--------------------------------------------|----------------------------------------------------------------------------------|
| `~/.jnext/jnext.conf`                      | GUI configuration, INI format. Written by **Settings > Preferences** (chapter 4). |
| `~/.jnext/sdcard/cspect-next-1gb-fixed.img` | The SD-card image used when `--sdcard` is not given.                             |
| `~/.jnext/sdcard/cspect-next-1gb.img`      | The canonical distribution image the one above is produced from.                  |
| `~/.jnext/warm-start/`                     | The recorded NextZXOS machine a `.nex` load starts from ([5. Running programs](../05-running-programs/index.md)). One file per machine type, about 100 KB. Deleting it costs one cold boot. |
| `~/.jnext/screenshots/`                    | Where **File ▸ Quick Screenshot** writes, unless Preferences says otherwise ([5.3 Display](../05-running-programs/03-display.md)). Created on the first capture. |

Two rules worth remembering:

- **Command-line options always beat saved settings.** A flag on the command
  line wins over the same setting in `jnext.conf`.
- **Headless runs never read the configuration file at all**, so an automated
  run (chapter 7) cannot be perturbed by whatever you last changed in
  Preferences.

Documentation installed with the packages — the man page, `README.md` and the
`ChangeLog` — goes to the usual system locations for your platform.
