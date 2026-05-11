# Pass-14 (CPU/Z80N/IM2) — Fix-of-Reviewer Review (V14-CPU-NIT-01)

- Fix-reviewer worktree: `.claude/worktrees/task2-verify14-cpu-z80n-im2-fix-reviewer`
- Fix-reviewer branch: `task2/verify14-cpu-z80n-im2-fix-reviewer` (forked off `task2/verify14-cpu-z80n-im2` at HEAD `be9e625`)
- Fix HEAD: `be9e625` (`6d14efb` fix + `be9e625` doc)
- Build: Release, `-DENABLE_QT_UI=ON`
- Verdict: **APPROVE** — DD/FD-prefix-walk fix correctly closes the V14-CPU-NIT-01 gap (and the V13-CPU-01 DJNZ-prefix sibling gap as a side effect). All six discriminative tests are honest (FAIL pre-revert, PASS post-restore), no regressions on ctest 38/38, FUSE 1356/1356, CPU regressions 36/36.

## Scope verified

| Item                                                 | Status                |
|------------------------------------------------------|-----------------------|
| VHDL claim `t80n.vhd:513-531` (DD/FD ISet stays "00") | VERIFIED              |
| VHDL claim `t80n_mcode.vhd:927-936` (INC/DEC ss DPair gating) | VERIFIED       |
| VHDL claim `t80n.vhd:1358-1360` (DJNZ latch unconditional) | VERIFIED        |
| VHDL claim `t80n.vhd:1361-1367` (BC inc/dec latch)   | VERIFIED              |
| Fix code: prefix-chain walk produces correct `inner_opcode` | VERIFIED       |
| Fix code: arbitrary-length DD-DD-... chains (loop bound 64) | VERIFIED       |
| Fix code: ED-block-xfer detection unchanged          | VERIFIED              |
| Fix code: polarity per branch (DJNZ inverted, INC/DEC BC nonzero=1) | VERIFIED |
| Discriminative tests V14-CPU-NIT-01-A..F             | VERIFIED (all 6 FAIL pre-revert, PASS post-restore) |
| V13-CPU-01 DJNZ-prefix sibling closure (tests E/F)   | VERIFIED              |
| Full ctest 38/38 PASS                                | PASS                  |
| FUSE 1356/1356 PASS                                  | PASS                  |
| CPU Z80N regressions 36/36 PASS                      | PASS (was 30 pre-NIT-01, +6) |
| Missed-cases hunt                                    | None found            |

## VHDL claims re-verified

### `t80n.vhd:513-531` — DD/FD ISet behaviour

Read at lines 505-531 in
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n.vhd`:

```vhdl
ISet <= "00";
if Prefix /= "00" then
   if Prefix = "11" then              -- DD/FD
      if IR(5) = '1' then
         XY_State <= "10";
      else
         XY_State <= "01";
      end if;
   else
      if Prefix = "10" then           -- ED
         XY_State <= "00";
         XY_Ind <= '0';
      end if;
      ISet <= Prefix;
   end if;
else
   XY_State <= "00";
   XY_Ind <= '0';
end if;
```

For DD/FD prefixes (Prefix="11"), only `XY_State` is updated; `ISet` stays
at "00" (the default). The parametric mcode dispatch in `t80n_mcode.vhd`
keys on `IRB` (= the latched IR byte after the prefix), so the inner
opcode 0x03/0x0B/0x10 takes the same `case` branch as the unprefixed
form. **Audit's claim verified.**

### `t80n_mcode.vhd:228, 927-936` — DPair = IR(5:4)

```vhdl
DPair := IR(5 downto 4);
...
when "00000011"|"00010011"|"00100011"|"00110011" =>          -- INC ss
   IncDec_16(3 downto 2) <= "01";
   IncDec_16(1 downto 0) <= DPair;
when "00001011"|"00011011"|"00101011"|"00111011" =>          -- DEC ss
   IncDec_16(3 downto 2) <= "11";
   IncDec_16(1 downto 0) <= DPair;
```

For inner opcode 0x03 (INC BC) and 0x0B (DEC BC), `IR(5:4)="00"` so
`DPair="00"` → `IncDec_16 = "0100"` / `"1100"` → low-3 bits `"100"` →
matches the latch gate at `t80n.vhd:1361`. **Confirmed.**

### `t80n.vhd:1358-1367` — both latch sites

```vhdl
if I_DJNZ = '1' and Save_ALU_r = '1' and Mode < 2 then
   IncDecZ <= F_Out(Flag_Z);                                 -- DJNZ polarity (inverted)
end if;
if (TState = 2 or (TState = 3 and MCycle = "001")) and IncDec_16(2 downto 0) = "100" then
   if ID16 = 0 then IncDecZ <= '0';
   else             IncDecZ <= '1';                          -- BC dec polarity (non-inverted)
   end if;
end if;
```

Two separate sites with opposite polarities — the audit and fix both
preserve this distinction. **Verified.**

## Coverage hunt — exhaustive `IncDec_16(2..0) = "100"` enumeration

I grep'd all `IncDec_16` writes in `t80n_mcode.vhd` (54 sites) and
classified each by its low-3 bits:

| Mnemonic / opcode group                     | IncDec_16 | low-3 | latch fires? |
|----------------------------------------------|-----------|-------|--------------|
| INC BC (parametric, DPair="00")              | "0100"    | "100" | YES          |
| DEC BC (parametric, DPair="00")              | "1100"    | "100" | YES          |
| LDPIRX (Z80N ED 8B), line 1969               | "1100"    | "100" | YES          |
| LDI/LDD/LDIR/LDDR + LDIX/LDIRX (line 2105)   | "1100"    | "100" | YES          |
| LDIRSCALE (Z80N), line 2204                  | "1100"    | "100" | YES          |
| LDDX/LDDRX (Z80N ED AC/BC), line 2236        | "1100"    | "100" | YES          |
| CPI/CPD/CPIR/CPDR (line 2264)                | "1100"    | "100" | YES          |
| INC DE/HL/SP, DEC DE/HL/SP                   | "0101..0111", "1101..1111" | "101", "110", "111" | NO |
| All HL-related sites (0110/1110)             | "0110"/"1110" | "110" | NO           |
| All DE-related sites (0101/1101)             | "0101"/"1101" | "101" | NO           |
| All SP-related sites (0111/1111)             | "0111"/"1111" | "111" | NO           |
| INI/IND/INIR/INDR (lines 2496-2506)          | "0xx0"/"1xx0" | "000"/"110" | NO    |
| OUTI/OUTD/OTIR/OTDR (lines 2544-2549)        | "0xx0"/"1xx0" | "000"/"110" | NO    |

**The complete set of opcodes that latch IncDecZ is {INC BC, DEC BC,
DJNZ, all BC-decrementing block transfers (ED A0/A8/B0/B8, ED A1/A9/
B1/B9, ED 8B, ED 8C, ED A4/B4, ED AC/BC)}.**

Coverage in jnext after the V14-CPU-NIT-01 fix:

| Opcode                  | Plain | DD/FD | DD-DD-… |
|-------------------------|-------|-------|---------|
| INC BC (0x03)           | YES (V14-CPU-01) | YES (NIT-01) | YES (loop) |
| DEC BC (0x0B)           | YES (V14-CPU-01) | YES (NIT-01) | YES (loop) |
| DJNZ (0x10)             | YES (V13-CPU-01) | YES (NIT-01) | YES (loop) |
| LDI/LDIR (ED A0/B0)     | YES (z80_cpu.cpp:884-890) | YES (ed_block_xfer set in walk) | YES |
| LDD/LDDR (ED A8/B8)     | YES | YES | YES |
| CPI/CPIR/CPD/CPDR (ED A1/A9/B1/B9) | YES | YES | YES |
| Z80N LDIX (ED A4)       | YES (inline z80n_ext.cpp:601) | NO* | NO* |
| Z80N LDIRX (ED B4)      | YES | NO* | NO* |
| Z80N LDDX (ED AC)       | YES | NO* | NO* |
| Z80N LDDRX (ED BC)      | YES | NO* | NO* |
| Z80N LDPIRX (ED 8B)     | YES | NO* | NO* |
| Z80N LDIRSCALE (ED 8C)  | YES | NO* | NO* |

`*` Z80N opcodes prefixed by DD/FD: jnext's Z80N intercept at
`z80_cpu.cpp:524` keys on `opcode == 0xED` at PC, so DD-ED-A4 (DD-LDIX)
is NOT routed to `execute_z80n`; FUSE handles it as standard ED-A4
which is undefined / 8T NOP — no IncDecZ update happens, but also no
BC decrement happens. This is consistent with how the FPGA would
actually behave: DD/FD prefixes don't affect Z80N opcodes (the Z80N
extension lives at the ED-prefix level only). Behaviour is
**FUSE-faithful** (DD-ED-A4 = NOP) and consistent with VHDL silence on
DD-ED-A4. **No finding here.**

## Fix code review (`src/cpu/z80_cpu.cpp:819-845`)

```cpp
uint8_t inner_opcode = opcode;
bool ed_block_xfer = false;
if (opcode == 0xDD || opcode == 0xFD) {
    uint16_t walk_pc = static_cast<uint16_t>((pc + 1) & 0xFFFF);
    for (int hop = 0; hop < 64; ++hop) {
        uint8_t b = mem_.read(walk_pc);
        if (b == 0xDD || b == 0xFD) {
            walk_pc = static_cast<uint16_t>((walk_pc + 1) & 0xFFFF);
            continue;
        }
        // Found the inner (non-prefix) opcode.
        inner_opcode = b;
        if (b == 0xED) {
            uint8_t ext = mem_.read(
                static_cast<uint16_t>((walk_pc + 1) & 0xFFFF));
            if ((ext == 0xA0) || (ext == 0xA8) || (ext == 0xB0) ||
                (ext == 0xB8) || (ext == 0xA1) || (ext == 0xA9) ||
                (ext == 0xB1) || (ext == 0xB9)) {
                ed_block_xfer = true;
            }
        }
        break;
    }
}
const bool is_djnz   = (inner_opcode == 0x10);
const bool is_inc_bc = (inner_opcode == 0x03);
const bool is_dec_bc = (inner_opcode == 0x0B);
```

Strengths:

- **Single walk replaces three opcode-keyed predicates** — DRY, removes
  the prior split between `is_djnz / is_inc_bc / is_dec_bc` (entry-keyed)
  and `ed_block_xfer` (walked).
- **Loop bound 64** is conservative — the longest legal Z80 prefix chain
  in practice is bounded by the 4 R-bumps per M1; a real program with
  64+ DD/FD bytes would also be the longest known instruction. No
  realistic program hits this. If the bound is hit, `inner_opcode` falls
  back to the entry opcode (0xDD/0xFD), which is not 0x10/0x03/0x0B/0xED
  → all four classifications stay false → safe (no false positive).
- **Memory-wrap is correct**: `(walk_pc + 1) & 0xFFFF` keeps the walk
  within the 16-bit address space.
- **No double-read or order-of-evaluation hazard**: the walk is a
  read-only inspection of memory at the time `execute()` was entered
  (before `fuse_z80_execute_one()` runs), so it cannot race with bus
  state mutated mid-instruction.
- **Polarity preservation**: the three `if` branches at lines 851-906
  each apply the appropriate VHDL polarity. DJNZ uses
  `(((BC>>8)&0xFF) == 0) ? 1 : 0` (post-dec B==0 ↔ V13 inverted polarity),
  INC/DEC BC uses `(BC != 0) ? 1 : 0` (BC nonzero ↔ V14 non-inverted),
  ED block xfer uses `(BC != 0) ? 1 : 0` (same as V14). Each polarity
  is verified by the discriminative tests.

Weaknesses / nits considered:

- **Not split into a helper** — the walk is in-line. The same shape
  appears in another walk-prefix loop at `z80_cpu.cpp:729-764` (the
  M1-callback prefix walk for DD/FD/CB/ED). Refactoring to a single
  shared helper `walk_to_inner_opcode(pc)` would be cleaner but is
  scope-creep for a fix-of-reviewer; not blocking.
- **The 64-hop bound** could in theory be `INT_MAX` since a DD-DD-…
  chain is bounded only by program memory. The chosen 64 is safe and
  faster (cache-friendly); not blocking.

The code is **clean, well-commented, VHDL-grounded, and FUSE-aware**. No
changes requested.

## Discriminative test verification

I executed the standard revert protocol on this fix-reviewer worktree:

1. Built Release at `be9e625` post-fix → ran
   `./build/test/cpu_z80n_im2_regressions_test` → **36/36 PASS**.
2. Edited `src/cpu/z80_cpu.cpp` line 830 to comment out
   `inner_opcode = b;` (the single line that propagates the inner
   opcode through the walk).
3. Rebuilt Release → ran the regression binary → **30 PASS, 6 FAIL**.

The six FAILs were exactly:

```
[FAIL] V14-CPU-NIT-01-A-DD-INC-BC-UPDATES-INCDECZ-VHDL-1361
       DD INC BC ($FFFF→$0000) IncDecZ=1 (post-fix: 0; pre-fix: stale 1);
       LDWS F=0x04 F.P=1 (post-fix: 0; pre-fix: 1)
[FAIL] V14-CPU-NIT-01-B-FD-INC-BC-UPDATES-INCDECZ-VHDL-1361
       FD INC BC ($FFFF→$0000) IncDecZ=1 (post-fix: 0); LDWS F.P=1 (post-fix: 0)
[FAIL] V14-CPU-NIT-01-C-DD-DEC-BC-UPDATES-INCDECZ-VHDL-1361
       DD DEC BC ($0002→$0001) IncDecZ=0 (post-fix: 1; pre-fix: stale 0);
       LDWS F.P=0 (post-fix: 1)
[FAIL] V14-CPU-NIT-01-D-FD-DEC-BC-UPDATES-INCDECZ-VHDL-1361
       FD DEC BC ($0002→$0001) IncDecZ=0 (post-fix: 1); LDWS F.P=0 (post-fix: 1)
[FAIL] V14-CPU-NIT-01-E-DD-DJNZ-UPDATES-INCDECZ-VHDL-1359
       DD DJNZ B=1→0 IncDecZ=0 (post-fix: 1; pre-fix: stale 0); LDWS F.P=0 (post-fix: 1)
[FAIL] V14-CPU-NIT-01-F-FD-DJNZ-UPDATES-INCDECZ-VHDL-1359
       FD DJNZ B=1→0 IncDecZ=0 (post-fix: 1); LDWS F.P=0 (post-fix: 1)
```

Pre-revert messages match the doc's "post-fix / pre-fix" predictions
exactly. **All six tests are honestly discriminative.**

After re-restoring the line and rebuilding, the regression binary again
reported **36/36 PASS**.

## Full test results (post-restore HEAD `be9e625`)

```
ctest --test-dir build           : 38/38 PASS (100%)
fuse_z80_test build/test/fuse    : 1356/1356 PASS
cpu_z80n_im2_regressions_test    : 36/36 PASS (was 30 pre-NIT-01, +6)
```

**FUSE invariant held** — the prefix walk doesn't change FUSE-driven
state mutation; it only post-dispatches IncDecZ.

## Cross-finding linkage

- **V13-CPU-01** (Pass-13): plain DJNZ polarity. Closed by the prior
  pass; left a sibling DD/FD-prefix gap.
- **V14-CPU-01** (Pass-14): plain INC BC / DEC BC latch coverage.
  Closed in the audit commit; reused the V13 opcode-keyed shape, so
  inherited the same DD/FD-prefix gap.
- **V14-CPU-NIT-01** (this fix-of-reviewer): unified DD/FD-prefix walk
  closes BOTH the V14-CPU-01 family gap and the V13-CPU-01 sibling gap
  via a single change.

Post-NIT-01 invariant (re-stated):

> Any opcode that fires the IncDecZ latch in VHDL — DJNZ, INC BC,
> DEC BC, BC-block-transfers (Z80 + Z80N variants) — updates the jnext
> IncDecZ shadow with the correct polarity, regardless of DD / FD /
> DD-DD-… prefix-chain length.

## Verdict

**APPROVE.** The DD/FD-prefix walk is correct, well-commented, and
covers all opcodes whose VHDL counterpart fires the IncDecZ latch. The
six discriminative regression tests are honest. No regressions; FUSE
invariant held. No missed cases identified after the exhaustive
`IncDec_16(2..0) = "100"` enumeration.

The fix-of-reviewer work is ready to merge into the integration branch.
