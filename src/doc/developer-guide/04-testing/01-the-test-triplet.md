# 4.1 The test triplet

Every change that lands on `main` has to clear three layers of testing. The
project calls them the triplet, and running them takes four commands:

```console
$ make clean && make gui-release
$ make unit-test
$ ./build/test/fuse_z80_test build/test/fuse
$ JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh
```

There must be no FAIL in any layer. Skips are acceptable only where the suite
already declares them.

## Rebuild first — always

The build at the top is not a courtesy, and it is not left to your good habits
either. `make regression` declares `unit-test-build`, `gui-release` and
`sdl-release` as real prerequisites, because the suite runs those exact
binaries. Most rows run `build/gui-release/jnext`; the four rows that exercise
the SDL-only frontend need `build/sdl-release/jnext` instead, because `SdlApp`
is instantiated only in a build configured with `ENABLE_QT_UI=OFF`; and the
`rewind-func` row runs `build/test/rewind_test`, which `make clean` deletes.
Building all of them from the target is what makes "the tests ran against this
source" true by construction rather than by discipline.

The rule was written after a 14-hour-old binary produced two bogus FAILs
immediately before a version bump. Staleness cuts the other way too, and that
direction is worse: an old binary can equally fail to *contain* the bug a test
would have caught, and then it reports a pass it never earned.

In a fresh agent worktree, run `make worktree-bootstrap` before anything else.
`roms/*` is git-ignored, so a new checkout has no SD-card image and cannot run
the suites at all.

## Layer 1 — the unit suites

`make unit-test` does not go looking for test binaries. It runs exactly the
suites declared in `test/unit-tests.conf`, in parallel, each one bounded by a
300 s timeout. That manifest currently declares **90 suites totalling 6610
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
$ JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh
```

This layer launches the real `jnext` binary headless and looks at what comes
out of it. It reports **119 rows**, made up of 2 preflight lints, 1 SD-image
provisioning row, 65 screenshot rows and 51 functional rows. The last two
numbers come from the `# expect: 65` and `# expect: 51` pins in
`regression_tests.conf` and `functional_tests.conf`, and the arithmetic that
ties them to the total is the harness's own end-of-run accounting assertion.

A screenshot row compares a whole frame, pixel for pixel, against a committed
reference PNG. That is exactly as strong and as blunt as it sounds: it catches
any rendering change anywhere in the pipeline, and when it fails it tells you
the frame changed without telling you why. Functional rows cover the things a
linked library cannot show at all — recording an MP4, an RZX round trip, audio
underruns through a real device, the CLI's exit contracts, keypress delivery in
both frontends.

`JNEXT_TEST_JOBS=4` caps the screenshot launcher's parallelism, and it is worth
keeping. The cap is not politeness towards the machine: `audio-underrun-func`
and `screenshot-paused-func` are bounded by real-time pacing and start failing
under CPU contention, so raising it makes the suite intermittently lie. Note
also that the cap is a caller convention rather than a default — the launcher
falls back to `nproc` when the variable is unset, so `make regression` on its
own is uncapped. CI sets it explicitly.

## What the triplet does not prove

It does not prove that the emulator matches real hardware anywhere no test row
asserts that it does; making those gaps visible is the job of the traceability
matrix ([4.4](04-traceability.md)). And it does not prove that the prose in the
man page or in the guides is true — only that the generated outputs match their
sources. [4.5](05-documentation-and-cli-gates.md) sets out exactly where that
line sits.
