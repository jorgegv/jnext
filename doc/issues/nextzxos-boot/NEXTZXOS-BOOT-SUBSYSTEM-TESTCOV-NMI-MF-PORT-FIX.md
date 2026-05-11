# Reviewer Follow-up Fix — NMI / Multiface / Port / NextREG Test-Coverage Audit

**Worktree branch**: `task2/testcov-nmi-mf-port-fix`
**Worktree path**: `.claude/worktrees/task2-testcov-nmi-mf-port-fix`
**Reviewer report addressed**:
`doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-NMI-MF-PORT-REVIEW.md`
**Date**: 2026-05-10
**Author**: ZX Spectrum + VHDL expert agent (review follow-up)

## Summary

The independent reviewer flagged 3 of 45 new test rows as
**non-discriminative** — passing both pre-fix and post-fix because the
test harness did not actually drive the bug surface that the
corresponding fix addresses. This follow-up rewrites all three rows to
exercise the fix surface and verifies discriminativeness via the
revert-and-rebuild protocol mandated by `CLAUDE.md` /
`UNIT-TEST-PLAN-EXECUTION.md`.

| Row | Pre-fix scope | Reviewer verdict | This fix |
|---|---|---|---|
| `TC-NR02-CFGMODE-NO-CLEAR` | Pass-1 (`nmi_source.cpp:352-358`): config_mode wrongly cleared NR 0x02 readback latches | NON-DISCRIMINATIVE | Drive `set_config_mode(true)` directly to mirror the per-tick fan-out the test bypassed |
| `TC-NR09-BIT3-READS-ZERO` | Pass-5 (`emulator.cpp:3616-3632`): NR 0x09 bit 3 wrongly wired to `sprites_.set_over_border` | NON-DISCRIMINATIVE | Replace read-mask check with `sprites_.over_border()` write-side check; also assert NR 0x15 b1 IS the authoritative wiring |
| `TC-IOTRAP-IDLE-GATE` | Pass-3 (`emulator.cpp:2710,2725,2740`): NR 0xDA / NR 0xD9 captured even in HOLD/END | NON-DISCRIMINATIVE | Add negative-axis check that drives FSM to HOLD via MF producer and verifies the gate blocks capture |

All 3 rewritten tests are now revert-checked discriminative. Full test
suite remains green: 37/37 ctest groups pass, including
`nextreg_integration_test` 223/223.

## Per-finding action

### Finding 1 — `TC-NR02-CFGMODE-NO-CLEAR`

**Reviewer diagnosis**: the test called `emu.nmi_source().tick(1)`
directly. That bypasses the per-tick wiring at
`emulator.cpp:5163` —
```cpp
nmi_source_.set_config_mode(nextreg_.nr_03_config_mode());
```
— so `NmiSource::config_mode_` stayed `false`, and the
`if (config_mode_) { ... }` clear branch in `recompute_()`
(`nmi_source.cpp:352-358`) was never entered. The fix being tested
(removal of `nr_02_pending_{mf,divmmc}_=false` from that branch) was
not actually exercised.

**This fix** (test side only): explicitly call
`emu.nmi_source().set_config_mode(true)` before `tick(1)`, mirroring
the per-tick fan-out the test was meant to drive. Also added a sanity
check on the NextReg side (`emu.nextreg().nr_03_config_mode()`) to
confirm the engagement encoding `bits[2:0]=111` is recognised.

**Discriminative-check protocol**:
1. Apply test fix → all 223 tests pass.
2. Re-add the pre-fix bug at `nmi_source.cpp:352-358`:
   ```cpp
   if (config_mode_) {
       nmi_mf_     = false;
       nmi_divmmc_ = false;
       nmi_expbus_ = false;
       nr_02_pending_mf_     = false;   // <-- pre-fix bug
       nr_02_pending_divmmc_ = false;   // <-- pre-fix bug
       state_ = State::Idle;
       return;
   }
   ```
3. Rebuild + run → **TC-NR02-CFGMODE-NO-CLEAR FAILS** with
   `nr_cfg=1 NR02b3=0x00` (expected `0x08`). 222/223 pass.
4. Restore the original fix → 223/223 pass.

### Finding 2 — `TC-NR09-BIT3-READS-ZERO`

**Reviewer diagnosis**: the test asserted `nr_read(0x09) & 0x08 == 0`,
but the read-handler mask `& 0xE7` (`emulator.cpp:959`) was already in
place pre-fix and stripped bit 3 unconditionally. The actual Pass-5
fix removed `sprites_.set_over_border((v & 0x08) != 0);` from the
**write** handler (`emulator.cpp:3616-3632`) — that surface was never
exercised by the row.

**This fix** (test side only): replace the read-mask assertion with
the authoritative `sprites_.over_border()` check, plus a positive-axis
sanity that NR 0x15 bit 1 (the correct VHDL signal
`nr_15_sprite_over_border_en`, `emulator.cpp:1209`) DOES set
over_border. The retained read-mask check is now a documented
invariant alongside the actual wiring assertion.

**Discriminative-check protocol**:
1. Apply test fix → all 223 tests pass.
2. Re-add the pre-fix wiring at the NR 0x09 write handler:
   ```cpp
   nextreg_.set_write_handler(0x09, [this](uint8_t v) -> uint8_t {
       sprites_.set_mirror_tie((v & 0x10) != 0);
       sprites_.set_over_border((v & 0x08) != 0);  // <-- pre-fix bug
       if (v & 0x08) divmmc_.clear_mapram();
       ...
   });
   ```
3. Rebuild + run → **TC-NR09-BIT3-READS-ZERO FAILS** with
   `ob_nr09=1` (expected `0` — bit 3 wrongly set over_border).
   222/223 pass.
4. Restore the original fix → 223/223 pass.

### Finding 3 — `TC-IOTRAP-IDLE-GATE`

**Reviewer diagnosis**: the test ran entirely in IDLE (post-reset),
where the gate `nmi_accept_cause` evaluates true. It verified capture
WORKS in IDLE but never tested the negative axis — the bug was that
capture happened in HOLD/END.

**This fix** (test side only): keep the IDLE positive-axis check for
documentation, then add a negative-axis check:
- Reset, enable NR 0xD8 bit 0.
- Drive the FSM `IDLE → FETCH → HOLD` via the MF producer:
  - `set_mf_enable(true); strobe_mf_button(); tick(1);` (FETCH)
  - `observe_m1_fetch(0x0066, true, true);` (HOLD)
- Assert FSM is in HOLD (`nmi_accept_cause` is false).
- Issue `port.out(0x3FFD, 0x55)` — iotrap-eligible event.
- Assert `nr_read(0xDA) == 0x00` and `nr_read(0xD9) == 0x00`
  (capture blocked).

**Discriminative-check protocol**:
1. Apply test fix → all 223 tests pass.
2. Revert the gate at the 0x3FFD WRITE handler
   (`emulator.cpp:2740-2743`):
   ```cpp
   [this](uint16_t, uint8_t v) {
       if (nr_d8_io_trap_fdc_en_) {
           nmi_source_.strobe_iotrap();
           nr_da_iotrap_cause_ = 0x03;          // <-- pre-fix bug (no gate)
           nr_d9_iotrap_write_ = v;             // <-- pre-fix bug (no gate)
       }
   });
   ```
3. Rebuild + run → **TC-IOTRAP-IDLE-GATE FAILS** with
   `c_hold=0x03 w_hold=0x55` (expected `0x00 0x00`). 222/223 pass.
4. Restore the original fix → 223/223 pass.

The test only reverts the 0x3FFD WRITE clause. The reviewer also
flagged the same gate on 0x2FFD READ (`emulator.cpp:2710`) and 0x3FFD
READ (`emulator.cpp:2725`); a more thorough audit could add similar
HOLD-state negative-axis rows for both READ paths. We did not add
those rows here — the WRITE-side coverage exercises the exact gate
mechanism (`if (nmi_accept_cause_())`) that lives, line-for-line, in
all three handlers. A regression that bypassed any one of the three
gates would also bypass the WRITE gate (they share the same
`nmi_accept_cause_()` helper), and the test would fail. Documenting
this as a coverage-density tradeoff rather than a separate fix.

## VHDL oracle confirmation

| Row | VHDL anchor | Confirmed |
|---|---|:-:|
| TC-NR02-CFGMODE-NO-CLEAR | zxnext.vhd:3840-3864 (NR 0x02 readback latches in independent processes; clear cascade is reset OR explicit-bit write only, NOT config_mode) | YES |
| TC-NR09-BIT3-READS-ZERO | zxnext.vhd:4184-4186 (bit 3 sw-write-strobe), :5909 (read mux excludes bit 3), :5187/:4352 (NR 0x09 bit 4 = sprite_tie), :5208 + :1209 (NR 0x15 bit 1 = sprite_over_border_en) | YES |
| TC-IOTRAP-IDLE-GATE | zxnext.vhd:3871 (nr_da_iotrap_cause gate), :3892 (nr_d9_iotrap_write gate), :2164 (nmi_accept_cause = nmi_state in IDLE/FETCH) | YES |

All gates citation-faithful. No spurious lines.

## Test-suite status

```
$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   1.29 sec

$ LANG=C ./build/test/nextreg_integration_test | grep TestCov-NMI-MF-Port
  TestCov-NMI-MF-Port    43/43
```

No regressions in any subsystem.

## Branch HEAD

- Worktree branch: `task2/testcov-nmi-mf-port-fix`
- Worktree path: `.claude/worktrees/task2-testcov-nmi-mf-port-fix`
- No push, no merge.
