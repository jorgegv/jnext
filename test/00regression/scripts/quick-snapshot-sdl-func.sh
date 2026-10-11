#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend Quick Save / Quick Load (GitHub issue #321): Alt+F5 writes a
# snapshot into $JNEXT_CONFIG_DIR/snapshots, Alt+F9 restores the newest.
#
# WHY A FUNCTIONAL ROW. host_key_latch_test (HKL-QS-*) pins the chord
# predicate, but nothing in the unit tree runs SdlApp::run(), which is where
# the predicate is consulted BEFORE the F1..F10 block (an Alt+F9 that reached
# that block would fire the Multiface NMI instead) and where the load request
# is serviced. Only the real binary shows that.
#
# THE BINARY IS build/sdl-release (SdlApp is instantiated only when
# ENABLE_QT_UI=OFF), exactly as sdl-keypress-func.
#
# THE RUNS (all 48K, $JNEXT_CONFIG_DIR is this row's own directory):
#   empty    Alt+F9 with no snapshot: the log says "no snapshot", no cold boot
#            loads a .jns, and the run exits normally;
#   save     BORDER 1, Alt+F5, BORDER 2, Alt+F5, then a screenshot A: exactly
#            two files named jnext-YYYYMMDD-HHMMSS[-NN].jns, two "quick save:
#            wrote" lines;
#   load     a fresh machine, Alt+F9, screenshot B;
#   control  a fresh machine, nothing pressed, screenshot C.
# The oracle is A's own render: B must show A's border colour (the NEWER save),
# and C must not, so the comparison is not vacuous. No palette value is
# hard-coded. Border pixels, not text: the BASIC cursor flashes and would make
# a whole-image comparison phase-dependent.
#
# The Xvfb is started with -noreset (GH #318; HS-89 enforces it).
if want quick-snapshot-sdl-func; then
    begin_func quick-snapshot-sdl-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    snap_dir="$RUN_DIR/snapshots"

    # qs_run <mode> <png>: mode = empty | save | load | control.
    # shellcheck disable=SC2016
    qs_run() {
        local mode="$1" out="$2"
        local log="$TMP_DIR/qs_sdl_$mode.log"
        rm -f "$out" "$log"
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
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
                wid=$(xdotool search --name JNEXT 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # 48K BASIC is at its prompt by emulated frame 150; six seconds is
            # 4x that at 1x speed (see sdl-keypress-func).
            sleep 6
            if [ -n "$wid" ]; then
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                tap() { xdotool key "$1" 2>/dev/null || true; sleep 0.4; }
                case "$mode" in
                    empty|load) tap alt+F9 ;;
                    save)
                        tap b; tap 1; tap Return; tap alt+F5
                        tap b; tap 2; tap Return; tap alt+F5 ;;
                esac
            fi
            wait $pid
        ' _ "$sdl_bin" "$out" "$mode" "$log" >/dev/null 2>&1 \
            && qs_rc[$mode]=0 || qs_rc[$mode]=$?
    }
    declare -A qs_rc=()

    # border_px <png>: the colour of the top-left pixel (the border).
    border_px() {
        local image_cmd=(magick); command -v magick &>/dev/null || image_cmd=(convert)
        "${image_cmd[@]}" "$1" -format '%[pixel:p{2,2}]' info: 2>/dev/null || echo "?"
    }

    shot_a="$TMP_DIR/qs_sdl_a.png"; shot_b="$TMP_DIR/qs_sdl_b.png"; shot_c="$TMP_DIR/qs_sdl_c.png"
    shot_e="$TMP_DIR/qs_sdl_e.png"

    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v xvfb-run &>/dev/null; then
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
                err_line=$(grep -m1 -E '\[(error|critical)\]' "$TMP_DIR/qs_sdl_$m.log" 2>/dev/null) || err_line="no error logged"
                problems+=("$m run: jnext exit ${qs_rc[$m]}, png $([[ -s "$shot" ]] && echo written || echo missing); $err_line")
            fi
        done
        if [[ ${#problems[@]} -gt 0 ]]; then
            fail_row " ($(IFS='|'; echo "${problems[*]}"))"
        else
            saved_lines=$(grep -c 'quick save: wrote' "$TMP_DIR/qs_sdl_save.log" || true)
            empty_msg=$(grep -c 'quick load: no snapshot' "$TMP_DIR/qs_sdl_empty.log" || true)
            empty_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$TMP_DIR/qs_sdl_empty.log" || true)
            load_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$TMP_DIR/qs_sdl_load.log" || true)
            px_a=$(border_px "$shot_a"); px_b=$(border_px "$shot_b"); px_c=$(border_px "$shot_c")
            if [[ "$n_files" -ne 2 || "$bad_names" -ne 0 || "$saved_lines" -ne 2 ]]; then
                fail_row " (save: $n_files file(s), $bad_names misnamed, $saved_lines 'quick save: wrote' line(s); want 2/0/2)"
            elif [[ "$empty_msg" -lt 1 || "$empty_boot" -ne 0 ]]; then
                fail_row " (empty folder: 'no snapshot' lines=$empty_msg, .jns cold boots=$empty_boot; want >=1/0)"
            elif [[ "$load_boot" -lt 1 ]]; then
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
