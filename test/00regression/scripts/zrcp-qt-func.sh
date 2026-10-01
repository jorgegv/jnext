#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #280 (WP-1) — the ZRCP server on the Qt loop owner (QtApp): the third loop
# owner, through the same DebugServers::start. QT_QPA_PLATFORM=offscreen, as
# dzrp-qt-func. The client (zrcp-peer.py smoke): welcome, enter-cpu-step, a
# program written and stepped (PC 8001), exit-cpu-step, quit.
if want zrcp-qt-func; then
    begin_func zrcp-qt-func
    log="$TMP_DIR/zrcp-qt-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the ZRCP client)"
    elif ! zrcp_launch_cmd "$log" env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
            --zrcp-port 0; then
        zrcp_stop
        fail_row " (the Qt frontend never logged 'zrcp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    elif zrcp_peer smoke; then
        zrcp_stop
        pass_row " (QtApp: ${ZRCP_OUT#PASS })"
    else
        zrcp_stop
        fail_row " (QtApp: ${ZRCP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
