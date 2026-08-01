# 6. Contributing

Work lands here the same way every time:

1. **An issue.** Bugs and pending features are tracked on GitHub, not in the
   repository. The issue is what a pull request links as its justification.
2. **A branch**, off current `main`, with its own worktree. Never `main`
   itself, and one branch per independent change so that parallel work does
   not collide.
3. **The full test triplet green on that branch** — a clean rebuild, then the
   unit suites, the FUSE Z80 opcode suite, and the screenshot/functional
   regression suite. No FAIL anywhere.
4. **An independent review** by someone who did not write the change. The
   verdict is binary: APPROVE or REJECT.
5. **The merge**, followed immediately by a patch version bump.

The bar for step 3 and step 4 is deliberately high, and it is worth saying so
plainly rather than letting a contributor discover it in review. A pull request
must ship tests that exercise the change, and those tests must be
**discriminative** — demonstrably failing without the change and passing with
it. A test that passes either way does not qualify. Existing tests are not
modified without written justification and the owner's approval. A feature
additionally needs a design document and an explicit use case. A pull request
that does not meet every applicable rule is not merged until it does.

None of that is bureaucracy for its own sake. This is an emulator whose
correctness is judged against the ZX Spectrum Next FPGA's VHDL sources, and
whose test suite is the only thing standing between a plausible change and a
subtly wrong machine. Several of the rules exist because the failure they
prevent has already happened at least once — a green suite that had silently
stopped running three of its own subsystems, a lint nothing invoked, a
documentation gate that proved the wrong thing. The chapters below say which.

- [6.1 Issues and pull requests](01-issues-and-pull-requests.md)
- [6.2 Branches, worktrees and review](02-branches-worktrees-and-review.md)
- [6.3 House style](03-house-style.md)
