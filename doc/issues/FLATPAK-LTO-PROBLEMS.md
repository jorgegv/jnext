# Flatpak build: LTO disabled (toolchain miscompilation)

## Summary

The Flatpak build of jnext is compiled **without LTO/IPO**, unlike every other
build (native, DEB, RPM, `gui-release`), which keep it. This is a deliberate
workaround: the Flatpak runtime's GCC (`org.kde.Sdk`) **miscompiles the Qt
`QApplication` startup path under whole-program LTO**, producing an immediate
`SIGSEGV` on GUI launch. Disabling LTO is the only known fix.

It is scoped to the Flatpak alone via the `JNEXT_ENABLE_LTO` CMake option
(default `ON`); only `packaging/flatpak/io.github.zxjogv.jnext.yml` sets
`-DJNEXT_ENABLE_LTO=OFF`.

## Symptom

The published Flatpak crashed (signal 11) on real-display GUI launch, right
after `sdcard: using default image`:

```
[emulator] [info] sdcard: using default image …
Detected locale "C" … switched to "C.UTF-8" …
[emulator] [critical] signal 11 received
```

`--version`, `--headless`, and `QT_QPA_PLATFORM=offscreen` all work — they never
construct a `QApplication`. The crash is inside the `QApplication` constructor.

## Root cause

Symbolized `gdb` (against `org.kde.Sdk.Debug`) puts the fault inside Qt's own
platform init, **not** jnext code:

```
QApplicationPrivate::init
  → QGuiApplicationPrivate::init
    → QCoreApplicationPrivate::init → createPlatformIntegration
      → QWaylandDisplay::initialize → wl_display_roundtrip
        → QWaylandScreen::maybeInitialize → handleScreenInitialized
          → QGuiApplication::screenAdded            (emitted SYNCHRONOUSLY)
            → doActivate(sender = 0x0)   ← qApp is still nullptr → 💥
```

A `wl_output` finishes initializing **during the first roundtrip**, inside the
`QApplication` constructor, *before* Qt assigns `qApp` — so a screen-added signal
is delivered with `qApp == nullptr`. The same crash occurs on xcb
(`QXcbConnection::initializeScreens`).

This is a latent Qt/Wayland ordering hazard that only fires when the surrounding
binary is built a particular way. On the **native host GCC** it never fires
(with or without LTO). On the **Flatpak `org.kde.Sdk` GCC** it fires **only when
LTO is enabled**.

### What was ruled out (all by reproduction in the Flatpak sandbox)

| Hypothesis | Result |
|------------|--------|
| Qt version (built clean on 6.8 **and** 6.11) | both crash — not an upstream Qt bug fixed later |
| SD-card / emulator init | `--headless`/offscreen boot the whole emulator fine |
| Any linked library (minimal `QApplication` linking jnext's entire lib set) | never crashes |
| Any pre-`QApplication` call (signal handlers, `ffmpeg` probe, `SDL_Init`, spdlog) | replicated, all fine |
| `QApplication` as the **first** statement in `main()` | still crashes ⇒ trigger runs *before* `main` (static init / build), not jnext's runtime |

### LTO narrowing (why it is not a single unit)

With a per-target LTO toggle, the Flatpak build was bisected (each step a full
build + install + GUI run):

| Attempt | LTO scope | Result |
|---------|-----------|--------|
| exclude `jnext_gui` + `jnext_debugger` | rest LTO | crash |
| LTO on `cpu`/`memory`/`port`/`core` + exe only | crash |
| LTO on `core` + exe only | crash |
| LTO on exe only (all libs opaque) | crash |
| LTO on all libs, exe excluded | crash |
| full LTO + `-fno-devirtualize -fno-devirtualize-speculatively -fno-ipa-icf` | crash |
| full LTO at `-O2` (instead of `-O3`) | crash |
| **LTO fully OFF** | **works** |

Both "only `main.cpp` under LTO" and "only the libraries under LTO" crash
independently; disabling the suspect IPA transforms (devirtualization, identical
code folding) or lowering the optimization level does not help. **The trigger is
the LTO process in that specific toolchain, not any single translation unit or
optimization.** ThinLTO (`-flto=thin`) is a Clang-only feature and the Flatpak
uses GCC, so it is not available without switching toolchains.

## The fix

`CMakeLists.txt`:

```cmake
option(JNEXT_ENABLE_LTO "Enable LTO/IPO for Release builds (Flatpak sets OFF)" ON)
...
if(JNEXT_IPO_SUPPORTED AND JNEXT_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
```

`packaging/flatpak/io.github.zxjogv.jnext.yml` (the only place it is turned off):

```yaml
config-opts:
  - -DJNEXT_ENABLE_LTO=OFF
```

## Cost

Flatpak-only, and only at turbo speeds. Headless `boot-next` benchmark in the
Flatpak sandbox:

| Build | fps | T-states/s |
|-------|-----|------------|
| LTO on  | ~171 | ~97 M |
| LTO off | ~117 | ~66 M |

That is a ~32 % drop in **maximum throughput**. Normal (100 %) emulation is far
below that ceiling and completely unaffected; the loss is only visible when
running the emulator at high turbo multipliers. Users who want maximum turbo
performance can install the native RPM/DEB packages, which keep LTO.

## If revisiting

- Re-test on a newer `org.kde.Sdk` runtime — the toolchain bug may be fixed
  upstream, at which point `-DJNEXT_ENABLE_LTO=OFF` can be dropped.
- Or build the Flatpak against a Clang-providing runtime and use `-flto=thin`.
- Reproduce with: `flatpak run --devel --command=sh io.github.zxjogv.jnext -c
  'gdb -q -batch -ex run -ex bt --args jnext'` (the sandbox has the display).
