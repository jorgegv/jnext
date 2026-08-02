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
| `STATIC_BUILD` | OFF | Link statically; needs static SDL2/Qt6 builds, which distribution packages are not |
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
instead of to the empty build type. An unoptimised `build/jnext` measured about
5× slower than Release, which made it a trap for anyone benchmarking anything.
RelWithDebInfo additionally keeps frame pointers, so `perf record -g` produces
usable call graphs from it. Release does not keep them: frame pointers were
measured at a 5.8% cost there, and that is an unacceptable permanent tax on the
binary users actually run.

Hence the rule that follows from those two facts: **any performance measurement
or benchmark uses `build/gui-release/jnext`** (`make gui-release`), never
`build/jnext`. When you do need to profile the release binary, use
`perf record --call-graph dwarf`.

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
nothing short of disabling it helps. Every other build keeps LTO on.

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
