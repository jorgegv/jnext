# 6.2 Branches, worktrees and review

## One branch per independent change

Never edit `main` directly. Every change gets a dedicated branch off current
`main`, and every *independent* change gets its own — so that two people (or
two agents) working in parallel cannot trash each other's tree.

## Worktrees are the practical mechanism

`git worktree` gives each branch its own checkout directory sharing one
repository, which is what makes "one branch per change" workable in practice:
you can have a feature branch building while a reviewer's branch runs the
regression suite next to it, with no stashing and no switching.

There is one project convention about them, and it is not negotiable:
**worktrees live outside the repository directory**, at
`/home/jorgegv/src/spectrum/jnext-worktrees/<name>`. Not inside it, not even
git-ignored. The reason is mechanical rather than aesthetic — anything that
walks the repository's file list walks the worktrees too. The traceability
generator does a `find` for `CMakeLists.txt` across the whole tree; several
gates read `git status`; an untracked multi-megabyte tree inside the checkout is
a trap for all of them, and the wasted I/O loads the machine that the timing-
sensitive tests are trying to run on.

Two practical notes:

- **Run `make worktree-bootstrap` in a fresh worktree.** `git worktree add`
  does *not* populate submodules, and `roms/` is git-ignored, so a new worktree
  can have an empty `third_party/spdlog` and no SD-card image. The target
  reports what is missing and how to get it. It only reports — it never mutates
  your checkout.
- Prefer `git -C /abs/path <cmd>` over `cd`-ing into a worktree. It keeps the
  working directory stable across commands and avoids shell-state surprises.

## The full triplet, green, on the branch

Before review, on the branch, in this order:

```console
$ make clean && make gui-release
$ make unit-test
$ ./build/test/fuse_z80_test build/test/fuse       # 1356/1356
$ JNEXT_TEST_JOBS=4 make regression
```

No FAIL anywhere, and SKIPs only where they are already declared.

`make clean` first is deliberate. The regression suite runs
`build/gui-release/jnext`, and a stale binary gives both false FAILs (it lacks
a fix the test expects) and — much worse — false PASSes (it lacks the bug the
test would have caught). Rebuilding makes "the tests ran against this source"
true by construction rather than by discipline. ccache makes it cheap; see
[5.2](../05-building/02-build-configurations.md).

**`JNEXT_TEST_JOBS=4` stays on every regression run.** It is not politeness
towards the machine. Parts of the suite are real-time-paced — an audio-underrun
test reports underruns when the box is loaded, and a paused-emulator screenshot
row takes about 55 s against a 60 s timeout — so raising in-suite concurrency
makes the suite intermittently *lie*. That was measured and rejected. If you
want more parallelism, run several branches in several build directories at
once, each capped at 4.

## Independent review

**Never self-review.** The review is done by an agent or a person who did not
write the change, working in their own worktree — never the author's.

The verdict is binary: **APPROVE or REJECT**. There is no
"approve with nits" — a nit either matters, in which case it is a REJECT with
the reason cited, or it does not, in which case it is not a verdict. On REJECT,
fix and re-review.

The reason this rule is absolute is empirical: on this project a series of
defects were caught by review and *none* of them by the green test suite that
had already run over them. A passing suite proves the tests that exist pass; it
proves nothing about the tests that should exist. A reviewer's most useful move
is to revert the fix and confirm the new tests actually fail without it — which
is exactly the discriminative-test requirement from
[6.1](01-issues-and-pull-requests.md), checked rather than believed.

## Merge, then bump

On a green APPROVE, merge — one branch at a time. If a merge conflicts, the
person who merged *last* fixes it, on their own branch.

**Immediately after each merge to `main`, run `make bump-patch`.** Every change
that lands gets its own patch bump; it is per merge, never batched. That is
separate from the deliberate minor/major release flow in
[5.3](../05-building/03-packaging-and-release.md).

Finally: **never push to origin without explicit authorisation.** Local
commits, merges, rebases and bump tags on your own branches are fine and stay
local until the owner says to push. The one standing exception is the merge
commit of a landing pull request, for the GitHub reason in
[6.1](01-issues-and-pull-requests.md).
