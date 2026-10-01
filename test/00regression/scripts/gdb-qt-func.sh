#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #281 (gdb-rsp-frontend.md WP-4) — the GDB RSP server on the Qt loop owner
# (QtApp). gdb-cli-func runs --headless; this one proves QtApp registers the
# server through DebugServers. QT_QPA_PLATFORM=offscreen, as dzrp-qt-func. The
# client (gdb-peer.py handshake) is z88dk-gdb's connect sequence.
if want gdb-qt-func; then
    begin_func gdb-qt-func
    log="$TMP_DIR/gdb-qt-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the GDB client)"
    elif ! gdb_launch_cmd "$log" env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
            --gdb-port 0; then
        gdb_stop
        fail_row " (the Qt frontend never logged 'gdb: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    elif gdb_peer handshake "$GDB_PORT"; then
        gdb_stop
        pass_row " (QtApp: ${GDB_OUT#PASS })"
    else
        gdb_stop
        fail_row " (QtApp: ${GDB_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
