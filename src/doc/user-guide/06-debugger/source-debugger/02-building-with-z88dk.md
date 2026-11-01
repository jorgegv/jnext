# Building programs with z88dk

JNEXT reads the map z88dk writes with `-debug -m`. This page is the reference
for building a program so that the Source tab works: which options, which
files must end up where, which compilers and libraries work on which z88dk,
and what changes for tape builds and banked code. [A first session](01-a-first-session.md)
uses the first line below.

Every command line here was run with z88dk 2.4 (commit 4d530b6eb7, sdcc 4.5.0)
and with a master build of 2026-10-16 (commit 39421a01cd, sdcc 4.6.0), unless a
table says one of them is different.

## The recommended build

```
zcc +zxn -clib=new -debug -m --list -subtype=nex -create-app -o prog main.c util.c asmfn.asm
```

| Option | What it gives you |
|---|---|
| `-debug` | the C line records in the map (`__C_LINE_` symbols). With sccz80 and 80cc it also changes the program: see below |
| `-m` | writes `prog.map`, the file JNEXT reads |
| `--list` | writes `prog.lis` and one `<source>.lis` beside each source: every assembly line, every address of a C line, exact extents, and the text of the start-up code |
| `-subtype=nex` and `-create-app` | write `prog.nex`, the loadable file (see the targets below) |
| `-clib=new` | the sccz80 compiler with the new library. A bare `zcc +zxn` uses sdcc: `zxn.cfg` sets `-clib=sdcc_iy -O2 -SO2` |

The minimum is `-debug -m`. With it JNEXT gets the C lines and the symbols; for
the example program that is 46 source lines instead of 281 with `--list`.

Loading `prog.nex` gives (z88dk 2.4):

```
Loaded 332 z88dk symbols from '/home/you/prog/prog.map'
Loaded 281 z88dk source lines from '/home/you/prog/prog.map' and 4 listings (program identity verified)
```

and 333 symbols with the same 281 lines on the master build.

## What must be beside what

| File | Where | Why |
|---|---|---|
| `prog.map` | beside the program, with the same stem | found by itself when the program is loaded |
| `prog_CODE.bin` (and `prog_<SECTION>.bin` for banked sections) | beside the map | the **binary identity**: JNEXT compares the bytes of every code range with the program in memory. z88dk's linker writes them with or without `-create-app`; `-create-app` makes the loadable file |
| `prog.lis` | beside the map | the start-up code's text (labelled `m4 output, from prog.lis`) |
| `main.c.lis`, `util.c.lis`, `asmfn.asm.lis` | where zcc wrote them: beside each source | found there, whatever the map's directory |
| the sources | beside the map, in a parent directory, or on the search path | [where the Source tab finds the files](../functions/14-source-level-debugging.md#where-the-source-tab-finds-the-files) |

If the map is not beside the program, load it by hand with **Load Source
Map...** in the Source tab, or with `--source-map FILE` after `--load`.

A build in another directory works too. This was run from `build/` with the
sources in `../src/`:

```
cd build
zcc +zxn -clib=new -debug -m --list -subtype=nex -create-app -o prog ../src/main.c ../src/util.c ../src/asmfn.asm
```

`prog.nex`, `prog.map`, `prog.lis` and `prog_CODE.bin` land in `build/`, the
`.lis` files land beside the sources (`src/main.c.lis`), and JNEXT finds them
there. The map records `../src/main.c`, and the Source tab finds the file.

## Which compiler and library work

| Build | z88dk 2.4 | z88dk master (39421a01cd) |
|---|---|---|
| sccz80, newlib: `+zxn -clib=new -debug -m --list` | works: 281 lines. Part of a statement can show on the next line | works: 281 lines |
| sdcc, newlib: `+zxn -clib=sdcc_iy -debug -m --list` | **does not build**: `main.c:224: error: syntax error ^---- defc Gcounter0_00==.` (z88dk issue 3172). Use `--c-code-in-asm` instead (next row) | works: 222 lines. Code from a header can show under the wrong line |
| sdcc on 2.4: `+zxn -clib=sdcc_iy --c-code-in-asm -m --list` (no `-debug`) | works: 230 lines, from the listings alone | not needed |
| 80cc: `+zxn -clib=new -compiler=80cc -debug -m --list` | not available | works: 234 lines. Some lines are shared |
| classic on the Next: `+zxn -clib=classic -debug` | **fails**: `Unknown option2 --hardware-keyboard` | fails the same way |
| classic on the Next, no `-debug`: `+zxn -clib=classic -m --list` | builds: 171 lines, from the listings. No `prog_CODE.bin`, so the identity is not verified | builds: 171 lines, same |
| classic on the 48K Spectrum: `+zx -debug -m --list` (no `-clib`: classic is the default) | works: 251 lines. No `prog_CODE.bin`: `(program identity not verified: no section binaries beside it)` | works |

Notes:

- On `+zx` classic is the default library; do not pass `-clib=classic` there
  (z88dk answers `Cannot find definition for -clib=classic`). On `+zxn` it is
  the other way round: sdcc is the default, and `-clib=new` selects sccz80 with
  the new library.
- sdcc writes its C lines into the listings only with `--c-code-in-asm`. With
  `--list` but without it, JNEXT says so and uses the map alone:
  `z88dk listings for 'prog.map': main.c.lis: it has no line information (sdcc writes it with --c-code-in-asm)`.
- The `--hardware-keyboard` failure is a z88dk bug that is fixed upstream (PR
  #3198, merged 2026-10-23) and not yet in a release.
- A `.lis` file from a different build is detected and dropped with a warning;
  see [Limits and troubleshooting](10-limits-and-troubleshooting.md).

## Other targets

`-subtype` and `-create-app` choose the file you load. All of these were built
with `-clib=new -debug -m --list` and load with the map attached:

| For | Command (after `zcc`) | Writes | JNEXT reports |
|---|---|---|---|
| a NEX | `+zxn -subtype=nex -create-app -o prog` | `prog.nex` | identity verified |
| a 48K tape | `+zx -create-app -o prog` | `prog.tap` | identity pending, then verified (below) |
| a tape for the Next | `+zxn -subtype=tap -create-app -o prog` | `prog.tap` | identity pending, then verified |
| a snapshot | `+zx -subtype=sna -create-app -o prog` or `+zxn -subtype=sna -create-app -o prog` | `prog.sna` | identity verified |

Without `-create-app` the map and `prog_CODE.bin` are written, but no loadable
file. `-o prog.bin` instead of `-o prog` also works and names the map `prog.map`
as before.

A program loaded from tape is not in memory when the load call returns, so its
binary identity is checked later, the first time the CPU runs the code the map
describes. This is the log of the 48K tape build (z88dk 2.4 and master alike):

```
Loaded 337 z88dk symbols from '/home/you/prog/prog.map'
Loaded 281 z88dk source lines from '/home/you/prog/prog.map' and 4 listings (identity check pending: the program loads from tape)
Verified z88dk source map '/home/you/prog/prog.map': the program loaded from tape matches its binary identity
```

(338 symbols on the master build.) More about tapes is in
[loading a source map](../functions/14-source-level-debugging.md#loading-them).

## Banked code

A function that lives in a bank, built with z88dk's `__banked` on the Next, is
matched to its source only while its 8K page is mapped. The two steps
(`-DUSE_BANKED_KW` makes `banked.h` use `__banked`; the sources are
`bmain.c`, `banked.c`, `banked.h` and `bankin.asm` in `test/fixtures/z88dk/src/`):

```
zcc +zxn -clib=new -debug -DUSE_BANKED_KW --codesegPAGE_20 --list -c banked.c -o banked.o
zcc +zxn -clib=new -debug -DUSE_BANKED_KW -pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000 -m --list -subtype=nex -create-app -o prog bmain.c bankin.asm banked.o
```

Give both pragmas. `CRT_ORG_PAGE_20` carries the page in its top byte
(`0x14C000` is page 20 at `$C000`) and `REGISTER_SP=0xC000` keeps the stack below the
banking window. Without them the program itself calls page 0 or loses its stack,
so it cannot run. A newer z88dk (PR #3217, merged 2026-10-31) makes the page the
default and documents the stack; its description says the pragma gives identical
output, so it is harmless there. Only 2.4 and the 2026-10-16 master were run for
this page; on anything newer, build the example and look.

The build writes `prog_CODE.bin` and `prog_PAGE_20.bin`, and JNEXT checks both:

```
Loaded 193 z88dk source lines from '/home/you/prog/prog.map' and 4 listings (program identity verified)
```

The banking driver adds its own code to the start-up section, after the start-up
code's. That part of `prog.lis` has no label in the map, so JNEXT places it by its
bytes. It goes where `prog_CODE.bin` holds its instructions, and only when exactly
one place in the section does. How banked code, pages and addresses show is in
[banked code](../functions/14-source-level-debugging.md#banked-code).

## Things that change the result

- **With sccz80 and 80cc, `-debug` changes the program.** It adds
  `l_debug_push_frame` and `l_debug_pop_frame` around functions, so the build you
  debug is not the build you ship: debug the `-debug` build, and do not compare its
  addresses or timing with the release build. sdcc's `-debug` leaves the code
  unchanged (the same `prog_CODE.bin` with and without it).
- **Optimisation moves code across lines.** The default is `-O2 -SO2`
  (`zxn.cfg` and `zx.cfg`). For sdcc, the `-SO` level decides which line a piece
  of code sits under; `-SO0` stays closest to the source. For sccz80, `-O3` made
  no difference to the example (the binary was identical to the default).
- **The program stops at `__Start`, not `main`,** when loaded into a paused
  machine: press Continue or set a breakpoint on `main`.
- **Code your program copies at run time,** or places in a `PHASE` block, has no
  source line where it runs.
