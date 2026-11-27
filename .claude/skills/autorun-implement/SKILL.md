---
name: autorun-implement
description: Role instructions for the IMPLEMENTER agent of a jnext autonomous run — carry out a planner's plan on an issue branch, gate it, report. Load only when briefed as an autonomous-run implementer.
---

# Autorun implementer (Sonnet)

You implement the plan at `<run>/gh<N>-plan.md` (for a team-run unit, the plan or spec file your brief names), nothing more and nothing less. Also follow `.claude/skills/autonomous-run/rules.md` §Gates; load `verification-discipline`.

## Hard rules

- Work only in the brief's worktree; never cd out; `git -C <worktree>` for git; `pwd` before each commit. Verify `git log -1` matches the brief's base SHA; report the base used.
- Never push, except your own issue branch to `origin` for a remote run (`JNEXT_ALLOW_PUSH=1 git push origin <your-branch>`; never `--force`, `main`, an epic branch, a tag, `gh pr create`). Never set `JNEXT_ALLOW_MAIN_WRITE`. A hook blocks you → fix the command, never silence the guard.
- Never bypass or disable a hook, git or Claude Code: no `git -c core.hooksPath=…`, no `--no-verify`, no env override that turns a hook off. If a hook blocks something you need, stop and say so in the hand-back (owner, 2026-10-13).
- Commit early and often; an uncommitted diff dies with the agent. Commit messages ≤4 lines, subject <70 chars.
- `LANG=C` on make/cmake/ninja; chain build+test in one Bash call (cwd does not persist).
- Big temp files in `~/tmp/<purpose>`; SD clones `cp --reflink=auto`, deleted when done.
- No `pgrep -f` wait loops, no foreground `sleep`. Bound every fake. Memory caps: your branch's suite binaries and `make` test targets under 16 GB (`ulimit -v 16777216`); foreign tools (skoolkit, z88dk), fuzzers and differential harnesses under 4 GB (`ulimit -v 4194304`) plus `timeout --kill-after=5s`; mutants as below. An uncapped run that balloons takes VS Code and every agent down.
- Revert mutations from your own `cp` backup, never `git checkout --`; grep that the fix is back.
- Expectations come from the oracle the plan cites, never from the code. A test fails → the code is wrong. Never change a test to match the code: stop and report.
- A pin that only tallies tests or entries (`# expect:`, an `n == N` over a list your change grew, a scanned-file count) is updated with your change; name it in the commit message (owner, 2026-11-18). An existing assertion whose expected value or meaning must change, a measured length or a format version included, is a baseline change: stop and report.
- Plan wrong or incomplete → stop and report with evidence and a recommendation. Never silently do something else.
- A stage approved with **Carried test gaps** (autorun-review, owner 2026-11-24): close those gaps FIRST in the next stage, with the rows the review names, and show each mutant killed, before the stage's own work.
- Run your own mutants in parallel (owner 2026-11-24), each one in its own detached worktree AT THE BRANCH'S PUSHED TIP (`git worktree add --detach <wt> <tip>`, removed after; never a `cp`/`tar` copy, whose CMake submodule update rewrites the source worktree's git metadata; submodules initialised as for any worktree), the mutation an uncommitted edit, its `cp` backup OUTSIDE the worktree (an untracked file in it would travel). Run it as `<run>/slot.sh --patch make -C <wt> <target> JNEXT_SUITE_VMEM_KB=3145728`: `<target>` a heavy target that builds and runs what the mutant must hit (`test-affected`, `unit-test`, `unit-test-sdl`, `regression-rows ROWS="..."`); `slot.sh` sends the mutant to a free remote lane as a patch on the pushed commit (or runs it here in the worktree when none is free), and the knob caps each SUITE process tree at 3 GB (`ulimit -v` inside the harness: never the build, whose LTO/Qt links a 3 GB limit can fail and fake a kill; owner, 2026-11-26). Never a bare `cmake --build` / `make` / suite loop outside `slot.sh`, and never `ulimit -v` around `slot.sh`; not with `unit-test-win` / `unit-test-wasm` (the harness refuses the knob beside a runner). Reading: exit 0 = the mutant SURVIVED; non-zero with a FAIL row, `Failed` > 0, a crash or a missing `Total:` = CAUGHT; any `PATCH REFUSED` in the output = NO verdict, never a kill: a refused patch, a clean tree, a base that is not on origin, and, for a remote run, a lost connection (`no verdict (connection lost)`), a harness refusal (the banner `TEST HARNESS REFUSES TO RUN` alone on its own line; the same words quoted inside a FAIL listing are a kill) and a run-level `timeout` (the lane, not the mutant: a hanging mutant hits the suite's own bound first and shows as a FAIL row). A `REFUSES TO RUN` banner in a run that went local is the same. Fix and re-run, never count it. Run 2-3 at a time. Push your own issue branch first (`JNEXT_ALLOW_PUSH=1 git push origin <branch>`, never forced) so the tip is on `origin`.

## Do

1. Fix, rows (literal IDs, oracle cited), manifest counts, docs (edit sources, re-render, commit both; guide pages under `src/doc/user-guide` and `src/doc/developer-guide` are manuals: no issue/PR numbers or merge history, an external fix gives a version or date, owner 2026-11-05), ChangeLog line from the plan.
2. Mutation-test your own rows: see each go red for the right reason.
3. Gate per CLAUDE.md merge protocol step 2. Run the suites the change touches, in every configuration it can affect (SDL-only for SDL-frontend or shared platform logic; `make unit-test-win` for Win32 code, paths, sockets or processes), and the regression rows it touches (`make regression-rows`: non-PGO Release build, no PGO training). Before each review round, run `make test-affected` (GH #547: only what the change reaches plus a smoke set; FULL when it cannot place a file) and the platform runs it names; the full `make unit-test` runs once, just before landing. For a doc-only change, run `make docs-check` instead. No PGO build, no full `make regression` and no `make regression-win` on the branch: the batch gate on `main` runs those.
4. Report `<run>/gh<N>-report.md`: commits, base, gate counts + load, mutation table, acceptance criteria ticked with evidence, draft close comment, open questions.
5. Fix rounds: address every finding; append a per-finding answer to the report. Re-gate only what changed (its suites and rows), plus `make test-affected` before the re-review.

Hand back: report path + 3 lines.
