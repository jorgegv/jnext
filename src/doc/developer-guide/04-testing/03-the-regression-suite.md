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
`8 lint + 1 sdcard-provision + screenshots + functional` (no lints in a platform run). Any mismatch is
reported as a **harness fault**, exit 2, and is explicitly not a pass.

Build artifacts that rows depend on — `rewind_test` and the SDL-only `jnext` —
are checked in the first second of the run rather than five minutes in. Running
with `--preflight-only` performs every guard and then exits, which is the seam
the harness self-test drives.

## The SD image

No test row passes `--sdcard`. Every row relies on jnext's own default-location
lookup instead, and the suite provisions that image for itself in the
`[sdcard-provision]` row. Two separate mechanisms do two different jobs:

- **A hash gate** protects the suites' own machine-wide master, under
  `~/.jnext/sdcard/test-suite/sdcard/`, from whatever happened to it between runs, re-deriving it only when it has
  drifted. This is not a theoretical concern: after an evening of manual
  NextZXOS booting the image had 11012 bytes changed, and a full run reported
  91 pass / 5 FAIL on a branch that was green. The hash witness is written
  when the image is derived, so it cannot notice that jnext now derives a
  different image. The gate therefore also re-derives when the image's
  `.recipe` sidecar, written by jnext, does not hold the
  `sdcard::kFixedImageRecipe` of the tree under test.
  The same goes for the distribution: a master whose `.distro` sidecar names
  another distribution than the tree's default is replaced.
  The gate is one function, `sd_master_gate` in `test-functions.inc`, shared with
  `tools/sd-master-ensure.sh`: `make gui-release` (the PGO training) and the
  unit-test targets (`make sdcard-image`, a prerequisite of `make unit-test`)
  run it too, so a host left with the previous distribution's card is repaired
  before anything reads it, under the same lock; a current master is not
  touched. `sdcard-ensure-func` tests the tool.
  The master is **never the user's own card**, the `cspect-next-1gb-fixed.img`
  directly under `~/.jnext/sdcard/`: the gate deletes and re-derives its card
  whenever the distribution or the recipe moves, and a developer's saved files
  must not go with it. `sd_test_state_dir` (shell) and `test/test_sd_image.h`
  (C++, the unit suites' fallback when `JNEXT_TEST_SD_IMAGE` is unset) name the
  one test location, `HS-197` checks that they agree, `sdcard-user-card-func`
  runs the gate against a current-looking sentinel user card and shows it
  byte-identical afterwards, and the preflight's `lint-user-card` (row 8) bans
  the user card's path from tests, the SD-master tools, the Makefile and the
  workflows. With a candidate card
  (`JNEXT_REGRESSION_SD_STATE_DIR`) the tool does nothing.
- **A per-run clone** points `JNEXT_CONFIG_DIR` at a private directory, so the
  run boots its own copy and every non-headless invocation starts from clean
  GUI preferences. That directory lives under `$HOME` rather than `/tmp`,
  because `cp --reflink` cannot cross a filesystem and `--reflink=auto` would
  quietly degrade to a real 1 GB copy into RAM.

## A content scan: the bug report row

`bug-report-card-func` proves a rule by looking for its violation. A bug report
([3.11](../03-subsystems/14-bug-reports.md)) taken in a session that booted NextZXOS must
carry none of the card's firmware, so the row extracts every file under `/MACHINES/NEXT`,
`/NEXTZXOS` and `/TBBLUE.FW` of the run's card clone with `mcopy`, adds `nextboot.rom`,
and fails if any 32-byte window of them (taken every 32 bytes, windows with a period of 4
or less, fewer than 8 distinct byte values, or a counting sequence left out: register files and register numbers equal such tables) occurs at any offset of any decompressed member of the report, as raw bytes and as what the member decodes to (JSON hex strings and integer arrays, nested zips). The only member left out is the program, proved byte-equal to the row's own file, and only when it is the one program. The
extracted files live in the row's scratch directory and are removed with it: nothing is
kept, and nothing of a distribution is ever committed. A scan with no files to scan is
itself a failure, so it cannot pass by finding nothing to look for.

## Boot-timeline frames: the boot-ready helper

A row that presses a key, captures a screen or resets "at frame N of a cold boot"
must mean the same moment of the boot on every card, and a new NextZXOS distribution
loads its firmware for a different number of frames before it reaches the same
state (the 26.11a firmware commits its machine type at frame 476, the 26.11 one at 472, the
24.11 one at frame 295). The anchor is the **firmware's machine-type commit**: the first NR
0x03 write whose bits 2:0 are 001 to 100, which by `zxnext.vhd` is the write that
clears config mode. It happens on every firmware boot whatever personality
`config.ini` selects, and `boot-commit-probe.jds` reports its frame in a couple of
seconds. `test-functions.inc` has the helper:

- `BOOT_REF_COMMIT` is the commit frame of the boot the row literals were written
  against, and `BOOT_DEFAULT_COMMIT` the frame the default card must give.
- `bf N [K]` prints `N + K * (measured - BOOT_REF_COMMIT)`. Use `K = 2` for a frame
  after a hard reset, which runs the firmware a second time; a soft reset does not
  (`K = 1`). A malformed `N` or `K`, or a probe that finds no commit within 1500
  frames, is a harness fault and never a pass-through.
- `bf_keys <frame key>...` fills `BF_KEYS` with `--delayed-keypress-frames`
  arguments; `bf_jds <template> <out>` expands `{{bf N}}` and `{{bf N K}}` in a
  script template and refuses an output that keeps a `{{`.
- `menu_index <image> <mode> <rom>` is not a timing helper but belongs with them: the
  `config.ini` `default=` value of the card's own menu line with that mode and ROM, counted
  as the firmware counts (it reads `menu.ini`, else `menu.def`, and counts only `menu=` lines
  with a ROM field). A distribution may insert menu lines, so the machine-personality rows ask
  the card instead of carrying a number; it fails when the key matches no line or more than one.
- A screenshot row in `regression_tests.conf` writes `@bf:N` (or `@bf:N:K`) in its
  frame field or among its extra arguments.

Compute every `bf` into a variable first (`shot=$(bf 400)`): a failure inside an
argument position would not stop the row. Only frames on the boot timeline AFTER
the commit are adopted. A frame inside a program loaded by `--load` (the warm
start rebases those to frame 500), a wall-clock bound, and a row that needs no
NextZXOS (`beam-replay-func` injects a halt program at frame 160, before the
firmware commits, and a shift does not help it) stay literals.

There is one deliberate exception, `boot-nextzxos-splash`: its frame 429 is before the
commit (476 on the 26.11a card), but it captures the firmware's own splash and the
screen must be taken at the same stage of that boot on every card. It is adopted
(`@bf:429`); on the 24.11 card (commit 295) the shifted frame 252 shows the same splash
stage with the older firmware's text, so the shift keeps its distance to the commit.

The literals are those of the 26.11 card (commit 472, `BOOT_REF_COMMIT`), whose shift is 0.
The default card is 26.11a, commit 476, so every adopted row's frames shift by +4 on it and
no literal changed when it became the default. (They were written against 24.11, commit
295, and rebased by `+177` per firmware run when 26.11 became the default: a 24.11 card
shifts by -177.) The `[sdcard-provision]` row reports the
commit frame and the shift, and on the default master FAILS when the commit is not
`BOOT_DEFAULT_COMMIT`: an emulator change that moves the boot is a red row, never a
shift the helper quietly absorbs. On a candidate card (the seam below) any commit frame
is allowed and reported.
`esxdos-write-probe-measure.sh` takes the shift as `BOOT_SHIFT`.

## Evaluating a new NextZXOS distribution

The default card is the 26.11a distribution, and the references and pins are
made against it. To find out what another distribution would change, without
touching the machine-wide master that every concurrent run tests against, run
the same tree twice (the same non-PGO `jnext`, master A = the default, master B
= the candidate) and compare. Nothing here switches the default.

1. **Provision B through jnext itself**, in a private state directory outside
   the checkout, so the download, unzip and patch path is the one a user hits:

   ```bash
   JNEXT_CONFIG_DIR=$HOME/tmp/cand/state \
   JNEXT_SDCARD_DISTRO_URL=<the candidate distribution zip URL> \
   build/gui-release-non-pgo/jnext --headless --sdcard-download-confirm --delayed-automatic-exit 2
   ```

   The state directory then holds `sdcard/cspect-next-1gb.img` (the raw card),
   its `.sha256` sidecar and `cspect-next-1gb-fixed.img` (the patched card).
   Never run it without `JNEXT_CONFIG_DIR`: a forced download would replace the
   shared master.
2. **Say what differs**, with the read-only inventory tool. It prints the MBR
   and BPB geometry, whether the card is a valid FAT32 volume, the size and
   sha256 of the ROMs, `menu.def`, `TBBLUE.FW` and `TBBLUE.TBU`, the firmware
   magic, the shape of `zx81.rom`, the core versions inside the `.TBU` and a
   digest of the `/dot` name list:

   ```bash
   tools/sdcard-inventory.sh <card.img>
   tools/sdcard-inventory.sh --diff <a.img> <b.img>    # exit 0 identical, 1 differ
   ```
3. **Boot it** before anything long: a headless `--machine next` run on a
   reflinked copy of B, with a screenshot at a few frames, shows whether the
   firmware starts on the emulated core and how many frames the boot takes. A
   row whose keys are timed against the boot (a key at frame 400) is written
   with the boot-ready helper (next section), so it moves with the boot.
4. **Run the unit suites on B**: `JNEXT_TEST_SD_IMAGE=<a reflinked copy of B's
   fixed card> make unit-test`. The harness honours a preset image; rows that pin
   file sizes or magic bytes of the default distribution turn red by design.
5. **Run the full regression twice, bare**, A and then B, with the same
   non-PGO binary (bare `regression.sh` would otherwise pick the PGO build
   first):

   ```bash
   JNEXT=$PWD/build/gui-release-non-pgo/jnext bash test/00regression/regression.sh
   JNEXT=$PWD/build/gui-release-non-pgo/jnext \
   JNEXT_REGRESSION_SD_STATE_DIR=$HOME/tmp/cand/state \
   JNEXT_REGRESSION_SHOTS_DIR=$HOME/tmp/cand/shots \
       bash test/00regression/regression.sh
   ```

   (`make regression-rows` pins that binary itself and is not refused under the
   seam.) The two variables are validated by the driver:
   `JNEXT_REGRESSION_SD_STATE_DIR` (the state directory whose `sdcard/` holds
   the master, in place of `~/.jnext/sdcard/test-suite`) and `JNEXT_REGRESSION_SHOTS_DIR` (where a
   failing screenshot row leaves its actual PNG, which is otherwise deleted with
   the run directory; the driver creates it). Both must be absolute paths
   without a `..` component, outside the checkout (`//` and `/./` are
   collapsed before that test; a symlink into the checkout is not detected). A
   run that sets the state directory is refused if it is stamped
   (`JNEXT_REGRESSION_STAMP`, so `make regression` and `make regression-confirm`)
   or asked to regenerate references (`--update`): the stamp and the references
   certify the default card. Such a run is bare `regression.sh`, never stamped.
   The `[sdcard-provision]` row names the master in use.

   **What the seam directory must hold.** A candidate card is never deleted
   unless a verified raw rebuilds it, and never replaced by a download. `sdcard/cspect-next-1gb-fixed.img` is the
   card under test. With no witness beside it, the card is used as it is and a
   witness is written. If the card is missing, or no longer matches its
   witness, the row can only rebuild it from `sdcard/cspect-next-1gb.img` plus
   `cspect-next-1gb.img.sha256` (what step 1 leaves behind) and fails, saying
   why, when that raw does not verify. A card with the wrong recipe sidecar is
   re-derived only from such a raw.

The card, the distribution zip and anything derived from them (an MBR or BPB
dump, a ROM, a screenshot of a candidate screen that is going to be posted) are
copyrighted. They live under `~/tmp` and `~/.jnext`, never in the repository,
an issue, a pull request, a release or a CI artifact. The tool that makes or
reads a card is public; its output is not.

## Concurrency: one process per row, in phases

The functional rows used to be *sourced* into the driver one after
another, sharing its counters, its `TMP_DIR` and its SD clone, which
made them serial. Most of them wait on real time rather than on the CPU,
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
row that quietly changes what its neighbours boot. `bound=<seconds>` gives one
row its own kill bound in place of the 600 s default, for a row whose measured
pass is slower (`harness-selftest-func` carries `bound=1500`); it takes a
positive integer, at most once, and anything else is a harness fault.

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

A second Xvfb race hit the rows that poll with `xdotool`: without `-noreset` the
X server regenerates whenever its last client disconnects, and the row's own
`xdotool search` poll is such a client while the app starts, so an app
connecting mid-regeneration is dropped (`SDL_Init: x11 not available`, no
screenshot; 18/1000 connections failed under load, 0/1000 with
`-noreset`). Those rows start Xvfb with `-noreset`, HS-89 enforces it, and a
missing screenshot or non-zero app exit is a FAIL naming the run and cause (the
solo-confirm path), never a SKIP.

## One full run at a time, and the stamp

A full run (no arguments, real manifests) takes a host lock through `flock -o`
before it sources anything, so a second one waits, saying who holds it, and no
child of the run can inherit the lock. It then waits, bounded, while the
1-minute load is above `nproc`. `make regression` also asks the run for a
**stamp**: on a green run of a tree with no uncommitted non-doc change,
`test/regression-stamp.sh` records the counts, the loads and the commit under a
key hashed from `git ls-tree` over every non-documentation path.
`make regression-stamp-check` prints the stamp covering the current tree, which
is how a fallback batch gate on an unchanged tree avoids repeating the run, and
`make regression-ci-check` finds a green CI run with the same key, which is how
a release reuses CI. Only a run with no FAIL and no SKIP is stamped, and since 2026-10-06 `regression.sh`
itself exits 1 on a SKIP in every mode (full, named rows, `confirm`, `--update`;
a standalone row's `standalone_summary` too), naming the rows. A tool a row needs
is provisioned rather than assumed: `make regression` first runs `make z88dk-gdb`
(`test/provision-z88dk-gdb.sh`), which builds z88dk v2.4's client, sha256-pinned,
into `~/.cache/jnext/tools` when none resolves. The two foreign readers of the reverse-engineering exports are provisioned
the same way by `make skoolkit` and `make ghidra`; see
[3.9.5.4.7](../03-subsystems/09-5-source-level-debugging/04-7-skoolkit-and-ghidra-exports.md). A run whose
only FAILs are functional rows that failed on a loaded host is recorded as
pending, and `make regression-confirm` re-runs exactly those rows solo; if they
all pass the run is stamped, and the stamp names them. Targeted rows run through
`make regression-rows ROWS="..."`, which builds the binaries they need. Its Qt
`jnext` is the non-PGO Release build (`build/gui-release-non-pgo`): a branch trains
no PGO profile, and the same code gives the same pixels. `ROWS_PGO=1` runs the PGO
`build/gui-release` instead; `make regression` and `regression-confirm` always do. A row
name that neither manifest declares (or an empty one) is a harness fault, exit 2, in every
mode, so a mistyped name can never run nothing and pass.

## A failure on a loaded host

Those two rows are examples, not the list. Rows that merely spawn short-lived
processes have also failed under a parallel-agent load and never reproduced
solo (`subsystem-gain-func`), and a concurrent duplicate build once
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
(`make regression-rows ROWS=<row> ROWS_PGO=1`, the PGO binary the full run tested) before treating it as a
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

## Platform runs: macOS, Windows under wine and WebAssembly under node

`regression.sh --platform` (`make regression-win`, `make regression-macos`,
`make regression-wasm`) runs
the screenshots and the functional rows an OS can run against that OS's
binaries. It reuses the whole driver: the manifest pins, the per-row processes,
SKIP = failure and the completeness accounting stay as they are; only the
selection, the binaries and the accounting change.

**Declaring applicability.** A row's line in `functional_tests.conf` may carry
`os=<list>`, a comma-separated subset of `linux,macos,windows,emscripten`. `linux`
is mandatory (a Linux run runs every row; the Linux pins never change), no tag
means linux, macos and windows (`emscripten` is opt-in: that run executes a row
only when its tag names it), and an unknown or empty element, or a list without `linux`, is a
harness fault. The reason sits in a `# os:` comment on the line above the row.
`# expect-macos: N`, `# expect-windows: N` and `# expect-emscripten: N` beside
`# expect:` pin how many rows each OS runs; once any row is tagged the first two
are required (the third as soon as a row names emscripten; it is 0 until then),
and a mismatch in either direction is a harness fault, so retagging a row is a
deliberate edit. Absent rows
are printed by name; a named row the OS does not run is refused; an absent row
that reports is a harness fault. A platform run's total is 1 provision + the
screenshots + the admitted rows (no lints, no stamp).

**The WebAssembly run.** `make regression-wasm` builds `build/wasm-core` (the one
tree stands for both: there is no Qt build for the web) and runs every screenshot
row against `jnext.js` under the SDK's node. No functional row runs yet: none
names emscripten, so all are printed absent and a WebAssembly run reports 1
provision + the screenshots. `jnext` in that tree is linked for node
(`-sNODERAWFS=1 -sENVIRONMENT=node -sEXIT_RUNTIME=1`, per target; the browser
build is `jnext_web`, which does not inherit them): NODERAWFS gives it the host
filesystem, which is how it finds the run's SD clone and writes the PNG to the
row's path. The driver writes one-line shims that `exec` `test/node-run.sh
--jnext` (the node the tree was configured with, never one from `PATH`; locale
fixed; no display; a private `TMPDIR`; TERM and INT forwarded; node's exit
status returned), so a row invokes `"$JNEXT"` as one word. emcc's `jnext.js` and
`jnext.wasm` are not executable, so the driver checks both with `-f`. The SD
master is derived by the host's own `jnext` (the SDL unit-test tree), because the
WebAssembly build declines downloads, as under wine. The screenshots are compared
exactly as everywhere else, decoded pixels at tolerance 0, against the committed
native references; none is regenerated for WebAssembly. The only
concession is the wall-clock safety bound: the emulated exit is untouched (it
counts emulated seconds), but a row's `timeout` is widened by
`SHOT_WALL_FACTOR` (4, set by the driver for this run only), because node runs the
core slower than the native build the bound was written for.

**Where the target OS comes from.** The OS is read from the binary's own tree, never
from the caller: `JNEXT_TARGET_OS` in the tree's `CMakeCache.txt` or, for a dir with
no cache (an unpacked Windows package, `make regression-package`), the PE header of
its `jnext.exe` (`MZ`, then `PE\0\0` at the `e_lfanew` offset). A dir with neither
is a harness fault.

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
   The desktop process (`explorer.exe /desktop`) is the same hazard: wine
   starts it from whichever client first needs the desktop, on that client's
   stderr (a row's filter pipe), and it lives while any other wine process uses
   the desktop, so a 0.1 s step could be held to its 120 s bound by another row. `--serve` therefore also starts a keeper (`wine start /wait cmd /c
   pause`, stdio on `/dev/null`), verifies exactly one stable `explorer.exe` of
   the prefix on `/dev/null` and prints its pid; the driver records it and
   raises a harness fault if that process is not the same one at the end of the run.
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
   (`build/win-release/wine-prefix`, or inside the unpacked package under `build/regression-package/`), never in `/tmp`.
9. `dzrp-paused-headless-func` measures jnext's own CPU: under a wrapper
   "the first child" is a shell, so the row finds the process named `jnext` /
   `jnext.exe` among the descendants (deepest one) and reads its CPU from `/proc`
   or, off Linux, `ps -o time=`.
10. The runner sets `DBUS_SESSION_BUS_ADDRESS` and `DBUS_SYSTEM_BUS_ADDRESS` to
    `disabled:` in every mode, next to the `DISPLAY` removal. SDL's audio init makes
    wine load winealsa, whose pipewire plugin makes synchronous calls to the desktop's
    xdg-desktop-portal over the inherited session bus; a slow portal stalled a Qt `jnext.exe`
    past its row's bound. CI's container has no D-Bus, so this makes a local run equal CI. Both
    are overridden, not unset (libdbus falls back to the system bus). Nothing of this exists on
    Windows.
11. A stalled `jnext.exe` names its blocked call. With
    `JNEXT_WINE_CAPTURE_DIR` set (the driver exports
    `build/regression-artifacts/wine-stall/<run>/`) the runner's watchdog fires once on the exe
    (the process of its name that carries the runner's `JNEXT_WINE_RUN_ID` marker: wine 11.20 runs
    a 32-bit exe behind `start.exe` as a separate process) when it is at least 20 s old, its CPU
    time has been flat for 10 s and totals 2 s or less. It writes `wine-stall-<time>-<pid>.txt`:
    `/proc` threads, a `target:` line, the prefix's processes, the `winedevice.exe` threads,
    `winedbg info thread`, and one `Backtrace:` per thread from `test/wine-stack.c` (built by
    `make wine-stack-x64` / `wine-stack-x86`, a helper that attaches no debugger: winedbg's attach
    kills a 32-bit process on that wine). TERM and INT are forwarded to both processes. Before the
    first row the driver blocks a decoy and a target `jnext.exe` on the SD prompt, and treats a
    capture that is not the target's, names no `ReadFile` frame, or leaves either process unable to
    exit by itself as a harness fault. Every Windows run ends with a `wine prefix at the end:`
    line and lists any captures.

**On macOS** the SD image comes from `make sdcard-image`, the per-run clone falls
back to a plain copy where `cp --reflink=always` does not exist (reported as
`copy`), the host load comes from `sysctl vm.loadavg`, and no host lock is taken.

