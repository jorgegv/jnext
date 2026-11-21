#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (headless paused under --debugger): with no client attached the paused run waits on
# the servers instead of spinning frames. After the stop the row reads jnext's own CPU time
# (/proc/<pid>/stat) over a silent 2 s: under a quarter of a core (a spinning loop burns
# all of one); and `--delayed-automatic-exit-frames` still ends the run on its wall-time
# bound with status 0. Linux only: it reads /proc.
# (ZRCP is the served protocol, so no fixed DZRP port is implied.)
if want debugger-headless-idle-func; then
    begin_func debugger-headless-idle-func
    log="$TMP_DIR/debugger-headless-idle-func.log"
    LANG=C timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --debugger --zrcp-port 0 --delayed-automatic-exit-frames 250 >"$log" 2>&1 &
    tpid=$!
    fails=()
    for _ in $(seq 1 100); do
        grep -aq "debugger: stopped at" "$log" && break
        kill -0 "$tpid" 2>/dev/null || break
        sleep 0.1
    done
    jpid=$(pgrep -P "$tpid" | head -1 || true)
    [[ -n "$jpid" ]] || jpid=$tpid
    grep -aq "debugger: stopped at" "$log" || fails+=("no stop in the log: $(tail -n 3 "$log" | tr '\n' ' ')")
    ticks() { awk '{print $14 + $15}' "/proc/$jpid/stat" 2>/dev/null || echo -1; }
    t0=$(ticks)
    sleep 2
    t1=$(ticks)
    hz=$(getconf CLK_TCK)
    if (( t0 < 0 || t1 < 0 )); then
        fails+=("could not read the process's CPU time")
    elif (( (t1 - t0) * 4 > 2 * hz )); then
        fails+=("the paused run used $(( (t1 - t0) * 100 / hz )) cs of CPU in 2 s (a wait uses next to none)")
    fi
    rc=0
    wait "$tpid" 2>/dev/null || rc=$?
    [[ $rc -eq 0 ]] || fails+=("jnext exited $rc, not 0 by its exit bound")
    grep -aq "automatic exit triggered" "$log" || fails+=("no 'automatic exit triggered' in the log")
    if (( ${#fails[@]} == 0 )); then
        pass_row " (paused with no client: $(( (t1 - t0) * 100 / hz )) cs of CPU in 2 s; the exit bound still fired, status 0)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
