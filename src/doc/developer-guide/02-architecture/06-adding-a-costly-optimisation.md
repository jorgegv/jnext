# 2.6 Adding a costly optimisation

An optimisation of the emulator's own work (skipping what a program's loop would do again, the way
the HALT batch and the raster-poll batch do) has a **fixed cost**: the test that asks whether it
applies runs for every program, whether or not the program benefits. This page is the rule for what
that cost may be, the way to measure it, the way to prove the optimisation changes nothing a program
can see, and the registry a costly one goes into.

## 1. When it belongs behind a flag

Measure the fixed cost on programs that do not use the optimisation (section 2). Then:

- **Free**: within +0.2 % of per-frame host instructions on every non-using `make bench` workload,
  under every compiler that ships (gcc, and clang for the macOS and WebAssembly builds). It is always
  on and is not registered anywhere. The HALT batch and the Copper unmet-WAIT skip are free.
- **Costly**: above that on any of them. It is registered (section 4) and off unless the user names it
  with `--enable-costly-optimizations NAME[,NAME...]` (the flag is
  not wired into the command line yet: section 4 says what exists).

A user switch for a free optimisation has no use: the output is identical either way and only the host
is slower, and the tests have their `_for_test` setter.

## 2. How to measure

Host instructions, not frames per second: instruction counts do not move with the load of the host,
which wall-time `make bench` needs a quiet machine for.

```bash
# one run's user-mode instructions; N frames of a workload
perf stat -x, -e instructions:u -o out.txt \
    taskset -c 0 build/gui-release-non-pgo/jnext --headless --machine next \
    --sdcard "$CLONE" --benchmark 600 [--load program.nex]
# per frame = (instructions(600) - instructions(200)) / 400
```

- Take the delta of two frame counts, so that start-up cancels. Run a throwaway `--benchmark 20`
  first on the same SD clone: the first run on a fresh clone costs far more instructions than any
  later one and would be read as the workload's.
- The eight workloads are the ones of `test/bench/bench.sh`, against the branch point, on a gcc
  Release build (`gui-release-non-pgo`) and on a clang Release build of the SDL tree
  (`-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=OFF`).
- Expect noise of about 0.1 % between builds of logically equal code: the inlining of the audio chain
  into the HALT replay and of the device cluster into `run_frame()` is heuristic, and an unrelated
  addition can flip one decision. A cost that is a **function-level surprise** (the replay no longer
  inlined, a function that appears in the profile that did not) is found with
  `perf record -e instructions:u -c 1000000` and `perf report -n --sort sym`, comparing absolute
  sample counts, not percentages.
- "Off" must be free too: when the optimisation is costly and off, the code that tests for it must
  not be in the loop at all (a runtime `if (enabled)` is the test that costs). Make the loop a
  template over the optimisation and pick the instantiation once per call.

## 3. How to prove it changes nothing

The optimisation is claimed bit-identical to the path it replaces, so the oracle is that path in the
same binary, held by the A/B rig (`test/core/batch_ab.h`; see section 4.7 of the testing chapter):

- the same machine built twice, the optimisation off and on, run for the same frames, with the video,
  the drained audio and the whole `save_state()` stream hashed after every frame;
- fixtures that engage it, one per gate and bound it has (each cites the hardware behaviour a batched
  stretch must not move), and refusal rows that assert it stands aside for each thing that observes the
  machine; every row asserts its engagement counter, and that the off run really was off;
- every program the project keeps (`halt_batch_corpus_*_test`) with the optimisation on, and the
  NextZXOS boot;
- every other optimisation's switch toggled, so that no one gates another;
- mutations from the diff (each gate dropped in turn, from a `cp` backup, with targeted rows): each
  must turn a row red.

## 4. The registry

`src/core/costly_optimizations.h` is the one table. A row is a `CostlyOptId`, a stable lower-case
`name`, and three one-line texts: `summary` (what it speeds up), `cost` (the measured fixed cost, and
who pays it) and `benefits` (which programs gain and where it does nothing). The header is Qt-free,
because `main.cpp` and the SDL-only and headless builds will include it when the flag exists (only the
tests do today), and holds the parser of
the flag's value (`parse_costly_optimizations()`, which takes its table as an argument so that its
rows run against a table of their own).

Adding an optimisation to it is:

1. one `CostlyOptId` and one table row;
2. one arm of the `switch (CostlyOptId)` in `Emulator::apply_costly_optimizations()` (added with the first
   member), with **no
   `default`**, so that a row without an arm does not compile (`-Werror=switch`); the arm sets the one
   member the optimisation reads;
3. its code, which reads only that member; its default is off, and a `_for_test` setter writes the same
   member (never a copy of it);
4. its rows (section 3), and the registry's own table-driven rows, which cover it without a new row
   (`costly_optimizations_test`: names unique and `[a-z0-9-]+`, texts non-empty, every name parses to the
   bit of its id).

Everything a user sees comes from the table: the names the flag accepts, the error for an unknown name,
the list in `--help`, the startup log line, the man page items (each carries the row's `summary`, `cost`
and `benefits` verbatim, and `make cli-check` fails on a row without one and on an item without a row)
and the "Advanced optimizations" page of the user guide, generated from those man page items by the same
script that generates the command-line chapter. The flag is a command-line setting only: it is not a
Preferences field, because it is not a setting a GUI user needs to find. In the browser build the
player's `args` option forwards it.

The table is **empty today**. The first optimisation, the raster-poll batch, costs a little above the line on two gcc workloads (below)
and the owner decided (2026-11-15) that it ships always on, so it is not
registered. The flag, its man page items, the generated user guide page and the `cli-check` rows
that need them land with the first optimisation that is really costly; what is in the tree now is the
table type and the parser.

## 5. The worked example: the raster-poll batch

Measured against the branch point, in per-frame host instructions on the eight `make bench` workloads
(the optimisation's fixed cost is one flag test per slot that is not a HALT repeat, plus the
refactoring of the HALT batch's gates into the function both batches call; the figures move by about
0.1 % between builds of logically equal code, see section 2):

| workload | gcc | clang |
|---|---|---|
| boot-48k | +0.10 % | +0.05 % |
| boot-nextzxos | +0.23 % | +0.15 % |
| copper-demo | +0.07 % | +0.04 % |
| beast | +0.06 % | +0.03 % |
| bifrost | +0.10 % | +0.05 % |
| halt-idle-35 | +0.08 % | +0.04 % |
| halt-idle-28 | +0.07 % | +0.04 % |
| ctc-dac-28 | +0.21 % | +0.10 % |

Two gcc entries are a little above the +0.2 % line: `ctc-dac-28` (a HALT workload, so the refactoring
of the HALT gates counts there) and `boot-nextzxos`, which reads the raster itself (a few hundred times a
frame) and so pays for the optimisation's observation as well as its test. The optimisation is kept always
on; a later compiler that moves these numbers further is the trigger to register it.

Where it works, it removes most of the host work of a screen that waits for the raster: the NextZXOS
Guide viewer goes from about 91 M host instructions a frame to 39 M (43 %), and the games that poll
the raster in a tight loop to 43-50 % (Warhawk 45 %, Revival Survival 45 %, show512 43 %, Shift 50 %). It does nothing at 3.5 MHz while contention can apply.
