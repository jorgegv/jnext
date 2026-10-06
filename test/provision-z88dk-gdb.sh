#!/usr/bin/env bash
#
# provision-z88dk-gdb.sh — provide the z88dk-gdb client the regression row
# `gdb-z88dk-func` drives, on any machine, the same way locally and in CI.
#
# WHY. z88dk is not packaged by Fedora, and the row used to SKIP wherever the
# client was absent (CI included), which left the project's one acceptance test
# of the GDB RSP frontend unrun there. A SKIP now fails the run, so the client
# is provisioned like the SD image: jnext's `make regression` runs this first
# (`make z88dk-gdb`), it is a no-op once cached, and it needs the network once.
#
# WHAT. z88dk v2.4's release SOURCE tarball (z88dk-src-2.4.tgz — unlike a
# GitHub tag archive it carries the ext/uthash submodule), pinned by sha256 and
# verified BEFORE it is unpacked. Only five paths are extracted and only
# `z88dk-gdb` is built (cc + make; the tarball ships the generated lexer and
# parser, so flex/bison are not needed) — about 4 s in all. The result is
# installed into the user cache, never committed and never shipped, so there is
# no redistribution (z88dk is under the Clarified Artistic License).
#
# HOW THE PIN WAS VERIFIED (2026-10-06): the sha256 below was computed from two
# independent downloads of the GitHub release asset (55 296 853 bytes). z88dk
# publishes no checksum file next to the asset.
#
# USAGE
#   bash test/provision-z88dk-gdb.sh               resolve, else download+build
#   bash test/provision-z88dk-gdb.sh --print-path  resolve only: print the path
#                                                  and exit 0, or exit 1
#
# RESOLUTION ORDER (the one place it is written down; the row calls --print-path):
#   $Z88DK_GDB, z88dk-gdb on PATH, $HOME/src/spectrum/z88dk/bin/z88dk-gdb,
#   then the cache: ${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools/z88dk-gdb-2.4/z88dk-gdb
#
# Env: JNEXT_Z88DK_GDB_SHA256 / JNEXT_Z88DK_GDB_URL override the pin / source
#      (the harness uses the first to prove a wrong checksum is refused).
set -euo pipefail

# Children's output must not depend on the user's locale (CLAUDE.md).
export LC_ALL=C LANG=C

VERSION=2.4
URL="${JNEXT_Z88DK_GDB_URL:-https://github.com/z88dk/z88dk/releases/download/v${VERSION}/z88dk-src-${VERSION}.tgz}"
SHA256="${JNEXT_Z88DK_GDB_SHA256:-96a57a01d44ff1d65d84e38b04aebb0a4e10eccb4845cb71f5a26f10abe7c5ac}"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools"
CACHED="$CACHE_ROOT/z88dk-gdb-$VERSION/z88dk-gdb"

# An explicit override that is not usable is an error, never a quiet fall-through
# to some other client: the user asked for THAT one.
if [[ -n "${Z88DK_GDB:-}" && ! -x "$Z88DK_GDB" ]]; then
    echo "z88dk-gdb: Z88DK_GDB='$Z88DK_GDB' is not an executable file" >&2
    exit 2
fi

resolve() {
    local c
    if [[ -n "${Z88DK_GDB:-}" && -x "$Z88DK_GDB" ]]; then echo "$Z88DK_GDB"; return 0; fi
    if c=$(command -v z88dk-gdb 2>/dev/null) && [[ -x "$c" ]]; then echo "$c"; return 0; fi
    for c in "$HOME/src/spectrum/z88dk/bin/z88dk-gdb" "$CACHED"; do
        if [[ -x "$c" ]]; then echo "$c"; return 0; fi
    done
    return 1
}

if [[ "${1:-}" == "--print-path" ]]; then
    resolve
    exit
fi

if found=$(resolve); then
    echo "z88dk-gdb: using $found"
    exit 0
fi

echo "z88dk-gdb: not found; building z88dk $VERSION's client into $CACHED"

missing=""
for tool in curl sha256sum tar make cc; do
    command -v "$tool" >/dev/null 2>&1 || missing+=" $tool"
done
if [[ -n "$missing" ]]; then
    echo "z88dk-gdb: required tool(s) missing:$missing" >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
WORK=$(mktemp -d "$CACHE_ROOT/z88dk-build.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

TARBALL="$WORK/z88dk-src-$VERSION.tgz"
curl -fsSL --retry 3 --max-time 300 -o "$TARBALL" "$URL"

# Verify BEFORE unpacking. A checksum checked after the fact is theatre.
actual=$(sha256sum "$TARBALL" | cut -d' ' -f1)
if [[ "$actual" != "$SHA256" ]]; then
    echo "z88dk-gdb: SHA-256 MISMATCH for $URL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $actual" >&2
    exit 1
fi
echo "z88dk-gdb: sha256 verified"

tar -xzf "$TARBALL" -C "$WORK" \
    z88dk/src/ticks z88dk/src/common z88dk/src/Make.common z88dk/ext/uthash z88dk/LICENSE
rm -f "$TARBALL"

make -C "$WORK/z88dk/src/ticks" z88dk-gdb >"$WORK/build.log" 2>&1 || {
    echo "z88dk-gdb: build failed:" >&2; tail -n 20 "$WORK/build.log" >&2; exit 1; }
built="$WORK/z88dk/src/ticks/z88dk-gdb"
[[ -x "$built" ]] || { echo "z88dk-gdb: build produced no $built" >&2; exit 1; }

# Prove it runs before installing: -h prints a banner naming the program.
banner=$("$built" -h 2>&1 </dev/null || true)
if ! grep -qF 'z88dk-gdb, a gdb client' <<<"$banner"; then
    echo "z88dk-gdb: the built client does not run (no banner from -h)" >&2
    exit 1
fi

mkdir -p "$(dirname "$CACHED")"
# mktemp in the target directory (never a fixed name): two first-time runs at
# once must not trample each other's half-copied file, and mv is atomic there.
inst=$(mktemp "$(dirname "$CACHED")/z88dk-gdb.XXXXXX")
cp "$built" "$inst"
chmod 755 "$inst"
mv -f "$inst" "$CACHED"
echo "z88dk-gdb: installed $CACHED"
