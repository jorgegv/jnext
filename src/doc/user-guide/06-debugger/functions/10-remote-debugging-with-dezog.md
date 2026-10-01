# Remote debugging with DeZog

[DeZog](https://github.com/maziac/DeZog) is a Z80 debugger that runs inside
Visual Studio Code: breakpoints in your source files, stepping, registers,
memory and disassembly views. It talks to the machine it debugs over the DeZog
Remote Protocol (DZRP). JNEXT serves that protocol, so DeZog can debug a
program running in JNEXT the same way it debugs one in CSpect or on a real
Next.

## Starting JNEXT for DeZog

Give JNEXT a TCP port to listen on:

```console
$ jnext --dzrp-port 11000 --load mygame.nex
```

The log confirms it:

```
[13:10:21.837] [debugger] [info] dzrp: listening on 127.0.0.1:11000
```

`--dzrp-port 0` lets the system pick a free port and prints it in that same
line, which is handy for scripts. The server is off unless you give the
option, and it works in every frontend — the Qt window, the SDL build and
`--headless`.

## Pointing DeZog at it

In your project's `.vscode/launch.json`, use a `cspect` remote with JNEXT's
port:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "JNEXT",
    "remoteType": "cspect",
    "cspect": { "hostname": "localhost", "port": 11000 },
    "sjasmplus": [ { "path": "mygame.sld" } ],
    "topOfStack": "stack_top"
}
```

The assembler and label settings are DeZog's, not JNEXT's; see DeZog's own
documentation for them. DeZog releases that have a `dzrp` remote type can use
that instead, with the same port.

Start debugging in VS Code. **Connecting pauses the machine** and DeZog shows
where it stopped.

## What to expect

- **DeZog's breakpoints are its own.** Breakpoints and watchpoints set from
  DeZog are kept apart from the ones in JNEXT's debugger window: they stop the
  machine whether that window is open or closed, and they are removed when
  DeZog disconnects. A DeZog session that crashes cannot leave the machine
  stopped on one of them.
- **Disconnecting resumes the machine** if the pause was DeZog's own. A pause
  you made yourself in JNEXT's debugger window stays.
- **Both can drive the machine.** JNEXT's debugger window and DeZog control the
  same machine, and whichever acted last wins. If something other than DeZog
  stops the machine while DeZog has it running — you press Pause in the
  debugger window, or a magic breakpoint fires — DeZog is told it stopped, and
  why.
- **One client at a time.** A second connection is closed straight away.
- **Hands off during an RZX.** While an RZX recording or playback is running,
  DeZog cannot read or write a port, or change memory, a register, a memory
  slot or the border: JNEXT refuses and logs a warning saying why. A recording
  cannot carry such a change, and a port read can disturb the device it reads.
- **Saving and restoring the state** (`-state save` and `-state restore` in
  DeZog's debug console) keeps the state inside JNEXT for the current session
  only; the file DeZog writes just refers to it. A state can be saved only at a
  frame boundary, so right after a breakpoint hit the save is refused — pause
  the machine by hand, then save.
- **Not served:** the commands a hardware stub uses to patch breakpoints into
  memory, and DeZog's `-dbg cmd_exec_asm`. JNEXT answers them empty and logs a
  warning, so DeZog never hangs waiting.

## Headless runs

With `--headless`, a client that holds the machine paused holds its frames
too: nothing is emulated, and JNEXT sleeps until the next command instead of
using the CPU. The automatic exit is still a hard bound — while a client holds
the machine paused, `--delayed-automatic-exit` (in either form) is counted in
wall time, one frame per 20 ms — so a run with a debugger attached still ends
on time.

## Security

The server listens on `127.0.0.1` only, so only programs on your own machine
can reach it. `--debug-listen-address` changes that (for example `0.0.0.0` to
debug from another computer), and it is refused unless a server port is given
too. **DZRP has no authentication**: anyone who can reach the port controls the
machine and can read and write all of its memory, so widen it only on a network
you trust.

---

For the full list of command-line options mentioned here, see **jnext**(1) or
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
