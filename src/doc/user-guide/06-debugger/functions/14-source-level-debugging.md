# Source-level debugging

When your program comes with a **source map**, the debugger can show the line
of BASIC or assembly the CPU is running and step through the program one
source statement at a time — including over and out of calls, and backwards
with rewind.

JNEXT reads source maps in the **sjasmplus SLD** format (`.sld`), which
sjasmplus writes with `--sld`, and which a Boriel ZX Basic / NextBuild build
can produce too. Symbols come from the **NextBuild `Memory.txt`** the NextBuild
toolchain writes beside a program, as well as from the MAP files in
[Symbols from Z88DK MAP files](05-symbols-from-z88dk-map-files.md).

## Loading them

**Automatically.** When a program is loaded — on the command line with
`--load`, with **File ▸ Open**, or by a remote debugger — JNEXT looks beside it
for:

- `<name>.Memory.txt`, else `Memory.txt`, beside a `.nex`: the symbols;
- `<name>.sld`, else `<name>.sld.txt`: the source map.

Each one found is loaded and named in the log (`Loaded 2 NextBuild symbols
from …`, `Loaded 812 SLD source traces from …`). This happens in every
frontend, with or without the debugger window open. Loading the next program
replaces them, or clears them if it has none, so a map never describes the
wrong program; so does rewinding (or restoring a state) to a point before the
program was loaded. A tape (`.tap`, `.tzx`, `.wav`) is not attached
automatically: its program is not in memory until the tape has loaded, so load
its source map by hand (below) once it has.

Symbols or a source map you loaded by hand (below), or a MAP
given with `--map`, are kept instead: they are your choice, not the program's.

**By hand.** The **Source** tab has **Load SLD...** and **Load Memory.txt...**
buttons. The symbols replace whatever was loaded before, as the
**Map ▸ Load MAP File** formats do.

**Binary identity.** An SLD may record the SHA-256 of the program's bytes as it
was built (`||program.sha256:`, `||program.org:` and `||program.size:` comment
records, which other SLD readers ignore). An SLD found beside a program whose
bytes in memory do not match is **rejected** — it belongs to another build.
One loaded by hand that does not match asks first, because a running
program may have changed variables stored inside its own image since it started.

## Banked code

A Next program pages code in and out, so one address can hold different code
at different times. Each source record names the 8K RAM page its code was built
for, and the debugger matches the RAM page that supplies that address *now*:
the Source tab, the Call Stack and the Breakpoints panel never show a line of
code that is not the one in memory. Code supplied by ROM or by an overlay
(DivMMC — including the instruction at which its automatic paging switches on
or off —, the Multiface, the Layer 2 read mapping, the boot ROM) is not any RAM
page, so page-tagged records never describe it; the Source tab says "ROM or
overlay" there. A record with no page matches whatever is there.

Code your program copies somewhere else at run time (a routine moved out of a
bank into low memory, say) has no record where it runs unless your build made
one, and shows as unmapped there.

## The Source tab

See [Source](../panels/15-source.md). Its buttons step by statement:

- **Into** — run to the next source statement, entering calls. Instructions
  with no source record (compiler runtime, library code) are run through.
- **Over** — the same, but a call is run to its return.
- **Out** — run until a statement in the routine that called this one.
- **Back** — rewind to the previous source statement that ran before this
  point (after a **Frame Back**, the history you rewound away from does not
  count).
- **Reverse** — rewind to the most recent source statement with an Execute
  breakpoint on it.

While a step runs, breakpoints, watchpoints, the magic breakpoint and debugger
script rules act exactly as they do when the program runs: a breakpoint stops
the step, a script's `log` rule logs and lets it carry on. A step that never
reaches another statement — a loop on one line, a `HALT` with interrupts off —
stops by itself (at a disabled-interrupt `HALT`, or after a million instructions
or 100 frames) and says so in the log.

**Over** and **Out** count calls with the call-stack tracker, which runs while
the debugger window is open. **Back** and **Reverse** need the trace log and
rewind, like **Step Back**
([Backward execution (rewind)](07-backward-execution-rewind.md)); they look for
their target in the instructions the trace has kept.

## Breakpoints on a source line

In the Breakpoints panel's **Add** dialog, the address can be a symbol
(`PlayerTick`), a number (`8000`, `$8000`, `0x8000`), or — for an Execute
breakpoint — a source line, `file:line`: `main.bas:120`, or the full path
when two files share a name. A breakpoint on a line of banked code is tied to
its page (shown as `$C000 @2E`): it fires only while that RAM page supplies the
address, and the disassembly gutter shows it only then.

Symbols work in the other address fields too: **Breakpoints ▸ Add …**
dialogs and **Watches ▸ Add** (a watch added by name is labelled with it).
