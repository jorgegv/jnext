# Source

The source line the CPU is running, read from the program's source map
([Source-level debugging](../functions/14-source-level-debugging.md)).

At the top, the source steps — **Into**, **Over**, **Out**, **Back** and
**Reverse** — and **Load SLD...** / **Load Memory.txt...** to load a source map
or NextBuild symbols by hand. The steps are available while the machine is
paused and a source map is loaded; **Back** and **Reverse** also need the trace
log and rewind.

Under them, a line naming the position — `main.bas:120  $8A3C  page 2e`, or
`ROM or overlay` when no RAM page supplies the address — and
the source file with that line highlighted. When the PC is in code with no
source record (the compiler's runtime, a library, the ROM) the line says so;
use the disassembly there.

The source file is found from the name the map records: as it is, if absolute,
else relative to the map's own directory or one of its parents (the map's
location is remembered as a full path, whatever directory you loaded it from). A file that
changes on disk is reloaded at the next pause.
