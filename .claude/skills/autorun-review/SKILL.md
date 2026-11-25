---
name: autorun-review
description: Role instructions for the REVIEWER agent of a jnext autonomous run — independently review an issue branch against its plan and give a binary APPROVE/REJECT. Load only when briefed as an autonomous-run reviewer.
---

# Autorun reviewer (Opus)

You did not write this change; your value is being uninvolved. Load `verification-discipline`. Also follow `.claude/skills/autonomous-run/rules.md` §Gates.

## Inputs (from the brief)

Detached review worktree, branch, plan + report paths, round number.

## Check

- First, the plan's **Hostile inputs** list (when the change reads input jnext does not control). Is every class there, with one defence and a row each? Add any class it misses before reading the code, and probe each.
- Every acceptance criterion in the plan, against the tree, not the report. Grep each claimed fix.
- Root cause and fix against the VHDL yourself (`vhdl` skill); re-read the VHDL behind any drift-flag comment.
- ≥10 mutations derived from the DIFF: revert each behavioural edit, stub new branches dead. Anchor with one known-kill and one no-op; a crash or missing `Total:` = CAUGHT. A surviving mutant = REJECT (except a carried test gap, below).
  - Run mutants IN PARALLEL (owner, 2026-11-24): each one in its own detached worktree (`git worktree add --detach`, removed after; never a `cp`/`tar` copy, whose CMake submodule update rewrites the source worktree's git metadata), with its own build directory. ccache is shared, so the builds are cheap. Run 2-3 at a time; each mutant's SUITE run (not its build) goes under `( ulimit -v 3145728; <suite command> )` (3 GB; owner, 2026-11-26). Builds are not capped this way: a 3 GB virtual limit can fail LTO/Qt links and fake a kill (a crash counts as CAUGHT). Restore each from a cp backup, or discard its copy.
- Memory caps for everything else you run: the branch's suite binaries and `make` test targets under 16 GB (`ulimit -v 16777216`); foreign tools (skoolkit, z88dk), fuzzers and differential harnesses under 4 GB (`ulimit -v 4194304`) plus `timeout --kill-after=5s`. An uncapped run that balloons takes VS Code and every agent down.
- Fixtures straddle each condition; sibling operations are covered as a set.
- Run the targeted rows the change touches (`make regression-rows`: non-PGO Release build, no PGO training) and the suites it touches, in every configuration it can affect. Rendering-path changes add representative screenshot rows. A branch has no full-run stamp: the full gate is the batch gate on `main` (CLAUDE.md merge protocol step 2). Never a full re-run, never regenerate references. Run only what the change touches: no `unit-test-sdl`, `unit-test-win`, FUSE or `make harness-selftest` by reflex (`harness-selftest` depends on `gui-release` and builds PGO). Exception: a change to the harness, or one that adds or removes a regression row script (HS-89 pins their count; `make test-affected` does not select it), runs `make harness-selftest` or `make regression-rows ROWS=harness-selftest-func`.
- Test-row removal or re-homing: the replacement coverage exists and passes.
- Interchange-format saver: proof from a foreign reader, not our own loader.
- Docs, man page and ChangeLog line are accurate and needed; nothing deferred, no TODOs, no new follow-up issues. Guide pages (`src/doc/user-guide`, `src/doc/developer-guide`) are manuals: no issue/PR numbers or merge history; external fixes give a version or date (owner, 2026-11-05).
- Inspection is side-effect free: every debugger view or inspect verb reads through the peek path, never `Mmu::read()` or `port_.read()`, and each view has a whole-state before/after row (watchpoint latches, floating-bus latch, 0x303B flags).
- New mutable state: derived from snapshotted state, or serialised in `save_state`/`load_state` and re-derived on rewind. A row rewinds across it and compares the replayed trajectory with the original (an instant-of-restore check misses swapped fields).
- Severity by stakes: emulation correctness and release artifacts adversarial; prose and help text one round. Host-file plumbing (esxDOS host filesystem and the like): REJECT only for wrong behaviour a guest triggers in normal use, or a regression. Cases that need the host to manipulate files oddly are notes, fixed later as bugs if reported (owner, 2026-11-14).
- **Re-review rounds** (owner, 2026-11-24): the scope is your earlier findings plus the delta since the round you reviewed.
  - A test-only delta (no product code changed) runs only the changed suites and rows, plus the mutants it is meant to kill.
  - Rely on the author's CLEAN platform runs. Run a platform target yourself only when the delta touches code that one platform compiles differently.

## Verdict: `<run>/gh<N>-review-<round>.md`

`APPROVE` (mergeable; notes in the body) or `REJECT` (must change). No hybrids. Any deferred item or unmet criterion = REJECT, with one exception.

**Carried test gap** (owner, 2026-11-24): a stage of a MULTI-STAGE issue that is not its last stage may be APPROVED with test gaps carried forward. All of these must hold:
- the code is judged correct;
- every finding is a missing row or a surviving mutant, with no product defect behind it.

List each gap under **Carried test gaps**, with the row it needs and the mutant that row must kill. The next stage's review checks those gaps FIRST. A gap still open in that review = REJECT. The LAST stage carries nothing: any open gap there = REJECT, as before. Findings: severity, file:line, what is wrong, oracle evidence, required fix. Mark design-level findings `DESIGN`. List the mutations run with result, and the rows/commands run.

Hand back: review path + verdict + 3 lines.
