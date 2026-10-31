# Source

The source line the CPU is running, read from the program's source map
([Source-level debugging](../functions/14-source-level-debugging.md)).

At the top, the source steps — **Into**, **Over**, **Out**, **Back** and
**Reverse** — and **Load Source Map...** / **Load Memory.txt...** to load a source map (an SLD or a z88dk map)
or NextBuild symbols by hand, and **Locate File...**, enabled while the tab says
*source file not found*. The steps are available while the machine is
paused and a source map is loaded; **Back** and **Reverse** also need the trace
log and rewind. The same steps and loads are also in **Debug ▸ Source**.

Under them, a line naming the position — `main.bas:120  $8A3C  page 2E`, or
`ROM or overlay` when no RAM page supplies the address — and
the source file with that line highlighted. When the PC is in code with no
source record (the compiler's runtime, a library, the ROM) the line says so;
use the disassembly there.

Left of the text is a gutter, like the disassembly's: a **BP** column and the file's
own **Line** numbers. A line carrying a breakpoint shows the same red mark as the
disassembly gutter: a filled dot for a breakpoint that can fire, a hollow ring for a
suspended one (individually disabled, or the master switch off). A line some of whose
places carry a breakpoint and some do not (one place of a routine in two banks was removed
in the disassembly, or a breakpoint sits inside the line, not at its start) shows the
**left half** of the dot or ring. Click in the gutter to set or clear the line's
breakpoint: a line that compiled to several places gets ONE breakpoint armed at every place,
listed in the Breakpoints panel as `main.bas:11 (2 locations)`, and a second click removes it
(and anything else the line shows). A line with no code adds nothing and the tooltip says
*no code at main.bas:3*. Hover the gutter for the words: the addresses and pages of the
breakpoint, how many of the places carry it, or why there is none. A line of banked code is
marked only while its page is mapped; a click on it still removes the breakpoint, and the
tooltip says *not mapped now*. The column shows and edits only the debugger window's own breakpoints, as the
disassembly does; those of GDB, ZRCP, DeZog or a script are in the Breakpoints list only, and a click
never removes them ([whose breakpoints the gutters show and change](08-breakpoints.md#whose-breakpoints-the-gutters-show-and-change)). It works
while the machine runs.

### Right-click menu

Right-click in the text, or in the gutter, for the same actions the disassembly offers,
on the line under the pointer. A right-click outside the selection first selects that line;
inside it, the selection stays (so a dragged range can be copied).

- **Copy** (Ctrl+C), **Copy with Addresses** and **Select All** (Ctrl+A). Copy with Addresses
  puts each selected line on the clipboard as `main.bas:11  $8002 @05, $9000 @06  REM line 11`:
  every address the line compiled to, with its page, or `-` for a line with no code.
- **Toggle Breakpoint**: the gutter click.
- **Run to Here**, also **Enter** in the text: run until the first place of the line is reached
  (see [Running to a source line](../functions/14-source-level-debugging.md#running-to-a-source-line)).
- **Show in Disassembly**: the disassembly moves to the line's address and selects it. A line
  that compiled to several places has a submenu, one item per place; a place whose page is not
  mapped now is disabled.
- **Watch** and **Break on Read / Write** entries, as in the disassembly menu, for the labels at
  the line's address, and for what is under the pointer: a symbol (a label or a name of the
  map, written as the map has it), a number (`$5800`, `0x5800`, `5800h`, `#5800` or decimal),
  or a register in parentheses (`(hl)`, `(ix+2)`). They are by CPU address, whatever is mapped
  there. A C variable is found under its map name today (`_counter`), not as `counter`.

On a line with no code Toggle Breakpoint, Run to Here and Show in Disassembly are disabled, and
their tooltip says *no code at main.bas:3*.

A tape program's map is attached before the program has loaded, and the tab adds
*Source map not verified yet: it is checked when the program first runs its code*
under the position line until it has; if the program then does not match, the map
is dropped and the tab says why.

While the machine runs the tab says **Running**; the next pause shows the line
again. While it is paused you can scroll the file: the view goes back to the
current line when the machine stops again (a breakpoint, a Pause, a step, a
rewind), even at the same line, or when the line shown changes (another map
loaded), and not before.
When a program's source map was found and refused, or was dropped by a reset or a
state load, the tab says why in place of "No source map loaded" (`source map
'game.sld' rejected: …`); the status bar and the log say it too. The reason shown
is the current one: a later load replaces it. A map you loaded by hand that no
longer matches the program says so, with its file name, and has to be loaded
again.

The source file is found from the name the map records: as it is, if absolute,
else relative to the map's own directory or one of its parents (the map's
location is remembered as a full path, whatever directory you loaded it from),
else in the directories of the source search path
([Where the Source tab finds the files](../functions/14-source-level-debugging.md#where-the-source-tab-finds-the-files)).
A file that changes on disk is reloaded at the next pause.
