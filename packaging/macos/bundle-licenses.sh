#!/usr/bin/env bash
#
# bundle-licenses.sh <jnext.app> <dest-licenses-dir>
#
# Ship the licence files of every Homebrew library that macdeployqt copied into
# the bundle: for each top-level entry of Contents/Frameworks (dylib or
# *.framework), find the formula that owns it under $HOMEBREW_PREFIX/opt/*/lib
# and copy that keg's LICENSE*/COPYING*/LICENCE* files and LICENSES/ directory
# into <dest>/bundled/<formula>/. Some kegs carry none (Homebrew copies only
# top-level files, and Qt keeps its texts elsewhere): packaging/macos/
# license-fallbacks/<key>/ then supplies them, key = the formula name, or "qt"
# for every qt* formula. A library no formula owns, or a formula with no licence
# file and no fallback, fails the build. Run before codesign, like the other bundle steps.
#
set -euo pipefail

[ "$#" -eq 2 ] || { echo "usage: $0 <jnext.app> <dest-licenses-dir>" >&2; exit 2; }
APP=$1; DEST=$2
FW="$APP/Contents/Frameworks"
[ -d "$FW" ] || { echo "error: no Frameworks dir in $APP" >&2; exit 1; }
rm -rf "$DEST/bundled"
FALLBACKS=${LICENSE_FALLBACKS:-$(cd "$(dirname "$0")" && pwd)/license-fallbacks}
BREW_PREFIX=${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}

for e in "$FW"/*; do
    base=$(basename "$e")
    formula=""
    for hit in "$BREW_PREFIX"/opt/*/lib/"$base"; do
        [ -e "$hit" ] || continue
        formula=$(basename "${hit%/lib/"$base"}")
        break
    done
    [ -n "$formula" ] || { echo "error: no Homebrew formula owns $base (cannot ship its licence)" >&2; exit 1; }
    out="$DEST/bundled/$formula"
    [ -d "$out" ] && continue
    mkdir -p "$out"
    keg="$BREW_PREFIX/opt/$formula"
    for f in "$keg"/LICENSE* "$keg"/COPYING* "$keg"/LICENCE* "$keg"/LICENSES/*; do
        if [ -f "$f" ]; then cp -f "$f" "$out/$(basename "$f")"; fi
    done
    key=${formula%%@*}
    case "$key" in qt*) key=qt ;; esac
    if [ -z "$(ls -A "$out")" ] && [ -d "$FALLBACKS/$key" ]; then cp -f "$FALLBACKS/$key"/* "$out/"; fi
    [ -n "$(ls -A "$out")" ] || { echo "error: formula $formula ($base) has no licence file in $keg and no fallback" >&2; exit 1; }
done
echo "bundle-licenses: licences shipped under $DEST/bundled"
