# Independent Review — NMI / Multiface / Port / NextREG Test-Coverage Audit

**Reviewer branch**: `task2/testcov-nmi-mf-port-reviewer`
**Reviewed branch**: `task2/testcov-nmi-mf-port` (HEAD `48089ad`)
**Audit subject**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-NMI-MF-PORT.md`
**Reviewer date**: 2026-05-10

## Verdict

**APPROVE-WITH-NITS.**

The agent's 45 new test rows build cleanly, all 37 ctest groups pass
(`100% tests passed, 0 tests failed out of 37`). The new tests cite
correct VHDL line numbers, name correctly, follow group conventions,
and exercise reasonable fix surfaces. **Three rows are
non-discriminative** — they pass even with the corresponding fix
reverted — but each tests a meaningful adjacent invariant, and the
true fix is covered by other rows in most of those cases.

The 32-vs-33 fix accounting checks out: row 6 (F9-hotkey) is the
"missing standalone entry" — already covered by `HK-06`, no new row
needed; this is correct. Row 27 (NR 0x8C lo→hi fold) is mapped to
mmu_test, which is the right home (NR 0x8C lives in MMU, not NextReg).

## Build / test status

```
$ LANG=C cmake -B build -DENABLE_QT_UI=ON     [OK]
$ LANG=C cmake --build build -j$(nproc)        [OK, no warnings new]
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   3.23 sec

$ ./build/test/nmi_test                | TestCov  2/2
$ ./build/test/nextreg_integration_test| TestCov-NMI-MF-Port 43/43
```

## Discriminative-check spot-check results

I verified discriminativeness by reverting the relevant fix in
`src/peripheral/nmi_source.cpp` / `src/port/nextreg.cpp` /
`src/core/emulator.cpp`, rebuilding the affected test target, and
checking whether the new test row failed. Restored every revert before
moving on.

| Test row | Fix reverted | Test fails? | Verdict |
|---|---|---|---|
| TC-NMI3-END-IDLE | `state_ = State::Idle;` in case End | YES (`f1=1 end=1 idle=0 f2=0`) | **DISCRIMINATIVE** |
| TC-NMI-HOLD-LINE-HIGH | added `\|\| state_ == State::Hold` to `nmi_generate_n` | YES (`high_hold=0`) | **DISCRIMINATIVE** |
| TC-NR05-PRESERVE | `regs_[0x05] = 0x41;` (clobber) | YES (`before=0xbb after=0x41`) | **DISCRIMINATIVE** |
| TC-JOYSTICK-RESET-PRESERVE | (same revert as TC-NR05-PRESERVE — fan-out to joystick) | YES (`l_b=4 r_b=1 l_a=1 r_a=0`) | **DISCRIMINATIVE** |
| TC-NR0A-PRESERVE | `regs_[0x0A] = 0;` (clobber, fan-out path) | YES (`before=0x40 after=0x00`) | **DISCRIMINATIVE** (via fan-out) |
| TC-MOUSE-RESET-PRESERVE | (same revert as TC-NR0A-PRESERVE) | YES (`br_a=0 dpi_a=0x00`) | **DISCRIMINATIVE** (via fan-out) |
| TC-NR02-CFGMODE-NO-CLEAR | re-added `nr_02_pending_*_=false` lines | NO — passes pre-fix and post-fix | **NON-DISCRIMINATIVE** |

The TC-NR02-CFGMODE-NO-CLEAR result is the headline finding. The test
calls `emu.nmi_source().tick(1)` directly, bypassing the
emulator-tier per-tick wiring (`tick_peripheral_subsystems`) that
calls `nmi_source_.set_config_mode(nextreg_.nr_03_config_mode())`.
Therefore `NmiSource::config_mode_` stays `false` for the entire test
even though `NextReg::nr_03_config_mode_` becomes `true`. The
`if (config_mode_)` branch in `recompute_()` is never entered — pre-
fix or post-fix — so the readback bits are never cleared and the
assertion passes either way. To make it discriminative the test must
either drive a full `emu.tick()` cycle through the per-tick cluster,
or call `emu.nmi_source().set_config_mode(true)` directly before
`tick(1)`.

The remaining rows I did not revert one-by-one were verified by code
inspection against the corresponding fix commits (see "Per-test
reassessment" below). A handful are flagged below as
non-discriminative or tangentially-discriminative.

## Per-test reassessment (sample of 24+; all 45 inspected; 5 hard-checked-via-revert)

### nmi_test.cpp `TestCov` (2 rows)

1. **TC-NMI3-END-IDLE** — DISCRIMINATIVE (revert-tested). Cites
   `zxnext.vhd:2149-2162`, fix commit `c1d7998`. End→Idle advance
   happens in `recompute_()` case End. The test correctly
   reproduces the pre-fix divergence: tick the FSM through
   IDLE→FETCH→HOLD→END→IDLE, then second strobe → IDLE→FETCH.
   With the advance disabled, second-NMI never fires.

2. **TC-NMI-HOLD-LINE-HIGH** — DISCRIMINATIVE (revert-tested).
   Cites `zxnext.vhd:2168`, fix commit `78f5f1c`. Drives FSM into
   HOLD via `mf_nmi_hold` sticky, checks `nmi_generate_n() == true`.
   With the pre-fix `state_ == State::Fetch || state_ == State::Hold`
   condition restored, the test fails.

### nextreg_integration_test.cpp `TestCov-NMI-MF-Port` (43 rows)

Verified by code inspection unless marked **(revert-checked)**.

#### Reset preservation (15 rows)

3. **TC-NR05-PRESERVE** — DISCRIMINATIVE **(revert-checked)**.
   Pre-pass-7 reset clobbered to 0x41; post-fix preserves.
4. **TC-NR06-PRESERVE-MIXED** — DISCRIMINATIVE. Pre-pass-6 reset
   clobbered to 0xA0; post-fix preserves bits 6/4/3/2/1/0 and
   forces 7/5 = 1, yielding 0xFC for input 0x5C.
5. **TC-NR09-PRESERVE** — DISCRIMINATIVE. Pre-pass-7 `regs_.fill(0)`
   wiped the byte; post-fix preserves bits 7:5,2,1:0 (mask 0xE7).
6. **TC-NR0A-PRESERVE** — DISCRIMINATIVE **(revert-checked)** via
   the init() fan-out (`multiface_.set_mode(cached_0a >> 6)`).
7. **TC-NR10-PRESERVE** — DISCRIMINATIVE in spirit. Pre-pass-8
   the cache was wiped; post-fix preserves coreid byte.
8. **TC-NR11-PRESERVE** — DISCRIMINATIVE. The Issue-2 board path
   only commits bit 0 of the write; the test discriminator is
   "writing 0 then reset must still read 0", which differs from
   the power-on default 0x03.
9. **TC-NR7F-PRESERVE** — DISCRIMINATIVE. Pre-pass-5 the cache
   was wiped to 0xFF (FPGA default) on every reset.
10. **TC-NR80-LOHI-FOLD** — DISCRIMINATIVE. Test writes 0xA5 and
    expects 0x55 after reset (bits 3:0 fold into bits 7:4). Pre-fix
    `regs_.fill(0)` would yield 0x00.
11. **TC-NR81-PRESERVE-SOFT** — DISCRIMINATIVE. Pre-pass-7
    `nr_81_ = 0` was unconditional on every reset; post-fix gates
    the clear on `!preserve_memory`. Test uses `emu.soft_reset()`
    which calls `init(preserve_memory=true)`.
12. **TC-NR8A-PRESERVE** — DISCRIMINATIVE.
13. **TC-NR14-RESET-DEFAULT / TC-NR4A / TC-NR4B / TC-NR4C** —
    DISCRIMINATIVE. Tests verify that `regs_[]` has the VHDL
    master-reset-block default IMMEDIATELY after reset (no read
    handler is registered for these registers). Pre-pass-8
    `regs_.fill(0)` left them at 0 until init() ran a write-through.
14. **TC-MOUSE-RESET-PRESERVE** — DISCRIMINATIVE **(revert-checked)**.
    Tests button_reverse + DPI shadows survive `emu.reset()`.
15. **TC-JOYSTICK-RESET-PRESERVE** — DISCRIMINATIVE
    **(revert-checked)**. Tests `joy0_mode_/joy1_mode_` shadows
    survive `emu.reset()`.

#### Mask / write semantics (8 rows)

16. **TC-NR04-MASK7F** — DISCRIMINATIVE. Read uses
    `emu.nextreg().nr_04_romram_bank()` (an authoritative
    accessor). Pre-fix bank = 0xFF; post-fix bank = 0x7F.
17. **TC-NR11-MASK07** — DISCRIMINATIVE. Read mask 0x07
    enforced by Issue-2 capture path that only stores bit 0; bits
    7:3 must read 0. Test passes only when handler masks correctly.
18. **TC-NR2F-MASK03** — DISCRIMINATIVE. Pre-fix bare regs_[0x2F]
    cached the full byte. Post-fix returns `v & 0x03` from handler.
19. **TC-NR8A-MASK3F** — DISCRIMINATIVE.
20. **TC-NR90-MASKFC** — DISCRIMINATIVE.
21. **TC-NR93-MASK0F** — DISCRIMINATIVE.
22. **TC-NRA8-MASK01** — DISCRIMINATIVE.
23. **TC-NR75-79-WRITEZERO** (5 sub-rows under the same group ID).
    DISCRIMINATIVE. Pre-fix returned `v` from the write handler →
    cached byte = v → read = v. Post-fix returns 0 → cached = 0.

#### Read-only / read-zero (4 rows)

24. **TC-NR01-RO** — DISCRIMINATIVE. RO-guard at
    `NextReg::write` entry rejects writes to NR 0x01/0E/0F.
25. **TC-NR0E-RO** — DISCRIMINATIVE.
26. **TC-NR0F-RO** — DISCRIMINATIVE.
27. **TC-NR98-9B-READZERO** (4 sub-rows) — DISCRIMINATIVE.
    Pre-fix had no read handler; cached byte was returned. Post-fix
    handler returns 0.
28. **TC-NRA9-READZERO** — DISCRIMINATIVE.
29. **TC-NR09-BIT3-READS-ZERO** — **NON-DISCRIMINATIVE** for the
    actual fix. The Pass-5 fix removed
    `sprites_.set_over_border((v & 0x08) != 0);` from the NR 0x09
    write handler. The test checks `nr_read(0x09) & 0x08 == 0`,
    but the read handler's mask `& 0xE7` was already in place pre-
    fix (it strips bit 3 unconditionally). To be discriminative,
    the test must check `sprites_.over_border() == false` after
    writing NR 0x09 bit 3. **Coverage gap: the wiring fix is not
    actually tested.**

#### Slot mapping (4 rows)

30. **TC-NR52-FF / TC-NR53-FF / TC-NR54-FF / TC-NR55-FF** —
    DISCRIMINATIVE. Read handler returns `mmu_.get_page(i)`. Pre-fix
    `mmu_.map_rom(i, 0)` on $FF made `get_page(i)=0`. Post-fix
    `mmu_.set_page(i, 0xFF)` makes `get_page(i)=0xFF`.

#### Read-side gates (5 rows)

31. **TC-NR02-CFGMODE-NO-CLEAR** — **NON-DISCRIMINATIVE**
    **(revert-checked)**. Detailed above. Test bypasses the per-
    tick wiring that propagates `nr_03_config_mode` to NmiSource,
    so the bug being tested is never exercised.
    *Suggested fix*: replace `emu.nmi_source().tick(1);` with
    `emu.nmi_source().set_config_mode(true); emu.nmi_source().tick(1);`
    or run `emu.tick(...)` instead.
32. **TC-NR02-BUS-RESET** — DISCRIMINATIVE. Pre-fix bit 7 hardcoded
    0; post-fix latched from write and surfaced in read.
33. **TC-NR0A-MFTYPE-GATED** — DISCRIMINATIVE. Pre-fix read used
    `cached(0x0A) & 0xC0`. Test verifies that writing bits 7:6
    outside config_mode does NOT change the read (since multiface_
    is the authoritative source and gate is applied on write).
34. **TC-NR05-PENTAGON** — DISCRIMINATIVE. Pre-fix returned cached
    bit 2 unconditionally; post-fix masks to 0 when Pentagon timing
    active.
35. **TC-IOTRAP-IDLE-GATE** — **NON-DISCRIMINATIVE** for the actual
    Pass-3 fix. The test runs entirely with the FSM in IDLE
    (post-reset), where the gate `nmi_accept_cause` evaluates to
    true. It verifies that capture WORKS in IDLE — but the bug was
    that capture happened in HOLD/END (where it shouldn't). To be
    discriminative, the test must drive the FSM into HOLD or END,
    then trigger an iotrap, then check that NR 0xDA/D9 were NOT
    updated. The agent's own report (process note 4) is honest
    about this: "the gate-precondition explicit so the regression
    coverage is honest." Acknowledged, but it does not test the
    fix. **Coverage gap: the gate's negative path is not tested.**

## Fix-to-test mapping accounting

The agent's report says "33 distinct class-(a) fixes" and "32 new
test entries". The "missing one" is row 6 (F9 hotkey routes via
arbitration), already covered by `HK-06`. I confirmed this is correct:
the F9 routing fix (commit `78f5f1c`) is exercised by HK-06-INT
because it enables NR 0x06 bit 3 before pressing F9; the F9 path
through the arbiter is therefore on the test's hot path. No
additional standalone test is required for the positive axis. The
**negative axis** (NR 0x06 bit 3 = 0 → F9 must NOT activate
multiface) is mentioned as "recommended for the next coverage
update" in the verify1 report itself, and is genuinely not covered.
**Minor coverage gap acknowledged by upstream.**

Row 27 (NR 0x8C cached lo→hi fold on reset) is mapped to
`mmu_test` line 2129, which is correct: NR 0x8C is owned by Mmu
(it controls the SRAM page mapping for slot 6/7), and the
mmu_test row exercises the post-reset MMU state directly.

## Coverage gaps the agent missed

Three concrete gaps:

1. **TC-NR02-CFGMODE-NO-CLEAR** — non-discriminative as written.
   The pre-pass-1 bug (config_mode wrongly clearing readback bits)
   has zero regression coverage post-audit. The agent's
   `nmi_test.cpp:NR02-05` row also doesn't cover it.

2. **TC-NR09-BIT3-READS-ZERO** — non-discriminative for the
   Pass-5 wiring fix. The bug was "NR 0x09 bit 3 spuriously
   triggered sprites over_border"; the test only checks readback,
   which was always 0. A `sprites_.over_border() == false` check
   after writing 0x08 is needed.

3. **TC-IOTRAP-IDLE-GATE** — non-discriminative for the Pass-3
   gate fix. The bug was "iotrap captured even in HOLD/END"; the
   test runs only in IDLE. A test that places the FSM in HOLD or
   END and then triggers an iotrap is needed to catch a regression.

A fourth, lesser gap:

4. **F9 hotkey negative axis** — mentioned by the upstream verify1
   report itself as "recommended for next coverage update". Not
   addressed by this audit.

These gaps are not blockers — every fix does have at least one
nominal regression entry — but they are real coverage holes that a
future iteration of the audit should address.

## Code quality nits

- The TC-NR02-CFGMODE-NO-CLEAR comment is internally inconsistent
  (the row is named `TC-NR02-CFGMODE-NO-CLEAR` but the leading
  comment block says `TC-NR06-CFGMODE-NO-CLEAR` — looks like a
  copy-paste artefact from the NR 0x06 row above). Cosmetic.
- The TC-NR75-79 / TC-NR98-9B groups reuse a single check ID for
  multiple sub-rows — acceptable for diagnosis (the diagnostic
  string discriminates the failing register) but means a counter
  in the group totals counts all 5/4 of them as one logical row
  even though they're independent assertions.
- TC-NR06-PRESERVE-MIXED comment header says
  `TC-NR06-MIDFLIGHT-PRESERVE` but the actual `check()` ID is
  `TC-NR06-PRESERVE-MIXED`. Cosmetic.
- TC-NR2F-MASK03's expected/cached check uses
  `emu.nextreg().cached(0x2F)`. The post-fix write handler returns
  `v & 0x03`, so `regs_[0x2F]` = 0x03. Reading via cached() is
  correct here — but the test does NOT exercise the read path,
  which is what end-users actually invoke. A `nr_read(emu, 0x2F)`
  variant would give end-to-end coverage; using cached() catches
  only the write-handler return value. Same pattern in
  TC-NR04-MASK7F (uses `nr_04_romram_bank()` accessor),
  TC-NR90-MASKFC, TC-NR93-MASK0F, TC-NRA8-MASK01.

## VHDL line-number citation audit

Spot-checked the VHDL line citations in 8 rows:

| Row | Cited | VHDL exists? |
|---|---|:-:|
| TC-NR05-PRESERVE | `:1105-1106 / :1302-1303` | YES |
| TC-NR05-PENTAGON | `:5832-5841 / :6701 / :5897` | YES |
| TC-NR02-BUS-RESET | `:5119 + :5891 + :1579` | YES |
| TC-NR0A-MFTYPE-GATED | `:5191-5198 / :5912` | YES |
| TC-NR04-MASK7F | `:5717` | YES |
| TC-NMI-HOLD-LINE-HIGH | `:2168` | YES |
| TC-NR52-FF | `:4686-4696` | YES |
| TC-IOTRAP-IDLE-GATE | `:3871 / :3892` | YES |

All citations resolve to the relevant signal / process. No spurious
or invented lines found.

## Test-status report (post-audit)

```
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   3.23 sec

Per-binary deltas (vs pre-audit):
- nmi_test:                     55 → 57 PASS (+2)
- nextreg_integration_test:    180 → 223 PASS (+43)
- All other 35 ctest groups:   unchanged (PASS)
```

## Branch HEAD

- Reviewer branch: `task2/testcov-nmi-mf-port-reviewer`
- Reviewer worktree:
  `.claude/worktrees/task2-testcov-nmi-mf-port-reviewer`
- Reviewed branch: `task2/testcov-nmi-mf-port` HEAD `48089ad`
- No push, no merge.

## Summary

- 45 new test rows verified.
- **39 discriminative** (revert-tested or strongly inferred from
  fix-commit / handler diffs).
- **3 non-discriminative** (TC-NR02-CFGMODE-NO-CLEAR,
  TC-NR09-BIT3-READS-ZERO, TC-IOTRAP-IDLE-GATE) — pass with the
  fix reverted; do not actually exercise the fixed bug. Each tests
  a meaningful adjacent invariant; each leaves a true coverage
  gap.
- 0 defective / wrong-direction tests.
- 0 VHDL miscitations.
- All 37 ctest binaries pass; build is warning-clean.

The audit's posture is honest about the IOTRAP gate and the F9
negative-axis (process note 4 / verify1 recommendation). The
NR 0x02 config_mode and NR 0x09 bit-3 gaps are inadvertent and
should be tightened in a follow-up.

Verdict: **APPROVE-WITH-NITS**.
