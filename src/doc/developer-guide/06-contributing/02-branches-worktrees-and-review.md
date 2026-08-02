# 6.2 Branches, worktrees and review

## One branch per independent change

Never edit `main` directly. Every change gets a dedicated branch off current
`main`, and every *independent* change gets its own, so that two people — or two
agents — working in parallel cannot trash each other's tree.

## Worktrees are the practical mechanism

Not everyone uses `git worktree`, so it is worth a sentence on what it is before
the rules about it. A worktree is a second (or third, or tenth) checkout
directory belonging to the *same* repository: each one has its own working files
and its own checked-out branch, while sharing a single object store and history.
Where `git checkout` swaps one directory between branches, worktrees give each
branch a directory of its own.

That is what makes "one branch per independent change" workable in practice
rather than merely aspirational. You can have a feature branch compiling in one
directory while a reviewer's branch runs the regression suite in another, with
no stashing and no switching, and without either of them seeing the other's
half-finished edits.

There is one project convention about where they go, and it is not negotiable:
**worktrees live outside the repository directory** — not inside it, not even
git-ignored. The convention is a sibling directory next to the repository
checkout, holding one subdirectory per worktree; if your clone is at
`<somewhere>/jnext`, the worktrees go in `<somewhere>/jnext-worktrees/<name>`.

The reason is mechanical rather than aesthetic. Anything that walks the
repository's file list walks the worktrees too, and several things here do: the
traceability generator runs a `find` for `CMakeLists.txt` across the whole tree,
and a number of gates read `git status`. An untracked multi-megabyte tree sitting
inside the checkout is a trap for all of them, and the wasted I/O loads the very
machine that the timing-sensitive tests are trying to run on.

Two practical notes on working with them:

- **Run `make worktree-bootstrap` in a fresh worktree.** `git worktree add` does
  *not* populate submodules, and `roms/` is git-ignored, so a brand-new worktree
  can have an empty `third_party/spdlog` and no SD-card image — and will fail in
  confusing ways as a result. The target reports what is missing and how to get
  it. It only reports; it never mutates your checkout.
- Prefer `git -C <path> <cmd>` over `cd`-ing into a worktree. It keeps your
  working directory stable across commands and avoids the shell-state surprises
  that come from half-remembering which directory you are in.

## The full triplet, green, on the branch

Before review, on the branch, in this order:

```console
$ make clean && make gui-release
$ make unit-test
$ ./build/test/fuse_z80_test build/test/fuse       # 1356/1356
$ JNEXT_TEST_JOBS=4 make regression
```

No FAIL anywhere, and SKIPs only where they are already declared.

Starting with `make clean` is deliberate, not caution. The regression suite runs
`build/gui-release/jnext`, and a stale binary produces both false FAILs — it
lacks a fix the test expects — and, much worse, false PASSes, because it also
lacks the bug the test would have caught. Rebuilding first makes "the tests ran
against this source" true by construction rather than by discipline, and ccache
makes it cheap enough that there is no reason to skip it; see
[5.2](../05-building/02-build-configurations.md).

**`JNEXT_TEST_JOBS=4` stays on every regression run.** It is not politeness
towards the machine. Parts of the suite are real-time-paced: an audio-underrun
test genuinely reports underruns when the box is loaded, and a paused-emulator
screenshot row takes about 55 s against a 60 s timeout. Raising in-suite
concurrency therefore does not make the suite faster so much as make it
intermittently *lie*, which was measured and rejected. If you want more
parallelism, run several branches in several build directories at once, each one
still capped at 4.

## Independent review

**Never self-review.** The review is done by an agent or a person who did not
write the change, working in their own worktree — never the author's.

The verdict is binary: **APPROVE or REJECT**. There is no "approve with nits",
because a nit either matters, in which case it is a REJECT with the reason
cited, or it does not, in which case it is not part of a verdict. On REJECT, fix
and re-review.

The reason this rule is absolute is empirical rather than philosophical. On this
project a series of real defects were caught by review and *none* of them by the
green test suite that had already run over the same code. That is not a
criticism of the suite: a passing suite proves that the tests which exist pass,
and it can say nothing at all about the tests that should exist. A reviewer's
single most useful move is to revert the fix and confirm that the new tests
actually fail without it — which is the discriminative-test requirement from
[6.1](01-issues-and-pull-requests.md), checked rather than believed.

## Merge, then bump

On a green APPROVE, merge — one branch at a time. If a merge conflicts, the
person who merged *last* is the one who fixes it, on their own branch.

**Immediately after each merge to `main`, run `make bump-patch`.** Every change
that lands gets its own patch bump, per merge and never batched. That is a
separate thing from the deliberate minor/major release flow described in
[5.3](../05-building/03-packaging-and-release.md).

Finally: **never push to origin without explicit authorisation.** Local commits,
merges, rebases and bump tags on your own branches are all fine, and they stay
local until the owner says to push. The one standing exception is the merge
commit of a landing pull request, for the GitHub reason given in
[6.1](01-issues-and-pull-requests.md).
