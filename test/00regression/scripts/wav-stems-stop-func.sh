#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# wav-stems-stop-func: the stem files are finalised when the run ends by a signal, and a write error
# or a refused DIR is an ERROR (non-zero exit).
#   leg 1   a headless run ended by SIGTERM: the shell reports 143 (the signal re-raised after the
#           teardown) and the seven headers are consistent (data length = size - 44, a second or more).
#   leg 2   the same run under full-disk.sh with a 64 KB per-file limit (JNEXT_FULL_DISK_KB): the open
#           and the 44-byte headers succeed, the data does not, so the run exits non-zero with
#           "--wav-record-stems: write error in".
#   leg 3   a DIR under a regular file cannot be created: exit 1, "cannot write the stem files in",
#           and no run (fast).
# (A start refused by a full disk cannot be made here: a full disk lets the open succeed and fails the
# write, so the start-time refusal is exercised through an uncreatable DIR instead.)
if want wav-stems-stop-func; then
    begin_func wav-stems-stop-func
    wz_dir="$TMP_DIR/wav-stems-stop"
    rm -rf "$wz_dir"; mkdir -p "$wz_dir"
    wz_faults=()

    # leg 1
    rc=0
    timeout --foreground --preserve-status --kill-after=5s -s TERM 10s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
        --machine 48k --wav-record-stems "$wz_dir/term" --delayed-automatic-exit-frames 5000000 \
        >"$wz_dir/term.log" 2>&1 || rc=$?
    if [[ "$rc" != 143 ]]; then
        wz_faults+=("SIGTERM: exit $rc (want 143)")
    elif ! command -v python3 &>/dev/null; then
        wz_faults+=("python3 not available for WAV validation")
    elif ! wz_out=$(python3 - "$wz_dir/term" <<'PY'
import os
import struct
import sys

for n in ["ay0", "ay1", "ay2", "dac", "beeper", "pi", "mix"]:
    path = f"{sys.argv[1]}/{n}.wav"
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(44)
    if len(head) < 44 or head[:4] != b"RIFF" or head[36:40] != b"data":
        raise SystemExit(f"{n}.wav: invalid header")
    payload = struct.unpack_from("<I", head, 40)[0]
    if payload != size - 44 or payload < 4 * 44100:
        raise SystemExit(f"{n}.wav: data length {payload}, file {size} bytes (want consistent, >= 1 s)")
print("ok")
PY
    ); then
        wz_faults+=("SIGTERM headers: $wz_out")
    fi

    # leg 2
    rc=0
    JNEXT_FULL_DISK_KB=64 timeout --foreground --kill-after=5s 60s bash "$SCRIPT_DIR/full-disk.sh" "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine 48k --wav-record-stems "$wz_dir/full" --delayed-automatic-exit-frames 200 \
        >"$wz_dir/full.log" 2>&1 || rc=$?
    if [[ "$rc" == 0 ]] || ! grep -qF -- "--wav-record-stems: write error in" "$wz_dir/full.log"; then
        wz_faults+=("full disk: exit $rc, no 'write error in' line")
    fi

    # leg 3
    : >"$wz_dir/plain"
    rc=0
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine 48k \
        --wav-record-stems "$wz_dir/plain/sub" --delayed-automatic-exit-frames 5000000 \
        >"$wz_dir/bad.log" 2>&1 || rc=$?
    if [[ "$rc" != 1 ]] || ! grep -qF -- "cannot write the stem files in" "$wz_dir/bad.log"; then
        wz_faults+=("uncreatable DIR: exit $rc, no 'cannot write the stem files in' line")
    fi

    if [[ ${#wz_faults[@]} -gt 0 ]]; then
        fail_row " ($(IFS=';'; echo "${wz_faults[*]}"))"
    else
        pass_row " (SIGTERM: exit 143, seven consistent headers; full disk: non-zero + write error line; uncreatable DIR: exit 1 + message)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
