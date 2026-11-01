#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #360 -- the debugger window's default-size / grow rows under a REAL window
# manager. The offscreen QPA platform has no window manager: it applies every
# resize() as asked and has no title bar. Under X11 a manager answers the
# opening fit's requests in ways only a real one does, and a real user is on X11:
#   - the REVERT: the first (pre-map) request is answered by the map with the
#     size the window had before it. The fit asks again from there, once the
#     window is managed and its frame is known (REVOFF, NOREVSKIP kill it);
#   - the late ECHO: openbox applies the first request after later ones. That
#     stale size is the manager's, not the user's, and must not end the fit or be
#     the final size (the size test's DW-11/DW-12, frame taller than the work area);
#   - a USER resize during the fit still ends it (Q131-05; NOUSER, NOUSERREV);
#   - the height decoration is the manager's real one, not the style's estimate
#     (OLDDECO: DW-12 `frame 771 tall`).
# This row runs the same test binaries with JNEXT_TEST_KEEP_QPA set (they keep
# QT_QPA_PLATFORM=xcb instead of forcing offscreen) inside xvfb-run + openbox:
#   3000x2200: debugger_window_grow_test (GR-05, 3 runs: the manager's timing is
#              probabilistic) and quit_cleanup_test (Q131-05);
#   1366x768:  debugger_window_size_test (DW-11, DW-12 and the rest).
# NOUSER and NOUSERREV race the user's resize against the first pass, so the
# unit suite's Q131-05 (offscreen) is what kills NOUSER deterministically.
# openbox, Xvfb and the three binaries are required: absent = FAIL, never SKIP.
if want debugger-window-wm-func; then
    begin_func debugger-window-wm-func

    if ! command -v xvfb-run &>/dev/null; then
        fail_row " (xvfb-run not available)"
    elif ! command -v openbox &>/dev/null; then
        fail_row " (openbox not available: install the openbox package)"
    elif [[ ! -x "$TEST_BIN_DIR/debugger_window_grow_test" || ! -x "$TEST_BIN_DIR/debugger_window_size_test" \
         || ! -x "$TEST_BIN_DIR/quit_cleanup_test" ]]; then
        fail_row " (debugger_window_grow_test / debugger_window_size_test / quit_cleanup_test not built: 'make unit-test-build')"
    else
        wm_dir="$TMP_DIR/debugger-window-wm"
        mkdir -p "$wm_dir"

        # One X session. $1 = screen WxHxD, $2 = log prefix, rest = "name:repeat" pairs.
        # The inner body is single-quoted on purpose (see qt-keypress-burst-func).
        # shellcheck disable=SC2016
        wm_session() {
            local screen="$1" prefix="$2"; shift 2
            env -u WAYLAND_DISPLAY JNEXT_TEST_KEEP_QPA=1 QT_QPA_PLATFORM=xcb \
            timeout --kill-after=5s 400s \
            xvfb-run -d --server-args="-screen 0 $screen -noreset" bash -c '
                set -uo pipefail
                bindir="$1"; prefix="$2"; shift 2
                openbox >"$prefix.openbox.log" 2>&1 &
                wm=$!
                sleep 2
                if ! kill -0 "$wm" 2>/dev/null; then echo "openbox did not stay up" >"$prefix.fault"; exit 0; fi
                for spec in "$@"; do
                    name="${spec%%:*}"; n="${spec##*:}"
                    for i in $(seq 1 "$n"); do
                        timeout --kill-after=5s 150s "$bindir/$name" >"$prefix.$name.$i.log" 2>&1
                        echo $? >"$prefix.$name.$i.rc"
                    done
                done
                kill "$wm" 2>/dev/null
                wait "$wm" 2>/dev/null
                exit 0
            ' _ "$TEST_BIN_DIR" "$prefix" "$@" >/dev/null 2>&1 || true
        }

        wm_session 3000x2200x24 "$wm_dir/roomy" debugger_window_grow_test:3 quit_cleanup_test:1
        wm_session 1366x768x24 "$wm_dir/laptop" debugger_window_size_test:1

        wm_bad=""
        wm_runs=0
        for prefix in roomy laptop; do
            if [[ -f "$wm_dir/$prefix.fault" ]]; then
                wm_bad+=" $prefix: $(cat "$wm_dir/$prefix.fault");"
            fi
        done
        for rc_file in "$wm_dir"/*.rc; do
            [[ -e "$rc_file" ]] || continue
            wm_runs=$((wm_runs + 1))
            log="${rc_file%.rc}.log"
            summary=$(grep -E '^Total:' "$log" | tail -1 || true)
            if [[ "$(cat "$rc_file")" != 0 || -z "$summary" || ! "$summary" =~ Failed:\ +0\  || ! "$summary" =~ Skipped:\ +0 ]]; then
                wm_bad+=" $(basename "${rc_file%.rc}") (rc=$(cat "$rc_file"), ${summary:-no Total line});"
                grep -E 'FAIL' "$log" | head -3 >>"$wm_dir/failures.txt" || true
            fi
        done
        if [[ "$wm_runs" -ne 5 ]]; then
            wm_bad+=" expected 5 test runs, got $wm_runs;"
        fi
        if [[ -z "$wm_bad" ]]; then
            pass_row " (5 runs under Xvfb + openbox: grow x3, quit_cleanup, window_size at 1366x768)"
        else
            fail_row " ($wm_bad $(head -c 400 "$wm_dir/failures.txt" 2>/dev/null | tr '\n' ' '))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
