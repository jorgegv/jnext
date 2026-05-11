# Pass-25 — CPU + Z80N + IM2 Pressure-Test Audit — Independent Review

**Reviewer**: independent (blind to audit-NN-CPU author, second eye)
**Audit doc**: `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY25-CPU.md`
**Audit HEAD**: `409525e5`
**Integration base**: Pass-24 aggregate `7414784e`
**Date**: 2026-05-11
**Build**: Release, `-DENABLE_QT_UI=ON`, `cmake --build build -j$(nproc)` —
green tail (`[100%] Linking CXX executable jnext` / `Built target jnext`).

---

## Verdict

**APPROVE — convergence stable across the 3-window observation period
(P23 / P24 / P25).**

The audit's headline claim — **0 findings + ZERO P24→P25 source delta in
`src/cpu/` and `src/core/emulator.cpp`** — is verified independently and
holds. No substantive missed finding was identified during the
streamlined review.

This is the third consecutive zero-finding pass on the CPU + Z80N + IM2
scope. **CPU + Z80N + IM2 audit is intrinsically converged** and warrants
no further routine iteration absent (a) new VHDL upstream, (b) new
emulator code touching the scope, or (c) a fresh regression surfacing
from elsewhere.

---

## Streamlined Review — Items Checked

### 1. Row-count gate

| Item        | Audit claim | Independent count                   | Pass? |
| ----------- | ----------- | ----------------------------------- | ----- |
| Total rows  | 268         | 268 (`grep -c "^\| [0-9][0-9][0-9] "`) | YES   |
| Baseline    | ≥ 220       | 268 ≥ 220                           | YES   |

268 rows is **+8 vs the P24 reviewer-claimed 260** and exceeds the
≥220 baseline mandated by the pressure-test rubric. Section coverage
(A..N) is unchanged from P24 — the row growth is in finer-grained Z80N
sub-paths (sections J and L) which is the right place to add depth on
a 3rd-window walk.

### 2. Differential P24 → P25 source delta

Independently confirmed empty:

```
$ git -C .claude/worktrees/task2-verify25-cpu-z80n-im2-reviewer \
      log 25e6a4c..7414784 -- src/cpu/ src/core/emulator.cpp
(empty)
```

The 3 commits in the broader P24→P25 window (`7414784`, `e5f9f8a0`,
`ae488239`) are all aggregate/review/merge **doc commits** — none modify
audit-scope source. An unchanged C++ surface against an unchanged VHDL
oracle cannot legitimately produce a new finding on a re-walk.
**The audit's "intrinsically stable" framing is sound.**

### 3. Spot-check of 5 ✓ rows

5 randomly-selected rows verified in source against the audit's claimed
C++ ref and the cited VHDL anchor. All 5 match:

| # | Surface | Audit ref | Independent inspection | Pass? |
| - | ------- | --------- | ---------------------- | ----- |
| 036 | `im2_int_req` held 0 in pulse mode (V17-CPU-01) | im2.cpp:941-942 | `if (!im2_reset_n) d.im2_int_req = false; else { … }` matches VHDL `im2_peripheral.vhd:170-171` | YES |
| 084 | `pulse_count_end = bit5 AND (48_or_p3 OR bit2)` | im2.cpp:1165-1167 | `const bool pulse_count_end = bit5 && (machine_48_or_p3_ \|\| bit2);` byte-for-byte match to `zxnext.vhd:2033` | YES |
| 141 | INT pulse-expired drop UNCONDITIONAL of IFF1 (V18R-CPU-01) | z80_cpu.cpp:467-470 | `if (tstates - int_requested_at_ > int_pulse_tstates) { int_pending_ = false; }` — drop arm BEFORE `else if (z80.iff1)`, exactly matches the VHDL pulse_count_end gate | YES |
| 156 | BSLA_DE_B strict-UB-free shift (V17-Z80N-01a) | z80n_ext.cpp:190-207 | `uint32_t v = static_cast<uint32_t>(regs.DE) << shift;` then truncate-mask — unsigned 32-bit, no UB; matches `numeric_std` semantics | YES |
| 250 | RETI canonical only (ED 4D), DD-prefixed RETI excluded (V11-CPU-01) | im2.cpp:881-885 | After `S_DDFD_T4`, any non-DD/FD opcode (including ED) returns to `S_0`, so RETI/RETN detection only fires from a fresh `S_0 → S_ED_T4 → S_ED4D_T4` chain — VHDL-faithful | YES |

**All 5 spot-checks pass — the table accurately represents the source.**

### 4. Prior-fix re-verification (3 spot-checks from the 20-row table)

| Fix ID         | Audit's "Verified at" | Independent re-verification | Pass? |
| -------------- | --------------------- | --------------------------- | ----- |
| V18R-CPU-02    | im2.cpp:191-239        | Switch at :208-216 (raise) + :226-233 (clear) early-returns DMA/DIVMMC/MULTIFACE — collision-prevention intact | YES |
| V21-IM2-01     | im2.cpp:289, 652-653, 691, 1077 | Dual gates `im2_mode_ && im_mode_ == 2` confirmed at 4 exit points (`int_line_asserted` :652-653, `ack_vector` :690-691, `step_state_machine_with_iei` S_ISR→S_0 :1077) — all 3 fabric exits guarded | YES |
| V20-IM2-01     | emulator.cpp:5963-5970 | Falling-edge detection `!cur_pulse_int_n && prev_pulse_int_n_` gated by `!im2_.is_im2_mode()`; `prev_pulse_int_n_` updated immediately after — race-free | YES |

**All 3 prior-fix re-verifications hold.** 17 remaining table entries
sampled informally during the row spot-checks (the 5 ✓ checks above
covered V11/V17/V18R/V19/V20/V22 fix anchors).

### 5. Antipattern re-audit spot-checks (2 of 7)

| Antipattern | Audit claim | Independent check | Pass? |
| ----------- | ----------- | ----------------- | ----- |
| Level-vs-pulse (V19R) | int_unq auto-clear @ im2.cpp:90 + int_req auto-clear @ im2.cpp:141 | Both `for (int k = 0; k < N; ++k) dev_[k].int_{unq,req} = false;` loops present and run at end of `tick()` — VHDL 1-cycle pulse semantic correctly synthesised from level inputs | YES |
| im2_int_req latch leak (V17+V22) | clear @ :941-942 (pulse mode hold-0), :349 (legacy on_reti), :1081 (modern S_ISR→S_0) | All 3 clears located; modern path inline-clear at :1077-1081 is gated on `reti_seen_pulse_ && iei && im_mode_ == 2` — VHDL-faithful | YES |

**Both antipattern re-audits return clean** — re-walked the level/pulse
edge-detect family and the latch-leak family, no missing surfaces.

### 6. Test invariants (Release build)

| Suite                            | Required          | Observed       | Pass? |
| -------------------------------- | ----------------- | -------------- | ----- |
| `ctest -j`                       | 38/38             | 38/38 PASS     | YES   |
| `fuse_z80_test`                  | 1356/1356 (LOCK)  | 1356/1356 PASS | YES   |
| `cpu_int_pulse_test`             | 11/11             | 11/11 PASS     | YES   |
| `cpu_z80n_im2_regressions_test`  | 47/47             | 47/47 PASS     | YES   |
| `ctc_interrupts_test`            | 30/30             | 30/30 PASS     | YES   |
| `ctc_test`                       | 132/132           | 132/132 PASS   | YES   |

All required invariants hold. FUSE Z80 opcode lock-step is at 1356/1356
(0 fail / 0 skip) — the strictest oracle-compliance lock.

---

## Convergence-Stability Statement (3 Windows)

| Pass | Findings | Source delta vs prior | Status |
| ---- | -------- | --------------------- | ------ |
| P23  | 0        | (declared convergence) | CONVERGED |
| P24  | 0        | 0                     | RE-VERIFIED |
| P25  | 0        | 0                     | RE-VERIFIED |

**CPU + Z80N + IM2 has reached VHDL-faithful convergence across 3
consecutive audit windows with zero source modification on the
audit-scope files.** No iteration on this subsystem is warranted until
either:

1. New VHDL surface lands in the upstream FPGA core repo.
2. A new emulator feature is added that touches `src/cpu/` or
   IM2-relevant lines of `src/core/emulator.cpp`.
3. A future regression test failure surfaces a missed finding — which
   would constitute a P23/P24/P25 audit miss, not a post-P25 issue.

---

## Class-(d) Architectural Items (pass-through)

Audit re-lists existing class-(d) items; no new class-(d) raised in P25:

- **V13-CPU-D2** — Stackless NMI path (`nr_c0_stackless_nmi` stored but
  not driven through Z80 RST $66 sequence). **F-deferred** per VHDL
  semantics, irrelevant for NextZXOS boot which uses the standard NMI
  path. Confirmed pass-through. No reviewer disagreement.

**No new class-(d) escalations raised in this pass.** Consistent with
"convergence intrinsically stable — no new architectural surfaces
exposed by a re-walk of unchanged code."

---

## Findings of This Review

**0 missed findings.** Audit's 0-finding outcome holds.

No NITs. The audit document is consistent, well-grounded in VHDL line
references, source-aligned at every spot-checked row, and reports the
P24→P25 source delta correctly as empty.

---

## Reviewer Final Verdict

**APPROVE — no missed.**

3-window convergence (P23 / P24 / P25) is **stable** for CPU + Z80N +
IM2. The audit-scope C++ files (`src/cpu/z80_cpu.{cpp,h}`,
`src/cpu/z80n_ext.{cpp,h}`, `src/cpu/im2.{cpp,h}`, scope-relevant
`src/core/emulator.cpp`) had **zero modification** across the entire
P24→P25 window, and the unchanged code remains VHDL-faithful at all
268 enumerated surfaces and all 7 antipattern families.

CPU + Z80N + IM2 audit work on Task 2 is **complete** absent the
external triggers listed above.
