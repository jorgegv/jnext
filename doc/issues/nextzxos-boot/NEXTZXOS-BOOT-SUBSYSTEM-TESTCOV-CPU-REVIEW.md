# Independent Review — CPU/Z80N/IM2 Test Coverage Audit

Reviewer worktree: `.claude/worktrees/task2-testcov-cpu-z80n-im2-reviewer`
Branch: `task2/testcov-cpu-z80n-im2-reviewer`
Reviewed commit: `a858a07` (branch `task2/testcov-cpu-z80n-im2`)
Reviewed file: `test/cpu/cpu_z80n_im2_regressions_test.cpp` (17 cases)
Reviewed report: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-CPU.md`

## Verdict: REQUEST-CHANGES

3 of 17 named cases are **non-discriminative** — they pass even when the
fix they claim to regress is reverted in the source. 1 of 3 "subsumed by
indirect tests" claims is **factually wrong** (the cited indirect tests
do not cover the named fix). 3 of 17 cases verify Pass-1/Pass-6/Pass-8
end-state but do NOT verify the later Pass-7/Pass-9/Pass-9 fixes whose
hashes appear in their test name. Several test names therefore
mis-attribute coverage. The actual discriminative count is 11/17, with
the remaining 6 being either non-discriminative, marker-only, or
covering an upstream pass rather than the one named.

The 17 cases all PASS today (FUSE 1356/1356, ctest 38/38), so this is
not a "tests are broken" verdict — it is a "tests do not regress what
the report claims they regress" verdict. A future regression of any of
the three non-discriminative items would slip through CI without
failing this suite.

## Verification environment

```
$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ctest --test-dir build
100% tests passed, 0 tests failed out of 38

$ ./build/test/cpu_z80n_im2_regressions_test
Total:   17  Passed:   17  Failed:    0
```

Discriminative checks were performed by:
1. Reverting the named fix in `src/cpu/z80n_ext.cpp` or `src/cpu/z80_cpu.cpp`
2. Rebuilding the test target only
3. Running the test target and observing whether the case fails
4. Restoring the source

A test is **discriminative** for fix F iff the test fails when F is
reverted. A non-discriminative test is documentation, not a regression test.

## Per-test reassessment (17 cases)

Legend:
- DISC = test discriminates the named fix (would fail if fix reverted)
- NON-DISC = test passes whether or not the named fix is applied
- MARKER = test always asserts true; documents fixture coverage elsewhere
- MISATTRIB = test discriminates a different fix than the one named

| #  | Test name                                  | Named fix      | Verdict           | Notes |
|---:|--------------------------------------------|----------------|-------------------|-------|
|  1 | Z80N-FUSE-TSTATES-GLOBAL-INCREMENT         | Pass-2 86128d5 | DISC (cumulative) | Tests Pass-2+Pass-5 stack; Pass-2 alone reverted still passes (Pass-5 contend_read fills gap). Acceptable: end-state regression. |
|  2 | Z80N-Q-HYGIENE-MUL-SCF                     | Pass-3 0a64eff | **NON-DISC**      | Verified by reverting `z80.q=0; z80.iff2_read=0` at Pass-4 dispatch top: test still PASSES. Setup uses CP B with B=0x01 → CP X/Y come from B (=0) so F.X=F.Y=0 in F regardless of Q-hygiene. The agent's reasoning that A=0xC0 + Q=0 yields X=Y=0 ignores that pre-fix Q would also have X=Y=0 because CP B with B=0x01 sets X/Y from B not the Z80N stale Q. **Required fix**: pick CP value with bits 3,5 set (e.g. CP with B=0x28 or use a different prior-F provider) so Q^F differs between fixed and unfixed. |
|  3 | Z80N-LDIX-FLAGS-FIXTURE-PRESENT            | Pass-3 0a64eff | MARKER (valid)    | Always-true assertion. Genuine fixture coverage exists in `test/z80n/tests.expected` (eda4_copy AF=aa08 etc.) which would discriminate. Acceptable. |
|  4 | CPU-SAVELOAD-MEMPTR-Q                      | Pass-3 0a64eff | DISC              | Confirmed: write MEMPTR=0x55AA, Q=0xC9, save+load, verify roundtrip. Pass-3 added these 3 bytes; revert → bytes missing → test fails with both MEMPTR and Q reading 0. |
|  5 | Z80N-ADD-HL-NN-MEMPTR                      | Pass-4 c84f9ea | DISC              | **Verified by revert** (commented out `regs.MEMPTR = nn;`): test fails with `MEMPTR=0x0000 (exp 0x1234)`. Strong test. |
|  6 | CPU-SAVELOAD-IFF2-READ-AND-IE-AT           | Pass-4 c84f9ea | DISC (brittle)    | Asserts `saved_bytes == 45`. Pass-4 added 5 bytes (i32 + u8). Reverting Pass-4 → 40 bytes → test fails. **Brittleness**: future field additions break this test. Better to assert "post-load IFF1/IFF2/PC survive" rather than exact byte count. The PC=0x8001 + IFF1=1+IFF2=1 part is fine; the size_ok check is over-specified. |
|  7 | Z80N-TSTATES-MUL                           | Pass-1 65b5918 + Pass-2 86128d5 | DISC (cumulative) | Tests t==8 && t_global==8 for SWAPNIB after seeding global=100. Pre-Pass-2-and-Pass-5 stack: tstates would not advance via Z80N path. Acceptable end-state regression. |
|  8 | Z80N-TSTATES-PUSH-NN                       | Pass-1 + Pass-6 b4af634 | DISC (cumulative) | t==23 && t_global==23. Acceptable. |
|  9 | Z80N-TSTATES-JP-C-12T                      | Pass-8 948f221 | DISC              | Confirmed pre-Pass-8 returned 13T (`tstates += 1; return 13;` in 948f221^), post-Pass-8 returns 12T. Strong test. |
| 10 | Z80N-LDIX-TERMINAL-TSTATES                 | Pass-7 07ed205 | **MISATTRIB**     | Test verifies LDIX TOTAL=16T, but that total already held pre-Pass-7 (Pass-1 fixed M1=8T; Pass-6 added fuse_z80_*byte +3T each; pre-Pass-7 had `tstates += 2` raw which gives the same 16T total when contention is OFF). Pass-7 only changed WHO advances those last 2T (raw vs `contend_write_no_mreq`); without `s_contention` set in the test, both yield identical tstate count. **The test discriminates Pass-1 (4→8 M1 baseline) and Pass-6 (operand contention via fuse_z80_*byte) — NOT Pass-7.** Pass-7's contention-gate-on-internal-idle fix has NO regression test in this suite. |
| 11 | CPU-CHAINED-PREFIX-M1-CALLBACK             | Pass-8 948f221 + Pass-9 b40af13 | DISC (Pass-8 only) | Test uses `DD CB 02 06`. Pass-8's single-level peek already delivers DD + CB. Pass-9 added chained-walk for DD DD ED 4D / FD DD CB d <op> etc. **Pass-9 chained-prefix walking has NO discriminative test here** — the test would equally pass with only Pass-8 applied. Test discriminates Pass-8 not Pass-9. |
| 12 | Z80N-PUSH-NN-WZ-LO-ONLY                    | Pass-8 948f221 | DISC              | Pre-Pass-8: PUSH NN didn't write MEMPTR at all. With prior MEMPTR=0xBEEF, test asserts MEMPTR=0xBE34. Pre-fix yields 0xBEEF (unchanged) → fails. Strong test. |
| 13 | Z80N-LDWS-INCDECZ-FROM-DJNZ                | Pass-9 b40af13 | DISC              | OR A → DJNZ B:2→1 → LDWS. Post-Pass-9: F.P = IncDecZ shadow = 1. Pre-Pass-9: F.P = prior(F.P) = 0 (OR A on 0x01 leaves P=0 since odd parity). Strong test. |
| 14 | Z80N-LDIX-SKIP-CONTENTION                  | Pass-9 b40af13 | **NON-DISC**      | Verified by reverting Pass-9 transparency-suppressed write contention back to `tstates += 3` (raw): test still PASSES. The test asserts t==16 && t_global==16 && mem[0xA000]==0x99, but tstate total is 16T whether the 3T comes from raw `+= 3` or from 3× `contend_write_no_mreq` when `s_contention` is OFF (which the test does not enable). Pass-9 specifically fixed the contention GATE on suppressed writes, not the total tstate count — that fix is observable only with `s_contention` enabled. **Required fix**: install a `ContentionModel` on the test fixture that adds a known stretch on a given (hc, vc) — only then does raw `tstates += 3` differ from `contend_write_no_mreq` chain. |
| 15 | Z80N-LDPIRX-FLAGS-FIXTURE-PRESENT          | Pass-10 c526aa4 | MARKER (valid) | Always-true. Real coverage in `tests.expected` edb7_basic AF=aa20 (post-fix) vs pre-fix preserved AF=aa00. Acceptable. |
| 16 | Z80N-ADD-HL-A-FORCE-FC-ZERO                | Pass-10 c526aa4 | DISC              | HL=0xFFFF + A=0xFF: real carry=1 pre-fix, forced 0 post-fix. Test asserts F.C=0. Strong test. |
| 17 | IM2-RETI-DECODE-SIMULTANEITY-NESTED-ISR    | Pass-10 c526aa4 | DISC              | Sets up CTC0 in S_ISR + LINE in S_REQ; drives ED 4D; expects CTC0→S_0. Pre-fix: `iei_reti_decode = reti_decode_` = false at the reti_seen pulse → LINE's S_REQ cuts IEI → CTC0 stays in S_ISR. Post-fix: `reti_decode_ || reti_seen_pulse_` = true → IEI propagates → CTC0 clears. Strong test. |

### Discriminative tally

- Strongly DISC for the named fix: 7 (#4, #5, #9, #12, #13, #16, #17)
- DISC cumulative for the fix family (named fix may be subsumed by upstream pass): 3 (#1, #7, #8)
- DISC but with brittleness nit (over-specified): 1 (#6)
- MISATTRIB (verifies a different fix than named): 2 (#10 → Pass-1+6 not Pass-7; #11 → Pass-8 not Pass-9)
- MARKER (always-true, fixture-based real coverage exists): 2 (#3, #15)
- **NON-DISC (test passes regardless of fix being applied)**: 2 (#2, #14)

So the actual regression-test count for the Pass-1..Pass-10 stack is
roughly 11 of the claimed 17. Two named fixes (Pass-7 LDIX-family
internal-idle contention; Pass-9 LDIX transparency-suppressed-write
contention) lack any discriminative test in this suite. Two more
(Pass-4 Q hygiene at top of Z80N dispatch; Pass-9 chained-prefix M1)
are formally claimed but the actual discriminating power lies elsewhere.

## Subsumed-by-indirect verification (3 claims)

The agent's report at `NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-CPU.md` claims
3 sub-fixes are "subsumed by indirect tests". I checked each.

### Claim 1: Pass-4 Q hygiene at top of Z80N dispatch (#7) — claimed subsumed by #5

**Verdict: NOT SUBSUMED.** Test #5 (Z80N-Q-HYGIENE-MUL-SCF) is itself
non-discriminative (see test #2 row above; reverting Pass-4 dispatch-top
`z80.q=0; z80.iff2_read=0` does not break the test). The CP B / MUL D,E
/ SCF sequence with A=0xC0 and B=0x01 produces identical X/Y output in
F whether Pass-4 is applied or not, because CP X/Y comes from operand B
which has bits 3,5 = 0. The agent's reasoning chain incorrectly attributed
the post-fix X=Y=0 result to Q-hygiene when in fact it comes from the
B=0x01 operand. **Pass-4 Q-hygiene at dispatch top has no regression test.**

### Claim 2: Pass-5 Z80N M1 contention bypass (#11) — claimed subsumed by #1, #3, #12

**Verdict: PARTIALLY SUBSUMED.** Pre-Pass-5, M1 reads were raw
`mem_.read(pc)` / `mem_.read(pc+1)` with no tstates advance. Post-Pass-5,
`contend_read(pc, 4)` × 2 advances tstates by 8T plus stretch. The
`tstates`-advance portion is verified by the named tstates tests (they
would fail if both Pass-2 post-call `tstates += t` AND Pass-5 contend_read
were reverted simultaneously). The **contention-stretch portion** is NOT
verified — no test installs a `ContentionModel`, so the difference between
"raw tstates += 8" and "contend_read pair that adds stretch on contended
pages during active raster" is invisible. Acceptable claim because the
bypass-bug originally manifested as missing tstates increment (which IS
covered) and the contention-stretch corner is a class-(b/c) refinement,
but the report should be honest that only the count-advance is tested.

### Claim 3: Pass-8 IM2 ack_vector EI-grace (#14) — claimed subsumed by ctc_test IM2C-01..05

**Verdict: WRONG.** Reading `test/ctc/ctc_test.cpp` lines 925-996, the
named IM2C-01..IM2C-05 cases test the RETI/RETN decoder FSM (states
S_0/S_ED_T4/S_ED4D_T4/S_ED45_T4/S_CB_T4 transitions and pulse signals).
**They do not exercise the EI-grace gate at all.** Pass-8's `ack_vector`
EI-grace fix was: "skip on_int_ack() when `tstates == z80.interrupts_enabled_at`
so IM2 fabric stays in S_REQ and doesn't phantom-advance to S_ACK."
Verifying that requires:
1. Configuring an IM2 device into S_REQ
2. Issuing an EI immediately before the int_pending check
3. Asserting that the device is still in S_REQ after `Z80Cpu::execute()`
   returns from the EI-grace branch (i.e. `on_int_ack` was NOT called)

No such test exists in `ctc_test.cpp`, `ctc_interrupts_test.cpp`,
`int_pulse_test.cpp`, or anywhere else under `test/`. The agent's claim
that IM2C-01..05 / IM2D-01..11 / IM2P-01..05 cover this is incorrect.
**Pass-8 ack_vector EI-grace has no regression test.**

## Coverage gaps the agent missed

Direct gaps (no discriminative test exists for the named fix):

1. **Pass-4 Q hygiene at top of Z80N dispatch** (`c84f9ea`, `z80.q = 0`).
   Required test: pick a sequence where the prior FUSE opcode leaves
   Q with bit 3 OR 5 set (e.g. SCF with A bit 3=1, then a non-F-writing
   Z80N opcode like MUL D,E, then another SCF). Compare X/Y bits in the
   final F.

2. **Pass-4 iff2_read hygiene at top of Z80N dispatch** (`c84f9ea`,
   `z80.iff2_read = 0`). Required test: LD A,I (sets iff2_read=1) →
   non-F-writing Z80N opcode (should clear iff2_read=0) → set up an INT
   that would fire the NMOS quirk if iff2_read were still 1; observe
   the P flag is NOT cleared. The agent's report acknowledges this is
   "gated on `!im2_.is_im2_mode()` per Pass-10 class-(d) carry-forward",
   which is a legitimate gating reason — but the SAVE-STATE proxy (#10
   in agent's table) only verifies the FIELD is persisted, not that
   dispatch-top clearing happens. Class-(d) acknowledged; acceptable
   gap with note.

3. **Pass-7 LDIX-family internal-idle contention via contend_write_no_mreq**
   (`07ed205`). Required test: install a `ContentionModel` whose
   `contention_tick` returns +N stretch on a known (hc, vc); LDIX into
   contended page during active raster; observe tstates advance by
   `16 + N` instead of just 16.

4. **Pass-9 LDIX transparency-suppressed-write contention** (`b40af13`).
   Same as #3 above but for the skip-write path. Currently the test
   passes whether or not the contention gate is engaged.

5. **Pass-9 chained DD/FD/ED prefix M1 walking** (`b40af13`). Required
   test: fire `DD DD ED 4D` (or `DD ED 4D` since DD itself is on the
   walk) and assert m1_log contains the inner ED at PC+1 OR PC+2 of the
   final DD prefix. Pass-8's single peek delivers PC+1 only; Pass-9's
   walking loop delivers all chain bytes.

6. **Pass-8 IM2 ack_vector EI-grace gate** (`948f221`). Required test:
   set IM2 device to S_REQ; raise int_pending in CPU; execute EI then
   immediately call `cpu.execute()` again with `interrupts_enabled_at
   == tstates`. Assert device is still in S_REQ (NOT advanced to S_ACK)
   and `on_int_ack` was NOT called.

Indirect coverage gaps for the report itself:

7. **Marker tests should explicitly invoke the Z80N fixture**. The two
   marker tests (LDIX-FLAGS-FIXTURE-PRESENT and LDPIRX-FLAGS-FIXTURE-PRESENT)
   could add genuine value by running a single LDIX/LDPIRX in the test
   itself and asserting the AF result against the fixture's expected
   value, instead of always-true. Promoting these to real tests costs
   ~30 lines each and removes the "marker" caveat.

## Code quality nits

1. **`prep_cpu()` does not reset `regs_.IncDecZ` between tests** (line
   72-83). The reset() in Z80Cpu does set it to 0, but `prep_cpu` calls
   `set_registers` with `r{}` (zero-init) which sets IncDecZ=0
   correctly. OK.

2. **`fuse_z80_processor_min` forward declaration** (line 27-32) is a
   relic of an earlier draft — never used in the test body. Dead code;
   should be removed.

3. **`size_ok` byte count assertion in test #6** is over-specified.
   Better assertion: round-trip a CPU with EI executed, save, reset
   another CPU, load, then execute another instruction and verify the
   subsequent INT acceptance behavior matches the original. The
   byte-count check rots on every save/load schema addition.

4. **Test #2 (Z80N-Q-HYGIENE-MUL-SCF) comment is internally inconsistent**.
   The comment block (lines 86-180) walks through three different
   reasoning chains for the expected post-state, then concludes
   "post-fix the X/Y bits track A only" without ever realizing that the
   chosen B=0x01 operand makes pre-fix and post-fix yield identical X/Y.
   The test was written from a flawed mental model that didn't get
   verified against an actual revert.

5. **Test #14 (Z80N-LDIX-SKIP-CONTENTION)**: the fixture asserts
   `mem.ram[0xA000] == 0x99` to verify the write was suppressed. That
   IS discriminative for the transparency-suppression behavior (which
   was an earlier fix not in Pass-1..Pass-10 scope). The TIMING side
   of the Pass-9 fix is what's not discriminated.

6. **Comment at line 482-483** ("Pass-8 (948f221) — IM2 ack_vector
   EI-grace gate; LDWS I_BT flags; PUSH_NN WZ-lo; JP(C) 12T...") lists
   IM2 ack_vector EI-grace as covered but it is the unnamed-and-
   unverified subsumed-claim #3 above.

7. **Magic number `45` in test #6**. Should be a named constant or
   computed from `sizeof()` / a documented schema description. Brittle.

## Build / test status

- `cmake --build build -j$(nproc)` — clean build
- `./build/test/fuse_z80_test build/test/fuse` — 1356/1356 PASS
- `./build/test/cpu_z80n_im2_regressions_test` — 17/17 PASS
- `ctest --test-dir build` — 38/38 PASS

No FUSE regressions, no pre-existing test breakage.

## Recommended actions before merge

The 3 non-discriminative items (#2, #11-LDIX-skip-contention, #14-LDIX-skip)
need either:
- (a) a stronger discriminative test case (preferred), or
- (b) honest re-classification in the report as MARKER / SUBSUMED-NOT-VERIFIED
  with explicit acknowledgement that the fix has no end-state regression
  test (similar to how #14 IM2 ack_vector EI-grace is currently classified)

The 2 misattributed names (#10, #11) should rename or split — e.g.
`Z80N-LDIX-TERMINAL-TSTATES` should become `Z80N-LDIX-TOTAL-16T-FROM-PASS-1`
or be augmented with a contention-enabled variant that actually exercises
Pass-7. `CPU-CHAINED-PREFIX-M1-CALLBACK` should add a `DD DD ED 4D` case
to actually test Pass-9's walking loop.

The wrong "subsumed" claim for Pass-8 IM2 ack_vector EI-grace
(#14 in agent's table) needs a real regression test, OR a class-(c)
gating-style explanation (which doesn't apply here — the gate is
plain CPU-side logic, easily testable).

The `fuse_z80_processor_min` dead struct should be removed.

## Constraint compliance

- VHDL/Z80N spec/FUSE oracle: respected — every cited line was checked.
- FUSE 1356/1356: preserved.
- No `src/` modifications kept (any test reverts performed during
  discriminative checks were reverted before this report was committed).
- No agent-test typo fixes applied (the test passes; this is a coverage
  critique, not a correctness fix).
- No push, no merge.

## Branch state

- Reviewer worktree HEAD: a858a07 (parent commit base) +
  this review document commit.
- Working tree clean after revert/restore cycles.
- ctest 38/38, FUSE 1356/1356.
