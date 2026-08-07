# 6.1 Issues and pull requests

## Issues

Pending features and known bugs are tracked as
[GitHub issues](https://github.com/jorgegv/jnext/issues), never in the
repository itself. `TODO.md` is only a pointer to that page, and
`doc/design/EMULATOR-DESIGN-PLAN.md` is a roadmap rather than a bug list, so
neither is the place to look for what is currently broken.

Blank issues are disabled. There are two templates, and both ask for the things
that actually make triage possible.

A **bug report** wants the jnext version, the platform and the package you
installed (rpm, deb, Flatpak, Windows zip, `.dmg`, or built from source), and
your OS version. Two further fields do most of the work: the **emulated
machine** — the `--machine` type you were running — and **whether the same
thing happens on real hardware**. That second one is what separates an emulator
defect from genuine ZX Spectrum Next behaviour, which is a distinction nobody
can make from a screenshot alone; "not tested" is a perfectly good answer to it.
After that, the template asks what happened and what you expected instead, plus
exact steps to reproduce, including the full command line if you were using the
CLI.

A **usage question** is not an issue, and the template configuration points
those at the README, `USAGE.md` and `BUILD.md` instead.

If your report involves a program that misbehaves, attach it. A `.nex`, `.tap`
or `.sna` plus the command line that runs it is worth more than any prose
description, because it can be run. Much of the regression suite is exactly
that shape: a small program, a fixed frame count, and a screenshot.

## Pull requests

`doc/PULL-REQUEST-PROTOCOL.md` is the authoritative gate, and enforcement is
strict: **a pull request that does not meet every applicable rule is not
merged** until it does. What follows is a summary to set expectations; the
protocol document is what a reviewer actually works from.

Every pull request, whatever it does, must satisfy the common requirements:

- **Tests included** that exercise the change.
- **No existing tests modified.** Changing or deleting an existing test needs
  written justification and manual approval from the project owner.
- **Licence-clean fixtures.** Any test fixture must be compatible with GPLv3.
  No proprietary content.
- **Code quality** consistent with the rest of the project — see
  [6.3](03-house-style.md).
- **No new dependencies.** A library, tool or submodule needs manual approval
  from the owner.

Beyond those, a pull request follows one of two flows depending on what it is.

A **bugfix** needs a bug description covering every field of the bug template,
or a link to a bug issue that already does, which then serves as the
justification. Its tests must be **discriminative**: they FAIL on the unfixed
code and PASS once the fix is applied, and the pull request has to
*demonstrate* that — for instance by showing the test result against the
pre-fix code. A test that passes with or without the fix does not qualify,
however thorough it looks, because it cannot tell you whether the fix is the
thing doing the work.

A **feature** needs a functionality description with an **explicit use case**,
on the principle that a feature should solve a problem someone actually has,
and a **design document under `doc/`** following the existing naming and content
conventions, covering the use case, the functionality and the design. Its tests
are discriminative in the same sense: they FAIL without the feature and PASS
with it, demonstrated.

## The review checklist lives in the pull request

Every review records its result **in the pull request description**, as a
checklist of the requirements above. The reviewer adds it on the first review
and updates it on every subsequent one, since an out-of-date checklist is worse
than no checklist at all. Only verified items are ticked, which means the
unticked ones name exactly what is blocking the merge, in the contributor's own
pull request, without anyone having to read back through a review thread. The
template is in the protocol document.

Two conventions about landing, both of them settled decisions rather than
habits:

- **Author agreement is implied by submission.** A pull request *is* a request
  to merge, so once the review is green it is merged immediately and the
  contributor is never asked to confirm.
- **The merge commit is pushed before anything is closed.** GitHub flips a
  still-open pull request to *merged* when the pushed merge reaches the base
  branch. Close it by hand first and it shows "closed" instead — which reads as
  rejected, permanently.

The rest of the landing flow is in [6.2](02-branches-worktrees-and-review.md).
