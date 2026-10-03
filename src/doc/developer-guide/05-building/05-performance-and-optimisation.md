# 5.5 Performance and optimisation

JNEXT's speed comes from the compiler at least as much as from its source. Two
build mechanisms decide how fast the shipped binary is — link-time optimisation
and profile-guided optimisation — and a third, ccache, decides how long you wait
for it. This page describes all three as the code has them today, then how to
measure a change, then what has been measured so far.

## Link-time optimisation

The emulator is fourteen per-subsystem static libraries. That split is good for
the code and bad for the compiler: every hot call that crosses a library — the
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
separate directories that both stay warm in ccache.

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
path, once, rather than to run a lot of anything. Adding a long run of a path
that is already trained costs build time and buys nothing.

`tools/pgo-train.sh` runs, headless and with the RTC pinned:

| Run | What it adds |
|---|---|
| boot-48k, boot-nextzxos, copper-demo, beast, bifrost | the `make bench` workloads, at half their frames |
| parallax, trainyard-express | two more Next games; both use the DMA, from memory to the sprite ports |
| `rzx_dma_demo.bin` (48K) | the DMA reading a port into memory |
| `dmaloop.bin` (48K) | the DMA copying memory to memory, back to back at 28 MHz — GH #294's workload |
| `ay_envelope_sweep.bin` (128K) | a 128K machine driving the AY |
| beanbros | a Next game with AY music |
| `beeper_demo.tap` | a real-time tape load: the EAR input, edge by edge |
| NXModPlayer | the Next's MOD player playing a MOD: Paula emulation on the DACs, driven by CTC interrupts in hardware IM2 mode, about 34,000 a second |
| GUI, offscreen | 150 frames of parallax in the Qt GUI on Qt's offscreen platform, at real speed with SDL's dummy audio driver, so the frontend's frame, paint and audio paths are profiled too |

Every fixture is in the repository except the MOD player. That one, and its MOD,
are on the SD image the training already needs (`/apps/audio/NXModPlayer` in
the 24.11 distribution), so nothing of the distribution is copied into the
repository. jnext has no way to pull a file out of the image from the command
line, and the player browses its MODs through NextZXOS anyway, so the run
launches it the way a user would: NextZXOS's Browser down to the `.nex`, then
the player's own Mod Browser to `mods/Jarresque.mod`, every key at a fixed
frame, then four seconds of play.

A key pressed at a fixed frame cannot tell whether the guest acted on it. So the
run also saves a snapshot one frame before its exit, and the training **fails**
unless the MOD's own text is in that RAM. A changed image or a slower boot
cannot quietly leave the player out of the profile.

The titles the GH #294 and GH #297 measurements used as *untrained* — celeste,
santaspressie, odemo, test02layer2 — are kept out of the set on purpose, so that they keep measuring how well the profile generalises.

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
- every training run must exit 0, the MOD run must prove the MOD loaded, and a
  training that writes no profile at all fails;
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
and `--log-level off`. With those, **two trainings without the GUI run produce
byte-identical profiles and byte-identical binaries**.

The GUI run is paced in real time, and its counts move by well under 1% from one
training to the next, so the shipped build is not byte-reproducible.
`JNEXT_PGO_NO_GUI=1` leaves the GUI run out, and the build is then reproducible.
The GUI run is kept by default because it is worth it: without it, the Qt
binary's CPU saving at 100% speed is 4–6 points smaller.

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
ratio. `test/bench/ab-hotlatch.sh` (`make bench-hotlatch`) is a committed
harness built this way, and GH #294, GH #297 and GH #298 measured with the same
method.

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
- **The MOD player and memory-to-memory DMA in the training set (GH #297
  follow-up).** Training went from about 28 s to about 38 s: 9 s for the MOD
  player run, 1 s for the DMA copy loop. Against the v1.0.74 PGO build, ten
  interleaved rounds: the MOD player playing **+7%** fps and the DMA copy loop
  **+22%**, while boot-48k, beast, celeste and santaspressie moved by less than
  1% either way — the size of the difference between two retrains. Against the
  build without PGO, the MOD player is now +21% where it was +13%.
