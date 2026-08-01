# 4.1 The test triplet

Every change that lands on `main` clears three layers. They are called the
triplet, and the commands are:

```console
$ make clean && make gui-release
$ make unit-test
$ ./build/test/fuse_z80_test build/test/fuse
$ JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh
```

No FAIL anywhere, in any layer. Skips are acceptable only where the suite
already declares them.

## Rebuild first — always

The build step is not a courtesy. `make regression` declares `unit-test-build`,
`gui-release` and `sdl-release` as real prerequisites, because the suite runs
those exact binaries: `build/gui-release/jnext` for almost every row,
`build/sdl-release/jnext` for the one row that drives the SDL-only frontend
(`SdlApp` is instantiated only when `ENABLE_QT_UI=OFF`), and
`build/test/rewind_test`, which `make clean` deletes. Building them from the
target makes "the tests ran against this source" true by construction instead
of by discipline. The rule was added after a 14-hour-old binary produced two
bogus FAILs immediately before a version bump — and a stale binary is worse in
the other direction too, since it can also lack the bug a test would have
caught.

In a fresh agent worktree run `make worktree-bootstrap` first: `roms/*` is
git-ignored, so a new checkout has no SD-card image and cannot run the suites
at all.

## Layer 1 — the unit suites

`make unit-test` runs exactly the suites declared in `test/unit-tests.conf`,
in parallel, each bounded by a 300 s timeout. That manifest currently declares
**90 suites totalling 6610 rows** — the suite count is pinned in the file's own
`# expect: 90` line, and the row total is the sum of the per-suite counts in
that same file. `test/SUBSYSTEM-TESTS-STATUS.md`, the committed dashboard,
reports the same 6610.

These suites link the emulator libraries directly and drive classes through
their public API. They are hermetic — only `sd_rom_extractor_test` reads the SD
image — and they are where VHDL compliance is asserted row by row. What they
cannot catch is anything that only exists once the whole machine is assembled:
they render no frame, open no window, and drive neither frontend.

`make unit-test` also pulls in the cheap structural gates ahead of the build:
the tautological-assertion lint, the Makefile help lint, the traceability
accounting/self-test/duplicate-ID checks, the documentation checks, and the
hermetic half of the packaging contract tests. A gate the inner loop does not
reach is not a gate.

## Layer 2 — the FUSE Z80 opcode suite

```console
$ ./build/test/fuse_z80_test build/test/fuse
```

1356 of 1356 pass. This is the FUSE project's data-driven opcode corpus: every
instruction, including undocumented behaviour and per-instruction cycle counts.
It is the only thing that proves the CPU core itself is right, and it proves
nothing else — Z80N extensions are `z80n_test` (85 rows), and everything
Next-specific is elsewhere.

Note that `fuse_z80_test` is *also* a declared row of `test/unit-tests.conf`
(pinned at 1356, with its data directory as an argument), so `make unit-test`
already runs it. The standalone invocation above runs the same binary against
the same data; it is a convenience for reading that one number, not a third
independent execution.

## Layer 3 — the screenshot and functional regression

```console
$ JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh
```

This layer launches the real `jnext` binary headless and looks at what comes
out. It reports **119 rows**: 2 preflight lints + 1 SD-image provisioning + 65
screenshot rows + 51 functional rows. Those two counts come from the
`# expect: 65` and `# expect: 51` pins in `regression_tests.conf` and
`functional_tests.conf`; the arithmetic is the harness's own end-of-run
accounting assertion.

Screenshot rows compare a whole frame, pixel for pixel, against a committed
reference PNG. That is exactly as strong and as blunt as it sounds: it catches
any rendering change anywhere, and on failure it tells you the frame changed
rather than why. Functional rows cover the things a linked library cannot show
— recording an MP4, an RZX round trip, audio underruns through a real device,
the CLI's exit contracts, keypress delivery in both frontends.

`JNEXT_TEST_JOBS=4` caps the screenshot launcher's parallelism. **Keep it.**
It is not politeness: `audio-underrun-func` and `screenshot-paused-func` are
real-time-pacing bounded and fail under CPU contention, so raising it makes the
suite intermittently lie. Note that the cap is a caller convention, not a
default — the launcher falls back to `nproc` when the variable is unset, so
`make regression` on its own is uncapped. CI sets it explicitly.

## What the triplet does not prove

It does not prove the emulator matches real hardware where no test row asserts
it; the traceability matrix ([4.4](04-traceability.md)) is what makes those
gaps visible. It does not prove the prose in the man page or the guides is
true, only that the generated outputs match their sources — see
[4.5](05-documentation-and-cli-gates.md) for exactly where that line sits.
