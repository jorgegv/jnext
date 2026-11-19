#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #544 - fast boot with the screen hidden: while the window is open the Qt and
# SDL windows show one held black frame, and the first real picture is the screen
# fast boot ends on. The wiring (the setting, the arming in src/main.cpp, the two
# present sites) is out of the unit rows' reach, so this row runs the real
# binaries under a virtual X server, grabs the window with ImageMagick `import`
# and measures its centre half (inside the emulator widget, clear of the menu bar,
# toolbar and status bar): maximum channel value 0 = black.
#
# The oracle is the boot itself, measured on the headless build: from the
# firmware's soft reset ("[preserve_memory=1 soft-reset]" in the log) every real
# frame is non-black (uniform grey 0.71, then the welcome screen).
#
#   G  Qt, --fast-boot 60 --set startup.fast_boot_hide_screen=true: black one second
#      after the soft reset; the extra log line; a host Shift ends it ("key press") and
#      one second later the window is not black.
#   H  Qt control, the same without the hide: NOT black at the same moment, and no extra
#      log line. It proves the capture can see a real screen, so G's black is not vacuous.
#   I  the SDL-only build, as G.
#   I2 the SDL-only build, as H.
#   J  Qt, the SAVED preference (jnext.conf [startup] fast_boot_seconds=2 and
#      fast_boot_hide_screen=true, no command-line setting): the extra log line, the window
#      ends by time up, and half a second after the soft reset (at normal speed) it is not black.
#
# A capture taken while the log already says "fast boot: ended" is not a hidden-window
# capture: that is a FAIL of the row, never a pass.
#
# No `trap`, every timeout escalates to SIGKILL, xvfb-run -d with -noreset, and
# no pipeline ends in a quiet grep: grep is asked alone, on the log file.
if want fast-boot-hide-func; then
    begin_func fast-boot-hide-func

    fh_dir="$TMP_DIR/fast-boot-hide"
    rm -rf "$fh_dir"; mkdir -p "$fh_dir"
    fh_sdl="$JNEXT_SDL"   # the SDL-only build, as sdl-keypress-func
    fh_conf="$JNEXT_CONFIG_DIR/jnext.conf"
    fh_faults=()
    fh_black_line="fast boot: the screen stays black until it ends"

    # One windowed run: $1 binary, $2 prefix of the files it writes (<prefix>.log, -mid.png,
    # -post.png, .premature), $3 mode (key: capture mid-boot, press Shift, capture again;
    # timeup: capture once, after the soft reset), then jnext's arguments.
    # shellcheck disable=SC2016
    fh_run() {
        local bin="$1" pre="$2" mode="$3"; shift 3
        rm -f "$pre".log "$pre"-mid.png "$pre"-post.png "$pre".premature
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 240s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            bin="$1"; pre="$2"; mode="$3"; shift 3
            log="$pre.log"
            "$bin" "$@" >"$log" 2>&1 &
            pid=$!
            for _ in $(seq 1 600); do
                grep -qF "soft-reset" "$log" 2>/dev/null && break
                sleep 0.2
            done
            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            if [ -n "$wid" ]; then
                if [ "$mode" = key ]; then sleep 1; else sleep 0.5; fi
                if [ "$mode" = key ] && grep -qF "fast boot: ended" "$log" 2>/dev/null; then : >"$pre.premature"; fi
                import -window "$wid" "png:$pre-mid.png" 2>/dev/null || true
                if [ "$mode" = key ]; then
                    X=0; Y=0; WIDTH=0; HEIGHT=0
                    eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                    # No window manager: keys go to the window under the pointer.
                    xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                    sleep 1
                    xdotool key --delay 80 shift 2>/dev/null || true
                    for _ in $(seq 1 150); do
                        grep -qF "fast boot: ended (key press)" "$log" 2>/dev/null && break
                        sleep 0.2
                    done
                    sleep 1
                    import -window "$wid" "png:$pre-post.png" 2>/dev/null || true
                fi
            fi
            kill -TERM "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            exit 0
        ' _ "$bin" "$pre" "$mode" "$@" >/dev/null 2>&1 || true
    }

    fh_has() { grep -qF -- "$2" "$1" 2>/dev/null; }

    # Maximum channel value (0..1) of the centre half of a capture, "" if it cannot be read.
    fh_max() {
        [[ -s "$1" ]] || return 0
        env LC_ALL=C magick "$1" -gravity center -crop 50%x50%+0+0 +repage -format '%[fx:maxima]' info: 2>/dev/null || true
    }
    fh_is_black() { [[ -n "$1" ]] && awk -v m="$1" 'BEGIN { exit !(m + 0 == 0) }'; }
    fh_not_black() { [[ -n "$1" ]] && awk -v m="$1" 'BEGIN { exit !(m + 0 > 0) }'; }

    # fh_phase LABEL BINARY PREFIX HIDE(1|0) jnext-args...: a key-ended phase, G/H/I/I2.
    fh_phase() {
        local label="$1" bin="$2" pre="$fh_dir/$3" hide="$4"; shift 4
        local extra=()
        [[ "$hide" == 1 ]] && extra=(--set startup.fast_boot_hide_screen=true)
        fh_run "$bin" "$pre" key \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 60 \
            ${extra[@]+"${extra[@]}"} "$@"
        if [[ -e "$pre.premature" ]]; then
            fh_faults+=("$label: fast boot had already ended at the mid-boot capture (harness fault)")
            return
        fi
        local mid post
        mid=$(fh_max "$pre-mid.png"); post=$(fh_max "$pre-post.png")
        if [[ "$hide" == 1 ]]; then
            fh_has "$pre.log" "$fh_black_line" || fh_faults+=("$label: no '$fh_black_line' line")
            fh_is_black "$mid" || fh_faults+=("$label: the window was not black mid-boot (max=${mid:-unreadable})")
            fh_has "$pre.log" "fast boot: ended (key press)" || fh_faults+=("$label: a host Shift did not end the window")
            fh_not_black "$post" || fh_faults+=("$label: the window stayed black after the key (max=${post:-unreadable})")
        else
            if fh_has "$pre.log" "$fh_black_line"; then fh_faults+=("$label: the black line without the setting"); fi
            fh_not_black "$mid" || fh_faults+=("$label: the control window was black mid-boot (max=${mid:-unreadable}): the capture sees nothing")
        fi
    }

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    elif ! command -v import &>/dev/null || ! command -v magick &>/dev/null; then
        fail_row " (ImageMagick import/magick not available; cannot measure the window)"
    elif [[ ! -x "$fh_sdl" ]]; then
        fail_row " (SDL-only binary not built: $fh_sdl; run 'make sdl-release')"
    else
        rm -f "$fh_conf"
        fh_phase G  "$JNEXT"   g  1
        fh_phase H  "$JNEXT"   h  0
        fh_phase I  "$fh_sdl"  i  1
        fh_phase I2 "$fh_sdl"  i2 0

        # --- J: Qt, the saved preference, ended by time up ---
        printf '[startup]\nfast_boot_seconds=2\nfast_boot_hide_screen=true\n' > "$fh_conf"
        fh_run "$JNEXT" "$fh_dir/j" timeup \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"}
        rm -f "$fh_conf"
        fh_has "$fh_dir/j.log" "$fh_black_line" || fh_faults+=("J: no '$fh_black_line' line for the saved preference")
        fh_has "$fh_dir/j.log" "fast boot: ended (time up) after 100 frames" \
            || fh_faults+=("J: the window did not end by time up after 100 frames")
        fh_j=$(fh_max "$fh_dir/j-mid.png")
        fh_not_black "$fh_j" || fh_faults+=("J: the window was black after the time-up (max=${fh_j:-unreadable})")

        if [[ ${#fh_faults[@]} -eq 0 ]]; then
            pass_row " (Qt and SDL windows black while fast boot hides the screen, real after a key; the control windows real; the saved preference real after time up)"
        else
            fail_row " ($(IFS='|'; echo "${fh_faults[*]}"))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
