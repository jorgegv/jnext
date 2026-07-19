#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"


# WAV capture: headless mixed-audio recording survives sibling-NEX chaining.
if want wav-record-func; then
    begin_func wav-record-func
    wav_file="$TMP_DIR/direct_audio.wav"
    menu_nex="$PROJECT_DIR/test/00regression/nex/menu.nex"
    if reject_out=$("$JNEXT" --headless --silent --wav-record "$wav_file" 2>&1); then
        reject_status=0
    else
        reject_status=$?
    fi
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$menu_nex" \
        --wav-record "$wav_file" --delayed-keypress-frames 100 1 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    chained=$(echo "$out" | grep -cE "NEX: loaded '.*red\.nex'" || true)
    if [[ "$reject_status" -eq 0 ]] ||
       ! echo "$reject_out" | grep -q -- "--wav-record cannot be combined with --silent"; then
        fail_row " (--silent conflict was not rejected clearly)"
    elif [[ "$chained" -lt 1 ]]; then
        fail_row " (selector did not chain-load red.nex)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for WAV validation)"
    elif wav_out=$(python3 - "$wav_file" <<'PY'
import struct
import sys

path = sys.argv[1]
with open(path, "rb") as wav:
    data = wav.read()
if len(data) < 44:
    raise SystemExit("WAV is shorter than its header")
if data[:4] != b"RIFF" or data[8:12] != b"WAVE" or data[36:40] != b"data":
    raise SystemExit("invalid WAV container")
rate, channels, bits, payload = (
    struct.unpack_from("<I", data, 24)[0],
    struct.unpack_from("<H", data, 22)[0],
    struct.unpack_from("<H", data, 34)[0],
    struct.unpack_from("<I", data, 40)[0],
)
if (rate, channels, bits) != (44100, 2, 16):
    raise SystemExit(f"unexpected format {rate} Hz/{channels} ch/{bits} bit")
if payload != len(data) - 44 or payload < 600000:
    raise SystemExit(f"capture stopped early ({payload} payload bytes)")
print(f"{payload} payload bytes across selector cold boot")
PY
    ); then
        pass_row " ($wav_out)"
    else
        fail_row " ($wav_out)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
