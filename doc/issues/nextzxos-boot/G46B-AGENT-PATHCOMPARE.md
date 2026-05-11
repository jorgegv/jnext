# G46(b) — Path-comparison RE: where our supervisor diverges from CSpect

**Date**: 2026-05-05 13:30 (read-only RE pass on `g46b-investigation` HEAD = `c771dc8`).
**Working set**: `/tmp/enNextZX.rom` (extracted from `roms/nextzxos-1gb-fat32fix.img`),
`/tmp/cpu-inst.log` (12-second `cpu_inst=trace` capture, ~1.77M instructions, 92 MB),
live investigation doc `doc/issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md`.

This report is read-only RE — no code or test changes. The intent is to identify the
upstream divergence that puts jnext on the dispatcher path (IX=$E01B, SP=$FF7F user stack)
while CSpect reaches $20E6 inside the wrapper (IX=$F700, SP=$5BF9 supervisor stack).

---

## TL;DR — DEFINITIVE FINDING (5-min verification result)

After running the suggested 5-minute investigation, **the actual IX-setter
that fires before the first $20E6 hit is at PC=$0EAD in supervisor bank
2** (`LD IX, $E000`, file offset $8EAD = `dd 21 00 e0`).

This is NOT the same as CSpect's $20E6 entry path. CSpect reaches $20E6
via the wrapper-mediated dispatcher with IX=$F700 (set by `LD IX,$F700`
at bank 0 PC=$2CB5 / $329A / $3409 — none of which fire on our jnext
trace). Our jnext reaches $20E6 via a **different code path entirely**:
it executes a sprite-pixel-iterator routine at bank 2 PC=$0EAD that
sets `IX = $E000 + computed_offset` (using `add ixl; ld ixl, a` after
the immediate load). The result is IX=$E01B (= $E000 + $1B).

So CSpect and jnext are running **two different routines** that
*coincidentally* both end up calling $20E6 (via the supervisor's print/
sprite dispatcher chain). CSpect's path is the welcome-screen-render
path. Our path is something else — most likely a "no-data fallback"
loop that the supervisor takes when its input data is uninitialized
(per `via_mem ix[0..15]=00 00 00 ...` log line).

The reason both paths converge on $20E6 is that $20E6 is shared code
(part of `$2043` sprite-descriptor setup chain). Inputs differ.

**Therefore the ACTUAL upstream divergence is**: our supervisor takes a
different EARLY decision (around $00EF supervisor entry) that routes it
into the bank-2 sprite-iterator at $0EAD instead of the bank-0
`LD IX,$F700; call $277F` path at $2CB5. The "different early decision"
is most likely:

1. NR 0x03 machine-type configuration (CSpect = +3, ours = ?)
2. Sysvar `($5C7F)` content (= 4 bits machine cfg used at $0E9F-$0EAD)
3. A keyboard/joystick-state read that selects a boot menu line.

**Single most actionable next step**: trace the unique-PC sequence from
$00EF to first $0EAD entry in our jnext. Identify the conditional
branch at `0E9F-0EAB` (just before the `ld ix,$e000` at $0EAD). Read
the supervisor's bank 2 code at file offset $8E9F to determine what
input drives that branch. Compare against what CSpect would compute.

Detailed evidence supporting this finding follows in the original
analysis sections below.

## Original analysis: top recommendation

**Root cause direction (high confidence)**: between trace lines 132994
and 134218 (the ~573 ms window leading to first $20E6 hit), slot 0 is
mapped to **AltROM page 0x0C** (= the first 8 KB of `enAltZX.rom`),
not to any supervisor ROM bank. **All 26 trace-observed bytes between
PC=$0001 and PC=$007F match AltROM page 0x0C byte-for-byte (100%
match)** while matching at most 13/27 bytes against any single
supervisor bank.

The supervisor expects to issue `LD IX, $F700` (= bytes `DD 21 00 F7`)
at one of three PCs in bank 0: $2CB5, $329A, $3409. None of these
firings happen normally in our trace — the trace records `PC=$3409
op=0x00` (NOP) at multiple lines (44271, 98096, 100632, 103690, ...),
which means **either**:
- (a) IS being read from AltROM page 0xC ($1409 = `$21`), but the trace
  was captured at a different slot-0-mapping moment where slot 0 maps
  to a different RAM page filled with zeros around $3400; OR
- (b) the supervisor never actually reaches PC=$3409 because earlier
  code took a different path.

(In all 10 of the trace's $3409 hits I sampled, op=$00 NOP. The
supervisor-bank-0 byte at $3409 = `DD`, AltROM-page-$0C byte at $1409 =
`$21`. Neither is $00. So slot 0 is mapped to a THIRD page at the
$3409 hits — most likely a RAM page filled with zeros. The slot 0
mapping varies across the trace.)

**The IX value at $20E6 = $E01B is whatever stale value IX had from
some earlier instruction.** The supervisor *never* successfully
executed `LD IX, $F700` on this code path (verified: 0 hits of `op=0xdd`
at PC=$2CB5, $329A, or $3409 in the entire 1.77 M-instruction trace).

**Single most actionable next test**: query the trace for **any
instruction byte $DD followed by $21 at any PC** in the path from
$00EF (line 32) to first $20E6 (line 134316):

```
awk -v in_window=0 '
/PC=0x00ef/ { in_window=1 }
in_window && /op=0xdd/ {
    pc = substr($3,4)+0
    print NR, $0
}
NR>=134316 { exit }
' /tmp/cpu-inst.log | head -20
```

Then check the next instruction byte at each `op=0xdd` site. The first
`dd 21 ?? ??` in the trace tells us the only IX-immediate-load that
fired before $20E6. If it is NOT `dd 21 00 f7`, the operand or the
slot-0 page underneath was wrong. If NO `dd 21` fires before $20E6,
then IX retains its power-on value (= 0 per VHDL `Z80::reset`) plus
whatever earlier `pop ix` / `ld ix,(nn)` updates fired — which makes
**$E01B traceable to specific stack-pop or memory-load operations.**

This is a 5-minute investigation. Run it next session and post the
results in `LIVE.md`.

So our trace is documenting an **AltROM round-trip**: the supervisor
enabled AltROM via `NEXTREG $8C, $80` (bit 7 = `nr_8c_altrom_en` plus
bit 4 = `nr_8c_altrom_lock_rom0`), called into the AltROM-resident
trampoline at $00EE, the trampoline executed its bank-switch + sysvar
fixups + (likely) IX-load, then disabled AltROM and `ret`'d back into
the main supervisor at $1800.

**The IX register at $20E6 is therefore a function of what the AltROM
trampoline at $00EE-$0100 stored into IX before exiting**. Looking at
the trampoline body (`08 f1 22 52 5b 2a 6a 5b ed 73 6a 5b f9 2a 52 5b
f5 08 ed 91 8e XX c9`), it does NOT touch IX directly — it only swaps
SP, swaps the saved HL via $5B52, and switches NR_8E bank. So **IX
comes from elsewhere**, most likely loaded by a `pop ix` or `ld ix,(nn)`
in AltROM code that runs **between** the trampoline calls and which we
have not yet decoded.

**Next concrete test (single highest-ROI experiment)**: disassemble
`enAltZX.rom` page 0x0C ($0000-$1FFF) and identify EVERY instruction
that loads IX (`dd 21`, `dd 2a`, `fd e1` would be IY, `dd e1` for
`pop ix`, etc.). Cross-reference with the trace to identify which IX
load fires last before $1800. The source operand of that IX load is
the divergence root — it's either a sysvar address (= NextZXOS state
divergence), or a stack pop (= upstream MMU/SP divergence).

A second, complementary test: log every `NEXTREG $8C, X` write across
the 12-second trace; compare the AltROM enable/disable sequence
counts and timing against what CSpect would do (CSpect debugger plugin
or port-trace if available).

---

## Methodology

1. Extract `enNextZX.rom` from the SD image and confirm size = 65536 bytes
   (4 × 16 KB banks; pages 0..7 in ROM-in-SRAM).
2. Generate a fresh `cpu_inst=trace` log via `--bypass-tbblue-fw --delayed-automatic-exit 2`.
3. Build a uniqued PC sequence and locate the first $1F40, $1D96, $20E6 hits.
4. For the **first** $20E6 hit (line 134316 in /tmp/cpu-inst.log), walk back
   the trace to identify every conditional branch on the call chain from
   supervisor entry $00EF down to the $20E6 hit.
5. Disassemble `enNextZX.rom` with `z88dk-dis -mz80n` at the conditional
   branches and decode the inputs (registers, IX-offsets, sysvars).
6. Cross-check trace-observed opcode bytes (`op=0xXX`) against the supervisor
   ROM at the corresponding bank/file offset. Anywhere the byte fails to
   match ANY of the four banks, slot 0 must be mapped to a RAM page or
   alternative ROM source — that is a divergence flag.

Trace line counts (from /tmp/cpu-inst.log):
- $00ef hits: 657 (first at line 32, second at 58128)
- $20e6 hits: 18 (first at line 134316)
- $2734 hits (wrapper entry test): 18 (first at 1710 — BEFORE first $20E6)
- $1f40 hits: 115200 (steady-state keyboard-poll)
- $5B00 hits (bank-flip routine): 657

So the wrapper IS being entered (1710 < 134316) **before** $20E6 first
fires; the wrapper is not fundamentally bypassed. Yet at $20E6 we are
on the user stack, not the supervisor stack. That means our supervisor
returns from the wrapper, then re-enters the dispatcher path **on the
user stack**, before the wrapper's exit-side `ret c` (the shallow-SP
short-circuit at $27a2) ever gets to fire on a CSpect-style supervisor-
stack frame.

---

## Findings — call chain to first $20E6 (file: `/tmp/enNextZX.rom`)

The exact instruction sequence trace from line 134218 (entering supervisor
print path) to line 134316 (first $20E6 hit) is:

```
$1800 ld d,$fe                       ; bank 0 file offset $1800
$1802 jr nz,$1834                    ; NOT TAKEN (Z flag was set on entry)
$1804 ex af,af'
$1805 inc hl
$1806 dec c
$1807 ld a,(hl)                      ; read next character byte
$1808 call $16c0
  $16c0 push af; push bc; push de; push hl
  $16c4 ld e,a                       ; E = char code
  $16c5 call $1d47                   ; <— enters dispatcher
    $1d47 ld a,(ix+$26)              ; (ix+$26) = print mode flag
    $1d4a and a
    $1d4b jr nz,$1d5a                ; *** BRANCH 1 ***
    $1d4d ld a,e                     ; A = char code
    $1d4e cp $20
    $1d50 jr nc,$1d6b                ; *** BRANCH 2 *** (taken: A >= $20)
    $1d6b ld h,$00; ld l,a; add hl,hl×3   ; HL = char_code * 8
    $1d71 cp $80
    $1d73 jr c,$1dad                 ; *** BRANCH 3 *** (NOT taken: A >= $80)
    $1d75 cp $90
    $1d77 jr nc,$1d93                ; *** BRANCH 4 *** (taken: A >= $90)
    $1d93 call $2b62                 ; (compute Y wrap)
      $2b62 .. $2b7e                 ; (Z80N MUL/DIV via mul de,bc + sp adj)
    $1d96 ld bc,$1d6b
    $1d99 jp c,$2b7f                 ; *** BRANCH 5 *** (NOT taken)
    $1d9c res 1,(ix+$25)
    $1da0 ld de,($5c7b)              ; *** READ SYSVAR $5c7b (E_LINE pointer) ***
    $1da4 add hl,de
    $1da5 ld de,$0480
    $1da9 sbc hl,de
    $1dab jr $1dc6
    $1dc6 ld a,h
    $1dc7 cp $bf
    $1dc9 jr c,$1de6                 ; *** BRANCH 6 *** (taken: H < $bf)
    $1de6 ld a,(ix+$24)              ; *** READ IX+$24 (sprite count?) ***
    $1de9 bit 4,(ix+$25)             ; *** READ IX+$25 (sprite flags) ***
    $1ded jr z,$1df0                 ; *** BRANCH 7 *** (taken: bit 4 = 0)
    $1df0 cp (ix+$1c)                ; *** READ IX+$1c (sprite x-coord?) ***
    $1df3 call nc,$2043              ; *** BRANCH 8 *** (taken: A >= (ix+$1c))
      $2043 ..                       ; sprite descriptor setup
        $2057 push ix
        $2058 call $1a88             ; (Z80N test/transform)
        $205b .. $2069 ..            ; descriptor copy
        $20a6 ld hl,$2199            ; trampoline list
        $20a9 call $2178             ; LDIR copy of trampolines to $5b91
        $20ac ld e,$01
        $20ae call $20e6             ; <— first $20E6 hit
```

### Critical branch evidence (all branches at first-hit time)

Inputs at each branch (best inference from disassembly + trace):

- **BRANCH 1** ($1d4b): (ix+$26) was non-zero on **CSpect** (different IX), but
  on jnext (ix+$26) was 0 (because IX=$E01B points at all-zero memory per
  PAGES log `p0f[0x1B..0x2A]=00 f7 21 90 e0 3e 08 df` — the $26 offset
  reads near zero). **Both jnext and CSpect could plausibly take the
  same branch here**; can't distinguish without CSpect register dump.

- **BRANCH 2** ($1d50): based on character code in E. **Same on both
  emulators** — they're trying to print the same character.

- **BRANCH 4** ($1d77 — `cp $90; jr nc, $1d93`): the character code's
  high nibble (post `add hl,hl ×3`) puts A into the [$90..$ff] band. Per
  trace this branch IS taken on jnext. **CSpect could take a different
  branch here** if its character A is in [$80..$8f] — call $2b62 vs jr
  $1dad. But this is a function of the supervisor's data, and the
  supervisor's data depends on its sysvars/IX, which depends on the
  upstream MMU state.

- **BRANCH 8** ($1df3 — `call nc, $2043`): the carry flag from `cp (ix+$1c)`.
  Since IX+$1c on jnext reads zero (because IX=$E01B → page is uninitialized),
  the comparison is `A vs 0` and almost always sets NC → call $2043 fires.
  On **CSpect** with IX=$F700 the byte at IX+$1c is non-zero (the descriptor
  is real), so `cp (ix+$1c)` may set C and skip the call. **This is the
  most consequential branch and is fully driven by IX.**

### The IX divergence

IX=$E01B vs $F700 is the single largest data-flow consequence. IX is
the sprite-descriptor pointer used by $1d47..$2043. CSpect's IX=$F700
points into slot 7 high-half (= AltROM-mirror page in pages 0x2C..0x2F
under NR_57=$10 = page 0x30). Our jnext IX=$E01B points into slot 7
low-half offset $001B (= page $0E or $0F under NR_57=$0F).

Looking at the supervisor RE in `LIVE.md` line 793: CSpect IX=$F700
reads bytes `EB D7 08 FA 01 93 E9 3F AA 46 F1 59 0E 00 FB 08 FF 20 18
00 00 1F 17 00 C0 10 18 01 20 08`, with IX+$11 = $20 (sprite-width=32).
Our IX=$E01B reads zeros at IX+$11.

**IX is NOT loaded by any instruction in the dispatcher path between
supervisor entry and $1800.** It must be loaded earlier — most likely
by a `pop ix` from the supervisor stack inside the wrapper $272e..$274a,
or via the wrapper-exit $279d..$27ab restoring user-stack state.

When the wrapper switches stacks (supervisor → user), it RESTORES IX
from the user stack. **If the user stack's saved IX was corrupted by
LDIR or by a wrong NR_57 mapping when the supervisor wrote the saved
value, IX comes back wrong.** Per `LIVE.md` Round 1 finding (Fix #1
already landed): the supervisor's NR-swap routine at $27DE used to read
NR_57 with stale value, leading to wrong slot 7 mapping. Fix #1 closed
the **read** side, but the **write** side at $27a3..$27ab pops a
saved-IX from the user stack at $5b6a, which was set up earlier — and
that earlier setup ran with whatever (possibly still divergent) MMU
state was active.

---

## Findings — slot 0 byte mismatch resolved

In the TL;DR I noted the bytes at $0001-$007F in our trace match
**AltROM page 0x0C** byte-for-byte. The `$08 $F1 $22 $52 $5B ...`
24-byte trampoline at PC $00EE is the AltROM-resident bank-switch stub
at AltROM file offset $00EE. The supervisor enables AltROM via `nextreg
$8C, $80` at AltROM offset $007B and the AltROM remains in slot 0 until
explicitly disabled by `nextreg $8C, $00`. So slot 0 alternates between
AltROM page 0x0C, supervisor banks 0/1/2/3, and other RAM pages depending
on NR 0x8C state and port_7FFD/1FFD ROM-bank selection.

(Original hypothesis that `nextreg $50, X` was directly written has been
**refuted** by static disassembly: zero hits for `ED 91 50` or `ED 92 50`
in any of the four supervisor banks. NR_51 IS written 22 times, but only
to map slot 1 to physical page $10 temporarily and back to $FF — expected.)

---

## Top 3 candidate divergence points (sorted by confidence)

### Candidate #1 — Different supervisor entry path (HIGHEST CONFIDENCE)

Per the TL;DR, our trace shows IX is computed by the bank-2 sprite-
iterator at PC=$0EAD (file offset $8EAD: `ld ix,$E000; ... add ixl;
ld ixl,a`). CSpect-style traces would set IX via `LD IX, $F700` at
bank 0 PC=$2CB5 / $329A / $3409. **None of those three PCs fires `op=DD`
in the entire 1.77M-instruction trace** — only `op=00` (NOP) at $3409
when slot 0 is on a NOP-filled page.

**Concrete fix candidate**: log a NR-write-trace and an instruction-
PC-trace at low cardinality. For every distinct PC reached in the path
$00EF → first $20E6 hit, identify the upstream conditional branch
that diverges from CSpect's path. The most likely divergence is at
bank 2 PC=$0E9F-$0EAB (just before `ld ix,$e000`) — read sysvar
`($5C7F)` content (4-bit machine cfg) and check whether our jnext
has it set correctly per the `--bypass-tbblue-fw` post-handoff init.

### Candidate #2 — IX restore from corrupt user-stack frame

If Candidate #1 is wrong (i.e. our jnext IS supposed to enter via the
$0EAD bank-2 path), then the divergence is upstream of $0EAD: a sysvar
or NR-port read that drives the conditional branch into $0EAD. Likely
sysvar candidates: `($5C7F)` (4-bit machine cfg), `($5C36)` (boot mode),
`($5B69)` (RAM-test result). The supervisor reads these at multiple
sites and they're set up by the user-stack pre-wrapper init.

### Candidate #3 — SP at first wrapper hit ($2734)

Diagnostic addition: log SP at trace line 1710 (first $2734 hit) and
compare against CSpect. If SP_high < $5C, our jnext takes the
"stay-on-current-stack" path which is the user-stack path. CSpect
takes the supervisor-stack-switch path. The SP divergence is itself
downstream of an earlier upstream issue, but identifying the SP value
at first hit narrows the search window.

---

## IX is set explicitly by the supervisor — `LD IX,$F700`

A grep of `enNextZX.rom` for IX-load instructions (`dd 21`, `dd 2a`,
`dd e1`) finds **multiple `LD IX, $F700` direct-immediate writes** in
bank 0:

```
bank 0 PC=$2cb5  ld ix, $f700
bank 0 PC=$329a  ld ix, $f700
bank 0 PC=$3409  ld ix, $f700
```

Plus dozens of `pop ix` and `ld ix,(nn)` sites. So **CSpect's IX=$F700
is the supervisor-intended value**. Our jnext IX=$E01B is wrong.

But the call chain to $1D47 in our trace does NOT go through any of
these `LD IX, $F700` sites. Instead it goes:

```
... → $0068 (AltROM bank-switch trampoline)
    → $0072 (push $007B; push BC; ld bc,($5B54); ret-using-pushed-BC)
    → $007B (nextreg $8C, $80 — AltROM ENABLE)
    → $007F (ret pops original BC = $1800)
    → $1800 (print routine entry)
```

The `nextreg $8C, $80` at AltROM offset $007B is the **canonical AltROM
enable trampoline**. The supervisor uses this pattern to enter AltROM-
resident code without needing to track AltROM state explicitly — the
matching disable trampoline at $007B in the AltROM (offset $7B in
file) is `nextreg $8C, $00` followed by ret.

So the print routine at $1800 is reached **after AltROM is ENABLED**.
And before this, the supervisor pushed BC=$1800 and called the AltROM
bank-switch trampoline. **Whatever code pushed BC=$1800 had IX in some
state — that state is what we observe as IX=$E01B.**

The push-BC-then-trampoline pattern means the IX-setter is somewhere
**before** the BC=$1800 push. Looking at the supervisor's print
output flow: there are 7 sites in bank 0 ($17a5, $17ab, $17b0, $17df,
$17fe, $1808, $1829) that call `$16C0`, but those are INSIDE the print
routine. There must be an upstream caller in the supervisor's main
event loop that sets BC=$1800 and chains into the AltROM trampoline.

A grep for `01 00 18` (= `ld bc, $1800`) in the supervisor would find
the caller. Run that grep next session.

---

## Conclusion

The dispatcher path through $1D47 → $20E6 is **NOT** itself the
divergence — both jnext and CSpect execute this code path. The
divergence is in the **DATA the dispatcher operates on**: IX, the
contents of the page mapped at slot 7, and the SP value at wrapper
entry.

The chain that reaches the dispatcher is:
1. Supervisor main code sets up some state (incl. IX).
2. Supervisor pushes BC=$1800 (= print-routine entry).
3. Supervisor calls AltROM bank-switch trampoline at $0068 in slot 0
   (= AltROM page 0x0C, AltROM enabled by `nextreg $8C, $80` at
   offset $007B).
4. AltROM trampoline returns (pops BC=$1800) → PC=$1800.
5. Print routine runs $1800 → $1808 → $16C0 → $1D47 → … → $20E6.

**The IX value used inside the print routine is whatever was set in
step 1 — by the supervisor's upstream code BEFORE step 2's push.**
The supervisor *intends* IX=$F700 (per the multiple `LD IX, $F700`
direct-immediate writes in bank 0). On real hardware (CSpect) IX is
indeed $F700 at $20E6. On our jnext, IX is $E01B — meaning step 1's
upstream code set IX wrong, OR a `pop ix` along the way picked up a
corrupt saved IX from RAM.

**The single concrete actionable next step**:

1. Disassemble bank 0 around the three `LD IX, $F700` sites ($2cb5,
   $329a, $3409). Identify the caller(s) of those routines.
2. In the trace, search for the FIRST hit of any of those PC=$2cb5,
   $329a, $3409. Confirm that the trace does NOT actually execute that
   instruction during the boot path leading to first $20E6 (we did not
   see $2cb5/$329a/$3409 in the unique-PC sequence up to first $20E6
   hit — line 1..134316). If correct, **the supervisor's intended
   `ld ix,$F700` setup never fires on our jnext**.
3. Find the alternate IX-setter that DID fire: search for `dd 21` and
   `dd 2a` and `dd e1` PCs in the trace before the first $20E6 hit.
   The last one fires before $20E6 — that's our IX-setter.
4. Compare its source operand against what `enNextZX.rom` says it
   should be. If the operand is a `(nn)` sysvar (`dd 2a XX XX`), the
   sysvar at that address has the wrong value — chase upstream.

This 4-step analysis will identify the IX divergence root in ≤ 1 hour.

---

## Files of relevance (absolute paths)

- Supervisor ROM (extracted): `/tmp/enNextZX.rom`
- Supervisor RE doc (prior session): `/home/jorgegv/src/spectrum/jnext/doc/issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md`
- ROM-slot mapping logic: `/home/jorgegv/src/spectrum/jnext/src/memory/mmu.cpp:319-339` (apply_legacy_rom_slots_)
- NR-write-handler registration (Fix #1 site): `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:~1281`
- Bypass-fw setup: `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:3506-3534, 3706-3739`
- Trace log: `/tmp/cpu-inst.log` (regenerable via `./build/jnext --headless --machine next --sd-card roms/nextzxos-1gb-fat32fix.img --bypass-tbblue-fw --log-level cpu_inst=trace --delayed-automatic-exit 2`)
- Disassembly tool: `z88dk-dis -mz80n -o 0x0000 -s <PC> -e <PC+N> /tmp/enNextZX.rom`
  - For supervisor banks 1/2/3 use `-o 0x4000 -k 0x4000` etc. (the `-k` skips bytes at file start, `-o` sets address origin)

---

## Open questions for next session

1. ~~Does the supervisor ever issue a direct `nextreg $50, X` or
   `nextreg $51, X` write?~~ **ANSWERED** by static disassembly search
   of all 4 banks: NR_50 is NEVER written directly (zero hits for
   `ED 91 50` and `ED 92 50`). NR_51 IS written 22 times — `nextreg
   $51, $10` (bank 0 PC $0B70 / bank 1 PC $0AB4 / bank 1 $0C3C) maps
   slot 1 to physical SRAM page $10, paired with `nextreg $51, $FF`
   to restore. This is **expected** — both jnext and CSpect would do
   this. So Candidate #1's specific NR_50 hypothesis is REFUTED for
   slot 0; it must instead be that **slot 0 ROM-bank selection via
   port_7FFD bit 4 + port_1FFD bit 2 + port_eff7 bit 3** drove slot 0
   to a non-supervisor-bank state. Re-investigate Candidate #1 with
   focus on `apply_legacy_rom_slots_()` triggers.

2. At trace line 1710 (first $2734 hit), what was SP at that moment?
   The trace log emits at instruction-boundary granularity but does
   not include all register state. A small diagnostic addition to the
   $2734 hook (already used for the existing G46B SPRITE log) that
   emits SP and IX would be enough.

3. Did our pre-Fix-#1 supervisor ever reach the welcome screen on a
   single iteration before settling into the loop? If yes, Fix #1
   IS sufficient for the welcome-render path and the loop is a
   secondary failure. If no, Fix #1 is necessary but not sufficient,
   confirming the multi-divergence framing in `LIVE.md` line 717.

These three questions are each ≤ 30 minutes of work and would
substantially narrow the candidate set.
