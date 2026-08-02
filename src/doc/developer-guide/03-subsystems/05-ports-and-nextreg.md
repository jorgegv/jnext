# 3.5 Ports and NextREG

Everything on a Spectrum that is not memory is reached through the Z80's I/O
space, with `IN` and `OUT` naming a 16-bit port address. A ZX Spectrum Next
inherits every port the machine ever had — the ULA, the 128K paging latches, the
AY chips, a decade of third-party peripherals — and then adds a great deal of
hardware of its own, far more than the remaining port addresses could sensibly
express. Its answer is the **NextREG**: a 256-entry register file that holds
essentially all of the Next-specific control state, reached through just two
ports, `0x243B` to select a register and `0x253B` to read or write the selected
one.

NextREG is therefore not a peripheral. It is the machine's control plane, and
almost every other subsystem in this guide is configured through it — machine
type and video timing, CPU speed, MMU slot mapping, layer priority and the
palettes, clip windows, interrupt setup, the DivMMC and Multiface gates. This
subsystem sits between the CPU and all of them, which has a practical
consequence for debugging: a mistake in a port decode or a register handler
almost never shows up here. It shows up as a wrong picture, a silent AY or a
machine that will not boot.

Two files do the work. `src/port/port_dispatch.{h,cpp}` routes `IN` and `OUT` to
whoever owns the address, and `src/port/nextreg.{h,cpp}` is the register file
behind the port pair. Both are small; almost all of the *content* — which port
belongs to whom, under what gate — lives in `Emulator::init()`, where every
handler is registered.

## Why masking, not comparison

A ZX Spectrum peripheral does not decode a 16-bit port number. It watches a few
address lines and responds when they match, ignoring the rest. The ULA is the
canonical case: `zxnext.vhd:2582` decodes it as `port_fe <= '1' when cpu_a(0) =
'0'` — that is, *any* even port, which is why the `OUT (0xFC),A` border trick
works on real hardware. A dispatcher built on `port == 0xFE` gets that wrong,
and jnext's did until it was fixed.

`PortHandler` is therefore a `(mask, value)` pair plus optional read and write
callbacks, and `register_handler(mask, value, rd, wr)` is the whole registration
API. The ULA is `(0x0001, 0x0000)`; Timex screen mode is `(0x00FF, 0x00FF)` —
low byte only, so `OUT (0x12FF),A` reaches it; the Kempston mouse is
`(0x0FFF, 0x0ADF)`, twelve lines; and NextREG select is `(0xFFFF, 0x243B)`, a
genuine full-address decode.

## Dispatch

There is **one** path, not a fast path and a slow path: a linear scan over a
`std::vector<PortHandler>` holding roughly fifty entries. Overlapping decodes
are resolved by **most-specific-wins** — the handler whose mask has the most set
bits is the most constrained decode, so it takes the port. That models the VHDL,
where decodes are exclusive one-hot equations, without anyone having to
hand-maintain a priority list.

Three details are worth knowing before adding a handler:

- **Reads fall through on a missing read side.** If the most specific match has
  no read callback, the scan continues to the next match that does. This models
  write-only ports, whose reads land on an overlapping decode instead.
- **Writes can decline.** A handler may call `PortDispatch::decline_write()` as
  its last action, and dispatch then retries with the next-most-specific match.
  The reason is that VHDL port decodes run in *parallel*: a decode ANDed with a
  cleared `internal_port_enable` bit simply drops out of the equation set, and
  an overlapping decode fires instead. So with NR `0x84` bit 2 clear,
  `OUT (0x7FF1),A` stops being a Soundrive write and becomes a plain `0x7FFD`
  paging write.
- **Observers run before dispatch, unconditionally.** `add_io_observer()`
  installs a side-effect-only callback invoked on every `IN` and `OUT`
  regardless of what matches. The Multiface needs this: its enable and disable
  strobes share low bytes (`0x1F`, `0x3F`, `0x9F`, `0xBF`) with the Kempston
  joystick and the DACs, which win the single-handler dispatch, yet the
  Multiface state machine must still see the strobe go past.

An unmatched read returns `0xFF` via the default-read callback, matching
`zxnext.vhd:1868-1878`. The floating bus is **not** the default: it is the
registered read handler of port `0xFF` alone, because the VHDL mux that produces
it is scoped to the `port_ff_rd` decode. `PortDispatch::in()` and `out()` also
carry the RZX hooks — a playback override that substitutes recorded `IN` values,
and a recording tap.

## How a subsystem gets a port

It does not register itself. Every handler is a lambda in `Emulator::init()`
capturing `this`, and that is deliberate rather than lazy: nearly every real
port is *gated*, and the gate is emulator-tier state. The gate is the
`internal_port_enable` vector in NR `0x82`-`0x85`, ANDed with the bus-port mask
NR `0x86`-`0x89` when NR `0x80` bit 7 enables the expansion bus
(`zxnext.vhd:2392-2393`). `Emulator::effective_internal_port_enable()` computes
one byte of it, and `propagate_effective_port_enables()` re-pushes the derived
bits into the subsystems that keep their own shadow copy — contention, DivMMC
and the Multiface. A handler that skips its gate check is a bug, not a shortcut.

`init()` clears the handler and observer lists before registering anything,
because a soft reset re-runs it and duplicate handlers would double-fire the
auto-incrementing ports.

## The NextREG file

`NextReg` holds `std::array<uint8_t,256> regs_` plus three parallel arrays of
per-register handlers. Port `0x243B` writes the select latch; port `0x253B`
reads or writes the selected register.

**Writes** go through `write_handlers_[reg]` if one is installed, and the
handler **returns the canonical byte to be cached**. That return value is the
contract: a handler that masks reserved bits, or refuses a gated write, returns
the masked or unchanged byte, so the readback matches hardware. Without a
handler the raw byte is stored. NR `0x01`, `0x0E` and `0x0F` are guarded
read-only, being board-generic constants with no write strobe in the VHDL
decoder.

**Reads** go through `read_handlers_[reg]`, which normally pull live state from
the owning subsystem, and otherwise fall back to the cache. Two further shapes
exist:

- `set_destructive_read_handler()` — for registers whose *read* mutates state.
  NR `0x2C` and `0x2E` latch the Pi-I2S low bits into the NR `0x2D` shadow, for
  instance. Such a handler may only be installed together with a
  side-effect-free twin, because `peek()` — the debugger's read — refuses to
  call a destructive handler. The API is shaped so that it is impossible to
  declare one without also saying how to observe it safely.
- `cached(reg)` is **the last byte written and nothing more**, not the
  register's value. Several registers are not owned by their cache: NR `0x69`
  bit 7 is also latched by port `0x123B`, and NR `0x15` is recomposed from the
  renderer. Reading those through `cached()` yields a stale byte, which is
  exactly how the debugger once reported Layer 2 as off while displaying its
  graphics. Use `peek()`.

The Z80N `NEXTREG rr,nn` opcode has its **own** write path,
`PortDispatch::nextreg_opcode_write_cb`, which writes the named register
directly and leaves the `0x243B` select latch alone, per `zxnext.vhd:4739-4744`.
Routing it through the port pair instead clobbered the latch and broke raster
waits in real games. Both paths defer during the per-instruction tick window
(`Emulator::enqueue_cpu_nr_write`); see
[A frame, end to end](../02-architecture/03-a-frame-end-to-end.md).

## Registers that are cached only

A few registers are stored and read back faithfully but drive nothing, because
the hardware behind them is out of scope for a software emulator. The
expansion-bus control bits of NR `0x81` are latched in `nr_81_` with no consumer
— though bit 5, the NMI debounce, *is* wired — as is the expansion-bus/ESP reset
line in NR `0x02` bit 7. The Pi GPIO output enables NR `0x90` and `0x93` are
write-masked per the VHDL but drive no pins. The Pi and ESP GPIO **inputs**, NR
`0x98`-`0x9B` and `0xA9`, get read handlers that return `0x00`, meaning "nothing
attached" — deliberately, rather than letting the output shadow leak into the
input read.

**The authoritative register and port lists are not reproduced here.** They live
in the FPGA source tree as `cores/zxnext/nextreg.txt` and
`cores/zxnext/ports.txt`, with the Next wiki linked from `doc/REFERENCES.md`.
What jnext adds on top of them is the per-row VHDL citation in `test/nextreg/`
and `test/port/`.
