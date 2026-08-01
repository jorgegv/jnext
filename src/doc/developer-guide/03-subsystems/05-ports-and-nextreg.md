# 3.5 Ports and NextREG

Two files make up this subsystem: `src/port/port_dispatch.{h,cpp}` routes Z80
`IN`/`OUT` to whoever owns the address, and `src/port/nextreg.{h,cpp}` is the
256-entry NextREG register file behind ports `0x243B` / `0x253B`. Both are small.
Almost all of the *content* — which port belongs to whom, under what gate — lives
in `Emulator::init()`, where every handler is registered.

## Why masking, not comparison

A ZX Spectrum peripheral does not decode a 16-bit port number. It watches a few
address lines and responds when they match, ignoring the rest. The ULA is the
canonical case: `zxnext.vhd:2582` decodes it as `port_fe <= '1' when cpu_a(0) =
'0'` — *any* even port, which is why the `OUT (0xFC),A` border trick works on
real hardware. A dispatcher built on `port == 0xFE` gets that wrong, and jnext's
did until it was fixed.

`PortHandler` is therefore a `(mask, value)` pair plus optional read and write
callbacks, and `register_handler(mask, value, rd, wr)` is the whole registration
API. The ULA is `(0x0001, 0x0000)`; Timex screen mode is `(0x00FF, 0x00FF)` —
low byte only, so `OUT (0x12FF),A` reaches it; the Kempston mouse is
`(0x0FFF, 0x0ADF)` — twelve lines; NextREG select is `(0xFFFF, 0x243B)`, a
genuine full-address decode.

## Dispatch

There is **one** path, not a fast path and a slow path: a linear scan over a
`std::vector<PortHandler>` holding roughly fifty entries. Overlapping decodes are
resolved by **most-specific-wins** — the handler whose mask has the most set bits
is the most constrained decode and takes the port. That models the VHDL, where
decodes are exclusive one-hot equations, without needing to hand-maintain a
priority list.

Three details are worth knowing before adding a handler:

- **Reads fall through on a missing read side.** If the most specific match has
  no read callback, the scan continues to the next one that does. This models
  write-only ports whose reads land on an overlapping decode.
- **Writes can decline.** A handler may call `PortDispatch::decline_write()` as
  its last action, and dispatch retries with the next-most-specific match. VHDL
  port decodes run in *parallel*, so a decode ANDed with a cleared
  `internal_port_enable` bit simply drops out of the equation set and leaves an
  overlapping decode to fire: with NR `0x84` bit 2 clear, `OUT (0x7FF1),A` stops
  being a Soundrive write and becomes a plain `0x7FFD` paging write.
- **Observers run before dispatch, unconditionally.** `add_io_observer()`
  installs a side-effect-only callback invoked on every `IN` and `OUT` regardless
  of what matches. The Multiface needs this: its enable/disable strobes share low
  bytes (`0x1F`/`0x3F`/`0x9F`/`0xBF`) with the Kempston joystick and the DACs,
  which win the single-handler dispatch, but the Multiface state machine must
  still see the strobe.

An unmatched read returns `0xFF` via the default-read callback, matching
`zxnext.vhd:1868-1878`. The floating bus is **not** the default: it is the
registered read handler of port `0xFF` alone, because the VHDL mux producing it
is scoped to the `port_ff_rd` decode. `PortDispatch::in()`/`out()` also carry the
RZX hooks — a playback override substituting recorded `IN` values, and a
recording tap.

## How a subsystem gets a port

It does not register itself. Every handler is a lambda in `Emulator::init()`
capturing `this`. That is deliberate: nearly every real port is *gated*, and the
gate is emulator-tier state — the `internal_port_enable` vector NR `0x82`-`0x85`,
ANDed with the bus-port mask NR `0x86`-`0x89` when NR `0x80` bit 7 enables the
expansion bus (`zxnext.vhd:2392-2393`).
`Emulator::effective_internal_port_enable()` computes one byte of it;
`propagate_effective_port_enables()` re-pushes the derived bits into the
subsystems holding their own shadow (contention, DivMMC, Multiface). A handler
that skips its gate check is a bug, not a shortcut.

`init()` clears the handler and observer lists first, because a soft reset re-runs
it and duplicates would double-fire auto-incrementing ports.

## The NextREG file

`NextReg` holds `std::array<uint8_t,256> regs_` plus three parallel arrays of
per-register handlers. Port `0x243B` writes the select latch, port `0x253B` reads
or writes the selected register.

**Writes** go through `write_handlers_[reg]` if one is installed, and the handler
**returns the canonical byte to be cached**. That return value is the contract:
a handler that masks reserved bits or refuses a gated write returns the masked or
unchanged byte, so the readback matches hardware. Without a handler the raw byte
is stored. NR `0x01`, `0x0E` and `0x0F` are guarded read-only (board-generic
constants with no write strobe in the VHDL decoder).

**Reads** go through `read_handlers_[reg]`, which normally pulls live state from
the owning subsystem, or fall back to the cache. Two further shapes exist:

- `set_destructive_read_handler()` — for registers whose *read* mutates state
  (NR `0x2C` / `0x2E` latch the Pi-I2S low bits into the NR `0x2D` shadow). Such
  a handler may only be installed together with a side-effect-free twin, because
  `peek()` — the debugger's read — refuses to call a destructive handler. The API
  makes it impossible to declare one without saying how to observe it safely.
- `cached(reg)` is **the last byte written and nothing more**, not the register's
  value. Several registers are not owned by their cache (NR `0x69` bit 7 is also
  latched by port `0x123B`; NR `0x15` is recomposed from the renderer). Reading
  those from `cached()` yields a stale byte — which is how the debugger once
  reported Layer 2 as off while displaying its graphics. Use `peek()`.

The Z80N `NEXTREG rr,nn` opcode has its **own** write path
(`PortDispatch::nextreg_opcode_write_cb`) writing the named register directly and
leaving the `0x243B` select latch alone, per `zxnext.vhd:4739-4744`. Routing it
through the port pair clobbered the latch and broke raster waits in real games.
Both paths defer during the per-instruction tick window
(`Emulator::enqueue_cpu_nr_write`); see
[A frame, end to end](../02-architecture/03-a-frame-end-to-end.md).

## Registers that are cached only

A few registers are stored and read back faithfully but drive nothing, because
the hardware behind them is out of scope. The expansion-bus control bits of NR
`0x81` are latched in `nr_81_` with no consumer (bit 5, the NMI debounce, *is*
wired), as is the expansion-bus/ESP reset line NR `0x02` bit 7. The Pi GPIO
output enables NR `0x90` / `0x93` are write-masked per VHDL but drive no pins.
The Pi and ESP GPIO **inputs** — NR `0x98`-`0x9B` and `0xA9` — get read handlers
returning `0x00` ("nothing attached"), deliberately rather than leaking the
output shadow into the input read.

**The authoritative register and port lists are not reproduced here.** They live
in the FPGA source tree as `cores/zxnext/nextreg.txt` and
`cores/zxnext/ports.txt`, with the Next wiki linked from `doc/REFERENCES.md`.
What jnext adds is the per-row VHDL citation in `test/nextreg/` and `test/port/`.
