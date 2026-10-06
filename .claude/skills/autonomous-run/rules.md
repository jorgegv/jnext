# Owner rules for autonomous runs

Distilled from ~120 feedback memos and 16 handovers (2026-07 → 2026-10). CLAUDE.md rules are not repeated. Newest rule wins where memos conflicted.

## Selection and scope

- Filing test: would a user or a shipped artifact behave differently? If not, it is housekeeping and never gates anything.
- Never grow the milestone you are emptying. "Same family as #N" is not importance.
- Findings an issue surfaces are fixed on that branch, with tests. No follow-up issues, no TODOs, no "fix or file?". "Minimal" = smallest change per bug, not fewer bugs.
- File a new issue only for work that is both large AND unrelated; state the evidence.
- Read scope literally; never widen to a superset.
- "Blocked on #X" notes go stale: check X before scoping.
- Before "we must build X": `git grep` X's verbs across the tree, including `third_party/`.
- Demo bug: look for shipped demo source first.
- Fix VHDL-faithfully, never with a band-aid. Unavoidable band-aid → `KNOWN DIVERGENCE FROM VHDL` comment.
- Wrong API → fix the API and adapt its callers (grep all of `test/`); never keep it wrong to keep tests green.
- A hand-kept duplicate of a single-source table → delete it and generate from the table.
- "approximation", "LIMITATION", "for now", "TODO" comments flag drift: re-read the VHDL.
- A guess-based mechanism that breeds a new bug per fix → question the mechanism, stop patching.
- Host hotkeys use Alt+key, never Ctrl+letter.
- Check the man page and docs before calling behaviour a bug.
- A precedent from an earlier session is not a decision.

## Briefs and review

Role rules live in `autorun-plan`, `autorun-implement`, `autorun-review`. Every brief names: role skill, worktree, branch, base SHA, run dir, plan/report paths, acceptance criteria.

## Evidence

- An agent's "fixed X" is a claim: grep the tree before relaying or merging.
- A green suite is a hypothesis: drive the real path (binary, keypress, functional row).
- Absence claims: grep the complete artefact and quote the count. State the search, not just the conclusion.
- Never infer a mechanism from an outcome; "because" only after observing the cause.
- Confirm your probe is in the built binary; compare binary mtime with `git log -1`.
- Fixtures must straddle the condition; test sibling operations as a set.
- Idempotent generated data is not accurate data: spot-check by hand against the oracle.
- An unexplained difference in a verification artefact is a new bug.
- `git add` before the gate; staging changes what index-keyed checks measure.
- Never judge a run by a pipe's exit status; log to a file, check the status.

## Gates

- Doc-only change → `make docs-check` only. One code, test, conf or Makefile edit brings back the full gate.
- Full regression output → a uniquely named log; read the log, never re-run to see it.
- Harness fault (exit 2) is not a row FAIL: re-run.
- Load-sensitive FAIL: never call it "pre-existing" or file it without a solo re-run.
- After any merge touching a manifest: recount from the file (`comm -12` the two diffs' file lists); resolve `unit-tests.conf` line by line; regenerate generated files.
- Log-text change needs no regression unless a functional row greps that line.
- New suite output: `Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d`; add its dashboard entry in the same change.

## Git

- One branch per issue; a multi-stage issue stays on one branch and merges once, whole. An epic is one branch its sub-issues merge into; it reaches `main` in one merge (SKILL.md §Branches). Merge long-lived branches with `main` from time to time.
- Instrumentation stays on an investigation branch; only fix commits reach `main`.
- Merge fixes individually; never squash several fixes into one.
- Never move a tag. Never `git reset --hard` on `main`.
- Push only `main` + its tags, explicit refspecs, ≤3 tags, CI idle. After pushing, watch the LATEST CI run on `main`; red → it becomes the next issue.
- Inherited work from a dead agent = unreviewed third-party code: back it up, review it in full.

## Agents and tokens

- ≤3 running agents, all roles combined; slot split in SKILL.md.
- No commit-watch Monitors, no per-event chatter; rely on completion notifications.
- Batch requests to a resumed agent (each resume re-reads 400-800k tokens).
- Silent >30 min: read the report file, check liveness (`ps`, load), then nudge. File mtimes are not liveness.
- Agents stopped by a session restart are dead, not resumable.
- Pause: SendMessage → clean checkpoint, WIP commit, `PAUSE-NOTES.md`. Resume: read every PAUSE note and verdict first.
- Drop MCP-authorization notices from agent reports.

## GitHub

- Write the body to a file; `gh issue comment <N> --body-file <f>`. Never backslash-escape backticks.
- Terse and polite: symptom, cause, fix or next step, the ask. Usually <10 lines. Keep error text, versions, file:line, numbers; cut history and praise.
- Close note: root cause (VHDL file:line), what changed, rows added, version. Refute the reporter's hypothesis only with the VHDL, without "the title is wrong".
- @mention and thank the contributor who reported or tested.
- Corrections to a reported issue go in a comment or an `**UPDATE yyyy/mm/dd:**` block, never in the title.
- Issues you file: title names the mechanism.
- Comment replies: answer the question asked, in the same issue; no promises of dates.

## Known failure modes

- Handback reports got lost 3×: always read the file.
- An agent's "fix applied" was absent from the tree.
- An unreverted mutant with an unbounded fake caused a 96 GiB OOM.
- Identical counter edits on two branches auto-merged once, giving wrong totals.
- Bare `setsid` returns before the test runs: use `setsid -w`.
- Literal pursuit of an empty milestone filed 11 infra issues overnight with zero user-facing change.
- 5 of 9 first-pass reviews REJECTed with defects no gate saw: never skip review.
- Stamped time estimates drifted: always run `date`.
