# Traceability Matrix — Full Generation Plan

> Status: **Phases 1-3 COMPLETE (2026-08-01)**; Phase 4 is the ongoing,
> non-blocking burn-down. Umbrella issue: GH #196.
> Companion history: #144-#151, #158/#159, #184, #187-#195 — the two-day chain
> that measured the problem this plan removes.

## 1. Motivation

The #187-#195 chain settled three questions worth recording, because they are
the case for this plan:

**The defects were in the matrix, not in the test plan.** Of the whole chain,
~90% was meta-maintenance of the matrix and its extractor. The exceptions —
found only *because* the matrix was being cleaned — were: 4 missing tests
(#191), 3 plan-doc errors out of thousands of rows (#193), one vacuous
assertion (`MMU-CFG-06`, which passed regardless of the code), and **one real
emulator bug** (#194).

**The test plan and suites are fundamentally sound.** 6560 manifest-pinned
rows, 0 skips, VHDL-as-oracle derivation, mutation testing, independent
review, FUSE 1356/1356, regression 118/118, and an emulator that boots
NextZXOS and runs the corpus. The project's real quality mechanism is the
test discipline — the matrix was a layer *on top of it*, and that layer is
what was rotten.

**The matrix as an artifact costs more than it earns.** Hand-curated, it
produced ~20 issues of meta-work, one manufactured-coverage incident (#190 —
"pass" claims over behaviours nothing tested), and near-zero trustworthy
signal: an artifact of quality assurance that nobody can believe manufactures
false confidence instead. But the traceability *function* — which VHDL
behaviour is covered, and where — is genuinely valuable in a project whose
specification IS the VHDL, and cleaning the matrix did find a real bug. The
conclusion is not to delete the function but to make the artifact cost zero:
**fully generated, never hand-maintained, staleness-gated** — the same
pattern this repo already trusts for the man page, USAGE.md, the user guide
and the dashboard.

## 2. Problem

`doc/testing/TRACEABILITY-MATRIX.md` has **two sources of truth**: hand-written
cells and extractor-computed columns, partially synchronised under a
"never overwrite hand-written" rule. Every residual class measured in
#187-#195 is a symptom of that split, not a separate defect:

| Class | Count (extractor report @ v0.99.105) | Root cause |
|---|---|---|
| Frozen citations | 149 | hand cell with no computed side → nothing can contradict it |
| Drift | 335 | both sides exist and disagree |
| Unrecorded | 878 | live test row with no document row |
| Missing | 326 (+72 extra-coverage) | document row with no test; 71 are rewrite orphans (#192) |
| Extra-coverage rows | 85 | 4-column tables outside the main scheme (#192) |
| Cross-subsystem ID collisions | 76 | IDs are a global namespace nothing enforces (#190 root cause) |

## 3. Goal

**The matrix becomes a 100% generated build artifact.** Same pattern as
`doc/man/jnext.1` / `USAGE.md` / the dashboard: generated, committed,
staleness-gated by `make unit-test` / `make regression`. Hand-written cells
cease to exist as a concept.

## 4. Design decisions

1. **Sources of truth**: (a) test sources — a row's ID, description and VHDL
   citation live in its own `check()`/`skip()` call; (b) plan docs
   (`doc/testing/*-TEST-PLAN-DESIGN.md`) — planned rows and their prose;
   (c) ONE small exceptions file — row-level tombstones for the genuinely
   non-derivable (`(jnext-internal)`-style), with pinned syntax and selftest
   fixtures. Nothing else feeds the matrix.
2. **Live-row descriptions = the `check()` string.** Terser than curated prose;
   accepted. A wrong description then fails loudly next to its own assertion
   instead of rotting in a document nothing checks (#190/#193's class).
3. **The generator emits the whole file.** No read-modify-write of the
   committed matrix; the no-overwrite rule and every code path serving it are
   deleted. Frozen, doc-vs-computed drift, unrecorded rows and stale locations
   die **by construction**.
4. **The only surviving "drift" is genuine signal**: a plan doc and a test
   source citing different VHDL for the same row. Kept as a visible report.
5. **ID uniqueness becomes a gate** (an ID asserted in two subsystems fails),
   with a declared alias allowlist for the sanctioned reuse (ESP-01
   `TRACE-*`/`HOOK-*` convention).
6. **Reuse, don't rewrite.** `refresh-traceability-matrix.pl` already computes
   every tier and every counter; the change inverts authority, not machinery.
   `traceability-citations-selftest.pl` and `traceability-accounting-check`
   remain the guardrails.

## 5. What remains as honest, visible lists (not defects)

- **Planned-not-implemented** rows (post-triage `missing`) — real backlog.
- **Uncited** test rows (673) — burned down opportunistically, never blocking.
- **Plan-vs-source citation disagreements** — the surviving drift report.

## 6. Phases

### Phase 1 — Triage the inputs ✓ DONE (2026-08-01)
- 1.1 Classify the 326+72 `missing` rows: planned (stays in plan doc) vs
      rewrite orphan (dropped from the doc, recorded in the commit). The 71
      known orphans (#192) first.
- 1.2 Triage the 76 duplicate IDs: rename, or add to the alias allowlist.
- 1.3 Fold the 85 extra-coverage rows into plan docs or the exceptions file.
- 1.4 Migrate the 149 frozen citations: VHDL-verified ones into their row's
      `check()` string; the rest to tombstones or dropped. No citation is
      migrated unread (#187 rule). **Owner decision (2026-07-31): FULL triage —
      every candidate row gets the read-the-VHDL treatment. A cheaper
      migrate-only-the-already-verified shortcut was considered and rejected.**
- 1.5 Decide the 13 NMI summary-table rows (hand-asserted vs a quoted runtime):
      keep as prose outside the matrix proper.

### Phase 2 — Invert the generator ✓ DONE (2026-08-01)
- 2.1 Emit the full matrix from sources; delete the read-modify-write path and
      the no-overwrite rule.
- 2.2 Exceptions file: pinned syntax, accept+refuse selftest fixtures.
- 2.3 Auto-emit "live rows not in plan" per subsystem (kills `unrecorded`).
- 2.4 Plan-vs-source citation disagreement report (the surviving drift).
- 2.5 Update selftest fixtures + `$EXPECTED_ROWS`; mutation-test every new path.

### Phase 3 — Gate it ✓ DONE (2026-08-01)
- 3.1 Staleness gate (regenerate + diff, `docs-check` pattern) wired into
      `make unit-test` / `make regression`.
- 3.2 Duplicate-ID gate, honouring the alias allowlist.
- 3.3 Delete dead extractor paths; update the selftest accordingly.
- 3.4 Update CLAUDE.md + UNIT-TEST-PLAN-EXECUTION.md to the new rules.

### Phase 4 — Burn-down (non-blocking, ongoing)
- 4.1 Uncited rows: opportunistic, report stays visible.
- 4.2 Review the planned-not-implemented list → file real test-gap issues
      (#191's shape) or drop rows deliberately.

## 7. Acceptance criteria

- Generator is idempotent (byte-identical on consecutive runs) and the
  committed matrix always matches a fresh run — enforced by the gate.
- Zero hand-written cells; `frozen`, doc-vs-computed `drift` and `unrecorded`
  counters read 0 by construction.
- Every remaining `missing` row is a deliberate, plan-doc-declared claim.
- Duplicate-ID gate green with a fully-declared allowlist.
- Full triplet green throughout; every phase lands via the standard
  branch + independent review + merge + bump protocol.

## 8. Estimated cost

Phase 2+3 ≈ one issue-cycle of the #187-#195 kind (the extractor already does
~90% of the computation). Phase 1 is the bulk: a per-row triage pass across
~600 rows, mechanical for the majority (71 orphans, 316 tombstone-backed),
judgement for the rest. Phase 4 is open-ended and deliberately non-blocking.

## Outcome (2026-08-01)

The matrix is a generated artifact. Every cell is computed from a test source,
a plan doc or the one exceptions file; `make unit-test` regenerates it and
fails if the committed copy is stale.

Counters below are read from the committed matrix at each point, which is the
generator's own output — the staleness gate is what makes that a safe source.
"Before" is the Phase 1 tip, immediately prior to inverting the generator.

| Counter | Before | After |
|---|---:|---:|
| Rows | 2993 | 4084 |
| `unrecorded` | 884 | **0** (impossible by construction) |
| `frozen` | 60 | **0** (no hand-written side to freeze) |
| doc-vs-computed `drift` | 335 | **0** (same reason) |

Row descriptions are no longer hand-written either: a row with a test source
takes its description from that row's own `check()`/`skip()` call, and only a
`missing` row — which by definition has no source — takes it from the plan doc.
The generator does not publish a counter for that split, so do not quote one
here; the `Rows` identity it does publish (pass + fail + skip + missing) is the
number to cite.

What is deliberately NOT zero: `missing` rows (a plan doc lists them and no
suite asserts them — a real, visible backlog) and the plan-vs-source citation
disagreement report, which is signal about the spec rather than bookkeeping.

Two rules the work produced, now in CLAUDE.md: **a row ID must be a literal**
(building one at run time made six real rows invisible and two phantoms
visible), and **an ID is a global name** (29 pre-existing cross-suite
collisions are baselined; anything new refuses).
