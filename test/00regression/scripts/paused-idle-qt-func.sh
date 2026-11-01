#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #428 -- a paused machine costs (almost) nothing in the Qt frontend: it
# neither re-uploads the unchanged framebuffer nor redraws the debugger panels.
# A GDB client interrupts the machine (which opens the debugger window, owner Q5
# / PBPUI-08), holds it, pokes memory once, resumes. The per-second `ticks:`
# debug line carries `uploads=U panel-refreshes=P`; gdb-peer.py idle-check
# asserts, over the log: a window with uploads before the pause; two
# consecutive held windows with ticks>0 and uploads=0 and panel-refreshes=0; a
# later window with the poke's panel refresh and no upload; a window with
# uploads after the resume. Counting, not timing: it holds on a loaded host. A
# log without the counters FAILS (the instrument is part of the contract).
if want paused-idle-qt-func; then
    begin_func paused-idle-qt-func
    log="$TMP_DIR/paused-idle-qt-func.log"
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the GDB client)"
    elif ! gdb_launch_cmd "$log" env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
            --gdb-port 0 --log-level platform=debug; then
        gdb_stop
        fail_row " (the Qt frontend never logged 'gdb: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    elif ! gdb_peer hold "$GDB_PORT"; then
        gdb_stop
        fail_row " (${GDB_OUT#FAIL })"
    else
        gdb_stop
        if gdb_peer idle-check qt "$log"; then
            pass_row " (${GDB_OUT#PASS })"
        else
            fail_row " (${GDB_OUT#FAIL })"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
