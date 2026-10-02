# Reading the machine

Everything below can be read in any rule, in its `when` condition and in a
`var` initial value. Reading never disturbs the machine: a script's read of
memory fires no watchpoint and no `on read` rule, and a NextREG read never
triggers the register's own read side effects (NR 0x1E/0x1F, for instance,
are read as they are). Every name reads the machine as it is when the rule
runs — except `PC` and `CYCLE` inside an event rule, which are the event's
(see [Events](03-events.md#when-a-rule-runs)).

Most of these names can also be changed with `set`: see [Changing the
machine](08-changing-the-machine.md).

## Registers and flags

| Name | What |
|---|---|
| `A B C D E H L F I R` | 8-bit registers |
| `AF BC DE HL IX IY SP PC` | 16-bit registers |
| `AF2 BC2 DE2 HL2` | the alternate set (AF', BC', DE', HL') |
| `SF ZF HF PF NF CF` | the flags (sign, zero, half carry, parity/overflow, subtract, carry), 0 or 1 |
| `IFF1 IFF2` | the interrupt flip-flops, 0 or 1 |
| `IM` | the interrupt mode, 0, 1 or 2 |
| `HALTED` | 1 while the CPU is halted |

## Memory

| Expression | What |
|---|---|
| `mem[a]` | the byte at CPU address `a` (0 to 0xFFFF), as the CPU sees it now |
| `mem16[a]` | the little-endian word at `a` |
| `phys[p, o]` | the byte at offset `o` (0 to 0x1FFF) of physical 8K page `p`, wherever it is paged |
| `stack[n]` | the word at `SP + 2n`: `stack[0]` is the top of the stack |

`phys[]` reads a page whether or not it is mapped; a page that does not exist
is a run-time error.

## The MMU

| Expression | What |
|---|---|
| `mmu[s]` | MMU slot `s` (0 to 7) as NR 0x50+`s` reads it: 0xFF when the slot holds ROM |
| `page[s]` | the physical 8K page slot `s` holds now |

`dump_mmu` logs all eight, `slot:mmu/page`.

In a slot holding ROM, `page[s]` is the ROM's own page number, 0 to 7 — the
same numbers as RAM pages 0 to 7. A `page 0x00` filter therefore matches ROM
code as well as RAM page 0: check `mmu[s] != 0xFF` if you need RAM only.

## NextREGs

`nextreg[r]` reads NextREG `r` (0 to 0xFF), without its read side effects.

## Time and the raster

| Name | What |
|---|---|
| `FRAME` | frames since power-on (or since the last hard reset); the first is 0 |
| `CYCLE` | 28 MHz master-clock cycles since power-on: 8 per T-state at 3.5 MHz, 4 at 7 MHz, … In an event rule, the event's own cycle |
| `TFRAME` | master cycles since the current frame began |
| `RAW_VC`, `RAW_HC` | the hardware's frame counters: line 0 is the top of the whole frame (blanking included), and `RAW_HC` counts 7 MHz pixels along it |
| `VC_ULA`, `HC_ULA` | the ULA's counters: `VC_ULA` 0 is the first line of the 256×192 paper |
| `CVC` | the line the Copper, the line interrupt and NR 0x1E/0x1F use: `VC_ULA` plus the NR 0x64 offset |
| `PHC` | the pixel column: 0 is the first paper pixel |

There is deliberately no plain `VC`: these counters start at different places,
and using one for another is a classic off-by-a-border bug. `CVC` is the line
number a Next programmer reasons in. `CVC` and `HC_ULA` step a few pixels into
a raw line, not at its start, as on the hardware.

`CYCLE`, `TFRAME` and `FRAME` wrap at 32 bits like every value: `CYCLE` passes
2³¹ after about 77 seconds of emulated time. A difference (`CYCLE - t0`) stays
right across the wrap; comparing an absolute `CYCLE` does not. The `on cycle N`
filter itself is 64-bit.

## The machine and the mixer

| Name | What |
|---|---|
| `MACHINE` | 0 = 48K, 1 = 128K, 2 = +3, 4 = Next |
| `AUDIO_MUTE` | the host-side mute mask, the Audio panel's (bit 0 to 2: AY chips 0 to 2, bit 3: the DACs, bit 4: the beeper) |

## An example

On the 48K, at the first IM 1 interrupt after frame 120:

```
on execute 0x0038 once when FRAME >= 120 do
    log "BC=${BC:x4} B=${B:x2} C=${C:x2} IX=${IX:x4} IY=${IY:x4} SP=${SP:x4} I=${I:x2} R=${R:x2}"
    log "AF'=${AF2:x4} BC'=${BC2:x4} DE'=${DE2:x4} HL'=${HL2:x4}"
    log "flags: S=${SF} Z=${ZF} H=${HF} P/V=${PF} N=${NF} C=${CF}; IFF1=${IFF1} IFF2=${IFF2} IM=${IM} HALTED=${HALTED}"
    log "stack: ${stack[0]:x4} ${stack[1]:x4} ${stack[2]:x4}; mem16[SP]=${mem16[SP]:x4}"
    log "mmu: ${mmu[0]:x2} ${mmu[1]:x2} ${mmu[2]:x2} ${mmu[3]:x2}; page: ${page[0]:x2} ${page[1]:x2} ${page[2]:x2} ${page[3]:x2}"
    log "phys[0x0A, 0x1C78] = ${phys[0x0A, 0x1C78]} = mem[0x5C78] = ${mem[0x5C78]}"
    log "time: FRAME=${FRAME} CYCLE=${CYCLE} TFRAME=${TFRAME}; MACHINE=${MACHINE} AUDIO_MUTE=${AUDIO_MUTE}"
    dump_mmu
    dump_mem 0x5C78 3
end
on frame 125 do exit 0 end
```

```
[debugger] [info] [jds F:120 C:67093160] BC=0000 B=00 C=00 IX=0000 IY=5C3A SP=FF48 I=3F R=43
[debugger] [info] [jds F:120 C:67093160] AF'=0044 BC'=174B DE'=0006 HL'=107F
[debugger] [info] [jds F:120 C:67093160] flags: S=0 Z=1 H=1 P/V=1 N=0 C=0; IFF1=0 IFF2=0 IM=1 HALTED=0
[debugger] [info] [jds F:120 C:67093160] stack: 15EB 0000 15E1; mem16[SP]=15EB
[debugger] [info] [jds F:120 C:67093160] mmu: FF FF 0A 0B; page: 00 01 0A 0B
[debugger] [info] [jds F:120 C:67093160] phys[0x0A, 0x1C78] = 37 = mem[0x5C78] = 37
[debugger] [info] [jds F:120 C:67093160] time: FRAME=120 CYCLE=67093160 TFRAME=680; MACHINE=0 AUDIO_MUTE=0
[debugger] [info] [jds F:120 C:67093160] mmu 0:FF/00 1:FF/01 2:0A/0A 3:0B/0B 4:04/04 5:05/05 6:00/00 7:01/01
[debugger] [info] [jds F:120 C:67093160] 5C78: 25 00 00
```

`IFF1` is 0: accepting the interrupt cleared it. Page 0x0A at offset 0x1C78 is
address 0x5C78, because slot 2 (0x4000..0x5FFF) holds page 0x0A.

The raster counters, at an instruction a few lines into the frame:

```
[debugger] [info] [jds F:101 C:56478296] at 10B0: CVC=252 VC_ULA=252 HC_ULA=289 PHC=277 RAW_VC=4 RAW_HC=406
```

Raw line 4 is near the top of the frame, in the vertical blanking, which on the
48K comes *after* the 192 paper lines and the bottom border in `CVC`'s count: so
`CVC` is 252.

## Symbols: `@name`

```
on execute @main_loop do …
on write 0x8000..(@__data_crt_head - 1) do …
set mem[@patch_byte] = 0x5A
```

`@name` is the address of a symbol from a z88dk `.map` file, loaded with
`--map FILE` or **Map ▸ Load MAP File ▸ Z88DK Format...** — the same table the
debugger's disassembly uses. Symbols are resolved **when the script loads**: an
unknown one is an error then, never a 0 at run time.

```
[debugger] [error] SCRIPT ERROR err3.jds:1:12: unknown symbol `@no_such_symbol` (not in any loaded MAP)
```

Both kinds of z88dk MAP entry can be named: `; addr` entries (labels) and `;
const` entries — compile-time constants such as the section bounds
`__data_crt_head` and `__code_user_tail`. A constant is a value with a name: it
resolves as `@name` but never names an address in the disassembly. A name the
MAP defines twice takes its first definition, and a name that is both a label
and a constant is the label.

The name after `@` can be any identifier, keywords included (`@start`).

## Reading something that is not there

An accessor out of range is a run-time error, which switches the rule off:

```
on frame 51 do log "mmu[8]=${mmu[8]}" end
on frame 52 do log "phys=${phys[0x1FF, 0]}" end
on frame 60 do exit 0 end
```

```
[debugger] [error] SCRIPT ERROR accessors.jds:1:30: `mmu[]` slot 8 is outside 0..7 — rule at 1:1 disabled
[debugger] [error] SCRIPT ERROR accessors.jds:2:28: `phys[]` page 0x1FF cannot be read (invalid_page) — rule at 2:1 disabled
[debugger] [error] SCRIPT: a run-time error disabled a rule; exiting 1
[platform] [info] script requested exit 1
```
