# Pull Request Review & Merge Protocol

Authoritative rules for accepting pull requests into JNEXT, including
**external PRs from third-party contributors**. Enforcement is **strict**: a PR
that does not meet every applicable rule is **not merged** until it does.

Two flows: **Bugfix** and **Feature**. Every PR must satisfy the *Common*
requirements plus the ones for its type.

## Common requirements (every PR)

1. **Tests included.** Functional/unit tests that exercise the change ship in
   the PR.
2. **No existing tests modified.** Changing or deleting an existing test
   requires an explicit written justification and manual approval from the
   JNEXT owner.
3. **License-clean fixtures.** Any test fixture is license-compatible with the
   project (GPLv3). No proprietary content.
4. **Code quality.** Style and structure align with the rest of the project.
5. **No new dependencies.** Adding a library, tool, or submodule requires
   manual approval from the JNEXT owner.

## Bugfix PR

- **Bug description** covering every field of the bug issue template
  (version, platform/package, OS, emulated machine, real-hardware result, what
  happened, steps to reproduce). If a bug issue exists, link it as the
  justification instead.
- Tests are **discriminative**: they must **FAIL on the unfixed code and PASS
  with the fix applied**, and the PR must demonstrate this (e.g. the test result
  against the pre-fix code). A test that passes with or without the fix does not
  qualify.

## Feature PR

- **Functionality description with an explicit use case.** A feature must solve
  a problem someone has. If a feature issue exists, link it as the
  justification.
- **Design document under `doc/`**, following the existing naming and content
  conventions, covering the use case, the functionality, and the feature
  design.
- Tests are **discriminative**: they must **FAIL without the feature and PASS
  with it**, and the PR must demonstrate this. A test that passes with or
  without the feature does not qualify.

## Requirement checklist on the PR

Every review records its result **in the PR description**, as a checklist of the
requirements above. This is what tells the contributor exactly what is still
missing, in their own PR, without reading a review thread.

- The reviewer **adds** the section on first review and **updates** it on every
  subsequent review. It is never left stale — an out-of-date checklist is worse
  than none.
- Tick only what is verified. Unticked items name what blocks the merge.
- If the PR description cannot be edited, post the same checklist as a review
  comment and keep updating that comment.

Template (drop the flow that does not apply):

```markdown
## Review checklist (maintainer-maintained — updated at each review)

**Common**
- [ ] Tests included that exercise the change
- [ ] No existing tests modified (or: justified + owner-approved)
- [ ] Fixtures license-clean (GPLv3-compatible, nothing proprietary)
- [ ] Code quality / style consistent with the project
- [ ] No new dependencies (or: owner-approved)

**Bugfix PR**
- [ ] Bug description complete (all bug-template fields), or a linked bug issue
- [ ] Tests are discriminative: FAIL before the fix, PASS after — demonstrated

**Feature PR**
- [ ] Functionality description with an explicit use case, or a linked issue
- [ ] Design document under `doc/`, following existing conventions
- [ ] Tests are discriminative: FAIL without the feature, PASS with it — demonstrated

_Reviewed at commit `<sha>` on `<date>`. Verdict: **APPROVE / REJECT**._
```

## Merge gate

- A maintainer reviews the PR against this document.
- Non-compliance is a **REJECT** with the failing rule cited; compliance is
  required before merge.
- The checklist above is added or refreshed as part of every review.
- **Author agreement is implied by submission** (owner decision, 2026-07-24):
  a PR *is* a request to merge. Once the maintainer review is green (APPROVE,
  including any owner sign-offs this document requires), the PR is merged
  immediately — the contributor is never asked to confirm the merge.
- **Push the merge commit before closing anything** (owner decision,
  2026-07-26): after the local merge + bump, push `main` to origin FIRST, then
  close the linked issue. Never close the PR manually — GitHub flips a still-open
  PR to **merged** when the pushed merge reaches the base branch; a hand-closed
  PR shows "closed" (rejected-looking) forever. This is the standing
  authorization to push the merge commit of a PR landing; tags still follow the
  usual batching rules and may follow later.
- The rest of the landing flow (branch, green test triplet, independent review,
  bump) follows [../CLAUDE.md](../CLAUDE.md) → "Merging a completed feature/fix
  to `main`".
