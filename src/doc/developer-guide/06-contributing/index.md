# 6. Contributing

Work lands here the same way every time, and the shape is worth having in mind
before you start:

1. **An issue.** Bugs and pending features are tracked on GitHub rather than in
   the repository, and the issue is what a pull request links to as its
   justification.
2. **A branch**, taken off current `main` and given its own worktree. Never
   `main` itself, and one branch per independent change, so that parallel work
   cannot collide.
3. **The full test triplet green on that branch** — a clean rebuild, then the
   unit suites, the FUSE Z80 opcode suite, and the screenshot and functional
   regression suite. No FAIL anywhere.
4. **An independent review**, by someone who did not write the change. The
   verdict is binary: APPROVE or REJECT.
5. **The merge**, followed immediately by a patch version bump.

The bar for steps 3 and 4 is deliberately high, and it is fairer to say so here
than to let a contributor discover it in review. A pull request must ship tests
that exercise the change, and those tests must be **discriminative** —
demonstrably failing without the change and passing with it. A test that passes
either way does not qualify. Existing tests are not modified without written
justification and the owner's approval. A feature additionally needs a design
document and an explicit use case. A pull request that does not meet every
applicable rule is not merged until it does.

None of that is bureaucracy for its own sake. This is an emulator whose
correctness is judged against the ZX Spectrum Next FPGA's VHDL sources, and
whose test suite is the only thing standing between a plausible-looking change
and a subtly wrong machine. Several of the rules exist because the failure they
prevent has already happened at least once here — a green suite that had
silently stopped running three of its own subsystems, a lint that nothing
invoked, a documentation gate that turned out to prove the wrong thing. The
pages below say which rule came from which.

- [6.1 Issues and pull requests](01-issues-and-pull-requests.md)
- [6.2 Branches, worktrees and review](02-branches-worktrees-and-review.md)
- [6.3 House style](03-house-style.md)

## Thank you

A last word, because the pages above are mostly rules and rules read colder
than they are meant.

If you are reading this at all, you are considering giving some of your own time
to a ZX Spectrum Next emulator, and that is genuinely appreciated. The
strictness in this chapter is aimed at the software, never at the person
proposing a change: an emulator that is wrong in a small way is worse than one
that is obviously broken, because the wrongness ends up in someone else's
program and takes weeks to find. The gates exist to catch that, and every one of
them applies just as much to the maintainer's own changes as to yours.

Contributions are not only code, either. A clear bug report — especially one
with the program that misbehaves and the exact command line — is often worth
more than a patch, because it turns a vague symptom into something reproducible.
So are questions that reveal where the documentation is unclear, and notes that
a paragraph of this guide no longer matches what the code does.

Thank you for spending your time on this.
