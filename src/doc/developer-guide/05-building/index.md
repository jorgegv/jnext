# 5. Building and packaging

JNEXT is C++17, built with CMake 3.16 or newer. Linux is the development host,
and everything in this chapter assumes it. The other two platforms are not
built natively here: Windows is cross-compiled from Linux using the MinGW
toolchain the maintainer's distribution packages, and macOS has to be built on
a Mac.

## What you need

To build the emulator on Fedora and its relatives:

```console
$ sudo dnf install SDL2-devel cmake gcc-c++ qt6-qtbase-devel libpng-devel \
                   zlib-devel libcurl-devel openssl-devel
```

On Debian and Ubuntu:

```console
$ sudo apt install libsdl2-dev cmake g++ qt6-base-dev libpng-dev \
                   zlib1g-dev libcurl4-openssl-dev libssl-dev
```

Clone with `--recursive`, because spdlog is a git submodule. If you forget,
CMake will run `git submodule update --init --recursive` for you at configure
time — but it does so silently, it takes about a minute, and it only works if
the network is up, so getting it right at clone time saves some confusion later.

Running the tests needs one more tool, **ripgrep**. Both suites run the
tautological-assertion lint, and when ripgrep is missing that lint refuses to
report a verdict at all rather than quietly skipping itself. The reasoning is
worth internalising, because it recurs throughout this project: a check that did
not run must never read as a check that passed.

**The documentation toolchain is separate, and it is optional.** `pandoc`
regenerates the man page and `USAGE.md`, `mkdocs-material` renders the user and
developer guides, and `graphviz` renders this guide's diagrams. No target that
compiles jnext ever invokes any of them, and every generated document is
committed to the repository, so a source build ships complete documentation on
a machine that has none of the three installed. You need them only when you want
to *edit* documentation.

Two smaller notes. `ccache` is optional but is picked up automatically when it
is present, and `ffmpeg` is a runtime dependency of MP4 recording alone — it is
never linked against.

## The shape of the build

CMake does the actual building. Sitting on top of it is a **semantic
Makefile**: every target carries a one-line description, so running `make` with
no arguments prints all 69 of them. That listing is generated from the Makefile
itself rather than maintained beside it, which is what stops it from drifting
out of date. It is the authoritative catalogue of what you can build, and
nothing in this chapter tries to replace it.

Build directories all follow a single scheme, `build/<variant>-<config>`, and
all of them live under `build/`. `build/` itself is the canonical development
tree: it is where `build/jnext` and every test binary end up.

- [5.1 Make targets](01-make-targets.md)
- [5.2 Build configurations](02-build-configurations.md)
- [5.3 Packaging and release](03-packaging-and-release.md)
- [5.4 Continuous integration](04-continuous-integration.md)
