# 5.2 Build configurations

The `make` targets already pass the right options for you, so this table
matters mainly when you invoke CMake directly.

| Option | Default | Meaning |
|---|---|---|
| `ENABLE_QT_UI` | **OFF** | Build the Qt6 native UI. `make gui-*` turns it on; a plain `cmake` without it gives the SDL frontend |
| `ENABLE_DEBUGGER` | **ON** | Include the Qt debugger UI. It opens from the Qt main window, so it is only reachable when `ENABLE_QT_UI` is also on |
| `ENABLE_TESTS` | ON | Build the unit-test binaries |
| `USE_CCACHE` | ON | Use ccache as the compiler launcher when it is found; a no-op if it is not |
| `JNEXT_ENABLE_LTO` | ON | LTO/IPO for Release builds. Flipped OFF in exactly one place — see below |
| `JNEXT_FORCE_QT5` | OFF | Build the GUI and debugger against Qt 5 instead of Qt 6 (the legacy Windows legs only) |
| `CYCLE_ACCURATE` | OFF | 28 MHz cycle-accurate mode |
| `STATIC_BUILD` | OFF | Link statically; needs static SDL3/Qt6 builds, which distribution packages are not |
| `MACOS_APP_BUNDLE` | OFF | Build a relocatable `jnext.app` bundle (macOS packaging only) |
| `GIT_SUBMODULE` | ON | Run `git submodule update --init --recursive` at configure time |

## The combinations are gated, not assumed

`ENABLE_QT_UI` and `ENABLE_DEBUGGER` between them give four combinations, and
only one of those is the one you build by hand. `make build-matrix` builds all
four and fails if any of them breaks.

That target exists because of a specific and rather subtle failure mode. CMake
derives static-link *order* from the dependency graph you declare, which means a
target that under-declares its dependencies still links successfully whenever
some other library happens to pull them in at a usable position. The bug is
real but invisible, and it shipped twice: `jnext_core` never declared its
subsystem libraries and `jnext_platform` never declared `jnext_core`, and in
both cases the default combination — both options ON — hid the problem, so the
link only broke for someone building without the debugger. Link rot of this kind
appears at build time and only in the combinations the default build does not
exercise, so the only way to find it is to build them all. The matrix keeps
going after a failure and reports every broken combination rather than stopping
at the first, because knowing whether three are broken or only one is the
difference between a single missing edge and a wrong graph.

Building is not the same as running, though, and for a long time it was all the
matrix did: every configuration was proved to link, and only the default one ever
had a suite run in it, so a suite could stay red in a supported configuration
indefinitely — which is what GH #273 found. Two configurations now run their
suites: the default Qt+debugger build through `make unit-test`, and the SDL-only
build through `make unit-test-sdl`. The other two, Qt without the debugger and
SDL with it, are not used in practice and remain build-only. CI runs both
targets.

## Where each target builds

There is one scheme, `build/<variant>-<config>`, and everything lives under
`build/`: `build/sdl-release`, `build/gui-release`, `build/win-release`,
`build/rpm-release`, `build/deb-release`, `build/mac-release`,
`build/flatpak-release`, and so on.

`build/` itself is the canonical development tree, holding `build/jnext` plus
every test binary. `make unit-test-build` configures it, and it does so
deliberately with `ENABLE_QT_UI=ON` and `ENABLE_DEBUGGER=ON` rather than
inheriting whatever the defaults happen to be. The reason is that `ENABLE_QT_UI`
defaults OFF, so an unconfigured `build/` would silently produce an SDL binary
with no main window — and the next person to check something in the GUI would
find no window at all and reasonably conclude their own change had broken it.
For the same reason the target *refuses* to build on a `build/` that someone
has configured by hand with either flag off, rather than handing back a binary
that is not what it claims to be. If you hit that refusal, `make clean` and
retry.

## Dev build versus release build

With no `-DCMAKE_BUILD_TYPE` given, CMake here defaults to **RelWithDebInfo**
instead of to the empty build type. An unoptimised `build/jnext` measured 5.8×
slower than Release, which made it a trap for anyone benchmarking anything.
RelWithDebInfo additionally keeps frame pointers, so `perf record -g` produces
usable call graphs from it. Release does not keep them: frame pointers were
measured at a 5.8% cost there, and that is an unacceptable permanent tax on the
binary users actually run.

Hence the rule that follows from those two facts: **any performance measurement
or benchmark uses a Release build**, never `build/jnext` — and which Release
build depends on the question. To compare two code changes, use
`build/gui-release-non-pgo/jnext` (`make gui-release-non-pgo`; `make bench`
uses it): its speed does not move when a PGO profile is retrained. To ask how
fast the shipped binary is, use `build/gui-release/jnext`, the PGO build
(`make bench BENCH_BUILD=gui-release`). When you do need to profile a release
binary, use `perf record --call-graph dwarf`.

LTO is enabled for **Release only**, through
`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`, and it is guarded by
`check_ipo_supported()` so that a toolchain without working LTO simply builds as
it did before. It earns its keep here because the emulator is split into
fourteen per-subsystem static libraries, and that split makes every hot
cross-library call — the CPU into `Mmu::read`, the CPU into `PortDispatch` —
un-inlinable at compile time.

`JNEXT_ENABLE_LTO=OFF` exists for exactly one build, the Flatpak one: the
`org.kde.Sdk` GCC miscompiles the QApplication init path under whole-program
LTO and segfaults at GUI launch. That was bisected to the LTO *process* in that
toolchain rather than to any single translation unit or transform, which is why
nothing short of disabling it helps. Every other build keeps LTO on —
including the Windows executables, which is why every `win-*-release` target
passes `-DCMAKE_BUILD_TYPE=Release` explicitly (`WIN_BUILD_TYPE` in the
Makefile). Before GH #298 they passed no build type, fell back to
RelWithDebInfo, and shipped without LTO.

## Profile-guided optimisation

`make gui-release` — and every package, see
[5.3](03-packaging-and-release.md) — is a gcc **PGO** build (GH #297). Measured
headless on the development host, it runs 13–22% faster than the same build
without PGO on titles the training never saw, and 15–31% on the ones it did.
`cmake/JnextPgo.cmake` adds the flags under the CMake option `JNEXT_PGO`
(`GENERATE` or `USE`, with the profile in `JNEXT_PGO_DIR`), and the build runs
in three trees:

1. `build/gui-release-pgo-gen`, instrumented with `-fprofile-generate`;
2. `tools/pgo-train.sh`, about half a minute of training with that binary — the
   `make bench` workloads plus parallax, trainyard-express, a 128K machine
   driving the AY, a game with AY music, a real-time tape load and a few
   seconds of the GUI on Qt's offscreen platform — which writes one `.gcda` per
   translation unit into `build/gui-release-pgo-profile`;
3. `build/gui-release`, compiled with `-fprofile-use -fprofile-partial-training`
   (code the training never reaches is optimised normally, not for size).

The flags go only on the sources of `jnext` and of the in-tree libraries it
links, never on generated code (Qt's moc output: gcc's profile checksum
includes the file's path, which differs between the two trees) and never on
other executables. **A profile problem fails the build**: a mismatched profile
is already an error in gcc, and a missing one is made an error for every
library member the instrumented link actually pulled in — the link map of the
instrumented build, filed with the profile, says which members those are, so
the SDL frontend in a Qt build, say, is not asked for a profile it can never
have.

Training is skipped when nothing that decides the profile changed: the
instrumented binary (which changes with any source or flag), the compiler, the
training script and its inputs. The training needs the NextZXOS SD image,
because the ROMs come from it, and provisions it through jnext's own download
when it is missing. A retrain forces a clean rebuild of `build/gui-release`;
ccache cannot help there, because it does not find gcc's mangled `.gcda` names
and treats every `-fprofile-use` compile as uncacheable — safe, never stale,
never a hit.

The headless training is **byte-reproducible**: with address-space
randomisation off, one fixed minimal environment and logging off, two trainings
give identical profiles and identical binaries. The few seconds of real-time
GUI do not, so the shipped build is not; `JNEXT_PGO_NO_GUI=1` leaves them out.

## ccache

`CMakeLists.txt` finds ccache and uses it as `CMAKE_{C,CXX}_COMPILER_LAUNCHER`.
This is what makes the project's mandatory clean-rebuild discipline affordable:
a clean rebuild of `gui-release` plus `build/` drops from roughly 65 s to 8 s on
a warm cache. Reverting a change and rebuilding — which is the core reviewer
move, and one this project asks for often — is a pure cache hit, because the
source is byte-identical to a state that has already been compiled once.

Give it room, once per machine; the 5 GB default thrashes on a tree this size:

```console
$ ccache -M 20G
```

That is a user-level setting rather than repository state, so it has to be
re-applied on any new machine.
