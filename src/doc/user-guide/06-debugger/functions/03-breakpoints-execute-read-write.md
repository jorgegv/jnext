# Breakpoints: execute, read, write, I/O

JNEXT has three kinds of breakpoint.

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

**I/O** breakpoints fire on a port access: **IO Read** on an `IN`, **IO Write**
on an `OUT`. They stop the machine the same way data breakpoints do — after
the instruction that made the access — and they are set from the Breakpoints
panel's **Add** dialog, where they are the last two entries in the type list.

The address you type is a *port*, and ZX ports are not addressed like memory:
an address of **00**–**FF** matches any port with that low byte, and an address
of **0100** or above matches that exact 16-bit port.

That split is not arbitrary — it is how the machine decodes ports and how the
port map itself writes them down. `OUT (254),A` puts the accumulator in the
high byte, so the ULA is reached at `01FE`, `7FFE`, `FEFE` and 253 other
addresses; typing `FE` catches all of them. Ports that really are decoded on
all sixteen lines are written that way, so `243B` catches the NextREG select
port and *not* `253B`, whose low byte is the same. One consequence: the exact
port `00xx` cannot be named — no Next port is decoded that way, so nothing is
lost.

Read and write are separate breakpoints; if you want both on a port, add both.
An I/O breakpoint never fires on a *memory* access at the same number, and a
memory breakpoint never fires on a port access.

**Breakpoints ▸ Clear All Breakpoints** removes all three kinds at once.

## Closing the debugger window disarms them

All three kinds are checked only while the debugger window is **open**. Close it
and your breakpoints stay in the list but nothing stops at them — the
emulator skips the check entirely so that a machine nobody is watching runs
at full speed.

If you want them to keep firing with the window closed, start JNEXT with
`--persistent-breakpoints`. A hit then pauses the machine and reopens the
debugger window at the breakpoint, exactly as if you had left it open. It is
a command-line option only, and it costs the per-instruction breakpoint check
for the whole run, which is why it is not the default.

If you want a *log* of port activity rather than a stop at it, use the
[magic port](08-the-magic-breakpoint-and-the-magic-port.md) instead — it prints
every write to a port you choose and lets the machine run on.
