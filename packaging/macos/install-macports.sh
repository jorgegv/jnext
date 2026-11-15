#!/usr/bin/env bash
#
# install-macports.sh [port...]
#
# Install MacPorts and the jnext build dependencies on an Intel macOS runner
# (GH #494). Homebrew builds no Intel bottles any more, and a source build of
# Qt would take hours on an unsupported tier; MacPorts publishes x86_64 binary
# archives for every dependency. Run the build with MACPORTS_PREFIX=/opt/local
# afterwards (make package-macos / make unit-test-sdl read it).
#
# The installer is pinned by version and sha256, like the other tools CI
# downloads. `port -b` installs from binary archives ONLY: a port whose archive
# is not published yet fails this step, in seconds, instead of starting a source
# build. Without arguments the ports are the ones the macOS package and the
# SDL-only unit suites need.
#
set -euo pipefail

VERSION=2.12.7
PKG="MacPorts-$VERSION-15-Sequoia.pkg"
URL="https://github.com/macports/macports-base/releases/download/v$VERSION/$PKG"
SHA256=87b2e87ed7adc4aca947b42eb7ff991614cc0edfccb486237f2bbc75f28aae63
PREFIX=/opt/local

[ "$(uname -s)" = Darwin ] || { echo "install-macports: macOS only" >&2; exit 1; }

PORTS=("$@")
[ "${#PORTS[@]}" -gt 0 ] || PORTS=(qt6-qtbase qt6-qtsvg SDL3 libpng openssl3 bash coreutils grep)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if [ ! -x "$PREFIX/bin/port" ]; then
    curl -fsSL --retry 3 -o "$tmp/$PKG" "$URL"
    echo "$SHA256  $tmp/$PKG" | shasum -a 256 -c -
    sudo installer -pkg "$tmp/$PKG" -target /
fi

# GitHub Actions: later steps find `port` and the installed tools.
if [ -n "${GITHUB_PATH:-}" ]; then
    echo "$PREFIX/bin" >> "$GITHUB_PATH"
    echo "$PREFIX/sbin" >> "$GITHUB_PATH"
fi

sudo "$PREFIX/bin/port" -N selfupdate
sudo "$PREFIX/bin/port" -N -b install "${PORTS[@]}"
echo "install-macports: installed ${PORTS[*]} under $PREFIX"
