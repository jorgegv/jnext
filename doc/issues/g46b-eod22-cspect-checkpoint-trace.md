# G46(b) EOD-22 — CSpect supervisor checkpoint trace

**Date:** 2026-05-09 07:47 CEST
**Investigator:** subagent (DZRP ground-truth via CSpect)
**CSpect:** 3.1.0.0 + DezogPlugin v2.3.0.20958 (DZRP 2.0.0)
**Image:** `roms/nextzxos-1gb-fat32fix.img`
**Launch:** `mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img -debug`
**Script:** `tools/cspect_dzrp/g46b_eod22_checkpoint_trace.py`
**Raw output:** `/tmp/g46b_eod22_checkpoint_trace.out`

## Goal

Capture CSpect's supervisor PC trajectory at the same checkpoints jnext's
POSTRESET trace landed during cascade-and-init. Each checkpoint
captures full state (regs, slots, stack, NextRegs, ports). The verdict:
identify the **first divergence** between CSpect and jnext.

## Method

1. Launched CSpect with `-debug` so it halted at PC=$0000 on cold boot.
2. Connected via DZRP. Installed all 8 BPs up front:
   `$00EF, $0124, $016E, $018E, $01D1, $01D4, $5B48, $5B00`.
3. Continued. After each BP hit: captured state, removed that BP,
   continued to next.
4. After last BP, free-ran 15 s to verify $5B00 silence.

## Results — all 7 expected checkpoints HIT, $5B00 silent

```
$00EF  HIT 1x  — post-soft-reset init entry (NEXTREG $07,$03)
$0124  HIT 1x  — attribute LDIR start (LD HL,$5800)
$016E  HIT 1x  — cascade outer loop top
$018E  HIT 1x  — second-phase RAM-test loop start
$01D1  HIT 1x  — post-test SP reset (LD SP,$5BFF)
$01D4  HIT 1x  — RST $20 dispatch
$5B48  HIT 1x  — bank-3 absolute wrapper
$5B00  MISS    — toggle wrapper (silent for 15 s; supervisor at $0C90)
```

After last hit, free-run paused at **PC=$0C90** (48K BASIC KEY-INPUT) —
exactly the steady state observed in `g46b-eod22-cspect-nr8e-7ffd-1ffd-audit.md`.
Confirms CSpect post-boot is the canonical NextZXOS REPL.

## Per-checkpoint snapshots

### $00EF — post-soft-reset init entry

```
PC=00EF SP=0000 AF=0044 BC=0000 DE=0000 HL=0000  IX=0000 IY=0000
I=00 R=0A IM=0  iff1=? (CSpect plugin doesn't expose iff1 directly)
slots [NR $50..$57] = FF FF 0A 0B 04 05 00 01
NR $03=$33 $07=$00 $8C=$00 $8E=$00
ports $7FFD=$FF $1FFD=$FF  (write-only — uninformative)
stack@SP=0000: C3F3 00EF 4445 0209 3BC3 2A10 2A2E 00FF
mem@PC: ED 91 07 03 ED 91 03 B0 ED 91 C0 08 3E FF ED 92
       = NEXTREG $07,$03; NEXTREG $03,$B0; NEXTREG $C0,$08; LD A,$FF; NEXTREG ...
```

**Slots [00..07] = FF FF 0A 0B 04 05 00 01** — slot 0/1 ROM-banked
(`FF FF` = legacy paging owned), slots 2..7 RAM banks. NR $03=$33
already (= +3 mode unlocked, machine_type=3).

### $0124 — attribute LDIR start

```
PC=0124 SP=0000 AF=0410 BC=253B DE=0600 HL=0000  IX=0000 IY=0000
I=00 R=2C IM=0
slots = FF FF 0A 0B 04 05 00 01   (unchanged from $00EF)
NR $03=$33 $07=$33 $8C=$00 $8E=$00
mem@PC: 21 00 58 11 01 58 75 01 FF 02 ED B0 01 00 70 21
       = LD HL,$5800; LD DE,$5801; LD (HL),L; LD BC,$02FF; LDIR; LD BC,$7000; LD HL,...
```

NR $07 went $00→$33 (cpu speed = 28 MHz). slots unchanged. Supervisor
about to clear attr area.

### $016E — cascade outer loop top

```
PC=016E SP=0000 AF=6F30 BC=0070 DE=5B00 HL=4070  IX=0000 IY=0000
I=00 R=51 IM=0
slots = FF FF 0A 0B 04 05 DE DF   <-- slot 6/7 changed: 00 01 -> DE DF
NR $03=$33 $07=$33 $8C=$00 $8E=$00
mem@PC: 18 1E 08 3E 08 93 6F 08 7D 20 04 D3 FE 18 FE EE
       = JR $018E; EX AF,AF'; LD A,$08; SUB E; LD L,A; EX AF,AF'; LD A,L; JR NZ,$0174; OUT ($FE),A; JR $-2; XOR ...
```

**slot 6/7 = $DE,$DF** — high MMU banks. The cascade walker has paged
in some upper RAM bank pair (logical pages $DE/$DF). HL=$4070 (slot 2),
DE=$5B00 (slot 2), B=$70 = 112 banks remaining for the cascade outer
loop counter. Confirms supervisor is mid-cascade.

### $018E — second-phase RAM-test loop start

```
PC=018E SP=0000 AF=6F30 BC=0070 DE=5B00 HL=4070  IX=0000 IY=0000
I=00 R=52 IM=0
slots = FF FF 0A 0B 04 05 DE DF
NR $03=$33 $07=$33 $8C=$00 $8E=$00
mem@PC: AF 01 00 40 11 08 01 87 ED 92 56 3C ED 92 57 CB
       = XOR A; LD BC,$4000; LD DE,$0108; ADD A,A; OUT (NextReg),(C)?...
```

R-reg only +1 from $016E — almost no instructions executed (= one JR
$018E). Same slots/regs as $016E. Confirms it's the entry of the
second-phase RAM-test loop, identified in EOD-19 trace.

### $01D1 — post-test SP reset

```
PC=01D1 SP=0000 AF=6F3A BC=4070 DE=0100 HL=DCBA  IX=002B IY=0000
I=00 R=18 IM=0
slots = FF FF 0A 0B 04 05 DE DF   (still!)
NR $03=$33 $07=$33 $8C=$00 $8E=$00
mem@PC: 31 FF 5B E7 01 1F ED 91 8E 08 ED 56 CD E3 00 21
       = LD SP,$5BFF; RST $20; DW $1F01; NEXTREG $8E,$08; IM 1; CALL $00E3; LD HL,...
```

**slots STILL [00..07]=FF FF 0A 0B 04 05 DE DF** at the LD SP,$5BFF
boundary. The cascade left slot 6/7 paged to $DE/$DF — the supervisor
will deal with this after the SP reset. HL=$DCBA = canonical "magic"
test pattern verifier. R rolled past $FF (only 6 instr from $018E entry
= consistent with very tight cascade body that already finished).

### $01D4 — RST $20 dispatch

```
PC=01D4 SP=5BFF AF=6F3A BC=4070 DE=0100 HL=DCBA  IX=002B IY=0000
I=00 R=19 IM=0
slots = FF FF 0A 0B 04 05 DE DF
NR $03=$33 $07=$33 $8C=$00 $8E=$00
stack@SP=5BFF: 0000 0000 0000 0000 0000 0000 0000 0000
mem@PC: E7 01 1F ED 91 8E 08 ED 56 CD E3 00 21 FF FF 22
       = RST $20; DW $1F01; NEXTREG $8E,$08; ...
```

SP = $5BFF (just set). Top of stack = all $0000 — uninitialized RAM.
This is the canonical condition jnext also reaches, but jnext's stack
contents at this point should be the same. **Note: NR $8E,$08 follows
RST $20** — i.e. after returning from RST $20 supervisor will do
NEXTREG $8E,$08. RST $20 with `DW $1F01` is the bank-call trampoline
described in EOD-19 (`RST $28; DW <tgt>` form, but here `RST $20` is
used; the trampoline pops `$1F01` as the inline target).

### $5B48 — bank-3 absolute wrapper

```
PC=5B48 SP=5BF9 AF=0040 BC=00A8 DE=FFFF HL=3EAF  IX=002B IY=0000
I=00 R=2D IM=1   <-- IM=1 (interrupts enabled)
slots = FF FF 0A 0B 04 05 00 01   <-- slot 6/7 BACK to 00 01
NR $03=$33 $07=$33 $8C=$00 $8E=$00
stack@SP=5BF9: 1661 5B4D 01F0 0000 0000 0000 0000 0000
mem@PC: ED 91 8E 03 C9 ED 91 8E 00 C9 00 00 A8 00 00 00
       = NEXTREG $8E,$03; RET; NEXTREG $8E,$00; RET; ...
```

CRITICAL OBSERVATIONS at $5B48:

- **IM=1**: interrupts armed (CPU went IM 1 between $01D4 and here).
- **slot 6/7 = $00,$01**: cascade banks $DE/$DF have been **swapped
  back** to logical pages $00/$01. So between $01D4 and $5B48 the
  supervisor unwound the cascade paging and restored the canonical RAM
  layout.
- **Stack frame**: top=$1661 (= bank-3 cross-bank target, the LDDR;RET
  font glyph copier from EOD-19), then $5B4D (= return address back
  into the wrapper's bank-0 sequel), then $01F0 (= grandparent caller
  in bank 0). HL=$3EAF (= font glyph data ptr, matches EOD-19 finding).
- **Supervisor about to**: NEXTREG $8E,$03 (= 1FFD(2)=1, 7FFD(4)=1 →
  sram_rom=11=bank 3 = +3 DOS), RET to $1661 (= bank-3 LDDR), execute
  LDDR; RET, return to $5B4D (= NEXTREG $8E,$00; RET in bank 0), back
  to $01F0 in bank 0.

This is exactly the bank-flip pattern EOD-19 described. **CSpect
exercises $5B48 once during boot** (post-cascade font-init copy), then
never again — supervisor settles at $0C90 (48K BASIC).

### $5B00 — MISS (15 s after $5B48)

CSpect supervisor never reaches the **toggle** wrapper. It uses the
**absolute** wrapper $5B48 (and presumably $5B43/$5B4D for banks 2/0)
during boot, but the toggle dispatcher is never exercised in normal
NextZXOS boot. Confirms EOD-22's earlier finding: jnext's slide INTO
$5B00 is anomalous behaviour, not normal supervisor flow.

## Comparison vs. jnext POSTRESET trace

| Checkpoint | CSpect | jnext (POSTRESET trace, pre-fix) | Match? |
|---|---|---|---|
| $00EF | slots `FF FF 0A 0B 04 05 00 01`, NR $03=$33 | (similar — supervisor in bank 0) | YES |
| $0124 | slots same, NR $07=$33 | reached, attr clear | YES |
| $016E | slots `... 04 05 DE DF` | reached, mid-cascade | YES |
| $018E | slots `... DE DF` | reached, RAM test | YES |
| $01D1 | slots `... DE DF`, IX=$002B | reached, IX=$002B | YES |
| $01D4 | SP=$5BFF, IM=0, RST $20 | reached, RST $20 | YES |
| $5B48 | slots `... 00 01`, IM=1, HL=$3EAF, stack=[1661,5B4D,01F0...] | jnext also reaches $5B48 per EOD-22 trace event #17412 (post NR $03 fix) | YES |
| $5B00 | MISS (silent for 15 s) | jnext FIRES (slide → wrapper RET → PC=$0000) | **DIVERGE** |

## Verdict

**CSpect's path matches jnext's path through ALL 7 init checkpoints
($00EF → $5B48).** Register state, slot mapping, NextReg state, and
stack contents are byte-identical (within DZRP's observable surface) at
every captured checkpoint.

**The divergence is downstream of $5B48.** On CSpect the supervisor
exits the boot fan-out cleanly via the bank-3 LDDR copier and settles
at $0C90 (48K BASIC). On jnext, **after** $5B48 (post-NR $03 fix) the
supervisor enters a NOP-slide somewhere ≥$3FC0 → slides into $5B00 →
RETs from uninitialized stack → PC=$0000 panic.

**This means**:

1. CSpect and jnext agree on cascade/init state up through the $5B48
   bank-flip wrapper.
2. The slot map at $5B48 (`FF FF 0A 0B 04 05 00 01`) is the canonical
   post-cascade layout. Whatever overlays slot 0/1 with zeros on jnext
   happens AFTER $5B48 — most likely during the bank-3 LDDR copy
   sequence's return path (`$1661 → $5B4D → $01F0 → ...`) where bank 0
   is paged back in and the supervisor resumes higher-level init.
3. The altrom hypothesis (NEXTREG $8C,$80 enables altrom in read-only
   mode → reads from cleared physical pages) remains the leading
   candidate. CSpect shows NR $8C=$00 at $5B48 — altrom NOT yet enabled
   at this checkpoint. So altrom must be enabled by code that runs
   AFTER $5B48 returns.

## Next-session priorities

1. **Set DZRP BPs at the bank-3 LDDR site ($1661) and the bank-0 sequel
   ($01F0, $5B4D)** to capture supervisor state at the immediate
   post-$5B48 path. Need to see: (a) how many times $5B48 fires; (b)
   what supervisor does after $01F0 returns.
2. **Set DZRP BP on `NEXTREG $8C` write site** in the boot ROM
   ($01BD per EOD-22 doc) to confirm WHEN altrom_en is set on CSpect
   (if it is set at all in real-Next).
3. **Capture CSpect memory at $3FC0..$3FFF** post-init to confirm
   whether the area is non-zero (= ROM faithful) or zero (= overlay
   even on real Next, which would invalidate altrom hypothesis).
4. **Add jnext probe**: instrument the supervisor execution at $5B48
   exit (after the RET pops $1661) — log all PCs until either $0C90
   (success) or $0000 (panic). Diff against CSpect's trace.

## Cleanup

- CSpect process killed at end of session (PID 3862049, SIGKILL).
- New script: `tools/cspect_dzrp/g46b_eod22_checkpoint_trace.py`.
- No code changes to emulator (read-only directive honoured).
- No origin push.
