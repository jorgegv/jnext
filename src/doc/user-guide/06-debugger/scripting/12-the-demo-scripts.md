# The demo scripts

JNEXT's source tree holds a demo program written to be debugged by scripts,
and nine scripts that check it. JNEXT's own test suite runs each script against
the program and against a deliberately buggy build of it, and requires the
first to pass and the second to fail with the right message. They are complete,
working examples of every kind of check in this chapter.

| What | Where |
|---|---|
| the program's source | `demo/dsl_demo/dsl_demo.asm` |
| the program, good and buggy, and its MAP | `test/00regression/nex/dsl_demo.nex`, `dsl_demo_buggy.nex`, `dsl_demo.map` |
| the scripts | `test/scripts/dsl/*.jds` |

Every script's header says how to run it, what it exits with and what it logs.
The commands below are run from the root of the source tree.

## The program

`dsl_demo` is a Next program in assembly (z88dk, `+zxn`), so every address a
script names is a MAP label. `make -C demo/dsl_demo` builds the good and the
buggy NEX and the MAP; the two builds share one MAP — every buggy difference is
an operand of the same size, so no label moves, and the build refuses to finish
if the two maps disagree on an address.

Every frame, under IM 2, the program:

| Does | Good build | Buggy build |
|---|---|---|
| polls Q, and latches the frame counter at the first Q it sees (`first_key`) | | |
| writes the frame counter to `mempoint_addr` | never 0xB7 | 0xB7 once, at its frame 20 |
| copies `patch_byte` to `patch_copy` | | |
| runs `trap_insn`, an `LD (trap_target),A` with A = 0xEE | | plus a second copy right after |
| a stray write | none | into `__data_crt_head - 1`, the last byte of the code |
| every 8 frames pages MMU0/MMU1 (`page_in_level`, `page_in_level_mmu1`) and back | 0x22, 0x23 | 0x22, **0x24** |
| every 16 frames uploads sprite patterns by DMA to port 0x5B | 256 bytes | 128 bytes |

Its IM 2 handler (`isr` to `isr_exit`) counts `frames`, and carries five faults
selected by the byte `isr_fault` (0 in the good build, 1 in the buggy one, any
other value set by a script): 1 returns IY one higher, 2 leaves MMU slot 7 on
page 0x0F, 3 rewrites the return address, 4 returns without `EI`, 5 leaves SP
two bytes deeper. The Copper runs a palette split: `MOVE NR 0x43,0x00`, `WAIT`
line 95 (96 in the buggy build), `MOVE NR 0x43,0x02`, `HALT`.

The program sets `magic` to 0xD5D5 when it has initialised itself, at about
frame 500 of a run; the scripts arm their checks only then, because before the
NEX is loaded other code runs at these addresses.

The common command line is:

```console
$ jnext --headless --machine next --load test/00regression/nex/dsl_demo.nex \
      --map test/00regression/nex/dsl_demo.map --script test/scripts/dsl/SCRIPT.jds \
      --delayed-automatic-exit-frames 900
```

with `dsl_demo_buggy.nex` for the buggy build.

## `range_watch.jds`: a guard on the code area

The #279 code-area guard — every write into 0x0000–0x3FFF and into
0x8000–`@__data_crt_head - 1` stops, naming the writer. The script is listed
and explained in [Catching memory-corruption bugs](11-catching-memory-corruption-bugs.md#1-a-guard-on-the-code-area).

Good build, exit 0:

```
[debugger] [info] [jds F:501 C:284201880] range_watch armed at FRAME 501
[debugger] [info] [jds F:564 C:319939512] PASS range_watch: no write into code in 64 frames
```

Buggy build, exit 3:

```
[debugger] [info] [jds F:501 C:284202151] write to main code 8317 <- EE from PC 8172
[debugger] [warning] SCRIPT STOP: write into main code area at PC=8172 FRAME=501 CYCLE=284202151
```

## `value_predicate.jds`: one value at one address

ChaseTheBug's MemPoint: stop only when 0xB7 is written to `@mempoint_addr`. The
condition is in `when`, so the debugger evaluates it and the body runs only on
a hit ([section 4](11-catching-memory-corruption-bugs.md#4-one-value-at-one-address)).

Good build, exit 0: `PASS value_predicate: 0xB7 never written in 64 frames`.
Buggy build, exit 3:

```
[debugger] [info] [jds F:520 C:294980080] MemPoint hit at 843B: forbidden value B7 from PC 8161
[debugger] [warning] SCRIPT STOP: MemPoint at PC=8161 FRAME=520 CYCLE=294980080
```

## `nextreg.jds`: MMU0 and MMU1 in step

On every write of NextREG 0x51, MMU1 must be MMU0 + 1, or both 0xFF ([section
2](11-catching-memory-corruption-bugs.md#2-mmu0-and-mmu1-in-step)). Buggy build,
exit 3:

```
[debugger] [info] [jds F:508 C:288173109] MMU0 is 22 whereas MMU1 write is 24 (src 0, PC 818B)
[debugger] [warning] SCRIPT STOP: MMU1 inconsistent with MMU0 at PC=818B FRAME=508 CYCLE=288173109
```

0x818B is `@page_in_level_mmu1`, the `NEXTREG 0x51` instruction itself: a CPU
NextREG write is delivered one instruction late, but `PC` names the writer.

**A red twin without a second build.** A script can inject the fault into the
*good* build — provided it is upstream of the write the guard watches, since a
script's own changes raise no events. Corrupt MMU0 after the guest has written
it and before it writes MMU1:

```
on execute @page_in_level_mmu1 when mem16[@magic] == 0xD5D5 do set nextreg[0x50] = 0xFF end
```

```console
$ jnext --headless --machine next --load test/00regression/nex/dsl_demo.nex \
      --map test/00regression/nex/dsl_demo.map --script test/scripts/dsl/nextreg.jds \
      --script mmu-upstream.jds --delayed-automatic-exit-frames 900
```

```
[debugger] [info] MUTATE nextreg 0x50 0x22 -> 0xFF by 2
[debugger] [info] [jds F:508 C:288173103] MMU0 is FF whereas MMU1 write is 23 (src 0, PC 818B)
[debugger] [warning] SCRIPT STOP: MMU1 inconsistent with MMU0 at PC=818B FRAME=508 CYCLE=288173103
```

The same injection one instruction earlier, at `@page_in_level`, does nothing:
the guest's own `NEXTREG 0x50,0x22` overwrites it, and the run passes. So does
an injection into 0x51 itself — the guard never sees a script's write.

## `span_invariants.jds`: the interrupt-handler audit

Snapshot at `@isr`, compare at `@isr_exit`: registers, return address, MMU
slots, and interrupts on ([section
3](11-catching-memory-corruption-bugs.md#3-the-interrupt-handler-audit), with
the output of all five faults). The faults other than the buggy build's default
are picked by a second script:

```
on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    set mem[@isr_fault] = 2
end
```

## `copper.jds`: the Copper split

Where along the frame the palette split lands, from the Copper's own events:

```
var armed = 0
var splits = 0

on copper move 0x43 when armed == 1 do
    log "Copper MOVE NR43=${VALUE:x2} at copper PC ${CPC} on cvc ${CVC} hc_ula ${HC_ULA}"
    if VALUE == 0x02 then
        assert CVC == 95 "the palette split lands on line 95"
        set splits = splits + 1
    end
end

on copper wait when armed == 1 and WAIT_V == 95 do
    assert CVC == 95 and HC_ULA >= WAIT_H "WAIT for line 95 is satisfied on the Copper's own line 95, at or past its threshold (GH #181)"
end

on copper halt once when armed == 1 do
    log "copper HALT at ${CPC}"
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    set armed = 1
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    assert splits >= 60 "the split ran every frame"
    log "PASS copper: 64 frames, the split on line 95 every time"
    exit 0
end
```

Good build, exit 0:

```
[debugger] [info] [jds F:501 C:284316507] Copper MOVE NR43=00 at copper PC 0 on cvc 0 hc_ula 1
[debugger] [info] [jds F:501 C:284489832] Copper MOVE NR43=02 at copper PC 2 on cvc 95 hc_ula 12
[debugger] [info] [jds F:501 C:284489832] copper HALT at 3
…
[debugger] [info] [jds F:564 C:319939512] PASS copper: 64 frames, the split on line 95 every time
```

Buggy build (`WAIT` line 96), exit 3:

```
[debugger] [info] [jds F:501 C:284491654] Copper MOVE NR43=02 at copper PC 2 on cvc 96 hc_ula 12
[debugger] [warning] [jds F:501 C:284491654] ASSERT FAILED: the palette split lands on line 95
[debugger] [warning] SCRIPT STOP: the palette split lands on line 95 at PC=8138 FRAME=501 CYCLE=284491654
```

Inside a `copper` rule, `CVC` and `HC_ULA` are the Copper step's own position.
`assert` inside an `if` fails only on the MOVE that matters.

## `dma.jds`: the DMA upload

Every transfer goes to the pattern port, never into memory below 0x4000, and
moves exactly 256 bytes:

```
var armed = 0
var uploads = 0

on dma start when armed == 1 do
    log "DMA ${SRC:x4} -> ${DST:x4} len ${LEN} mode ${DMA_MODE} (io dst ${IO_DST})"
end

on dma byte when armed == 1 and not IO_DST and (DST < 0x4000) do
    stop "DMA wrote into ROM/banked code at ${DST:x4} from ${SRC:x4}"
end

on dma end when armed == 1 do
    assert LEN == 256 "sprite upload must move exactly 256 bytes"
    set uploads = uploads + 1
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    set armed = 1
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    assert uploads == 3 "an upload every 16 frames (16, 32, 48)"
    log "PASS dma: ${uploads} uploads of 256 bytes to the pattern port"
    exit 0
end
```

Good build, exit 0:

```
[debugger] [info] [jds F:516 C:292711711] DMA 8217 -> 005B len 256 mode 1 (io dst 1)
[debugger] [info] [jds F:532 C:301787935] DMA 8217 -> 005B len 256 mode 1 (io dst 1)
[debugger] [info] [jds F:548 C:310864160] DMA 8217 -> 005B len 256 mode 1 (io dst 1)
[debugger] [info] [jds F:564 C:319939512] PASS dma: 3 uploads of 256 bytes to the pattern port
```

Buggy build, exit 3:

```
[debugger] [info] [jds F:516 C:292711718] DMA 8217 -> 005B len 128 mode 1 (io dst 1)
[debugger] [warning] [jds F:516 C:292712073] ASSERT FAILED: sprite upload must move exactly 256 bytes
[debugger] [warning] SCRIPT STOP: sprite upload must move exactly 256 bytes at PC=81A8 FRAME=516 CYCLE=292712073
```

## `mutation.jds`: changing the machine

A script that changes the machine: it skips an instruction by moving `PC`, and
patches a byte.

```
on execute @trap_insn when mem16[@magic] == 0xD5D5 do
    set PC = PC + 3
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    set mem[@patch_byte] = 0x5A
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    assert mem[@trap_target] == 0 "the trapped write never happened"
    assert mem[@patch_copy] == 0x5A "the program copied the patched byte"
    log "PASS mutation: trap skipped, patch copied"
    exit 0
end
```

Good build, exit 0 — one `MUTATE` line for the patch, and one per frame for the
skip:

```
[debugger] [info] MUTATE mem cpu:0x843C 0x0 -> 0x5A by 2
[debugger] [info] MUTATE reg PC 0x816C -> 0x816F by 2
[debugger] [info] MUTATE reg PC 0x816C -> 0x816F by 2
…
[debugger] [info] [jds F:564 C:319939514] PASS mutation: trap skipped, patch copied
[debugger] [info] [jds F:564 C:319939514] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

Buggy build (a second, unskipped copy of the write), exit 3:

```
[debugger] [warning] [jds F:564 C:319939520] ASSERT FAILED: the trapped write never happened
[debugger] [warning] SCRIPT STOP: the trapped write never happened at PC=8139 FRAME=564 CYCLE=319939520
[debugger] [info] [jds F:564 C:319939520] PASS mutation: trap skipped, patch copied
[debugger] [info] [jds F:564 C:319939520] SCRIPT EXIT 0
[debugger] [warning] SCRIPT EXIT 0 not taken: "the trapped write never happened" failed at the same boundary (exit 3)
[platform] [info] script requested exit 3
```

Note the `PASS` line after the failure: the rest of a rule runs after a
`stop`. The status is what counts — 3, because the `exit 0` at the same event as
a failure is not taken.

## `hostkey.jds`: arming from the keyboard

The guards of `range_watch`, `nextreg` and `value_predicate`, all disabled,
armed by host key 1 and disarmed by host key 2 ([section
5](11-catching-memory-corruption-bugs.md#5-arm-and-disarm-from-the-keyboard)).
Headless, `--script-key 560 1`:

| Build | `--script-key 560 1` | Exit | Log |
|---|---|---|---|
| buggy | yes | 3 | `MemWatch enabled`, then `SCRIPT STOP: write into main code area at 8317 at PC=8172 FRAME=561 …` |
| buggy | no | 0 | `PASS hostkey: never armed` — nothing armed, nothing seen |
| good | yes | 0 | `PASS hostkey: armed 40 frames, nothing tripped` |

## `replay_edge.jds`: input lands at the frame edge

A key pressed during frame *N* is seen from frame *N*+1 — never later in frame
*N*, never at *N*+2. The program polls Q in its main loop after `main_loop`,
and latches its frame counter at the first Q it sees:

```
on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] == 30 do
    press "q"
    log "press q in the program's frame 30 (FRAME ${FRAME})"
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@first_key] != 0 do
    release "q"
    assert mem[@first_key] == 31 "Q first seen in the frame after the one it was pressed in"
    log "PASS replay_edge: pressed in frame 30, first seen in frame ${mem[@first_key]}"
    exit 0
end
```

```
[debugger] [info] MUTATE key matrix [2,0] pressed at the next frame edge by 2
[debugger] [info] [jds F:530 C:300652538] press q in the program's frame 30 (FRAME 530)
[debugger] [info] MUTATE key matrix [2,0] released at the next frame edge by 2
[debugger] [info] [jds F:532 C:301787065] PASS replay_edge: pressed in frame 30, first seen in frame 31
[debugger] [info] [jds F:532 C:301787065] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

This is the property that makes a [recorded session](10-recording-and-replaying.md)
replay exactly: input changes only between frames.

## Two recorded sessions

`test/scripts/dsl/dapr-keyb.jds` and `dapr-joystick.jds` are not hand-written:
they were recorded in the Qt GUI, playing two interactive test programs (a
keyboard and a joystick test), with PNG captures. JNEXT's suite replays them
headless and requires every capture to be identical. See [Recording and
replaying a session](10-recording-and-replaying.md#programs-that-use-the-other-layers).
