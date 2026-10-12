#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #281 (gdb-rsp-frontend.md WP-4) — the GDB RSP server on the SDL loop owner
# (SdlApp). gdb-cli-func runs --headless; this one proves SdlApp registers the
# server through DebugServers. SDL_VIDEODRIVER=dummy, as dzrp-sdl-func: no X
# server needed. The client (gdb-peer.py handshake) is z88dk-gdb's connect
# sequence: qSupported, target.xml, ?, g, m, s, monitor mmu, D.
if want gdb-sdl-func; then
    begin_func gdb-sdl-func
    sdl_bin="$JNEXT_SDL"
    log="$TMP_DIR/gdb-sdl-func.log"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the GDB client)"
    elif ! gdb_launch_cmd "$log" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --machine 48k \
            --gdb-port 0; then
        gdb_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'gdb: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    elif gdb_peer handshake "$GDB_PORT"; then
        gdb_stop
        pass_row " (SdlApp: ${GDB_OUT#PASS })"
    else
        gdb_stop
        fail_row " (SdlApp: ${GDB_OUT#FAIL })"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
