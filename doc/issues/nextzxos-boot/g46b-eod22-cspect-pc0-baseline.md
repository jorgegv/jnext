# G46(b) EOD-22 — CSpect PC=$0000 baseline

**Date:** 2026-05-08 21:40 UTC
**Investigator:** subagent (DZRP ground-truth via CSpect)
**CSpect:** 3.1.0.0 + DezogPlugin v2.3.0.20958 (DZRP 2.0.0)
**Image:** `roms/nextzxos-1gb-fat32fix.img`
**Launch:** `mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img` (no `-debug`)

## Question

Per EOD-21 (jnext post-NR$03 fix), jnext's supervisor PC lands at
`$0000` ~640 times in 6 minutes wall clock without NR $02 soft reset
firing. Each landing re-runs the post-reset init at `$00EF` / `$00F3`.
Three hypotheses: stack underflow popping `$0000`, a `JP $0000` from
corrupt code, or IM 1 ISR returning to wrong PC.

**Does CSpect's NextZXOS land at PC=$0000 in normal post-boot
operation?**

## Method

1. Launched CSpect; waited 30 s for NextZXOS to settle.
2. Connected via DZRP, set persistent BP at `$0000` (bank=0, 64K).
3. `cont()`; waited up to 90 s for `NTF_PAUSE`.
4. On timeout, `pause()` and snapshot state.

Script: `tools/cspect_dzrp/g46b_eod22_pc0.py` (committed for reuse).

## Result — BP at $0000 did NOT fire

```
# DZRP 2.0.0 program='DeZogPlugin v2.3.0.20958'
# pre-BP state:  PC=0C90 SP=FF3F AF=0322 BC=0921 ... IM=1 | slots=[FF FF 0A 11 04 05 00 01]
# NR $03=$33  NR $07=$33
# BP at $0000 set (id=1); waiting up to 90s ...
# TIMEOUT after 90.1s — BP at $0000 did NOT fire
# post-pause:    PC=0C90 SP=FF3F AF=0322 BC=0920 ... IM=1 | slots=[FF FF 0A 11 04 05 00 01]
```

CSpect was actively executing (BC `$0921 → $0920` between samples, R=0
each time — a tight keyboard-poll loop), not halted. Across 90 s of
free-running, PC never visited `$0000` once.

### Where CSpect actually sits

`PC = $0C90` in slot-0 ROM (NR $50 = `$FF` = 16K ROM region).

```
$0C80  F1 A7 28 0B 0D 20 08 10 06 3D CA 70 0B 18 E3 76
$0C90  21 3B 5C CB 6E 28 EA CB AE 3A 08 5C 21 41 5C FE   ; LD HL,$5C3B; BIT 5,(HL); JR Z,$0C7C
$0CA0  0E 28 06 CB 86 FE 10 30 1F F5 FE 06 20 09 21 6A
```

This is the canonical **48K BASIC ROM `KEY-INPUT`/`MAIN-EXEC` wait
loop** at the well-known address `$0C90` — `BIT 5,(IY+$01)` polling
the keyboard flag, RET on no key. Boot vector intact at `$0000`:
`F3 C3 EF 00` = `DI; JP $00EF`. NR $03=$33 (+3 mode); slots map
`[FF FF 0A 11 04 05 00 01]` (ROM-paged-in slots 0/1, RAM banks
$0A/$11/04/05/00/01 across slots 2..7).

CSpect is sitting at the post-boot interactive prompt, exactly as
expected — no `JP $0000`, no RET-to-$0000, no IM-1 ISR returning to
$0000 in the 90 s window. If any of these occurred at jnext's rate
(~1.8 hits/sec), the BP would have fired roughly 160 times.

## Verdict

**jnext's behaviour is a bug.** Real-Next NextZXOS does NOT land at
`$0000` in normal post-boot operation. The ~640 PC=`$0000` events
jnext sees in 6 minutes are spurious and indicate one of the three
EOD-21 hypotheses is firing in jnext but not on real Next.

Next-session priority: instrument `JNEXT_G46B_PC0_TRAP` to log
`prev_pc`/`SP`/`mem[SP]`/`opcode-at-prev_pc`/`IFF1` on every PC=$0000
transition in jnext, then re-run the same DZRP test with a BP at the
matching jnext trigger site to confirm whether real-Next executes the
same code path without diverting to $0000.
