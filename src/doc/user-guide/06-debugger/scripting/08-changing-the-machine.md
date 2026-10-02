# Changing the machine

Reading the machine never changes it. Changing it is always **explicit**: only
two actions write to the machine, `set` on a machine name and `out`, and you
can find every one of them in a script by searching for those two words. Every
change is **logged** as a `MUTATE` line, the same line the debugger's own panels
and a remote debugger produce, so a CI run can `grep MUTATE` to prove a script
changed nothing, or to see what it did. And every change is applied **at the
event's boundary**, through the debugger's own write paths, so a run that makes
changes is as repeatable as one that does not.

## `set`

| `set` target | Changes |
|---|---|
| a `var` | the variable (not the machine; not logged) |
| `A` … `R`, `AF` … `PC`, `AF2` … `HL2` | a register |
| `CF ZF SF PF HF NF` | one flag (0 or not 0) |
| `IFF1`, `IFF2`, `IM` | the interrupt state (`IM` 0, 1 or 2) |
| `mem[a]`, `mem16[a]` | a byte or a little-endian word, as the CPU sees address `a` |
| `phys[p, o]` | a byte in physical 8K page `p` |
| `nextreg[r]` | a NextREG, as a `NEXTREG` instruction would write it |
| `AUDIO_MUTE` | the host-side mute mask (not the machine; never saved in a snapshot) |

## `out`

```
out 0xFE 0x02
```

Writes a byte to a port, as an `OUT` instruction would.

## An example

```
on frame 150 once do
    out 0xFE 0x02                       ; red border
    set mem16[0x5C78] = 0               ; zero the FRAMES counter
    set phys[0x0A, 0] = 0xFF            ; page 0x0A is 0x4000..0x5FFF on a 48K
    log "mem[0x4000] now reads ${mem[0x4000]:x2}"
    set AUDIO_MUTE = 0b00111
    log "AUDIO_MUTE is ${AUDIO_MUTE}"
end
on frame 151 once do
    log "FRAMES is ${mem16[0x5C78]} one frame later"
    set mem[0x0000] = 0                 ; ROM: refused
end
on frame 160 do exit 0 end
```

```console
$ jnext --headless --machine 48k --script mut.jds
```

```
[debugger] [info] SCRIPT loaded mut.jds: 3 rules
[debugger] [info] MUTATE port out 0x00FE = 0x02 by 2
[debugger] [info] MUTATE mem cpu:0x5C78 2 bytes by 2
[debugger] [info] MUTATE mem page10:0x0000 0x0 -> 0xFF by 2
[debugger] [info] [jds F:150 C:84424784] mem[0x4000] now reads FF
[debugger] [info] MUTATE audio mute mask 0x0 -> 0x7 by 2
[debugger] [info] [jds F:150 C:84424784] AUDIO_MUTE is 7
[debugger] [info] [jds F:151 C:84983872] FRAMES is 1 one frame later
[debugger] [error] SCRIPT ERROR mut.jds:11:5: `set` at 0000: 0 of 1 byte(s) landed (the rest is read-only) — rule at 9:1 disabled
[debugger] [error] SCRIPT: a run-time error disabled a rule; exiting 1
[platform] [info] script requested exit 1
```

`by 2` names who made the change: the script engine is debugger client 2 here.
A change that does not wholly land — a write into ROM — is a run-time error,
never silent; the bytes that did land stay.

Registers, in an `execute` rule:

```
on execute 0x0038 once when FRAME >= 120 do
    log "before: A=${A:x2} F=${F:x2} CF=${CF} ZF=${ZF} HL=${HL:x4} IFF1=${IFF1} IM=${IM} HALTED=${HALTED}"
    set A = 0x12
    set CF = 1
    log "after:  A=${A:x2} F=${F:x2} CF=${CF}"
end
on frame 125 do exit 0 end
```

```
[debugger] [info] [jds F:120 C:67093160] before: A=00 F=5C CF=0 ZF=1 HL=5CB6 IFF1=0 IM=1 HALTED=0
[debugger] [info] MUTATE reg A 0x0 -> 0x12 by 2
[debugger] [info] MUTATE reg F 0x5C -> 0x5D by 2
[debugger] [info] [jds F:120 C:67093160] after:  A=12 F=5D CF=1
```

## When the change is seen

| The rule's event | A `set` or `out` is seen by |
|---|---|
| `execute`, `cycle`, `hostkey` (before the instruction) | **the instruction at `PC` itself**: `set PC = …` redirects before anything runs, and `set A = 0xFF` is the value the instruction reads |
| `read`, `write`, `io_read`, `io_write`, `nextreg`, `copper`, `dma`, `interrupt`, `nmi` (after the instruction) | **the next instruction**. The instruction that raised the event has completed with its own effects: `set mem[ADDR] = PREV` after a caught write undoes it before anyone reads it, but the write did happen |
| `frame`, `scanline` | the next instruction |

One exception to watch: a CPU `NEXTREG` write commits *after* its instruction's
events are delivered. So an `on io_write 0x253B` rule — or any `read`/`write`
rule of the same instruction — that sets the same NextREG is overwritten by the
guest's write a moment later. To override a guest NextREG write, use `on nextreg`,
which runs after it.

## A script's changes raise no events

A `set mem[x]` does not fire `on write x`, a `set nextreg[0x51]` does not fire
`on nextreg 0x51`, an `out` does not fire `on io_write`. Rules never trigger
each other:

```
on write 0x8000 do log "on write saw ${VALUE}" end
on frame 10 do set mem[0x8000] = 1 end
on frame 11 do log "mem[0x8000] is ${mem[0x8000]}" exit 0 end
```

```
[debugger] [info] MUTATE mem cpu:0x8000 0x0 -> 0x1 by 2
[debugger] [info] [jds F:11 C:6709264] mem[0x8000] is 1
```

This matters when you inject a fault to test a guard: the fault has to be
*upstream* of the write the guard watches, so the guest's own write is what
trips it. [`nextreg.jds`'s red twin](12-the-demo-scripts.md#nextregjds-mmu0-and-mmu1-in-step)
does exactly that.

## What a script may not do

- **Reset or reload the machine from inside a rule.** A soft reset (NR 0x02
  bit 0) would replace the machine the rule is running in, and is refused; a
  hard reset (NR 0x02 bit 1) is carried out by JNEXT after the rule, and is
  allowed (see [`reset`](03-events.md#interrupt-nmi-reset)):

```
[debugger] [error] nextreg_write() called from inside an event delivery — refused (§5: a handler may not drive, rewind or replace the machine it runs in)
[debugger] [error] SCRIPT ERROR softreset.jds:2:5: `set nextreg[]` was refused (unsupported) — rule at 1:1 disabled
```

- **Change the machine during an RZX recording or playback**: the recording
  would no longer reproduce. The change is refused.
- **Rewind across a change it made.** The rewind buffer captures the machine
  but not the script, and a replay does not re-apply the change, so the
  debugger refuses a step back that would have to replay past it. Stepping
  back to a point before the change is allowed (the change is undone with
  everything after that point), and so is rewinding to a frame start. A script
  that changes the machine while `--rewind-buffer-size` is on logs a warning
  once.
- **Rules do not fire during a rewind's replay.**

And one interaction: an `execute` rule at A that sets `PC` to B skips a
breakpoint at B for that one instruction. A script that redirects and wants to
stop at the new address says `stop` itself.

## Skipping an instruction

The classic: jump over an instruction that is in the way.

```
on execute @trap_insn when mem16[@magic] == 0xD5D5 do
    set PC = PC + 3        ; skip the 3-byte LD (trap_target),A
end
```

Each skip logs one line, `MUTATE reg PC 0x816C -> 0x816F by 2`. The complete
script is [`mutation.jds`](12-the-demo-scripts.md#mutationjds-changing-the-machine).
