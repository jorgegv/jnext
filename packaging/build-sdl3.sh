#!/usr/bin/env bash
#
# build-sdl3.sh — provide SDL3 development files when the distro ships none.
#
# WHY THIS EXISTS (GH #57). jnext links SDL3 directly. Every platform it
# packages for has SDL3 available EXCEPT one: Ubuntu 24.04 LTS (noble), which
# has no libsdl3 at any version — nor does 24.10; it first appears in 25.04.
# `release.yml` builds a .deb in a pinned ubuntu:24.04 container because
# dpkg-shlibdeps bakes in the host distro's library package names, and 24.04 is
# supported until 2029. So that one artifact needs SDL3 built from source.
#
# From-source is established practice here, not a new risk: the Flatpak
# manifest built SDL2 this way for years, with a pinned URL and a verified
# sha256, until GH #57 deleted that module (the KDE runtime ships SDL3).
#
# WHY STATIC, and why that makes the .deb honest. A bundled SHARED libSDL3
# would have to be installed into a private directory, found through an rpath,
# and then explained to dpkg-shlibdeps, which cannot map a library belonging to
# no package — the exact objection raised against this migration. Linking SDL3
# STATICALLY removes the whole problem: there is no extra file in the package,
# no rpath, and nothing for dpkg-shlibdeps to fail to resolve. jnext's
# CMakeLists needs no special case either, because SDL3's own SDL3Config.cmake
# points the SDL3::SDL3 alias at whichever of shared/static it was built with —
# so a prefix containing only the static library just works.
#
# What the package does gain is SDL3's own link-time dependencies, which
# dpkg-shlibdeps DOES resolve to real Ubuntu packages. SDL's display, audio and
# input backends are not among them: SDL dlopen()s libX11/libwayland/libasound/
# libpulse by soname at run time and degrades gracefully when one is absent
# (SDL_*_SHARED=ON, the upstream default, left alone here). That is how the
# distro's own SDL is built too.
#
# THE COST, stated rather than buried: this artifact's SDL does not receive
# distro security updates. It is pinned here and moves only when someone bumps
# it. That is the accepted price of shipping a 24.04 .deb at all, and it ends
# when 24.04 leaves support in 2029 — at which point this script, and the
# package-deb hook that calls it, should be deleted outright.
#
# IDEMPOTENT AND SELF-SKIPPING. If the system already provides SDL3 dev files
# (Fedora, Debian, Ubuntu >= 25.04, Homebrew, the CI container), this script
# does nothing at all and leaves no prefix behind, so the caller configures
# against the system SDL3 exactly as before. Re-running after a successful
# build is also a no-op.
#
set -euo pipefail

SDL3_VERSION=3.4.16
SDL3_URL="https://github.com/libsdl-org/SDL/releases/download/release-${SDL3_VERSION}/SDL3-${SDL3_VERSION}.tar.gz"
# Verified against the GitHub release asset AND the independent libsdl.org
# mirror on 2026-09-24; both hosts served byte-identical archives.
SDL3_SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK="$REPO_ROOT/build/sdl3-vendor"
PREFIX="$WORK/prefix"

# --- already satisfied? ------------------------------------------------------
# Ask CMake, not pkg-config: CMake's find_package is what the actual build
# uses, so this answers the question that matters instead of a proxy for it.
sdl3_found() {
    local probe="$WORK/probe"
    rm -rf "$probe"; mkdir -p "$probe"
    cat > "$probe/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(sdl3probe LANGUAGES C)
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    message(FATAL_ERROR "no SDL3")
endif()
EOF
    cmake -S "$probe" -B "$probe/b" >/dev/null 2>&1
}

if [ -d "$PREFIX" ]; then
    echo "build-sdl3: vendored SDL3 already built at $PREFIX"
    exit 0
fi

if sdl3_found; then
    echo "build-sdl3: the system provides SDL3 — nothing to build"
    exit 0
fi

echo "build-sdl3: no system SDL3; building ${SDL3_VERSION} from source"

for tool in curl sha256sum tar cmake; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-sdl3: required tool '$tool' not found" >&2; exit 1; }
done

mkdir -p "$WORK"
TARBALL="$WORK/SDL3-${SDL3_VERSION}.tar.gz"

if [ ! -f "$TARBALL" ]; then
    curl -fsSL -o "$TARBALL.part" "$SDL3_URL"
    mv "$TARBALL.part" "$TARBALL"
fi

# Verify BEFORE unpacking. A checksum checked after the fact is theatre.
actual=$(sha256sum "$TARBALL" | cut -d' ' -f1)
if [ "$actual" != "$SDL3_SHA256" ]; then
    echo "build-sdl3: SHA-256 MISMATCH for $TARBALL" >&2
    echo "  expected $SDL3_SHA256" >&2
    echo "  got      $actual" >&2
    rm -f "$TARBALL"
    exit 1
fi
echo "build-sdl3: sha256 verified"

SRC="$WORK/SDL3-${SDL3_VERSION}"
rm -rf "$SRC"
tar -xzf "$TARBALL" -C "$WORK"
[ -d "$SRC" ] || { echo "build-sdl3: unexpected archive layout" >&2; exit 1; }

# Static only. SDL's own CMakeLists refuses an in-tree build, hence -B.
cmake -S "$SRC" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DSDL_SHARED=OFF \
    -DSDL_STATIC=ON \
    -DSDL_TESTS=OFF \
    -DSDL_EXAMPLES=OFF
cmake --build "$WORK/build" -j"$(nproc 2>/dev/null || echo 2)"
cmake --install "$WORK/build"

# Prove the prefix is usable rather than assuming the install worked: a
# half-installed prefix would send the jnext configure back to the system SDL3
# that does not exist, and the error would name jnext instead of this script.
if [ ! -f "$PREFIX/lib/cmake/SDL3/SDL3Config.cmake" ] \
   && [ ! -f "$PREFIX/lib64/cmake/SDL3/SDL3Config.cmake" ]; then
    echo "build-sdl3: install completed but no SDL3Config.cmake under $PREFIX" >&2
    rm -rf "$PREFIX"
    exit 1
fi

echo "build-sdl3: SDL3 ${SDL3_VERSION} (static) installed to $PREFIX"
