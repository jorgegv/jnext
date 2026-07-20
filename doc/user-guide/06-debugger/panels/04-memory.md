# Memory

![Memory panel](../../img/debugger-memory.png)

A hex editor over the address space: address, sixteen bytes in two groups of
eight, and the ASCII rendering.

- The **Addr** box takes a hex address (`5800`, `$5800` or `0x5800`) and centres
  the view on it.
- The **page selector** chooses the window. `CPU View` shows the whole 64K as
  the CPU sees it; `Slot 0`–`Slot 7` restrict the view to one 8K slot and name
  the page currently mapped there, which updates as the program pages.
- In `CPU View`, rows are colour-coded: the row containing `SP` in orange,
  pixel VRAM `$4000`–`$57FF` in cyan, and attributes `$5800`–`$5AFF` in yellow.

To edit, click a byte and type two hex digits — the write happens on the second
digit and the selection advances to the next byte. Arrow keys move the
selection, **Page Up/Down**, **Home** and **End** scroll, and **Esc** clears
the selection. Writes go through the MMU exactly as a `LD (nn),A` would, so
ROM stays read-only.
