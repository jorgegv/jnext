---
name: autonomous-run
description: Work jnext GitHub issues unattended, end to end — epic and non-epic queues, planner/implementer/reviewer agents, gate, merge, bump, push, close, answer issue comments — until the milestone is empty, tokens run out, or a failure cannot be explained. Use when told to "work autonomously", "autonomous run", "overnight run", "work unattended", "keep going until the milestone is empty", "drive milestone X to zero", or to carry on without checking in.
---

# Autonomous run

Owner is away. Decide, record why, keep going. CLAUDE.md applies in full; this adds the run loop. Before the first dispatch read [rules.md](rules.md) and load the `agent-orchestration` skill. You (main session) are the orchestrator: you never write code.

## Setup

- Target milestone: as given at launch. Not given → ask once before starting; that is the last question.
- Run dir: `~/tmp/autorun/<YYYY-MM-DD>/`: briefs, plans, reports, reviews, `decisions.md`, `agents.md` (slot, agent id, role, issue, worktree, state). Survives compaction.
- `git fetch origin`; if local `main` is behind, `git merge --ff-only origin/main`. Never reset.
- Read `/proc/loadavg`, `ps` for other Claude sessions, `git worktree list`. A branch/worktree another live session holds → skip that issue, record it.

## Work source: two queues

- **Epic queue**: open issues labelled `epic` in the milestone (usually one; more is allowed). Their open sub-issues (`gh api repos/jorgegv/jnext/issues/<E>/sub_issues`) in sub-issue order, unless dependencies dictate otherwise; epics by number. Every sub-issue goes here, bugs included. Never close the epic: when its last sub-issue closes, comment that on the epic.
- **Non-epic queue**: every other open milestone issue, including those from contributors. Order:
  1. `bug` (`critical` first)
  2. `feature` opened by someone other than the owner (`jorgegv`)
  3. `feature` opened by the owner
  4. `test-infra`
  5. others

  Ties: lowest number first.
- Re-read both queues after each landing. Skip HOLD and parked issues. External PRs: not touched.

## Slots and models

- 3 live agents max (owner cap), in **2 epic slots + 1 non-epic slot**. A queue with no workable issue lends its slots to the other; take them back when work appears there (at the next phase boundary, never mid-phase).
- An issue runs its phases one after another in its slot; only running agents count.
- Roles (`model:` override on every `Agent` call; `subagent_type: general-purpose` unless noted):

| role | model | skill (brief says: read `.claude/skills/<skill>/SKILL.md` first) |
|---|---|---|
| orchestrator | you (Opus) | this one + `agent-orchestration` |
| planner (design, diagnosis) | `opus` | `autorun-plan`; boot-stall issues: `subagent_type: boot-trace-detective` |
| implementer (code, tests, docs) | `sonnet` | `autorun-implement` |
| reviewer | `opus` | `autorun-review` |

## Per-issue loop

1. Select the next issue for a free slot. Group issues that touch the same files on one branch (one commit each). Land near-done branches before starting new ones.
2. Branch `gh<N>-<slug>` + worktree via `worktree-launch`.
3. Planner → `<run>/gh<N>-plan.md` (root cause, design, rows, mutations, acceptance criteria). Plan says PARK → park (see below).
4. Implementer, briefed with the plan path → `<run>/gh<N>-report.md`.
5. Verify the tree against the report's claims (grep), not the handback text.
6. Reviewer in its own detached worktree (`git worktree add --detach ~/tmp/worktrees/rev-gh<N> gh<N>-<slug>`) → `<run>/gh<N>-review-<round>.md`. Read the FILE.
7. REJECT → SendMessage findings to the same implementer; design-level finding → back to the same planner first. Re-review by the same reviewer. 10 rounds without APPROVE → park.
8. APPROVE → implementer merges current `main` into the branch, recounts manifest counters by hand, keeps both lines in ChangeLog conflicts.
9. Land: `JNEXT_ALLOW_MAIN_WRITE=1 git merge`, then `make bump-patch PUBLIC_RELEASE=n`.
10. Push: CI on main idle? (`gh run list --workflow CI --limit 1`). Busy → keep landing locally, push later in one batch. `JNEXT_ALLOW_PUSH=1 git push origin main <tags…>`, ≤3 tags per push.
11. Close the issue once pushed (rules.md §GitHub). Untestable-by-gate fix (feel, speed, host-specific) → comment, leave open for reporter confirmation.
12. Remove both worktrees; keep the branch.

**Park** = WIP commit + `PAUSE-NOTES.md` on the branch, entry in `decisions.md`, slot moves to the next issue.

Between phases: check comments on milestone issues (and issues closed this run) since the last check; reply in the same issue.

## Authorized

Merge of a green + APPROVED branch, `bump-patch` (private), push `main` + its tags, close issues, comment on issues, ChangeLog `Unreleased` lines, Playwright for checking web artefacts.

## Never (owner's alone)

- Public release (`PUBLIC_RELEASE=y`, `releases.yaml`, release announcements, renaming the ChangeLog `Unreleased` header).
- Force push, pushing branches, PRs, touching external PRs, closing an epic.
- Regenerating reference screenshots (sole exception: purely geometric change with 0 pixel AE everywhere).
- Changing a test to match the code → park.
- Editing `EMULATOR-DESIGN-PLAN.md` / `TASK*-PLAN.md`, changing reported issue titles.

## Decisions

Decide; append to `<run>/decisions.md`: time, issue, decision, why, rejected alternative. Design forks: pick the VHDL-faithful option, record it, mention it in the close comment.

## Waits

- Full regression only when 1-min load < `nproc`. Otherwise wait in a background loop (`while awk -v n=$(nproc) '{exit !($1>=n)}' /proc/loadavg; do sleep 60; done`), then retry. No foreground `sleep`, no `pgrep -f` loops.
- Timing row FAIL under load → wait for low load, then `make regression-confirm` (or `make regression-rows ROWS=<row>`). Record row + load.

## Stop the run when

- Both queues have no workable issue left (re-query; issues filed this run count; parked and HOLD issues do not).
- Token budget runs out.
- A test failure cannot be explained (FAIL on a quiet host, solo, reproducible, cause unknown).

On stop: finish or checkpoint agents (WIP commit + `PAUSE-NOTES.md`), kill leftover gate processes, then the `handover` skill. Handover adds: landings table, `decisions.md` summary, ChangeLog lines added, parked issues with reason.
