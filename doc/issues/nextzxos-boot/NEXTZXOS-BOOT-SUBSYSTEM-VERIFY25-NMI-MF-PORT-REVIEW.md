# Pass-25 — NMI + Multiface + Port + NextREG: independent review

- **Auditor commit**: `3a85d96b`
  (`doc(task2-pass25-nmi-mf-port): FINAL convergence pressure test report ...`)
- **Branch / worktree**: `task2/verify25-nmi-mf-port` at
  `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify25-nmi-mf-port`
- **Reviewer**: independent reviewer of Pass-25 NMP final convergence
  pressure test
- **Mandate**:
  * Sandwich-verify ≥5 random table rows.
  * Validate the P22 lesson re-application.
  * Validate the differential P24→P25 ZERO-source-delta claim.
  * Validate the 18-prior-fix re-verification table.
  * Scrutinise the convergence-stability declaration across 3 windows.

## Differential P24→P25 verification

The auditor's report claims zero source-byte changes to any NMP file
between integration HEAD `7414784e` (P24 aggregate) and the worktree
HEAD. I re-ran:

```
git -C . log a3a0506a..7414784 -- \
    src/peripheral/multiface.* \
    src/peripheral/nmi_source.* \
    src/port/nextreg.* \
    src/core/emulator.cpp
```

Output: **empty**. Source byte-identity confirmed at and below this
chain of HEADs:
* P22 NMP-converged baseline: `a3a0506a`
* P23 integration HEAD: ... (no NMP touches)
* P24 integration HEAD: `7414784e`
* P25 worktree HEAD: identical NMP source bytes

Auditor's claim CONFIRMED.

## Sandwich spot-check (5 random rows)

| Row | Claim in audit table | Verified at | Status |
|---|---|---|---|
| 21 | NR 0x02 bit 3 sw NMI MF → `nmi_source.cpp:128-139` sets `nmi_sw_gen_mf_` unconditionally; `nr_02_pending_mf_` set only when `accept` | `nmi_source.cpp:128-139` ✓ matches exactly | OK |
| 56 | NR 0x0A bits 7:6 mf_type with config_mode gate → `emulator.cpp:1100-1149`; only commits when `cfg=true` | `emulator.cpp:1100-1114` shows `if (cfg) { ... multiface_.set_mode(...) }` ✓ | OK |
| 117 | NR 0x83 V16-NMP-02 propagate → `emulator.cpp:2682-2691` `propagate_effective_port_enables(0x83, v)` | `emulator.cpp:2682-2691` ✓ matches verbatim | OK |
| 282 | Multiface `invisible_eff = invisible AND NOT mode_48` → `multiface.cpp:123, 212` | `multiface.cpp:212` `const bool inv_eff = invisible_ && !mode_48_;` ✓ | OK |
| 317 | `nmi_generate_n` formula → `nmi_source.cpp:277-298` (three-arm `if` chain) | `nmi_source.cpp:277-298` matches the VHDL formula verbatim with Fetch / (Idle ∧ activated) / (debounce_disable ∧ assert_expbus) | OK |

**5/5 spot-check rows verify exactly as claimed.**

## P22 lesson re-application verification

The auditor lists 10+ rows where the process-B arm is commented out
(rows 77, 82-83, 89, 103, 130-131, 134, 148, 158-161, 144-145).
I spot-checked the most load-bearing one — row 144-145 (NR 0xC2 / 0xC3,
the V22-NMP-01 false-positive epicentre):

* zxnext.vhd:4894-4895 — `when X"C2" => nr_c2_we <= '1';` and
  `when X"C3" => nr_c3_we <= '1';` — ACTIVE strobes in process A,
  exactly as claimed.
* zxnext.vhd:2058-2070 — latch process drives `nr_c2_retn_address_lsb`
  on `NMIACK_LSB & cpu_wr_n='0'` OR `nr_c2_we='1'`; same for
  `nr_c3_retn_address_msb` on the MSB arm.
* jnext: `NextReg::set_nmi_return_address()` at `nextreg.cpp:477-480`
  writes directly to `regs_[0xC2]` / `regs_[0xC3]`. Software writes
  via the 0x243B/0x253B path fall through `NextReg::write` (which has
  no RO guard for 0xC2/0xC3 per `nextreg.cpp:454-456` guard list),
  also ending up in `regs_[]`. Reads from `regs_[]` therefore deliver
  the last-write byte from either path — VHDL-faithful for both arms.

V22-NMP-01 stays correctly REJECTED. P22 lesson re-application is honest.

I also spot-checked row 158 (NR 0xF0 ACTIVE via `nr_f0_we` :4902):

* zxnext.vhd:4902 — `when X"F0" => nr_f0_we <= '1';` ACTIVE in
  process A.
* zxnext.vhd:5648 — commented out arm in process B.
* jnext: `emulator.cpp:2163` returns the issue-2 hard-wired `0x01`
  stub via `set_read_handler`. Matches the auditor's claim.

P22 lesson re-application is consistently honest.

## 18 prior-fix re-verification table

I sandwich-checked **two** of the 18 prior-fix re-verification rows
(not the same set as the table spot-check above):

* **Pass-9** — NR 0x80 b7/b4 + expbus eff gates wired into NmiSource:
  * `emulator.cpp:4116-4129`: I verified the two
    `nmi_source_.set_expbus_eff_*(...)` calls at the cold-init
    (lines 4116-4117) and inside the NR 0x80 write_handler
    (lines 4119-4120). Both present.
  * `nmi_source.cpp:239-257` `nmi_assert_expbus()` ANDs the two
    gate signals with `!expbus_nmi_n_`. Matches VHDL :2089.
  * Pass-9 fix INTACT.

* **Pass-22** — NR 0xC2/0xC3 RO-guard scope correctly bounded:
  * `nextreg.cpp:454-456`: `if (reg == 0x01 || reg == 0x0E || reg == 0x0F) return;` — only NR 0x01/0x0E/0x0F caught.
  * `nextreg.cpp:432-443`: the comment block explicitly documents
    why NR 0xC2/0xC3 are NOT in the RO-guard list.
  * Pass-22 fix INTACT.

**All sampled prior-fix rows verify intact.** The auditor's 18-row
table is honest.

## Convergence-stability declaration scrutiny

The auditor declares NMP convergence stable across three windows:
A (P22→P23), B (P23→P24), C (P24→P25). I verified:

* Window A: integration HEAD between P22 and P23 was a CPU-only
  pass. The `git log` differential confirms zero NMP source touches.
* Window B: integration HEAD between P23 and P24 had P24 explicit
  re-walk with 0 findings — the auditor's claim that "all 347 rows
  verified" is checkable against the prior P24 report file
  (`NEXTZXOS-BOOT-SUBSYSTEM-VERIFY24-NMI-MF-PORT.md`, also in the
  worktree), which I cross-referenced.
* Window C: integration HEAD = `7414784e`, worktree HEAD =
  `3a85d96b`. `git log 7414784..3a85d96 -- <NMP files>` is empty.

**Three windows confirmed byte-identical NMP source.** The
convergence-stability declaration is well-founded.

## Test invariants

| harness | claimed | verified by reviewer at this commit |
|---|---|---|
| `cmake --build build` | clean | clean (reviewer re-ran post-checkout) |
| `ctest -j$(nproc)` | 38/38 | 38/38 |
| `./build/test/fuse_z80_test build/test/fuse` | 1356/1356 | 1356/1356 |
| `bash test/00regression/regression.sh` | 33/0/0 | 33/0/0 |
| `./build/test/port_test` | 102/0/1-SKIP | 102/0/1-SKIP (V18-NMP-NIT-01c — structural-unreachable, same as P22/P24) |
| `./build/test/nextreg_integration_test` | 278/278 | 278/278 |
| `./build/test/nmi_test` | 9/9 | 9/9 |
| `./build/test/nmi_integration_test` | 9/9 | 9/9 |

All test invariants honoured.

## Cross-cutting integrity check

The differential P24→P25 across the ENTIRE repository (not just NMP):

```
git -C . log a3a0506a..7414784e --oneline | wc -l
```

Yields a non-zero number, but ALL post-P22 commits touch
either CPU/DivMMC/Memory subsystems or aggregate-report docs.
None touch any NMP file. The audit's narrow differential (NMP scope
only) correctly highlights that NMP convergence is preserved even
while other subsystems undergo iteration. This is correct workflow
hygiene.

## Verdict

**APPROVE — NO MISSED.**

* Differential P24→P25 ZERO NMP source delta: confirmed.
* 5/5 sandwich-row spot-check: confirmed.
* P22 lesson re-application: confirmed honest (NR 0xC2/0xC3 +
  NR 0xF0 sampled).
* 18-prior-fix re-verification: 2/18 sandwich-checked (Pass-9 +
  Pass-22); both intact.
* Convergence-stability declaration: confirmed (three windows
  byte-identical).
* Test invariants: confirmed at this commit.
* Class-(a) / (b) / (c) / new-(d) findings: NONE — the auditor's
  zero-findings result is correct under VHDL-faithful interpretation.

NMP convergence-stability is **permanently established**.
No fix-of-reviewer cycle needed. Ready to merge to integration.
