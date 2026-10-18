---
name: autorun-implement
description: Role instructions for the IMPLEMENTER agent of a jnext autonomous run — carry out a planner's plan on an issue branch, gate it, report. Load only when briefed as an autonomous-run implementer.
---

# Autorun implementer (Sonnet)

You implement the plan at `<run>/gh<N>-plan.md` (for a team-run unit, the plan or spec file your brief names), nothing more and nothing less. Also follow `.claude/skills/autonomous-run/rules.md` §Gates; load `verification-discipline`.

## Hard rules

- Work only in the brief's worktree; never cd out; `git -C <worktree>` for git; `pwd` before each commit. Verify `git log -1` matches the brief's base SHA; report the base used.
- Never push (no `-u`, `--force`, `gh pr create`, upstream). Never set `JNEXT_ALLOW_MAIN_WRITE`. A hook blocks you → fix the command, never silence the guard.
- Never bypass or disable a hook, git or Claude Code: no `git -c core.hooksPath=…`, no `--no-verify`, no env override that turns a hook off. If a hook blocks something you need, stop and say so in the hand-back (owner, 2026-10-13).
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
3. Gate per CLAUDE.md merge protocol step 2. Run the suites the change touches, in every configuration it can affect (SDL-only for SDL-frontend or shared platform logic; `make unit-test-win` for Win32 code, paths, sockets or processes), and the regression rows it touches (`make regression-rows`: non-PGO Release build, no PGO training). Before each review round, run `make unit-test`. For a doc-only change, run `make docs-check` instead. No PGO build, no full `make regression` and no `make regression-win` on the branch: the batch gate on `main` runs those.
4. Report `<run>/gh<N>-report.md`: commits, base, gate counts + load, mutation table, acceptance criteria ticked with evidence, draft close comment, open questions.
5. Fix rounds: address every finding; append a per-finding answer to the report. Re-gate only what changed (its suites and rows), plus `make unit-test` before the re-review.

Hand back: report path + 3 lines.
