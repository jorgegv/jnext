# JNEXT — Building

Linux only for now. Windows and macOS ports are pending.

## Prerequisites

**Fedora / RHEL:**

```sh
sudo dnf install SDL2-devel cmake gcc-c++ qt6-qtbase-devel libpng-devel \
                 zlib-devel libcurl-devel openssl-devel
```

**Debian / Ubuntu:**

```sh
sudo apt install libsdl2-dev cmake g++ qt6-base-dev libpng-dev \
                 zlib1g-dev libcurl4-openssl-dev libssl-dev
```

Optional:

- **ccache** — picked up automatically and used as the compiler launcher; makes
  clean rebuilds nearly free. `ccache -M 20G` once per machine is worth it: the
  5 GB default thrashes on a tree this size.
- **ffmpeg** — needed at runtime for MP4 video recording (`--record`).
- **z88dk** — only to rebuild the demo programs in `demo/`.

## Build

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext

make gui-release      # Qt6 GUI + debugger, optimised  → build/gui-release/jnext
make release          # SDL only, no GUI, no debugger   → build/release/jnext
```

`--recursive` matters: spdlog is a git submodule. (CMake will try to
initialise submodules itself if they are missing.)

### Make targets

| Target | Description |
|--------|-------------|
| `make` | List all targets with their descriptions |
| `make gui-release` | Qt6 GUI, release (optimised) |
| `make gui-debug` | Qt6 GUI, debug (sanitisers + debug symbols) |
| `make release` | SDL-only, release |
| `make debug` | SDL-only, debug |
| `make gui-release-run` / `gui-debug-run` / `release-run` / `debug-run` | Build, then run |
| `make unit-test` | Build `build/` and run every subsystem unit-test suite in parallel |
| `make unit-test-dashboard` | `unit-test`, then refresh `test/SUBSYSTEM-TESTS-STATUS.md` |
| `make regression` | Run the screenshot + functional regression suite |
| `make gui-clean` | Remove the GUI build directories |
| `make unit-test-clean` | Remove the `build/` directory |
| `make clean` | Remove all build directories |
| `make kloc-count` | Lines of code per directory |
| `make version` | Show the current version |
| `make bump-patch` / `bump-minor` / `bump-major` / `bump` | Bump the version, commit, tag (`bump` = `bump-minor`) |

### CMake options

The `make` targets pass these for you; use them when invoking CMake directly.

| Option | Default | Meaning |
|--------|---------|---------|
| `ENABLE_QT_UI` | **OFF** | Build the Qt6 native UI. `make gui-*` turns it on — a plain `cmake` without it gives the SDL frontend |
| `ENABLE_DEBUGGER` | **ON** | Include the Qt debugger UI. It is opened from the Qt main window, so it is only reachable in a build that also has `ENABLE_QT_UI=ON` |
| `ENABLE_TESTS` | ON | Build the unit-test binaries |
| `USE_CCACHE` | ON | Use ccache as the compiler launcher when it is found (no-op if it is not) |
| `CYCLE_ACCURATE` | OFF | 28 MHz cycle-accurate mode |
| `STATIC_BUILD` | OFF | Link statically (needs static SDL2/Qt6 builds) |

Directly, without the Makefile:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=ON
cmake --build build -j$(nproc)
```

### Building in Docker

A container recipe that needs nothing on the host but Docker is described in
[doc/LINUX-BUILD-DOCKER.md](doc/LINUX-BUILD-DOCKER.md).

## Tests

Two suites, and they are complementary:

```sh
make unit-test      # subsystem unit tests, incl. the FUSE Z80 opcode suite
make regression     # screenshot comparisons + functional tests, headless
```

- **`make unit-test`** builds `build/` and runs every subsystem suite in
  parallel, printing a per-suite PASS/FAIL/SKIP table and a total. It includes
  the **FUSE Z80 opcode test suite** (1356/1356, 100%) and the Z80N opcode
  suite. To run the FUSE suite alone:

  ```sh
  ./build/test/fuse_z80_test build/test/fuse
  ```

- **`make regression`** runs the screenshot and functional tests headless and
  compares the output against the reference images. Details, and how to add a
  test, in [doc/testing/REGRESSION-TEST-SUITE.md](doc/testing/REGRESSION-TEST-SUITE.md);
  the current state of the suite is tracked in
  [doc/CURRENT-REGRESSION-STATE.md](doc/CURRENT-REGRESSION-STATE.md).

  The reference screenshots are only regenerated after an *intentional*
  rendering change, and never without checking every image that moves:

  ```sh
  bash test/00regression/generate-references.sh
  ```

  Some regression tests are real-time-paced (audio underruns, a paused-emulator
  screenshot with a 60 s timeout) and will report false failures if the machine
  is loaded. Cap the suite's own concurrency with `JNEXT_TEST_JOBS=4` when
  running it alongside anything else:

  ```sh
  JNEXT_TEST_JOBS=4 make regression
  ```

The live per-subsystem test dashboard — what is verified against the ZX Next
FPGA VHDL, and what is still skipped — is
[test/SUBSYSTEM-TESTS-STATUS.md](test/SUBSYSTEM-TESTS-STATUS.md).

## Demo programs

The test/demo programs in `demo/` are built with z88dk:

```sh
make -C demo all      # NEX + TAP
make -C demo nex
make -C demo tap
```
