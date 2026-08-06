# Breakpoints: execute, read, write

JNEXT has two kinds of breakpoint.

**Execute** breakpoints fire before the instruction at that address is
executed. Set one by clicking the disassembly gutter, from the right-click
menu, from the Breakpoints panel, or from **Breakpoints ▸ Add Execute
Breakpoint…**.

**Data** breakpoints fire on access to an address: **Read**, **Write** or
**Read/Write**. They are checked as the access happens, but the machine stops
*after* the current instruction completes — so when you land, the access has
already taken effect and `PC` is on the next instruction. Set them from the
Breakpoints panel, from **Breakpoints ▸ Add Read/Write/Read-Write
Breakpoint…**, or from the disassembly right-click menu, which pre-fills the
address from an immediate or from a register's current contents.

**Breakpoints ▸ Clear All Breakpoints** removes both kinds at once.

## Closing the debugger window disarms them

Both kinds are checked only while the debugger window is **open**. Close it
and your breakpoints stay in the list but nothing stops at them — the
emulator skips the check entirely so that a machine nobody is watching runs
at full speed.

If you want them to keep firing with the window closed, start JNEXT with
`--persistent-breakpoints`. A hit then pauses the machine and reopens the
debugger window at the breakpoint, exactly as if you had left it open. It is
a command-line option only, and it costs the per-instruction breakpoint check
for the whole run, which is why it is not the default.

There are no I/O port breakpoints. To trap on port activity, use the
[magic port](08-the-magic-breakpoint-and-the-magic-port.md) — it logs every write to
a port you choose — or put an execute breakpoint on the `OUT` itself.
