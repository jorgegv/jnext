# Pass-13 CPU Subsystem Audit — Independent Review

**Audit branch**: `task2/verify13-cpu-z80n-im2` HEAD `a629b86`
**Reviewer branch**: `task2/verify13-cpu-z80n-im2-reviewer`
**Reviewer worktree**: `.claude/worktrees/task2-verify13-cpu-z80n-im2-reviewer`
**Methodology**: VHDL oracle (`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`) + FUSE oracle (`third_party/fuse-z80/`); no probes; no prior pass reports consulted.

## Verdict: APPROVE

Both V13-CPU-01 (class-(a) production fix) and the related test rewrite +
new discriminative test are **VHDL-faithful and well-grounded**. V13-CPU-D1
class-(d) escalation is **genuinely architectural** and correctly listed
without attempting an in-pass fix.

## Findings audited

### V13-CPU-01 — DJNZ IncDecZ shadow polarity inverted (class-(a))

**VHDL claim verification**: Confirmed.

- `t80n.vhd:1358-1360` (DJNZ latch site):
  ```vhdl
  if I_DJNZ = '1' and Save_ALU_r = '1' and Mode < 2 then
     IncDecZ <= F_Out(Flag_Z);
  end if;
  ```
  Stores the F.Z flag of the SUB-1 ALU operation on B. F.Z=1 when result
  is zero, i.e. when B was 1 entering DJNZ → IncDecZ=1 then.
  **Zero-meaning convention.**

- `t80n.vhd:1361-1366` (BC-dec / I_BC latch site):
  ```vhdl
  if (TState = 2 or (TState = 3 and MCycle = "001"))
     and IncDec_16(2 downto 0) = "100" then
     if ID16 = 0 then
        IncDecZ <= '0';
     else
        IncDecZ <= '1';
     end if;
  end if;
  ```
  Stores '1' when post-decrement BC is **non-zero**. **Nonzero-meaning
  convention.**

- `t80n.vhd:1283-1284`:
  ```vhdl
  if I_BC = '1' or I_BT = '1' then
     F(Flag_P) <= IncDecZ;
  end if;
  ```
  Straight copy from IncDecZ to F.P. The two latch sites pre-bias
  IncDecZ such that the straight copy emits the correct F.P regardless
  of which path latched it.

- `t80n_mcode.vhd:2141, 2171` (LDWS X"A5", MCycle 3): I_BT='1'. Confirms
  LDWS reads IncDecZ for F.P composition — discriminative coupling for
  DJNZ→LDWS.

- `t80n_mcode.vhd:1140-1144` (DJNZ MCycle 1): `ALU_Op="0010"` (SUB),
  `BusA=B(000)`, `BusB="00000001"`, `Save_ALU='1'`. Confirms B-1 is the
  SUB result and F.Z is what the latch reads.

**Production fix correctness**: Confirmed.

`src/cpu/z80_cpu.cpp:828` post-fix:
```cpp
regs_.IncDecZ = (((regs_.BC >> 8) & 0xFF) == 0) ? 1u : 0u;
```
Maps `B == 0` (post-decrement) → IncDecZ=1, which matches F.Z(B-1)=1
(zero-meaning convention from VHDL :1359). Pre-fix `(B != 0) ? 1 : 0`
applied the BC-dec polarity to the DJNZ branch — confirmed inverted.

**No regression on the BC-dec sites**: All 8 writers of `IncDecZ` audited.
The DJNZ site (z80_cpu.cpp:828) is the ONLY one using zero-meaning
convention; the 7 other writers (1 reset at :404 + 1 base ED block
transfer at :677 + 1 DD-prefixed ED block transfer at :835 + 6 Z80N
LDIX/LDIRX/LDDX/LDDRX/LDPIRX/LDIRSCALE writers in z80n_ext.cpp) all
correctly use `(regs.BC != 0) ? 1 : 0` matching the VHDL nonzero-meaning
latch at :1361-1366. No inverted-polarity sites remain.

**Read sites**: F.P composition reads `IncDecZ` straight (no inversion)
in:
- z80n_ext.cpp:670 (LDWS): `if (regs.IncDecZ) f |= FLAG_P;`
- z80n_ext.cpp:879 (LDPIRX): `if (regs.IncDecZ) f |= FLAG_P;`
This is correct — VHDL :1284 is also a straight copy. Both writers (DJNZ
zero-meaning, BC-dec nonzero-meaning) pre-bias IncDecZ to the value to
emit, so the read site applies neither inversion.

### Original-test re-verification (CRITICAL angle)

The audit report claims that `test_pass9_ldws_incdecz_after_djnz`
(introduced in commit `a858a07`, the testcov retroactive wave) ENCODED
the bug. I read the original commit and confirm:

The original test layout: B=2 entering DJNZ → branch taken → B=1 after
exec. Asserted:
```cpp
bool incdecz_set = after_djnz.IncDecZ == 1;       // <-- WRONG per VHDL
bool b_after_one = ((after_djnz.BC >> 8) & 0xFF) == 1;
...
bool p_set = (f & 0x04) != 0;                     // F.P should be 1
...
check(res, "Z80N-LDWS-INCDECZ-FROM-DJNZ (b40af13)",
      ... && incdecz_set && p_set, detail);
```

Detail printf included `"IncDecZ=%d (want 1)"` and `"LDWS ... F.P=%d
(expect 1 per IncDecZ shadow)"`. The inline comment said *"DJNZ — B:2→1,
IncDecZ=1 (per jnext convention), branch taken"*. The parenthetical
**"(per jnext convention)"** is the smoking gun: the test author
acknowledged they were matching jnext's emitted behavior, NOT the VHDL
zero-meaning semantic. The VHDL citation in the test header ("Pass-9
fix: VHDL t80n.vhd:1358-1367") was for the *existence* of the shadow,
not its polarity — and the polarity claim "want 1" with B=2→1 is
incompatible with VHDL :1359 `IncDecZ <= F_Out(Flag_Z)` (F.Z(1)=0).

**Conclusion**: the original test was matching buggy emulator output
(coverage theatre — Pass-9 fix introduced the shadow with the wrong
polarity, and the testcov wave at `a858a07` enshrined that polarity as
"correct"). The V13 rewrite is justified — the original was NOT a
defensible-but-mistaken VHDL reading; it was direct enshrinement of
emulator output.

Note: the rewritten test name preserves the trail
(`V13-CPU-01-Z80N-LDWS-INCDECZ-FROM-DJNZ-TAKEN (was Pass-9 b40af13)`),
so audit history is not erased.

### Discriminative revert-check

I reverted the V13 production fix in this reviewer worktree (z80_cpu.cpp:828
back to `(B != 0) ? 1 : 0`), rebuilt Release, and ran the regression
tests:

```
[FAIL] V13-CPU-01-Z80N-LDWS-INCDECZ-FROM-DJNZ-TAKEN
       ... IncDecZ=1 (V13 expect 0 — VHDL F.Z(B-1)=F.Z(1)=0);
       LDWS F=0x04 F.P=1 (V13 expect 0; pre-fix would be 1)
[FAIL] V13-CPU-01-Z80N-LDWS-INCDECZ-FROM-DJNZ-NOT-TAKEN
       ... IncDecZ=0 (V13 expect 1 — VHDL F.Z(B-1)=F.Z(0)=1);
       LDWS F=0x00 F.P=0 (V13 expect 1; pre-fix would be 0)
Total:   27  Passed:   25  Failed:    2
```

Both V13 tests fail simultaneously when production code is reverted —
exactly as the audit claimed. Restoring the fix → 27/27 pass. **The
two tests are jointly discriminative and pin both polarity sides of
the inverted shadow.**

### V13-CPU-D1 — IM2 mode does not drive CPU INT line (class-(d))

`Im2Controller::int_line_asserted()` is declared at `im2.h:122`,
defined at `im2.cpp:505-519`. Caller search:
```
src/cpu/im2.h:122            (declaration)
src/cpu/im2.cpp:505          (definition)
src/cpu/im2.cpp:870          (comment only)
src/core/emulator.cpp:4872   (comment only)
```
Zero callers. Confirmed.

ULA-int and line-int paths in `emulator.cpp` (lines 4880-4881 and
5965-5966) both gate `cpu_.request_interrupt(0xFF)` on
`!im2_.is_im2_mode()`. When NR 0xC0 bit 0 sets HW IM2 mode,
`raise_req` correctly latches device state, but `cpu_.request_interrupt`
is never called, so `Z80Cpu::execute()` never enters its INT-acceptance
branch and `on_int_ack` is never wired to `ack_vector`.

**Architectural scope confirmed**: wiring the bridge requires
- per-tick or per-event polling of `int_line_asserted()` in
  `Emulator::run_frame`,
- vector capture path: `compute_vector()` → `cpu_.request_interrupt(...)`
  with proper IM2 vector composition (`nr_c0_im2_vector(2:0) &
  im2_vec(3:0) & '0'`),
- `on_int_ack` callback wiring to `Im2Controller::ack_vector()` to
  walk the daisy chain priority and update fabric state on accept,
- audit of all 14 peripherals (CTC/UART/Md6/etc.) currently relying on
  the legacy pulse path,
- decision on EI grace gate interaction (Pass-8 fix interaction).

This is a multi-call-site change in `emulator.cpp` plus cross-subsystem
peripheral audit. Genuinely architectural. NextZXOS boot does not
exercise HW IM2 mode (stays in pulse/IM1), so this gap does not block
the broader Task 2 boot regression target.

**class-(d) listing accepted as-is.**

## Test execution

Reviewer worktree, Release build, all green:

```
ctest --test-dir build --output-on-failure  →  100% (38/38) PASS
./build/test/fuse_z80_test build/test/fuse  →  1356/1356 PASS, 0 FAIL
./build/test/cpu_z80n_im2_regressions_test  →  27/27 PASS
```

Discriminative revert-check (V13-CPU-01 production reverted) → 2 fails;
restored → 27 pass. Working tree clean after restore.

## Hunt for missed cases

Audited 8 IncDecZ writers + 2 IncDecZ readers across z80_cpu.cpp and
z80n_ext.cpp. Polarity matrix:

| Site                                  | Convention                  | VHDL anchor          |
|---------------------------------------|-----------------------------|----------------------|
| z80_cpu.cpp:404 (reset)               | 0                           | (init only)          |
| z80_cpu.cpp:677 (base ED block xfer)  | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80_cpu.cpp:828 (DJNZ, V13-fixed)     | zero (B == 0)               | t80n.vhd:1358-1360   |
| z80_cpu.cpp:835 (DD-prefix ED block)  | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:601 (LDIX)               | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:705 (LDIRX/LDDRX)        | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:756 (LDDX)               | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:805 (LDIRSCALE)          | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:869 (LDPIRX)             | nonzero (BC != 0)           | t80n.vhd:1361-1366   |
| z80n_ext.cpp:929 (additional Z80N)    | nonzero (BC != 0)           | t80n.vhd:1361-1366   |

All non-DJNZ sites use BC-dec nonzero convention; only DJNZ uses F.Z
zero convention. Both readers (LDWS, LDPIRX F.P emit) are straight
(no inversion). **No missed inverted-polarity sites.**

## Issues / nits

None. The fix is minimal, well-commented (cites both VHDL latch sites
and explains the inverted-meaning trap), and the test rewrite is
honest — it explicitly documents that the prior expectation matched
emulator output rather than VHDL semantics. The new not-taken test
adds the missing polarity coverage.

## Summary

- 1 class-(a) finding verified, fix correct, no regression at sibling
  sites.
- 1 modified test correctly retargets to VHDL semantics; the prior
  test was indeed bug-enshrining.
- 1 new discriminative test added; revert-check confirms both V13
  tests fail jointly without the production fix.
- 1 class-(d) finding correctly listed as architectural (out of CPU
  subsystem scope).
- All 38 ctest, 1356 FUSE, 27 CPU regression tests pass.
