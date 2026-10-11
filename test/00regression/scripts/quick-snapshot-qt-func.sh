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
# Same four runs and the same oracle as quick-snapshot-sdl-func (read its
# header): the border colour of the newer save's own render. Qt reports in the
# status bar, which MainWindow mirrors into the log as "status bar: ...", so
# this row also checks those lines: two "Quick saved:", a "Quick Load: no
# snapshot" on the empty folder, and "Quick loading:" on the load.
#
# The window is found by title and keys are aimed with the pointer, exactly as
# qt-keypress-func (no window manager runs, so focus follows PointerRoot).
# The Xvfb is started with -noreset (GH #318; HS-89 enforces it).
if want quick-snapshot-qt-func; then
    begin_func quick-snapshot-qt-func

    snap_dir="$RUN_DIR/snapshots"

    # qs_run <mode> <png>: mode = empty | save | load | control.
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
                # A QAction shortcut only fires in the ACTIVE window, and with no
                # window manager nothing activates one: the keys of
                # qt-keypress-func reach the guest through the pointer, but
                # Alt+F5 never matched until the window took the input focus
                # (measured: Qt activates it on the FocusIn this generates).
                xdotool windowfocus --sync "$wid" 2>/dev/null || true
                tap() { xdotool key "$1" 2>/dev/null || true; sleep 0.4; }
                case "$mode" in
                    empty|load) tap alt+F9 ;;
                    save)
                        tap b; tap 1; tap Return; tap alt+F5
                        tap b; tap 2; tap Return; tap alt+F5 ;;
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

    shot_a="$TMP_DIR/qs_qt_a.png"; shot_b="$TMP_DIR/qs_qt_b.png"; shot_c="$TMP_DIR/qs_qt_c.png"
    shot_e="$TMP_DIR/qs_qt_e.png"

    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        rm -rf "$snap_dir"
        qs_run empty   "$shot_e"
        qs_run save    "$shot_a"
        # A missing folder (nothing saved) is a count of 0, not an error under
        # pipefail: the verdict below reports it.
        snap_names=""
        [[ -d "$snap_dir" ]] && snap_names=$(find "$snap_dir" -maxdepth 1 -type f -name '*.jns')
        n_files=$(grep -c . <<<"$snap_names") || n_files=0
        bad_names=$(grep -vcE '/jnext-[0-9]{8}-[0-9]{6}(-[0-9]{2})?\.jns$' <<<"$snap_names") || bad_names=0
        [[ -n "$snap_names" ]] || bad_names=0
        qs_run load    "$shot_b"
        qs_run control "$shot_c"

        problems=()
        for m in empty save load control; do
            case "$m" in empty) shot="$shot_e" ;; save) shot="$shot_a" ;; load) shot="$shot_b" ;; *) shot="$shot_c" ;; esac
            if [[ ! -s "$shot" || "${qs_rc[$m]}" -ne 0 ]]; then
                err_line=$(grep -m1 -E '\[(error|critical)\]' "$TMP_DIR/qs_qt_$m.log" 2>/dev/null) || err_line="no error logged"
                problems+=("$m run: jnext exit ${qs_rc[$m]}, png $([[ -s "$shot" ]] && echo written || echo missing); $err_line")
            fi
        done
        if [[ ${#problems[@]} -gt 0 ]]; then
            fail_row " ($(IFS='|'; echo "${problems[*]}"))"
        else
            saved_lines=$(grep -c 'status bar: Quick saved:' "$TMP_DIR/qs_qt_save.log" || true)
            empty_msg=$(grep -c 'status bar: Quick Load: no snapshot' "$TMP_DIR/qs_qt_empty.log" || true)
            empty_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$TMP_DIR/qs_qt_empty.log" || true)
            load_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$TMP_DIR/qs_qt_load.log" || true)
            load_msg=$(grep -c 'status bar: Quick loading:' "$TMP_DIR/qs_qt_load.log" || true)
            px_a=$(border_px "$shot_a"); px_b=$(border_px "$shot_b"); px_c=$(border_px "$shot_c")
            if [[ "$n_files" -ne 2 || "$bad_names" -ne 0 || "$saved_lines" -ne 2 ]]; then
                fail_row " (save: $n_files file(s), $bad_names misnamed, $saved_lines 'status bar: Quick saved:' line(s); want 2/0/2)"
            elif [[ "$empty_msg" -lt 1 || "$empty_boot" -ne 0 ]]; then
                fail_row " (empty folder: 'no snapshot' lines=$empty_msg, .jns cold boots=$empty_boot; want >=1/0)"
            elif [[ "$load_boot" -lt 1 || "$load_msg" -lt 1 ]]; then
                fail_row " (Alt+F9 with snapshots present did not cold-boot a .jns)"
            elif [[ "$px_a" == "$px_c" ]]; then
                fail_row " (oracle is vacuous: the saved border $px_a equals the untouched machine's)"
            elif [[ "$px_b" != "$px_a" ]]; then
                fail_row " (Quick Load did not restore the newest save: border $px_b, saved $px_a, control $px_c)"
            else
                pass_row " (2 saves, Alt+F9 restored the newer: border $px_b vs control $px_c)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
