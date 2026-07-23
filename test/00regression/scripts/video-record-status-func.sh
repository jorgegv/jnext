#!/usr/bin/env bash
# Pin --record's process-status contract (GH #78). Sourced by regression.sh;
# also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

if want video-record-status-func; then
    begin_func video-record-status-func
    success_file="$TMP_DIR/jnext_test_status_success.mp4"
    start_file="$TMP_DIR/jnext_test_start_failed.mp4"
    failed_file="$TMP_DIR/jnext_test_failed_recording.mp4"
    empty_file="$TMP_DIR/jnext_test_empty_recording.mp4"
    fake_bin="$TMP_DIR/fake-ffmpeg-bin"
    mkdir -p "$fake_bin"
    {
        echo '#!/bin/sh'
        echo '[ "$JNEXT_TEST_FFMPEG_MODE" = "probe-fail" ] && exit 41'
        echo '[ "$1" = "-version" ] && exit 0'
        echo 'for output_path do :; done'
        echo '[ "$JNEXT_TEST_FFMPEG_MODE" = "empty-success" ] && exit 0'
        echo ': > "$output_path"'
        echo 'exit 42'
    } > "$fake_bin/ffmpeg"
    chmod +x "$fake_bin/ffmpeg"

    # A successful recording must remain successful. The older recording row
    # validates its streams but deliberately swallows the process status; this
    # row pins the missing half of that contract explicitly.
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$success_file" \
        --delayed-automatic-exit-frames 5 &>/dev/null; then
        success_rc=0
    else
        success_rc=$?
    fi

    # The success-without-output stub does not touch its target. Seed a stale
    # non-empty file so the recorder must remove it before encoding; merely
    # checking "a non-empty file exists afterwards" would accept the stale one.
    printf 'stale recording\n' > "$empty_file"
    if start_out=$(PATH="$fake_bin:/usr/bin:/bin" JNEXT_TEST_FFMPEG_MODE=probe-fail \
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$start_file" \
        --delayed-automatic-exit-frames 5 2>&1); then
        start_rc=0
    else
        start_rc=$?
    fi
    if failed_out=$(PATH="$fake_bin:/usr/bin:/bin" JNEXT_TEST_FFMPEG_MODE=fail \
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$failed_file" \
        --delayed-automatic-exit-frames 5 2>&1); then
        failed_rc=0
    else
        failed_rc=$?
    fi
    if empty_out=$(PATH="$fake_bin:/usr/bin:/bin" JNEXT_TEST_FFMPEG_MODE=empty-success \
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$empty_file" \
        --delayed-automatic-exit-frames 5 2>&1); then
        empty_rc=0
    else
        empty_rc=$?
    fi

    if [[ "$success_rc" -eq 0 && -s "$success_file" \
          && "$start_rc" -ne 0 && ! -e "$start_file" \
          && "$failed_rc" -ne 0 && ! -s "$failed_file" \
          && "$empty_rc" -ne 0 && ! -s "$empty_file" ]] \
        && grep -qF "Failed to start recording" <<< "$start_out" \
        && grep -qF "FFmpeg encoding failed" <<< "$failed_out" \
        && grep -qF "reported success but produced no usable output" <<< "$empty_out"; then
        pass_row " (success exits 0; start, encode and empty-output failures exit non-zero)"
    else
        fail_row " (success_rc=$success_rc success_size=$([[ -s "$success_file" ]] && echo nonzero || echo zero) start_rc=$start_rc fail_rc=$failed_rc empty_rc=$empty_rc)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
