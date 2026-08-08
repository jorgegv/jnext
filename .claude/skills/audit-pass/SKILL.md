---
name: audit-pass
description: Run one pass of a Task-2-style subsystem audit. Dispatches a `subsystem-auditor` agent per subsystem in scope (in parallel where possible), then dispatches independent `subsystem-reviewer` agents on each result. Use when the user says "run an audit pass" or "audit subsystem X".
---

# Audit pass

Run one complete audit pass: auditor → reviewer, for one or more subsystems.

## Inputs

Ask the user (if not already specified):

- **Which subsystems?** (mmu, divmmc, nmi, cpu, ula, copper, sprites, nextreg, ports, ctc, … or "all boot-critical")
- **Pass number** (auto-increment from prior pass reports; ask only if ambiguous).
- **Base branch** (default: integration branch off main, or current branch if it looks audit-related).

## Worktrees

For each subsystem in scope, create a dedicated worktree — **outside the repo**,
per CLAUDE.md (anything walking the repository file list also walks an in-repo
worktree):

```
git worktree add /home/jorgegv/tmp/worktrees/audit-<subsystem>-pass<N> -b audit-<subsystem>-pass<N> main
```

Verify the base branch is fresh (per `feedback_agent_worktree_stale_base`), and
stop if the branch or worktree already exists — another session may hold it. The
`worktree-launch` skill does both checks plus fixture provisioning.

## Dispatch — auditors in parallel

Single message, multiple `Agent` calls (all `subsystem-auditor`), one per subsystem. Each gets:

- Subsystem name
- Worktree path
- Pass number
- Prior-pass report path (if any)
- The enumeration-table mandate (the agent already enforces this, but reaffirm)

**Wait for all to complete.**

## Dispatch — reviewers (NEVER self-review)

For each auditor that produced findings, spawn a `subsystem-reviewer` in parallel. Reviewer gets:

- Worktree path
- Original mandate
- Auditor's report path
- List of commit SHAs

Reviewer is critical-by-default. Verdicts are binary: APPROVE / REJECT. Residual
observations go in the body as notes, never in the verdict.

## Convergence accounting

After all reviewers report:

- For each subsystem: was it **converged**? (per `feedback_task2_converged_subsystem_skip` — zero findings AND APPROVE-no-missed = converged → skip in subsequent passes).
- Comment-only fix-of-reviewer commits skip the fix-reviewer step (per `feedback_task2_skip_review_comment_only`).
- If an auditor self-reviewed (mistake), reject and re-dispatch with a different agent.

## Merge

Per CLAUDE.md mandate, the manager (you, in this skill) merges worker branches to main only after reviewer APPROVE:

```
git checkout main
git merge --no-ff audit-<subsystem>-pass<N>
```

Test triplet on main MUST be green before declaring the pass complete.

## Final report to user

```
## Audit pass N report

### Subsystems audited
| Subsystem | Findings (class-a/b/c/d) | Reviewer verdict | Converged? | Merged to main? |
|---|---|---|---|---|
| mmu | 3/0/1/0 | APPROVE | NO | YES |
| ... | ... | ... | ... | ... |

### Cumulative
- Total fixes this pass: N (class-a × A + class-b × B + ...)
- Subsystems converged across all passes: <list>
- Subsystems still iterating: <list>

### Test triplet on main (post-merge)
ctest N/N • FUSE 1356/1356 • regression P/F/S

### Next pass
- Subsystems to audit: <list, excluding converged>
- Any architectural class-(d) items escalated to user: <list>
```

## The five mandates

Every pass is governed by these. The `subsystem-auditor` agent enforces them;
you reject any pass that violates one. Distilled from Task 2's 25 audit passes
(May 2026) on the boot-critical subsystems, after the "defensive zero / coverage
theatre" failures earlier in the project.

1. **Enumeration table at the top of every audit report**
   (`feedback_task2_audit_enumeration_table`) — a complete table covering *every
   surface in scope*, before any finding count. For MMU that means every NextREG
   written to it, every port that touches it, every slot-mapping operation, every
   IPL/loader/supervisor consumer, and every test rig that should validate it.
   Row format: `Surface | C++ site | VHDL line | Match ✓/✗ | Notes`.
   **Sparse table → reject. "No findings" without a complete table → reject.**
2. **Fix + discriminative regression test, in the same commit** — the test must
   FAIL on pre-fix code and PASS on post-fix code, with a file+line reference to
   the motivating VHDL. A fix "too obvious to need a test" is a red flag: the test
   documents the bug for the next reader. Write it anyway.
3. **Thorough per pass** (`feedback_task2_audit_thorough_per_pass`) — find as many
   bugs as possible in ONE pass. Agents drift into a wasteful
   find-one-fix-one-re-audit loop unless the prompt says otherwise.
4. **Enumerate consumers per boot stage**
   (`feedback_audit_enumerate_all_protocol_consumers_per_boot_stage`) — for any
   protocol bits (SD R1, OCR, NextREG readback, port masks, save-state schema),
   cover IPL/loader, kernel/runtime, supervisor, and test rigs separately.
   Single-layer enumeration is defensive zero: it shipped V20-DIVMMC-01, a
   class-(a) regression filed as class-(c).
5. **The audit prompt must carry the fix mandate**
   (`feedback_task2_audit_prompt_must_include_fix_mandate`) — the literal text
   *"fix + discriminative regression test in same commit"*. Without it agents
   default to report-only and you pay for an extra fix-of-audit cycle.

## Finding classes

- **class-a** — silent regression: wrong behaviour, no test catches it. Highest priority.
- **class-b** — test gap: right behaviour, but nothing would catch a future regression.
- **class-c** — lint/clean-up. Lowest priority, still fixed in the pass.
- **class-d** — architectural: too big for an audit fix. Escalate to the user with
  a cost/benefit, and implement on its own branch with its own audit + review.

## Pitfalls that mean reject

- **Defensive zero** — "audited X, found nothing" with no enumeration table.
- **Single-layer enumeration** — IPL consumers listed, supervisor consumers skipped.
- **Self-review** — the auditor approving its own work.
- **Non-discriminative test** — it passes on pre-fix code, so it proves nothing.
- **"Follow-up later"** — a reviewer noting a bug and deferring it
  (`feedback_review_no_defer`). Fix in this pass or escalate as class-d.
- **Coalesced fixes** — one commit for several findings breaks the fix↔test pairing.

## Hard rules

- **No self-review.** If you can't find a distinct agent for review, escalate.
- **No merge until APPROVE.** REJECTS go back to the auditor on its own branch.
- **No pushes.** Always local.
- **No main-branch writes by workers.** Only the manager merges to main, and only after APPROVE.
