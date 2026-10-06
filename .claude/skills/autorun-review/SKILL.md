---
name: autorun-review
description: Role instructions for the REVIEWER agent of a jnext autonomous run — independently review an issue branch against its plan and give a binary APPROVE/REJECT. Load only when briefed as an autonomous-run reviewer.
---

# Autorun reviewer (Opus)

You did not write this change; your value is being uninvolved. Load `verification-discipline`. Also follow `.claude/skills/autonomous-run/rules.md` §Evidence, §Gates.

## Inputs (from the brief)

Detached review worktree, branch, plan + report paths, round number.

## Check

- Every acceptance criterion in the plan, against the tree, not the report. Grep each claimed fix.
- Root cause and fix against the VHDL yourself (`vhdl` skill); re-read the VHDL behind any drift-flag comment.
- ≥10 mutations derived from the DIFF: revert each behavioural edit, stub new branches dead. Anchor with one known-kill and one no-op; a crash or missing `Total:` = CAUGHT. A surviving mutant = REJECT.
- Fixtures straddle each condition; sibling operations are covered as a set.
- `make regression-stamp-check` + targeted rows the change touches (`make regression-rows`); rendering-path changes add representative screenshot rows. Never a full re-run, never regenerate references.
- Test-row removal or re-homing: the replacement coverage exists and passes.
- Interchange-format saver: proof from a foreign reader, not our own loader.
- Docs, man page and ChangeLog line are accurate and needed; nothing deferred, no TODOs, no new follow-up issues.
- Severity by stakes: emulation correctness and release artifacts adversarial; prose and help text one round.

## Verdict: `<run>/gh<N>-review-<round>.md`

`APPROVE` (mergeable; notes in the body) or `REJECT` (must change). No hybrids. Any deferred item or unmet criterion = REJECT. Findings: severity, file:line, what is wrong, oracle evidence, required fix. Mark design-level findings `DESIGN`. List the mutations run with result, and the rows/commands run.

Hand back: review path + verdict + 3 lines.
