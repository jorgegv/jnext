# Debugging with z88dk-gdb

`z88dk-gdb` is z88dk's command-line debugger. It speaks the GDB remote serial
protocol, and JNEXT serves that protocol, so you can debug a z88dk program
running in JNEXT from a terminal: breakpoints on your own symbols, stepping,
registers, memory, and the source line you stopped on. (The `gdb` your
distribution ships cannot help here — it has no Z80 support; `z88dk-gdb` is
the client.)

## Build with a map file

`z88dk-gdb` finds your symbols in the linker map, so add `-m` to the `zcc`
line:

```console
$ zcc +zxn -vn -startup=31 -clib=sdcc_ix -SO3 mygame.c -o mygame \
      -subtype=nex -create-app -m
```

That writes `mygame.map` next to `mygame.nex`. (With `-debug` as well, JNEXT's own
Source tab steps through the C source too: see
[Building programs with z88dk](../source-debugger/02-building-with-z88dk.md); sessions are in
[Scripts and remote debuggers](../source-debugger/09-scripts-and-remote-debuggers.md).)

## Start JNEXT, then connect

Give JNEXT a TCP port:

```console
$ jnext --gdb-port 3333 --load mygame.nex
```

The log confirms it:

```
[13:10:21.837] [debugger] [info] gdb: listening on 127.0.0.1:3333
```

`--gdb-port 0` lets the system pick a free port and prints it in that line.
The server is off unless you give the option, and it works in every frontend —
the Qt window, the SDL build and `--headless`. It can run together with the
DeZog server (`--dzrp-port`): each has its own port and its own client.

Then connect, handing `z88dk-gdb` the map:

```console
$ z88dk-gdb -h 127.0.0.1 -p 3333 -x mygame.map
```

**Connecting stops the machine**, and `z88dk-gdb` shows where. JNEXT starts
your program as soon as it loads, so by the time you connect it is usually past
`main`: put breakpoints in code that runs again, such as your main loop.

## A session

```
> break _update_player   # a symbol from the map
> cont                   # runs until it is called
Hit breakpoint 1: @8412 (_update_player)
> reg                    # registers, and the flags
> stepi                  # one instruction
> nexti                  # one instruction, running a CALL through
> x /16 0x8000           # memory
> set hl 0x1234          # write a register
> quit                   # detach; JNEXT keeps running
```

Press **Ctrl-C** while the program runs to stop it.

## What to expect

- **Memory is what the Z80 sees now**: the 64 KB CPU view, with the current
  MMU mapping and any overlay (DivMMC, Multiface, Layer 2 write-over) as the CPU
  has it. A write that lands on ROM is refused; the parts of a write that do
  land (RAM, an overlay's RAM) stay written.
- **Your breakpoints are your session's.** They stop the machine whether
  JNEXT's debugger window is open or closed, and they are removed when
  `z88dk-gdb` disconnects — a crashed client cannot leave the machine stopped
  on one of them. `quit` resumes the machine if the stop was the client's own
  and nothing else is attached; with JNEXT's debugger window open, or another
  remote debugger connected, it stays paused and the pause becomes theirs. A
  pause you made in JNEXT's debugger window stays.
  In that window they appear in the Breakpoints panel as `Execute (client N)`, read-only, but are not drawn as a dot
  in the Disassembly or Source breakpoint columns, and a click there does not remove them (use `delete` in
  `z88dk-gdb`; see [Whose breakpoints the gutters show and change](../panels/08-breakpoints.md#whose-breakpoints-the-gutters-show-and-change)).
- **Both can drive the machine.** JNEXT's debugger window and `z88dk-gdb`
  control the same machine, and whichever acted last wins. If something else
  stops the machine while `z88dk-gdb` has it running, the client is told it
  stopped.
- **I, R, IFF1/IFF2 and IM are not in `reg`.** `z88dk-gdb`'s `set` writes the
  whole register set at once and zeroes every register it does not know, so
  those five are left out on purpose. Use `monitor regs` and `monitor set` (see
  below).
- **One client at a time.** A second connection is closed straight away.

## monitor: the Next-specific state

Recent `z88dk-gdb` versions (newer than 2.4) have a `monitor` command, which
reaches what the GDB protocol itself cannot express:

| Command | What it does |
|---|---|
| `monitor help` | lists these commands |
| `monitor regs` | I, R, IFF1, IFF2, IM, HALT and MEMPTR, and the register pairs |
| `monitor set i\|r\|iff1\|iff2\|im VALUE` | writes one of those |
| `monitor mmu` | the eight MMU slots and the paging ports. Each slot line ends with the memory the slot reads now (`P0A`, `ROM0`, `DIVROM`...), and on the Next its address in the machine's memory (`$048000`, `BRAM5+$0000`); the tags are the ones of [the trace log](06-the-trace-log.md) |
| `monitor mmu SLOT PAGE` | maps a page at a slot, as `NEXTREG 0x50+SLOT` does |
| `monitor nextreg [REG [VALUE]]` | all NextREGs, one, or writes one |
| `monitor page N OFF [LEN]` | a hex dump of physical 8 KB page N |
| `monitor in PORT` / `monitor out PORT VALUE` | a real port access — it can change the device's state, so both are refused while an RZX recording or playback is running |
| `monitor sym NAME\|ADDR` | JNEXT's own symbol table (the debugger window's map); an address is named only by a label that is visible in the page mapped there now, and the answer carries the tag of that memory (`$C004 P14 = banked+4`, `banked = $C000 P14`) |
| `monitor src [ADDR]` | the source line JNEXT's own map gives an address (default the PC) and the memory it is in, such as `$8CB8 main.c:19 P04`, or `$8CB8 unmapped P04`; a cross-check like `monitor sym`; ADDR may be a symbol |
| `monitor locals [FRAME]` | the C parameters and locals of a frame of the program (0, the default, is where the CPU is; 1 is its caller...); needs a z88dk `-debug` map. See [C variables](../source-debugger/09-scripts-and-remote-debuggers.md#c-variables) |
| `monitor globals` | the C globals, file statics and static locals, each with its value and the memory it lives in |
| `monitor print [FRAME] PATH` | one C value: `p->x`, `*p`, `g_matrix[1][2]`, `sum::calls`, `vars.c::s_dup`; a struct or an array prints whole |
| `monitor set var [FRAME] PATH = VALUE` | writes one C value (see [Changing a C variable](../source-debugger/09-scripts-and-remote-debuggers.md#c-variables)); the answer is the new value read back, or `refused: ` and the reason (and `E01`) |
| `monitor time` | frame, cycle, T-states and the raster position |
| `monitor reset [soft\|hard]` | resets the machine; it stays stopped |
| `monitor bp` | every breakpoint and watchpoint, and who set it |

Numbers may be written `0x..`, `$..` or in decimal.

## Source lines

`z88dk-gdb` does its own source mapping, from the map you give it with `-x`:
build the program with `-m -debug` and `break main.c:19`, `list` and `next`
work in the client. `monitor src` shows what JNEXT's own map (the one it
attaches when it loads the program) says about an address, so the two can be
compared.

## Headless runs

With `--headless`, a client that holds the machine stopped holds its frames
too: nothing is emulated, and JNEXT sleeps until the next command. The
automatic exit is still a hard bound, counted in wall time while the machine
is held, exactly as for DeZog.

## Security

The server listens on `127.0.0.1` only. `--debug-listen-address` widens it (it
is refused unless a server port is given too). **The GDB protocol has no
authentication**: anyone who can reach the port controls the machine and can
read and write all of its memory.

---

For the command-line options, see **jnext**(1) or
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
