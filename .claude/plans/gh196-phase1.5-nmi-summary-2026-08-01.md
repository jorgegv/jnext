# GH #196 Phase 1.5 — NMI summary-table rows: prose, out of the matrix

Date: 2026-08-01. Manager: agent-team-manager (this session). Status: DONE, merged.

## Scope

Plan doc §6, task 1.5: "Decide the 13 NMI summary-table rows (hand-asserted vs
a quoted runtime): keep as prose outside the matrix proper."

Two hand-maintained tables sit inside `## NMI Source Pipeline` in
`doc/testing/TRACEABILITY-MATRIX.md`, both already marked `NOT REFRESHED`
(GH #192) because they carry neither a `Test file:line` nor a `VHDL file:line`
column: "Extended/self-streaming NEX" (9 rows, `XNEX-01..27`) and "Atic Atac
Next NMI regressions" (4 rows, `ATIC-NMI-01..04`). Total 13 rows. Both suites
already tombstoned in `%NO_MATRIX_SECTION` and listed in the matrix's own
tombstone table — these two full tables are a duplicate rendering left over
from commit 7f6d0afd (extended-NEX + stackless NMI landed together).

## Decision — placement

Relocate each table's content, as prose (not a table), out of the matrix
entirely, into the design/test-plan doc that owns that feature: XNEX ->
`doc/design/TASK84-EXTENDED-NEX-PLAN.md` §9; ATIC-NMI ->
`doc/testing/NMI-PIPELINE-TEST-PLAN-DESIGN.md` (final section). Rejected: a
prose appendix kept inside the matrix — Phase 2.1's full-generator-inversion
would need a permanent carve-out for it; moving out entirely needs nothing
from Phase 2 ever.

## Outcome

- Branch `gh196-phase1.5-nmi-summary`, commit `dbdb15f2` (after a manager
  double-dispatch collision on the same worktree, caught and reconciled by
  the second worker before commit — verified independently three times over:
  by the second worker, by a coordinator spot-check, and by a from-scratch
  independent review in its own worktree).
- Independent review verdict: **APPROVE** (own worktree, own build, own test
  run, word-for-word content diff against `main`'s original tables).
- Merged to main: `82a74ff7`. Bumped: **v0.99.137**.
- Counter movement: predicted zero, verified zero. Summary TOTAL unchanged
  (2996/2719/0/2/275/884/142/0). "TABLES NOT REFRESHED" report: 2 tables/13
  rows -> entirely absent.
- Triplet on main post-merge: unit-test 6566/6566 (90/90 suites) · FUSE
  1356/1356 · `make regression` 118/118 · traceability-selftest 183/183.
- Issue #196: task 1.5 checked off, summary comment posted.

## Note

An `rm -rf .claude/plans` typo during worktree cleanup deleted this file;
recreated from memory immediately after (no git-tracked content was lost —
this directory is untracked scratch, not repo history).
