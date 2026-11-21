#!/usr/bin/env bash
#
# The `dnf install` package lists of ci.yml, in one place: ci.yml calls this script per
# leg, and the remote-run image build (.claude/skills/remote-run) calls it too, so a CI
# container and a remote container carry the same packages.
#
#   test/ci-deps.sh <set>...          install the union of the named sets
#   test/ci-deps.sh --list <set>...   print that union, one package per line, sorted; install nothing
#   test/ci-deps.sh -h | --help
#
# Sets (each is the list one ci.yml step installs):
#   test       the `test` job's base list (every leg of it)
#   mingw      MinGW cross toolchain + wine (legs unit-win and regression-win)
#   mingw-qt   MinGW Qt6 (leg regression-win, on top of `mingw`)
#   qt5-guard  the `qt5-guard` job's build-only list
#   package    the `package` job's toolchains
#
# Installation is `dnf install -y`, so run it as root in the container.
set -euo pipefail
IFS=$'\n\t'

usage() {
    sed -n '3,/^# Installation is/p' "$0" | sed -e 's/^# \{0,1\}//' -e '$d'
}

# ---- set: test --------------------------------------------------------------
# Package set mirrors release.yml's rpm job (its fedora:45 leg,
# same dnf names) plus the tools the test
# layers need on top: imagemagick (`compare`, screenshot diffing),
# openbox (debugger-window-wm-func: the debugger window's size/grow rows
# under a real window manager, which Xvfb alone lacks),
# xvfb (audio-underrun-func's real-audio-device functional test, and
# crt_shader_widget_test in `make unit-test`: Qt's offscreen GL is GLX and
# needs an X server; with mesa-dri-drivers + mesa-libGL, declared here, it
# gets llvmpipe -
# every other GUI functional test uses QT_QPA_PLATFORM=offscreen and
# needs no display), python3 (check-audio-underruns.py), unzip (the
# SD-image provisioning step's own dependency, ultimately via
# libcurl inside jnext, not this step directly, but kept explicit),
# ripgrep (the tautological-assertion lint, run by BOTH `make unit-test`
# and the regression preflight - GH #129), ffmpeg-free (video-record-func
# and ffprobe; the same package the maintainer has locally), and bc/file/
# xxd, which functional rows shell out to. That set was established by
# checking every external command the suite invokes against a bare
# fedora:44 plus this list, rather than discovering them one CI run at a
# time.
# git, make and diffutils come preinstalled on an Ubuntu runner but are
# absent from the bare Fedora image, so they are explicit here.
#
# Notes on individual packages:
# nss ... libdrm (the last line): the shared libraries the pinned Chromium headless
# shell of `make wasm-browser-test` links (`ldd` on it), declared rather than
# inherited. The browser comes from test/provision-playwright.sh, like the SDK.
# util-linux: sfdisk, for test/minimal-card-test.sh (the bare
# fedora:45 has only util-linux-core).
# zip: package-web.sh's writer (the browser leg builds the web zip through
# wasm-browser-test; the unit leg runs web-package-test through make unit-test).
# mtools + dosfstools are the FOREIGN validators the GH #269
# sdcard-file-add-func row reads the SD image back with (mdir,
# mcopy, fsck.vfat). They are not a convenience: jnext writes FAT32
# there, and a filesystem writer checked only by its own reader is
# the .szx defect this project already shipped once. That row FAILS
# rather than skips when they are absent, so they are declared here.
# rpm-build provides `rpmspec`, which test/packaging/sync-version-test.sh
# (a contract suite of `make unit-test`) needs for its rpm-spec rows 12-13.
# Without it they printed SKIP - hidden, because the contract wrapper read
# only the exit status. A SKIP now fails the run, and the package job
# installs the same package, so it is not a new tool.
# java-25-openjdk-devel runs Ghidra (ghidra-foreign-func, GH #389), which needs
# a JDK 25 or newer to launch. Ghidra and skoolkit themselves are not packaged
# by Fedora and are not installed here: `make regression` provisions them
# (`make ghidra`, `make skoolkit`; test/provision-ghidra.sh, provision-skoolkit.sh)
# with the curl, sha256sum, unzip, tar and python3 this image already has.
# z88dk-gdb (gdb-z88dk-func's client) is not packaged by Fedora and is not
# installed here: `make regression` provisions it (`make z88dk-gdb`,
# test/provision-z88dk-gdb.sh) with the curl, sha256sum, tar, make and cc
# this image already has - the same command a developer runs locally.
# The five perl-* entries are the perl tooling's own dependencies
# (test/refresh-traceability-matrix.pl,
# test/traceability-citations-selftest.pl, and perl-File-Find for
# test/lint-devguide-cites.pl, which docs-check runs), declared rather than
# inherited. They USED to arrive transitively - perl is not in the
# bare fedora:44 image at all, and something above dragged it in -
# which held until 2026-07-29, when `make unit-test` failed on
# `Can't locate FindBin.pm` with no change to the script or the
# workflow. Reproduced in the real image: after installing exactly
# the list above, File::Temp/Cwd/File::Spec are present and FindBin
# alone is not. The same held for File::Find (GH #444: `Can't locate
# File/Find.pm` in docs-devguide-cites-check; `perl -c` of every perl
# script in the real image, after exactly this list, now passes).
# Depending on another package's dependency graph is
# precisely the "discover them one CI run at a time" failure the
# comment above says this list exists to prevent.
# libspectrum-devel is the ONLY foreign adjudication in this project
# (GH #27 S9, design §13.2(1)). It builds `szx_probe`, which links the
# library FUSE itself uses and reads a `.szx` jnext wrote, so
# snapshot-foreign-szx-func can require jnext's `.jns` to agree with
# an implementation nobody here can accidentally make permissive.
# §13.1 is why that matters: jnext's `.szx` saver once wrote RAM
# pages 0-111, its own loader accepted them, every test passed
# byte-exact - and libspectrum rejects any page > 63, so every file
# jnext could produce failed to load in real FUSE. Without this
# package the row SKIPS, and a SKIP fails the run: the best evidence
# in the design here is never silently lost.
# python3-jsonschema is `make schema-check`'s FOREIGN validator, and is
# declared for the same reason mtools is: the second half of that gate
# hands doc/formats/jns-snapshot.schema.json to an implementation that
# is not ours, and `test/snapshot/verify-schema.sh` FAILS (everywhere,
# not just in CI) when the module is missing. It arrives
# transitively from nothing in this list (checked with `dnf5 repoquery
# --requires` across every package above, and `--whatrequires`), so
# `make unit-test` fails in the container without it - reproduced
# locally in a venv with no site-packages, exit 1. This gate was the
# first one added to `make unit-test` whose tool was not already here;
# docs-check's pandoc/mkdocs-material/graphviz and
# traceability-check's perl-* entries are the precedent.
# liberation-sans-fonts is a docs dependency, not a font preference.
# The developer guide's .dot sources ask for fontname="Helvetica",
# which no installed font is actually NAMED: fontconfig resolves it,
# and graphviz bakes the resulting TEXT METRICS into every coordinate
# of the committed SVG. graphviz hard-requires urw-base35-fonts but
# nothing pulls Liberation into the container, so the same dot 14.1.4
# that renders system-overview.svg at 731x361 on the maintainer's
# Fedora desktop (Helvetica -> Liberation Sans) renders it at 758x383
# here (Helvetica -> Noto Sans), and docs-devguide-check reports all
# four diagrams stale when nothing is stale. The version fingerprint
# in DEVGUIDE_RENDERER cannot catch it: the dot version IS identical.
# Verified in a real fedora:44 with the full list above - without this
# package 758x383, with it all four SVGs byte-match the committed ones.
PKGS_test=(
    cmake gcc-c++ git make ccache
    SDL3-devel zlib-devel libcurl-devel openssl-devel libpng-devel
    qt6-qtbase-devel qt6-qtsvg-devel libspectrum-devel
    ImageMagick xorg-x11-server-Xvfb openbox xdotool python3 python3-pyyaml python3-jsonschema unzip diffutils
    ripgrep ffmpeg-free bc file xxd dbus-daemon
    mtools dosfstools rpm-build util-linux zip
    pandoc mkdocs-material graphviz liberation-sans-fonts
    perl-interpreter perl-FindBin perl-File-Temp perl-File-Find perl-PathTools
    mesa-dri-drivers mesa-libGL java-25-openjdk-devel
    nss nspr atk at-spi2-atk alsa-lib libXcomposite libXdamage libXfixes libXrandr libXi
    mesa-libgbm libxkbcommon libdrm
)

# ---- set: mingw -------------------------------------------------------------
# The unit-win leg builds the SDL-only suites with the MinGW cross toolchain and runs
# them under wine. The `package` job installs a superset of this for the Windows
# packages; this is the SDL-only subset (no Qt, no i686) plus wine, as windows-build.yml
# names it. A set of its own, so the other legs do not pull ~1 GB of cross packages.
PKGS_mingw=(
    mingw64-gcc mingw64-gcc-c++ mingw64-binutils mingw64-filesystem
    mingw64-SDL3 mingw64-zlib mingw64-libpng mingw64-winpthreads
    wine-core wine-common
)

# ---- set: mingw-qt ----------------------------------------------------------
# The regression-win leg also builds the Qt frontend for Windows (the PGO build that
# ships, trained under wine), so it needs the MinGW Qt6.
PKGS_mingw_qt=(
    mingw64-qt6-qtbase mingw64-qt6-qtsvg
)

# ---- set: qt5-guard ---------------------------------------------------------
# The build-only subset of the test list, with the Qt5 pair replacing the Qt6 pair.
PKGS_qt5_guard=(
    cmake gcc-c++ git make ccache
    SDL3-devel zlib-devel libcurl-devel openssl-devel libpng-devel
    qt5-qtbase-devel qt5-qtsvg-devel diffutils
)

# ---- set: package -----------------------------------------------------------
# release.yml's rpm job (Fedora-native build deps) + its windows job (the MinGW cross
# toolchain) + dpkg/dpkg-dev/file for CPack's DEB generator (CPACK_DEBIAN_PACKAGE_SHLIBDEPS
# is ON, which shells out to dpkg-shlibdeps) + zip/unzip, which the src and win rows use,
# + mkdocs-material for sync-version-test's mkdocs hook rows (a SKIP there fails the run).
#
# The deb built by the package job is only checked for CONTENT (does it carry bin/jnext).
# Do not turn that job into an Ubuntu container to make its dependency metadata shippable:
# release.yml builds the SHIPPED debs in pinned ubuntu containers for that, and the job
# would lose the MinGW toolchain and the rpm in one move.
PKGS_package=(
    cmake make gcc-c++ git ccache
    SDL3-devel libcurl-devel openssl-devel zlib-devel libpng-devel
    qt6-qtbase-devel qt6-qtsvg-devel
    rpm-build dpkg dpkg-dev file zip unzip
    findutils gawk grep coreutils diffutils
    python3 python3-pyyaml mkdocs-material
    mingw64-gcc mingw64-gcc-c++ mingw64-binutils mingw64-filesystem
    mingw64-qt6-qtbase mingw64-qt6-qtsvg mingw64-SDL3
    mingw64-qt5-qtbase mingw64-qt5-qtbase-devel mingw64-qt5-qmake mingw64-qt5-qtsvg
    mingw64-zlib mingw64-libpng mingw64-winpthreads
    mingw32-gcc mingw32-gcc-c++ mingw32-binutils mingw32-filesystem
    mingw32-SDL3
    mingw32-qt5-qtbase mingw32-qt5-qtbase-devel mingw32-qt5-qmake mingw32-qt5-qtsvg
    mingw32-zlib mingw32-libpng mingw32-winpthreads
    wine-core wine-common
)

main() {
    local list_only=0 set count
    local -a pkgs=()
    case "${1:-}" in
        -h|--help) usage; exit 0 ;;
        --list) list_only=1; shift ;;
        *) ;;
    esac
    [[ $# -ge 1 ]] || { usage >&2; exit 2; }
    for set in "$@"; do
        case "$set" in
            test)      pkgs+=("${PKGS_test[@]}") ;;
            mingw)     pkgs+=("${PKGS_mingw[@]}") ;;
            mingw-qt)  pkgs+=("${PKGS_mingw_qt[@]}") ;;
            qt5-guard) pkgs+=("${PKGS_qt5_guard[@]}") ;;
            package)   pkgs+=("${PKGS_package[@]}") ;;
            *) echo "ci-deps.sh: unknown set '$set' (test, mingw, mingw-qt, qt5-guard, package)" >&2; exit 2 ;;
        esac
    done
    count=${#pkgs[@]}
    [[ $count -gt 0 ]] || { echo "ci-deps.sh: empty package list" >&2; exit 2; }
    if [[ $list_only -eq 1 ]]; then
        printf '%s\n' "${pkgs[@]}" | sort -u
    else
        dnf install -y "${pkgs[@]}"
    fi
}

main "$@"
