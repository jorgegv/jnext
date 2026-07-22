# JNEXT — Building

JNEXT builds and ships on Linux, Windows and macOS. Linux is the development
host, and everything below — prerequisites, `make` targets, tests — is written
for it. The other two are not native builds here:

- **Windows** is a *cross-build*: `make win-release` (just `jnext.exe`) and
  `make package-win` (the zip) run Fedora's MinGW toolchain on Linux. There is
  no MSVC build. Both targets are exercised on this dev host and in CI.
- **macOS** must be built *on a Mac*: `make package-macos` prints a SKIP and
  exits cleanly on any other platform. It is exercised on the `macos-latest`
  GitHub Actions runner rather than on the Linux dev host.

The platform-specific detail for both — the MinGW package list and the DLL
bundling, the `.app` bundle deployment and its verification — lives in
[packaging/README.md](packaging/README.md).

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
- **pandoc** — only to regenerate the man page and `USAGE.md` (`make docs-man`).
  See [Documentation](#documentation) below.
- **mkdocs-material** — only to render the user guide (`make docs-userguide`).
  See [Documentation](#documentation) below.

None of the optional tools are needed to build jnext. In particular the two
documentation tools are never invoked by a code build: the generated man page
and `USAGE.md` are committed, so a source-only build ships complete docs on a
machine that has neither.

## Build

```sh
git clone --recursive https://github.com/jorgegv/jnext.git
cd jnext

make gui-release      # Qt6 GUI + debugger, optimised  → build/gui-release/jnext
make sdl-release      # SDL only, no GUI, no debugger   → build/sdl-release/jnext
```

`--recursive` matters: spdlog is a git submodule. (CMake will try to
initialise submodules itself if they are missing.)

### Make targets

This is a **semantic Makefile**: every target carries a one-line description,
and running `make` with **no arguments** prints the full, always-up-to-date list
of targets and what each one does — so you never have to read the Makefile to
find out what is available:

```sh
make            # print every target with its description
```

Because that listing is generated from the Makefile itself, it is authoritative:
if a target exists, `make` shows it. The table below is a curated subset of the
most representative targets — run `make` for the complete list (currently ~40
targets: build variants, per-variant `-run`/`-clean`, tests, packaging, versioning).

**Build**

| Target | Description |
|--------|-------------|
| `make gui-release` | Qt6 GUI + debugger, release (optimised) → `build/gui-release/jnext` |
| `make gui-debug` | Qt6 GUI, debug (sanitisers + debug symbols) |
| `make sdl-release` | SDL-only, release → `build/sdl-release/jnext` |
| `make sdl-debug` | SDL-only, debug |
| `make win-release` | Cross-compile the Windows `jnext.exe` (Fedora MinGW), DLLs bundled beside it |
| `make gui-release-run` / `gui-debug-run` / `sdl-release-run` / `sdl-debug-run` | Build, then run |

**Test**

| Target | Description |
|--------|-------------|
| `make unit-test` | Build `build/` and run every subsystem unit-test suite in parallel |
| `make unit-test-dashboard` | `unit-test`, then refresh `test/SUBSYSTEM-TESTS-STATUS.md` |
| `make regression` | Run the screenshot + functional regression suite |
| `make harness-selftest` | Prove the test harness fails loudly on injected faults |

**Package** (details under [Building packages](#building-packages))

| Target | Description |
|--------|-------------|
| `make package-rpm` / `package-deb` | Fedora/RHEL `.rpm` / Debian/Ubuntu `.deb` |
| `make package-win` | Windows `.zip` (exe + bundled Qt6/SDL2 DLLs + plugins) |
| `make package-flatpak` / `package-src` | Flatpak bundle / source tarball |
| `make package-test` | Build every package (except macOS) and check each artifact |

**Clean / housekeeping**

| Target | Description |
|--------|-------------|
| `make gui-clean` | Remove the GUI build directories |
| `make unit-test-clean` | Remove the `build/` directory |
| `make clean` | Remove all build directories |
| `make kloc-count` | Lines of code per directory |
| `make version` | Show the current version |
| `make bump-patch` / `bump-minor` / `bump-major` / `bump` | Bump the version, commit, tag (`bump` = `bump-minor`) |

### CMake options

The `make` targets pass these for you; use them when invoking CMake directly.

| Option            | Default | Meaning                                                                                                                              |
|-------------------|---------|--------------------------------------------------------------------------------------------------------------------------------------|
| `ENABLE_QT_UI`    | **OFF** | Build the Qt6 native UI. `make gui-*` turns it on — a plain `cmake` without it gives the SDL frontend                                |
| `ENABLE_DEBUGGER` | **ON**  | Include the Qt debugger UI. It is opened from the Qt main window, so it is only reachable in a build that also has `ENABLE_QT_UI=ON` |
| `ENABLE_TESTS`    | ON      | Build the unit-test binaries                                                                                                         |
| `USE_CCACHE`      | ON      | Use ccache as the compiler launcher when it is found (no-op if it is not)                                                            |
| `CYCLE_ACCURATE`  | OFF     | 28 MHz cycle-accurate mode                                                                                                           |
| `STATIC_BUILD`    | OFF     | Link statically (needs static SDL2/Qt6 builds)                                                                                       |

Directly, without the Makefile:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON -DENABLE_TESTS=ON
cmake --build build -j$(nproc)
```

### Building in Docker

A container recipe that needs nothing on the host but Docker is described in
[doc/LINUX-BUILD-DOCKER.md](doc/LINUX-BUILD-DOCKER.md).

## Documentation

The documentation build is **deliberately separate from the code build**: no
make target that compiles jnext ever invokes a documentation tool, and every
generated file is committed. A contributor who never touches the docs needs
neither pandoc nor mkdocs.

**The CLI reference has exactly one source: `doc/man/jnext.1.md`.** It generates
both the man page and `USAGE.md`, so the two cannot drift apart:

```sh
make docs-man       # regenerate doc/man/jnext.1 and USAGE.md   (needs pandoc)
make docs-check     # fail if either committed output is stale
make docs-userguide # render src/doc/user-guide -> doc/user-guide  (needs mkdocs)
make read-userguide # serve the rendered guide on localhost for reading
```

Never edit `doc/man/jnext.1` or `USAGE.md` by hand — edit the source and rerun
`make docs-man`, committing the regenerated outputs alongside it. `make docs-check`
is the guard: it regenerates into a temporary directory and diffs, so a stale
committed output is a hard failure rather than something a reviewer has to
spot. It skips (rather than fails) when pandoc is absent.

`make docs-man` needs pandoc:

```sh
sudo dnf install pandoc          # Fedora / RHEL
sudo apt install pandoc          # Debian / Ubuntu
brew install pandoc              # macOS
```

The man page is installed by the CMake install rules to
`${CMAKE_INSTALL_MANDIR}/man1/jnext.1`, so it lands in every package
(rpm, deb, Flatpak, macOS) with no per-format step. The Windows zip ships
`USAGE.md` instead, man pages being meaningless there.

The **user guide** is written under `src/doc/user-guide` and rendered with
[mkdocs-material](https://squidfunk.github.io/mkdocs-material/)
([issue #28](https://github.com/jorgegv/jnext/issues/28)). The Markdown is
readable as-is in the repository; to build the browsable site:

```sh
make docs-userguide     # renders src/doc/user-guide -> doc/user-guide
make read-userguide     # serve it at http://localhost:8000/ to read in a browser
mkdocs serve            # live preview with auto-reload, for editing
```

The **rendered** guide is committed under `doc/user-guide`, so anyone who clones
the repository can read it without installing a documentation toolchain — open
`doc/user-guide/index.html`, or run `make read-userguide`. The **sources** live
under `src/doc/user-guide`; edit those and rerun `make docs-userguide`,
committing the regenerated output alongside your change, exactly as with the man
page.

`make docs-userguide` needs mkdocs-material, which is packaged by the
distributions — no pip or virtualenv required:

```sh
sudo dnf install mkdocs-material     # Fedora / RHEL
```

Other distributions and macOS package it too, under varying names; check your
own package manager.

Like the man page, this is documentation tooling only — no code build invokes
mkdocs, and a source build never needs it installed.

## Building packages

Packages are the recommended way for end users to install JNEXT (see the main
[README](README.md#install)). To build them yourself:

| Target | Produces |
|--------|----------|
| `make package-rpm` | Fedora/RHEL `.rpm` |
| `make package-deb` | Debian/Ubuntu `.deb` |
| `make package-flatpak` | Flatpak bundle (needs `flatpak-builder` + the KDE runtime) |
| `make package-win` | Windows `.zip` (cross-compiled with Fedora MinGW — see below) |
| `make package-src` | source tarball |
| `make package-test` | build every package above (except macOS) and check each artifact |

A **Windows** executable can be cross-compiled on Fedora with MinGW
(`make win-release` for just `jnext.exe`, or `make package-win` for the
zip). See [packaging/README.md](packaging/README.md) for the exact MinGW
package list, the macOS notes, and per-distro maintainer packaging
(`packaging/rpm`, `packaging/debian`, `packaging/flatpak`).

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
