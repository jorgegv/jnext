# 3.10.1 The shared socket transport

The three protocol servers — DZRP, GDB RSP and ZRCP — share one transport, in
**`src/remote/transport.*`** (target `jnext_remote`). It has no toolkit
dependency and is built in every configuration. It carries no protocol: it never
parses a byte. Each server is its own listener with its own backend client, so
all three may run at once. The design, and the reason behind each rule below, is
`doc/design/debug-subsystem/transport.md`.

## The three types

| Type | Role |
|---|---|
| `remote::Server` | the listener and its one client connection. It is a `jnext::dbg::Service`, so the loop owner's `pump()` drives it |
| `remote::Protocol` | what a server implements: `on_connect`, `on_service`, `on_notify`, `on_disconnect` |
| `remote::Connection` | the adapter's view of its client: `available()`, `read()`, `write()`, `close()`, `peer_gone()` |

A server is built from a `ServerConfig` and its `Protocol`:

| `ServerConfig` | Meaning | Default |
|---|---|---|
| `name` | the log prefix | — |
| `busy_reply` | what a second client is sent before it is closed | — |
| `max_input` | unconsumed input at which reading stops, backpressuring the peer through the kernel | 1 MiB |
| `max_output` | queued output at which a client that stopped reading its replies is disconnected | 32 MiB |
| `linger_ms` | how long a closing connection keeps delivering what was queued | 2000 |

## One pass

`Server::service_once(wait_ms)` accepts, reads, asks the `Protocol` to execute
**at most one** complete command (`on_service`), and writes — repeated until a
command ran or `wait_ms` passed. `wait_ms` 0 is exactly one pass. That is the
contract `pump()`'s drain relies on ([3.9.3](09-3-sessions-the-pump-and-reconstruct.md)).
`flush_notifications()` calls the adapter's `on_notify()` after the drain, which
is where a server turns a recorded `Paused` push into a packet.

The rules a `Connection` keeps:

- `write()` never blocks and never drops. It queues; the server sends once the
  adapter's callback returns, and what the kernel does not take goes out on later
  passes.
- `close()` still delivers what is queued, for at most `linger_ms`.
- `on_disconnect()` is called exactly once per `on_connect()`, whoever ended the
  session, and every byte the client sent before hanging up is offered to the
  adapter first.

**One client per listener.** A second client is sent the adapter's `busy_reply`
and closed. A client that hangs up and dials again is not a second one: a pass
serves the current client first, so a hang-up is seen before a redial is
judged; it takes one more look at the client after the listener has parked an
arrival, because the client may have read its reply, hung up and redialled in
between; and it leaves the redial parked until the old session is retired
(`remote_transport_test` rows XPT-SRV-30..33).

## Sockets

Sockets come only from esp01's public seam — `esp::make_socket_listener`,
`EspListener`, `EspTransport` (`src/esp01/include/esp01/esp_socket.h`) — which
is already non-blocking and has a Windows twin. `src/remote/` makes no socket
call of its own. The seam has no readiness wait, so the one wait, the headless
loop's paused `pump()`, is a bounded 1 ms sleep between non-blocking passes.

The server logs to the `debugger` channel. Among its lines is
`<name>: listening on <addr>:<port>`, with the port actually bound, which is
what a `--<proto>-port 0` regression row reads to find the server.

`--debug-listen-address ADDR` (numeric only, default `127.0.0.1`) is validated
in `main.cpp` and held in `EmulatorConfig::debug_listen_address`. It is refused
unless a server port (`--dzrp-port`, `--gdb-port` or `--zrcp-port`) is given
too (`debug-listen-address-func`).

## How a loop owner opens the servers

`DebugServers::start(Debugger&, EmulatorConfig)` (`src/platform/debug_servers.*`)
builds each server the configuration asks for, opens it on the listen address,
and registers it with `Debugger::add_service()`. A server that cannot bind its
port is a startup error in every frontend: the loop owner logs it and exits.
`DebugServers` also holds the pump budgets of
[3.9.3](09-3-sessions-the-pump-and-reconstruct.md):
`frame_loop_budget()`, `headless_should_wait()` and `headless_wait_budget()`.

## The fake

`FakeListener`, `FakeTransport` and `FakePeer` (`src/remote/fake_transport.*`)
implement the same esp interfaces in memory. An adapter's unit suite runs the
production `Server` with only the kernel replaced. A peer's window and receive
chunk model partial writes and reads, so a command split across two reads, or a
reply the peer takes a byte at a time, is a test case rather than a hope.

`remote_transport_test` pins the transport three ways: over the fake, over a
real socket on `127.0.0.1` port 0, and through `Debugger::pump()` (`XPT-PUMP`).
