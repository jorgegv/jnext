#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Render-skip at turbo speed (Qt frontend only). At --speed 400 the Qt tick
# throttles Emulator::render_frame() to ~50 Hz wall-clock for frames nobody
# displays. This row proves the three load-bearing guarantees:
#   (1) capture-frame correctness: a --delayed-screenshot taken at --speed 400
#       is pixel-identical to the headless (never-skipping) capture of the
#       SAME emulated frame — i.e. the skip can never hit the capture frame.
#       beast.nex is used because its parallax animates every frame, so a
#       stale framebuffer (frame ~97-99 instead of 100) differs visibly.
#   (2) recorder guard: with --record active the core NEVER skips (the debug
#       witness line "render skipped for undisplayed frame" must be absent),
#       and the MP4 contains genuinely fresh frames — adjacent decoded frames
#       in the animated region differ. Without the guard the recorder would
#       capture the same stale composite 3-4x in a row.
#   (3) engagement: the skip actually fires at --speed 400 (witness line
#       present in the no-record run). NOTE: (3) is wall-clock-pacing bounded
#       like audio-underrun-func — skips only happen when ticks arrive faster
#       than 20 ms, so a severely starved host could render every frame and
#       report zero skips. The 48K boot workload (~2.5 ms/frame) + 600 frames
#       makes that require a sustained ~8x slowdown.
if want render-skip-turbo-func; then
    begin_func render-skip-turbo-func
    beast_nex="$PROJECT_DIR/test/00regression/nex/beast.nex"
    ctrl_png="$TMP_DIR/rsk-ctrl.png"
    turbo_png="$TMP_DIR/rsk-turbo.png"
    turbo_mp4="$TMP_DIR/rsk-turbo.mp4"
    turbo_log="$TMP_DIR/rsk-turbo.log"
    rec_log="$TMP_DIR/rsk-rec.log"
    engage_log="$TMP_DIR/rsk-engage.log"
    rm -f "$ctrl_png" "$turbo_png" "$turbo_mp4" "$turbo_log" "$rec_log" "$engage_log"
    SKIP_WITNESS="render skipped for undisplayed frame"

    if ! $HAS_COMPARE || ! command -v ffmpeg &>/dev/null || ! command -v ffprobe &>/dev/null; then
        skip_row " (needs ImageMagick compare + ffmpeg/ffprobe)"
    else
        rsk_fail=""

        # (1a) headless control: beast frame 100, never-skipping path.
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$beast_nex" \
            --delayed-screenshot "$ctrl_png" --delayed-screenshot-frames 100 \
            --delayed-automatic-exit-frames 130 >/dev/null 2>&1 || rsk_fail="ctrl-run"

        # (1b) Qt offscreen at --speed 400: same emulated frame.
        QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --silent --speed 400 \
            --log-level video=debug --load "$beast_nex" \
            --delayed-screenshot "$turbo_png" --delayed-screenshot-frames 100 \
            --delayed-automatic-exit-frames 130 >/dev/null 2>"$turbo_log" || rsk_fail="${rsk_fail:+$rsk_fail,}turbo-run"

        if [[ -z "$rsk_fail" ]]; then
            diff_pixels=$(png_diff "$turbo_png" "$ctrl_png")
            [[ "$diff_pixels" -eq 0 ]] || rsk_fail="capture-diff=$diff_pixels"
        fi

        # (2) --record at --speed 400: guard forces every frame to render.
        if [[ -z "$rsk_fail" ]]; then
            QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --silent --speed 400 \
                --log-level video=debug --load "$beast_nex" \
                --record "$turbo_mp4" \
                --delayed-automatic-exit-frames 220 >/dev/null 2>"$rec_log" || rsk_fail="rec-run"
        fi
        if [[ -z "$rsk_fail" ]]; then
            if grep -q "$SKIP_WITNESS" "$rec_log"; then
                rsk_fail="skip-while-recording"
            fi
            rec_streams=$(count_streams "$turbo_mp4" video)
            [[ "$rec_streams" -ge 1 ]] || rsk_fail="${rsk_fail:-mp4-no-video}"
        fi
        if [[ -z "$rsk_fail" ]]; then
            # Adjacent decoded frames in the animated region must differ.
            # A missing recorder guard duplicates the stale composite across
            # each ~20 ms window (3-4 frames), so at least one adjacent pair
            # among 5 consecutive frames decodes (near-)identical.
            rm -f "$TMP_DIR"/rsk-f*.png
            ffmpeg -v error -i "$turbo_mp4" -vf "select='between(n,150,154)'" -fps_mode passthrough \
                "$TMP_DIR/rsk-f%d.png" </dev/null 2>/dev/null || true
            if [[ -f "$TMP_DIR/rsk-f5.png" ]]; then
                for i in 1 2 3 4; do
                    # Sentinel 0 is deliberate: a parse failure must read as
                    # "stale pair" and fail the check, not slip past it.
                    pair_diff=$(png_diff "$TMP_DIR/rsk-f$i.png" "$TMP_DIR/rsk-f$((i+1)).png" 0)
                    if [[ "$pair_diff" -le 100 ]]; then
                        rsk_fail="stale-recorded-frame-pair-$i-diff=$pair_diff"
                        break
                    fi
                done
            else
                rsk_fail="mp4-frame-extract"
            fi
        fi

        # (3) engagement: skips actually fire at turbo speed (48K boot, cheap).
        if [[ -z "$rsk_fail" ]]; then
            QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 --silent --speed 400 \
                --log-level video=debug \
                --delayed-automatic-exit-frames 600 >/dev/null 2>"$engage_log" || rsk_fail="engage-run"
        fi
        if [[ -z "$rsk_fail" ]]; then
            engage_count=$(grep -c "$SKIP_WITNESS" "$engage_log" || true)
            # Lower bound: the throttle engages at all. Upper bound: it must
            # also RENDER at ~50 Hz — a broken throttle that skips every
            # single frame produces skips == frames. 590 leaves room for the
            # fastest plausible tick rate while rejecting skip-everything.
            [[ "$engage_count" -gt 0 ]] || rsk_fail="no-skip-at-turbo"
            [[ -n "$rsk_fail" ]] || [[ "$engage_count" -le 590 ]] || rsk_fail="skipped-every-frame=$engage_count"
        fi

        if [[ -z "$rsk_fail" ]]; then
            pass_row " (turbo capture identical; recorder never skipped, frames fresh; ${engage_count} skips engaged)"
        else
            fail_row " ($rsk_fail)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
