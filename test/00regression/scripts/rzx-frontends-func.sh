#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-frontends-func: command-line RZX playback and recording work in ALL THREE
# frontends — headless, the Qt GUI ($JNEXT, offscreen) and the SDL-only build
# (build/sdl-release/jnext, dummy drivers) — in every command-line spelling.
#
# WHY THIS ROW EXISTS. QtApp read --rzx-play / --rzx-record at the end of its
# init(), but main.cpp only calls set_rzx_play() / set_rzx_record() AFTER
# app.init(), so the GUI saw them empty and silently played and recorded
# nothing; SdlApp's two setters were empty stubs. rzx-playback-func and
# rzx-record-func only ever ran --headless, where the options are read in run(),
# so nothing in the suite could see either frontend.
#
# THE MEASUREMENT.
#   truth    headless 48K records 150 frames with a keypress at frame 100 ('p',
#            which the 48K editor turns into PRINT) and a screenshot at frame 150.
#   control  a plain 48K boot, screenshot at frame 150, no RZX, no key.
#   Then, for each frontend:
#   play     --rzx-play with a screenshot at frame 150: the log must say the
#            playback started with the recorded frame count AND that it
#            completed (every recorded frame consumed by the frontend's own
#            frame loop), and the screenshot must equal `truth`. The playback
#            run presses no key, so PRINT can only come from the replayed input;
#            `truth` must also DIFFER from `control`, or the equality would
#            prove nothing.
#   load     --load x.rzx and a bare x.rzx argument (main.cpp routes both to
#   bare     --rzx-play): playback starts with the recorded frame count.
#   record   --rzx-record writes a file with the RZX! signature, and headless
#            plays it back to completion with the frame count the recorder
#            reported — a frontend-made recording is a real one.
#   bad      a garbage .rzx logs "RZX: failed to load" and exits non-zero (the
#            failed-load exit contract; load-exit-status-func covers only the
#            headless case of it).
if want rzx-frontends-func; then
    begin_func rzx-frontends-func

    rf_dir="$TMP_DIR/rzx-frontends"
    rm -rf "$rf_dir"; mkdir -p "$rf_dir"
    rf_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    rf_rec="$rf_dir/truth.rzx"
    rf_bad="$rf_dir/bad.rzx"
    rf_faults=()
    printf 'NOTRZX%.0s' {1..20} > "$rf_bad"

    # rf_run <frontend> <log> <jnext args...>: one 48K run of that frontend,
    # combined output to <log>; prints the exit status (never trips set -e).
    rf_run() {
        local fe=$1 log=$2 rc=0; shift 2
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$rf_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }
    # rf_saved_frames <log>: N from "RZX: recording saved — N frames", or empty.
    rf_saved_frames() {
        grep -oP 'RZX: recording saved — \K[0-9]+(?= frames)' "$1" | head -1 || true
    }

    # --- truth + control ---------------------------------------------------
    rc=$(rf_run headless "$rf_dir/truth.log" --rzx-record "$rf_rec" \
            --delayed-keypress-frames 100 p \
            --delayed-screenshot "$rf_dir/truth.png" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 150)
    rf_frames=$(rf_saved_frames "$rf_dir/truth.log")
    rc_ctl=$(rf_run headless "$rf_dir/control.log" \
            --delayed-screenshot "$rf_dir/control.png" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 150)

    if [[ "$rc" != 0 || -z "$rf_frames" || ! -s "$rf_rec" || ! -s "$rf_dir/truth.png" ]]; then
        fail_row " (could not record the ground truth: rc=$rc frames=${rf_frames:-none})"
    elif [[ "$rc_ctl" != 0 || ! -s "$rf_dir/control.png" ]]; then
        fail_row " (control boot failed: rc=$rc_ctl)"
    elif [[ ! -x "$rf_sdl" ]]; then
        fail_row " (SDL-only binary not built: $rf_sdl; run 'make sdl-release')"
    else
        rf_can_diff=$HAS_COMPARE
        if $rf_can_diff; then
            ctl_diff=$(png_diff "$rf_dir/truth.png" "$rf_dir/control.png")
            [[ "$ctl_diff" -gt 0 && "$ctl_diff" -lt 999999 ]] \
                || rf_faults+=("truth does not differ from a plain boot (png_diff=$ctl_diff): the replayed key proves nothing")
        fi
        started="RZX: playback started — $rf_frames frames"

        for fe in headless qt sdl; do
            # play: frame count, completion, and the replayed picture.
            png="$rf_dir/$fe-play.png"; log="$rf_dir/$fe-play.log"
            rc=$(rf_run "$fe" "$log" --rzx-play "$rf_rec" \
                    --delayed-screenshot "$png" --delayed-screenshot-frames 150 \
                    --delayed-automatic-exit-frames 160)
            [[ "$rc" == 0 ]] || rf_faults+=("$fe --rzx-play rc=$rc")
            grep -qF "$started" "$log" \
                || rf_faults+=("$fe --rzx-play: no '$started'")
            grep -qF "RZX: playback complete (all frames consumed)" "$log" \
                || rf_faults+=("$fe --rzx-play: playback never completed")
            if $rf_can_diff; then
                d=$(png_diff "$png" "$rf_dir/truth.png")
                [[ "$d" == 0 ]] || rf_faults+=("$fe --rzx-play frame 150 differs from the recording (png_diff=$d)")
            fi

            # load / bare: main.cpp routes both to --rzx-play.
            log="$rf_dir/$fe-load.log"
            rc=$(rf_run "$fe" "$log" --load "$rf_rec" --delayed-automatic-exit-frames 10)
            [[ "$rc" == 0 ]] && grep -qF "$started" "$log" \
                || rf_faults+=("$fe --load x.rzx: rc=$rc, no '$started'")
            log="$rf_dir/$fe-bare.log"
            rc=$(rf_run "$fe" "$log" "$rf_rec" --delayed-automatic-exit-frames 10)
            [[ "$rc" == 0 ]] && grep -qF "$started" "$log" \
                || rf_faults+=("$fe bare x.rzx: rc=$rc, no '$started'")

            # record: a real file, and headless can play all of it.
            out="$rf_dir/$fe-made.rzx"; log="$rf_dir/$fe-record.log"
            rc=$(rf_run "$fe" "$log" --rzx-record "$out" --delayed-automatic-exit-frames 30)
            n=$(rf_saved_frames "$log")
            magic=$(xxd -l 4 -p "$out" 2>/dev/null || true)
            if [[ "$rc" != 0 || -z "$n" || "$magic" != "525a5821" ]]; then
                rf_faults+=("$fe --rzx-record: rc=$rc saved=${n:-none} magic=${magic:-none}")
            else
                log="$rf_dir/$fe-made-play.log"
                rc=$(rf_run headless "$log" --rzx-play "$out" --delayed-automatic-exit-frames 60)
                [[ "$rc" == 0 ]] && grep -qF "RZX: playback started — $n frames" "$log" \
                    && grep -qF "RZX: playback complete (all frames consumed)" "$log" \
                    || rf_faults+=("$fe --rzx-record: its $n-frame file does not play back to completion (rc=$rc)")
            fi

            # bad: a failed RZX load exits non-zero, with the loader's error.
            log="$rf_dir/$fe-bad.log"
            rc=$(rf_run "$fe" "$log" --rzx-play "$rf_bad" --delayed-automatic-exit-frames 10)
            [[ "$rc" != 0 ]] && grep -qF "RZX: failed to load" "$log" \
                || rf_faults+=("$fe garbage --rzx-play: rc=$rc (want non-zero + 'RZX: failed to load')")
        done

        if [[ ${#rf_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${rf_faults[*]}"))"
        elif ! $rf_can_diff; then
            skip_row " (no ImageMagick — cannot verify the replayed picture; every other check passed)"
        else
            pass_row " (headless/Qt/SDL: --rzx-play, --load, bare .rzx replay all $rf_frames frames and match the recording; --rzx-record plays back; bad .rzx exits !=0)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
