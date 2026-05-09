# DivMMC + SD + SPI — Test-Coverage Audit Fix-Followup Independent Review

Reviewer worktree: `task2/testcov-divmmc-sd-spi-fix-reviewer`
Reviewed agent commit: `4ec5bfa` (branch `task2/testcov-divmmc-sd-spi-fix`)
Reviewed report: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-FIX.md`
Original audit: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI.md`
First-pass review: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-REVIEW.md`

Reviewer mandate: independently verify each of the 4 dispositions the
fix agent claimed to address. Be very critical — verify
discriminativeness in-place by reverting the fix and observing the test
fail. Reject hand-waving.

---

## 1 — Verdict

**APPROVE.**

All 4 reviewer findings are confirmed addressed. The SD-15 redesign is
genuinely discriminative against the pass-5 mount-reset fix (verified
by in-place revert: pre-fix produces FAIL with `leaked=1`, post-fix
PASS). The NR-14 commit attribution is now correct. DA-09/NA-09/SS-14
are honestly relabelled as CONTRACT-PINs with explicit "NOT a
discriminative sentinel" wording. The d54a053 button_nmi gate is
genuinely an Emulator-tier deferral that cannot be unit-tested without
re-implementing integration-tier infrastructure.

Build clean. ctest 37/37 PASS, divmmc 132/132, sdcard 21/21,
sd_rom_extractor PASS, fuse_z80 1356/1356.

---

## 2 — Per-finding independent verification

| # | Finding                                       | Agent claim                                            | Independently verified                                      | Verdict            |
|---|-----------------------------------------------|--------------------------------------------------------|-------------------------------------------------------------|--------------------|
| 1 | SD-15 redesigned                              | Discriminative; pre-fix FAIL `[leaked=1]`              | **In-place revert performed.** Backed up `sd_card.cpp`; replaced both pass-5 mount() `reset()` and pass-8 unmount() `reset()` with pre-fix partial field-clears (mount: state_/initialized_/app_cmd_/cmd_idx_; unmount: state_/initialized_/file_size_/pending_write_after_r1_). Rebuilt sdcard_test, ran. **SD-15 FAIL captured exactly as claimed: `[r1_cmd0=1 leaked=1 r1_17=0 tok17=1 got4=204 got0=2]`.** SD-16 did NOT fire (pass-5 cmd16 idle-bit fix not reverted). Restored sd_card.cpp from backup, rebuilt — all 21/21 PASS. | **CONFIRMED FIXED** |
| 2 | NR-14 commit ref `399c9ae`                    | 399c9ae touches `divmmc.cpp`; ff84d3e doesn't          | `git show 399c9ae --stat` shows `src/peripheral/divmmc.cpp` is touched (+66/-25) and the commit message describes the `$3Dxx` wildcard `rom3_instant_on` path gated on NR $BB bit 7 + rom3_path_eligible — the exact fix NR-14 pins. Test source at `divmmc_test.cpp:951` reads `(CONTRACT-PIN; TASK2-VERIFY1/2 commit 399c9ae)`. The check() label at line 977-986 prefixes `CONTRACT-PIN:` and ends with "NOT a discriminative regression sentinel for 399c9ae — pre-fix the wildcard branch did not exist; this row guards against future regressions". | **CONFIRMED FIXED** |
| 3 | DA-09 / NA-09 / SS-14 relabelled CONTRACT-PIN | Comments + check() labels updated                      | Read each test in turn: DA-09 at `divmmc_test.cpp:1112+` opens with `(CONTRACT-PIN; TASK2-PASS10 commit 770f78d)` and the check() label prefixes `CONTRACT-PIN:` and ends "NOT a discriminative sentinel for 770f78d — reverting the Emulator-tier fix does not fail this test (integration-tier coverage required)". NA-09 at `:2110+` opens with `(CONTRACT-PIN; TASK2-VERIFY6 commit c54192d)` and check() label "NOT a discriminative sentinel for c54192d". SS-14 at `:2595+` opens with `(CONTRACT-PIN; TASK2-VERIFY9 commit ff84d3e)` and check() label "NOT a discriminative sentinel for ff84d3e — test attaches dev directly to both CS lines, bypassing Emulator::init". All three label-edits faithfully describe what is and isn't tested. The comments' "integration-tier coverage required" framing is honest. | **CONFIRMED FIXED** |
| 4 | d54a053 button_nmi gate integration-tier      | Documented at §3.2 INTEGRATION-TIER DEFERRAL           | Read `git show d54a053 -- src/core/emulator.cpp`: fix gates `if (nmi_source_.divmmc_button_strobe())` with `&& divmmc_.is_enabled()` at TWO call sites: `Emulator::run_frame()` and `Emulator::execute_single_instruction()`. To discriminate, a unit test would need: NextReg, Mmu, Z80Cpu, NmiSource, DivMmc all wired together and a button-strobe injection path — i.e. the entire Emulator. The audit report §3.2 was rewritten with: re-classification (was "covered by NM-05", now "INTEGRATION-TIER DEFERRAL"), a clear logical distinction between DivMmc-internal invariant and Emulator-tier gate, and a recommended integration test sketch. The §2 coverage table row 2a now reads "INTEGRATION-TIER DEFERRAL — see §3.2". The deferral is genuinely correct: there is no unit-tier discriminative test that would not just re-test the construction of a mock. | **CONFIRMED FIXED**  |

---

## 3 — Discriminative-revert experiment detail (Finding #1)

I performed the in-place revert experiment that the reviewer's findings
required and the agent's report claimed to have performed:

### 3.1 — Revert procedure

1. `cp src/peripheral/sd_card.cpp /tmp/sd_card.cpp.bak`.
2. Edited `mount()` body to replace `reset()` with the pre-fix partial
   clear: `state_ = State::IDLE; initialized_ = false; app_cmd_ = false;
   cmd_idx_ = 0;` (no clear of resp_buf_/persistent_response_byte_/etc).
3. Edited `unmount()` body similarly: `state_ = State::IDLE;
   initialized_ = false; file_size_ = 0; pending_write_after_r1_ =
   false;` (no clear of persistent_response_byte_).
4. `cmake --build build -j$(nproc) --target sdcard_test`.
5. `./build/test/sdcard_test`.

### 3.2 — Captured FAIL output

```text
FAIL SD-15: mount() does full reset() — persistent_response_byte_ MUST
NOT leak across a runtime mount swap. ... [r1_cmd0=1 leaked=1 r1_17=0
tok17=1 got4=204 got0=2]
Total:   21  Passed:   20  Failed:    1  Skipped:    0
```

`leaked=1` (= 0x01) is exactly the `persistent_response_byte_` value
that `cmd0_go_idle()` sets at `sd_card.cpp:431`, surfaced via the bare
`send()` call in `state_=IDLE` (which returns
`persistent_response_byte_` verbatim per `sd_card.cpp:243`).

### 3.3 — Restoration

5. `cp /tmp/sd_card.cpp.bak src/peripheral/sd_card.cpp`.
6. `cmake --build build -j$(nproc) --target sdcard_test`.
7. `./build/test/sdcard_test` → **21/21 PASS**.

### 3.4 — Confirmation: the new design is genuinely discriminative

The discriminator is the bare `send()` call in IDLE state between
`mount(img2)` and `init_card(sd)`. This avoids the prior masking
mechanism (the default-state abort branch in `receive()` at
`sd_card.cpp:201-218` that fires on a CMD start byte and clears
`multi_block_*`/`persistent_response_byte_`/etc — which the reviewer's
first-pass analysis correctly identified). The redesigned probe is the
shortest path that surfaces the leak with no abort branch interference.

The agent's claim that "the IDLE-branch send() is the shortest path
that surfaces the leak" is correct. SD-15's PRIMARY discriminator is
`leaked_persistent==0xFF`. The round-trip integrity sub-check
(`got4==0xCC`, `got0==0x02`) is preserved as a sanity guard but is
not the primary discriminator.

---

## 4 — Section-2 coverage-table sanity check

Re-verified the rewritten coverage table in
`NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI.md` §2:

- 18 rows mapping to 9 source commits; sub-fixes (a/b/c) per commit
  individually addressed.
- Row classifications match the test-source labelling I confirmed in
  Section 2 above (DA-09/NA-09/SS-14/NR-14 = CONTRACT-PIN; SD-15/16/17/
  18/19/20/SX-11/SX-12/SS-13/NM-09 = discriminative; 1c + 2a =
  INTEGRATION-TIER DEFERRAL).

**Minor counting inconsistency (nit-only)**: The §2 narrative says
"9 fully-discriminative regression sentinels (SD-15 redesigned;
SD-16/SD-17/SD-18/SD-19/SD-20/NM-09/SS-13/SX-11/SX-12)" — but the
parenthetical list contains **10 names** (SD-15, SD-16, SD-17, SD-18,
SD-19, SD-20, NM-09, SS-13, SX-11, SX-12), and the FIX report's §6
summary correctly says "10 discriminative + 4 contract-pins + 3
already-covered + 2 integration-tier deferrals". The §2 narrative's
"9" and "12/14 fixes have a dedicated unit test, 9 of which are fully
discriminative" is internally inconsistent with the list. Should read
"10". Non-blocking — the test classifications themselves are correct
and the FIX report's accounting is right; only the audit report's
§2 narrative count needs a one-character edit.

---

## 5 — Code-quality nits

1. **Tally inconsistency in §2 narrative** of the audit report:
   "9 fully-discriminative" should be "10" (post SD-15 redesign).
   The FIX report's tally is correct; only the audit-report narrative
   sentence is stale. One-character fix.

2. **SD-15 redesigned test scenario is well-documented**: the inline
   comment block at `sdcard_test.cpp:600-660` walks through the masking
   mechanism (CMD55-after-CMD8 hits the abort branch in
   `receive()`:201-218), the cleanest leaked field
   (`persistent_response_byte_`), and the discriminator (bare `send()`
   in IDLE state). No further action needed.

3. **CONTRACT-PIN labels are uniformly applied** across DA-09 / NA-09 /
   SS-14 / NR-14: comment block prefix, check() label prefix, and the
   trailing "NOT a discriminative sentinel" descriptor are consistent.
   No further action needed.

4. **§3.2 INTEGRATION-TIER DEFERRAL is well-framed**: the logical
   distinction between DivMmc-internal invariant (NM-05 covers) vs
   Emulator-tier strobe-forwarding gate (uncovered by unit tests) is
   spelled out clearly. The recommended integration test sketch is
   actionable. No further action needed.

5. **No source/ changes were made** by this fix — only test-code
   comments, check() labels, and audit-report text. Confirmed by
   `git show 4ec5bfa --stat` showing only `test/divmmc/divmmc_test.cpp`,
   `test/sdcard/sdcard_test.cpp`, and 2 audit-report .md files modified.

---

## 6 — Test status (this worktree, after revert undone)

```text
$ LANG=C ctest --test-dir build -R 'divmmc|sd_card|sdcard|sd_rom|fuse_z80|spi'
1/4 Test  #1: fuse_z80_tests ...................   Passed
2/4 Test #19: divmmc_tests .....................   Passed
3/4 Test #20: sdcard_tests .....................   Passed
4/4 Test #21: sd_rom_extractor_tests ...........   Passed
100% tests passed, 0 tests failed out of 4

$ LANG=C ctest --test-dir build
100% tests passed, 0 tests failed out of 37

$ ./build/test/sdcard_test  → 21/21 PASS, 0 fail, 0 skip
```

No regressions. SD-15 verified discriminative via in-place revert
experiment performed independently of the agent.

---

## 7 — VHDL-faithfulness spot-check

Sampled the comments / VHDL citations newly added or revised in the fix:

- DA-09 cites `zxnext.vhd:2981-3008,:3138` (sram_pre_rom3 derivation)
  for the rom3_active_ feeder shadow — matches the original audit and
  the 770f78d commit's own citations.
- NA-09 cites `zxnext.vhd:5052-5057` (NR 0x83 reset reload, reset_type_1
  power-on default 0x80 from `:1230`) — correct.
- NR-14 cites `zxnext.vhd:2898-2899` and `divmmc.vhd:130` (rom3_active
  composite gate) — matches 399c9ae commit.
- SS-14 cites `zxnext.vhd:3280` (single i_SPI_SD_MISO MUX from
  spi_ss_sd1_n='0' or spi_ss_sd0_n='0') — matches.
- SD-15 cites `sd_card.cpp:201-218` (mid-stream-abort branch) and
  `sd_card.cpp:236-243` (IDLE-branch send returning
  persistent_response_byte_) — verified by direct file inspection.

No mis-citations introduced.

---

## 8 — Final verdict

**APPROVE.**

- 4 of 4 reviewer findings confirmed addressed.
- 0 of 4 still defective.
- 1 minor counting nit in audit-report §2 narrative (9 vs 10) —
  non-blocking; the FIX report's own tally is correct.
- SD-15 verified discriminative via independent in-place revert.
- All 37 ctest entries green; divmmc 132/132, sdcard 21/21,
  sd_rom_extractor PASS, fuse_z80 1356/1356.
- VHDL/SD-spec citations all verified correct.
- No source-code changes — net audit improvement is honest
  test-classification + one redesigned discriminative test.

The agent has materially improved the audit's honesty: SD-15 is now a
genuine regression sentinel (was: claimed-but-not-actually
discriminative), the contract-pin framing is explicit (was: implied),
and the integration-tier deferrals are clearly marked (was: hand-waved
"covered"). This is exactly the cleanup the first-pass review asked
for.

---

## 9 — Reviewer artefacts

- This document.
- In-place revert experiment performed on
  `src/peripheral/sd_card.cpp` (mount + unmount); restored to
  pre-revert state from `/tmp/sd_card.cpp.bak`.
- No production code modifications retained.
- No tests modified.
- All ctest entries green post-restore.
