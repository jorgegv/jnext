#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #417: every Preferences setting can be set for one run with
# `--set section.key=value`, and the command line is NEVER written back.
#
# WHY A FUNCTIONAL ROW. The unit suites drive parse_assignment(), merge_cli_
# assignments() and MainWindow in isolation; the feature is the chain between
# them on the SHIPPED frontends: main.cpp parses, the Qt window merges the values
# over the saved file, the machine gets them, and the window saves jnext.conf for
# unrelated reasons during the run. Only a real run shows that the saved file
# still holds the saved values afterwards.
#
# The oracles are the machine's own state, not the program's log of what it did:
#   * NextREG 0x07 read back by a --script at frame 10: bits 1:0 are the speed
#     that was programmed, bits 5:4 the actual one (zxnext.vhd:5785-5791, 5817,
#     5903), so a request for 28 MHz reads 0x33 and 3.5 MHz reads 0x00;
#   * the tape loader's own line "tape attached ... mode: fast|realtime";
#   * the controller the host binds to Joy 1 ("Joystick 1 connected: '<name>' [<id>]"),
#     on SDL virtual joysticks (no hardware);
#   * the saved file, read after the run.
#
#   Q1  Qt, saved cpu_speed=0 / tape_fast_load=true / last_load_dir: --set
#       startup.cpu_speed=3 and tape_fast_load=false win for the run (NR 0x07 33,
#       "mode: realtime"), and AFTER the run the file still says 0 / true / the
#       saved directory although the run saved it (the loaded tape is in
#       [recent] files).
#   Q2  Qt: the later of --cpu-speed and --set startup.cpu_speed wins, both orders.
#   Q3  Qt: a saved controller on Joy 1 and `--set input.joy1_device=` (empty):
#       Automatic wins for the run, and the file keeps the saved controller.
#   Q4  Qt under Xvfb (Linux only: the X11 window is where size and keys are
#       observable): --set startup.window_scale=3 gives a bigger window than
#       --scale 2, which gives a bigger one than the saved scale 1; --set screenshot.quick_dir / quick_format govern Alt+K, Quick
#       Screenshot (a .scr in the command line's directory); and the file, which the
#       run rewrote, still holds the saved scale, directory and format.
#   Q5  Qt: saved silent=true / tape_auto_play=true / tape_fast_load=false: --set
#       startup.silent=false lets a WAV be recorded (the saved value would refuse
#       it), tape_auto_play=false reads "off", tape_fast_load=true reads "fast".
#   S1  SDL-only: --set startup.cpu_speed, tape_fast_load=false, and a Qt-only key
#       (window_scale) that warns and runs on.
if want cli-set-func; then
    begin_func cli-set-func
    cs_dir="$TMP_DIR/cli-set"
    rm -rf "$cs_dir"; mkdir -p "$cs_dir"
    cs_conf="$JNEXT_CONFIG_DIR/jnext.conf"
    cs_tap="$PROJECT_DIR/test/00regression/tap/slow_gap.tap"
    cs_jds="$cs_dir/nr07.jds"
    printf 'on frame 10 once do log "NR07=${nextreg[0x07]:x2}" end\n' > "$cs_jds"
    cs_faults=()
    cs_jp_spec="jnext pad A:1234:0001;jnext pad A:1234:0001;jnext stick C:5678:0002:raw"

    # cs_qt <log> <args...>: one Qt run, 48K, the NR 0x07 script, 40 frames.
    cs_qt() { local log=$1; shift
        QT_QPA_PLATFORM=offscreen JNEXT_TEST_VIRTUAL_JOYSTICKS="$cs_jp_spec" \
        timeout --foreground --kill-after=5s 120s "$JNEXT" --silent --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --script "$cs_jds" "$@" \
            --delayed-automatic-exit-frames 40 >"$log" 2>&1; }
    cs_sdl() { local log=$1; shift
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s "$JNEXT_SDL" --silent --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --script "$cs_jds" "$@" \
            --delayed-automatic-exit-frames 40 >"$log" 2>&1; }
    # cs_nr07 <log>: the two hex digits of the NR 0x07 line.
    cs_nr07() { sed -n 's/.*NR07=\([0-9a-f][0-9a-f]\).*/\1/p' "$1" | head -1; }
    cs_expect_nr07() { # <log> <want> <what>
        local got; got=$(cs_nr07 "$1")
        [[ "$got" == "$2" ]] || cs_faults+=("$3: NR 0x07 is '${got:-none}', want $2")
    }

    # --- Q1 ---
    printf '[startup]\ncpu_speed=0\ntape_fast_load=true\n[paths]\nlast_load_dir=/saved/dir\n' > "$cs_conf"
    cs_qt "$cs_dir/q1.log" --set startup.cpu_speed=3 --set startup.tape_fast_load=false \
        --set paths.last_load_dir=/cli/dir --set debugger_keys.run=Shift+F9 --set debugger.attached=false \
        --load "$cs_tap" || cs_faults+=("Q1: the Qt run failed")
    # The debugger entries reach the window (this build has the debugger unless it says otherwise).
    if ! grep -qF "in a build without the debugger" "$cs_dir/q1.log"; then
        grep -qF "the --set debugger_keys entries are in force for this run" "$cs_dir/q1.log" \
            || cs_faults+=("Q1: --set debugger_keys.run did not reach the window")
        grep -qF "debugger window: attached=false for this run" "$cs_dir/q1.log" \
            || cs_faults+=("Q1: --set debugger.attached=false did not reach the debugger manager")
        grep -q "debugger_keys\]" "$cs_conf" && cs_faults+=("Q1: a [debugger_keys] section was written to the file")
    fi
    # A clash is refused before the window opens (run=F7 is Step Over's default).
    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" --silent --machine 48k \
           "${SD_CARD_ARGS[@]}" --set debugger_keys.run=F7 --delayed-automatic-exit-frames 5 >"$cs_dir/q1clash.log" 2>&1; then
        grep -qF "in a build without the debugger" "$cs_dir/q1clash.log" \
            || cs_faults+=("Q1: a clashing --set debugger_keys.run=F7 was accepted")
    else
        grep -qF "would both be bound" "$cs_dir/q1clash.log" || cs_faults+=("Q1: the clash was refused without saying which keys")
    fi
    cs_expect_nr07 "$cs_dir/q1.log" 33 "Q1 --set startup.cpu_speed=3"
    grep -qE "tape attached.*mode: realtime" "$cs_dir/q1.log" || cs_faults+=("Q1: tape not real time")
    grep -qE "cpu_speed=0" "$cs_conf" || cs_faults+=("Q1: cpu_speed was written to the file: $(tr '\n' ' ' < "$cs_conf")")
    grep -qE "tape_fast_load=true" "$cs_conf" || cs_faults+=("Q1: tape_fast_load was written to the file")
    grep -qF "last_load_dir=/saved/dir" "$cs_conf" || cs_faults+=("Q1: last_load_dir was written to the file")
    grep -qF "slow_gap.tap" "$cs_conf" || cs_faults+=("Q1: the file was not rewritten during the run (no recent file), so the leak check proves nothing")

    # --- Q2 ---
    rm -f "$cs_conf"
    cs_qt "$cs_dir/q2a.log" --cpu-speed 28 --set startup.cpu_speed=0 || cs_faults+=("Q2: run a failed")
    cs_expect_nr07 "$cs_dir/q2a.log" 00 "Q2 --cpu-speed 28 then --set startup.cpu_speed=0"
    cs_qt "$cs_dir/q2b.log" --set startup.cpu_speed=0 --cpu-speed 14 || cs_faults+=("Q2: run b failed")
    cs_expect_nr07 "$cs_dir/q2b.log" 22 "Q2 --set startup.cpu_speed=0 then --cpu-speed 14"

    # --- Q3: the controller ids come from the log of a run with no config ---
    rm -f "$cs_conf"
    cs_qt "$cs_dir/q3a.log" || cs_faults+=("Q3: first run failed")
    cs_g=$(sed -n "s/.*Joystick 1 connected: 'jnext pad A' \[\([0-9a-f]\{32\}\)\].*/\1/p" "$cs_dir/q3a.log" | head -1)
    cs_h=$(sed -n "s/.*'jnext stick C' \[\([0-9a-f]\{32\}\)\] detected but not connected.*/\1/p" "$cs_dir/q3a.log" | head -1)
    if [[ -z "$cs_g" || -z "$cs_h" ]]; then
        cs_faults+=("Q3: no controller ids in the log")
    else
        printf '[input]\njoy1_device=%s\n' "$cs_h" > "$cs_conf"
        cs_qt "$cs_dir/q3b.log" || cs_faults+=("Q3: saved-controller run failed")
        grep -qF "Joystick 1 connected: 'jnext stick C' [$cs_h]" "$cs_dir/q3b.log" \
            || cs_faults+=("Q3: the saved controller did not drive Joy 1 (the control leg)")
        printf '[input]\njoy1_device=%s\n' "$cs_h" > "$cs_conf"
        cs_qt "$cs_dir/q3c.log" --set input.joy1_device= || cs_faults+=("Q3: --set input.joy1_device= run failed")
        grep -qF "Joystick 1 connected: 'jnext pad A' [$cs_g]" "$cs_dir/q3c.log" \
            || cs_faults+=("Q3: --set input.joy1_device= (Automatic) did not override the saved controller")
        grep -qF "joy1_device=$cs_h" "$cs_conf" || cs_faults+=("Q3: the saved controller was changed in the file")
    fi

    # --- Q5: the booleans a flag could not say over a saved value ---
    printf '[startup]\nsilent=true\ntape_auto_play=true\ntape_fast_load=false\n' > "$cs_conf"
    cs_qt5() { local log=$1; shift   # no --silent of its own: the saved value is what is under test
        QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s "$JNEXT" --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" --delayed-automatic-exit-frames 40 >"$log" 2>&1; }
    cs_wav="$cs_dir/q5.wav"
    if cs_qt5 "$cs_dir/q5a.log" --wav-record "$cs_wav"; then
        cs_faults+=("Q5: saved silent=true + --wav-record was not refused (the control leg)")
    fi
    rm -f "$cs_wav"
    cs_qt5 "$cs_dir/q5b.log" --set startup.silent=false --set startup.tape_auto_play=false \
        --set startup.tape_fast_load=true --wav-record "$cs_wav" --load "$cs_tap" \
        || cs_faults+=("Q5: --set startup.silent=false + --wav-record failed")
    [[ -s "$cs_wav" ]] || cs_faults+=("Q5: no WAV recorded although --set startup.silent=false")
    grep -qF "Tape automatic play/stop: off" "$cs_dir/q5b.log" || cs_faults+=("Q5: --set startup.tape_auto_play=false over a saved true is not off")
    grep -qE "tape attached.*mode: fast" "$cs_dir/q5b.log" || cs_faults+=("Q5: --set startup.tape_fast_load=true over a saved false is not fast")
    grep -qF "silent=true" "$cs_conf" && grep -qF "tape_auto_play=true" "$cs_conf" && grep -qF "tape_fast_load=false" "$cs_conf" \
        || cs_faults+=("Q5: a command-line boolean was written to the file: $(tr '\n' ' ' < "$cs_conf")")

    # --- Q4: the Qt window itself (X11 under Xvfb, Linux only) ---
    cs_legs="Qt, SDL-only"
    if [[ "${JNEXT_TARGET_OS:-linux}" == linux ]]; then
        cs_legs="Qt (also the window), SDL-only"
        if ! command -v xvfb-run >/dev/null 2>&1 || ! command -v xdotool >/dev/null 2>&1; then
            cs_faults+=("Q4: xvfb-run / xdotool are not installed (the test job installs both)")
        else
            cs_saved_q="$cs_dir/saved-quick"; cs_cli_q="$cs_dir/cli-quick"
            mkdir -p "$cs_saved_q" "$cs_cli_q"
            printf '[screenshot]\nquick_dir=%s\nquick_format=png\n[startup]\nwindow_scale=1\n' "$cs_saved_q" > "$cs_conf"
            # cs_xwin <geometry file> <keys> <jnext args...>: one Qt run on X11; the window's
            # width is written to the file after 3 s, then the keys are sent to it.
            cs_xwin() { local out=$1 keys=$2; shift 2
                env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_AUDIODRIVER=dummy \
                timeout --kill-after=5s 120s \
                xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
                    set -uo pipefail
                    out="$1"; keys="$2"; shift 2
                    "$@" >/dev/null 2>&1 &
                    pid=$!
                    wid=""
                    for _ in $(seq 1 100); do
                        wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                        [ -n "$wid" ] && break
                        sleep 0.2
                    done
                    sleep 3
                    WIDTH=0
                    [ -n "$wid" ] && eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
                    echo "$WIDTH" > "$out"
                    if [ -n "$keys" ] && [ -n "$wid" ]; then
                        xdotool windowfocus --sync "$wid" 2>/dev/null || true
                        xdotool key "$keys" 2>/dev/null || true
                        sleep 1
                    fi
                    wait $pid
                ' _ "$out" "$keys" "$JNEXT" --silent --machine 48k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" \
                    --delayed-automatic-exit-frames 400 >/dev/null 2>&1 || true; }
            cs_xwin "$cs_dir/w3" alt+k --set startup.window_scale=3 --set "screenshot.quick_dir=$cs_cli_q" \
                --set screenshot.quick_format=scr --load "$cs_tap"
            cs_xwin "$cs_dir/w2" "" --scale 2
            cs_xwin "$cs_dir/w1" ""      # the saved scale, 1
            cs_w3=$(cat "$cs_dir/w3" 2>/dev/null || echo 0); cs_w2=$(cat "$cs_dir/w2" 2>/dev/null || echo 0)
            cs_w1=$(cat "$cs_dir/w1" 2>/dev/null || echo 0)
            if ! [[ "$cs_w3" =~ ^[0-9]+$ && "$cs_w2" =~ ^[0-9]+$ && "$cs_w1" =~ ^[0-9]+$ ]] \
               || (( cs_w1 < 256 || cs_w2 <= cs_w1 || cs_w3 <= cs_w2 )); then
                cs_faults+=("Q4: window widths: saved scale 1 is $cs_w1, --scale 2 is $cs_w2, --set startup.window_scale=3 is $cs_w3 (want each bigger than the last)")
            fi
            cs_scr=$(find "$cs_cli_q" -maxdepth 1 -name 'jnext-*.scr' | head -1)
            [[ -n "$cs_scr" && $(stat -c %s "$cs_scr") -eq 6912 ]] \
                || cs_faults+=("Q4: Alt+K wrote no 6912-byte .scr into the command line's quick_dir ($(ls "$cs_cli_q" "$cs_saved_q" | tr '\n' ' '))")
            [[ -z "$(find "$cs_saved_q" -type f)" ]] || cs_faults+=("Q4: the saved quick_dir received a capture")
            grep -qF "quick_dir=$cs_saved_q" "$cs_conf" && grep -qF "quick_format=png" "$cs_conf" && grep -qE "window_scale=1" "$cs_conf" \
                || cs_faults+=("Q4: quick_dir / quick_format / window_scale were written to the file: $(tr '\n' ' ' < "$cs_conf")")
            grep -qF "slow_gap.tap" "$cs_conf" || cs_faults+=("Q4: the file was not rewritten during the run (no recent file)")
        fi
    fi

    # --- S1 ---
    if [[ ! -x "$JNEXT_SDL" ]]; then
        cs_faults+=("S1: SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release'")
    else
        cs_sdl "$cs_dir/s1a.log" --set startup.cpu_speed=3 || cs_faults+=("S1: cpu run failed")
        cs_expect_nr07 "$cs_dir/s1a.log" 33 "S1 SDL --set startup.cpu_speed=3"
        cs_sdl "$cs_dir/s1b.log" --set startup.tape_fast_load=false --load "$cs_tap" || cs_faults+=("S1: tape run failed")
        grep -qE "tape attached.*mode: realtime" "$cs_dir/s1b.log" || cs_faults+=("S1: SDL tape not real time")
        cs_sdl "$cs_dir/s1c.log" --set startup.window_scale=2 || cs_faults+=("S1: the window_scale run did not exit 0")
        grep -qF "warning: --set startup.window_scale has no effect in the SDL-only build" "$cs_dir/s1c.log" \
            || cs_faults+=("S1: no warning for a Qt-only key in the SDL-only build")
    fi

    if [[ ${#cs_faults[@]} -eq 0 ]]; then
        pass_row " ($cs_legs: --set cpu_speed 33, real-time and fast tape, auto play off, silent=false, empty joy1_device = Automatic, the file unchanged after runs that saved it; the later of --cpu-speed and --set wins; SDL-only: cpu speed, tape mode, warning on a Qt-only key)"
    else
        fail_row " ($(IFS=';'; echo "${cs_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
