#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; design §0 item 4 "reported, never silent", the
# legacy rows 5/12) — CMD_SET_BREAKPOINTS (13), CMD_EXEC_ASM (22) and an unknown
# id (99) each get a seq-only reply and a warn line naming the id, and the
# connection stays in sync; CMD_SET_BORDER and CMD_WRITE_BANK, removed in DZRP
# 2.2.0, are still SERVED to a 2.2.0 client — the write lands — with a
# debug-level line saying so, never a warning. The log is read at
# `debugger=debug` for that line.
if want dzrp-unsupported-func; then
    begin_func dzrp-unsupported-func
    log="$TMP_DIR/dzrp-unsupported-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
    elif ! dzrp_launch "$log" --log-level debugger=debug; then
        dzrp_stop
        fail_row " (no 'dzrp: listening on' line within 10 s)"
    else
        peer_rc=0
        dzrp_peer unsupported || peer_rc=$?
        dzrp_stop
        fails=()
        [[ $peer_rc -eq 0 ]] || fails+=("${DZRP_OUT#FAIL }")
        for id in 13 22 99; do
            grep -aq "\[warning\] dzrp: unsupported DZRP command $id " "$log" \
                || fails+=("no warn line for command $id")
        done
        for name in CMD_SET_BORDER CMD_WRITE_BANK; do
            grep -aq "\[debug\] dzrp: $name was removed in DZRP 2.2.0 — served as legacy to a 2.2.0 client" "$log" \
                || fails+=("no 'served as legacy' line for $name")
        done
        if grep -aqE "unsupported DZRP command (5|12) " "$log"; then
            fails+=("a legacy command was reported unsupported")
        fi
        if (( ${#fails[@]} == 0 )); then
            pass_row " (${DZRP_OUT#PASS }; warn lines for 13/22/99, debug 'served as legacy' for 12 and 5)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
