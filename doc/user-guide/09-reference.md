# 9. Reference

This chapter points at the authoritative sources rather than repeating them.
That is deliberate: a copy of an option list here would drift out of date the
first time an option changed, and a manual that quietly lies is worse than one
that sends you somewhere else.

## 9.1 Command-line options

The complete, current option reference is the man page:

```console
$ man jnext
```

The same content is in [`USAGE.md`](../../USAGE.md) in the repository, for
reading in a browser. **Both are generated from a single source**, so they
cannot disagree with each other.

If you ever find the documentation and the program disagreeing, the program is
right — please [report it](https://github.com/jorgegv/jnext/issues).

For a quick reminder without leaving the shell:

```console
$ jnext --help
$ jnext --version
```

## 9.2 Files and directories

Everything JNEXT keeps for you lives under `~/.jnext`.

| Path                                       | What it is                                                                       |
|--------------------------------------------|----------------------------------------------------------------------------------|
| `~/.jnext/jnext.conf`                      | GUI configuration, INI format. Written by **Settings > Preferences** (chapter 4). |
| `~/.jnext/sdcard/cspect-next-1gb-fixed.img` | The SD-card image used when `--sdcard` is not given.                             |
| `~/.jnext/sdcard/cspect-next-1gb.img`      | The canonical distribution image the one above is produced from.                  |

Two rules worth remembering:

- **Command-line options always beat saved settings.** A flag on the command
  line wins over the same setting in `jnext.conf`.
- **Headless runs never read the configuration file at all**, so an automated
  run (chapter 7) cannot be perturbed by whatever you last changed in
  Preferences.

Documentation installed with the packages — the man page, `README.md` and the
`ChangeLog` — goes to the usual system locations for your platform.

## 9.3 ZX Spectrum Next hardware documentation

JNEXT emulates the official ZX Spectrum Next FPGA core, and the VHDL source of
that core is the authority on hardware behaviour. For everything the VHDL does
not specify — file formats, firmware behaviour, host-side conventions — the
project keeps its external links in one place:
[`doc/REFERENCES.md`](../REFERENCES.md).

It holds the Next hardware wiki (registers, ports, the boot sequence, the NEX
file format) and the TBBlue firmware sources. That file is the single place
those URLs live, so they are not repeated here.

## 9.4 The project

| | |
|---|---|
| Home page      | <https://github.com/jorgegv/jnext>                    |
| Report a bug   | <https://github.com/jorgegv/jnext/issues>             |
| Feature list   | [`FEATURES.md`](../../FEATURES.md)                    |
| Release notes  | [`ChangeLog`](../../ChangeLog)                        |
| Building it    | [`BUILD.md`](../../BUILD.md)                          |
| Licence        | GPLv3 — [`LICENSE`](../../LICENSE)                    |
| Credits        | [`CREDITS.md`](../../CREDITS.md)                      |
