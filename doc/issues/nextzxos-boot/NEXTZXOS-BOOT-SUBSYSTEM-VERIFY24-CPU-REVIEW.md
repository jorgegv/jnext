# NEXTZXOS Boot Subsystem — Pass-24 CPU + Z80N + IM2 — Independent Review

**Pass:** 24 (CONVERGENCE PRESSURE TEST — second consecutive zero-finding pass)
**Subsystems:** CPU + Z80N + IM2 (combined)
**Audit doc reviewed:** `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-CPU.md`
**Audit HEAD reviewed:** `25e6a4ca`
**P23 baseline HEAD:** `b059f4b9`
**Reviewer worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify24-cpu-z80n-im2-reviewer`
**Branch:** `task2/verify24-cpu-z80n-im2-reviewer`

---

## Methodology

Streamlined review per Pass-24 directive (P23 already established detailed
methodology):

1. **Row-count check** — confirm ≥ 210 P23 baseline.
2. **5-row VHDL spot-check** — pick 5 random rows; verify VHDL citations
   are faithful and not "hand-wave OK".
3. **Differential audit vs P23** — `git log b059f4b9..25e6a4c` to see what
   changed between passes; flag anything that could invalidate convergence.
4. **Cross-cutting families final sweep** — cache-leak, multi-writer
   fan-out, level-vs-pulse antipatterns, WO-NR readback.
5. **Test invariants** — FUSE 1356/1356, ctest 38/38, regression 33/0/0,
   plus targeted `cpu_int_pulse`, `cpu_z80n_im2_regressions`, `ctc_tests`,
   `ctc_interrupts_tests`.

---

## 1. Row count

- **Actual table row count: 260** (verified by `grep -cE "^\| [0-9]+ \|"`
  on the audit doc).
- **P23 baseline: 210** (verified the same way on
  `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY23-CPU.md`).
- **Audit-doc header / commit message claim: 220 rows.**

**The audit's stated row-count is a documentation discrepancy** — the
table actually contains 260 numbered rows (1..260), not 220 as the
title and commit subject claim. The mechanisms enumerated are real and
each row is independently audited; this is a cosmetic mistake in the
header text, not a substantive issue, and does NOT affect the
convergence verdict. The 260-row count comfortably exceeds the 210
P23 baseline (+50 rows of additional mechanism coverage, mostly in
the Z80N opcode block and `step_devices` Phase-1/2/3 breakdown).

**Verdict: PASS** (260 ≥ 210). Documentation NIT noted, see below.

---

## 2. 5-row VHDL spot-check

Five rows spanning all sections (CPU wrapper, Im2Controller, Z80N) were
picked and verified against the cited VHDL.

### Row 40 — Pulse drop unconditional on IFF1

- **jnext site:** `src/cpu/z80_cpu.cpp:467-470`
- **VHDL oracle:** `zxnext.vhd:2017-2033` pulse_int_n process
- **Verification:** Read both. VHDL `pulse_int_n` returns to `'1'` when
  `pulse_count_end='1'` (line 2027), independent of IFF1 / any CPU
  signal. jnext's drop arm at `tstates - int_requested_at_ >
  int_pulse_tstates` clears `int_pending_=false` unconditionally before
  the IFF1 check. Matches V18R-CPU-01 fix rationale.
- **Verdict: VHDL-FAITHFUL ✓**

### Row 110 — `on_reti()` IEI snapshot with simultaneity

- **jnext site:** `src/cpu/im2.cpp:312`
- **VHDL oracle:** `im2_control.vhd:233-234` + `im2_device.vhd:142`
- **Verification:** VHDL :233 `o_reti_decode <= '1' when state = S_ED_T4
  else '0'`; :234 `o_reti_seen <= '1' when state_next = S_ED4D_T4`.
  Device IEO at :142: `o_ieo <= i_iei AND i_reti_decode` for S_REQ.
  jnext combines `reti_decode_ || reti_seen_pulse_` to cover the
  simultaneous-edge case where the state machine has just transitioned;
  Pass-10 simultaneity fix is reflected in code and comment.
- **Verdict: VHDL-FAITHFUL ✓**

### Row 138 — `ack_vector()` priority walk + S_REQ→S_ACK

- **jnext site:** `src/cpu/im2.cpp:692-698`
- **VHDL oracle:** `im2_device.vhd:111-116` (S_REQ→S_ACK transition)
- **Verification:** VHDL :112 `if i_m1_n = '0' and i_iorq_n = '0' and
  i_iei = '1' and i_im2_mode = '1'` → `state_next <= S_ACK`. jnext's
  `ack_vector()` is called only from the on_int_ack callback (i.e.
  during IntAck cycle, so m1_n=0 AND iorq_n=0 are implicit), walks the
  chain from i=0, checks IEI via `device_ieo(i-1)`, transitions the
  first qualified S_REQ device to S_ACK. The `im_mode_ != 2` early
  return at line 691 covers VHDL's `i_im2_mode='1'` gate.
- **Verdict: VHDL-FAITHFUL ✓**

### Row 165 — im2_int_req held at 0 in pulse mode

- **jnext site:** `src/cpu/im2.cpp:941-943`
- **VHDL oracle:** `im2_peripheral.vhd:105 + 170-171`
- **Verification:** VHDL :105 `im2_reset_n <= i_mode_pulse_0_im2_1 and
  not i_reset`. VHDL :170-171 `if im2_reset_n = '0' then im2_int_req
  <= '0'`. In pulse mode `i_mode_pulse_0_im2_1='0'`, so `im2_reset_n
  ='0'`, holding `im2_int_req=0`. jnext's `if (!im2_reset_n) {
  d.im2_int_req = false; }` exactly matches.
- **Verdict: VHDL-FAITHFUL ✓**

### Row 230 — NEXTREG_NN routes via internal Z80N_data_o strobes

- **jnext site:** `src/cpu/z80n_ext.cpp:477-497`
- **VHDL oracle:** `t80n_mcode.vhd:1672-1707`
- **Verification:** VHDL :1672-1688 ED 91 (NEXTREGW) uses
  `Z80N_data_o`, `Z80N_data_o_strobe_hi/lo`, `Z80N_command_o`,
  `Z80N_dout_o` — all internal CPU→fabric strobes, NOT external IO bus
  cycles. jnext's `cpu.io().out(0x243B/0x253B)` routes through the
  internal IoMux dispatch, bypassing `fuse_z80_writeport`'s 4 T per
  port write (which would over-count by 8 T per NEXTREG_NN). Code
  comment correctly identifies this as a documented internal shortcut,
  not a divergence.
- **Verdict: VHDL-FAITHFUL ✓**

**Spot-check result: 5/5 VHDL-faithful.**

---

## 3. Differential audit P23 → P24

Commits between P23 reviewer-APPROVE (`b059f4b9`) and P24 audit HEAD
(`25e6a4c`):

```
25e6a4ca doc(task2-pass24-cpu): pressure-test audit report — ...
d8647df0 doc(task2-pass23): aggregate report — ...
1fd8b7bf merge(task2-verify23-cpu-z80n-im2): ... (P23 merge to integration)
d0ab0913 review(task2-verify23-cpu-z80n-im2): independent review — APPROVE no missed
```

**Files changed between P23 and P24** (audited via `git log
b059f4b9..25e6a4c --stat`):

- P23 reviewer doc (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY23-CPU-REVIEW.md`,
  +136 lines, new file)
- P23 aggregate report (`NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS.md`, 1 line
  modified)
- P24 audit doc (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-CPU.md`, +497 lines,
  new file)

**ZERO source-code changes** in `src/cpu/`, `src/core/emulator.cpp`, or
`src/core/nextreg.cpp`. The audited subsystem is byte-for-byte
identical between P23 and P24.

**Implication:** Convergence stability is intrinsic. P24 re-audits the
exact same C++ source against the exact same VHDL oracle and produces
the same zero-finding result as P23. No regression risk introduced
between passes.

**Verdict: PASS** (no source delta; stability is by construction).

---

## 4. Cross-cutting families final sweep

### Multi-writer fan-out on `im2_int_req`

`grep -nE "im2_int_req.*= (true|false)"` in `src/cpu/im2.cpp`:

| Site | Line | Direction | VHDL anchor |
|------|------|-----------|-------------|
| `on_reti()` S_ISR clear | 349 | false | im2_peripheral.vhd:175 |
| `raise_unq()` | 407 | true | im2_peripheral.vhd:172 (unq path) |
| `step_devices` pulse-mode reset | 942 | false | im2_peripheral.vhd:170-171 |
| `step_devices` edge & int_en | 945 | true | im2_peripheral.vhd:172 |
| `step_devices` int_unq bypass | 948 | true | im2_peripheral.vhd:172 |
| `step_state_machine_with_iei` S_ISR→S_0 | 1081 | false | im2_peripheral.vhd:175 |

All 6 sites map to VHDL processes :167-178 (`im2_int_req <= ...`
combinational composition). No phantom writer; clear/set paths
mutually consistent with VHDL `if/elsif/else` priority. ✓

### Level-vs-pulse antipattern on `pulse_int_n_`

3 sites total — constructor init true, `step_pulse` drop on
`pulse_int_en` edge (line 1148), raise on `pulse_count_end`
(line 1170). Matches VHDL :2017-2031 single-process state machine
exactly. No multi-writer collision. ✓

### WO-NR readback semantics (int_unq, int_status, im2_int_req)

- `int_unq` is a one-shot latch cleared at end of every tick (im2.cpp
  line 90, `for (int k = 0; k < N; ++k) dev_[k].int_unq = false;`).
  Matches VHDL `nr_20_we and nr_wr_dat(N)` 1-cycle pulse semantic
  (zxnext.vhd:1946-1947). ✓
- `int_status` set on edge or unq (lines 935, 952), cleared only on
  explicit `clear_status()` (line 417) or NR write. Matches VHDL :160
  `int_status <= (int_req or i_int_unq) or (int_status and not
  i_int_status_clear)`. ✓
- `im2_int_req` composite readback at `int_status(DevIdx)`
  (im2.cpp:424-427) returns `int_status OR im2_int_req`, matching
  peripheral.vhd `o_int_status <= int_status or im2_int_req`. ✓

### Cache-leak sweep (Pass-19..23 refactor checkpoints)

- `iei_snap[]` recomputed every tick in `step_devices()` — no stale
  snapshot leakage between ticks. ✓
- `reti_decode_`, `reti_seen_pulse_`, `retn_seen_pulse_` cleared at
  start of every `on_m1_cycle()` (lines 721-746). ✓
- `prev_pulse_int_n_` is a snapshot used for edge detection;
  save/load slot appended (emulator.cpp:7188 + 7435). ✓
- `last_acked_` is debug-only; reset()-cleared. ✓

All cross-cutting families clean. No new findings.

**Verdict: PASS** (no antipatterns surface).

---

## 5. Test invariants

Built Release on this worktree; symlinked `roms/` from main repo to
satisfy the embedded `nextboot.rom` link step.

| Test | Required | Observed | Pass |
|------|----------|----------|------|
| FUSE Z80 1356/1356 | required | 1356/1356 | ✓ |
| ctest 38/38 | required | 38/38 | ✓ |
| regression 33/0/0 | required | 33/0/0 | ✓ |
| cpu_int_pulse_tests | required | PASS | ✓ |
| cpu_z80n_im2_regressions_tests | required | PASS | ✓ |
| ctc_tests | required | PASS | ✓ |
| ctc_interrupts_tests | required | PASS | ✓ |

All invariants met.

---

## Documentation NIT

V24-CPU-NIT-DOC-01 (class-(d), trivial): the audit doc title, header
"Enumeration Table (220 rows)" line (~line 59), and commit subject all
claim "220 rows" but the actual numbered table contains rows 1 through
260. This is a documentation typo, not a substantive issue — every row
is independently audited and the 260-row count exceeds the P23 baseline
of 210 rows by +50. **Not actionable as a bug-class finding;** mentioned
only for the aggregate-report cosmetic-cleanup queue, OR an in-place
edit to the audit doc to correct "220" → "260". The convergence verdict
is unaffected.

---

## Final verdict

**APPROVE — no missed findings.**

Pass-24 is a successful convergence pressure test. The CPU + Z80N + IM2
subsystem has now produced zero findings of any class across two
consecutive independent passes (P23 + P24). Between those passes, zero
source code changed (only doc changes), so the convergence is intrinsic
to the source state, not an artifact of partial coverage.

The 260-row enumeration (which exceeds the 210 P23 baseline) covers:

- 78 CPU-wrapper mechanisms (z80_cpu.{h,cpp})
- 121 IM2 fabric mechanisms (im2.{h,cpp}, im2_client.h)
- 37 Z80N opcode/extension mechanisms (z80n_ext.{h,cpp})
- 24 emulator wiring mechanisms (emulator.cpp)

All 5 spot-checks were VHDL-faithful with verifiable VHDL line
citations. All cross-cutting families (multi-writer, level-vs-pulse,
WO-NR, cache-leak) clean. All test invariants pass.

**CPU + Z80N + IM2 convergence is empirically demonstrated stable
across 2 consecutive passes.**

Combined with the P21 DivMMC convergence, P22 NMP convergence, and
P14 Memory convergence (all previously approved), Task 2 boot-critical
subsystem audit remains COMPLETE.

---

## Reviewer test output (raw)

### FUSE Z80 opcode tests

```
FUSE Z80 Test Results
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

### ctest (all 38)

```
100% tests passed, 0 tests failed out of 38
Total Test time (real) =   0.43 sec
```

### ctest (targeted CPU/IM2/CTC)

```
1/4 Test  #2: cpu_int_pulse_tests ..............   Passed    0.00 sec
2/4 Test  #3: cpu_z80n_im2_regressions_tests ...   Passed    0.00 sec
3/4 Test #14: ctc_tests ........................   Passed    0.00 sec
4/4 Test #15: ctc_interrupts_tests .............   Passed    0.03 sec

100% tests passed, 0 tests failed out of 4
```

### regression suite

```
=== Results ===
  Pass: 33  Fail: 0  Skip: 0
```

---

## Provenance

- Audit HEAD reviewed: `25e6a4ca`
- P23 baseline HEAD: `b059f4b9`
- Differential: doc-only (no `src/` changes)
- Reviewer build: Release, Qt6 UI enabled
- Reviewer worktree branch: `task2/verify24-cpu-z80n-im2-reviewer`
- Reviewer VHDL oracle reads:
  `t80n_mcode.vhd:1672-1707`,
  `zxnext.vhd:2015-2041`,
  `im2_control.vhd:228-241`,
  `im2_device.vhd:100-146`,
  `im2_peripheral.vhd:100-178`
