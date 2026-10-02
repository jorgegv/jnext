#!/usr/bin/env bash
# PGO build of one release tree (GH #297): instrument, train, rebuild with the
# profile. Used by the package-* / win-*-release targets, which configure
# their trees with their own commands (cmake, mingw64-cmake, ...). `make
# gui-release` does the same three steps inline, through the configure guard.
#
# Usage: tools/pgo-build.sh <build dir> -- <configure command and args...>
#
#   tools/pgo-build.sh build/rpm-release -- cmake -S . -DCMAKE_BUILD_TYPE=Release ...
#
# The configure command is run twice, with "-B <dir>-pgo-gen -DJNEXT_PGO=GENERATE"
# and with "-B <dir> -DJNEXT_PGO=USE" appended, both pointing JNEXT_PGO_DIR at
# <dir>-pgo-profile. Only the jnext target is built in the instrumented tree;
# the whole optimised tree is built. tools/pgo-train.sh skips the training
# when nothing changed, and the optimised tree is rebuilt from clean whenever
# the profile is new (make cannot see .gcda files as dependencies).
#
# A Windows (.exe) instrumented build is trained under wine: its runtime DLLs
# (and Qt's offscreen platform plugin, for the GUI run) are bundled next to it
# first, and wine runs in its own prefix under build/. No wine is a hard
# error that says how to build without PGO.
#
# Env: JOBS (default: nproc).
set -euo pipefail
export LC_ALL=C LANG=C

die() { echo "pgo-build: ERROR: $*" >&2; exit 1; }

[[ $# -ge 3 && "$2" == "--" ]] || die "usage: $0 <build dir> -- <configure command...>"
DIR=$1
shift 2
GEN="$DIR-pgo-gen"
mkdir -p "$DIR-pgo-profile"
PROF=$(cd "$DIR-pgo-profile" && pwd)
JOBS=${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# 1. Instrumented tree: only the jnext target.
"$@" -B "$GEN" -DJNEXT_PGO=GENERATE "-DJNEXT_PGO_DIR=$PROF"
cmake --build "$GEN" -j"$JOBS" --target jnext

# 2. Train (skipped inside pgo-train.sh when nothing changed).
BIN=""
for b in "$GEN/jnext" "$GEN/jnext.exe" "$GEN/jnext.app/Contents/MacOS/jnext"; do
    [[ -f "$b" ]] && { BIN=$b; break; }
done
[[ -n "$BIN" ]] || die "no instrumented jnext under $GEN"
if [[ "$BIN" == *.exe ]]; then
    command -v wine >/dev/null 2>&1 ||
        die "training the Windows PGO build needs wine (e.g. 'dnf install wine').
pgo-build: Without it, build the non-PGO executable instead (make win-release-non-pgo)."
    BUNDLE_EXTRA_QT_PLUGINS="platforms/qoffscreen.dll" \
        bash "$SCRIPT_DIR/../packaging/windows/bundle-dlls.sh" "$BIN" "$GEN" >/dev/null
    export JNEXT_PGO_RUNNER=wine
    export WINEPREFIX="${WINEPREFIX:-$(cd "$(dirname "$DIR")" && pwd)/wine-pgo}"
    export WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all
    unset DISPLAY WAYLAND_DISPLAY
    if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
        echo "pgo-build: creating the wine prefix $WINEPREFIX"
        timeout --kill-after=5s 600s wineboot -i >/dev/null 2>&1 ||
            die "cannot initialise the wine prefix $WINEPREFIX"
    fi
fi
bash "$SCRIPT_DIR/pgo-train.sh" "$BIN" "$PROF"

# 3. Optimised tree, rebuilt from clean when the profile is new.
"$@" -B "$DIR" -DJNEXT_PGO=USE "-DJNEXT_PGO_DIR=$PROF"
if [[ -f "$DIR/.pgo-applied" ]] && ! [[ "$PROF/.trained" -nt "$DIR/.pgo-applied" ]]; then
    cmake --build "$DIR" -j"$JOBS"
else
    rm -f "$DIR/.pgo-applied"
    echo "pgo-build: new profile — rebuilding $DIR from clean"
    cmake --build "$DIR" -j"$JOBS" --clean-first
fi
touch "$DIR/.pgo-applied"
