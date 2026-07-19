#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Video recording test: verify --record produces a valid MP4 file
if want video-record-func; then
    begin_func video-record-func
    rec_file="$TMP_DIR/jnext_test_recording.mp4"
    rm -f "$rec_file"
    timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --record "$rec_file" \
        --delayed-automatic-exit 3 2>/dev/null || true
    if [[ -f "$rec_file" ]] && command -v ffprobe &>/dev/null; then
        rec_probe=$(ffprobe -show_streams "$rec_file" 2>/dev/null || true)
        has_video=$(grep -c "codec_type=video" <<< "$rec_probe" || true)
        has_audio=$(grep -c "codec_type=audio" <<< "$rec_probe" || true)
        if [[ "$has_video" -ge 1 && "$has_audio" -ge 1 ]]; then
            pass_row " (MP4 with video+audio streams)"
        else
            fail_row " (MP4 missing video or audio stream)"
        fi
    elif [[ -f "$rec_file" ]]; then
        skip_row " (ffprobe not available for validation)"
    else
        fail_row " (no MP4 file produced)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
