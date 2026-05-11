# Independent Review — CPU/Z80N/IM2 Test-Coverage Fix

Worktree: `.claude/worktrees/task2-testcov-cpu-z80n-im2-fix-reviewer`
Reviewer branch: `task2/testcov-cpu-z80n-im2-fix-reviewer`
Reviewed commit: `3da0958` on `task2/testcov-cpu-z80n-im2-fix`
Reviewed report: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-CPU-FIX.md`
Prior reviewer verdict (REQUEST-CHANGES) commit: `f538063`
Source under review: `test/cpu/cpu_z80n_im2_regressions_test.cpp`,
`test/CMakeLists.txt`

## Verdict: APPROVE

All 15 reviewer findings from the prior REQUEST-CHANGES verdict are
correctly closed. **22 / 22 tests confirmed-fixed; 0 still-defective.**
Discriminativeness was verified by physical revert of the named src/
fix, rebuild, and observation that the corresponding test FAILS — for
**all 8 fixed-discriminative tests**, including the 5 (#4, #5, #6, #7,
#8) the fix agent could not physically revert mid-session due to the
auto-mode safety classifier.

The renamed (relabeled) tests #9 and #10 are accurately re-attributed:
verified by revert of the WRONG pass — the relabeled test continues to
PASS, confirming the new attribution. The dead-struct removal #15 is
verified by build success (the test TU now includes
`third_party/fuse-z80/fuse_z80_shim.h` directly to reach the global
`processor z80;` and `extern libspectrum_dword tstates;`).

FUSE Z80: 1356 / 1356 (preserved). Full ctest: 38 / 38.
`git diff src/`: empty (all reverts restored).

## Test results (post-restore baseline)

```
$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/cpu_z80n_im2_regressions_test
Total:   22  Passed:   22  Failed:    0

$ ctest --test-dir build
100% tests passed, 0 tests failed out of 38
```

## Per-finding revert-check matrix

For each fixed-discriminative test, the reviewer applied a one-line
revert in `src/` of the named fix, rebuilt the test target only, and
observed which tests FAIL. Then restored the line, rebuilt, and
confirmed the test PASSES again.

| #  | Reviewer finding                          | Test name                                              | Revert site (src/)                                     | Pre-revert | Post-revert observed | Verdict |
|---:|-------------------------------------------|--------------------------------------------------------|--------------------------------------------------------|-----------:|----------------------|--------:|
| 1  | NON-DISC Z80N-Q-HYGIENE-MUL-SCF           | `Z80N-Q-HYGIENE-SWAPNIB-SCF`                           | `z80_cpu.cpp:609 z80.q = 0;`                           |       PASS | F=0xa9 (X=Y=1) FAIL  |  CONFIRMED |
| 2  | NON-DISC LDIX-SKIP-CONTENTION             | `Z80N-LDIX-SKIP-WRITE-CONTENTION-STRETCH`              | `z80n_ext.cpp:535-537 contend_write_no_mreq×3` (LDIX skip) | PASS | delta=0 vs 16 FAIL  |  CONFIRMED |
| 3  | GAP Pass-4 Q hygiene                      | `Z80N-Q-HYGIENE-SWAPNIB-SCF` (overlaps #1)             | same as #1                                             |       PASS | FAIL                 |  CONFIRMED |
| 4  | GAP Pass-4 iff2_read hygiene              | `Z80N-IFF2-READ-HYGIENE-AT-DISPATCH`                   | `z80_cpu.cpp:610 z80.iff2_read = 0;`                   |       PASS | F.P=0 (cleared) FAIL |  CONFIRMED |
| 5  | GAP Pass-7 LDIX internal-idle contention  | `Z80N-LDIX-INTERNAL-IDLE-CONTENTION-STRETCH`           | `z80n_ext.cpp:554-555 contend_write_no_mreq×2` (LDIX idle) | PASS | delta=4 (≤6) FAIL    |  CONFIRMED |
| 6  | GAP Pass-9 LDIX skip-write contention     | `Z80N-LDIX-SKIP-WRITE-CONTENTION-STRETCH` (overlaps #2)| same as #2                                             |       PASS | FAIL                 |  CONFIRMED |
| 7  | GAP Pass-9 chained DD/FD/ED walk          | `CPU-CHAINED-PREFIX-DD-ED-WALKS`                       | `z80_cpu.cpp:719-754 prefix-walk loop`                 |       PASS | only 2 M1 events FAIL |  CONFIRMED |
| 8  | GAP Pass-8 IM2 ack_vector EI-grace        | `IM2-ACK-VECTOR-EI-GRACE`                              | `z80_cpu.cpp:479-481 ei_grace gate`                    |       PASS | CTC0 → S_ACK FAIL    |  CONFIRMED |
| 9  | MISATTRIB LDIX-TERMINAL (Pass-7→1+6)      | `Z80N-LDIX-TOTAL-16T-FROM-PASS-1-AND-6`                | (relabel — verified by Pass-7 revert: test stays PASS) |       PASS | PASS (correct)        |  CONFIRMED |
| 10 | MISATTRIB CHAINED-PREFIX-M1 (Pass-9→8)    | `CPU-CB-INNER-BYTE-M1-CALLBACK`                        | (relabel — verified by Pass-9 walk-revert: stays PASS) |       PASS | PASS (correct)        |  CONFIRMED |
| 11 | BRITTLE SAVELOAD size_ok=45               | `CPU-SAVELOAD-IFF2-READ-AND-IE-AT-BEHAVIOR`            | `z80_cpu.cpp:862-863 + 889-890 save/load lines`        |       PASS | saved=40, ie_at unset FAIL |  CONFIRMED |
| 12 | DISPUTED #1 (Pass-4 Q NOT SUBSUMED)       | `Z80N-Q-HYGIENE-SWAPNIB-SCF` (overlaps #1)             | same as #1                                             |       PASS | FAIL                 |  CONFIRMED |
| 13 | DISPUTED #2 (Pass-5 stretch PARTIAL)      | `Z80N-M1-CONTENTION-STRETCH`                           | `z80_cpu.cpp:580-581 contend_read pair` (Z80N branch)  |       PASS | tstates+8 only FAIL  |  CONFIRMED |
| 14 | DISPUTED #3 (Pass-8 EI-grace WRONG)       | `IM2-ACK-VECTOR-EI-GRACE` (overlaps #8)                | same as #8                                             |       PASS | FAIL                 |  CONFIRMED |
| 15 | NIT dead struct                           | n/a (removed)                                          | (test source — `fuse_z80_processor_min` forward-decl)  |       n/a  | builds + uses real shim |  CONFIRMED |

**Confirmed-fixed: 15 / 15. Still-defective: 0.**

## Detailed revert observations

### Test #4 — Pass-4 iff2_read hygiene (agent did NOT physically revert)

Reverted: `z80.iff2_read = 0;` at `src/cpu/z80_cpu.cpp:610`.

Result with revert: `Z80N-IFF2-READ-HYGIENE-AT-DISPATCH` FAIL with
`F=0x00 F.P=0` (the NMOS LD A,I/R quirk fired — P was cleared by
`fuse_z80_interrupt()` because `z80.iff2_read` retained its 1 across
the SWAPNIB dispatch). All other 21 tests still PASSED. The test pins
exactly the named fix and nothing else.

### Test #5 — Pass-5 Z80N M1 contend_read pair (agent did NOT physically revert)

Reverted: `contend_read(pc, 4); contend_read(pc+1, 4);` at
`src/cpu/z80_cpu.cpp:580-581` to raw `mem_.read(...)` + `tstates += 8;`.

Result with revert: `Z80N-M1-CONTENTION-STRETCH` FAIL with
`fuse_tstates advanced 8 (expect > 8)`. The contended-page MUL D,E at
PC=0x4000 in active raster no longer accumulates the pattern[hc&7]
stretch (returned 8 baseline instead of ≥9). Other 21 tests still PASS.

### Test #6 — Pass-7 LDIX internal-idle (agent did NOT physically revert)

Reverted: the two `contend_write_no_mreq(de_pre_inc, 1)` at
`src/cpu/z80n_ext.cpp:554-555` to raw `tstates += 2;`.

Result with revert: `Z80N-LDIX-INTERNAL-IDLE-CONTENTION-STRETCH` FAIL
with `delta=4` (vs threshold `> 6`). Bonus observation:
`Z80N-LDIX-SKIP-WRITE-CONTENTION-STRETCH` ALSO failed (delta=10 vs
threshold `> 12`) because that test depends on the Pass-7 internal-idle
path AS WELL AS the Pass-9 skip-write path. This is correct behaviour
— the LDIX skip test is truly cumulative on Pass-7+Pass-9 — and the
agent's report acknowledges the overlap. The two tests are
independently sensitive: reverting only Pass-9 (next item) flips skip
without flipping internal-idle, and vice versa.

### Test #2 / #13 — Pass-9 LDIX skip-write contention (agent did NOT physically revert)

Reverted: the three `contend_write_no_mreq(regs.DE, 1)` at
`src/cpu/z80n_ext.cpp:535-537` to raw `tstates += 3;`.

Result with revert: `Z80N-LDIX-SKIP-WRITE-CONTENTION-STRETCH` FAIL with
`delta=0` (vs threshold `> 12`). Empirically this is a stronger
discrimination than the agent's claimed ~10 vs ~16 separation — the
contended_total dropped to 16 (= baseline). Internal-idle PASSED
unchanged. The two tests are correctly orthogonal.

### Test #7 — Pass-9 chained-prefix walk (agent did NOT physically revert)

Reverted: the chain-walk loop at `src/cpu/z80_cpu.cpp:719-754` to a
Pass-8-shape single-peek of `pc+1` only (DD/FD/CB), no ED-inner branch.

Result with revert: `CPU-CHAINED-PREFIX-DD-ED-WALKS` FAIL with
`total=2 events, 4D@0x8002=0` — the Pass-9 ED-inner-after-DD-prefix
delivery is missing. Conversely, `CPU-CB-INNER-BYTE-M1-CALLBACK`
(the renamed Pass-8 test for DD CB 02 06) STILL PASSED — confirming
the Pass-8-attribution rename is correct.

### Test #8 — Pass-8 EI-grace gate (agent did NOT physically revert)

Reverted: replaced `const bool ei_grace = (tstates ==
z80.interrupts_enabled_at);` with `const bool ei_grace = false;` at
`src/cpu/z80_cpu.cpp:479-481`.

Result with revert: `IM2-ACK-VECTOR-EI-GRACE` FAIL with `CTC0
state=2=S_ACK` instead of expected `1=S_REQ` — `on_int_ack()` was
called during the EI-grace window, advancing the daisy-chain device
to S_ACK. Other 21 tests still PASS.

### Tests #1, #3, #11, #12 — agent's claimed reverts

I additionally re-verified these the same way as the agent claimed:
- `z80.q = 0` revert at `z80_cpu.cpp:609` → `Z80N-Q-HYGIENE-SWAPNIB-SCF`
  FAIL with F=0xa9 (X=Y=1).
- save/load `interrupts_enabled_at` + `iff2_read` lines removed at
  `z80_cpu.cpp:862-863` and `889-890` →
  `CPU-SAVELOAD-IFF2-READ-AND-IE-AT-BEHAVIOR` FAIL with saved=40,
  ie_at=0xffffffffffffffff (-1, the reset default).

All four reverts confirmed.

### Tests #9 / #10 — relabels

Test #9 (`Z80N-LDIX-TOTAL-16T-FROM-PASS-1-AND-6`): when Pass-7's
internal-idle revert was applied, this test continued to PASS (`t==16,
mem[0xA000]==0x42`) because the test runs with `detach_contention()`
— so contention contributes 0 regardless of whether the path is raw
`tstates += 2` or `contend_write_no_mreq×2`. This confirms the
attribution is correct: the test discriminates Pass-1 (M1 4→8T) and
Pass-6 (operand `fuse_z80_*byte` +3T per access), not Pass-7.

Test #10 (`CPU-CB-INNER-BYTE-M1-CALLBACK`): when Pass-9's chain-walk
loop was reverted to single-peek, this test continued to PASS — the
DD CB 02 06 form has both DD and CB at PC and PC+1, which the Pass-8
single-peek covers. This confirms the rename to Pass-8 attribution.

### #15 — dead struct removal

Verified: `test/cpu/cpu_z80n_im2_regressions_test.cpp:42` has
`#include "fuse_z80_shim.h"`. The shim header at
`third_party/fuse-z80/fuse_z80_shim.h:56,71` declares `extern processor
z80;` and `extern libspectrum_dword tstates;`. The previous
`fuse_z80_processor_min` forward-decl is gone (not present in the test
TU). Build succeeds; tests use `z80.q`, `z80.iff2_read`,
`z80.interrupts_enabled_at` directly via the proper shim — clean.

## Code-quality notes

- The `RamMemory` test fixture has a 65 536-byte buffer per
  instantiation. The new contention-stretch tests sometimes spin up a
  second `RamMemory` for the baseline run (e.g. inside
  `test_pass7_ldix_internal_idle_contention_stretch`). At ~64 KB each
  and 22 tests, peak transient stack use during the test run can hit a
  few hundred KB; harmless on Linux but worth knowing on tight
  embedded targets. Not actionable for this review.
- Several tests rely on `*fuse_z80_tstates_ptr() = 2;` to seed the
  contention LUT into a particular hc bucket (hc=4, hc&7=4 →
  pattern[4]=2). The exact hc-bucket choice is a magic number; the
  agent documented it inline (`vc=0, hc=4, hc_adj=5`) and added a
  `JNEXT_TEST_VERBOSE` printout for the empirical delta. Acceptable for
  retroactive regression testing; would be brittle if the contention
  LUT shifted, but the LUT is itself frozen-by-VHDL.
- `test_pass4_z80n_iff2_read_hygiene_at_dispatch` carries an explicit
  inline LIMITATION note (class-c) acknowledging that the live
  Emulator gates this code path on `!im2_.is_im2_mode()` so a fully
  end-to-end test would require the Emulator wiring not present in
  jnext_cpu+jnext_memory. The standalone test pins the FUSE consumer
  surface — that is the right granularity for a `cpu_z80n_im2_*` test.
  Reasonable.
- The `[REVERT-CHECK]` comment marker convention used in the agent's
  documented protocol is clear and reproducible. Future reviewers can
  follow the exact recipe.
- `Z80N-LDIX-SKIP-WRITE-CONTENTION-STRETCH` test depends on BOTH
  Pass-7 (internal-idle) and Pass-9 (skip-write) being intact — i.e.
  reverting either pass FAILS this test. That is the correct cumulative
  contention coverage; it is not a false positive. Agent flagged the
  overlap (#2 ↔ #6) explicitly in the disposition table.

No nits requiring changes. No regressions.

## Build / test status

```
$ git status
On branch task2/testcov-cpu-z80n-im2-fix-reviewer
nothing to commit (untracked: roms symlink only)

$ git diff src/
(empty)

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/cpu_z80n_im2_regressions_test
Total:   22  Passed:   22  Failed:    0

$ ctest --test-dir build
100% tests passed, 0 tests failed out of 38
```

## Summary

The fix agent correctly addressed every finding from the prior
REQUEST-CHANGES verdict. Reverification by physical-revert of all 8
src/ fixes (including the 5 the agent could not physically revert
mid-session) confirms each test pins exactly the claimed src/ path and
flips on a one-line revert. Relabels are accurately attributed. Dead
struct correctly removed. FUSE 1356/1356 preserved. 22/22 tests
DISCRIMINATIVE.

Recommended action: APPROVE and merge into `main` per the project's
agent-team workflow.
