#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Qt frontend Quick Save / Quick Load (GitHub issue #321): File > Quick Save
# Snapshot (Alt+F5) and Quick Load Snapshot (Alt+F9) on the real binary, so the
# MainWindow wiring and the real QtApp::cold_boot are exercised end to end
# (preferences_apply_test PA-22* drives the slots without a cold boot).
#
# Same five runs and the same oracle as quick-snapshot-sdl-func (read its
# header). Qt reports in the status bar, which MainWindow mirrors into the log
# as "status bar: ...": two "Quick saved:", "Quick Load: no snapshot" on the
# empty folder, "Quick Load: refused" for the unreadable file (no dialog).
#
# The window is found by title and keys are aimed with the pointer, exactly as
# qt-keypress-func; a QAction shortcut only fires in the ACTIVE window and no
# window manager runs, so the window also takes the input focus (measured: Qt
# activates it on the FocusIn this generates; without it Alt+F5 does nothing).
# The Xvfb is started with -noreset (GH #318; HS-89 enforces it). Tagged
# `quiet` like its SDL twin.

if want quick-snapshot-qt-func; then
    begin_func quick-snapshot-qt-func


    snap_dir="$RUN_DIR/snapshots"

    # qs_run <mode> <png>: mode = empty | save | load | load2 | bad.
    # shellcheck disable=SC2016
    qs_run() {
        local mode="$1" out="$2"
        local log="$TMP_DIR/qs_qt_$mode.log"
        rm -f "$out" "$log"
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 120s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            bin="$1"; out="$2"; mode="$3"; log="$4"
            "$bin" --machine 48k --silent \
                --delayed-screenshot "$out" --delayed-screenshot-frames 700 \
                --delayed-automatic-exit-frames 760 >"$log" 2>&1 &
            pid=$!
            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --onlyvisible --name "ZX Spectrum Next Emulator" 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # 48K BASIC is at its prompt by emulated frame 150; six seconds is
            # 4x that at 1x speed (see sdl-keypress-func).
            sleep 6
            X=0; Y=0; WIDTH=0; HEIGHT=0
            [ -n "$wid" ] && eval "$(xdotool getwindowgeometry --shell "$wid" 2>/dev/null)"
            if [ "$WIDTH" -ge 320 ] && [ "$HEIGHT" -ge 240 ]; then
                xdotool mousemove $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) 2>/dev/null || true
                xdotool windowfocus --sync "$wid" 2>/dev/null || true
                tap() { xdotool key "$1" 2>/dev/null || true; sleep 0.5; }
                case "$mode" in
                    empty|load|load2) tap alt+F9 ;;
                    save)
                        tap b; tap 1; tap Return; tap alt+F5
                        tap b; tap 2; tap Return; tap alt+F5 ;;
                    bad)
                        tap b; tap 3; tap Return; tap alt+F9 ;;
                esac
            fi
            wait $pid
        ' _ "$JNEXT" "$out" "$mode" "$log" >/dev/null 2>&1 \
            && qs_rc[$mode]=0 || qs_rc[$mode]=$?
    }
    declare -A qs_rc=()

    # border_px <png>: the colour of the top-left pixel (the border).
    border_px() {
        local image_cmd=(magick); command -v magick &>/dev/null || image_cmd=(convert)
        "${image_cmd[@]}" "$1" -format '%[pixel:p{2,2}]' info: 2>/dev/null || echo "?"
    }

    shot_e="$TMP_DIR/qs_qt_e.png"; shot_a="$TMP_DIR/qs_qt_a.png"
    shot_l="$TMP_DIR/qs_qt_l.png"; shot_l2="$TMP_DIR/qs_qt_l2.png"
    shot_bad="$TMP_DIR/qs_qt_bad.png"

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        rm -rf "$snap_dir"
        qs_run empty "$shot_e"
        qs_run save  "$shot_a"
        # A missing folder (nothing saved) is a count of 0, not an error under
        # pipefail: the verdict below reports it.
        snap_names=""
        [[ -d "$snap_dir" ]] && snap_names=$(find "$snap_dir" -maxdepth 1 -type f -name '*.jns')
        n_files=$(grep -c . <<<"$snap_names") || n_files=0
        bad_names=$(grep -vcE '/jnext-[0-9]{8}-[0-9]{6}(-[0-9]{2})?\.jns$' <<<"$snap_names") || bad_names=0
        [[ -n "$snap_names" ]] || bad_names=0
        first_saved=$(sort <<<"$snap_names" | head -1)   # names sort in save order
        qs_run load "$shot_l"
        # Make the OLDER save the newest by modification time: the load must now
        # restore a DIFFERENT state, which also proves the two saves differ.
        [[ -n "$first_saved" ]] && touch -d '10 minutes' "$first_saved"
        qs_run load2 "$shot_l2"
        # A garbage .jns, newest of all: Quick Load must refuse it and leave the
        # session alone (a BORDER 3 typed beforehand must survive).
        printf 'not a snapshot' >"$snap_dir/zz-bad.jns" 2>/dev/null || true
        touch -d '20 minutes' "$snap_dir/zz-bad.jns" 2>/dev/null || true
        qs_run bad "$shot_bad"

        problems=()
        for m in empty save load load2 bad; do
            case "$m" in empty) shot="$shot_e" ;; save) shot="$shot_a" ;; load) shot="$shot_l" ;; load2) shot="$shot_l2" ;; *) shot="$shot_bad" ;; esac
            if [[ ! -s "$shot" || "${qs_rc[$m]}" -ne 0 ]]; then
                err_line=$(grep -m1 -E '\[(error|critical)\]' "$TMP_DIR/qs_qt_$m.log" 2>/dev/null) || err_line="no error logged"
                problems+=("$m run: jnext exit ${qs_rc[$m]}, png $([[ -s "$shot" ]] && echo written || echo missing); $err_line")
            fi
        done
        if [[ ${#problems[@]} -gt 0 ]]; then
            fail_row " ($(IFS='|'; echo "${problems[*]}"))"
        else
            L() { echo "$TMP_DIR/qs_qt_$1.log"; }
            saved_lines=$(grep -c 'status bar: Quick saved:' "$(L save)" || true)
            empty_msg=$(grep -c 'status bar: Quick Load: no snapshot' "$(L empty)" || true)
            boots_save=$(grep -c 'Cold boot' "$(L save)" || true)
            boots_empty=$(grep -c 'Cold boot' "$(L empty)" || true)
            load_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$(L load)" || true)
            bad_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$(L bad)" || true)
            boots_bad=$(grep -c 'Cold boot' "$(L bad)" || true)
            bad_msg=$(grep -c 'status bar: Quick Load: refused' "$(L bad)" || true)
            px_e=$(border_px "$shot_e"); px_a=$(border_px "$shot_a"); px_l=$(border_px "$shot_l")
            px_l2=$(border_px "$shot_l2"); px_bad=$(border_px "$shot_bad")
            if [[ "$n_files" -ne 2 || "$bad_names" -ne 0 || "$saved_lines" -ne 2 ]]; then
                fail_row " (save: $n_files file(s), $bad_names misnamed, $saved_lines 'status bar: Quick saved:' line(s); want 2/0/2)"
            elif [[ "$empty_msg" -lt 1 || "$boots_empty" -ne "$boots_save" ]]; then
                fail_row " (empty folder: 'status bar: Quick Load: no snapshot' lines=$empty_msg, cold boots $boots_empty vs $boots_save at startup alone; want >=1 and equal)"
            elif [[ "$px_a" == "$px_e" ]]; then
                fail_row " (the saves did not change the machine: border $px_a equals the untouched machine's; BORDER keystrokes lost?)"
            elif [[ "$load_boot" -lt 1 ]]; then
                fail_row " (Alt+F9 with snapshots present did not cold-boot a .jns)"
            elif [[ "$px_l" != "$px_a" ]]; then
                fail_row " (Quick Load did not restore the newest save: border $px_l, saved $px_a, untouched $px_e)"
            elif [[ "$px_l2" == "$px_l" || "$px_l2" == "$px_e" ]]; then
                fail_row " (the two saves are not distinguishable, or Quick Load ignored modification time: after touching the older save the border is $px_l2; newest-by-name gave $px_l, untouched $px_e)"
            elif [[ "$bad_msg" -lt 1 || "$bad_boot" -ne 0 || "$boots_bad" -ne "$boots_save" ]]; then
                fail_row " (unreadable newest .jns: 'status bar: Quick Load: refused' lines=$bad_msg, .jns cold boots=$bad_boot, cold boots $boots_bad vs $boots_save; want >=1/0/equal)"
            elif [[ "$px_bad" == "$px_e" ]]; then
                fail_row " (unreadable newest .jns reset the session (or BORDER 3 was lost): border $px_bad equals the untouched machine's)"
            else
                pass_row " (2 distinct saves; Alt+F9 restored the newer ($px_l) and, once the older was touched, the older ($px_l2); empty and unreadable folders left the session alone)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
