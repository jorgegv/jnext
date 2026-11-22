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

# GH #548 (--debugger on the SDL loop owner), two launches (SDL_VIDEODRIVER=dummy, as dzrp-sdl-func):
#   1. `--debugger --dzrp-port 0 NEX`: paused at the NEX entry (a plain pause), a client reads the
#      PC, held, and CONTINUE runs it.
#   2. `--break-at _main --script s.jds --dzrp-port 0 NEX`: the stop is reached by RUNNING, so it is
#      an Action::Stop, which the SDL loop's StopPolicy decides: under --debugger it pauses and
#      waits (ExitNonZero would exit 3). A client reads 0x80C4 (from the map) and jnext is alive.
if want debugger-sdl-func; then
    begin_func debugger-sdl-func
    log="$TMP_DIR/debugger-sdl-func.log"
    sdl_bin="$JNEXT_SDL"
    sdl_env=(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy)
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! dbgstart_have_python debugger-sdl-func; then
        :
    elif ! dzrp_launch_cmd "$log" "${sdl_env[@]}" \
            "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$DBGSTART_NEX"; then
        dzrp_stop
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:" "$log"; then
            skip_row " (SDL could not initialise on this host)"
        else
            fail_row " (the SDL frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
        fi
    else
        fails=()
        dbgstart_wait_stop "$log" "$DZRP_PID" || fails+=("no stop within 90 s")
        if dzrp_peer startup "$(dbgstart_pc_of nex "$DBGSTART_NEX")"; then
            ok_msg="SdlApp: ${DZRP_OUT#PASS }"
        else
            fails+=("SdlApp: ${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|')")
        fi
        dzrp_stop
        log2="$TMP_DIR/debugger-sdl-script.log"
        jds="$TMP_DIR/debugger-sdl-script.jds"
        echo 'on frame 1000000 once do log "never" end' >"$jds"
        main_hex=$(sed -n 's/^_main  *= \$\([0-9A-Fa-f]*\) ;.*/\1/p' "$DBGSTART_MAP" | head -1)
        if ! dzrp_launch_cmd "$log2" "${sdl_env[@]}" "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" \
                --break-at _main --script "$jds" --dzrp-port 0 --load "$DBGSTART_NEX"; then
            fails+=("--break-at _main --script: never listened: $(tail -n 3 "$log2" | tr '\n' ' ')")
        else
            dbgstart_wait_stop "$log2" "$DZRP_PID" || fails+=("--script run: no stop within 90 s")
            dzrp_peer startup "0x$main_hex" || fails+=("--script run: ${DZRP_OUT#FAIL }")
            kill -0 "$DZRP_PID" 2>/dev/null || fails+=("the SDL run under --script exited during the session")
            ! grep -aqi "requesting exit\|requested exit" "$log2" || fails+=("the SDL run under --script requested an exit")
        fi
        dzrp_stop
        if (( ${#fails[@]} == 0 )); then
            pass_row " (${ok_msg:-}; under --script the stop at _main paused and waited, no exit)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
