#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #428 -- the SDL twin of paused-idle-qt-func: a held machine uploads and
# presents nothing. The loop's once-per-second `sdl present: ticks=N uploads=K`
# debug line is the instrument; gdb-peer.py idle-check sdl asserts, over the
# log, uploads before the pause, two consecutive held windows with ticks>0 and
# uploads=0, and uploads again after the resume. SDL_VIDEODRIVER=dummy, as
# gdb-sdl-func: no X server needed. A log without the counters FAILS.
if want paused-idle-sdl-func; then
    begin_func paused-idle-sdl-func
    sdl_bin="$JNEXT_SDL"
    log="$TMP_DIR/paused-idle-sdl-func.log"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the GDB client)"
    elif ! gdb_launch_cmd "$log" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --machine 48k \
            --gdb-port 0 --log-level platform=debug; then
        gdb_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'gdb: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    elif ! gdb_peer hold "$GDB_PORT"; then
        gdb_stop
        fail_row " (${GDB_OUT#FAIL })"
    else
        gdb_stop
        if gdb_peer idle-check sdl "$log"; then
            pass_row " (${GDB_OUT#PASS })"
        else
            fail_row " (${GDB_OUT#FAIL })"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
