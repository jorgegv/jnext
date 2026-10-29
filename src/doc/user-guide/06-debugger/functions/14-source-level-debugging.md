# Source-level debugging

When your program comes with a **source map**, the debugger can show the line
of BASIC or assembly the CPU is running and step through the program one
source statement at a time — including over and out of calls, and backwards
with rewind.

JNEXT reads source maps in the **sjasmplus SLD** format (`.sld`), which
sjasmplus writes with `--sld`, and which a Boriel ZX Basic / NextBuild build
can produce too. Symbols come from the **NextBuild `Memory.txt`** the NextBuild
toolchain writes beside a program, as well as from the MAP files in
[Symbols from MAP files](05-symbols-from-z88dk-map-files.md).

## Loading them

**Automatically.** When a program is loaded — on the command line with
`--load`, with **File ▸ Load NEX File…**, or by a remote debugger — JNEXT looks beside it
for:

- `<name>.Memory.txt`, else `Memory.txt`, beside a `.nex`: the symbols;
- `<name>.sld`, else `<name>.sld.txt`: the source map.

Each one found is loaded and named in the log (`Loaded 2 NextBuild symbols
from …`, `Loaded 812 SLD source traces from …`). This happens in every
frontend, with or without the debugger window open, but only the Qt debugger
shows the source map: with `--headless`, and in the SDL-only build, it is
attached and logged and nothing reads it yet (a remote debugger can still look
up the symbols).

They belong to the program that was loaded with them. Loading the next program
replaces them, or clears them if it has none. Rewinding brings back the ones
that belonged to the point you land on (the last four programs are remembered).
When the machine is reset, or a state is restored from a snapshot, a bookmark or
a remote debugger, or a replay starts, the source map stays only if it carries
the program's SHA-256 (an SLD extension that most assemblers do not write) and
the memory still matches it; a reset is checked again at the next pause, since
the ROM clears memory a few frames later. Otherwise the program's symbols and
source map are dropped. **Back** and **Reverse Continue** never go back past
such a landing. A program the guest loads for
itself, such as one chosen in the NextZXOS browser or started with `LOAD ""`,
is not seen by JNEXT: load its source map by hand (below). With
`--esxdos-stub`, a `.RUN` of a sibling NEX is a load like any other: the
machine cold-boots with it, which drops the old ones, and the new program's
are attached. A tape (`.tap`, `.tzx`, `.wav`) is not attached automatically:
its program is not in memory until the tape has loaded, so load its source map
by hand (below) once it has.

Symbols you loaded by hand (below), or a MAP given with `--map`, are kept: they
are your choice, not the program's. A source map you loaded by hand is yours too
(a program's map never replaces it), but at each of those landings it is checked
the same way: if it has no SHA-256, or the memory no longer matches it, it goes
**stale**. The Source tab then says so and why, and the steps are off; load it
again (**Load SLD...**) to use it.

**By hand.** The **Source** tab has **Load SLD...** and **Load Memory.txt...**
buttons, also in **Debug ▸ Source ▸ Load Source Map…** and **Load Symbols
(Memory.txt)…**. The symbols replace whatever was loaded before, as the
**Map ▸ Load MAP File** formats do.

**Binary identity.** An SLD may record the SHA-256 of the program's bytes as it
was built (`||program.sha256:`, `||program.org:` and `||program.size:` comment
records, which other SLD readers ignore). An SLD found beside a program whose
bytes in memory do not match is **rejected** — it belongs to another build.
One loaded by hand that does not match asks first, because a running
program may have changed variables stored inside its own image since it started.

A sidecar that is found and refused, for any reason (an SLD built for another
binary, a file that is not an SLD, not a regular file, or over 64 MiB), says
why: the reason is in the log, in the main window's status bar for ten
seconds, and in the Source tab in place of "No source map loaded".

## Banked code

A Next program pages code in and out, so one address can hold different code
at different times. Each source record names the 8K RAM page its code was built
for, and the debugger matches the RAM page that supplies that address *now*:
the Source tab, the Call Stack and the Breakpoints panel never show a line of
code that is not the one in memory. Code supplied by ROM or by an overlay
(DivMMC — including the instruction at which its automatic paging switches on
or off —, the Multiface, the Layer 2 read mapping, the boot ROM) is not any RAM
page, so page-tagged records never describe it; the Source tab says "ROM or
overlay" there.

A statement covers every byte up to the next one: the data of a
`DB` after an instruction belongs to that instruction's statement, and so does everything after the last
statement of a page up to the end of that 8K page. Only addresses below a
page's first statement, and pages with no statement at all, are unmapped.

Code your program copies somewhere else at run time (a routine moved out of a
bank into low memory, say) has no record where it runs unless your build made
one, and shows as unmapped there.

## The Source tab

See [Source](../panels/15-source.md). Its buttons step by statement:

- **Into** — run to the next source statement, entering calls. Instructions
  with no source statement (compiler runtime, library code) are run through.
- **Over** — the same, but a call is run to its return.
- **Out** — run until a statement in the routine that called this one.
- **Back** — rewind to the start of the previous source statement that ran
  before this point (after a **Frame Back**, the history you rewound away from does not
  count).
- **Reverse** — rewind to the most recent source statement with an Execute
  breakpoint on it.

The same five are in **Debug ▸ Source**. Into, Over and Out are on **Ctrl+F6**,
**Ctrl+F7** and **Ctrl+F8** (like F6, F7 and F8 for instructions); Back and
Reverse ship unbound. All of them can be rebound ([Changing the keys](09-changing-the-keys.md)), and they
work in the debugger window only.

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

In the Breakpoints panel's **Add** dialog, the address can be a number
(`8000`, `$8000`, `0x8000`), a symbol (`PlayerTick`), or, for an Execute
breakpoint, a source line, `file:line`: `main.bas:120`, or the end of the
path (`game/main.bas:120`) when two files share a name. A line the program
compiled to several places (a routine in two banks, say) is ONE breakpoint,
armed at each place and listed as one row, "main.bas:120 (2 locations)": Enabled,
Edit and Remove act on all its places, and a click on the dot of one place in the
disassembly gutter removes only that place. A line with no code is refused with "no code at main.bas:120". A number wins over a symbol: `FE` or `B` is that
address even when the program has a variable called `FE` or `B`. Write `@FE`
to mean the symbol. A breakpoint on a line of banked code is tied to
the memory that supplies it (shown as `$C000 @2E` for a RAM page): it fires only
while that memory supplies the address, and the disassembly gutter shows it only then.

**Breakpoints ▸ Add Execute Breakpoint…** takes `file:line` too. Symbols work
in the other address fields: the other **Breakpoints ▸ Add …** dialogs,
**Watches ▸ Add** (a watch added by name is labelled with it) and the Go to
Address box of the Disassembly and Memory panels.
