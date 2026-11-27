---
name: autonomous-run
description: Work jnext GitHub issues unattended, end to end — epic and non-epic queues, planner/implementer/reviewer agents, gate, merge, bump, push, close, answer issue comments — until the milestone is empty, tokens run out, or a failure cannot be explained. Use when told to "work autonomously", "autonomous run", "overnight run", "work unattended", "keep going until the milestone is empty", "drive milestone X to zero", or to carry on without checking in.
---

# Autonomous run

Owner is away. Decide, record why, keep going. CLAUDE.md applies in full; this adds the run loop. Before the first dispatch read [rules.md](rules.md) and load the `agent-orchestration` skill. You (main session) are the orchestrator: you never write code.

## Setup

- Target milestone: as given at launch. Not given → ask once before starting; that is the last question.
- Guide audits (doc/RELEASE-PROTOCOL.md §2.3): the milestone must hold `milestone <M>: User Guide audit and review` and `milestone <M>: Developer Guide audit and review`. Create either one that is missing (label `documentation`, the milestone), without asking. They start only after every other issue and epic of the milestone has landed (RELEASE-PROTOCOL §2.3; owner, 2026-11-19): hold them out of both queues until then, so an idle non-epic slot waits rather than taking them early.
- Run dir: `~/tmp/autorun/jnext/<YYYY-MM-DD>/` (per project: a date-named dir shared with another project's run was overwritten on 2026-10-29): briefs, plans, reports, reviews, `decisions.md`, `agents.md` (slot, agent id, role, issue, worktree, state). Survives compaction.
- `git fetch origin`; if local `main` is behind, `JNEXT_ALLOW_MAIN_WRITE=1 git merge --ff-only origin/main`. Never reset.
- Read `/proc/loadavg`, `ps` for other Claude sessions, `git worktree list`. A branch/worktree another live session holds → skip that issue, record it.
- Build/test slots: write `<run>/slots/NSLOTS` (`2`, whether or not the owner is at the machine (owner, 2026-11-26): these are LOCAL slots; remote lanes are extra capacity that `slot.sh` counts itself; he may edit it any time) and write `<run>/slot.sh` as the short wrapper `#!/bin/bash`, `export JNEXT_REMOTE_RUN_HOSTS="<ssh alias> <ssh alias>" JNEXT_REMOTE_RUN_DIR=<work directory>` (the remote-run hosts in preference order; one host: `JNEXT_REMOTE_RUN_HOST=<alias>`), `exec <main checkout>/.claude/skills/autonomous-run/slot.sh --dir <run>/slots "$@"` (the main checkout's copy holds the logic; the contract `test/packaging/slot-test.sh` tests it; agent shells do not carry the two variables, so without the exports every heavy command stays local and says `JNEXT_REMOTE_RUN_HOST is not set (nor JNEXT_REMOTE_RUN_HOSTS)`). Briefs say: run every build and test through `<run>/slot.sh`, in the foreground. It admits by slot and load (below `nproc`), at nice 19, and PICKS THE HOST: every heavy `make -C <worktree> <target>` (the full unit-test targets, wasm-browser-test, full regression, regression-win/-wasm, regression-rows, test-affected) from a clean, pushed or pushable `gh<N>-*` branch goes to a free remote-run lane first, long or short (owner, 2026-11-23: fill the remote workers before building locally; it pushes the branch itself, never forced), and runs here only when every lane is busy; everything else, and every dirty tree, runs here; it tries the hosts in order and takes the first with a free lane (a probe, no wait per busy host); every lane of every host busy, it waits for a local slot and re-probes the hosts about once a minute while it waits. A run that fell back prints nothing until its summary (the driver logs it to a file only); `admissions.log` tags its local line `fallback`. A MUTANT (an uncommitted edit in a detached worktree at a pushed tip) goes with `slot.sh --patch make -C <wt> <target> JNEXT_SUITE_VMEM_KB=3145728`: it is sent to a free remote lane as a patch on the pushed commit, proved there (exit 2 `PATCH REFUSED` = no verdict, never a kill or a survivor), or runs here in its worktree when it cannot go; a clean worktree is refused. `JNEXT_SLOT_NO_OVERFLOW=1` keeps a run local: set it ONLY when the run needs this host (the owner's "unless needed": uncommitted state other than a `--patch` run, bench or xvfb probes, a measurement) or material that must never leave it (a reporter's game shared for testing, the user's own card, never committed); never for a gate run. A run admitted to a local slot gets `nproc`/NSLOTS threads (at least 1; `JOBS` through `MAKEFLAGS`, `JNEXT_UNIT_TEST_JOBS`, `JNEXT_TEST_JOBS`, each unless the caller set it) and one log line names them; remote runs keep the driver's caps (measured: two concurrent `make unit-test` from an empty build took 617/519 s at 12 threads each and 496/491 s at 6 each). `<run>/slots/admissions.log` has one line per admission (slot or remote, seconds waited, load): read it to tune `NSLOTS`. `slots/PAUSED` makes it refuse with 75.

## Work source: two queues

- **Epic queue**: open issues labelled `epic` in the milestone (usually one; more is allowed). Their open sub-issues (`gh api repos/jorgegv/jnext/issues/<E>/sub_issues`) in sub-issue order, unless dependencies dictate otherwise; epics by number. Every sub-issue goes here, bugs included.
- **Non-epic queue**: every other open milestone issue, including those from contributors. Order:
  1. `bug` (`critical` first)
  2. `feature` opened by someone other than the owner (`jorgegv`)
  3. `feature` opened by the owner
  4. `test-infra`
  5. others

  Ties: lowest number first.
- Re-read both queues after each landing. Skip HOLD and parked issues. External PRs: not touched.

## Slots and models

- **Agent cap** (the one place it is written; team-run, rules.md and audit-pass point here): at most 5 live agents, every role counted; concurrent builds and tests are admitted by `slot.sh` (local slots + remote lanes), not by an agent count (owner, 2026-11-26). The owner may reset it for a run. Of the slots, at least **2 epic + 1 non-epic** (owner, 2026-10-06), and the rest go where work exists. A queue with no workable issue lends its slots to the other; take them back when work appears there (at the next phase boundary, never mid-phase).
- An issue runs its phases one after another in its slot; only running agents count.
- Agent tool only: never wrap the loop in a Workflow, even when the harness defaults to one (owner, 2026-10-18: workflow agents do not show in the agents view). SendMessage only agents you started with the Agent tool; a message to a workflow agent forks it (team-run §Agents).
- Roles: dispatch by `subagent_type`; the agent definition pins the model and loads the role skill. Never pass a `model:` override, except where noted.

| role | `subagent_type` | model | role skill |
|---|---|---|---|
| orchestrator | (you) | Opus | this one + `agent-orchestration` |
| planner (design, diagnosis) | `autorun-planner` | opus | `autorun-plan` |
| planner, boot-stall issues | `boot-trace-detective` + `model: opus` | opus | brief: read `autorun-plan` too |
| implementer (code, tests, docs) | `autorun-implementer` | sonnet | `autorun-implement` |
| reviewer | `autorun-reviewer` | opus | `autorun-review` |

## Per-issue loop

1. Select the next issue for a free slot. Group issues that touch the same files on one branch (one commit each). Land near-done branches before starting new ones.
2. Branch + worktree via `worktree-launch` (see §Branches for the base).
3. Planner → `<run>/gh<N>-plan.md` (root cause, design, rows, mutations, acceptance criteria). Plan says PARK → park (see below).
4. Implementer, briefed with the plan path → `<run>/gh<N>-report.md`.
5. Verify the tree against the report's claims (grep), not the handback text.
6. The branch's tip is on `origin` first (push it if not: CLAUDE.md authorizes the issue branch; the reviewer's mutants go remote as patches on it). Reviewer in its own detached worktree (`git worktree add --detach ~/tmp/worktrees/jnext/rev-gh<N> gh<N>-<slug>`) → `<run>/gh<N>-review-<round>.md`. Read the FILE.
7. REJECT → SendMessage findings to the same implementer; design-level finding → back to the same planner first. Re-review by the same reviewer. 10 rounds without APPROVE → park.
8. APPROVE → implementer merges its target branch (§Branches) into the branch, recounts manifest counters by hand, keeps both lines in ChangeLog conflicts.
9. Land into the target. `main`: `JNEXT_ALLOW_MAIN_WRITE=1 git merge`, then `JNEXT_ALLOW_MAIN_WRITE=1 make bump-patch PUBLIC_RELEASE=n`; it stays local until the batch gate (step 10). Epic branch: plain merge, no bump. After each landing on `main`, add its ChangeLog line under `## Unreleased` on `main`, merged into an existing line on the same area where one exists (changelog-update §Style rules), with its contributor per CLAUDE.md's Contributors rule; commit it on `main` with `JNEXT_ALLOW_MAIN_WRITE=1 git commit` (it gates nothing; owner, 2026-11-18).
10. Batch gate, then push (main only).
    - When: only at a sync point (owner, 2026-11-21): the end of the session, a public release, or an urgent user-facing fix (a reporter waiting, a broken release); once, after all the landings so far, and before any push. The landing count never triggers a gate; landings in between stay local.
    - What: the CI gate (CLAUDE.md merge protocol step 2), run by the main session with `test/ci-gate.sh`: push `main`'s tip `<sha>` as `gate-v<version>`, `bash test/ci-gate.sh dispatch gate-v<version> <sha>`, `wait <run-id>`, `make ci-gate-check RUN=<run-id> SHA=<sha>`, then delete the branch from `origin`, green or red. No `regression-runner` agent is needed, so no agent slot is spent waiting. The local gate (`~/tmp/worktrees/jnext/main-gate`, a `regression-runner` agent, counted toward the cap) or `remote-run.sh --gate` is the fallback only, when GitHub Actions is down or gives no runner within 2 hours.
    - CLEAN and CI on main idle (`gh run list --workflow CI --limit 1`) → push the gated sha: `JNEXT_ALLOW_PUSH=1 git push origin <sha>:refs/heads/main` (not the local tip, which may have moved), then all the batch's tags at or before `<sha>` except the newest in one push, then the newest tag on its own (it always gets its tag event; a public release tag alone, RELEASE-PROTOCOL.md §6). CI busy → wait, then push.
    - Red → find the landing that broke it: `gh run view <id> --log-failed` names the failing suites and rows; re-run them at each merge commit of the batch (CLAUDE.md "A red batch gate"; a CI-only failure by dispatching `gate-v<version>-b<k>`, deleted after). Fix forward on a branch, or undo that merge with a new revert commit. Never push red.
11. Close the issue (rules.md §GitHub): standalone once pushed; epic sub-issue once merged into the epic branch, noting it reaches `main` with the epic. Untestable-by-gate fix (feel, speed, host-specific) → comment, leave open for reporter confirmation.
12. Once the issue is closed: remove both worktrees and delete the merged branch (`worktree-launch` §Cleanup). Parked issues keep theirs; anything that fails the checks is listed in the handover.

## Branches

- **Standalone issue** (bug, feature, …): `gh<N>-<slug>` off `main`; target `main`.
- **Epic**: `epic<E>-<slug>` off `main`, created when its first sub-issue starts, with its own worktree `~/tmp/worktrees/jnext/epic<E>` (merges and the final gate happen there). Sub-issues: `gh<N>-<slug>` off the epic branch (`worktree-launch` with the epic branch as base, not `main`); target the epic branch, never `main`. Merge `main` into the epic branch after each landing on `main`.
- **Multi-stage issue**: one branch until the whole issue is done, merged once; each stage is still reviewed on that branch (owner, 2026-09-24).
- **Epic finished** (all sub-issues closed, epic scope done):
  1. Merge `main` into the epic branch; `make unit-test` on it. Push the epic branch and start CI on it (`gh workflow run CI --ref epic<E>-<slug>`); it must be green (it covers Qt5, macOS clang and the bare CI container, which no local gate reaches; GH #444).
  2. Reviewer (`autorun-reviewer`) on the whole epic diff vs `main`: cross-sub-issue interactions, the merge, acceptance criteria of the epic.
  3. APPROVE → merge into `main` as one merge commit, `JNEXT_ALLOW_MAIN_WRITE=1 make bump-patch PUBLIC_RELEASE=n`, push (step 10).
  4. Terse summary comment on the epic (what was done, sub-issues closed, version), then close it.
- Epic branches may be pushed any time (`JNEXT_ALLOW_PUSH=1 git push origin epic<E>-<slug>`, never force; a push does not start CI, the finished epic gets a manual run, step 1 above). Stopping or pausing mid-epic → push it.

**Park** = WIP commit + `PAUSE-NOTES.md` on the branch, entry in `decisions.md`, slot moves to the next issue.

Between phases: check comments on milestone issues (and issues closed this run) since the last check; reply in the same issue. Clean up any issue closed since the last check (deferred closes included: reporter-confirmed, batched pushes) per step 12.

## Authorized

Merge of an APPROVED branch (its targeted tests and `make unit-test` green), `bump-patch` (private), push `main` + its tags after a green batch gate, push epic branches, push issue branches `gh<N>-<slug>` (never force) to share them with the remote-run host or to start CI on them before they land, push `main`'s tip as the temporary `gate-v<version>` branch and bisect branches `gate-v<version>-b<k>` and delete each from `origin` right after its run, close issues (finished epics included), after a push remove a closed issue's clean worktrees and merged branches (local and on `origin`), comment on, file and edit issues, ChangeLog `Unreleased` lines, Playwright for checking web artefacts.

## Never (owner's alone)

- Public release (`PUBLIC_RELEASE=y`, `releases.yaml`, release announcements, renaming the ChangeLog `Unreleased` header).
- Force push, pushing any branch other than `main`, epic branches, issue branches `gh<N>-<slug>` and the temporary `gate-v*` branches, tags other than a batch's, PRs, touching external PRs.
- Regenerating reference screenshots (sole exception: purely geometric change with 0 pixel AE everywhere).
- Changing a test to match the code → park (a tally pin grown by new rows or entries is not that: autorun-implement).
- Editing `EMULATOR-DESIGN-PLAN.md` / `TASK*-PLAN.md`, changing reported issue titles.

## Decisions

Decide; append to `<run>/decisions.md`: time, issue, decision, why, rejected alternative. Design forks: pick the VHDL-faithful option, record it, mention it in the close comment.

## Waits

- A LOCAL full regression (fallback gate only; a remote lane is tried first) runs only when the 1-min load < `nproc`. Otherwise wait in a background loop (`while awk -v n=$(nproc) '{exit !($1>=n)}' /proc/loadavg; do sleep 60; done`), then retry. No foreground `sleep`, no `pgrep -f` loops. (Owner rule, 2026-10-06, for unattended runs only; overrides the `regression` skill's no-wait default.)
- Timing row FAIL under load → wait for low load, then `make regression-confirm` (or `make regression-rows ROWS=<row> ROWS_PGO=1`, the PGO binary the full run tested). Record row + load.

## Stop the run when

- Both queues have no workable issue left (re-query; issues filed this run count; parked and HOLD issues do not).
- Token budget runs out.
- A test failure cannot be explained (FAIL on a quiet host, solo, reproducible, cause unknown).

On stop: finish or checkpoint agents (WIP commit + `PAUSE-NOTES.md`), merge finished sub-issues into their epic branch and push it, run the batch gate on `main` if anything landed since the last green one (push the gated sha if CLEAN), kill leftover gate processes, then the `handover` skill. Handover adds: landings table, `decisions.md` summary, ChangeLog lines added, parked issues with reason.
