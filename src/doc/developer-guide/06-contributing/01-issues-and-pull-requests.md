# 6.1 Issues and pull requests

## Issues

Pending features and known bugs are tracked as
[GitHub issues](https://github.com/jorgegv/jnext/issues), never in the
repository. `TODO.md` is only a pointer to that page, and
`doc/design/EMULATOR-DESIGN-PLAN.md` is a roadmap rather than a bug list.

Blank issues are disabled; there are two templates, and both ask for the
things that actually make triage possible.

A **bug report** wants the jnext version, the platform and package you
installed (rpm, deb, Flatpak, Windows zip, `.dmg`, or built from source), the
OS version, and — the fields that do the most work — the **emulated machine**
(`--machine` type) and **whether it also happens on real hardware**. That last
one is what tells an emulator defect apart from genuine Next behaviour, and a
"not tested" is a perfectly good answer. Then: what happened and what you
expected, and exact steps to reproduce, including the full command line if you
used the CLI.

A **usage question** is not an issue; the template configuration points those
at the README, `USAGE.md` and `BUILD.md` instead.

If your report includes a program that misbehaves, a `.nex`, `.tap` or `.sna`
plus the command line that runs it is worth more than any description. Much of
the regression suite is exactly that: a small program, a fixed frame count, and
a screenshot.

## Pull requests

`doc/PULL-REQUEST-PROTOCOL.md` is the authoritative gate, and enforcement is
strict: **a pull request that does not meet every applicable rule is not
merged** until it does. What follows is a summary; the protocol document is
what a reviewer works from.

Every pull request must satisfy the common requirements:

- **Tests included** that exercise the change.
- **No existing tests modified.** Changing or deleting an existing test needs
  written justification and manual approval from the project owner.
- **Licence-clean fixtures.** Any test fixture must be compatible with GPLv3.
  No proprietary content.
- **Code quality** consistent with the rest of the project — see
  [6.3](03-house-style.md).
- **No new dependencies.** A library, tool or submodule needs manual approval
  from the owner.

Then one of two flows.

A **bugfix** needs a bug description covering every field of the bug template —
or a link to a bug issue, which serves as the justification instead. Its tests
must be **discriminative**: they FAIL on the unfixed code and PASS with the fix
applied, and the pull request must *demonstrate* that, for example by showing
the test result against the pre-fix code. A test that passes with or without
the fix does not qualify, however thorough it looks.

A **feature** needs a functionality description with an **explicit use case** —
a feature must solve a problem someone actually has — and a **design document
under `doc/`** following the existing naming and content conventions, covering
the use case, the functionality and the design. Its tests are discriminative in
the same sense: they FAIL without the feature and PASS with it, demonstrated.

## The review checklist lives in the pull request

Every review records its result **in the pull request description**, as a
checklist of the requirements above. The reviewer adds it on the first review
and updates it on every subsequent one — an out-of-date checklist is worse than
none. Only verified items are ticked; the unticked ones name exactly what is
blocking the merge, in the contributor's own pull request, without anyone
having to read a review thread. The template is in the protocol document.

Two conventions about landing, both settled decisions:

- **Author agreement is implied by submission.** A pull request *is* a request
  to merge, so once the review is green it is merged immediately; the
  contributor is never asked to confirm.
- **The merge commit is pushed before anything is closed.** GitHub flips a
  still-open pull request to *merged* when the pushed merge reaches the base
  branch. A hand-closed one shows "closed" — which reads as rejected — forever.

The rest of the landing flow is in [6.2](02-branches-worktrees-and-review.md).
