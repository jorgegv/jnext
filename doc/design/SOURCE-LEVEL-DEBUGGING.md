# Source-level debugging: design (CAP-SRC)

> Status: **partly implemented** (v1.1.64, plus E23-07 and E23-06 on the epic branch). The backend
> capability, the SLD and `Memory.txt` adapters, the Source tab, the ownership rules, and the
> fetch identity (one read arbiter, the current-PC prediction; section 4) exist. The rest is
> designed here and built by the sub-issues of epic #23, named `E23-NN (#NNN)` wherever a
> section depends on one.
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

The tools and formats this document cites by name, version and file path (z88dk, sjasmplus,
DeZog, SkoolKit, Ghidra) have their repository URLs in [`../REFERENCES.md`](../REFERENCES.md).

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

`Z`, `T` and `L` are read and the other types are skipped (`L` by E23-23 (#375), section 10.4). The optional binary identity travels in comment records other readers ignore:
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
   compiler output says which install, at which z88dk commit, produced it, and both installs
   are covered. Fixtures are generated by the real tools from our own sources and committed,
   because CI has neither install (E23-12 (#343)).
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
The addresses quoted here are therefore examples: E23-12 (#343) generated the fixtures on each
install (`test/fixtures/z88dk/`, README there), and every row takes its values from them; they
supersede the planning examples where the two differ (master sccz80 equals the planning values).

**What each output carries.**

- `prog.map` (`-m`): linked, absolute addresses (16-bit; 24-bit for a classic `BANK_n`
  section, see 72ea9d221b below) and the section name (`PAGE_20`, `BANK_10`); no lines. jnext
  reads it today for symbols.
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
  2026-10-18, and again by E23-12), so a classic map has no C lines; without `-debug` a classic
  build can link (our fixture sources, both installs). The cause: `-debug` makes zcc forward
  ALIAS options (`lib/config/alias.inc`, here the classic `CLIB` line's `--hardware-keyboard`)
  to the compiler, which rejects them; `zcc +zx --vt100 -debug -c util.c` fails the same way.

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
- G8. Banked code takes its page from the section name on newlib: `PAGE_20` starts in page 20
  (and runs on into page 21 if it is larger than 8K), and `BANK_10` is the 8K pages 20 and
  21. Addresses are logical and 16-bit, so without the section every bank's $C000 is the same
  address: D3's hazard (section 9.4).
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
  `CODE_10`; fixture `test/fixtures/z88dk`, `--codesegBANK_10`): `_banked_add` is `$AC000` on
  2.4 (ORG $C000) and `$14C000` on master (`memory_map.asm`: `SECTION BANK_10`,
  `ORG 0x140000 + CRT_ORG_BANK_10`, `CRT_ORG_BANK_10 = 0xc000`).
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
`NAME: EQU 0x0000ABCD` line each (sjasmplus 1.23.1). The value is 32-bit: `--sym` also lists
EQU constants wider than 16 bits (`bigConst: EQU 0x00012345`) and `=` variables
(`varSym: EQU 0x0000000E`, which the SLD leaves out), and `--exp` lists them when they are
`EXPORT`ed (observed 2026-10-18). The files carry symbols only. #390 loads them as unpaged
labels (section 10.4) and settles what a value above 0xFFFF becomes; it is a standalone issue
that lands ahead of the epic, as #352 did (OD-10).

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
| `prog.sld` `T` records | SLD | `SourceMap` | done (ranges, E23-06 (#338)) |
| `prog.sld` `L` records | SLD labels | `SymbolTable` | E23-23 (#375) |
| `Memory.txt` | NextBuild symbols | `SymbolTable` | done |
| `prog.map` (`-m`) | z88dk map symbols | `SymbolTable` | done (#352) |
| `prog.map` (`-m -debug`) | z88dk debug map | `SourceMap`, `SymbolTable` | done (E23-13 (#344)) |
| `*.lis` and `prog.map` | z88dk listings | `SourceMap` | E23-17 (#348) |
| `prog_<GROUP>.bin` | program identity | `SourceMap` | done (E23-13 (#344)) |
| `--sym` / `--exp` files | EQU symbols | `SymbolTable` | #390 |
| `.cdb` / `.adb` | CDB | `SourceMap` | E23-20 (#350), if the files exist |
| project file | annotations reader and writer | user annotations | E23-24 (#387) |
| `__CDBINFO__` records | CDB variables | `VariableStore` | follow-up epic #353 |

### 3.2 The four design requirements

D1 to D4 are epic #23's design requirements (its acceptance criteria 1 to 4). Each is stated
with how it is met.

**D1. History records what the fetch did; the current PC is predicted by the same function.**
One arbiter, `Mmu::route(addr) const`, returns a `Route`: the `FetchSource`, the offset within
its store, and the byte pointer. It is the only place the priority order lives (section 4.2),
and `read()` is that route plus one side-effect tail. The CPU's opcode read goes through
`Mmu::read_m1(pc)`, which stores the result in `last_m1_` (read back by `Mmu::last_m1()`). A
trace entry takes it after the instruction ran (nothing is recorded for a slot that fetched nothing: an accepted
INT, an NMI or the esxdos shim keep the prediction for their PC); a call frame's caller identity is the CALL's
recorded fetch and its target identity the target's first M1 (E23-08). The Source tab and
`source_location()` predict the next M1 with `route_with(addr, divmmc_active, mf_active)`, fed
by `DivMmc::active_on_m1()` and a const `Multiface::would_latch_on_m1(pc)`
(`Emulator::predict_fetch(pc)`); `route()` is `route_with()` with the live values, one body, so
the prediction cannot drift from the fetch. Built: the arbiter, `read_m1` and the prediction by
E23-07 (#339); trace entries and call frames take the recorded value (E23-08 (#340), section
4.3). Check (epic acceptance 1): no
prediction of the M1 overlay outside `route_with`, `active_on_m1` and `would_latch_on_m1`;
SRCDBG-ID-17 (an instruction that remaps its own slot keeps its recorded identity).

**D2. The source map is range-aware and many-to-many.** A record covers its start up to an
extent the adapter gives (section 9); ranges may overlap, and an address may have several
locations. `lookup(space, addr)` returns all of them with a primary (the shortest range; on a
tie the record emitted last, which for a z88dk map is the highest line, G3).
`resolve("file:line")` returns every start; a zero-width line resolves through its alias or
refuses with "no code at file:line". Built by E23-06 (#338), with the z88dk extents in E23-13
(#344) and E23-17 (#348). Built (E23-06): SRCRNG-01 to SRCRNG-12. Check (epic acceptance 2): Z88MAP-02,
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
(section 4.5). The type is built (E23-07 (#339)) and every consumer has moved: the source map (E23-06 (#338)),
history (E23-08) and breakpoints (E23-09 (#341), with one predicate); `page_ram_only` is gone. This document is E23-02 (#334). Check (epic acceptance 4):
SRCBP-02 (a page number is not an identity), SRCBP-05 (the legacy meaning pinned).

### 3.3 Implementation status

| Mechanism | Today (v1.1.64 plus E23-07) | Design | Changed by |
|---|---|---|---|
| Fetch identity at the PC | DONE (E23-07 (#339)): `Mmu::route()` / `route_with()` are the one arbiter and `read()` / `read_m1()` / `read_fetch()` sit on it; `Emulator::predict_fetch(pc)` asks `route_with()` with the M1's own answers (`DivMmc::active_on_m1()`, `Multiface::would_latch_on_m1()`); `Debugger::source_page()` returns the `FetchSource` (`MemSpace`, `debug/mem_space.h`). The breakpoint gate, the Breakpoints panel and the disassembly gutter use the `FetchSource` itself (E23-09 (#341)); only the Source tab's display string still reduces it to a RAM page through `ram_page_of()` (`debug/ram_page.h`), which is E23-22's | 4.1, 4.2, 4.4 | E23-07 (#339) |
| Identity in history | DONE (E23-08 (#340)): `TraceEntry` carries `fetch_kind` / `fetch_index` (taken from the old `int opcode_len`, still 56 bytes), set after `execute()` from `Mmu::last_m1()`; a slot that fetched nothing keeps the prediction for its PC. `TraceLog::fetch_not_mmu_ram()` is gone (E23-09): `history_stop_at()` builds an `Event` from the entry and asks `EventFilter::matches()`. Back and Reverse Continue look up the recorded identity; the export prints `FETCH=kind:index` | 4.3 | E23-08 (#340) |
| Identity in call frames | DONE (E23-08 (#340)): `CallFrame::caller_space` and `target_space` (`MemSpace`): the caller is the CALL's recorded fetch (for an INT / NMI frame the prediction at the interrupted PC), the target is recorded at the first fetch at `target_pc`; the call-stack opcode bytes follow the M1 overlay | 4.3 | E23-08 (#340) |
| Source map | DONE (E23-06 (#338)): ranges `{FetchSource, lo, hi_excl, location}`, overlaps allowed, a segment index (one binary search, no allocation); `lookup` returns the primary `const SourceLocation*` into the store, `lookup_all` every one; `resolve()` returns the line's entries and a reason ("no code at"); the SLD adapter extends each record to the next distinct start in its page, capped at the 8K page end | 5 | E23-06 (#338) |
| Breakpoint qualifier | DONE (E23-09 (#341)): `EventFilter::space` (a `FetchSource`, compared exactly with `Event::space`) is the canonical qualifier, `EventFilter::page` stays as the legacy protocol one; ONE predicate, `EventFilter::matches(ev)`, called by `EventTable::filter_matches()`, `Debugger::probe_execute()` and `history_stop_at()`; source breakpoints are groups (`subscribe_source_line()`, `unsubscribe_group()`, `set_group_enabled()`, `SubscriptionInfo::group`) | 4.5, 8.1 | E23-09 (#341) |
| Lifetime | DONE (E23-10 (#342)): a load generation (`Debugger::Impl` counter, `DebugState::load_generation()`, kept per rewind slot and set before `load_state()`); a cache of the last 4 attachments; `Impl::recheck()` (readers and writers) judges a landing: ring landing = the cache, other landing = identity check (a user map goes stale); `source_map_status()`; Back filters by generation through `TraceLog::recorded()` markers | 6 | E23-10 (#342) |
| z88dk | DONE (E23-13 (#344), stages 1 and 2): the map gives symbols (`load_z88dk_map` skips the three debug prefixes, #352) and, through `load_z88dk_source_map()`, source lines; `<stem>.map` is a sidecar, Load Source Map... and `--source-map` take it, identity is checked from `prog_<GROUP>.bin`; tape programs: stage 3 | 9.4, 9.5 | E23-13 (#344), E23-17 (#348) |
| Symbols | DONE (E23-23 (#375) stages 1 and 2): `SymbolTable` keyed by (memory, address); SLD labels reach the backend's table through the sidecar and the hand load; z88dk pages: stage 3 | 10 | E23-23 (#375); EQU files: #390 |
| User labels and comments | none | 10 | E23-24 (#387) |
| Coverage | INS-20: one bit per executed 16-bit PC (`CoverageBits`) | 11.1 | E23-25 (#388) |
| Source steps in menus and keys | Debug > Source menu; Ctrl+F6/F7/F8, all rebindable (done in E23-05 (#337)) | 8.2 | E23-05 (#337) |
| Source tab | the PC line highlighted, the step buttons, "Running" while the machine runs; no click-to-break, Run to Line, location list or selection sync | 8.2 | E23-16 (#347) |
| Remote and script surface | GDB `monitor sym`; no `monitor src`, no DSL `file:line` | 8.3 | E23-15 (#346) |
| The identity in every view; remote vocabularies | none | 4.6 | E23-22 (#360) |
| C stepping on real z88dk code | none | 7 | E23-14 (#345) |
| Dot commands | none | 9.4 | E23-21 (#351) |
| `.cdb` / `.adb` | none | 9.6 | E23-20 (#350) |
| Fixtures | `test/fixtures/zdebugmap/` (trimmed 2.4 sccz80 maps, #352) | 2.3, 13 | E23-12 (#343) |
| skoolkit / ghidra | none | 11.2 | E23-26 (#389) |

Done: E23-01, E23-03 and E23-04 (v1.1.64: the backend capability, the SLD and `Memory.txt`
adapters, the Source tab, ownership, the sidecar refusal reasons, a clock-based
lifetime, replaced by the generation model in E23-10); #332 (v1.1.46: the call-stack tracker follows rewind); #352 (v1.1.45). The epic closes
with E23-19 (#349). A sub-issue that lands updates its row here, not the prose.

## 4. Which memory supplied the instruction

A source record describes code built for one memory, so the question a lookup must answer is
"which memory supplied this instruction". The MMU's page number alone does not answer it: a
ROM-mapped slot reports a page number from the same range as the RAM pages, and the overlays
that the CPU's reads go through first (the boot ROM, the Multiface, DivMMC, the Layer 2 read
mapping, the alternate ROM and config-mode routing) replace the MMU's page without changing
it. One identity, used everywhere CAP-SRC matches code, answers it.

Implemented on the epic branch: 4.1, 4.2 and 4.4 (E23-07 (#339)) and 4.3 (E23-08 (#340))
describe the tree; 4.5 (breakpoints) and 4.6 (presenting) are still plans.

### 4.1 `FetchSource`

`FetchSource` is `MemSpace` (INS-02) extended (OD-13) with the overlays that `Mmu::read()` serves
before the MMU, and the type moved to `src/debug/mem_space.h` (std headers only, so `mmu.h` can
return it without reaching the published `inspect.h`, which re-exports it).
`using FetchSource = MemSpace` names the role; it is one type. The kinds, in enum order, with
the index each carries:

| Kind | Index | Supplies |
|---|---|---|
| `Cpu` (0) | none | the live view; never a fetch source and never a route result |
| `Page` (1) | MMU page n, 0 to 223 | RAM page n and nothing else |
| `Rom` (2) | 0 to 3 (`sram_rom`) | a 16 KB ROM image |
| `BootRom` (3) | 0 | the 8 KB boot ROM, mirrored through 0x0000-0x3FFF |
| `MultifaceRom` (4), `MultifaceRam` (5) | 0 | the two Multiface halves (a13 = 0 and 1) |
| `DivMmcRom` (6) | 0 | the DivMMC ROM |
| `DivMmcRam` (7) | bank 0 to 15 (3 at page 0 under MAPRAM) | a DivMMC RAM bank |
| `AltRom` (8) | `sram_alt_128_n`: 0 = Alt ROM0 128K, 1 = Alt ROM1 48K | the alternate-ROM read override |
| `Layer2` (9) | `layer2_active_page` (8 bits) | the Layer 2 read mapping |
| `Config` (10) | NR 0x04 (a 16 KB bank) | the config-mode window onto SRAM |
| `Floating` (11) | 0 | nothing: the read returns the floating bus (jnext: 0xFF) |

The kinds 3 to 11 are identity-only: `peek` and `poke` refuse them with `Result::Unsupported`
(one total switch, so `-Werror=switch` makes a future kind a compile error there); E23-21
(#351) adds `peek(DivMmcRam)` with its own rows. Every route result has `kind <= 15` and
`index <= 0xFF` (mmu_integration_test FSRC-09 and static_asserts), which leaves room for the 12-bit packing
that E23-08 needs.

Equality is exact (kind and index) and by decode path, not by physical store. `Layer2{p}` and
`Page{p}` share SRAM for every p outside 0x0A, 0x0B and 0x0E, and `DivMmcRam{b}`, `Config{b}`,
the Multiface and alt-ROM windows alias SRAM pages that some `Page{}` also reaches; they stay
distinct, because the VHDL reaches them by different routes and a view that shows which one
answered must not collapse them. Canonicalising by physical page was rejected: it loses the
BRAM/SRAM and decode-path facts that E23-22 shows.

- Why not `NOT_RAM_PAGE`: it lumps ROM, DivMMC RAM and the Multiface into one value, and a
  z88dk dot command runs from DivMMC RAM, so its records would match any overlay.
- Why `Layer2{page}` is a kind of its own: the mapping is RAM, but not the slot's page, and
  for banks 5 and 7 not even the same memory. The MMU reads the dedicated BRAM for pages 0x0A,
  0x0B and 0x0E (`zxnext.vhd:2961-2962`, `3039-3041`); the Layer 2 arm forces the SRAM copy
  (`zxnext.vhd:3100-3107`).
- Every arm `Mmu::read()` has gets a kind. The VHDL's expansion-bus ROMCS arm
  (`zxnext.vhd:3108`) has none, because jnext models no expansion-bus device.
- Why `Floating`: no SRAM answers for a slot 2 to 7 holding an MMU page of 0xE0 or more
  (`zxnext.vhd:3060-3061`) or a Layer 2 page of 0xE0 or more (`3101-3102`), and such a page
  names memory that does not exist (`Page{0xE5}` would be a "RAM page" the SRAM never answers).
  It is not RAM, so `ram_page_of()` and the trace flag say `NOT_RAM_PAGE` for it, where they
  said page 0xE0 to 0xFE before. The name `None` was rejected: X11 defines it as a macro.

### 4.2 One arbiter

`Mmu::route(addr) const` (D1) is the only place the priority order lives; it returns a `Route`
(`space`, `offset` within the space's store, `ptr` to the byte, null when the bus floats).
`route_with(addr, divmmc_active, mf_active)` is the same body with the two overlays that an M1
itself switches given by the caller. The body is one template, `Mmu::route_bits_<Live>()`
(always inline; a packed 32-bit identity, kind in the low 16 bits and index in the high 16):
the live form asks the DivMMC and the Multiface lazily, only for 0x0000-0x3FFF and only when
the boot ROM has not answered, so a read makes no more calls than the old cascade did, and
the two answers of `route_with()` cost nothing. The rare arms (Multiface, DivMMC, Layer 2, alt
ROM, config mode) are built out of line; a slot's own source is cached as `slot_space_[8]`,
written at the places that write `read_ptr_[]` and rebuilt by `load_state()`. The DivMMC's
ROM/RAM/bank decode exists once, in `DivMmc::overlay_at()`. The VHDL's order for
0 to 16K is the boot ROM, the Multiface, DivMMC, the Layer 2 mapping, the MMU, config mode,
the expansion-bus ROMCS, then ROM, which the alternate ROM replaces (`zxnext.vhd:2933-2950`);
16K to 48K is the Layer 2 mapping and then the MMU, and 48K to 64K the MMU. The arms: boot ROM
1856, Multiface (in the pre-arbiter) 3030, config mode 3044, DivMMC ROM and RAM 3084 and 3092,
Layer 2 3100, alternate ROM 3116.

The VHDL arbiter takes the access direction as an input (`zxnext.vhd:3077`: the Layer 2
mapping uses its read enable on a read and its write enable on a write; `3078`: the
alternate-ROM decode). So any later write-side identity (E23-25) comes from the same function,
given the direction, never from a second copy of the order. `write_landed()` keeps its own
cascade for now, because write arbitration has other rules (read-only slots, the Layer 2 write
enable, the alt-ROM write-over). If inlining misses the bench threshold (4.7), the fallback
goes to the owner, never to a second arbiter.

`read()` is the route followed by ONE side-effect tail, `finish_read_()`, for every source: the
+3 floating-bus latch on every contended access (`zxnext.vhd:4498-4509` latches whatever
supplied `cpu_di`; an overlay read over a contended page used to miss it) and the READ
watchpoint (the boot-ROM and floating reads used to miss it). `read_m1(pc)` is the same read
that also stores the route's identity in `last_m1_`; `read_fetch(pc)` is the same read with no
record (4.4). The CPU core's default `MemoryInterface::read_m1()` and `read_fetch()` are plain
reads, so every stub memory is unaffected. `last_m1()` names the first opcode byte of the last
instruction fetched; an accepted INT or NMI and an intercepted esxdos call fetch nothing and
leave the previous value, so pair it with `Z80Cpu::fetched_opcode_last_execute()`. It is
`MemSpace::cpu()` ("none recorded") after construction, `reset()` and `load_state()`.

### 4.3 Recorded identity (E23-08 (#340))

A trace entry stores the `FetchSource` of its own M1 as a kind plus an 8-bit index
(`TraceEntry::fetch_kind`, `fetch_index`, with `fetch_space()` / `set_fetch_space()`; a 4-bit
index cannot name pages 16 to 223). The bytes come from narrowing `TraceEntry::opcode_len` to
`uint8_t` (OD-4 (a); its maximum is 66), so `TraceEntry` stays 56 bytes and every offset is
unchanged (`debug_types_check.cpp`, INS-13-15). The parallel `not_ram_` vector is gone, and with
it its resize and ring-wrap hazards.

`Emulator::step_one_instruction()` records the entry before `execute()` (as before, with a
provisional identity) and `TraceLog::set_newest_fetch()` writes the identity into that entry
after it, from `Mmu::last_m1()`, read once per instruction and only when
`fetched_opcode_last_execute()` says the CPU fetched. A slot that records no fetch keeps the
prediction for its PC, the value the live Execute gate used: an accepted INT reads no memory
(`t80na.vhd:278-279`); an NMI does an M1 read at the interrupted PC and discards the byte
(`t80n.vhd:502`), which jnext's core does not perform, so the prediction stands for it; the
esxdos shim is jnext-only.

`CallFrame` carries `caller_space` and `target_space` (`cpu()` = none recorded). The caller is
the CALL's or RST's own recorded fetch. The target is pending until the first fetch AT
`target_pc`, and then recorded once; a fetch elsewhere (the debugger moved PC, the esxdos shim
serviced the call) leaves it unrecorded. An INT or NMI frame's caller is the prediction at the
interrupted PC: the interrupted instruction has not been fetched, and will be only after the
return. The per-instruction prediction of the post-instruction PC is gone.

Back and Reverse Continue look the source map up with the entry's recorded identity, and the
trace export prints it as ` FETCH=kk:ii` (hex kind and index) after `ROM=`; E23-22 (#360)
replaces that column's text with its formatter. ZRCP `cpu-history` and DZRP history are not
affected.

### 4.4 The current-PC prediction (E23-07 (#339))

The Source tab, `source_location()` and the breakpoint gate ask about an M1 that has not
happened yet, and the M1 itself switches overlays. DivMMC's instant-on entry points map DivMMC
for the very fetch that hits them. The hold carried from the previous M1 decides whether
DivMMC still answers: the fetch after a 0x1FF8-0x1FFF unmap is RAM again while DivMMC still
reads as mapped. The Multiface latches on the 0x0066 fetch.

So the answer is a prediction, but of the same function: `Emulator::predict_fetch(pc)` is
`route_with(pc, divmmc_active, mf_active).space`, the two answers coming from
`Emulator::predict_m1_overlays_(pc)`: the DivMMC decision from `DivMmc::active_on_m1()` (it
shares its entry-point decode with `check_automap()`), asked only when `automap_m1_may_react(pc)`,
and the Multiface decision from the const `Multiface::would_latch_on_m1(pc)`, which replaced
the copy of the whole Multiface that the tree made before. The arbiter inputs the automap
decode needs (`sram_pre_override(2)`, `(0)` with config mode, the alt-ROM read enable and
`sram_alt_128_n`) exist once, in `Emulator::m1_automap_gates_(pc, mf_active)`, which the
`on_m1_prefetch` hook and the prediction both call; the hand-kept mirror of the hook is gone.
`Debugger::source_page()` returns the predicted `FetchSource`; `Emulator::fetch_not_mmu_ram()`
and `Mmu::read_not_mmu_ram()` are deleted (`TraceLog::fetch_not_mmu_ram()` stays as a view of the
recorded identity, section 4.3). A prediction has no side effects: rows compare the
whole machine state before and after (SRCDBG-PURE-01, with SRCDBG-PURE-02 as the control).

**Fetch view and data view.** DivMMC's `automap` is not one value across an instruction
(`divmmc.vhd:124-148`). `automap_hold` loads on every M1 (MREQ and M1 low) from that M1's entry
terms and the previous hold; `automap_held` takes it on the first MREQ-high after the fetch; the
instant terms are latched only while M1 is low. So the opcode fetch sees `held OR instant`
(`DivMmc::is_active()`), and every bus cycle after it sees the hold that fetch loaded
(`DivMmc::is_active_after_fetch()`, `active_with_hold(hold)` for a hold other than the live
one). Consequences, all pinned by rows:

- A delayed entry's own operands come from the DivMMC ROM although the opcode came from RAM;
  an instruction in the 0x1FF8-0x1FFF unmap window fetches from the DivMMC and reads its
  operands, and writes, from RAM.
- `Mmu::read()` and the write path are the data view. `Mmu::read_m1()` and
  `Mmu::read_fetch()` (and the FUSE core's re-read of the opcode, `MemoryInterface::read_fetch`)
  are the fetch view.
- The inner bytes of a prefix are M1s of their own, with their own decode: CB and ED one more,
  DD and FD the DD/FD bytes and the opcode that ends them, and one more after an ED.
  `Z80Cpu::fetch_prefix_chain_()` runs the `on_m1_prefetch` decode at each M1's address and
  reads the byte once into a buffer that the core and the look-aheads read back. The `d` and
  `op` of `DD CB d op` are data reads, not M1s.
- `DivMmc::is_nmi_hold()` follows the post-fetch hold (`o_disable_nmi = automap or button_nmi`,
  `divmmc.vhd:148-151`): the instruction at a delayed entry disables the NMI, and the one that
  unmaps releases it.

**The debugger reads what the CPU runs.** `Emulator::predict_instruction_(pc)` returns the first
four bytes of the instruction about to execute with the route of its opcode fetch: the same
chain, each M1 byte under the overlay its own M1 selects (the hold chained with
`DivMmc::hold_after_m1()`), the operands under what the last M1 left. The trace bytes, the
instruction-length reader, the trace flag and the call-stack opcode bytes and caller page use
it, so the esxdos `RST $08` entry and the delayed NMI entry trace the bytes the CPU executed,
not the bytes of the map before the M1. The length reader and the disassembler give the true
length of a redundant DD/FD chain (`byte_count` may exceed 4; the first four bytes are kept in
`opcode_bytes[4]` and `bytes[4]`), both bounded at 64 redundant prefixes: past it the
disassembler makes the prefix byte its own `DB $DD` line (a 65-byte line for a run of DD), and
the trace's length reader stops its recursion by returning 2 (66 bytes in all). The CPU itself
consumes any run of prefixes in one `execute()`.

Two limits, stated: `predict_instruction_` predicts the first four bytes only, so an M1 chain
past four bytes is predicted for its first four; and the only production reader of
`Mmu::last_m1()` is `Emulator::step_one_instruction()`, after `execute()` (the `source_debug_test`
rows compare it with the prediction for every fetched instruction).

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
`CFG` plus the page) is provisional until E23-08 has landed; E23-07 fixed the kinds and indices
(4.1).

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
E23-07 (#339) gated on `make bench` (non-PGO A/B on a quiet host; the plan was at most 1% on the
boot and game benches). Measured on the non-PGO build: `boot-nextzxos` +1.1% to +1.9% cycles,
`boot-48k` inside the noise; the shipped PGO build was measured faster than the PGO build
before. The owner accepted the non-PGO figure after trying two variants inside the one
arbiter, neither kept (one shared out-of-line body for `read`, `read_m1` and `read_fetch`,
about +4% cycles; a shared `noinline` fetch body beside the inlined `read()`, +21.6%).
`read_m1()` costs one load and one store more than `read()`. With the trace on, the identity is
stored per M1; with the call stack on, per frame; an armed `Execute` subscription is evaluated
only for its address (the existing per-page bitmap). E23-08 (#340) removed a full M1 prediction per
instruction from the call-stack post-hook and a parallel-vector store from the trace, and added one load of
`last_m1_`, one branch and a two-byte store into the entry already in cache. Measured on the non-PGO
build, base against branch, interleaved, 5 runs each, pinned to core 0, `--headless --benchmark`, median
fps; the host was loaded throughout (1-min load 7 to 11 on 12 CPUs), so the spread of single runs is large
(trace-on 48K ran 290 to 727 fps) and only a gross regression is excluded:

| workload | trace off, base / branch | trace on, base / branch |
|---|---|---|
| `next` 500 frames (NextZXOS boot) | 219.0 / 218.2 (-0.4%) | 151.3 / 151.1 (-0.1%) |
| `48k` 600 frames | 912.4 / 917.6 (+0.6%) | 603.4 / 585.1 (-3.0%; the 9-run repeat gave +6.4%) |

No consistent difference in either direction beyond the noise; a quiet-host `make bench` is still the
arbiter.

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

Built by E23-06 (#338), as above with these particulars. The store is keyed by `FetchSource`
directly (E23-07 landed first), and `source_location()` has no `ram_page_of()` bridge. File
strings are kept once per position (no interning, no function field yet). A line's *entries*
are the starts `resolve()` returns: a range that continues another range of the same line
(`hi_excl == lo`, same space), as one instruction after another does for a multi-statement
line, a macro or a `DUP`, is not an entry; disjoint copies are. History (`TraceLog`) and call
frames name the code's `FetchSource` (E23-08, section 4.3).

### 5.2 The backend verbs (CAP-SRC, `debug/debugger.h`)

| Verb | Meaning | Changes in |
|---|---|---|
| `load_source_map(path, accept_identity_mismatch)` | load a source map by hand; refused on a parse error, or on an identity mismatch unless accepted; the path is stored absolute | DONE (E23-13 (#344)): the format is told by content, a z88dk map fills the source half only |
| `clear_source_map()`, `source_map()` | the store | |
| `source_location(addr)`, `source_location()` | the primary position of `addr` or of the PC, by pointer into the store (done, E23-06 (#338)); every location there: `source_map().lookup_all()` | |
| `source_page(addr)`, `effective_page(addr)` | the `FetchSource` that supplies the M1 at `addr` (done, E23-07); the MMU's page | `effective_page` stays, as the MMU's page for the legacy qualifier |
| `load_program_sidecars(path)` | attach a loaded program's `Memory.txt` and SLD (section 6) | DONE: `<stem>.map` (E23-13 (#344)); lifetime (E23-10 (#342)) |
| `source_step(by, kind)` | `Into`, `Over`, `Out`, `Back`, `ReverseContinue` (section 7) | ranges (done, E23-06); recorded identity (done, E23-08) |
| `load_map(path, MapFormat::NextBuild)` | `Memory.txt` by hand (CAP-SYM gains a format) | |
| `--source-map FILE` (command line) | a source map, attached right after the first `--load` | DONE (OD-15, E23-13 (#344)); requires `--load`, a mismatch exits 1 |

`debugger.h` is a frozen public header: the sub-issue that needs an addition makes it, under
the owner decisions recorded on #23 and #378.

## 6. Sidecars, ownership and lifetime

### 6.1 Search

After a successful program load every frontend calls `load_program_sidecars(path)` (Qt and SDL
from their load tick, headless from `--load`, and `Debugger::load()` for a remote client). It
looks for `<stem>.Memory.txt`, else `Memory.txt`, beside a `.nex`; for `<stem>.sld`, else
`<stem>.sld.txt`; and, with E23-13 (#344) (OD-5), for `<stem>.map` beside a `.nex`, `.tap` or
`.sna`. As built (E23-13): `<stem>.map` fills the symbols last (not when a `Memory.txt`
exists) and the source lines only when no SLD sidecar exists; a map with neither is ignored; a map
whose section binaries do not match the program is rejected whole.

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
symbol half attaches, and so do an SLD's labels (E23-23 stage 2). An SLD's labels attach with its
sidecar unit: not when the file is refused as a whole or its identity does not match, and not
when the user's source map is kept (the SLD is then not loaded at all); they do attach when only
its source records are refused. A tape's map is loaded by hand once the program is in memory. **Open,
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

Two tiers. (The clock-based model, `sidecar_attach_cycle`, is gone.)

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

How it is built:

- The generation counter lives in `Debugger::Impl`, not in the `Emulator` (a cold boot builds a
  new one and a counter it owned would restart and collide with the cache keys). The current
  value is `DebugState::load_generation()`: host-side, **not** in `save_state`, so the snapshot
  width and the `.jns` do not change. A new generation starts at `machine_replaced()` (any
  landing that is not a ring restore) and at `load_program_sidecars()` (the one program-load
  notification, tapes included). A user map is carried into the latter without a second check.
- `RewindBuffer::restore_nearest()` sets the slot's generation **before** `load_state()`, so
  the hook that fires inside it, and the replay that follows, see the landing's generation.
- The hook only marks the landing pending (the machine may be half-built inside `init()`). The
  one recheck (`Impl::recheck()`) runs it, in every reader, in `pump()` and, first, in every
  writer (`load_map`, `clear_symbols`, `load_source_map`, `clear_source_map`,
  `load_program_sidecars`), so a pending landing cannot undo a write. It is O(1) when nothing
  is pending, and it clears its flags before it logs, since a listener may call a reader.
- A program's sidecars are one unit, judged by the source map's identity (`Memory.txt` has
  none). User symbols have no identity and never change with lifetime.
- **Stale** is one gate: `source_map()` and `source_location()` are empty while the user's map
  is stale, so every consumer matches nothing. `Debugger::source_map_status()` says `None`
  (with the last refusal or drop as the reason), `Attached` or `Stale` (the file and why).
  Loading the map again by hand is how a user accepts it (it also re-reads a rebuilt file).
- The cache holds one record per generation, at most 4, the current one included; a failed
  identity check at the next pause erases the record of the generation the map was re-keyed
  to, so a detached map does not come back by a rewind within that generation. A failure at
  the landing itself erases nothing: the record belongs to the previous generation, whose
  history a rewind still returns to.
- The next-pause check applies to a map **carried** across a landing and verified there, once,
  at the first recheck while paused after the clock moved. It is not set by a fresh attach.
- Every lifetime transition is announced to the listeners as an Info line, the existing
  wording.
- Back: `TraceLog::recorded()` counts entries ever stored; `Impl` keeps `(first sequence
  number, generation)` markers, noted at every new generation and at a ring landing. An entry
  counts for Back only if its generation is the current one, and the "statement's first
  instruction" search stops at the first entry of another generation.
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
backend: its predecessor's maps stay until a reset or a load, and nothing re-verifies them (E23-10
decided: no event marks the load, and verifying at every pause would detach a map whose program
writes inside its own image; SRCLIFE-20 pins it). A sibling NEX run under
`--esxdos-stub` (`.RUN`) is a load like any other: the loop owner cold-boots with it, which
drops the old sidecars, and attaches the new program's.

### 6.7 Remote load and Reload

`Debugger::load()` (the DZRP and ZRCP servers; File > Reload program, #374, is planned as a
plain `load()` of the last path) attaches sidecars as the CLI does. It reports a refused
sidecar to every listener (a Warn line, `load: ...`) and in `source_map_status().reason`
(SRCLIFE-16).

A map loaded by hand, across a Reload of a rebuilt program, stays the user's (a sidecar never
replaces it) and is checked like any landing: live if its identity matches the new build, else
stale with the reason, and the user loads it again (SRCLIFE-18). A Reload of a rebuilt program
at the same path attaches the new build's map (SRCLIFE-17).

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
current one (a statement repeated over consecutive instructions counts once), then lands on the
FIRST instruction of that statement's run: it walks older entries, passing unmapped ones, while
they are at that same position. That is the inverse of `Into`. `ReverseContinue`
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
`BreakpointModel::pc_marked(addr, space)` takes the `FetchSource` where it took a page number.
Clicking a Source tab line toggles its group (E23-16); a click on a gutter dot removes only that
location. As built: the group verbs are `Debugger::subscribe_source_line(by, file_line, like)`,
`unsubscribe_group(by, group)` and `set_group_enabled(by, group, on)`, with `SubscriptionInfo::group`;
the model folds a group into one row (`Row::group`, `Row::locations`), enabled only while every
location is; a `space` on a kind other than `Execute` is refused.

### 8.2 Qt

- **Source tab** (`src/debugger/source_panel.*`): the PC's file with the PC's line highlighted.
  It follows the PC at every stop and says "Running" while the machine runs (done in v1.1.64).
  The file is found from the recorded name, absolute or else relative to the map's directory
  or one of its parents, and reloaded when it changes on disk. A refused sidecar says why in
  the tab.
- **Menu and keys** (E23-05 (#337)): a Debug > Source submenu (Step Into, Over, Out, Back,
  Reverse Continue, Load Source Map..., Load Symbols (Memory.txt)...), each action bindable in
  Settings > Preferences... > Debugger Keys like the other debugger actions
  (`src/debug/debug_keymap.cpp`). Decided by the owner on #337 (2026-10-22): source Into,
  Over and Out default to Ctrl+F6, Ctrl+F7 and Ctrl+F8; Back and Reverse Continue ship
  unbound; the emulator window does not forward them. Done.
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
  module in that space, or the record's own `__<section>_tail` (the linker's end of the section,
  when the map has it), or start + 0x400, or the 8K page end, whichever is first. The tail bound
  is ours; the rest is DeZog's rule
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

Input: `prog.map`. One file fills both stores, each its own transaction: symbols through
`load_z88dk_map`, and ranges from `__C_LINE_` and `__ASM_LINE_` through
`load_z88dk_source_map()` (`src/debug/z88dk_source_map.*`, rules 1 to 5 in
`z88dk_section_space()`, `src/debug/z88dk_space.*`). The parser accepts the three location forms of 2.3 (the same
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

1. Section `PAGE_<n>` or `<KIND>_PAGE_<n>`:
   `Page{n + (((head & 0x1fff) + (addr - head)) >> 13)}`, where `head` is the map's
   `__PAGE_<n>_head`, the section's start. That is page n for the first 8K and the following
   pages for the rest, because `appmake` lays a `PAGE_<n>` section out in successive 8K pages
   from page n whatever its size (`src/appmake/zxn.c`, "merge PAGE space into BANK space",
   identical on 2.4 and at 39421a01cd). Measured on 2026-10-18 on both installs (newlib,
   sccz80): in a `PAGE_16` section of 12000 bytes at ORG $C000 (`__PAGE_16_size` $2EE4), a
   symbol at $EEE0 is in page 17; a 600-byte `PAGE_16` section at ORG $E100 starts at offset
   $100 of page 16, and its symbol at $E358 is in page 16.
2. A section of the 16K bank n, which is the 8K pages 2n and 2n + 1. `BANK_<n>`, `<KIND>_<n>`
   and the aligned sections: `Page{2n + ((addr16 >> 13) & 1)}`. `BANK_<n>_L` and
   `<KIND>_<n>_L`: `Page{2n}`. `BANK_<n>_H` and `<KIND>_<n>_H`: `Page{2n + 1}`. `addr16` is
   the low 16 bits of the value. The map's `CRT_ORG_BANK_<n>[_L|_H]` is not used, because
   z88dk's `appmake` (`src/appmake/zxn.c`, identical on 2.4 and at 39421a01cd) places a
   section in the 16K bank at `(org & 0x1fff) + 0xc000` for `_L`, at `(org & 0x1fff) + 0xe000`
   for `_H` and at `(org & 0x3fff) + 0xc000` for the rest: a byte of a plain bank is at bank
   offset `addr16 & 0x3fff`, whatever the ORG. For a bank, a page computed from the distance
   to the ORG is wrong when the ORG is not 16K-aligned (`BANK_10` at $E000 starts in page 21,
   not 20). Checked on 2026-10-18 by scratch builds on 2.4 and on the latest install (newlib,
   sccz80, one function): in `BANK_10` at ORG $C000 or $D000 it is in page 20, at $E000 or
   $A000 in page 21; in `BANK_10_L` (ORG $C000) in page 20; in `BANK_10_H` (ORG $E000 or
   $C000) in page 21.
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
   the program. The adapter binds a rule-4 record with the `space_of` function its caller
   gives (the backend passes `source_page()`, the identity lookups compare against), and the
   space must be a `Page`: an address the MMU maps to ROM refuses the half.
5. A DivMMC dot command (org $2000), and the `DIV_<n>` and `<KIND>_DIV_<n>` sections of the
   memory maps: `DivMmcRam{bank}` (E23-21 (#351), OD-14). E23-21 establishes how a dot
   command's sections map to a bank. Until then a record in such a section refuses the source
   half (there is no unqualified record, D3), as does a `PAGE_<n>` section with no
   `__PAGE_<n>_head` and a section number out of range.

**Zero-width lines** (G3). A line whose address equals the next line's start is an alias of
that line if both are in the same function: sccz80 and 80cc give the function; for sdcc, which
writes `x`, the test is the same module and no function-entry `addr` symbol of another
function at that address. "Which writes `x`" is read from the location's form: sdcc's carries a
TAB before the colon of the line (`main.c::x::10000::11<TAB>:11`), and a function named `x` under
sccz80 keeps its name. Without a function on either side, a line is not an alias of the owner if
a `_`-named `addr` symbol of its module sits at that address and is located at a later line of the
same file (the entry of a function declared after it). Otherwise the line has no code (`main.c:14` in the fixture, the line
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
  means unverified: the map attaches and the log says so (OD-6). As built
  (`load_z88dk_source_map()`): the binaries are `<map stem>_<GROUP>.bin` for each upper-case
  group with a `__<GROUP>_head` in the map; a binary covers a range when the range lies in
  `[head, head + file size)` and the group's own space rule puts both ends of the range in the
  range's space; each non-data range (a section not starting with `data`, `bss` or `smc`, any
  case) that a binary covers becomes one byte check `{space, lo, bytes}`, and a range no binary
  covers is not checked. `SourceMap::verify_program(read, read_space)` reads them through
  `read_space`.
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
  `+struct_def` offset or size). An SLD `L` value above 0xFFFF, or negative, fits no
  16-bit name and is skipped (GH #375, as for an EQU file).

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
`Debugger`. A hand-loaded SLD (`load_source_map()`) fills the toolchain layer with its labels
and owns it as the user's (`Impl::symbols_from_sld`), unless the user loaded a MAP, which is
kept; a later hand SLD replaces an earlier one's labels, and one with none clears only those.

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
  an unpaged label ("stay unpaged", #375). A value that does not fit 16 bits (an EQU can be
  32-bit), or is negative, is skipped; only a `+struct_def` record (a struct's offsets and size)
  is a name-only value. sjasmplus writes -1 for every `EQU` without an explicit
  page, address constants included (2.1), so the format cannot tell an address from a
  constant; making them labels agrees with the `--sym` file, which lists the EQU constants
  too. A record with no main label (a module marker) names nothing.
- **z88dk map `; addr` symbols** (E23-23): the space by 9.4's derivation, the same as the map's
  source records, so that one file has one derivation for both stores. `; const` stays
  name-only.
- **Simple map, `Memory.txt`, sjasmplus `--sym` and `--exp` files (#390)**: unpaged. A
  `--sym` or `--exp` value above 0xFFFF is settled by #390; the SLD rule above (name-only) is
  the precedent.
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
`TraceEntry`; INS-02-30, `peek`/`poke` refuse the identity-only kinds), and the functional rows
`nextbuild-symbols-func` and `source-map-func`. E23-07 (#339) added FSRC-01 to FSRC-16 (the
arbiter's order, one row per arm, and `read()` equal to its route; `mmu_test` and
`mmu_integration_test`), MF-M1G-07 (`multiface_test`), DM-PRED-01 (`divmmc_test`) and
SRCDBG-ID-09 to SRCDBG-ID-15 and SRCDBG-ID-18 to SRCDBG-ID-50, and SRCDBG-PURE-01/02
(`source_debug_test`: the prediction against the recorded fetch, the fetch and data
views, the prefix chains, the trace bytes, the call stack). E23-06 (#338) added SRCRNG-01 to
SRCRNG-15 (`source_map_test`: ranges, overlap, line entries, "no code at", the file suffix
rule, sorted unique starts, the SLD page-end cap), SRCDBG-STEP-18/19 and SRCDBG-BACK-10 to
SRCDBG-BACK-12 (`source_debug_test`: steps by range, Back to the statement's first
instruction and to its last pass), and QBPP-04 to QBPP-07 and QADR-10/11
(`debugger_source_panel_test`: a source line arms every start, "no code at", the Source
column of a logical breakpoint).

E23-12 (#343) added Z88FIX-01/02 (`source_map_test`: the `test/fixtures/z88dk` files match
their manifest, and the manifest lists exactly those files).

E23-09 (#341) added SRCBP-01 to SRCBP-04 and SRCBP-06 to SRCBP-10 (`source_debug_test`: the live gate,
the probe and Reverse Continue on the exact fetch source, the DivMMC RAM and ROM cases, the group
verbs), SRCBP-05 (`dzrp_adapter_test`: the legacy qualifier through `CMD_ADD_BREAKPOINT`) and
QBPM-12/13 (`debugger_source_panel_test`: one row per group, the gutter removes one location).
QBPP-04, QBPP-06 and QADR-10 now assert the group row. The review round added SRCBP-11/12
(group verb refusals) and QADR-12, QBPM-14 to QBPM-18 (Add Execute with a paged name, Edit and the
Enabled cell on a group row, notification, the lowest location, another client's legacy-page row).
QADR-12 injects its paged label into the backend's table; QSYM-13 (#375 stage 2) is the same
dialog through the real SLD-label path.

E23-23 (#375) stage 2 added SYMPG-12 (`source_map_test`: `merge()`), SRCDBG-SYM-01 to SRCDBG-SYM-10
and SRCDBG-SYM-12 to SRCDBG-SYM-14 (`source_debug_test`: an SLD's labels reach the backend's table,
by page, through the sidecar and the hand load, with their ownership rules), GDB-MON-13
(`gdb_rsp_test`: `monitor sym` by page), SCRIPT-SLD-01 (`script_events_test`), QSYM-06 and QSYM-13
(`debugger_source_panel_test`: the Load Source Map message and Add Execute with an SLD label) and the
functional row `sld-labels-func`.

E23-10 (#342) added SRCLIFE-01 to SRCLIFE-39 (`source_debug_test`: the cache both ways, identity
checks at a state load, a `.sna`, an RZX start and a reset, stale user maps, the readers' first
call, `load()` reporting, Back across a landing, purity of the check), RB-GEN-01
(`rewind_test`), TRACE-SEQ-01 (`debugger_backend_test`) and QSRC-58 to QSRC-61
(`debugger_source_panel_test`).

E23-13 (#344) stage 2 added SRCDBG-Z88-01 to SRCDBG-Z88-14 and 18 to 22 (`source_debug_test`: the
sidecar, the hand load, precedence, identity, lifetime, real code), SRCDBG-Z88-15 to 17
(`source_debug_posix_test`: a FIFO), QSRC-62 to QSRC-64 (`debugger_source_panel_test`), QMAP-07
(`debugger_verbs_test`) and the functional row `z88dk-source-map-func`.

E23-13 (#344) added Z88MAP-01 to Z88MAP-28 (`z88dk_map_test`: the section rules against where
`appmake` put the bytes in each fixture's `.nex`, extents, zero-width lines, strict refusal,
identity from the section binaries, the symbol loaders' file-type refusal).

**Planned.** `z88dk_lis_test` (E23-17), and the functional rows `source-map-func` (a looked-up line, E23-15) and
`z88dk-source-func` (E23-15).

**Oracles.**

- The stores and the adapters have no VHDL counterpart: the oracle is the format's
  documentation, the fixture output and the rules of this document.
- The fetch identity is VHDL-oracled (the lines in 4.1 and 4.2).
- C stepping is checked against `z88dk-gdb` on the same fixture (E23-14).
- The z88dk space rules (9.4) are checked against where `appmake` put the bytes (the bank and
  offset of a symbol's bytes in the fixture's `.nex`), not against the map alone: the map
  shows only an address and a section, and a rule can fit those and still be wrong.
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

