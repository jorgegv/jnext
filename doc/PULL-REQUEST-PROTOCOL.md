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

## Merge gate

- A maintainer reviews the PR against this document.
- Non-compliance is a **REJECT** with the failing rule cited; compliance is
  required before merge.
- The rest of the landing flow (branch, green test triplet, independent review,
  bump) follows [../CLAUDE.md](../CLAUDE.md) → "Merging a completed feature/fix
  to `main`".
