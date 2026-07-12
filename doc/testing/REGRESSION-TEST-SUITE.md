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
