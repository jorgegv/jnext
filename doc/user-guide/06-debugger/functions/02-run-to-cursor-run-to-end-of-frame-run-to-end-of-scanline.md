# Run to cursor, run to end of frame, run to end of scanline

**Run to Here** is the run-to-cursor: select a line in the disassembly and press
**Enter**, or use the right-click menu. Execution resumes and stops when that
address is reached. If it is never reached, the machine keeps running.

**Run to EOSL** (Run to End of Scan Line) resumes until the end of the current
scanline. It is the tool for stepping through a raster effect one line at a
time: run to EOSL, look at the Video panel, repeat.

**Run to EOF** (Run to End of Frame) resumes until the end of the last visible
scanline of the frame, at which point every framebuffer row has been rendered
and the Video panel shows a complete picture. If you are already past that
point, it targets the same position in the next frame.

Both are on the toolbar and in the **Debug** menu, and both require the machine
to be paused already.
