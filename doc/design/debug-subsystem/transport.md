# T — the debugger servers' shared socket transport (GH #287)

> Work package **T** of epic [#276](https://github.com/jorgegv/jnext/issues/276),
> as [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
> §10.1 defines it: one non-blocking listener/`Service` over the public
> `esp::make_socket_listener` / `EspListener` / `EspTransport` seam, the
> in-memory fake the adapter suites push packets through, and
> `--debug-listen-address`. **No protocol content.** DZRP (#12), ZRCP (#280)
> and GDB RSP (#281) all depend on it, so it is written once, by one owner.
>
> Code: `src/remote/transport.{h,cpp}`, `src/remote/fake_transport.{h,cpp}`
> (target `jnext_remote`). Suite: `test/remote/remote_transport_test.cpp`.

## Work packages — the tracker for this package

Mirrors this package's row in [DEBUG-SUBSYSTEM-ARCHITECTURE.md](../DEBUG-SUBSYSTEM-ARCHITECTURE.md)
§10.1, which stays authoritative: if the two ever disagree, §10.1 wins and this
table is stale.

Status values: `todo` · `in progress` · `in review` · **`done`** (independently
reviewed and APPROVED). The whole package lands on **one branch** and merges
whole, so `done` here means the sub-item is approved, not merged.

| WP | Branch `gh276-transport` (issue #287) | Status |
|---|---|---|
| **T1** | `remote::Server` — the listener/`dbg::Service` over the esp seam — with `Connection` (the adapter's byte pipe) and `Protocol` (what an adapter implements); target `jnext_remote`, built in every configuration | in review |
| **T2** | the in-memory fake: `FakeListener` / `FakeTransport` / `FakePeer`, implementing the esp interfaces so an adapter suite runs the production `Server` | in review |
| **T3** | `--debug-listen-address ADDR` — table row, `main.cpp` dispatch, `EmulatorConfig::debug_listen_address`, man page, and the `debug-listen-address-func` regression row | in review |
| **T4** | `remote_transport_test` (64 rows, `gate: none`, both configurations), this appendix, the Developer Guide notes | in review |

Depends on: B0, B (both landed). Blocks: D (#12), Z (#280), G (#281).

---

## 1. The interface, in brief

```cpp
namespace jnext::remote {

struct ServerConfig {
    std::string name;          // "dzrp" / "zrcp" / "gdb": prefixes every log line
    std::string busy_reply;    // sent to a second concurrent client, then closed ("" = nothing)
    std::size_t max_input  = 1 MiB;   // stop reading a peer the adapter has not caught up with
    std::size_t max_output = 32 MiB;  // disconnect a peer this far behind on its replies
    int         linger_ms  = 2000;    // time a connection the ADAPTER closed gets to flush
};

class Connection {            // the adapter's one client — valid inside a callback only
    std::size_t available() const;
    std::size_t read(std::uint8_t* buf, std::size_t cap);   // never blocks
    void write(const std::uint8_t* data, std::size_t len);  // never blocks, never drops
    void write(const std::string& text);
    void close();                                           // queued bytes still delivered
    bool closing() const; bool peer_gone() const;
    const std::string& peer() const; std::size_t pending_output() const;
};

class Protocol {              // what an adapter implements; all pure virtual
    virtual void             on_connect(Connection&)    = 0;
    virtual dbg::ServiceStep on_service(Connection&)    = 0;  // AT MOST ONE command
    virtual void             on_notify(Connection&)     = 0;  // flush queued notifications
    virtual void             on_disconnect()            = 0;  // exactly once per on_connect
};

class Server final : public dbg::Service {
    Server(ServerConfig, Protocol&);
    bool open(const std::string& bind_address, std::uint16_t port);            // real socket
    bool open(std::unique_ptr<esp::EspListener>, const std::string& label,
              std::uint16_t port);                                            // any listener
    void stop();
    bool listening() const; std::uint16_t port() const; const std::string& last_error() const;
    dbg::ServiceStep service_once(int wait_ms) override;
    void             flush_notifications() override;
    bool             peer_connected() const override;
};

// fake_transport.h
class FakeListener  : public esp::EspListener  { std::shared_ptr<FakePeer> connect(from); ... };
class FakeTransport : public esp::EspTransport { ... };   // the server end of one pipe
class FakePeer { send, take, pending, close, reset, closed_by_server,
                 set_window /* partial writes */, set_recv_chunk /* partial reads */ };
}
```

An adapter owns a `Protocol` implementation and a `Server`; its loop-owner
wiring (D WP-5, Z WP-1/…, G WP-4) calls `server.open(cfg.debug_listen_address,
port)` and `debugger.add_service(server)`. Its unit suite does the same with
`server.open(std::make_unique<FakeListener>(), "127.0.0.1", 0)` and drives
`FakeListener::connect()` / `FakePeer`.

## 2. Decisions, each with its reason

1. **The fake sits at the esp interfaces, not at a new `Transport` class.**
   `FakeListener` / `FakeTransport` implement `esp::EspListener` /
   `esp::EspTransport` — "the same interface the socket does" (backend §7) taken
   literally. So an adapter suite runs the *production* `Server`: admission, the
   one-client rule, the busy reply, buffering, partial writes, disconnect and
   linger are the shipped code, and only the kernel is replaced. A fake one
   layer up (at `Connection`) would leave all of that untested by every adapter
   suite, and would have to be kept equal to the real path by hand.
   What adapters code against is `Connection` — `read` / `write` / `close` —
   because the raw esp contract (partial sends, `state()` after a 0) is exactly
   the mechanism each of the three would otherwise re-implement.

2. **One command per `service_once`, enforced by structure.** T cannot count
   commands — it never parses a byte — so `Protocol::on_service` carries the
   contract ("execute AT MOST ONE complete command") and `Server` calls it once
   per pass and returns on `Serviced`. The drain policy stays in `pump()` (B3),
   which is what SES-03 requires.

3. **`on_service` runs on every pass, new bytes or not.** A second command
   already buffered behind the last one must not wait for more input; ZRCP's
   "any byte stops a `run`" and DZRP's 5 s `chunk_timeout` both need the
   adapter to be asked while nothing new arrives.

4. **The only wait is a bounded sleep between non-blocking passes.** The esp
   seam has no readiness wait by design (`EspTransport::poll()` takes no
   timeout), and T makes no socket call of its own. So `service_once(wait_ms)`
   is: pass; if a command ran or the client went, return; else sleep
   `min(1 ms, remaining)` and pass again, never beyond `wait_ms`. A command
   arriving mid-wait is therefore answered within one sleep interval of its
   arrival, by construction (XPT-NET-07 / XPT-PUMP-06 assert the early return,
   with bounds generous enough for a loaded host); `wait_ms` 0 is exactly one
   pass with no sleep. On Windows a
   1 ms sleep can last a scheduler quantum (~15 ms), which only lengthens the
   poll interval of a *waiting* headless loop. If a true readiness wait is ever
   wanted, it belongs in esp01's twins, not here.

5. **Every byte a client sent before hanging up is executed.** A peer close is
   observed after the data in front of it (TCP order), and the client is kept —
   `peer_connected()` true — until `on_service` has reported `Idle` with it gone.
   `flush_notifications()` between commands (which `pump()` interleaves) does
   not end the session early (XPT-SRV-10/11). Replies to such a tail go nowhere;
   the commands' effects happen.

6. **`on_disconnect()` exactly once per `on_connect()`, whoever ended it** — the
   peer, a reset, the output bound, the adapter's own `close()`, or `stop()`.
   SES-01's detach has one home in every adapter.

7. **Writes are never dropped, and never unbounded.** `Connection::write` queues
   and pushes what the kernel takes; the rest goes out on later passes. A peer
   `max_output` bytes behind (32 MiB — twice DZRP's 16 MiB frame cap) is
   disconnected with a warn line rather than buffered into a host OOM or
   truncated into a corrupt stream. Reading stops at `max_input` unconsumed
   bytes, which backpressures the peer through the kernel instead of losing
   anything.

8. **An adapter's `close()` still delivers what it owes** (ZRCP's `Sayonara
   baby`, a busy reply) — for up to `linger_ms`, then the socket closes anyway
   and the dropped byte count is logged. Unread input on a closing connection
   is read and discarded, so the close is a FIN rather than a reset that could
   cost the peer that last reply.

9. **One client per listener; the second one's treatment is the adapter's.**
   The three designs agree on the rule and differ on the words (§3), so T
   admits one, refuses the rest with a warn line, and sends
   `ServerConfig::busy_reply` (possibly empty) before closing. A refused
   connection never reaches `on_connect`, and its bytes are never executed.

10. **Accept happens only inside a service pass.** A client whose handshake the
    kernel has completed is not a session until `pump()` runs — so nothing about
    a session changes between ticks.

11. **Log lines go to the `debugger` channel.** Its own comment (`core/log.h`)
    names "a remote protocol server" among the clients it exists for, and a new
    spdlog channel is a user-visible `--log-level` token (SES-06's note). Lines:
    `<name>: listening on <addr>:<port>` (info — the **bound** port, which is
    what a `--<proto>-port 0` regression row reads), `cannot listen` (error),
    `client connected from` / `disconnected` (info), `refused a connection`
    and `is not reading its replies` and `dropped … undelivered bytes` (warn),
    `listener … failed` (error, once). IPv6 addresses are bracketed.

12. **`open(bind_address, port)` refuses a name** (`esp::parse_ip`, numeric
    only) — the same posture as `--esp-listen-address`. Bind failures (in use,
    not a local address, no network) are `false` + `last_error()` + an error
    line; nothing throws, nothing crashes.

13. **The destructor makes no protocol call.** An adapter that holds its
    `Server` as a member is partly destroyed when that member's destructor
    runs; `stop()` is the orderly shutdown and calls `on_disconnect()`.

14. **`--debug-listen-address` is validated in `main.cpp` and held in
    `EmulatorConfig::debug_listen_address`** beside `magic_breakpoint` and
    `persistent_breakpoints`, where every loop owner already has it. It sits in
    the man page's **Debugging** section: the table's order is the man page's
    reading order, and "after `--esp-listen-address`" in arch §3.1 / gdb §6.4
    would put a debugger option under "Networking (ESP-01 WiFi)". (gdb §6.4
    says the flag is *spelled* after `--esp-listen-address`, which it is.)

15. **No loop owner is touched.** Registration with a real port is each
    server's WP. The per-tick budgets are T's to choose (the B4 comment in
    `headless_app.cpp` says so) and are recorded here, not wired: with no
    service registered on `main` a changed budget is unobservable, and
    `qt_app.cpp` is package Q's. The values, from SES-03 and arch §5:

    | loop owner | running | paused |
    |---|---|---|
    | Qt, SDL | `PumpBudget{}` | `PumpBudget{0, 2, 10}` — drain, never block the tick |
    | headless | `PumpBudget{}` | `PumpBudget{50, 2, 10}` while `remote_attached` — the spin becomes a wait |

    Because decision 4 answers a mid-wait command within one ~1 ms sleep, the
    headless half of REQ-dzrp-9 (a ≤2 ms paused cadence) is met by the 50 ms
    wait itself.
    Whether the first server wires these in its loop-owner WP or T does is an
    owner question (report).

## 3. Where the three consumers' texts agree, differ, or are wrong

- **Agree, provided once:** bind to `--debug-listen-address`, default
  `127.0.0.1`; `0` = an OS-chosen port logged as `<proto>: listening on
  127.0.0.1:NNNNN` (dzrp §4.3, zrcp §5.6, gdb §6.4); one client per listener
  (dzrp §2 row 1, zrcp §5.5, gdb §6.1); non-blocking accept/read/write inside
  `pump` (dzrp §4.2, zrcp §5.1, gdb §6.1); socket EOF = detach (dzrp §4.1,
  zrcp §4.5, gdb §6.3).
- **Differ — a hook, the policy left to the adapter:** what a second
  connection gets. DZRP: "accepted and immediately closed with a log line";
  ZRCP: `Error. Another ZRCP client is connected\n`, then closed; RSP:
  "accepted and closed with nothing sent, logged at warn". `busy_reply` covers
  all three; the log level (unstated by DZRP) is warn for all.
- **Differ — attach timing, left to the adapter:** DZRP attaches on `CMD_INIT`,
  ZRCP greets on connect; so T attaches nothing, and `on_connect` is where a
  greeting or an attach goes.
- **Wrong against the binary today — a finding:** the agreed
  `--debug-listen-address` help text (identical in the three files) names
  `--dzrp-port`, `--zrcp-port` and `--gdb-port`, none of which exists before its
  server lands. T ships a text that is true of this binary ("no server is
  available yet, so the address is only checked"); **the first server to land
  replaces it with the agreed wording**, naming only the flags that then exist.
- **Not needed:** dzrp §4.2(4)'s fallback ("if sharing `src/esp01` from
  `src/remote` proves awkward, lift `esp::net` → `src/net/`"). esp01 is a
  self-contained static library with a public include root; `jnext_remote`
  links it and nothing moved.
- **Unverified here:** gdb §6.1 says "IPv4/IPv6". The seam binds either family
  and `--debug-listen-address ::1` is accepted (regression row), but no unit row
  binds `::1`: the CI container's IPv6 loopback is not guaranteed.

## 4. Tests

`remote_transport_test`, Qt-free, `gate: none` — it runs in `make unit-test`
and `make unit-test-sdl`. 64 rows:

| class | rows | what |
|---|---|---|
| XPT-FAKE | 13 | the fake itself: both directions, partial reads (`set_recv_chunk`) and writes (`set_window`), peer close after its data, server close, reset, nothing blocks, refused/failed listener |
| XPT-SRV | 29 | the Server over the fake: accept only inside a pass, the greeting, one command per call, partial writes held and delivered in order, partial reads assembled, hang-up / reset / `stop()` / adapter close each end the session once, the tail before EOF executed, linger and its bound, one client + busy reply (and the first client untouched), the output bound, input backpressure without loss, listener fault, open failure, destructor silence, `service_once(0)` never sleeps, a wait waits, returns at once on a buffered command, returns early when the client goes |
| XPT-NET | 13 | the real socket on 127.0.0.1 port 0: the bound port reported and logged, accept inside a pass, both directions, a 16 MiB reply through the kernel's partial writes, never blocks with no client or a silent one, a wait waits and returns early on data (a client thread), busy reply to a second client, disconnect and re-accept, port in use, a word / a name / a non-local address refused cleanly |
| XPT-PUMP | 9 | the real listener through `add_service` / `pump()`: `remote_attached`, one command while running, a queued chain drained while paused, `pump(wait)` waits and returns early on data, `PumpBudget{}` never blocks, a drop releases the dropped client's pause (SES-01), `remove_service` stops it |

Plus `debug-listen-address-func` (regression): bad values and names refused
with the documented message, `127.0.0.1` / `0.0.0.0` / `::1` accepted, nothing
listens.
