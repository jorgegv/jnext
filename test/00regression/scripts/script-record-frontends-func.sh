#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #20 / GH #26 WP6 — --record-script in the two windowed loop owners (the
# headless one is script-record-replay-func): each starts the recorder, hands it
# the session's facts (here the fixed --rtc, which only the loop owner knows)
# and writes the script when it exits at the --delayed-automatic-exit bound.
if want script-record-frontends-func; then
    begin_func script-record-frontends-func
    sdl_bin="$JNEXT_SDL"
    fails=()
    check_rec() {   # $1 = what, $2 = file, $3 = run output
        if [[ ! -s "$2" ]]; then
            fails+=("$1: no script written")
        elif ! grep -q '^# jds-recorder: 1$' "$2" ||
             ! grep -q '^# machine=48k load=none rtc=2026-01-01 00:00:00 ' "$2" ||
             ! grep -q '^on frame [0-9]* do exit 0 end$' "$2"; then
            fails+=("$1: header $(head -n 3 "$2" | tr '\n' ' ')")
        elif ! grep -q "RECORD: wrote" <<<"$3"; then
            fails+=("$1: no RECORD: wrote line")
        fi
    }
    if [[ ! -x "$sdl_bin" ]]; then
        fails+=("SDL-only binary not built: $sdl_bin")
    else
        out=$(LANG=C timeout --foreground --kill-after=5s 60s env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent --machine 48k "${SD_CARD_ARGS[@]}" \
            --rtc "2026-01-01 00:00:00" --record-script "$TMP_DIR/rf-sdl.jds" \
            --delayed-automatic-exit-frames 60 2>&1) && rc=0 || rc=$?
        [[ $rc -eq 0 ]] || fails+=("SDL: exit $rc")
        check_rec SDL "$TMP_DIR/rf-sdl.jds" "$out"
    fi
    out=$(LANG=C QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy timeout --foreground --kill-after=5s 60s \
        "$JNEXT" --silent --machine 48k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --rtc "2026-01-01 00:00:00" --record-script "$TMP_DIR/rf-qt.jds" \
        --delayed-automatic-exit-frames 60 2>&1) && rc=0 || rc=$?
    [[ $rc -eq 0 ]] || fails+=("Qt: exit $rc")
    check_rec Qt "$TMP_DIR/rf-qt.jds" "$out"
    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (SDL and Qt: recording started, the session's --rtc in the header, written at exit)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
