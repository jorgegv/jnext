#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Qt frontend host-keypress test (GitHub issue #137) — the twin of
# sdl-keypress-func, for the frontend every shipped package actually uses.
#
# WHY THIS ROW EXISTS AT ALL. Same shape as the gap #122 closed for SDL.
# src/gui/qt_app.cpp has exactly three lines that CONNECT the issue-#120
# minimum-hold latch to the running emulator — the attach() in
# wire_gamepad_and_sources(), the on_host_key() forward in the window's key
# callback, and the on_tick_end() discharge in TickEffects::post_frames(). The
# latch's policy is unit-tested to death (host_key_latch_test), but none of
# those rows touches the wiring: reverting all three to the pre-fix
# `emulator_.keyboard().set_key(sc, pressed)` leaves the whole unit suite green
# — 5914/5914 across 84 suites, MEASURED with the reverted file in the tree, and
# build/ is configured ENABLE_QT_UI=ON so it really was compiled in — because
# QtApp::run() is reachable only through a live QApplication. Before
# this row, no committed test reached it: the seven Qt-exercising regression
# rows all run QT_QPA_PLATFORM=offscreen or bare xvfb-run and none injects a
# key. So the same treatment applies — run the real binary under a virtual X
# server and look at what the guest actually did.
#
# THE COST, SINCE #137 LEFT IT OPEN. 44 s wall for all three runs — within a
# second of what sdl-keypress-func already costs, so this belongs in the main
# suite and not a slower tier. The ~28 s/run figure in the issue came from
# screenshot-io-qt-func, which boots NextZXOS off the SD card; the frontend is
# not what costs, real-time pacing of 500 emulated frames is. Measured on the
# dev box: SDL under Xvfb 14.9 s, Qt under Xvfb 14.7 s, Qt offscreen 11.7 s.
#
# THE BINARY IS $JNEXT, AND THAT IS THE POINT. The SDL twin has to reach past
# $JNEXT for build/sdl-release because SdlApp is compiled but never run in the
# default build (src/main.cpp:944-951). Here the relationship is inverted:
# QtApp IS what $JNEXT runs. test-functions.inc resolves $JNEXT to
# build/gui-release/jnext, which `make regression` already builds as a declared
# prerequisite, so this row needs NO new artifact and NO new preflight guard —
# an absent Qt binary already fails most of the suite.
#
# UNGUARDED GAP, DELIBERATELY. A caller who overrides JNEXT= to a non-Qt build
# gets a row that still measures a real frontend, just not this one. That IS
# checkable: `nm -D "$JNEXT" | grep -q QApplication` separates the two builds
# (measured — 2 matches in gui-release, 0 in sdl-release). Note the trap:
# `ldd | grep Qt6` does NOT work, it matches 4 libraries in BOTH, because the
# debugger pulls Qt6 into sdl-release too. No guard is added anyway, because
# screenshot-io-qt-func — the other Qt row — has the identical unguarded gap,
# and consistency between the two Qt rows is worth more than closing it in one
# of them. Guard both or neither.
#
# THE MEASUREMENT. Three runs of 48K BASIC, each ending in a
# --delayed-screenshot at a fixed EMULATED frame:
#
#   control  pointer moved in, nothing typed  — the baseline screen
#   slow     keydown, pause, keyup            — a release that arrives after a
#                                               frame has run, so it is
#                                               delivered unlatched and works
#                                               with OR without the fix
#   fast     xdotool key --delay 0            — press and release emitted back
#                                               to back, so both reach Qt's
#                                               event loop inside one 20 ms
#                                               inter-tick gap: THE RACE
#
# `slow` is the environment's self-test, and it is what makes this row safe to
# run unattended. A headless X server that cannot deliver keys at all, a keymap
# that does not resolve the keysyms, a machine too loaded to reach the BASIC
# prompt before typing starts — every one of those makes `slow` match `control`
# too, and the row SKIPS instead of blaming jnext. Only a run where `slow`
# proved the keys DO get through and `fast` still changed nothing is a FAIL,
# and that is exactly the regression this row is here for.
#
# DIGITS, NOT LETTERS. At the 48K `K` cursor a digit types itself, while a
# letter is a BASIC keyword — and letter keysyms proved unreliable under a
# loaded Xvfb during the #122 review (xkb "Multiple symbols" warnings, keys not
# registering). The race is keysym-agnostic, so the row uses the vocabulary
# that survives a hostile X server.
#
# HOW THE KEYS GET IN, WITH NO WINDOW MANAGER. Xvfb runs bare. Two things
# follow, and only the first is a dead end:
#   - `xdotool windowactivate` genuinely refuses, even against the CORRECT
#     window: "_NET_ACTIVE_WINDOW not supported", rc=1 (measured). It needs an
#     EWMH-speaking window manager and there is none.
#   - Input focus therefore stays at PointerRoot (getwindowfocus returns 1), so
#     key events go to whatever window the pointer is over — and the pointer
#     starts at the screen centre 640,512, ONE PIXEL outside the 640x597 Qt
#     window at 0,0. Do nothing about either and every run scores 0 px and looks
#     like a broken emulator. That is what a naive port of this row does.
#
# TWO mechanisms fix that, and BOTH were measured to work here, producing
# byte-identical guest screens (each 1460 px against its control, 0 px against
# each other): `xdotool windowfocus --sync <main window>` — which returns rc=0
# with no error, both immediately after discovery and after the 6 s grace — and
# moving the pointer into the window. Re-measured with openbox running inside
# the Xvfb: both still deliver, 1460 px each. So there is no robustness argument
# between them in either direction. This row moves the pointer because that is
# the version with the run history behind it, NOT because focus was unavailable.
#
# NO BadMatch CLAIM HERE. An earlier draft of this header said `windowfocus
# --sync` dies with BadMatch under bare Xvfb. It does not. BadMatch appears only
# when focus is aimed at the WRONG window — the auxiliary Qt window that
# `xdotool search --name JNEXT` returns FIRST (a 3x3 selection owner, or a 10x10
# utility window named "jnext"; the ids vary per run). That is the same
# wrong-window root cause the title match below already fixes, not a property of
# Xvfb, and not a reason to prefer one delivery mechanism over the other.
#
# Two further traps this avoids:
#   - `xdotool search --name JNEXT` on the Qt binary matches FOUR windows and
#     the first is one of those auxiliary windows, not the emulator. The main
#     window is selected by its real title (src/gui/main_window.cpp:214) and
#     its geometry is then sanity-checked, because that geometry is also what
#     the pointer is aimed at.
#   - ALL THREE runs move the pointer, including `control`. Only the typing
#     differs between them, so a pixel diff can only be the keys. (The SDL twin
#     touches focus in the typing runs only. Whichever mechanism delivers the
#     keys, it has to be held constant across the three runs, or a side effect
#     of the delivery step could be misread as a keypress.)
if want qt-keypress-func; then
    begin_func qt-keypress-func

    # Typed at the K cursor; any one of them landing is decisive.
    qt_keys=(1 2 3 4)

    # One run. $1 = output PNG, $2 = none|slow|fast.
    #
    # xvfb-run only sets DISPLAY, and on a Wayland session Qt prefers the
    # Wayland backend and never opens a window on the Xvfb display at all. So
    # WAYLAND_DISPLAY is unset and the xcb platform forced, exactly as
    # audio-underrun-func does. The inner body is single-quoted ON PURPOSE:
    # everything it needs arrives as a positional argument, so nothing from
    # this shell is interpolated into a string that xvfb-run re-parses; the
    # linter reads that as SC2016.
    # shellcheck disable=SC2016
    qt_keypress_run() {
        local out="$1" mode="$2"
        rm -f "$out"
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s \
        xvfb-run -a --server-args="-screen 0 1280x1024x24" bash -c '
            set -uo pipefail
            bin="$1"; out="$2"; mode="$3"; shift 3
            "$bin" --machine 48k --silent \
                --delayed-screenshot "$out" --delayed-screenshot-frames 500 \
                --delayed-automatic-exit-frames 560 >/dev/null 2>&1 &
            pid=$!

            # The real main window, by the title MainWindow sets. The mouse-grab
            # title (main_window.cpp) only APPENDS to it, so this still matches.
            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # Generous: 48K BASIC is at its prompt by emulated frame 150, but a
            # loaded runner emulates slower than real time and typing into a
            # machine that has not booted yet is how this row would flake. Six
            # seconds is 4x the requirement at 1x speed; if it is still not
            # enough the keys are lost in the `slow` run too and the row skips.
            sleep 6
            if [ -n "$wid" ]; then
                # No focus call is made, so focus stays at PointerRoot and the
                # pointer decides where keys land. Aim at the window centre.
                # WIDTH/HEIGHT also sanity-check that this is the emulator
                # window and not one of the auxiliary Qt windows; too small and
                # nothing is typed, so the run reads as "no keys" and the row
                # skips rather than accusing the emulator.
                X=0; Y=0; WIDTH=0; HEIGHT=0   # set -u safety if the query fails
                eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                if [ "$WIDTH" -ge 320 ] && [ "$HEIGHT" -ge 240 ]; then
                    xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                    if [ "$mode" != none ]; then
                        for k in "$@"; do
                            if [ "$mode" = fast ]; then
                                # Press and release with nothing between them.
                                xdotool key --delay 0 "$k" 2>/dev/null || true
                            else
                                # Held well over one 20 ms frame, so a frame
                                # samples it down and the release needs no latch
                                # to be seen.
                                xdotool keydown "$k" 2>/dev/null || true
                                sleep 0.25
                                xdotool keyup "$k" 2>/dev/null || true
                            fi
                            sleep 0.3
                        done
                    fi
                fi
            fi
            wait $pid
        ' _ "$JNEXT" "$out" "$mode" "${qt_keys[@]}" >/dev/null 2>&1 || true
    }

    shot_control="$TMP_DIR/qt_keypress_control.png"
    shot_slow="$TMP_DIR/qt_keypress_slow.png"
    shot_fast="$TMP_DIR/qt_keypress_fast.png"

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        qt_keypress_run "$shot_control" none
        qt_keypress_run "$shot_slow"    slow
        qt_keypress_run "$shot_fast"    fast

        if [[ ! -s "$shot_control" || ! -s "$shot_slow" || ! -s "$shot_fast" ]]; then
            skip_row " (no screenshot captured; Qt could not open a window?)"
        else
            slow_diff=$(png_diff "$shot_control" "$shot_slow")
            fast_diff=$(png_diff "$shot_control" "$shot_fast")
            if [[ "$slow_diff" -eq 0 ]]; then
                # Held keypresses did not reach the guest either, so this run
                # says nothing about the race. Never a FAIL: the pre-fix code
                # produces an IDENTICAL reading here, and a row that cannot tell
                # the two apart must not accuse the emulator.
                skip_row " (X server delivered no keys at all; held-key control also unchanged)"
            elif [[ "$fast_diff" -eq 0 ]]; then
                fail_row " (issue #137: held keys registered (${slow_diff} px) but a press+release inside one Qt tick gap changed nothing)"
            else
                pass_row " (sub-frame keypress registered: ${fast_diff} px vs ${slow_diff} px held)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
