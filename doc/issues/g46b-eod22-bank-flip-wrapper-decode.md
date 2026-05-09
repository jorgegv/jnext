# G46(b) EOD-22 — Bank-flip wrapper at $5B00..$5B52 — static decode

**Date:** 2026-05-08 (EOD-22 follow-up to EOD-21 PC=$0000 blocker)
**Source:** static analysis only (`doc/issues/dasm/enNextZX-bank0.asm` lines
94–153; `g46b-boot-chain-disassembly.md` §3.3.3 / §9.9). No code modified, no
emulator run.

## 1. Provenance

`$00E3` LDIRs `$0091..$00E2` (82 bytes) into RAM `$5B00..$5B51`. The bytes are
not relocated — every internal absolute reference inside the block was authored
to point at the **RAM** copy. So `$5B00 = $0091`, `$5B34 = $00C5`, `$5B48 =
$00D9`, `$5B4D = $00DE`. Outside-block reads/writes at `$5B5C` (7FFD shadow)
and `$5B67` (1FFD shadow) are BASIC-area sysvar bytes, NOT inside the LDIR.

## 2. Wrapper bytecode + decode (`$5B00..$5B11`, the toggle core)

```
$5B00 f5            push af
$5B01 c5            push bc
$5B02 01 fd 7f      ld   bc,$7FFD
$5B05 3a 5c 5b      ld   a,($5B5C)        ; 7FFD shadow
$5B08 ee 10         xor  $10              ; flip bit 4 (rom_lo)
$5B0A f3            di
$5B0B 32 5c 5b      ld   ($5B5C),a
$5B0E ed 79         out  (c),a            ; OUT $7FFD,a
$5B10 01 fd 1f      ld   bc,$1FFD
$5B13 3a 67 5b      ld   a,($5B67)        ; 1FFD shadow
$5B16 ee 04         xor  $04              ; flip bit 2 (rom_hi)
$5B18 32 67 5b      ld   ($5B67),a
$5B1B ed 79         out  (c),a            ; OUT $1FFD,a
$5B1D fb            ei
$5B1E c1            pop bc
$5B1F f1            pop af
$5B20 c9            ret
```

## 3. Other entry points inside the RAM block

```
$5B22 cd 00 5b   call $5B00              ; toggle, then…
$5B25 e5         push hl
$5B26 2a 5a 5b   ld   hl,($5B5A)         ; saved return
$5B29 e3         ex   (sp),hl
$5B2A c9         ret                     ; longer-form bank-flip + RET
$5B2B…$5B33                              ; second variant (uses $5B34 trampoline)
$5B48 ed 91 8e 03  nextreg $8e,$03  ; ret  ← (target of $0085 push + $008E jp)
$5B4D ed 91 8e 00  nextreg $8e,$00  ; ret  ← used by $0030/$038E
```

## 4. Caller protocol (bank 0 RST $28 / $0080 trampoline)

```
RST $28 → $0028: ld ($5B54),bc      ; preserves caller BC
         $002C: ex (sp),hl          ; SP=DW-target, HL preserved
         $002D: jp $0080            ; common dispatcher

$0080:   ld c,(hl); inc hl; ld b,(hl); inc hl   ; BC <- inline DW
         ex (sp),hl                              ; restore caller HL, push past-DW PC
         push $5B4D                              ; ED 8A — return-to-bank-0 nextreg
         push bc                                 ; final RET pops DW target
         ld bc,($5B54)                           ; restore caller BC
         jp $5B48                                ; nextreg $8E,$03; RET → enters bank-3 at DW
```

Net effect: `RST $28; DW <addr>` jumps to `<addr>` after issuing
`NEXTREG $8E,$03`, with a stacked `$5B4D` so the eventual `RET` from the
target re-enters `NEXTREG $8E,$00` and resumes after the DW.

## 5. Critical observation — wrapper IS protocol-symmetric

The wrapper at `$5B00..$5B20` toggles **BOTH** `$7FFD(4)` and `$1FFD(2)` on
every call. Its toggle is shadow-driven (XOR), so two calls round-trip
deterministically regardless of how many bits compose `sram_rom`. Pre-fix
(ZXN_ISSUE2, 1-bit) only `$7FFD(4)` was effective — the `$1FFD(2)` write
was a no-op as far as ROM mapping was concerned, but the shadow byte at
`$5B67` was still updated. **Post-fix (+3, 2-bit)** the same two writes
now compose into a 2-bit `sram_rom`. Because both bits flip together the
wrapper still describes a **two-state alternation** (00↔11 in `sram_rom`
ordering or 10↔01 depending on initial shadow), not a four-way walk.
**It cannot drop into an unintended bank** as long as both shadows are
in lockstep.

## 6. Could 1-bit→2-bit `sram_rom` cause PC=$0000?

**Verdict: NO** for the wrapper itself; **MAYBE** indirectly via shadow desync.

Reasoning:

- The wrapper does not "know" about the machine type — it just XORs two
  hardware shadow bytes and re-emits them. In +3 mode it composes
  `sram_rom = ((1FFD>>2)&1)<<1 | ((7FFD>>4)&1)`, so the two shadows are
  the bank index. As long as BOTH shadows started in agreement with the
  hardware state (`$5B5C ↔ $7FFD`, `$5B67 ↔ $1FFD`), every toggle stays
  on the bank-0 ↔ bank-3 axis. The DW-pop mechanism never reads from
  `$0000`; it reads from caller-stacked words above SP.
- The supervisor's NR $03,$B3/$B0 commit (now correctly propagated post
  EOD-19 fix) does not itself rewrite `$5B5C`/`$5B67`. If the supervisor
  toggled `$1FFD(2)` directly without updating `$5B67`, the next wrapper
  call would now flip from a stale shadow and the new `OUT $1FFD,A`
  would land on an unintended `sram_rom` value — the RET would still
  return to the caller's stacked target, but execution after that target
  would be in a wrong bank. None of that explains PC=$0000 directly:
  there's no path inside the wrapper that pushes `$0000` onto the stack.
- The `$0000` landings observed in EOD-21 are ~640× over 6 minutes
  WITHOUT NR $02 firing. The wrapper's `RET` only pops caller-supplied
  values and `$5B4D` — both non-zero. So PC=$0000 is **not** a wrapper
  RET. The likely cause is elsewhere: stack underflow that pops `$0000`
  from cleared RAM, or a `JP (HL)` with HL=0, or an IM 1 ISR returning
  via a corrupted shadow stack.

## 7. Recommendation

Add `JNEXT_G46B_5B5C_5B67_AUDIT=1`: at every NR $03 / $7FFD / $1FFD
write, log shadow vs hardware mismatches. If shadows drift, every later
wrapper call routes to the wrong bank — different blocker, but worth
ruling out. The PC=$0000 trap should be diagnosed via the
`JNEXT_G46B_PC0_TRAP` probe proposed in EOD-21 (log prev_pc / SP /
mem[SP±2] on each PC=$0000 transition).
