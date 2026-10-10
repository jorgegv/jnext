#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-5) — the DZRP server on the SDL loop owner
# (SdlApp). Every other DZRP row runs --headless; this one proves SdlApp
# registers the server and gives a paused remote T's draining budget.
# SDL_VIDEODRIVER=dummy, as sdl-host-probe-func: no X server needed.
# The client (dzrp-peer.py gui-drain): CMD_INIT, then eight commands written in
# one send while paused must all be answered within 60 ms — one tick's drain
# (T's paused budget PumpBudget{0, 2, 10}); the running budget answers one per
# tick, ~20 ms apart. Then CONTINUE runs frames and PAUSE notifies (reason 1).
if want dzrp-sdl-func; then
    begin_func dzrp-sdl-func
    sdl_bin="$JNEXT_SDL"
    log="$TMP_DIR/dzrp-sdl-func.log"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
    elif ! dzrp_launch_cmd "$log" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --machine 48k \
            --dzrp-port 0; then
        dzrp_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    elif dzrp_peer gui-drain; then
        dzrp_stop
        pass_row " (SdlApp: ${DZRP_OUT#PASS })"
    else
        dzrp_stop
        fail_row " (SdlApp: ${DZRP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
