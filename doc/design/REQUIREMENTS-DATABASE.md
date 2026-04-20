# Requirements Database — Design Note

**Status**: proposal. Not implemented. Queued for a future session once the current Task 3 SKIP-reduction programme lands.

**Motivation** (from the 2026-04-20 session /btw thread): the repo has a requirements/test traceability story today, but it's three-tier markdown (plans → matrix → dashboard), not a structured database. Cross-subsystem queries are grep gymnastics, priority/blocker tags don't exist as structured data, and the refresh script has known limitations (single test file per subsystem, no cross-file pointer following, no sub-letter alias support) that cause drift — e.g. the NextREG Phase 1 re-homing on 2026-04-20 flipped 32 rows from `skip` to `missing` in the matrix because the extractor can't follow the `COVERED AT …` / `TRACKED AT …` pointers introduced in the source comments.

The goal is a single queryable source of truth, populated mechanically from the existing authoritative artefacts (plan files + test sources + VHDL citations), that the matrix and dashboard become views of.

## What exists today (and why it's not enough)

1. **Per-subsystem plans** — [doc/testing/*-TEST-PLAN-DESIGN.md](../testing/) × 16 files. Each row carries: plan ID, preconditions, stimulus, expected value, VHDL `file:line` citation. Authoritative spec, derived exclusively from VHDL per
   [UNIT-TEST-PLAN-EXECUTION.md](../testing/UNIT-TEST-PLAN-EXECUTION.md) §1.
2. **Traceability matrix** — [doc/testing/TRACEABILITY-MATRIX.md](../testing/TRACEABILITY-MATRIX.md). Maps plan row → test ID → VHDL citation → status → test `file:line`. Refreshed by [test/refresh-traceability-matrix.pl](../../test/refresh-traceability-matrix.pl).
3. **Dashboard** — [test/SUBSYSTEM-TESTS-STATUS.md](../../test/SUBSYSTEM-TESTS-STATUS.md). Aggregate pass/fail/skip/total per subsystem, auto-refreshed by `make unit-test`.
4. **Live backlog** — `.prompts/YYYY-MM-DD.md` files carry the daily tasklist and the "Emulator Bug backlog" items surfaced by test runs. Not machine-readable.

**Concrete limitations that made today's session noisy:**

- **Not queryable.** Can't answer "every skipped row whose VHDL file is `layer2.vhd`" or "every NextZXOS-blocker row across all subsystems" without grep, and even then you reconstruct structure ad hoc.
- **Script assumption mismatch.** `refresh-traceability-matrix.pl` scans one test file per subsystem, literal-string ID match, no cross-file pointer following, no sub-letter alias support. The NextREG re-homing on 2026-04-20 produced 43 "missing" rows that are genuinely covered elsewhere — all visible to a human reading the source comments, invisible to the script.
- **Backlog drift risk.** Feature backlog notes live in three places: `skip(id, reason)` strings inside test sources, plan-file Current-status blocks, and `.prompts/YYYY-MM-DD.md` bug lists. They drift and sometimes disagree. A recent example (critic finding on 2026-04-20): the PE-05 skip reason said "seed regs_[0x89]=0xFF" until a critic pass caught that VHDL actually specifies 0x8F (bit 7 + 4-bit enable per zxnext.vhd:6147-6150). The plan file was silent; only the skip string was wrong.
- **No priority / blocker tags.** Which rows unblock NextZXOS boot? Which are cosmetic? The answer lives in prose (prompt files, session handovers) with no structured flag.
- **No tier labelling on plan rows.** Plans mix bare-class rows, subsystem rows, and integration-tier rows without an explicit tag. The NextREG plan's own Current-status block calls this out as "plan nit D" (2026-04-15).

## Proposed schema

**Storage**: SQLite file at `test/requirements.db`, committed to git (small, diffable via `sqldiff`). A plain CSV snapshot at `test/requirements.csv` can be emitted by the builder for grep-friendliness and matrix-script consumption.

**One table** (v1): `requirement`

| Column | Type | Notes |
|---|---|---|
| `subsystem` | text, not null | Canonical subsystem slug (e.g. `nextreg`, `memory-mmu`, `layer2`). Must match a plan file. |
| `row_id` | text, not null | Plan row ID, e.g. `CLIP-08`, `MMU-12`, `PAL-01`. Sub-letter variants (`CLIP-07a`, `CLIP-07b`) are separate rows, with an alias pointing to the canonical parent. |
| `alias_of` | text, nullable | For sub-letter variants: the canonical parent ID (e.g. `CLIP-07`). Null for parents. |
| `tier` | text, not null | `bare` / `subsystem` / `integration` / `full-machine` / `crosscut`. Inferred from test file location; can be overridden. |
| `vhdl_file` | text, not null | e.g. `zxnext.vhd`, `layer2.vhd`, `zxnext_top_issue2.vhd`. |
| `vhdl_line_start` | int, nullable | |
| `vhdl_line_end` | int, nullable | `NULL` when citation is a single line or imprecise. |
| `status` | text, not null | `pass` / `fail` / `skip` / `comment-rehome` / `missing` / `harness-gap`. `comment-rehome` is today's new state (skip converted to source comment). `missing` stays reserved for genuinely-dropped rows. |
| `test_file` | text, nullable | Test source file path. Null for `missing`. |
| `test_line` | int, nullable | |
| `covering_file` | text, nullable | For `comment-rehome` rows: where the real coverage lives (points to another test file/row). |
| `covering_row` | text, nullable | Foreign key onto `requirement.row_id` within that subsystem's scope. |
| `backlog_note` | text, nullable | The short reason string from `skip()` / the plan's Current-status block, normalised. |
| `priority` | text, nullable | `boot-blocker` / `nextzxos-blocker` / `feature` / `cosmetic` / `nit`. Manual tag. |
| `last_touched_commit` | text, not null | 10-char SHA of the most recent commit that moved the row's status. |
| `extracted_at` | text, not null | ISO-8601 timestamp of the last extraction. |

**Why one table, not three**: rows, citations, and statuses are 1:1:1 — a normalised design would produce mostly-empty foreign-key tables. Flatter is easier to query and diff.

**What the DB is NOT**: it is not the authoritative spec. The plan files remain authoritative — the DB is a generated view over them plus the live test source tree. If DB and plan disagree, plan wins; the builder must flag the divergence.

## Builder and migration

**Builder** (`test/build-requirements-db.pl`): single Perl script, extends the existing `test/refresh-traceability-matrix.pl` logic. Phases:

1. **Plan scan** — parse each `*-TEST-PLAN-DESIGN.md` for rows matching the `| ID | description | vhdl file:line |` style. Capture every row into `requirement` with `status='missing'` and `test_file=NULL` as the default.
2. **Test source scan** — for each subsystem, grep every `test/<sub>/*.cpp` for string-literal row IDs inside `check(...)` / `skip(...)` / struct initialisers. Match each hit to a plan row and upgrade the status.
3. **Comment-rehome scan** — look for the `// <ID> — COVERED AT <file> <row>` / `// <ID> — TRACKED AT <file> <row>` convention introduced in Phase 1 of today's NextREG work. Fill `covering_file` / `covering_row` and set `status='comment-rehome'`.
4. **VHDL citation extraction** — read the `zxnext.vhd:NNNN` / `layer2.vhd:NNNN-MMMM` citations inside each `check()` description string (not just the plan file — the test source is often more precise). Prefer the test-source citation when the two disagree.
5. **Backlog-note extraction** — copy the `skip()` reason string verbatim into `backlog_note`.
6. **Priority/blocker join** — read `test/requirements-priority.csv` (hand-maintained) and left-join on `(subsystem, row_id)` into `priority`.
7. **Commit fingerprint** — `git log -1 --format=%h -- <test_file>` for `last_touched_commit`.

The script is **idempotent** and emits a diff against the existing DB so CI can fail on drift.

**Data migration**: zero manual data entry needed for v1 — every column except `priority` is populated from existing artefacts. Running the builder once against the current tree produces the full DB. Expected result for today's state: ~1790 rows (Z80N excluded, or emitted with `tier='missing'` if we want the bookkeeping).

## Rollout

**v1** (single evening, 6-10h):

- Schema + builder script.
- Emit DB + CSV snapshot alongside the matrix refresh.
- No priority column yet (or populated as NULL).
- `test/requirements-query.pl --skip --subsystem nextreg` CLI for sanity checks.
- Existing matrix / dashboard stay in place, unchanged.

**v2** (a later session, 4-8h):

- Priority column populated via `requirements-priority.csv`.
- Matrix markdown becomes a rendered view of the DB (no more manual summary-table edits).
- CI check: builder run in pre-commit hook + drift check in CI.

**v3** (optional):

- Plan-file tier-labelling retrofit across the 16 subsystems (1-2h × 16 = 16-32h).
- Tier currently inferred from test file location; retrofit makes it explicit.
- Probably skip unless a query genuinely depends on it.

## Risks and open questions

- **Schema drift** — if plans evolve without the owner running the builder, DB goes stale. Mitigation: pre-commit hook that re-runs the builder and fails on drift against the committed DB file.
- **Parser false negatives** — novel `check()` / `skip()` idioms. Mitigation: the builder emits a "rows present in plan but not found in any test file" list on every run; deltas flag a missed idiom.
- **Sub-letter explosion** — as rows are split (`CLIP-07a`/`07b`), the DB row count grows and the plan row count stays the same. Current intent: track both, with `alias_of` linking variants to the canonical parent so summaries can roll up.
- **Re-homing convention enforcement** — today's NextREG Phase 1 introduced the `// <ID> — COVERED AT …` / `TRACKED AT …` convention in one subsystem. Repo-wide adoption is needed for the builder's `covering_file` extraction to be reliable. Can be retrofitted subsystem-by-subsystem in a separate pass (~1h × 16).

## Out of scope

- Any form of run-time test result ingestion (DB stores *plan* state, not *run* state beyond the most recent pass/fail). Live test runs stay in the dashboard.
- Cross-repo linking (e.g. to upstream VHDL commits). The citation is a file:line string, not a link.
- Web UI. CLI-only for v1.

## Context (for future pickup)

- Today's instigator: session 2026-04-20 NextREG Phase 1 SKIP-reduction, commit `d455e23`. After re-homing 32 skip()s to source comments, the matrix's "missing" count jumped from 11 to 43 for NextREG alone — entirely because the extractor can't follow the re-home pointers. That session put the issue on the map.
- Related memory: [`project_test_plan_audit_20260414.md`](../../../../Nextcloud/Claude/_claude/projects/-home-jorgegv-src-spectrum-jnext/memory/project_test_plan_audit_20260414.md) (the audit that established the current plan→matrix→dashboard hierarchy).
- The existing refresh script's behaviour is documented in [UNIT-TEST-PLAN-EXECUTION.md](../testing/UNIT-TEST-PLAN-EXECUTION.md) §6a.
