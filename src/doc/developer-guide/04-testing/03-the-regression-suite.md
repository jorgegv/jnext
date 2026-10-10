# 4.3 The regression suite

The regression suite is where the emulator is tested as a whole program rather
than as a set of linked classes. It launches the real `jnext` binary, headless,
and checks what comes out of it — mostly by comparing rendered frames against
committed reference images, and otherwise by exercising behaviour that only
exists once there is a real process, such as writing a file or opening an audio
device.

`test/00regression/regression.sh` is only the driver. It parses arguments,
validates the manifests, runs the test scripts — the group scripts in its own
shell, each functional row in a process of its own (`row-runner.sh`, scheduled
by `parallel-rows.inc`) — and enforces the end-of-run accounting. The test logic itself lives in
`test/00regression/scripts/`, which holds one script per functional test plus
three group scripts — `00-preflight-lint.sh`, `01-sdcard-provision.sh` and
`screenshots.sh` — with the shared helpers in `test-functions.inc`.

## Two kinds of row, two manifests

**Screenshot rows** are declared in `regression_tests.conf`:

```
test_name  machine_type  nex_file  screenshot_delay_frames  [extra CLI args...]
```

`nex_file` may be the literal `BOOT`, which means "boot the machine and load
nothing". The delay is counted in **emulated frames**, never in wall-clock
seconds, so that a slow CI runner and a fast desktop capture identical machine
state. Each row launches `jnext --headless` with `--delayed-screenshot`, and the
resulting PNG is compared against the committed `img/<name>-reference.png` at a
default tolerance of zero pixels, using an exact any-channel difference mask. If
the two images have different dimensions the row counts as different rather than
being cropped down to the overlap.

One entry in the extra-args field is a harness sentinel rather than a jnext
flag: `@private-sd` is stripped by the launcher, which then gives that row its
own SD-card clone. Any row whose guest **writes** to the card needs it.

**Functional rows** are declared in `functional_tests.conf`, one name per line,
optionally followed by tags (below). Each has its logic in `scripts/<name>.sh`
and calls `begin_func <name>` to register that its row really was reported.

Both files carry a `# expect: N` pin and the driver faults if a pin and the
declared lines disagree.

## The independent witness

The completeness check compares rows reported against rows declared, but for
screenshots both of those sides are read from the same conf file, so on its own
that comparison is a tautology. The committed reference images supply the
witness from outside: **every `img/<name>-reference.png` must have a conf
entry**, which means truncating the manifest cannot silently shrink the suite.
This exists because a review round once deleted a screenshot row *and* its
reference image together, and got a green 59/0/0 — which happened to be the
project's own previous baseline. Nobody would have blinked at it.

The same both-directions rule covers the scripts directory. A declared
functional test with no `scripts/<name>.sh` could never report its row, and a
stray `scripts/*.sh` that nothing declares is a test that has been dropped from
the manifest.

## The accounting assertion

At the end of a full run — that is, one not in `--update` mode — the driver
proves three things: that every declared functional test reported **exactly
one** row, that no undeclared row appeared, and that the grand total equals
`6 lint + 1 sdcard-provision + screenshots + functional`. Any mismatch is
reported as a **harness fault**, exit 2, and is explicitly not a pass.

Build artifacts that rows depend on — `rewind_test` and the SDL-only `jnext` —
are checked in the first second of the run rather than five minutes in. Running
with `--preflight-only` performs every guard and then exits, which is the seam
the harness self-test drives.

## The SD image

No test row passes `--sdcard`. Every row relies on jnext's own default-location
lookup instead, and the suite provisions that image for itself in the
`[sdcard-provision]` row. Two separate mechanisms do two different jobs:

- **A hash gate** protects the machine-wide master under `~/.jnext/sdcard/`
  from whatever happened to it between runs, re-deriving it only when it has
  drifted. This is not a theoretical concern: after an evening of manual
  NextZXOS booting the image had 11012 bytes changed, and a full run reported
  91 pass / 5 FAIL on a branch that was green. The hash witness is written
  when the image is derived, so it cannot notice that jnext now derives a
  different image. The gate therefore also re-derives when the image's
  `.recipe` sidecar, written by jnext, does not hold the
  `sdcard::kFixedImageRecipe` of the tree under test (GH #284).
- **A per-run clone** points `JNEXT_CONFIG_DIR` at a private directory, so the
  run boots its own copy and every non-headless invocation starts from clean
  GUI preferences. That directory lives under `$HOME` rather than `/tmp`,
  because `cp --reflink` cannot cross a filesystem and `--reflink=auto` would
  quietly degrade to a real 1 GB copy into RAM.

## Concurrency: one process per row, in phases

Until GH #295 the functional rows were *sourced* into the driver one after
another — they shared its counters, its `TMP_DIR` and its SD clone, and that
alone made them serial. Most of them wait on real time rather than on the CPU,
so a full run spent twelve minutes on a host that was 85% idle.

Now each row runs in a process of its own. `row-runner.sh` initializes the
suite library in *row-process mode* — with the row's own `TMP_DIR`
(`$TMP_DIR/rows/<row>`) and `RUN_DIR` (`$RUN_DIR/rows/<row>`, which is also its
`JNEXT_CONFIG_DIR`, so preferences and the warm-start cache are private) handed
over by the driver — and then sources the row, inside the same counter guard
as before. It hands its tally back in a result file; a harness fault inside it
also leaves `<result>.fault`. The driver runs the rows in phases:

| Phase | Rows | Lanes |
|---|---|---|
| screenshots | `regression_tests.conf` | `JNEXT_TEST_JOBS`, default `nproc` |
| parallel | untagged functional rows | `JNEXT_TEST_JOBS`, default `nproc` |
| quiet | rows tagged `quiet` | `JNEXT_TEST_QUIET_JOBS`, default 1 |
| serial tail | rows tagged `serial` | 1 |

and prints each row's buffered output, in declared order within its phase, as
soon as it and every earlier row have finished.

The **tags** carry what used to be an unwritten rule. `quiet` marks a row that
paces against real time — `audio-underrun-func` reports underruns on a loaded
box, `screenshot-paused-func`'s control run takes about 55 s against a 60 s
timeout — and was *measured* to fail in the parallel phase; that is where the
old advice "pass `JNEXT_TEST_JOBS=4`, never raise it" went. `serial` is
`sdcard-isolation-func`, whose "the master was never touched" check must
follow every other row. `private-sd` marks a row that writes to the SD card,
which then boots a copy of its own; every other row boots the run's clone
through a hard link, and the driver fingerprints that clone after provisioning
and again at the end, so an untagged writer is a harness fault rather than a
row that quietly changes what its neighbours boot.

The driver keeps every accounting check and adds the ones a process boundary
needs: a row process that dies without a result, or outlives its 600 s bound,
is a FAIL naming it; a row's directory that already exists when it starts, is
still there when it exits, or is left over at the end, is a harness fault.
`JNEXT_TEST_JOBS` is now only a cap, for leaving CPUs to something else; do not
raise `JNEXT_TEST_QUIET_JOBS` to buy speed. Output is printed in declared
order, so a heartbeat line every 60 s names the rows still running.

Parallel rows exposed one shared-resource race the serial suite never could:
`xvfb-run -a` chooses an X display number by scanning lock files, so two rows
starting together could pick the same one, and the loser SKIPPED (a failure now). Every row now
uses `xvfb-run -d`, where Xvfb picks the display itself, and harness-selftest
HS-72 bans the racy forms.

## One full run at a time, and the stamp

A full run (no arguments, real manifests) takes a host lock through `flock -o`
before it sources anything, so a second one waits, saying who holds it, and no
child of the run can inherit the lock. It then waits, bounded, while the
1-minute load is above `nproc`. `make regression` also asks the run for a
**stamp**: on a green run of a tree with no uncommitted non-doc change,
`test/regression-stamp.sh` records the counts, the loads and the commit under a
key hashed from `git ls-tree` over every non-documentation path.
`make regression-stamp-check` prints the stamp covering the current tree, which
is how a reviewer verifies the author's run without repeating it, and
`make regression-ci-check` finds a green CI run with the same key, which is how
a release reuses CI. Only a run with no FAIL and no SKIP is stamped, and since 2026-10-06 `regression.sh`
itself exits 1 on a SKIP in every mode (full, named rows, `confirm`, `--update`;
a standalone row's `standalone_summary` too), naming the rows. A tool a row needs
is provisioned rather than assumed: `make regression` first runs `make z88dk-gdb`
(`test/provision-z88dk-gdb.sh`), which builds z88dk v2.4's client, sha256-pinned,
into `~/.cache/jnext/tools` when none resolves. A run whose
only FAILs are functional rows that failed on a loaded host is recorded as
pending, and `make regression-confirm` re-runs exactly those rows solo; if they
all pass the run is stamped, and the stamp names them. Targeted rows run through
`make regression-rows ROWS="..."`, which builds the binaries they need.

## A failure on a loaded host

Those two rows are examples, not the list. Rows that merely spawn short-lived
processes have also failed under a parallel-agent load and never reproduced
solo (`subsystem-gain-func`, GH #245), and a concurrent duplicate build once
failed with `undefined reference to main`. A list of sensitive rows in a
document is therefore the wrong tool: a reader either chases a ghost, or —
worse — waves a genuine failure through as "probably contention" because the
row was not on the list.

So the harness says it itself. It prints the 1-minute load average at the start
and at the end of the run, marks every FAIL that happened with the load at or
above `nproc` on the spot, and after the results lists each failed row by name
with the load at the moment it failed, plus a warning when the run was loaded.
None of that changes a verdict: a FAIL is still a FAIL and the exit status is
unchanged. What it changes is the next step — re-run the row SOLO
(`make regression-rows ROWS=<row>`) before treating it as a
regression, and do not dismiss it until that solo run passes. The self-test
pins the behaviour both ways (`HS-50..55`).

## No row script may install a `trap`

Every row script is still **sourced** — into its `row-runner.sh` shell — and
that shell already holds the one `trap regression_cleanup EXIT/INT/TERM` that
deletes the row's directories (in the driver, the same trap deletes the 1–2 GB
per-run SD clone). Bash keeps a single handler per signal, so a second
`trap ... EXIT` in a sourced row silently replaces the harness's own. Because
INT and TERM are left alone, it is specifically the **successful** run that
then leaks its entire run directory while the counts stay green — a host
reached 93% full with 112/112 passing before anyone noticed.

`lint-traps.sh`, which is row 2 of the suite, bans `trap` in `scripts/*.sh` for
every signal and at any depth: behind `builtin` or `command`, inside `eval`,
and via a heredoc fed to `source`, `.` or `eval`, all of which run in *this*
shell. Matching is done on a syntax skeleton in which every quoted string
collapses to a single inert token, so a live string whose contents happen to
look like syntax stays clean. Its scope is bounded on purpose: **it catches the
accidental trap, not deliberate obfuscation, which no static grep can.** When a
row needs scratch files, put them under `$TMP_DIR` instead of installing a
cleanup handler — the harness trap already removes that directory.

## No row script may touch the counters

The same sourcing puts the harness's own `pass`, `fail` and `skip` in every
row's scope. `script-replay-edge-func` once used `skip` for a reason string;
that zeroed an earlier SKIP, so CI — where `gdb-z88dk-func` then skipped — reported 214
of 215 rows with an empty `Skip:`, while every local run, with nothing skipped,
stayed green. The driver now wraps each functional row in a guard
(`row_counters_snapshot` / `row_counters_check`, `test-functions.inc`) that
checks the EFFECT after the row, however it was done: the three counters are
still integers, none went down, and together they grew by exactly the row's one
result (none for a row a name filter left out). Anything else is a harness
fault naming the row. A runtime check, not a lint, because it also sees an
indirect write — `read`, `printf -v`, `declare`, a helper the row called — that
a grep would not. What it cannot see: a row that changes a counter and puts it
back, one that swaps results while keeping the sum, and any other harness
global. Give a row's own variables names of their own.

## Every `timeout` must escalate to `SIGKILL`

`timeout N cmd` sends `SIGTERM` and nothing after it. A command that does not
act on `SIGTERM` keeps running; `timeout` waits for it and then reports 124, so
the bound is decorative and the status lies. Two `jnext` processes were once
found alive **9289 seconds** after a `timeout 120` in a row — reparented to
systemd, the worktree that spawned them long deleted, burning a core apiece
underneath the suite's pacing-bound rows. A runaway of that class fails nothing
itself; it makes *other* rows lie.

So every invocation uses the house form:

```bash
timeout --foreground --kill-after=5s 60s "$JNEXT" --headless ...
```

`--kill-after` is the requirement, and `--signal=KILL` satisfies it too:
`SIGKILL` cannot be ignored, so the bound is real. It costs nothing when the
command is well behaved, because it fires only if `SIGTERM` was already
disregarded. `--foreground` is *not* required and is not always right — without
it `timeout` gives the command its own process group and the signal reaches its
**children**, which is what you want when it spawns a process tree.

`test/lint-timeouts.sh`, row 3 of the suite, enforces it across every tracked
`*.sh` under `test/` — a wider scope than the trap lint, because this hazard
has nothing to do with being sourced: a process a packaging test leaves behind
costs the same as one a regression row leaves behind. The rule has **no
exception list**, deliberately: whether a given program handles `SIGTERM` is
not statically decidable, and "this one is fine" is the reasoning that put the
bare `timeout` there in the first place. A sweep fixes only what it is pointed
at: the hand pass before the lint fixed five call sites in two files and left
**three** behind elsewhere, one of them seven weeks old in
`test/packaging/packaging-test.sh` — which is the case for a whole-tree scope.

## Regenerating reference screenshots

```console
$ bash test/00regression/generate-references.sh [test_name...]
```

This is a thin wrapper around `regression.sh --update`, which overwrites each
reference image with the frame just captured. **It is not a routine action.** In
`--update` mode the accounting is skipped and every row reports `UPDATED`
regardless of what it produced, so a genuine regression quietly becomes the new
baseline and every future run agrees with it. Regenerate only when a rendering
change is both intentional and understood, name the specific rows wherever you
can, and treat a reference diff appearing in a review as a claim to be checked.

## Platform runs: macOS and Windows under wine (GH #319)

`regression.sh --platform` (`make regression-win`, `make regression-macos`) runs
the screenshots and the functional rows an OS can run against that OS's
binaries. It reuses the whole driver: the manifest pins, the per-row processes,
SKIP = failure and the completeness accounting stay as they are; only the
selection, the binaries and the accounting change.

**Declaring applicability.** A row's line in `functional_tests.conf` may carry
`os=<list>`, a comma-separated subset of `linux,macos,windows`. `linux` is
mandatory (a Linux run runs every row; the Linux pins never change), no tag means
all three, and an unknown or empty element, or a list without `linux`, is a
harness fault. The reason sits in a `# os:` comment on the line above the row.
`# expect-macos: N` and `# expect-windows: N` beside `# expect:` pin how many rows
each OS runs; once any row is tagged both are required, and a mismatch in either
direction is a harness fault, so retagging a row is a deliberate edit. Absent rows
are printed by name; a named row the OS does not run is refused; an absent row
that reports is a harness fault. A platform run's total is 1 provision + the
screenshots + the admitted rows (no lints, no stamp).

**The Windows runner.** `test/wine-run.sh --jnext` runs `jnext.exe` as `$JNEXT`
(two one-line shims in the run's `$TMP_DIR` call it, so rows keep invoking
`"$JNEXT"` as one word). Each of these was measured to be necessary under wine
11:

1. spdlog writes CRLF on Windows, to stdout and stderr; the runner folds it to LF
   with an unbuffered `sed -u`, one filter when both streams are the same file.
   A row cannot see a CRLF jnext writes there; it is the platform's documented
   EOL, not a contract any row asserts. Text *files* are another matter: jnext's
   trace files are written in binary mode so they are LF on every host.
2. wine drops `QT_*` and `SDL_*` from the Unix environment and imports
   `WINE<name>` as `<name>`, so the runner exports `WINEQT_QPA_PLATFORM` and the
   like. Without it a Qt `jnext.exe` uses the `windows` platform and never exits.
3. The first wine process spawns a `wineserver` that inherits the output
   filters' pipes and never lets them see EOF, so rows hang. The driver starts
   one with stdio on `/dev/null` before any row (`wine-run.sh --serve`) and
   `regression_cleanup` stops it (`wineserver -k`).
4. The filter must be unbuffered: the rows' "listening on" polls time out on
   block-buffered output.
5. A background job has stdin on `/dev/null`; the runner passes stdin on
   explicitly. TERM and INT are forwarded to wine, and its exit status is the
   exit status.
6. bash and wine are called by absolute path (a row may set `PATH` to an empty
   directory); `DISPLAY` is kept only for an `xvfb-run` display, never a desktop.
7. wine cannot `CreateProcess` an ELF, so the `--record` rows (ffmpeg) are absent
   on Windows; the Windows spawn path is covered by the unit rows SF-*.
8. The prefix is ~1.7 GB and lives under the build tree
   (`build/win-release-non-pgo/wine-prefix`), never in `/tmp`.
9. `dzrp-paused-headless-func` measures jnext's own CPU: under a wrapper
   "the first child" is a shell, so the row finds the process named `jnext` /
   `jnext.exe` among the descendants (deepest one) and reads its CPU from `/proc`
   or, off Linux, `ps -o time=`.

**On macOS** the SD image comes from `make sdcard-image`, the per-run clone falls
back to a plain copy where `cp --reflink=always` does not exist (reported as
`copy`), the host load comes from `sysctl vm.loadavg`, and no host lock is taken.

