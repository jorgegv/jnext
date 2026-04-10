# Windows Porting Guide

This document describes how to build jnext for Windows using MinGW cross-compilation from a Fedora Linux host.

## Approach

Cross-compile from Fedora using the `mingw64` toolchain. Fedora ships a ready-made CMake toolchain file at `/usr/share/mingw/toolchain-mingw64.cmake` that configures the `x86_64-w64-mingw32` cross-compiler and sets the sysroot to `/usr/x86_64-w64-mingw32/sys-root/mingw`.

All vendored libraries (spdlog, fuse-z80, zot) compile without changes. Three of the four system dependencies are packaged:

| Dependency | Status        | Package                  |
|------------|---------------|--------------------------|
| Qt6        | Packaged      | `mingw64-qt6-qtbase`     |
| libpng     | Packaged      | `mingw64-libpng`         |
| zlib       | Packaged      | `mingw64-zlib`           |
| SDL2       | Not packaged  | Build from source (below)|

## Step 1 — Install MinGW packages

```sh
sudo dnf install \
    mingw64-gcc-c++ \
    mingw64-qt6-qtbase \
    mingw64-libpng \
    mingw64-zlib
```

## Step 2 — Cross-build SDL2

The Fedora `mingw64-SDL2` package is only an SDL3 compatibility shim, not SDL2 proper. Build SDL2 once and install it into the MinGW sysroot:

```sh
git clone https://github.com/libsdl-org/SDL.git --branch SDL2 --depth 1
cmake -S SDL -B SDL/build-mingw \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw
sudo cmake --install SDL/build-mingw
```

This only needs to be done once per machine.

## Step 3 — Apply the two required code changes

### 3.1 ROM directory default

`src/core/emulator_config.h` hardcodes `/usr/share/fuse` as the default ROM path. Add a platform guard:

```cpp
#ifdef _WIN32
    std::string roms_directory = "./roms";
#else
    std::string roms_directory = "/usr/share/fuse";
#endif
```

On Windows, ROMs are expected alongside the executable in a `roms/` subdirectory.

### 3.2 FFmpeg shell redirection

`src/core/video_recorder.cpp` uses POSIX shell redirection (`>/dev/null 2>&1`) when probing for FFmpeg. Replace with:

```cpp
#ifdef _WIN32
    int ret = system("ffmpeg -version >nul 2>&1");
#else
    int ret = system("ffmpeg -version >/dev/null 2>&1");
#endif
```

Apply the same substitution to the FFmpeg invocation command string in the same file.

## Step 4 — Build jnext for Windows

```sh
cmake -B build-windows \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_QT_UI=ON \
    -DENABLE_TESTS=OFF
cmake --build build-windows -j$(nproc)
```

The output executable is `build-windows/jnext.exe`.

## Step 5 — Collect DLLs for distribution

The executable requires a set of DLLs alongside it. Copy them from the MinGW sysroot:

```sh
MINGW_ROOT=/usr/x86_64-w64-mingw32/sys-root/mingw
DIST=build-windows/dist
mkdir -p $DIST
cp build-windows/jnext.exe $DIST/

# MinGW runtime
cp $MINGW_ROOT/bin/libgcc_s_seh-1.dll   $DIST/
cp $MINGW_ROOT/bin/libstdc++-6.dll       $DIST/
cp $MINGW_ROOT/bin/libwinpthread-1.dll   $DIST/

# SDL2
cp $MINGW_ROOT/bin/SDL2.dll              $DIST/

# Qt6
cp $MINGW_ROOT/bin/Qt6Core.dll           $DIST/
cp $MINGW_ROOT/bin/Qt6Gui.dll            $DIST/
cp $MINGW_ROOT/bin/Qt6Widgets.dll        $DIST/

# Qt6 platform plugin
mkdir -p $DIST/platforms
cp $MINGW_ROOT/lib/qt6/plugins/platforms/qwindows.dll $DIST/platforms/
```

Zip `build-windows/dist/` for distribution.

## ROM files

jnext does not ship ROMs. On Windows, place the ROM files in a `roms/` subdirectory next to the executable, or pass `--roms-directory PATH` on the command line.

| Machine  | ROM files                           |
|----------|-------------------------------------|
| 48K      | `48.rom`                            |
| 128K     | `128-0.rom`, `128-1.rom`            |
| +3       | `plus3-0.rom` through `plus3-3.rom` |
| Pentagon | `128p-0.rom`, `128p-1.rom`          |

ROMs can be obtained from the FUSE emulator package or the relevant copyright holders.

## Portability notes

- All emulation core code (`src/core/`, `src/cpu/`, `src/memory/`, `src/video/`, `src/audio/`, `src/peripheral/`) is pure C++17 with no platform dependencies.
- `std::filesystem` is used throughout for file I/O (fully portable since C++17).
- No POSIX-specific headers (`unistd.h`, `sys/*`, `dirent.h`) are used anywhere in the codebase.
- Signal handlers in `src/main.cpp` use only `SIGABRT`, `SIGFPE`, `SIGSEGV` — all standard C signals available on Windows.
- The Makefile convenience layer uses Linux shell conventions and is not intended for Windows; use CMake directly.
- The regression test suite (`test/regression.sh`) is bash-based and not portable; a CMake/Python equivalent would be needed for Windows CI.
