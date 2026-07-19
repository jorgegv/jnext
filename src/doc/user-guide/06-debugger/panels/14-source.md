# Source

The Source tab shows the source file and highlights the position mapped to the
paused instruction. It appears after an sjasmplus SLD v1 map has been loaded,
either manually with **Map > Load SLD Source Map…** or automatically from a
same-stem `.sld` or `.sld.txt` beside a loaded NEX.

The file is re-read when its modification time changes, so an edit made while
debugging appears on the next refresh. If the current instruction has no source
mapping, the panel says so and Disassembly remains the authoritative view.

Source locations can include the physical 8K page. That matters in banked Next
programs: two routines may occupy the same logical address at different times,
and the Source tab follows the page currently mapped into the CPU slot.
