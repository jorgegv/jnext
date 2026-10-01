#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-5) — the DZRP server on the Qt loop owner
# (QtApp, frame_sequencer's post_frames pump). Every other DZRP row runs
# --headless; this one proves QtApp registers the server and gives a paused
# remote T's draining budget. QT_QPA_PLATFORM=offscreen, as qt-host-probe-func.
# The client (dzrp-peer.py gui-drain): CMD_INIT, then eight commands written in
# one send while paused must all be answered within 60 ms — one tick's drain
# (T's paused budget PumpBudget{0, 2, 10}); the running budget answers one per
# tick, ~20 ms apart. Then CONTINUE runs frames and PAUSE notifies (reason 1).
if want dzrp-qt-func; then
    begin_func dzrp-qt-func
    log="$TMP_DIR/dzrp-qt-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
    elif ! dzrp_launch_cmd "$log" env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
            --dzrp-port 0; then
        dzrp_stop
        fail_row " (the Qt frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    elif dzrp_peer gui-drain; then
        dzrp_stop
        pass_row " (QtApp: ${DZRP_OUT#PASS })"
    else
        dzrp_stop
        fail_row " (QtApp: ${DZRP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
