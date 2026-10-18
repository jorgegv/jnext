# Source-level debugging: design (CAP-SRC)

> Status: **partly implemented** (v1.1.64). The backend capability, the SLD and `Memory.txt`
> adapters, the Source tab and the ownership rules exist. The rest is designed here and built
> by the sub-issues of epic #23, named `E23-NN (#NNN)` wherever a section depends on one.
> The sections below describe the target. Section 3.3 lists, mechanism by mechanism, what the
> tree does today and which sub-issue changes it; a symbol the tree does not have yet is
> introduced by the sub-issue named beside it. `OD-n` are the owner decisions recorded on #23.

## 1. Use case and scope

Issue [#23](https://github.com/jorgegv/jnext/issues/23) (epic, "Source-level debugging with
z88dk"). The debugger works at the assembly level, so a program written in a higher-level
language (C with z88dk, Boriel ZX Basic with NextBuild) or in assembly is debugged by mapping
every PC back to a source line by hand, and in banked Next code the same address means
different code depending on which 8K page is mapped. The Cobra remake for the Next (Boriel,
many banks) is debugged this way: a crash in a line-interrupt routine meant reading compiler
output to find the BASIC line it came from.

z88dk's own debugger, `z88dk-gdb`, already debugs C on jnext's GDB server: against an sccz80
`-debug` build it does `break main.c:19`, `list`, `next`, `step`, `bt` with arguments and
`finish` with a return value (z88dk 2.4, verified 2026-10-11). The value of the epic is
therefore the in-emulator experience: the Source tab, statement steps, `file:line`
breakpoints, banked code, rewind.

**First usable cut ("cut 1").** SLD (sjasmplus, Boriel) and z88dk `-debug` maps (sccz80 on 2.4
and on master; sdcc and 80cc on master); a Source tab with a menu and keys, click-to-break
and Run to Line; Into / Over / Out / Back / Reverse Continue; `file:line` breakpoints with
several locations; banked code; correct behaviour across rewind, state loads and resets;
`monitor src` and DSL `file:line`. It works at the default `-SO2`, with documented
mis-attributions (OD-7). Cut 1 is E23-00 to E23-16; the close-out is E23-19 (#349).

**After cut 1, in the epic.** Per-line asm in z88dk projects and sdcc lines on 2.4, E23-17
(#348); `.cdb`/`.adb` input, E23-20 (#350, OD-2); dot commands, E23-21 (#351, OD-14); the
page-aware symbol table, E23-23 (#375); and a thin reverse-engineering layer (owner decision
on #378): user labels and comments, E23-24 (#387), memory coverage, E23-25 (#388), skoolkit
and ghidra interop, E23-26 (#389). E23-22 (#360) follows E23-08 (#340). The GUI items cut 1
leaves out are done after it too (OD-8).

**Out of scope.** C variables, types and scopes (follow-up epic #353, OD-9); a built-in Z80N
assembler; a native export of source that assembles again (#378).

## 2. Inputs

### 2.1 sjasmplus SLD v1

`--sld` writes one record per line, `file|line[:col[:endcol]]|defFile|defLine|page|value|type|data`,
after the header `|SLD.data.version|1` (sjasmplus 1.23.1, "Source Level Debugging (SLD) data").
`value` is any 32-bit integer; for a memory address it is the 16-bit Z80 address. The types:

- `Z`, the device model. It must have 8K pages and at most 224 of them (the Next's RAM).
- `T`, an instruction trace: the instruction at address `value` on memory page `page` was
  emitted from `file:line`. One record per emitted instruction; sjasmplus emits none for data.
- `L`, a label. `page >= 0` is the memory page of a 16-bit address; `page = -1` means "value
  is not memory address". An `EQU` without an explicit page is exported with -1, an address
  constant included; `=` and `DEFL` are not exported at all.
- `F` and `D` (deprecated, parsed as `L`) and `K` (a keyword comment).

Today only `Z` and `T` are read and the other types are skipped; E23-23 (#375) reads `L`
(section 10.4). The optional binary identity travels in comment records other readers ignore:
`||program.name:`, `||program.sha256:`, `||program.org:`, `||program.size:` (section 9.7).

### 2.2 NextBuild `Memory.txt`

`8000: ._Main` lines (hex address, label), written by the NextBuild / Boriel ZX Basic
toolchain beside a program. Symbols only, with no page (section 10.4).

### 2.3 z88dk ground truth

**The owner's z88dk rules** (epic #23, "UPDATE 15/10/26" and its 2026-10-18 paragraph). This
design, every row and every fixture that depends on compiler output follow them:

1. jnext's z88dk support must work with z88dk 2.4, the latest release. It may use a newer
   z88dk feature (for example the CDB records) where that suits jnext better, and the 2.4 path
   keeps working (OD-3: both).
2. It is tested on two installs, both built with SDCC: 2.4 and the latest master (on the
   maintainer's machine `~/src/spectrum/z88dk-original-2.4`, which `~/src/spectrum/z88dk` links
   to, and `~/src/spectrum/z88dk-original-latest`). Every row or fixture that depends on
   compiler output covers both installs and says which one, at which z88dk commit, produced
   it. Fixtures are generated by the real tools from our own sources and committed, because CI
   has neither install (E23-12 (#343)).
3. A clear z88dk bugfix may become a change proposed upstream to z88dk; a new or enhanced
   z88dk feature only after the owner has assessed it. A proposal (what, where in z88dk,
   evidence) goes to the owner, who decides and authorizes it.

**Provenance.** The facts below were measured while planning, on 2026-10-11, with our own
sources, on z88dk 2.4 (commit 4d530b6eb7, tag v2.4, 2025-10-01) and on a scratch build of
master at 4522364999 (2026-10-11, zsdcc 4.6.0). Where the checked-in fixture
`test/fixtures/zdebugmap/` (2.4, sccz80, trimmed maps) overlaps them, its values are quoted and
marked "fixture". The project's latest install is at 39421a01cd (2026-10-16) and contains every
master commit cited here (each is an ancestor of it). Between 4522364999 and 39421a01cd the
assembler, the driver, sccz80, ticks and 80cc changed only in the 80cc optimiser (with one zcc
option for it), ticks timings and the definition of the `--math48` alias (`git diff --stat
4522364999..39421a01cd -- src/z80asm src/zcc src/sccz80 src/ticks src/80cc lib/config`), so
the debug formats are expected to be the same. The zxn newlib did change its malloc selection
(65eb39ec1f, 882356991b, 2026-10-14), so library code, and the addresses after it, may move.
The addresses quoted here are therefore examples: E23-12 (#343) generates the fixtures on each
install, and every row takes its values from them.

**What each output carries.**

- `prog.map` (`-m`): linked, absolute 16-bit addresses and the section name (`PAGE_20`,
  `BANK_10`); no lines. jnext reads it today for symbols.
- `prog.map` (`-m -debug`): adds `__C_LINE_<n>_<enc>` (the start of C line n),
  `__ASM_LINE_<n>_<enc>` (labelled asm lines only) and `__CDBINFO__` records. sccz80 only on
  2.4; sdcc and 80cc on master.
- `<src>.lis` (`--list`): every assembled line with its bytes, at module- and
  section-relative addresses; sccz80 `C_LINE` directives (every occurrence); sdcc
  `;file:line:` comments, only with `--c-code-in-asm`; `SECTION` lines; no page.
- `prog_<GROUP>.bin` (`-create-app`): one file per section group, from its `__<group>_head`:
  the linked bytes, the source of the program identity (section 9.7).
- No `.cdb`/`.adb` file on either tree (zsdcc runs in `--c1mode` over stdin; master folds the
  records into the map, 29da3be373). No SLD and no DWARF on any branch: a search of the
  history since 2025-01-01 found no such commit, and the tree at 39421a01cd names neither.
- Naming traps: `+zxn` defaults to `-clib=sdcc_iy -O2 -SO2` (`lib/config/zxn.cfg`), so a bare
  `zcc +zxn` is sdcc; sccz80 needs `-clib=new` (or classic); `-clib=classic` failed on both
  trees while planning (`Unknown option2 --hardware-keyboard`).

**Findings, and what each demands of the design.** Values marked "fixture" are in the checked-in
fixture; the others are from the planning builds.

- G1. One start address per C line, and only its first occurrence per (line, scope) per
  module: z80asm's `C_LINE` handler (`src/z80asm/src/cpp/preproc.cpp`) defines the symbol only
  `if (!find_local_symbol(...))`. A line covers its start up to the next start, so ranges are
  required (D2).
- G2. Lines without code are in the map with value $0000 and an empty section (system
  headers, `util.h`, declarations; fixture: `main.c:0`, `util.h:13`). They are dropped, keyed
  on the empty section.
- G3. Several lines share one address (fixture: 16 and 17 at $8C95, 24 and 25 at $8D12,
  `util.c` 5 and 6 at $8D38), some across functions (fixture: line 14 is `square`'s closing
  brace and line 15 is `main`'s opening brace, both at $8C92). The function field tells a
  zero-width line of the previous function from the line that owns the address.
- G4. 2.4 only (fixed on master by 1621b2a128, 2026-01-14): sccz80 attributes part of a
  statement to the next line (fixture: line 19 at $8CB5, line 20 at $8CBE; on master line 20
  starts at $8CCD). A property of the toolchain: documented for 2.4 users, never repaired by an
  adapter.
- G5. sccz80 `-debug` changes the program: `l_debug_push_frame` and `l_debug_pop_frame` in
  every function (fixture: `_main` at $8C73 without `-debug`, $8C92 with it). The program
  debugged is the `-debug` build, and steps run through the helpers, which have no lines.
- G6. 2.4 only: sdcc line information exists only as `.lis` comments (`--c-code-in-asm`).
- G7. Optimisation moves code across lines, and the default `-SO2` is already optimised: the
  `while(1)` back-jump sits under `return 0; }` at `-SO2` and under line 26 at `-SO0`. sdcc
  emits an out-of-line copy of a `static inline` in each module that includes it, so `util.h:7`
  is two ranges in two modules (2.4, sdcc): one line, many addresses.
- G8. Banked code takes its page from the section name on newlib: `PAGE_20` is page 20, and
  `BANK_10` is the 8K pages 20 and 21. Addresses are logical and 16-bit, so without the
  section every bank's $C000 is the same address: D3's hazard (section 9.4).
- G9. `__banked` calls go through `banked_call`, which pops its return address, switches to a
  private stack, pages the target in and calls it through `l_jphl`. jnext's `CallStack` pops
  every frame whose `sp_at_call` is below SP on a RET, so the depth comes back right, by luck.
  E23-14 (#345) pins it with rows (section 7).
- G10. Paths are as the build spelled them: user files relative to the build directory (as
  given to `zcc`), headers as written in `#include`, system headers absolute; the module name
  is the mangled path.
- G11. The CDB records are `__CDBINFO__` symbols in the `-debug` map (sccz80; sdcc and 80cc on
  master): the input for C variables (#353).
- G12. 2.4 only (reworked on master, eb1a9b1299, 2026-04-18): sdcc `-debug` fails to assemble
  (`defc Gcounter0_00==.`; z88dk's own issue 3172).

**Master since 2.4** (all in 39421a01cd):

- 1621b2a128 (2026-01-14): sccz80 line numbers (G4 fixed).
- The sdcc `-debug` rework, merged as eb1a9b1299 (2bafa2baee, 29da3be373, 2026-04-16/18): the
  CDB records become `__CDBINFO__` defc in the asm, and `C_LINE` carries scope and block.
- 72ea9d221b (2026-04-20): in classic `+zxn`, the `BANK_n` ORG is `(2n)<<16 + CRT_ORG_BANK_n`,
  so the 24-bit value carries the 8K page. Newlib still uses 16-bit addresses. Classic `+zxn`
  could not be built while planning, so this row is from the source.
- 80cc, a new C compiler (147d689ad7, 21148942d7, 7ac2e80b4f): `-compiler=80cc -debug` writes
  the same map format as sccz80, with function names.
- z88dk-gdb: `monitor` passthrough, watchpoints, exit codes (0f5f252276, b11cb3f69b,
  c054874416): nothing is needed on jnext's side.

What the master maps showed (planning build):

- Three location forms: `file:line`; `file::func::level::block:line` (sccz80, 80cc);
  `file::x::<level*10000+sub>::<block><TAB>:line` (master sdcc, which writes the function as
  `x`; the tab is real).
- Master sdcc keeps every occurrence of a line that differs in level or block (line 18's two
  ranges), so G1's first-occurrence limit is sccz80's.
- Master sdcc reports header code compiled into a second module with the wrong line
  (`util_inc` in `util_c` as `util.h:9`; the listing agrees). The commit message of 2bafa2baee
  says "the filename in the sdcc information is incorrect at the moment - to be fixed". The
  adapter reports what the map says.
- sdcc 4.6 inlines what 4.5.0 called (`util_inc` folded into `util_twice`).
- Master's `z88dk-gdb` cannot yet use master's sdcc output (`Warning: unsupported address
  space`, then `Cannot break on 'main.c:19'`); the sccz80 path is the mature one.
- The page encoding depends on the z88dk version and library (newlib: the section name, on
  both trees; classic: a 24-bit value, the page after 72ea9d221b and the bank before it),
  hence the order of 9.4's rules.

**What `.lis` guarantees.** Every assembled line with its bytes and module-relative address
(relocation needs the map and a label in the same section piece); for sccz80, `C_LINE`
markers with every occurrence and exact extents; for sdcc, comments only with
`--c-code-in-asm`, and a peephole rule can move code past them (G7). No page: the `SECTION`
line names it. It guarantees neither a single current line under optimisation nor one address
per line.

The `-debug` map once broke jnext's symbol loader (the C-line markers named the function
labels). #352 fixed it in v1.1.45: `load_z88dk_map` skips `__C_LINE_`, `__ASM_LINE_` and
`__CDBINFO__`.

### 2.4 sjasmplus EQU files

`--sym=FILE` writes the whole symbol table and `--exp=FILE` only the `EXPORT` labels, one
`NAME: EQU 0x0000ABCD` line each (sjasmplus 1.23.1). Addresses are 16-bit and the files carry
symbols only. #390 loads them as unpaged labels (section 10.4); it lands on `main` on its own,
as #352 did (OD-10), and reaches the epic through merges of `main`.

<!-- CONTINUE -->
