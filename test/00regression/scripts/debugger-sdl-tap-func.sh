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

# GH #548 stage 2: the fixture tape on the SDL loop owner (SDL_VIDEODRIVER=dummy, as dzrp-sdl-func):
# 0x8000. The client attaches once the stop is logged.
if want debugger-sdl-tap-func; then
    begin_func debugger-sdl-tap-func
    log="$TMP_DIR/debugger-sdl-tap-func.log"
    if [[ ! -x "$JNEXT_SDL" ]]; then
        fail_row " (SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
    elif ! dbgstart_have_python debugger-sdl-tap-func; then
        :
    elif dbgstart_dzrp_attempt "$log" 0x8000 env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            "$JNEXT_SDL" --silent --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$DBGSTART_TAP"; then
        pass_row " (SdlApp: $DBGSTART_MSG)"
    elif grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
        skip_row " (SDL could not initialise on this host)"
    else
        fail_row " (SdlApp: $DBGSTART_MSG)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
