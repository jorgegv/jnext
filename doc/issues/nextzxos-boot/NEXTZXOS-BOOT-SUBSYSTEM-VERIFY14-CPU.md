# Pass-14 (CPU/Z80N/IM2) — Audit Report

- Worktree: `.claude/worktrees/task2-verify14-cpu-z80n-im2`
- Branch: `task2/verify14-cpu-z80n-im2` (off integration HEAD `73c3146`)
- Build: Release, `-DENABLE_QT_UI=ON`
- Methodology: VHDL (`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`) as oracle for FPGA bits;
  FUSE Z80 (`third_party/fuse-z80/`) as oracle for base-Z80 instruction semantics.
- Blind audit: prior pass reports (Pass-1..Pass-13) NOT consulted.

## Findings summary

| ID              | Class | Status          | Subsystem | One-line |
|-----------------|-------|-----------------|-----------|----------|
| V14-CPU-01      | (a)   | FIX + TEST      | CPU base  | `INC BC` (0x03) and `DEC BC` (0x0B) must update the `IncDecZ` shadow latch (VHDL `t80n.vhd:1361-1367`) |
| V14-CPU-NIT-01  | (a)   | FIX-OF-REVIEWER | CPU base  | DD/FD-prefixed `INC BC` / `DEC BC` / `DJNZ` skip the IncDecZ latch update (reviewer-promoted; sibling of V13-CPU-01 prefix gap) |

Counts: **1 finding + 1 reviewer-promoted NIT (2 class-(a), 0 class-(b), 0 class-(c), 0 class-(d))**.

---

## V14-CPU-01 — `INC BC` / `DEC BC` do not update `IncDecZ`

### Class

(a) — class-(a) discriminative bug. Behaviour observable by any
program that places `INC BC` or `DEC BC` (without an intervening
DJNZ or BC-decrementing block transfer) before a `LDWS` (Z80N
`ED A5`) read of the F.P override path.

### VHDL oracle

`t80n.vhd:1361-1367`:

```vhdl
if (TState = 2 or (TState = 3 and MCycle = "001")) and IncDec_16(2 downto 0) = "100" then
   if ID16 = 0 then
      IncDecZ <= '0';
   else
      IncDecZ <= '1';
   end if;
end if;
```

i.e. on every 16-bit BC inc/dec cycle (`IncDec_16(2 downto 0) = "100"`
matches BC pair, `DPair = "00"`, both inc-dir `"01..."` and dec-dir
`"11..."` variants), the shadow latch `IncDecZ` is written from the
post-update bus value `ID16`:

* `ID16 = 0` (post-inc/dec BC is zero) → `IncDecZ ← 0`
* otherwise → `IncDecZ ← 1`

The mcode source for those updates is `t80n_mcode.vhd:927-936`:

```vhdl
when "00000011"|"00010011"|"00100011"|"00110011" =>
   -- INC ss
   IncDec_16(3 downto 2) <= "01";
   IncDec_16(1 downto 0) <= DPair;
when "00001011"|"00011011"|"00101011"|"00111011" =>
   -- DEC ss
   IncDec_16(3 downto 2) <= "11";
   IncDec_16(1 downto 0) <= DPair;
```

For BC, `DPair = "00"`, so `INC BC` (0x03) sets `IncDec_16 = "0100"` and
`DEC BC` (0x0B) sets `IncDec_16 = "1100"`. Both satisfy
`IncDec_16(2 downto 0) = "100"` and trigger the latch. INC/DEC of
DE/HL/SP set `DPair` to "01"/"10"/"11", which produce `IncDec_16(2..0)`
of "101"/"110"/"111" — silent on the latch.

The same latch site is shared with all BC-decrementing block transfers
(LDI/LDD/CPI/CPD/LDIR/LDDR/CPIR/CPDR + Z80N
LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE), which assert `IncDec_16 = "1100"`
internally — the same `(2..0) = "100"` mask. So **the polarity** of the
latch is consistent: `IncDecZ = '1'` ↔ `ID16 ≠ 0`, regardless of whether
the source is INC BC, DEC BC, or any BC-block-transfer mcycle.

`IncDecZ` feeds `F(Flag_P)` via `t80n.vhd:1284` for the `I_BT` / `I_BC`
override paths — most observably **LDWS** (Z80N `ED A5`), where the
spec says "preserve P" but the VHDL emits IncDecZ from the most recent
BC inc/dec or DJNZ.

### jnext bug (pre-fix)

`src/cpu/z80_cpu.cpp::execute()` updates `regs_.IncDecZ` only on:

1. DJNZ (0x10) — using `F_Out(Flag_Z)` polarity (V13-CPU-01 fix).
2. ED A0/A8/B0/B8 + ED A1/A9/B1/B9 (LDI/LDD/LDIR/LDDR + CPI/CPD/CPIR/CPDR).
3. DD/FD-prefixed ED-block transfers (the 0xDD/0xFD walk-the-prefix-chain
   code path).

Plain `INC BC` (0x03) and `DEC BC` (0x0B) executed by FUSE's
`fuse_z80_execute_one()` mutate BC via `BC++` / `BC--` but never
update the jnext-side `regs_.IncDecZ` shadow. A subsequent **LDWS**
inside `execute_z80n()` then reads a stale `regs.IncDecZ` from the
last DJNZ / block-transfer / reset baseline.

Concrete divergence scenario:

```
B = 1                 ; load
DJNZ +d (not taken)   ; B → 0 → IncDecZ ← 1 (V13 polarity)
LD BC,$FFFF           ; (no IncDecZ update — direct LD)
INC BC                ; BC → $0000 — VHDL: IncDecZ should latch to 0
                      ; jnext (pre-V14): IncDecZ stays 1 (stale)
LDWS                  ; F.P override = IncDecZ
                      ; VHDL: F.P = 0
                      ; jnext (pre-V14): F.P = 1 ← divergence
```

A program using INC BC for re-arming a per-row pattern + LDWS for the
copy step would observe wrong P-flag composition (and therefore wrong
JP PE/PO/JR PE/PO branch outcomes) on every row.

### Fix

`src/cpu/z80_cpu.cpp::execute()`:

* Pre-execute classification (alongside the existing DJNZ + `ed_block_xfer`
  detection): `is_inc_bc = (opcode == 0x03)`, `is_dec_bc = (opcode == 0x0B)`.
* Post-`fuse_z80_execute_one` tail update:
  * If `is_inc_bc || is_dec_bc`: `regs_.IncDecZ = (regs_.BC != 0) ? 1u : 0u`.

Polarity is the same as the existing BC-block-transfer update: VHDL emits
`'1'` when `ID16 ≠ 0`, `'0'` when zero. This matches the existing site at
line 677 (`(ext == 0xA0) || ... ? 1u : 0u` style). The DJNZ polarity
remains inverted (V13-CPU-01) because the VHDL DJNZ latch site at
`t80n.vhd:1359` writes `F_Out(Flag_Z)` directly, which is asserted on a
zero result.

### Discriminative regression tests

`test/cpu/cpu_z80n_im2_regressions_test.cpp`:

1. **`V14-CPU-01-INC-BC-UPDATES-INCDECZ-VHDL-1361`** —
   primes `IncDecZ=1` via DJNZ (B=1→0, V13 polarity), then runs
   `LD BC,$FFFF; INC BC` (BC: $FFFF → $0000). Asserts IncDecZ flipped
   to 0 and LDWS F.P = 0. Pre-fix: IncDecZ stays 1, LDWS F.P = 1.
   *Verified by stash-revert + rebuild + re-run: pre-fix [FAIL],
   post-fix [PASS].*

2. **`V14-CPU-01-DEC-BC-UPDATES-INCDECZ-VHDL-1361`** —
   reset baseline (IncDecZ=0), then `DEC BC` with BC=$0002 (post-dec
   BC=$0001 nonzero). Asserts IncDecZ flipped to 1 and LDWS F.P = 1.
   Pre-fix: IncDecZ stays 0 (stale), LDWS F.P = 0.
   *Verified by stash-revert: pre-fix [FAIL], post-fix [PASS].*

3. **`V14-CPU-01-INC-HL-MUST-NOT-UPDATE-INCDECZ`** — negative-invariant:
   prime IncDecZ=1, then `LD HL,$FFFF; INC HL` (HL: $FFFF → $0000 — same
   condition that would flip IncDecZ if HL were on the latch). Asserts
   IncDecZ remains 1 and LDWS F.P = 1. A buggy "fix the latch on
   any 16-bit inc/dec" would flip IncDecZ here; the test forbids that.
   *Both pre-fix and post-fix [PASS], confirming the VHDL gate
   `IncDec_16(2..0) = "100"` is BC-only.*

The pair (1, 2) covers both branches of the latch (zero and nonzero
result). Together with (3) they pin down the BC-pair-only gate.

### Tests / regressions

* `cmake --build build -j$(nproc)`: clean.
* `ctest --test-dir build`: 38/38 PASS.
* `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS.
* `./build/test/cpu_z80n_im2_regressions_test`: 30/30 PASS (was 27 pre-V14;
  +3 new V14-CPU-01 tests).

### Files touched

* `src/cpu/z80_cpu.cpp` — pre-execute classification + post-execute IncDecZ
  update for INC BC / DEC BC.
* `test/cpu/cpu_z80n_im2_regressions_test.cpp` — three new tests + main()
  wire-up.

---

## V14-CPU-NIT-01 — DD/FD-prefixed `INC BC` / `DEC BC` / `DJNZ` skip the IncDecZ latch update

### Class

(a) — class-(a) discriminative bug, surfaced by the independent reviewer
on top of V14-CPU-01 (and the pre-existing V13-CPU-01 family). The
prior V14-CPU-01 fix keyed its post-execute IncDecZ classification on
the **entry M1 byte** (`opcode == 0x03 / 0x0B`). DD/FD-prefixed forms
(DD 03, FD 03, DD 0B, FD 0B) and the V13-CPU-01 sibling DD/FD-prefixed
DJNZ (DD 10, FD 10) executed the SAME mcode path per VHDL but slipped
the gate.

### VHDL oracle

`t80n.vhd:513-531` — after a DD/FD prefix, ISet stays at "00" (only
XY_State updates to "01"/"10"); the parametric mcode dispatch keys on
`IRB` (= the inner opcode), and `DPair := IR(5 downto 4)` (line 228).

For inner opcode 0x03 / 0x0B (INC BC / DEC BC), DPair="00" →
IncDec_16="0100"/"1100" → low-3="100" → latch fires per
`t80n.vhd:1361-1367` (the V14-CPU-01 site).

For inner opcode 0x10 (DJNZ), the DJNZ latch at `t80n.vhd:1358-1360`
fires unconditionally (no XY_State gating); polarity is V13-CPU-01's
inverted form (`F_Out(Flag_Z)`).

### jnext bug (post-V14-CPU-01)

`src/cpu/z80_cpu.cpp::execute()` originally set:

```cpp
const bool is_djnz   = (opcode == 0x10);
const bool is_inc_bc = (opcode == 0x03);
const bool is_dec_bc = (opcode == 0x0B);
```

`opcode` is the M1 byte at PC. For DD-prefixed forms, that's 0xDD, so
the gates skipped; the same for 0xFD. The reviewer's scratch tests
confirmed concretely:

```
SCRATCH-DD-INC-BC-INCDECZ-VHDL-1361  FAIL  IncDecZ stayed 1 (should 0)
SCRATCH-DD-DEC-BC-INCDECZ-VHDL-1361  FAIL  IncDecZ stayed 0 (should 1)
```

### Fix

Walk the DD/FD prefix chain (same shape as the existing ED-block-xfer
walk) **once**, capturing the first non-prefix byte as `inner_opcode`,
and rebase ALL four classifications on it:

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
        inner_opcode = b;
        if (b == 0xED) {
            uint8_t ext = mem_.read(
                static_cast<uint16_t>((walk_pc + 1) & 0xFFFF));
            if (/* LDI/LDD/LDIR/LDDR/CPI/CPD/CPIR/CPDR */) {
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

FUSE Z80's `z80_ddfd.c:556-565` default branch backs PC up and re-enters
the main switch on any non-IX/IY opcode — so DD 03 mutates BC just like
plain 0x03, DD 10 mutates B (and PC, when taken) just like plain 0x10.
We can therefore observe the post-state identically and apply the same
IncDecZ polarity for both prefix and plain forms.

The fix also closes the V13-CPU-01 DD/FD-prefix gap (DD DJNZ, FD DJNZ)
as a side effect; its scratch reproduction was carried over into
explicit V14-CPU-NIT-01-E/F regression tests.

### Discriminative regression tests

`test/cpu/cpu_z80n_im2_regressions_test.cpp`:

1. **`V14-CPU-NIT-01-A-DD-INC-BC-UPDATES-INCDECZ-VHDL-1361`** — primes
   IncDecZ=1 via DJNZ-not-taken, then `LD BC,$FFFF; DD INC BC` (BC:
   $FFFF → $0000). Asserts IncDecZ flipped to 0 and LDWS F.P=0. Pre-fix:
   IncDecZ stays 1, LDWS F.P=1.
2. **`V14-CPU-NIT-01-B-FD-INC-BC-UPDATES-INCDECZ-VHDL-1361`** — same
   shape with FD prefix.
3. **`V14-CPU-NIT-01-C-DD-DEC-BC-UPDATES-INCDECZ-VHDL-1361`** — reset
   baseline (IncDecZ=0), then `DD DEC BC` with BC=$0002 (post-dec
   $0001 nonzero). Asserts IncDecZ=1, LDWS F.P=1. Pre-fix: stale 0.
4. **`V14-CPU-NIT-01-D-FD-DEC-BC-UPDATES-INCDECZ-VHDL-1361`** — FD form
   of (3).
5. **`V14-CPU-NIT-01-E-DD-DJNZ-UPDATES-INCDECZ-VHDL-1359`** —
   sibling-of-V13-CPU-01 closure: B=1 → DD DJNZ not-taken → V13
   polarity sets IncDecZ=1 → LDWS F.P=1. Pre-fix: stale 0.
6. **`V14-CPU-NIT-01-F-FD-DJNZ-UPDATES-INCDECZ-VHDL-1359`** — FD form
   of (5).

All 6 verified by stash-revert + rebuild + re-run protocol: pre-fix
[FAIL] x6, post-fix [PASS] x6.

### Tests / regressions

* `cmake --build build -j$(nproc)`: clean.
* `ctest --test-dir build`: 38/38 PASS.
* `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS (FUSE
  invariant held — DD/FD-prefixed inner-opcode regressions did not
  touch any pre-existing FUSE expectations).
* `./build/test/cpu_z80n_im2_regressions_test`: 36/36 PASS (was 30
  pre-NIT; +6 new V14-CPU-NIT-01 tests).

### Files touched

* `src/cpu/z80_cpu.cpp` — replaced opcode-keyed `is_djnz / is_inc_bc /
  is_dec_bc` classification with `inner_opcode`-keyed classification
  fed by the unified DD/FD prefix-chain walk; updated the surrounding
  comment block. Polarity per-branch unchanged.
* `test/cpu/cpu_z80n_im2_regressions_test.cpp` — six new tests + main()
  wire-up.
* `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY14-CPU.md` —
  this entry.

### Cross-finding linkage

* **V13-CPU-01** (Pass-13): closed the DJNZ polarity bug for plain DJNZ
  (0x10). The same fix's classification was also opcode-keyed, so DD/FD
  DJNZ remained broken. V14-CPU-NIT-01 closes that gap.
* **V14-CPU-01** (Pass-14): closed the latch-coverage gap for plain
  INC/DEC BC. Same opcode-keyed pattern carried the prefix gap forward;
  V14-CPU-NIT-01 closes it.

The post-NIT-01 invariant: any opcode that fires the IncDecZ latch in
VHDL (per `t80n.vhd:1358-1366`) — DJNZ, INC BC, DEC BC, BC-block-
transfers (Z80 + Z80N variants) — updates the jnext IncDecZ shadow with
the correct polarity, regardless of DD / FD / DD-DD-… prefix-chain
length.

---

## Convergence note

Pass-14 did **not** reach defensible-zero. One class-(a) finding was
identified, fixed, and tested. The Pass-13 V13-CPU-01 finding had
already established that the IncDecZ shadow latch's polarity was
inverted on the DJNZ leg; the V14 audit re-walked that area and found
that the *coverage* of the latch was also incomplete — plain `INC BC`
and `DEC BC` were missed entirely. The two findings together (V13 +
V14) now cover the full set of `IncDec_16(2..0) = "100"` triggers in
the VHDL: DJNZ (inverted polarity), BC-block-transfers (already
covered), and `INC/DEC BC` (V14 fix).

Other audit angles surveyed and confirmed clean:

* Z80N opcode coverage (`Z80NOpcode` enum) matches `t80n_mcode.vhd`'s
  ED 23/24/27/28-2C/30-36/8A/90-95/98/A4-A5/AC/B4-B7/BC. Commented-out
  VHDL slots (25, 26, 2D, 2E, 2F, 37-3B, 3C-3F) correctly remain NOPs
  in the FUSE-base path.
* `ADD HL,A` / `ADD DE,A` / `ADD BC,A` F.C unconditional clear (Pass-10
  fix at z80n_ext.cpp:247-296) correctly tracks `t80n.vhd:778-783` —
  `reg_temp_t(15 downto 0) := unsigned(...)` truncates the 16-bit add at
  bit 15; bit 16 is pre-zeroed by `reg_temp_t := (others=>'0')` at line
  698 and never written. F.C is therefore unconditionally 0.
* `OUTINB` extended-M1 contention via `contend_read_no_mreq(IR, 1)`
  (V12-CPU-NIT-02 fix) traverses the contention gate correctly per
  `zxula.vhd:582-600`.
* `EI`-grace gate at z80_cpu.cpp:479-512 correctly replicates FUSE's
  `tstates == interrupts_enabled_at` semantic and avoids advancing the
  IM2 fabric to S_ACK on a rejected IntAck cycle (Pass-8 fix).
* DD/FD/CB inner-byte M1 delivery via the prefix-walking loop at
  z80_cpu.cpp:729-764 correctly fires `on_m1_cycle` for every prefix
  byte plus the inner ED-byte (when present) — matches the IM2
  RETI/RETN decoder's per-T4-edge model in `im2_control.vhd:158-209`.
* `IM2` decoder DDFD→ED bytes do NOT register as RETI (V11-CPU-01 fix at
  im2.cpp::advance_decoder S_DDFD_T4 case) — VHDL falls through to S_0
  on any non-DDFD opcode.
* `Q` / `iff2_read` hygiene at Z80N dispatch (line 619-620 in z80_cpu.cpp)
  correctly mirrors FUSE's `Q = 0; iff2_read = 0` reset at every opcode
  boundary in `fuse_z80_core.c:194,207`.
* `R` register increment for Z80N opcodes (`z80.r = (z80.r + 2) & 0x7F`
  at line 586 of z80_cpu.cpp) correctly accounts for the ED prefix M1 +
  ext byte M1 (= 2 R bumps per Z80N).
* LDIR/LDDR/CPIR/CPDR INT-checkpoint mid-instruction handled by FUSE's
  PC-rewind-and-retry shape; same shape replicated in the Z80N block-
  transfer cases at z80n_ext.cpp:LDIRX/LDDRX/LDPIRX/LDIRSCALE.
* All `IncDec_16` writes in `t80n_mcode.vhd` audited against jnext's
  IncDecZ-update sites — V14-CPU-01 is the sole gap.

The audit team should treat Pass-14 as **converged-with-one-finding**;
Pass-15 (if any) should re-walk the BC-shadow-latch ↔ I_BT/I_BC override
loop end-to-end after the V14 fix lands on integration.
