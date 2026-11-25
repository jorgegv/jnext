# 4.1 The test triplet

Every batch of changes on `main` has to clear three layers of testing before
it is pushed. The project calls them the triplet, and running them takes four
commands:

```console
$ make clean && make gui-release
$ make unit-test
$ ./build/test/fuse_z80_test build/test/fuse
$ make regression
```

The triplet is the core of the **batch gate**, which adds `make unit-test-sdl`,
`make unit-test-win`, `make regression-win` and the four WebAssembly targets,
`make wasm-core`, `make wasm-browser-test`, `make unit-test-wasm` and
`make regression-wasm` ([5.6](../05-building/06-the-browser-build.md)).

- **When it runs:** once, after all of a work session's landings and before any
  push, at a sync point: the end of a session, a public release or an urgent
  user-facing fix. The landing count never triggers it.
- **Where it runs:** on GitHub, as a CI run on the commit to be pushed. The
  maintainer puts that commit on `origin` as a temporary `gate-v<version>`
  branch and runs `bash test/ci-gate.sh dispatch gate-v<version> <sha>`, then
  `wait <run-id>`, then `make ci-gate-check RUN=<run-id> SHA=<sha>`; the sha is mandatory, so a run on any other commit is refused. The verdict is
  `CLEAN` only if the run and every job succeeded (a skipped or cancelled job is
  not clean) and no job of the last green `main` run is missing. The commit
  that was gated, not the local tip, is what is pushed to `main`. CI saves its
  caches only from a push to `main`, so a gate run reads main's warm ccache and
  evicts nothing. When GitHub cannot run it, the list above is run locally or on
  the remote-run host as a fallback.
- **On a branch, nobody runs it.** The author runs the suites and regression
  rows the change touches, plus `make unit-test` before each review. The
  reviewer runs the targeted rows again.
- **When it is red:** the failure is traced to the landing that caused it, which
  is fixed forward or reverted, and the gate runs again. Nothing is pushed red.

There must be no FAIL and no SKIP in any layer. A SKIP is a row that was not
tested, so every harness exits non-zero on one, in every mode (owner decision,
2026-10-06); the SKIP badge stays only to say what to install or fix.

## Rebuild first — always

The build at the top is not a courtesy, and it is not left to your good habits
either. `make regression` declares `unit-test-build`, `gui-release` and
`sdl-release` as real prerequisites, because the suite runs those exact
binaries. Most rows run `build/gui-release/jnext`; the four rows that exercise
the SDL-only frontend need `build/sdl-release/jnext` instead, because `SdlApp`
is instantiated only in a build configured with `ENABLE_QT_UI=OFF`; and the
`rewind-func` row runs `build/test/rewind_test`, which `make clean` deletes.
Building all of them from the target is what makes "the tests ran against this
source" true by construction rather than by discipline. (`make regression-rows`
declares `gui-release-non-pgo` instead of `gui-release`, so a branch runs no PGO
build; `ROWS_PGO=1` switches it back.)

The rule was written after a 14-hour-old binary produced two bogus FAILs
immediately before a version bump. Staleness cuts the other way too, and that
direction is worse: an old binary can equally fail to *contain* the bug a test
would have caught, and then it reports a pass it never earned.

In a fresh agent worktree, run `make worktree-bootstrap` before anything else.
`roms/*` is git-ignored, so a new checkout has no SD-card image and cannot run
the suites at all.

## Layer 1 — the unit suites

`make unit-test` does not go looking for test binaries. It runs exactly the
suites declared in `test/unit-tests.conf`, in parallel (by default one suite
per CPU), each one bounded by a 300 s timeout that measures the suite, not the
host's queue. That manifest currently declares **90 suites totalling 6610
rows**: the suite count is pinned in the file's own `# expect: 90` line, and the
row total is the sum of the per-suite counts in that same file. The committed
dashboard, `test/SUBSYSTEM-TESTS-STATUS.md`, reports the same 6610.
[4.2](02-declared-suites-and-pinned-counts.md) explains what those pins are for.

These suites link the emulator libraries directly and drive the classes through
their public API, which makes them fast and hermetic — `sd_rom_extractor_test`
is the only one that reads the SD image. This is also where VHDL compliance is
asserted, row by row. What they cannot catch is anything that only exists once
the whole machine is assembled: they render no frame, open no window, and drive
neither frontend.

`make unit-test` also pulls in the cheap structural gates ahead of the build —
the tautological-assertion lint, the Makefile help lint, the traceability
accounting, self-test and duplicate-ID checks, the documentation checks, and
the hermetic half of the packaging contract tests. They live there rather than
only in CI for a simple reason: a gate the inner loop never reaches is not a
gate.

## Layer 2 — the FUSE Z80 opcode suite

```console
$ ./build/test/fuse_z80_test build/test/fuse
```

All 1356 rows pass. This is the FUSE project's data-driven opcode corpus, which
covers every instruction, including undocumented behaviour and per-instruction
cycle counts. It is the only thing that proves the CPU core itself is right —
and it proves nothing else. The Z80N extensions are covered by `z80n_test` (85
rows), and everything Next-specific lives elsewhere.

Note that `fuse_z80_test` is *also* a declared row of `test/unit-tests.conf`,
pinned at 1356 with its data directory passed as an argument, so `make
unit-test` has already run it. The standalone invocation above runs the same
binary against the same data; it is a convenience for reading that one number,
not a third independent execution.

## Layer 3 — the screenshot and functional regression

```console
$ make regression
```

This layer launches the real `jnext` binary headless and looks at what comes
out of it. It reports **310 rows**, made up of 8 preflight lints, 1 SD-image
provisioning row, 68 screenshot rows and 233 functional rows. The last two
numbers come from the `# expect: 68` and `# expect: 233` pins in
`regression_tests.conf` and `functional_tests.conf`, and the arithmetic that
ties them to the total is the harness's own end-of-run accounting assertion.

A screenshot row compares a whole frame, pixel for pixel, against a committed
reference PNG. That is exactly as strong and as blunt as it sounds: it catches
any rendering change anywhere in the pipeline, and when it fails it tells you
the frame changed without telling you why. Functional rows cover the things a
linked library cannot show at all — recording an MP4, an RZX round trip, audio
underruns through a real device, the CLI's exit contracts, keypress delivery in
both frontends.

The rows run in parallel — every CPU for the screenshots and the untagged
functional rows, then the rows tagged `quiet`, which are bounded by real-time
pacing (`audio-underrun-func`, `screenshot-paused-func` and a few more), a lane
at a time on a host the first phase no longer loads (see
[The regression suite](03-the-regression-suite.md)). `JNEXT_TEST_JOBS` caps the
lanes; it is no longer needed for correctness, and CI passes none. Rows that
merely spawn short-lived processes have failed under outside load too, which is
why the suite itself flags a FAIL that happened on a loaded host.

A green `make regression` on a committed tree leaves a stamp keyed on the
tree's non-doc content, and `make regression-stamp-check` finds it (the CI gate
needs no stamp: `make regression-ci-check` finds its run). A fallback gate whose
tree changed only in documentation since the last green run need not repeat it. Reviewers run targeted rows, never a full run.

## The branch round: `make test-affected`

On a branch the author does not run the whole of `make unit-test` every round. `make test-affected`
asks `test/test-affected.pl` which suites the branch's changed files reach, and runs those, a fixed
smoke set, `docs-check`, `cli-check` and one boot screenshot row (`boot-48k`). It is not a gate and
says so; the full `make unit-test` runs once, just before landing, and the batch gate on `main` is
unchanged.

The map is read from the build tree, not written by hand. Each object has a compiler depfile (the
source and every header it read); each test executable is linked with a GNU ld map
(`-Wl,-Map`, set in `test/CMakeLists.txt`), whose "Archive member included" section names the exact
archive members the link pulled; an archive's own `link.txt` says which object is which member. A
suite's files are the union of the depfiles of its own objects and of the members it pulled, so a
change to a header or a source selects exactly the suites that read it. A suite with no map is taken
with its archives' whole membership: more suites, never fewer.

A depfile cannot see what a suite reads as data at run time, so a second kind of edge is read from
the same tree: the paths CMake bakes into a suite's compile definitions (`-DJNEXT_MAIN_SRC="..."`,
fixture directories) in its `flags.make`. A changed file equal to such a path, or under it when it
is a directory, selects the suite, whatever the file's class (a source, a document, a fixture, a
regression reference); a document a suite reads therefore leaves docs-only mode. A suite given the
whole source root (`JNEXT_SRC_DIR`) is selected for any changed file whose root-relative path its
own sources name literally, and every suite is selected for a changed file that no object compiles
and whose path its sources name. Comments do not count as a mention.

What neither can see (documents, regression row scripts and references, the harness, packaging, CMake
files, submodule pointers) is classified in `test/test-affected.conf`, one glob and one or more actions
per line, first match wins. A changed file that is in no depfile, that no suite reads and that matches
no glob is UNMAPPED, and an unmapped file makes the plan FULL (`make unit-test`), named: a new kind of
file is never skipped silently. A changed submodule pointer is FULL too. `make test-affected-check`,
part of `make unit-test`, fails while a tracked file is neither in a depfile nor classified, so the
conf grows in the change that adds the file.

The two files a branch touches to add or count tests are read by what changed in them, from the
branch's diff against its base, and not classified wholesale. In `test/unit-tests.conf` the suites
whose lines were added, removed, re-pinned or re-marked (`?`) are planned; a changed `# gate:`,
`# os:` or `# expect:` line plans no suite of its own, because every `--only` run makes the
manifest checks (registry both ways, gate, OS, the total) on the whole manifest and they refuse
a wrong directive or total; a line of no known shape, or no git diff to read, is FULL. In
`test/CMakeLists.txt` the file is parsed into an ordered list of statements, each with its enclosing
`if()`/`else()`/`foreach()` chain, comments stripped with quotes respected. CMake is order-dependent
(directory options reach later targets only, a `file(GLOB)` precedes its `foreach`), so the list is
diffed as a sequence: every statement added, removed or moved is a changed statement. A changed
target-level statement (`add_executable`, `target_*`, `add_test`, `set_target_properties`,
`set_tests_properties`, `add_dependencies`) selects the declared suites it names; any other changed
statement, a shared statement moved, or a suite's statement moved past a shared one, is FULL. So is a
file the scanner cannot read with confidence (bracket arguments or comments, an unterminated
statement or quote, unbalanced `end*()`). A child the planner reads that fails (`diff` with a status other than 0 or 1, or missing; a `git` call that reads the change) also makes the plan FULL, never an empty change list. A change to either file, to `test/refresh-traceability-matrix.pl`
or to the matrix also plans `make traceability-check-subset` and `make traceability-selftest`:
the first generates the matrix with `--no-fails` (every suite taken as passing, since a subset
run keeps no logs) and fails if the committed copy differs, so a wrong or missing row, summary
count or stale citation still fails on the branch.

The plan also prints which of `unit-test-sdl`, `unit-test-win` and `unit-test-wasm` the change
needs, from the changed files' paths and content, lines the change removed included (`ENABLE_QT_UI`,
`_WIN32`, `std::filesystem`, sockets, `__EMSCRIPTEN__`, CMake and harness files, the sources only the
browser build compiles), with the reason. It prints and does not run them; they are heavy and the
author runs the ones it names.

`test/run-unit-tests.sh --only a,b,c` is the subset runner. Every manifest check still applies to the
whole manifest, a name the manifest does not declare is a refusal, each selected suite's own count is
still pinned, and the summary says `subset run: K of N suites -- not a gate`. A suite that fails in
the full run and that the planner would not have selected for the branch's diff against the merge base
with `main` (`origin/main` when there is no local `main`; the remote-run host's run tree is given one) prints
`TEST-AFFECTED MISS`, so the map or the conf is fixed on the same branch; with no such base, or in a
platform tree (SDL-only, Windows, WebAssembly), the check is silent. A docs-only plan ran no suite, so
every suite that fails is a miss. The planner's rows are
`TAF-01..10` and `TAF-12..79` in `test/test-affected-selftest.sh` (`make test-affected-selftest`,
part of `make unit-test`), on a hand-written fixture build tree, and `HS-194a..m` in the harness
self-test.

A FULL plan runs `make unit-test` and then every make target the plan names. `make lint-test-scripts` (the
regression preflight's four test-script lints: traps, timeouts, quiet pipe grep and bare in-place `sed`,
which BSD sed on macOS rejects) is a prerequisite of both `make unit-test` and `make test-affected`, so a
branch round lints every tracked test script and a bad one no longer waits for the batch gate's
regression run.

What `make test-affected` does not do: it runs the traceability check only when the manifest, the test
CMake file, the generator or the matrix changed, so a new `check()` row alone leaves the committed
matrix stale until the full `make unit-test`, which also takes each suite's real FAIL set from its
log; and it does not run the
platform suites or the regression rows other than the boot smoke row and the rows the diff names.

## What the triplet does not prove

It does not prove that the emulator matches real hardware anywhere no test row
asserts that it does; making those gaps visible is the job of the traceability
matrix ([4.4](04-traceability.md)). And it does not prove that the prose in the
man page or in the guides is true — only that the generated outputs match their
sources. [4.5](05-documentation-and-cli-gates.md) sets out exactly where that
line sits.
