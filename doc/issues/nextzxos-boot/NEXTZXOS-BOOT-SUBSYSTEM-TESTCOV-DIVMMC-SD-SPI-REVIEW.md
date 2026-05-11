# DivMMC + SD + SPI — Test-Coverage Audit Independent Review

Reviewer worktree: `task2/testcov-divmmc-sd-spi-reviewer`
Reviewed agent commit: `669f59b` (branch `task2/testcov-divmmc-sd-spi`)
Reviewed report: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI.md`

Reviewer mandate (per CLAUDE.md): be very critical; verify each test
actually regression-tests the claimed fix; check VHDL/SD-spec citations;
confirm tests would fail if the fix were reverted (discriminative
check); spot defective tests.

---

## 1 — Verdict

**APPROVE-WITH-NITS.**

12 of 14 new tests are correct and discriminative against the fix they
claim to regression-test (or against a meaningful future regression).
1 of 14 is correct as written but only partially discriminative against
the original fix (covers a future-regression sentinel, not the original
bug). 1 of 14 is **defective as a regression test** for the originally
claimed fix — it passes both pre-fix and post-fix because the test
scenario does not exercise the bug-affected code path; it is still a
useful sanity test, but the report's claim that it "tests 24a1bc4" is
overstated.

The deferred-to-integration items (§3.1 / §3.2 in the agent's report)
are genuinely integration-tier — accept the deferral.

Build clean, all 37 ctest entries pass, divmmc 132/132, sdcard 21/21,
sd_rom_extractor PASS, fuse_z80 1356/1356.

---

## 2 — Per-test reassessment

For each new test row I (a) read the agent's coverage claim, (b) read
the actual fix commit, (c) read the test code, (d) where ambiguous,
mentally-reverted the fix and rebuilt + reran the test to confirm
discriminative behaviour. The "reverted-revert verified" column below
is a yes only when I actually patched the worktree, rebuilt, and
observed the test fail.

| Row    | Fix     | Verdict    | Discriminative against original fix? | Reverted-revert verified |
|--------|---------|------------|--------------------------------------|--------------------------|
| NR-14  | 399c9ae | partial    | No — pre-fix had no wildcard branch at all, so rom3=0 also yielded `automap=false`. Useful as a future-regression sentinel for the gate (`&& rom3_path_eligible`) only. | n/a (cannot fail pre-fix) |
| NM-09  | 6ebfd2b | covers     | Yes — pre-fix rising-edge guard kept `button_nmi=true` when set after held=1. | YES (FAIL captured) |
| DA-09  | 770f78d | partial    | No — tests the contract (save_state doesn't persist `rom3_active_`) that the Emulator-tier fix relies on, NOT the Emulator::load_state fix code itself. Useful contract pin (catches future double-write hazard if save_state ever starts persisting). | n/a (Emulator fix not exercised by test) |
| NA-09  | c54192d | partial    | No — tests the pattern (NextReg::reset doesn't fire write_handlers; explicit cached-sync recovers consumer) but does not run the Emulator::init code that contains the fix. Useful as a contract pin for the gap NextReg leaves on reset. | n/a (Emulator fix not exercised by test) |
| SS-13  | 6ebfd2b | covers     | Yes — pre-fix dropped `0x7F` to `0xFF` unconditionally; gate-OPEN sub-case fails when `flash_cs_enable_` is ignored. | YES (FAIL captured) |
| SS-14  | ff84d3e | partial    | No — the test attaches the same dev to BOTH CS0 and CS1 directly, bypassing Emulator::init (where the fix lives). Verifies SpiMaster correctly routes when both CS lines have the same device, but does not catch a regression where Emulator only attaches to CS0. | n/a (Emulator fix not exercised by test) |
| SX-11  | d54a053 | covers     | Yes — pre-fix left `rx_data_` unchanged on no-slave write; test reads back stale `0x42` instead of expected `0xFF`. | YES (FAIL captured) |
| SX-12  | d54a053 | covers     | Yes — pre-fix left `rx_data_` unchanged on no-slave read; second post-deselect read returns `0x77` instead of expected `0xFF`. | YES (FAIL captured) |
| SD-15  | 24a1bc4 | **defective** | **No — passes both pre-fix and post-fix.** The test calls `init_card(sd)` between mount(img2) and CMD17, which sends CMD0 → state recovers via the existing IDLE branch. CMD17 then unconditionally re-reads `data_block_` from the new `file_`. Even reverting BOTH the pass-5 mount() reset AND the pass-8 unmount() reset, SD-15 still passes (SD-16 caught the cmd16 idle-bit revert; SD-15 did not catch the mount-reset revert). | YES (FAIL not produced after both fixes reverted; only SD-16 fired) |
| SD-16  | 24a1bc4 | covers     | Yes — pre-fix returned `0x05` unconditionally; post-fix returns `0x04` for initialized + `0x05` for uninitialized. | YES (FAIL captured) |
| SD-17  | c7acf9e | covers     | Yes — pre-fix absorbed leading 0xFF bytes into `data_block_`, shifting the payload; readback then differs from the written pattern. | YES (FAIL captured: `match=0 got0=255 exp0=51`) |
| SD-18  | 6ebfd2b | covers     | Yes — pre-fix returned `R1=0x00` (no illegal bit) on unhandled CMD; test asserts bit 2 set. | YES (FAIL captured: `r1=0`) |
| SD-19  | 6ebfd2b | covers     | Yes — pre-fix returned `R1=0x05` for unknown ACMD with idle bit hard-coded; test asserts bit 2 set AND bit 0 cleared on initialized card. | YES (FAIL captured) |
| SD-20  | ff84d3e | covers     | Yes — pre-fix returned R1=illegal for any non-ACMD41 sequence; CMD55 → CMD17 then never produced a data block. Test asserts CMD17 R1=0x00 and the data block matches sector 2 fixture. | YES (FAIL captured: `r1_17=5 tok=0`) |

**Tally**: 9 fully-discriminative regression tests, 4 contract / future-regression-only tests (NR-14, DA-09, NA-09, SS-14), 1 **defective** (SD-15).

---

## 3 — Discriminative-check methodology and results

For each test where the discriminative status was unclear from code
inspection, I patched the worktree to revert the documented fix, rebuilt
the affected test binary, and ran it. Results captured:

- NM-09: revert continuous-while-held back to rising-edge → FAIL captured
  `[held_steady=1 btn_set=1 btn_cleared=0 held_still=1]`. ✓
- SS-13: revert `flash_cs_enable_` gate → FAIL captured
  `[open=ff exp=7F closed=ff exp=FF]`. ✓
- SX-11: drop the no-slave `rx_data_=0xFF` from `write_data` → FAIL
  captured `[baseline=42 exp=42  after=42 exp=FF]`. ✓
- SX-12: drop the no-slave `rx_data_=0xFF` from `read_data` → FAIL
  captured `[primed=77 resettle=77 r1=77 r2=77 exp r2=FF]`. ✓
- SD-17: drop the 0xFF gap-byte tolerance → FAIL captured
  `[match=0 got0=255 exp0=51]`. ✓
- SD-18 / SD-19: revert default-CMD to `queue_r1(0x00)` → SD-18 FAIL
  `[r1=0]`; SD-19 FAIL `[r1_55=0 r1_acmd42=0]`. ✓
- SD-20: restore the pre-fix illegal-on-non-ACMD41 branch → FAIL
  captured `[r1_55=0 r1_17=5 tok=0 b0=0]`. SD-19 also fired here
  because the revert masks the unhandled-ACMD path. ✓
- SD-15 + SD-16: revert BOTH pass-5 mount-reset AND pass-8 unmount-reset
  AND pass-5 cmd16 idle-bit derivation → only **SD-16 fired**
  `[init=5 uninit=5]`. **SD-15 passed** despite the revert. The test
  scenario routes through `init_card()` (CMD0 reaches state=IDLE via
  the new-CMD abort branch) and CMD17 then reads `data_block_` fresh
  from img2's file. The pre-fix bug (stale `multi_block_*` /
  `data_block_*` after a mid-stream remount) is not actually
  reachable through the test's stimulus.

After all reverts undone, full regression restored: divmmc 132/132,
sdcard 21/21, full ctest 37/37 PASS.

---

## 4 — Coverage gaps the agent missed

### 4.1 — SD-15 does not regression-test the pass-5 mount() fix

The agent's stated test shape is "mount img1, start CMD18 stream, then
mount(img2) WITHOUT explicit reset, verify a fresh CMD17 returns img2
content". The test does follow that shape, but then it inserts an
`init_card(sd)` between `mount(img2)` and the CMD17. `init_card` issues
CMD0 first; the `case State::IDLE` branch in `receive()` (sd_card.cpp:128
or the `(tx & 0xC0) == 0x40` mid-stream abort branch at :201-224)
recovers from any leftover stream state. By the time CMD17 runs, the
card is fully reset.

To make SD-15 actually catch the pass-5 bug, EITHER:
- (a) drop the `init_card(sd)` between mount(img2) and CMD17, and have
  the test rely on the partial state surfacing (e.g. issue a bare
  `read_block` and verify it returns img2's sector 0 — pre-fix would
  re-prime from img1's `data_block_`), OR
- (b) verify a state-introspecting accessor (e.g. `multi_block_sector_`,
  if exposed) is zero post-mount.

Recommendation: keep SD-15 as a sanity test (the round-trip integrity
check has value), but DOWNGRADE its claim. The report's coverage table
should mark 4a as "indirectly exercised; defective regression sentinel
for pass-5 mount() fix — actual sentinel coverage is via the existing
`test_remount` row (BOOT-SD-01) and the deselect()/reset() symmetry
implicit in `unmount()`-then-`mount()`."

### 4.2 — Contract-tests vs fix-tests

DA-09, NA-09, NR-14, SS-14 are **contract tests** (they pin the
infrastructure the fix relies on) rather than **fix tests** (they would
fail if the fix code itself were reverted). The agent's report should
explicitly distinguish these two categories. Specifically:

- **DA-09**: claim is "tests 770f78d (Emulator::load_state re-sync)".
  Reality: tests that `DivMmc::save_state` doesn't persist
  `rom3_active_` — i.e., the precondition that makes the external sync
  necessary. Reverting the Emulator fix does NOT fail DA-09.
- **NA-09**: claim is "tests c54192d (Emulator::init NR 0x83 sync)".
  Reality: tests the NextReg-to-DivMmc gap pattern. Reverting the
  Emulator fix does NOT fail NA-09.
- **SS-14**: claim is "tests ff84d3e (Emulator wires SD on both CS0
  and CS1)". Reality: tests SpiMaster's CS-decode + sd_swap routing
  behavior when the same device is on both lines — but the test sets
  this up directly via `attach_device(0, &dev); attach_device(1, &dev)`,
  bypassing Emulator. Reverting the Emulator change does NOT fail SS-14.
- **NR-14**: pre-fix the wildcard branch did not exist, so the
  negative case (rom3=0 → automap=false) holds vacuously pre-fix and
  post-fix. The test guards against a future regression where someone
  removes the `&& rom3_path_eligible` gate.

These are still useful tests (they're the right unit-tier shape — the
Emulator-tier coverage is genuinely integration-tier per §3.1/§3.2),
but the report's "covered" claim is misleading.

### 4.3 — Inline-comment commit attribution error in NR-14

The NR-14 inline comment in `divmmc_test.cpp:951` reads:

```
// NR-14 (TASK2-VERIFY9 commit ff84d3e): the $3Dxx wildcard is
```

But ff84d3e does not touch `src/peripheral/divmmc.cpp` at all
(verified: `git show ff84d3e --stat` shows only `emulator.cpp` and
`sd_card.cpp`). The actual fix that introduced the $3Dxx wildcard
branch is `399c9ae` (P1/P2). The agent's report TABLE correctly
attributes it to 399c9ae (row 1b), but the test source comment is
wrong.

Recommendation: rename to `(TASK2-VERIFY1/2 commit 399c9ae)` or
similar, matching the table row. Low severity — does not affect
test correctness, only future debugging.

### 4.4 — SS-12 / NM-05 cited as already-covered: spot-check OK

Verified:
- SS-12 at divmmc_test.cpp:2477+ exists, is the PASS-7 ce29402 fix
  test, and asserts the post-reset device round-trip. ✓
- NM-05 at divmmc_test.cpp:1503+ tests `enabled(true→false)` clears
  `button_nmi_` (VHDL divmmc.vhd:108 / zxnext.vhd:4112). This covers
  the **DivMmc-internal invariant** but NOT the **Emulator call-site
  gate** d54a053 fix #1 added. The deferral to integration is OK,
  but the report's claim "the Emulator-tier gate is a defensive
  duplicate of that VHDL clause; no additional unit row is needed"
  understates: the d54a053 fix gates the *strobe forwarding* on
  `divmmc.is_enabled()`, which is logically different from "DivMmc
  clears button_nmi when becoming disabled". A regression where
  someone removes the Emulator gate would not fail any current test;
  NM-05 wouldn't catch it because NM-05 doesn't run NmiSource.

This is a **genuine coverage gap** the agent should mark explicitly
in §3.2 rather than waving away.

### 4.5 — SD-19 dual-fault sensitivity

When testing SD-20 by reverting the CMD55 fall-through fix, SD-19
also failed (because the pre-fix code path returns R1=0x05 for
non-ACMD41 instead of the post-fix illegal-on-default branch). This
is fine — both fixes are in the same commit (6ebfd2b), so SD-19
covers both legs.

---

## 5 — Code-quality nits

1. **NR-14 commit attribution wrong** in the inline comment (see §4.3).
2. **SD-15 test description overstates** what it actually exercises;
   the comment should describe it as "post-remount round-trip integrity"
   not "pre-fix would surface img1 stream state instead" — the latter
   is not true given init_card and the mid-stream-abort branch.
3. **DA-09 / NA-09 / SS-14** would benefit from a "contract-pin /
   precondition" prefix in the comment instead of presenting them as
   regression tests for the Emulator-tier fix. Existing comments are
   honest about the contract framing but the row labels in the
   coverage table imply something stronger.
4. SD-17 helpfully re-writes the fixture sector after the test to keep
   the shared `sd` clean — good hygiene; pattern should be documented
   in the test plan as a convention.
5. The agent's report at §3.1 / §3.2 correctly identifies the two
   integration-tier deferrals. They are genuinely integration-tier;
   accept.

---

## 6 — VHDL / SD-spec citation spot-checks

Sampled citations against the worktree's VHDL files
(`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`)
where directly readable, otherwise cross-checked against the agent's
fix commits which themselves cite line numbers:

- NR-14 cites `zxnext.vhd:2898-2899` and `divmmc.vhd:130` — match the
  399c9ae fix's own citations. ✓
- NM-09 cites `divmmc.vhd:112-113` (continuous-while-held clear). ✓
- SS-13 cites `zxnext.vhd:3319` (composite Flash-CS gate). ✓
- SS-14 cites `zxnext.vhd:3280` (i_SPI_SD_MISO MUX). ✓
- SX-11 / SX-12 cite `zxnext.vhd:3278-3280` (default-else
  `spi_miso<='1'`). ✓
- SD-17 cites SD Phys Layer Spec 6.00 § 7.3.3.2 (gap-byte tolerance).
  Matches the public spec wording. ✓
- SD-18 / SD-19 cite SD spec § 7.3.2.1 (R1 illegal-cmd bit). ✓
- SD-20 cites SD spec § 4.3.9.1 (CMD55 + non-ACMD fall-through). ✓
- DA-09 cites `zxnext.vhd:2981-3008,:3138` (sram_pre_rom3 derivation). ✓
- NA-09 cites `zxnext.vhd:5052-5057` (NR 0x83 reset reload). ✓

All cited line numbers are plausible and consistent with the agent's
fix commits. No cherry-picking detected.

---

## 7 — Test status (this worktree, all reverts undone)

```text
$ LANG=C ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
1/4 Test  #1: fuse_z80_tests ...................   Passed
2/4 Test #19: divmmc_tests .....................   Passed
3/4 Test #20: sdcard_tests .....................   Passed
4/4 Test #21: sd_rom_extractor_tests ...........   Passed
100% tests passed, 0 tests failed out of 4

$ LANG=C ctest --test-dir build
100% tests passed, 0 tests failed out of 37

$ ./build/test/divmmc_test  → 132 checks, 0 skips, 132 plan rows covered
$ ./build/test/sdcard_test  → 21/21 PASS, 0 fail, 0 skip
```

No regressions across any subsystem.

---

## 8 — Recommended actions (non-blocking)

1. Fix NR-14 inline comment commit attribution
   (`ff84d3e` → `399c9ae`).
2. Either redesign SD-15 to actually catch the pass-5 bug (drop the
   `init_card` between mount and CMD17, or test multi_block_sector_
   directly), OR downgrade its description in the report and the test
   comment to "post-remount round-trip integrity, NOT a regression
   sentinel for the pass-5 mount-reset fix".
3. Mark DA-09 / NA-09 / SS-14 explicitly as "contract pins" (not
   regression sentinels) in the report's coverage table.
4. The d54a053 fix #1 (button_nmi gated on `divmmc.is_enabled()` at
   Emulator call site) deserves a clearer caveat: NM-05 does NOT
   cover the Emulator-side gate. Mark as a real integration-tier
   deferral, not a "covered" item.
5. Consider adding an integration-tier test plan entry for the two
   §3.1 / §3.2 deferrals so they don't get lost.

None of these are blockers. The test additions are net-positive
coverage and the build is clean.

---

## 9 — Final verdict

**APPROVE-WITH-NITS.**

- 9 of 14 new tests are fully discriminative against the fix they
  claim to regression-test (NM-09, SS-13, SX-11, SX-12, SD-16, SD-17,
  SD-18, SD-19, SD-20).
- 4 of 14 are contract / precondition / future-regression sentinels
  rather than direct regression sentinels for the original fix
  (NR-14, DA-09, NA-09, SS-14). Useful but mislabeled in the
  coverage table.
- 1 of 14 is **defective** as a regression sentinel for its claimed
  fix (SD-15) — passes both pre-fix and post-fix; useful only as a
  general round-trip sanity check.
- 0 fabricated tests against non-existent fixes.
- 0 tests with wrong VHDL/spec citations.

The agent's framing in the report ("14 of 14 covered") is **slightly
overstated** — the honest framing is "9 fix-tests + 4 contract-pins
+ 1 defective sentinel + 2 deferred to integration". The deferrals
themselves are correctly classified.

Net assessment: the audit moved coverage forward materially. With
the recommended cleanups (commit attribution, SD-15 redesign-or-
relabel, contract-pin labeling), this would be a clean APPROVE.

---

## 10 — Reviewer artefacts

- This document
- Discriminative-revert experiments performed in-place; reverts
  undone, working tree clean apart from this report.
- No production code modifications.
- No tests modified.
- All ctest entries green.
