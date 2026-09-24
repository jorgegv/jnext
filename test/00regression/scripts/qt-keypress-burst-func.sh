#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Qt frontend BURST-typing test (GitHub issue #268) — two keystrokes inside one
# inter-frame gap.
#
# WHY A SECOND KEYPRESS ROW. qt-keypress-func next door already types into the
# running Qt frontend, and it is BLIND to this defect for one reason: it sleeps
# 0.3 s between keys. Every key it sends therefore lands in its own inter-frame
# gap, which is the case that always worked. #268 is what happens when two
# keystrokes land in the SAME gap — the second was asserted on top of the first
# and the guest sampled a two-key CHORD the host never had. That is not a
# corner case: it is ordinary typing the moment the frame rate drops, and it is
# what the issue's reporter hit.
#
# MEASURED before the fix, this same recipe, v1.0.30 under Xvfb:
#
#   xdotool key --delay 0 ctrl p     ->  guest typed `"`   (SYMBOL SHIFT + P)
#   xdotool key --delay 0 1 2        ->  guest typed NOTHING AT ALL
#   the same keys 0.35 s apart       ->  PRINT, and 12
#
# So the defect both LOSES characters and silently produces the WRONG one, and
# neither shows up with a sleep in between.
#
# THE MEASUREMENT IS A DIFFERENTIAL, SO IT NEEDS NO REFERENCE IMAGE. Three runs
# of 48K BASIC, identical but for the typing, each ending in a
# --delayed-screenshot at a fixed EMULATED frame:
#
#   control  nothing typed                     — the baseline screen
#   slow     the same four keys, 0.35 s apart  — what the guest SHOULD show
#   burst    the same four keys as TWO BURSTS  — the race
#
# and the row passes only when `burst` is PIXEL-IDENTICAL to `slow`. Typing
# fast may lag, and after the fix it does; it may not change what is typed.
# `slow` doubles as the environment's self-test exactly as in qt-keypress-func:
# if it matches `control`, the X server delivered no keys at all and the row
# SKIPS rather than accusing the emulator — the pre-fix build reads identically
# there, so a row that cannot tell them apart must not blame anyone.
#
# WHY `ctrl p` AND `1 2`, AND WHY TWO KEYSTROKES PER BURST.
#   - `ctrl p` is the issue's own headline repro and the only one that catches
#     BOTH halves of the fix: the router must split the two taps into separate
#     frames, AND the membrane shift hysteresis must have expired by the next
#     frame (it was advanced once per video frame instead of once per 4608
#     master cycles, so SYMBOL SHIFT stayed "pressed" into P's frame and the
#     guest still typed `"` with the router fix alone).
#   - `1 2` is the plain two-key case, with no shift involved at all.
#   - TWO per burst, not four, because of the GUEST, not the emulator: the 48K
#     ROM's KEYBOARD routine (0x02BF) parks an accepted key in one of two
#     K-STATE sets for five interrupts, so it accepts at most two keys per five
#     frames no matter what the matrix does. A four-key burst delivered one per
#     frame yields `12` from `1 2 3 4` — measured, and that is the ROM behaving
#     as it does on hardware. Bursting two is the honest test of THIS layer.
#     (A shift takes no K-STATE set: K-TEST returns no character for a
#     shift-only scan, so `ctrl p` costs one set, not two.)
#
# The two bursts are separated by a normal 0.35 s so the ROM's sets have
# retired between them — this row measures sub-frame delivery, not the ROM's
# throughput.
#
# Everything about how the keys get in — bare Xvfb, no window manager, focus
# left at PointerRoot, the pointer aimed at the window centre in ALL THREE runs,
# the real window selected by its title rather than by `xdotool search` order —
# is the same as qt-keypress-func, for the same reasons, documented at length
# there.
if want qt-keypress-burst-func; then
    begin_func qt-keypress-burst-func

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        # One run. $1 = output PNG, $2 = none|slow|burst.
        #
        # The inner body is single-quoted ON PURPOSE: everything it needs
        # arrives as a positional argument, so nothing from this shell is
        # interpolated into a string that xvfb-run re-parses; the linter reads
        # that as SC2016.
        # shellcheck disable=SC2016
        qt_burst_run() {
            local out="$1" mode="$2"
            rm -f "$out"
            env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
            timeout --foreground --kill-after=5s 120s \
            xvfb-run -a --server-args="-screen 0 1280x1024x24" bash -c '
                set -uo pipefail
                bin="$1"; out="$2"; mode="$3"
                "$bin" --machine 48k --silent \
                    --delayed-screenshot "$out" --delayed-screenshot-frames 700 \
                    --delayed-automatic-exit-frames 760 >/dev/null 2>&1 &
                pid=$!

                wid=""
                for _ in $(seq 1 100); do
                    wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                    [ -n "$wid" ] && break
                    sleep 0.2
                done
                # 48K BASIC is at its prompt by emulated frame 150; six seconds
                # is 4x that at 1x speed. If it is not enough the keys are lost
                # in the `slow` run too and the row skips.
                sleep 6
                if [ -n "$wid" ]; then
                    X=0; Y=0; WIDTH=0; HEIGHT=0   # set -u safety if the query fails
                    eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                    if [ "$WIDTH" -ge 320 ] && [ "$HEIGHT" -ge 240 ]; then
                        xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                        case "$mode" in
                        slow)
                            # One key per inter-frame gap: the case that always
                            # worked, and the reference for what SHOULD appear.
                            for k in ctrl p 1 2; do
                                xdotool key --delay 0 "$k" 2>/dev/null || true
                                sleep 0.35
                            done
                            ;;
                        burst)
                            # TWO keystrokes with nothing between them: both
                            # reach Qt inside one inter-tick gap. THE RACE.
                            xdotool key --delay 0 ctrl p 2>/dev/null || true
                            sleep 0.35
                            xdotool key --delay 0 1 2 2>/dev/null || true
                            sleep 0.35
                            ;;
                        esac
                    fi
                fi
                wait $pid
            ' _ "$JNEXT" "$out" "$mode" >/dev/null 2>&1 || true
        }

        shot_control="$TMP_DIR/qt_burst_control.png"
        shot_slow="$TMP_DIR/qt_burst_slow.png"
        shot_burst="$TMP_DIR/qt_burst_fast.png"

        qt_burst_run "$shot_control" none
        qt_burst_run "$shot_slow"    slow
        qt_burst_run "$shot_burst"   burst

        if [[ ! -s "$shot_control" || ! -s "$shot_slow" || ! -s "$shot_burst" ]]; then
            skip_row " (no screenshot captured; Qt could not open a window?)"
        else
            slow_diff=$(png_diff "$shot_control" "$shot_slow")
            burst_vs_slow=$(png_diff "$shot_slow" "$shot_burst")
            burst_vs_control=$(png_diff "$shot_control" "$shot_burst")
            if [[ "$slow_diff" -eq 0 ]]; then
                # Spaced-out keys did not reach the guest either, so this run
                # says nothing about the race. Never a FAIL: the pre-fix code
                # reads identically here.
                skip_row " (X server delivered no keys at all; spaced-out control also unchanged)"
            elif [[ "$burst_vs_slow" -ne 0 ]]; then
                fail_row " (issue #268: two keystrokes inside one frame gap typed something ELSE — burst differs from spaced-out by ${burst_vs_slow} px; burst vs nothing-typed ${burst_vs_control} px, spaced-out vs nothing-typed ${slow_diff} px)"
            else
                pass_row " (burst typing matches spaced-out typing exactly: 0 px, both ${slow_diff} px from the untyped screen)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
