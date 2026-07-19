# Source-Level Debugging — Design Document

## 1. Purpose

JNEXT needs source debugging for programs built with different toolchains. The
first supported inputs are NextBuild/Boriel `Memory.txt` symbols and sjasmplus
SLD source traces; z88dk and BasicStudio integrations are expected later.

The debugger must understand banked Next memory. A 16-bit address alone is not
enough when different 8K pages can occupy the same logical address.

## 2. Goals

- Keep compiler and assembler formats out of the debugger execution engine.
- Resolve symbols, source locations and breakpoints without losing an optional
  physical 8K page.
- Load matching sidecars automatically when a NEX starts, while retaining
  manual loading for an active session.
- Provide source display, source breakpoints and forward/reverse source steps.
- Preserve call-stack and page information through rewind.
- Allow future data sources to populate the same stores without changing the
  panels or stepping engine.

## 3. Non-goals

- Parsing compiler-specific type information or variable layouts.
- Bank-qualified data watchpoints; these remain logical-address based.
- Defining the eventual z88dk debugger protocol or BasicStudio transport.
- Replacing instruction-level disassembly where no source record exists.
- Background execution of source steps. They run synchronously with a bounded
  instruction count.

## 4. Architecture

The feature has three layers:

1. **Format adapters** parse external files and validate their rules.
2. **Neutral stores** hold symbols and source locations.
3. **Debugger consumers** query those stores for panels, breakpoints and
   stepping.

Dependencies point inward: adapters and UI depend on the stores; the stores do
not depend on a compiler, file format, Qt or the emulator.

### 4.1 Neutral stores

`SymbolTable` maps a 16-bit logical address to a display name and maps aliases
back to an address. A successful loader replaces the complete table. Failed
loads leave the active table unchanged.

`SourceMap` stores `SourceLocation` records containing:

- source path, one-based line and optional column;
- 16-bit logical address;
- optional physical 8K page.

It also stores optional `SourceProgramIdentity` metadata and the path of the
loaded sidecar. Replacement is transactional.

### 4.2 Address matching

At execution time, JNEXT obtains the PC's effective 8K page from the MMU.
Source lookup first tries `(page, address)`, then an unqualified address record.
This makes unbanked metadata a wildcard while allowing precise records for
overlaid code.

Execution breakpoints follow the same rule:

- a logical breakpoint matches the address on every mapped page;
- a page-qualified breakpoint matches only that physical page and address.

Source-gutter breakpoints are page-qualified. The Breakpoints panel can create
logical wildcards. Existing numeric and symbol breakpoints remain unqualified.

### 4.3 Format adapters

The adapters are free functions at the edge of the feature:

- `load_z88dk_map` and `load_simple_map` populate `SymbolTable`;
- `load_nextbuild_memory` parses NextBuild/Boriel `Memory.txt` output;
- `load_sld` parses sjasmplus SLD v1 records into `SourceMap`.

The SLD adapter accepts Next 8K-page device records and rejects inconsistent
device or slot metadata with a line-numbered error. Page-less records remain
valid wildcards. Identity metadata is carried in SLD comment records so the
file remains consumable by other SLD tools.

A future compiler adapter should emit `SymbolDefinition` and `SourceLocation`
records. It must not add compiler cases to debugger panels or stepping code.

### 4.4 Sidecar discovery and identity

When a program starts, the debugger searches beside it for same-stem symbol
and source sidecars. Same-stem names take priority so several NEX builds can
share one directory.

An SLD may identify the program with SHA-256, load origin and byte size.
Automatic loading verifies that range immediately after the NEX load and
rejects a mismatch. Manual loading warns and offers to continue: a running
Boriel program may have legitimately changed variables within its original
binary range.

Identity is optional. A sidecar without identity metadata remains usable.

## 5. Debugger behavior

### 5.1 Display and resolution

The Source panel uses the current effective page and PC to select a record and
highlight its line. Unmapped compiler/runtime code falls back to disassembly.

Address fields resolve loaded symbols before hexadecimal text. Explicit `$` or
`0x` syntax selects a numeric value when a symbol resembles hexadecimal.
`file:line` resolves to the first emitted instruction for that line; an exact
path or unique basename is accepted.

### 5.2 Forward source stepping

Source Step Into executes until the mapped file, line or column changes.
Source Step Over also requires call depth to be no greater than its starting
depth. Source Step Out stops when call depth drops below its starting depth.

Unmapped instructions between two statements are skipped. PC breakpoints and
data-breakpoint hits still stop the run. A step is capped at one million
instructions and returns control to disassembly if it reaches the cap.

Call depth comes from the existing CALL/RST/INT/RET tracker, which now records
the relevant physical page in each call frame.

### 5.3 Reverse source stepping

Trace entries retain the effective page. Source Step Back scans retained trace
entries for the previous different file/line/column and asks the rewind engine
to restore and replay to it. Repeated executions of one source position are
treated as one position.

Reverse Continue similarly searches for the previous mapped source breakpoint.
Both operations require trace and rewind to be enabled.

## 6. Rewind integration

Each frame snapshot stores the tracked call frames alongside serialized machine
state. Restoring a snapshot restores both, then instruction replay rebuilds the
precise state at the target trace entry.

Serialized state can grow when a variable-length subsystem grows. Rewind slots
therefore expand together when required, preserving published history and each
slot's actual byte count. A failed or inconsistent snapshot write is not
published.

## 7. Failure handling

- Loaders replace a live store only after a complete successful parse.
- Automatic identity mismatch clears the rejected source map and logs why.
- Manual identity mismatch requires explicit confirmation.
- Missing source records degrade to disassembly rather than stopping execution.
- Rewind corruption follows the existing corrupt-state warning and resume gate.
- Source stepping has a finite instruction limit to prevent an unbounded run.

## 8. Test strategy

Unit tests cover symbol aliases and resolution, loader rejection and CRLF
handling, SLD identity, page-qualified/wildcard lookup, breakpoint matching,
source-step stop conditions, reverse indexing, call-frame restore and
variable-size rewind growth.

Functional tests run real NEX files and verify automatic NextBuild symbol and
SLD sidecar loading through emitted log evidence. They are discriminative: a
binary without this feature cannot recognize or attach either sidecar.

## 9. Extension contract

A z88dk or BasicStudio integration should provide an adapter or transport that
atomically replaces the neutral stores. If it can identify an 8K physical page,
it should supply it; otherwise it should emit an unqualified record. The core
matching, breakpoint, stepping, rewind and UI behavior then remains unchanged.
