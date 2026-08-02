# 4.6 The harness self-test

Testing the test harness sounds like one turn of the screw too many, until you
notice where every number this project quotes comes from. Each one has passed
through a harness on its way to you. If that harness under-reports, everything
downstream of it is wrong in a way that looks identical to being right — the
run is green, the totals are plausible, and nothing anywhere says otherwise. So
the harnesses are themselves under test.

```console
$ make harness-selftest
```

The regression suite also runs it as the `harness-selftest-func` row, so it is
exercised on every full run rather than only when someone remembers to ask.

## Why it exists

`test/run-unit-tests.sh` once shipped with a bug that appeared **only when a
suite failed**, which is the one path nobody exercises while everything is
green. It had been verified, thoroughly, against suites that pass. What
happened when one did not was that `set -e` killed the runner subshell before
it could record the suite's exit code; the aggregator then aborted; and every
suite after the failing one was dropped from the run. A harness that
under-reports precisely when something is wrong is worse than no harness at
all.

## How it works

Each fault is **injected** against stub suites in a throwaway build directory,
and the refusal is then asserted — both the expected exit status and the exact
diagnostic text. The principle is the same one that runs through the chapter: a
guard that cannot be shown to fire is not a guard.

The stubs are three lines of bash that print a `Total:` line and exit with a
chosen code. A `register` helper writes a matching `CTestTestfile.cmake`, and a
`manifest` helper writes a manifest with a matching — or deliberately wrong —
`# expect:` pin. From those few primitives the self-test drives, among others,
a clean two-suite run; a suite that fails while a later suite must still be
reported; a suite that prints no summary; a suite that hangs into its timeout;
an entry with no row count; a pin of 0; one binary registered under two
`add_test()` names; a wrong suite-count pin; a manifest with no pin at all; a
nested build tree that must **not** be enumerated; an unparseable `add_test()`
line; and a membership probe that proves the `printf | grep -q` SIGPIPE race
cannot come back.

It also reaches beyond the unit harness. Rows `HS-21`..`HS-24`, `HS-31` and
`HS-32` drive `regression.sh --preflight-only` against truncated manifests, a
declared functional test with no script, and a stray undeclared script — which
is why `make harness-selftest` declares `unit-test-build`, `gui-release` and
`sdl-release` as prerequisites, since those are the same binaries preflight
resolves. Other rows assert that the lints are still *wired in* at all: that
`make unit-test` reaches the assertion lint and reaches it first, and that an
offending row script fails the whole regression preflight rather than just the
lint. The rest cover cleanup, checking that every SD-clone cleanup script
handles INT and TERM as well as EXIT, that each handler **exits** rather than
resuming, and that the three real cleanup bodies are bounded and remove only
their own run directory.

## It pins its own count

`EXPECTED_TOTAL = 45` sits in the script, right next to the rows it counts, and
running a different number of checks is exit 2 with an explicit refusal
message. The reasoning is the project's usual one: without the pin, deleting a
check shrinks the declared side and the reported side in lockstep, which is
precisely the silent-truncation move that the harnesses this file guards were
built to forbid. Adding or removing a check means editing that number,
deliberately.

The count could not simply live in `test/unit-tests.conf`. That manifest is
cross-checked against CMake's `add_test()` registrations in both directions, so
an entry with no CMake counterpart would make the unit harness refuse to run.

## The traceability self-tests

```console
$ make traceability-selftest          # the citation extractor, 202 pinned rows
$ make traceability-accounting-check  # the suite-accounting gate, ~0.01 s
```

`test/traceability-citations-selftest.pl` pins the tool that decides which VHDL
lines justify each traceability row. Its end-to-end rows build a throwaway
repository out of the real manifest, CMakeLists and matrix, populated with stub
sources and binaries, and then run the real refresh script against it twice, so
that idempotence and the refusal paths are both exercised. It pins
`$EXPECTED_ROWS = 202` in the same shape as the harness self-test, and for the
same reason it cannot live in the unit manifest — it is a perl script with no
CMake target.

Both are prerequisites of `make unit-test`, and that matters more than the
checks themselves do. Until they were wired in, nothing invoked them — not
`unit-test`, not `regression`, not CI — and they were the only thing standing
between the project and a plausible-but-wrong VHDL citation. A guard nobody
runs is worse than no guard, because it looks like coverage. The same lesson
produced `regression-doc-check`, which wired up a screenshot-documentation
checker that had existed, had been correct, and had been invoked by nothing for
long enough to rot into failing on 18 rows.

## The lint self-tests

Two of the lints verify themselves on **every invocation**, before their
verdict on the real tree is trusted at all:

- `test/lint-assertions.sh` writes a fixture containing one instance of each
  banned tautology — `check(id, desc, true)`, `|| true`, and
  `a == b || a != b` — and refuses with exit 2 if any of its patterns fails to
  match that fixture. A lint that has silently become a no-op reports success,
  which is worse than not running it.
- `test/00regression/lint-traps.sh` carries **86 pinned cases: 57 that must
  flag and 29 that must not.** There is one fixture file per case,
  deliberately, because a combined fixture still passed when a case was lost,
  whereas with one file per case the lost case is named. It also cross-checks
  its own prose case table against the fixture files that exist, in both
  directions, so that the documentation and the fixtures cannot drift apart.

Both directions matter, and not equally. A false negative merely fails to help,
whereas a false positive blocks a correct row — and that is the one that costs.
