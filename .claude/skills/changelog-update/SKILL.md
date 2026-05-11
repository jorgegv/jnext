---
name: changelog-update
description: Coalesce commits since the last git tag into a ChangeLog entry following the project's strict rules (4 sections, terse, no commit IDs, no trivial fixes). Use when the user says "update the ChangeLog" or as a step of /version-bump.
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

## vP.Q.R (date)
...
```

If no commits exist for a section, omit the section (don't leave an empty header).

## Workflow

1. Find last tag: `git describe --tags --abbrev=0`.
2. Get commit list: `git log --oneline <last-tag>..HEAD`.
3. For each commit, ask: "would an end-user / developer-user / triager care about this in a changelog?" If no → drop.
4. Classify the rest into the 4 sections.
5. Coalesce similar items into single 10–20 word lines.
6. Write the new entry at the top of `ChangeLog` (above the previous version's entry).
7. Show the diff to the user. **Do NOT commit until they confirm** — per CLAUDE.md, ChangeLog updates are user-requested.

## Hard rules

- **CLAUDE.md says: "The file should only be updated when the user requests it."** Don't pre-empt.
- **Don't push.** Commit locally only.
- **Don't include "Co-Authored-By"** in the ChangeLog commit message (CLAUDE.md project mandate).
- **Don't claim work that isn't tested.** If a fix didn't pass the triplet, leave it out.
