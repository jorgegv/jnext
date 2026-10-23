# Source

The source line the CPU is running, read from the program's source map
([Source-level debugging](../functions/14-source-level-debugging.md)).

At the top, the source steps — **Into**, **Over**, **Out**, **Back** and
**Reverse** — and **Load SLD...** / **Load Memory.txt...** to load a source map
or NextBuild symbols by hand. The steps are available while the machine is
paused and a source map is loaded; **Back** and **Reverse** also need the trace
log and rewind. The same steps and loads are also in **Debug ▸ Source**.

Under them, a line naming the position — `main.bas:120  $8A3C  page 2E`, or
`ROM or overlay` when no RAM page supplies the address — and
the source file with that line highlighted. When the PC is in code with no
source record (the compiler's runtime, a library, the ROM) the line says so;
use the disassembly there.

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
location is remembered as a full path, whatever directory you loaded it from). A file that
changes on disk is reloaded at the next pause.
