#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #280 WP-4 (zrcp-frontend.md §6.2, items 3, 5 and 6) — a live headless jnext
# serving ZRCP on `--zrcp-port 0`, driven by zrcp-peer.py m2: DeZog's breakpoint
# sequence (set-breakpointaction / set-breakpoint / enable-breakpoint) stops a
# run with `Breakpoint fired: PC=8002H`; a `PC=… AND A=5` condition fires only
# where it holds; a memory breakpoint names the address written; DeZog's
# step-over (`SP>=65280`) and step-out (`PC=PEEKW(SP-2) AND SP>=65280`)
# conditions stop after the RET — the conditions compiled through the DSL's
# expression library.
if want zrcp-bp-func; then
    begin_func zrcp-bp-func
    log="$TMP_DIR/zrcp-bp-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the ZRCP client)"
    elif ! zrcp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
            --zrcp-port 0; then
        zrcp_stop
        fail_row " (no 'zrcp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
    elif zrcp_peer m2; then
        zrcp_stop
        pass_row " (${ZRCP_OUT#PASS })"
    else
        zrcp_stop
        fail_row " (${ZRCP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
