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
  static locals. A local declared in a block that has not been entered at that line is not listed (see the notes below for what the compilers' records allow).
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
- The edit of an 80cc stack variable succeeds, and the message above the tree then repeats the 80cc note (the value may not be the one the
  program reads). A variable that shares its stack slot with another listed one is refused, naming the other, and the slot is not written.

## When there is nothing to show

The tab says why: no source map attached; an SLD map (SLD carries no C variables, only a z88dk `-debug` map does); a z88dk
map with line records but no variable records (build with `zcc -debug`; with z88dk 2.4, zsdcc cannot build with
`-debug`); or `zcc -O0` with zsdcc, which writes no variable
records. A map made stale by a different program in memory is shown as stale.

## Notes on a value

A value the compiler's records cannot be fully trusted for is shown with a note in brackets after it, in the Value column and in the tooltip;
the same notes reach the Watches tab and GDB's `monitor locals`, `print` and `set var`. A note never hides the variable.

- **`80cc may not keep it in the frame slot its record names: the value shown can be stale or another variable's (z88dk#3233)`**: on every
  stack variable (a local or a parameter, in any frame, and the members or elements of it) of a function built with the 80cc compiler. 80cc can
  keep a local in a register and never write its slot, and it records a register-passed parameter at the slot of another variable. A static or
  a global never has it. In z88dk master the locals are fixed from 2026-11-04 and the parameters from 2026-11-05. JNEXT cannot tell a fixed build from an old one, so the note stays on every 80cc stack variable.
- **`shares its stack slot with <name>: only one of them holds its value`**: two variables that are both listed at this line and whose bytes
  overlap, each naming the other (several names are separated by commas). Two blocks that reuse one slot can both look open when the compiler's
  block numbers cannot tell which is, and 80cc's parameter record can name another variable's slot. Neither variable is hidden. Writing either
  is refused (the value would land in the other). A member of such a struct or array carries its variable's note (`shares ...`, `scope unknown`, `ambiguous`), and writing it is refused too.
- **`scope unknown: shown whatever block the PC is in`**: JNEXT cannot place this local's block, so it is listed, flagged, and not hidden. Writing one is refused. With 80cc **every block local** is flagged (or hidden where its block is provably closed): 80cc stamps some code with another block's scope, so JNEXT never lists an 80cc block local as certainly in scope. Also the case when there is no `--list` listing beside the map, or the build predates z88dk master of 2026-11-04 (which adds the block records).

The words `U1` and `U2` in the messages are JNEXT's names for two z88dk record bugs (a fastcall parameter's slot, a bitfield written as an `int`).

Refusals instead of a value: `location unknown (z88dk U1...)` for a `__z88dk_fastcall` parameter that sccz80 recorded where it is not;
`an array of unknown length: its debug record has no size`; `float format mbf32 is not read: the record does not say it (z88dk#3236)` (also
`mbf64`, `bbc` and `dai32`) when the map shows the program links one of those float libraries.

## What each z88dk can show

These are limits of what the compilers record, not of the debugger. One row per version and compiler. The last column is the date from which
z88dk master contains the fix (a release that contains it is not tracked here); `not fixed yet` means master does not have it. Every fix listed
with a date is in z88dk master at `0f0a1cf70e` (2026-11-05). No z88dk nightly up to `z88dk-20261105-ddfed0b200` has them all: that one has the fixes merged by about 00:10 UTC on 2026-11-05 and lacks those merged after 07:00 UTC
(enumerators, the zsdcc records, the 80cc parameters). Nightlies from `z88dk-20261106-*` on have all of them, and so does z88dk master built from source.

| z88dk, compiler | What the compiler records wrongly or not at all | How JNEXT shows it | Fixed in z88dk master from |
|---|---|---|---|
| 2.4, sccz80 | a `__z88dk_fastcall` function's parameters are recorded at the stack slot they would have without `__z88dk_fastcall`, where they are not | each parameter `location unknown (z88dk U1...)`, with a listing beside the map or (no listing) recognised from the function's first instructions. Without a listing a `float` or `long long` fastcall parameter is not recognised and is read from the recorded slot: a wrong value | 2026-11-04 |
| 2.4, sccz80 | a local or static array sized by its initialiser (`char s[] = "x";`) has no size; a global one is shown correctly | `an array of unknown length: its debug record has no size` | 2026-11-04 |
| 2.4, sccz80 | bitfields as whole `int`s | members marked `bitfield layout unknown (z88dk U2)`. A lone field in a struct smaller than an `int` is refused, `member '<name>' lies beyond the object`; a lone field that fits the struct is shown as its whole `int`: wrong when the padding bits are not 0, and for a `signed int` field | 2026-11-04 |
| 2.4, sccz80 | block numbers name the last block opened | a local declared after an inner block, in the same block, is not listed | 2026-11-04 |
| 2.4, sccz80 | a `for (int i ...)` variable gets the enclosing block's number | not listed inside its loop; two listed variables that share a slot both say `shares its stack slot with ...` | 2026-11-04 |
| 2.4, sccz80 | enumerators have no value and no type | the number, not the name | 2026-11-05 |
| 2.4, sccz80 | the float format is not recorded | a 6-byte float is read as z88dk's 48-bit format | not fixed yet |
| master (before the fixes above), sccz80 | the same as 2.4 | the same as 2.4 | as above |
| master, sccz80 | enumerators | the number (JNEXT recognises the `N:` and `E:` records master writes from 2026-11-05 and does not read the enumerator names yet) | 2026-11-05 |
| master, sccz80 | the float format of classic `--math-mbf32` and `--math-dai32` | `float format mbf32 is not read` | not fixed yet |
| master, zsdcc | a register variable has no live range | frame 0: `in BC; may be stale`; an older frame: `not available`; a removed variable: `optimised out` | not applicable |
| master, zsdcc | a `const` global has no record | not listed | not applicable (an SDCC bug, no fix in z88dk) |
| master, zsdcc | `zcc -O0` writes no variable records | the tab says so | not applicable |
| master, zsdcc | no enum information | the number | not fixed yet (master fixes sccz80 and 80cc only) |
| master, zsdcc | `C_LINE` records end in a TAB (a line reads as -1 in z88dk's own tools); a breakpoint inside a loop has no frame in ticks; the file name is not the one given | JNEXT reads the records with or without the TAB | 2026-11-05 |
| master, 80cc | a local is kept in a register; a register-passed parameter is recorded at another variable's slot | the value, with the 80cc note; a write is allowed and carries the note, and is refused where the slot is a listed variable's (`shares ...`) | locals: 2026-11-04; parameters: 2026-11-05 |
| master, 80cc | `C_LINE` has no level and no block; from 2026-11-04 it has both, but 80cc stamps some code with another block's scope: the jump that ends an `if`'s then-branch (with the `else` part's), the code after a trailing block, loop code hoisted before the loop (with the loop body's) | with the 2026-11-04 records and the `--list` listings beside the map, a block local is hidden where its block is provably closed and otherwise `scope unknown`: never listed as certainly in scope; without the listings, or with an older z88dk, every block local is `scope unknown`. At a then-branch's closing jump a then-branch local can be hidden (the Source tab shows that jump on the else line too) | 2026-11-04 (80cc's own stamps are not fixed) |
| master, 80cc | some locals have no record (constant-initialised, `float`) | not listed | 2026-11-04 |
| master, 80cc | a static or global array sized by its initialiser has no size | `an array of unknown length` | 2026-11-05 |
| master, 80cc | bitfields as whole `int`s | as 2.4 sccz80 | 2026-11-04 |
| master, 80cc | a `for (int i ...)` variable's block | a `for` opens no block on 80cc, so its variable and anything under the `for` stay `scope unknown` (above) | not fixed yet |
| master, 80cc | enumerators | as master sccz80 | 2026-11-05 |
| master, 80cc | the float format | as master sccz80 | not fixed yet |

More, in short:
- A plain `int` bitfield (`int s:4;`) is **unsigned** in sccz80 and 80cc and signed in zsdcc, so the value shown is each program's own reading; a `signed int` field is signed everywhere.
- On a `for` line itself (its initialiser, condition and increment), the loop variable is not listed with sccz80 and 80cc: those lines are recorded in the enclosing block. It is listed in the body, and again from the next line. With 80cc the loop variable is listed on its `for` line too, flagged `scope unknown`, because 80cc opens no block for a `for`.
- A source path that contains a `$` (`game$v2/main.c`): sccz80 and 80cc write the path into each local's and static's record, and `$` separates the record's fields, so JNEXT cannot read the records of that file and does not list its parameters, locals or file statics (its globals are listed). zcc itself needs the `$` escaped and cannot write `--list` listings for such a path. Keep `$` out of source paths.
- 80cc writes one scope per source line and stamps some code with another block's scope (see the table), so its block locals are never certain. On a line that holds code of two blocks the locals hidden are those of the block 80cc wrote first. Keeping a block's statements on lines of their own does not avoid it: the stamp comes from the compiler's order of work, not from the line.
- A name that two visible variables share (an inner block's `t` over the function's `t`): the innermost is the one `print`, a watch, `cvar()` and a write mean, as in gdb. The Variables tab lists both; the outer one is marked `shadowed`, or `may be shadowed` when the choice is not certain, that is when the inner one is `scope unknown` (that is 80cc's block locals; a parameter or level-1 local shadowed by a certain pick is `shadowed` on every compiler). A read of an uncertain name carries `ambiguous: 2 variables named t may be in scope here; this is the innermost (...)`, and a write to it is refused. A block-scoped `static` local is in scope only in its block, like a block local: where its block is closed it is not listed and the name means the local; where it is open it wins; on 80cc it is `scope unknown` like every block local. Each of the two rows acts on the variable it shows: Edit Value writes that variable and expanding it lists that variable's members. Watch is offered only on the innermost row, because a Watch is a name and the name reaches the innermost declaration; the shadowed row's Watch is disabled and its tooltip says so. Two block statics of one name in a function (zsdcc) share the path `function::name`: that typed path is refused with `ambiguous: 2 statics named n in f`, `monitor globals` marks both entries `(ambiguous path)`, and the Globals rows read and edit their own static but cannot be watched.
- Not a display limit, but wrong values in the program itself: a bitfield initialiser across two bytes written as one (fixed in z88dk master from 2026-11-04), and three 80cc code-generation bugs (fixed in master on 2026-11-04 and 2026-11-05; one only on the Game Boy target).
- Not JNEXT: defects of z88dk's own debuggers (one fixed in master from 2026-11-04) and ticks' reading of floats (not fixed yet).
- No C variables at all: zsdcc on 2.4 (`-debug` does not build), and the classic library on the Next with 2.4 (`-debug` fails; it works on master from 2026-10-23).
- Enumerations show their number, not their name; a `union` is shown as a `struct`.
- Before a function has set up its frame, its locals are `not allocated`; a variable at an offset below the stack the frame owns, too.
- Some variables cannot be written: see [Editing a value](#editing-a-value).

To read the same values from a script, GDB or ZRCP, see [Scripts and remote debuggers](09-scripts-and-remote-debuggers.md#c-variables).
