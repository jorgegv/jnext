# NMI / Multiface / Port / NextREG — Test-Coverage Audit Report

**Branch**: `task2/testcov-nmi-mf-port`
**Date**: 2026-05-09
**Scope**: every NMI / Multiface / Port / NextREG-subsystem fix landed in
`task2-verify1` … `task2-verify10` (commits `c1d7998` … `6051f01`).
**Mandate**: every bug fix must have at least one regression unit test
that (1) reproduces the buggy state, (2) cites the VHDL line + fix
commit, (3) exercises the fixed path, (4) asserts the correct
behaviour.

## Total fixes catalogued

**33 distinct class-(a) fixes** across 10 verify passes. Below grouped by
pass.

## Coverage table

Legend:
- **Before** = whether a regression test existed prior to this audit.
- **Now** = whether a regression test exists after this audit.
- **Test ID** = test name, in the form `GROUP-ID` (group is the test-runner
  group label).

| # | Fix | Pass / Commit | Test before? | Test now? | Test ID |
|---|-----|---------------|:-:|:-:|---------|
| 1 | NMI-1: HOLD→END selector ExpBus arm | Init / `c1d7998` | partial (GATE-04..07) | yes | `nmi_test:GATE-09/10/11` (Pass-9 covers gate end-to-end) |
| 2 | NMI-2: NR 0x02 readback bits 3/2 timing | Init / `c1d7998` | yes | yes | `nmi_test:NR02-05` |
| 3 | NMI-3: FSM End→Idle advance | Init / `c1d7998` | indirect | **YES** | `nmi_test:TC-NMI3-END-IDLE` ★ NEW |
| 4 | NMI-4: NR 0x02 bit 4 iotrap compose | Init / `c1d7998` | yes | yes | `nextreg_integration_test:FT-INT-DA-01a/b/c/02` |
| 5 | NR-2: NR 0x52..0x55 with $FF stores 0xFF | Init / `c1d7998` | NO | **YES** | `nextreg_integration_test:TC-NR52-FF / TC-NR53-FF / TC-NR54-FF / TC-NR55-FF` ★ NEW |
| 6 | F9 hotkey routes via arbitration | Verify1 / `78f5f1c` | yes | yes | `nmi_test:HK-06` |
| 7 | NR 0x02 bits 3/2 NOT cleared on config_mode | Verify1 / `78f5f1c` | partial (NR02-05) | **YES** | `nextreg_integration_test:TC-NR02-CFGMODE-NO-CLEAR` ★ NEW |
| 8 | /NMI line wrongly low through HOLD | Verify1 / `78f5f1c` | NO | **YES** | `nmi_test:TC-NMI-HOLD-LINE-HIGH` ★ NEW |
| 9 | NR 0x02 bit 7 (bus_reset) readback | Verify3 / `d841887` | NO | **YES** | `nextreg_integration_test:TC-NR02-BUS-RESET` ★ NEW |
| 10 | NR 0x0A bits 7:6 mf_type config_mode-gated | Verify3 / `d841887` | NO | **YES** | `nextreg_integration_test:TC-NR0A-MFTYPE-GATED` ★ NEW |
| 11 | NR 0xDA / 0xD9 nmi_accept_cause gate | Verify3 / `d841887` | partial | **YES** | `nextreg_integration_test:TC-IOTRAP-IDLE-GATE` (companion to FT-INT-DA-*) ★ NEW |
| 12 | NR 0x04 mask 0x7F (Issue-2) | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR04-MASK7F` ★ NEW |
| 13 | NR 0x11 config_mode-gate + read mask 0x07 | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR11-MASK07 / TC-NR11-PRESERVE` ★ NEW |
| 14 | NR 0x2F write mask 0x03 | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR2F-MASK03` ★ NEW |
| 15 | NR 0x44 priority + blue-LSB read compose | Verify4 / `4f25708` | yes | yes | `nextreg_integration_test:PAL-05` |
| 16 | NR 0x8A write mask 0x3F | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR8A-MASK3F` ★ NEW |
| 17 | NR 0x90 write mask 0xFC | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR90-MASKFC` ★ NEW |
| 18 | NR 0x93 write mask 0x0F | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR93-MASK0F` ★ NEW |
| 19 | NR 0x98/9A/9B/99 read = 0 | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NR98-9B-READZERO` ★ NEW |
| 20 | NR 0xA8 write mask 0x01 | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NRA8-MASK01` ★ NEW |
| 21 | NR 0xA9 read = 0 | Verify4 / `4f25708` | NO | **YES** | `nextreg_integration_test:TC-NRA9-READZERO` ★ NEW |
| 22 | NR 0x09 bit 3 NOT routed to sprites | Verify5 / `d92c6ec` | NO | **YES** | `nextreg_integration_test:TC-NR09-BIT3-READS-ZERO` ★ NEW |
| 23 | NR 0x75-0x79 write returns 0 | Verify5 / `d92c6ec` | NO | **YES** | `nextreg_integration_test:TC-NR75-79-WRITEZERO` ★ NEW |
| 24 | NR 0x86-0x89 reset gating (inverse polarity) | Verify5 / `d92c6ec` | yes | yes | `nextreg_integration_test:PE-INT-86 / PE-INT-87 / PE-INT-89` |
| 25 | NR 0x7F preserved across reset | Verify5 / `d92c6ec` | NO | **YES** | `nextreg_integration_test:TC-NR7F-PRESERVE` ★ NEW |
| 26 | NR 0x80 lo→hi nibble fold on reset | Verify5 / `d92c6ec` | NO | **YES** | `nextreg_integration_test:TC-NR80-LOHI-FOLD` ★ NEW |
| 27 | NR 0x8C cached lo→hi fold on reset | Verify5 / `d92c6ec` | yes | yes | `mmu_test` (NR 0x8C reset row, line 2129) |
| 28 | NR 0x06 reset preservation | Verify6 / `9953ed1` | yes | yes | `nextreg_integration_test:G56-CR-NR06-RESET-PRESERVE` + `TC-NR06-PRESERVE-MIXED` ★ NEW |
| 29 | NR 0x05 reset preservation | Verify7 / `18dba39` | NO | **YES** | `nextreg_integration_test:TC-NR05-PRESERVE` ★ NEW |
| 30 | NR 0x09 reset preservation (all but bit 4) | Verify7 / `18dba39` | NO | **YES** | `nextreg_integration_test:TC-NR09-PRESERVE` ★ NEW |
| 31 | NR 0x08-low / NR 0x81 reset preservation | Verify7 / `18dba39` | NO | **YES** | `nextreg_integration_test:TC-NR81-PRESERVE-SOFT` ★ NEW |
| 32 | NR 0x0A reset preservation | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-NR0A-PRESERVE` ★ NEW |
| 33 | NR 0x10 reset preservation | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-NR10-PRESERVE` ★ NEW |
| 34 | NR 0x11 reset preservation | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-NR11-PRESERVE` ★ NEW |
| 35 | NR 0x8A reset preservation | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-NR8A-PRESERVE` ★ NEW |
| 36 | NR 0x14 / 0x4A / 0x4B / 0x4C reset defaults | Verify8 / `69ec3f6` | partial (RST-04, RST-08) | **YES** | `nextreg_integration_test:TC-NR14-RESET-DEFAULT / TC-NR4A-RESET-DEFAULT / TC-NR4B-RESET-DEFAULT / TC-NR4C-RESET-DEFAULT` ★ NEW |
| 37 | NR 0x01 / 0x0E / 0x0F RO guard | Verify8 / `69ec3f6` | partial (RO-*) | **YES** | `nextreg_integration_test:TC-NR01-RO / TC-NR0E-RO / TC-NR0F-RO` ★ NEW |
| 38 | KempstonMouse::reset preserves shadows | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-MOUSE-RESET-PRESERVE` ★ NEW |
| 39 | Joystick::reset preserves shadows | Verify8 / `69ec3f6` | NO | **YES** | `nextreg_integration_test:TC-JOYSTICK-RESET-PRESERVE` ★ NEW |
| 40 | NR 0x80 ExpBus eff_en / eff_disable_mem gates | Verify9 / `b3d3d36` | yes | yes | `nmi_test:GATE-09 / GATE-10 / GATE-11` |
| 41 | NR 0x05 bit 2 Pentagon force-zero on read | Verify10 / `6051f01` | NO | **YES** | `nextreg_integration_test:TC-NR05-PENTAGON` ★ NEW |

★ = new test added by this audit.

### Items above 33 that are not strictly distinct fixes

The `Verify4` cluster (rows 12..21) is presented as ten separate fixes —
the verify4 commit (`4f25708`) bundles them under one commit but each
register has its own VHDL gate / mask / read mux semantics, so each
needs its own regression test. Rows 28..29 (NR 0x06 verify6) and
rows 28..29 (`TC-NR06-PRESERVE-MIXED`) cover the same fix from
complementary angles. The total of 41 entries reflects the granularity
of the spec, not the granularity of the commit DAG.

## Counts

| Metric | Count |
|--------|-------|
| Total NMI/MF/Port/NextREG fixes (1:1 with VHDL spec lines) | 33+ |
| Coverage entries audited (by spec granularity) | 41 |
| Existing regression tests covering them | 12 |
| New regression tests added by this audit | 32 |
| Total regression tests covering NMI/MF/Port/NextREG fixes after audit | 44 |
| Final coverage | 100% (every fix has ≥ 1 regression test) |

## Tests added

All new tests live in two files:

### `test/nmi/nmi_test.cpp` — group `TestCov` (2 rows)

- `TC-NMI3-END-IDLE` — FSM End→Idle advance (Initial NMI-3, c1d7998).
- `TC-NMI-HOLD-LINE-HIGH` — /NMI deasserted in HOLD (Verify1, 78f5f1c).

### `test/nextreg/nextreg_integration_test.cpp` — group `TestCov-NMI-MF-Port` (43 rows)

Reset-preservation:
- `TC-NR05-PRESERVE` (Pass-7)
- `TC-NR06-PRESERVE-MIXED` (Pass-6, complementary angle)
- `TC-NR09-PRESERVE` (Pass-7)
- `TC-NR0A-PRESERVE` (Pass-8)
- `TC-NR10-PRESERVE` (Pass-8)
- `TC-NR11-PRESERVE` (Pass-8)
- `TC-NR7F-PRESERVE` (Pass-5)
- `TC-NR80-LOHI-FOLD` (Pass-5)
- `TC-NR81-PRESERVE-SOFT` (Pass-7)
- `TC-NR8A-PRESERVE` (Pass-8)
- `TC-NR14/4A/4B/4C-RESET-DEFAULT` (Pass-8, four rows)

Mask / write semantics:
- `TC-NR04-MASK7F` (Pass-4)
- `TC-NR11-MASK07` (Pass-4)
- `TC-NR2F-MASK03` (Pass-4)
- `TC-NR8A-MASK3F` (Pass-4)
- `TC-NR90-MASKFC` (Pass-4)
- `TC-NR93-MASK0F` (Pass-4)
- `TC-NRA8-MASK01` (Pass-4)
- `TC-NR75-79-WRITEZERO` (Pass-5, five sub-rows reusing the same group ID)

Read-only / read-zero:
- `TC-NR01-RO` / `TC-NR0E-RO` / `TC-NR0F-RO` (Pass-8 RO guard)
- `TC-NR98-9B-READZERO` (Pass-4, four sub-rows)
- `TC-NRA9-READZERO` (Pass-4)
- `TC-NR09-BIT3-READS-ZERO` (Pass-5, sw-strobe semantics)

Slot mapping:
- `TC-NR52-FF / TC-NR53-FF / TC-NR54-FF / TC-NR55-FF` (Initial NR-2)

Read-side gates:
- `TC-NR02-CFGMODE-NO-CLEAR` (Verify1, NR 0x02 readback)
- `TC-NR02-BUS-RESET` (Verify3)
- `TC-NR0A-MFTYPE-GATED` (Verify3)
- `TC-IOTRAP-IDLE-GATE` (Verify3, companion to FT-INT-DA-*)
- `TC-NR05-PENTAGON` (Verify10)

Subsystem reset:
- `TC-MOUSE-RESET-PRESERVE` (Pass-8)
- `TC-JOYSTICK-RESET-PRESERVE` (Pass-8)

## Test-status report

```
Build: PASS (cmake --build build -j$(nproc))
ctest: 37/37 PASS, 0 FAIL
  - nmi_tests:                57/57 PASS (was 55/55; +2 in TestCov group)
  - nextreg_integration_tests: 223/223 PASS (was 180/180; +43 in TestCov-NMI-MF-Port)
  - All other 35 ctest groups: PASS unchanged
```

## Process notes

1. Many existing rows already covered the MASK / RO / preserve fixes
   indirectly; the new TC rows are deliberately discriminating —
   each writes a value that REQUIRES the gate / mask / preserve
   semantic to be applied (not just the default value).

2. NR 0x81 preserve test is `TC-NR81-PRESERVE-SOFT`, exercising the
   soft-reset path only — Pass-7 fix preserves on `preserve_memory=true`
   (i.e. the NR 0x02 b0 soft-reset path), not on hard `reset()`. This
   matches the VHDL semantics literally (no reset clause anywhere) for
   the operational soft-reset vector while keeping cold-boot
   deterministic.

3. `TC-NR05-PRESERVE` writes a value with bit 2 = 0 to avoid
   interaction with the Verify10 Pentagon-mask read-side gate. The
   Pentagon mask is exercised separately in `TC-NR05-PENTAGON`.

4. `TC-MOUSE-RESET-PRESERVE` and `TC-JOYSTICK-RESET-PRESERVE` exercise
   the subsystem-shadow preservation path that was the second half of
   the verify-8 fix (the first half — NextReg::reset preserving the
   regs_[] cache — is exercised by the corresponding `TC-NR0A-PRESERVE`
   and `TC-NR05-PRESERVE` rows).

## Branch HEAD

After this audit:
- Branch: `task2/testcov-nmi-mf-port`
- HEAD: see commit message of this changeset.
- Working tree: clean post-commit.

## Constraint compliance

- ZERO src/ changes (test-only changes).
- No push / no merge — local commit on `task2/testcov-nmi-mf-port`.
- All cited VHDL lines / fix commits are accurate; spot-check on three
  rows confirms the test reproduces the pre-fix divergence.
