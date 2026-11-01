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
(#348); `.cdb`/`.adb` input, E23-20 (#350, OD-2: z88dk writes no such file, section 9.6); dot commands, E23-21 (#351, OD-14); the
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
- No `.cdb`/`.adb` file on either tree (E23-20, section 9.6). No `.cdb`: sdcc's comes from
  its linker (`sdld -y`), z88dk has none and z80asm writes the map instead. 2.4: zsdcc runs in
  `--c1mode` over stdin and opens the `.adb` only with a source file name (`SDCCmain.c:1758`),
  so none is written. Master: zsdcc writes a temporary `.adb` (7e0bebd667), zcc folds it into
  the asm as `__CDBINFO__` (2bafa2baee, 29da3be373; only at `-O1` and above, the `+zx`/`+zxn`
  cfgs set `-O2`) and deletes it. No SLD and no DWARF in any z88dk branch: a search of the
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
  The helper pops its return address and pushes it back 2 bytes LOWER before it returns, so the
  frame's slot is below SP after the RET: this is why the tracker's RET rule matches return
  addresses (section 7), not SP alone.
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
  every frame whose `sp_at_call` was below SP on a RET, so the depth came back right only at the
  end, by luck: `banked_call`'s frame was lost at `set_mmu`'s RET, before `l_jphl`. E23-14 (#345)
  fixed it with the return-address rule (section 7): the depth is d0+1 for the whole trampoline,
  d0+2 inside the target, and d0 after the call. A newlib `+zxn` `__banked` program built with
  the defaults cannot run: `CRT_ORG_PAGE_20` is 16-bit, so the `defq` page byte is 0, and the
  stack ($FF58) is in the banking segment. It needs
  `-pragma-define:CRT_ORG_PAGE_20=0x14C000 -pragma-define:REGISTER_SP=0xC000` (the `-run`
  fixtures). z88dk-gdb differs on banked code by design: its `finish` stops in `banked_call`
  with no source line, its `next` hangs (it runs `nexti` to the `defq`), and it segfaults on a
  24-bit `__C_LINE_` value; jnext runs unmapped code through (section 2.5).
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

**Listing facts (GH #348, evidence in `test/fixtures/z88dk`).**

- F9. z80asm keeps the LAST `MODULE`: the classic CRT listing has zcc's `MODULE zxn_crt0_asm` and then
  `MODULE zxn_crt0`, and the map names `zxn_crt0`.
- F10. A false `IF` branch is listed exactly like a taken one, with its text and line number, only without
  bytes; the listing does not say which branch was assembled.
- F11. MACRO bodies are listed once, at their definition (`MACRO name`, `name MACRO`, `name: MACRO`, to `ENDM`).
  A macro invocation row carries all the bytes the expansion emitted, even those that went to another section;
  REPT bytes sit on the `ENDR` row.
- F12. A section piece's listed addresses start at 0 and are continuous; ORG does not change them, ALIGN and DEFS
  list their fill bytes.
- F13. With MACRO bodies skipped and the last MODULE used, the classic CRT places exactly (47 rows, every
  instruction's first byte equal to the binary); the start-up code is in the default section.
- F14. Line 0 is z80asm's file-entry marker (`C_LINE 0,"<file>::<func>::0::1"` after an `#include` following a
  function), never a source line.

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
| `*.lis` and `prog.map` | z88dk listings | `SourceMap` | done (E23-17 (#348)) |
| `prog_<GROUP>.bin` | program identity | `SourceMap` | done (E23-13 (#344)) |
| `--sym` / `--exp` files | EQU symbols | `SymbolTable` | #390 |
| `.cdb` / `.adb` | none: z88dk writes no such file; the same records are the map's `__CDBINFO__` | `SourceMap` | E23-20 (#350), 9.6 |
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
| z88dk | DONE (E23-13 (#344), stages 1 to 3; the `--list` listings E23-17 (#348)): the map gives symbols (`load_z88dk_map` skips the three debug prefixes, #352) and, through `load_z88dk_source_map()`, source lines; `<stem>.map` is a sidecar, Load Source Map... and `--source-map` take it, identity is checked from `prog_<GROUP>.bin`; tape programs attach at once and are judged per space where the CPU first runs them (6.2, stage 3) | 9.4, 9.5 | E23-13 (#344), E23-17 (#348) |
| Symbols | DONE (E23-23 (#375) stages 1 to 3): `SymbolTable` keyed by (memory, address); SLD labels reach the backend's table through the sidecar and the hand load; z88dk pages (stage 3): `z88dk_section_space()` places each symbol, rule-4 symbols are bound by `SymbolTable::bind()` | 10 | E23-23 (#375); EQU files: #390 |
| User labels and comments | none | 10 | E23-24 (#387) |
| Coverage | INS-20: one bit per executed 16-bit PC (`CoverageBits`) | 11.1 | E23-25 (#388) |
| Source steps in menus and keys | Debug > Source menu; Ctrl+F6/F7/F8, all rebindable (done in E23-05 (#337)) | 8.2 | E23-05 (#337) |
| Source tab | the PC line highlighted, the step buttons, "Running" while the machine runs; gutter with line numbers and click-to-break done in E23-28 (#421); right-click menu with Run to Here done in E23-30 (#423); syntax highlighting done in E23-29 (#422); the amber current-line wedge and the "also:" list done in E23-16 (#347) stage 1; selection sync both ways, browsing, Show in Source and Go to PC Line done in E23-16 (#347) stage 2 | 8.2 | E23-16 (#347) |
| Remote and script surface | GDB `monitor sym` and `monitor src`; DSL `on execute file:line`, and `on execute @name` in its label's memory; a `--script` naming a line, a symbol unknown at start or a load-bound symbol waits for the `--load` program, still parsed at once (done in E23-15 (#346)) | 8.3 | E23-15 (#346) |
| The identity in every view; remote vocabularies | none | 4.6 | E23-22 (#360) |
| C stepping on real z88dk code | DONE (E23-14 (#345)) | 7 | E23-14 (#345) |
| Dot commands | DONE (E23-21 (#351)): rule 5, `peek(DivMmcRam)`, a map judged at the command's entry, `--source-map` without `--load` | 9.4 | E23-21 (#351) |
| `.cdb` / `.adb` | none: no such file | 9.6 | E23-20 (#350), closed |
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
(one total switch, so `-Werror=switch` makes a future kind a compile error there), except that
E23-21 (#351) made `peek(DivMmcRam{b})` read bank b (`InvalidPage` above 15, a short read at the
end of the 8K bank is `RefusedUnavailable` with the count, INS-02-31/32); `poke(DivMmcRam)` is
still refused. Every route result has `kind <= 15` and
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
given the direction, never from a second copy of the order. As built (E23-25 (#388)):
`route_bits_<Live, Write>()` takes the direction at compile time, `Mmu::write_route(addr)` is the
public form, and `write_landed()` is that route followed by a store by the space's kind, so
`git grep` finds the priority order in `route_bits_()` only. The write direction differs
exactly where the VHDL does: no boot-ROM arm (`1856`, the `cpu_di` mux), the Layer 2 write
enable (`3077`), the alt ROM only with en+rw (`3078`, `3056`), and a write lands only where
`sram_rdonly` is clear (`3154`): the Multiface ROM half (`3035`), the DivMMC ROM and read-only
RAM (`3089`, `3097`, `divmmc.vhd:100`, which `DivMmc::Overlay::rdonly` now carries for both
`DivMmc::write()` and the route) and a ROM or unmapped slot are dropped (`ptr == nullptr`, the
space still named). If inlining misses the bench threshold (4.7), the fallback goes to the
owner, never to a second arbiter.

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

**The verify window (E23-13 (#344) stage 3, 6.2).** `DebugState::verify_hold_` is one more `armed()` term,
folded into `refresh_gates_()` when a window is set or cleared (never per instruction), and one test in the
armed block, `verify_window_hit(pc)`: a bool and two compares, before the breakpoint tests. With no client
and no tape map the machine is unarmed and nothing runs; a client already armed it, and then the cost is
that bool. A headless run with a map pending pays the armed block's tests only from the tape load call to
the program's first instruction in its code, then nothing. Measured on the non-PGO build, base (stage 2's
tip e17a21b79) against branch, on a shared host (two qemu guests ran throughout, 1-min load 5 to 13 on 12
CPUs): `make bench` was VOID for four of five workloads on spread (16% to 77% between runs of one binary),
so the figures are the user-space instruction counts of `perf stat -e instructions:u`, which load does not
move, pinned to core 0, `--headless --benchmark`:

| workload | base | branch |
|---|---|---|
| `48k` 600 frames | 14,005,972,689 | 14,001,665,685 (-0.03%) |
| `next` 400 frames | 25,572,238,574 | 25,567,440,750 (-0.02%) |

A pending-case figure, for information: `bifrost.tap` with a map beside it that arms a window (dropped
by its first mismatch about 130 frames in), 48K, 600 frames: 14,198,198,778 instructions against
14,157,419,521 with no map (+0.29%, window armed for the first part of the run only). An interleaved
throughput A/B (9 runs each) gave best-of-9 differences of -1.9% to +5.0% tstates/s in both directions on
the four unarmed workloads, which is the noise of that host; the medians swung up to 25% between two runs of
the same code.

**Memory coverage (E23-25 (#388)).** Off must cost nothing measurable; the gate is the interleaved A/B of
`test/bench/ab-hotlatch.sh` with `AB_SET=coverage` (B = the epic tip 3a4dbb3da, U = this branch with
nothing armed, V = `--coverage`; 5 pairs, five workloads, one pinned core, run on a quiet host with
the SMT sibling idle; commit `ac12b723b`, result `test/bench/coverage-ac12b723b.txt`), plus `perf stat -e
instructions:u,cycles:u` on the same core. The owner accepted the PGO A/B as the gate. Median tstates/s on the
shipped PGO build (`make gui-release`, each tree trained on its own profile):

| workload | B | U (off) | V (`--coverage`) | U vs B | V vs B |
|---|---|---|---|---|---|
| boot-48k | 89,852,692 | 89,736,366 | 83,005,444 | -0.1% | -7.6% |
| boot-nextzxos | 227,943,656 | 227,673,265 | 210,791,154 | -0.1% | -7.5% |
| copper-demo | 98,383,048 | 97,986,997 | 94,847,384 | -0.4% | -3.6% |
| beast | 92,408,246 | 91,850,012 | 89,654,608 | -0.6% | -3.0% |
| bifrost | 84,614,016 | 84,277,878 | 80,273,863 | -0.4% | -5.1% |

(copper-demo's B spread is VOID, 39.6%: the first run of every copper-demo series is a cold-start outlier, seen
on the base tree alone three times; the other four workloads have spreads of at most 4.2%.) Counted on the same
PGO builds: `instructions:u` U vs B -0.03% (48k, 600 frames), +0.06% (next, 400), -0.07% (bifrost); `cycles:u`
+0.30%, -0.51%, -0.61%. With `--coverage` on, V costs a further 3-8% in throughput (+4.6% to +10.0% instructions).
So in the shipped build **off costs nothing measurable**. The first design (hooks fed from the route) cost +0.58% and
+0.76% in `instructions:u`; re-deriving the route out of line in the cold hook brought that to +0.06% and +0.08%.

The non-PGO development build (`gui-release-non-pgo`) is different and is stated plainly: at the same instruction counts
(+0.06%, +0.12%, +0.02%) U takes **+5% to +7% more cycles on boot-48k and bifrost and about +1.5% on beast** than B
(min-of-7 `cycles:u`, B 3.007G / 2.912G / 18.46G cycles; U +4.9% / +5.5% / +1.4%), and +0.1% on boot-nextzxos. Counters
exclude store-to-load forwarding, 4K aliasing or misalignment, mispredicts and instruction-cache or D-TLB misses
(all equal or better in U); what grows is frontend starvation (`no_ops_from_frontend` +17%) and L2-BTB corrections
(`bp_l2_btb_correct` +78%), spread over functions this change did not edit (`render_row`, `AyChip::tick`,
`tick_devices_after_instruction`) as well as `Mmu::read_m1` (+99 of 1,074 extra samples). Three code-padding variants
(16, 48, 112 bytes inside the never-hot cold hook) all stay at +5% to +7%, so the gap is not a one-off alignment. We attribute it to code
placement in the non-PGO LTO link (the function order is the linker's; PGO orders by hotness), **not proven**.
That is why the gate is the PGO build, and why a non-PGO A/B on this tree reads 5% slower on the 48K workloads
whatever the instruction count says.

## 5. The source map and the backend API

### 5.1 The store (D2)

- `files[]`: the paths as the build spelled them, interned, with a cache of resolved paths.
- `locations[]`: `{file, line, column (0 = none), function (optional)}`. A format without
  columns writes 0; sccz80 and 80cc give the function, sdcc writes `x`.
- `ranges[]`: `{space, lo, hi_excl, location}`, sorted by (space, lo); overlaps are allowed.
- `by_line`: (file, line) to range indices (several, disjoint). `aliases`: (file, line) to a
  range, for the zero-width lines of G3.
- An optional program identity (9.7).
- `texts`: source files the toolchain did not keep, taken from a listing (`SourceMap::embedded_text()`, keyed by
  the normalised name; GH #348): the m4 output of z88dk's start-up code, shown by the Source tab labelled
  `(m4 output, from prog.lis)`.

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
| `load_source_map(path, accept_identity_mismatch, verify_at_first_execution = false)` | load a source map by hand; refused on a parse error, or on an identity mismatch unless accepted; the path is stored absolute | DONE (E23-13 (#344)): the format is told by content, a z88dk map fills the source half only; the third parameter (stage 3, a tape program) attaches a map with an identity pending, judged per space where the CPU first runs it (6.2); `SourceMapStatus::pending`, `SidecarLoad::sources_pending` |
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

`.tap`, `.tzx` and `.wav` programs load over the frames after the load call returns, so there
is no point at which the program is in memory when the sidecars are attached. Owner decision
2026-10-29 (E23-13 (#344) stage 3): a source map attaches to a tape program **at once**, and
its identity is judged **per memory space, the first time the CPU is about to execute inside
that space**. This holds for the three ways a tape gets a map: `<stem>.sld`, the source half
of `<stem>.map`, and `--source-map` with a tape `--load`. (The hand load is unchanged: it
checks at once, with the program already in memory.) Symbols and an SLD's labels attach with
their map, as ever.

- **Attach.** A map that carries an identity (an SLD hash, z88dk byte checks) is attached
  PENDING: it works at once (the Source tab, `file:line` breakpoints before the tape has
  loaded), `SourceMapStatus::pending` is set, the Source tab says it is not verified yet, and
  the log line ends `(identity check pending: the program loads from tape)`. A map with no
  identity attaches unverified and nothing is waited for (OD-6). Rule-4 sections (9.4) bind at
  the attach, to the page the MMU maps at the load call: that is where the ROM loader puts the
  bytes (48K fixed; Next MMU2-5 = 10, 11, 4, 5). A program that pages its code elsewhere
  fails its identity and is dropped, with the reason.
- **Units.** The identity is split into units: each memory space a z88dk byte check lies in,
  and, for an SLD's SHA-256 (which hashes logical memory as one), a single unit. The spans of a
  unit are the code the check covers. A unit is judged the first time the CPU is about to
  execute an instruction inside one of its spans **in that span's own space**
  (`predict_fetch(pc)`), so a bank the tape loads after the program has started is judged when it
  first runs, not before, and a page that merely shares the logical address does not trigger it.
- **Verdict.** The unit is judged with the bytes exactly as they are about to run. A match
  clears it; when the last unit matches the log says `Verified ...`. A mismatch in ANY space
  drops the map with an error (`Rejected ... does not match its binary identity`): a sidecar
  with the symbols that came with it, the user's map to `None` (not `Stale`) with the reason
  kept. A sidecar z88dk map's symbols answer to the map's identity even when the user's source map is the one
  kept. With `--source-map` the run exits 1.
- **Why not one check.** A tape whose own code loads a further bank after it has started would
  have the whole identity fail at the first execution, which is the owner's per-space rule.
- **A reset before the program arrived** drops the pending map: the program will not arrive.

How it runs is 4.7 (the window) and 6.5 (the lifetime). The symbols of a tape's `<stem>.map`
bind at the attach with the same function as its source records (E23-23 (#375) stage 3, #344
D17): they are live while the check is pending, and a deferred mismatch in any space drops them
with the map. The user's map (`--map`, Load MAP) is bound again at the tape load call.

**Dot commands (E23-21 (#351)).** A z88dk dot command's map (9.4 rule 5) is not a tape's, but it
shares the pending machinery: the guest loads the command into DivMMC RAM bank 2 when the user
types it, after any number of resets, and every dot command shares that bank. So a dot map with
an identity is deferred whatever the caller passes for `verify_at_first_execution` (the hand load,
`--source-map`, with or without `--load`, or with an RZX replay), so it attaches pending and its log line ends
`(identity check pending: the dot command loads when it runs)`; the one exception is a hand load made while
the CPU is already running the command from bank 2 with its bytes in place, which judges like a landing there
(6.5(3)) and is verified. The DivMMC unit is judged at the command's ENTRY,
when the CPU is about to execute $2000 fetched from `DivMmcRam{2}`, with the bytes about to run,
not at the first run of a span. A match verifies it (`Verified ... the dot command in DivMMC RAM
bank 2 matches its binary identity`) and arms the other units (a dotx's main part, `Page{4}`) for
the tape rule: judged at their first execution, a mismatch drops the map, since the same run's
dot matched. A mismatch at the entry means another dot command is in the shared bank: nothing is
dropped, the map stays (or goes back to) pending, and one Info line says so (`The dot command
started in DivMMC RAM bank 2 is not the one source map '...' describes; it stays pending`), not
repeated while it stays pending. A later mismatching entry puts a verified map back to pending.
The check is never finished while the map is attached. The Source tab says `Source map not
verified yet: it is checked when the dot command runs`.

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

3. **Dot commands (E23-21 (#351)).** A landing never drops or stales a dot map (6.2): a reset puts
   the CPU elsewhere and NextZXOS's own boot resets the machine long before the user types the
   command. A FRESH landing (reset, state load, program load, cold boot) leaves the map pending until the next entry
   (its earlier records are kept). A RING landing reads back what the original run
   showed at that instant: the check records its state (entry verdict, units still to judge) with
   the clock cycle and load generation at each change, and a landing to cycle T takes the last
   record of that generation strictly before T (a verdict at C applies to the instruction about to
   run at C, which a landing at C has not run), else pending. So a rewind into a dotx's main part, or
   into a plain dot's code outside bank 2, shows the map verified, as it was, and one to before
   the entry shows it pending. A hand load of a dot map made while the CPU is already running
   inside the command (paused in bank 2 with the bytes matching) judges like a landing at that
   point: verified. Every entry judgement in bank 2, matching or foreign, is recorded, and a record erases those at or after
   its cycle, so a re-run that enters the bank replaces what the first run recorded from there on. The records of
   earlier generations are kept (a rewind back across a reset reads them; the ring cannot rewind across a state load, and SRCLIFE-08 refuses Back across one; the generation key is for the state load, which restores an earlier clock, so a new run's cycles can fall below the old run's records); the cap of 256 covers all
   generations together, the oldest going first, and a landing older than the oldest record reads pending. The
   limit that remains: a re-run that makes no entry judgement in bank 2 and judges no unit leaves the abandoned
   run's later records in place, so a rewind to a cycle past the divergence reads the abandoned run's state until
   a later judgement replaces it.

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
- **Pending checks (E23-13 (#344) stage 3).** The units of a tape map still to judge
  (`Impl::DeferredCheck`, 6.2) are part of the attachment: the cache records them
  (`Attachment::checks`), updated at every unit judged, so a rewind into the generation finds the
  map with the spaces judged so far and the window re-armed for the rest, and a rewind to before
  the load clears both. A landing that is not a ring restore judges a pending map WHOLE
  against the memory now (a match clears it; a mismatch, or a reset before the program arrived,
  drops it or makes the user's stale, as for any carried map). A user's pending map is carried
  into the next program's load still pending. The verdict the hook records is applied by
  `recheck()` before it does anything else.
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

The one thing the backend cannot see about a DOT command is which command is in the shared DivMMC
bank 2. A source breakpoint on a dot map's lines is a subscription on `DivMmcRam{2}` (8.1), so it
also stops another dot command that runs code at that address; the map then reports itself pending
again (6.2), which is the visible sign. Not gated (E23-21 (#351), 351-D9).

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
(#347) measured the worst of its five scenarios on the Release build (non-PGO, LTO, 2026-10-31,
pinned core, load under 1.0): 275 ms median (call stack, trace, rewind and a subscription on;
100 frames of a HALT loop), 147 ms with none of them, so the bound stays synchronous. Not
measured: a step that meets both bounds together, and a frame with Layer 2 and sprites on.

**Call depth** is the call-stack tracker's (`set_call_stack_enabled`), which the Qt debugger
runs while its window is open and which follows rewind (#332, v1.1.46). E23-14 (#345) fixed it
on real z88dk code across `banked_call` (G9), the sccz80 `l_debug_push_frame` and
`l_debug_pop_frame` helpers (G5), RST $08 esxDOS calls, `__z88dk_callee` routines and a tail
`JP`, in `CallStack` for every program, never as a special case for `banked_call`. The rule: a
frame ends when a RET lands on its own return point, the NEWEST frame only: a CALL frame's is
`caller_pc + 3`, an INT or NMI frame's is the interrupted PC (`+ 1` when the interrupt ended a
HALT). Any other taken RET ends every frame whose return slot is below SP (a `PUSH; RET`
computed jump leaves the frame). RST frames end by SP alone (an inline `DEFW` makes their return
point vary); the esxDOS shim's return (a slot that fetched no opcode) ends the RST $08 frame it
serviced and only it. Trade-off: a routine that drops its return address and leaves by JP
(`POP HL; JP (HL)`) looks like `banked_call`, so its frame stays on the stack until a RET ends
it by SP. Out from a function lands at the return
address, mid-line on the call's own line, as gdb's `finish` does.

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
Clicking a Source tab line toggles its group (E23-28 (#421)); a click on a gutter dot removes only that
location. As built: the group verbs are `Debugger::subscribe_source_line(by, file_line, like)`,
`unsubscribe_group(by, group)` and `set_group_enabled(by, group, on)`, with `SubscriptionInfo::group`;
the model folds a group into one row (`Row::group`, `Row::locations`), enabled only while every
location is; a `space` on a kind other than `Execute` is refused.

### 8.2 Qt

- **Source tab** (`src/debugger/source_panel.*`): the PC's file with the PC's line highlighted.
  It follows the PC at every stop and says "Running" while the machine runs (done in v1.1.64).
  The file is found from the recorded name, absolute or else relative to the map's directory
  or one of its parents, and reloaded when it changes on disk. A refused sidecar says why in
  the tab. E23-27 (#420) adds a source search path after those two steps (Preferences > Paths,
  `--source-dir`, `paths.source_dirs`), tried longest trailing part of the name first, and a
  **Locate File...** button that adds the directory of the file the user picks.
- **Source gutter** (E23-28 (#421)): `BP | Line | text` with a header, the Disassembly's
  glyphs from one header (`breakpoint_glyph.h`); a click toggles the line's group through
  `SourcePanel::toggle_breakpoint_at_line()`; a half glyph marks a line only some of whose places
  carry a breakpoint. Rows QSRC-79..98.
- **Syntax highlighting** (E23-29 (#422)): `source_highlighter.*` colours C, Z80/Z80N assembly and Boriel ZX Basic,
  chosen by extension (`.m4` stripped) and then by the first 200 lines; assembly inside C and Boriel is coloured as
  assembly. Colour only, and the current-line selection stays on top. Rows QSRC-120..137, 139 and 145..149. Stage 2 gave it the
  owner's ten classes (Text, Keyword, Instruction, Preprocessor, Label, String, Char, Integer, Float, Comment; punctuation and
  operators are Keyword) and four schemes, Auto, Light, Dark and Borland C++ 3.1 (`source.colour_scheme`, Preferences > Debugger),
  the background and the current-line bar being the editor's palette. Rows QSRC-190..199.
- **Menu and keys** (E23-05 (#337)): a Debug > Source submenu (Step Into, Over, Out, Back,
  Reverse Continue, Load Source Map..., Load Symbols (Memory.txt)...), each action bindable in
  Settings > Preferences... > Debugger Keys like the other debugger actions
  (`src/debug/debug_keymap.cpp`). Decided by the owner on #337 (2026-10-22): source Into,
  Over and Out default to Ctrl+F6, Ctrl+F7 and Ctrl+F8; Back and Reverse Continue ship
  unbound; the emulator window does not forward them. Done.
- **Run to Line** moved to E23-30 (#423), with the Source tab's context menu: `Debugger::run_to_source_line` (a
  transient group at every start, then a run), reached by Run to Here and Enter. Done.
- **Source tab completion** (E23-16 (#347)): the current-line marker; the
  "also:" list of the other locations at the PC (OD-7); selection sync between the Source tab
  and the disassembly. Stage 1 built the marker (an amber wedge in the gutter's trailing 5-px
  strip, `paint_pc_marker()`, D1) and the "also:" suffix of the position label (D3). Stage 2 built
  the selection sync as tints in both views (D5, one pale violet from `source_sync_colors.h`, a
  line's instructions being those with ANY location on it, D6), the Disassembly's reveal only for a
  move the user made in the Source tab (D7), browsing (D8: any Disassembly selection whose primary
  line is not the PC's shows that line, in its file, with `(browsing; the PC is at f:l, $addr)`,
  until a stop, a run, another map or **Go to PC Line**), and **Show in Source** in the
  Disassembly's menu while a map is loaded (D10).
- **Address fields**: a number first, `@` forces a symbol, `$` or `0x` force hex (OD-12; done).
  `file:line` is accepted wherever an Execute address is (done). Call Stack "Called from" uses
  the caller's recorded identity (E23-08). E23-22 (#360) shows the identity in every view, and
  E23-24 (#387) edits labels and comments in the disassembly.

### 8.3 Remote protocols and scripts

The rule of 4.6 governs every protocol: each carries as much of the identity as its vocabulary
allows, and none is extended.

- **GDB.** `z88dk-gdb` maps sources itself, so nothing is required. E23-15 (#346) added (as built:
  `$8CB8 main.c:19 P04` / `$8CB8 unmapped P28`, primary location only, a symbol accepted, the PC when
  no argument; the memory spelling is an interim helper that #360 replaces)
  `monitor src [ADDR]`: the location and the space of an address, else "unmapped", which makes
  the source map observable in headless and SDL. `monitor sym` exists. E23-22 puts the identity
  in its text form into `monitor src` and `monitor mmu`.
- **DZRP and ZRCP.** DeZog maps sources from `.lis`, `.map` and `.sld` itself, so there are no
  new commands. Banked breakpoints keep the legacy qualifier (4.5); the slot views are as in 4.6.
- **DSL.** `addr_spec` accepts `file:line` (E23-15), resolved at load through the backend like
  `@name` (as built, GH #346: bare, or quoted for a name with spaces; `execute` only, no range, no
  `page`; with a CLI `--load` the script, and the ones after it, load once the program and its map
  are in; `on execute @name` takes a paged label's own memory, an explicit `page N` keeping its
  legacy meaning). An unresolvable one is a load error, and a multi-location line arms every start.
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
half that is strict: any malformed record refuses the half, and the reason names the line. The one
exception is a z88dk `--list` listing (9.5): a listing found beside the map that is malformed, is
not this build's or does not hold up against the map or the section binaries drops that listing
alone, with a note naming the file, never the half; its module keeps the map's records. The
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
   the name without knowing the version. The same holds for `__PAGE_<n>_head`: `head` is its
   low 16 bits, and a head up to 24 bits is accepted (newlib puts the page there for
   `__banked`, E23-14 (#345)).
4. Any other section (`CODE`, `code_*`, the main program): the RAM page the MMU maps at that
   slot with the program in memory. For a NEX that is banks 5, 2 and 0, the pages 10, 11, 4, 5,
   0 and 1 for $4000-$FFFF, so $8000-$9FFF is `Page{4}`. A map read before the program is in
   memory (`--map`, `--source-map`) takes its rule-4 spaces at the program load, not when it is
   read: `--map` is read when the script host starts, before the `--load` countdown applies
   the program. The adapter binds a rule-4 record with the `space_of` function its caller
   gives (the backend passes `source_page()`, the identity lookups compare against), and the
   space must be a `Page`: an address the MMU maps to ROM refuses the half. The symbol half is
   lenient: a rule-4 symbol is stored unbound (`bind_at_load`), `SymbolTable::bind()` gives it
   the RAM page `source_page()` names at its address, and any other space leaves it unpaged. For the symbol half an empty section (z80asm's default section) is rule 4 too; only the source half drops such G2 lines.
5. A z88dk DOT COMMAND (E23-21 (#351), OD-14), checked before rule 4. A map is a dot command's
   when its const `startupoffset` is $0100 (`-subtype=dot`), $0200 (`dotx`) or $0300 (`dotn`):
   zcc defines it only for those subtypes (`lib/config/zxn.cfg`, `-startupoffset=0x100|0x200|
   0x300`, the same on 2.4 and at 39421a01cd), newlib and classic alike, and a `-subtype=nex`
   map has none. Its `__CODE_head` must then be $2000 (the file is assembled to run at $2000);
   otherwise the half is refused saying so. Not the address range ($2000-$3FFF is ROM, or
   whatever a NEX maps there, at the attach) and not `__crt_org_code` (newlib only).
   NextZXOS copies the file into a DivMMC RAM bank and runs it from $2000 there, with
   port 0xE3 = $82 (VHDL `divmmc.vhd:89-96`: `conmem` makes the bank `port_e3_reg(3:0)` the
   $2000-$3FFF page; SRAM page 0x10 + bank, `zxnext.vhd:3092-3098`). The bank is not in the map.
   **Measured** (jnext booting NextZXOS, 2026-10-31, scratch runs in the issue): for a plain dot,
   a dotx and a dotn, the file lands in bank 2 (E3 alternates $02 and $00 during the copy,
   then `E3 <- $82` and the CPU executes $2000 with the file's bytes); dotx and dotn need the
   crt's `CLEAR 32767` first. This agrees with the NextZXOS API document (a dot command is
   assembled at $2000 and loaded into DivMMC RAM; NextZXOS reserves the first six DivMMC banks).
   NextZXOS's 48K personality and esxDOS proper were not measured. In a dot or dotx, a section
   that names no page with an address in $2000-$3FFF is `DivMmcRam{2}` (`kNextZxosDotBank`,
   `z88dk_space.h`); a dotx's other such sections (its main part, at `CRT_ORG_MAIN` $8000) are
   rule 4: the MMU's page, `Page{4}`/`Page{5}` under NextZXOS (measured). A dotn is REFUSED
   (owner decision 351-D3, 2026-10-31): its main bank and its `PAGE_<n>` / `DIV_<n>` sections are
   pages NextZXOS allocates when the command runs (`__z_page_table`, `__z_div_table`;
   measured $DC $DB $DE $DD), so a map cannot place them; the source half is refused with that
   reason (build it as a dotx to debug it), and the symbol half skips what it cannot place.
   `DIV_<n>` and `<KIND>_DIV_<n>` outside a dotn stay `Unpaged`: z88dk's `appmake` only writes
   them as raw `prog_DIV_<n>.bin` files for the program to load wherever it chooses, so the
   number is a logical bank, never `DivMmcRam{n}`. A `PAGE_<n>` section with no `__PAGE_<n>_head`
   and a section number out of range refuse the source half (there is no unqualified record,
   D3). The detection is ONE function (`z88dk_dot_command()`), and every consumer of
   `z88dk_section_space()` passes its result: the source half's records, extents and section
   binaries, the symbol half, and the `.lis` placement (10.4).

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

As built (`src/debug/z88dk_lis.*`, `z88dk_source_map.cpp`). The listings beside the map are `<map
stem>.lis` (the start-up code) and `<file>.lis` for every file the map's module symbols name. Each is
interpreted on its own, and a listing that does not hold up is dropped alone, with a note naming it: its
module keeps the map's records (never the half). The listing wins over the map for a module it covers, the
reverse of DeZog; the map's line records are cross-checked against it under z80asm's first-occurrence rules and
a disagreement is a warning.

- B1. The listing's module is its LAST `MODULE` line outside MACRO bodies.
- B2. MACRO definition bodies (three forms, nesting counted, text before a `;` only) are skipped.
- B3. An invoked macro whose body, transitively, switches section (SECTION, PHASE, DEPHASE) drops the listing.
- B4. A REPT body that does the same drops the listing; otherwise the `ENDR` row is an ordinary byte row.
- B5. False IF branches are not modelled (F10): their directives are obeyed.
- B6. Continuity guard: a byte row outside PHASE whose listed address is not its section's pc (an unseen section
  starts at 0) drops the whole listing, with the line, the section and both addresses.
- B7. The default section is an ordinary section (anchored by the map's symbols in it, placed by 9.4's rule 4).
- B8. A map line record of line 0 is "no code" (the map half and the cross-check); a C_LINE marker of line 0
  attributes nothing.

Relocation is one offset per (module, section piece), from the labels whose map symbol is defined at that very
line; all must agree, and a label-less piece that is its section's whole content starts at `__<s>_head`. Every
byte row is a range of its length, capped at its 8K page. The line source is `C_LINE` when the listing has it, the
`;file:line:` comments for a C module otherwise (sdcc 2.4), z80asm's own file and line for assembly.

### 9.6 `.cdb` / `.adb` (E23-20 (#350), OD-2)

z88dk writes no `.cdb` and no usable `.adb` (zcc 2.4 and master 39421a01cd, `+zxn`/`+zx`,
`sdcc_iy`/`sdcc_ix`, `-debug`, `-c`, `-S`, `-no-cleanup`, `-Cs--debug`; no file in the build
dir; on master only a temporary one in `$TMPDIR` that zcc deletes):

- No `.cdb` exists: it is sdld's product and z88dk has no sdld.
- The `.adb` is a temporary file on master only (7e0bebd667). zcc folds it into the asm
  (`zsdcc_embed_adb()`, `zcc.c:2041-2072`, 2bafa2baee) and deletes it (`zcc.c:3988`); only
  `-no-cleanup` leaves it. On 2.4 zsdcc writes none (`SDCCmain.c:1758`).
- The folding runs only at `-O1` and above (`if (peepholeopt)`, `zcc.c:1551`; the default is 0,
  the `+zx`/`+zxn` cfgs set `-O2`). At `-O0` the map has no `__CDBINFO__` and the `.adb` is
  deleted unread, so there are no CDB records at all (a limit of #353's input).
- Its records (`M:`/`F:`/`S:`/`T:`, no addresses, no lines) are the map's `__CDBINFO__` symbols
  once zcc's rename of the temporary module name to the source name is applied (`M:`,
  `F<module>$`, `L<module>.` carry it): equal one for one, 16 = 16 on four sdcc builds. The
  map has the real name where the `.adb` has the temporary one. The rename covers only `/`
  (`zcc.c:2052-2053`; sdcc mangles every non-alphanumeric, `SDCCmain.c:1719-1720`), so a
  `TMPDIR` with non-alphanumerics other than `/` and `_` (sdcc leaves `_` as is) leaves the
  temporary name in the map (#353 scope keys).
  Line addresses come from the map's `__C_LINE_` (E23-13) and the `.lis` (E23-17).
- So a `SourceMap` adapter has nothing to read. The CDB-record reader is #353's (`VariableStore`),
  fed from the map.

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
  `read_space`. Beside a tape the same bytes are judged later, one memory space at a time
  (`SourceMap::verify_spans()`, `verify_unit()`: the spans of a space are its byte checks
  merged), the first time the CPU is about to run inside it (6.2).
- The `.lis` bytes are a check on the listing, not an identity of the program. References to
  other modules are unrelocated in a listing (`cd0000`), so only the FIRST byte of an
  instruction whose first token is a Z80 or Z80N mnemonic is compared with the section binary
  covering its address (operands are not). A mismatch drops that listing (9.1, 9.5), with a
  note naming the line, the address and both bytes; the listing marks no byte as final. The
  ranges a used listing supplies then get the same byte checks as the map's own (the binary's
  bytes, above).

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
UI, and the rule for a name already used at another key are E23-24's. After both layers, the
toolchain layer's by-name read also answers to the C name of a z88dk C symbol (10.6).

### 10.4 Page rules per adapter

- **SLD `L`** (E23-23): `page >= 0` gives (`Page{page}`, `value & 0xFFFF`). `page = -1` gives
  an unpaged label ("stay unpaged", #375). A value that does not fit 16 bits (an EQU can be
  32-bit), or is negative, is skipped; only a `+struct_def` record (a struct's offsets and size)
  is a name-only value. sjasmplus writes -1 for every `EQU` without an explicit
  page, address constants included (2.1), so the format cannot tell an address from a
  constant; making them labels agrees with the `--sym` file, which lists the EQU constants
  too. A record with no main label (a module marker) names nothing.
- **z88dk map `; addr` symbols** (E23-23, #375 stage 3): the space by 9.4's derivation, the same as the map's
  source records, so that one file has one derivation for both stores; both halves take a page's
  head from `z88dk_page_head()`. The loader reads the file in two passes. Paged: a `BANK_n` or
  `PAGE_n` symbol is a label of its page at its low 16 bits (24-bit classic values included).
  BindAtLoad: stored with a flag and no memory; the backend's `bind()` gives it the RAM page
  mapped at its address, with the one function the source half uses, at `load_map()`, at the
  sidecar attach (a tape too) and, for the user's store, at every program load. Unpaged (DIV, a
  head-less PAGE, a number out of range): an unpaged label when the value fits 16 bits. Refused
  (24 bits in a section that names no page) and a value past 24 bits are skipped. `; const` stays
  name-only, 16 bits (a wider constant is no name). A symbol with an empty section is rule 4 (BindAtLoad); only the source half drops those lines (G2). A dot command's map (9.4 rule 5, E23-21 (#351)) gives its $2000-$3FFF symbols `DivMmcRam{2}` and a dotn's main-bank symbols are skipped (Refused), by the same `z88dk_dot_command()` as its source half.
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

### 10.6 C names (E23-31, #424)

z88dk's compilers write every C identifier with a leading `_` (sccz80 `Z80ASM_PREFIX`; sdcc and 80cc
the same), so the map lists `_counter` for `int counter;`. The user types and reads `counter`.

- **Which symbols (D1).** A `; addr` symbol that was placed, whose name is `_` followed by a
  character other than `_`, and whose module ends in `_c`. zcc heads every file it assembles with
  `MODULE <file name, non-alphanumerics to _>` (`zcc.c`), so a `.c` file is `<file>_c` and an `.asm`
  file `<file>_asm`; every fixture, compiler and install agrees, with or without `-debug`. Constants,
  `__x`, module-less lines and other modules are not C. Rejected: every `_x` (it would rename
  C-callable asm such as `_asm_bump`, the library's `_memset`, an asm program's own `_loop`), the
  location field (it varies by compiler and flags), the CDB records alone (absent without `-debug`).
  Known limit: C compiled from `.c.m4` or `.i` has another module name and keeps its assembler names
  (they still resolve). The owner may prefer that asm functions callable from C also answer to `x`.
- **Where (D8).** `SymbolTable` marks each C label (`SymbolLabel::c_symbol`) and keeps the key of the
  first C symbol of each C name (`c_name_to_key_`, filled by the one z88dk reader, so the sidecar, Load
  MAP and `--map` all get it). The rule is per SYMBOL, not per assembler name: a C `_dup` and an asm
  `_dup` are two symbols, and `dup` is the C one in either file order. `find_name()` falls back to
  `c_name_to_key_` after every exact name and constant; `lookup()`/`nearest()` show a C label's C name.
  No panel has code of its own, except the Disassembly copy and the watch label.
- **Same C name twice.** Two C modules with a local `_inc`: `inc` resolves to the first (file order, as
  every name); the second shows its assembler name `_inc`, so a shown C name always resolves to the symbol
  it labels. (`_inc` itself is the first one's, as before: equal assembler names cannot be told apart.)
- **Ambiguity (D2).** An exact name always wins, in any load or merge order. A C symbol whose C name
  belongs to something else (a label, an alias, a constant, an earlier C symbol) is shown by its
  assembler name, so every shown C name resolves to the symbol it labels. Storing the C names as aliases was rejected: the map is sorted by name, `_counter` comes
  before `counter`, and first-wins would let the alias beat the real symbol.
- **Copy (D4).** The Disassembly copy writes the assembler name (`lookup_assembler()`, through
  `symbol_namer(..., true)`): the user guide promises that a copy assembles, and z88dk's assembler knows
  `_counter`, not `counter`. The painter and the context menu show the C name.
- **Views (D3, D5).** Only the C name is shown (as `Memory.txt` does for Boriel); an auto-filled watch
  label is the shown name.
- **Not touched (D7).** SLD, sjasmplus `--sym`/`--exp`, simple maps and `Memory.txt`. A reader of the
  map's `__CDBINFO__` records (#353) should reuse `SymbolLabel::c_symbol` and `c_name_to_key_`: its
  records give the C names directly. (#350 is closed: z88dk writes no `.cdb`/`.adb`; see 9.6.)

## 11. Coverage and reverse-engineering interop

### 11.1 Coverage (E23-25 (#388)): opt-in and benchmarked

Coverage is off by default. When on, it records executed, read and written bytes per space
(`FetchSource`: RAM pages, ROM, the overlays). The execute side comes from the recorded fetch
identity (E23-08); the read and write sides from the same arbiter, given the access direction
(4.2). It is shown in the disassembly and memory panels, with clear and reset, and it is
E23-26's automatic code/data map. Off must cost nothing measurable; off and on are measured with
the interleaved A/B `AB_SET=coverage` (PGO build is the gate, owner decision 2026-10-31; the numbers and the non-PGO
finding are in 4.7). E23-25
decides and pins the rewind rule (no double count), and whether INS-20's logical bitset stays
beside it.

**As built (E23-25 (#388)).**

- *What is recorded.* One byte of flags per byte per space (`dbg::MemCoverage`, lazily allocated
  8 KB chunks keyed by kind, index and half; at most 6 MB when every space is touched, and 246
  spaces, about 2 MB plus the 64 KB chunk table, after 500 frames of a NextZXOS boot, which writes
  across nearly every RAM page): X is the
  first opcode byte of an executed instruction (set by `read_m1()` from the route it stores as
  `last_m1_`; a prefix's inner M1, an accepted INT or NMI and a serviced esxdos shim mark
  nothing), R every other memory read, operands and the DMA's included, and W a write that
  landed. A `Floating` route is not memory and a dropped write marks nothing. Only guest accesses
  count, exactly what a watchpoint can see (inside `GuestExecutionScope`, outside
  `InspectionScope`): the CPU, the DMA and the esxdos shim's own stores; never video (it reads
  the BRAM and SRAM through other ports, `zxnext.vhd:6558-6575`, `1666`), the Copper, `peek`,
  `poke`, the trace and call-stack captures, or a loader.
- *Cost off.* `DebugState::mem_gate_live()` is `wp_live || mem_cov_live`, one cached bool
  replacing `watchpoints_live()` as the first test of the Mmu's read and write sites, so with no
  client the hot path is the load and branch it was; behind it the coverage sink pointer is
  tested before the watchpoint mask. `watchpoints_live()` is unchanged for the port sites. The
  coverage hook re-derives its route out of line (`Mmu::cov_note_`), so the hot read and write sites
  compute no space or offset they do not use. Measured result in 4.7: no measurable cost in the
  shipped (PGO) build; the owner accepted that A/B as the gate (2026-10-31).
- *The rewind rule.* Coverage is a set of what this session's machine did, backend state beside
  INS-20's (the sink is re-published at the one re-application and retired by `~Debugger()`): a
  rewind neither removes nor adds a mark, a replay sets the same bits again (no double count by
  construction), and a reset, a state load and a cold boot keep it. Only Clear and switching off
  empty it. It is not in the rewind ring or a `.jns`, so the format is unchanged.
- *INS-20 stays.* The logical-PC set is returned by ZRCP `cpu-code-coverage` (the ZEsarUX
  contract DeZog reads) and the logical address a page ran at cannot be rebuilt from a physical
  byte; a shared switch would make ZRCP users pay for the read and write hooks. INS-21-15
  cross-checks the two on a fixed mapping.
- *What E23-26 (#389) gets.* `mem_coverage_spaces()` and a dense `mem_coverage(space, 0, size)`
  per space (8 KB, or 16 KB for `Rom`, `AltRom`, `Config`; `space_offset()` maps a logical
  address). X marks instruction starts, which is skoolkit's code map and DeZog's and ZEsarUX's
  coverage; an R byte inside an executed instruction's extent (`opcode_len` at each X) is an
  operand, so #389 classifies code and data itself.
- *Surface.* Debug ▸ Coverage, `--coverage` (a debugger session toggle like `--trace`: no
  Preferences field), the Disassembly bar (X only, by the fetch view), the Memory panel's
  X | R | W underline with a legend, and the Source tab's tint of the lines
  `executed_source_lines()` returns (X only: R and W have no per-line meaning). No DZRP, ZRCP,
  GDB or DSL verb: the C++ backend API is what #389 consumes.

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
- The z88dk start-up code's text is shown only when the build kept `prog.lis` (`--list`); without it the Source tab
  says the m4 output is not available.
- Toolchain mis-attributions (G4 on 2.4, master sdcc's `util.h:9`, G7) are shown as the
  toolchain wrote them, with the primary line and the "also" list (OD-7), and documented in the
  user guide.
- A map read with no program load after it is bound once, when it is read (#375): at
  `ScriptHost::start` that is the reset mapping, MMU2-7 = 10, 11, 4, 5, 0, 1, which NEX loaders
  set and 48K/128K mode maps, so the names are right for those programs; a program the guest
  loads itself is the case where they may not be.
- Programs the guest loads for itself (6.6). A tape's map is judged where the program first runs
  (6.2); a program that rewrites its own code before running it fails its identity.

## 13. Tests

**Suites today.** `source_map_test` (the SLD and `Memory.txt` adapters, the store),
`source_debug_test` (the backend on a real machine: loading and identity, fetch identity now
and in history, sidecar ownership and failure paths, the lifetime rows, every step kind),
`source_debug_posix_test` (`# os: posix`: the link and FIFO fixtures),
`debugger_source_panel_test` (the Source tab, page-qualified model rows and gutter, the Call
Stack column, symbol address fields), `rewind_call_stack_test` (the tracker across rewind),
`debugger_backend_test` (SYM-14 to SYM-18, the `-debug` map symbols; INS-13-15, the 56-byte
`TraceEntry`; INS-02-30, `poke` of every identity-only kind and `peek` of all but `DivMmcRam` are refused; INS-02-31/32, `peek(DivMmcRam)`), and the functional rows
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

E23-21 (#351) added Z88DOT-01 to Z88DOT-08 (`z88dk_map_test`: the dot / dotx / dotn / master fixtures'
spaces, detection by the map and not the address, the symbol half, the identity units, the `.lis`
route), INS-02-31/32 (`debugger_backend_test`: `peek(DivMmcRam)`; INS-02-30 now keeps `divmmc_ram(3)`
in its `poke` half only, owner-approved 2026-10-31), SRCDOT-01 to SRCDOT-09 (`source_debug_test`: a
source breakpoint and the steps inside the command, the same bytes in RAM and under the ROM, the
judgement at the entry, a hand load, landings, dotx, a rewind), QDOT-01/02
(`debugger_source_panel_test`) and the functional row `z88dk-dot-source-func` (real NextZXOS, the dot on the
card with `--sdcard-file-add`, typed, in headless, SDL and Qt).

E23-23 (#375) stage 3 added SYMPG-13 (`source_map_test`: `bind()`), Z88SYM-01 to Z88SYM-09
(`z88dk_map_test`: the symbol loader's pages, the rule-4 bind, the Unpaged and Refused table, two
passes, a 24-bit page head, an empty section as rule 4), SRCDBG-SYM-18 to SRCDBG-SYM-22 (`source_debug_test`: a banked symbol
names its address only while its page is mapped, the bind points, a running program paging its own
symbol) and SCRIPT-HOST-MAP-Z88BANK (`script_events_test`: `--map` with 24-bit banked symbols).

E23-14 (#345) added SRCSTEP-01 to SRCSTEP-13 and SRCDBG-CS-09 to CS-15 (`source_debug_test`: Into, Over
and Out on the runnable `__banked` and `-debug` fixtures, the return-address rule, the shim's
return), Z88MAP-29 (`z88dk_map_test`: a 24-bit page head) and rewrote SRCDBG-CS-08 (the shim's
return now ends the RST frame; owner-approved 2026-10-30).

E23-12 (#343) added Z88FIX-01/02 (`source_map_test`: the `test/fixtures/z88dk` files match
their manifest, and the manifest lists exactly those files).

E23-25 (#388) added FSRC-17 to FSRC-19 and MCOV-01 to MCOV-07 (`mmu_test`, `mmu_integration_test`:
the write route against the memory map for all 64 KB of 17 configurations with no stray write in
any store, the offsets, and the hook per arm, direction, scope and gate), INS-21-01 to INS-21-16
and HOST-COV-01 (`debugger_backend_test`), SRCDBG-COV-01/02 (`source_debug_test`), CLI-COV-01
(`cli_options_test`) and QCOV-01 to QCOV-07 (`debugger_menu_test`, `debugger_panels_test`,
`debugger_source_panel_test`).

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

E23-13 (#344) stage 2 added SRCDBG-Z88-01 to SRCDBG-Z88-14 and 18 to 32 (`source_debug_test`: the
sidecar, the hand load, precedence, identity, lifetime, real code), SRCDBG-Z88-15 to 17
(`source_debug_posix_test`: a FIFO), QSRC-62 to QSRC-64 (`debugger_source_panel_test`), QMAP-07
(`debugger_verbs_test`) and the functional row `z88dk-source-map-func`.

E23-13 (#344) stage 3 added SRCDBG-TAPE-03 to SRCDBG-TAPE-27 (`source_debug_test`: attach pending
and the verdict, the space and not the address, inertness and the hold, rewind and reset, an SLD hash,
`--source-map`, the verdict before a stop, two spaces in either order, a partial state across a rewind,
the live purity row with its control, who answers for a verdict), QSRC-65 (`debugger_source_panel_test`)
and the functional row `z88dk-tape-source-func` over the new `v2.4/sccz80-tape` fixture; SRCDBG-TAPE-01
and SRCDBG-Z88-03 follow the owner's tape decision.

E23-13 (#344) added Z88MAP-01 to Z88MAP-28 (`z88dk_map_test`: the section rules against where
`appmake` put the bytes in each fixture's `.nex`, extents, zero-width lines, strict refusal,
identity from the section binaries, the symbol loaders' file-type refusal).

E23-17 (#348) added `z88dk_lis_test` (Z88LIS-01 to Z88LIS-34: relocation, every occurrence of a C line,
zero-width lines, the cross-check, dropped listings, the classic CRT, MACRO/REPT/PHASE and the guard),
SRCTXT-01 (`source_map_test`), Z88MAP-30, SRCDBG-LIS-01 to 04 (`source_debug_test`) and QSRC-140/141
(`debugger_source_panel_test`).

Z88SYM-03 (E23-23) pins the classic fixtures' symbol count; it went from 6 to 22 when E23-17 made the two
`classic-bank10` builds keep the classic CRT's 16 `addr` symbols (`KEEP_CRT`). The owner approved the new pin on
2026-10-31.

E23-15 (#346) added GDB-MON-30..34 (`monitor src`), LEX-27/28 and PARSE-SRC-01..03, DSL-SRC-01..08,
DSL-SYMSP-01..05, SCRIPT-HOST-SRC-01..11, and the functional rows `source-map-func` (a looked-up line
over `monitor src`) and `z88dk-source-func` (scripts on a line or a symbol in three frontends, `monitor src`).

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

**E23-27 (#420), the source search path.** `source_map_test` SRCPATH-01..13 (the resolution
order, the longest trailing part first, the fixture's recorded CRT name, a Windows spelling, no
escape by `..`, `root_for` and its round trip, the list spelling); `config_keys_test` SETK-17..19;
`app_config_test` AC-100 (and AC-97 now covers nine keys); `debugger_source_panel_test`
QSRC-66..78 (the fixture map in `build/` with its sources in `src/`, Locate File... with No, Yes
and a wrong name, the z88dk `.m4` message, the past-the-end message, the lazy window, the located
directories surviving a configured-list change); `debugger_menu_test` MWSRC-01..06 (file,
command line and Preferences to the manager); `cli-set-func` Q6, S2 and U1.

**E23-31 (#424), C names.** `z88dk_map_test` Z88CN-01..16 (resolve under every binding, shown names, what is
not C, every compiler and install, banked, ambiguity both file orders, merge, reload, `shown_name()`, the
invariant over every committed z88dk map, `lookup_assembler()`, the rule per symbol in either file order, two C modules with one name); `source_debug_test` SRCDBG-CN-01;
`script_events_test` SCRIPT-HOST-CN-01/02; `gdb_rsp_test` GDB-MON-14; `debugger_source_panel_test`
QCN-01..05; `debugger_verbs_test` QMAP-08; `debugger_disasm_copy_test` DCN-01/02. Ten existing rows asserted
the shown `_name` of a C symbol and now assert the C name (owner-approved): Z88SYM-01, Z88SYM-04,
Z88SYM-08, SYM-14, SRCDBG-Z88-01, SRCDBG-SYM-18..22. Two more existing assertions changed, for the same
reason, and are owner-approved (2026-10-31): Z88MAP-15 (shown `_main` is now `main`) and SYM-31 (its "no sjasmplus
entry loaded" check compares the sjasmplus ADDRESS, because `main` and `counter` now resolve as C names of
the z88dk map).

**Row IDs are global**: the duplicate-ID gate refuses a reuse. IDs written into a sub-issue's
text before v1.1.64 may be taken already (`SRCDBG-CS-02`, `SRCDBG-STEP-17`, `SRCDBG-SIDE-12`,
`QBPM-11` and `QSRC-28` to `QSRC-33` are in the tree), so each sub-issue checks its IDs against
the tree.


E23-29 (#422) added QSRC-120 to QSRC-137, QSRC-139 and QSRC-145 to QSRC-149 (`debugger_source_panel_test`: language by extension and content, the assembly, C
and Boriel rules, colour-only formats, light and dark, the current line and the gutter marks, one highlighting pass per
load) and DASM-Z80N-01 (`debugger_backend_test`: the disassembler names ED B6 LDIRSCALE and no longer names ED C6).
E23-29 (#422) stage 2 added QSRC-138, QSRC-190 to QSRC-199 (`debugger_source_panel_test`), MWSRC-07 to MWSRC-09
(`debugger_menu_test`), SETK-20 and AC-101; QSRC-124 to QSRC-131, QSRC-133, QSRC-137, QSRC-146 to QSRC-149 were re-expressed in the
owner's classes (owner request 2026-10-31).
E23-30 (#423) added QSRC-99 to QSRC-119 (`debugger_source_panel_test`: the Source tab's context menu, Run to Here, Show in
Disassembly, the copy forms, the operand entries and the Disassembly's menu pinned across the shared builder),
SRCRUN-01 to SRCRUN-07 (`source_debug_test`: `run_to_source_line`) and CTL-06-04 (`debugger_backend_test`: a run-to on
a running machine drops the caller's earlier transient target, not another client's).
E23-16 (#347) added QSRC-170 to QSRC-189 (`debugger_source_panel_test`: the current-line wedge, the `also:` list, selection sync
both ways, the reveal and browse rules, Go to PC Line, Show in Source, the real z88dk map's two-start line, and the tint in
every colour scheme).
