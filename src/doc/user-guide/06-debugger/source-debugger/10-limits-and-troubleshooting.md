# Limits and troubleshooting

The messages that come with loading and using a source map, what causes each,
and what to do. Where one appears is in the row: a source map that is found and
refused is also in the log, in the main window's status bar for ten seconds, and
in the Source tab in place of "No source map loaded" (see the paragraph after the table); the
others are in the log, the Source tab, the Add dialog or the compiler's own
output.

## Messages

| Message | Cause | What to do |
|---|---|---|
| `program identity not verified: no section binaries beside it` | a z88dk map with no `prog_CODE.bin` beside it: a classic build (`+zx` with its default, classic, library, or `+zxn -clib=classic`), or the file was moved or deleted | the map is used anyway. For a verified map, build with `-clib=new` and keep `prog_CODE.bin` (and `prog_<SECTION>.bin`) beside `prog.map` |
| `z88dk listings for 'prog.map': main.c.lis: it has no line information (sdcc writes it with --c-code-in-asm)` | an sdcc build with `--list` but without `--c-code-in-asm` | add `--c-code-in-asm`; on z88dk 2.4 this is the only way to get sdcc's C lines |
| `z88dk listings for 'prog.map': main.c.lis: line 579: labels '_square' (line 558) and '_main' put section 'code_compiler' at different places; this is not the listing of this build` | a `.lis` file left from another build, copied or from before a change | rebuild with `--list`, or delete the stale `.lis`; the map's own lines stay |
| `z88dk listings for 'prog.map': prog.lis: 17 listed lines could not be placed (no label of their section piece is in the map, or inside PHASE)` | a newlib program with `__banked` functions (seen with both z88dk installs): the start-up section is no longer one piece, or code is in a `PHASE` block | harmless for the banked case: the start-up code shows no m4 text there. See [Building programs with z88dk](02-building-with-z88dk.md#banked-code) |
| `(identity check pending: the program loads from tape)`, then `Verified z88dk source map ...` | a tape program: the map attaches at once and is checked when the program first runs | nothing; wait for the `Verified` line |
| `Rejected z88dk source map ...: the program loaded from tape does not match its binary identity` | the tape holds a different build than the map | the map and its symbols are dropped. Load the tape the map was built with |
| `main.c:19 - source file not found` (Source tab) | the map names a file the Source tab cannot find | **Locate File...**, the source search path, or `--source-dir`: [where the Source tab finds the files](../functions/14-source-level-debugging.md#where-the-source-tab-finds-the-files) |
| `main.c:19 - /path/main.c has only 12 lines; it is not the text the build used` | the file found is not the one the program was built from | put the right file where it is found first |
| `no code at main.c:3` | the line has no machine code (a comment, a declaration) | choose a line with code |
| `ROM or overlay` (Source tab) | the address is supplied by a ROM or an overlay (DivMMC, Multiface, Layer 2, boot ROM), not a RAM page | use the disassembly there |
| `Unknown option2 --hardware-keyboard` (z88dk, at build) | `+zxn -clib=classic -debug` | use `-clib=new`, or `+zx` with the default classic library. Fixed upstream (PR #3198), not in a release |
| `defc Gcounter0_00==.` syntax error (z88dk 2.4, at build) | sdcc with `-debug` on 2.4 | use `--c-code-in-asm --list` instead of `-debug`, or master |

A source map that is found and refused says why. The reasons: the SLD or map was
built for another binary, the file is not a map, is not a regular file, or is
larger than 64 MiB. A source map you loaded by hand that stops matching (a reset,
or a state load, where it carries no identity or the memory has changed) is kept
but goes **stale**: the Source tab says so and why, the steps are off, and
**Load Source Map...** again loads it.

A banked program that runs page 0, or returns to `$FFFF`, was built without
the two pragmas: see [Building programs with z88dk](02-building-with-z88dk.md#banked-code).

## Limits

- **Toolchain mis-attributions.** A map is only as good as the compiler's line
  records: on z88dk 2.4 part of an sccz80 statement can show on the next line,
  on master sdcc code from a header can show under the wrong line, and an
  optimising build moves code between lines.
- **Code the program copies at run time** to another address has no record where
  it runs, unless your build made one, and shows as unmapped there. Code in a
  `PHASE` block is not placed.
- **Programs the guest loads itself** (one chosen in the NextZXOS browser, or
  `LOAD ""`) are not seen by JNEXT: load their source map by hand.
- **DivMMC sections** (`DIV_n`, and z88dk's `dotn` programs) are refused: a map
  with a line in one is rejected, with the line named, rather than attached half.
- **C variables beyond their names** (types, locals, structs) are not in the
  debugger yet.
- **A step is synchronous and bounded.** A step that never reaches another
  statement (a loop on one line, a `HALT` with interrupts disabled) stops by itself
  at a disabled-interrupt `HALT`, or after a million instructions or 100 frames,
  and says so in the log.
- **With sccz80 and 80cc, `-debug` changes the code**, so addresses and timing
  differ from your release build. sdcc's `-debug` leaves the code unchanged.
- **Headless and SDL builds attach the map but do not show it.** Only the Qt
  debugger has a Source tab; with `--headless` or the SDL frontend the map is read
  by scripts and by `monitor src` over the GDB server.
- **Back does not cross a state load**, or a reset: it stops at the load, since
  the history before it belongs to another program.
