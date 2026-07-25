#!/usr/bin/env bash
# Pin --record's process-status contract (GH #78, GH #86). Sourced by
# regression.sh; also directly executable.
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
        echo '[ "$JNEXT_TEST_FFMPEG_MODE" = "partial-fail" ] && { printf partial > "$output_path"; exit 42; }'
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

    # Leg 5 (review round 1, required item 2): the stale-target-removal
    # branch (video_recorder.cpp fs::remove error path). Point --record at a
    # NON-EMPTY DIRECTORY: fs::remove fails ENOTEMPTY before any encoder is
    # consulted, the recorder must report "cannot replace output file" and
    # the process must exit non-zero, leaving the directory untouched.
    dir_target="$TMP_DIR/jnext_test_dir_target.mp4"
    mkdir -p "$dir_target"
    printf 'occupied\n' > "$dir_target/keep.txt"
    if dir_out=$(PATH="$fake_bin:/usr/bin:/bin" \
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$dir_target" \
        --delayed-automatic-exit-frames 5 2>&1); then
        dir_rc=0
    else
        dir_rc=$?
    fi

    # Leg 6 (GH #86): a --record START failure must abort the run
    # IMMEDIATELY, not after the requested duration. Request a duration far
    # beyond the timeout (500000 frames ≈ 2.7 h emulated): a working abort
    # exits within seconds of init; the pre-fix behaviour (run everything,
    # report at exit) hits the 40 s timeout (rc 124) and/or blows the
    # wall-clock bound.
    abort_file="$TMP_DIR/jnext_test_abort_early.mp4"
    abort_t0=$(date +%s)
    if abort_out=$(PATH="$fake_bin:/usr/bin:/bin" JNEXT_TEST_FFMPEG_MODE=probe-fail \
        timeout --foreground --kill-after=5s 40s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$abort_file" \
        --delayed-automatic-exit-frames 500000 2>&1); then
        abort_rc=0
    else
        abort_rc=$?
    fi
    abort_elapsed=$(( $(date +%s) - abort_t0 ))

    # Leg 7 (GH #86 MINOR): an encoder that writes PARTIAL non-empty data
    # and then fails must still fail the exit contract, but the partial file
    # is KEPT for inspection (only 0-byte leftovers are removed).
    partial_file="$TMP_DIR/jnext_test_partial_recording.mp4"
    if partial_out=$(PATH="$fake_bin:/usr/bin:/bin" JNEXT_TEST_FFMPEG_MODE=partial-fail \
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --record "$partial_file" \
        --delayed-automatic-exit-frames 5 2>&1); then
        partial_rc=0
    else
        partial_rc=$?
    fi

    # GH #86 strengthened ! -s to ! -e on failed_file/empty_file: a failing
    # encoder's 0-byte artifact must be REMOVED, not merely empty.
    if [[ "$success_rc" -eq 0 && -s "$success_file" \
          && "$start_rc" -ne 0 && ! -e "$start_file" \
          && "$failed_rc" -ne 0 && ! -e "$failed_file" \
          && "$empty_rc" -ne 0 && ! -e "$empty_file" \
          && "$dir_rc" -ne 0 && -f "$dir_target/keep.txt" \
          && "$abort_rc" -ne 0 && "$abort_rc" -ne 124 \
          && "$abort_elapsed" -le 15 && ! -e "$abort_file" \
          && "$partial_rc" -ne 0 && -s "$partial_file" ]] \
        && grep -qF "Failed to start recording" <<< "$start_out" \
        && grep -qF "FFmpeg encoding failed" <<< "$failed_out" \
        && grep -qF "reported success but produced no usable output" <<< "$empty_out" \
        && grep -qF "cannot replace output file" <<< "$dir_out" \
        && grep -qF "Failed to start recording" <<< "$abort_out" \
        && grep -qF "FFmpeg encoding failed" <<< "$partial_out"; then
        pass_row " (success exits 0; start, encode, empty-output and stale-removal failures exit non-zero; start failure aborts early; partial output kept)"
    else
        fail_row " (success_rc=$success_rc success_size=$([[ -s "$success_file" ]] && echo nonzero || echo zero) start_rc=$start_rc fail_rc=$failed_rc empty_rc=$empty_rc dir_rc=$dir_rc abort_rc=$abort_rc abort_elapsed=${abort_elapsed}s partial_rc=$partial_rc partial_size=$([[ -s "$partial_file" ]] && echo nonzero || echo zero))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
