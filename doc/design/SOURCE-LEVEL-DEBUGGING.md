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
mis-attributions (OD-7). Cut 1 is E23-00 to E23-16; the close-out is E23-19 (#349). The
z88dk support follows the owner's rules of section 2.3.

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
- `F` and `D` (deprecated, parsed as `L`) and `K` (a keyword comment). Version 1.23.1 still
  writes an `F` or `D` record beside the `L` record of the same label (observed 2026-10-18),
  so reading `L` alone sees each label once.

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
  `__ASM_LINE_<n>_<enc>` (labelled asm lines only; fixture: `asmfn.asm:5` at $8D65) and
  `__CDBINFO__` records. sccz80 only on 2.4; sdcc and 80cc on master.
- `<src>.lis` (`--list`): every assembled line with its bytes, at module- and
  section-relative addresses; sccz80 `C_LINE` directives (every occurrence); sdcc
  `;file:line:` comments, only with `--c-code-in-asm`; `SECTION` lines; no page.
- `prog_<GROUP>.bin` (`-create-app`): one file per section group, from its `__<group>_head`:
  the linked bytes, the source of the program identity (section 9.7).
- No `.cdb`/`.adb` file on either tree (zsdcc runs in `--c1mode` over stdin; master folds the
  records into the map, 29da3be373). No SLD and no DWARF in any z88dk branch: a search of the
  history since 2025-01-01 found no such commit, and the tree at 39421a01cd names neither.
- Naming traps: `+zxn` defaults to `-clib=sdcc_iy -O2 -SO2` (`lib/config/zxn.cfg`), so a bare
  `zcc +zxn` is sdcc; sccz80 needs `-clib=new` (or classic). `-clib=classic -debug` fails on
  both trees (`Unknown option2 --hardware-keyboard`: while planning, and again on
  2026-10-18), so a classic map has no C lines; without `-debug` a classic build can link
  (our fixture sources, both installs, 2026-10-18). E23-12 (#343) re-checks both installs.

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
  `util.c` 5 and 6 at $8D38), some across functions (fixture: line 14, `int main(void)`,
  carries `square`'s scope and line 15, `main`'s opening brace, carries `main`'s; both are at
  $8C92, because sccz80 closes a function's scope on the line where it reads the next
  declaration: the 2.4 listing, rebuilt on 2026-10-18, emits `C_LINE 14,"main.c::square::0::1"`
  after `square`'s `ret`). The function field tells a zero-width line of the previous
  function from the line that owns the address.
- G4. 2.4 only (fixed on master by 1621b2a128, 2026-01-14): sccz80 attributes part of a
  statement to the next line (fixture: line 19 at $8CB5, line 20 at $8CBE; on master line 20
  starts at $8CCD). A property of the toolchain: documented for 2.4 users, never repaired by an
  adapter.
- G5. sccz80 `-debug` changes the program: `l_debug_push_frame` and `l_debug_pop_frame` in
  every function (fixture: `_main` at $8C73 without `-debug`, $8C92 with it). The program
  debugged is the `-debug` build, and steps run through the helpers, which have no lines.
- G6. 2.4 only: sdcc line information exists only as `.lis` comments (`--c-code-in-asm`), so
  sdcc on 2.4 needs the listing adapter (E23-17).
- G7. Optimisation moves code across lines, and the default `-SO2` is already optimised (sdcc,
  2.4): the `while(1)` back-jump sits under `return 0; }` at `-SO2` and under line 26 at `-SO0`.
  sdcc emits an out-of-line copy of a `static inline` in each module that includes it, so
  `util.h:7` is two ranges in two modules: one line, many addresses (several starts for
  `resolve()`, and the primary and "also" locations of OD-7).
- G8. Banked code takes its page from the section name on newlib: `PAGE_20` is page 20, and
  `BANK_10` is the 8K pages 20 and 21. Addresses are logical and 16-bit, so without the
  section every bank's $C000 is the same address: D3's hazard (section 9.4).
- G9. `__banked` calls go through `banked_call`, which pops its return address, switches to a
  private stack, pages the target in and calls it through `l_jphl`. jnext's `CallStack` pops
  every frame whose `sp_at_call` is below SP on a RET, so the depth comes back right, by luck.
  E23-14 (#345) pins it with rows (section 7).
- G10. Paths are as the build spelled them: user files relative to the build directory (as
  given to `zcc`), headers as written in `#include`, system headers absolute; the module name
  is the mangled path. Files are therefore found relative to the map's directory (8.2) and
  `resolve()` matches whole path components (5.1).
- G11. The CDB records are `__CDBINFO__` symbols in the `-debug` map (sccz80; sdcc and 80cc on
  master): the input for C variables (#353).
- G12. 2.4 only (reworked on master, eb1a9b1299, 2026-04-18): sdcc `-debug` fails to assemble
  (`defc Gcounter0_00==.`; z88dk's own issue 3172), so again the sdcc lines of 2.4 come from
  the listings only.

**Master since 2.4** (all in 39421a01cd):

- 1621b2a128 (2026-01-14): sccz80 line numbers (G4 fixed).
- The sdcc `-debug` rework, merged as eb1a9b1299 (2bafa2baee, 29da3be373, 2026-04-16/18): the
  CDB records become `__CDBINFO__` defc in the asm, and `C_LINE` carries scope and block.
- 72ea9d221b (2026-04-20): in classic `+zxn`, the `BANK_n` ORG is `(2n)<<16 + CRT_ORG_BANK_n`,
  so the 24-bit value carries the 8K page; before it the prefix was the 16K bank number (2.4:
  `ORG 0x0a0000 + CRT_ORG_BANK_10`). Only a plain `BANK_n` has the prefix: `BANK_n_L`,
  `BANK_n_H`, `PAGE_n` and `DIV_n` have 16-bit ORGs on both trees, and newlib has them
  throughout. Measured on 2026-10-18 (classic build without `-debug`, one function in
  `CODE_10`): `_banked_add` is `$AC000` on 2.4 (ORG $C000) and `$14E000` on the latest
  install (ORG $E000, in page 21).
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
  both trees; classic: the section name, and in a plain `BANK_n` also a 24-bit value, the page
  after 72ea9d221b and the bank before it), hence the order of 9.4's rules.

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
symbols only. #390 loads them as unpaged labels (section 10.4); it is a standalone issue that
lands ahead of the epic, as #352 did (OD-10).

## 3. Architecture

### 3.1 Layers

Three layers, with dependencies pointing inward:

1. **Format adapters** know file formats and nothing else (section 9). Each parses a whole
   input and replaces its store only on success.
2. **Neutral stores** know neither a file format nor Qt nor the emulator: the backend's
   `SymbolTable` (CAP-SYM) and `SourceMap` (`src/debug/source_map.*`), and the stores the epic
   adds (user annotations, per-space coverage, later a `VariableStore`).
3. **Consumers** read the stores through the `jnext::dbg::Debugger` backend
   ([DEBUG-SUBSYSTEM-ARCHITECTURE.md](DEBUG-SUBSYSTEM-ARCHITECTURE.md)): the CAP-SRC verbs, the
   stepping engine, the Qt panels, the DSL, the GDB, DZRP and ZRCP servers, the exporters.

Each store is one backend instance, so every frontend (Qt, SDL, headless, the remote servers,
scripts) sees the same source map and symbols.

```
  files                          machine
    |                              |
    v                              v
  adapters                       Mmu::route() --> FetchSource {kind, index}
    |  ranges, labels,             - recorded by the real M1 into trace entries and call frames
    |  program identity            - predicted for the current PC by the same function
    v                              |
  stores <-------------------------+  every range, label and breakpoint is compared with it, exactly
    |
    v
  Debugger backend (CAP-SRC, CAP-SYM)
    |
    v
  Qt panels, stepping engine, DSL, GDB / DZRP / ZRCP servers, exporters
```

| Input | Adapter | Fills | State |
|---|---|---|---|
| `prog.sld` `T` records | SLD | `SourceMap` | done; ranges in E23-06 (#338) |
| `prog.sld` `L` records | SLD labels | `SymbolTable` | E23-23 (#375) |
| `Memory.txt` | NextBuild symbols | `SymbolTable` | done |
| `prog.map` (`-m`) | z88dk map symbols | `SymbolTable` | done (#352) |
| `prog.map` (`-m -debug`) | z88dk debug map | `SourceMap`, `SymbolTable` | E23-13 (#344) |
| `*.lis` and `prog.map` | z88dk listings | `SourceMap` | E23-17 (#348) |
| `prog_<GROUP>.bin` | program identity | `SourceMap` | E23-13 (#344) |
| `--sym` / `--exp` files | EQU symbols | `SymbolTable` | #390 |
| `.cdb` / `.adb` | CDB | `SourceMap` | E23-20 (#350), if the files exist |
| project file | annotations reader and writer | user annotations | E23-24 (#387) |
| `__CDBINFO__` records | CDB variables | `VariableStore` | follow-up epic #353 |

### 3.2 The four design requirements

D1 to D4 are epic #23's design requirements (its acceptance criteria 1 to 4). Each is stated
with how it is met.

**D1. History records what the fetch did; the current PC is predicted by the same function.**
One arbiter, `Mmu::route(addr) const`, returns the `FetchSource` and the physical offset. It is
the only place the priority order lives (section 4.2), and `read()` becomes
`byte_at(route(addr), addr)`. The CPU's opcode read goes through `Mmu::read_m1(pc)`, which
stores the result in `last_m1_`. A trace entry takes it after the instruction ran; a call
frame's caller identity is the CALL's recorded fetch and its target identity the target's first
M1. The Source tab and `source_location()` predict the next M1 with
`route_with(addr, divmmc_active, mf_active)`, fed by `DivMmc::active_on_m1()` and a const
`Multiface::would_latch_on_m1(pc)`; `route()` is `route_with()` with the live values, so the
prediction cannot drift from the fetch. Built by E23-07 (#339) (the arbiter, `read_m1`, the
prediction) and E23-08 (#340) (trace entries and call frames). Check (epic acceptance 1): no
prediction of the M1 overlay outside `route_with`, `active_on_m1` and `would_latch_on_m1`;
SRCDBG-ID-17 (an instruction that remaps its own slot keeps its recorded identity).

**D2. The source map is range-aware and many-to-many.** A record covers its start up to an
extent the adapter gives (section 9); ranges may overlap, and an address may have several
locations. `lookup(space, addr)` returns all of them with a primary (the shortest range; on a
tie the record emitted last, which for a z88dk map is the highest line, G3).
`resolve("file:line")` returns every start; a zero-width line resolves through its alias or
refuses with "no code at file:line". Built by E23-06 (#338), with the z88dk extents in E23-13
(#344) and E23-17 (#348). Check (epic acceptance 2): SRCRNG-01 to SRCRNG-09, Z88MAP-02,
Z88MAP-05.

**D3. Every record says which memory it describes, and the bytes are checked.** No adapter
emits an unqualified record, and the store has no "matches every bank" fallback (E23-06
(#338)). The z88dk adapter derives the space from the section name first, then from a 24-bit
value (section 9.4; E23-13 (#344)). A map with a program identity is compared with memory when
it is attached, and again after any machine replacement that is not a same-generation rewind
(section 6; E23-10 (#342)): an SLD by its SHA-256 records, a z88dk map by the linker's section
binaries over every line range. Check (epic acceptance 3): Z88MAP-08 to Z88MAP-11,
Z88MAP-14.

**D4. One canonical page identity; the legacy page qualifier stays for the protocols that use
it.** The identity is `FetchSource`, `MemSpace` extended with the overlays (OD-13). A source
record, a source-line breakpoint and a history entry each carry one, compared exactly, and it
replaces `NOT_RAM_PAGE` and `EventFilter::page_ram_only`. The plain `EventFilter::page`, which
DZRP long breakpoints (`bank+1`) and the DSL `page` keyword use, keeps comparing
`Mmu::get_effective_page(slot)` and nothing more: a protocol-level qualifier, not the identity
(section 4.5). Built by E23-07 (#339) (the type), E23-09 (#341) (breakpoints on it, with one
predicate) and this document, E23-02 (#334). Check (epic acceptance 4): SRCBP-02 (a page
number is not an identity), SRCBP-05 (the legacy meaning pinned).

### 3.3 Implementation status

| Mechanism | Today (v1.1.64) | Design | Changed by |
|---|---|---|---|
| Fetch identity at the PC | `Emulator::fetch_not_mmu_ram()` predicts the next M1 through `DivMmc::active_on_m1()`, a copy of the `Multiface` and `Mmu::read_not_mmu_ram()`; `Debugger::source_page()` returns a RAM page or `NOT_RAM_PAGE` (`debug/ram_page.h`) | 4.2, 4.4 | E23-07 (#339) |
| Identity in history | `TraceLog` keeps a flag per entry beside `TraceEntry` (`not_ram_`), predicted just before the instruction runs; Back reads `te.mmu[slot]`, `rom_slots` and the flag | 4.3 | E23-08 (#340) |
| Identity in call frames | `CallFrame::caller_page` and `target_page` (`uint8_t`; `NOT_RAM_PAGE` for ROM or an overlay) | 4.3 | E23-08 (#340) |
| Source map | exact (page, address) to one `SourceLocation`, a duplicate replaces; `lookup` returns a copy; `resolve()` returns the first start, by exact path or unique basename; an unqualified-record fallback that no adapter feeds (SLD records always name a page) | 5 | E23-06 (#338) |
| Breakpoint qualifier | `EventFilter::page` plus `page_ram_only` (`Event::fetch_not_ram`); the predicate sits in three places: `EventTable::filter_matches()`, `Debugger::probe_execute()`, `history_stop_at()` | 4.5, 8.1 | E23-09 (#341) |
| Lifetime | `sidecar_attach_cycle`; `machine_replaced()` drops the sidecar stores at once on a reset or a state from outside the rewind ring; a rewind rechecks by the clock | 6 | E23-10 (#342) |
| z88dk | the map gives symbols only (`load_z88dk_map` skips the three debug prefixes, #352) | 9.4, 9.5 | E23-13 (#344), E23-17 (#348) |
| Symbols | `SymbolTable` keyed by the 16-bit address; no page | 10 | E23-23 (#375); EQU files: #390 |
| User labels and comments | none | 10 | E23-24 (#387) |
| Coverage | INS-20: one bit per executed 16-bit PC (`CoverageBits`) | 11.1 | E23-25 (#388) |
| Source steps in menus and keys | Source tab buttons only | 8.2 | E23-05 (#337) |
| Source tab | the PC line highlighted, the step buttons, "Running" while the machine runs; no click-to-break, Run to Line, location list or selection sync | 8.2 | E23-16 (#347) |
| Remote and script surface | GDB `monitor sym`; no `monitor src`, no DSL `file:line` | 8.3 | E23-15 (#346) |
| The identity in every view; remote vocabularies | none | 4.6 | E23-22 (#360) |
| C stepping on real z88dk code | none | 7 | E23-14 (#345) |
| Dot commands | none | 9.4 | E23-21 (#351) |
| `.cdb` / `.adb` | none | 9.6 | E23-20 (#350) |
| Fixtures | `test/fixtures/zdebugmap/` (trimmed 2.4 sccz80 maps, #352) | 2.3, 13 | E23-12 (#343) |
| skoolkit / ghidra | none | 11.2 | E23-26 (#389) |

Done: E23-01, E23-03 and E23-04 (v1.1.64: the backend capability, the SLD and `Memory.txt`
adapters, the Source tab, ownership, the sidecar refusal reasons, the clock-based
lifetime); #332 (v1.1.46: the call-stack tracker follows rewind); #352 (v1.1.45). The epic closes
with E23-19 (#349). A sub-issue that lands updates its row here, not the prose.

## 4. Which memory supplied the instruction

A source record describes code built for one memory, so the question a lookup must answer is
"which memory supplied this instruction". The MMU's page number alone does not answer it: a
ROM-mapped slot reports a page number from the same range as the RAM pages, and the overlays
that the CPU's reads go through first (the boot ROM, the Multiface, DivMMC, the Layer 2 read
mapping, the alternate ROM and config-mode routing) replace the MMU's page without changing
it. One identity, used everywhere CAP-SRC matches code, answers it.

### 4.1 `FetchSource`

`FetchSource` is `MemSpace` (`src/debug/inspect.h`, INS-02) extended (OD-13) with the overlays
that `Mmu::read()` serves before the MMU: `BootRom`, `MultifaceRom`, `MultifaceRam`,
`DivMmcRom`, `DivMmcRam{bank}`, `AltRom{i}`, `Layer2{page}` and config-mode routing. It keeps
the kinds `MemSpace` has: `Page{n}`, which is RAM page n (0 to 223) and nothing else, and
`Rom{i}`. `Cpu`, the live view, is never a fetch source. Equality is exact (kind and index).
E23-07 (#339) names and encodes the kinds.

- Why not `NOT_RAM_PAGE`: it lumps ROM, DivMMC RAM and the Multiface into one value, and a
  z88dk dot command runs from DivMMC RAM, so its records would match any overlay.
- Why `Layer2{page}` is a kind of its own: the mapping is RAM, but not the slot's page, and
  for banks 5 and 7 not even the same memory. The MMU reads the dedicated BRAM for pages 0x0A,
  0x0B and 0x0E (`zxnext.vhd:2961-2962`, `3039-3041`); the Layer 2 arm forces the SRAM copy
  (`zxnext.vhd:3100-3107`).
- Every arm `Mmu::read()` has gets a kind. The VHDL's expansion-bus ROMCS arm
  (`zxnext.vhd:3108`) has none, because jnext models no expansion-bus device.

### 4.2 One arbiter

`Mmu::route(addr) const` (D1) is the only place the priority order lives. The VHDL's order for
0 to 16K is the boot ROM, the Multiface, DivMMC, the Layer 2 mapping, the MMU, config mode,
the expansion-bus ROMCS, then ROM, which the alternate ROM replaces (`zxnext.vhd:2933-2950`);
16K to 48K is the Layer 2 mapping and then the MMU, and 48K to 64K the MMU. The arms: boot ROM
1856, Multiface (in the pre-arbiter) 3030, config mode 3044, DivMMC ROM and RAM 3084 and 3092,
Layer 2 3100, alternate ROM 3116.

The VHDL arbiter takes the access direction as an input (`zxnext.vhd:3077`: the Layer 2
mapping uses its read enable on a read and its write enable on a write; `3078`: the
alternate-ROM decode). So any later write-side identity (E23-25) comes from the same function,
given the direction, never from a second copy of the order. If inlining misses the bench
threshold (4.7), the fallback goes to the owner, never to a second arbiter.

### 4.3 Recorded identity (E23-08 (#340))

A trace entry stores the `FetchSource` of its own M1 as a kind plus an 8-bit index (a 4-bit
index cannot name pages 16 to 223). The bytes come from narrowing `TraceEntry::opcode_len` to
`uint8_t` (OD-4 (a)), so `TraceEntry` stays 56 bytes, which INS-13-15 pins. The parallel
`not_ram_` vector goes, and with it its resize and ring-wrap hazards. `CallFrame` gains the
caller's and the target's identity (`caller_space`, `target_space`): the caller's is the CALL's
recorded fetch, the target's is filled at the target's first M1. Back and Reverse Continue
match history by the recorded identity, and the trace export prints it. ZRCP `cpu-history` and
DZRP history are not affected.

### 4.4 The current-PC prediction (E23-07 (#339))

The Source tab, `source_location()` and the breakpoint gate ask about an M1 that has not
happened yet, and the M1 itself switches overlays. DivMMC's instant-on entry points map DivMMC
for the very fetch that hits them. The hold carried from the previous M1 decides whether
DivMMC still answers: the fetch after a 0x1FF8-0x1FFF unmap is RAM again while DivMMC still
reads as mapped. The Multiface latches on the 0x0066 fetch.

So the answer is a prediction, but of the same function: `route_with(addr, divmmc_active,
mf_active)`, with the DivMMC decision from `DivMmc::active_on_m1()` (it shares its entry-point
decode with `check_automap()`) and the Multiface decision from the const
`Multiface::would_latch_on_m1(pc)`, which replaces the copy of the whole Multiface that the
tree makes today. `Debugger::source_page()` returns the predicted `FetchSource`;
`Emulator::fetch_not_mmu_ram()` and `Mmu::read_not_mmu_ram()` are deleted. A prediction has no
side effects: rows compare the whole machine state before and after.

### 4.5 Canonical and legacy

The canonical identity is the one that every source record, source-line breakpoint and history
entry carries (D4). The plain `EventFilter::page` stays, as a protocol-level qualifier and not
the identity, for the protocols that use it: DZRP long breakpoints (`bank+1`; a bank byte
other than 0 becomes `Execute{addr, addr, page = bank}`, `dzrp-frontend.md:402`) and the DSL
`page` keyword (`dsl-frontend.md:286`). It compares `Mmu::get_effective_page(slot)` and
nothing more, so it still fires on a ROM-mapped slot whose effective page matches (SRCBP-05).
`dzrp-frontend.md` stays frozen as a design record, and the developer guide says which meaning
is canonical (E23-09 (#341)).

A GUI breakpoint dialog that gets a page field uses `FetchSource` and E23-22's text form (4.6),
so a breakpoint set there and one set on a source line mean the same (#372). A space-qualified
breakpoint has no faithful `.jds` spelling until the DSL has a `FetchSource` form, so a saver
of GUI breakpoints as `.jds` (#376) refuses or flags one and never writes it as `page N`: that
would also match ROM or an overlay with that number, which is what SRCBP-02 pins.

### 4.6 Presenting the identity (E23-22 (#360))

One formatter over the identity serves every view (disassembly, memory, MMU, trace and
history, call stack, breakpoints, stack, watches): a friendly form and a switchable physical
form. For the MMU pages 0x0A, 0x0B and 0x0E it shows the BRAM that the CPU fetch used, and the
SRAM copy only where the Layer 2 mapping reaches it.

**The text form is a stable, serialisable name**, because the project file (10.5) and the
exports (11.2) key on it. There is one spelling per identity, and a parser that is the
formatter's inverse (the breakpoint dialog, the project file, imports). Spellings are
upper-case `[A-Z0-9]` only, with no whitespace and no `:` (a ghidra address-space name is
split at the last `:`; skoolkit file names). E23-22 fixes them. The list on #360 (`P0E`,
`ROM0` to `ROM3`, `ALTROM0`, `ALTROM1`, `DIVROM`, `DIVRAM3`, `MFROM`, `MFRAM`, `BOOT`, `L2P12`,
`CFG` plus the page) is provisional until E23-07 and E23-08 have landed.

**Remote protocols** (owner decision on #360, 2026-10-18). Each carries as much of the identity
as its existing vocabulary allows, and none is extended: no new commands, codes or fields on a
protocol jnext does not own. E23-22 works out the exact values.

- DZRP: the slot banks that `CMD_GET_REGISTERS` already sends, from which DeZog builds its long
  addresses, each filled with the most precise value DZRP defines (a RAM page 0 to 223, or 0xFF
  for ROM). DZRP has no value for which ROM, DivMMC or Layer 2 mapping backs a slot.
- ZRCP: ZEsarUX's full spelling. In `get-memory-pages`: `O<n>` (which ROM page), `A<page>`,
  `RO` and `SR<n>` in config mode, and `DIV` for a paged-in DivMMC (jnext prints only `RO` and
  `A<page>` today). In the `MMU=` field of `get-registers`: a value of 0x8000 or more is a ROM
  half, as DeZog reads it. Nothing finer (a DivMMC RAM page, a Layer 2 mapping) has a ZRCP
  spelling.
- GDB: `monitor` commands are the stub's own, so the full identity text goes into the existing
  `monitor mmu` output. No new command.

### 4.7 Cost

Nothing new runs with the debugger off except `read()`'s restructuring onto `route()`, which
E23-07 (#339) gates on `make bench` (non-PGO A/B on a quiet host; at most 1% on the boot and
game benches). With the trace on, the identity is stored per M1; with the call stack on, per
frame; an armed `Execute` subscription is evaluated only for its address (the existing per-page
bitmap). E23-07 and E23-08 record the numbers.

## 5. The source map and the backend API

### 5.1 The store (D2)

- `files[]`: the paths as the build spelled them, interned, with a cache of resolved paths.
- `locations[]`: `{file, line, column (0 = none), function (optional)}`. A format without
  columns writes 0; sccz80 and 80cc give the function, sdcc writes `x`.
- `ranges[]`: `{space, lo, hi_excl, location}`, sorted by (space, lo); overlaps are allowed.
- `by_line`: (file, line) to range indices (several, disjoint). `aliases`: (file, line) to a
  range, for the zero-width lines of G3.
- An optional program identity (9.7).

`lookup(space, addr)` returns every range that contains `addr` in `space`. The primary
location is the shortest range; on a tie the record the adapter emitted last wins. The z88dk
adapter emits equal starts in ascending line order, so the highest line owns the address
(DeZog's rule, G3). It returns pointers into the store, not copies. `resolve("file:line")`
returns every range start of the location. The file matches the exact path, else a unique
suffix of whole path components (a basename is the one-component case, today's rule; `ain.c`
never matches `main.c`). A zero-width line resolves through its alias or refuses with "no code
at file:line" (G3; `main.c:14` in the fixture, the line of `int main(void)`). Two addresses are
at the same position when the primary locations of the ranges under them have equal file, line
and column.

E23-06 (#338) builds the store on a placeholder 16-bit page key and fixes its interface;
E23-07 (#339) only swaps the key for `FetchSource`.

### 5.2 The backend verbs (CAP-SRC, `debug/debugger.h`)

| Verb | Meaning | Changes in |
|---|---|---|
| `load_source_map(path, accept_identity_mismatch)` | load a source map by hand; refused on a parse error, or on an identity mismatch unless accepted; the path is stored absolute | accepts `.map` (E23-13 (#344)) |
| `clear_source_map()`, `source_map()` | the store | |
| `source_location(addr)`, `source_location()` | the position of `addr` or of the PC | the primary location and all the others, by pointer (E23-06 (#338)) |
| `source_page(addr)`, `effective_page(addr)` | the RAM page that supplies `addr` (or `NOT_RAM_PAGE`); the MMU's page | `source_page` returns the `FetchSource` (E23-07); `effective_page` stays, as the MMU's page for the legacy qualifier |
| `load_program_sidecars(path)` | attach a loaded program's `Memory.txt` and SLD (section 6) | `<stem>.map` (E23-13); lifetime (E23-10 (#342)) |
| `source_step(by, kind)` | `Into`, `Over`, `Out`, `Back`, `ReverseContinue` (section 7) | ranges (E23-06); recorded identity (E23-08 (#340)) |
| `load_map(path, MapFormat::NextBuild)` | `Memory.txt` by hand (CAP-SYM gains a format) | |
| `--source-map FILE` (command line) | a source map at startup | new (OD-15, E23-13) |

`debugger.h` is a frozen public header: the sub-issue that needs an addition makes it, under
the owner decisions recorded on #23 and #378.

## 6. Sidecars, ownership and lifetime

### 6.1 Search

After a successful program load every frontend calls `load_program_sidecars(path)` (Qt and SDL
from their load tick, headless from `--load`, and `Debugger::load()` for a remote client). It
looks for `<stem>.Memory.txt`, else `Memory.txt`, beside a `.nex`; for `<stem>.sld`, else
`<stem>.sld.txt`; and, with E23-13 (#344) (OD-5), for `<stem>.map` beside a `.nex`, `.tap` or
`.sna`.

Only a regular file under the 64 MiB cap is a sidecar (`source_file_problem()`); a FIFO would
block the load. Today `load_sld()` and `load_nextbuild_memory()` call it, and E23-13 applies it
to the map loaders before `<stem>.map` is attached automatically. A file that cannot be read is
refused with a reason; a dangling or looping link is "no sidecar" (the search uses the
non-throwing filesystem calls). Each outcome is logged, and a refused one is reported with its
reason (`SidecarLoad::problem`): in the Qt status bar and the Source tab, at warn level in
headless and SDL.

### 6.2 Tapes

`.tap`, `.tzx` and `.wav` get no automatic source map. The machine loads them over the frames
after the load call returns, and neither the tape loader nor the backend has a point at which
"the program has loaded" (a program may also run, and change its own image, before any check
could match). This holds for every source format, the source half of `<stem>.map` included; its
symbol half attaches. A tape's map is loaded by hand once the program is in memory. **Open,
settled in E23-23 (#375):** once z88dk symbols are paged by the rules of 9.4, rule 4 (the MMU
mapping with the program in memory) has no load point for a tape program either, so E23-23
states when the symbols of a tape's `<stem>.map` take their spaces.

### 6.3 Ownership

Each store is the user's (a map loaded by hand, `--map`, and `--source-map` by the same rule),
a program sidecar's, or nobody's, and it changes only when a load succeeds. A sidecar never
replaces or clears the user's store, even one that looks empty (a map of `; const` lines
only). A previous program's sidecar data never survives into the next program: it is replaced,
or cleared when the new program has no sidecar or its sidecar cannot be read. The user
annotation store (10.2) is outside this rule: only its own verbs change it.

### 6.4 Identity on attach

A sidecar whose identity does not match the program in memory is refused and logged. A map
loaded by hand that does not match is offered anyway in the Qt GUI, because a running program
changes variables inside its own image. A map with no identity attaches unverified, and the
log says so (OD-6).

### 6.5 The lifetime model (E23-10 (#342))

The clock-based model of today (`sidecar_attach_cycle`) goes. Two tiers replace it.

1. **Load generation.** Every program load, state load (`.jns`, `.sna`, `.z80`, `.szx`, a DZRP
   or ZRCP restore), RZX playback start and reset increments a 64-bit generation. It is part
   of the per-slot state the rewind ring keeps in memory, beside the call frames that #332
   added, so a rewind restores it. An attached map records its generation. A rewind within
   the generation keeps the map; a rewind into an earlier generation re-attaches that
   generation's map from a cache of the last 4 attachments.
2. **Content re-verification.** On any replacement that is not a same-generation rewind, and
   at the next pause after one, a map with an identity is verified against memory. Pass: the
   map is kept and re-keyed to the new generation. Fail: a sidecar map is detached; a user map
   is kept, marked **stale** and matching nothing, and the Source tab says why, so the user can
   reload it or accept it. A map with no identity is detached on a generation change (sidecar)
   or marked stale (user).

- **Reset** is a new generation. The map survives only by passing re-verification at the next
  pause, which catches the ROM clearing RAM a few frames later (the code is intact at the
  reset itself).
- **RZX.** Rewind is refused during record and playback (today). Playback start is tier 2.
  Forward source steps during playback are ordinary steps.
- **`.jns`.** The format is unchanged: no attached-program member (OD-16). A restore is tier 2.
- **Back** after a foreign state load refuses: the trace filter adds the generation to the
  `te.cycle < now` test.
- **Every reader** goes through the one recheck: `symbols()`, `lookup()`, `lookup_name()`,
  `source_map()`, `pump()`, the owner reset of `clear_symbols()`, and the reconstruct hook.

### 6.6 What the backend cannot see

A program the guest loads for itself (the NextZXOS browser, `LOAD ""`) is not a load to the
backend: its predecessor's maps stay until a reset or a load. E23-10 decides whether the
re-verification at the next pause covers it (a residual handed to it). A sibling NEX run under
`--esxdos-stub` (`.RUN`) is a load like any other: the loop owner cold-boots with it, which
drops the old sidecars, and attaches the new program's.

### 6.7 Remote load and Reload

`Debugger::load()` (the DZRP and ZRCP servers; File > Reload program, #374, is planned as a
plain `load()` of the last path) attaches sidecars as the CLI does. Today it discards the
`SidecarLoad`, so a refused map shows only in the log; E23-10 reports the reason (a residual
handed to it).

**Open, settled in E23-10:** a map loaded by hand, across a Reload of a rebuilt program. The
model above marks it stale unless its identity verifies; the rule recorded on #342 from #374
keeps it the user's, as today. E23-10 decides, pins the decision with a row and updates this
paragraph. A Reload of a rebuilt program at the same path attaches the new build's map.

### 6.8 The user annotation store

It has no lifetime: it is kept across loads, resets, state loads and rewinds, it is not part of
`.jns`, and only its own verbs and loading a project file change it (10.2).

## 7. Source steps

**Forward** steps (`Into`, `Over`, `Out`) are a run of the debugger's Step
(`Emulator::debugger_step()`, which turns frames over and runs a HALT out), synchronous, with
the same pause ownership and stop evidence as `step_into()`. The position is the primary
location of the range under the PC (section 5); an unmapped instruction (library, crt, a
trampoline) is run through.

- `Into` stops when the position (file, line, column) differs from the start's.
- `Over` additionally requires call depth <= the start's.
- `Out` stops at the first mapped position at a depth below the start's; with no tracked
  frame it is one instruction.
- Starting on an unmapped instruction, any step is one instruction.

Between instructions, each next PC gets the delivery `run_frame()` gives it: the legacy PC
breakpoint, then the backend's own `Execute` gate. Handlers run (a script's `log` rule fires),
and a `Stop` stops the step with its hit and `once` recorded, exactly as a run would. A handler
may move the PC (section 4.2a of
[DEBUG-SUBSYSTEM-ARCHITECTURE.md](DEBUG-SUBSYSTEM-ARCHITECTURE.md)): the step then re-evaluates
its boundary at the new PC, without a second delivery there, as in a run, where the redirect
target simply runs next. Any stop latched during an instruction (a watchpoint, the magic
breakpoint) ends the step too. The step also ends, and logs why, at a HALT with interrupts
disabled (only an NMI or a reset can end it), after `SOURCE_STEP_LIMIT` (one million)
instructions, or after `SOURCE_STEP_FRAME_LIMIT` (100) frames of machine time, so a statement
that waits never holds the frontend. Steps stay synchronous and bounded (OD-11); E23-16
(#347) measures the worst case on the Release build.

**Call depth** is the call-stack tracker's (`set_call_stack_enabled`), which the Qt debugger
runs while its window is open and which follows rewind (#332, v1.1.46). E23-14 (#345) pins it
on real z88dk code across `banked_call` (G9), the sccz80 `l_debug_push_frame` and
`l_debug_pop_frame` helpers (G5), RST $08 esxDOS calls and a tail `JP`. A tracker defect found
there is fixed in `CallStack` for every program, never as a special case for `banked_call`.

**Backward** steps search the trace from newest to oldest, considering only entries before the
machine's current cycle: a Frame Back or a slider jump leaves the abandoned history in the
trace, and that is not "back". `Back` takes the newest mapped position different from the
current one (a statement repeated over consecutive instructions counts once). `ReverseContinue`
takes the newest mapped instruction covered by a live, unconditional, handler-less,
non-transient `Execute` Stop (or a legacy PC breakpoint). Both match entries by the recorded
identity (E23-08 (#340)) and use the one breakpoint predicate (E23-09 (#341)). The count goes
to `step_back()`, so rewind's refusals (RZX, an empty ring, a replay that would cross a
debugger mutation) and its corruption handling apply as they do for Step Back. A trace that is
off, or no target, is `RefusedUnavailable` with a `REWIND REFUSED:` log line, which the Qt
status bar shows.

## 8. Breakpoints, panels and the remote surface

### 8.1 Source-line breakpoints (E23-09 (#341))

`resolve("file:line")` gives N starts. The backend adds one `Execute` subscription per start,
each qualified with its range's `FetchSource`, grouped under one source breakpoint. The
Breakpoints panel shows one row, "main.c:18 (2 locations)", and remove, enable and edit act on
the group. One predicate, `EventFilter::matches(ev)`, replaces the three copies of today (3.3):
the gate (`execute_gate` to `filter_matches`), `probe_execute()` and `history_stop_at()` all
call it. The disassembly gutter draws, toggles and patches from the same identity:
`BreakpointModel::pc_marked(addr, space)` takes the `FetchSource` where it takes a page number
today. Clicking a Source tab line toggles its group (E23-16).

### 8.2 Qt

- **Source tab** (`src/debugger/source_panel.*`): the PC's file with the PC's line highlighted.
  It follows the PC at every stop and says "Running" while the machine runs (done in v1.1.64).
  The file is found from the recorded name, absolute or else relative to the map's directory
  or one of its parents, and reloaded when it changes on disk. A refused sidecar says why in
  the tab.
- **Menu and keys** (E23-05 (#337)): a Debug > Source submenu (Step Into, Over, Out, Back,
  Reverse Continue, Load Source Map..., Load Symbols (Memory.txt)...), each action bindable in
  Debug > Change Keys, with Shift+F7, Shift+F8 and Shift+F6 as the defaults for Into, Over and
  Out (OD-17). The owner approved changing the pinned menu tree and key bindings
  (`debugger_accel_test` DACC-05, `debugger_keymap_test`).
- **Source tab completion** (E23-16 (#347)): click a line number to toggle its source
  breakpoint group; Run to Line (a transient group, then Continue); the current-line marker; the
  "also:" list of the other locations at the PC (OD-7); selection sync between the Source tab
  and the disassembly.
- **Address fields**: a number first, `@` forces a symbol, `$` or `0x` force hex (OD-12; done).
  `file:line` is accepted wherever an Execute address is (done). Call Stack "Called from" uses
  the caller's recorded identity (E23-08). E23-22 (#360) shows the identity in every view, and
  E23-24 (#387) edits labels and comments in the disassembly.

### 8.3 Remote protocols and scripts

The rule of 4.6 governs every protocol: each carries as much of the identity as its vocabulary
allows, and none is extended.

- **GDB.** `z88dk-gdb` maps sources itself, so nothing is required. E23-15 (#346) adds
  `monitor src [ADDR]`: the location and the space of an address, else "unmapped", which makes
  the source map observable in headless and SDL. `monitor sym` exists. E23-22 puts the identity
  in its text form into `monitor src` and `monitor mmu`.
- **DZRP and ZRCP.** DeZog maps sources from `.lis`, `.map` and `.sld` itself, so there are no
  new commands. Banked breakpoints keep the legacy qualifier (4.5); the slot views are as in 4.6.
- **DSL.** `addr_spec` accepts `file:line` (E23-15), resolved at load through the backend like
  `@name`. An unresolvable one is a load error, and a multi-location line arms every start.
  Saving GUI source-line breakpoints as `.jds` (#376) needs it.

### 8.4 Headless and SDL

Sidecars attach and log in both (done, v1.1.64). `monitor src` gives them a reader (E23-15),
and `source-map-func` asserts a looked-up line instead of a log line.

## 9. Adapter contract

### 9.1 The contract

An adapter turns one file format into the neutral stores and nothing else. It emits:

- source ranges `{space, lo, hi_excl, location}`, **every one with a space**: no unqualified
  record, and no "matches every bank" fallback in the store (D3);
- optionally a program identity;
- symbols (section 10).

It **parses the whole input and fails with a reason before replacing a store.** For a source
half that is strict: any malformed record refuses the half, and the reason names the line. The
symbol-only loaders keep the MAP rule: a file that cannot be opened, is not a regular file or
is over the size cap is refused with a reason, and inside a readable file a line that does not
parse as a symbol is skipped, not fatal (real z88dk maps carry lines of every kind: `; const`
values, `-debug` records, values wider than 16 bits). A file that fills two stores (a z88dk
map) is two independent transactions, each reported: a map whose line records are refused still
gives its symbols, as v1.1.45 does today.

**Extents** (D2). Each adapter gives every record its extent:

- SLD: to the next record in the same space, capped at the 8K page end.
- z88dk map: to the next line start in the same space, or the next `addr` symbol of a different
  module, or start + 0x400, or the 8K page end, whichever is first. This is DeZog's rule
  (`z88dklabelparserv2.ts`, `addCLineDebugInfo`, `MAX_C_LINE_SIZE = 0x400`): library code has
  no lines and must not inherit a module's last C line.
- `.lis`: exact, from the bytes column.

The stepping engine, the panels and history read ranges, spaces and the primary and "also"
locations. A new format is therefore supported only once this section states its extent and
space rules.

### 9.2 SLD v1

The `T` records become ranges (E23-06 (#338)). The SLD page becomes `Page{n}`, since SLD's
device model is RAM pages; a record on a ROM device is out of scope. The adapter's FIFO, size
cap and short-SHA-256 fixes are done (E23-03, v1.1.64).

### 9.3 `Memory.txt`

Symbols only, with no page (10.4).

### 9.4 z88dk `-debug` map (E23-13 (#344))

Input: `prog.map`. One read fills both stores: symbols through `load_z88dk_map`, and ranges from
`__C_LINE_` and `__ASM_LINE_`. The parser accepts the three location forms of 2.3 (the same
that DeZog's `z88dkmapfile.ts` accepts). Lines in an empty section are dropped (G2).

**Space derivation**, in this order. The section name comes first, because the meaning of a
24-bit value changed between z88dk versions. The section families are those of z88dk's memory
maps. Newlib's `crt_memory_map.inc` (`libsrc/_DEVELOPMENT/target/zxn/` in 2.4,
`libsrc/newlib/target/zxn/` at 39421a01cd) defines `PAGE_<n>` (n = 0 to 223), `BANK_<n>`,
`BANK_<n>_L` and `BANK_<n>_H` (n = 0 to 111), and `DIV_<n>` (n = 0 to 15). Classic's
`lib/target/zxn/classic/memory_map.asm` (the same path in both trees) defines those names and,
for each `<KIND>` (`CODE`, `RODATA`, `DATA` or `BSS`), `<KIND>_PAGE_<n>`, `<KIND>_<n>`,
`<KIND>_<n>_L`, `<KIND>_<n>_H` and `<KIND>_DIV_<n>`, with `RODATA_<n>_align_<m>` and
`DATA_<n>_align_<m>` inside a plain bank. The rules read the word (`PAGE`, `DIV`, `BANK` or a
`<KIND>`), the number `<n>` and the `_L` or `_H` suffix of the name.

1. Section `PAGE_<n>` or `<KIND>_PAGE_<n>`: `Page{n}`.
2. A section of the 16K bank n, which is the 8K pages 2n and 2n + 1. `BANK_<n>`, `<KIND>_<n>`
   and the aligned sections: `Page{2n + ((addr16 >> 13) & 1)}`. `BANK_<n>_L` and
   `<KIND>_<n>_L`: `Page{2n}`. `BANK_<n>_H` and `<KIND>_<n>_H`: `Page{2n + 1}`. `addr16` is
   the low 16 bits of the value. The map's `CRT_ORG_BANK_<n>[_L|_H]` is not used, because
   z88dk's `appmake` (`src/appmake/zxn.c`, identical on 2.4 and at 39421a01cd) places a
   section in the 16K bank at `(org & 0x1fff) + 0xc000` for `_L`, at `(org & 0x1fff) + 0xe000`
   for `_H` and at `(org & 0x3fff) + 0xc000` for the rest: a byte of a plain bank is at bank
   offset `addr16 & 0x3fff`, whatever the ORG. A page computed from the distance to the ORG
   is wrong when the ORG is not 16K-aligned (`BANK_10` at $E000 starts in page 21, not 20).
   Checked on 2026-10-18 by scratch builds on 2.4 and on the latest install (newlib, sccz80,
   one function): in `BANK_10` at ORG $C000 or $D000 it is in page 20, at $E000 or $A000 in
   page 21; in `BANK_10_L` (ORG $C000) in page 20; in `BANK_10_H` (ORG $E000 or $C000) in
   page 21.
3. A value above 0xFFFF in a section whose name has no number (none of z88dk's standard memory
   maps has one; a user memory map can): bits 16 to 23 are the 8K page, and the upper 8K of an
   even page adds 1 (DeZog's rule, right for classic after 72ea9d221b). This rule exists only
   if E23-12 (#343) can produce a fixture of such a map; otherwise the value is refused with a
   reason, and the rule waits for a fixture. In a numbered section the bits are not read: they
   are the 16K bank number before 72ea9d221b and the 8K page after it, so they cannot check
   the name without knowing the version.
4. Any other section (`CODE`, `code_*`, the main program): the RAM page the MMU maps at that
   slot with the program in memory. For a NEX that is banks 5, 2 and 0, the pages 10, 11, 4, 5,
   0 and 1 for $4000-$FFFF, so $8000-$9FFF is `Page{4}`. A map read before the program is in
   memory (`--map`, `--source-map`) takes its rule-4 spaces at the program load, not when it is
   read: `--map` is read when the script host starts, before the `--load` countdown applies
   the program.
5. A DivMMC dot command (org $2000), and the `DIV_<n>` and `<KIND>_DIV_<n>` sections of the
   memory maps: `DivMmcRam{bank}` (E23-21 (#351), OD-14). E23-21 establishes how a dot
   command's sections map to a bank.

**Zero-width lines** (G3). A line whose address equals the next line's start is an alias of
that line if both are in the same function: sccz80 and 80cc give the function; for sdcc, which
writes `x`, the test is the same module and no function-entry `addr` symbol of another
function at that address. Otherwise the line has no code (`main.c:14` in the fixture, the line
of `int main(void)`, which carries `square`'s scope). Toolchain mis-attributions are reported
as the map says (G4 on 2.4, master sdcc's `util.h:9`).

**Loading.** `<stem>.map` is a sidecar per 6.1 (OD-5). A user `--map` keeps the user's symbols
and still attaches the source half. By hand, Load Source Map... accepts `.map`, and the command
line takes `--source-map FILE` (OD-15). The user guide gets "Debugging z88dk C programs" (build
flags per compiler and z88dk version, and the G4, G5 and G7 limits) from E23-13 and E23-19.

### 9.5 z88dk `.lis` and map (E23-17 (#348))

Relocation is per (module, section piece): offset = map(label) - lis(label), re-anchored at
each label (DeZog's `z88dkMapOffset`). Then every instruction is a range of its byte length.
The line source is, for sccz80, `C_LINE`; for sdcc, the `;file:line:` comments, every
occurrence, so line 18 gets both of its ranges; for an asm file, its own line numbers. The
space comes from the `SECTION` line by 9.4's rules. There is one `.lis` per source (z88dk
writes `main.c.lis`, `util.c.lis`), found beside the map or by a glob. With a `-debug` map
present, its C lines are cross-checked against the listing and the listing wins, because it is
exact and has every occurrence: the reverse of DeZog, which takes its C lines from the map.
The listing is the only per-line asm source and the only sdcc line source on 2.4 (G6, G12).

### 9.6 `.cdb` / `.adb` (E23-20 (#350), OD-2)

Neither tree produced a standalone file in the planning builds. E23-20 establishes whether
sdcc's own `--debug` path can produce them through zcc on either install, and with which flags.
If it can, the adapter follows this contract (spaces by 9.4); if not, E23-20 closes with the
evidence (commands, z88dk commits, outputs).

### 9.7 Program identity

- SLD: the optional SHA-256 records (64 hex digits; a short one is refused; done).
- z88dk: the section binaries `prog_<GROUP>.bin`, compared with memory over every line range
  through `MemSpace` peeks of the range's own space. A bank that is not mapped is still
  checked, and data sections are not compared (a program changes its variables). No binaries
  means unverified: the map attaches and the log says so (OD-6).
- The `.lis` bytes are a secondary check only: references to other modules are unrelocated in
  a listing (`cd0000`), so they verify only the bytes the listing marks as final.

### 9.8 Symbol adapters and pages

See 10.4.

## 10. Symbols and user annotations

The page-aware symbol table (E23-23 (#375)) and the user-owned label and comment store
(E23-24 (#387)) are designed together, on the owner model the stores already have (none,
sidecar, user), so that neither needs a retrofit.

### 10.1 The key

A label is keyed by a `FetchSource` and a 16-bit logical address, the same key the source
ranges use. There are three kinds of entry:

- a paged label: (space, address) to a name;
- an unpaged label: address to a name, for a format that names no page. It matches that address
  in any space, as every label does today;
- a name-only value: name to value, which never names an address (z88dk `; const`, #279; an SLD
  `L` value above 0xFFFF).

A page mapped into a slot other than the one its labels were made for shows no labels there,
as with source records (section 12).

### 10.2 Two layers, on the existing owner model

The **toolchain layer** is the `SymbolTable` as today: one owner for the whole store
(`Impl::StoreOwner`: None, Sidecar, User), filled only by a complete load (a sidecar, `--map`,
Load MAP), cleared by `clear_symbols()`, under the lifetime of section 6. The **user layer**
(E23-24) is a separate backend store of user labels and comments. It is always user-owned;
only its own verbs (add, rename and remove a label; set or clear a comment) and loading a
project file change it, and loading replaces it whole and transactionally; a sidecar load, a
hand load of a MAP, `clear_symbols()`, a reset, a state load and a rewind never touch it. Both
layers live in `Debugger::Impl` beside the `SourceMap`, and frontends reach them only through
`Debugger`.

### 10.3 One reader

By address, `(space, addr)`: the user label, else the toolchain paged label, else the
toolchain unpaged label. By name: the user layer, then the toolchain layer, giving the entry's
key. Every consumer goes through it (disassembly lines and operands, Call Stack, Breakpoints,
trace export, the address fields' `resolve()` with OD-12, the DSL's `@name`, GDB `monitor sym`,
ZRCP names), and no panel merges the layers itself.

The disassembly asks with each line's identity (the PC line predicted, other lines by the live
`route()`, history entries recorded: E23-22's view rules) and names an operand address by the
space that supplies it now. A paged name gives its space to a consumer that can carry one (an
Execute breakpoint or `on execute @name` stops only in that space, as a source-line breakpoint
does); a consumer that cannot gets the 16-bit address (4.6's rule). The verbs' signatures, the
UI, and the rule for a name already used at another key are E23-24's.

### 10.4 Page rules per adapter

- **SLD `L`** (E23-23): `page >= 0` gives (`Page{page}`, `value & 0xFFFF`). `page = -1` gives
  an unpaged label ("stay unpaged", #375), and a name-only value when the value does not fit
  16 bits (an EQU can be 32-bit). sjasmplus writes -1 for every `EQU` without an explicit
  page, address constants included (2.1), so the format cannot tell an address from a
  constant; making them labels agrees with the `--sym` file, which lists the EQU constants
  too. A record with no main label (a module marker) names nothing.
- **z88dk map `; addr` symbols** (E23-23): the space by 9.4's derivation, the same as the map's
  source records, so that one file has one derivation for both stores. `; const` stays
  name-only.
- **Simple map, `Memory.txt`, sjasmplus `--sym` and `--exp` files (#390)**: unpaged.
- **User entries**: always paged, by the identity of the line they were added on or, for an
  import, by the rule of 11.2.

### 10.5 The project file (E23-24; "plain text, decided in the design doc", #387)

- Extension `.jdp`; UTF-8; LF written, CRLF accepted; a line starting with `#` is a comment;
  blank lines are ignored.
- The first other line is `jnext-annotations 1` (format and version); anything else is refused.
- Records, one per line, fields separated by one space:
  `label <SPACE> $<ADDR> <NAME>` and `comment <SPACE> $<ADDR> <TEXT>`. `<SPACE>` is the
  identity's text form (4.6); `<ADDR>` is four upper-case hex digits; `<NAME>` is one token with
  no whitespace; `<TEXT>` is the rest of the line, with `\\` for a backslash and `\n` for a
  line break. At most one label and one comment per key.
- Written sorted by (space, address, record kind), so that it diffs well in a repository.
- Read strictly, by the adapter contract: the whole file is validated, an error names the
  line, and only then is the user layer replaced. An unknown record kind or version is refused.
- Opened and saved by the user (a menu action); never attached automatically beside a program;
  not part of `.jns`.

## 11. Coverage and reverse-engineering interop

### 11.1 Coverage (E23-25 (#388)): opt-in and benchmarked

Coverage is off by default. When on, it records executed, read and written bytes per space
(`FetchSource`: RAM pages, ROM, the overlays). The execute side comes from the recorded fetch
identity (E23-08); the read and write sides from the same arbiter, given the access direction
(4.2). It is shown in the disassembly and memory panels, with clear and reset, and it is
E23-26's automatic code/data map. Off must cost nothing measurable; off and on are measured with
`make bench` (non-PGO, A/B, quiet host) and the numbers are recorded in the sub-issue. E23-25
decides and pins the rewind rule (no double count), and whether INS-20's logical bitset stays
beside it.

### 11.2 skoolkit and ghidra (E23-26 (#389)): how a page identity maps into each format

Export writes what the disassembly shows (the user layer over the toolchain layer); import
creates user entries only (E23-24). Each writer is proved by its foreign reader (skoolkit's
tools, ghidra's script), never by jnext's own importer.

- **ghidra.** The export is a symbol file for Ghidra's `ImportSymbolsScript.py`: one label per
  line, `NAME ADDRESS l`. The script splits a line on whitespace and reads a name, an address
  and an optional `f` or `l`; it carries no comments and no code/data map. `ADDRESS` is
  `0x<hex>` for an unpaged label and `<TAG>:0x<hex>` for a label with a space, `<TAG>` being the
  space's text form (4.6). In ghidra `<TAG>` names an address space: an overlay block the user
  creates for that page's bytes at the logical range it was mapped at. (The script's header
  lists `ADDRESS_SPACE:0x1234abcd` among its address forms, `AbstractAddressSpace.getAddress()`
  splits `NAME:offset` at the last `:` and accepts `0x`, and an overlay's address space normally
  carries the block's name.) This is read from the source, not run; E23-26's foreign-reader row
  proves it with ghidra itself. Import reads the same lines: a `<TAG>:` prefix gives the space,
  and an address without one takes the space that supplies it at import (the live `route()`).
- **skoolkit.** One control file per space. A control file describes one 64K memory view with
  16-bit addresses, and skoolkit's only bank notion is the `@bank=0-7` directive, the 128K RAM
  bank at $C000-$FFFF (SkoolKit 9.1 and later), which names neither the Next's 224 8K pages nor
  ROM and the overlays. Each file's name carries its `<TAG>` (unpaged labels get a file of
  their own), and its first line is `# jnext space <TAG>`, a control-file comment that skoolkit
  ignores. `sna2skool.py -c` may be repeated, so a view of several spaces is several files. By
  default `sna2skool.py` reads every `<input-stem>*.ctl` beside its input, which would merge
  spaces that share addresses, so E23-26 names the files so that a default pickup cannot do
  that. Labels are `@ <addr> label=<NAME>` directives, comments are instruction or mid-block
  comments, and the code/data map is block directives from coverage (E23-26 picks them). Import
  takes the space from the header comment, else from the file name's `<TAG>`, else from the
  live mapping at import. A control file regenerated by skoolkit loses its comments.

### 11.3 Out of scope

Owner decision on #378: a built-in Z80N assembler, and a native export of source that
assembles again.

## 12. Non-goals and known limits

- C variables, types and scopes: the follow-up epic #353 (OD-9); the CDB records of G11 are its
  input.
- Executing a source step in the background: a step is synchronous and bounded (OD-11).
- Editing source; a built-in Z80N assembler; a native export of source that assembles again
  (11.3).
- Data watchpoints qualified by bank: DZRP's `bank+1` watchpoints use the legacy qualifier (4.5).
- **Code that moves.** A record describes where its code was built to run. Code copied at run
  time to another address or page (a driver copied out of a bank into low RAM, as Cobra's audio
  driver is), or a page mapped into another slot, has no record where it actually runs and
  shows as unmapped there; labels behave the same (10.1). Producing records for the copy is the
  build's job (an SLD for the copy's `DISP`). The SLD binary identity likewise covers only
  `org..org+size`.
- Toolchain mis-attributions (G4 on 2.4, master sdcc's `util.h:9`, G7) are shown as the
  toolchain wrote them, with the primary line and the "also" list (OD-7), and documented in the
  user guide.
- Tapes and programs the guest loads for itself (6.2, 6.6).

## 13. Tests

**Suites today.** `source_map_test` (the SLD and `Memory.txt` adapters, the store),
`source_debug_test` (the backend on a real machine: loading and identity, fetch identity now
and in history, sidecar ownership and failure paths, the lifetime rows, every step kind),
`source_debug_posix_test` (`# os: posix`: the link and FIFO fixtures),
`debugger_source_panel_test` (the Source tab, page-qualified model rows and gutter, the Call
Stack column, symbol address fields), `rewind_call_stack_test` (the tracker across rewind),
`debugger_backend_test` (SYM-14 to SYM-18, the `-debug` map symbols; INS-13-15, the 56-byte
`TraceEntry`), and the functional rows `nextbuild-symbols-func` and `source-map-func`.

**Planned.** `z88dk_map_test` (E23-13), `z88dk_lis_test` (E23-17), the fixture-hash row
Z88FIX-01 (E23-12), new rows in `mmu_test`, `multiface_test` and `divmmc_test` (E23-07), and the
functional rows `source-map-func` (a looked-up line, E23-15) and `z88dk-source-func` (E23-15).

**Oracles.**

- The stores and the adapters have no VHDL counterpart: the oracle is the format's
  documentation, the fixture output and the rules of this document.
- The fetch identity is VHDL-oracled (the lines in 4.1 and 4.2).
- C stepping is checked against `z88dk-gdb` on the same fixture (E23-14).
- Every writer (11.2) is checked against a foreign reader.
- The project file has no foreign reader, so its rows assert the written bytes against 10.5, not
  only a round trip.
- z88dk fixtures are our own sources, generated on both installs and committed with the z88dk
  commit and the command; every row that depends on compiler output names its install (2.3);
  a row checks the fixtures' recorded hashes, since CI has no z88dk.

**Row IDs are global**: the duplicate-ID gate refuses a reuse. IDs written into a sub-issue's
text before v1.1.64 may be taken already (`SRCDBG-CS-02`, `SRCDBG-STEP-17`, `SRCDBG-SIDE-12`,
`QBPM-11` and `QSRC-28` to `QSRC-33` are in the tree), so each sub-issue checks its IDs against
the tree.

