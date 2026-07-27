#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend F1 hard reset (GitHub issue #134).
#
# WHAT IS UNCOVERED HERE. SdlApp keeps its own host-hotkey table inside the
# SDL key callback (src/platform/sdl_app.cpp:114-149) and polls the resulting
# hard-reset request in its own loop (sdl_app.cpp:342-345). Neither is shared
# with the Qt frontend, whose hotkeys live in MainWindow and whose poll lives in
# frame_sequencer; and reset-to-nextzxos-func — the one row that exercises a
# hard reset — drives it through JNEXT_DELAYED_RESET_FRAMES, which is
# implemented ONLY in HeadlessApp (src/platform/headless_app.cpp:434-446).
# There is no way to reach SdlApp's cold boot except by pressing the key, so
# this row presses it.
#
# ONE RUN. 48K BASIC; F2 pressed as a key-delivery witness, then F1 once the
# machine is up, then digits typed into the rebooted machine, then a capture.
#
# THE WITNESS COMES FIRST, AND IT IS NOT F1. F2 cycles the window scale 2->3->4
# (sdl_app.cpp:120-125 -> SdlDisplay::set_scale, sdl_display.cpp:60-69), which
# changes the X window's geometry — something xdotool can read WITHOUT the
# emulator's cooperation. That is what separates "this X server delivers no
# keys" from "SdlApp no longer acts on F1": without it, deleting the F1 handler
# outright would read exactly like a mute X server and the row would SKIP
# through its own regression. Costs two xdotool calls and no extra run.
#
#   geometry unchanged after F2
#       SKIP. Nothing is reaching the window; the run says nothing either way.
#       The pre-fix and post-fix code produce an identical reading here, so it
#       must never be a FAIL (the sdl-keypress-func rule).
#
#   geometry changed, cold boots == 0
#       FAIL. Keys ARE being delivered and acted on, and F1 still did not cold
#       boot the machine.
#
#   cold boots >= 1, capture == boot reference
#       FAIL. The machine cold-booted, yet the digits typed afterwards changed
#       nothing on screen: the rebooted machine is not accepting host keys.
#
#   cold boots >= 1, capture != boot reference
#       PASS. F1 reached SdlApp's hotkey table, the loop acted on the request,
#       and keys typed after the reset still reach the guest.
#
# WHAT THE SECOND HALF DOES *NOT* PIN — measured, not assumed. Deleting the
# key_router_.attach() call from cold_boot()'s rewire_host hook
# (sdl_app.cpp:199-203) leaves this row GREEN. emulator_frontend_cold_boot
# reconstructs the machine with placement-new at the same address
# (platform/emulator_boot.h:73) and Keyboard is a by-value member
# (core/emulator.h:907), so the Router's stale sink pointer still lands on the
# new Keyboard and keys keep flowing; only the latch reset is lost. So read the
# post-reset typing as "the rebooted machine is alive and reachable", NOT as
# cover for the re-attach. The mutations this row DOES catch are: the F1 handler
# removed from the hotkey table, and run()'s take_hard_reset_request() poll
# removed (sdl_app.cpp:342-345) — both turn it red.
#
# WHY THE BOOT REFERENCE IS THE YARDSTICK. A freshly cold-booted 48K sits at
# the untouched copyright screen, which is what img/boot-48k-reference.png IS
# (and it is static across the whole capture window — measured at frames
# 148..700). So "something was typed after the reset" is exactly "the capture
# differs from that reference", with no second control run to pay for.
#
# DIGITS, NOT LETTERS, and the xvfb/x11 environment handling: both inherited
# from sdl-keypress-func.sh, which documents why.
if want sdl-reset-hotkey-func; then
    begin_func sdl-reset-hotkey-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    shot="$TMP_DIR/sdl-reset-hotkey.png"
    log="$TMP_DIR/sdl-reset-hotkey.log"
    geom="$TMP_DIR/sdl-reset-hotkey.geom"
    boot_ref="$IMG_DIR/boot-48k-reference.png"
    rm -f "$shot" "$log" "$geom"
    # Typed at the K cursor of the REBOOTED machine; any one landing is decisive.
    reset_keys=(1 2 3 4)

    # The inner body is single-quoted ON PURPOSE: everything it needs arrives as
    # a positional argument, so nothing from this shell is interpolated into a
    # string that xvfb-run re-parses. shellcheck reads that as SC2016.
    # shellcheck disable=SC2016
    sdl_reset_run() {
        # The Xvfb screen is 2048x1536 so the post-F2 window (scale 3 =
        # 640*3 x 512*3 = 1920x1536) still fits and its geometry is readable.
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 180s \
        xvfb-run -a --server-args="-screen 0 2048x1536x24" bash -c '
            set -uo pipefail
            bin="$1"; out="$2"; log="$3"; geom="$4"; shift 4
            # The capture is at emulated frame 1150 (23 s at 1x — SdlApp has no
            # speed multiplier), which is past the whole script below. Frames,
            # not seconds, is the safe unit here: a loaded box emulates SLOWER
            # than real time, so the capture drifts LATER in wall-clock while
            # the sleeps below do not — i.e. further past the typing, never
            # before it.
            "$bin" --machine 48k --silent \
                --delayed-screenshot "$out" --delayed-screenshot-frames 1150 \
                --delayed-automatic-exit-frames 1210 >"$log" 2>&1 &
            pid=$!

            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --name JNEXT 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # Settle before the F2 witness. Not the critical margin: F1
            # pressed mid-boot still cold-boots the machine, so what matters is
            # the margin given to the SECOND boot, below. (No apostrophes in
            # this body: it is single-quoted, see the note above the function.)
            sleep 5
            if [ -n "$wid" ]; then
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                # Key-delivery witness: window geometry before and after F2.
                xdotool getwindowgeometry --shell "$wid" 2>/dev/null \
                    | sed -n "s/^\(WIDTH\|HEIGHT\)=/before_\1=/p" >"$geom" || true
                xdotool key F2 2>/dev/null || true
                sleep 1
                xdotool getwindowgeometry --shell "$wid" 2>/dev/null \
                    | sed -n "s/^\(WIDTH\|HEIGHT\)=/after_\1=/p" >>"$geom" || true

                xdotool key F1 2>/dev/null || true
                # THE CRITICAL MARGIN, AND A KNOWN REAL-TIME-PACED ONE — leave
                # it generous. The digits below must land at the K cursor of the
                # REBOOTED machine, which is up by emulated frame 150 (3 s at
                # 1x; SdlApp has no speed multiplier, so emulated and wall time
                # track each other). 12 s is 4x that requirement, the same
                # headroom sdl-keypress-func gives its own settle step, because
                # a loaded runner emulates slower than real time and a miss here
                # FAILs rather than skips (see the outcome table above). Do not
                # trim it back to "what works on an idle box".
                sleep 12
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                for k in "$@"; do
                    xdotool keydown "$k" 2>/dev/null || true
                    sleep 0.25
                    xdotool keyup "$k" 2>/dev/null || true
                    sleep 0.3
                done
            fi
            wait $pid
        ' _ "$sdl_bin" "$shot" "$log" "$geom" "${reset_keys[@]}" >/dev/null 2>&1 || true
    }

    if [[ ! -x "$sdl_bin" ]]; then
        # A build artifact the Makefile can guarantee, so this is loud (the
        # rewind-func rule). The driver's preflight normally catches it first.
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    elif ! command -v magick &>/dev/null && ! command -v convert &>/dev/null; then
        skip_row " (no ImageMagick — cannot tell the rebooted screen from a typed-on one)"
    else
        sdl_reset_run

        # SdlApp::cold_boot logs this once per hard reset (sdl_app.cpp:187).
        cold_boots=$(grep -cF "Cold boot (reconstruct + init)" "$log" 2>/dev/null || true)
        typed_diff=999999
        [[ -s "$shot" ]] && typed_diff=$(png_diff "$shot" "$boot_ref")

        # The F2 witness: WIDTH/HEIGHT before vs after. Both empty (xdotool
        # could not read the window) counts as "no witness", i.e. a SKIP.
        geom_before=$(grep -c '^before_' "$geom" 2>/dev/null || true)
        f2_resized=no
        if [[ "$geom_before" -eq 2 ]]; then
            before_wh=$(sed -n 's/^before_//p' "$geom" | tr '\n' ' ')
            after_wh=$(sed -n 's/^after_//p' "$geom" | tr '\n' ' ')
            [[ -n "$after_wh" && "$before_wh" != "$after_wh" ]] && f2_resized=yes
        fi

        if [[ ! -s "$shot" ]]; then
            skip_row " (no screenshot captured; SDL could not open a window?)"
        elif [[ "$f2_resized" != yes ]] && [[ "$cold_boots" -eq 0 ]]; then
            # Neither hotkey had any effect, so no key reached SdlApp at all and
            # the run cannot tell that from a broken handler. Never a FAIL.
            skip_row " (X server delivered no keys at all; F2 did not resize the window either)"
        elif [[ "$cold_boots" -eq 0 ]]; then
            fail_row " (F2 resized the window, so keys ARE delivered, but F1 produced no cold boot)"
        elif [[ "$typed_diff" -le "$TOLERANCE" ]]; then
            fail_row " (F1 cold-booted ${cold_boots}x but keys typed afterwards changed nothing: ${typed_diff} px vs the boot reference)"
        else
            pass_row " (F2 resized the window; F1 cold-booted ${cold_boots}x; the rebooted machine took keys: ${typed_diff} px vs boot ref)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
