# Pass-17 CPU + Z80N — Fix-Review of Reviewer's Added Fix

**Verdict**: **APPROVE**

**Branch**: `task2/verify17-cpu-z80n-im2-fix-reviewer`
**Reviewer-fix HEAD**: `4bd35e0`
**Reviewer-added-fix commit**: `d30cd77` (V17-CPU-NIT-04 — BSRA strict-UB-free shift)
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify17-cpu-z80n-im2-fix-reviewer`

## Scope

This is the **fix-review** of the single class-(c) fix the Pass-17 reviewer
added on top of the audit's findings. The reviewer caught a same-family
UB pattern the audit missed: BSRA (sibling of BSLA / BSRF) used
`int16_t >> shift`, which is C++17 implementation-defined for negative
operands. The fix, the discriminative test, and the regression suite
state are all verified below.

## Files reviewed

* `src/cpu/z80n_ext.cpp` — `BSRA_DE_B` case (lines 209-242 post-fix).
* `test/cpu/cpu_z80n_im2_regressions_test.cpp` — `test_v17_cpu_nit_04_bsra_shift_ge_16_sign_fill` (lines 2436-2522).
* `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY17-CPU-REVIEW.md` — reviewer report.

## VHDL oracle re-derivation

Authoritative spec: `t80n.vhd:1001-1014` (case `BSRA_DE_B | BSRL_DE_B | BSRF_DE_B`).

```vhdl
1003   reg_temp_t(15 downto 0) := RegsH(...) & RegsL(...);
1006   if IR(1) = '0' then        -- BSRA 0x29
1007      reg_temp_t(16) := reg_temp_t(15);
1008   else
1009      reg_temp_t(16) := IR(0);
1011   end if;
1013   reg_temp_t(16 downto 0) := std_logic_vector(shift_right(
                                       signed(reg_temp_t(16 downto 0)),
                                       to_integer(unsigned(...(4 downto 0)))));
1016   reg_direct_val_H_a <= reg_temp_t(15 downto 8);
1017   reg_direct_val_L_a <= reg_temp_t(7 downto 0);
```

For BSRA (`IR(1)=0`): bit 16 = bit 15 (sign-extend) → 17-bit value
`signed(DE[15] & DE)`. Shift count is `B[4:0] = 0..31`.

Per IEEE 1076.3 numeric_std `shift_right(signed, COUNT)`: arithmetic
right shift; for COUNT ≥ width, all bits become the sign bit. So:

* `shift = 0`: identity.
* `shift in 1..15`: arithmetic right shift; high `shift` bits filled
  with bit 15 of original DE.
* `shift in 16..31`: result bits 15:0 are entirely sign-fill — `0xFFFF`
  if `DE[15]=1`, `0x0000` else.

## C++ implementation review (`z80n_ext.cpp:209-242`)

```cpp
auto regs = cpu.get_registers();
uint8_t shift = (regs.BC >> 8) & 0x1F;
uint16_t result;
const bool sign_bit = (regs.DE & 0x8000) != 0;
if (shift == 0) {
    result = regs.DE;
} else if (shift >= 16) {
    result = sign_bit ? 0xFFFFu : 0x0000u;
} else if (sign_bit) {
    const uint16_t mask_hi = static_cast<uint16_t>(
        (0xFFFFu << (16 - shift)) & 0xFFFFu);
    result = static_cast<uint16_t>((regs.DE >> shift) | mask_hi);
} else {
    result = static_cast<uint16_t>(regs.DE >> shift);
}
regs.DE = result;
```

Branch-by-branch correctness check:

| shift | sign_bit | C++ output                              | VHDL spec |
|-------|----------|-----------------------------------------|-----------|
| 0     | any      | DE                                      | DE (identity) ✓ |
| 1..15 | 0        | `DE >> shift`                           | logical shift right ✓ |
| 1..15 | 1        | `(DE >> shift) \| ((0xFFFF << (16-shift)) & 0xFFFF)` | arithmetic shift right ✓ |
| 16..31| 0        | 0x0000                                  | sign-fill 0 ✓ |
| 16..31| 1        | 0xFFFF                                  | sign-fill 1 ✓ |

Mask formula spot-check:

* shift=1  → mask = (0xFFFFu << 15) & 0xFFFF = `0x8000`. ✓ (only top bit replicated).
* shift=8  → mask = (0xFFFFu << 8)  & 0xFFFF = `0xFF00`. ✓
* shift=15 → mask = (0xFFFFu << 1)  & 0xFFFF = `0xFFFE`. ✓

**VHDL-faithful: yes**, for all 256 inputs × 32 shift counts.

## UB-freeness audit (C++17 / C++20 strict)

* `(0xFFFFu << (16 - shift))`: `0xFFFFu` is at least 16-bit `unsigned int` → integer-promoted to (at least) `int`/`unsigned int`. Shift count `16 - shift` is in `1..15`. Both operands well-defined; result fits in 32-bit unsigned. No UB.
* `regs.DE >> shift`: `regs.DE` is `uint16_t`, integer-promoted to `int`. Shift count `shift` is `uint8_t` in `1..15`. No signed-overflow path; no shift ≥ width. No UB.
* No `int16_t >> n` for negative values.
* No left-shift of negative values.
* No shift count ≥ promoted-type width.
* All branches return well-defined values.

**UB-free: yes**, in C++17 and C++20.

## Discriminative test review

Test name: `V17-CPU-NIT-04-BSRA-UB-FREE-VHDL-1006-1014`
Location: `test/cpu/cpu_z80n_im2_regressions_test.cpp:2447-2522`.

Five behaviour-pinning cases:

| DE       | shift | expected | C++ post-fix output | VHDL output |
|----------|-------|----------|---------------------|-------------|
| 0x8000   | 16    | 0xFFFF   | 0xFFFF              | 0xFFFF ✓    |
| 0x4000   | 16    | 0x0000   | 0x0000              | 0x0000 ✓    |
| 0xC000   | 31    | 0xFFFF   | 0xFFFF              | 0xFFFF ✓    |
| 0x4000   | 14    | 0x0001   | 0x0001              | 0x0001 ✓    |
| 0xFFFE   | 1     | 0xFFFF   | 0xFFFF              | 0xFFFF ✓    |

**Discriminativity classification**: same as V17-Z80N-01a/b — these are
**behaviour-pinning** rather than **failure-detecting** on x86 GCC. The
pre-fix code (`int16_t de_signed = (int16_t)regs.DE; regs.DE = de_signed >> shift;`)
relies on integer-promotion of `int16_t` to `int` (32-bit), then GCC's
native arithmetic right shift on signed `int`, which **happens** to
match VHDL on x86. I confirmed this empirically in this review: I
reverted the fix to the pre-fix code and re-ran the test — all 5
assertions still PASS on x86 GCC -O3 (Release).

This is the **stated intent** of the reviewer (per
`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY17-CPU-REVIEW.md` lines around
"behaviour-pinning rather than failure-detecting"). Class-(c) fix's
purpose is strict-conformance and portability (clang `-fsanitize=shift`,
non-x86 targets, future C++ standards), not catching a current
observable bug. The test pins the spec so any future regression
(e.g. someone replaces the impl with a logical-only shift, or someone
breaks the sign-fill for shift ≥ 16) is caught immediately.

This is consistent with the Pass-17 reviewer's documented framework
and matches V17-Z80N-01a/b. Approved as-is.

## Sibling-instruction non-regression check

Verified the BSRA fix did not touch BSLA / BSRL / BSRF / BRLC paths.

* `BSLA_DE_B` (`z80n_ext.cpp:190-207`) — V17-Z80N-01b fix intact, uses `uint32_t v = (uint32_t)regs.DE << shift; regs.DE = v & 0xFFFF`.
* `BSRL_DE_B` (`z80n_ext.cpp:244-250`) — unchanged baseline `regs.DE >> shift` (logical right of unsigned uint16_t — no UB; spec-correct).
* `BSRF_DE_B` (`z80n_ext.cpp:252-277`) — V17-Z80N-01a fix intact, uses explicit branching with `uint16_t mask_hi` ORed for shift in 1..15, returns 0xFFFF for shift ≥ 16.
* `BRLC_DE_B` (`z80n_ext.cpp:279+`) — unchanged.

Test suite confirms: `cpu_z80n_im2_regressions_test` runs 40/40 PASS
post-fix, including `V17-Z80N-01a-BSRF-UB-FREE-VHDL-1006-1014` and
`V17-Z80N-01b-BSLA-UB-FREE-VHDL-992`.

## Test results (Release build, post-fix at HEAD `4bd35e0`)

```
ctest --test-dir build -j$(nproc)
  → 100% tests passed, 0 tests failed out of 38

./build/test/fuse_z80_test build/test/fuse
  → Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

./build/test/cpu_z80n_im2_regressions_test
  → Total:   40  Passed:   40  Failed:    0
```

## Verdict

**APPROVE**. The reviewer-added fix V17-CPU-NIT-04 is:

1. **VHDL-faithful** for all 256 × 32 = 8192 input/shift-count
   combinations (verified by branch-by-branch derivation against
   `t80n.vhd:1001-1014`).
2. **Strictly UB-free** in C++17 / C++20 — no `int16_t >> n`, no
   left-shift of negative, no signed-overflow path, no shift-by-≥-width.
3. **Test is behaviour-pinning** in the same documented sense as
   V17-Z80N-01a/b — pins all 5 spec cases; will fail loudly under any
   future regression of arithmetic-shift semantics, sign-fill cutoff,
   or shift-count masking. On x86 GCC the pre-fix code happens to give
   correct results (consistent with the Pass-17 reviewer's documented
   classification of this fix family).
4. **Non-regressing** for sibling instructions (BSLA / BSRL / BSRF /
   BRLC) — no path crosses, full Z80N + IM2 + FUSE Z80 + ctest suites
   all pass.

No additional fix needed. Ready to merge.
