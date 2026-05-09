# DivMMC + SD + SPI — Test-Coverage Audit Reviewer Findings — Fix Follow-up

Worktree: `task2/testcov-divmmc-sd-spi-fix`
Base review: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-REVIEW.md`
Original audit: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI.md`
Reviewer commit: `196f67a`

This document records the per-finding action taken in response to the
reviewer's `APPROVE-WITH-NITS` verdict. The reviewer flagged 1
defective test (SD-15), 1 commit-attribution error (NR-14), 3
contract-pins masquerading as regression sentinels (DA-09 / NA-09 /
SS-14), and 1 mis-classified integration-tier deferral (d54a053
fix #1).

---

## 1 — Per-finding disposition

| # | Finding                                                   | Reviewer rec.                | Action taken           | Status |
|---|-----------------------------------------------------------|------------------------------|------------------------|--------|
| 1 | SD-15 defective — passes pre-fix and post-fix             | Redesign or downgrade        | **Redesigned**         | Fixed  |
| 2 | NR-14 inline comment cites wrong commit `ff84d3e`         | Fix attribution              | **Fixed** to `399c9ae` | Fixed  |
| 3 | DA-09 / NA-09 / SS-14 are contract-pins, not fix-tests    | Add discriminative or relabel| **Relabeled**          | Fixed  |
| 4 | d54a053 fix #1 button_nmi-on-disabled gate uncovered      | Mark as integration deferral | **Documented**         | Documented |

All four findings addressed. No source code (src/) changes needed —
all changes are test code, comments, and the audit report.

---

## 2 — Finding-by-finding detail

### 2.1 — SD-15 redesigned to be discriminative

**Reviewer claim**: SD-15 passes both pre-fix (pass-5 mount-reset
revert) and post-fix; only SD-16 fires when both pass-5 and pass-8
fixes are reverted.

**Root cause**: the original test design called `init_card(sd)`
between `mount(img2)` and CMD17. `init_card` issues CMD0 followed by
CMD8/CMD55/ACMD41/CMD58. After CMD8's `resp_buf_` (6 bytes:
`{0xFF, 0x01, 0x00, 0x00, 0x01, check}`) is partially-drained by
`send_cmd_r1()` — which reads only until the first non-0xFF byte —
the next command (CMD55) arrives with `state_=RESPONDING`. The
default-state abort branch in `receive()` (sd_card.cpp:201-218)
fires, and clears `multi_block_`, `multi_block_sector_`, and
`pending_write_after_r1_`. By the time CMD17 runs, the leak has
been masked. CMD17 then unconditionally re-reads `data_block_` from
the new `file_`, so visible content is always img2-correct.

**Discriminative redesign**: probe `persistent_response_byte_`
instead of `multi_block_` / `data_block_`. After issuing a single
CMD0 on img1, `cmd0_go_idle()` (sd_card.cpp:431) sets
`persistent_response_byte_=0x01`. Mount(img2):

- **Pre-fix** (only `state_/initialized_/app_cmd_/cmd_idx_` cleared):
  `persistent_response_byte_=0x01` LEAKED.
- **Post-fix** (`reset()` called): `persistent_response_byte_=0xFF`.

A bare `send()` in IDLE state returns `persistent_response_byte_`
verbatim (sd_card.cpp:236-243). No abort branch fires, no command is
issued, no write touches `data_block_`. The IDLE-branch send() is
the shortest path that surfaces the leak.

**Discriminative-revert verification**:
1. Backed up `src/peripheral/sd_card.cpp` to `/tmp/sd_card.cpp.bak`.
2. Reverted both pass-5 mount fix and pass-8 unmount fix to pre-fix
   state (replaced the `reset()` call with the old field-by-field
   clearing).
3. Rebuilt sdcard_test, ran: **SD-15 FAIL** captured —
   `[r1_cmd0=1 leaked=1 r1_17=0 tok17=1 got4=204 got0=2]`. The
   `leaked=1` (= 0x01) confirms the persistent_response_byte_ leak.
4. Restored `src/peripheral/sd_card.cpp` from backup.
5. Rebuilt, ran: **SD-15 PASS**. All 21/21 sdcard tests green.

The redesign is genuinely discriminative against the pass-5 mount-
reset fix. The probe — a single bare `send()` — is the cleanest
possible signal. Test code documented in detail at
`test/sdcard/sdcard_test.cpp:595-696` with the multi-block-leak
analysis the reviewer requested.

### 2.2 — NR-14 commit attribution corrected

**Reviewer claim**: Inline comment at `divmmc_test.cpp:951` cited
commit `ff84d3e` but `git show ff84d3e --stat` shows that commit
only touches `emulator.cpp` and `sd_card.cpp` — it does NOT touch
`src/peripheral/divmmc.cpp` and is therefore not the fix that
introduced the `$3Dxx` wildcard branch. The actual fix is `399c9ae`
(P1/P2), which the report's table correctly attributes (row 1b) but
the test source comment did not.

**Action**: Fixed inline comment. Replaced `(TASK2-VERIFY9 commit
ff84d3e)` with `(CONTRACT-PIN; TASK2-VERIFY1/2 commit 399c9ae)` and
added a multi-paragraph block explaining (a) what the fix does,
(b) why this row is a contract-pin not a regression sentinel
(pre-fix the wildcard branch did not exist, so rom3=0 also yielded
automap=false trivially), and (c) the explicit attribution
correction. See `test/divmmc/divmmc_test.cpp:951-980`.

The check() label was also updated to prefix `CONTRACT-PIN:` and
spell out the future-regression sentinel framing.

### 2.3 — DA-09 / NA-09 / SS-14 relabeled as contract-pins

**Reviewer claim**: All three test the precondition / contract that
the Emulator-tier fix relies on, but reverting the actual Emulator
fix code does NOT make these tests fail. They are useful as contract
pins (catch future churn that breaks the precondition) but not as
discriminative regression sentinels for the original fix.

**Decision**: Relabel rather than add discriminative rows. Adding a
discriminative row would require constructing a full `Emulator`
instance with NextReg + Mmu + Saveable + StateReader/Writer wiring
inside a unit test — that would re-implement integration-tier
infrastructure inside a unit test and is out of scope.

**Actions** (test code comments + `check()` labels):

- **DA-09** (`test/divmmc/divmmc_test.cpp:1112+`):
  - Comment block now opens with `CONTRACT-PIN; TASK2-PASS10 commit
    770f78d` and explicitly states "this is a CONTRACT-PIN, not a
    discriminative regression sentinel for the 770f78d fix itself.
    Reverting Emulator::load_state's set_rom3_active() does NOT make
    this test fail".
  - `check()` label prefixed with `CONTRACT-PIN:` and the trailing
    description spells out "NOT a discriminative sentinel for
    770f78d ... integration-tier coverage required for the actual
    fix path".

- **NA-09** (`test/divmmc/divmmc_test.cpp:2110+`):
  - Same shape as DA-09. Comment block prefix `CONTRACT-PIN`,
    explicit reviewer-finding citation, and integration-tier-
    required note. `check()` label updated similarly.

- **SS-14** (`test/divmmc/divmmc_test.cpp:2595+`):
  - Same shape. Note that SS-14 sets up `attach_device(0, &dev);
    attach_device(1, &dev);` directly, bypassing Emulator::init —
    that's the bypassing pattern the reviewer flagged. Comment
    block + `check()` label updated.

### 2.4 — d54a053 fix #1 documented as integration-tier deferral

**Reviewer claim**: The original audit at §3.2 claimed NM-05 covered
the Emulator-side gate that gates `nmi_source_.divmmc_button_strobe()`
forwarding on `divmmc_.is_enabled()`. Reviewer disputes: NM-05 only
tests the DivMmc-internal `enabled(true→false) clears button_nmi_`
invariant — that is logically distinct from the Emulator-tier gate
that prevents the strobe from being forwarded when DivMmc is already
disabled. A regression where someone removes the Emulator-side
guard would NOT fail any current test.

**Action**: Updated `doc/issues/nextzxos-boot/
NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI.md` §3.2 to:

1. Re-classify as **INTEGRATION-TIER DEFERRAL** (was previously
   "covered by NM-05").
2. Spell out the logical distinction between the two gates:
   - DivMmc-internal: when DivMmc transitions enabled→disabled,
     button_nmi_ clears (NM-05 covers).
   - Emulator-tier: when DivMmc is already disabled, the Emulator
     MUST NOT forward a button strobe (uncovered).
3. Sketch the recommended integration test: with
   `divmmc_.set_enabled(false)`, simulate an NmiSource button
   strobe via the public emulator hook, run one frame of execution,
   and assert no NMI was dispatched. Pre-fix the strobe would
   forward unconditionally; post-fix it would be gated.

The audit report's coverage table (§2) was also updated:
- Row 2a (`d54a053 — button_nmi gated on divmmc.is_enabled`) now
  reads "INTEGRATION-TIER DEFERRAL — see §3.2 (NM-05 doesn't cover
  Emulator gate)" instead of "Emulator-call-site gate; equivalent
  VHDL invariant covered by NM-05".

No new unit test added — adding one would require integration-tier
infrastructure (NmiSource + Emulator frame loop) that is out of
scope for divmmc_test.cpp.

---

## 3 — Audit-report cleanup (§2 coverage table)

The original report's Section 2 coverage table claimed
"14/14 fixes have at least one dedicated row". The reviewer pointed
out this was overstated: 4 of the 14 rows are contract-pins, not
discriminative sentinels. The table was rewritten to:

- Mark each row as `discriminative`, `CONTRACT-PIN`, or
  `INTEGRATION-TIER DEFERRAL`.
- Add an honest tally: 9 fully-discriminative, 4 contract-pins,
  3 already-covered, 2 integration-tier deferrals.

This matches the reviewer's framing of "9 fix-tests + 4 contract-pins
+ 1 defective sentinel + 2 deferred to integration" — except SD-15
is no longer defective after the redesign in §2.1, so the post-fix
tally is **10 discriminative + 4 contract-pins + 3 already-covered
+ 2 integration-tier deferrals** = 14 total fixes covered, 12 with
a dedicated unit-tier row, 9-of-12 fully discriminative against the
original fix path.

---

## 4 — Discriminative-check methodology

The reviewer required: "for each new/changed test, mentally revert
the fix and verify the test would FAIL. If it doesn't, the test is
still defective."

For SD-15 (the only test code change with a real semantic shift), I
performed an in-place revert experiment — patched `sd_card.cpp` to
the pre-fix mount() and unmount() bodies, rebuilt, ran sdcard_test,
captured FAIL, restored sd_card.cpp from a `/tmp` backup, rebuilt,
ran sdcard_test, captured PASS. Documented in §2.1.

For NR-14 / DA-09 / NA-09 / SS-14, no in-place revert experiment was
needed — the reviewer's analysis already established these are
contract-pins (cannot be made discriminative without integration-tier
infrastructure). The test code change is comment-and-label only;
behaviour unchanged. The relabeling makes the framing honest, which
is what the reviewer asked for.

---

## 5 — Test status (post-fix, after all reviewer findings addressed)

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

No regressions. SD-15 verified discriminative via revert experiment.

---

## 6 — Summary

- **1 defective test fixed** (SD-15 — redesigned to probe
  `persistent_response_byte_` instead of `multi_block_`/`data_block_`,
  now genuinely discriminative against the pass-5 mount-reset fix).
- **1 commit attribution error fixed** (NR-14 inline comment now
  cites `399c9ae` not `ff84d3e`; row also relabeled as CONTRACT-PIN).
- **3 contract-pins explicitly relabeled** (DA-09, NA-09, SS-14 —
  comments + check() labels now make the contract-pin framing
  obvious; integration-tier-required note added).
- **1 integration-tier deferral documented** (d54a053 fix #1
  button_nmi-on-disabled gate — audit report §3.2 reclassified;
  recommended integration test sketched).
- **Audit report §2 coverage table rewritten** — honest tally with
  per-row classification.

All test changes verified discriminative or honestly-relabeled. No
source code changes. Branch HEAD updated; working tree clean.

