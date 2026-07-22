# Windows Build

jnext's Windows executable is **cross-compiled from Linux with Fedora's MinGW
toolchain**. There is no Windows build host, and nothing here is built on
Windows — CI included.

> **History.** This file used to be a step-by-step porting *proposal*: create a
> `docker/Dockerfile.windows`, apply two code changes by hand, optionally add a
> Makefile target. The port has since landed, and none of that described how the
> build actually works — the Dockerfile was never created, the Makefile target is
> not optional, and both of the "required code changes" have since been resolved
> in the code (one fixed, one made obsolete; see Known gaps below). Rewritten to
> describe the build that exists.

## Building it

```sh
make win-release     # cross-compile jnext.exe + bundle its runtime DLLs
make package-win     # the above, plus the distributable ZIP
```

`win-release` checks the toolchain first and names the missing packages rather
than failing inside CMake:

```sh
sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-qt6-qtbase \
    mingw64-sdl2-compat mingw64-curl mingw64-openssl mingw64-zlib \
    mingw64-libpng mingw64-winpthreads
```

`mingw64-filesystem` supplies `mingw64-cmake`; the **native** `qt6-qtbase-devel`
supplies `moc`/`rcc`/`uic`, which run on the build host rather than the target.

The build is configured `-DENABLE_QT_UI=ON -DENABLE_TESTS=OFF` — the test suites
are not cross-built, because they are run on Linux.

`packaging/windows/bundle-dlls.sh` then copies the Qt6/SDL2/SDL3 runtime DLLs
**and the `platforms/qwindows.dll` plugin** next to the executable. Both halves
matter: `jnext.exe` alone cannot start (no `Qt6Core.dll`), and with the DLLs but
without the platform plugin it still cannot open a window.

## CI

`.github/workflows/release.yml`'s `windows` job runs **the same `make
package-win`** inside a `fedora:44` container — the identical recipe a developer
runs locally, not a CI-only reimplementation. It has run green on a real hosted
runner and shipped `jnext-0.98.19-windows-x64.zip`, confirmed working on real
Windows hardware.

## Windows-specific code

There is very little, which is the point:

- **`CMakeLists.txt:246-247`** — `-Wl,--stack,16777216`, `WIN32` only. An early
  startup path (before `main`; same `__chkstk` address for `--version`,
  `--help` and `--headless`) reserves a >2 MB frame that overflows MinGW's 2 MB
  default. The 16 MB reserve fully resolves the crash; **the frame itself was
  never root-caused** and no >256 KB array was found by grep. Tracked in
  [EMULATOR-DESIGN-PLAN.md](EMULATOR-DESIGN-PLAN.md) §11.
- The GUI subsystem is selected so no console window appears alongside the app.

## Known gaps on Windows

This is a runtime bug, not a build problem — the build path itself has nothing
outstanding:

- [#62](https://github.com/jorgegv/jnext/issues/62) — **non-ASCII file paths
  fail**, because UTF-8 `std::string`s are handed to the narrow CRT. This is
  the only one outstanding in the code.

The old "Step 3.2 — FFmpeg shell redirection" is **done**, not outstanding:
`src/core/video_recorder.cpp` gained a `CmdStyle` abstraction with per-platform
quoting and a `win_run_hidden()` runner in `344aa061`, shipped in v0.98.80.
[GH #56](https://github.com/jorgegv/jnext/issues/56) is still open on the
tracker pending confirmation on real Windows hardware, but the code change it
asks for has landed — describe the code, not the tracker.

The old "Step 3.1 — ROM directory default" is obsolete rather than outstanding:
ROMs now come from the SD image (see below), so there is no `/usr/share/fuse`
default left to guard.

## Portability notes

- All emulation core code is pure C++17 with no platform dependencies.
- `std::filesystem` is used throughout (fully portable since C++17).
- POSIX headers are used in two places and both are handled: `anon_mem.h:27`
  includes `<sys/mman.h>` behind a `_WIN32` guard that selects the Win32 path
  instead, and `sdcard_provisioner.cpp:18-19` includes `<sys/stat.h>` /
  `<sys/types.h>`, which MinGW provides. (The pre-rewrite version of this file
  claimed no `sys/*` header was used at all; it was wrong, and inheriting that
  sentence unchecked is how it survived the rewrite.)
- Signal handlers use only `SIGABRT`, `SIGFPE`, `SIGSEGV` — standard C
  everywhere.
- The Makefile and the regression suite (`test/00regression/regression.sh`) are
  bash-based and are not run on Windows. The Windows artifact is validated by
  being built and published, and by use on real hardware — not by running the
  suites there.

## SD-card image

The SD image is the canonical source of every ROM jnext needs at runtime except
the FPGA boot ROM, which is silicon-baked into the binary.

Nothing under `roms/` is involved: it holds only `nextboot.rom`, and the SD
images that used to sit beside it were removed
([#75](https://github.com/jorgegv/jnext/issues/75)). jnext provisions its own
image under the user's home directory on first run, exactly as on Linux — pass
`--sdcard` only to override that.
