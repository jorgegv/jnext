#!/usr/bin/env bash
# Run a Windows test executable under wine (GH #214): the runner behind
# `make unit-test-win`, handed to test/run-unit-tests.sh as JNEXT_TEST_RUNNER.
#
#   wine-run.sh --init <build_dir>     create / refresh the build's wine prefix
#   wine-run.sh <build_dir>/test/X.exe [args...]
#
# The environment is the one tools/pgo-build.sh trains the Windows PGO build
# in: a private WINEPREFIX under the build tree, no wine debug noise, no Mono /
# Gecko prompt, and NO DISPLAY / WAYLAND_DISPLAY / XDG_RUNTIME_DIR, so nothing
# can ever open a window on the developer's desktop. The DLLs the executables
# link (SDL3, libpng, zlib, winpthreads, libstdc++ ...) are found through
# WINEPATH -- the MinGW sys-root and the build's own spdlog DLL -- rather than
# copied beside ninety executables.
#
# What wine cannot stand in for (accepted, owner 2026-10-10; documented in the
# developer guide): symlink creation, a real console attach for a GUI-subsystem
# exe, NTFS specifics (short names, streams).
set -euo pipefail

if [[ "${1:-}" == --init ]]; then
    BUILD=$(cd "${2:?usage: wine-run.sh --init <build_dir>}" && pwd)
else
    EXE=${1:?usage: wine-run.sh <exe> [args...]}
    BUILD=$(cd "$(dirname "$EXE")/.." && pwd)
fi

SYSROOT=${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
export WINEPREFIX="${WINEPREFIX:-$BUILD/wine-prefix}"
export WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all LC_ALL=C.UTF-8 LANG=C.UTF-8
export WINEPATH="$SYSROOT/bin;$BUILD/third_party/spdlog"
unset DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR

# As root (the CI container) wine would write through a read-only file: wine
# emulates the Windows read-only attribute with a permission bit, and root's
# CAP_DAC_OVERRIDE / CAP_DAC_READ_SEARCH ignore permission bits -- where real
# Windows refuses even an administrator. So root drops those two capabilities
# from the bounding set for everything wine starts -- wineserver included, which
# is the process that opens the files -- the same thing the Linux suites do
# around SD-28 (test/dac_caps_dropped.h). Without setpriv the run cannot be
# faithful, so it refuses rather than pass or fail by accident.
PRIV=()
if [[ "$(id -u)" == 0 ]]; then
    command -v setpriv >/dev/null 2>&1 \
        || { echo "wine-run: running as root needs setpriv (util-linux) to drop CAP_DAC_OVERRIDE" >&2; exit 2; }
    PRIV=(setpriv --bounding-set=-dac_override,-dac_read_search)
fi

if [[ "${1:-}" == --init ]]; then
    command -v wine >/dev/null 2>&1 || { echo "wine-run: wine is not installed (dnf install wine-core wine-common)" >&2; exit 2; }
    # A wineserver left over from a run that kept the capabilities would serve us.
    [[ ${#PRIV[@]} -eq 0 ]] || wineserver -k >/dev/null 2>&1 || true
    # Refreshed every time (wineboot -u) once it exists, as pgo-build.sh does.
    if [[ -f "$WINEPREFIX/system.reg" ]]; then flag=-u; else flag=-i; fi
    timeout --kill-after=5s 600s ${PRIV[@]+"${PRIV[@]}"} wineboot "$flag" >/dev/null 2>&1 \
        || { echo "wine-run: cannot initialise the wine prefix $WINEPREFIX" >&2; exit 2; }
    exit 0
fi

exec ${PRIV[@]+"${PRIV[@]}"} wine "$@"
