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

# GH #548 (the startup stop survives another client's pause): a DZRP client attaches while a REAL-TIME
# tape is still loading (CMD_INIT pauses the machine, the normal DeZog flow), CONTINUEs, and the machine
# still stops at the tape's entry, 0x8000. Under the earlier rule the attach cancelled the stop.
if want debugger-headless-midload-func; then
    begin_func debugger-headless-midload-func
    log="$TMP_DIR/debugger-headless-midload-func.log"
    if ! dbgstart_have_python debugger-headless-midload-func; then
        :
    elif ! dzrp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 \
            --tape-realtime --load "$DBGSTART_TAP"; then
        dzrp_stop
        fail_row " (no 'dzrp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
    elif dzrp_peer midload 0x8000; then
        dzrp_stop
        if grep -aq 'debugger: stopped at \$8000 (the first instruction from the tape' "$log"; then
            pass_row " (${DZRP_OUT#PASS })"
        else
            fail_row " (the client saw the stop but the log has none: $(grep -a 'debugger:' "$log" | tr '\n' '|'))"
        fi
    else
        dzrp_stop
        fail_row " (${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|'))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
