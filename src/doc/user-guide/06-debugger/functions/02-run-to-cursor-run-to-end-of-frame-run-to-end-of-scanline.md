# Run to cursor, run to end of frame, run to end of scanline

**Run to Here** is the run-to-cursor: select a line in the disassembly and press
**Enter**, or use the right-click menu. Execution resumes and stops when that
address is reached. If it is never reached, the machine keeps running. The Source tab has
its own **Run to Here** on a source line (right-click menu, or **Enter**).

**Run to EOSL** (Run to End of Scan Line) resumes until the end of the current
scanline. It is the tool for stepping through a raster effect one line at a
time: run to EOSL, look at the Video panel, repeat.

**Run to EOF** (Run to End of Frame) resumes until the end of the last visible
scanline of the frame, at which point every framebuffer row has been rendered
and the Video panel shows a complete picture. If you are already past that
point, it targets the same position in the next frame.

Both are on the toolbar and in the **Debug** menu, and both require the machine
to be paused already.

**Run to Next Interrupt** (in the **Debug** menu only, no toolbar button) resumes and stops at
the entry of the next maskable interrupt: the first instruction of the interrupt routine, so
`PC` is `$0038` in interrupt mode 1 (the 48K ROM's). It is the way to land in a game's frame handler. It needs the machine to be paused
already and the interrupts enabled (`EI`), and it works with the **Breakpoints enabled** master
switch off. It leaves nothing behind: any breakpoint you set stops it first, and the next
**Run** does not stop at an interrupt. To stop at every interrupt, add an
[Interrupt breakpoint](03-breakpoints-execute-read-write.md#interrupt-and-nmi-breakpoints).

None of these four has a key out of the box, but all four can be given one —
Run to Cursor then acts on the disassembly's current line from anywhere in the
debugger window, or on the Source tab's cursor line while that tab has the keyboard focus. See [Changing the keys](09-changing-the-keys.md).
