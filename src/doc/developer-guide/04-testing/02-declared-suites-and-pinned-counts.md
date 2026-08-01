# 4.2 Declared suites and pinned counts

`test/unit-tests.conf` is the single source of truth for what `make unit-test`
runs. Its own header says it plainly: *this file is a contract, not a
convenience.*

## A line

```
<executable> <expected_rows> [args...]
```

- `<executable>` — the binary under `<build>/test/`. A leading `?` marks an
  **optional** suite: one a legitimate build configuration may not register at
  all. The `debugger_*` suites exist only under `-DENABLE_DEBUGGER=ON` and
  `app_config_test` only under `-DENABLE_QT_UI=ON`, so they are declared `?`.
  An optional suite the build did not register prints a NOTICE and is counted
  as not-run — skipped, but never silently.
- `<expected_rows>` — the exact `Total:` the suite must report. It must be
  `>= 1`; a pin of `0` is rejected outright at parse time, because a suite
  pinned at 0 reporting 0 rows would pass, while the same suite printing no
  summary at all is a hard failure. Zeroing a suite is precisely the silent
  truncation this file exists to forbid.
- `[args...]` — passed through. `@BUILD@` expands to the build directory, which
  is how `fuse_z80_test` and `z80n_test` find their data.
- A `# expect: N` line pins **how many suites the file declares**.

Blank lines and `#` comments are ignored. A duplicate entry is rejected.

## Refuse to run — exit 2, before a single test executes

`test/run-unit-tests.sh` cross-checks the manifest against what CMake actually
registered, read from every `CTestTestfile.cmake` in the build tree (nested
build trees with their own `CMakeCache.txt` are pruned, so a stray
`build/gui-debug` cannot make everything look registered twice). It refuses
when:

- a suite is **declared here but not registered** by CMake — a stale entry;
- a suite is **registered by CMake but not declared here** — it would never
  run;
- a suite is **declared here but not built** — no binary at `<build>/test/<name>`;
- a suite is **declared twice** — a duplicate runs it twice and inflates every
  count;
- one binary is **registered under two `add_test()` names**, which the manifest
  cannot even express;
- the parser read fewer `add_test()` lines than the file contains, so it cannot
  vouch for the list;
- the `# expect: N` pin is absent, or disagrees with the number of declared
  suites.

That last one is the outside witness, and it is the subtle one. Without it,
"N declared == N registered" is a tautology against the edit that matters most:
delete a suite's `add_test()` **and** its manifest row, both sides shrink
together, every count agrees, exit 0, and the suite is gone. A review round did
exactly that with `log_gate_test` and got a clean green run.

## Fail — exit 1, after the run

A suite fails when it:

- reports a row count **other than the pinned one, in either direction**. Fewer
  rows means rows vanished; more rows means the manifest has to be updated
  deliberately;
- prints **no parseable `Total:` line** — including when it exits 0, which
  means it asserted nothing;
- **crashes** or exits non-zero, or any row inside it failed;
- **times out** (300 s per suite by default).

`make unit-test` exits non-zero when any suite fails, and the harness prints an
explicit `UNIT TESTS FAILED` banner so the grand-total line — the one people
copy into status reports — cannot be read as a pass.

## Why the number is edited by hand

**Adding or removing a test row means editing its count in this file.** That
edit is the point: the number is the project's claim about how much it tests,
and it is meant to be made deliberately rather than adopted from whatever a
binary happened to print.

The CMake side, by contrast, is *not* a second hand-kept list — it is read from
the generated `CTestTestfile.cmake`. Only the counts are human.

## The three incidents

All three happened within one day, and all three were found by accident:

- **Task 32** — `cpu_int_pulse_test` and `cpu_z80n_im2_regressions_test` had
  been compiled and `add_test()`'d for months, were absent from the hand-kept
  list that then lived inside the Makefile, and had **never run**. 63 passing
  assertions, uncounted.
- **Task 35** — `make clean` deleted `build/test/rewind_test` and the suite
  printed no row at all. Not a skip: the total went 57 → 56 in silence.
- **Task 37** — a worktree with no `roms/` made `sd_rom_extractor_test` report
  8 rows when it has 26. Eighteen rows ceased to exist and the run still looked
  green.

## Other things the harness pins down

The run is parallel, so the aggregator must survive a failing suite: each
suite's exit code is captured with `|| rc=$?` in its subshell, because
inheriting `set -e` once killed the subshell before the code was recorded and
dropped every suite after the failure. Membership tests are in-shell hash
lookups, never `printf ... | grep -q`, which under `pipefail` can report a
present suite as absent about 4% of the time on a loaded box.

`LC_ALL=C` is exported for the whole run: Qt's `QApplication` constructor calls
`setlocale(LC_ALL, "")`, which localises `strerror()`, and a test's verdict must
depend on the code rather than on who ran it. Finally, the harness clones the
machine-wide SD master into a private per-run directory and points
`JNEXT_TEST_SD_IMAGE` at it, so a concurrent manual `jnext` session writing back
to the card cannot turn `sd_rom_extractor_test` red for reasons that have
nothing to do with the code.
