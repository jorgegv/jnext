# Events

The event is what a rule waits for. Each one has a **filter** (which addresses,
ports, registers, lines…), a **delivery point** (the instant the rule runs),
and a **payload**: upper-case names that are valid only inside that rule and
describe this occurrence of the event.

| Event | Fires on |
|---|---|
| `execute` *addr* | an instruction about to execute |
| `read` *addr*, `write` *addr* | a memory read or write |
| `io_read` *port*, `io_write` *port* | a port read or write |
| `nextreg` *reg* | a NextREG write, by the CPU or the Copper |
| `frame` [*n*] | the end of a frame |
| `scanline` *n* | the start of a line |
| `cycle` *n* | the first instruction boundary at or after master cycle *n* |
| `interrupt` | an accepted maskable interrupt |
| `nmi` | an NMI |
| `reset` | a hard or soft reset |
| `hostkey` *n* | host key *n*, 1 to 8 |
| `copper move` / `wait` / `halt` | a Copper instruction |
| `dma start` / `byte` / `end` | a DMA transfer |
| `stop` | any pause of the machine |

## When a rule runs

Everything a script does happens with the machine stopped at an **instruction
boundary**. No rule ever runs in the middle of a memory write or a port
access; that is why a script can look at anything without disturbing it.

| Event | The rule runs |
|---|---|
| `execute`, `cycle`, `hostkey` | **before** the instruction at `PC` |
| `read`, `write`, `io_read`, `io_write`, DMA, Copper | **after** the instruction that caused it |
| `nextreg` by the Copper or the DMA | after the instruction during which it happened |
| `nextreg` by the CPU | **one instruction later**: a CPU NextREG write commits after its instruction's events are delivered. `PC` still names the instruction that wrote it |
| `interrupt`, `nmi`, `reset` | at the boundary where it happened |
| `frame` | at the frame edge, before the next frame's first instruction |
| `scanline` | at the first instruction boundary after the line began — at most one instruction late |
| `stop` | when the machine has paused |

**`PC` in a rule is the event's PC**: the instruction that caused it, not where
the CPU is now. For `read`/`write` the CPU has already moved on; for `frame` it
is the instruction during which the frame ended. `dump_regs` shows the live
registers, so the two can differ:

```
[debugger] [info] [jds F:20 C:11741208] caught my own stop: boom, PC 11DC, loaded at FRAME 0
```

**`CYCLE` in a rule is the event's cycle** too, and so is the `C:` of every log
line the rule writes. The other state names (`FRAME`, the raster counters,
`TFRAME`, the registers, memory) read the machine as it is when the rule runs.

**Order.** Several events at one boundary are delivered in the order they
happened, and the rules on one event in file order. Up to 512 events can be
delivered at one boundary; if more happen (a Copper program busy through a
long `LDIR`, say) the first 512 are delivered and the run logs `SCRIPT: event
ring overflowed at CYCLE …, N events dropped` — a script cannot see the
dropped ones, and it is told so rather than left to guess.

## Address filters: `execute`, `read`, `write`

```
on execute 0x0038 do …                 ; one address
on write 0x4000..0x57FF do …           ; an inclusive range
on write @buffer..(@buffer + 255) do … ; symbols and expressions
on write 0x5C00..0x5CFF page 0x0A do … ; …and only when the slot holds page 0x0A
on write page 0x0A..0x0B do …          ; any address, while in pages 0x0A..0x0B
on execute page 0x00 do …              ; any instruction in page 0x00
```

An address is a 16-bit CPU address (0 to 0xFFFF). **`page`** is a physical 8K
page — the one the MMU has mapped behind the address at the time of the access
— so a filter can follow one piece of memory wherever it is paged. With an
address range, `page` narrows it ("this range, while it holds this page");
alone, it means every address of that page. An `execute` page range may name
up to 16 pages.

Payload:

| Name | `execute` | `read` | `write` | Meaning |
|---|---|---|---|---|
| `ADDR` | ✓ | ✓ | ✓ | the address |
| `PAGE` | ✓ | ✓ | ✓ | the physical 8K page behind `ADDR` |
| `VALUE` | | ✓ | ✓ | the byte read or written |
| `PREV` | | | ✓ | the byte before the write |
| `SOURCE` | | ✓ | ✓ | who did it: `CPU`, `DMA` or `COPPER` |
| `PC` | ✓ | ✓ | ✓ | the instruction |

One caveat on `PAGE`: the DivMMC, Multiface and Layer 2 overlays sit in front
of the MMU, and a write one of them captures is still seen (which is what you
want when guarding memory) but `PAGE` names the MMU slot's page, not the
overlay's.

## Port filters: `io_read`, `io_write`

```
on io_write 0xFE do …                  ; low byte 0xFE: any port xxFE
on io_read 0x7FFE do …                 ; above 0xFF: exactly this port
on io_write 0x10..0x20 do …            ; a range of low bytes
on io_read mask 0x00FF value 0xFE do … ; mask and value, as written
```

A port written 0x00 to 0xFF is matched on its **low byte**, like the hardware
decodes it, whatever the high byte is; a port above 0xFF is matched exactly. A
range decodes like a single port, so it must lie wholly in 0x00..0xFF or wholly
above it:

```
[debugger] [error] SCRIPT ERROR errs2.jds:1:13: a port range lies wholly in 0x00..0xFF (decoded on the low byte, GH #222) or wholly above it
```

With `mask M value V` a port matches when `port & M == V`.

Payload: `PORT` (the full 16-bit port), `VALUE`, `SOURCE` (a DMA byte to a
port is an `io_write` with `SOURCE == DMA`), `PC`.

```
on io_read mask 0x00FF value 0xFE when FRAME == 120 do
    log "mask/value form: ${PORT:x4} -> ${VALUE:x2}"
end
on io_read 0x7FFE when FRAME == 120 do
    log "a port above 0xFF is matched exactly: ${PORT:x4}"
end
```

```
[debugger] [info] [jds F:120 C:67094464] mask/value form: FEFE -> BF
[debugger] [info] [jds F:120 C:67094936] mask/value form: FDFE -> BF
…
[debugger] [info] [jds F:120 C:67097768] mask/value form: 7FFE -> BF
[debugger] [info] [jds F:120 C:67097768] a port above 0xFF is matched exactly: 7FFE
```

That is the 48K ROM scanning the eight keyboard half-rows.

## `nextreg`

```
on nextreg 0x51 do …
on nextreg 0x50..0x57 do …
```

A write to a NextREG, by the CPU (`NEXTREG` or port 0x253B) or by the Copper.
When the rule runs, the write has happened: `nextreg[REG]` already reads
`VALUE`.

Payload: `REG`, `VALUE`, `PREV` (the register before the write), `SOURCE`,
`PC`.

```
on nextreg 0x07 once do
    log "nextreg: NR ${REG:x2} ${PREV:x2} -> ${VALUE:x2} by PC ${PC:x4} (SOURCE ${SOURCE})"
end
```

```
[debugger] [info] [jds F:23 C:13427480] nextreg: NR 07 00 -> 03 by PC 6B07 (SOURCE 0)
```

(`jnext --headless --machine next`: the boot firmware setting 28 MHz.)

## `frame`, `scanline`, `cycle`

```
on frame do …          ; every frame
on frame 100 do …      ; when FRAME is 100
on scanline 96 do …    ; when line 96 begins, every frame
on cycle 50000000 do … ; once, at the first boundary at or after that cycle
```

- **`frame`** runs at the frame edge. `FRAME` counts frames since power-on, the
  first being 0, and a hard reset starts it again at 0, so an `on frame N` rule
  fires again after one.
- **`scanline N`** runs once per frame as line *N* begins, *N* being the line
  number a Next programmer uses: `CVC`, the counter NextREGs 0x1E/0x1F read, the
  line interrupt (NR 0x22/0x23) and the Copper's `WAIT` compare against. Line 0
  is the first line of the 256×192 paper. *N* is 0 to 1023.
- **`cycle N`** runs once, at the first instruction boundary at which `CYCLE`
  is *N* or more. `CYCLE` is the 28 MHz master clock since power-on (8 cycles
  per T-state at 3.5 MHz).

Payload: `PC`.

```
on scanline 100 when FRAME == 50 do
    log "scanline 100: CVC=${CVC} RAW_VC=${RAW_VC} RAW_HC=${RAW_HC} TFRAME=${TFRAME}"
end
on cycle 50000000 do
    log "cycle: CYCLE=${CYCLE} FRAME=${FRAME} TFRAME=${TFRAME}"
end
on frame 99 do
    log "frame: FRAME=${FRAME}, PC ${PC:x4}"
end
```

```
[debugger] [info] [jds F:50 C:28249088] scanline 100: CVC=99 RAW_VC=164 RAW_HC=20 TFRAME=293968
[debugger] [info] [jds F:89 C:50000056] cycle: CYCLE=50000056 FRAME=89 TFRAME=239800
[debugger] [info] [jds F:99 C:55910456] frame: FRAME=99, PC 15ED
```

Note `CVC=99` in the `scanline 100` rule. The rule runs as the raw line begins
(raw line 164 on the 48K: 64 lines of border and blanking come first), and
`CVC` — exactly as a program reading NR 0x1E/0x1F would see it — steps to 100 a
few pixels into that line. `RAW_HC` shows how far in the beam is. See
[Time and the raster](04-reading-the-machine.md#time-and-the-raster).

## `interrupt`, `nmi`, `reset`

```
on interrupt do …   ; a maskable interrupt accepted (IM 0, 1 or 2)
on nmi do …         ; an NMI: the Multiface or DivMMC button, or NR 0x02
on reset do …       ; a hard or a soft reset
```

Payload: `PC` — for `interrupt` and `nmi`, the instruction that was
interrupted.

```
on interrupt once do
    log "interrupt: accepted at PC ${PC:x4}, IM ${IM}, I=${I:x2}"
end
on execute 0x0038 once do
    log "execute: IM 1 handler entered, ADDR=${ADDR:x4} PAGE=${PAGE:x2} SP=${SP:x4} return to ${stack[0]:x4}"
end
```

```
[debugger] [info] [jds F:83 C:46406264] interrupt: accepted at PC 0E5C, IM 1, I=3F
[debugger] [info] [jds F:83 C:46406368] execute: IM 1 handler entered, ADDR=0038 PAGE=00 SP=FF48 return to 0E5C
```

An NMI from the Multiface button (`--delayed-nmi-frames 600 nmi`, on the Next):

```
[debugger] [info] MUTATE nmi button MF pressed by 0
[debugger] [info] [jds F:600 C:340358408] nmi: taken at PC 0C8F
```

A reset: the Next's firmware resets the machine itself at frame 310 of a cold
boot, and a script can cause a hard reset by writing NR 0x02 (see [Changing the
machine](08-changing-the-machine.md)):

```
on reset do
    log "reset at PC ${PC:x4}, FRAME ${FRAME}"
end
on frame 650 once do
    set nextreg[0x02] = 0x02          ; NR 0x02 bit 1: a hard reset
end
```

```
[debugger] [info] [jds F:310 C:175891965] reset at PC 6D2F, FRAME 310
[debugger] [info] MUTATE nextreg 0x02 0x2 -> 0x2 by 2
[debugger] [info] RESET hard (guest-initiated cold boot, reconstruct re-applied)
[debugger] [info] [jds F:0 C:0] reset at PC 0000, FRAME 0
```

After a hard reset `FRAME` is 0 again. Without `once`, the `frame 650` rule
would reset the machine every time it reached frame 650.

## `hostkey`

```
on hostkey 1 do …
```

Host key *N*, 1 to 8: **Alt+1** … **Alt+8** in the emulator or debugger window,
or `--script-key FRAME N` headless. The rule runs before the next instruction
(headless: at the edge of frame FRAME). See [Keys, joysticks and host
keys](07-keys-joysticks-and-host-keys.md#host-keys-alt1-to-alt8).

Payload: `KEY` (1 to 8), `PC`.

## Copper: `copper move`, `copper wait`, `copper halt`

```
on copper move do …               ; every MOVE
on copper move 0x40..0x44 do …    ; a MOVE to NextREGs 0x40..0x44
on copper wait do …               ; every WAIT, when it is satisfied
on copper halt do …               ; the HALT
on copper move at 0..15 do …      ; …only at Copper addresses 0..15
```

The Copper's own view of its program: one event per instruction, at its own
raster position. `at A..B` filters on the Copper's program counter (0 to
1023). A Copper `MOVE` to a NextREG raises both `copper move` and `nextreg`
(with `SOURCE == COPPER`).

| Name | `move` | `wait` | `halt` | Meaning |
|---|---|---|---|---|
| `CPC` | ✓ | ✓ | ✓ | the Copper's program counter |
| `HC_ULA`, `CVC` | ✓ | ✓ | ✓ | the raster position of this Copper step (see below) |
| `REG`, `VALUE` | ✓ | | | the NextREG and the value |
| `WAIT_V`, `WAIT_H` | | ✓ | | the WAIT's line, and its horizontal threshold `(hpos << 3) + 12` |
| `PC` | ✓ | ✓ | ✓ | the CPU instruction during which it happened |

Inside a `copper` rule, `HC_ULA` and `CVC` are **the Copper step's** position,
not the live beam's — the rule runs at most one CPU instruction later, so the
two can differ. Use `RAW_HC`/`RAW_VC` for the live beam. A `WAIT` is satisfied
on the Copper's own line equal to its `WAIT_V`, at or past `WAIT_H`:

```
on copper move 0x43 when armed == 1 do
    log "Copper MOVE NR43=${VALUE:x2} at copper PC ${CPC} on cvc ${CVC} hc_ula ${HC_ULA}"
end
on copper halt once when armed == 1 do
    log "copper HALT at ${CPC}"
end
```

```
[debugger] [info] [jds F:501 C:284316507] Copper MOVE NR43=00 at copper PC 0 on cvc 0 hc_ula 1
[debugger] [info] [jds F:501 C:284489832] Copper MOVE NR43=02 at copper PC 2 on cvc 95 hc_ula 12
[debugger] [info] [jds F:501 C:284489832] copper HALT at 3
```

(From [`copper.jds`](12-the-demo-scripts.md#copperjds-the-copper-split).)

## DMA: `dma start`, `dma byte`, `dma end`

```
on dma start do …                 ; a transfer begins
on dma byte do …                  ; every byte it moves
on dma byte 0x0000..0x3FFF do …   ; a byte whose DESTINATION is in 0x0000..0x3FFF
on dma end do …                   ; it finished
```

The address range of `dma byte` is the destination; there is no `page` filter
for DMA (`dma byte page 3` is an error).

| Name | `start` | `byte` | `end` | Meaning |
|---|---|---|---|---|
| `SRC`, `DST` | ✓ | ✓ | ✓ | the block's source and destination; on `byte`, this byte's |
| `VALUE` | | ✓ | | the byte |
| `LEN` | ✓ | | ✓ | the programmed length on `start`; the bytes moved on `end` |
| `DMA_MODE` | ✓ | | ✓ | 0 byte, 1 continuous, 2 burst |
| `IO_SRC`, `IO_DST` | ✓ | ✓ | | 1 when that side is a port, not memory |
| `PC` | ✓ | ✓ | ✓ | the CPU instruction during which it happened |

```
on dma start when armed == 1 do
    log "DMA ${SRC:x4} -> ${DST:x4} len ${LEN} mode ${DMA_MODE} (io dst ${IO_DST})"
end
```

```
[debugger] [info] [jds F:516 C:292711711] DMA 8217 -> 005B len 256 mode 1 (io dst 1)
```

(From [`dma.jds`](12-the-demo-scripts.md#dmajds-the-dma-upload): a sprite-pattern
upload to port 0x5B.)

## `stop`

```
on stop do …
on stop when REASON == "boom" do …
```

Runs whenever the machine pauses — a script's `stop` or failed `assert`, a
breakpoint, the Pause button, a step that finished — after every rule of the
event that caused it has run. Headless, the run then ends; the `on stop` rules
run first, so they can log the state the run ended in.

Payload: `REASON`, a string — a script stop's own message, otherwise the kind
of pause (`user`, `breakpoint`, …) — and `PC`, where the machine is paused.

A `stop` inside an `on stop` rule only logs: the machine is already stopped.
And an `exit` there never changes the status — the pause that ran the rule has
already decided it:

```
on frame 20 do stop "x" end
on stop do
    log "on stop runs, then exit 7"
    exit 7
end
```

```
[debugger] [warning] SCRIPT STOP: x at PC=11E0 FRAME=20 CYCLE=11741208
[debugger] [info] [jds F:20 C:11741208] on stop runs, then exit 7
[debugger] [info] [jds F:20 C:11741208] SCRIPT EXIT 7
[platform] [info] script requested exit 3
```

```
on frame 20 do stop "boom" end
on stop when REASON == "boom" do
    log "caught my own stop: ${REASON}, PC ${PC:x4}, loaded at FRAME ${loaded_at}"
    stop "this only logs"
end
```

```
[debugger] [warning] SCRIPT STOP: boom at PC=11E0 FRAME=20 CYCLE=11741208
[debugger] [info] [jds F:20 C:11741208] caught my own stop: boom, PC 11DC, loaded at FRAME 0
[debugger] [warning] SCRIPT STOP: this only logs at PC=11DC FRAME=20 CYCLE=11741208
[platform] [info] script requested exit 3
```

(`loaded_at` is `var loaded_at = FRAME`.) The first `SCRIPT STOP` names the
`frame` event's PC, 0x11E0; the `on stop` rule sees the paused PC, 0x11DC.

## What a payload name outside its event does

Using a payload name the event does not carry is an error when the script
loads, never a silent 0:

```
[debugger] [error] SCRIPT ERROR err2.jds:4:31: `PREV` is event payload and is not available in a `read` event
[debugger] [error] SCRIPT ERROR err2.jds:5:22: `ADDR` is event payload and is not available in a `frame` event
```

The same goes for a `var` initial value and a filter bound: they are computed
when the script loads, outside any event, so a payload name there is an error.
