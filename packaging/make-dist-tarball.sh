#!/usr/bin/env bash
# Produce a source tarball suitable for rpmbuild/debuild: a plain
# `git archive` does NOT include git-submodule content (third_party/spdlog,
# third_party/zot are gitlinks, not tracked files), so a naive tarball
# configures cleanly but fails at `add_subdirectory(third_party/spdlog)`
# with "does not contain a CMakeLists.txt file" (found while verifying
# packaging/rpm/jnext.spec, Task 67). This script vendors submodule
# content into the tarball, matching what `git clone --recursive` gives.
#
# Usage: packaging/make-dist-tarball.sh [OUTPUT_DIR]
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)"
VERSION="$(grep '^version:' "$ROOT/version.yaml" | awk '{print $2}')"
OUT_DIR="${1:-$ROOT}"
PREFIX="jnext-$VERSION"
TARBALL="$OUT_DIR/v$VERSION.tar.gz"

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
