#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend host-keypress test (GitHub issue #122).
#
# WHY THIS ROW EXISTS AT ALL. jnext's host-key minimum-hold latch is unit-tested
# to death (host_key_latch_test, rows HK-* and RT-* for the policy, SDL-* for
# the tick shapes). None of that touches the three lines in src/platform/
# sdl_app.cpp that CONNECT the latch to the running emulator — the attach(), the
# on_host_key() forward and the on_tick_end() discharge. Reverting all three to
# the pre-fix `emulator_.keyboard().set_key(sc, pressed)` leaves the entire unit
# suite green (5846/5846, measured), because SdlApp::run() is reachable only
# through a live SDL window. That is the same irreducible-glue gap that
# audio-underrun-func exists for, and it gets the same treatment: run the real
# binary under a virtual X server and look at what the guest actually did.
#
# THE BINARY IS NOT $JNEXT. SdlApp is instantiated only when ENABLE_QT_UI=OFF
# (src/main.cpp:944-951); the suite's usual binary is the Qt one, where the code
# under test is compiled but never runs. So this row uses build/sdl-release,
# which `make regression` builds and the driver's preflight demands — the same
# contract rewind-func has for build/test/rewind_test.
#
# THE MEASUREMENT. Three runs of 48K BASIC, each ending in a --delayed-screenshot
# at a fixed EMULATED frame:
#
#   control  nothing typed                      — the baseline screen
#   slow     keydown, pause, keyup              — a release that arrives after a
#                                                 frame has run, so it is
#                                                 delivered unlatched and works
#                                                 with OR without the fix
#   fast     xdotool key --delay 0              — press and release emitted back
#                                                 to back, so both are drained by
#                                                 one SDL_PollEvent(): THE RACE
#
# `slow` is the environment's self-test, and it is what makes this row safe to
# run unattended. A headless X server that cannot deliver keys at all, a keymap
# that does not resolve the keysyms, a machine too loaded to reach the BASIC
# prompt before typing starts — every one of those makes `slow` match `control`
# too, and the row SKIPS instead of blaming jnext. Only a run where `slow`
# proved the keys DO get through and `fast` still changed nothing is a FAIL, and
# that is exactly issue #122.
#
# DIGITS, NOT LETTERS. At the 48K `K` cursor a digit types itself, while a
# letter is a BASIC keyword — and letter keysyms proved unreliable under a
# loaded Xvfb during review (xkb "Multiple symbols" warnings, keys not
# registering). The race is keysym-agnostic, so the row uses the vocabulary that
# survives a hostile X server.
if want sdl-keypress-func; then
    begin_func sdl-keypress-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    # Typed at the K cursor; any one of them landing is decisive.
    sdl_keys=(1 2 3 4)

    # One run. $1 = output PNG, $2 = none|slow|fast.
    #
    # xvfb-run only sets DISPLAY, and on a Wayland session SDL prefers the
    # Wayland backend and never opens a window on the Xvfb display at all
    # (observed: `xdotool search` found nothing). So WAYLAND_DISPLAY is unset
    # and the x11 backend forced, exactly as audio-underrun-func does for Qt.
    # The inner body is single-quoted ON PURPOSE: everything it needs arrives as
    # a positional argument, so nothing from this shell is interpolated into a
    # string that xvfb-run re-parses. shellcheck reads that as SC2016.
    # shellcheck disable=SC2016
    sdl_keypress_run() {
        local out="$1" mode="$2"
        rm -f "$out"
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s \
        xvfb-run -a --server-args="-screen 0 1280x1024x24" bash -c '
            set -uo pipefail
            bin="$1"; out="$2"; mode="$3"; shift 3
            "$bin" --machine 48k --silent \
                --delayed-screenshot "$out" --delayed-screenshot-frames 500 \
                --delayed-automatic-exit-frames 560 >/dev/null 2>&1 &
            pid=$!

            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --name JNEXT 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # Generous: 48K BASIC is at its prompt by emulated frame 150, but a
            # loaded runner emulates slower than real time and typing into a
            # machine that has not booted yet is how this row would flake. Six
            # seconds is 4x the requirement at 1x speed; if it is still not
            # enough the keys are lost in the `slow` run too and the row skips.
            sleep 6
            if [ -n "$wid" ] && [ "$mode" != none ]; then
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                for k in "$@"; do
                    if [ "$mode" = fast ]; then
                        # Press and release with nothing between them.
                        xdotool key --delay 0 "$k" 2>/dev/null || true
                    else
                        # Held well over one 20 ms frame, so a frame samples it
                        # down and the release needs no latch to be seen.
                        xdotool keydown "$k" 2>/dev/null || true
                        sleep 0.25
                        xdotool keyup "$k" 2>/dev/null || true
                    fi
                    sleep 0.3
                done
            fi
            wait $pid
        ' _ "$sdl_bin" "$out" "$mode" "${sdl_keys[@]}" >/dev/null 2>&1 || true
    }

    shot_control="$TMP_DIR/sdl_keypress_control.png"
    shot_slow="$TMP_DIR/sdl_keypress_slow.png"
    shot_fast="$TMP_DIR/sdl_keypress_fast.png"

    if [[ ! -x "$sdl_bin" ]]; then
        # A build artifact the Makefile can guarantee, so this is loud (the
        # rewind-func rule). The driver's preflight normally catches it first.
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        sdl_keypress_run "$shot_control" none
        sdl_keypress_run "$shot_slow"    slow
        sdl_keypress_run "$shot_fast"    fast

        if [[ ! -s "$shot_control" || ! -s "$shot_slow" || ! -s "$shot_fast" ]]; then
            skip_row " (no screenshot captured; SDL could not open a window?)"
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
                fail_row " (issue #122: held keys registered (${slow_diff} px) but a press+release in one poll batch changed nothing)"
            else
                pass_row " (sub-frame keypress registered: ${fast_diff} px vs ${slow_diff} px held)"
            fi
        fi
    fi
fi
