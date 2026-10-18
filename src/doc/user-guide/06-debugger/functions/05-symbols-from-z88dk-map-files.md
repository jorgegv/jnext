# Symbols from MAP files

Load a symbol table with **Map ▸ Load MAP File**, which offers three formats:

- **Z88DK Format…** — the `.map` file `zcc` writes next to your binary.
  Entries marked `; addr` are addresses. Entries marked `; const` are
  compile-time constants, such as the section bound `__data_crt_head`: they
  are kept by name only, so a script's `@__data_crt_head` resolves, but they
  never name an address in the views below. A name the file defines twice
  takes its first definition; a name that is both an address and a constant
  is the address.
  A map built with `zcc -debug` is accepted: its per-line markers
  (`__C_LINE_…`, `__ASM_LINE_…`) and C debug records (`__CDBINFO__…`) are
  not symbols and are ignored, so the function labels keep their addresses.
- **Simple Format (48K ROM)…** — plain `NAME = $ADDR` lines, one per line,
  `;` for comments.
- **sjasmplus Format (--sym, --exp)…** — the symbol file sjasmplus writes with
  `--sym=FILE` or `--exp=FILE`, lines such as `main.loop: EQU 0x00008006`.
  `--sym` lists every label, sorted by name; `--exp` only the labels the source
  `EXPORT`s, in the order it exports them. The names are sjasmplus's full ones
  (`main.loop`, `gfx.draw`). The file records no type, so every entry is a
  label: an `EQU` size or a struct field offset names an address too (a
  disassembled `LD BC,$1B00` can read `LD BC,SCRSIZE`), and `EXPORT` is how you
  choose what is loaded. A value above `0xFFFF`, or a negative one, is ignored.
  The file records no memory bank either: of two labels at one address in
  different banks the first names the address, and both resolve by name. A
  Windows file, with CRLF line ends, loads as it is.

Once loaded, symbols appear wherever an address is shown and a name is known:
the disassembly replaces a matching 16-bit immediate with the symbol name, the
Call Stack names call targets, the Breakpoints panel gains a Symbol column, and
the disassembly right-click menu offers to watch the symbol by name. The open
panels show the names as soon as the file is loaded.

Where two symbols share an address, the first one in the file wins.

On the command line, `--map FILE` loads a z88dk `.map` or an sjasmplus `--sym`
or `--exp` file. It tells them apart by what is in the file, not by its name
(z88dk's assembler also writes a `.sym`, in the map format). A script names a
symbol as `@name`, an sjasmplus label with its dots as `@main.loop`: see
[Symbols](../scripting/04-reading-the-machine.md#symbols-name).

NextBuild / Boriel ZX Basic programs come with a `Memory.txt` instead, which
JNEXT loads by itself when the program is loaded: see
[Source-level debugging](14-source-level-debugging.md).
