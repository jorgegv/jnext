#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #499 - fast boot: the first emulated seconds of a plain Next start run at
# maximum speed. The wiring is in src/main.cpp (the arming) and the frontends
# (Qt's burst, SDL's tick); the unit rows reach neither, so this row runs the
# real binaries under a virtual X server and reads their log.
#
#   A  Qt, the SAVED preference: jnext.conf [startup] fast_boot_seconds=2, a plain
#      Next start. The window is armed (2 s = 100 frames) and ends by time up.
#   B  Qt, --fast-boot 60, and a host key (xdotool, Shift) ends it: "key press".
#   C  SDL-only build, --set startup.fast_boot_seconds=2: as A.
#   D  Qt with --fast-boot 10 --delayed-automatic-exit-frames 50: command-line work on
#      the emulated timeline disarms it, with a warning, and the run exits 0.
#   E  --headless --fast-boot 10 --delayed-automatic-exit-frames 1: the generic
#      "no effect with --headless" warning, exit 0.
#   F  one short windowed run per exclusion term of src/main.cpp (--inject, --rzx-play,
#      --delayed-screenshot, --delayed-automatic-exit in seconds, --script, --delayed-pok-frames
#      (SDL only: the Qt window refuses it), --record, --wav-record, --rzx-record, --dac-trace,
#      --record-script) with --fast-boot 10: the warning `fast boot: off (<reason>)`, so a term
#      dropped from main.cpp's chain is a FAIL naming the flag.
#
# Each windowed run is ended with SIGTERM once its line is seen (the quit-signal
# contract): an exit flag of its own would disarm the fast boot. The unit
# rows (fast_boot_test) pin the rules; this row pins that they are connected.
#
# No `trap`, every timeout escalates to SIGKILL, xvfb-run -d with -noreset, and
# no pipeline ends in a quiet grep: grep is asked alone, on the log file.
if want fast-boot-func; then
    begin_func fast-boot-func

    fb_dir="$TMP_DIR/fast-boot"
    rm -rf "$fb_dir"; mkdir -p "$fb_dir"
    fb_sdl="$JNEXT_SDL"   # the SDL-only build, as sdl-keypress-func
    fb_conf="$JNEXT_CONFIG_DIR/jnext.conf"
    fb_faults=()

    # One windowed run: $1 binary, $2 log, $3 key to send ("" = none), $4 pattern to wait for,
    # then jnext's arguments. Prints nothing; the log is the evidence.
    # shellcheck disable=SC2016
    fb_run() {
        local bin="$1" log="$2" key="$3" pat="$4"; shift 4
        rm -f "$log"
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 180s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            bin="$1"; log="$2"; key="$3"; pat="$4"; shift 4
            "$bin" "$@" >"$log" 2>&1 &
            pid=$!
            for _ in $(seq 1 600); do
                grep -q "fast boot: " "$log" 2>/dev/null && break
                sleep 0.2
            done
            if [ -n "$key" ]; then
                wid=""
                for _ in $(seq 1 100); do
                    wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                    [ -n "$wid" ] && break
                    sleep 0.2
                done
                if [ -n "$wid" ]; then
                    X=0; Y=0; WIDTH=0; HEIGHT=0
                    eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                    # No window manager: keys go to the window under the pointer.
                    xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                    sleep 1
                    xdotool key --delay 80 "$key" 2>/dev/null || true
                fi
            fi
            for _ in $(seq 1 900); do
                grep -q "$pat" "$log" 2>/dev/null && break
                sleep 0.2
            done
            kill -TERM "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            exit 0
        ' _ "$bin" "$log" "$key" "$pat" "$@" >/dev/null 2>&1 || true
    }

    fb_has() { grep -qF -- "$2" "$1" 2>/dev/null; }

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    elif [[ ! -x "$fb_sdl" ]]; then
        fail_row " (SDL-only binary not built: $fb_sdl; run 'make sdl-release')"
    else
        # --- A: Qt, the saved preference ---
        printf '[startup]\nfast_boot_seconds=2\n' > "$fb_conf"
        fb_run "$JNEXT" "$fb_dir/a.log" "" "fast boot: ended" \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"}
        fb_has "$fb_dir/a.log" "fast boot: the first 2 s (100 frames) run at maximum speed" \
            || fb_faults+=("A: no armed line for the saved 2 s")
        fb_has "$fb_dir/a.log" "fast boot: ended (time up) after 100 frames" \
            || fb_faults+=("A: the window did not end by time up after 100 frames")
        rm -f "$fb_conf"

        # --- B: Qt, --fast-boot 60, a host key ends it ---
        fb_run "$JNEXT" "$fb_dir/b.log" "shift" "fast boot: ended" \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 60
        fb_has "$fb_dir/b.log" "fast boot: the first 60 s (3000 frames) run at maximum speed" \
            || fb_faults+=("B: no armed line for --fast-boot 60")
        fb_has "$fb_dir/b.log" "fast boot: ended (key press)" \
            || fb_faults+=("B: a host Shift did not end the window (key press)")

        # --- C: the SDL-only build, --set ---
        fb_run "$fb_sdl" "$fb_dir/c.log" "" "fast boot: ended" \
            --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} \
            --set startup.fast_boot_seconds=2
        fb_has "$fb_dir/c.log" "fast boot: the first 2 s (100 frames) run at maximum speed" \
            || fb_faults+=("C: SDL: no armed line for --set startup.fast_boot_seconds=2")
        fb_has "$fb_dir/c.log" "fast boot: ended (time up) after 100 frames" \
            || fb_faults+=("C: SDL: the window did not end by time up after 100 frames")

        # --- D: command-line work on the emulated timeline disarms it ---
        if env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
               timeout --foreground --kill-after=5s 120s "$JNEXT" --machine next --sdcard-readonly \
               ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 10 --delayed-automatic-exit-frames 50 \
               >"$fb_dir/d.log" 2>&1; then
            fb_has "$fb_dir/d.log" "fast boot: off (the command line schedules work on the emulated timeline)" \
                || fb_faults+=("D: no 'off (the command line schedules ...)' line")
            if fb_has "$fb_dir/d.log" "fast boot: the first"; then fb_faults+=("D: armed despite --delayed-automatic-exit-frames"); fi
        else
            fb_faults+=("D: jnext exited non-zero")
        fi

        # --- E: headless ---
        if timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next --sdcard-readonly \
               ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 10 --delayed-automatic-exit-frames 1 \
               >"$fb_dir/e.log" 2>&1; then
            fb_has "$fb_dir/e.log" "has no effect with --headless" \
                || fb_faults+=("E: no 'no effect with --headless' warning")
            if fb_has "$fb_dir/e.log" "fast boot: the first"; then fb_faults+=("E: armed under --headless"); fi
        else
            fb_faults+=("E: jnext exited non-zero")
        fi

        # --- F: every exclusion term, one windowed run each ---
        printf '\xc9' > "$fb_dir/inject.bin"
        printf 'NScreen\nZ  8 16384 171 0\nY\n' > "$fb_dir/t.pok"
        fb_prog="a program is loaded at start"
        fb_sched="the command line schedules work on the emulated timeline"
        fb_rec="a recording is running"
        # fb_case NAME BIN REASON jnext-args...
        fb_case() {
            local name="$1" bin="$2" reason="$3"; shift 3
            local log="$fb_dir/f-$name.log"
            fb_run "$bin" "$log" "" "fast boot: off" \
                --machine next --sdcard-readonly ${SD_CARD_ARGS[@]+"${SD_CARD_ARGS[@]}"} --fast-boot 10 "$@"
            grep -qE "warning.*fast boot: off \($reason\)" "$log" 2>/dev/null \
                || fb_faults+=("F $name: no warn line 'fast boot: off ($reason)'")
        }
        fb_case inject  "$JNEXT" "$fb_prog"  --inject "$fb_dir/inject.bin"
        # An explicit --machine next wins over the machine an .rzx recorded (GH #321), so the Next stays
        # the machine and the program term is what keeps the window off. The .rzx is recorded here, on a 48K.
        if timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 48k \
               --rzx-record "$fb_dir/in.rzx" --delayed-automatic-exit-frames 60 >"$fb_dir/f-mkrzx.log" 2>&1 \
           && [[ -s "$fb_dir/in.rzx" ]]; then
            fb_case rzxplay "$JNEXT" "$fb_prog"  --rzx-play "$fb_dir/in.rzx"
        else
            fb_faults+=("F rzxplay: could not record the .rzx fixture")
        fi
        fb_case shot    "$JNEXT" "$fb_sched" --delayed-screenshot "$fb_dir/f.png" --delayed-screenshot-frames 100000
        fb_case exitsec "$JNEXT" "$fb_sched" --delayed-automatic-exit 1000
        fb_case script  "$JNEXT" "$fb_sched" --script "$SCRIPT_DIR/boot-commit-probe.jds"
        fb_case pok     "$fb_sdl" "$fb_sched" --delayed-pok-frames 100000 "$fb_dir/t.pok" --pok-trainers 1
        fb_case record  "$JNEXT" "$fb_rec"   --record "$fb_dir/f.mp4"
        fb_case wavrec  "$JNEXT" "$fb_rec"   --wav-record "$fb_dir/f.wav"
        fb_case stems   "$JNEXT" "$fb_rec"   --wav-record-stems "$fb_dir/stems"
        fb_case rzxrec  "$JNEXT" "$fb_rec"   --rzx-record "$fb_dir/f.rzx"
        fb_case dac     "$JNEXT" "$fb_rec"   --dac-trace "$fb_dir/f.csv"
        fb_case recscr  "$JNEXT" "$fb_rec"   --record-script "$fb_dir/f.jds"

        if [[ ${#fb_faults[@]} -eq 0 ]]; then
            pass_row " (Qt saved pref + --fast-boot key press, SDL --set, --headless and every program/scheduled/recording flag disarm: armed, time up, key press, off)"
        else
            fail_row " ($(IFS='|'; echo "${fb_faults[*]}"))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
