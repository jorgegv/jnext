#!/usr/bin/env bash
# Produce source packages suitable for rpmbuild/debuild AND for the GitHub
# Release: a plain `git archive` does NOT include git-submodule content
# (third_party/spdlog is a gitlink, not tracked files), so a naive archive
# configures cleanly but fails at `add_subdirectory(third_party/spdlog)`
# with "does not contain a CMakeLists.txt file" (found while verifying
# packaging/rpm/jnext.spec, Task 67). This script vendors submodule
# content into the staging tree, matching what `git clone --recursive` gives,
# then emits BOTH:
#   * v<ver>.tar.gz        — the rpmbuild/debuild source tarball
#                            (packaging/rpm/jnext.spec Source0 refers to it)
#   * jnext-<ver>-src.zip  — the multiplatform source zip attached to the
#                            GitHub Release
# Both wrap the same `jnext-<ver>/` prefixed tree.
#
# Usage: packaging/make-dist-tarball.sh [OUTPUT_DIR]
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)"
VERSION="$(grep '^version:' "$ROOT/version.yaml" | awk '{print $2}')"
OUT_DIR="${1:-$ROOT}"
mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"   # absolute: zip runs from inside $TMP below
PREFIX="jnext-$VERSION"
TARBALL="$OUT_DIR/v$VERSION.tar.gz"
SRCZIP="$OUT_DIR/jnext-$VERSION-src.zip"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

DEST="$TMP/$PREFIX"
mkdir -p "$DEST"
git -C "$ROOT" archive HEAD | tar -x -C "$DEST"
git -C "$ROOT" submodule update --init --recursive
git -C "$ROOT" submodule foreach --recursive --quiet '
    mkdir -p "'"$DEST"'/$path"
    git archive HEAD | tar -x -C "'"$DEST"'/$path"
'

tar -C "$TMP" -czf "$TARBALL" "$PREFIX"
echo "Wrote $TARBALL"

rm -f "$SRCZIP"
( cd "$TMP" && zip -qr "$SRCZIP" "$PREFIX" )
echo "Wrote $SRCZIP"
