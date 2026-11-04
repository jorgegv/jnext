# C variables in the debugger

With a z88dk `-debug` map attached ([Building programs with z88dk](02-building-with-z88dk.md)), the
debugger window's **Variables** tab shows the program's C variables at every stop: the locals of the
call frame you choose, and the globals and statics. They are the values the program itself would read,
found by name, type and place from the compiler's records, so a `struct`, an array or a pointer is shown
as one, and a variable that cannot be read says why instead of showing a number.

The tab is the second one of the bottom-right tab bar, between **Watches** and **Breakpoints**. A value can be
[edited](#editing-a-value), and it shows data only while the machine is **paused** ([the panel](../panels/16-variables.md)).

## What it shows

- At the top, **the frame**: a list of the calls that led to the stop, newest first. Frame 0, marked
  `(current)`, is where the CPU is. A frame is `function at file.c:line`, or `<not C code> $8B64` where the
  program is in assembly, the C runtime or a library, and there are no variables to show.
- **Locals**: the frame's parameters (in stack order), then the locals visible at the frame's line, then its
  static locals. A local declared in a block that has not been entered at that line is not listed.
- **Globals and statics**: every global, file static and static local, one group per source file.
  A static local is named `function::name` (`sum::calls`), because two functions can each have one.

Each row has the **Name**, the C **Type**, the **Value** and the **Address**: `$909F P04` is a variable
in page 4 (the tag of the memory it is in, or its physical address with **Debug ▸ Physical Addresses**:
[which memory an address is in](../index.md#which-memory-an-address-is-in)); a stack variable is
shown at its address and the memory mapped there at this moment; a variable the compiler kept in a register
shows the register (`BC`). A global is always read from the page its symbol was placed in, so a banked
variable is read correctly even when its page is not mapped at that moment: its row says so
(`page 20 is not mapped at $C000 now`).

## Choosing a frame

Pick one in the selector, or click a row of the **Call Stack** tab: **row N of the Call Stack is the frame
N + 1**, the Call Stack having no row for the frame the CPU is in. The two stay in step in both directions.
A frame that is not a call of the Call Stack (the CPU's own, or an older one JNEXT found by following the
saved frame pointers) is reached from the selector only, and the Call Stack then shows no row selected.
Moving the machine (a step, a run to a breakpoint) goes back to frame 0, because the frame you were
looking at may no longer exist; redrawing at the same stop (a style switch, a click elsewhere) keeps your choice.

## Expanding

A struct, an array or a pointer has a triangle. Opening it shows its members, its elements, or what the
pointer points at (named `*name`, or the members of the struct it points at). The children are read when you
open the row, up to 100 at a time: an array of more than 100 elements shows ranges (`[0..99]`, `[100..199]`,
...) that open onto their elements. A NULL pointer opens onto the text `null pointer`. What you have open stays
open at the next stop, and a row you have selected stays selected.

## Watching a variable

Right-click a row and choose **Watch**: it becomes a row of the **Watches** tab (the tab does not change).
- A parameter or local is tied to **its function**: the row is labelled `t (in sum)` and is read in the
  newest call of `sum` wherever it is, and when `sum` is not on the call stack it says
  `out of scope: sum is not on the call stack`.
- A global, a file static or a static local always reads, whatever the CPU is running (a static local is pinned under its
  own file and function, so another file's function of the same name is never read instead).
- **Watch** is available only while the machine is paused, like everything in the tab.
- C rows are read at each stop, not while the machine runs: they keep the text of the last stop.
- Edit does nothing on a C row (there is no address to edit); Remove deletes it. C watches last the session.
- Without a source map, the rows show why there are no variables, and bind again when a map with that function
  and variable is attached.

## Editing a value

Double-click a variable's **Value** (or right-click ▸ **Edit Value**) while the machine is paused: an editor opens holding the value's
first word (`120` for `120 'x'`, `$8D21` for `$8D21 <g_node>`, `1.5` for a float). Type the new value and press Enter; Esc cancels.

- A value is a decimal, `0x..` or `$..` integer, a character such as `'x'`, a decimal number for a float variable, or an address
  for a pointer. A value that does not fit the variable's type is refused, never truncated (`-1` into a `uint16_t`).
- The edit writes the frame you chose (a variable of an older frame included) and the machine sees the new value when it next reads it.
  It is logged as a `MUTATE` line, like a write in the Memory panel.
- A refusal is shown above the tree as `<path>: <reason>`, and the value stays as it was. Aggregates (a struct, an array), a row that
  has no value, a register variable and the other cases in [Limits and troubleshooting](10-limits-and-troubleshooting.md#limits) cannot be
  edited.
- Watches rows of C variables are not editable.

## When there is nothing to show

The tab says why: no source map attached; an SLD map (SLD carries no C variables, only a z88dk `-debug` map does); a z88dk
map with line records but no variable records (build with `zcc -debug`; with z88dk 2.4, zsdcc cannot build with
`-debug`, [z88dk#3172](https://github.com/z88dk/z88dk/issues/3172)); or `zcc -O0` with zsdcc, which writes no variable
records. A map made stale by a different program in memory is shown as stale.

## What it cannot show

These are limits of what the compilers record, not of the debugger:
- A zsdcc variable kept in a register is read from the live registers, so it is right only at frame 0 and only within its live
  range: it is marked `may be stale`, and in an older frame it is `not available`. A variable the optimiser removed is `optimised out`.
- The 80cc compiler records no block structure, so every block local is listed, marked `scope unknown`; it records no 1-byte locals.
- An sccz80 `__z88dk_fastcall` parameter is recorded at a place where it is not: its row says `location unknown`.
- sccz80 and 80cc write a bitfield as a whole `int`: its members are marked `bitfield layout unknown`.
- Enumerations show their number, not their name; a `union` is shown as a `struct`; zsdcc writes no record for a `const` global.
- Before a function has set up its frame, its locals are `not allocated`; a variable at an offset below the stack the frame owns, too.
- Some variables cannot be written: see [Editing a value](#editing-a-value).

To read the same values from a script, GDB or ZRCP, see [Scripts and remote debuggers](09-scripts-and-remote-debuggers.md#c-variables).
