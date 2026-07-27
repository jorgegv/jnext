#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend capture/exit contract (GitHub issue #134). The SDL twin of
# exit-frames-func (headless) and screenshot-pending-func (headless) /
# screenshot-io-qt-func (Qt): each frontend enforces this contract in its OWN
# code, and only the two of them that main.cpp can reach with ENABLE_QT_UI=ON
# were ever tested.
#
# SdlApp counts the two countdowns itself, in its hand-rolled loop
# (src/platform/sdl_app.cpp:371-403 — the capture is handled BEFORE the exit
# within a tick), and raises the never-written failure in its own shutdown()
# (sdl_app.cpp:434-440). None of that is shared with QtApp, whose equivalents
# live in frame_sequencer::Sequencer and QtApp::shutdown().
#
# Two runs, N=30 frames so the pair costs ~1.6 s:
#
#   at    capture due at frame N, exit at frame N -> taken, exit 0
#   past  capture due at frame N+1, exit at frame N -> never taken, an error
#         naming the file, exit non-zero, and NO stale PNG left behind
#
# One frame either way and one of the two runs flips, so an off-by-one in the
# SDL loop's countdown cannot hide; and `at` is the positive control that stops
# `past` from passing merely because the emulator never got anywhere.
if want sdl-exit-contract-func; then
    begin_func sdl-exit-contract-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    at_n="$TMP_DIR/sdl-exit-at-n.png"      # capture at the exit frame -> taken
    past_n="$TMP_DIR/sdl-exit-past-n.png"  # capture one frame later   -> never taken
    rm -f "$at_n" "$past_n"
    N=30

    # SDL_VIDEODRIVER=dummy: no X server needed (see sdl-render-func.sh for why
    # that still exercises SdlDisplay's real code path).
    sdl_exit_run() {
        local out="$1" shot_frame="$2"
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 60s \
        "$sdl_bin" --machine 48k --silent "${SD_CARD_ARGS[@]}" \
            --delayed-screenshot "$out" --delayed-screenshot-frames "$shot_frame" \
            --delayed-automatic-exit-frames "$N" 2>&1
    }

    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    else
        if at_out=$(sdl_exit_run "$at_n" "$N"); then at_rc=0; else at_rc=1; fi
        if past_out=$(sdl_exit_run "$past_n" "$((N + 1))"); then past_rc=0; else past_rc=1; fi

        # A host whose SDL cannot bring up a video device fails BOTH runs for a
        # reason that has nothing to do with the contract under test, and
        # SdlApp::init logs SDL's own error text when that happens
        # (sdl_app.cpp:9-14, sdl_display.cpp:16-39). Checked only when the
        # positive control produced no PNG, so it can never mask a real defect.
        sdl_init_re="SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:"

        if [[ ! -s "$at_n" ]] && echo "$at_out" | grep -qE "$sdl_init_re"; then
            skip_row " (SDL could not open a video device on this host; no capture was possible)"
        elif [[ "$at_rc" -eq 0 ]] && [[ -s "$at_n" ]] \
             && [[ "$past_rc" -ne 0 ]] && [[ ! -f "$past_n" ]] \
             && echo "$past_out" | grep -q "NO screenshot was written"; then
            pass_row " (capture due at exit frame $N taken, exit 0; one due at $((N + 1)) not taken, error + exit!=0)"
        else
            fail_row " (at_rc=$at_rc at_png=$([[ -s "$at_n" ]] && echo y || echo n) past_rc=$past_rc past_png=$([[ -f "$past_n" ]] && echo y || echo n))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
