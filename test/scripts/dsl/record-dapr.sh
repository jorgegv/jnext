#!/usr/bin/env bash
# Re-record the two DAPR interactive tests as replay scripts (GH #20 / GH #26
# WP6): the Qt GUI under a virtual X server, its keyboard driven by xdotool,
# recorded with --record-script. Alt+8 takes each capture.
#
#   test/scripts/dsl/record-dapr.sh keyb       # -> dapr-keyb.jds + dapr-keyb-000N.png
#   test/scripts/dsl/record-dapr.sh joystick   # -> dapr-joystick.jds + ...-000N.png
#
# Run from the repository root, after `make` (it uses build/jnext). The
# timing is wall-clock, so every recording differs in its frame numbers; any
# one of them replays exactly (script-replay-keyb-func,
# script-replay-joystick-func). Re-record rather than edit.
set -euo pipefail
mode="${1:?usage: $0 keyb|joystick}"
case "$mode" in
    keyb)     nex=test/00regression/nex/test06keyb.nex;     extra=() ;;
    joystick) nex=test/00regression/nex/test07joystick.nex; extra=(--joy1-source keys) ;;
    *) echo "usage: $0 keyb|joystick" >&2; exit 2 ;;
esac
out="test/scripts/dsl/dapr-$mode.jds"
rm -f "test/scripts/dsl/dapr-$mode.jds" test/scripts/dsl/dapr-"$mode"-0*.png
command -v xvfb-run >/dev/null && command -v xdotool >/dev/null ||
    { echo "needs xvfb-run and xdotool" >&2; exit 1; }

# The Wayland display would win over Xvfb; the dummy audio driver needs no device.
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy LANG=C \
timeout --kill-after=5s 180s \
xvfb-run -d --server-args="-screen 0 1600x1200x24 -noreset" bash -c '
    set -uo pipefail
    out="$1"; nex="$2"; mode="$3"; shift 3
    build/jnext --machine next --load "$nex" --record-script "$out" \
        --delayed-automatic-exit-frames 450 "$@" >/dev/null 2>&1 &
    pid=$!
    wid=""
    for _ in $(seq 1 100); do
        wid=$(xdotool search --onlyvisible --name JNEXT 2>/dev/null | head -1) || true
        [ -n "$wid" ] && break
        sleep 0.2
    done
    sleep 3
    # No window manager under Xvfb: a click is what gives the window the keyboard.
    xdotool mousemove --window "$wid" 300 300 click 1
    sleep 0.5
    cap() { xdotool key alt+8; sleep 0.3; }
    if [ "$mode" = keyb ]; then
        xdotool keydown q; sleep 0.2
        cap                                         # Q held
        xdotool keyup q; sleep 0.3
        # W held across CAPS SHIFT: overlapping level holds, not pulses.
        xdotool keydown w; sleep 0.15; xdotool keydown Shift_L; sleep 0.2
        xdotool keyup w; sleep 0.15; xdotool keyup Shift_L; sleep 0.1
        cap
        # CAPS + 1 (EDIT). Alt+8 with Shift held is not the script key, so
        # the capture comes after.
        xdotool keydown Shift_L; xdotool keydown 1; sleep 0.3
        xdotool keyup 1; xdotool keyup Shift_L; sleep 0.1
        cap
        xdotool keydown e; xdotool keydown r; sleep 0.2
        cap                                         # E and R held
        xdotool keyup e; xdotool keyup r; sleep 0.3
        cap
    else
        # --joy1-source keys: the arrows and Space are Kempston joystick 1.
        xdotool keydown Right; sleep 0.3; xdotool keydown Up; sleep 0.2
        cap                                         # right + up
        xdotool keyup Right; sleep 0.2; xdotool keyup Up; sleep 0.3
        xdotool keydown space; sleep 0.2
        cap                                         # fire
        xdotool keyup space; sleep 0.4
        xdotool keydown Left; xdotool keydown Down; sleep 0.25; xdotool keyup Left; sleep 0.1
        cap                                         # down
        xdotool keyup Down; sleep 0.5
        cap
    fi
    wait $pid
' _ "$out" "$nex" "$mode" "${extra[@]}"
ls -l "$out" test/scripts/dsl/dapr-"$mode"-0*.png
