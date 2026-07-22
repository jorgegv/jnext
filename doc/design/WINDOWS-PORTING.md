# Windows Build

jnext's Windows executable is **cross-compiled from Linux with Fedora's MinGW
toolchain**. There is no Windows build host, and nothing here is built on
Windows — CI included.

> **History.** This file used to be a step-by-step porting *proposal*: create a
> `docker/Dockerfile.windows`, apply two code changes by hand, optionally add a
> Makefile target. The port has since landed, and none of that described how the
> build actually works — the Dockerfile was never created, the Makefile target is
> not optional, and one of the two "required code changes" is still an open bug
> ([#56](https://github.com/jorgegv/jnext/issues/56)). Rewritten to describe the
> build that exists.

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

Both are runtime bugs, not build problems — the build path itself has nothing
outstanding:

- [#56](https://github.com/jorgegv/jnext/issues/56) — **MP4 recording cannot
  work.** `src/core/video_recorder.cpp` builds POSIX shell commands with no
  `_WIN32` guard: the probe is `system("ffmpeg -version >/dev/null 2>&1")`, and
  `cmd.exe` has no `/dev/null`, so `ffmpeg_available()` is always false and the
  menu item is permanently greyed out. Paths are also quoted POSIX-style, which
  `cmd.exe` does not strip. (This is what the old version of this document
  listed as "Step 3.2 — apply before building". It was never applied.)
- [#62](https://github.com/jorgegv/jnext/issues/62) — **non-ASCII file paths
  fail**, because UTF-8 `std::string`s are handed to the narrow CRT.

The old "Step 3.1 — ROM directory default" is obsolete rather than outstanding:
ROMs now come from the SD image (see below), so there is no `/usr/share/fuse`
default left to guard.

## Portability notes

- All emulation core code is pure C++17 with no platform dependencies.
- `std::filesystem` is used throughout (fully portable since C++17).
- No POSIX-specific headers (`unistd.h`, `sys/*`, `dirent.h`) are used —
  `video_recorder.cpp`'s shell strings above are the exception that proves it.
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
