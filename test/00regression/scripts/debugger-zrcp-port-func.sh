#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (--debugger with --zrcp-port): only the port the user gave is served, and a ZRCP
# client (zrcp-peer.py `startup`) reads the NEX entry PC from the paused machine and finds
# it held.
if want debugger-zrcp-port-func; then
    begin_func debugger-zrcp-port-func
    log="$TMP_DIR/debugger-zrcp-port-func.log"
    if ! dbgstart_have_python debugger-zrcp-port-func; then
        :
    else
        pc=$(dbgstart_pc_of nex "$DBGSTART_NEX")
        if ! zrcp_launch_cmd "$log" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --debugger --zrcp-port 0 \
                --load "$DBGSTART_NEX"; then
            zrcp_stop
            fail_row " (no 'zrcp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        elif ! dbgstart_wait_stop "$log" "$ZRCP_PID"; then
            zrcp_stop
            fail_row " (no stop within 90 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        elif zrcp_peer startup "$pc"; then
            zrcp_stop
            if grep -aq "dzrp:" "$log"; then
                fail_row " (a dzrp line in the log with only --zrcp-port given: $(grep -a 'dzrp:' "$log" | head -1))"
            else
                pass_row " (${ZRCP_OUT#PASS }; no DZRP server)"
            fi
        else
            zrcp_stop
            fail_row " (${ZRCP_OUT#FAIL })"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
