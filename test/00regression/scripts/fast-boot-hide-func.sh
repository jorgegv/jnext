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
#   G  Qt, --fast-boot 60 --set startup.fast_boot_hide_screen=true: the window grabbed frozen at
#      the soft reset, inside the window, is black; the extra log line is there; and the window
#      grabbed 0.7 s after "fast boot: ended" is not black.
#   H  Qt control, the same without the hide: the frozen grab is NOT black, and no extra log line. It proves the capture can see a real screen,
#      so G's black is not vacuous.
#   I  the SDL-only build, as G but with no grab inside the window (an `import` of that window hangs
#      the X server, see the call below); the black frame is pinned by sdl_app_tick_test.
#   I2 the SDL-only build, as H but, like I, with no grab inside the window.
#   J  Qt, the SAVED preference (jnext.conf [startup] fast_boot_seconds=2 and
#      fast_boot_hide_screen=true, no command-line setting): the extra log line, the window
#      ends by time up, and half a second after the soft reset (at normal speed) it is not black.
#
# Fast boot runs the 3000 frames of --fast-boot 60 in a few seconds on a quiet host, faster than
# a host key can be sent and a screen read. So G, H, I and I2 SIGSTOP the emulator at the firmware's
# soft reset (a frame the real screen is not black on), grab the frozen window, and let it go on; a
# grab only counts if the log still lacks "fast boot: ended" after the stop. The host key that ends
# the window is covered by fast-boot-func and the unit rows (FBT-32), not here.
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

    # One windowed run: $1 binary, $2 prefix of the files it writes (<prefix>.log, -mid-N.png,
    # -post.png), $3 mode (window: grab the window in a loop while fast boot is open, then once
    # 0.7 s after it ended; timeup: one grab, 0.5 s after the soft reset), then jnext's arguments.
    # shellcheck disable=SC2016
    fh_run() {
        local bin="$1" pre="$2" mode="$3"; shift 3
        rm -f "$pre".log "$pre"-mid-*.png "$pre"-post.png
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 240s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            bin="$1"; pre="$2"; mode="$3"; shift 3
            log="$pre.log"
            "$bin" "$@" >"$log" 2>&1 &
            pid=$!
            wid=""
            for _ in $(seq 1 300); do
                wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                [ "$mode" = window ] && grep -qF "fast boot: ended" "$log" 2>/dev/null && break
                sleep 0.05
            done
            if [ "$mode" = window ] || [ "$mode" = nomid ]; then
                if [ "$mode" = window ]; then
                    # Freeze the emulator at the firmware soft reset, still inside the window, and grab
                    # the frozen window: a stopped process cannot finish fast boot under the grab.
                    for _ in $(seq 1 6000); do
                        grep -qF "soft-reset" "$log" 2>/dev/null && break
                        sleep 0.02
                    done
                    kill -STOP "$pid" 2>/dev/null
                    if [ -n "$wid" ] && ! grep -qF "fast boot: ended" "$log" 2>/dev/null; then
                        import -window "$wid" "png:$pre-mid-1.png" 2>/dev/null || true
                    fi
                    kill -CONT "$pid" 2>/dev/null
                fi
                for _ in $(seq 1 600); do
                    grep -qF "fast boot: ended" "$log" 2>/dev/null && break
                    sleep 0.2
                done
                sleep 0.7
            else
                for _ in $(seq 1 600); do
                    grep -qF "soft-reset" "$log" 2>/dev/null && break
                    sleep 0.2
                done
                sleep 0.5
            fi
            [ -n "$wid" ] && import -window "$wid" "png:$pre-post.png" 2>/dev/null
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

    # fh_phase LABEL BINARY PREFIX HIDE(1|0) GRAB(1|0) jnext-args...: G, H, I, I2. GRAB=0 skips the
    # frozen grab (see I below) and so the black check; the rest of the phase still runs.
    fh_phase() {
        local label="$1" bin="$2" pre="$fh_dir/$3" hide="$4" grab="$5"; shift 5
        local extra=() f m n=0 black=0 lit=0 post
        [[ "$hide" == 1 ]] && extra=(--set startup.fast_boot_hide_screen=true)
        local fmode=window; [[ "$grab" == 1 ]] || fmode=nomid
        fh_run "$bin" "$pre" "$fmode" \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 60 \
            ${extra[@]+"${extra[@]}"} "$@"
        for f in "$pre"-mid-*.png; do
            [[ -e "$f" ]] || continue
            n=$((n + 1)); m=$(fh_max "$f")
            if fh_is_black "$m"; then black=$((black + 1)); elif fh_not_black "$m"; then lit=$((lit + 1)); fi
        done
        post=$(fh_max "$pre-post.png")
        if [[ "$hide" == 1 ]]; then
            fh_has "$pre.log" "$fh_black_line" || fh_faults+=("$label: no '$fh_black_line' line")
            if [[ "$grab" == 1 ]]; then
                [[ $n -gt 0 ]] || fh_faults+=("$label: no capture inside the fast-boot window (harness fault)")
                [[ $lit -eq 0 ]] || fh_faults+=("$label: $lit of $n captures inside the window were not black")
            fi
            fh_not_black "$post" || fh_faults+=("$label: the window stayed black after fast boot ended (max=${post:-unreadable})")
        else
            if fh_has "$pre.log" "$fh_black_line"; then fh_faults+=("$label: the black line without the setting"); fi
            if [[ "$grab" == 1 ]]; then
                [[ $lit -gt 0 ]] || fh_faults+=("$label: the control never showed a real screen inside the window ($n captures): the capture sees nothing")
            else
                fh_not_black "$post" || fh_faults+=("$label: the control window is black after fast boot ended (max=${post:-unreadable})")
            fi
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
        fh_phase G  "$JNEXT"   g  1 1
        fh_phase H  "$JNEXT"   h  0 1
        # I, I2: no grab and no SIGSTOP inside the window. An `import` of the SDL window while its only
        # presented frame is the held black one stalled the app and the X server until the row's
        # timeout, and a SIGSTOP/SIGCONT of the SDL process ends in "XIO: fatal IO error 4/11" now and
        # then (both measured; Qt is fine with both). The SDL black frame is pinned by
        # sdl_app_tick_test SDLT-15..17; here I checks the wiring (--set reaches the arming in the
        # SDL-only binary: the log line) and the real screen after the window ends, I2 the same
        # without the setting.
        fh_phase I  "$fh_sdl"  i  1 0
        fh_phase I2 "$fh_sdl"  i2 0 0

        # --- J: Qt, the saved preference, ended by time up ---
        printf '[startup]\nfast_boot_seconds=2\nfast_boot_hide_screen=true\n' > "$fh_conf"
        fh_run "$JNEXT" "$fh_dir/j" timeup \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"}
        rm -f "$fh_conf"
        fh_has "$fh_dir/j.log" "$fh_black_line" || fh_faults+=("J: no '$fh_black_line' line for the saved preference")
        fh_has "$fh_dir/j.log" "fast boot: ended (time up) after 100 frames" \
            || fh_faults+=("J: the window did not end by time up after 100 frames")
        fh_j=$(fh_max "$fh_dir/j-post.png")
        fh_not_black "$fh_j" || fh_faults+=("J: the window was black after the time-up (max=${fh_j:-unreadable})")

        if [[ ${#fh_faults[@]} -eq 0 ]]; then
            pass_row " (Qt window black while fast boot hides the screen, SDL --set wired, both real after it ends; the control windows real inside it; the saved preference real after time up)"
        else
            fail_row " ($(IFS='|'; echo "${fh_faults[*]}"))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
