# 4.2 Declared suites and pinned counts

Most projects discover their test suites: a runner scans a build tree, executes
whatever it finds, and adds up whatever those binaries print. JNEXT does the
opposite. `test/unit-tests.conf` lists every unit suite by name and states, for
each one, the exact number of rows it must report; the harness then proves that
what ran matches what was declared, and refuses to produce a result when it
does not.

The reason is the one that runs through this whole chapter. A discovered test
run can only tell you that everything it found passed, which says nothing about
what it did not find — and the failure mode nobody notices is a suite or a row
quietly disappearing, because that makes the run *greener*, not redder. Pinning
the numbers turns the denominator into a deliberate, reviewable claim: the file
says how much this project tests, a human edits it when that changes, and any
drift between the claim and reality stops the run. The file's own header puts
it more briefly — *this file is a contract, not a convenience.*

## A line

```
<executable> <expected_rows> [args...]
```

- `<executable>` is the binary under `<build>/test/`. A leading `?` marks the
  suite **optional**, meaning a legitimate build configuration may not register
  it at all: the `debugger_*` suites exist only under `-DENABLE_DEBUGGER=ON`
  and `app_config_test` only under `-DENABLE_QT_UI=ON`, so all of them are
  declared with `?`. When an optional suite is absent, the harness prints a
  NOTICE and counts it as not-run — skipped, but never silently.
- `<expected_rows>` is the exact `Total:` the suite must report, and it must be
  at least 1. A pin of `0` is rejected outright at parse time, because a suite
  pinned at 0 that reports 0 rows would pass, whereas the same suite printing
  no summary at all is a hard failure. Zeroing a suite is precisely the silent
  truncation this file exists to forbid.
- `[args...]` are passed through to the binary. `@BUILD@` expands to the build
  directory, which is how `fuse_z80_test` and `z80n_test` locate their data.
- A `# expect: N` line pins **how many suites the file declares**.

Blank lines and `#` comments are ignored, and a duplicate entry is rejected.

## Refuse to run — exit 2, before a single test executes

Before anything is executed, `test/run-unit-tests.sh` cross-checks the manifest
against what CMake actually registered. It reads that from every
`CTestTestfile.cmake` in the build tree, pruning nested build trees that have
their own `CMakeCache.txt` so that a stray `build/gui-debug` cannot make
everything look as though it were registered twice. It refuses when:

- a suite is **declared here but not registered** by CMake, which means the
  entry is stale;
- a suite is **registered by CMake but not declared here**, in which case it
  would never run;
- a suite is **declared here but not built** — there is no binary at
  `<build>/test/<name>`;
- a suite is **declared twice**, since a duplicate runs it twice and inflates
  every count that follows;
- one binary is **registered under two `add_test()` names**, a shape the
  manifest cannot even express;
- the parser read fewer `add_test()` lines than the file contains, so it cannot
  vouch for the list it just built;
- the `# expect: N` pin is missing, or disagrees with the number of declared
  suites.

That last condition is the subtle one, and it is what makes the whole check
more than bookkeeping. Without it, "N declared equals N registered" is a
tautology against exactly the edit that matters most: delete a suite's
`add_test()` **and** its manifest row, and both sides shrink together, every
count agrees, the run exits 0, and the suite is simply gone. A review round did
precisely that with `log_gate_test` and got a clean green run for it. The
`# expect:` pin is the witness from outside that arithmetic.

## Fail — exit 1, after the run

Once the suites have run, one fails when it:

- reports a row count **other than the pinned one, in either direction** —
  fewer rows means rows vanished, and more rows means the manifest needs a
  deliberate update;
- prints **no parseable `Total:` line**, which is a failure even when the
  binary exits 0, because a suite that prints no summary asserted nothing;
- **crashes**, exits non-zero, or contains any row that failed;
- **times out**, which by default means 300 s for a single suite.

`make unit-test` exits non-zero whenever a suite fails, and the harness prints
an explicit `UNIT TESTS FAILED` banner so that the grand-total line — the one
people copy into status reports — cannot be mistaken for a pass.

## Why the number is edited by hand

**Adding or removing a test row means editing that suite's count in this
file.** The manual edit is the point rather than an inconvenience: the number
is the project's claim about how much it tests, and a claim should be made
deliberately, not adopted from whatever a binary happened to print this
afternoon.

The CMake side is different, and deliberately so. It is *not* a second
hand-kept list — it is read from the generated `CTestTestfile.cmake`. Only the
counts are human.

## The three incidents

All three of these happened within a single day, and all three were found by
accident rather than by any check:

- **Task 32** — `cpu_int_pulse_test` and `cpu_z80n_im2_regressions_test` had
  been compiled and registered with `add_test()` all along, but were absent
  from the hand-kept list that then lived inside the Makefile, so they had
  **never run**. That was 63 passing assertions nobody was counting.
- **Task 35** — `make clean` deleted `build/test/rewind_test`, and the suite
  then printed no row at all. It was not reported as a skip; the total simply
  went from 57 to 56 in silence.
- **Task 37** — a worktree with no `roms/` made `sd_rom_extractor_test` report
  8 rows when it has 26. Eighteen rows ceased to exist and the run still looked
  green.

## Other things the harness pins down

The run is parallel, so the aggregator has to survive a failing suite. Each
suite's exit code is captured with `|| rc=$?` inside its subshell, because
inheriting `set -e` once killed that subshell before the code was recorded,
which dropped every suite after the failure from the run. Membership tests are
in-shell hash lookups rather than `printf ... | grep -q`, because under
`pipefail` that pipeline can report a suite that is present as absent about 4%
of the time on a loaded machine.

`LC_ALL=C` is exported for the whole run. Qt's `QApplication` constructor calls
`setlocale(LC_ALL, "")`, which localises `strerror()`, and a test's verdict has
to depend on the code rather than on who ran it. Finally, the harness clones the
machine-wide SD master into a private per-run directory and points
`JNEXT_TEST_SD_IMAGE` at the copy, so that a concurrent manual `jnext` session
writing back to the card cannot turn `sd_rom_extractor_test` red for reasons
that have nothing to do with the code under test.
