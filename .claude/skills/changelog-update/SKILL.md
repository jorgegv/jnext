---
name: changelog-update
description: Coalesce commits since the last public release (newest tag in releases.yaml) into the ChangeLog `Unreleased` entry following the project's strict rules (4 sections + contributors, terse, no commit IDs, no trivial fixes). Use when the user says "update the ChangeLog" or as a step of /version-bump.
---

# ChangeLog update

The `ChangeLog` file lives at repo root. Per CLAUDE.md, it has very specific rules — they exist because the ChangeLog targets **emulator users**, not developers, and is **NOT** an exhaustive change log.

## Inputs

- **Target:** `Unreleased (YYYY-MM-DD)`, or the public release version being cut.
- **Date:** today's date in `YYYY-MM-DD` form.

## Source material

```
git log --oneline <last-public-tag>..HEAD
git log --stat <last-public-tag>..HEAD
```

Also scan handover memos since the last public release for "what this session did" sections — they're often more readable than raw commit messages.

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
Public releases only, from v1.1.0 on (owner decision 2026-10-04). One `- ` bullet: a comma-separated list of the handles of everyone other than the owner (jorgegv) who contributed since the previous public release — opened an issue or PR, commented on one, reviewed a PR, or tested and reported back (owner, 2026-10-05: testers and commenters count). Every handle is `@handle` whatever its origin — GitHub, a forum, elsewhere — with no note of where it comes from (owner, 2026-10-07: `@vmorilla, @MickeyG`). No per-person details. `<date>` = the tag time of the previous public release (the newest tag in `releases.yaml`). Find them:
  - openers: `gh issue list --state all --search "created:>=<date>"` and `gh pr list --state all --search "updated:>=<date>"`;
  - commenters and testers: for EVERY issue/PR updated since `<date>`, `gh api --paginate repos/jorgegv/jnext/issues/<n>/comments` and keep comments with `created_at >= <tag time>`. Do NOT rely on the repo-wide `issues/comments?since=` endpoint: it filters by update time and returned an incomplete set when this rule was introduced (it missed a tester);
  - PR reviewers: `gh api repos/jorgegv/jnext/pulls/<n>/reviews` and `pulls/<n>/comments`;
  - non-GitHub contributors (forum requesters, testers): `gh` cannot list them — they are named in the issue bodies and comments ("Requested by MickeyG (Spectrum Computing forums)"), so read each issue's body as well as its comments.
  Drop bots and the owner.
  - **Then filter (owner, 2026-11-17):** keep a contributor only if at least one issue they opened (or that was filed for them, as a forum requester) was CLOSED between the previous public release's tag time and now: `gh issue list --state closed --search "closed:>=<date>" --json number,author,closedAt` plus each closed issue's body for named requesters. Commenters, testers, and openers of still-open issues are contributors but earn no mention. An issue counts for the release whose tagged commit contains its fix: issues are closed after the push, so one closed just after the previous tag whose fix is in that tag (`git merge-base --is-ancestor <fix> <prev-tag>`) belongs to the previous release, not this one. If nobody is left, omit the section.

## Style rules (strict)

- **One line per entry, short: well under 20 words.** Name the feature, not its mechanism or every flag (at most the one or two flags a user would type). Before adding a line, merge into an existing line of the same area. A coalesced line may run longer when it has to (owner, 2026-11-25).
- **Coalesce similar items.** If 5 commits all touch DMA timing, that's ONE entry.
- **NO commit IDs.** Don't paste SHAs.
- **NO trivial fixes.** Reformats, doc tweaks, test name changes, internal renames, project-plan updates, CI tweaks — drop entirely.
- **NO ongoing features.** If a feature is half-done or has known bugs, do NOT include it. Per CLAUDE.md: "Don't be overly confident about features."
- **Most recent at top.** Reverse chronological.

## File format

```
# ChangeLog

## Unreleased (YYYY-MM-DD)

...

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

1. Find the last PUBLIC release: the newest tag listed in `releases.yaml` (never `git describe`, which returns private bump tags). Add to, or create, the top `## Unreleased (YYYY-MM-DD)` header. Rename it to `## vX.Y.Z (date)` only when a public release is being cut.
2. Get commit list: `git log --oneline <last-public-tag>..HEAD`.
3. For each commit, ask: "would an end-user / developer-user / triager care about this in a changelog?" If no → drop.
4. Classify the rest into the 4 sections, then build the contributors list.
5. Coalesce similar items into single short lines (Style rules).
6. Write the new entry at the top of `ChangeLog` (above the previous version's entry). Insert above the previous version header; never overwrite it. Before showing the diff, confirm `git diff --numstat ChangeLog` deletes 0 lines (or only lines of the `Unreleased` section you are rewriting).
7. Show the diff to the user. **Do NOT commit until they confirm** — per CLAUDE.md, ChangeLog updates are user-requested (the autonomous-run exception: Hard rules).

## Hard rules

- **Outside an autonomous run, update only when the user asks** (CLAUDE.md: "The file should only be updated when the user requests it"), and commit only after they confirm the diff. **In an autonomous run** each landed issue's line is added to `Unreleased` and committed on `main` right after its bump (owner, 2026-11-18; `autonomous-run` §Authorized). Renaming `Unreleased` to a version stays the owner's.
- **Don't push.** Commit locally only.
- **Don't include "Co-Authored-By"** in the ChangeLog commit message (CLAUDE.md project mandate).
- **Don't claim work that isn't tested.** If a fix has not landed with its gates green, leave it out.
