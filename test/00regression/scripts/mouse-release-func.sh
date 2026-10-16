#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Configurable mouse-release gesture, end to end (GitHub issue #365).
#
# WHY THIS ROW EXISTS. The decision (pointer_capture::ReleaseFilter), the SDL
# route (sdl_pointer_release.h) and the Qt window are unit-tested (PCAP-36..44,
# MRS-*, MRQ-*, AC-84..86). What none of them reaches is the glue: main.cpp's
# --mouse-release parse, the headless warning, the Qt merge with the saved
# preference, the SDL setter, and the log line / title a user actually reads.
# Reverting any of those leaves every unit suite green, so this row runs the
# real binaries under a virtual X server and injects real host events.
#
# THE ARMS. Each is a real run; the verdict names the arm that failed.
#   1 SDL --mouse-release esc          click captures, Esc releases
#   2 SDL --mouse-release middle-click click captures, a middle click releases
#   3 SDL default                      Ctrl+Alt still releases (default unchanged)
#   4 Qt  --mouse-release esc          title names Esc, Esc restores the title
#   5 Qt  saved [input] mouse_release=middle-click, no flag: title names the
#                                      middle click, a middle click restores it
#   6 --headless --mouse-release bogus exits 1 naming the valid values
#   7 --headless --mouse-release esc   exits 0 with the "no effect" warning
#
# THE BINARIES. SDL arms use build/sdl-release ($JNEXT_SDL; SdlApp is only
# instantiated in an ENABLE_QT_UI=OFF build), Qt arms and the headless arms use
# $JNEXT, exactly as sdl-keypress-func / qt-keypress-func do.
#
# THE XVFB IS STARTED WITH -noreset (GH #318, HS-89) and WAYLAND_DISPLAY is
# unset with the x11 backends forced, as in those rows. No focus call is made
# (Xvfb has no window manager): focus stays at PointerRoot, so the pointer is
# moved into the window and decides where keys and clicks land. A run whose
# window never appears, or whose jnext exits non-zero, is a named FAIL.
if want mouse-release-func; then
    begin_func mouse-release-func

    declare -A mr_rc=()

    # One interactive run. $1 = arm label, $2 = sdl|qt, $3 = binary, then the
    # jnext arguments up to a literal `--`, then the steps:
    #   click:N  key:KEYSYM  ctrlalt  name:LABEL (record the window title)
    # jnext's output goes to $TMP_DIR/mr_<arm>.log, titles to mr_<arm>.names,
    # its exit status to mr_rc[<arm>].
    # The inner body is single-quoted ON PURPOSE (everything arrives as a
    # positional argument), which shellcheck reads as SC2016.
    # shellcheck disable=SC2016
    mr_run() {
        local arm="$1" kind="$2" bin="$3"; shift 3
        local log="$TMP_DIR/mr_$arm.log" names="$TMP_DIR/mr_$arm.names"
        rm -f "$log" "$names"
        : >"$names"
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 120s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            kind="$1"; bin="$2"; log="$3"; names="$4"; shift 4
            args=()
            while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do args+=("$1"); shift; done
            shift
            "$bin" --machine 48k --silent "${args[@]}" \
                --delayed-automatic-exit-frames 450 >"$log" 2>&1 &
            pid=$!

            wid=""
            for _ in $(seq 1 100); do
                if [ "$kind" = qt ]; then
                    wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                else
                    wid=$(xdotool search --name JNEXT 2>/dev/null | head -1) || true
                fi
                [ -n "$wid" ] && break
                sleep 0.2
            done
            sleep 3
            if [ -n "$wid" ]; then
                X=0; Y=0; WIDTH=0; HEIGHT=0
                eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                sleep 0.3
                for s in "$@"; do
                    case "$s" in
                        click:*) xdotool click "${s#click:}" 2>/dev/null || true ;;
                        key:*)   xdotool key "${s#key:}" 2>/dev/null || true ;;
                        ctrlalt) xdotool keydown ctrl 2>/dev/null || true
                                 xdotool keydown alt 2>/dev/null || true
                                 sleep 0.2
                                 xdotool keyup alt 2>/dev/null || true
                                 xdotool keyup ctrl 2>/dev/null || true ;;
                        name:*)  echo "${s#name:}=$(xdotool getwindowname "$wid" 2>/dev/null)" >>"$names" ;;
                    esac
                    sleep 0.6
                done
            fi
            wait $pid
        ' _ "$kind" "$bin" "$log" "$names" "$@" >/dev/null 2>&1 \
            && mr_rc[$arm]=0 || mr_rc[$arm]=$?
    }

    # Did arm $1's log (or titles file with suffix) hold fixed string $2?
    mr_has() { grep -qF -- "$2" "$TMP_DIR/mr_$1.$3" 2>/dev/null; }

    sdl_bin="$JNEXT_SDL"
    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host events)"
    else
        mr_run sdl_esc  sdl "$sdl_bin" --mouse-release esc -- click:1 key:Escape
        mr_run sdl_mid  sdl "$sdl_bin" --mouse-release middle-click -- click:1 click:2
        mr_run sdl_def  sdl "$sdl_bin" -- click:1 ctrlalt
        mr_run qt_esc   qt  "$JNEXT"   --mouse-release esc -- name:before click:1 name:captured key:Escape name:released
        # Arm 5: the saved preference alone picks the method (no CLI flag).
        printf '[input]\nmouse_release=middle-click\n' >"$RUN_DIR/jnext.conf"
        mr_run qt_conf  qt  "$JNEXT"   -- name:before click:1 name:captured click:2 name:released
        rm -f "$RUN_DIR/jnext.conf"

        bad=()
        for a in sdl_esc sdl_mid sdl_def qt_esc qt_conf; do
            if [[ "${mr_rc[$a]}" -ne 0 ]]; then
                err_line=$(grep -m1 -E '\[(error|critical)\]' "$TMP_DIR/mr_$a.log" 2>/dev/null) || err_line="no error logged"
                bad+=("$a: jnext exit ${mr_rc[$a]}; $err_line")
            fi
        done

        # Arms 1-3 read jnext's own log lines.
        mr_has sdl_esc  "Mouse captured (Esc to release)"          log || bad+=("arm 1: no 'Mouse captured (Esc to release)'")
        mr_has sdl_esc  "Mouse released"                           log || bad+=("arm 1: Esc did not release")
        mr_has sdl_mid  "Mouse captured (middle click to release)" log || bad+=("arm 2: no 'Mouse captured (middle click to release)'")
        mr_has sdl_mid  "Mouse released"                           log || bad+=("arm 2: the middle click did not release")
        mr_has sdl_def  "Mouse captured (Ctrl+Alt to release)"     log || bad+=("arm 3: no 'Mouse captured (Ctrl+Alt to release)'")
        mr_has sdl_def  "Mouse released"                           log || bad+=("arm 3: Ctrl+Alt did not release")

        # Arms 4-5 read the window title. captured must name the gesture;
        # released must be back to the title it started with.
        mr_title() { sed -n "s/^$2=//p" "$TMP_DIR/mr_$1.names" | head -1; }
        for pair in "qt_esc:Esc to release mouse:4" "qt_conf:middle click to release mouse:5"; do
            a="${pair%%:*}"; rest="${pair#*:}"; want_txt="${rest%:*}"; n="${rest##*:}"
            before=$(mr_title "$a" before); cap=$(mr_title "$a" captured); rel=$(mr_title "$a" released)
            [[ -n "$before" && "$before" != *"to release mouse"* ]] || bad+=("arm $n: no clean base title ('$before')")
            [[ "$cap" == *"$want_txt"* ]] || bad+=("arm $n: captured title '$cap' lacks '$want_txt'")
            [[ "$rel" == "$before" ]] || bad+=("arm $n: title not restored ('$rel')")
        done

        # Arms 6-7: the CLI itself, no display needed.
        rc6=0; err6=$(env LANG=C "$JNEXT" --headless --mouse-release bogus 2>&1 >/dev/null) || rc6=$?
        [[ "$rc6" -eq 1 && "$err6" == *"ctrl-alt, middle-click, esc"* ]] \
            || bad+=("arm 6: bogus value gave exit $rc6 / '$err6'")
        rc7=0; err7=$(env LANG=C timeout --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            --mouse-release esc --delayed-automatic-exit-frames 1 2>&1 >/dev/null) || rc7=$?
        [[ "$rc7" -eq 0 && "$err7" == *"--mouse-release has no effect with --headless"* ]] \
            || bad+=("arm 7: headless gave exit $rc7 / no warning")

        if [[ ${#bad[@]} -gt 0 ]]; then
            fail_row " ($(IFS='|'; echo "${bad[*]}"))"
        else
            pass_row " (SDL esc/middle-click/ctrl-alt and Qt esc/saved middle-click release; CLI errors and warning)"
        fi
    fi
fi
