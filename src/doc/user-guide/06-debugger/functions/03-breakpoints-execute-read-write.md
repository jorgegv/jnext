# Breakpoints: execute, read, write, I/O

JNEXT has four kinds of breakpoint.

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
panel's **Add** dialog, as the two entries after Read/Write.

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

## Interrupt and NMI breakpoints

**Interrupt** and **NMI** breakpoints fire when the CPU takes a maskable interrupt or a
non-maskable one. They have no address (the field is greyed out in the dialog) and stop the
machine at the first instruction of the handler: `PC` is `$0038` for an interrupt in
interrupt mode 1 (the 48K ROM's) and `$0066` for an NMI. They are the last two entries in the
Add dialog's type list. A maskable interrupt only happens with interrupts enabled, so on a
program that never executes `EI` an Interrupt breakpoint never fires. The Multiface's button is
one way to raise an NMI. For a single stop at the next interrupt, without leaving a breakpoint
behind, use **Debug ▸ Run to Next Interrupt**.

## Ranges, port masks and conditions

The Add dialog has three optional fields that narrow a breakpoint.

**To** turns an Execute, Read, Write or Read/Write breakpoint into a **range**: it fires on any address from the
*Address* up to *To*, inclusive. Both can be numbers or symbols, and *To* may not be below the address. The row's
Address column reads `$8000-$80FF`. A range always covers the CPU's address space, never one particular memory: a
symbol's own memory (`$C000 P14`) is dropped when *To* is given. A source line cannot have a *To*.

**Mask** (the two IO types only, in hex) replaces the address rule above with your own port decode: the breakpoint
fires when the port number, ANDed with the mask, equals the address. `FE` with mask `FFFF` fires only on the exact
port `00FE` and not on `7FFE`, where the plain `FE` would; mask `0000` with port `0000` fires on every port. The row
reads `$00FE/FFFF`. The ULA answers *every even port*, so the plain `FE` (low byte FE) misses an `OUT (0xFC),A`;
port `0000` with mask `0001` fires on every ULA access. A port with bits outside the mask could never match and is refused, and so is mask `FFFF` with
port `0000`.

**Condition** is a [debugger-script expression](../scripting/04-reading-the-machine.md) such as `B == 2` or
`A != 0 and mem[0x9000] == 1`, written in the names of the breakpoint's own kind (a Read or Write can use `ADDR`,
`VALUE`; a Write also `PREV`; an IO breakpoint `PORT`). The breakpoint fires only when the expression is true: the
machine does not even stop otherwise, and a conditional breakpoint's **Hits** counts only the true hits. The text is
checked when you press OK: one that does not compile, names something the kind does not carry, or could not be saved
and read back as the same text (for example one with a `;` comment or a line break) is refused with the reason, and the
dialog stays open. If the expression fails while the program runs (a division by zero, a memory read outside the 64 KB),
the breakpoint simply does not fire, and the row's **Condition** cell shows the error as a tooltip.

Interrupt and NMI breakpoints take a condition too; count-only works with all of these.

A range, a port mask or a condition makes a breakpoint of its own, apart from the plain breakpoint at the same address:
both can exist, as two rows. The disassembly gutter and the Source tab's BP column draw and toggle **plain** breakpoints
only.

## Counting hits instead of stopping

Every breakpoint has a **Hits** count in the [Breakpoints panel](../panels/08-breakpoints.md): how
many times it fired since you made it, or since you pressed **Reset Hits**. A stop breakpoint
counts each time it stops the machine. Tick **Count hits only (don't stop)** in the Add dialog
and the breakpoint counts and lets the machine run on: the list shows it as `Execute (count
only)`. It is the way to ask "how often is this routine called?" or "how many times does the
frame handler read this variable?" without stepping through every hit. Any type can be
count-only, Interrupt and NMI included.

The count is runtime state: Save as Script does not keep it, and a breakpoint you **Edit**
starts again from 0 (an edit makes a new breakpoint). Disabling a breakpoint stops its counting
and turning it back on keeps the count; with the **Breakpoints enabled** master switch off
nothing counts.

The disassembly gutter and the Source tab's BP column draw only plain (stopping) breakpoints,
so a count-only breakpoint is never drawn there. Clicking the gutter on its line adds a plain
breakpoint beside it, and the two are separate rows.

**Breakpoints ▸ Clear All Breakpoints** removes all four kinds at once.

## Saving and loading breakpoints

**Breakpoints ▸ Save as Script…** writes the debugger window's own breakpoints to a
`.jds` file (the [debugger scripting language](../scripting/index.md)), and
**Breakpoints ▸ Load from Script…** reads one back, in this session or a later one. Each
breakpoint is one plain rule that stops the machine:

```
; jnext debugger breakpoints (Breakpoints > Save as Script).
bp1: on execute 0x8000 do stop end
disabled bp2: on read_write 0x9000 do stop end
bp3: on io_read 0xFE do stop end
bp4: on execute "main.bas:10" do stop end
bp5: on execute @banked do stop end
bp6: on interrupt do stop end
bp7: on nmi do stop end
bp8: on execute 0x8100 do end
bp9: on write 0x9000..0x90FF do stop end
bp10: on io_read mask 0xFFFF value 0x00FE do stop end
bp11: on execute 0x8002 when B == 2 do stop end
```

The file keeps each breakpoint's type, address (or range, port mask and condition: `on read 0x9000..0x90FF`,
`on io_read mask 0xFFFF value 0x00FE`, `... when B == 2 do stop end`), **On** state and whether it is count-only (a rule with an empty body,
`do end`: the engine counts its hits and does not stop; `jnext --script FILE` runs it the same way). A source-line breakpoint is
written as its line, so it arms every place the line compiled to when you load it. An Execute
breakpoint tied to the memory its code lives in (`$C000 P14` in the list) is written as the
symbol that names that address in that memory, so loading it needs the same symbols
loaded first.

A condition is written exactly as typed, on one line. A breakpoint that has no faithful spelling is **left out, never written approximately**:
the file gets a `; not saved:` comment where it would have been, and one warning box lists
what was left out. That happens to a qualified Execute breakpoint no symbol names, to a source
line that was partly removed in the Disassembly or that the loaded source map no longer
resolves, and to a file name a script string cannot hold. Not saved either: breakpoints that
another client (GDB, ZRCP, DZRP, a script) set, and the master switch.

**Load adds to the breakpoints you have**; it does not replace them (use **Clear All
Breakpoints** first for that). A breakpoint that already exists takes the **On** state
the file gives it, and a source line that is already armed on the same places is not added
twice. A file is loaded whole or not at all: anything that is not one of these breakpoints (`once`, a message, `log` or other actions, a `page` qualifier,
an IO range, a condition over several lines or one that does not check for its kind, other events than the ones the panel has) is refused with the line and column,
and nothing is added. The same file runs from the command line with `jnext --script FILE`.

## Turning them off instead of deleting them

Each row of the [Breakpoints panel](../panels/08-breakpoints.md) carries an
**On** checkbox, and the panel carries a **Breakpoints enabled** master switch.
Unticking a row suspends that one breakpoint; unticking the master switch
suspends all of them at once. Nothing is deleted either way — addresses, types
and the individual On states all survive, so ticking the master switch back
gives you exactly the set you had. New breakpoints are always created enabled,
whichever route you create them from.

A suspended execute breakpoint is drawn in the disassembly gutter as a hollow
red ring rather than a filled dot, so it is still visible and still obviously
not armed. Stepping and **Run to Here** are unaffected by the master switch.

## Closing the debugger window disarms them

All four kinds are checked only while the debugger window is **open**. Close it
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
