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
# are bundled next to it first, and wine runs in its own prefix under build/.
# No wine is a hard error that says how to build without PGO. The GUI run of
# the training set is left out there (JNEXT_PGO_NO_GUI=1): in a display-less
# container (release.yml's) the Qt exe under wine reaches its automatic exit
# and then never terminates, so the run could only time out. The headless
# runs are unaffected, and -fprofile-partial-training keeps the untrained GUI
# code optimised as it is without PGO.
#
# A Windows build then runs the FUSE Z80 suite under wine against the
# optimised tree and fails unless every case passes (step 4).
#
# Env: JOBS (default: nproc); FUSE_CASES (the FUSE case count, default 1356).
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
# What pgo-train.sh says when the SD image is missing and cannot be fetched.
if [[ "$BIN" == *.exe ]]; then
    export JNEXT_PGO_NO_SD_HINT="Build the Windows executable without PGO instead:  WIN_PGO=0 (e.g. make win-release-non-pgo)"
else
    export JNEXT_PGO_NO_SD_HINT="This package is always a PGO build and REQUIRES the SD image: provide ~/.jnext/sdcard/cspect-next-1gb-fixed.img (e.g. run jnext once with --sdcard-download-confirm) or fix the network, then retry."
fi
if [[ "$BIN" == *.exe ]]; then
    command -v wine >/dev/null 2>&1 ||
        die "training the Windows PGO build needs wine (e.g. 'dnf install wine').
pgo-build: Without it, build the non-PGO executable instead (make win-release-non-pgo)."
    bash "$SCRIPT_DIR/../packaging/windows/bundle-dlls.sh" "$BIN" "$GEN" >/dev/null
    export JNEXT_PGO_RUNNER=wine JNEXT_PGO_NO_GUI=1
    export WINEPREFIX="${WINEPREFIX:-$(cd "$(dirname "$DIR")" && pwd)/wine-pgo}"
    export WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all
    unset DISPLAY WAYLAND_DISPLAY
    # An existing prefix is refreshed every time (wineboot -u), not only
    # created once: a prefix made while the host wine was broken keeps that
    # state after wine is fixed. One made while the i386 d3d11/dxgi links were
    # missing had no d3d11.dll in syswow64, so the i686 Qt5 training kept
    # failing (Qt5Gui import, exit 53) until `wineboot -u` repaired it.
    if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
        echo "pgo-build: creating the wine prefix $WINEPREFIX"
        timeout --kill-after=5s 600s wineboot -i >/dev/null 2>&1 ||
            die "cannot initialise the wine prefix $WINEPREFIX"
    else
        timeout --kill-after=5s 600s wineboot -u >/dev/null 2>&1 ||
            die "cannot update the wine prefix $WINEPREFIX"
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

# 4. Windows: the FUSE Z80 suite must pass against the PGO+LTO CPU core under
# wine, or the build fails (GH #298). The same contract as the Flatpak's
# in-build FUSE gate: a different toolchain plus LTO is where the Flatpak
# miscompile came from. fuse_z80_test is built in this tree with its flags
# (EXCLUDE_FROM_ALL, linked against the same jnext_cpu objects); its DLLs are
# bundled beside it first. LANG=C/LC_ALL=C are exported above, so wine and the
# test inherit them. FUSE_CASES is the suite's case count (the Makefile passes
# its own).
if [[ "$BIN" == *.exe ]]; then
    FUSE_CASES=${FUSE_CASES:-1356}
    cmake --build "$DIR" -j"$JOBS" --target fuse_z80_test
    bash "$SCRIPT_DIR/../packaging/windows/bundle-dlls.sh" "$DIR/fuse_z80_test.exe" "$DIR" >/dev/null
    FUSE_LOG="$DIR/fuse-wine.log"
    rc=0
    (cd "$DIR" && timeout --kill-after=5s 600s wine fuse_z80_test.exe "$SCRIPT_DIR/../test/fuse") \
        >"$FUSE_LOG" 2>&1 || rc=$?
    if [[ $rc -ne 0 ]] || ! grep -Eq "^Total: +$FUSE_CASES +Passed: +$FUSE_CASES " "$FUSE_LOG"; then
        tail -20 "$FUSE_LOG" >&2
        die "FUSE Z80 on the Windows PGO build under wine: exit $rc, expected $FUSE_CASES of $FUSE_CASES passed (log: $FUSE_LOG)"
    fi
    echo "pgo-build: FUSE Z80 under wine: $FUSE_CASES/$FUSE_CASES"
fi
