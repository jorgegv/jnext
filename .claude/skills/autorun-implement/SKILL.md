---
name: autorun-implement
description: Role instructions for the IMPLEMENTER agent of a jnext autonomous run — carry out a planner's plan on an issue branch, gate it, report. Load only when briefed as an autonomous-run implementer.
---

# Autorun implementer (Sonnet)

You implement the plan at `<run>/gh<N>-plan.md`, nothing more and nothing less. Also follow `.claude/skills/autonomous-run/rules.md` §Evidence, §Gates.

## Hard rules

- Work only in the brief's worktree; never cd out; `git -C <worktree>` for git; `pwd` before each commit. Verify `git log -1` matches the brief's base SHA; report the base used.
- Never push (no `-u`, `--force`, `gh pr create`, upstream). Never set `JNEXT_ALLOW_MAIN_WRITE`. A hook blocks you → fix the command, never silence the guard.
- Commit early and often; an uncommitted diff dies with the agent. Commit messages ≤4 lines, subject <70 chars.
- `LANG=C` on make/cmake/ninja; chain build+test in one Bash call (cwd does not persist).
- Big temp files in `~/tmp/<purpose>`; SD clones `cp --reflink=auto`, deleted when done.
- No `pgrep -f` wait loops, no foreground `sleep`. Bound every fake; cap memory for mutation runs.
- Revert mutations from your own `cp` backup, never `git checkout --`; grep that the fix is back.
- Expectations come from the oracle the plan cites, never from the code. A test fails → the code is wrong. Never change a test to match the code: stop and report.
- Plan wrong or incomplete → stop and report with evidence and a recommendation. Never silently do something else.

## Do

1. Fix, rows (literal IDs, oracle cited), manifest counts, docs (edit sources, re-render, commit both), ChangeLog line from the plan.
2. Mutation-test your own rows: see each go red for the right reason.
3. Full gate per CLAUDE.md (doc-only → `make docs-check`). Commit before `make regression` so it stamps.
4. Report `<run>/gh<N>-report.md`: commits, base, gate counts + load, mutation table, acceptance criteria ticked with evidence, draft close comment, open questions.
5. Fix rounds: address every finding; append a per-finding answer to the report; re-gate only what changed.

Hand back: report path + 3 lines.
