# Breakpoints, steps and running to a line

What the Source tab does once a source map is attached: stop on a source line,
step by statement, run to a line, and move between the source and the
disassembly. The tab's buttons, gutter and menu are described in
[its panel page](../panels/15-source.md); [A first session](01-a-first-session.md)
shows all of it on one program.

## Breakpoints on a source line

In the Breakpoints panel's **Add** dialog, the address can be a number
(`8000`, `$8000`, `0x8000`), a symbol (`PlayerTick`), or, for an Execute
breakpoint, a source line, `file:line`: `main.bas:120`, or the end of the
path (`game/main.bas:120`) when two files share a name. A line the program
compiled to several places (a routine in two banks, say) is ONE breakpoint,
armed at each place and listed as one row, "main.bas:120 (2 locations)": Enabled,
Edit and Remove act on all its places, and a click on the dot of one place in the
disassembly gutter removes only that place. A line with no code is refused with "no code at main.bas:120". A click in the Source tab's
gutter sets or clears the same breakpoint on the line shown, and removing one place in the
disassembly leaves a half mark on the Source tab's line. A number wins over a symbol: `FE` or `B` is that
address even when the program has a variable called `FE` or `B`. Write `@FE`
to mean the symbol (a z88dk C variable takes its C name: `counter`, not `_counter`). A breakpoint on a line of banked code is tied to
the memory that supplies it (shown as `$C000 P2E` for a RAM page): it fires only
while that memory supplies the address, and the disassembly gutter shows it only then.
The tags are explained in [banked code](07-banked-code.md).

**Breakpoints ▸ Add Execute Breakpoint…** takes `file:line` too. Symbols work
in the other address fields: the other **Breakpoints ▸ Add …** dialogs,
**Watches ▸ Add** (a watch added by name is labelled with it) and the Go to
Address box of the Disassembly and Memory panels.

The Source tab's gutter shows and changes only the debugger window's own
breakpoints; those of GDB, ZRCP, DeZog or a script are in the Breakpoints
panel only (see [Scripts and remote debuggers](09-scripts-and-remote-debuggers.md)).

## The steps

The Source tab's buttons step by statement:

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
Reverse ship unbound. All of them can be rebound ([Changing the keys](../functions/09-changing-the-keys.md)), and they
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
([Backward execution (rewind)](../functions/07-backward-execution-rewind.md)); they look for
their target in the instructions the trace has kept.

## Moving between the source and the disassembly

The Source tab and the Disassembly follow each other's selection: click a source line and its
instructions are tinted in the Disassembly; click an instruction and the Source cursor goes to its
line, in its own file if that is not the CPU's, until the next stop or **Go to PC Line**. The
disassembly's right-click menu has **Show in Source**, the counterpart of **Show in Disassembly**
([Following the disassembly](../panels/15-source.md#following-the-disassembly)).

## Running to a source line

**Run to Here** in the Source tab's right-click menu (or **Enter** on the line) runs the machine
until the line is reached. A line that compiled to several places stops at the first one
reached; a line of banked code stops only when its page supplies the code there. No
breakpoint is left behind, and it works while the machine runs: a second Run to Here replaces
the first. Run to Cursor, once bound, acts on the Source tab's cursor line while that tab has
the focus. A breakpoint on the way still stops the run first. See
[Run to cursor](../functions/02-run-to-cursor-run-to-end-of-frame-run-to-end-of-scanline.md).

## Watching a variable from the source

The same menu offers **Watch** and **Break on Read / Write** for the label, number or register
under the pointer ([the Source tab's menu](../panels/15-source.md#right-click-menu), and
[In the Source tab](08-around-the-source.md)).
