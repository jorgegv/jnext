#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# File > Recent Files records a startup --load (GH #406), on the real Qt binary.
# Oracle: the issue and its comment (a program started with --load joins the
# recent list once it has loaded; a failed one does not). The unit suite
# recent_files_menu_test drives MainWindow, but main.cpp's set_startup_load()
# and QtApp's startup_rzx_started() are the wiring between the command line and
# the window, and QtApp cannot be constructed in a unit suite.
#
# Offscreen Qt, on every OS (no X needed). The row's config dir is its own
# $RUN_DIR (= $JNEXT_CONFIG_DIR), which also holds the SD-card clone, so only
# jnext.conf is removed between runs.
if want recent-load-func; then
    begin_func recent-load-func

    rl_problem=""
    rl_conf="$RUN_DIR/jnext.conf"
    rl_run() {   # rl_run <log> <jnext args...>: the Qt window, offscreen
        local log="$1"; shift
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            timeout --kill-after=5s 90s \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --machine 48k --silent \
            --delayed-automatic-exit 3 "$@" >"$log" 2>&1 || true
    }
    rl_recent() {   # the [recent] group of the saved config, "" when absent
        [[ -f $rl_conf ]] || return 0
        sed -n '/^\[recent\]/,/^\[/p' "$rl_conf"
    }

    # A recording to play: 48K, two seconds (as rzx-playback-func).
    rl_rzx="$TMP_DIR/recent.rzx"
    timeout --foreground --kill-after=5s 15s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --machine 48k --rzx-record "$rl_rzx" --delayed-automatic-exit 2 &>/dev/null || true
    [[ -s $rl_rzx ]] || rl_problem="could not record the .rzx to play"

    rm -f "$rl_conf"
    if [[ -z $rl_problem ]]; then
        rl_run "$TMP_DIR/rl-tap.log" --load "$SCRIPT_DIR/tap/beeper_demo.tap"
        rl_got=$(rl_recent)
        [[ $rl_got == *beeper_demo.tap* ]] ||
            rl_problem="--load beeper_demo.tap was not recorded: [$rl_got]"
    fi
    if [[ -z $rl_problem ]]; then
        rl_run "$TMP_DIR/rl-rzx.log" --load "$rl_rzx"
        rl_got=$(rl_recent)
        [[ $rl_got == *recent.rzx* && $rl_got == *beeper_demo.tap* ]] ||
            rl_problem="--load recent.rzx was not recorded next to the earlier entry: [$rl_got]"
    fi
    if [[ -z $rl_problem ]]; then
        rl_before=$(rl_recent)
        rl_run "$TMP_DIR/rl-bad.log" --load "$TMP_DIR/no-such-file.tap"
        rl_after=$(rl_recent)
        if [[ $rl_after != "$rl_before" || $rl_after == *no-such-file* ]]; then
            rl_problem="a failing --load changed the list: [$rl_after]"
        fi
    fi

    if [[ -z $rl_problem ]]; then
        pass_row " (--load of a .tap and an .rzx recorded; a failing --load is not)"
    else
        fail_row " ($rl_problem)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
