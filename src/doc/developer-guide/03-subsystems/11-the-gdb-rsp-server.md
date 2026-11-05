# 3.10.4 The GDB RSP server (z88dk-gdb)

`--gdb-port N` makes JNEXT serve the GDB Remote Serial Protocol to
`z88dk-gdb`, z88dk's debugger (a distribution's `gdb` has no Z80 target, so the
Z80 register model lives on this side, in the target description the server
hands the client). The server is `src/remote/gdb/`, one adapter over the shared
socket transport ([3.10.1](10-1-the-socket-transport.md)), reaching the machine only through `jnext::dbg::Debugger`.
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

## Naming the memory in `monitor`

`monitor mmu` ends each slot line with `space_tag(SlotInfo::now)` (what the slot reads
now, overlays included) and, on a Next-layout machine (`ZXN_ISSUE2`), the
`physical_text()` of the slot's first byte, unless that equals the tag (`FLOAT`). Other machines
print the tag alone. `monitor sym ADDR` prints the tag of `source_page(addr)`, the memory the
lookup used; `monitor sym NAME` appends the label's own tag only when the label is paged.
The text is `debug/long_address.h`'s, never composed here.

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

`monitor src [ADDR]` answers `$ADDR file:line MEM` from
`source_location()` and `source_page()`: the primary location only, in the memory
mapped at ADDR now, `unmapped` when no record covers it. ADDR is a number, a
symbol (through `symbols().find_name()`; a paged symbol answers in its OWN
memory, `source_location(space, addr)`, as `monitor sym` does) or the PC. The memory spelling is the one of
`monitor mmu` (`P04`, `ROM0`, `BOOT`).

`monitor locals [FRAME]`, `monitor globals` and `monitor print [FRAME] PATH` print the C variables the debugger
facade reads ([7.10](../07-source-level-debugging/10-c-frames-and-values.md)); [7.7](../07-source-level-debugging/07-frontends.md)
has the formats. `pkt_monitor()` (`rsp_server.cpp:821`) hands `print` the raw rest of the packet text, not `split_words()`, because a
C path may hold spaces; the other two take the words. The reply convention is `monitor src`'s: a content refusal (no C
variables, no such frame, an unknown name) is an `O` line and `OK`; a wrong argument count or a non-decimal frame is `E01`. Each
verb asks `c_variables_status()` first and prints its reason when C variables are unavailable. Those three are read-only; the
write is `monitor set var [FRAME] PATH = VALUE` (`monitor_set_var()`, `rsp_server.cpp:1212`). `monitor set` takes a register
name (`i`, `r`, `im`), and those collide with C names (`i` is a local of `main`), so the C form is gdb's own `set var` spelling:
`pkt_monitor()` hands `monitor_set_var()` the raw rest after `set var` (a path may hold spaces), a leading decimal word is the frame, the
path is everything up to the first `=` and the value the trimmed rest. It calls `write_c_value()` ([7.10](../07-source-level-debugging/10-c-frames-and-values.md)):
success prints the value read back (`(int) g_int = 10`) and `OK`; a refusal is one `refused: <why>` line and `E01`, as `monitor set` answers a
refused register write (a write that did not happen must not answer `OK`); a missing path, value or `=` is `E01` with no line.

## Tests

- `gdb_rsp_test` (Qt-free, both configurations) runs the production `Server`
  over the transport's in-memory fake, on a real machine through `pump()`: the wire, the
  target description under the ceiling, every packet class, the stop-reply
  state machine, `monitor`, and DZRP and GDB on one backend.
- `gdb-cli-func`, `gdb-sdl-func` and `gdb-qt-func` drive the real binary over a
  socket with an independent client (`test/00regression/gdb-peer.py`).
- `gdb-z88dk-func` is the acceptance row: the real `z88dk-gdb` debugging
  `magic_bp_demo.nex` with its linker map. `make regression` provisions the client
  first (`make z88dk-gdb`: `Z88DK_GDB`, `PATH`, the source-tree default, else z88dk
  v2.4's built into `~/.cache/jnext/tools`), so the row never skips.
