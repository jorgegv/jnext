# Debugger scripting

A **debugger script** watches the running machine and acts on what it sees. You
write rules — *when this happens, do that* — and the debugger runs them at
exactly the instant the event happens, with the machine stopped at an
instruction boundary:

```
# stop the moment anything writes into my code, and say who did it
on write 0x8000..(@__data_crt_head - 1) do
    log "write to ${ADDR:x4} <- ${VALUE:x2} from PC ${PC:x4}"
    stop "write into the code area"
end
```

A rule fires on an instruction about to execute, a memory read or write, a port
read or write, a NextREG write, a frame, a scanline, a master-clock cycle, an
interrupt, an NMI, a reset, a host key, a Copper `MOVE`, `WAIT` or `HALT`, a DMA
transfer, or any pause of the machine. Its condition is checked by the debugger
itself, so a rule costs nothing until its event and its condition both match.
Its actions log, stop, assert, dump state, take and compare screenshots, press
keys, and — explicitly, and logged — change the machine.

The same script runs in two places:

- **In the debugger window** (**Script** menu, **Script** tab), to catch a bug
  while you play: a `stop` pauses the machine and shows you where.
- **Headless, in a build or CI pipeline** (`jnext --headless --script …`): a
  script's verdict becomes the process exit status, `0` for a pass and `3` for
  a stop or a failed check, so a test is a script.

And a session you play in the GUI can be **recorded** as a script and
**replayed** headless, with the screens you captured compared byte for byte.

## In this chapter

- [Your first script](01-your-first-script.md) — a guided tour, from one line of
  logging to a check that fails a run.
- [The language](02-the-language.md) — files, comments, numbers, strings,
  variables, rules, operators and errors.
- [Events](03-events.md) — every event, its filter, when it is delivered and
  the payload names it carries.
- [Reading the machine](04-reading-the-machine.md) — registers, flags, memory,
  the MMU, NextREGs, time and the raster counters, symbols.
- [Actions](05-actions.md) — every action: `log`, `stop`, `assert`, `exit`, the
  dumps, `enable`/`disable`, screenshots, snapshots, `compare_scr`, `if`.
- [Snapshots and span checks](06-snapshots-and-span-checks.md) — capture the
  machine at one event and compare it at another, nested.
- [Keys, joysticks and host keys](07-keys-joysticks-and-host-keys.md) — typing
  into the guest, and arming rules from the keyboard.
- [Changing the machine](08-changing-the-machine.md) — `set` and `out`: what they
  write, when it is seen, and how it is logged.
- [Running scripts](09-running-scripts.md) — the command line, the GUI, exit
  codes, the watchdog, and CI.
- [Recording and replaying a session](10-recording-and-replaying.md) — record in
  the GUI, replay headless.
- [Catching memory-corruption bugs](11-catching-memory-corruption-bugs.md) — a
  code-range guard, an MMU consistency check and an interrupt-handler audit.
- [The demo scripts](12-the-demo-scripts.md) — nine complete scripts and the
  program they run against, one by one, with their output.
- [Quick reference](13-quick-reference.md) — the whole language on one page.

## About the examples

Every example in this chapter was run, and its output pasted from the run.
Log lines are shown without the time stamp each one starts with and without
the `[client N]` tag the debugger appends, so

```
[13:10:21.837] [debugger] [info] [jds F:100 C:56469512] SCRIPT EXIT 0 [client 2]
```

is shown as

```
[debugger] [info] [jds F:100 C:56469512] SCRIPT EXIT 0
```

Most examples need nothing but JNEXT: they run on the 48K Spectrum's own ROM.
The [demo scripts](12-the-demo-scripts.md) need the source tree, which holds the
demo program and its MAP file.
