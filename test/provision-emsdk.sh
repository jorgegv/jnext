#!/usr/bin/env bash
#
# provision-emsdk.sh - provide the pinned Emscripten SDK the WebAssembly build
# (`make wasm-core`) uses, on any machine, the same way locally and in CI.
#
# WHY. The browser build needs emcc (clang + wasm-ld + the SDL3, zlib and libpng
# ports) at ONE exact version: the rows depend on its behaviour (libc quirks,
# the exception model, the port variants). Fedora does not package it, and a
# SKIP fails the run, so it is provisioned like Ghidra and z88dk-gdb: no-op once
# cached, needs the network once.
#
# WHAT. emsdk 6.0.13 (the installer), the emscripten release it installs, and the
# node it bundles. emsdk verifies NO download itself, so this script downloads
# the three archives, verifies all three sha256 BEFORE anything is unpacked,
# pre-seeds them under the names emsdk expects (EMSDK_KEEP_DOWNLOADS=1 makes it
# skip a download whose file is already there) and lets `emsdk install` unpack
# them. The three port sources (SDL 3.4.2, zlib 1.3.2, libpng 1.6.59) are then
# fetched once by `embuilder`; emscripten pins each with a sha512 of its own in
# tools/ports/*.py, so they are pinned transitively by the release sha256.
# The result lives in the user cache, is used at build time only, is never
# committed and never shipped.
#
# HOW THE PINS WERE VERIFIED (2026-11-06):
#   emsdk tag archive  two independent downloads of the GitHub tag tarball matched.
#   release tarball    equals the sha_linux emsdk itself publishes in
#                      bazel/revisions.bzl for 6.0.13, and the download matched.
#   node tarball       equals nodejs.org SHASUMS256.txt for v24.19.0, and the
#                      download matched.
#
# USAGE
#   bash test/provision-emsdk.sh               resolve, else download + install
#   bash test/provision-emsdk.sh --print-path  resolve only: print the emsdk
#                                              root, or exit 1
#
# RESOLUTION: only the cache, ${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools/emsdk-6.0.13/,
# and only when its stamp file (written LAST, with the pins) and
# upstream/emscripten/emcc exist. An emcc on PATH is deliberately never used,
# and a directory with no stamp (a manual install) is untrusted and replaced.
#
# Env: JNEXT_EMSDK_URL / _SHA256, JNEXT_EMSDK_RELEASE_URL / _RELEASE_SHA256 and
#      JNEXT_EMSDK_NODE_URL / _NODE_SHA256 override the pins / sources (the
#      harness uses them to prove a wrong checksum is refused).
set -euo pipefail

# Children's output must not depend on the user's locale (CLAUDE.md).
export LC_ALL=C LANG=C

VERSION=6.0.13
RELEASE_HASH=eb132757325583b787fe9414b09afea9442b72ad
NODE_VERSION=24.19.0

URL="${JNEXT_EMSDK_URL:-https://github.com/emscripten-core/emsdk/archive/refs/tags/${VERSION}.tar.gz}"
SHA256="${JNEXT_EMSDK_SHA256:-8c04da75eeceb015e766f4766b713bae0dbb9156ceaacf3cf353585b88656114}"
RELEASE_URL="${JNEXT_EMSDK_RELEASE_URL:-https://storage.googleapis.com/webassembly/emscripten-releases-builds/linux/${RELEASE_HASH}/wasm-binaries.tar.xz}"
RELEASE_SHA256="${JNEXT_EMSDK_RELEASE_SHA256:-cda6801012ff3319a539111826ed02a9abd7eff135c2ef261c8941caf5a3763e}"
NODE_URL="${JNEXT_EMSDK_NODE_URL:-https://storage.googleapis.com/webassembly/emscripten-releases-builds/deps/node-v${NODE_VERSION}-linux-x64.tar.xz}"
NODE_SHA256="${JNEXT_EMSDK_NODE_SHA256:-14b342e71204f811bde6153be8e04b62aef63c236fef92b55f9c83154b409647}"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools"
CACHED="$CACHE_ROOT/emsdk-$VERSION"
STAMP_NAME=.jnext-provisioned
STAMP_BODY="emsdk $VERSION $SHA256 $RELEASE_SHA256 $NODE_SHA256"

resolve() {
    if [[ -x "$CACHED/upstream/emscripten/emcc" && -f "$CACHED/$STAMP_NAME" \
          && "$(cat "$CACHED/$STAMP_NAME")" == "$STAMP_BODY" ]]; then
        echo "$CACHED"; return 0
    fi
    return 1
}

if [[ "${1:-}" == "--print-path" ]]; then
    resolve
    exit
fi

if found=$(resolve); then
    echo "emsdk: using $found"
    exit 0
fi

echo "emsdk: not provisioned; installing Emscripten $VERSION into $CACHED (a 345 MB download)"

missing=""
for tool in curl sha256sum tar python3 timeout flock; do
    command -v "$tool" >/dev/null 2>&1 || missing+=" $tool"
done
if [[ -n "$missing" ]]; then
    echo "emsdk: required tool(s) missing:$missing" >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"

# One provisioner at a time per version: the install is built IN PLACE (see below),
# so there is no atomic rename to make two first-time runs harmless. The lock is held
# to the end of the script (fd 9 closes with it); a run that waited for it re-resolves,
# and finds the tree the other run stamped.
exec 9>"$CACHE_ROOT/emsdk-$VERSION.lock"
flock 9
if found=$(resolve); then
    echo "emsdk: using $found"
    exit 0
fi

WORK=$(mktemp -d "$CACHE_ROOT/emsdk-build.XXXXXX")
cleanup() { rm -rf "$WORK"; }   # redefined below once $CACHED is in place
trap cleanup EXIT

# Download one archive and verify it BEFORE anything is unpacked. A checksum
# checked after the fact is theatre.
fetch_verified() {
    local url=$1 want=$2 dest=$3 actual
    curl -fsSL --retry 3 --max-time 1800 -o "$dest" "$url"
    actual=$(sha256sum "$dest" | cut -d' ' -f1)
    if [[ "$actual" != "$want" ]]; then
        echo "emsdk: SHA-256 MISMATCH for $url" >&2
        echo "  expected $want" >&2
        echo "  got      $actual" >&2
        exit 1
    fi
}

mkdir "$WORK/dl"
fetch_verified "$URL"         "$SHA256"         "$WORK/dl/emsdk.tar.gz"
fetch_verified "$RELEASE_URL" "$RELEASE_SHA256" "$WORK/dl/release.tar.xz"
fetch_verified "$NODE_URL"    "$NODE_SHA256"    "$WORK/dl/node.tar.xz"
echo "emsdk: sha256 verified (emsdk, release, node)"

tar -xzf "$WORK/dl/emsdk.tar.gz" -C "$WORK"
SRC="$WORK/emsdk-$VERSION"
[[ -f "$SRC/emsdk.py" ]] || { echo "emsdk: the archive holds no emsdk-$VERSION/emsdk.py" >&2; exit 1; }

# Pre-seed under the names emsdk looks for; EMSDK_KEEP_DOWNLOADS=1 makes it skip
# the (unverified) download of a file that is already there.
mkdir -p "$SRC/downloads"
mv "$WORK/dl/release.tar.xz" "$SRC/downloads/${RELEASE_HASH}-wasm-binaries.tar.xz"
mv "$WORK/dl/node.tar.xz"    "$SRC/downloads/node-v${NODE_VERSION}-linux-x64.tar.xz"

# python3 and emsdk are foreign tools: bounded in memory and time.
(
    ulimit -v 8388608
    cd "$SRC"
    export EMSDK_KEEP_DOWNLOADS=1
    timeout --kill-after=10s 1800s ./emsdk install "$VERSION"
    timeout --kill-after=10s 300s ./emsdk activate "$VERSION"
)
rm -rf "$SRC/downloads"

# The directory is moved to its FINAL place before the ports are built: emscripten
# records the absolute path of its LLVM in cache/sanity.txt and CLEARS its whole
# cache (ports, their CMake config, every generated library) the first time it
# runs from another path. The stamp, written last, is what makes it trusted; a
# run that dies in between leaves a stampless directory, which the trap removes
# and every later run would replace.
if [[ -e "$CACHED" ]]; then rm -rf "$CACHED"; fi
mv -T "$SRC" "$CACHED"
UNSTAMPED=1
cleanup() { rm -rf "$WORK"; if [[ "$UNSTAMPED" == 1 ]]; then rm -rf "$CACHED"; fi; }

(
    ulimit -v 8388608
    cd "$CACHED"
    timeout --kill-after=10s 1800s upstream/emscripten/embuilder build sdl3 zlib libpng
    # Link a program that uses the three ports with the flags the build uses
    # (-fwasm-exceptions picks the libpng variant that links): emcc installs the
    # SDL3 CMake config (sysroot/lib/cmake/SDL3) the first time it is asked for
    # the port, which embuilder alone does not, and generates the remaining
    # variants now, while the network is known to be there.
    printf '#include <SDL3/SDL.h>\n#include <png.h>\n#include <zlib.h>\nint main() { return 0; }\n' > "$WORK/ports.cpp"
    env -i timeout --kill-after=10s 1800s upstream/emscripten/emcc -fwasm-exceptions \
        -sUSE_SDL=3 -sUSE_LIBPNG=1 -sUSE_ZLIB=1 "$WORK/ports.cpp" -o "$WORK/ports.js"
)
[[ -f "$CACHED/upstream/emscripten/cache/sysroot/lib/cmake/SDL3/sdl3-config.cmake" ]] \
    || { echo "emsdk: the SDL3 port installed no sdl3-config.cmake" >&2; exit 1; }

# Prove it runs, with an empty environment: no emsdk_env.sh is ever sourced.
emcc_out=$(env -i "$CACHED/upstream/emscripten/emcc" --version 2>&1 | head -1) || true
[[ "$emcc_out" == *"$VERSION"* ]] || { echo "emsdk: emcc --version does not name $VERSION: $emcc_out" >&2; exit 1; }
node_out=$("$CACHED/node/${NODE_VERSION}_64bit/bin/node" --version)
[[ "$node_out" == "v$NODE_VERSION" ]] || { echo "emsdk: node --version is $node_out, expected v$NODE_VERSION" >&2; exit 1; }

printf '%s\n' "$STAMP_BODY" > "$CACHED/$STAMP_NAME"
UNSTAMPED=0
echo "emsdk: installed $CACHED"
