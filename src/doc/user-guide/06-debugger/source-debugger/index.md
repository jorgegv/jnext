# The Source Debugger

When your program comes with a **source map**, JNEXT's debugger shows the line
of C, assembly or BASIC the CPU is running, stops on a source line
(`main.c:19`), and steps one source statement at a time: into calls, over them,
out of them, and backwards with rewind. This section is the task-oriented
guide to it, with a worked example in C built with z88dk. The debugger itself
is in [chapter 6](../index.md); the **Source** tab's controls are
in [its panel page](../panels/15-source.md).

A source map is a file your build writes beside the program. JNEXT finds it by
itself when the program is loaded, checks that it belongs to that program, and
attaches it. Which file you need depends on the tool:

| You build with | The file | What it brings |
|---|---|---|
| z88dk (`zcc ... -debug -m`) | `prog.map` beside the program, plus `prog_CODE.bin` and, with `--list`, `prog.lis` and one `.lis` per source | C and assembly lines, symbols; the binaries are the identity check; the listings add every assembly line and exact extents |
| sjasmplus (`--sld`) | `prog.sld` beside the program | assembly lines and labels; an optional SHA-256 is the identity check |
| Boriel ZX Basic / NextBuild | `Memory.txt` (or `prog.Memory.txt`) beside the program | symbols only; lines need an SLD, see [sjasmplus and Boriel ZX Basic](04-sjasmplus-and-boriel.md) |
| anything else | an SLD or a z88dk map you load by hand | as above |

## The pages

- [A first session](01-a-first-session.md): a C program from source to a
  breakpoint, steps and a watch.
- [Building programs with z88dk](02-building-with-z88dk.md): the options,
  which compiler and library work on which z88dk, tape and snapshot builds,
  banked code.
- [Dot commands](03-dot-commands.md): debugging a NextZXOS `.command` from its map.
- [sjasmplus and Boriel ZX Basic](04-sjasmplus-and-boriel.md): the SLD and
  `Memory.txt` routes.
- [Loading a source map, and finding the files](05-loading-a-source-map.md):
  sidecars, by hand, `--source-map`, tapes, binary identity, what a reset drops,
  and where the Source tab looks for the sources.
- [Breakpoints, steps and running to a line](06-breakpoints-and-steps.md):
  `file:line`, Into / Over / Out / Back, Run to Here.
- [Banked code, and which memory a line is in](07-banked-code.md): pages, the
  `P04` and `ROM0` tags, Physical Addresses, z88dk's `__banked`.
- [In the Source tab: names, watches, coverage and colours](08-around-the-source.md):
  C names, watching and breaking from the source, which lines have run, and
  the colour schemes.
- [Scripts and remote debuggers](09-scripts-and-remote-debuggers.md): `on
  execute main.c:19`, z88dk-gdb, DeZog.
- [Limits and troubleshooting](10-limits-and-troubleshooting.md): what each
  message means and what to do, then the known limits.

## What you will need

The examples use the sources JNEXT's own tests are built on, in
`test/fixtures/z88dk/src/` of the repository (`main.c`, `util.c`, `util.h`,
`asmfn.asm`). They are quoted in full in [A first session](01-a-first-session.md), so you do not need a clone. You
need z88dk (2.4 or newer), a ZX Spectrum Next SD card image set up as in
[First run](../../03-first-run/index.md), and the Qt build of JNEXT, which has the
debugger window.
