# 3.10.5 The ZRCP server (ZEsarUX's protocol)

`--zrcp-port N` makes JNEXT serve ZRCP, the line-based text protocol ZEsarUX
offers on its `--remoteprotocol-port`, speaking as ZEsarUX 12.0 does. Its main
client is DeZog's `zrcp` remote (the one DeZog uses for ZEsarUX); because every
command is a line of text, `telnet` is a client too. The server is
`src/remote/zrcp/`, one adapter over the shared socket transport
([3.10.1](10-1-the-socket-transport.md)), reaching the machine only through `jnext::dbg::Debugger`. The design, with the
ZEsarUX transcripts and DeZog source lines every rule below comes from, is
`doc/design/debug-subsystem/zrcp-frontend.md` (§11 records what was built and
every deviation; §10 lists the deliberate divergences from ZEsarUX).

## The pieces

| File | What it holds |
|---|---|
| `src/remote/zrcp/zrcp_server.{h,cpp}` | `ZrcpServer`, a `remote::Protocol` and a `dbg::Listener`: the line reader, the command table — every one of the 125 names ZEsarUX 12.0's own `ls` prints: 67 served, 1 declined (`exit-emulator`), 57 answered `Error. Unsupported command in jnext:` — the session state, the `run` state machine, the breakpoint slots and the memory-breakpoint map |
| `src/remote/zrcp/zrcp_format.{h,cpp}` | the reply formatters: the register line at the widths DeZog's `decodezesaruxdata.ts` reads, `MMU=`, disassembly at column 7, hexdump, the `cpu-history` line, quoted-argument splitting |
| `src/remote/zrcp/zrcp_condition.{h,cpp}` | ZEsarUX's breakpoint-condition dialect: its tokeniser and its operator grouping, translated into a fully bracketed DSL expression that `script::compile_expr` ([3.10.6](13-the-debugger-scripting-language.md)) compiles; `SEGn` / `ROM` / `RAM` are evaluated here, since the DSL cannot read whether a slot is ROM |
| `src/platform/debug_servers.{h,cpp}` | `DebugServers` opens it beside the DZRP and GDB servers — its own listener, its own backend client — in all three loop owners |

## The session and `run`

The client is attached on connect and detached on disconnect or `quit`, which
removes everything it subscribed and releases a pause that is its own, or hands
it to another arming client if one is attached (the backend's rule,
[3.9.3](09-3-sessions-the-pump-and-reconstruct.md)). cpu-step mode, the prompt, `set-cr`, the debug-settings
byte and the partial T-state base are adapter state; the backend learns none
of them.

`run`, `run n` and `cpu-step-over` do not answer at once: the `Running until …`
line goes out first, and the stop reply is written the first time the adapter
is asked after the machine stops — at the top of a pump or after its drain —
so it is level-triggered on `state().paused`, not on the `Paused` edge. While
one is in flight no line is executed: any byte received stops the machine and
the line it belongs to is discarded, which is how ZEsarUX's clients interrupt a
run. `run n` is a loop of `step_into` in time slices, so a large `n` never
holds one pump. `hard-reset-cpu` is a cold boot completed inside the reply,
through the loop owner's `LoopDriver` (the reconstruct contract of [3.9.3](09-3-sessions-the-pump-and-reconstruct.md)): the session
and its subscriptions survive it, and a stopped machine stays stopped.

## Breakpoints, history, loading

The 100 condition slots of `set-breakpoint` are one backend subscription each,
owned by the client. A `PC=` first term is the fast path (an `Execute`
subscription at that address); any other condition fires on its false-to-true
edge, as in ZEsarUX, keyed to the master cycle. Memory breakpoints are kept as
ZEsarUX's per-address type map and turned into one `Mem` range subscription
per run of equal type. The history (`cpu-history`) is a view over the
machine's trace log: `clear` and `set-max-size` move the session's view, never
the trace jnext's own Step Back reads. Turning the trace, coverage or call
tracking on is owned: a client turns off only what it turned on. `smartload`
goes through the loop owner's `LoopDriver::load`, the same path as `--load`;
`snapshot-save` / `snapshot-load` are the backend's bookmarks, kept for the
session.

## Tests

- `zrcp_adapter_test` (Qt-free, both configurations) runs the production
  `Server` over the transport's in-memory fake, on a real machine through `pump()`: the
  framing, the command census, every formatter against ZEsarUX's bytes, the
  run state machine (a stop is proved by the machine being stopped where it
  should be), the condition translator on both sides of every grouping rule,
  the slots, the memory map, history, coverage and loading.
- `test/fixtures/zrcp/zesarux-12.0-exchanges.txt` holds scenes recorded from
  the real ZEsarUX 12.0 server, one fresh server per scene: the exchanges whose
  replies depend on nothing but the session. `zrcp_adapter_test` (rows
  `ZRCP-FIX-*`) and the `zrcp-func` regression row both replay every scene and
  compare the bytes. A new deliberate divergence from ZEsarUX therefore shows
  up as a fixture failure, and belongs in the design's §10, not in the fixture.
- `zrcp-func`, `zrcp-bp-func`, `zrcp-hist-func`, `zrcp-sdl-func` and
  `zrcp-qt-func` drive the real binary over a socket with an independent
  client (`test/00regression/zrcp-peer.py`), which knows ZRCP only from the
  fixture, the transcripts and DeZog's parser.
