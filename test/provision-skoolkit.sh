#!/usr/bin/env bash
#
# provision-skoolkit.sh - provide the skoolkit tools (sna2skool.py, skool2asm.py,
# skool2ctl.py) the regression row `skoolkit-foreign-func` drives, on any
# machine, the same way locally and in CI (GH #389).
#
# WHY. jnext's skoolkit control-file export is proved by running skoolkit
# itself on it, never by jnext's own reader. Fedora does not package skoolkit,
# and a SKIP fails the run, so the tools are provisioned like the SD image and
# z88dk-gdb: `make regression` runs this first (`make skoolkit`), it is a no-op
# once cached, and it needs the network once.
#
# WHAT. SkoolKit 10.1's source distribution from PyPI (skoolkit-10.1.tar.gz,
# 1.3 MB), pinned by sha256 and verified BEFORE it is unpacked. It runs from the
# unpacked directory with plain `python3` (no pip, no venv, no compiler: the C
# accelerator is optional). The result is installed into the user cache, used at
# test time only, never committed and never shipped (SkoolKit is GPLv3).
#
# HOW THE PIN WAS VERIFIED (2026-11-02): the sha256 below is the digest PyPI's
# JSON API publishes for the sdist, and two independent downloads agree.
#
# USAGE
#   bash test/provision-skoolkit.sh               resolve, else download+unpack
#   bash test/provision-skoolkit.sh --print-path  resolve only: print the directory
#                                                 holding sna2skool.py, or exit 1
#
# RESOLUTION: only the cache, ${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools/skoolkit-10.1/
# (the rows depend on this exact version's behaviour, so whatever skoolkit is on
# PATH is deliberately not used).
#
# Env: JNEXT_SKOOLKIT_SHA256 / JNEXT_SKOOLKIT_URL override the pin / source
#      (the harness uses them to prove a wrong checksum is refused).
set -euo pipefail

# Children's output must not depend on the user's locale (CLAUDE.md).
export LC_ALL=C LANG=C

VERSION=10.1
URL="${JNEXT_SKOOLKIT_URL:-https://files.pythonhosted.org/packages/source/s/skoolkit/skoolkit-${VERSION}.tar.gz}"
SHA256="${JNEXT_SKOOLKIT_SHA256:-e71f0bac2bd38de1eacbb58650e95d43082351a1e9ecb8edecb11da112d02383}"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools"
CACHED="$CACHE_ROOT/skoolkit-$VERSION"

resolve() {
    if [[ -f "$CACHED/sna2skool.py" ]]; then echo "$CACHED"; return 0; fi
    return 1
}

if [[ "${1:-}" == "--print-path" ]]; then
    resolve
    exit
fi

if found=$(resolve); then
    echo "skoolkit: using $found"
    exit 0
fi

echo "skoolkit: not found; installing SkoolKit $VERSION into $CACHED"

missing=""
for tool in curl sha256sum tar python3; do
    command -v "$tool" >/dev/null 2>&1 || missing+=" $tool"
done
if [[ -n "$missing" ]]; then
    echo "skoolkit: required tool(s) missing:$missing" >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
WORK=$(mktemp -d "$CACHE_ROOT/skoolkit-build.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

TARBALL="$WORK/skoolkit-$VERSION.tar.gz"
curl -fsSL --retry 3 --max-time 300 -o "$TARBALL" "$URL"

# Verify BEFORE unpacking. A checksum checked after the fact is theatre.
actual=$(sha256sum "$TARBALL" | cut -d' ' -f1)
if [[ "$actual" != "$SHA256" ]]; then
    echo "skoolkit: SHA-256 MISMATCH for $URL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $actual" >&2
    exit 1
fi
echo "skoolkit: sha256 verified"

mkdir "$WORK/unpack"
tar -xzf "$TARBALL" -C "$WORK/unpack"
rm -f "$TARBALL"
src="$WORK/unpack/skoolkit-$VERSION"
[[ -f "$src/sna2skool.py" ]] || { echo "skoolkit: the archive holds no skoolkit-$VERSION/sna2skool.py" >&2; exit 1; }

# Prove it runs before installing.
ver=$(python3 "$src/sna2skool.py" --version 2>&1 </dev/null || true)
if [[ "$ver" != "SkoolKit $VERSION" ]]; then
    echo "skoolkit: the unpacked sna2skool.py does not report 'SkoolKit $VERSION' (got: $ver)" >&2
    exit 1
fi

# mv is atomic within one filesystem: a directory that exists is complete, and
# two first-time runs at once cannot leave a half-copied one.
mv -T "$src" "$CACHED" 2>/dev/null || { resolve >/dev/null || { echo "skoolkit: could not install $CACHED" >&2; exit 1; }; }
echo "skoolkit: installed $CACHED"
