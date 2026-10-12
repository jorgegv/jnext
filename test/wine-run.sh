#!/usr/bin/env bash
# Run a Windows test executable under wine (GH #214): the runner behind
# `make unit-test-win`, handed to test/run-unit-tests.sh as JNEXT_TEST_RUNNER.
#
#   wine-run.sh --init <build_dir>     create / refresh the build's wine prefix
#   wine-run.sh <build_dir>/test/X.exe [args...]
#   wine-run.sh --serve <build_dir>    start a persistent wineserver (see below)
#   wine-run.sh --jnext <build_dir>/jnext.exe [args...]
#                                      run a jnext.exe as the regression suite's $JNEXT
#                                      (GH #319): the unit-test mode above plus the
#                                      differences listed at the bottom of this header
#
# The environment is the one tools/pgo-build.sh trains the Windows PGO build
# in: a private WINEPREFIX under the build tree, no wine debug noise, no Mono /
# Gecko prompt, and NO DISPLAY / WAYLAND_DISPLAY / XDG_RUNTIME_DIR, so nothing
# can ever open a window on the developer's desktop. The DLLs the executables
# link (SDL3, libpng, zlib, winpthreads, libstdc++ ...) are found through
# WINEPATH -- the MinGW sys-root and the build's own spdlog DLL -- rather than
# copied beside ninety executables.
#
# --jnext mode (`make regression-win`), each point MEASURED necessary under wine 11:
#   - QT_* and SDL_* are dropped from the Unix environment wine hands a Windows
#     process, and WINE<name> is imported as <name>: QT_QPA_PLATFORM=offscreen
#     and SDL_VIDEODRIVER=dummy are passed as WINEQT_QPA_PLATFORM / WINESDL_...
#     (without it a Qt jnext.exe uses the `windows` platform and never exits);
#   - spdlog writes CRLF on Windows, to stdout and stderr alike; folded to LF on
#     the fly (sed -u: an unbuffered filter, or the rows' "listening on" polls
#     time out) so rows that anchor or compare lines see the text, not the EOL;
#   - the filters are children of this script, and it waits for them, so no
#     output arrives after the row has read it; with one stream (stdout and
#     stderr the same file) there is one filter, which keeps their interleaving;
#   - wine runs as a background child with stdin passed on explicitly, and TERM
#     and INT are forwarded to it; its exit status is the exit status;
#   - bash and wine by absolute path, since a row may set PATH to an empty dir;
#   - DISPLAY is kept only for an xvfb-run display, never a desktop one.
# (JNEXT_WINE_BIN replaces /usr/bin/wine: the harness self-test's stub.)
# A persistent wineserver (--serve, started once before the rows, stopped with
# `wineserver -k`) must exist first: the first wine process otherwise spawns one
# that inherits the filters' pipes, never lets them see EOF, and the row hangs.
#
# What wine cannot stand in for (accepted, owner 2026-10-10; documented in the
# developer guide): symlink creation, a real console attach for a GUI-subsystem
# exe, NTFS specifics (short names, streams).
set -euo pipefail

if [[ "${1:-}" == --jnext ]]; then
    EXE=${2:?usage: wine-run.sh --jnext <jnext.exe> [args...]}
    # No dirname here: a row may have set PATH to an empty directory (below), and
    # this line runs before it is reset. Parameter expansion needs no PATH.
    BUILD=$(cd "${EXE%/*}" && pwd)
    JNEXT_MODE=1
    # A row may set PATH to an empty directory (nextpi-func does): this script's
    # own tools must not depend on it.
    export PATH=/usr/bin:/bin
    # An xvfb-run display is the only one a Qt/SDL window may open on.
    KEEP_DISPLAY=
    [[ -n "${DISPLAY:-}" && "${XAUTHORITY:-}" == */xvfb-run.*/Xauthority ]] && KEEP_DISPLAY=$DISPLAY
elif [[ "${1:-}" == --init || "${1:-}" == --serve ]]; then
    BUILD=$(cd "${2:?usage: wine-run.sh $1 <build_dir>}" && pwd)
else
    EXE=${1:?usage: wine-run.sh <exe> [args...]}
    BUILD=$(cd "$(dirname "$EXE")/.." && pwd)
fi

SYSROOT=${MINGW_SYSROOT:-/usr/x86_64-w64-mingw32/sys-root/mingw}
export WINEPREFIX="${WINEPREFIX:-$BUILD/wine-prefix}"
export WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all LC_ALL=C.UTF-8 LANG=C.UTF-8
# JNEXT_EXTRA_WINEPATH: directories a row wants on the Windows PATH of what it runs
# (wine does not import the Unix PATH: nextpi-link-func puts its stand-in QEMU's
# directory here, GH #316). Unix or DOS form; appended after the two above.
export WINEPATH="$SYSROOT/bin;$BUILD/third_party/spdlog${JNEXT_EXTRA_WINEPATH:+;$JNEXT_EXTRA_WINEPATH}"
unset DISPLAY WAYLAND_DISPLAY XDG_RUNTIME_DIR
[[ -z "${KEEP_DISPLAY:-}" ]] || export DISPLAY=$KEEP_DISPLAY

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

if [[ "${1:-}" == --serve ]]; then
    # stdio on /dev/null: the daemon must hold none of our callers' pipes.
    # A server left over from --init's wineboot would make -p fail (status 2):
    # stop it first, and wait until it is gone.
    /usr/bin/wineserver -k >/dev/null 2>&1 || true
    /usr/bin/wineserver -w >/dev/null 2>&1 || true
    ${PRIV[@]+"${PRIV[@]}"} /usr/bin/wineserver -p </dev/null >/dev/null 2>&1 \
        || { echo "wine-run: cannot start wineserver" >&2; exit 2; }
    # Start wine's own services (services.exe, winedevice.exe ...) NOW, with stdio
    # on /dev/null: the first wine client otherwise starts them, they inherit ITS
    # stdout/stderr -- the CRLF filters' pipes -- and never let the filters see EOF
    # (measured: a row hung to its bound). `wineboot -u` is not enough: it leaves
    # no services running; a cmd.exe client does.
    timeout --kill-after=5s 300s ${PRIV[@]+"${PRIV[@]}"} /usr/bin/wine cmd /c exit </dev/null >/dev/null 2>&1 \
        || { echo "wine-run: the services warm-up after wineserver -p failed" >&2; exit 2; }
    # Desktop keeper (GH #329). wine's desktop process, `explorer.exe /desktop`, is
    # started by whichever wine client first needs the desktop and inherits THAT
    # client's stderr -- a row's CRLF filter pipe -- and lives while any other wine
    # process uses the desktop (+ ~1 s): the row's wrapper then waits for the filter
    # to reach EOF, for as long as other rows keep wine busy (measured: a 0.1 s
    # step held to its 120 s bound). So start the desktop ONCE, on /dev/null, and
    # keep it alive: `start /wait cmd /c pause` holds the desktop indefinitely
    # (measured); `timeout`, `cmd /c ping` and `notepad` do not (ping also needs
    # ICMP, which a CI container may lack). Its pid is the only stdout output; the
    # driver faults if that pid is not the one running at the end of the run.
    setsid ${PRIV[@]+"${PRIV[@]}"} /usr/bin/wine start /wait cmd /c pause </dev/null >/dev/null 2>&1 &
    # Exactly one explorer.exe of this prefix, all of fds 0-2 on /dev/null. Scans
    # /proc, not pgrep: the CI container has no procps-ng (GH #295, 716cff6a9).
    find_explorer() {
        local d p c env_txt n=0 found=
        for d in /proc/[0-9]*; do
            p=${d#/proc/}
            { read -r c < "$d/comm"; } 2>/dev/null || continue
            [[ $c == explorer.exe ]] || continue
            env_txt=$({ tr '\0' '\n' < "$d/environ"; } 2>/dev/null || true)
            grep -qxF "WINEPREFIX=$WINEPREFIX" <<<"$env_txt" || continue
            n=$((n + 1))
            [[ "$(readlink "$d/fd/0" 2>/dev/null)" == /dev/null \
               && "$(readlink "$d/fd/1" 2>/dev/null)" == /dev/null \
               && "$(readlink "$d/fd/2" 2>/dev/null)" == /dev/null ]] && found=$p
        done
        [[ $n -eq 1 && -n $found ]] && echo "$found"
        return 0
    }
    # Poll (30 s bound) until ONE pid has stayed the same for 3 s (15 polls of 0.2 s,
    # longer than the ~1 s close timeout of the warm-up explorer on its way out); a
    # change of pid restarts the count, so a slow keeper attach is not a fault.
    kpid=; cur=; stable=0
    for ((i = 0; i < 150; i++)); do
        cur=$(find_explorer)
        if [[ -n $cur && $cur == "${last:-}" ]]; then stable=$((stable + 1)); else stable=0; fi
        last=$cur
        [[ $stable -ge 15 ]] && { kpid=$cur; break; }
        /usr/bin/sleep 0.2
    done
    [[ -n $kpid ]] || { echo "wine-run: no persistent explorer.exe on /dev/null for $WINEPREFIX (GH #329)" >&2; exit 2; }
    echo "$kpid"
    exit 0
fi

if [[ -n "${JNEXT_MODE:-}" ]]; then
    shift   # --jnext; $1 is the exe
    for v in $(compgen -e); do
        case $v in QT_*|SDL_*) export "WINE$v=${!v}" ;; esac
    done
    exec 3<&0
    fpids=()
    exec {out_fd}> >(exec /usr/bin/sed -u 's/\r$//')
    fpids+=($!)
    if [[ /dev/fd/1 -ef /dev/fd/2 ]]; then
        err_fd=$out_fd
    else
        exec {err_fd}> >(exec /usr/bin/sed -u 's/\r$//' >&2)
        fpids+=($!)
    fi
    ${PRIV[@]+"${PRIV[@]}"} "${JNEXT_WINE_BIN:-/usr/bin/wine}" "$@" <&3 >&"$out_fd" 2>&"$err_fd" {out_fd}>&- {err_fd}>&- 3<&- &
    wpid=$!
    exec {out_fd}>&- {err_fd}>&- 3<&-
    trap 'kill -TERM "$wpid" 2>/dev/null' TERM
    trap 'kill -INT "$wpid" 2>/dev/null' INT
    rc=0
    wait "$wpid" || rc=$?
    # A trapped signal interrupts `wait`: keep waiting for wine's own end.
    while kill -0 "$wpid" 2>/dev/null; do rc=0; wait "$wpid" || rc=$?; done
    for p in "${fpids[@]}"; do wait "$p" 2>/dev/null || true; done
    exit "$rc"
fi

exec ${PRIV[@]+"${PRIV[@]}"} wine "$@"
