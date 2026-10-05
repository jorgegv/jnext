---
name: changelog-update
description: Coalesce commits since the last git tag into a ChangeLog entry following the project's strict rules (4 sections + contributors, terse, no commit IDs, no trivial fixes). Use when the user says "update the ChangeLog" or as a step of /version-bump.
---

# ChangeLog update

The `ChangeLog` file lives at repo root. Per CLAUDE.md, it has very specific rules — they exist because the ChangeLog targets **emulator users**, not developers, and is **NOT** an exhaustive change log.

## Inputs

- **Target version:** the version that will be bumped to (e.g. `v0.92.0`). If unknown / not bumping, use `(current date)` per CLAUDE.md fallback.
- **Date:** today's date in `YYYY-MM-DD` form.

## Source material

```
git log --oneline <last-tag>..HEAD
git log --stat <last-tag>..HEAD
```

Also scan handover memos since the last tag for "what this session did" sections — they're often more readable than raw commit messages.

## The 4 sections (in this order)

For each commit, classify into exactly ONE section. Drop trivial commits entirely.

### User Features
New things the user (= someone running games / programs) will notice. Examples:
- New GUI screen / menu item
- New emulation feature (e.g. "MMU memory contention now modelled")
- New main-menu option
- Significant performance improvement

### Developer Features
New things a developer using jnext will notice. Examples:
- New debugger feature
- New instrospection panel
- New CLI flag
- New test framework capability

### Bug Fixes
Functional fixes that change behaviour for the user. Examples:
- "NextZXOS now boots past the supervisor screen"
- "Audio no longer crackles on Beast"

### Internal JNEXT Development
Big architectural/test/process changes the user doesn't directly see but that are worth noting. Examples:
- "Subsystem audit framework with enumeration-table mandate"
- "Regression test suite restructured to 33 screenshot cases"

### Contributors in this release
Public releases only, from v1.1.0 on (owner decision 2026-10-04). One `- ` bullet: a comma-separated list of the GitHub handles of everyone other than the owner (jorgegv) who contributed since the previous public release — opened an issue or PR, commented on one, reviewed a PR, or tested and reported back (owner, 2026-10-05: testers and commenters count). No per-person details. `<date>` = the tag time of the previous public release (the newest tag in `releases.yaml`). Find them:
  - openers: `gh issue list --state all --search "created:>=<date>"` and `gh pr list --state all --search "updated:>=<date>"`;
  - commenters and testers: for EVERY issue/PR updated since `<date>`, `gh api --paginate repos/jorgegv/jnext/issues/<n>/comments` and keep comments with `created_at >= <tag time>`. Do NOT rely on the repo-wide `issues/comments?since=` endpoint: it filters by update time and returned an incomplete set when this rule was introduced (it missed a tester);
  - PR reviewers: `gh api repos/jorgegv/jnext/pulls/<n>/reviews` and `pulls/<n>/comments`.
  Drop bots and the owner.

## Style rules (strict)

- **One line per entry, 10–20 words.** No exceptions.
- **Coalesce similar items.** If 5 commits all touch DMA timing, that's ONE entry.
- **NO commit IDs.** Don't paste SHAs.
- **NO trivial fixes.** Reformats, doc tweaks, test name changes, internal renames, project-plan updates, CI tweaks — drop entirely.
- **NO ongoing features.** If a feature is half-done or has known bugs, do NOT include it. Per CLAUDE.md: "Don't be overly confident about features."
- **Most recent at top.** Reverse chronological.

## File format

```
# ChangeLog

## vX.Y.Z (YYYY-MM-DD)

### User Features
- ...

### Developer Features
- ...

### Bug Fixes
- ...

### Internal JNEXT Development
- ...

### Contributors in this release
- @handle1, @handle2

## vP.Q.R (date)
...
```

If no commits exist for a section, omit the section (don't leave an empty header).

## Workflow

1. Find last tag: `git describe --tags --abbrev=0`.
2. Get commit list: `git log --oneline <last-tag>..HEAD`.
3. For each commit, ask: "would an end-user / developer-user / triager care about this in a changelog?" If no → drop.
4. Classify the rest into the 4 sections, then build the contributors list.
5. Coalesce similar items into single 10–20 word lines.
6. Write the new entry at the top of `ChangeLog` (above the previous version's entry).
7. Show the diff to the user. **Do NOT commit until they confirm** — per CLAUDE.md, ChangeLog updates are user-requested.

## Hard rules

- **CLAUDE.md says: "The file should only be updated when the user requests it."** Don't pre-empt.
- **Don't push.** Commit locally only.
- **Don't include "Co-Authored-By"** in the ChangeLog commit message (CLAUDE.md project mandate).
- **Don't claim work that isn't tested.** If a fix didn't pass the triplet, leave it out.
