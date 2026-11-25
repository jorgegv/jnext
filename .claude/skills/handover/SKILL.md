---
name: handover
description: Produce an end-of-session jnext handover memo in the project's established format (branch, HEAD, commits-ahead, batch gate, next-session priorities) and save it as an auto-memory entry. Use when the user says "prepare for handover", "prepare for session handover", "EOD", or similar.
---

# Session handover

Produce the end-of-session memo and save it to memory. The format is rigid (this project has 200+ handover memory entries; consistency matters).

## Inputs to gather (auto, don't ask)

Run these in parallel:

- `git rev-parse --abbrev-ref HEAD` → current branch
- `git rev-parse --short HEAD` and `git rev-parse HEAD` → commit IDs
- `git rev-list --count main..HEAD` → commits ahead of main
- `git rev-list --count origin/main..HEAD` → commits ahead of origin/main
- `git log --oneline main..HEAD` → list of session commits
- `git status --short` → working-tree state
- `cat .claude/last-test-triplet.txt` if present → last gate line

## Gate check

The end of a session is a sync point for the batch gate (CLAUDE.md merge protocol step 2: a CI run on `main`'s tip, as in `autonomous-run` step 10). If anything landed on `main` since the last pushed CLEAN gate, run it now: push `main`'s tip `<sha>` as the temporary branch `gate-v<version>` (`<sha>:refs/heads/gate-v<version>`, never force; dispatch refuses, exit 3, unless `origin/gate-v<version>` is exactly that 40-hex `<sha>`), then `test/ci-gate.sh dispatch gate-v<version> <sha>`, `wait <run-id>`, `make ci-gate-check RUN=<run-id> SHA=<sha>`, then delete the gate branch from `origin`, green or red. On CLEAN, push the gated sha and its tags under the standing authorization. The local `regression-runner` gate is the fallback only, when Actions cannot run. If there is no time for it (the owner is leaving now), write "batch gate not run; N landings unpushed" in the memo.

Write the gate line to `.claude/last-test-triplet.txt` (gitignored; the SessionStart hook shows it): `main <sha> (v<version>) CI gate <run-id> <verdict>, <date>`. The file records whichever gate ran last, CI or the local fallback. If nothing landed and no CLEAN gate covers HEAD's content (`make regression-ci-check`), ask the user whether to run the batch gate; if they say no, note "gate not freshly verified" in the memo.

## Memo content

Save to `/home/jorgegv/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/project_session_handover_<YYYY-MM-DD><suffix>.md`. Suffix is `_eod`, `b_eod`, `c_eod`, etc. depending on how many handovers happened today. Frontmatter:

```yaml
---
name: jnext session handover <YYYY-MM-DD><suffix>
description: <one-line summary of where the session left off — branch, what was done, what's next>
type: project
---
```

Body sections (in this order, terse, project conventions):

1. **State** — branch, HEAD short SHA, +N vs main, +N vs origin/main, tree clean/dirty, push status (NOT pushed unless the standing authorization or the user covered it).
2. **What this session did** — bullet list, one line per significant change. Reference commit SHAs.
3. **Gate** — last CI gate: run id, gated sha, verdict (`CLEAN` / failing jobs), timestamp; landings since. A local fallback run gives its triplet instead (unit N/N • sdl N/N • FUSE 1356/1356 • regression P/F/S (stamp?)).
4. **Discoveries / findings** — anything non-obvious learned this session that future-Claude will need.
5. **Next-session priority** — concrete next step(s), specific enough to act on without re-discovery.
6. **Outstanding** — anything escalated to user, deferred, or follow-up.

## Update MEMORY.md

Add a one-line index entry at the top of the "## Read first" section in `/home/jorgegv/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/MEMORY.md`:

```
- **[<file>](<file>) ← READ FIRST. <YYYY-MM-DD> EOD — <one-line state>.** <gate>. <next-priority>.
```

(The "← READ FIRST" marker moves from the previous handover to this one; downgrade the previous one to "(legacy)".)

## Hard rules

- **NEVER include Co-Authored-by lines** (CLAUDE.md project rule).
- **Terse but insightful** (CLAUDE.md). Don't write a development diary.
- **Don't claim a fix is shipped if the gate didn't confirm it.** Per `feedback_tremendous_claims_proof`, every claim needs proof; if the gate wasn't run, say so explicitly.
- **Push only what the standing authorization covers** (the gated sha and its tags after a CLEAN gate). Anything else stays local: mention "NOT pushed (N ahead of origin)" if commits exist.
- **Convert relative dates to absolute** (per memory rules).

## Examples to model after

Read `project_g46b_v2_2026_05_11_session_handover.md` and `project_task2_2026_05_11_FINAL_SESSION_HANDOVER.md` from memory for the exact tone, density, and structure.
