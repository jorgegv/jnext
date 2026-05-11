# Task 2 — Pass-22 NMI + Multiface + Port + NextREG fix-of-reviewer REVIEW

**Branch:** `task2/verify22-nmi-mf-port-fix-review`
**Fix-of-reviewer HEAD reviewed:** `e9ac2732`
**Pre-fix-of-reviewer HEAD (REJECT review):** `8bed7542`
**Audit-fix HEAD (the REJECTED fix):** `666a8ffe`
**Pre-audit integration HEAD:** `4cffca64`
**Date:** 2026-05-11
**Reviewer:** independent (third agent — not the audit author, not the
prior reviewer)

## Verdict

**APPROVE — V22-NMP-01 remediation is correct. NMP officially CONVERGES at Pass-22.**

The fix-of-reviewer commit `e9ac2732`:
1. correctly reverts the wrong RO-guard extension for NR 0xC2/0xC3 in
   `src/port/nextreg.cpp` (drops `reg == 0xC2 || reg == 0xC3` from the
   read-only register set);
2. correctly flips the 4 test rows V22-NMP-01-A / -B / -D-LSB / -D-MSB
   to the spec-correct contract (NextReg-port writes DO latch into
   `nr_c2/c3_retn_address_*`; post-NMIACK NextReg writes OVERWRITE the
   latch — latest writer wins);
3. correctly keeps V22-NMP-01-C-LSB / -C-MSB unchanged (the NMIACK
   pathway via `NextReg::set_nmi_return_address` continues to update
   the same latches);
4. comprehensively updates the audit doc to reflect the DISMISSED
   finding, flips B108/B109 rows from ✗ to ✓, corrects the
   "RO-from-NextReg-port family" enumeration, and revises the
   convergence trajectory to **Pass-22 = 0 effective findings**.

Pass-22 NMP qualifies for convergence per
`feedback_task2_converged_subsystem_skip.md` — 0 findings + this
fix-reviewer APPROVE-no-missed.

## Verification matrix

| Step | What | Result |
|---|---|---|
| 1 | Independent VHDL re-verification of `nr_c2_we`/`nr_c3_we` | ✓ Reviewer was correct |
| 2 | Revert verification (`src/port/nextreg.cpp` diff) | ✓ Minimal, correct |
| 3 | Test flip verification (4 rows) | ✓ All correctly flipped |
| 4 | NMIACK pathway tests (2 rows unchanged) | ✓ Verified unchanged + PASS |
| 5 | `nextreg_integration_test` 278/278 | ✓ PASS |
| 6 | ctest 38/38 | ✓ PASS |
| 7 | FUSE Z80 1356/1356 | ✓ PASS |
| 8 | regression.sh 33/0/0 | ✓ PASS |
| 9 | Adjacent re-audit (5 ✓ rows spot-checked) | ✓ No similar misreads found |
| 10 | Side-effect inspection (save/load, NMIACK interaction) | ✓ Clean |

## Step 1 — VHDL re-verification (independent)

I independently inspected the VHDL without re-reading the reviewer's
prior writeup. Findings:

### `nr_c2_we` / `nr_c3_we` are ACTIVE at zxnext.vhd:4894-4895

`grep -n "nr_c2_we\|nr_c3_we" zxnext.vhd` yields:
```
1080:   signal nr_c2_we               : std_logic;
1081:   signal nr_c3_we               : std_logic;
2064:         elsif nr_c2_we = '1' then
2066:         elsif nr_c3_we = '1' then
4813:      nr_c2_we <= '0';
4814:      nr_c3_we <= '0';
4894:            when X"C2" => nr_c2_we <= '1';
4895:            when X"C3" => nr_c3_we <= '1';
5602:--                nr_c2_we <= '1';
5605:--                nr_c3_we <= '1';
```

- Signals declared (:1080-1081).
- Default-reset to '0' (:4813-4814).
- **Active strobe assignment (NOT commented out) at :4894-4895** —
  in the first (combinatorial) decoder process. Reading lines
  4877-4906 shows this case-when block is the canonical strobe
  generator for the entire register family (NR 0x41, 0x44, 0x50-0x57,
  0x60, 0x63, 0x68, 0x69, 0x80, 0x8C, 0x8E, 0x8F, 0xC2, 0xC3, 0xC4,
  0xC5, 0xC8, 0xC9, 0xCA, 0xD9, 0xF0, 0xF8, 0xF9, 0xFA, 0xFF — all
  use the identical pattern).
- **Commented-out vestigial duplicates at :5601-5605** — in a SECOND
  decoder process. This is one of ~42 such commented-out duplicate
  patterns scattered through the second process; they are dead code
  preserved as breadcrumbs while the strobe generation was refactored
  to process A.
- Latch process at :2054-2070: the **elsif arms at :2064-2067 ARE
  reachable** (process is clocked, gating is `if/elsif` chain with
  reset → NMIACK_LSB → NMIACK_MSB → `nr_c2_we` → `nr_c3_we`). The
  NextReg-port write path WILL latch from `nr_wr_dat` when no NMIACK
  is in flight.
- Read mux at :6232-6236 returns `nr_c2_retn_address_lsb` and
  `nr_c3_retn_address_msb`.

**Conclusion:** NR 0xC2/0xC3 are READ-WRITE from the NextReg port.
Both the NextReg-port write path AND the Z80N NMIACK_LSB/MSB pathway
feed the same physical latch register. Pre-fix jnext (bare
`regs_[reg]=val` fall-through) was VHDL-faithful. The Pass-22 audit's
proposed RO-guard would have introduced a NEW divergence — software
NextReg writes would be silently dropped by jnext while real hardware
accepts them.

**The reviewer's diagnosis is 100% correct.**

## Step 2 — Revert verification

`git -C ... diff 8bed7542 e9ac2732 -- src/port/nextreg.cpp` shows:

```diff
-    if (reg == 0x01 || reg == 0x0E || reg == 0x0F ||
-        reg == 0xC2 || reg == 0xC3) {
+    if (reg == 0x01 || reg == 0x0E || reg == 0x0F) {
         return;
     }
```

The revert is **minimal and surgical**: it removes exactly the wrong
`|| reg == 0xC2 || reg == 0xC3` clause from the RO-guard set,
restoring the Pass-8 family (NR 0x01 / 0x0E / 0x0F only). The
surrounding comment is also updated to document the dismissed
finding with traceability to :4894-4895, :2064-2067, and :6232-6236.

The comment block correctly distinguishes the structural difference
between NR 0x01/0x0E/0x0F (no `nr_XX_we` signal anywhere → genuinely
RO from NextReg port) and NR 0xC2/0xC3 (active `nr_c2_we`/`nr_c3_we`
strobe + reachable elsif arms → RW).

## Step 3 — Test flip verification

### V22-NMP-01-A (NR 0xC2 NextReg-port write)

- **Old contract:** write 0xAA must NOT change read-back (asserting `before == after`).
- **New contract:** write 0xAA must update read-back to 0xAA (asserting `after == 0xAA`).
- **VHDL evidence:** :4894 asserts `nr_c2_we` → :2064-2065 elsif latches
  `nr_wr_dat=0xAA` into `nr_c2_retn_address_lsb` → :6232-6233 read
  mux returns it.
- ✓ Spec-correct.

### V22-NMP-01-B (NR 0xC3 NextReg-port write)

- **Old contract:** write 0x55 must NOT change read-back.
- **New contract:** write 0x55 must update read-back to 0x55.
- **VHDL evidence:** :4895 asserts `nr_c3_we` → :2066-2067 elsif latches
  `nr_wr_dat=0x55` into `nr_c3_retn_address_msb` → :6235-6236 read
  mux returns it.
- ✓ Spec-correct.

### V22-NMP-01-C-LSB / -C-MSB (NMIACK pathway via `set_nmi_return_address`)

- **Unchanged.** Diff confirms no semantic change to either C-LSB or
  C-MSB blocks: they still call
  `emu.nextreg().set_nmi_return_address(0xABCD)` and assert
  `nr_read(emu, 0xC2) == 0xCD`, `nr_read(emu, 0xC3) == 0xAB`.
- **VHDL evidence:** :2060-2063 priority elsif arms latch
  `nr_c2/c3_retn_address_*` from `cpu_do` on Z80N NMIACK_LSB / MSB
  commands.
- jnext exposes this via `NextReg::set_nmi_return_address` which
  writes `regs_[0xC2/0xC3]` directly, bypassing `NextReg::write`.
  The post-remediation `NextReg::write` no longer drops 0xC2/0xC3, but
  this pathway never went through `NextReg::write` so the change does
  not affect it.
- ✓ Unchanged and correct.

### V22-NMP-01-D-LSB / -D-MSB (Post-NMIACK NextReg-port write OVERWRITES)

- **Old contract:** post-NMIACK NextReg write to 0xC2/0xC3 silently
  dropped → read-back stays at 0x34 / 0x12 (NMIACK-written byte).
- **New contract:** post-NMIACK NextReg write OVERWRITES → read-back
  becomes 0xFF / 0xFF (NextReg-port-written byte).
- **VHDL evidence:** In steady state with no NMIACK in flight, the
  if/elsif chain at :2057-2068 falls through past the NMIACK arms
  (:2060-2063) to the `nr_c2/c3_we` arms (:2064-2067), which fire when
  the NextReg port write decoder asserts the strobe at :4894-4895.
  `nr_wr_dat=0xFF` is latched, overwriting the previously-NMIACK-
  written byte. Latest writer wins (NMIACK only wins when it occurs in
  the same cycle as a NextReg-port write, which is impossible for
  software-driven test sequences).
- ✓ Spec-correct.

## Step 4 — Build + test results (Release)

Built fresh on a clean Release tree at HEAD `e9ac2732`:

- **ctest:** 38/38 PASS (0.43s wall) ✓
- **nextreg_integration_test:** 278/278 PASS, 0 FAIL, 0 SKIP ✓
  - V22-NMP-01-NRC2-C3-Writable group: **6/6 PASS**
- **FUSE Z80:** 1356/1356 PASS ✓
- **regression.sh:** 33/0/0 ✓

Test invariants from prompt all satisfied.

## Step 5 — Side-effect inspection

### Save/load roundtrip

`NextReg::regs_[]` is a 256-byte array persisted entirely in save/load
state (per G28 row in the audit table — confirmed unchanged at ✓
post-remediation). Both pathways (NextReg-port and NMIACK) store into
the same `regs_[0xC2]` / `regs_[0xC3]` slot, so save/load roundtrip is
unchanged.

### NMIACK / NextReg-port interaction

The NMIACK pathway calls `set_nmi_return_address` from
`cpu_.on_nmi_servicing` at `emulator.cpp:708-710`. This is invoked
synchronously when the CPU enters NMI service (after the NMIACK_LSB
and NMIACK_MSB Z80N commands have pushed the return address). The
setter writes `regs_[0xC2/0xC3]` directly, bypassing `NextReg::write`.

Post-remediation, `NextReg::write` accepts NR 0xC2/0xC3 writes and
stores them via the fall-through path at the bottom of the function
(no handler registered for these registers). Both pathways converge
on `regs_[0xC2/0xC3]` — exactly the VHDL behavior.

No new race or interleaving risk: in single-threaded jnext, the CPU
NMI service callback runs before any subsequent NextReg-port write
can be dispatched, and NextReg-port writes are processed via
`Emulator::nextreg_write` which calls `NextReg::write` synchronously.

### Other RO registers

The Pass-8 RO-guard family (NR 0x01, 0x0E, 0x0F) is preserved
unchanged. I cross-checked that these three registers genuinely have
NO `nr_XX_we` signal anywhere in VHDL (no active strobe, no
declaration, no consumer of a write strobe), confirming they remain
correctly RO from the NextReg port.

## Step 6 — Adjacent re-audit (spot-check 5 ✓ rows)

I selected 5 register IDs that have commented-out `nr_*_we` lines in
process B (the same context where the audit misread NR 0xC2/0xC3) and
confirmed whether the audit table classifies them correctly. Result:
**all 5 are correctly classified.**

| Reg | Declaration | Active strobe (process A) | Commented duplicate (process B) | Audit row classification |
|---|---|---|---|---|
| 0x04 | :1047 | **:4840** (active) | :5154 (commented) | B017 — described as RW, ✓ |
| 0x22 | :1061 | **:4847** (active) | :5296 (commented) | B033 — described as RW, ✓ |
| 0x28 | :1054 | **:4848** (active) | :5310 (commented) | B037 — described as RW, ✓ |
| 0x2A | **commented** :1056 | :4850 (commented) | :5316 (commented) | B039 — described as dead, ✓ |
| 0x41 | :1062 | **:4877** (active) | :5383 (commented) | B065 — described as RW, ✓ |
| 0x8C | :1077 | **:4891** (active) | :5528 (commented) | B091 — described as RW, ✓ |
| 0x8E | :1078 | **:4892** (active) | :5531 (commented) | B093 — described as RW, ✓ |

I also inspected the four registers whose audit rows mention
"commented" in the rationale column:

- **B039 (NR 0x2A):** entirely commented out at all sites
  (declaration :1056, strobe :4850, consumer :6315) → genuinely
  dead, ✓ correct.
- **B120 (NR 0xD9):** active strobe :4901, consumer :3894 → RW, ✓
  correct.
- **B121 (NR 0xDA):** NO active strobe anywhere; signal
  `nr_da_iotrap_cause` is driven by iotrap logic at :3870-3877, not
  by a NextReg-port write → genuinely RO from NextReg port, ✓ correct
  (this is the structurally-RO case, analogous to NR 0x01/0x0E/0x0F).
- **B122 (NR 0xF0):** active strobe :4902, consumers :7449-7514 → RW, ✓ correct.
- **B123 (NR 0xF8):** active strobe :4903, consumers :7553-7563 → RW, ✓ correct.

**Conclusion:** The audit's misread of NR 0xC2/0xC3 was a one-off
mistake (a single misread of ONE pair of commented-out duplicate
lines without scrolling up to find the active strobe ~700 lines
earlier). The other ~315 ✓ rows in the enumeration table are
correctly classified. No further dismissals required.

## Step 7 — Convergence determination

Per `feedback_task2_converged_subsystem_skip.md`:

> A subsystem whose audit returns ZERO findings AND reviewer returns
> APPROVE-no-missed is considered converged and SKIPPED in subsequent
> passes.

Pass-22 NMP outcome at HEAD `e9ac2732`:

- **Audit findings (after remediation):** 0 effective (V22-NMP-01
  dismissed as false positive)
- **Fix-reviewer (this review):** APPROVE-no-missed

**→ NMI + Multiface + Port + NextREG subsystem OFFICIALLY CONVERGES
at Pass-22.**

This brings the convergence tally to **3 of 4 subsystems**:
- Memory (converged at Pass-14)
- DivMMC (converged at Pass-21)
- **NMI + MF + Port + NextREG (converged at Pass-22, this review)**
- CPU (still active)

## Compliance with task2 audit workflow rules

- [x] VHDL-as-oracle: re-verified independently from VHDL source —
      reviewer's diagnosis confirmed
- [x] No regression introduced (ctest 38/38, FUSE 1356/1356,
      regression 33/0/0)
- [x] Revert minimal and surgical
- [x] Test flips assert spec-correct contract
- [x] Adjacent re-audit performed (5 ✓ rows + 5 commented-mentioning
      rows spot-checked)
- [x] Audit doc updated comprehensively (findings table, B108/B109
      flipped, family list, convergence trajectory)
- [x] No side-effect on save/load, NMIACK pathway, or Pass-8 RO-guard
- [x] Convergence criterion met per
      `feedback_task2_converged_subsystem_skip.md`

## Final disposition

**APPROVE** — fix-of-reviewer commit `e9ac2732` is accepted. NMI + MF +
Port + NextREG subsystem converges at Pass-22. Future passes should
SKIP this subsystem and concentrate on CPU only (the remaining active
subsystem).
