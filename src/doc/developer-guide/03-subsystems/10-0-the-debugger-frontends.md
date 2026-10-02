# 3.10 The debugger frontends

A frontend turns something outside the backend — a Qt window, a socket peer, a
script file — into calls on `jnext::dbg::Debugger`. Six of them ship:

| Frontend | Code | Started by | `ClientKind` |
|---|---|---|---|
| the Qt debugger | `src/debugger/` | View > Debugger (Alt+D) in the Qt build, or any pause | `Gui` |
| the DZRP server (DeZog) | `src/remote/dzrp/` | `--dzrp-port N` | `Dzrp` |
| the GDB RSP server (`z88dk-gdb`) | `src/remote/gdb/` | `--gdb-port N` | `GdbRsp` |
| the ZRCP server (ZEsarUX's protocol) | `src/remote/zrcp/` | `--zrcp-port N` | `Zrcp` |
| the script engine | `src/script/` | `--script FILE`, or the Qt Script tab | `Script` |
| the recorder | `src/script/recorder.*` | `--record-script FILE`, or the Qt Script menu | `Script` |

All six run in every frontend build that can host them: the three servers and
the scripting language need no toolkit, so they work in the Qt GUI, the SDL
frontend and `--headless` alike. Each is one or more clients of the same
backend, so any combination can run at once on one machine, and each sees the
others' transitions through its listener.

## What every frontend does

A frontend, whatever it speaks:

1. **attaches** with a `ClientInfo` and keeps the `ClientId`;
2. **installs a `Listener`** for the pushes: `on_paused`, `on_resumed`,
   `on_reset`, `on_frame_ended`, `on_subscriptions_changed`,
   `on_exit_requested`, `on_log`. A listener records and returns; the work is
   done later, on the frontend's own turn;
3. **issues verbs** with its id, and reads the `Result` of each;
4. **owns its subscriptions** — breakpoints, watchpoints, script rules, temporary
   step breakpoints — and loses them at detach;
5. **detaches** when its session ends, which also releases or hands on a pause it
   owns ([3.9.3](09-3-sessions-the-pump-and-reconstruct.md)).

A socket frontend also implements the transport's `Protocol` and lets the
transport's `Server`, a `jnext::dbg::Service`, be driven by the loop owner's
`pump()` ([3.10.1](10-1-the-socket-transport.md)). Its commands therefore run
on the emulation thread, between two frames, with the machine at an instruction
boundary.

What a frontend must **not** do is model the machine. It never includes
`Emulator`, never composes a `MemSpace` from a page number, never inserts its
own step-off breakpoint, and never sets the stop policy. Its protocol model —
DZRP's bank bytes, ZRCP's 100 condition slots, RSP's register packing, the DSL's
grammar — stays in its own directory.

## A session, end to end

This is a real ZRCP exchange with a headless 48K machine, driven from a short
Python client that reads the port from the log line. ZRCP is a line protocol,
so it shows the transport, a client's attach, a mutation and a detach without a
protocol decoder. The register values depend on when the client connects.

```
$ jnext --headless --machine 48k --sdcard sd.img --zrcp-port 0 --delayed-automatic-exit 10
[03:22:10.087] [debugger] [info] zrcp: listening on 127.0.0.1:38661
```

(`sd.img` is a private copy of the cached SD image, as any manual run that must
stay reproducible should use.)

The client sends `enter-cpu-step`, `get-registers`, `set-register PC=8000H` and
`quit`. The server replies:

```
Welcome to ZEsarUX remote command protocol (ZRCP)
Write help for available commands

command> enter-cpu-step

command@cpu-step> get-registers
PC=11dc SP=ffff AF=3f2b BC=0000 HL=eef2 DE=ffff IX=0000 IY=0000 AF'=ffff BC'=0000 HL'=0000 DE'=0000 I=3f R=46  F=--5-3-NC F'=SZ5H3PNC MEMPTR=11dc IM0 IFF-- VPS: 0 MMU=80000005000200000000000000000000
command@cpu-step> set-register PC=8000H
PC=8000 SP=ffff AF=3f2b BC=0000 HL=eef2 DE=ffff IX=0000 IY=0000 AF'=ffff BC'=0000 HL'=0000 DE'=0000 I=3f R=46  F=--5-3-NC F'=SZ5H3PNC MEMPTR=11dc IM0 IFF-- VPS: 0 MMU=80000005000200000000000000000000
command@cpu-step> quit
Sayonara baby
```

Meanwhile the `debugger` log channel records the session from the backend's
side:

```
[03:22:10.088] [debugger] [info] zrcp: client connected from 127.0.0.1
[03:22:10.088] [debugger] [info] ATTACH client 1 "ZRCP client 127.0.0.1" kind=2
[03:22:10.092] [debugger] [info] MUTATE reg PC 0x11DC -> 0x8000 by 1
[03:22:10.092] [debugger] [info] zrcp: client 127.0.0.1 disconnected (closed by the server)
[03:22:10.092] [debugger] [info] DETACH client 1 (released its pause)
```

`enter-cpu-step` paused the machine on behalf of client 1. The `MUTATE` line is
the backend's, not the server's: every client's write produces one. On `quit`
the client detached, and because no other arming client was attached, its pause
was released and the machine ran on until `--delayed-automatic-exit` ended it.

## The capability matrix

What each frontend actually calls, read from its call sites on the facade. A
mark means the frontend uses at least one verb of that row; "r" is a read, "w"
a write.

| Capability | Qt | DZRP | GDB RSP | ZRCP | script engine | recorder |
|---|---|---|---|---|---|---|
| `pause`, `run`, `state` | ✓ | ✓ | ✓ | ✓ | — (a `Stop` verdict) | — |
| `step_into` | ✓ | — | ✓ | ✓ | — | — |
| `step_over`, `step_out` | ✓ | — (transients + `run`) | — (`run_to`) | — (`run_to`) | — | — |
| `run_to` | ✓ | — | ✓ | ✓ | — | — |
| `run_to_end_of_frame`, `run_to_end_of_scanline` | ✓ | — | — | — | — | — |
| `step_back`, `rewind_to_frame`, rewind buffer | ✓ | — | — | — | — | — |
| `reset` | — | — | ✓ | ✓ | — | — |
| `load` | — | — | — | ✓ | — | — |
| registers | r | r w | r w | r w | r w | — |
| `peek` / `poke` | r w | r w | r w | r w | r w | — |
| MMU slots, paging | r | r w | r w | r | r | — |
| NextREG | r w | r | r w | r w | r w | r |
| ports | — | in, out | in, out | out | out | — |
| `time`, `raster`, `machine` | r | — | r | r | r | r |
| sprites, patterns | r | r | — | r w | — | — |
| Copper, AY | r | — | — | — | — | — |
| audio mute mask | r w | — | — | — | r w | — |
| disassembly, call stack | r | — | — | r | — | — |
| trace | r w | — | — | r w | — | — |
| coverage | — | — | — | r w | — | — |
| palettes, clip windows | r | r (sprite palette, clip) | — | r w | — | — |
| `render_layer` | r | — | — | — | — | — |
| `set_border` | — | w | — | — | — | — |
| `input_state` | — | — | — | — | — | r |
| input injection | — | — | — | NMI | keys, joystick | — |
| `subscribe` | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| `subscriptions` listing | ✓ | — | ✓ | ✓ | — | — |
| master switch | ✓ | — | — | — | — | — |
| `probe_execute` | — | — | — | ✓ | — | — |
| `raise_host_event` | ✓ | — | — | — | ✓ | — |
| bookmarks | — | ✓ | — | ✓ | — | — |
| `screenshot`, `ula_screen_dump` | — | — | — | — | ✓ | ✓ |
| `save_snapshot` | — | — | — | — | ✓ | — |
| symbols | load, read | — | read | — | load, read | — |
| `set_live_raster` | ✓ | — | — | — | — | — |

The loop owners call the rest: `set_stop_policy`, `set_loop_driver`,
`add_service`, `pump`, `on_cold_boot_begin` and `on_cold_boot_done`, and, for
the CLI `--delayed-*` flags, `press_key`, `press_nmi`, `save_snapshot` and
`screenshot`.

Read it as a statement of the code, not of the protocols. A blank cell is either
a capability the protocol has no verb for (DZRP cannot step back, `z88dk-gdb`
sends no reverse-execution packet) or one a frontend declines (the DSL never
calls `port_in`, which perturbs the machine). The design's own matrix, with
that distinction drawn cell by cell, is §7.6 of
`doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md`.

## In this section

- [3.10.1 The shared socket transport](10-1-the-socket-transport.md)
- [3.10.2 The Qt debugger](10-2-the-qt-debugger.md)
- [3.10.3 The DZRP server (DeZog)](10-the-dzrp-server.md)
- [3.10.4 The GDB RSP server (z88dk-gdb)](11-the-gdb-rsp-server.md)
- [3.10.5 The ZRCP server (ZEsarUX's protocol)](12-the-zrcp-server.md)
- [3.10.6 The debugger scripting language](13-the-debugger-scripting-language.md)
- [3.10.7 Extending the debug subsystem](10-7-extending-the-debug-subsystem.md):
  adding a frontend, a verb or an event kind, and where the tests go.

Chapter 6 of the user guide, *The debugger*, documents every frontend from the
user's side: the panels, the remote protocols under its functions, and the
scripting language and the recorder under *Debugger scripts*.
