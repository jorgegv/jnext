# 3.10.3 The DZRP server (DeZog)

`--dzrp-port N` makes JNEXT serve the DeZog Remote Protocol, the wire protocol
DeZog — the Z80 debugger for VS Code — uses to drive CSpect and real hardware.
The server is `src/remote/dzrp/`, one adapter over the shared socket transport
([3.10.1](10-1-the-socket-transport.md)), and it reaches the machine only through
the published debugger API (`jnext::dbg::Debugger`). It never includes `Emulator`
and never makes a socket call of its own. The design, with the reason behind
every rule below, is `doc/design/debug-subsystem/dzrp-frontend.md`; how it is
validated is `doc/testing/DZRP-VALIDATION.md`.

## The pieces

| File | What it holds |
|---|---|
| `src/remote/dzrp/dzrp_frame.{h,cpp}` | `FrameParser` and `encode_response`: the framing, and nothing else |
| `src/remote/dzrp/dzrp_server.{h,cpp}` | `DzrpServer`, a `remote::Protocol` (the transport's adapter interface) and a `dbg::Listener` (the backend's push interface) |
| `src/platform/debug_servers.{h,cpp}` | `DebugServers`: how a loop owner opens the server, registers it, and chooses each tick's pump budget |
| `src/core/cli_options.h`, `src/main.cpp` | `--dzrp-port` (0..65535, 0 = a port the system chooses) and the rule that `--debug-listen-address` is refused without a server port |

**Framing.** The two directions count their length differently, and both are
easy to get wrong: a command's length counts its payload only, a response's (and
a notification's) counts from the sequence byte. Sequence 0 is reserved for
notifications, and a command that uses it closes the connection. A payload over
16 MiB is refused as soon as its header arrives, and a frame that stops arriving
for 5 s is a truncated stream (the clock restarts on every byte that advances
it, as DeZog's own chunk timeout does). The sequence byte of a reply is echoed
verbatim: DeZog 3.8 compares it exactly.

## What is served

`DzrpServer::COMMANDS[]` is **one table**: the dispatcher looks commands up in
it, and `CMD_GET_SUPPORTED_COMMANDS` computes its bitfield from it, so a command
cannot be served but not advertised, or advertised but not served. Each row
carries its minimum payload length and a `session` flag.

- **Served**: `INIT`, `CLOSE`, registers get/set, CPU-view memory read/write,
  `SET_SLOT`, `GET_TBBLUE_REG`, ports in/out, `LOOPBACK`, `INTERRUPT_ON_OFF`,
  `CONTINUE`, `PAUSE`, breakpoints and watchpoints add/remove, the four sprite
  commands, `READ_STATE`/`WRITE_STATE`, and the DZRP 2.2.0 additions
  `GET_SUPPORTED_COMMANDS`, `READ_BANK_MEM`, `WRITE_BANK_MEM` and
  `ENABLE_BREAK_ON_INTERRUPT`. The bitfield is `DE 8F BF 07 80 0F 0C`.
- **Legacy**: `WRITE_BANK` and `SET_BORDER`, which DZRP 2.2.0 removed, are
  still served — DeZog 3.7.4 loads programs with `WRITE_BANK` — but never
  advertised; to a 2.2.0 client that uses them the server says so at debug
  level.
- **Not served**: `SET_BREAKPOINTS` and `RESTORE_MEM` (the RST-patching pair of
  a hardware stub: JNEXT's breakpoints are native), `EXEC_ASM` (there is no
  scratch context to run it in), and any unknown id. Each gets a sequence-only
  reply and a warning naming it, so a client is never left waiting.
- **Before `CMD_INIT`**, only the machine-free commands (`INIT`, `CLOSE`,
  `LOOPBACK`, `GET_SUPPORTED_COMMANDS`) are served. Every mutation must be
  attributed to a client, and `INIT` is what attaches one.
- A payload shorter than the command's fixed fields is a sequence-only reply and
  a warning, and nothing runs; the three commands whose reply has an error field
  (`INIT`, `WRITE_BANK`, `SET_SLOT`) report error 1 there instead.

## The two remote types

DeZog reaches JNEXT two ways, and both land on the same server:

- **`remoteType: "cspect"`** (DeZog 3.7.4 and earlier): the client CSpect's
  plugin speaks, a DZRP 2.0/2.1 client.
- **`remoteType: "dzrp"`** (DeZog 3.8): the 2.2.0 client, which asks for
  `GET_SUPPORTED_COMMANDS` right after `INIT` and turns every clear bit into a
  named client-side error rather than a timeout.

`CMD_INIT` records the client's version and answers `2.2.0`, machine type 4
(ZXNEXT) and `jnext v<version>` to both. The only behaviour that depends on the
recorded version is the debug line for a legacy command.

## Banks, and the ROM

On the wire a bank is an 8 KB page, 0..223. Breakpoints and watchpoints carry it
as `bank + 1`, with 0 meaning a plain 64K address; `bank1_to_page()` turns it
into the backend's `page` qualifier, so a banked breakpoint fires only while
that page is mapped at the PC's slot.

**The ROM is where DZRP's model and the Next's part ways.** DeZog calls the ROM
bank 0xFF (0xFE in older versions), but the backend names a ROM slot by its ROM
image (`MemSpace::Kind::Rom`), not by a DZRP bank, so there is nothing to
compare a ROM bank byte against:

- a breakpoint or watchpoint on a ROM bank is armed in **every** bank
  (`PAGE_ANY`) — one that never fired would be worse;
- the `NTF_PAUSE` bank byte of a stop in a ROM slot is 0xFF for slot 0 (what
  DeZog 3.7.4 matches) and 0 for slot 1, where 3.8's 0xFF + 1 does not fit;
- `READ_BANK_MEM` bank 0xFF is DeZog 3.8's 16 KB ROM bank: offsets
  0x0000-0x1FFF are slot 0's half, 0x2000-0x3FFF slot 1's. While a slot holds
  ROM the half is read through the space the backend reports for it
  (`SlotInfo.space`), so the bytes are the CPU's. While RAM is paged at 0x0000
  it is read from `Debugger::rom_select()`, the image the legacy paging
  selects. The adapter composes no ROM index of its own.
- **The limit**: `MemSpace::Rom` cannot name the NR 0x8C *alternate* ROM. With
  it enabled the CPU reads the alt ROM at 0x0000 (and `CMD_READ_MEM` shows it),
  but bank 0xFF shows the ROM image beneath.

## The loop and the pump

The server has no thread. It is a `Service` registered with the loop owner's
`Debugger`, and every command runs inside `Debugger::pump()` on the emulation
thread, between two frames. That is what makes a `CMD_PAUSE` land on a frame
boundary, a `CONTINUE` reply leave before anything executes, and every register
and memory read coherent.

`DebugServers` opens the server in all three loop owners and picks each tick's
pump budget ([3.9.3](09-3-sessions-the-pump-and-reconstruct.md)). While the
machine is paused with a remote attached, one pump drains a queued chain of
commands, which is why a DeZog step — four to six round trips — costs about one
tick rather than one tick per trip.

Notifications are built in `on_notify()`, the transport's flush after the pump's
commands, not inside the backend's `on_paused()` push: building one removes the
leftover temporary breakpoints, which is not something to do from inside the
backend's fan-out.

## How DZRP maps onto the backend

| DZRP | Backend |
|---|---|
| `CMD_INIT` | `attach(ClientInfo{name, Dzrp})` and `set_listener`; then `pause(cid)` — only if the machine is running, since pausing a paused machine would re-attribute a GUI's or a magic breakpoint's pause to this client |
| `CMD_CLOSE`, or the socket dropping | `detach(cid)`: the connection itself stays for `CLOSE`, and a client may `INIT` again on it |
| `CMD_CONTINUE` | each enabled address becomes a **transient** `Execute` subscription, owned by this client, then `run(cid)`; the reply is sent first |
| a stop | `on_paused()` records it; `on_notify()` sends one `NTF_PAUSE`, reason in this order: a temporary (0), this client's own pause (1), its breakpoint (2), its watchpoint (3/4, at the accessed address), break-on-interrupt (255, "Break on interrupt."), anything else (255, with who stopped it) |
| `CMD_PAUSE` | `pause(cid)`; one notification only if it stopped a running machine |
| `ADD/REMOVE_BREAKPOINT` | an `Execute` subscription with the page qualifier; ids 1..65535, never reused in a session, 0 = refused |
| `ADD/REMOVE_WATCHPOINT` | a `Mem` subscription over the range; removed by the exact tuple the add carried (DZRP watchpoints have no id) |
| `ENABLE_BREAK_ON_INTERRUPT` | an `IntAck` subscription that stops |
| `READ_STATE` / `WRITE_STATE` | a named **bookmark** (at most 8), sent as the token `JNXB` + its name; refused mid-frame, since DeZog does not re-read registers after a save; a restore validates the token before any backend call |
| memory, banks, ports, NextREGs, sprites | `peek`/`poke` of `MemSpace::cpu()`/`page(n)`, `set_mmu_slot`, `port_in`/`port_out`, `nextreg_peek`, the sprite and pattern accessors |

**"Owner = internal" temporaries.** The design calls DeZog's per-step temporary
breakpoints "owner=internal". In code that means **not user-visible**, not
unowned: they carry the `transient` flag, which hides them from every list the
Qt debugger shows, but they belong to the DZRP client. Owned, they go with the
client's `detach()`; unowned, they would outlive a crashed DeZog and stop the
machine later with nobody to answer. They are removed at every notified stop,
because the backend drops transients only at a stop it caused itself.

**SES-01, the detach rule.** `detach()` removes the client's subscriptions — its
breakpoints, watchpoints, temporaries and break-on-interrupt — and its
bookmarks. A pause that is **this client's** (its `PAUSE`, its `INIT`, or a stop
on one of its subscriptions) passes to another arming client if one is attached
— an open Qt debugger window, another remote — and the machine stays paused; it
is released only when no arming client remains
([3.9.3](09-3-sessions-the-pump-and-reconstruct.md)). A pause made from the Qt
window, or a magic breakpoint's unowned stop, survives it. So a DeZog that
crashes, or a socket that simply drops, never leaves the machine hung with
nobody to resume it.

Two consequences of the shared machine: the Qt debugger and DeZog can both
pause and resume it, and whichever acted last wins; and a `--headless` stop that
would exit a run (`ExitNonZero`) pauses and notifies instead while a remote
client is connected, so a client blocked in a `CONTINUE` gets its stop.

## Tests

- `dzrp_adapter_test` runs the production `DzrpServer` and `Server`
  over the transport's in-memory fake, on a real `Emulator` and `Debugger`, and
  asserts bytes on the wire and machine state for every command, including both
  sides of every bank and range check.
- `remote_transport_test` pins the transport the server stands on, including the
  reconnect rules its rows found (XPT-SRV-30..33).
- The `dzrp-*-func` regression rows drive a live JNEXT over `--dzrp-port 0`
  through two independently written clients — `tools/cspect_dzrp` and the
  owner's dezogif_ng conformance suite, copied into `test/dzrp/` — in all three
  frontends.
- An interactive DeZog session in VS Code is the part no row can cover; its
  checklist is in `doc/testing/DZRP-VALIDATION.md`.
