# G46(b) EOD-23 — slide entry traced to RAM bank 0 (page $20) empty

## Summary

Wave 8 fix landed (commit `8242098`) — supervisor now reaches bank-3 NR
$02 reset trampoline at `$3BE8`, bank 5 RAM has attribute paint, real
register state. **New blocker isolated**: NOP slide through slot 6/7
RAM ($BFxx-$FFFF range) traced to bank 2 wrapper RET at `$3E17`
returning to `$C3F3`, which lives in slot 6 → physical page $20 (RAM
bank 0 in Next-mode +$20 shift) — and that page is **empty in jnext**.

This EOD-23 isolates the slide entry, identifies the upstream wrapper
trampoline (`$3E00..$3E17` in bank 2), and rules out the obvious
hypotheses: page $00 (bank 0 ROM) **is correctly loaded** with
enNextZX bank 0 contents; only physical page $20 (= where NR $56,$00
lands per VHDL `:2964` mmu_A21_A13 +$20 shift, with `rom_in_sram=1`)
is empty.

## Probes added (TEMP, env-gated)

- `JNEXT_G46B_SLIDETRAP=N` (z80_cpu.cpp:816+) — log first N transitions
  from PC<$C000 to PC>=$C000 where mem[PC..PC+2] are all $00 (slide
  signature). Captures: prev_pc, slot map, rom_bank, regs, stack[0..7],
  64-deep distinct-PC ring, 32 bytes at PC, nr_mmu_ register values,
  altrom state, machine_type, rom_in_sram, and dumps physical pages
  $00/$20/$21 at the equivalent offsets to compare ROM-area-vs-RAM-area
  contents.

- (Existing) `JNEXT_G46B_PC0_TRAP=N` — log transitions to PC=$0000.

## Captured data

### PC0_TRAP run (`JNEXT_G46B_PC0_TRAP=8`)

```
G46B PC0_TRAP #2 prev_pc=0x6d2f sp=0xffff stk=0xf300 0xefc3 0x4500 ...
                 iff1=0 im=0 halted=0 eff_mmu[0..7]=00 01 0a 0b 04 05 00 01
                 rom_bank=0x00
G46B PC0_TRAP  ring=6cff 6d02 6d06 6d09 6d0c 6d0e 6d11 6d14 6d16 6d19
               6d1b 6d1e 6d1f 6d2a 6d2c 6d2f
```
= legitimate post-soft-reset cold-boot landing (= "BASIC handoff" reset
at bank 0 $6d31 per memory).

```
G46B PC0_TRAP #3 prev_pc=0xffff sp=0x5bed
                 stk=0x3e93 0x0040 0x5c3a 0x0144 0x006c 0x3e13 0x0a8c 0x0339
                 iff1=1 im=1 halted=0 eff_mmu[0..7]=04 05 0a 0b 04 05 0e 0f
                 rom_bank=0x02
G46B PC0_TRAP  ring=fff0 fff1 fff2 fff3 fff4 fff5 fff6 fff7
               fff8 fff9 fffa fffb fffc fffd fffe ffff
```
= classic NOP slide tail wrapping $FFFF → $0000 → $0001 (`JR $0000`)
infinite-loop trap. Stack contains valid wrapper trampoline call chain
($3E93, $3E13, $0040, etc. — Wave-7-decoded values).

PC0_TRAP #4..#9 confirm the $0000↔$0001 trap loops.

### SLIDETRAP run — Event A (after soft reset, ~500ms post-handoff)

```
G46B SLIDETRAP entry pc=0xc3f3 prev_pc=0x3e17 sp=0xff55
                     stk=0x3e13 0x0002 0x423c 0x7e42 0x4242 0x0000 0x427c 0x427c
                     eff_mmu[0..7]=04 05 0a 0b 04 05 00 01 rom_bank=0x02
G46B SLIDETRAP regs A=0xff F=0x8a BC=0x0000 DE=0x5760 HL=0xff55
                IX=0x2e8e IY=0x5c3a iff1=1 im=1
G46B SLIDETRAP ring=09f2..09f7 (loop) 09f9 09c3
               27bb 27bd 27d9 27dc 27e1..27f8 27c0 279d..27ab
               3e00 3e04 3e05..3e0f 3e13 3e17
G46B SLIDETRAP bytes pc..pc+31 = 00*32 (= NOP slide region)
G46B SLIDETRAP nr_mmu[0..7]=ff ff 0a 0b 04 05 00 01
                nr_8c_altrom_en=0 nr_8c_altrom_rw=0 machine_type=3 rom_in_sram=1
G46B SLIDETRAP page$20 @$03F0..$040F = 00*32                  ← RAM bank 0 EMPTY
G46B SLIDETRAP page$21 @$1F50..$1F6F = 5b 00 03 f3 c3 13 3e 02 ...   ← stack content
G46B SLIDETRAP page$00 @$03F0..$040F = dd 7e 01 fe 5b 20 11 ...      ← ROM correct
```

### SLIDETRAP run — Event B (~500ms later, different rom_bank)

```
G46B SLIDETRAP entry pc=0xfccb prev_pc=0x5b4c sp=0x1f8e
                     stk=0xf4cb 0xcdf5 0x1fa8 0x28f1 0xed0c 0x8e91 0x2a0b 0x5b6a
                     eff_mmu[0..7]=06 07 0a 0b 04 05 00 01 rom_bank=0x03
G46B SLIDETRAP ring=0ecc..0ed4 (loop) 0038..0063 (RST/IM1) 37c8..37e2 3ec1..3ec6
               5b48 5b4c                    ← bank-flip wrapper $5B48
G46B SLIDETRAP nr_mmu[0..7]=ff ff 0a 0b 04 05 00 01
G46B SLIDETRAP page$00 @$03F0..$040F = dd 7e 01 fe 5b 20 11 ...      ← ROM correct
G46B SLIDETRAP page$20 @$03F0..$040F = 00*32                          ← RAM bank 0 EMPTY
```

## Decode

### The wrapper trampoline (bank 2 $3E00..$3E17)

Per `enNextZX-bank2.asm`:
```
$3E00: LD ($5b54),BC          ; save BC
$3E04: EX (SP),HL              ; HL = inline-target-addr (post-CALL)
$3E05: LD C,(HL); INC HL       ; read inline target lo
$3E07: LD B,(HL); INC HL       ; read inline target hi
$3E09: EX (SP),HL              ; restore stack with HL+2 = real return
$3E0A: PUSH $3E13              ; push trampoline-end addr
$3E0E: PUSH BC                 ; push target
$3E0F: LD BC,($5b54)           ; restore BC
$3E13: NEXTREG $8E,$00         ; flip slot 0/1 to bank 0 ROM
$3E17: RET                     ; pops target → jumps
```

Pattern: caller does `CALL $3E00; DW <target>`. Wrapper extracts
target, switches slot 0/1 to bank 0 ROM, RETs to target. Target ends
with RET → pops $3E13 → re-runs NEXTREG $8E,$00 (idempotent) → pops
real-return.

### The slide path

PC sequence (from ring buffer, oldest → newest):
1. Bank-2 supervisor at $27BB→$27F8→$27C0→$279D→...→$27AB
2. (CALL $3E00 from somewhere — hidden by ring dedup)
3. Wrapper $3E00..$3E17 executes
4. RET at $3E17 pops `$C3F3` from stack → PC = $C3F3
5. $C3F3 in slot 6 (= NR $56,$00 = logical page 0)
6. With `rom_in_sram=1` and Next-mode `+$20` shift (mmu.h:921,
   to_sram_page()), logical 0 → physical SRAM page **$20**
7. Page $20 is **empty** (all $00) → NOP slide $C3F3→$FFFF
8. $FFFF wraps to $0000 → bank 2 ROM at $0000-$0007 = `00 18 fd ...`
   (NOP, JR $0000) → infinite-loop trap

### Why page $20 empty?

The +$20 shift is VHDL-faithful (zxnext.vhd:2964 mmu_A21_A13 formula:
`((1 + (page >> 5)) & 0x0F) << 5 | (page & 0x1F)` → for logical 0 with
rom_in_sram=1, physical = $20). This means **NR $56,$00 maps to RAM
bank 0, NOT bank 0 ROM**.

For supervisor's `CALL $3E00; DW $C3F3` to land in valid code, RAM bank
0 (physical page $20) must have been pre-populated with code containing
a valid entry at offset $03F3. **In jnext, RAM bank 0 is empty.**

Page $00 (bank 0 ROM-area) IS correctly loaded with enNextZX bank 0
contents (verified — bytes match the SD-extracted enNextZX.rom file
exactly). Only RAM bank 0 (= page $20) is empty.

### What should be in RAM bank 0?

Unknown without DZRP comparison. Hypotheses:
- Supervisor (or tbblue.fw) loads enNextZX bank 0 into RAM bank 0
  somewhere during boot, jnext's path doesn't fire that load. Page
  $00 (ROM-area) IS loaded — maybe page $20 should mirror it?
- A NextZXOS subsystem module is loaded into RAM bank 0 (e.g., a
  dispatcher/jump-table/RAM-resident module).
- The supervisor uses RAM bank 0 as scratch/working area, and the
  CALL→$C3F3 pattern only lands in valid code AFTER some specific
  initialization (which jnext skips or does differently).

## Re-framing — actually a stack corruption (added EOD-23 second pass)

Further static analysis flips this from a "RAM bank 0 missing data"
issue to a **stack corruption** issue:

1. The bytes "$F3 $C3" appear in NO enNextZX bank as inline data
   (`f3 c3` never appears as 2-byte inline DW). They appear ONLY at
   bank 0 offset $0000-$0001 = the boot vector `DI; JP $00EF`.

2. Searching all 9 `CALL $3E00` sites in bank 2 (only bank with such
   calls): NONE have inline DW $C3F3. Inline targets are $2c52,
   $0576, $053e, $03e5, $27f9, $2751, $0010 (×2), $07d7.

3. Bank 0's wrapper at $3E13 has `NEXTREG $8E,$02` (vs bank 2's
   `NEXTREG $8E,$00`). NR8E_TRACE confirms the LAST firing before
   slide was `v=$02 pre[slots=00,01]` → **bank 0's wrapper executed**
   with slot 1 = bank 0 hi (page 1).

4. Stack reconstruction at SLIDETRAP: pre-wrapper SP = $FF57. After
   `$3E04 EX (SP),HL` and `$3E09 EX (SP),HL` with two HL increments,
   `MEM[$FF57] = old_TOS + 2 = $0002`. So **pre-wrapper TOS was $0000**.

5. Wrapper's `EX (SP),HL` swapped HL ↔ $0000. Then `LD C,(HL)` read
   `MEM[$0000]` from slot 0 = bank 0 (page 0). Bank 0 at $0000 = $F3
   (= DI byte). `INC HL; LD B,(HL)` read bank 0 at $0001 = $C3 (=
   high byte of `JP $00EF`). So BC = $C3F3 came directly from the
   bank 0 boot vector.

6. RET at $3E17 popped $C3F3 → PC=$C3F3 → slide through empty page
   $20.

### Real bug: caller's stack[0] = $0000

The bank 0 wrapper protocol requires `CALL $3E00; DW <target>` —
caller pushes return-address (= addr of inline DW). At wrapper's
`EX (SP),HL`, HL becomes the inline-DW addr.

Per bank 2 $0D3C-$0D44, the supervisor's INIT routine pushes
[$0676, $3E00] manually onto stack and JPs to $0207. After $0207
RETs, $0676 RETs, then wrapper runs. Wrapper sees TOS = whatever was
on stack BEFORE the $0D3C pushes.

In CSpect that pre-stack TOS is a valid inline-DW addr. In jnext
it's $0000 → wrapper reads bank 0 boot vector → BC=$C3F3 → slide.

**The actual bug is upstream**: something on the supervisor's call
chain failed to leave a valid stack frame in jnext. Possible causes:
- An earlier RET popped $0000 from cleared memory
- The cascade clear at boot wiped a stack region
- A stack-pointer corruption before $0D3C
- Soft reset preserved a corrupt stack

## Hypotheses for next-session investigation

### H1 — RAM bank 0 was supposed to be a copy of bank 0 ROM

Test: capture CSpect's RAM bank 0 (logical page 0 / physical $20)
contents at the equivalent supervisor state via DZRP. If it matches
bank 0 ROM contents (`dd 7e 01 fe 5b 20 11 ...`), then jnext is missing
a load step that copies bank 0 ROM into RAM bank 0.

### H2 — RAM bank 0 contains a different module / dispatcher

Test: capture CSpect's RAM bank 0 — disassemble the byte at offset
$03F3 onwards. The function/jump-table there reveals what NextZXOS
loads at boot.

### H3 — Logical-to-physical shift differs in real Next at this point

Test: BP CSpect at PC=$C3F3 (during the equivalent boot phase) and
read NR $56 + observe slot 6 mapping. If CSpect's slot 6 maps to
physical page $00 (= bank 0 ROM) when NR $56,$00 with rom_in_sram=1,
then the +$20 shift behavior differs.

### H4 — Different code path on jnext that bypasses RAM-bank-0 load

Trace pre-soft-reset supervisor flow looking for SD reads / LDIRs
that target physical page $20. If absent in jnext, supervisor's load
sequence diverged before this point.

## Why this is downstream of Wave 8

Pre-Wave-8: NR $51,$FF hardcoded slot 1 to page 1 → cascade fail at
bank 1 wrapper $26B0 → PC=$0000 panic via different path.

Post-Wave-8: that path fixed; supervisor reaches NEXT init phase, sets
up bank-2 supervisor MAIN, eventually hits the RAM bank 0 dispatch
that depends on page $20 being populated. **This is a more advanced
boot stage than pre-Wave-8.**

## Next-session priorities (updated post-second-pass)

1. **DZRP probe CSpect at the equivalent point** — BP somewhere on
   the supervisor INIT path at bank 2 $0D2x area, or at bank 0 wrapper
   $3E00 entry. Dump SP, MEM[SP..SP+15] (= the stack frame), regs.
   In CSpect, pre-wrapper TOS should be a valid inline-DW addr, not
   $0000. The diff vs jnext's $0000-on-stack is the upstream bug.

2. **Trace SP/stack history pre-wrapper** — add a probe that logs
   SP value and stack[0..3] at every PC change in the bank 2 $0D2x
   range. Identify when $0000 first appears at the top-of-original-
   stack location. Walk back to find what put it there.

3. **Audit cascade clear / soft-reset stack-preservation** — the
   supervisor's $0168 cascade (per memory EOD-18) clears 31 RAM
   banks. If it touched the stack region, that's the smoking gun.

2. **Decode bank 2 supervisor MAIN at $27xx area** — the ring shows
   $27BB → $27AB before wrapper entry. Disassemble these to find the
   call site and understand the dispatch context.

3. **Trace BC source for $5B4C → $FCCB jump** — Event B's slide entry
   from bank-flip wrapper $5B48..$5B52 area. The target $FCCB suggests
   slot 7 = page $21 (= same RAM bank 0). Same root cause likely.

4. **Search supervisor code for SD-load patterns targeting page $20**
   — patterns like `NEXTREG $56,$00; LDI...` or copy loops should
   appear in bank 0/1/2/3 disasm if RAM bank 0 is populated by
   supervisor.

5. **Audit other NR $5x with $FF cases (slots 2-5)** — Wave 8 fix
   handled slots 0/1 and 6/7 for NR $5x,$FF. Review slots 2-5 NR
   handler — search bank disasms for NEXTREG $52, $53, $54, $55 with
   $FF and verify semantics.

## Probe cleanup (closure)

When G46(b) closes, remove all `JNEXT_G46B_*` probes:

```
grep -rn JNEXT_G46B_ src/
```

Currently active probes (from EOD-22 + EOD-23):
- `JNEXT_G46B_POST_RESET` (mmu.cpp:17, z80_cpu.cpp:506)
- `JNEXT_G46B_RING_AT` (z80_cpu.cpp:539)
- `JNEXT_G46B_SLOTCTX` (z80_cpu.cpp:665)
- `JNEXT_G46B_PC0_TRAP` (z80_cpu.cpp:753)
- `JNEXT_G46B_NR8E_TRACE` (mmu.cpp:461)
- `JNEXT_G46B_PORTSPY` (z80_cpu.cpp:159)
- `JNEXT_G46B_NR43_TRACE` (emulator.cpp:749)
- `JNEXT_G46B_DUMP_AT_FRAME` / `_PREFIX` (emulator.cpp:4162-4164)
- `JNEXT_G46B_ULA_WRITES` (mmu.cpp:11)
- `JNEXT_G46B_SLIDETRAP` (z80_cpu.cpp:816+) ← NEW THIS EOD

## End of EOD-23 — slide entry isolated, RAM bank 0 empty proven

## EOD-23 third pass — bank 0 $27A3 stack-swap routine identified

Further investigation found the actual mechanism: bank 0 has a
stack-swap routine at $27A0-$27AB:

```
$27A0: CP $5C            ; check A
$27A2: RET C             ; return early if A < $5C
$27A3: LD HL,($5B6A)     ; HL = saved alternate SP
$27A6: LD ($5B6A),SP     ; save current SP
$27AA: LD SP,HL          ; SP = HL (= switch to alternate stack)
$27AB: RET               ; pop new TOS into PC
```

This is bank-stack-swapping via `($5B6A)` sysvar — same mechanism as
the bank 1 $32CC routine identified in EOD-22 Wave 4 (per memory:
"$32CC at bank 1 does `LD HL,($5B6A); LD ($5B6A),SP; LD SP,HL`").

### Slide-causing wrapper invocation path (from RING_AT=$3E00 + ring data)

Pre-wrapper, PC went through bank 0 $27A3-$27AB stack swap:
- `LD HL,($5B6A)` → HL = saved alternate SP value
- `LD SP,HL` → SP = HL
- `RET` → pops new TOS = $3E00 → PC = wrapper

After RET to $3E00, wrapper sees pre-wrapper SP = (old $5B6A
contents) + 2. **TOS at that position was $0000** in jnext (per
SLIDETRAP capture: page$21 at $1F57 = $02 post-wrapper, meaning
pre-wrapper $0000).

### Key sysvars dump at SLIDETRAP

```
($5B6A) = $5BEB   ← saved SP from this current swap
($5B68) = $6F00
($5B58) = $FF55   ← supervisor's primary stack SP save
($5B54) = $0000   ← BC save (cleared by wrapper's LD ($5B54),BC + restore)
($5B6C) = $0000
```

Note: ($5B6A) is NON-zero ($5BEB), so the original Wave-4 hypothesis
"($5B6A) = $0000 → SP = $0000" is REFUTED for this exact invocation.

### What's actually happening

The supervisor uses bank-stack-swap (`($5B6A)` swap routine) to
transition between bank-context-A's stack and bank-context-B's
stack. Each invocation:
1. Saves current SP to ($5B6A)
2. Loads alternate SP from ($5B6A)
3. RETs on the new stack

If the alternate stack has $3E00 on top followed by a valid inline-DW
pointer, the wrapper-via-RET pattern works. If the alternate stack's
TOS-1 (= what wrapper reads as inline-DW-pointer) is $0000, wrapper
reads bank 0 boot vector → slide.

In the slide-causing invocation, the alternate stack's content was:
- TOS = $3E00 (= wrapper entry, will be popped first)
- TOS+2 = $0000 (= pre-wrapper-from-wrapper's-perspective)

**The bug is somewhere in the supervisor's bank-stack-swap state
machine**: a stack should have been set up with [$3E00, <valid-DW-
ptr>, ...] but instead only [$3E00, $0000, ...] is present.

### Comparison points needed via DZRP

1. CSpect's value at MEM[$FF57,$FF58] (or wherever the equivalent
   pre-wrapper TOS lives) at the equivalent boot phase. Should be
   non-$0000.
2. CSpect's ($5B6A) and stack history during these swaps.
3. CSpect's bank 0 $27A3 invocation count vs jnext's — does CSpect
   reach this routine the same number of times?

### Why this is downstream of Wave 8

Wave 8 fixed NR $51,$FF legacy paging. Post-Wave-8, supervisor
progresses through bank-stack-swaps further, eventually hitting this
new corruption point.

Wave 4 (EOD-22) hypothesized the same `$5B6A` mechanism but at a
different code site (bank 1 $32CC). This EOD-23 confirms the SAME
mechanism is exercised at bank 0 $27A3 and is symptomatic of the
underlying upstream stack-state divergence.


## EOD-23 NR $5x,$FF audit (slots 2-5)

Per Wave 8 closure priority "Audit other NR $5x with $FF cases (slots
2-5)". Static scan of all 4 enNextZX banks for `NEXTREG $5x, $FF`
(bytes `ED 91 5x FF`):

```
bank 0 $0bf6: NEXTREG $51, $FF
bank 1 $0ae9, $0c60, $0ec2, $0ef0, $0f23, $0ff7, $1041, $126a, $15b0:
              NEXTREG $51, $FF
bank 3 $090c: NEXTREG $51, $FF
```

**Result: ALL 11 NR $5x,$FF sites are for slot 1 (NR $51,$FF) — already
covered by Wave 8 fix.** No NR $52, $53, $54, $55 with $FF.

So the fallback `map_rom(i, 0)` for slots 2-5 in [emulator.cpp:1396]
is not exercised by the supervisor. Audit complete; no further fix
needed for slots 2-5.


## NR $5x usage patterns in supervisor

Full inventory of NEXTREG $5x writes (any value) in supervisor:

| Slot | NR | Sites | Common values |
|------|-----|-------|---------------|
| 1 | $51 | 13 | $10 (= bank 8 lo, transient remap) and $FF (restore legacy) |
| 7 | $57 | 16 | $10 (= bank 8 lo) and $0F (= bank 7 hi, dual-port) |
| 3 | $53 | 1 | $00 (bank 2 $0971) |
| 4 | $54 | 1 | $01 (bank 2 $0975) |
| 6 | $56 | 2 | $0E (= bank 7 lo, dual-port) |

The supervisor's slot manipulation patterns:
- Slot 1: transient bank 8 swap for LDIR copies
- Slot 7: bank 7/8 swap for screen RAM dual access (with logical pages
  $0F/$0E that bypass the +$20 shift per Mmu::to_sram_page())
- Slot 6: bank 7 lo for dual-port access
- Slots 3,4: one-time setup writes (1 each)


## EOD-23 SWAPTRAP probe — captured the exact slide-causing swap

Added `JNEXT_G46B_SWAPTRAP=N` probe at bank 0 $27A3 entry (= start of
the stack-swap routine). Captured 20 swap events showing the
supervisor's bank-stack-swap pattern.

### Per-cycle pattern (5 swaps, repeating every ~3s)

```
#1: pc=$27a3 sp=$5BFF ($5B6A)=$FF4F alt_stk=[$23AA, $0274, $006F, $3E00]
#2: pc=$27a3 sp=$5BFF ($5B6A)=$FF51 alt_stk=[$0274, $006F, $3E00, $0000]
#3: pc=$27a3 sp=$5BFF ($5B6A)=$FF4B alt_stk=[$3E93, $150B, $3E93, $0277]
#4: pc=$27a3 sp=$5BEB ($5B6A)=$FF55 alt_stk=[$3E00, $0000, $423C, $7E42]   ← SLIDE
#5: pc=$27a3 sp=$ff3b ($5B6A)=$5BEB alt_stk=[$01D6, $3E93, $0040, $5C3A]   ← swap-back
```

Cycle then repeats: SWAP #6-#10 same pattern, etc.

### SWAP #4 decoded — the slide trigger

Pre-swap: SP=$5BEB, ($5B6A)=$FF55.

After swap routine ($27A3-$27AB):
- HL = ($5B6A) = $FF55
- ($5B6A) = SP = $5BEB (saved)
- SP = HL = $FF55
- RET → pops MEM[$FF55,$FF56] = $3E00 (alt_stk[0]) → PC = $3E00

Wrapper at $3E00:
- $3E04 EX (SP),HL: HL ↔ MEM[$FF57,$FF58] = **$0000** (= alt_stk[1])
- $3E05-$3E08: read bytes at $0000,$0001 (slot 0 = bank 0) → $F3, $C3
- BC = $C3F3
- $3E13 NEXTREG $8E,$02: flip slot 0/1 to bank 2
- $3E17 RET: pops $C3F3 → slide

### The actual missing piece

Pre-SWAP-#4, alt stack at $FF55 had:
```
$FF55: $3E00     ← wrapper entry (legitimate, pushed by supervisor)
$FF57: $0000     ← BUG: should be a valid inline-DW pointer (= return address)
$FF59+: font glyph data ('A' = 3C 42 42 7E 42 42 00, 'B', 'C', ...)
```

The supervisor pushed $3E00 at $FF55, but $FF57-$FF58 was never
populated with the expected return address.

### Comparison with SWAP #1, #2, #3 (valid)

These had ($5B6A) = $FF4F, $FF51, $FF4B (4-6 bytes apart). Their
alt_stk content showed real return-addr-like values ($23AA, $0274,
$3E93, $150B). Even SWAP #2 had alt_stk = [$0274, $006F, $3E00, $0000]
— note **the $3E00 at index [2]**, not [0]. So SWAP #2 isn't going
to trigger the slide because its TOS is $0274 (some return addr),
not $3E00.

Only SWAP #4 has $3E00 at index [0] WITH $0000 at index [1]. That's
the unique condition that triggers the wrapper into reading bank 0
boot vector.

### Why is alt_stk[1] $0000 at SWAP #4?

Looking at $FF55+ memory layout in jnext:
- $FF55: $3E00 (= pushed by some prior supervisor code)
- $FF57-$FF58: $0000 (= UNINITIALIZED — never written by supervisor)
- $FF59+: font glyph data (= written by supervisor at boot when
  copying system font to RAM)

The font-data is at $FF59+, which suggests the supervisor wrote
glyphs to $E000+something. The bytes BEFORE the font area
($FF55-$FF58) were partially used (by the $3E00 push) but $FF57-$FF58
was never touched.

In CSpect, $FF57-$FF58 must contain a valid 16-bit value. It's likely
that:
- Supervisor's font-copy LDIR runs differently in CSpect, leaving
  different content in the gap
- OR the supervisor uses a different stack layout altogether
- OR a prior PUSH on jnext had its target value clobbered

### Concrete next-session DZRP query

In CSpect at the equivalent boot phase (after first soft reset, before
the supervisor stabilizes):

1. Set BP at bank 0 $27A3.
2. Hit it, capture: SP, ($5B6A), MEM[($5B6A)..+15].
3. Find the equivalent of jnext's SWAP #4 (= ($5B6A)=$FF55, alt_stk[0]=$3E00).
4. Compare alt_stk[1] in CSpect vs jnext.
5. Whatever's at alt_stk[1] in CSpect — that's the missing value.
6. Walk back to find what code wrote it.


## EOD-23 DZRP CSpect comparison — slot mapping divergence found

Launched CSpect via `mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img -debug`,
ran `tools/cspect_dzrp/g46b_eod23_swap_capture.py --hits 6` twice
(first run captures hits 1-6 from cold-boot $0000; CSpect then
continued running between runs).

### CSpect first 6 hits (cold-boot $0000 → first stabilization)

```
HIT #1: SP=$5BFF ($5B6A)=$FF4F slots[6,7]=$00,$01
        alt_stk=[$23AA, $0274, $006F, $3E00, $0000, $423C, $7E42, $4242]
HIT #2: SP=$5BFF ($5B6A)=$FF51 slots[6,7]=$00,$01
        alt_stk=[$0274, $006F, $3E00, $0000, $423C, $7E42, $4242, $0000]
HIT #3: SP=$5BFF ($5B6A)=$FF4B slots[6,7]=$00,$01
        alt_stk=[$3E93, $150B, $3E93, $0277, $006F, $3E00, $0000, $423C]
HIT #4: SP=$5BED ($5B6A)=$FF55 slots[6,7]=$0E,$0F  ← DIVERGENCE
        alt_stk=[$0000, $0000, $0000, $0000, $0000, $0000, $0000, $0000]
HIT #5: SP=$5BEB ($5B6A)=$FF55 slots[6,7]=$0E,$0F
        alt_stk=[$0000, $0000, ...]
```

### CSpect after stabilization (fewer cycles, productive boot)

```
HIT #4 (later): SP=$5BFB ($5B6A)=$FF2C slots[6,7]=$00,$01
                alt_stk=[$0388, $F700, $22FF, $2043, $5B3E, $13EC, $36C0, $FB6D]
                ← ALL VALID return addresses
```

### Key divergences vs jnext

**1. At the slide-trigger swap (($5B6A)=$FF55, SP near $5BED):**
- jnext: slots[6,7]=$00,$01 (= legacy paging RAM bank 0, +$20 shift → physical $20,$21)
- **CSpect: slots[6,7]=$0E,$0F (= bank 7 dual-port unshifted, physical $0E,$0F)**

**2. What's at MEM[$FF55]:**
- jnext (slot 6,7 = page $20,$21 with shift): MEM[$FF55] reads page $21 offset $1F55 = $3E00
  (= what supervisor pushed via bank 2 $0D3F path)
- CSpect (slot 6,7 = page $0E,$0F unshifted): MEM[$FF55] reads page $0F offset $1F55 = $0000
  (= bank 7 ULA dual-port memory which is empty in the supervisor's stack region)

**3. What RET pops at $27AB:**
- jnext: pops $3E00 → enters wrapper → reads MEM[$0000,$0001] = bank 0 boot vector ($F3,$C3) → BC=$C3F3 → SLIDE
- CSpect: pops $0000 → bank 0 boot vector → re-init at $00EF (productive cycle)

### Root cause re-frame

The slide isn't a stack-content corruption per se — it's a **slot 6/7 mapping divergence**. In CSpect at the equivalent slide-point swap, slots 6,7 are mapped to bank 7 (dual-port ULA), where MEM[$FF55] reads from bank 7 page $0F (= $0000 / unused).

In jnext, slots 6,7 are mapped to RAM bank 0 (= page $20,$21 with +$20 shift), where MEM[$FF55] reads supervisor's earlier-written stack frame ($3E00).

So jnext's apparent "stack at $FF55" is really a STALE stack frame in physical page $21 (RAM bank 0) that the supervisor wrote earlier and didn't expect to re-read. The supervisor probably expects slot 6/7 to be mapped to bank 7 (= empty space) at this point, but jnext keeps the legacy 7FFD bank 0 mapping.

### Why does jnext have slot 6,7 = bank 0 vs CSpect bank 7?

The supervisor must have written `NEXTREG $56,$0E + NEXTREG $57,$0F` in CSpect before this swap. In jnext, either those writes didn't happen, OR they were overwritten by a subsequent 7FFD write that triggered `apply_legacy_ram_slots_` reverting slots 6,7 to legacy bank-0 mapping.

Per VHDL :4677-4680, port_memory_ram_change_dly DOES update MMU6/MMU7 from 7FFD-derived bank. So a 7FFD write between NR $56,$0E and the swap could revert.

### Next investigation steps

1. **Add SLOTCTX-like probe at PC=$0A61 (NEXTREG $56,$0E site)** — confirm whether jnext executes this in the slide cycle. If yes, capture pre/post slot state.
2. **Trace 7FFD writes between NR $56,$0E and the swap** — find what 7FFD write reverts slot 6,7 to bank 0 in jnext.
3. **DZRP CSpect: BP at bank 2 $0A61** — confirm CSpect executes NEXTREG $56,$0E in the equivalent cycle and the value sticks.

### File: tools/cspect_dzrp/g46b_eod23_swap_capture.py

Captures SP, ($5B6A), regs, slot map, current stack, alternate stack
(8 words at ($5B6A)), and sysvars at every BP hit at $27A3. Reusable
for future DZRP comparison work.


## EOD-23 final analysis — 3 next-session tasks completed

### Task #1: Probe at NEXTREG $56,$0E sites (RING_AT=$0A61)

jnext supervisor DOES execute NEXTREG $56,$0E at bank 2 $0A61. At
that moment, slots 6,7 = $0E,$0F (already bank 7 from earlier $1F01
NEXTREG $8E,$7A). So slots 6,7 ARE briefly mapped to bank 7 in
jnext, matching CSpect.

### Task #2: Trace 7FFD writes that revert slots 6,7 (RAM_REBUILD probe)

Per jnext RAM_REBUILD trace, slot 6,7 changes per cycle:
```
pc=$1F01 old=$DE,$DF new=$0E,$0F bank=7  ← NEXTREG $8E,$7A sets bank 7
pc=$01D7 old=$0E,$0F new=$00,$01 bank=0  ← NEXTREG $8E,$08 reverts to bank 0
pc=$104D old=$00,$01 new=$0E,$0F bank=7  ← RST $08 handler NEXTREG $8E,$78
pc=$5B0E old=$0E,$0F new=$00,$01 bank=0  ← RAM toggle wrapper OUT (7FFD),$10
```

Cycle then repeats. After the slide (which fires when slot 6,7 = bank
0), supervisor triggers another soft reset and the cycle starts over.

### Task #3: DZRP CSpect at NEXTREG $56,$0E (script: g46b_eod23_slot67_trace.py)

CSpect HIT sequence per cycle (BPs at $0A61, $0A65, $01D7, $1F01,
$103B, $5B0E, $27A3):
```
HIT #1 $1F01: slots[6,7]=DE,DF, after NEXTREG $8E,$7A → bank 7
HIT #2 $01D7: slots[6,7]=0E,0F, after NEXTREG $8E,$08 → bank 0
HIT #3-5 $27A3: slots[6,7]=00,01 (bank 0, multiple swaps)
HIT #6 $103B: slots[6,7]=00,01 (RST $08 entry)
HIT #7 $0A61: slots[6,7]=0E,0F (= already bank 7 — RST $08 handler at
              $104D NEXTREG $8E,$78 already restored)
HIT #8-12 $0A65: slots[6,7]=0E,0F (productive LDIR loop)
```

**KEY DIFFERENCE: $5B0E does NOT fire in CSpect's path** (no BP hit
captured in 12-event window). CSpect supervisor goes from RST $08
handler directly to $0A61 NEXTREG $56,$0E (= confirms bank 7) and
into a productive LDIR loop at $0A65. jnext supervisor instead routes
through $5B0E (toggle wrapper) which reverts slot 6,7 back to bank 0
and triggers the slide.

### Why the divergence?

After RST $08 handler at $104D, control RETs to user code (= popped
PC from alternate stack). The alternate-stack content at the post-
handler RET differs between jnext and CSpect.

In CSpect, post-handler RET goes to a path that calls $0A61.
In jnext, post-handler RET goes to a path that calls $5B0E.

The supervisor's API-dispatch / state-machine state must differ
between the two implementations. This is upstream of the slide and
requires more investigation to pinpoint.

### Hypothesis for the upstream divergence

The RST $08 handler reads the post-RST return address (= byte after
RST $08 = api_id from caller) and uses it as the user-code resume PC.
If jnext's caller has different bytes after RST $08 than CSpect's,
the post-handler PC differs.

Or: the api_id system uses a dispatch table loaded at boot. If
jnext's dispatch table is in a different state, the same api_id maps
to different code.

Or: the alternate stack's state ($5B6A target) differs in jnext vs
CSpect, causing the swap-and-RET mechanism to land at different code.

### Next-session priority (after EOD-23 closure)

Compare jnext's vs CSpect's RST $08 user-code site:
1. In CSpect, find the RST $08 caller PC (= the byte BEFORE the RST
   $08 instruction that fires HIT #6). Capture user code post-RST.
2. In jnext, do the same — capture user code post-RST.
3. Compare: do they call different APIs / take different paths?


## EOD-23 RST $08 caller comparison — POST-RST PC divergence isolated

Probe `JNEXT_G46B_RST08_TRACE` captures every RST $08 handler entry
with the post-RST PC (= what `POP AF` will read at $103C).

### jnext post-RST-$08 PCs (per cycle, 3 RST $08 calls):

```
HIT #1: sp=$FF53 post_rst_pc=$0302  (= bank 0 'PUSH AF; CALL $0360; RST $18')
HIT #2: sp=$FF53 post_rst_pc=$0302  (= same call site again)
HIT #3: sp=$FF59 post_rst_pc=$423C  ← CORRUPT (= font glyph data!)
```

Bank 0 $0301 = `CF` (RST $08), $0302 = `F5 CD 60 03 DF` (PUSH AF;
CALL $0360; RST $18). That's the supervisor's "switch to bank 7,
push AF, dispatch" pattern.

But hit #3's post_rst_pc=$423C is NOT a real Z80 address pushed by
RST $08. SP=$FF59 reads from slot 7 = page $21 (with +$20 shift) at
offset $1F59. Per the SLIDETRAP page$21 dump: $1F59=$3C, $1F5A=$42 =
**font glyph 'A' bytes** (the 8x8 'A' bitmap stored in page $21 by
supervisor's font copy LDIR).

So jnext's RST $08 hit #3 reads **font glyph data as a return address**
because the supervisor's stack at $FF59 OVERLAPS the font glyph
memory.

### CSpect post-RST-$08 PCs (DZRP comparison via g46b_eod23_rst08_compare.py):

```
HIT #1: sp=$FF53 post_rst_pc=$0302   ← MATCHES jnext hit #1!
HIT #2: sp=$FF42 post_rst_pc=$5CCB   ← BASIC PROG sysvar (interpreter)
HIT #3: sp=$FF48 post_rst_pc=$0D7B
HIT #4-6: sp=$FF3E/4A/27 post_rst_pc=$5CCB (BASIC interpreter loop)
HIT #7: sp=$FF1A post_rst_pc=$5CFB   ← still BASIC area
```

CSpect's post-RST PCs are ALL valid code addresses. The supervisor
reaches the BASIC interpreter at $5CCB (= PROG sysvar) and runs in
that loop. NO corrupt addresses.

### The actual divergence

jnext's slide-causing path:
1. First 2 RST $08 calls: same as CSpect (post=$0302 ✓)
2. Third RST $08 call: SP grew to $FF59 — now reads font glyph data
   as post-RST PC ($423C)
3. Handler pushes $423C, RETs to $423C
4. $423C is in slot 2 = bank 5 lo at offset $023C = screen memory
   area (likely zeros from cleared screen)
5. PC=$423C executes whatever's there → likely NOP slide → eventually
   reaches the bank-stack-swap that triggers the wrapper slide

CSpect's productive path:
1. RST $08 calls progress through different supervisor states
2. SP stays in a region that DOESN'T overlap font glyph data
3. Post-RST PCs are all valid (e.g., $5CCB = BASIC PROG)
4. Supervisor reaches the BASIC interpreter and processes
   autoexec.1st productively

### Why does jnext's SP reach $FF59 (font area)?

The first 2 RST $08 calls in jnext have SP=$FF53. The third has SP=$FF59
— SP INCREASED by 6 bytes between hit #2 and hit #3. That's 3 POPs (3
words = 6 bytes) without matching pushes.

Some code between hit #2 and hit #3 in jnext does extra POPs that
unwind the stack into font glyph memory.

In CSpect, between hits the SP changes are different (e.g., HIT #1 SP=
$FF53, HIT #2 SP=$FF42 = went DOWN by $11 = 17 bytes = 8.5 PUSHes).

So CSpect's supervisor PUSHes more stuff, jnext's POPs (or doesn't
PUSH) and stack ascends into font glyph memory.

### Root cause hypothesis

jnext is missing a sequence of PUSHes between RST $08 hit #2 and hit
#3 that CSpect performs. The missing PUSHes correspond to supervisor
state setup that doesn't happen in jnext.

Or: jnext does the same PUSHes but then does extra POPs that unwind
back out of safe stack territory.

Either way, the supervisor's stack-management diverges between hit #2
and hit #3, and that drives the slide.

### Next investigation

1. Add probe between jnext's RST $08 hit #2 and hit #3: trace every
   PUSH/POP/CALL/RET to see where the divergent stack manipulation
   happens.
2. DZRP CSpect at the same range to see what PUSHes happen.
3. The DIFF in PUSH/POP behavior reveals the exact upstream bug.


## EOD-23 RST $08 GAP trace — full path between hits #2 and #3

Probe `JNEXT_G46B_RST08_GAP=N` traces every PC + SP between the
N-th RST $08 handler entry and the next one.

### jnext path (between hits #2 and #3, ~5000 instructions)

Sequence:
1. **Lines 0-20**: Post-RST-#2 returns to $0302 (PUSH AF; CALL $0360;
   RST $18 → JP $3E80). Bank-flip wrappers run.
2. **Lines 20-50**: Reaches bank 2 supervisor MAIN at $1558 area.
   Plays through CALL/RET sequences with `PUSH $0000` etc.
3. **Lines 1521-1527**: Reaches bank 0/3 RST $XX vector area at
   $004D. With slot 0 mapped to bank 3 lo, executes:
   ```
   $004D: POP DE
   $004E: POP BC
   $004F: POP HL
   $0050: POP AF
   $0051: EI
   $0052: RET
   ```
   Multi-pop epilogue + return. SP unwinds 10 bytes ($5BE5 → $5BEF).
   RET pops $3F00 → PC=$3F00.
4. **Lines 1527-4654**: Bank 3 hi code runs at $3F00-$3FBF area
   (real instructions: NOP, INC E, LD (nnnn),HL, JR NZ, etc.).
   Supervisor's "function epilogue + dispatch" routines.
5. **Lines 4654-4910**: PC enters slot 2 area ($4C00-$4CFF). Slot
   2 = bank 5 lo (unshifted page $0A). Bytes there are mostly $00
   (cleared screen RAM area). NOP slide.
6. **Lines 4910-5000**: Slide continues into $4D00-$4D59. Trace cap
   hit. Next $103B fires somewhere AFTER.

SP histogram across the 5000-line gap: SP stays in $5Bxx range
throughout (low stack). Most-common SP=$5BEF (3766/5000 lines = the
NOP slide region).

### Key observation: NOP slide at $4C00-$4D59 in bank 5 lo

The supervisor at line 4654 reaches PC=$4C00 in slot 2. Slot 2 maps
to physical page $0A (= bank 5 lo, unshifted dual-port). The bytes
at this address are zeros (cleared screen memory). PC slides
linearly through 600+ bytes of NOPs.

Eventually the slide hits a non-zero byte that triggers some
dispatch. By then enough damage is done — the supervisor's stack
state is corrupted by the time it reaches the next RST $08 (with
SP=$FF59, reading font glyph data as return address).

### Root cause refinement

The bug isn't a single divergence — it's a CASCADE:
1. Supervisor reaches bank 3 hi code that JPs into cleared screen RAM
2. NOP slide corrupts execution
3. Eventually bank-stack-swap happens with wrong state
4. RST $08 #3 reads corrupt return address from font glyph memory
5. Wrapper $3E00 is invoked with bad TOS
6. Slide #2 at $C3F3 fires
7. PC=$0000 trap loops

Each step compounds. The ORIGIN of the cascade is somewhere in the
bank 3 hi code path between lines 1527-4654 in the GAP trace.
Specifically: what JP/CALL takes PC into $4C00 area? That's the
upstream divergence.

### Next investigation

1. Find the PC at which the supervisor JPs/CALLs from $3Fxx area
   into $4C00. Add probe at $4Bxx-$4C00 entry.
2. DZRP CSpect at the equivalent point — find what PC in bank 3 hi
   chooses a different target. The diff in JP target is the bug.


## EOD-23 cascade origin — corrupt return address $3F00

The multi-POP epilogue at bank 3 $004D-$0052 pops 4 saved registers
+ RET. The RET target is $3F00 — but bank 3 hi at $3F00..$3FFF is
**font glyph data** (vertical line and smiley face glyphs at $3FE0+).

Stack frame state at line #1521 of the GAP trace (just before the
multi-POP):
```
[$5BE3] = $004D  (= POP-target / address of multi-POP routine)
[$5BE5] = $0101  (saved DE)
[$5BE7] = $0000  (saved BC)
[$5BE9] = $03DD  (saved HL)
[$5BEB] = $0828  (saved AF)
[$5BED] = $3F00  ← RET target (CORRUPT in jnext)
```

So earlier supervisor code did:
- `PUSH AF` (= $0828) on alt stack
- `PUSH HL` (= $03DD)
- `PUSH BC` (= $0000)
- `PUSH DE` (= $0101)
- `CALL <function>` (pushes return-after-call)
- ... or pre-pushed $3F00 manually then `CALL $004D`

The `$3F00` return target is INVALID — bank 3 hi at $3F00 is font
glyph data, not real code. Executing it leads to the slide.

In CSpect, this same RET should pop a VALID return target (e.g.,
real bank 3 code or BASIC interpreter address) so execution continues
productively.

### Identifying the upstream pusher

Some earlier supervisor code pushed `$3F00` onto the alt stack.
Search in supervisor banks for `LD HL,$3F00; PUSH HL` (= bytes `21
00 3F E5`) or `LD <reg>,$3F00; PUSH`:


```
Sites with bytes "21 00 3F E5" (LD HL,$3F00; PUSH HL):

Sites with bytes "00 3F" anywhere (potential 16-bit $3F00 references):
```

## EOD-23 ROOT CAUSE FOUND — wrong bank at $3F00 wrapper call

**$3F00 is another bank-flip wrapper** (similar to $3E00), present
ONLY in banks 1 and 2:

| Bank | $3F00..$3F17 contents |
|------|------|
| 0 | `77 18 e4 ...` real bank-0 code (LD (HL),A; JR $3EE7; ...) |
| 1 | wrapper with NEXTREG $8E,$02 (= flip to bank 2) |
| 2 | wrapper with NEXTREG $8E,$01 (= flip to bank 1) |
| 3 | `00 1c 22 78 ...` — **font glyph data, NO wrapper** |

7 `CALL $3F00` sites in bank 2 (at $0888, $0ADA, $0AE9, $0FAE,
$1044, $27F9, $2815). All expect slot 1 = bank 1 or bank 2 to
provide the wrapper.

### Failure mode in jnext

Supervisor in jnext's slide-cycle does `CALL $3F00` while slot 1 =
**bank 3 hi**. Bank 3 hi at $3F00..$3FFF is font glyph data (vertical
line / smiley face glyphs). The CPU executes:
- $3F00: $00 = NOP
- $3F01: $1C = INC E
- $3F02: $22 = LD ($nnnn),HL  (3 bytes — first byte of "real LD instr")
- ... real Z80 ops continue, but they're interpreting font bitmaps
  as code

Eventually the byte-stream falls off the end of slot 1 ($3FFF) and
into slot 2 ($4000) cleared screen RAM — NOP slide.

Then the multi-POP epilogue at bank 3 $004D pops the wrapper's pre-
pushed [$3F00] return target, returning to $3F00 again — recursive
slide.

### CSpect's correct behavior

CSpect's supervisor at the same `CALL $3F00` has slot 1 = bank 1
or bank 2 hi (which has the wrapper). The wrapper does:
```
LD ($5B54),BC
EX (SP),HL          ; HL = post-CALL inline-DW addr
LD C,(HL); INC HL
LD B,(HL); INC HL
EX (SP),HL          ; restore stack with HL+2 = real return
PUSH $3F13          ; trampoline-end
PUSH BC             ; target
LD BC,($5B54)
NEXTREG $8E,$01    ; flip to bank 1 (or $02 for bank 2 variant)
RET                 ; pops target → executes in new bank
```

This is the SAME pattern as $3E00 wrapper. Real Next supervisor
uses both wrappers: $3E00 for one set of bank flips, $3F00 for
another set.

### The actual bug

Some PRIOR supervisor code in jnext leaves slot 0/1 mapped to bank 3
instead of bank 1 or bank 2. When CALL $3F00 fires, the wrong bank
is read, causing the slide cascade.

This connects to the existing slot 6/7 divergence (= jnext's slot
mapping state diverges from CSpect's at multiple points). Both
slot 0/1 and slot 6/7 wind up wrong in jnext, suggesting a SHARED
upstream cause — likely a NEXTREG $8E or 7FFD write sequence that
operates differently between the two emulators.

### Concrete fix path

1. DZRP probe CSpect at all `CALL $3F00` sites (bank 2 $0888,
   $0ADA, $0AE9, $0FAE, $1044, $27F9, $2815). Capture slot 0/1
   mapping at each.
2. Add jnext probe at the same sites, log slot 0/1.
3. Compare. The mismatch shows when jnext's bank state diverges
   vs CSpect.
4. Walk back to the NEXTREG/port write that set the wrong bank.

