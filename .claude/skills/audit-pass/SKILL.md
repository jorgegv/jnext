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

For each subsystem in scope, create a dedicated worktree:

```
git worktree add .claude/worktrees/audit-<subsystem>-pass<N> -b audit-<subsystem>-pass<N>
```

Verify the base branch is fresh (per `feedback_agent_worktree_stale_base`).

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

Reviewer is critical-by-default. Verdicts: APPROVE / APPROVE-WITH-NITS / REJECT.

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

## Hard rules

- **No self-review.** If you can't find a distinct agent for review, escalate.
- **No merge until APPROVE.** REJECTS go back to the auditor on its own branch.
- **No pushes.** Always local.
- **No main-branch writes by workers.** Only the manager merges to main, and only after APPROVE.
- **Five mandates from feedback memory.** See `subsystem-auditor` agent definition — it enforces them, but you must reject any pass that violates them (e.g. sparse enumeration tables, missing regression tests).
