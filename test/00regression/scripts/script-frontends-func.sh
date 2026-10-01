#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (§6.3) — the two windowed loop owners. The SDL-only frontend has
# no pause, so a script decides its exit as headless does: `exit 7` exits 7,
# a `stop` exits 3, a verdict the watchdog cut off exits 3. The Qt GUI never exits from a script: `exit 4` is logged
# and pauses, and the run ends at the --delayed-automatic-exit bound with 0;
# a script that does not load is still a startup failure (exit 1).
if want script-frontends-func; then
    begin_func script-frontends-func
    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    fails=()
    printf 'on frame 3 do exit 7 end\n' > "$TMP_DIR/sf-exit.jds"
    printf 'on frame 3 do stop "sdl stop" end\n' > "$TMP_DIR/sf-stop.jds"
    printf 'on frame 3 do exit 4 end\n' > "$TMP_DIR/sf-gui.jds"
    printf 'on frame 5000 do exit 0 end\n' > "$TMP_DIR/sf-late.jds"
    printf 'on frame 3 do\n  log +\nend\n' > "$TMP_DIR/sf-bad.jds"
    sdl_run() {
        LANG=C timeout --foreground --kill-after=5s 60s env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy "$sdl_bin" --silent --machine 48k "${SD_CARD_ARGS[@]}" \
            --delayed-automatic-exit-frames 100 "$@" 2>&1
    }
    qt_run() {
        LANG=C QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy timeout --foreground --kill-after=5s 60s \
            "$JNEXT" --silent --machine 48k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-automatic-exit-frames 30 "$@" 2>&1
    }
    if [[ ! -x "$sdl_bin" ]]; then
        fails+=("SDL-only binary not built: $sdl_bin")
    else
        out=$(sdl_run --script "$TMP_DIR/sf-exit.jds") && rc=0 || rc=$?
        if grep -qE "SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:" <<<"$out"; then
            fails+=("SDL could not initialise")
        elif [[ $rc -ne 7 ]] || ! grep -q "SCRIPT EXIT 7" <<<"$out"; then
            fails+=("SDL exit 7: exit $rc")
        fi
        out=$(sdl_run --script "$TMP_DIR/sf-stop.jds") && rc=0 || rc=$?
        if [[ $rc -ne 3 ]] || ! grep -q "SCRIPT STOP: sdl stop" <<<"$out"; then
            fails+=("SDL stop: exit $rc")
        fi
        out=$(sdl_run --script "$TMP_DIR/sf-late.jds") && rc=0 || rc=$?
        if [[ $rc -ne 3 ]] || ! grep -q "SCRIPT: 1 deferred actions never ran" <<<"$out"; then
            fails+=("SDL watchdog before the verdict: exit $rc")
        fi
    fi
    out=$(qt_run --script "$TMP_DIR/sf-gui.jds") && rc=0 || rc=$?
    if [[ $rc -ne 0 ]] || ! grep -q "SCRIPT EXIT 4" <<<"$out" || grep -q "script requested exit" <<<"$out"; then
        fails+=("Qt exit 4: exit $rc")
    fi
    out=$(qt_run --script "$TMP_DIR/sf-bad.jds") && rc=0 || rc=$?
    if [[ $rc -ne 1 ]] || ! grep -q "SCRIPT ERROR .*sf-bad.jds:2:" <<<"$out"; then
        fails+=("Qt load error: exit $rc")
    fi
    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (SDL: exit 7 = 7, stop = 3, verdict not reached = 3; Qt: exit pauses, bound exits 0; Qt load error = 1)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
