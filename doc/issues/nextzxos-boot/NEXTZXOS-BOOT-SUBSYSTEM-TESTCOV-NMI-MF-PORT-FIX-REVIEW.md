# Independent Review — NMI/MF/Port/NextREG Test-Coverage FIX (commit 217f701)

**Reviewer worktree branch**: `task2/testcov-nmi-mf-port-fix-reviewer`
**Reviewer worktree path**: `.claude/worktrees/task2-testcov-nmi-mf-port-fix-reviewer`
**Fix commit reviewed**: `217f701` on branch `task2/testcov-nmi-mf-port-fix`
**Fix report reviewed**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-NMI-MF-PORT-FIX.md`
**Date**: 2026-05-10

---

## Verdict

**APPROVE**.

All 3 reviewer-flagged tests are now actually discriminative. Each one
was independently revert-checked by manually re-introducing the
pre-fix bug into `src/peripheral/nmi_source.cpp` or
`src/core/emulator.cpp` (without touching the test code), rebuilding,
and confirming the failure mode reported by the fix agent. After
restoring the original fix, the full suite returned to 37/37 PASS
(223/223 nextreg_integration_test, 43/43 TestCov-NMI-MF-Port).

The fix-agent's revert-check protocol claims hold up when reproduced
end-to-end by an independent reviewer.

---

## Per-test independent revert-check

| # | Test ID | Revert applied (src/) | Reviewer-observed failure detail | Verdict |
|---|---|---|---|:-:|
| 1 | `TC-NR02-CFGMODE-NO-CLEAR` | `nmi_source.cpp:352-358` — re-added `nr_02_pending_mf_=false; nr_02_pending_divmmc_=false;` inside the `if (config_mode_)` clear branch | `FAIL TC-NR02-CFGMODE-NO-CLEAR ... [nr_cfg=1 NR02b3=0x00]` — 42/43 in TestCov-NMI-MF-Port | **DISCRIMINATIVE** |
| 2 | `TC-NR09-BIT3-READS-ZERO` | `emulator.cpp:3619` — re-added `sprites_.set_over_border((v & 0x08) != 0);` to NR 0x09 write handler | `FAIL TC-NR09-BIT3-READS-ZERO ... [ob0=0 ob_nr09=1 NR09b3=0x00 ob_nr15=1]` — 42/43 in TestCov-NMI-MF-Port | **DISCRIMINATIVE** |
| 3 | `TC-IOTRAP-IDLE-GATE` | `emulator.cpp:2740-2743` — removed `if (nmi_accept_cause_())` guard from 0x3FFD WRITE handler | `FAIL TC-IOTRAP-IDLE-GATE ... [idle=1 c_idle=0x03 w_idle=0xcc fetch=1 hold=1 c_hold=0x03 w_hold=0x55]` — 42/43 in TestCov-NMI-MF-Port | **DISCRIMINATIVE** |

**All 3/3 confirmed discriminative. 0/3 still non-discriminative.**

After each revert+verify+restore cycle the worktree was rebuilt and
the suite returned to 37/37 PASS, 223/223 nextreg, confirming no
side-channel/test-shared-state contamination.

### Detail per test

#### Test 1 — `TC-NR02-CFGMODE-NO-CLEAR`

The new test calls `emu.nmi_source().set_config_mode(true)` directly
before `tick(1)`, mirroring the per-tick fan-out at
`emulator.cpp:5163` (`nmi_source_.set_config_mode(nextreg_.nr_03_config_mode())`).
This is the missing wiring in the original test.

Reverting the fix reintroduces the cleared `nr_02_pending_*_` latches
inside the config_mode branch. The detail string `nr_cfg=1
NR02b3=0x00` proves both:
- `nr_cfg=1` → NextReg side correctly recognized config_mode entry
  via `nr_write(0x03, 0x07)`.
- `NR02b3=0x00` → after `tick(1)`, the readback bit was wrongly
  cleared (post-fix would show `0x08`).

VHDL oracle confirmed: `zxnext.vhd:3840-3864` defines two independent
clocked processes for `nr_02_generate_mf_nmi` and
`nr_02_generate_divmmc_nmi`. Neither process clears on config_mode
entry — clear paths are only `reset='1'` and explicit NR 0x02 writes
with the corresponding bit cleared. The fix surface (removal of
`nr_02_pending_*_` clears in the config_mode branch) is correctly
exercised.

#### Test 2 — `TC-NR09-BIT3-READS-ZERO`

The new test asserts three things:
1. `sprites_.over_border()` == false initially (post-reset).
2. After `nr_write(0x09, 0x08)`, `sprites_.over_border()` STILL == false.
3. After `nr_write(0x15, 0x02)`, `sprites_.over_border()` == true.

The read-mask invariant `nr_read(0x09) & 0x08 == 0` is kept as
documentation but is no longer the discriminating axis.

Reverting the fix re-wires NR 0x09 bit 3 to `sprites_.set_over_border`
in the write handler. The detail string `ob0=0 ob_nr09=1 NR09b3=0x00
ob_nr15=1` proves all four sub-axes are exercised:
- `ob0=0` → reset clean.
- `ob_nr09=1` → BUG REVEALED: NR 0x09 bit 3 wrongly drove over_border.
- `NR09b3=0x00` → read mask still zeroes the bit (pre-existing invariant).
- `ob_nr15=1` → NR 0x15 bit 1 still works (positive axis intact).

VHDL oracle confirmed: `zxnext.vhd:4184-4186` shows NR 0x09 bit 3 is
ONLY a sw-write-strobe to clear `port_e3_reg(6)` (DivMMC mapram).
`zxnext.vhd:5233` shows NR 0x15 bit 1 is the authoritative
`nr_15_sprite_over_border_en` driver. `zxnext.vhd:4336` wires it to
`sprites_mod.over_border_i`. The fix surface (removal of
`sprites_.set_over_border((v & 0x08) != 0);` from NR 0x09 write) is
correctly exercised.

#### Test 3 — `TC-IOTRAP-IDLE-GATE`

The new test now drives the FSM via the MF producer path:
1. `set_mf_enable(true)` → enable MF NMI source.
2. `strobe_mf_button()` → assert nmi_assert_mf for one tick.
3. `tick(1)` → IDLE → FETCH transition.
4. `observe_m1_fetch(0x0066, m1=true, mreq=true)` → FETCH → HOLD.
5. State assertions confirm `reached_fetch` and `reached_hold`.
6. `port.out(0x3FFD, 0x55)` issued in HOLD → expect NO capture.

Reverting the fix removes the `if (nmi_accept_cause_())` gate from
the 0x3FFD WRITE handler. The detail string `idle=1 c_idle=0x03
w_idle=0xcc fetch=1 hold=1 c_hold=0x03 w_hold=0x55` proves all six
sub-axes:
- `idle=1` → FSM verified in IDLE for positive-axis check.
- `c_idle=0x03 w_idle=0xcc` → IDLE captures correctly (positive axis).
- `fetch=1 hold=1` → FSM was actually driven through FETCH→HOLD
  (the producer path works; not vacuous).
- `c_hold=0x03 w_hold=0x55` → BUG REVEALED: HOLD-state WRITE wrongly
  captured (post-fix should be `0x00 0x00`).

VHDL oracle confirmed: `zxnext.vhd:2164` defines
`nmi_accept_cause <= '1' when nmi_state = S_NMI_IDLE or nmi_state = S_NMI_FETCH else '0'`.
`zxnext.vhd:3871` and `:3892` both gate `nr_da_iotrap_cause` and
`nr_d9_iotrap_write` updates on `nmi_accept_cause = '1'`. The fix
surface (the WRITE-side `if (nmi_accept_cause_())` gate at
`emulator.cpp:2740`) is correctly exercised.

---

## Code quality nits

### NIT-1 (informational — no change requested)

The fix report (lines 137-146) explicitly acknowledges the test only
exercises the WRITE-side gate (`emulator.cpp:2740`) and not the two
READ-side gates (`:2710` and `:2725`). The agent's rationale —
shared `nmi_accept_cause_()` helper — is reasonable: a regression
that broke the helper would surface in all three. However, a
regression that removes the gate from only the READ paths (independent
of the WRITE path) would NOT be caught by `TC-IOTRAP-IDLE-GATE`. This
is a minor coverage-density gap; documenting as a NIT.

Two additional rows (e.g. `TC-IOTRAP-IDLE-GATE-RD2FFD`,
`TC-IOTRAP-IDLE-GATE-RD3FFD`) could close the gap fully. Not blocking
for this approval — the fix agent already documented the trade-off in
the fix report.

### NIT-2 (informational — no change requested)

In Test 1, the comment block (test cpp lines 3593-3605) and the fix
report (lines 39-44) both explain WHY the original test was
non-discriminative. The corrective approach — directly calling
`emu.nmi_source().set_config_mode(true)` rather than just relying on
NR 0x03 write fan-out — is the right call for a unit-style test that
only ticks the NmiSource directly. Future test authors should be
aware that `nmi_source().tick(N)` does NOT pull config_mode from
NextReg — that wiring lives in `tick_peripheral_subsystems` only.

The test is correct as written. NIT only suggests considering a
helper such as `tick_with_full_fanout(N)` if this pattern recurs in
other tests; not needed here.

### NIT-3 (informational — no change requested)

In Test 2, the test reads `emu.sprites().over_border()` (the
write-side latch) which is the correct discriminating signal. The
NR 0x15 positive axis is added in the same row, which is a slight
concern about "row-bundling" multiple invariants in a single check —
if the test fails, the detail string still distinguishes (4 sub-axes
in the message). This is acceptable.

---

## Test suite status

```
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   1.30 sec

$ ./build/test/nextreg_integration_test (last lines)
  TestCov-NMI-MF-Port    43/43
```

`nextreg_integration_test` reports 223/223 (43/43 in
TestCov-NMI-MF-Port). All 37 ctest groups pass. No regressions in
any subsystem after restoring the fix.

---

## VHDL oracle confirmation (independent)

| Row | VHDL anchor | Independent verdict |
|---|---|:-:|
| TC-NR02-CFGMODE-NO-CLEAR | `zxnext.vhd:3840-3864` | Confirmed: two independent clocked processes; clear cascade is `reset='1'` OR `nr_02_we='1' AND nr_wr_dat(N)='0'`. NO config_mode in clear path. |
| TC-NR09-BIT3-READS-ZERO | `zxnext.vhd:4184-4186`, `:4336`, `:5233`, `:5939` | Confirmed: NR 0x09 bit 3 = sw-write-strobe to `port_e3_reg(6)`. `nr_15_sprite_over_border_en` is bit 1 of NR 0x15 (line 5233) wired to `sprites_mod.over_border_i` (line 4336). |
| TC-IOTRAP-IDLE-GATE | `zxnext.vhd:2164`, `:3871`, `:3892` | Confirmed: `nmi_accept_cause <= '1'` only in `S_NMI_IDLE` or `S_NMI_FETCH`. Both `nr_da_iotrap_cause` and `nr_d9_iotrap_write` clocked processes gate on `nmi_accept_cause='1'`. |

All citations spot-checked from the live VHDL source. No spurious
references.

---

## Branch HEAD

- Reviewer worktree branch: `task2/testcov-nmi-mf-port-fix-reviewer`
- Reviewer worktree path: `.claude/worktrees/task2-testcov-nmi-mf-port-fix-reviewer`
- Reviewed commit: `217f701`
- No push, no merge.
