#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# tape-save-warmstart-func: a warm start that has to RECORD its machine does
# not open the tape save a second time (GH #89 review B4).
#
# A .nex loaded on a Next with no cached warm-start state cold-boots the
# firmware on a second, private Emulator to record one. That machine used to be
# built from the live configuration with --tape-save still in it, so it armed
# a second TapeRecorder on the same file: two writers, "recording" logged twice.
#
# The run uses a PRIVATE config directory, so its warm-start cache is cold
# whatever earlier rows left (the cache lives beside the SD image the config
# directory names), and passes the run's SD clone explicitly. Asserted:
#   record   the firmware is cold-booted (the recording machine exists),
#   once     "Tape SAVE: recording" is logged exactly once,
#   file     the WAV's header sizes match the file: one writer finished it.
if want tape-save-warmstart-func; then
    begin_func tape-save-warmstart-func

    tw_dir="$TMP_DIR/tape-save-warmstart"
    rm -rf "$tw_dir"; mkdir -p "$tw_dir/config"
    tw_sd="$JNEXT_CONFIG_DIR/sdcard/cspect-next-1gb-fixed.img"
    tw_faults=()

    tw_out=$(JNEXT_CONFIG_DIR="$tw_dir/config" timeout --foreground --kill-after=5s 120s \
        "$JNEXT" --headless --machine next --sdcard "$tw_sd" --rtc "$NEXTZXOS_RTC" \
        --tape-save "$tw_dir/ws.wav" --load test/00regression/nex/tilemap_demo.nex \
        --delayed-automatic-exit-frames 10 2>&1 || true)

    grep -q "cold-booting the firmware" <<<"$tw_out" \
        || tw_faults+=("record: the warm start did not record (cache not cold?)")
    tw_n=$(grep -c "Tape SAVE: recording" <<<"$tw_out" || true)
    [[ "$tw_n" == 1 ]] || tw_faults+=("once: 'Tape SAVE: recording' logged $tw_n times")
    tw_ok=$(perl -e '
        open my $f, "<:raw", $ARGV[0] or exit 1; read $f, my $h, 44;
        my @v = unpack "a4 V a4 a4 V v v V V v v a4 V", $h; my $size = -s $ARGV[0];
        print( ($v[0] eq "RIFF" && $v[1] == $size - 8 && $v[12] == $size - 44) ? "yes" : "no");' \
        "$tw_dir/ws.wav" 2>/dev/null || true)
    [[ "$tw_ok" == yes ]] || tw_faults+=("file: the WAV header does not match the file")

    if [[ ${#tw_faults[@]} -eq 0 ]]; then
        pass_row " (warm start recorded; the save opened once; the WAV is whole)"
    else
        fail_row " ($(IFS=';'; echo "${tw_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
