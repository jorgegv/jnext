# Regression Test Suite

Automated testing infrastructure for the JNEXT emulator. Runs FUSE Z80 opcode
tests and screenshot-based visual regression tests in headless mode.

## Prerequisites

- Built emulator (`cmake --build build`)
- ImageMagick (`compare` command) for pixel-level screenshot comparison
- z88dk toolchain (only if rebuilding demo programs)

## Quick Start

```bash
# Run the full regression suite
bash test/regression.sh

# Generate/update reference screenshots
bash test/generate-references.sh
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

Tests are defined in `test/regression_tests.conf`:

```
# Format: test_name machine_type nex_file screenshot_delay_secs
boot-48k          48k     BOOT                        3
palette-demo      next    demo/palette_demo.nex       3
```

- `BOOT` as the nex_file means "boot without loading a program" (tests ROM boot)
- `screenshot_delay_secs` is how long to wait before capturing
- The auto-exit delay is automatically set to `screenshot_delay + 2`

## Reference Screenshots

Stored in `test/img/<test_name>-reference.png`. These are the known-good
baselines for comparison.

### Generating References

```bash
# Generate all references
bash test/generate-references.sh

# Generate specific test references
bash test/generate-references.sh boot-48k palette-demo
```

### When to Regenerate

Regenerate references after intentional rendering changes (e.g. palette fixes,
compositor changes, new video modes). Always review the screenshots visually
before committing updated references.

## Running Tests

```bash
# Run all tests
bash test/regression.sh

# Run specific tests
bash test/regression.sh boot-48k palette-demo

# Set pixel tolerance (default: 0 = exact match)
JNEXT_TEST_TOLERANCE=10 bash test/regression.sh
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

Failed tests save a diff image to `test/img/<test_name>-diff.png` for debugging.

## Adding New Tests

1. Create the demo program in `demo/` (C source compiled with z88dk)
2. Build it: `make -C demo <name>.nex`
3. Add a line to `test/regression_tests.conf`
4. Generate the reference: `bash test/generate-references.sh <test_name>`
5. Verify the reference screenshot looks correct
6. Commit the reference image

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
