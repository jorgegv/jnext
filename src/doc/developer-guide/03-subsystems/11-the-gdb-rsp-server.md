# 3.11 The GDB RSP server (z88dk-gdb)

`--gdb-port N` makes JNEXT serve the GDB Remote Serial Protocol to
`z88dk-gdb`, z88dk's debugger (a distribution's `gdb` has no Z80 target, so the
Z80 register model lives on this side, in the target description the server
hands the client). The server is `src/remote/gdb/`, one adapter over the shared
socket transport of 3.9, reaching the machine only through `jnext::dbg::Debugger`.
The design, with the client measurements every rule below comes from, is
`doc/design/debug-subsystem/gdb-rsp-frontend.md` (§12 records what was built
and every deviation).

## The pieces

| File | What it holds |
|---|---|
| `src/remote/gdb/rsp_codec.{h,cpp}` | framing (`$…#xx`), checksum, `}` escapes, hex; `RspParser` yields ONE event at a time (packet, 0x03 outside a packet, bad checksum, oversize) |
| `src/remote/gdb/target_desc.{h,cpp}` | the 600-byte `target.xml` as a `constexpr` string — a `static_assert` keeps it at or under 1022 bytes, because `z88dk-gdb` 2.4 copies the reply into `char[1024]` and a larger one crashes it — and the `g`/`G`/`p`/`P` packing in document order |
| `src/remote/gdb/rsp_server.{h,cpp}` | `GdbServer`, a `remote::Protocol` and a `dbg::Listener`: the packet table, the stop-reply state machine, the `Z` breakpoint map and `monitor` (`qRcmd`) |
| `src/platform/debug_servers.{h,cpp}` | `DebugServers` opens it beside the DZRP server — its own listener, its own backend client — in all three loop owners |

## Stop replies: one per request, never spontaneous

The client routes replies by position — the next packet after a request is
that request's reply — so a `T` stop reply may only ever answer a request the
client is waiting on: `c`, `s`, `i<len>` (z88dk's step over a CALL, served as
`run_to(pc+len)`) or `?`. One field, `owed_`, says a reply is owed and to what;
the backend's `Paused` push is kept only while one is owed and is written by
`on_notify()` after the pump's drain. Two rules come from running the real
client (gdb-rsp-frontend.md §12.5):

- **0x03 is not a request.** It pauses a running machine; the stop edge then
  answers whatever is owed. With nothing owed it sends nothing: z88dk-gdb puts
  the 0x03 of a Ctrl-C typed at its prompt next to its following `c`, and an
  answer there was taken as that `c`'s reply.
- **An inspection packet abandons an owed reply.** `g G p P m M X Z z qRcmd`
  are sent only by a client that counts the machine stopped; they pause a
  running machine first and drop any owed reply, which would otherwise arrive
  where the client expects this packet's.

## Memory, registers, breakpoints

`m`/`M`/`X` are the 64 KB CPU view. A write reports `E01` unless every byte
landed: `poke(Cpu)` returns the count `Mmu::write_landed()` says reached memory,
so an overlay that takes a byte over a ROM slot (Layer 2 write-over, DivMMC or
Multiface RAM) counts and ROM does not. `G` writes only the registers that
change — the client resends the whole file on every `set`, and rewriting an
unchanged PC would clear a HALT. `Z0`/`Z1` are one `Execute` subscription each,
`Z2`/`Z3`/`Z4` one `Mem` subscription (write, read, either), all owned by the
client and removed by its detach (`D`, `k` or a dropped socket), so a crashed
client can never leave a stopping breakpoint behind.

## Tests

- `gdb_rsp_test` (Qt-free, both configurations) runs the production `Server`
  over T's in-memory fake, on a real machine through `pump()`: the wire, the
  target description under the ceiling, every packet class, the stop-reply
  state machine, `monitor`, and DZRP and GDB on one backend.
- `gdb-cli-func`, `gdb-sdl-func` and `gdb-qt-func` drive the real binary over a
  socket with an independent client (`test/00regression/gdb-peer.py`).
- `gdb-z88dk-func` is the acceptance row: the real `z88dk-gdb` debugging
  `magic_bp_demo.nex` with its linker map. It skips when no `z88dk-gdb` is
  found (`Z88DK_GDB`, then `PATH`, then the source-tree default).
