#!/usr/bin/env bash
#
# provision-ghidra.sh - provide Ghidra (headless) and its Jython extension, the
# foreign reader the regression row `ghidra-foreign-func` runs jnext's symbol
# export through, on any machine, the same way locally and in CI (GH #389).
#
# WHY. jnext's Ghidra symbol export is proved by running Ghidra's own
# ImportSymbolsScript.py on it (a script that aborts at the first line it cannot
# place was only found by running it), never by jnext's own reader. Ghidra is
# not packaged by Fedora, and a SKIP fails the run, so it is provisioned like
# the SD image and z88dk-gdb: `make regression` runs this first (`make ghidra`),
# it is a no-op once cached, and it needs the network once.
#
# WHAT. Ghidra 12.2's release zip (about 570 MB, 880 MB unpacked), pinned by
# sha256 and verified BEFORE it is unpacked, plus the Jython extension that
# zip bundles (since Ghidra 12.1 the script is delivered only by that
# extension, which is not installed by default). The result is installed into
# the user cache, used at test time only, never committed and never shipped
# (Ghidra is Apache-2.0). Running it needs a JDK 25 or newer (`java-25-openjdk-devel`
# on Fedora): the row checks for it and FAILS without one.
#
# HOW THE PIN WAS VERIFIED (2026-11-02): the sha256 below is the one the
# release notes publish (release Ghidra_12.2_build), and the download matched it.
#
# USAGE
#   bash test/provision-ghidra.sh               resolve, else download+unpack
#   bash test/provision-ghidra.sh --print-path  resolve only: print the Ghidra
#                                               directory (the one holding
#                                               support/analyzeHeadless), or exit 1
#
# RESOLUTION: only the cache, ${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools/ghidra-12.2/
# (the rows depend on this exact version's behaviour, so whatever Ghidra is
# installed elsewhere is deliberately not used).
#
# Env: JNEXT_GHIDRA_SHA256 / JNEXT_GHIDRA_URL override the pin / source (the
#      harness uses them to prove a wrong checksum is refused).
set -euo pipefail

# Children's output must not depend on the user's locale (CLAUDE.md).
export LC_ALL=C LANG=C

VERSION=12.2
URL="${JNEXT_GHIDRA_URL:-https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_${VERSION}_build/ghidra_${VERSION}_PUBLIC_20261016.zip}"
SHA256="${JNEXT_GHIDRA_SHA256:-2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67}"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools"
CACHED="$CACHE_ROOT/ghidra-$VERSION"

resolve() {
    if [[ -x "$CACHED/support/analyzeHeadless" ]]; then echo "$CACHED"; return 0; fi
    return 1
}

if [[ "${1:-}" == "--print-path" ]]; then
    resolve
    exit
fi

if found=$(resolve); then
    echo "ghidra: using $found"
    exit 0
fi

echo "ghidra: not found; installing Ghidra $VERSION into $CACHED (a 570 MB download)"

missing=""
for tool in curl sha256sum unzip; do
    command -v "$tool" >/dev/null 2>&1 || missing+=" $tool"
done
if [[ -n "$missing" ]]; then
    echo "ghidra: required tool(s) missing:$missing" >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"
WORK=$(mktemp -d "$CACHE_ROOT/ghidra-build.XXXXXX")
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

ZIP="$WORK/ghidra.zip"
curl -fsSL --retry 3 --max-time 1800 -o "$ZIP" "$URL"

# Verify BEFORE unpacking. A checksum checked after the fact is theatre.
actual=$(sha256sum "$ZIP" | cut -d' ' -f1)
if [[ "$actual" != "$SHA256" ]]; then
    echo "ghidra: SHA-256 MISMATCH for $URL" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $actual" >&2
    exit 1
fi
echo "ghidra: sha256 verified"

mkdir "$WORK/unpack"
unzip -q "$ZIP" -d "$WORK/unpack"
rm -f "$ZIP"
src=$(echo "$WORK"/unpack/ghidra_*)
[[ -x "$src/support/analyzeHeadless" ]] || { echo "ghidra: the archive holds no ghidra_*/support/analyzeHeadless" >&2; exit 1; }

# The Jython extension the release bundles, unpacked where Ghidra looks for
# installed extensions (what File > Install Extensions does).
jy=$(echo "$src"/Extensions/Ghidra/*_Jython.zip)
[[ -f "$jy" ]] || { echo "ghidra: the release bundles no *_Jython.zip extension" >&2; exit 1; }
unzip -q "$jy" -d "$src/Ghidra/Extensions"
[[ -f "$src/Ghidra/Extensions/Jython/ghidra_scripts/ImportSymbolsScript.py" ]] \
    || { echo "ghidra: the Jython extension holds no ImportSymbolsScript.py" >&2; exit 1; }

# mv is atomic within one filesystem: a directory that exists is complete, and
# two first-time runs at once cannot leave a half-copied one.
mv -T "$src" "$CACHED" 2>/dev/null || { resolve >/dev/null || { echo "ghidra: could not install $CACHED" >&2; exit 1; }; }
echo "ghidra: installed $CACHED"
