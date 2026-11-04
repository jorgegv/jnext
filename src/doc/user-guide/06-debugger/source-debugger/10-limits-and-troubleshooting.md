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
| `z88dk listings for 'prog.map': prog.lis: 17 listed lines could not be placed (no label of their section piece is in the map and its bytes do not fit exactly one place in the section binary, or inside PHASE)` | part of a listing has no label in the map and JNEXT cannot prove where it goes: its instructions fit more than one place in its section, or no `prog_<GROUP>.bin` beside the map holds them; or the lines are in a `PHASE` block | nothing: those lines are left out rather than guessed, and the rest is attached. Keep `prog_CODE.bin` beside the map |
| `z88dk listings for 'prog.map': prog.lis: section 'code_crt_init': its label-less lines match no place in the section binary; this is not the listing of this build` | a stale `prog.lis`, from another build | rebuild with `--list`, or delete it; the map's own lines stay |
| `(identity check pending: the program loads from tape)`, then `Verified z88dk source map ...` | a tape program: the map attaches at once and is checked when the program first runs | nothing; wait for the `Verified` line |
| `Rejected z88dk source map ...: the program loaded from tape does not match its binary identity` | the tape holds a different build than the map | the map and its symbols are dropped. Load the tape the map was built with |
| `main.c:19 - source file not found` (Source tab) | the map names a file the Source tab cannot find | **Locate File...**, the source search path, or `--source-dir`: [where the Source tab finds the files](05-loading-a-source-map.md#where-the-source-tab-finds-the-files) |
| `main.c:19 - /path/main.c has only 12 lines; it is not the text the build used` | the file found is not the one the program was built from | put the right file where it is found first |
| `no code at main.c:3` | the line has no machine code (a comment, a declaration) | choose a line with code |
| `$0038 (ROM3): no source mapping; use the disassembly` (Source tab; the tag names the memory: `ROM3`, `DIVROM`, `MFROM`, `BOOT`...) | the address is supplied by a ROM or an overlay (DivMMC, Multiface, Layer 2, boot ROM), not a RAM page, and page-tagged records never describe it | use the disassembly there |
| `$8B5E (P04): no source mapping; use the disassembly` | the address is in RAM but no line of the map covers it (the compiler's runtime, a library, code copied at run time) | use the disassembly there |
| `--source-map requires --load FILE, or the map of a z88dk dot command: 'prog.map' is not one` | `--source-map` with no `--load`, and a map that is not a dot command's | give `--load`; see [Dot commands](03-dot-commands.md) |
| `--source-map 'prog.map': line 2089: section 'code_compiler': a dotn command's main bank and PAGE / DIV sections are pages NextZXOS allocates when it runs (__z_page_table); a dotn map cannot place them (build it as a dotx to debug it)` | a map of a `-subtype=dotn` command; the run exits with status 1 | build it as a dotx: [Dot commands](03-dot-commands.md) |
| *Source map not verified yet: it is checked when the dot command runs* (Source tab) | a dot command's map, attached before `.prog` has been typed | nothing; the log says `Verified z88dk source map ...` when it runs |
| *The dot command started in DivMMC RAM bank 2 is not the one source map ... describes; it stays pending* (log) | another dot command ran from the same bank | nothing: the map is not dropped. Type the command it describes |
| `Cannot break on 'main.c:19'` (z88dk-gdb) | a map built by master's sdcc (`-clib=sdcc_iy -debug -m --list`), with the 2.4 and the master clients (the 2026-10-16 master) | use an address or a symbol in the client, or build with sccz80 |
| `Segmentation fault` of z88dk-gdb (the 2026-10-16 master client, on the 2.4 and the master banked `-run` maps) | the map has a 24-bit `__C_LINE_` value | debug it in JNEXT's own Source tab. z88dk PR #3218 (merged 2026-10-31) is said to fix it; not run here |
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
  A `dot` or `dotx` command is supported: [Dot commands](03-dot-commands.md).
- **C variables beyond their names** (types, locals, structs) are read by scripts
  (`cvar()`), over GDB and over ZRCP, never over DeZog's protocol (DZRP): see
  [Scripts and remote debuggers](09-scripts-and-remote-debuggers.md#c-variables).
- **Some C variables cannot be written** (from a script, over GDB and ZRCP; each refusal says why):
  a variable the compiler keeps in a register (zsdcc), and anything reached through a pointer held in a register (the
  debug record gives no live range, so the register may hold something else by then); a whole struct or array (write a
  member or an element); sccz80's fastcall parameter (z88dk U1) and a member of an overlapping bitfield struct
  (z88dk U2), whose places are not certain; an 80cc block local (the compiler writes no block structure, so the slot
  may hold another variable); a variable not allocated yet or out of scope; a global of a dot command, which lives in
  DivMMC RAM, where the debugger cannot write; and anything in ROM.
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
