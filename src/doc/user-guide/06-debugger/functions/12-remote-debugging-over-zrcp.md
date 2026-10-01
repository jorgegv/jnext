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

In your `launch.json`, use the `zrcp` remote:

```json
"remoteType": "zrcp",
"zrcp": { "port": 10000 },
"load": "mygame.nex"
```

DeZog resets the machine, loads your program with `smartload`, and then you
have breakpoints (with conditions), stepping, step over and step out, the
call stack, reverse debugging over the CPU history, code coverage, and its
`-state save` / `-state restore`.

## By hand

```console
$ telnet localhost 10000
Welcome to ZEsarUX remote command protocol (ZRCP)
Write help for available commands

command> enter-cpu-step
command@cpu-step> get-registers
```

`help` lists every command JNEXT serves; `help` *command* describes one, and
says where JNEXT behaves differently from ZEsarUX.

## What to expect

- **Connecting does not stop the machine**; `enter-cpu-step` does. Leaving
  (or `quit`) resumes it if the stop was the client's own and nothing else is
  attached; with JNEXT's debugger window open, or another remote debugger
  connected, it stays paused and the pause becomes theirs.
- **Breakpoints are the session's.** They start disabled, as in ZEsarUX
  (`enable-breakpoints` turns them on), and they are removed when the client
  leaves. Conditions are ZEsarUX's (`PC=8000H`, `SP>=65280`,
  `A<>0 AND (HL&FFH)=5`, ...).
- **The history, the coverage and the call tracking are the machine's.** A
  client that turns one on turns it off again when it leaves; one that was
  already on — the trace you enabled in the debugger window — stays on. A
  client's `cpu-history clear` and `set-max-size` change only what that client
  sees: JNEXT's own Step Back keeps its whole history.
- **`snapshot-save` / `snapshot-load` keep the snapshot in memory**, for that
  session only; nothing is written to disk. Save it with the machine paused
  while it runs, not stopped at a breakpoint (a breakpoint stops it
  mid-frame). To keep a snapshot on disk, save a `.jns` file from JNEXT's File
  menu.
- **`smartload`, `load-binary` and `save-binary` read and write files on the
  computer running JNEXT**, like ZEsarUX. Keep the server on `127.0.0.1` (the
  default) unless you trust the network: ZRCP has no authentication.
