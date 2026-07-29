# Regression Test Suite

Automated testing infrastructure for the JNEXT emulator. Runs FUSE Z80 opcode
tests and screenshot-based visual regression tests in headless mode.

See also: [TEST-TAXONOMY.md](TEST-TAXONOMY.md) — every screenshot row belongs to one of three layers (full-OS SD boot / NEX autoload / z88dk probe). Use the taxonomy to map a failing test back to the subsystem layer most likely to harbour the bug.

## Prerequisites

- Built emulator (`cmake --build build`)
- ImageMagick (`compare` command) for pixel-level screenshot comparison
- z88dk toolchain (only if rebuilding demo programs)
- `xvfb-run` + `python3` (only for `audio-underrun-func`; the test SKIPs without them)

## Quick Start

```bash
# Run the full regression suite
bash test/00regression/regression.sh

# Generate/update reference screenshots
bash test/00regression/generate-references.sh
```

## Headless Mode

The `--headless` CLI option runs the emulator without display, audio, or input.
The emulation runs as fast as possible, making it ideal for automated testing.

```bash
./build/jnext --headless \
    --machine 48k \
    --delayed-screenshot /tmp/screenshot.png \
    --delayed-screenshot-time 3 \
    --delayed-automatic-exit 5
```

## Test Configuration

Tests are defined in `test/00regression/regression_tests.conf`:

```
# Format: test_name machine_type nex_file screenshot_delay_secs
boot-48k          48k     BOOT                                  3
palette-demo      next    test/00regression/nex/palette_demo.nex 3
```

- `BOOT` as the nex_file means "boot without loading a program" (tests ROM boot)
- `screenshot_delay_secs` is how long to wait before capturing
- The auto-exit delay is automatically set to `screenshot_delay + 2`

## Reference Screenshots

Stored in `test/00regression/img/<test_name>-reference.png`. These are the known-good
baselines for comparison.

### Generating References

```bash
# Generate all references
bash test/00regression/generate-references.sh

# Generate specific test references
bash test/00regression/generate-references.sh boot-48k palette-demo
```

### When to Regenerate

Regenerate references after intentional rendering changes (e.g. palette fixes,
compositor changes, new video modes). Always review the screenshots visually
before committing updated references.

## Running Tests

```bash
# Run all tests
bash test/00regression/regression.sh

# Run specific tests
bash test/00regression/regression.sh boot-48k palette-demo

# Set pixel tolerance (default: 0 = exact match)
JNEXT_TEST_TOLERANCE=10 bash test/00regression/regression.sh
```

### Output

```
=== JNEXT Regression Test Suite ===

[fuse-z80] Running FUSE Z80 opcode tests...
  PASS: 1340/1356 opcodes passed

Running screenshot tests...

  [boot-48k]                PASS (0 pixel diff)
  [palette-demo]            PASS (0 pixel diff)
  ...

=== Results ===
  Pass: 13  Fail: 0  Skip: 0
```

Exit code is 0 if all tests pass, 1 if any fail.

Failed tests save a diff image to `test/00regression/img/<test_name>-diff.png` for debugging.

## Adding New Tests

1. Create the demo program in `demo/` (C source compiled with z88dk)
2. Build it: `make -C demo <name>.nex`
3. Copy the built `.nex` into `test/00regression/nex/`
4. Add a line to `test/00regression/regression_tests.conf`
   pointing at `test/00regression/nex/<basename>.nex`
5. Generate the reference: `bash test/00regression/generate-references.sh <test_name>`
6. Verify the reference screenshot looks correct
7. Commit the reference image and the .nex asset

## Building Demo Programs

```bash
# Build all demos (NEX and TAP)
make -C demo all

# Build only NEX files
make -C demo nex

# Build only TAP files
make -C demo tap

# Clean build artifacts
make -C demo clean
```

## Current Test Coverage

| Test            | Machine  | What it verifies                      |
|-----------------|----------|---------------------------------------|
| boot-48k        | 48K      | ROM boot, ULA rendering               |
| boot-128k       | 128K     | 128K ROM, menu display                |
| boot-plus3      | +3       | +3 ROM, Amstrad copyright             |
| boot-pentagon   | Pentagon | Pentagon ROM variant                  |
| palette-demo    | Next     | Layer 2 palette, NextREG I/O          |
| copper-demo     | Next     | Copper co-processor, per-line effects |
| floating-bus    | 48K      | Floating bus behavior                 |
| tilemap-demo    | Next     | Tilemap layer rendering               |
| contention-test | 48K      | Memory contention timing              |
| layer2-320x256  | Next     | Layer 2 320x256 8bpp mode             |
| layer2-640x256  | Next     | Layer 2 640x256 4bpp mode             |
| sprite-scaling  | Next     | Hardware sprite scaling               |

## Functional test: cli-bare-file-func

`jnext <file>` must load the file exactly as `--load <file>` does (Task 25), and
the two ways of getting that wrong must stay errors: a mistyped flag
(`--hedless`) must NOT be silently swallowed as a filename, and `--load X Y` is
ambiguous and must be rejected rather than silently picking one.

## Functional test: audio-underrun-func

The only test in the suite that exercises the **audio output path** end to end,
and the only one that needs a display (audio is disabled in `--headless`).

It runs the GUI binary under `xvfb-run` with SDL's `disk` audio driver, so the
capture is byte-for-byte what jnext hands the sound card (raw S16LE stereo
44100). An 18-byte injected Z80 square-wave loop
(`bin/beeper_tone.bin`) gives an immediate, continuous tone on a 48K machine —
no tape fastload burst, which would pre-fill the audio queue and mask the leak
for the first ~15 s. `check-audio-underruns.py` then scans for **zero-runs that
cut in abruptly from a live signal level**: SDL injects silence when the audio
device queue runs dry, and the emulator's own sample stream continues seamlessly
across the hole, which is what proves the zeros are foreign.

Guards GitHub issue #7 / Task 23: audio is synthesised on the *emulated* clock,
so pacing emulation on a wall-clock timer starves (48K: 50.08 Hz frame vs 50.00
Hz timer) or floods (128K/Next: 49.36 Hz) the device queue. See
`src/platform/audio_pacing.h`. On the pre-fix binary this test reports 25
underruns; the fix reports none.

## Functional test: esp-loopback-func

The only test that exercises the **emulated ESP-01 end to end** (GH #25): a Z80
guest reaching port 0x133B, through `UartChannel`, `EspUartAdapter`, `AtEngine`
and `EspGatedTransport`, out of a **real socket** to a real TCP peer, and every
byte back again. The three ESP unit suites each replace one end of that path
with a fake by design, so none of them can fail if the socket path is broken.

The assertion is a byte stream, not a picture: the guest echoes everything the
ESP says to the magic port in LINE mode, and the row compares the result
against the exact ordered sequence `OK / ERROR / OK / OK / OK / "> " / SEND OK
/ +IPD,14:JNEXT-ESP-OK`, plus the payload the peer actually received. The
`ERROR` is load-bearing rather than incidental — `AT+CIPCLOSE` with nothing
open MUST answer it, because nextsync loops that command *while `ERROR` is not
seen*.

**The peer binds an RFC1918 address, not 127.0.0.1.** The emulated ESP's
address policy denies loopback by default (design doc §8.2) and a test is not a
reason to relax a security decision; RFC1918 is allowed by that same decision,
so the row exercises the "reach a machine on the user's own LAN" configuration
the policy exists for. No packet leaves the host. On a machine with no private
IPv4 the row SKIPs rather than pretending. `test/00regression/esp-loopback-peer.py`
picks the address, listens, and writes the guest binary that dials it — it can
only write the guest once it knows its own port, which is why the three jobs
live in one file.

It cannot flake: no wall-clock threshold, no race with the peer (the shell
waits for a ready file, and the peer holds the connection open until killed so
no `CLOSED` URC can appear mid-assertion), and the guest BLOCKS on each
expected byte so it cannot run ahead of the network. The one wall-clock
dependency — that the frame budget outlasts a same-host TCP connect (~6 ms
measured against a ~2 s budget) — fails safe under load: a busy box runs fewer
frames per second, so the same frame budget buys *more* wall time, not less.

## Design notes

Load-bearing rationale that used to live as long comments inside
`regression.sh`; the script now carries only terse functional comments.

- **Why the `# expect: N` pins exist.** Without a pinned count, deleting a
  test row shrinks the declared and reported sides of the completeness check
  in lockstep: a review experiment removed one screenshot row plus its
  reference image and got a green run whose smaller total matched a previously
  published baseline. The pin forces the denominator to be stated out loud.
- **Why the preflight is itself under test** (`--preflight-only` +
  `test/harness-selftest.sh`). Two preflight guards once shipped dead — a grep
  exiting 1 under `set -e` killed the script before the fault could print —
  and the unit-test harness once shipped a bug that appeared only when a suite
  failed. Untested guards ship broken; the self-test injects each fault and
  asserts the refusal.
- **Why the suite self-provisions the SD image instead of a repo fixture.**
  The earlier git-ignored local fixture (`roms/nextzxos-1gb-fat32fix.img`) had
  accumulated dev-session leftover files, which made `boot-nextzxos-dotls`
  (which screenshots a live SD directory listing) unreproducible against a
  clean checkout. The pristine self-provisioned `~/.jnext/sdcard/` image is
  the same one an end user gets, so references stay reproducible.
- **Why every run boots a PRIVATE clone of that image** (GH #65). The image is
  machine-wide mutable state and NextZXOS writes back to the card it boots, so
  one interactive session can silently redefine what the whole suite tests
  against: a drift once turned a green branch into 91 pass / 5 FAIL, with
  screenshot diffs indistinguishable from a rendering regression. Two
  mechanisms, doing different jobs. The **hash gate** in
  `scripts/01-sdcard-provision.sh` protects the master between runs, re-deriving
  it only when it has drifted. The **per-run clone**
  (`sd_clone_for_run`, `test-functions.inc`) makes a run incapable of dirtying
  the master at all: it reflinks the image into `$JNEXT_CONFIG_DIR`, which
  jnext's provisioner resolves the fallback image from, and the EXIT trap
  removes it. That is what lets regression runs execute concurrently with no
  lock. The clone lives under `$HOME/.jnext/runs/`, deliberately NOT in
  `$TMP_DIR`: reflink works only within one filesystem, and on a typical dev
  host `/tmp` is tmpfs — `cp --reflink=auto` across that boundary silently
  degrades to a real 1 GB copy into RAM per concurrent run.
  `sdcard-isolation-func` asserts each of those properties, and asserts what the
  harness DID (which file jnext opened, which device the clone is on, whether
  the master's inode/mtime moved, whether the run directory survives a kill) —
  never what the image looks like afterwards, which is identical either way.
- **Why membership/count checks are pure-bash hashes.**
  `printf ... | grep -q` over a list is unsound under `set -o pipefail`: grep
  exits on match, printf can die of SIGPIPE (141), and pipefail promotes 141 —
  a present name reports as absent. The measured capacity-vs-scheduling
  analysis lives in the Task 88 write-up (`.prompts/2026-07-18.md`); the idiom
  is also textually banned by harness-selftest HS-30.
- **render-skip engagement upper bound (590).** An early throttle bug (an
  INT64_MIN sentinel overflow in the now-last subtraction) skipped every
  single frame — 601 skips in 601 frames, frozen display. The `<= 590` bound
  rejects that skip-everything mode while leaving room for the fastest
  plausible tick rate.
- **Snapshot reload is pixel-verified.** A review mutation wrote all-zero RAM
  into every ZXSTRAMPAGE payload: structurally valid, loads fine, renders
  garbage — "a PNG came out" passes, the pixel comparison does not.
- **Tape-save boot guard.** An ungated SA-BYTES trap fired 10 times during a
  normal NextZXOS boot, appending ~327 KB of garbage and breaking the boot;
  the ROM-identity gate (48K prologue bytes at 0x04C2) plus the
  `tape-save-boot-func` row keep it fixed.
- **audio-underrun under Xvfb/Wayland.** The stray-desktop-window fix (unset
  `WAYLAND_DISPLAY`, force the X11 backends) was verified by strace (two
  connects to `wayland-0` pre-fix). Pointing `WAYLAND_DISPLAY` at a dead
  socket instead makes SDL fail to open an audio backend and the row SKIPs —
  a silently-disabled test is the worse trade versus a harmless residual
  probe connect.
