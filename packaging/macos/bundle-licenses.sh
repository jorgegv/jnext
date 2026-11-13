#!/usr/bin/env bash
#
# bundle-licenses.sh <jnext.app> <dest-licenses-dir>
#
# Ship the licence files of every Homebrew library that macdeployqt copied into
# the bundle: for each top-level entry of Contents/Frameworks (dylib or
# *.framework), find the formula that owns it under $HOMEBREW_PREFIX/opt/*/lib
# and copy that keg's LICENSE*/COPYING*/LICENCE* files and LICENSES/ directory
# into <dest>/bundled/<formula>/. A library no formula owns, or a formula with no
# licence file, fails the build. Run before codesign, like the other bundle steps.
#
set -euo pipefail

[ "$#" -eq 2 ] || { echo "usage: $0 <jnext.app> <dest-licenses-dir>" >&2; exit 2; }
APP=$1; DEST=$2
FW="$APP/Contents/Frameworks"
[ -d "$FW" ] || { echo "error: no Frameworks dir in $APP" >&2; exit 1; }
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
        [ -f "$f" ] && cp -f "$f" "$out/$(basename "$f")"
    done
    [ -n "$(ls -A "$out")" ] || { echo "error: formula $formula ($base) has no licence file in $keg" >&2; exit 1; }
done
echo "bundle-licenses: licences shipped under $DEST/bundled"
