# 5. Building and packaging

JNEXT is C++17 built with CMake (3.16 or newer). Linux is the development
host: everything in this chapter is written for it, and the two other
platforms are not native builds here — Windows is cross-compiled from Linux
with Fedora's MinGW toolchain, and macOS must be built on a Mac.

## What you need

To build the emulator, on Fedora or RHEL:

```console
$ sudo dnf install SDL2-devel cmake gcc-c++ qt6-qtbase-devel libpng-devel \
                   zlib-devel libcurl-devel openssl-devel
```

On Debian or Ubuntu:

```console
$ sudo apt install libsdl2-dev cmake g++ qt6-base-dev libpng-dev \
                   zlib1g-dev libcurl4-openssl-dev libssl-dev
```

Clone with `--recursive`: spdlog is a git submodule. If you forget, CMake runs
`git submodule update --init --recursive` for you at configure time — silently,
for about a minute, and only if the network is up.

To run the tests you additionally need **ripgrep**. Both suites run the
tautological-assertion lint, which refuses to report a verdict without it
rather than skipping: a lint that did not run must never read as a pass.

**The documentation toolchain is separate and optional.** `pandoc` regenerates
the man page and `USAGE.md`; `mkdocs-material` renders the user and developer
guides; `graphviz` renders this guide's diagrams. No target that compiles jnext
ever invokes any of them, and every generated document is committed — so a
source build ships complete documentation on a machine that has none of the
three installed. You need them only to *edit* documentation.

`ccache` is optional but picked up automatically, and `ffmpeg` is a runtime
dependency of MP4 recording only, never linked.

## The shape of the build

CMake does the building. On top of it sits a **semantic Makefile**: every
target carries a one-line description, and running `make` with no arguments
prints all 69 of them. That listing is generated from the Makefile itself, so
it cannot go stale — it is the authoritative catalogue, and this chapter never
tries to replace it.

Every build directory follows one scheme, `build/<variant>-<config>`, all
under `build/`. `build/` itself is the canonical development tree: it holds
`build/jnext` and every test binary.

- [5.1 Make targets](01-make-targets.md)
- [5.2 Build configurations](02-build-configurations.md)
- [5.3 Packaging and release](03-packaging-and-release.md)
- [5.4 Continuous integration](04-continuous-integration.md)
