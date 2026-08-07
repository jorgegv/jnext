# 4.3 The regression suite

The regression suite is where the emulator is tested as a whole program rather
than as a set of linked classes. It launches the real `jnext` binary, headless,
and checks what comes out of it — mostly by comparing rendered frames against
committed reference images, and otherwise by exercising behaviour that only
exists once there is a real process, such as writing a file or opening an audio
device.

`test/00regression/regression.sh` is only the driver. It parses arguments,
validates the manifests, sources the test scripts in the declared order, and
enforces the end-of-run accounting. The test logic itself lives in
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
in run order. Each has its logic in `scripts/<name>.sh` and calls
`begin_func <name>` to register that its row really was reported.

Both files carry a `# expect: N` pin — currently 65 screenshots and 51
functional — and the driver faults if a pin and the declared lines disagree.

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
`2 lint + 1 sdcard-provision + screenshots + functional`. Any mismatch is
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
  91 pass / 5 FAIL on a branch that was green.
- **A per-run clone** points `JNEXT_CONFIG_DIR` at a private directory, so the
  run boots its own copy and every non-headless invocation starts from clean
  GUI preferences. That directory lives under `$HOME` rather than `/tmp`,
  because `cp --reflink` cannot cross a filesystem and `--reflink=auto` would
  quietly degrade to a real 1 GB copy into RAM.

## Concurrency

`JNEXT_TEST_JOBS` caps the number of parallel jobs the screenshot launcher
runs. **Do not raise it to buy speed.** Some rows are bounded by real-time
pacing rather than by CPU: `audio-underrun-func` reports underruns when the box
is loaded, and `screenshot-paused-func`'s control run takes about 55 s against
a 60 s timeout. Higher concurrency therefore makes the suite intermittently
lie. The variable defaults to `nproc` when unset, so pass `JNEXT_TEST_JOBS=4`
explicitly, exactly as CI does.

## No row script may install a `trap`

The driver **sources** every row script into its own shell, and that shell
already holds the one `trap regression_cleanup EXIT/INT/TERM` that deletes the
1–2 GB per-run SD clone. Bash keeps a single handler per signal, so a second
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
