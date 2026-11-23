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

# GH #548 (--debugger, no protocol port given, no window): DZRP on the DEFAULT port 11000 and
# the waiting line in the log. The port is fixed, so the row is `serial`; on a host where
# 11000 is taken it FAILS with the bind error (a real fault of that host for this row),
# never skips.
if want debugger-default-port-func; then
    begin_func debugger-default-port-func
    log="$TMP_DIR/debugger-default-port-func.log"
    if ! dbgstart_have_python debugger-default-port-func; then
        :
    elif ! dzrp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger; then
        dzrp_stop
        fail_row " (no 'dzrp: listening on' line within 10 s - is port 11000 taken? $(tail -n 3 "$log" | tr '\n' ' '))"
    else
        fails=()
        [[ "$DZRP_PORT" == 11000 ]] || fails+=("DZRP listens on $DZRP_PORT, not 11000")
        grep -aq "debugger: waiting for a debugger on dzrp 127.0.0.1:11000" "$log" ||
            fails+=("no 'debugger: waiting for a debugger on dzrp 127.0.0.1:11000' line")
        dzrp_peer startup 0x0000 || fails+=("${DZRP_OUT#FAIL }")
        dzrp_stop
        if (( ${#fails[@]} == 0 )); then
            pass_row " (DZRP on 11000, the waiting line, a client attached to the paused machine at PC 0)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
