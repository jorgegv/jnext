#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #280 WP-5 (zrcp-frontend.md §6.2 item 7, and the rest of WP-5 live) — a
# headless jnext serving ZRCP, driven by zrcp-peer.py m3: smartload of a .sna
# the client writes, DeZog's history / coverage / extended-stack init,
# `cpu-history get 0` after three steps (the newest, with (PC)=, (SP)= and
# MMU=), coverage of the executed addresses, a CALL typed on the stack,
# load-binary / save-binary through host files, and a snapshot-save /
# snapshot-load round trip. The client's files live under the run's $TMP_DIR.
if want zrcp-hist-func; then
    begin_func zrcp-hist-func
    log="$TMP_DIR/zrcp-hist-func.log"
    work="$TMP_DIR/zrcp-hist-func"
    mkdir -p "$work"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the ZRCP client)"
    elif ! zrcp_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
            --zrcp-port 0; then
        zrcp_stop
        fail_row " (no 'zrcp: listening on' line within 10 s: $(tail -n 3 "$log" | tr '\n' ' '))"
    elif zrcp_peer m3 "$work"; then
        zrcp_stop
        pass_row " (${ZRCP_OUT#PASS })"
    else
        zrcp_stop
        fail_row " (${ZRCP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
