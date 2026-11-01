# 10. Source-level debugging

When your program comes with a **source map**, JNEXT's debugger shows the line
of C, assembly or BASIC the CPU is running, stops on a source line
(`main.c:19`), and steps one source statement at a time: into calls, over them,
out of them, and backwards with rewind. This chapter is the task-oriented
guide to it, with a worked example in C built with z88dk. The debugger itself
is in [chapter 6](../06-debugger/index.md); the **Source** tab's controls are
in [its panel page](../06-debugger/panels/15-source.md).

A source map is a file your build writes beside the program. JNEXT finds it by
itself when the program is loaded, checks that it belongs to that program, and
attaches it. Which file you need depends on the tool:

| You build with | The file | What it brings |
|---|---|---|
| z88dk (`zcc ... -debug -m`) | `prog.map` beside the program, plus `prog_CODE.bin` and, with `--list`, `prog.lis` and one `.lis` per source | C and assembly lines, symbols; the binaries are the identity check; the listings add every assembly line and exact extents |
| sjasmplus (`--sld`) | `prog.sld` beside the program | assembly lines and labels; an optional SHA-256 is the identity check |
| Boriel ZX Basic / NextBuild | `Memory.txt` (or `prog.Memory.txt`) beside the program | symbols only; lines need an SLD, see 10.4 |
| anything else | an SLD or a z88dk map you load by hand | as above |

## The pages

- [10.1 A first session](01-a-first-session.md): a C program from source to a
  breakpoint, steps and a watch.
- [10.2 Building programs with z88dk](02-building-with-z88dk.md): the options,
  which compiler and library work on which z88dk, tape and snapshot builds,
  banked code.
- [10.4 sjasmplus and Boriel ZX Basic](04-sjasmplus-and-boriel.md): the SLD and
  `Memory.txt` routes.
- [10.8 In the Source tab: names, watches and coverage](08-around-the-source.md):
  C names, watching and breaking from the source, and which lines have run.
- [10.10 Limits and troubleshooting](10-limits-and-troubleshooting.md): what each
  message means and what to do, then the known limits.

The rest of the subject is described on one page of the debugger chapter for
now: [Source-level debugging](../06-debugger/functions/14-source-level-debugging.md)
covers [loading a source map](../06-debugger/functions/14-source-level-debugging.md#loading-them)
(sidecars, by hand, `--source-map`, tapes, binary identity),
[where the Source tab finds the files](../06-debugger/functions/14-source-level-debugging.md#where-the-source-tab-finds-the-files),
[banked code](../06-debugger/functions/14-source-level-debugging.md#banked-code),
[breakpoints on a source line](../06-debugger/functions/14-source-level-debugging.md#breakpoints-on-a-source-line),
[running to a source line](../06-debugger/functions/14-source-level-debugging.md#running-to-a-source-line)
and [scripts and z88dk-gdb](../06-debugger/functions/14-source-level-debugging.md#from-scripts-and-z88dk-gdb).

## What you will need

The examples use the sources JNEXT's own tests are built on, in
`test/fixtures/z88dk/src/` of the repository (`main.c`, `util.c`, `util.h`,
`asmfn.asm`). They are quoted in full in 10.1, so you do not need a clone. You
need z88dk (2.4 or newer), a ZX Spectrum Next SD card image set up as in
[First run](../03-first-run/index.md), and the Qt build of JNEXT, which has the
debugger window.
