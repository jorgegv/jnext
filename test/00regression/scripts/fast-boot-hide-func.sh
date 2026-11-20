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
#      the soft reset, inside the window, is black; the extra log line is there; fast boot ends by
#      time up; and the window grabbed 0.7 s after "fast boot: ended" is not black.
#   H  Qt control, the same without the hide: the frozen grab is NOT black, and no extra log line.
#      It proves the capture can see a real screen, so G's black is not vacuous.
#   I  the SDL-only build, as G, but grabbed without any request against the SDL window (see the
#      modes of fh_run): every grab taken while the window is open is black, there is at least one,
#      and the grab after the end is not black.
#   I2 the SDL-only build, as H: at least one grab inside the window is not black.
#   J  Qt, the SAVED preference (jnext.conf [startup] fast_boot_seconds=2 and
#      fast_boot_hide_screen=true, no command-line setting): the extra log line, the window
#      ends by time up, and half a second after the soft reset (at normal speed) it is not black.
#
# Fast boot runs the 3000 frames of --fast-boot 60 in a few seconds on a quiet host, faster than
# a host key can be sent and a screen read, so the grabs are taken inside the window by freezing
# the Qt emulator (SIGSTOP) or by looping on the SDL one. The host key that ends the window is
# covered by fast-boot-func and the unit rows (FBT-32), not here. Measured on this host: an
# `import -window` of the SDL window stalls until the row's bound, and SIGSTOP of the SDL process
# now and then ends in "XIO: fatal IO error"; a root-window grab cropped to the window does neither
# (GH #544 review 1). Every `import` is bounded on its own.
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
    # -post.png), $3 mode, then jnext's arguments. Every `import` is bounded on its own
    # (timeout, SIGKILL after 2 s): a stalled grab must never hold the row for the run's bound.
    #   qt      Qt: SIGSTOP the emulator 0.3 s after the firmware soft reset (still inside the
    #           window), grab the frozen window, SIGCONT; one more grab 0.7 s after "ended".
    #   sdl     SDL: no signal and NO request against the SDL window (an `import -window` of it
    #           stalls): grab the ROOT window cropped to the SDL window's geometry in a loop while
    #           the log lacks "fast boot: ended", dropping any grab after which the line is there;
    #           one more such grab 0.7 s after "ended".
    #   timeup  Qt: one grab 0.5 s after the soft reset (the saved-preference run).
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
            ended() { grep -qF "fast boot: ended" "$log" 2>/dev/null; }
            grab_win()  { timeout --foreground --kill-after=2s 10s import -window "$wid" "png:$1" 2>/dev/null || rm -f "$1"; }
            grab_root() { timeout --foreground --kill-after=2s 10s import -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" "png:$1" 2>/dev/null || rm -f "$1"; }
            "$bin" "$@" >"$log" 2>&1 &
            pid=$!
            wid=""
            for _ in $(seq 1 300); do
                wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                [ "$mode" != timeup ] && ended && break
                sleep 0.05
            done
            X=0; Y=0; WIDTH=0; HEIGHT=0
            [ -n "$wid" ] && eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
            case "$mode" in
            qt)
                for _ in $(seq 1 6000); do
                    grep -qF "soft-reset" "$log" 2>/dev/null && break
                    sleep 0.02
                done
                # 0.3 s on: the frame the widget holds is then a real one (the screen is cleared at the
                # reset itself), and the window is still open (about 2500 frames to go).
                sleep 0.3
                kill -STOP "$pid" 2>/dev/null
                if [ -n "$wid" ] && ! ended; then grab_win "$pre-mid-1.png"; fi
                kill -CONT "$pid" 2>/dev/null
                ;;
            sdl)
                n=0
                while [ -n "$wid" ] && ! ended && [ $n -lt 80 ]; do
                    n=$((n + 1))
                    grab_root "$pre-mid-$n.png"
                    if ended; then rm -f "$pre-mid-$n.png"; fi
                    sleep 0.1
                done
                ;;
            timeup)
                for _ in $(seq 1 600); do
                    grep -qF "soft-reset" "$log" 2>/dev/null && break
                    sleep 0.2
                done
                sleep 0.5
                ;;
            esac
            if [ "$mode" != timeup ]; then
                for _ in $(seq 1 600); do ended && break; sleep 0.2; done
                sleep 0.7
            fi
            if [ -n "$wid" ]; then
                if [ "$mode" = sdl ]; then grab_root "$pre-post.png"; else grab_win "$pre-post.png"; fi
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

    # fh_phase LABEL BINARY PREFIX HIDE(1|0) MODE(qt|sdl) jnext-args...: G, H, I, I2.
    fh_phase() {
        local label="$1" bin="$2" pre="$fh_dir/$3" hide="$4" mode="$5"; shift 5
        local extra=() f m n=0 lit=0 seen=0 post
        [[ "$hide" == 1 ]] && extra=(--set startup.fast_boot_hide_screen=true)
        fh_run "$bin" "$pre" "$mode" \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 60 \
            ${extra[@]+"${extra[@]}"} "$@"
        for f in "$pre"-mid-*.png; do
            [[ -e "$f" ]] || continue
            n=$((n + 1)); m=$(fh_max "$f")
            if ! fh_is_black "$m"; then lit=$((lit + 1)); fi
            if fh_not_black "$m"; then seen=$((seen + 1)); fi
        done
        post=$(fh_max "$pre-post.png")
        fh_has "$pre.log" "fast boot: ended (time up) after 3000 frames" || fh_faults+=("$label: fast boot did not end by time up")
        if [[ "$hide" == 1 ]]; then
            fh_has "$pre.log" "$fh_black_line" || fh_faults+=("$label: no '$fh_black_line' line")
            [[ $n -gt 0 ]] || fh_faults+=("$label: no capture inside the fast-boot window (harness fault)")
            [[ $lit -eq 0 ]] || fh_faults+=("$label: $lit of $n captures inside the window were not black")
            fh_not_black "$post" || fh_faults+=("$label: the window stayed black after fast boot ended (max=${post:-unreadable})")
        else
            if fh_has "$pre.log" "$fh_black_line"; then fh_faults+=("$label: the black line without the setting"); fi
            [[ $seen -gt 0 ]] || fh_faults+=("$label: the control never showed a real screen inside the window ($n captures): the capture sees nothing")
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
        fh_phase G  "$JNEXT"   g  1 qt
        fh_phase H  "$JNEXT"   h  0 qt
        fh_phase I  "$fh_sdl"  i  1 sdl
        fh_phase I2 "$fh_sdl"  i2 0 sdl

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
            pass_row " (Qt and SDL windows black while fast boot hides the screen and real after it ends, both controls real inside it, the saved preference real after time up)"
        else
            fail_row " ($(IFS='|'; echo "${fh_faults[*]}"))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
