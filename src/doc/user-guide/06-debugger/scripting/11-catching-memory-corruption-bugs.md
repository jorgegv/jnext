# Catching memory-corruption bugs

The nastiest bugs in a Next program are silent corruptions. A stray write lands
in the code area, or in a banked page that should never be touched, and the
program crashes much later, somewhere unrelated; by then the culprit is gone.
The same goes for broken invariants: an interrupt handler that returns with a
clobbered register, a changed MMU slot or interrupts left off; MMU0 and MMU1
set to pages that do not belong together.

Scripts catch these at the moment they happen, at the instruction that does
it. This page is the path for a user of
[ChaseTheBug](https://github.com/vmorilla/ChaseTheBug), the CSpect plugin that
does these checks: each of its checks is a short script, and the scripts run
in the GUI while you play and headless in your build.

| ChaseTheBug | Script | Section |
|---|---|---|
| MemWatch: code-area write protection, range from the MAP | `on write 0x8000..(@__data_crt_head - 1)` | [1](#1-a-guard-on-the-code-area) |
| MMU consistency on NextREG 0x51 | `on nextreg 0x51 when …` | [2](#2-mmu0-and-mmu1-in-step) |
| Interrupt entry/exit audit, indented trace | `snap` / `changed()` / `depth()` | [3](#3-the-interrupt-handler-audit) |
| `MemPoint=addr,value` | `on write ADDR when VALUE == V` | [4](#4-one-value-at-one-address) |
| Ctrl+G / Ctrl+H arm and disarm | `on hostkey 1` / `on hostkey 2`, Alt+1 / Alt+2 | [5](#5-arm-and-disarm-from-the-keyboard) |

The examples run against the demo program in the source tree,
`test/00regression/nex/dsl_demo.nex`, and its deliberately buggy twin
`dsl_demo_buggy.nex`; the scripts are in `test/scripts/dsl/`. [The demo
scripts](12-the-demo-scripts.md) describes the program.

## Before you start: the MAP file

The checks name your program's addresses through its symbols, so build it with
a MAP file — z88dk's `-m`:

```console
$ zcc +zxn -startup=31 -clib=sdcc_ix -subtype=nex -create-app -m game.c -o game
```

and give it to JNEXT with `--map game.map` (or **Map ▸ Load MAP File ▸ Z88DK
Format...**). Two kinds of entry are useful:

- **labels** (`; addr` in the MAP): `@main_loop`, `@isr`, your functions;
- **section bounds** (`; const`): the linker's `__code_user_tail`,
  `__data_crt_head`, `__bss_head` …, which say where code ends and data begins.
  The demo's MAP has:

```
__code_user_tail                = $8317 ; const, public, def, , ,
__data_crt_head                 = $8318 ; const, public, def, , ,
```

Both resolve as `@name`.

**Arm the checks only when your program runs.** Before it is loaded, other code
runs at the same addresses — the boot, the loader — and loading writes all
over the code area. Start the guards `disabled`, and enable them from a rule
that knows the program is up (the demo sets a `magic` word when it has
initialised itself), or [from the keyboard](#5-arm-and-disarm-from-the-keyboard).

## 1. A guard on the code area

```
# range_watch.jds
disabled rom_guard: on write 0x0000..0x3FFF do
    log "write to code area ${ADDR:x4} (page ${PAGE:x2}) <- ${VALUE:x2} from PC ${PC:x4} src ${SOURCE}"
    stop "write into MMU0/1 code area"
end

disabled main_guard: on write 0x8000..(@__data_crt_head - 1) do
    log "write to main code ${ADDR:x4} <- ${VALUE:x2} from PC ${PC:x4}"
    stop "write into main code area"
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    enable rom_guard
    enable main_guard
    log "range_watch armed at FRAME ${FRAME}"
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    log "PASS range_watch: no write into code in 64 frames"
    exit 0
end
```

Two ranges: 0x0000–0x3FFF, where MMU0/MMU1 hold the ROM or banked code, and the
program's own code from 0x8000 up to the last byte before its data,
`@__data_crt_head - 1`. The guard costs nothing on writes elsewhere: the
debugger filters by range before it runs anything.

```console
$ jnext --headless --machine next --load test/00regression/nex/dsl_demo_buggy.nex \
      --map test/00regression/nex/dsl_demo.map --script test/scripts/dsl/range_watch.jds \
      --delayed-automatic-exit-frames 900
```

```
[debugger] [info] SCRIPT loaded test/scripts/dsl/range_watch.jds: 4 rules
[debugger] [info] [jds F:501 C:284201887] range_watch armed at FRAME 501
[debugger] [info] [jds F:501 C:284202151] write to main code 8317 <- EE from PC 8172
[debugger] [warning] SCRIPT STOP: write into main code area at PC=8172 FRAME=501 CYCLE=284202151
[platform] [info] script requested exit 3
```

The buggy build's stray write hit 0x8317 — the last byte of the code, the very
top of the guarded range — from the instruction at 0x8172. The good build runs
64 frames clean:

```
[debugger] [info] [jds F:501 C:284201880] range_watch armed at FRAME 501
[debugger] [info] [jds F:564 C:319939512] PASS range_watch: no write into code in 64 frames
[debugger] [info] [jds F:564 C:319939512] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

**In the GUI**, the same stop pauses the machine. A write is caught after its
instruction, so the debugger shows the machine paused at the *next* instruction;
the `SCRIPT STOP` line in the Script tab's log names the one that wrote (`PC`).
`PAGE` tells you which 8K page was written, and `SOURCE` whether the CPU, the
DMA or the Copper did it.

To guard a banked page wherever it is mapped, filter on the page: `on write
page 0x24 do stop … end` stops on any write into physical page 0x24, in
whichever slot it sits.

## 2. MMU0 and MMU1 in step

When a program pages a 16K bank into 0x0000–0x3FFF it writes MMU0 and MMU1
(NextREGs 0x50 and 0x51), and the two must agree: MMU1 = MMU0 + 1, or both
0xFF (the ROM). Check it on every write of 0x51:

```
# nextreg.jds
var armed = 0

disabled mmu_guard: on nextreg 0x51 when not ((nextreg[0x50] + 1 == VALUE) or (nextreg[0x50] == 0xFF and VALUE == 0xFF)) do
    log "MMU0 is ${nextreg[0x50]:x2} whereas MMU1 write is ${VALUE:x2} (src ${SOURCE}, PC ${PC:x4})"
    stop "MMU1 inconsistent with MMU0"
end

on nextreg 0x51 when armed == 1 do
    log "MMU1 write ${VALUE:x2} after MMU0 ${nextreg[0x50]:x2} at PC ${PC:x4}"
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    enable mmu_guard
    set armed = 1
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    log "PASS nextreg: MMU1 followed MMU0 on every NR 0x51 write"
    exit 0
end
```

When the `nextreg` rule runs, the write to 0x51 has happened and the earlier
write to 0x50 too, so `nextreg[0x50]` and `VALUE` are the pair to compare. The
whole check is in `when`: the debugger evaluates it, and the body runs only on a
mismatch. A Copper writing MMU1 is caught the same way (`SOURCE` 2).

Against the buggy build (MMU1 = 0x24 after MMU0 = 0x22):

```
[debugger] [info] [jds F:508 C:288173109] MMU0 is 22 whereas MMU1 write is 24 (src 0, PC 818B)
[debugger] [warning] SCRIPT STOP: MMU1 inconsistent with MMU0 at PC=818B FRAME=508 CYCLE=288173109
[debugger] [info] [jds F:508 C:288173109] MMU1 write 24 after MMU0 22 at PC 818B
[platform] [info] script requested exit 3
```

The good build logs every pair, every 8 frames, and passes:

```
[debugger] [info] [jds F:508 C:288173103] MMU1 write 23 after MMU0 22 at PC 818B
[debugger] [info] [jds F:508 C:288173168] MMU1 write FF after MMU0 FF at PC 8196
…
[debugger] [info] [jds F:564 C:319939512] PASS nextreg: MMU1 followed MMU0 on every NR 0x51 write
```

## 3. The interrupt-handler audit

A handler must leave the machine as it found it: the same registers, the same
return address on the stack, the same MMU slots — and interrupts enabled
again. Snapshot at the entry, compare at the exit, with a trace indented by
nesting depth:

```
# span_invariants.jds
disabled isr_in: on execute @isr do
    snap isr
    log indent (depth(isr) * 2) "==> isr at ${PC:x4}"
end

disabled isr_out: on execute @isr_exit do
    log indent (depth(isr) * 2) "<== isr at ${PC:x4}, Ret=${stack[0]:x4}"
    if depth(isr) == 0 then
        log "Warning: isr exit with empty entry stack"
    else
        if changed(isr, regs) then
            log "Warning: registers differ on exit from isr"
            dump_diff isr
            stop "isr clobbered registers"
        end
        if changed(isr, stack0) then
            log "Warning: top of stack modified ${isr.STACK0:x4} vs ${stack[0]:x4}"
            stop "isr modified return address"
        end
        if not IFF1 then
            stop "isr exit with interrupts disabled"
        end
        if changed(isr, mmu) then
            dump_diff isr
            stop "isr changed an MMU slot"
        end
        unsnap isr
    end
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 do
    enable isr_in
    enable isr_out
end

on execute @main_loop once when mem16[@magic] == 0xD5D5 and mem[@frames] >= 64 do
    log "PASS span_invariants: 64 frames of clean handler exits"
    exit 0
end
```

`@isr` is the handler's first instruction and `@isr_exit` its `RETI`. The
`unsnap` sits after the checks, so even a failing exit pops its entry: the `stop`
takes effect when the rule has finished. (See [Snapshots and span
checks](06-snapshots-and-span-checks.md) for `snap`, `changed()` and `dump_diff`.)

The good build:

```
[debugger] [info] [jds F:502 C:284768896]   ==> isr at 81DA
[debugger] [info] [jds F:502 C:284769124]   <== isr at 820B, Ret=8139
[debugger] [info] [jds F:503 C:285336163]   ==> isr at 81DA
[debugger] [info] [jds F:503 C:285336391]   <== isr at 820B, Ret=8139
…
[debugger] [info] [jds F:564 C:319939512] PASS span_invariants: 64 frames of clean handler exits
```

The demo's handler can be made to break each invariant on its own, by a byte
the script sets (`isr_fault`, 1 in the buggy build). Each fault is caught, and
named:

**IY returned one higher** (a register the handler did not save):

```
[debugger] [info] [jds F:502 C:284769133] Warning: registers differ on exit from isr
[debugger] [info] [jds F:502 C:284769133] dump_diff isr: IY 5C3B -> 5C3C
[debugger] [info] [jds F:502 C:284769133] dump_diff isr: IFF1 0 -> 1
[debugger] [info] [jds F:502 C:284769133] dump_diff isr: IFF2 0 -> 1
[debugger] [warning] SCRIPT STOP: isr clobbered registers at PC=820B FRAME=502 CYCLE=284769133
```

**MMU slot 7 left on page 0x0F**:

```
[debugger] [info] [jds F:502 C:284769143] dump_diff isr: MMU7 01 -> 0F
[debugger] [warning] SCRIPT STOP: isr changed an MMU slot at PC=820B FRAME=502 CYCLE=284769143
```

**The return address rewritten**:

```
[debugger] [info] [jds F:502 C:284769175] Warning: top of stack modified 8139 vs 8138
[debugger] [warning] SCRIPT STOP: isr modified return address at PC=820B FRAME=502 CYCLE=284769175
```

**A return without `EI`**:

```
[debugger] [warning] SCRIPT STOP: isr exit with interrupts disabled at PC=820B FRAME=502 CYCLE=284769119
```

**One `PUSH` more than `POP`s** (SP two bytes deeper):

```
[debugger] [info] [jds F:502 C:284769154] Warning: registers differ on exit from isr
[debugger] [info] [jds F:502 C:284769154] dump_diff isr: SP BCFE -> BCFC
…
[debugger] [warning] SCRIPT STOP: isr clobbered registers at PC=820B FRAME=502 CYCLE=284769154
```

The `IFF1 0 -> 1` lines are expected in every `dump_diff`: interrupts are off on
entry and on again at the exit. That is why the interrupt check is `not IFF1`
at the exit, and not a comparison with the entry.

The same script audits any routine: point `isr_in` at its entry and `isr_out`
at its `RET` (`when depth(…) > 0` if it has several exits).

## 4. One value at one address

ChaseTheBug's `MemPoint=addr,value`: stop only when one particular value is
written to one particular address. The condition goes in `when`:

```
# value_predicate.jds
disabled mempoint: on write @mempoint_addr when VALUE == 0xB7 do
    log "MemPoint hit at ${ADDR:x4}: forbidden value ${VALUE:x2} from PC ${PC:x4}"
    stop "MemPoint"
end
```

```
[debugger] [info] [jds F:520 C:294980080] MemPoint hit at 843B: forbidden value B7 from PC 8161
[debugger] [warning] SCRIPT STOP: MemPoint at PC=8161 FRAME=520 CYCLE=294980080
[platform] [info] script requested exit 3
```

## 5. Arm and disarm from the keyboard

ChaseTheBug's Ctrl+G and Ctrl+H are host keys here: **Alt+1** arms every guard,
**Alt+2** disarms them — so you can play through the boot and the loading, arm
the checks where the bug is, and disarm them for a part you know is fine.

```
# hostkey.jds (the guards of sections 1, 2 and 4, all disabled)
disabled rom_guard: on write 0x0000..0x3FFF do
    stop "write into MMU0/1 code area"
end
disabled main_guard: on write 0x8000..(@__data_crt_head - 1) do
    stop "write into main code area at ${ADDR:x4}"
end
disabled mmu_guard: on nextreg 0x51 when not ((nextreg[0x50] + 1 == VALUE) or (nextreg[0x50] == 0xFF and VALUE == 0xFF)) do
    stop "MMU1 inconsistent with MMU0"
end
disabled mempoint: on write @mempoint_addr when VALUE == 0xB7 do
    stop "MemPoint"
end

on hostkey 1 do
    enable rom_guard
    enable main_guard
    enable mmu_guard
    enable mempoint
    set armed = 1
    set armed_at = mem[@frames]
    log "MemWatch enabled"
end

on hostkey 2 do
    disable rom_guard
    disable main_guard
    disable mmu_guard
    disable mempoint
    set armed = 0
    log "MemWatch disabled"
end
```

(The complete file also declares `armed` and `armed_at` and two `exit` rules.)
Headless, `--script-key 560 1` presses Alt+1 at frame 560, once the demo runs:

```console
$ jnext --headless --machine next --load test/00regression/nex/dsl_demo_buggy.nex \
      --map test/00regression/nex/dsl_demo.map --script test/scripts/dsl/hostkey.jds \
      --script-key 560 1 --delayed-automatic-exit-frames 900
```

```
[debugger] [info] [jds F:560 C:318235104] MemWatch enabled
[debugger] [warning] SCRIPT STOP: write into main code area at 8317 at PC=8172 FRAME=561 CYCLE=318237990
[platform] [info] script requested exit 3
```

Without the key, nothing is armed and the same buggy program passes —
which is the point of arming late:

```
[debugger] [info] [jds F:620 C:351706305] PASS hostkey: never armed
```

## Putting it together

1. Build your program with `-m`.
2. Write the checks for its invariants — the scripts above are templates: change
   the symbols to your own.
3. Play it in the GUI: `jnext --map game.map --script guards.jds game.nex`,
   arm with Alt+1 where the bug shows, and the machine pauses at the
   instruction that breaks the invariant, with the Script tab's log saying which
   and where.
4. When the bug is fixed, keep the script: run it headless in your build with
   `--delayed-automatic-exit-frames`, and the bug can never come back
   unnoticed. For checks that need you to play, [record a
   session](10-recording-and-replaying.md) and load both scripts in the replay.
