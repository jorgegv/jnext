# Compiler symbols

Load a symbol table with **Map ▸ Load Symbols**, which offers three formats:

- **Z88DK Format…** — the `.map` file `zcc` writes next to your binary. Only
  entries marked `; addr` are used; `; const` entries are compile-time
  constants and are skipped, since they are not addresses.
- **Boriel/NextBuild Memory.txt…** — the `Memory.txt` file emitted by a
  NextBuild/Boriel build.
- **Simple Format (48K ROM)…** — plain `NAME = $ADDR` lines, one per line,
  `;` for comments.

When a NEX starts, JNEXT automatically checks beside it for a same-stem
`game.Memory.txt`, then `Memory.txt`. Prefer the same-stem form when a directory
contains more than one build.

Once loaded, symbols appear wherever an address is shown and a name is known:
the disassembly replaces a matching 16-bit immediate with the symbol name, the
Call Stack names call targets, the Breakpoints panel gains a Symbol column, and
the disassembly right-click menu offers to watch the symbol by name.

Where two symbols share an address, the first one in the file is the displayed
name; aliases still resolve when typed into a breakpoint or watch address.

Symbols are logical 16-bit addresses. Physical-page identity comes from the
[SLD source map](09-source-level-debugging.md), not from `Memory.txt`.
