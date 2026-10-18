---
name: team-run
description: Run parallel work the owner directs in person and that does not come from the issue queue (several spikes at once, a multi-part task). The main session splits it into units, shows the plan, runs planner/implementer/reviewer agents in their own worktrees within the agent cap, has every unit independently reviewed, merges, and ends with a summary table. Use it instead of a manager agent.
---

# Team run

The owner is here and directs the work. CLAUDE.md applies in full. You, the main session, are the orchestrator, and you never write code. Roles, gates and the merge protocol are those of `autonomous-run` (its SKILL.md §Per-issue loop and `rules.md`); only the differences are written here. Load `agent-orchestration`.

## Why the main session and not a manager agent

- `agent-team-manager` (retired 2026-10-18) ended its turn while waiting six times and killed its own workers each time. Resuming a stalled manager made four duplicate spawns: one silently reverted finished work, another put two writers in one worktree. One manager ran `git reset --hard` on every worktree of a wave and wiped it (2026-07-31 and 2026-08-01, GH #196).
- A subagent cannot ask the owner anything mid-run. The main session can, and its turn can end while its agents keep running: their completion notifications wake it (verified 2026-10-17).

## Steps

1. **Split** the task into units. A unit is one independent change that can be reviewed on its own. It has a measurable success criterion, a name (`<topic>-<n>`), the files it touches, and its dependencies on other units. Units that touch the same files share one branch and worktree, and run one after another (one commit each).
2. **Show the plan** to the owner: the units, their order, the agents each one uses, and the branch each one merges into. Wait for the go. The run dir is `~/tmp/team/<YYYY-MM-DD>-<topic>/`: the plan, one spec file per unit, briefs, reports and `decisions.md`.
3. **Workspaces.** Per unit, a branch off the current target (`main`, or the epic branch for epic work) and a worktree via `worktree-launch`. Each reviewer gets a detached worktree of its own (`rev-<unit>`).
4. **Dispatch** within the owner's cap: at most 4 running agents, every role counted (owner, 2026-10-11). Queue the rest. A freed slot goes to the unit nearest to landing.
   - Roles: `autorun-planner` (only for a unit that needs a diagnosis or a design), `autorun-implementer`, `autorun-reviewer`; `vhdl-oracle` for read-only spec lookups.
   - Brief the role agents with the unit's spec file in place of an issue. The unit name replaces `gh<N>` in their file names.
5. **Review.** Every unit gets an independent `autorun-reviewer` with a binary APPROVE / REJECT. A REJECT goes back to the same implementer, and the same reviewer re-reviews.
6. **Merge** on APPROVE, one unit at a time, as CLAUDE.md "Merging a completed feature/fix to `main`" says. The implementer first merges the target into its branch. Then merge, `bump-patch` after each merge to `main`, and push only as CLAUDE.md's push rule allows.
7. **Finish** with the summary table below. With the owner's OK, clean up the worktrees and merged branches of finished units (`worktree-launch` §Cleanup): the standing cleanup authorization in CLAUDE.md covers closed issues only.

## Agents

- Agents run in the background and their notifications wake you: do not poll. Between dispatches, answer the owner or end the turn.
- Have every agent give its full report as its final response, and save it to the run dir yourself: the harness can refuse report files written by subagents (seen 2026-10-17). A report is a set of claims: check them against the tree before you relay them.
- SendMessage reaches agents you launched with the Agent tool. Never SendMessage an agent running inside a Workflow: it starts a second copy that works in the same worktree (2026-10-17).
- The Workflow tool only when the owner asks for one. Resuming an edited parallel workflow re-runs agents that had already finished; launch a fresh continuation script instead.
- To pause an agent, SendMessage it to checkpoint (WIP commit and `PAUSE-NOTES.md`); never kill it.

## Summary table

```
| Unit | Branch | Implementer | Reviewer | Verdict | Merged (version) |
|---|---|---|---|---|---|
```

Below it: the last batch gate on `main` (unit • sdl • win • FUSE • regression • regression-win) and the landings since then, what is still open, and what needs the owner.
