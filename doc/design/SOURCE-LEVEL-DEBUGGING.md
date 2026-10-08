# Source-level debugging — design (CAP-SRC)

> Status: **implemented** on `feature/source-debug-1.1.11`. This document
> replaces the version that accompanied PR #206, which was written against the
> pre-#276 debugger (the Qt `DebuggerManager` owned the symbol table and stepped
> the `Emulator` directly). The feature is the same; where it lives is not:
> everything except the Source tab is now a capability of the
> `jnext::dbg::Debugger` backend ([DEBUG-SUBSYSTEM-ARCHITECTURE.md](DEBUG-SUBSYSTEM-ARCHITECTURE.md)),
> so every frontend — Qt, SDL, headless, the remote servers — sees the same
> source map and symbols.

## 1. Use case

Issue [#23](https://github.com/jorgegv/jnext/issues/23) (epic, "Source-level
debugging with z88dk"): the debugger works at the assembly level, so a program
written in a higher-level language — C with z88dk, Boriel ZX Basic with
NextBuild — or in assembly is debugged by mapping every PC back to a source line
by hand, and in banked Next code the same address means different code depending
on which 8K page is mapped. Concretely, the Cobra remake for the Next (Boriel,
200+ banks) is debugged this way: a crash in a line-interrupt routine meant
reading compiler output to find the BASIC line it came from.

This is the compiler-neutral foundation #23 needs, not the z88dk part of it: a
source-map model, page-aware lookup, statement stepping in the backend, and a
Source tab, with two first adapters — sjasmplus SLD (which Boriel/NextBuild
projects can emit too) and NextBuild's `Memory.txt` for symbols. A z88dk adapter
plugs into the same contract (§9); the format the issue discussion favours is
SDCC's `.cdb`, which z88dk now produces correctly with sdcc and which also
carries C variables and arguments.

## 2. Inputs

- **sjasmplus SLD v1** (`--sld`): `file|line[:col[:endcol]]|defFile|defLine|page|value|type|data`
  records. `Z` is the device model; it must have 8K pages and at most 224 of
  them (the Next's RAM). `T` is a source trace: the address `value` on RAM page
  `page` was emitted from `file:line`. Other record types are ignored.
- **Optional binary identity**, in SLD comment records other readers ignore:
  `||program.name:`, `||program.sha256:`, `||program.org:`, `||program.size:` —
  the SHA-256 of `size` bytes of logical memory from `org` once loaded.
- **NextBuild `Memory.txt`**: `8000: ._Main` lines (hex address, label).

## 3. Architecture

Three layers, dependencies pointing inward:

1. **Format adapters** (`src/debug/sld_loader.*`,
   `SymbolTable::load_nextbuild_memory()`) parse a file into the store. The SLD
   adapter is transactional: it parses and validates the whole file into a
   local list and replaces the map only on success; a refused file leaves the
   live map as it was. The `Memory.txt` loader follows the existing MAP loaders:
   a file that cannot be opened changes nothing; once opened, the table is
   replaced by whatever valid lines it holds (a malformed line is skipped, not
   fatal).
2. **Neutral stores**: the backend's existing `SymbolTable` (CAP-SYM) and a new
   `SourceMap` (`src/debug/source_map.*`) of `SourceLocation` records —
   file, line, column, optional RAM page, logical address — indexed by
   (page, address) and by (file, line). Neither knows a file format, Qt or the
   emulator.
3. **Consumers**: the backend's CAP-SRC verbs, and the Qt panels through them.

## 4. Matching banked code: what supplied the instruction

A record names the **RAM page** its code was built for, or no page. The question
a lookup must answer is therefore "which RAM page supplied this instruction",
and the MMU's page number alone does not answer it: a ROM-mapped slot reports a
page number from the same range as RAM pages, and the overlays the CPU's reads
go through first — the boot ROM, the Multiface, DivMMC, the Layer 2 read
mapping, the alternate ROM and config-mode routing — replace the MMU's page
without changing it.

So there is one identity, used everywhere CAP-SRC matches code:

- `Emulator::fetch_not_mmu_ram(pc)` is true when the opcode fetch of the next
  M1 at `pc` will be served by an overlay or a ROM slot. It is a side-effect-free
  PREDICTION of that M1, because the M1 itself switches overlays: DivMMC's
  instant-on entry points map DivMMC for the very fetch that hits them, the
  hold carried from the previous M1 decides whether DivMMC still answers (so the
  fetch after a 0x1FF8-0x1FFF unmap is RAM again while DivMMC still reads as
  mapped), and the Multiface latches on the 0x0066 fetch. It runs the same
  arbiter inputs the `on_m1_prefetch` hook does through const predictors
  (`DivMmc::active_on_m1()`, which shares its entry-point decode with
  `check_automap()`; a copy of the Multiface at 0x0066) and asks
  `Mmu::read_not_mmu_ram(pc, divmmc, multiface)` the rest (boot ROM, Layer 2
  read mapping, alt-ROM, config mode, ROM slots).
- `Debugger::source_page(addr)` is the MMU's page when RAM supplies the fetch,
  else `NOT_RAM_PAGE` (0xFF, `debug/ram_page.h`; never a Next RAM page).
- `SourceMap::lookup(NOT_RAM_PAGE, addr)` matches only an unqualified record.
- **History**: each trace entry carries the eight MMU pages and a ROM-slot mask
  (GH #276/#280); the trace now also records, beside each entry, the same
  prediction for that instruction's fetch — made immediately before it, so it is
  what the fetch then did (`TraceLog::fetch_not_mmu_ram()`, kept outside the entry
  because `TraceEntry`'s 56-byte layout is published). Back and Reverse
  Continue derive the same identity from those.
- **Call frames** record the caller's and target's identity (the target's from
  the prediction for the first fetch there)
  (`CallFrame::caller_page`, `target_page`, `NOT_RAM_PAGE` for ROM/overlay).
- **Breakpoints** on a source line are page-qualified `Execute` subscriptions
  with the new `EventFilter::page_ram_only`: they match only a fetch from that
  RAM page (`Event::fetch_not_ram` is false). The plain page qualifier, which
  DZRP's banked breakpoints and the DSL's `page` use, keeps comparing page
  numbers alone; changing what it means for those clients is out of scope.

**Limitation — code that moves.** A record describes where its code was built
to run. Code copied at run time to another address or page (a driver copied out
of a bank into low RAM, as Cobra's audio driver is) has no record where it
actually runs, and shows as unmapped there. Producing records for the copy is the
build's job (an SLD for the copy's `ORG`/`DISP`). The binary identity likewise
covers only `org..org+size`.

## 5. Backend API (CAP-SRC, `debug/debugger.h`)

| Verb | Meaning |
|---|---|
| `load_source_map(path, accept_identity_mismatch)` | load an SLD; refused on a parse error, or on an identity mismatch unless accepted. The path is stored absolute |
| `clear_source_map()`, `source_map()` | the store |
| `source_location(addr)`, `source_location()` | the position of `addr` / the PC, by `source_page()` |
| `source_page(addr)`, `effective_page(addr)` | the RAM page that supplies `addr` (or `NOT_RAM_PAGE`); the MMU's page |
| `load_program_sidecars(path)` | attach a loaded program's `Memory.txt` and SLD (§6) |
| `source_step(by, kind)` | `Into`, `Over`, `Out`, `Back`, `ReverseContinue` (§7) |
| `load_map(path, MapFormat::NextBuild)` | Memory.txt by hand (CAP-SYM gains a format) |

## 6. Sidecars

After a successful program load every frontend calls
`load_program_sidecars(path)` (Qt and SDL from their load tick, headless from
`--load`, and `Debugger::load()` for a remote client). It looks for
`<stem>.Memory.txt`, else `Memory.txt` (beside a `.nex` only), and `<stem>.sld`,
else `<stem>.sld.txt`. Anything there that is not a directory is the sidecar:
one that cannot be read is reported (`SidecarLoad::symbols_unreadable`), not
mistaken for none. The search uses the non-throwing filesystem calls: a dangling
or looping link is "no sidecar". Each outcome is logged.

**Tape programs** (`.tap`, `.tzx`, `.wav`) get no automatic source map. The
machine loads them over the frames after the load call returns, and neither the
tape loader nor the backend has a point at which "the program has loaded" (a
program may also run, and change its own image, before any check could match).
Their map is loaded by hand once the program is in memory.

**Ownership** is explicit: each store is the user's (a MAP or SLD loaded by
hand, `--map`), a program sidecar's, or nobody's, and it changes only when a load
succeeds. A sidecar never replaces or clears the user's store (even one that
looks empty, such as a MAP of `; const` lines). A previous program's sidecar
data never survives into the next program: it is replaced, or cleared when the
new program has no sidecar or its sidecar cannot be read.

**History.** A sidecar belongs to the load it came with: the backend records the
machine's clock at the attach. Every replacement of the machine (a rewind, a
state load, a reset) marks the sidecars for a recheck, which `pump()` and every
reader of the stores perform before reading — deferred because a rewind
restores a snapshot and then replays forward, so only where it lands counts. If
the machine now stands BEFORE the attach point, the sidecar stores are dropped:
in that history the program has not been loaded. Only the clock is compared: a
state from another session whose clock is past the attach point keeps them.
Stores the user loaded are never dropped this way.

**Identity.** A sidecar SLD whose identity does not match the program in memory
is rejected and logged. A map loaded by hand that does not match is offered
anyway in the Qt GUI, because a running Boriel program changes variables inside
its own image.

## 7. Source steps

**Forward** steps are a run of the debugger's Step (`Emulator::debugger_step()`,
which turns frames over and runs a HALT out), synchronous, with the same pause
ownership and stop evidence as `step_into()`:

- `Into` stops when the mapped position (file, line, column) differs from the
  start; unmapped instructions in between run.
- `Over` additionally requires call depth ≤ the start's.
- `Out` stops at the first mapped position at a depth below the start's; with
  no tracked frame it is one instruction.
- Starting on an unmapped instruction, any step is one instruction.

Between instructions, each next PC gets the delivery `run_frame()` gives it:
the legacy PC breakpoint, then the backend's own `Execute` gate — handlers run
(a script's `log` rule fires), a `Stop` stops the step with its hit and `once`
recorded, exactly as a run would. A handler may move the PC (§4.2a of the
backend design): the step then re-evaluates its boundary at the new PC, without
a second delivery there — as in a run, the redirect target simply runs next. Any stop latched during an instruction (a
watchpoint, the magic breakpoint) ends the step too. The step also ends, and
logs why, at a HALT with interrupts disabled (only an NMI or a reset can end
it), after `SOURCE_STEP_LIMIT` (one million) instructions, or after
`SOURCE_STEP_FRAME_LIMIT` (100) frames of machine time — so a statement that
waits never holds the frontend. Call depth is the call-stack tracker's
(`set_call_stack_enabled`), which the Qt debugger runs while its window is open.

**Backward** steps search the trace from newest to oldest, considering only
entries **before the machine's current cycle**: a Frame Back or a slider jump
leaves the abandoned history in the trace, and that is not "back". `Back` takes
the newest mapped position different from the current one (a statement repeated
over consecutive instructions counts once); `ReverseContinue` takes the newest
mapped instruction covered by a live, unconditional, handler-less,
non-transient `Execute` Stop (or a legacy PC breakpoint), its page qualifier
checked against the entry's identity. The count goes to `step_back()`, so
rewind's refusals (RZX, an empty ring, a replay that would cross a debugger
mutation) and its corruption handling apply unchanged. A trace that is off, or
no target, is `RefusedUnavailable` with a `REWIND REFUSED:` log line the Qt
status bar shows.

Rewind restores the call-stack tracker with each snapshot, and any other
replacement of the machine (a state load, a reset) empties it — a separate bug
fix, `fix/rewind-call-stack`, this branch's base.

## 8. Qt

- **Source tab** (`src/debugger/source_panel.*`): the PC's file with its line
  highlighted, and buttons for the five steps and the two loads. The file is
  found from the recorded name — absolute, else relative to the map's directory
  or a parent — and reloaded when it changes on disk. The buttons are not menu
  items and have no key binding: the menu tree and the bindings are pinned by
  `debugger_accel_test` (DACC-05) and `debugger_keymap_test`, and extending
  either is an owner decision.
- **Breakpoints panel**: the address field takes a symbol, a number, or, for
  Execute, `file:line` — a breakpoint qualified with the record's RAM page and
  `page_ram_only`, which the model lists as the GUI's own row and the gutter
  draws only while that RAM page supplies the address; the last column shows
  the symbol, else the source line.
- **Call Stack**: "Called from", the caller's line on the caller's recorded page
  (`CallFrame::caller_page`).
- **Address fields** of the Breakpoints menu dialogs and Watches resolve symbols
  (`SymbolTable::resolve()`).

## 9. Adapter contract, and the next adapter

An adapter turns one file format into the neutral stores and nothing else:

- `SourceLocation` records — file, line, optional column, logical address, and
  the RAM page when the format can name it (unqualified otherwise, which then
  matches whatever RAM or ROM supplies that address);
- optionally a `SourceProgramIdentity`;
- symbols into the `SymbolTable` (a `MapFormat`).

It must parse the whole input before replacing a store, and fail with a reason.
Panels, stepping, breakpoints and history then work unchanged.

**SDCC `.cdb`** (z88dk with sdcc), suggested on #23, is the intended next
adapter. Its linker records give C source lines with addresses (`L:C$file$line$…:ADDR`)
— `SourceLocation`s; banked sections give the page where z88dk's bank
assignment is known, and otherwise records stay unqualified. Its symbol records
carry functions, globals, locals and arguments with types and scopes: the
function and global addresses fit `SymbolTable` today, while variables and
types need a store of their own (a later step; the steps and the Source tab do
not depend on it). `.lis` listings (DeZog's route) would fit the same way with
less information.

## 10. Non-goals

Variables, types and scopes (see §9); bank-qualified data watchpoints; executing
a source step in the background (it is synchronous and bounded); editing source;
records for code copied at run time (§4).

## 11. Tests

- `rewind_call_stack_test` (the base fix) — the tracker after `rewind_to_frame`
  and `step_back`, and emptied by a state load and a reset.
- `source_map_test` — the SLD and Memory.txt adapters, validation, CRLF,
  sidecar search, page/wildcard lookup, `file:line` resolution, identity,
  `SymbolTable::resolve()`.
- `source_debug_test` — the backend on a real machine: loading and identity,
  fetch identity now and in history (ROM slot, Layer 2 overlay) and the
  `page_ram_only` qualifier, sidecar ownership and failure paths, tape
  exclusion and the history rule (rewind before / after the load, a state
  restored from before it), DivMMC instant entry and unmap foreseen at M1,
  looping links, an unopenable sidecar, relative paths, every step kind and its
  stops (breakpoints, handlers, a handler redirect, `once`, DI:HALT, the frame
  budget), backward steps through the rewind ring including after a Frame
  Back, frame pages.
- `debugger_source_panel_test` — the Source tab and its buttons (and a map
  loaded by a relative name), page-qualified model rows and gutter, the panel's
  `file:line`, the Call Stack column, symbol address fields.
- Functional `nextbuild-symbols-func`, `source-map-func` — sidecars attach on
  `--load` in the headless, Qt and SDL frontends, every run exits 0, a wrong
  identity is rejected, and a GDB client's `monitor sym` finds a sidecar symbol
  in the headless machine. The source map itself has no command-line or
  protocol surface; the two unit suites above drive it.

Without the feature the unit suites do not compile (the API is new) and the
functional rows fail (nothing attaches; `monitor sym` answers "no symbol").
Each fix made after review has a row that fails with that fix reverted.
