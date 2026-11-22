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

# GH #548 (--debugger Pause policy): headless normally turns a Stop into an exit (code 3), and
# `--script` makes the script host the one that exits. With --debugger (here via --break-at
# _main, a stop the machine reaches by running, so it is an Action::Stop: a stop AT the PC the
# program starts at is a plain pause and would not exercise the policy) the startup stop must
# NOT end the run: the machine is paused at _main (0x80C4, read from the map) for a DZRP client
# (dzrp-peer.py `startup`) and jnext is still alive after it, with no `requesting exit` in
# the log. The script's own frame rule never fires (paused).
if want debugger-script-func; then
    begin_func debugger-script-func
    log="$TMP_DIR/debugger-script-func.log"
    jds="$TMP_DIR/debugger-script-func.jds"
    if ! dbgstart_have_python debugger-script-func; then
        :
    else
        echo 'on frame 1000000 once do log "never" end' >"$jds"
        main_hex=$(sed -n 's/^_main  *= \$\([0-9A-Fa-f]*\) ;.*/\1/p' "$DBGSTART_MAP" | head -1)
        if ! dzrp_launch_cmd "$log" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --break-at _main \
                --script "$jds" --dzrp-port 0 --load "$DBGSTART_NEX"; then
            dzrp_stop
            fail_row " (no 'dzrp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        else
            fails=()
            dbgstart_wait_stop "$log" "$DZRP_PID" || fails+=("no stop within 90 s")
            dzrp_peer startup "0x$main_hex" || fails+=("${DZRP_OUT#FAIL }")
            kill -0 "$DZRP_PID" 2>/dev/null || fails+=("jnext exited during the session")
            ! grep -aqi "requesting exit\|requested exit" "$log" || fails+=("the log says an exit was requested")
            dzrp_stop
            if (( ${#fails[@]} == 0 )); then
                pass_row " (the startup stop at _main under --script did not end the run; a client read it and continued)"
            else
                fail_row " (${fails[*]})"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
