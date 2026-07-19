# Breakpoints: execute, read, write

JNEXT has two kinds of breakpoint.

**Execute** breakpoints fire before the instruction at that address is
executed. Set one by clicking the disassembly gutter, from the right-click
menu, or from the Breakpoints panel.

An address field accepts a loaded symbol name as well as hexadecimal. Use a
`$` or `0x` prefix when a symbol happens to look like a hexadecimal number.

With an SLD map, source-gutter breakpoints retain the physical 8K page as well
as the logical address, so the same address in a different bank does not stop.
Breakpoints entered in the panel remain logical-address wildcards and match
whichever page is currently mapped there.

**Data** breakpoints fire on access to an address: **Read**, **Write** or
**Read/Write**. They are checked as the access happens, but the machine stops
*after* the current instruction completes — so when you land, the access has
already taken effect and `PC` is on the next instruction. Set them from the
Breakpoints panel, from **Breakpoints ▸ Add Read/Write/Read-Write
Breakpoint…**, or from the disassembly right-click menu, which pre-fills the
address from an immediate or from a register's current contents.

**Breakpoints ▸ Clear All Breakpoints** removes both kinds at once.

There are no I/O port breakpoints. To trap on port activity, use the
[magic port](08-the-magic-breakpoint-and-the-magic-port.md) — it logs every write to
a port you choose — or put an execute breakpoint on the `OUT` itself.
