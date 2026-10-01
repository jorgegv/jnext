#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/zrcp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../zrcp-functions.inc"

# GH #280 (WP-1) — the ZRCP server on the SDL loop owner (SdlApp): zrcp-func
# runs --headless; this row proves SdlApp opens and pumps it too, through the
# same DebugServers::start. SDL_VIDEODRIVER=dummy, as dzrp-sdl-func. The client
# (zrcp-peer.py smoke): welcome, enter-cpu-step, a program written and stepped
# (PC 8001), exit-cpu-step, quit.
if want zrcp-sdl-func; then
    begin_func zrcp-sdl-func
    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    log="$TMP_DIR/zrcp-sdl-func.log"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the ZRCP client)"
    elif ! zrcp_launch_cmd "$log" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --machine 48k \
            --zrcp-port 0; then
        zrcp_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'zrcp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    elif zrcp_peer smoke; then
        zrcp_stop
        pass_row " (SdlApp: ${ZRCP_OUT#PASS })"
    else
        zrcp_stop
        fail_row " (SdlApp: ${ZRCP_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
