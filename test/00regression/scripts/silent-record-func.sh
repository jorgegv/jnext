#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --silent + --record test: with audio synthesis skipped the audio temp file
# is 0 bytes; VideoRecorder::stop() must detect that and encode video-only (no
# audio input, no "-shortest") — ffmpeg fed a zero-duration raw-PCM input plus
# "-shortest" clamps the WHOLE output to zero duration and still exits 0. So
# assert the output MP4 has a real video stream with a real (non-zero)
# duration, not just "a file appeared".
if want silent-record-func; then
    begin_func silent-record-func
    rec_file="$TMP_DIR/silent_recording.mp4"
    rm -f "$rec_file"
    if ! command -v ffprobe &>/dev/null; then
        skip_row " (ffprobe not available for validation)"
    else
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --silent \
            "${SD_CARD_ARGS[@]}" \
            --record "$rec_file" \
            --delayed-automatic-exit 3 &>/dev/null || true
        if [[ ! -s "$rec_file" ]]; then
            fail_row " (no MP4 file produced)"
        else
            has_video=$(count_streams "$rec_file" video)
            duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rec_file" 2>/dev/null || echo 0)
            duration_ok=$(awk -v d="$duration" 'BEGIN{print (d+0 >= 1.0) ? 1 : 0}')
            if [[ "$has_video" -ge 1 && "$duration_ok" -eq 1 ]]; then
                pass_row " (video-only MP4, ${duration}s duration, ${has_video} video stream)"
            else
                fail_row " (has_video=$has_video duration=$duration — corrupt/empty recording reported as success)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
