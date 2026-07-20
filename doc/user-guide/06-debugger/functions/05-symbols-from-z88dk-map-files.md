# Symbols from Z88DK MAP files

Load a symbol table with **Map ▸ Load MAP File**, which offers two formats:

- **Z88DK Format…** — the `.map` file `zcc` writes next to your binary. Only
  entries marked `; addr` are used; `; const` entries are compile-time
  constants and are skipped, since they are not addresses.
- **Simple Format (48K ROM)…** — plain `NAME = $ADDR` lines, one per line,
  `;` for comments.

Once loaded, symbols appear wherever an address is shown and a name is known:
the disassembly replaces a matching 16-bit immediate with the symbol name, the
Call Stack names call targets, the Breakpoints panel gains a Symbol column, and
the disassembly right-click menu offers to watch the symbol by name.

Where two symbols share an address, the first one in the file wins.
