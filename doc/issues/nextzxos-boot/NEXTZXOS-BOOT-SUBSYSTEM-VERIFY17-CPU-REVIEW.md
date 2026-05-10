# Pass-17 CPU + Z80N + IM2 — Independent Reviewer Report

Branch: `task2/verify17-cpu-z80n-im2-reviewer`
Audit branch reviewed: `task2/verify17-cpu-z80n-im2`
Audit HEAD: `9f92c40`
Reviewer HEAD (post-NIT fix): `d30cd77`
Date: 2026-05-10
Reviewer: blind-protocol independent (audit findings + fixes verified;
own-sweep cross-cutting bugs).

## Verdict — APPROVE-WITH-NITS

The audit's three findings (V17-CPU-01 class-(b), V17-Z80N-01a/b
class-(c)) are spec-correct and the fixes match the VHDL oracle. The
discriminative test for V17-CPU-01 genuinely FAILS pre-fix and PASSES
post-fix; tests for V17-Z80N-01a/b are "behaviour-pinning" rather than
"failure-detecting" on the current x86 GCC target (the pre-fix UB
happens to give correct results on x86), but the fixes themselves are
spec-correct and eliminate strict-conformance risk for future
compilers / architectures / sanitizer builds.

The audit missed one same-family UB issue: **BSRA** (sibling of BSLA
and BSRF) has the identical pattern — `int16_t >> shift` with
implementation-defined behaviour on negative values per C++17.
Captured as **V17-CPU-NIT-04**, fixed in commit `d30cd77` on this
review branch with a discriminative behaviour-pinning test.

## Verification of audit's findings

### V17-CPU-01 — IM2 `im2_int_req` held at 0 in pulse mode

**VHDL re-derivation**: Independently read `device/im2_peripheral.vhd:95-178`.
Confirmed:
* Line 105: `im2_reset_n <= i_mode_pulse_0_im2_1 AND NOT i_reset` —
  asserted ('1') only in IM2 mode with no reset.
* Lines 167-178: synchronous process holds `im2_int_req <= '0'`
  whenever `im2_reset_n = '0'` (i.e. while in pulse mode); otherwise
  the latch is set on `int_unq='1'` or `(int_req='1' AND int_en='1')`,
  and held by `(im2_int_req AND NOT im2_isr_serviced)` otherwise.
* Line 180: `o_int_status <= int_status OR im2_int_req`.
* Lines 154-162: `int_status` is NOT held by `im2_reset_n` — it
  persists across mode switches (fed by `(int_req or i_int_unq)` and
  cleared only by `i_int_status_clear`).

**Fix verification**: `src/cpu/im2.cpp:771-803` — the new code introduces
`im2_reset_n = im2_mode_` (correct: jnext's `Im2Controller::reset()`
runs separately to model the VHDL `i_reset` system reset), then forces
`d.im2_int_req = false` whenever `!im2_reset_n`. Otherwise applies the
original edge×int_en latch logic and the int_unq bypass per VHDL :172.
The `int_status = true` on `int_unq` is correctly preserved outside
the `im2_reset_n` gate (because VHDL int_status is NOT held by
im2_reset_n).

**Discriminative test verification**: I reverted the fix locally
(rolled `src/cpu/im2.cpp` Phase 1 back to the unconditional latch set)
and rebuilt the regression test. The new test
`V17-CPU-01-IM2-INT-REQ-HELD-IN-PULSE-MODE-VHDL-170` FAILS pre-fix
with output:

```
[FAIL] V17-CPU-01-IM2-INT-REQ-HELD-IN-PULSE-MODE-VHDL-170
       after pulse-mode tick state=0 (S_0=0); after IM2-mode tick
       state=1 (post-fix: 0=S_0; pre-fix: 1=S_REQ);
       int_line=1 (post-fix: 0; pre-fix: 1)
```

Genuine class-(b) bug, genuine discriminative test. No regression in
any other test (37/37 ctest, 1356/1356 fuse, 39/39 cpu_z80n_im2,
132/132 ctc — all pass with the fix in place).

**IM2W-07 update review**: The audit also updated
`test/ctc/ctc_test.cpp::IM2W-07` to drop+re-raise int_req across the
pulse→IM2 mode switch instead of relying on the pre-fix stale-latch
behaviour. I reviewed the new test shape against VHDL:
* In pulse mode, `im2_reset_n='0'` holds the per-device state at S_0
  AND holds the latch at 0. `int_req_d` still tracks `int_req` per
  cycle (line 92-99), so the rising edge `int_req AND NOT int_req_d`
  is consumed during the pulse-mode tick.
* On mode flip (pulse→IM2) with no fresh edge, the latch stays 0
  (because in IM2 mode the latch update is `(int_req AND int_en) OR
  int_unq`, and there's no new rising edge — `int_req_d` already
  matches `int_req`).
* Only a fresh edge in IM2 mode produces the S_REQ transition that
  the test name asserts.

The drop+re-raise pattern is the **only VHDL-faithful path** that
produces the asserted "released" semantic (S_REQ entry). The update
is correct.

### V17-Z80N-01a — BSRF strict-UB-free shift

**VHDL re-derivation**: `t80n.vhd:1001-1014` — common case for
BSRA/BSRL/BSRF; bit 16 of the 17-bit value is selected by IR(1)/IR(0):
* IR(1)=0: bit 16 = bit 15 (sign-extend) — BSRA
* IR(1)=1, IR(0)=0: bit 16 = 0 — BSRL
* IR(1)=1, IR(0)=1: bit 16 = 1 — BSRF

Then `shift_right(signed(17-bit), shift_count)` with shift_count in
B[4:0] = 0..31.

For BSRF (bit 16 = 1, treated as signed): the pre-shift 17-bit value
is `1 & DE[15:0]`, signed value = `0x10000 | DE`. Bit 16 = 1 = sign
bit (in 17-bit signed). For shift >= 16 the arithmetic shift fills
with sign = 1 → result bits 15:0 = 0xFFFF.

For shift in 1..15: result = (0x10000 | DE) shifted right `shift`
positions, taking bits 15:0 = `(DE >> shift) | (0xFFFF >> (16-shift)
<< (16-shift))` (top `shift` bits = 1 from the bit-16 sign extension).

**Fix verification**: `src/cpu/z80n_ext.cpp:226-251`. The new
implementation:
* shift==0 → DE unchanged ✓
* shift>=16 → 0xFFFF ✓
* else → `(DE >> shift) | mask_hi` where `mask_hi = (0xFFFFu << (16 -
  shift)) & 0xFFFFu` — a uint32 shift on `0xFFFFu` (well-defined for
  shift counts up to 16). I verified algebraically the mask matches
  VHDL semantics for shift in 1..15.

**Test verification**: Reverted the BSRF fix; the test
`V17-Z80N-01a-BSRF-UB-FREE-VHDL-1006-1014` STILL PASSES on x86 GCC
(the pre-fix code with `(int)x << 15 >> 15` produces correct results
by accident due to GCC's native arithmetic right shift). This is
expected: the fix is class-(c) (strict UB elimination, not a current
observable bug). The audit's claim "FAIL pre-fix, PASS post-fix" for
this test is **inaccurate on x86 GCC** — but the fix itself is
spec-correct and pins behaviour for any future compiler / target
change. No reviewer push-back; the fix is defensible.

### V17-Z80N-01b — BSLA strict-UB-free shift

**VHDL re-derivation**: `t80n.vhd:987-993` —
`shift_left(unsigned(16-bit), shift)` on a 16-bit value, with
shift_count = B[4:0]. For shift >= 16 the result is 0
(numeric_std-defined for unsigned shift past width).

**Fix verification**: `src/cpu/z80n_ext.cpp:190-207`. New code:
`uint32_t v = uint32_t(DE) << shift; DE = v & 0xFFFF`. For shift in
0..31 on a uint32 value, the shift is fully defined (shift count <
32). Result for shift >= 16 has the original bits shifted past bit 15,
masked to 0. ✓

**Test verification**: same pattern as V17-Z80N-01a — pre-fix passes
on x86, fix is class-(c) UB elimination. Defensible.

## Missed findings — REVIEWER-ADDED FIXES

### V17-CPU-NIT-04 (class-(c)) — BSRA strict-UB-free shift

**Same family as V17-Z80N-01a/b, missed by audit.**

`src/cpu/z80n_ext.cpp:209-216` (pre-fix):
```cpp
auto regs = cpu.get_registers();
uint8_t shift = (regs.BC >> 8) & 0x1F;
int16_t de_signed = static_cast<int16_t>(regs.DE);
regs.DE = static_cast<uint16_t>(de_signed >> shift);
```

`de_signed >> shift` where `de_signed` is `int16_t` (promoted to
`int` before the shift) and `shift` is in `0..31`:
* For DE positive (sign bit 15 = 0), `de_signed` is non-negative; `>>`
  is well-defined.
* For DE negative (sign bit 15 = 1), `de_signed` is negative; **C++17
  signed-rshift on negative is implementation-defined** (clarified to
  arithmetic shift in C++20). On x86 GCC -O2 the implementation
  matches the spec; on a strict-conformance build, sanitiser, or
  different target, behaviour is unspecified.

Same family as the audit's V17-Z80N-01a (BSRF) and V17-Z80N-01b
(BSLA). The audit caught two of the three Z80N shift opcodes; the
third (BSRA) has the identical pattern.

**Fix landed in commit `d30cd77`**: explicit branching with unsigned
arithmetic — `sign_bit` test, explicit shift>=16 sign-fill (0xFFFF or
0x0000 per sign), explicit mask_hi composition for sub-16 shift on
negative values. No implementation-defined behaviour possible.

**Discriminative test**:
`V17-CPU-NIT-04-BSRA-UB-FREE-VHDL-1006-1014` pins five
VHDL-faithful cases:
* neg DE=0x8000 shift=16 → 0xFFFF
* pos DE=0x4000 shift=16 → 0x0000
* neg DE=0xC000 shift=31 → 0xFFFF
* pos DE=0x4000 shift=14 → 0x0001
* neg DE=0xFFFE shift=1 → 0xFFFF

Like V17-Z80N-01a/b, this test PASSES on the pre-fix code on x86 GCC
(arithmetic shift behaviour by accident); the fix's value is
strict-conformance / portability / sanitiser-clean.

## Independent sweep — no other findings

I conducted an own-sweep across the CPU subsystem looking for:

### Other Z80N opcodes with shift / arithmetic UB

* **SWAPNIB** (`(a & 0x0F) << 4`) — uint8_t promoted to int, max
  result 240. Fully defined. ✓
* **MIRROR_A** (`r << 1`) — uint8_t promoted to int, max 254. ✓
* **TEST_N** — bitwise AND only. ✓
* **BSRL_DE_B** (`regs.DE >> shift`) — uint16_t promoted to int (32
  bits), shift 0..31. Always non-negative (DE is in low 16 bits of
  promoted int). Fully defined. ✓
* **BRLC_DE_B** (`(regs.DE << rot) | (regs.DE >> (16 - rot))`) — rot
  is masked to 0..15 inside the rot!=0 branch. `DE << 15` max =
  0x7FFF8000 (sign bit clear). `DE >> 1..16` on uint16-promoted-int:
  always non-negative. ✓ (Special case rot==0 short-circuits; rot==16
  reduces to (DE << 0) | (DE >> 16) = DE | 0 = DE — well-defined.) ✓
* **MUL_DE** — `(uint16_t)d * (uint16_t)e` — both ≤ 255 after the
  high/low byte split, max product = 65025. ✓
* **ADD_HL_A / ADD_DE_A / ADD_BC_A** — `regs.XX + a` mod 0x10000. ✓
* **ADD_HL_NN / ADD_DE_NN / ADD_BC_NN** — same shape. ✓
* **PIXELDN / PIXELAD / SETAE / JP_C / NEXTREG_NN / NEXTREG_A /
  PUSH_NN / OUTINB / LDIX / LDWS / LDDX / LDIRX / LDDRX / LDPIRX /
  LDIRSCALE** — no shift / overflow concerns. ✓

### IM2 controller — additional surfaces

* **`raise_unq()`** sets `dv.im2_int_req = true` directly, bypassing
  the `im2_reset_n` gate added in V17-CPU-01. This is observationally
  benign because:
  (1) `step_state_machine_with_iei` forces `state = S_0` in pulse
      mode regardless of `im2_int_req`, and
  (2) the next `tick()`'s `step_devices()` Phase 1 (post-V17 fix)
      flips `im2_int_req = false` while still in pulse mode.
  Any caller observing `im2_int_req` between `raise_unq()` and the
  next tick gets a non-VHDL-faithful "true" — but the ONLY consumer
  is `int_status()` which composites `int_status OR im2_int_req`, and
  `raise_unq()` also sets `int_status = true`, so the composite
  result is identical to VHDL (`int_status` is not held by
  `im2_reset_n`). No observable bug. Not a finding.
* **`int_line_asserted()`** correctly gates on `im2_mode_` (line
  512). ✓
* **`ack_vector()`** correctly gates on `im2_mode_` (line 539). ✓
* **`compute_vector()`** correctly composes from
  `vector_base_msb3_ << 5 | idx << 1`. ✓
* **`device_ieo()`** correctly walks the chain per VHDL :136-146. ✓
* **`set_machine_timing_48_or_p3` save/load** is correctly persisted
  in `Im2Controller::save_state` (line 1141) and restored at line
  1179. ✓ (Audit's confirmation that emulator.cpp load_state does NOT
  re-fan-out to im2 is correct — only cpu_ needs the re-fan-out.)
* **NR 0xC0 shadow re-push** — all three fields (vector_base,
  stackless_nmi, im2_mode) are persisted in IM2 save_state and
  restored in load_state. No load_state re-push gap (the V12-MEM-02
  / V16-CPU-01 family is closed for IM2). ✓

### Z80 CPU register save/load — shadow re-push

* All Z80 register fields persisted (AF, BC, DE, HL, AF2, BC2, DE2,
  HL2, IX, IY, SP, PC, I, R, IFF1, IFF2, IM, halted, MEMPTR, Q,
  interrupts_enabled_at, iff2_read). ✓
* IncDecZ shadow intentionally NOT persisted — documented in
  z80_cpu.cpp:941-949 with rationale (snapshot format compatibility,
  worst-case observation = single LDWS reading P=0 instead of prior
  value, immediately resynced by next BC-dec or DJNZ). Defensible
  class-(c) trade-off. Not a missed finding.
* `nmi_pending_`, `int_pending_`, `int_vector_`, `int_requested_at_`
  all persisted. ✓

### NMI / INT / HALT interaction

* NMI on HALT bumps PC by +1 in fuse_z80_nmi (matches VHDL). ✓
* INT pulse window correctly sized 32T for 48K/+3, 36T otherwise per
  zxnext.vhd:2033. ✓
* EI-grace gate before IntAck callback was already fixed in Pass-8;
  V17 doesn't regress it. ✓
* Stackless NMI (V15-CPU-NIT-02) — already-known class-(d). Not
  fixed. ✓ (per task brief)
* IM2 daisy-chain bridge to Z80 INT line (V13-CPU-D1) — already-known
  class-(d). Not fixed. ✓ (per task brief)
* DD-ED-Z80N + Alternate routing (V15-CPU-NIT-01) — already-known
  class-(d). Not fixed. ✓ (per task brief)

### R register / MEMPTR / Q

* R increment by 2 for ED-prefixed Z80N opcodes (line 586) matches
  VHDL: each M1 increments R by 1. ✓
* MEMPTR (WZ) updates correctly in ADD_*_NN cases (Pass-4 fix) and
  PUSH_NN (Pass-8 fix). ✓
* Q register correctly cleared at Z80N dispatch (line 619), F-writing
  Z80N opcodes set Q=F at end. ✓
* iff2_read cleared at Z80N dispatch (line 620). ✓

### Cross-cutting families (per task brief)

* **load_state shadow re-push**: V12-MEM-02 / V12-MEM-03 /
  V15-CPU-NIT-03 / V16-CPU-01 family — checked emulator.cpp load_state
  re-pushes. The post-V16 + V12 set covers cpu_speed (NR 0x07),
  contention_disable (NR 0x08 b6), port_7ffd_io_en (NR 0x82 b1),
  port_ulap_io_en (NR 0x85 b0), divmmc rom3_active. No additional
  gaps in the IM2 / CPU / Z80N path. ✓
* **IncDecZ shadow polarity** (V13-CPU-01 / V14-CPU-01 /
  V14-CPU-NIT-01) — all paths covered (LDWS reads via shadow; INC/DEC
  BC + DJNZ + DD/FD-prefixed variants update). ✓
* **WO-NR readback cache leak** (V14/V15-NMP family) — out of scope
  for CPU subsystem (NMI/MF/Port). ✓
* **SD past-EOF token** (V12/V14-DIVMMC family) — out of scope. ✓

## Tests / Build

Build:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
```
Built without warnings introduced by this review.

Test results post-review (with V17-CPU-NIT-04 fix in place):
* `ctest --test-dir build -j$(nproc)`: **38/38 passed** (no failures, no skips).
* `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 passed**.
* `./build/test/cpu_z80n_im2_regressions_test`: **40/40 passed** (3 new V17 + 1 reviewer-added V17-NIT + 36 pre-existing).
* `./build/test/ctc_test`: **132/132 passed** (IM2W-07 updated by audit, VHDL-faithful).

## Summary

| Class | Audit findings | Reviewer-added |
|-------|----------------|----------------|
| (a)   | 0              | 0              |
| (b)   | 1 (V17-CPU-01) | 0              |
| (c)   | 2 (V17-Z80N-01a/b) | 1 (V17-CPU-NIT-04) |
| (d)   | 0 (3 catalogued, all pre-existing) | 0 |
| **Total** | **3 fixed** | **1 fixed** |

**Verdict: APPROVE-WITH-NITS**. Audit fixes are spec-correct and
verified. One same-family missed finding (BSRA UB) found and fixed
inline by reviewer. No further class-(a/b/c) findings; the three
known class-(d) items remain as catalogued.
