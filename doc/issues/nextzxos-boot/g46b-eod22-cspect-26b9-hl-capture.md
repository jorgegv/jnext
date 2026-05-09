# G46(b) EOD-22 — CSpect $26B9 HL + sysvar capture (vs jnext divergence)

**Date:** 2026-05-09 08:48 CEST
**Tooling:** `tools/cspect_dzrp/g46b_eod22_26b9_hl_capture.py` (BP capture)
+ `tools/cspect_dzrp/g46b_eod22_26b9_idle_sysvars.py` (idle dump).
**Target:** CSpect 3.1.0.0 + DeZogPlugin v2.3.0.20958, NextZXOS image
`roms/nextzxos-1gb-fat32fix.img`, launched with `-debug`.

## Method

1. Killed running CSpect, relaunched with `-debug` so it halts at PC=$0000.
2. Connected via DZRP, installed persistent BPs at $26B9 and $26C2.
3. Issued CONTINUE; waited up to 60 s for first BP hit.
4. CSpect free-ran past boot to its idle REPL — **neither BP fired**. Paused
   manually; idle PC = $0C90 (= 48K BASIC `KEY-INPUT`, the canonical post-boot
   idle for a real Next).
5. Captured idle-state sysvars + memory at the wrapper sites for indirect
   comparison.

## Key finding: CSpect's supervisor never executes the $26B0 wrapper

Both `$26B9` and `$26C2` BPs **0 hits in 60 s**. Real-Next-faithful CSpect's
NextZXOS supervisor never invokes that bank-1 wrapper protocol during boot.
This corroborates EOD-22 Wave 1's earlier finding (CSpect's PC=$0000 BP
silent for 30 s, $5B00-toggle BP also silent).

The wrapper is a code path jnext's broken state forces it into; CSpect's
correctly-initialised state simply doesn't go there.

## Idle-state state on CSpect (paused at $0C90)

| Probe | jnext (at $26B9) | CSpect (idle $0C90) |
|---|---|---|
| HL | $0000 | $5C3B (= channel info ptr) |
| SP | $5BFF | $FF3F |
| AF | $1F08 | $0222 |
| BC | $253B | $0B27 |
| DE | $FFBF | $0002 |
| IX | $E01B | $F700 |
| IY | $5C3A | $5C3A *(matches)* |
| IM | n/a | 1 |
| slots NR $50..$57 | (jnext: 02 03 0A 0B 04 05 1E 1F) | **FF FF 0A 11 04 05 00 01** |
| NR $03 | n/a | $33 (+3 mode) |
| NR $07 | n/a | $33 |
| NR $8C | $00 (altrom-en flipped on later in jnext) | $00 *(altrom OFF)* |
| NR $8E | n/a | $00 |
| ($5B52) | (jnext: $0000) | **$0044** |
| ($5B54) | **$3E93** *(THE bug)* | **$004B** |
| ($5B5C) | n/a | $CF00 |
| ($5B6A) | (jnext, hypothesized $0000) | **$5BFB** |

Mem at $26B0..$26C5 on CSpect (slot 1):
```
FC C9 37 FD CB 45 4E 28 2C D5 F5 7C 07 07 07 E6 07 5F 7C E6 1F F6
```
**Different bytes** — not the wrapper. CSpect's slot 1 at idle holds bank
$11 ($51 NR), not the bank that contains the wrapper protocol. Same site
holds different content.

Mem at $3E93..$3E9A on CSpect: `ED 91 8E 01 C9 ...` = `NEXTREG $8E,$01; RET`
= the **bank-0 mirror** (switches TO bank 1). Slot 1 = $11 confirms a non-
ROM bank is mapped there; slot 0 = $FF (= ROM 0) so $3E93 reads from ROM 0
which holds the bank-0 wrapper variant.

Mem at $5B00..$5B1F on CSpect:
```
F5 C5 01 FD 7F 3A 5C 5B EE 10 F3 32 5C 5B ED 79
01 FD 1F 3A 67 5B EE 04 32 67 5B ED 79 FB C1 F1
```
This IS the toggle wrapper code (PUSH AF; PUSH BC; LD BC,$7FFD; LD A,($5B5C);
XOR $10; LD ($5B5C),A; OUT (C),A; LD BC,$1FFD; LD A,($5B67); XOR $04;
LD ($5B67),A; OUT (C),A; EI; POP BC; POP AF). RAM-resident, dormant in idle.

## Verdict

**(c) sysvars at $5B6A (and $5B54) — corroborated.**

The Wave 7 hypothesis stands: jnext's `$5B6A` and `$5B54` are uninitialised
(cleared by cascade, never re-written), while CSpect has them at real
working values ($5BFB and $004B respectively). The divergence is upstream
of $26B9 — supervisor MAIN's path that initialises sysvars + dispatches
ROM bank routines never runs the $26B0 wrapper at all on CSpect, because
CSpect's state never lands in the broken branch that requires it.

**The bug is therefore one of two upstream causes:**
1. `$5B6A` (and `$5B52`/`$5B54`/`$5B5C`) need an init pass that jnext is
   skipping — find the supervisor code that writes them on real Next.
2. Supervisor's MAIN dispatch in jnext takes a wrong branch much earlier
   (into the $26B0 wrapper code path) because of some prior state
   divergence (e.g. the `eff_mmu = 02,01` mixed-slot state at the
   $3E93 entry noted in Wave 6).

Next-session move: BP at every NextZXOS post-soft-reset checkpoint
($00EF, $0124, $016E, $018E, $01D1, $5B48) and dump $5B50..$5B6F at each
to find the EXACT supervisor instruction that writes $5B6A=$5BFB and
$5B54=$004B on CSpect — then check whether jnext skips that site.

## Slot mapping comparison (idle)

| Slot | CSpect | meaning |
|---|---|---|
| NR $50 | $FF | ROM 0 |
| NR $51 | $FF | ROM 0 |
| NR $52 | $0A | bank 5 lo |
| NR $53 | $11 | bank 8 hi |
| NR $54 | $04 | bank 2 lo |
| NR $55 | $05 | bank 2 hi |
| NR $56 | $00 | bank 0 lo |
| NR $57 | $01 | bank 0 hi |

Note: NR $51 = $11 (= page 17) — not the typical $01. ROM in slot 0/1 is
mapped via NR $50/$51 = $FF (= use legacy ROM routing), so slot 1 actually
shows page $11 content because... wait, $FF means "MMU disabled, fall
through to legacy ROM" per VHDL. But the read at $26B0 returned non-ROM
bytes. Possibly DZRP's `read_mem` reflects the CPU's effective view via
slot translation, and the $11 takes precedence — to be checked.

## Files

- Capture script: `tools/cspect_dzrp/g46b_eod22_26b9_hl_capture.py`
- Idle dump script: `tools/cspect_dzrp/g46b_eod22_26b9_idle_sysvars.py`
- Raw transcripts: `/tmp/26b9-capture.txt`, `/tmp/26b9-idle.txt` (host-local).
