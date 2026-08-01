# 5.2 Build configurations

The `make` targets pass the right options for you. Use these when invoking
CMake directly.

| Option | Default | Meaning |
|---|---|---|
| `ENABLE_QT_UI` | **OFF** | Build the Qt6 native UI. `make gui-*` turns it on; a plain `cmake` without it gives the SDL frontend |
| `ENABLE_DEBUGGER` | **ON** | Include the Qt debugger UI. It opens from the Qt main window, so it is only reachable when `ENABLE_QT_UI` is also on |
| `ENABLE_TESTS` | ON | Build the unit-test binaries |
| `USE_CCACHE` | ON | Use ccache as the compiler launcher when it is found; a no-op if it is not |
| `JNEXT_ENABLE_LTO` | ON | LTO/IPO for Release builds. Flipped OFF in exactly one place — see below |
| `JNEXT_FORCE_QT5` | OFF | Build the GUI and debugger against Qt 5.15 instead of Qt 6 (the legacy Windows legs only) |
| `CYCLE_ACCURATE` | OFF | 28 MHz cycle-accurate mode |
| `STATIC_BUILD` | OFF | Link statically; needs static SDL2/Qt6 builds, which distribution packages are not |
| `MACOS_APP_BUNDLE` | OFF | Build a relocatable `jnext.app` bundle (macOS packaging only) |
| `GIT_SUBMODULE` | ON | Run `git submodule update --init --recursive` at configure time |

## The combinations are gated, not assumed

`ENABLE_QT_UI` and `ENABLE_DEBUGGER` give four combinations, and only one of
them is what you build by hand. `make build-matrix` builds all four and fails
if any breaks.

This exists because of a specific failure mode. CMake derives static-link
*order* from the declared dependency graph, so a target that under-declares its
dependencies still links whenever some other library happens to pull them in at
a usable position. It shipped twice: `jnext_core` never declared its subsystem
libraries and `jnext_platform` never declared `jnext_core`, and the default
combination (both options ON) hid both — the link only broke for someone
building without the debugger. Link rot appears at build time and only in the
combinations the default build does not exercise, so the only way to find it is
to build them all. The matrix keeps going after a failure and reports every
broken combination, because knowing whether three are broken or one is the
difference between a missing edge and a wrong graph.

## Where each target builds

One scheme, `build/<variant>-<config>`, everything under `build/`:
`build/sdl-release`, `build/gui-release`, `build/win-release`,
`build/rpm-release`, `build/deb-release`, `build/mac-release`,
`build/flatpak-release`, and so on.

`build/` itself is the canonical development tree — `build/jnext` plus every
test binary. `make unit-test-build` configures it, and it configures it
deliberately with `ENABLE_QT_UI=ON` and `ENABLE_DEBUGGER=ON`. That is not a
default it inherits: `ENABLE_QT_UI` defaults OFF, so an unconfigured `build/`
would silently produce an SDL binary with no main window, and the next person
to check the GUI would find no window and conclude their change had broken it.
The target also *refuses* to build on a `build/` that someone configured by
hand with either flag off, rather than handing back a binary that is not what
it claims to be. If that happens, `make clean` and retry.

## Dev build versus release build

With no `-DCMAKE_BUILD_TYPE`, CMake here defaults to **RelWithDebInfo** rather
than to the empty build type — an unoptimised `build/jnext` was about 5×
slower than Release and a trap for anyone measuring anything. RelWithDebInfo
additionally keeps frame pointers, so `perf record -g` produces usable call
graphs. Release does not: frame pointers were measured at a 5.8% cost there,
which is an unacceptable permanent tax on the user-facing binary.

Hence the rule: **any performance measurement or benchmark uses
`build/gui-release/jnext`** (`make gui-release`), never `build/jnext`. To
profile the release binary, use `perf record --call-graph dwarf`.

LTO is enabled for **Release only**, via
`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`, and guarded by
`check_ipo_supported()` so a toolchain without working LTO builds exactly as
before. It matters here because the emulator is split into fourteen
per-subsystem static libraries, which makes every hot cross-library call — the
CPU into `Mmu::read`, the CPU into `PortDispatch` — un-inlinable at compile
time. `JNEXT_ENABLE_LTO=OFF` exists for the Flatpak build alone: the
`org.kde.Sdk` GCC miscompiles the QApplication init path under whole-program
LTO and segfaults at GUI launch. That was bisected to the LTO *process* in that
toolchain rather than to any single unit or transform, so nothing short of
disabling it helps. Every other build keeps LTO.

## ccache

`CMakeLists.txt` finds ccache and uses it as `CMAKE_{C,CXX}_COMPILER_LAUNCHER`.
This is what makes the project's mandatory clean-rebuild discipline affordable:
a clean rebuild of `gui-release` plus `build/` drops from roughly 65 s to 8 s
on a warm cache. Reverting a change and rebuilding — the core reviewer move —
is a pure cache hit, because the source is byte-identical to a state already
compiled.

Give it room once per machine; the 5 GB default thrashes on a tree this size:

```console
$ ccache -M 20G
```

That is a user-level setting, not repository state, so re-apply it on any new
machine.
