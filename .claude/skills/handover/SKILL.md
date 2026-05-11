---
name: handover
description: Produce an end-of-session jnext handover memo in the project's established format (branch, HEAD, commits-ahead, test triplet, next-session priorities) and save it as an auto-memory entry. Use when the user says "prepare for handover", "prepare for session handover", "EOD", or similar.
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
- `cat .claude/last-test-triplet.txt` if present → last triplet

## Triplet check

If `.claude/last-test-triplet.txt` is missing OR older than the latest commit, ask the user whether to run the full triplet now (the `regression-runner` agent or `/test-triplet` command). If they say no, note "triplet not freshly verified" in the memo.

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

1. **State** — branch, HEAD short SHA, +N vs main, +N vs origin/main, tree clean/dirty, push status (NOT pushed unless user authorized).
2. **What this session did** — bullet list, one line per significant change. Reference commit SHAs.
3. **Test triplet** — ctest N/N • FUSE 1356/1356 • regression P/F/S, with timestamp.
4. **Discoveries / findings** — anything non-obvious learned this session that future-Claude will need.
5. **Next-session priority** — concrete next step(s), specific enough to act on without re-discovery.
6. **Outstanding** — anything escalated to user, deferred, or follow-up.

## Update MEMORY.md

Add a one-line index entry at the top of the "Latest session" section in `/home/jorgegv/.claude/projects/-home-jorgegv-src-spectrum-jnext/memory/MEMORY.md`:

```
- **[<file>](<file>) ← READ FIRST. <YYYY-MM-DD> EOD — <one-line state>.** <triplet>. <next-priority>.
```

(The "← READ FIRST" marker moves from the previous handover to this one; downgrade the previous one to "(legacy)".)

## Hard rules

- **NEVER include Co-Authored-by lines** (CLAUDE.md project rule).
- **Terse but insightful** (CLAUDE.md). Don't write a development diary.
- **Don't claim a fix is shipped if the triplet didn't confirm it.** Per `feedback_tremendous_claims_proof`, every claim needs proof; if the triplet wasn't run, say so explicitly.
- **Don't push.** A handover is local. Mention "NOT pushed (N ahead of origin)" if commits exist.
- **Convert relative dates to absolute** (per memory rules).

## Examples to model after

Read `project_g46b_v2_2026_05_11_session_handover.md` and `project_task2_2026_05_11_FINAL_SESSION_HANDOVER.md` from memory for the exact tone, density, and structure.
