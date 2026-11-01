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
use the disassembly there. The start-up code of a z88dk program built with `--list`
shows its m4 text from the listing, the label ending `(m4 output, from prog.lis)`;
**Locate File...** does not apply to it.

The text is coloured by its language: C (`.c`, `.h`), Z80 and Z80N assembly (`.asm`, `.s`,
`.z80`, `.inc`; the sjasmplus and z88dk z80asm directives are recognised) and Boriel ZX Basic
(`.bas`). A file with another extension is recognised by its first lines, and shown plain when
they do not decide. Assembly written inside C (`#asm`, `__asm`) or inside Boriel Basic (`asm` ...
`end asm`) is coloured as assembly.

Ten classes are coloured: *Text* (identifiers and everything else), *Keyword*, *Instruction*,
*Preprocessor*, *Label*, *String*, *Char*, *Integer*, *Float* and *Comment*. In C, a keyword is a
Keyword, a `#` line is Preprocessor and a character constant is Char; a number is Integer, or Float
when it has a point or an exponent (`1.5`, `1e-3`, `0x1p-3`). In assembly, a directive (`defb`,
`section`, `include`, ...) is a Keyword, a mnemonic or a register is an Instruction, a label at the
start of the line is a Label and every number is an Integer. In Boriel ZX Basic, a Basic keyword is
a Keyword, a `#` line is Preprocessor, a line number is a Label and a number is Integer or Float. The
punctuation and operators (`: , ; ( ) [ ] { } + - * / = < > & | ! ~ ^ % ? .`, a multi-character one
such as `->` or `<<=` as a whole) are coloured as keywords; a comment, string, character or number
keeps its own colour.

The colours come from a scheme, chosen in **Settings > Preferences… > Debugger** or for one run with
`--set source.colour_scheme=WORD`: **Auto** (the default) is Light or Dark after the application's
theme, **Light** and **Dark** are the two fixed schemes, and **Borland** is Borland C++ 3.1, yellow
text on a blue background. A scheme sets the Source tab's background, text colour and syntax colours
and the colours of the current-line bar, and applies at once. The current line keeps its bar in every
scheme, and the gutter (line numbers and breakpoint marks) is not affected.

With [memory coverage](../functions/15-memory-coverage.md) on, every line of the
shown file that ran has a tinted background that follows the colour scheme (pale
green in Light, a dark green in Dark, a dark teal in Borland; the current line keeps
its highlight).

A small amber arrow at the right edge of the gutter marks the line the CPU is on. The
highlight alone can also mean a line you selected by right-clicking; the arrow is only ever
on the CPU's line. When a toolchain
attributes the same address to more than one source line, the position line names the
primary one and lists the others, `main.bas:11  $8002  page 2E  also: util.bas:7`.

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

### Following the disassembly

The Source tab and the [Disassembly](03-disassembly.md) follow each other's selection. They are
two views of the same code, and a selection in one lights what it covers in the other:

- Click a source line, or select several, and every instruction compiled from it is tinted pale
  violet in the Disassembly: all the places of a line that compiled to several (`main.c:18` of a
  z88dk program has two). A line that is only in a page not mapped now tints nothing, and a line
  without code tints nothing. The Disassembly scrolls to the first of those addresses only when none
  of them is in view, and only for a click you made in the Source tab; a stop never scrolls it.
  The PC row keeps its yellow.
- Click an instruction, or drag over several, in the Disassembly and the Source cursor goes to the
  source line of the instruction you clicked, and the lines of every instruction in the range are
  tinted the same violet. An address with no source line (the ROM, a library) leaves the Source tab
  as it was.

When the line you clicked is not the line the CPU is on, the tab is **browsing**: the label describes
the address you clicked, not the PC, and ends `(browsing; the PC is at main.bas:10, $8000)` (or
`the PC is in no source line`), and the Source tab shows that line's file, which may be another one
than the PC's. The arrow stays on the CPU's own line, and the highlight too, when that line is in
the file shown. The view stays where you put it through the refreshes while paused. If the clicked line's file cannot be found, the tab says so and **Locate File...** and **Go to PC Line** stay available. Browsing ends at
the next stop (a breakpoint, a step, a Pause, a rewind), when the machine runs, when the source
map is cleared or another is loaded, and when you press **Go to PC Line**, which is enabled only while browsing and shows
the CPU's line again. A click on an instruction of the CPU's own line is not browsing.

While the machine runs, neither tab follows the other.

The tint is a pale violet in Light and a dark violet in Dark and Borland; it differs from the
current-line bar and the coverage tint in every scheme. Nothing is configurable: the two views always
follow each other.

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
  mapped now is disabled. (The disassembly's own right-click menu has the counterpart, **Show in
  Source**, while a source map is loaded.)
- **Watch** and **Break on Read / Write** entries, as in the disassembly menu, for the labels at
  the line's address, and for what is under the pointer: a symbol (a label or a name of the
  map; a z88dk C variable or function is offered under its C name, `counter`, whether the source
  writes `counter` or `_counter`), a number (`$5800`, `0x5800`, `5800h`, `#5800` or decimal),
  or a register in parentheses (`(hl)`, `(ix+2)`). They are by CPU address, whatever is mapped
  there.

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
