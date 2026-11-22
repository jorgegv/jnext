#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (--debugger with --gdb-port): only the port the user gave is served, so there is no
# `dzrp:` line, and a GDB client (gdb-peer.py `startup`) reads the NEX entry PC from the
# paused machine and finds it held.
if want debugger-gdb-port-func; then
    begin_func debugger-gdb-port-func
    log="$TMP_DIR/debugger-gdb-port-func.log"
    if ! dbgstart_have_python debugger-gdb-port-func; then
        :
    else
        pc=$(dbgstart_pc_of nex "$DBGSTART_NEX")
        if ! gdb_launch_cmd "$log" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --debugger --gdb-port 0 \
                --load "$DBGSTART_NEX"; then
            gdb_stop
            fail_row " (no 'gdb: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        elif ! dbgstart_wait_stop "$log" "$GDB_PID"; then
            gdb_stop
            fail_row " (no stop within 90 s: $(tail -n 3 "$log" | tr '\n' ' '))"
        elif gdb_peer startup "$GDB_PORT" "$pc"; then
            gdb_stop
            if grep -aq "dzrp:" "$log"; then
                fail_row " (a dzrp line in the log with only --gdb-port given: $(grep -a 'dzrp:' "$log" | head -1))"
            else
                pass_row " (${GDB_OUT#PASS }; no DZRP server)"
            fi
        else
            gdb_stop
            fail_row " (${GDB_OUT#FAIL })"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
