#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (--debugger on the SDL loop owner): starts paused at the NEX entry and serves DZRP;
# a client reads the PC, held, and CONTINUE runs it. SDL_VIDEODRIVER=dummy, as
# dzrp-sdl-func: no X server needed.
if want debugger-sdl-func; then
    begin_func debugger-sdl-func
    log="$TMP_DIR/debugger-sdl-func.log"
    sdl_bin="$JNEXT_SDL"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! dbgstart_have_python debugger-sdl-func; then
        :
    elif ! dzrp_launch_cmd "$log" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$DBGSTART_NEX"; then
        dzrp_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    elif dzrp_peer startup "$(dbgstart_pc_of nex "$DBGSTART_NEX")"; then
        dzrp_stop
        pass_row " (SdlApp: ${DZRP_OUT#PASS })"
    else
        dzrp_stop
        fail_row " (SdlApp: ${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|'))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
