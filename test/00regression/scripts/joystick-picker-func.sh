#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Which physical controller drives each joystick port (GitHub issue #311), on
# the REAL binaries.
#
# WHY A FUNCTIONAL ROW. The unit suites drive GamepadHost, the Input menu and
# AppConfig, but the feature is the chain between them: main.cpp merges the
# command line over jnext.conf, QtApp / SdlApp hand the result to the Emulator,
# the Emulator pushes it to the host, the host binds a device. QtApp cannot be
# constructed in a unit suite, so that chain was twice a release blocker in this
# project (wiring no unit row touched). This row starts the shipped frontends.
#
# NO HARDWARE. JNEXT_TEST_VIRTUAL_JOYSTICKS makes both frontends attach SDL
# virtual joysticks at startup and admit ONLY virtual devices (see
# GamepadHost::attach_test_devices_from_env), so a real pad on the machine
# running the suite changes nothing. Two identical pads and one raw stick: the
# stick's id is read from the log, never hard-coded.
#
# THE LOG LINES ARE THE CONTRACT. "Joystick N connected: '<name>' [<id>]" is
# also how a user learns the id to give --joyN-device, so this row greps exactly
# what a user would read.
if want joystick-picker-func; then
    begin_func joystick-picker-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    jp_spec="jnext pad A:1234:0001;jnext pad A:1234:0001;jnext stick C:5678:0002:raw"
    jp_g_none="ffffffffffffffffffffffffffffffff"

    # shellcheck disable=SC2016
    jp_run() {   # $1 = binary, $2 = log, $3 = config dir, rest = jnext arguments
        local bin="$1" log="$2" cfg="$3"; shift 3
        rm -f "$log"
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
            JNEXT_CONFIG_DIR="$cfg" JNEXT_TEST_VIRTUAL_JOYSTICKS="$jp_spec" \
        timeout --foreground --kill-after=5s 90s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24" \
            "$bin" --machine 48k --silent --delayed-automatic-exit-frames 100 "$@" \
            >"$log" 2>&1 || true
    }
    jp_conf() {   # $1 = dir, rest = lines of the [input] group
        local dir="$1"; shift
        mkdir -p "$dir"
        { echo "[input]"; printf '%s\n' "$@"; } >"$dir/jnext.conf"
    }

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    else
        failures=""
        note() { failures+=" [$1]"; }

        # F1 — Qt, fresh config: today's behaviour. Pads on Joy 1 and Joy 2 in
        # arrival order, the stick unbound; every line carries the id.
        log1="$TMP_DIR/jp_f1.log"
        jp_run "$JNEXT" "$log1" "$TMP_DIR/jp_cfg1"
        g=$(sed -n "s/.*Joystick 1 connected: 'jnext pad A' \[\([0-9a-f]\{32\}\)\].*/\1/p" "$log1" | head -1)
        h=$(sed -n "s/.*'jnext stick C' \[\([0-9a-f]\{32\}\)\] detected but not connected.*/\1/p" "$log1" | head -1)
        if [[ -z "$g" || -z "$h" ]]; then
            note "F1: no 'Joystick 1 connected' / 'detected but not connected' line with an id"
        elif ! grep -qF "Joystick 2 connected: 'jnext pad A' [${g}#2]" "$log1"; then
            note "F1: second identical pad is not '<guid>#2' on Joy 2"
        fi

        if [[ -n "$g" && -n "$h" ]]; then
            # F2 — Qt, saved assignment: stick on Joy 1, the second pad on Joy 2.
            log2="$TMP_DIR/jp_f2.log"
            jp_conf "$TMP_DIR/jp_cfg2" "joy1_device=$h" "joy2_device=${g}#2"
            jp_run "$JNEXT" "$log2" "$TMP_DIR/jp_cfg2"
            grep -qF "Joystick 1 connected: 'jnext stick C' [$h]" "$log2" || note "F2: Joy 1 is not the saved stick"
            grep -qF "Joystick 2 connected: 'jnext pad A' [${g}#2]" "$log2" || note "F2: Joy 2 is not the saved second pad"

            # F3 — Qt, saved controller that is not present: fall back to the
            # first free one, saying so.
            log3="$TMP_DIR/jp_f3.log"
            jp_conf "$TMP_DIR/jp_cfg3" "joy1_device=$jp_g_none" "joy1_device_name=Gone Pad"
            jp_run "$JNEXT" "$log3" "$TMP_DIR/jp_cfg3"
            grep -qF "Joy 1: assigned controller 'Gone Pad' [$jp_g_none] is not connected; using the first free controller instead" "$log3" \
                || note "F3: no fallback line"
            grep -qF "Joystick 1 connected: 'jnext pad A' [$g]" "$log3" || note "F3: Joy 1 did not fall back to the first pad"

            # F4 — Qt, the command line beats the saved assignment.
            log4="$TMP_DIR/jp_f4.log"
            jp_run "$JNEXT" "$log4" "$TMP_DIR/jp_cfg2" --joy1-device "$g"
            grep -qF "Joystick 1 connected: 'jnext pad A' [$g]" "$log4" || note "F4: --joy1-device did not win over the saved stick"

            # F5 — the SDL-only frontend: no config file, the command line is all.
            log5="$TMP_DIR/jp_f5.log"
            jp_run "$sdl_bin" "$log5" "$TMP_DIR/jp_cfg5" --joy1-device "$h"
            grep -qF "Joystick 1 connected: 'jnext stick C' [$h]" "$log5" || note "F5: SDL-only --joy1-device not applied"

            # F6 — a malformed id is refused; headless says the flag is inert.
            rc=0
            "$JNEXT" --joy1-device zz >"$TMP_DIR/jp_f6a.log" 2>&1 || rc=$?
            [[ "$rc" -eq 1 ]] || note "F6: --joy1-device zz exited $rc, not 1"
            grep -qF "Invalid --joy1-device value 'zz'" "$TMP_DIR/jp_f6a.log" || note "F6: no 'Invalid --joy1-device' message"
            timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k --joy1-device "$h" \
                --delayed-automatic-exit-frames 5 >"$TMP_DIR/jp_f6b.log" 2>&1 || true
            grep -qF "have no effect with --headless" "$TMP_DIR/jp_f6b.log" || note "F6: no headless warning for --joy1-device"
        fi

        if [[ -z "$failures" ]]; then
            pass_row " (ids logged, saved/CLI/fallback assignments applied, Qt and SDL-only)"
        else
            fail_row " (GH #311:$failures)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
