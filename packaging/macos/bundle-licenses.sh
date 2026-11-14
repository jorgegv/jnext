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
# MacPorts mode (MACPORTS_PREFIX set, the Intel build): the owner is the port
# `port provides` names for the library's file under $MACPORTS_PREFIX, the licence
# files are those of `port contents <port>` named LICENSE*/COPYING*/LICENCE* or
# under a LICENSES/ directory, and the fallback key is "qt" for every qt6-* port,
# "glib" for glib2, "sqlite" for sqlite3, else the port name. The same two hard
# failures apply, so a library that leaked in from another prefix (the runner's
# own Homebrew in /usr/local) fails here as unowned. MACPORTS_PREFIX unset:
# Homebrew, exactly as above.
#
set -euo pipefail

[ "$#" -eq 2 ] || { echo "usage: $0 <jnext.app> <dest-licenses-dir>" >&2; exit 2; }
APP=$1; DEST=$2
FW="$APP/Contents/Frameworks"
[ -d "$FW" ] || { echo "error: no Frameworks dir in $APP" >&2; exit 1; }
rm -rf "$DEST/bundled"
FALLBACKS=${LICENSE_FALLBACKS:-$(cd "$(dirname "$0")" && pwd)/license-fallbacks}
MP_PREFIX=${MACPORTS_PREFIX:-}
if [ -z "$MP_PREFIX" ]; then
    BREW_PREFIX=${HOMEBREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}
fi

# macports_owner <entry-basename> - the port that provides it, or empty.
# A framework is looked up by its real binary (Versions/A/<name>): the registry
# lists files, not directories.
macports_owner() {
    local base=$1 name cand line
    name=${base%.framework}
    if [ "$name" != "$base" ]; then
        set -- "$MP_PREFIX/libexec/qt6/lib/$base/Versions/A/$name" \
               "$MP_PREFIX/lib/$base/Versions/A/$name" \
               "$MP_PREFIX/Library/Frameworks/$base/Versions/A/$name"
    else
        # <prefix>/libexec/<port>/lib is MacPorts' analogue of Homebrew's opt/*/lib
        # (openssl3 keeps its libraries in libexec/openssl3/lib, Qt in libexec/qt6/lib).
        set -- "$MP_PREFIX/lib/$base"
        for cand in "$MP_PREFIX"/libexec/*/lib/"$base"; do
            [ -e "$cand" ] && set -- "$@" "$cand"
        done
    fi
    for cand in "$@"; do
        # `port -q provides` prints ONLY the bare port name (MacPorts 2.12.7
        # port.tcl action_provides: the "is provided by:" sentence is the non-quiet
        # form), and nothing at all when no port provides the file.
        line=$(port -q provides "$cand" 2>/dev/null | head -1 || true)
        if [ -n "$line" ]; then echo "${line%% *}"; return 0; fi
    done
    echo ""
}

macports_licences() {   # macports_licences <port> <outdir>
    local f
    while IFS= read -r f; do
        f=${f#"${f%%[![:space:]]*}"}
        [ -f "$f" ] || continue
        case "$(basename "$f")" in
            LICENSE*|COPYING*|LICENCE*) cp -f "$f" "$2/$(basename "$f")" ;;
            *) case "$f" in */LICENSES/*) cp -f "$f" "$2/$(basename "$f")" ;; esac ;;
        esac
    done < <(port -q contents "$1" 2>/dev/null || true)
}

# MacPorts mode reports EVERY unowned or licence-less library before failing, so
# one CI run shows the whole list instead of one library per round trip.
mp_failed=0

for e in "$FW"/*; do
    base=$(basename "$e")
    if [ -n "$MP_PREFIX" ]; then
        port=$(macports_owner "$base")
        if [ -z "$port" ]; then
            echo "error: no MacPorts port owns $base (cannot ship its licence)" >&2
            mp_failed=1; continue
        fi
        out="$DEST/bundled/$port"
        [ -d "$out" ] && continue
        mkdir -p "$out"
        macports_licences "$port" "$out"
        key=$port
        case "$key" in qt6-*) key=qt ;; glib2) key=glib ;; sqlite3) key=sqlite ;; esac
        if [ -z "$(ls -A "$out")" ] && [ -d "$FALLBACKS/$key" ]; then cp -f "$FALLBACKS/$key"/* "$out/"; fi
        if [ -z "$(ls -A "$out")" ]; then
            echo "error: port $port ($base) has no licence file in its contents and no fallback" >&2
            mp_failed=1
        fi
        continue
    fi
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
[ "$mp_failed" -eq 0 ] || exit 1
echo "bundle-licenses: licences shipped under $DEST/bundled"
