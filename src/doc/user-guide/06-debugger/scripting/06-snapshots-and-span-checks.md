# Snapshots and span checks

Some checks compare the machine at two moments: *a routine must return with
the registers it was entered with*, *an interrupt handler must leave the MMU as
it found it*. A **snapshot** captures the machine at one event so a later event
can compare against it. Snapshots are kept on named **stacks**, so a routine
that is entered again before it returns (an interrupt during an interrupt, a
recursive call) pairs each exit with its own entry.

| Action or expression | What |
|---|---|
| `snap NAME` | push a snapshot onto stack `NAME` |
| `unsnap NAME` | pop it |
| `NAME.FIELD` | a field of the snapshot on top of the stack |
| `changed(NAME, GROUP)` | 1 if the machine differs from the top snapshot in that group |
| `depth(NAME)` | how many snapshots the stack holds (0 when empty) |
| `dump_diff NAME` | log every field that differs from the top snapshot, one line each |

## What a snapshot holds

The registers `AF BC DE HL IX IY SP PC AF2 BC2 DE2 HL2 I R` (and their 8-bit
halves `A B C D E H L F`), `IFF1 IFF2 IM`, `STACK0` (the word at SP), the eight
MMU slots `MMU[0]` to `MMU[7]` (as `mmu[s]` reads them), `FRAME` and `CYCLE`.
Each is a field: `isr.SP`, `isr.STACK0`, `isr.MMU[7]`, `isr.CYCLE`. Flags and
`HALTED` are not fields.

## The comparison groups

| `changed(NAME, …)` | True when |
|---|---|
| `regs` | any of `AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP` differs |
| `mmu` | any of the eight slots differs |
| `iff1` | `IFF1` differs |
| `stack0` | the word at the **current** SP differs from the one captured |

`dump_diff` logs one line per differing field, `NAME: FIELD old -> new`, in the
order `AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP IFF1 IFF2 IM STACK0 MMU0 … MMU7`.
`PC`, `I`, `R`, `FRAME` and `CYCLE` are never listed: between an entry and its
exit they always differ, and would bury the line that matters.

```
on frame 5 do
    snap s
    log "A at the snap: ${s.A:x2}, MMU slot 2: ${s.MMU[2]:x2}, STACK0 ${s.STACK0:x4}, FRAME ${s.FRAME}"
end
on frame 6 do
    log "a frame later: changed(s, regs)=${changed(s, regs)} changed(s, mmu)=${changed(s, mmu)} changed(s, iff1)=${changed(s, iff1)} changed(s, stack0)=${changed(s, stack0)}"
    dump_diff s
    exit 0
end
```

```
[debugger] [info] [jds F:5 C:3354640] A at the snap: 3F, MMU slot 2: 0A, STACK0 F302, FRAME 5
[debugger] [info] [jds F:6 C:3913744] a frame later: changed(s, regs)=1 changed(s, mmu)=0 changed(s, iff1)=0 changed(s, stack0)=0
[debugger] [info] [jds F:6 C:3913744] dump_diff s: AF 3F0B -> 3F03
[debugger] [info] [jds F:6 C:3913744] dump_diff s: HL CCD2 -> C44A
```

## A span check: the 48K's interrupt handler

The 48K ROM's IM 1 handler starts at 0x0038 and returns with the `RET` at
0x0052. Snap at the entry, compare at the exit:

```
# span.jds — check the 48K ROM's IM 1 handler (0x0038 .. its RET at 0x0052)
on execute 0x0038 when FRAME >= 100 and FRAME < 102 do
    snap im1
    log indent (depth(im1) * 2) "==> IM 1 handler, returns to ${stack[0]:x4}"
end

on execute 0x0052 when depth(im1) > 0 do
    log indent (depth(im1) * 2) "<== RET after ${(CYCLE - im1.CYCLE) / 8} T-states, IFF1 ${im1.IFF1} -> ${IFF1}"
    dump_diff im1
    if changed(im1, regs) or changed(im1, stack0) or changed(im1, mmu) then
        stop "the IM 1 handler did not restore the machine"
    end
    unsnap im1
end

on frame 102 do exit 0 end
```

```console
$ jnext --headless --machine 48k --script span.jds
```

```
[debugger] [info] SCRIPT loaded span.jds: 3 rules
[debugger] [info] [jds F:100 C:55911080]   ==> IM 1 handler, returns to 10AC
[debugger] [info] [jds F:100 C:55918056]   <== RET after 872 T-states, IFF1 0 -> 1
[debugger] [info] [jds F:100 C:55918056] dump_diff im1: IFF1 0 -> 1
[debugger] [info] [jds F:100 C:55918056] dump_diff im1: IFF2 0 -> 1
[debugger] [info] [jds F:101 C:56470168]   ==> IM 1 handler, returns to 15E6
[debugger] [info] [jds F:101 C:56477144]   <== RET after 872 T-states, IFF1 0 -> 1
[debugger] [info] [jds F:101 C:56477144] dump_diff im1: IFF1 0 -> 1
[debugger] [info] [jds F:101 C:56477144] dump_diff im1: IFF2 0 -> 1
[debugger] [info] [jds F:102 C:57587776] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

The ROM's handler is clean: the only difference is the interrupt flip-flops,
off on entry (accepting the interrupt cleared them) and on again at the `RET`
(its `EI`). That is also why the check for "interrupts re-enabled" is `if not
IFF1 then stop … end` at the exit, not `changed(…, iff1)`: in a correct handler
IFF1 *does* change. `when depth(im1) > 0` keeps the exit rule from firing on a
`RET` whose entry was not seen.

The [interrupt-exit audit](11-catching-memory-corruption-bugs.md#3-the-interrupt-handler-audit)
is the full form of this check, with a call-depth trace.

## Errors

A snapshot stack holds up to 4096 entries. `snap` on a full stack, and
`unsnap`, a field, `changed()` or `dump_diff` on an empty one, are run-time
errors, and leave the stack as it was. `depth()` of an empty stack is 0, not an
error — it is how a rule asks.

```
[debugger] [info] [jds F:5 C:3354640] depth 0
[debugger] [error] SCRIPT ERROR snaperr.jds:3:5: `unsnap`: snapshot stack `s` is empty — rule at 1:1 disabled
```
