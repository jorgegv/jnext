# Windows Porting Guide

This document describes how to build jnext for Windows using a Docker container for cross-compilation. Nothing is installed on the host machine beyond Docker itself.

## Approach

A Fedora-based Docker image contains the full MinGW64 toolchain, all packaged dependencies (Qt6, libpng, zlib), and a pre-built SDL2 cross-library. The host source tree is mounted read-only into the container; build output lands in `build-windows/` on the host.

## Prerequisites

Install Docker on your host (one-time):

```sh
sudo dnf install docker
sudo systemctl enable --now docker
sudo usermod -aG docker $USER   # log out and back in after this
```

## Step 1 — Create the Dockerfile

Create `docker/Dockerfile.windows` in the repository:

```dockerfile
FROM fedora:latest

# MinGW toolchain + packaged dependencies
RUN dnf install -y \
    mingw64-gcc-c++ \
    mingw64-qt6-qtbase \
    mingw64-libpng \
    mingw64-zlib \
    cmake \
    make \
    git \
    && dnf clean all

# Cross-build SDL2 and install into the MinGW sysroot
RUN git clone https://github.com/libsdl-org/SDL.git --branch SDL2 --depth 1 /tmp/SDL2 \
    && cmake -S /tmp/SDL2 -B /tmp/SDL2/build \
        -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32/sys-root/mingw \
    && cmake --build /tmp/SDL2/build -j$(nproc) \
    && cmake --install /tmp/SDL2/build \
    && rm -rf /tmp/SDL2
```

## Step 2 — Build the Docker image (one-time)

```sh
docker build -f docker/Dockerfile.windows -t jnext-windows-builder .
```

This takes a few minutes the first time. The resulting image is ~2 GB and is cached locally; subsequent builds are instant.

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

### 3.2 FFmpeg shell redirection

`src/core/video_recorder.cpp` uses POSIX shell redirection (`>/dev/null 2>&1`). Replace with:

```cpp
#ifdef _WIN32
    int ret = system("ffmpeg -version >nul 2>&1");
#else
    int ret = system("ffmpeg -version >/dev/null 2>&1");
#endif
```

Apply the same substitution to the FFmpeg invocation command string in the same file.

## Step 4 — Build jnext for Windows

Run the container, mounting the source tree and the output directory:

```sh
mkdir -p build-windows

docker run --rm \
    -v "$(pwd)":/src:ro \
    -v "$(pwd)/build-windows":/build \
    jnext-windows-builder \
    cmake -S /src -B /build \
        -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_QT_UI=ON \
        -DENABLE_TESTS=OFF

docker run --rm \
    -v "$(pwd)":/src:ro \
    -v "$(pwd)/build-windows":/build \
    jnext-windows-builder \
    cmake --build /build -j$(nproc)
```

The output executable is `build-windows/jnext.exe`.

Or add a convenience `make` target (see Step 6).

## Step 5 — Collect DLLs for distribution

The executable needs DLLs alongside it. Run this inside the container to collect them:

```sh
docker run --rm \
    -v "$(pwd)/build-windows":/build \
    jnext-windows-builder \
    bash -c '
        MINGW=/usr/x86_64-w64-mingw32/sys-root/mingw
        DIST=/build/dist
        mkdir -p $DIST/platforms
        cp /build/jnext.exe $DIST/

        # MinGW runtime
        cp $MINGW/bin/libgcc_s_seh-1.dll   $DIST/
        cp $MINGW/bin/libstdc++-6.dll       $DIST/
        cp $MINGW/bin/libwinpthread-1.dll   $DIST/

        # SDL2
        cp $MINGW/bin/SDL2.dll              $DIST/

        # Qt6
        cp $MINGW/bin/Qt6Core.dll           $DIST/
        cp $MINGW/bin/Qt6Gui.dll            $DIST/
        cp $MINGW/bin/Qt6Widgets.dll        $DIST/

        # Qt6 platform plugin
        cp $MINGW/lib/qt6/plugins/platforms/qwindows.dll $DIST/platforms/
    '
```

The distributable package is `build-windows/dist/`. Zip it and it runs on any 64-bit Windows machine.

## Step 6 — Makefile target (optional convenience)

Add a `windows` target to the Makefile so the whole flow is a single command:

```makefile
# Build Windows executable using Docker cross-compilation
windows:
	docker build -f docker/Dockerfile.windows -t jnext-windows-builder .
	mkdir -p build-windows
	docker run --rm -v "$$(pwd)":/src:ro -v "$$(pwd)/build-windows":/build \
	    jnext-windows-builder \
	    cmake -S /src -B /build \
	        -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
	        -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF
	docker run --rm -v "$$(pwd)":/src:ro -v "$$(pwd)/build-windows":/build \
	    jnext-windows-builder cmake --build /build -j$$(nproc)
	docker run --rm -v "$$(pwd)/build-windows":/build jnext-windows-builder bash -c '\
	    MINGW=/usr/x86_64-w64-mingw32/sys-root/mingw; \
	    DIST=/build/dist; mkdir -p $$DIST/platforms; \
	    cp /build/jnext.exe $$DIST/; \
	    cp $$MINGW/bin/{libgcc_s_seh-1,libstdc++-6,libwinpthread-1,SDL2,Qt6Core,Qt6Gui,Qt6Widgets}.dll $$DIST/; \
	    cp $$MINGW/lib/qt6/plugins/platforms/qwindows.dll $$DIST/platforms/'
	printf "$(BOLD)Windows build ready in build-windows/dist/$(RESET)\n"
```

## ROM files

Place ROM files in a `roms/` subdirectory next to the executable, or pass `--roms-directory PATH`:

| Machine  | ROM files                           |
|----------|-------------------------------------|
| 48K      | `48.rom`                            |
| 128K     | `128-0.rom`, `128-1.rom`            |
| +3       | `plus3-0.rom` through `plus3-3.rom` |
| Pentagon | `128p-0.rom`, `128p-1.rom`          |

## Portability notes

- All emulation core code is pure C++17 with no platform dependencies.
- `std::filesystem` is used throughout (fully portable since C++17).
- No POSIX-specific headers (`unistd.h`, `sys/*`, `dirent.h`) are used.
- Signal handlers use only `SIGABRT`, `SIGFPE`, `SIGSEGV` — standard C on all platforms.
- The Makefile and regression test suite (`test/regression.sh`) are bash-based and not portable to Windows; use CMake directly on a Windows host if needed.
