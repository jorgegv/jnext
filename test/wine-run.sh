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
#   - a stalled jnext.exe names its blocked call (GH #383): when the row's `timeout`
#     sends TERM at the bound and JNEXT_WINE_CAPTURE_DIR is set, the TERM handler first
#     saves what the one stalled process is doing to <dir>/wine-stall-<time>-<pid>.txt
#     (its Linux threads and CPU time from /proc; the prefix's processes with their
#     thread counts; `winedbg info thread`; `winedbg bt all` of THAT Windows pid), then
#     forwards TERM as before. It runs after the row's verdict is decided (reaching the
#     bound is the failure), so it cannot change one. It is the internal mode
#     `--capture`, so `timeout` can bound it; `--prefix-state` prints the thread counts
#     of the prefix's wine processes (the driver's end-of-run line).
#     BUDGET: the rows wrap $JNEXT in `timeout --foreground --kill-after=5s`, so KILL
#     reaches this script 5 s after TERM; if it dies before forwarding, wine keeps the
#     row's pipes and the row hangs. CAPTURE_BUDGET plus its 0.4 s kill leaves >= 1.1 s.
#     winedbg runs only while the target is alive, and the name fallback only if /proc shows
#     it as the only process of its name in the prefix: a winedbg killed by its bound kills
#     its debuggee under wine 11, so it may only ever be pointed at the stalled process.
#     The /proc sections come first and are never cut. `bt all` is per pid on purpose:
#     prefix-wide it took 21.6 s at load 20 and pauses other rows' processes. wine does
#     not catch SIGTERM (measured), so the forward always lands. INT is forwarded at once.
# A persistent wineserver (--serve, started once before the rows, stopped with
# `wineserver -k`) must exist first: the first wine process otherwise spawns one
# that inherits the filters' pipes, never lets them see EOF, and the row hangs.
#
# What wine cannot stand in for (accepted, owner 2026-10-10; documented in the
# developer guide): symlink creation, a real console attach for a GUI-subsystem
# exe, NTFS specifics (short names, streams).
set -euo pipefail

# This script's own absolute path, for --capture (parameter expansion only: PATH may be empty).
SELF=${BASH_SOURCE[0]}
[[ $SELF == /* ]] || SELF=$PWD/$SELF
# What the TERM handler may spend on the stall capture (see the header: BUDGET).
CAPTURE_BUDGET_MS=3500
CAPTURE_BUDGET=$((CAPTURE_BUDGET_MS / 1000)).$((CAPTURE_BUDGET_MS % 1000 / 100))s

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
elif [[ "${1:-}" == --capture || "${1:-}" == --prefix-state ]]; then
    # Internal modes: the prefix is the caller's (exported WINEPREFIX).
    : "${WINEPREFIX:?wine-run.sh $1 needs WINEPREFIX}"
    BUILD=.
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
    # Poll (at most 150 polls of 0.2 s plus a /proc scan each: 30 s or more under load) until ONE pid has stayed the same for 3 s (15 polls of 0.2 s,
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

# One line per process of this prefix (wineserver and every *.exe): pid comm threads
# state wchan. Errors silenced: other users' /proc/<pid>/environ is EACCES.
prefix_procs() {
    local d c env_txt k v th st wc
    for d in /proc/[0-9]*; do
        { read -r c < "$d/comm"; } 2>/dev/null || continue
        [[ $c == wineserver || $c == *.exe ]] || continue
        env_txt=$({ tr '\0' '\n' < "$d/environ"; } 2>/dev/null || true)
        grep -qxF "WINEPREFIX=$WINEPREFIX" <<<"$env_txt" || continue
        th=?; st=?; wc=?
        while read -r k v; do
            case $k in Threads:) th=$v ;; State:) st=${v%% *} ;; esac
        done < "$d/status" 2>/dev/null || true
        wc=; { read -r wc < "$d/wchan" || [[ -n $wc ]]; } 2>/dev/null || wc=?   # no trailing newline
        echo "${d#/proc/} $c $th $st $wc"
    done
    return 0
}

if [[ "${1:-}" == --prefix-state ]]; then
    # "plugplay.exe=212 services.exe=10 winedevice.exe=8/12 (threads)": every run gets a
    # data point on the 500-worker hypothesis of GH #383, stall or not.
    declare -A ps_cnt=()
    ps_order=()
    while read -r _ c th _; do
        [[ -n $c ]] || continue
        [[ -n "${ps_cnt[$c]:-}" ]] || ps_order+=("$c")
        ps_cnt[$c]="${ps_cnt[$c]:+${ps_cnt[$c]}/}$th"
    done < <(prefix_procs)
    line=
    for c in ${ps_order[@]+"${ps_order[@]}"}; do line+="${line:+ }$c=${ps_cnt[$c]}"; done
    [[ -z $line ]] || echo "$line (threads)"
    exit 0
fi

if [[ "${1:-}" == --capture ]]; then
    # --capture <wpid> <outfile> <exe> [args...]  (see the header). Appends section by
    # section: a KILL at the bound loses only the section in progress.
    cap_start_us=${EPOCHREALTIME/./}
    cap_pid=${2:?}; cap_out=${3:?}; cap_exe=${4:?}; shift 4
    cap_args=("$@")
    WINE_BIN=${JNEXT_WINE_BIN:-/usr/bin/wine}
    {
        echo "== header"
        echo "date: $(/usr/bin/date '+%Y-%m-%d %H:%M:%S')"
        echo "pid: $cap_pid"
        printf 'command: %q' "$cap_exe"
        for a in ${cap_args[@]+"${cap_args[@]}"}; do printf ' %q' "$a"; done
        echo
        { read -r la < /proc/loadavg; } 2>/dev/null || la=?
        echo "loadavg: $la"
        echo "WINEPREFIX: $WINEPREFIX"
        if { read -r st < "/proc/$cap_pid/stat"; } 2>/dev/null; then
            read -ra f <<<"${st##*) }"
            { read -r up _ < /proc/uptime; } 2>/dev/null || up=0
            echo "age: $((${up%.*} - ${f[19]} / 100)) s, cpu: $(((${f[11]} + ${f[12]}) / 100)) s (utime+stime)"
        else
            echo "age/cpu: /proc/$cap_pid is gone"
        fi
        echo
        echo "== target threads (tid comm state cpu-ticks wchan syscall)"
        for t in /proc/"$cap_pid"/task/*; do
            [[ -d $t ]] || continue
            { read -r tc < "$t/comm"; } 2>/dev/null || tc=?
            { read -r ts < "$t/stat"; } 2>/dev/null || ts="? ? ?"
            read -ra f <<<"${ts##*) }"
            tw=; { read -r tw < "$t/wchan" || [[ -n $tw ]]; } 2>/dev/null || tw=?
            { read -r tsy < "$t/syscall"; } 2>/dev/null || tsy=?
            echo "${t##*/} $tc ${f[0]:-?} $((${f[11]:-0} + ${f[12]:-0})) $tw syscall: $tsy"
        done
        echo
        echo "== prefix processes (pid comm threads state wchan)"
        prefix_procs
        echo
    } >> "$cap_out" 2>&1 || true

    # A DEADLINE, not fixed bounds (GH #383 review): the outer timeout kills this mode at
    # CAPTURE_BUDGET_MS, so winedbg gets what is left of it. `info thread` is capped (it only
    # finds the pid); `bt all` gets all the rest less a margin for its own 0.2 s kill.
    deadline_us=$((cap_start_us + CAPTURE_BUDGET_MS * 1000))
    left_ms() { echo $(((deadline_us - ${EPOCHREALTIME/./}) / 1000)); }
    secs() { printf '%d.%03ds' $(($1 / 1000)) $(($1 % 1000)); }   # secs <ms>

    # winedbg, its output only ever to the capture file (never the row's pipes). A winedbg
    # killed by its bound kills its debuggee under wine 11 (measured), so it is only ever
    # pointed at the stalled process itself, and only while that is alive.
    dbg_run() {   # dbg_run <ms> <tmpfile> <winedbg args...>
        local ms=$1 tmp=$2 rc=0
        shift 2
        /usr/bin/timeout --kill-after=0.2s "$(secs "$ms")" ${PRIV[@]+"${PRIV[@]}"} "$WINE_BIN" winedbg "$@" \
            </dev/null >"$tmp" 2>&1 || rc=$?
        if [[ $rc -eq 124 || $rc -eq 137 ]]; then echo "winedbg $*: no answer within $(secs "$ms") (rc $rc)" >> "$cap_out"; fi
        /usr/bin/tr -d '\r' < "$tmp" >> "$cap_out" 2>/dev/null || true
        return 0
    }
    target_alive() {   # the stalled process still exists (not gone, not a zombie)
        local s
        { read -r s < "/proc/$cap_pid/stat"; } 2>/dev/null || return 1
        s=${s##*) }
        [[ ${s%% *} != Z && ${s%% *} != X ]]
    }
    if ! target_alive; then
        { echo "== winedbg"; echo "pid $cap_pid is gone: no debugger is started (it could only reach another process)"; } >> "$cap_out"
        exit 0
    fi
    info_tmp=$cap_out.info
    info_ms=$(($(left_ms) - 500)); ((info_ms <= 1500)) || info_ms=1500
    echo "== winedbg info thread" >> "$cap_out"
    if ((info_ms >= 200)); then dbg_run "$info_ms" "$info_tmp" --command "info thread"; else echo "no time left for it" >> "$cap_out"; fi
    echo >> "$cap_out"

    # The Windows pid of the target: the one process block whose command line is exactly
    # "Z:<exe, / -> \>" <args as CreateProcess quotes them>; else (name only) the one
    # process of that name, but only if /proc shows the target as the ONLY process of
    # that name in the prefix: otherwise "the one jnext.exe" may be another row's.
    want="\"Z:${cap_exe//\//\\}\""
    for a in ${cap_args[@]+"${cap_args[@]}"}; do
        a=${a//\"/\\\"}
        if [[ -z $a || $a == *[[:space:]]* ]]; then want+=" \"$a\""; else want+=" $a"; fi
    done
    base=${cap_exe##*/}
    exact=(); byname=(); hdr=
    if [[ -r $info_tmp ]]; then
        while IFS= read -r line; do
            line=${line%$'\r'}
            if [[ $line =~ ^([0-9a-fA-F]{8})[[:space:]]+([^[:space:]].*)$ ]]; then
                hdr=${BASH_REMATCH[1]}
                [[ ${BASH_REMATCH[2]} == "$base" ]] && byname+=("$hdr")
            elif [[ -n $hdr && $line == $'\t['*']' ]]; then
                cl=${line#*[}; cl=${cl%]}
                [[ $cl == "$want" ]] && exact+=("$hdr")
            fi
        done < "$info_tmp" 2>/dev/null || true
    fi
    /usr/bin/rm -f "$info_tmp"
    pid=; how=
    if ((${#exact[@]} == 1)); then pid=${exact[0]}; how="exact command-line match"
    elif ((${#exact[@]} == 0 && ${#byname[@]} == 1)); then
        same=0; me=0
        while read -r pp pc _; do
            [[ $pc == "${base:0:15}" ]] || continue
            same=$((same + 1)); [[ $pp == "$cap_pid" ]] && me=1
        done < <(prefix_procs)
        if ((same == 1 && me == 1)); then pid=${byname[0]}; how="matched by name only; the only $base in the prefix"; fi
    fi
    bt_ms=$(($(left_ms) - 300))
    if [[ -z $pid ]]; then
        { echo "== winedbg bt all"; echo "no unique match (${#exact[@]} exact, ${#byname[@]} by name); no backtrace"; } >> "$cap_out"
    elif ! target_alive; then
        { echo "== winedbg bt all"; echo "pid $cap_pid is gone: no backtrace"; } >> "$cap_out"
    elif ((bt_ms < 300)); then
        { echo "== winedbg bt all 0x$pid ($how)"; echo "no time left for it"; } >> "$cap_out"
    else
        echo "== winedbg bt all 0x$pid ($how)" >> "$cap_out"
        dbg_run "$bt_ms" "$cap_out.bt" --command "bt all" "0x$pid"
        /usr/bin/rm -f "$cap_out.bt"
    fi
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
    cap_done=; cap_file=; cap_msg=
    on_term() {   # $1 = the exe, the rest its arguments
        # GH #383: save the stalled process's state BEFORE forwarding (it is gone after).
        if [[ -z $cap_done && -n "${JNEXT_WINE_CAPTURE_DIR:-}" ]]; then
            cap_done=1
            cap_file=
            cap_file=$JNEXT_WINE_CAPTURE_DIR/wine-stall-$(/usr/bin/date +%Y%m%d-%H%M%S)-$wpid.txt
            if /usr/bin/mkdir -p "$JNEXT_WINE_CAPTURE_DIR" 2>/dev/null; then
                /usr/bin/timeout --kill-after=0.4s "$CAPTURE_BUDGET" /bin/bash "$SELF" --capture \
                    "$wpid" "$cap_file" "$@" </dev/null >/dev/null 2>&1 || true
                cap_msg="wine-run: TERM at the bound; state of ${1##*/} (pid $wpid) saved to $cap_file"
            fi
        fi
        kill -TERM "$wpid" 2>/dev/null || true
        # After the forward: an echo into a dead pipe must not kill this shell first. A cut
        # capture can leave its temp files behind.
        if [[ -n ${cap_file:-} ]]; then /usr/bin/rm -f "$cap_file.info" "$cap_file.bt"; fi
        if [[ -n ${cap_msg:-} ]]; then ( trap '' PIPE; echo "$cap_msg" >&2 ) || true; cap_msg=; fi
    }
    trap 'on_term "$@"' TERM
    trap 'kill -INT "$wpid" 2>/dev/null' INT
    rc=0
    wait "$wpid" || rc=$?
    # A trapped signal interrupts `wait`: keep waiting for wine's own end.
    while kill -0 "$wpid" 2>/dev/null; do rc=0; wait "$wpid" || rc=$?; done
    # wine may have exited (and been reaped by a trap handler's own forks) before the loop ran:
    # bash keeps a reaped child's status, so collect the real one, not the interrupted wait's 128+sig.
    rc=0; wait "$wpid" 2>/dev/null || rc=$?
    for p in "${fpids[@]}"; do wait "$p" 2>/dev/null || true; done
    exit "$rc"
fi

exec ${PRIV[@]+"${PRIV[@]}"} wine "$@"
