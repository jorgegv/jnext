# Remote debugging over ZRCP

ZRCP is the text protocol the ZEsarUX emulator offers for remote control. DeZog
can debug through it (its `zrcp` remote type), and because every command is a
line of text you can also drive it by hand with `telnet`. JNEXT speaks ZRCP as
ZEsarUX 12.0 does.

## Start JNEXT with a ZRCP port

```console
$ jnext --zrcp-port 10000
```

The log confirms it:

```
[13:10:21.837] [debugger] [info] zrcp: listening on 127.0.0.1:10000
```

`10000` is ZEsarUX's own default, and the one DeZog expects. `--zrcp-port 0`
lets the system pick a free port and prints it. The server is off unless you
give the option, and it works in every frontend: the Qt window, the SDL build
and `--headless`. It can run together with the DeZog (`--dzrp-port`) and
`z88dk-gdb` (`--gdb-port`) servers; each has its own port and one client.

## DeZog

In your project's `.vscode/launch.json`, use DeZog's `zrcp` remote — the one
it uses for ZEsarUX — with JNEXT's port:

```json
{
    "type": "dezog",
    "request": "launch",
    "name": "JNEXT (ZRCP)",
    "remoteType": "zrcp",
    "zrcp": {
        "hostname": "localhost",
        "port": 10000,
        "resetOnLaunch": true,
        "skipInterrupt": false
    },
    "load": "mygame.nex",
    "sjasmplus": [ { "path": "mygame.sld" } ],
    "topOfStack": "stack_top"
}
```

The assembler, label and `topOfStack` settings are DeZog's, not JNEXT's; see
DeZog's own documentation for them. Two of the `zrcp` settings matter here:

- **`resetOnLaunch`** (DeZog's default, `true`): DeZog starts every session
  with `hard-reset-cpu`. In JNEXT that is a **cold boot** of the whole machine,
  the same as **Machine > Power Reset** (F1), finished before JNEXT answers,
  so the program is then loaded into a fresh machine. With `false` the program is
  loaded into the machine as it is.
- **`skipInterrupt`**: leave it `false` (DeZog's default). With `true`, DeZog
  asks the emulator to step over interrupt routines, which JNEXT does not do:
  DeZog shows one warning at launch (`Unsupported in jnext:
  step-over-interrupt (bit 5)`), the session carries on, and stepping enters
  an interrupt routine when one is taken.

DeZog then holds the machine stopped and loads `load` with `smartload`, which
takes a `.nex`, `.sna`, `.szx`, `.z80`, `.jns`, `.tap`, `.tzx`, `.wav` or
`.rzx` file. From there you have breakpoints (with conditions) and
watchpoints, stepping, step over and step out, the call stack, reverse
debugging over the CPU history, code coverage, and `-state save` /
`-state restore`.

Run JNEXT as the machine your program needs: `--machine next`, the default,
for a `.nex`.

## By hand

```console
$ telnet localhost 10000
Welcome to ZEsarUX remote command protocol (ZRCP)
Write help for available commands

command> enter-cpu-step

command@cpu-step> smartload "mygame.nex"

command@cpu-step> disassemble 8000H 3
  8000 DI
  8001 PUSH IY
  8003 EXX
command@cpu-step> enable-breakpoints

command@cpu-step> set-breakpoint 1 PC=8005H

command@cpu-step> run
Running until a breakpoint, key press or data sent, menu opening or other event
Breakpoint fired: PC=8005H
PC=8005 SP=fdf9 AF=0044 BC=0b00 HL=339b DE=369b IX=8000 IY=5c3a ...
  8005 LD (8379),SP
command@cpu-step>
```

Every reply ends with the prompt: `command> `, or `command@cpu-step> ` while
the machine is held stopped. `help` lists every command JNEXT serves; `help`
*command* describes one, and says where JNEXT behaves differently from
ZEsarUX. Numbers are decimal, or hexadecimal with an `H` suffix (`8000H`). A
command JNEXT does not serve answers `Error. Unsupported command in jnext:`
and its name.

While `run` runs, **anything you send stops it**, and what you sent is
discarded, not executed: pressing Enter on an empty line is the way to stop
the machine by hand.

## What to expect

- **Connecting does not stop the machine**; `enter-cpu-step` does. Leaving
  (or `quit`) resumes it if the stop was the client's own and nothing else is
  attached; with JNEXT's debugger window open, or another remote debugger
  connected, it stays paused and the pause becomes theirs.
- **Breakpoints are the session's.** They start disabled, as in ZEsarUX:
  until `enable-breakpoints`, `set-breakpoint` answers `Error. You must
  enable breakpoints first`. They are removed when the client leaves.
  Conditions are ZEsarUX's (`PC=8000H`, `SP>=65280`,
  `PC=PEEKW(SP-2) AND SP>=65280`, `A<>0 AND (HL&FFH)=5`, ...); `help
  set-breakpoint` lists the terms JNEXT honours.
- **The history, the coverage and the call tracking are the machine's.** A
  client that turns one on turns it off again when it leaves; one that was
  already on — the trace you enabled in the debugger window — stays on. A
  client's `cpu-history clear` and `set-max-size` change only what that client
  sees: JNEXT's own Step Back keeps its whole history. Because the history is
  JNEXT's trace, `cpu-history enabled yes` shows what the trace already holds
  (up to 10000 entries) and `clear` starts the view afresh, where ZEsarUX's
  history starts empty and records only after `started yes`. DeZog sends
  `clear` itself, so it sees only its own steps.
- **`snapshot-save` / `snapshot-load` keep the snapshot in memory**, for that
  session only; nothing is written to disk. Save it with the machine paused
  while it runs, not stopped at a breakpoint (a breakpoint stops it
  mid-frame). To keep a snapshot on disk, save a `.jns` file from JNEXT's File
  menu.
- **`smartload`, `load-binary` and `save-binary` read and write files on the
  computer running JNEXT**, like ZEsarUX.
- **One client at a time.** A second connection is answered `Error. Another
  ZRCP client is connected` and closed.
- **`hard-reset-cpu` and `reset-cpu` keep the session**: its breakpoints and
  settings survive the reset, and a machine held in cpu-step mode stays
  stopped, at the reset address.

## Headless runs

With `--headless`, a client that holds the machine stopped holds its frames
too, exactly as described for DeZog in
[Remote debugging with DeZog](10-remote-debugging-with-dezog.md): nothing is
emulated while it waits, and `--delayed-automatic-exit` still ends the run on
time.

## Security

The server listens on `127.0.0.1` only. `--debug-listen-address` changes that
for every debug server, and is refused unless a server port is given too.
**ZRCP has no authentication**: anyone who can reach the port controls the
machine and can read and write files on your computer through `smartload`,
`load-binary` and `save-binary`, so widen it only on a network you trust.

---

For the full list of command-line options mentioned here, see **jnext**(1) or
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
