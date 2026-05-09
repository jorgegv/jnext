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
