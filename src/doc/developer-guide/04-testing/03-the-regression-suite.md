# 4.3 The regression suite

`test/00regression/regression.sh` is only a driver: it parses arguments,
validates the manifests, sources the test scripts in declared order, and
enforces the end-of-run accounting. The test logic lives in
`test/00regression/scripts/` — one script per functional test plus three group
scripts (`00-preflight-lint.sh`, `01-sdcard-provision.sh`, `screenshots.sh`) —
and the shared helpers in `test-functions.inc`.

## Two kinds of row, two manifests

**Screenshot rows** are declared in `regression_tests.conf`:

```
test_name  machine_type  nex_file  screenshot_delay_frames  [extra CLI args...]
```

`nex_file` may be the literal `BOOT`, meaning "boot the machine, load nothing".
The delay is in **emulated frames**, never wall-clock seconds, so a slow CI
runner and a fast desktop capture identical machine state. Each row launches
`jnext --headless` with `--delayed-screenshot`, and the PNG is compared against
the committed `img/<name>-reference.png` at a default tolerance of zero pixels,
using an exact any-channel difference mask. A pixel-dimension mismatch counts as
different rather than being cropped to the overlap.

`@private-sd` in the extra-args field is a harness sentinel, not a jnext flag:
the launcher strips it and gives that row its own SD-card clone, which any row
whose guest **writes** to the card needs.

**Functional rows** are declared in `functional_tests.conf`, one name per line,
in run order. Each has its logic in `scripts/<name>.sh` and calls
`begin_func <name>` to register that its row was actually reported.

Both files carry a `# expect: N` pin — currently 65 screenshots and 51
functional — and the driver faults if a pin and the declared lines disagree.

## The independent witness

The completeness check compares rows reported against rows declared, but for
screenshots both sides come from the same conf, so on its own that term is a
tautology. The committed reference images are the outside witness: **every
`img/<name>-reference.png` must have a conf entry**, so truncating the manifest
cannot silently shrink the suite. It exists because a review round deleted one
screenshot row *and* its reference image and got a green 59/0/0 — the project's
own previous baseline. Nobody would have blinked.

The same both-directions rule covers the scripts directory: a declared
functional test with no `scripts/<name>.sh` could never report its row, and a
stray `scripts/*.sh` that nothing declares is a test dropped from the manifest.

## The accounting assertion

At the end of a full, non-`--update` run the driver proves that every declared
functional test reported **exactly one** row, that no undeclared row appeared,
and that the grand total equals `2 lint + 1 sdcard-provision + screenshots +
functional`. Any mismatch is a **harness fault** — exit 2, printed as such, and
explicitly not a pass. Build artifacts that rows depend on (`rewind_test`, the
SDL-only `jnext`) are checked in the first second rather than five minutes in,
and `--preflight-only` runs every guard and exits, which is the seam the harness
self-test drives.

## The SD image

No test row passes `--sdcard`. Every row relies on jnext's own default-location
lookup, and the suite provisions that image itself, in the `[sdcard-provision]`
row. Two mechanisms do different jobs:

- **A hash gate** protects the machine-wide master at `~/.jnext/sdcard/` from
  whatever happened between runs, re-deriving it only on drift. Not
  theoretical: after an evening of manual NextZXOS booting, the image had 11012
  bytes changed and a full run reported 91 pass / 5 FAIL on a green branch.
- **A per-run clone** points `JNEXT_CONFIG_DIR` at a private directory, so the
  run boots its own copy and every non-headless invocation gets clean GUI
  preferences. It lives under `$HOME`, not `/tmp`: `cp --reflink` cannot cross a
  filesystem, and `--reflink=auto` would degrade to a real 1 GB copy into RAM.

## Concurrency

`JNEXT_TEST_JOBS` caps the screenshot launcher's parallel jobs. **Do not raise
it to buy speed.** The suite contains real-time-pacing-bounded rows —
`audio-underrun-func` reports underruns when the box is loaded,
`screenshot-paused-func`'s control run takes ~55 s against a 60 s timeout — so
higher concurrency makes the suite intermittently lie. It defaults to `nproc`
when unset, so pass `JNEXT_TEST_JOBS=4` explicitly, as CI does.

## No row script may install a `trap`

The driver **sources** every row script into its own shell, which already holds
the one `trap regression_cleanup EXIT/INT/TERM` that deletes the 1–2 GB per-run
SD clone. Bash keeps one handler per signal, so a second `trap ... EXIT` in a
sourced row silently replaces it — and because INT and TERM are left alone, it
is specifically the **successful** run that then leaks its whole run directory
while the count stays green. A host reached 93% full with 112/112 passing before
anyone noticed.

`lint-traps.sh` — row 2 of the suite — bans `trap` in `scripts/*.sh` for every
signal at any depth, including behind `builtin`/`command`, inside `eval`, and
via a heredoc fed to `source`/`.`/`eval` (which runs in *this* shell). Matching
uses a syntax skeleton in which every quoted string collapses to one inert
token, so a live string whose contents look like syntax stays clean. Its scope
is bounded on purpose: **it catches the accidental trap, not deliberate
obfuscation, which no static grep can.** Instead of a trap, put scratch files
under `$TMP_DIR` — the harness trap already removes it.

## Regenerating reference screenshots

```console
$ bash test/00regression/generate-references.sh [test_name...]
```

A thin wrapper around `regression.sh --update`, which overwrites each reference
with the frame just captured. **This is not a routine action.** In `--update`
mode the accounting is skipped and every row reports `UPDATED` regardless of
what it produced, so a genuine regression becomes the new baseline and every
future run agrees with it. Regenerate only when a rendering change is
intentional and understood, name the specific rows where you can, and treat a
reference diff in review as a claim to be checked.
