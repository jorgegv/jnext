# Starting from the command line

`--debugger` starts a program already under the debugger: the machine is
**paused** where the program starts, so you can set breakpoints, read the
code and step before a single instruction of it has run. No breakpoint, no
Pause button pressed in time, no wait for a menu.

```console
$ jnext --debugger game.nex
```

## Where it stops

The stop is right after the program is loaded, before it has run an
instruction:

| You give | It stops at |
|---|---|
| a **NEX** file | its entry point (the PC in its header) |
| a **snapshot** (`.sna`, `.z80`, `.szx`, `.jns`) | the PC the snapshot restores |
| an **RZX** recording | the PC of the snapshot it starts from |
| **`--inject`** | the PC the injected program is started at |
| nothing | power-on, before the first instruction (PC 0) |
| a tape (`.tap`, `.tzx`, `.wav`) | the first instruction fetched from memory the tape loaded, after its last block (see below) |

A tape runs its loader first, so the stop is the program it starts: the first
instruction fetched from memory **the tape loaded**, once its last block has
loaded. A custom loader is itself loaded by the ROM, so with a turbo loader the stop
is in the loader's code right after its last block. Fast load and real time,
TAP, TZX and WAV all stop at the same place, and on the 128K and +3 the ROM's own
paging routines in RAM are never the stop, because the tape did not load them. A
tape that cannot carry code (a BASIC program, a SCREEN$) stops at the first
instruction after its last block. A **WAV** has no blocks to tell the two apart, so it
always takes the code rule: a BASIC-only WAV is never stopped, and a recording with
noise in its trailing silence can stop late (the end of the tape is taken as its last
signal edge). A trailing pure-data or direct-recording block of a TZX counts as data
in real time only. Two limits: a BASIC program that runs code from a
`REM` stops at the end of its last block, not in the `REM`; and a tape that loads
code which never runs is never stopped (give it `--break-at`, which is armed before the
first block).

A NEX whose header entry is 0 is a load-only file: nothing is executed, the CPU
is parked, and that is where it stops. A program that cannot be loaded stops
where the machine is, and the log says the load failed. Each stop is logged:

```
[13:10:21.837] [platform] [info] debugger: stopped at $8000 (the NEX entry)
```

## In the Qt window

The debugger window opens, with the machine paused at that point. From there it
is the debugger you know: **Run** resumes, and everything in this chapter
applies.

## With `--headless` and in the SDL build

These have no debugger window, so the run waits for a remote debugger: DeZog,
`z88dk-gdb` or any ZRCP client. The machine starts paused and a debugger that
connects finds it paused at the stop point, reads that PC and continues.

JNEXT serves DZRP on port **11000**, the port DeZog's `cspect` remote assumes
(see [Remote debugging with DeZog](10-remote-debugging-with-dezog.md)):

```console
$ jnext --headless --debugger game.nex
```

If you give a protocol port yourself, only those ports are served and DZRP is
not opened:

```console
$ jnext --headless --debugger --gdb-port 2331 game.nex     # GDB only
$ jnext --debugger --zrcp-port 10000 game.nex              # ZRCP only
```

An explicit `--dzrp-port` is used as given; `--debug-listen-address` works with
`--debugger`. The log says what it waits on
(`debugger: waiting for a debugger on dzrp 127.0.0.1:11000`). Waiting uses no
CPU, and `--delayed-automatic-exit` still ends the run on time.

Because a session is wanted, a stop in these two frontends pauses and waits
instead of ending the run with exit code 3, as it does without `--debugger`:
under `--script`, a `stop` or a failed `assert` pauses, while a script's own
`exit` still exits.

## `--break-at`: stop somewhere else

`--break-at ADDR|SYMBOL` runs to an address and stops there instead of at the
default point. It implies `--debugger`.

```console
$ jnext --break-at _main game.nex
$ jnext --break-at 0x8123 game.nex
$ jnext --break-at main.c:40 game.nex
```

It takes the same forms as an Execute breakpoint in the Breakpoints panel:

- a hexadecimal address: `8123`, `$8123` or `0x8123`. There is no decimal form:
  `32768` is not an address;
- a symbol from the program's own map (a `.map` or `.sld` beside it) or from
  `--map`, such as `_main`; `@name` always means the symbol, even when the name
  reads as a hex number (`@bad`);
- a source line, `file:line`, when a source map is loaded.

A symbol or a source line keeps the memory it was assembled for, so a routine
in a banked page stops only while that bank is mapped. The stop is armed when the
program is loaded, so it also catches code a tape runs while loading. It is a
one-shot: it fires once, stays out of the Breakpoints panel, and is gone.

A value that is neither an address, a symbol nor a source line is refused when
the program is loaded, before it has run, with the reason and exit code 1.

## After the stop

Once the machine has run again, `--debugger` has nothing more to do: later
breakpoints, steps and pauses are the debugger's as usual. Another debugger
pausing the machine first does not cancel the stop: a DeZog that attaches while the tape is
still loading pauses the machine, and when it continues the stop is still ahead (it is put back
at the next tick, whether the machine is paused or running, and the other debugger's own Step Out,
Run to or breakpoints are not disturbed). If that debugger holds the machine paused exactly on the stop
point, the stop counts as reached. Only a hard reset cancels it (a soft reset leaves a
pending `--break-at` armed), or reaching it. The stop is reported as a breakpoint of the command line.
One limit: instructions another debugger steps through between its stop and our next tick are not watched,
so a stop point it single-steps across in that window is missed.

The browser build has no debugger servers or script host, so `--debugger` and
`--break-at` are refused there.
