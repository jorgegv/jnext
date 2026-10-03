# 5.5 Performance and optimisation

JNEXT's speed comes from the compiler at least as much as from its source. Two
build mechanisms decide how fast the shipped binary is — link-time optimisation
and profile-guided optimisation — and a third, ccache, decides how long you wait
for it. This page describes all three as the code has them today, then how to
measure a change, then what has been measured so far.

## Link-time optimisation

The emulator is built as per-subsystem static libraries — seventeen in the
shipped build with the Qt GUI and the debugger, fifteen in the SDL-only build
(`add_library(... STATIC ...)` in each `src/*/CMakeLists.txt`). That split is good for the code and bad for the
compiler: every hot call that crosses a library — the
CPU into `Mmu::read`, the CPU into `PortDispatch` — cannot be inlined or
devirtualised when each library is compiled on its own. LTO hands the whole
program to the optimiser at link time, so it can.

- **Release only.** `CMakeLists.txt` sets
  `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`, guarded by
  `check_ipo_supported()`, so a toolchain without working LTO builds as it did
  before. RelWithDebInfo — `build/`, the development binary — and Debug stay
  LTO-free, so a rebuild stays quick and `perf` call graphs stay readable.
- **Every Release build has it, with one exception.** `gui-release`, the rpm
  and the debs, and the Windows executables are all Release builds. The
  Windows ones only became Release builds with GH #298: before that, the
  `win-*-release` targets passed no build type, fell back to RelWithDebInfo
  and shipped without LTO. Each one now passes `-DCMAKE_BUILD_TYPE=Release`
  (`WIN_BUILD_TYPE` in the `Makefile`), and the `package-win-lto` row of
  `make package-test` fails if any Windows tree is not a Release build linked
  with `-flto`.
- **The Flatpak is the exception.** The `org.kde.Sdk` gcc miscompiles the
  `QApplication` start-up path under whole-program LTO, and the bundle crashed
  on its first GUI launch. The fault was bisected to the LTO *process* in that
  toolchain, not to one translation unit or one transform, so nothing short of
  turning LTO off fixes it. `JNEXT_ENABLE_LTO` (default ON) exists for that
  build alone; only `packaging/flatpak/io.github.zxjogv.jnext.yml` sets it
  OFF. The full record is in `doc/issues/FLATPAK-LTO-PROBLEMS.md`.

## Profile-guided optimisation

Every binary JNEXT ships is a **PGO** build (GH #297) — gcc's everywhere except
macOS, which uses clang's. The compiler first
builds an instrumented jnext, that binary runs a training set and records which
branches it takes and how often, and the real build is then compiled with that
profile. Hot paths are laid out, inlined and unrolled for the way they are
actually used.

### The pipeline

`cmake/JnextPgo.cmake` adds the flags under the CMake option `JNEXT_PGO`, with
the profile in `JNEXT_PGO_DIR`:

| `JNEXT_PGO` | gcc flags | clang flags (macOS) |
|---|---|---|
| `OFF` (default) | none | none |
| `GENERATE` | `-fprofile-generate=DIR -fprofile-update=single` | `-fprofile-instr-generate=DIR/jnext-%p.profraw` |
| `USE` | `-fprofile-use=DIR -fprofile-partial-training -Werror=missing-profile` | `-fprofile-instr-use=DIR/jnext.profdata -Werror=profile-instr-out-of-date` |

The flags go on the **sources** of `jnext` and of the in-tree libraries it
links, and on nothing else: not on generated code (Qt's moc output — gcc's
profile checksum includes the file's path, which differs between the two trees)
and not on other executables (the tests and `gen-snapshot-schema` never run in
training). `-fprofile-prefix-path` makes the profile's file names relative to
the build directory, so the instrumented tree and the optimised tree can be two
separate directories and still agree on those names. Only the instrumented tree
stays warm in ccache; the optimised one never hits it (see [ccache](#ccache)).

`make gui-release` runs the three steps in three trees:

1. `gui-release-pgo-gen` builds the instrumented `jnext` in
   `build/gui-release-pgo-gen`;
2. `tools/pgo-train.sh` runs the training set with it, writing one `.gcda` per
   translation unit into `build/gui-release-pgo-profile`;
3. `build/gui-release` is configured with `JNEXT_PGO=USE` and built — from
   clean whenever the profile is newer than the last build, because `make`
   cannot see `.gcda` files as dependencies.

The package targets run the same three steps through `tools/pgo-build.sh`, which
takes the package's own configure command and runs it twice, for
`<dir>-pgo-gen` and for `<dir>` with `<dir>-pgo-profile` between them.
`make gui-release-non-pgo` builds the same Release binary without PGO, in its
own tree, and needs no SD image.

### The training set: broad, not big

A profile saturates quickly. GH #294 measured that a profile trained on eight
workloads made four titles it had never seen 17–20% faster, against 17–45% on
the ones it had seen. So the training set exists to reach every kind of hot
path, once, rather than to run a lot of anything.

`tools/pgo-train.sh` runs, headless and with the RTC pinned:

| Run | What it adds |
|---|---|
| boot-48k, boot-nextzxos, copper-demo, beast, bifrost | the `make bench` workloads |
| parallax, trainyard-express, beanbros | three Next games; the first two upload sprites by DMA, beanbros plays AY music |
| `ay_envelope_sweep.bin` (128K) | a 128K machine driving the AY |
| `beeper_demo.tap` | a real-time tape load: the EAR input, edge by edge |
| Layer 2 320×256 and 640×256, LoRes, tilemap, sprite scaling, 512 colours, stencil, palette | one program per video mode (`test/00regression/nex/`) |
| test10tilemapper, test04tilemap, test03sprite | three of the dapr test programs |
| `dma_all.bin` (48K, `demo/dma_all`) | the zxnDMA's features, each pass checked by the program itself — see below |
| NXModPlayer | the Next's MOD player playing a MOD: Paula emulation on the DACs, driven by CTC interrupts in hardware IM2 mode, about 34,000 a second |
| GUI, offscreen | 150 frames of parallax in the Qt GUI on Qt's offscreen platform, at real speed and `--silent`, one composited frame handed to the display per tick, so the frontend's frame and paint paths are profiled too |

`demo/dma_all` drives the DMA through both ports (ZXN `0x6B` and Z80-DMA
`0x0B`), all three directions (memory to memory, memory to port, port to
memory), A to B and B to A, incrementing, decrementing and fixed addresses on
both sides, continuous, burst and byte mode, a prescaled burst to the SpecDrum
DAC with auto-restart, CONTINUE, the timing bytes and the R6 commands, and it
reads the status, the counter and the addresses back. It compares every
result with what `dma.vhd` says, and writes its signature to RAM only after a
pass in which everything matched. Two things are not covered: DMA
interrupts, which `dma.vhd` does not implement, and the mode latch on a read
of a DMA port, which `dma_test` checks instead.

Every fixture is in the repository except the MOD player. That one, and its MOD,
are on the SD image the training already needs (`/apps/audio/NXModPlayer` in
the 24.11 distribution), so nothing of the distribution is copied into the
repository. jnext has no way to pull a file out of the image from the command
line, and the player browses its MODs through NextZXOS anyway, so the run
launches it the way a user would: NextZXOS's Browser down to the `.nex`, then
the player's own Mod Browser to `mods/Jarresque.mod`, every key at a fixed
frame. That navigation is not profiled (see below); it saves a snapshot with
the MOD playing, and the profiled run plays 150 frames from it.

A key pressed at a fixed frame cannot tell whether the guest acted on it, and
a DMA program that stopped working would still run. So both of those runs save
a snapshot one frame before they exit, and the training **fails** unless the
MOD's own text, or `dma_all`'s signature, is in that RAM.

Seven titles are **held out**, never trained, so that they measure how well
the profile generalises: santaspressie, celeste, celeste2, odemo,
test02layer2, shift and nirvana. They are listed in the script's header.

### Why PGO trades between code paths

Without a profile, `-O3` guesses: gcc estimates branch probabilities and block
frequencies from static heuristics (`-fguess-branch-probability`) and spreads
its effort over the code by those estimates. `-fprofile-use` replaces the
guesses with measured counts, and turns on the optimisations that need them
(gcc's documentation lists, among others, `-fbranch-probabilities`,
`-fprofile-values`, `-funroll-loops`, `-fpeel-loops`, `-ftracer` and
`-fvpt`). That is where PGO's gain comes from.

Several of the decisions it then makes spend a limited room:

- **inlining** grows the code, and gcc caps the growth (`--param
  inline-unit-growth`, `large-function-growth`); the room goes to the call
  sites the profile calls hot;
- **code layout**: each branch's likely path is made the fall-through, and
  functions and blocks are split into hot and cold sections
  (`-freorder-blocks-and-partition`, `-freorder-functions`), which keeps the
  hot code dense in the instruction cache.

"Hot" is relative to the whole profile. In gcc with LTO — every gcc release
build here except the Flatpak — a block is hot if it belongs to the
most-executed 99% of all the counts in the profile (`--param
hot-bb-count-ws-permille`, default 990). Without LTO the test is relative to
the largest single count (`--param hot-bb-count-fraction`, default 10000).
(macOS builds with clang, whose thresholds are its own.)

So code that ran a lot in training gets that room, and code that ran only a
little falls outside the hot set and loses the treatment hot code gets: its
call sites get less of the inlining room, and its blocks are not laid out and
optimised as hot ones. A program that uses that code heavily can then run
slower than it would under a better-balanced profile. An earlier, unbalanced version of this training set did exactly that
to santaspressie, which is never trained: **2.3–2.7% slower** than the
previous PGO build, in two retrains — though still about 9% faster than the
build without PGO. Which title pays depends on the mix: with the full-length
runs the loser was test02layer2 instead (+1.0% to +2.8% over three retrains),
and cutting 100 frames from two runs moved the loss to santaspressie.

Code that the training never reaches at all is a different case: with
`-fprofile-partial-training` it is compiled as it would be without a profile
(next section).

### Keeping the training fair

- **Every run carries the same weight.** A run's weight in the merged profile
  is its total count, and the hot set is a share of the total, so one heavy run
  pushes the others' hot code out of it. Each run's length is therefore chosen
  so that its total is about 3 G counts. Measured on v1.0.76, 23 runs total
  2.94 to 3.11 G; the GUI run, the one exception, totals 2.41 G since it went
  `--silent` (it was 2.75 G with sound). `JNEXT_PGO_REPORT_WEIGHTS=1 tools/pgo-train.sh
  <instrumented jnext> <profile dir>` profiles each run into its own directory
  (`GCOV_PREFIX`), prints each run's total, and stops without writing a
  profile. It is gcc-only and needs `gcov-dump` beside the compiler.
- **One-off work is not profiled.** The MOD player's navigation through
  NextZXOS (16 G counts, more than five other runs together) and jnext's
  warm-start boot (the first NEX loaded on a Next records a 500-frame NextZXOS
  cold boot, once per SD image) run with their profile redirected into a
  scratch directory (`GCOV_PREFIX`; `LLVM_PROFILE_FILE` for clang).
- **Weighting by run length, not by tool.** gcc's own tool for this,
  `gcov-tool merge -w` (and `gcov-tool rewrite -n/-s` to normalise), does not
  work on jnext's profiles with gcc 16: rewriting and any merge weight above 1
  crash on the value-profile counters, and weights of 1 or less are ignored.
  Run length needs no tool, so it works with every toolchain the build uses:
  native gcc, MinGW under wine, the Flatpak SDK's gcc and macOS clang.
- **Broad, real software**, as in the table above, and **a held-out set** that
  is never trained.
- **The acceptance rule.** A change to the training set is accepted only if,
  measured with at least two retrains on each side against the current shipped
  PGO build, every held-out title is within retrain noise (the spread between
  the shipped build's own retrains) or faster, and every title is faster than
  the build without PGO.

**When you add a training workload,** size it to the same total with
`JNEXT_PGO_REPORT_WEIGHTS=1`, then re-measure the held-out set against the
shipped PGO build with the method in [Measuring a change](#measuring-a-change).

### Partial training

`-fprofile-partial-training` matters as much as the profile. Without it, gcc
optimises code the training never reached *for size*, as cold code, and the
debugger, rewind and most of the GUI would get slower than in a non-PGO build.
With it, untrained code is optimised exactly as it would be without PGO, and
only the trained paths get the profile's benefit.

### A profile problem fails the build

A binary labelled PGO is PGO in every unit, or there is no binary:

- a profile that does not match the source is an error in gcc already
  (`-Wcoverage-mismatch`);
- a translation unit with **no** profile is made an error by
  `-Werror=missing-profile` — except for the library members the instrumented
  link never pulled in (the SDL frontend in a Qt build, spdlog's unused sinks),
  which cannot have one. The instrumented link writes a link map;
  `tools/pgo-train.sh` files it with the profile as `jnext.map`, and
  `cmake/JnextPgo.cmake` reads it to exempt exactly those members. The map
  decides, not the presence of a `.gcda`, so a naming mismatch still fails
  every unit `jnext` links;
- every training run must exit 0, the MOD run must prove the MOD loaded and
  `dma_all` that a fully checked pass completed, and a training that writes no
  profile at all fails;
- with no SD image and no way to provision one, the training fails and says
  which non-PGO target to use instead. No platform falls back to a non-PGO
  binary on its own.

clang (macOS) has no per-unit equivalent of `missing-profile`: there a stale
profile is an error, but a unit without one is not detected.

### Retraining only when something changed

Training is skipped when nothing that decides the profile changed. The
fingerprint `tools/pgo-train.sh` stores beside the profile covers the
instrumented binary — which changes with any source compiled into jnext and with
any flag, even a comment that moves line numbers — the compiler's version, the
configured flags, the script itself, every fixture, and the SD master's size and
date. A no-op `make gui-release` takes about a second. A retrain rebuilds the
optimised tree from clean.

### Reproducibility

gcc's value profiling records runtime *values*, so stack and heap addresses end
up in the profile, and spdlog re-reads the clock once per wall-clock second in
which it formats a message. The headless runs are made deterministic: address
space randomisation off (`setarch -R`, where the host allows it — docker's
default seccomp profile does not), one minimal environment for every run
(`env -i`; the stack layout depends on its size), a fixed-length run directory,
`--log-level off`, and the MOD player's snapshot stored uncompressed (its
manifest records the time and the run directory, and deflated those bytes
change the archive's layout, and with it the counts of the run that loads it).
With those, **two trainings without the GUI run produce byte-identical
profiles, whatever the load on the host**: checked with no added load and with
twelve extra busy loops on a twelve-CPU host, twice independently, and the
optimised binaries built from those profiles are byte-identical too.

The GUI run is the one part that follows the wall clock. It runs `--silent`:
with an audio device, the frame pacer runs one or two frames per tick
depending on how full the device's queue is, so how many frames skipped the
compositor depended on the host's load, and that moved the profile of the
renderer itself. Without one it runs exactly one composited frame per tick.
Between two full trainings, one with no added load and one under twelve busy
loops, 169 of the 176 `.gcda` files are byte-identical. The other seven differ
because of the GUI run's timing: `main.cpp`, `qt_app.cpp`, `main_window.cpp`,
`emulator_widget.cpp` and `debugger_manager.cpp` through the GUI's timer- and
event-loop-driven code (the one-second status-bar timer ticked 3 times in one
training and 9 in the other; 146 and 140 of the 150 frames were painted,
the rest coalesced by Qt); `spdlog.cpp` through one call count; and
`i2c.cpp` through a single value-profile counter (an `ior` of pointer
alignment) that records a heap address the GUI run allocates at a
timing-dependent moment — its execution counts are identical. Through LTO those
differences still change some cold GUI, debugger and snapshot code, so the
default build is not byte-reproducible; no CPU, memory, video, audio, DMA or
Copper function changes size. `JNEXT_PGO_NO_GUI=1` leaves the GUI run out, and the build is then
reproducible. The GUI run is kept by default because it is worth it: in GH
#297, without it, the Qt binary's CPU saving at 100% speed was 4–6 points
smaller (measured when the run still had sound).

### Per platform

| Platform | Compiler | Trained | Extra gate |
|---|---|---|---|
| `gui-release` (Linux) | gcc | natively, with the GUI run | `make fuse-pgo`, a prerequisite of `make regression`: the FUSE Z80 suite against the PGO tree's CPU core |
| rpm, debs (Fedora; Ubuntu 24.04, 26.04) | gcc | where the package is built (release.yml: that distribution's container), with the GUI run | — |
| Windows (x64 Qt6, x64 Qt5, i686 Qt5) | MinGW gcc | under **wine**, headless only | the FUSE Z80 suite under wine against the optimised tree, all 1356 cases |
| Flatpak | the KDE SDK's gcc, LTO off | inside the build sandbox, with the GUI run | the FUSE Z80 suite against the optimised CPU core, before the bundle is installed |
| macOS | AppleClang | on the macOS runner; the profile is merged with `llvm-profdata` (Xcode's first) | — |

Windows trains headless only because, in a container with no display, the Qt
executable under wine reaches its automatic exit and then never terminates;
partial training keeps the untrained GUI code as fast as without PGO. Each
Windows PGO build also refreshes its wine prefix (`wineboot -u`), because a
prefix created while the host's wine was broken stays broken after wine is
fixed. `WIN_PGO=0`, or `make win-release-non-pgo` for the x64 leg, builds
without PGO and without wine.

The two FUSE gates exist for the same reason. The Flatpak's toolchain has
already miscompiled jnext once, under LTO, and the Windows build is a different
toolchain with LTO and PGO together. The CPU core is the part whose
miscompilation would be quietest and most damaging, so both builds prove it
before they ship. [5.3](03-packaging-and-release.md) has the packaging detail.

## ccache

`CMakeLists.txt` uses ccache as the compiler launcher when it is installed
(`USE_CCACHE`, default ON). It is what makes the project's clean-rebuild
discipline affordable: a clean rebuild of the non-PGO Release tree plus
`build/` drops from roughly 65 s to 8 s on a warm cache, and reverting a change
to rebuild — the reviewer's core move — is a pure cache hit.

It **never** caches a profile-use compile. ccache looks for the `.gcda` next to
the object's path, while gcc 16 writes mangled names (`dir#sub#file.gcda`), so
ccache finds no profile, logs "No profile data file found", and counts the
compile as uncacheable. That is safe — never a stale object — but it means the
optimised tree is always a cold build: a warm `make gui-release` after a source
change costs the training plus about a minute of compiling and linking, and
retraining with unchanged sources does not hit either. The instrumented tree is
cached normally.

Give ccache room once per machine; the 5 GB default thrashes on this tree:

```console
$ ccache -M 20G
```

## Measuring a change

**Which binary.** Never `build/jnext`: it is RelWithDebInfo — optimised, but
without LTO and with frame pointers kept — so its numbers say nothing about
what ships. Then:

- to compare two code changes, use `build/gui-release-non-pgo/jnext`
  (`make gui-release-non-pgo`). Its speed does not move when a profile is
  retrained, so a difference is the code's;
- to ask how fast the shipped binary is, use `build/gui-release/jnext`.

**`make bench`.** `test/bench/bench.sh` runs the five canonical workloads
(boot-48k, boot-nextzxos, copper-demo, beast, bifrost) through
`jnext --benchmark N`, which runs *N* frames headless and uncapped and prints a
machine-readable `BENCH` line. It takes the median of five runs, pins them with
`taskset` to the fastest core class (read from `scaling_max_freq`, never
hard-coded), marks a workload VOID when the spread exceeds 5%, reports
T-states per second as the primary metric, and writes everything to
`test/bench/baseline-<sha>.txt`. `make bench` measures the non-PGO build;
`make bench BENCH_BUILD=gui-release` measures the PGO one.

**Interleaved, paired rounds.** `make bench` measures one binary at a time,
which is fine for watching one build drift and wrong for comparing two: host
load that changes between the two sessions reads as a difference between the
binaries. To compare binaries, run them **interleaved**:

- in every round, run each workload once per variant, back to back on the same
  core (`taskset -c N`);
- rotate the order of the variants each round, so none is always first;
- give every run its own reflinked clone of the SD master, so no run sees
  another's writes;
- wrap each run in `perf stat -e instructions:u,cycles:u`. Cycles are less
  sensitive to frequency changes than wall time, and instructions say whether
  the code or only its speed changed;
- take each variant's ratio to the baseline **within** each round, then report
  the median and IQR of those ratios over the rounds.

A slow drift then hits every variant in a round equally and cancels in the
ratio. GH #294, GH #297 and GH #298 measured this way, with scripts that are not
committed; `doc/analysis/CLANG-LLVM-EVALUATION.md` §3 records the method as GH
#294 ran it. The one committed interleaved harness, `test/bench/ab-hotlatch.sh`
(`make bench-hotlatch`), does only part of it: it interleaves the variants on
one core, but in a fixed order, from one shared SD clone, without `perf stat`,
and it reports each variant's median T-states per second against the baseline
rather than per-round ratios.

**Host load.** Read `/proc/loadavg` before you start and record it. A busy host
does not inflate a spread — it depresses every run equally — so neither a
median nor a VOID check can see it. `bench.sh` warns above a load of 1.5, and
interleaving protects a comparison much better than a single session, but
neither makes a loaded host quiet: a few minutes of somebody else's build can
move a ratio by several points.

**Profiling.** `perf record -g` needs frame pointers, which only the
RelWithDebInfo build keeps (they cost 5.8% in Release, so Release does not);
on a Release binary use `perf record --call-graph dwarf`.

## What has been measured

The numbers below are each from their own issue, on the development host; the
detail lives in the documents named.

- **Task 27 (2026-07): LTO.** Turning LTO on for Release — a 17-line CMake
  change — gave **+34.5%** on boot-nextzxos and +17.4% on bifrost, more than
  all the hand-tuned hot spots of that task together; the task as a whole took
  boot-nextzxos from 57 to 161 million T-states per second, about 2.8×
  (`doc/design/TASK27-FINAL-OPTIMIZATION-REPORT.md`).
- **GH #294 (2026-10): clang versus gcc — stay on gcc.** With LTO on both
  sides, clang 22 was slower than gcc 16 on every workload, by 5–16% in fps;
  clang with PGO beat plain gcc with LTO, but gcc with PGO beat clang with PGO
  by 8–29%. The same evaluation found gcc PGO to be the bigger lever
  (`doc/analysis/CLANG-LLVM-EVALUATION.md`).
- **GH #297 (v1.0.73): gcc PGO.** Against the same build without PGO,
  headless: **+13% to +22%** on titles the training never saw and +15% to +31%
  on the trained ones. The Qt binary at 100% speed uses 12–20% less CPU. Three
  independent retrains differed by at most 0.015 in the speed ratio.
- **GH #298 (v1.0.74): Windows as Release builds.** Under wine, the x64
  executable with Release + LTO + PGO is **+13% to +55%** faster than the
  RelWithDebInfo + PGO build it replaced; LTO alone already matched or beat the
  old PGO build, and PGO adds 7–31% on top of LTO, where it had added only
  2–8% without it. Wine is not Windows, so these are indicative.
- **The balanced, broad training set (GH #297 follow-up, measured on
  v1.0.76).** Against the shipped v1.0.76 PGO build, minimum cycles over six
  interleaved rounds, two retrains of the new set — one with no added load
  (the host was busy with other work, 1-minute load 7–18), one under twelve
  extra busy loops — and three of the shipped one: santaspressie -2.2 to -2.5%
  (-2.1 to -3.2% over 1200 frames), celeste -0.6 to -1.7%, celeste2 -0.7 to
  -1.7%, shift -1.6 to -1.9%, odemo +1.1 to +1.3%, test02layer2 +0.1 to +2.2%,
  nirvana -0.4% (no added load) and +0.8% (busy loops). odemo and test02layer2
  are within the shipped build's own retrain spread on them (2.3%, and 3.6% /
  5.6% over 400 / 1200 frames); nirvana's +0.8% is 0.1 point above its 0.7%
  spread. A second, independent session — three retrains of the new set, one
  under twelve busy loops, on a quieter host — measured every held-out title
  between -3.4% and +0.1% (nirvana -0.2% to +0.1%, test02layer2 -0.3% to
  -0.7%). The MOD player plays 5.1 to 5.3% faster. The build without PGO needs
  14.9 to 43.5% more cycles than the new one on each title. Training takes
  about a minute, where it took about 30 s. Before the balancing, the same
  additions made test02layer2 1.0 to 2.8% slower (three retrains), or
  santaspressie 2.3 to 2.7% with two runs shortened; and before the GUI run
  went `--silent`, a retrain made under load moved test02layer2 by about a
  point.
