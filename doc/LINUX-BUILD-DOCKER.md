# Linux Build with Docker

This document describes how to build jnext and run the full regression test suite inside a Docker container. Nothing beyond Docker itself needs to be installed on the host machine.

## Prerequisites

Install Docker (one-time):

```sh
sudo dnf install docker
sudo systemctl enable --now docker
sudo usermod -aG docker $USER   # log out and back in after this
```

## Step 1 — Build the Docker image (one-time)

```sh
docker build -f docker/Dockerfile.linux -t jnext-builder .
```

The image is based on Fedora and installs:
- GCC C++17, CMake, Make, Ninja, Git
- SDL2, Qt6, libpng, zlib development libraries
- FFmpeg (for video recording tests)
- ImageMagick `compare` (for screenshot regression tests)
- `fuse-emulator-roms` — ROM files for all supported machines

Subsequent builds use the Docker layer cache and are instant.

## Step 2 — Build jnext

Mount the source tree (read-only) and an output directory:

```sh
mkdir -p build/gui-release

docker run --rm \
    -v "$(pwd)":/src:ro \
    -v "$(pwd)/build/gui-release":/build \
    jnext-builder \
    cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_QT_UI=ON \
        -DENABLE_TESTS=ON

docker run --rm \
    -v "$(pwd)":/src:ro \
    -v "$(pwd)/build/gui-release":/build \
    jnext-builder \
    cmake --build /build -j$(nproc)
```

The output executable is `build/gui-release/jnext`.

## Step 3 — Run the regression test suite

The regression script expects `build/jnext` (SDL-only headless build) and `build/test/fuse_z80_test`. Build these first:

```sh
mkdir -p build/headless

docker run --rm \
    -v "$(pwd)":/src:ro \
    -v "$(pwd)/build/headless":/build \
    jnext-builder \
    bash -c '
        cmake -S /src -B /build \
            -DCMAKE_BUILD_TYPE=Release \
            -DENABLE_QT_UI=OFF \
            -DENABLE_TESTS=ON && \
        cmake --build /build -j$(nproc)
    '

# Symlink so regression.sh finds the binary at the expected path
mkdir -p build
ln -sf headless/jnext build/jnext
```

Then run the suite:

```sh
docker run --rm \
    -v "$(pwd)":/src \
    -v "$(pwd)/build":/src/build \
    jnext-builder \
    bash /src/test/regression.sh
```

The script compares screenshots to reference images in `test/img/`. To update references after an intentional rendering change:

```sh
docker run --rm \
    -v "$(pwd)":/src \
    -v "$(pwd)/build":/src/build \
    jnext-builder \
    bash /src/test/regression.sh --update
```

## Makefile targets

Add these targets to the Makefile for convenience:

```makefile
# Build the Linux Docker image
docker-image:
	docker build -f docker/Dockerfile.linux -t jnext-builder .

# Build the Qt6 GUI release inside Docker
docker-build: docker-image
	mkdir -p build/gui-release
	docker run --rm \
	    -v "$$(pwd)":/src:ro -v "$$(pwd)/build/gui-release":/build \
	    jnext-builder \
	    bash -c 'cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release \
	        -DENABLE_QT_UI=ON -DENABLE_TESTS=ON && cmake --build /build -j$$(nproc)'

# Run the regression test suite inside Docker
docker-test: docker-image
	mkdir -p build/headless
	docker run --rm \
	    -v "$$(pwd)":/src:ro -v "$$(pwd)/build/headless":/build \
	    jnext-builder \
	    bash -c 'cmake -S /src -B /build -DCMAKE_BUILD_TYPE=Release \
	        -DENABLE_QT_UI=OFF -DENABLE_TESTS=ON && cmake --build /build -j$$(nproc)'
	ln -sf headless/jnext build/jnext
	docker run --rm \
	    -v "$$(pwd)":/src -v "$$(pwd)/build":/src/build \
	    jnext-builder bash /src/test/regression.sh
```

## ROM files

The `fuse-emulator-roms` package installs ROMs to `/usr/share/fuse/` inside the container, which is the default path used by jnext. No extra configuration is needed.

| Machine  | ROM files                           |
|----------|-------------------------------------|
| 48K      | `48.rom`                            |
| 128K     | `128-0.rom`, `128-1.rom`            |
| +3       | `plus3-0.rom` through `plus3-3.rom` |
| Pentagon | `128p-0.rom`, `128p-1.rom`          |

## Notes

- The container has no display server. The headless build (`--headless`) is used for all tests — no display is needed.
- The Qt6 GUI build produces a binary that requires a running display to launch interactively. For CI/automated use, always use the headless build.
- FFmpeg is available inside the container, so the video recording regression test works without extra setup.
