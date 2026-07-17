#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Validate the CLI range and prove +6.0206 dB doubles captured host PCM.
if want audio-gain-func; then
    begin_func audio-gain-func
    cli_ok=true
    "$JNEXT" --audio-gain-db -24 --help &>/dev/null || cli_ok=false
    "$JNEXT" --audio-gain-db 24 --help &>/dev/null || cli_ok=false
    for bad_gain in -24.1 24.1 nan loud; do
        if bad_out=$("$JNEXT" --audio-gain-db "$bad_gain" --headless 2>&1); then
            cli_ok=false
        elif ! grep -q -- "--audio-gain-db: expected a number from -24 to +24 dB" \
                <<<"$bad_out"; then
            cli_ok=false
        fi
    done

    tone_bin="$SCRIPT_DIR/bin/beeper_tone.bin"
    base_wav="$TMP_DIR/audio_gain_0db.wav"
    gain_wav="$TMP_DIR/audio_gain_6db.wav"
    rm -f "$base_wav" "$gain_wav"
    common=(--headless --machine 48k
            "${SD_CARD_ARGS[@]}"
            --inject "$tone_bin" --inject-org 8000 --inject-pc 8000
            --inject-delay 100 --delayed-automatic-exit-frames 300)

    if ! $cli_ok; then
        fail_row " (CLI range validation failed)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for PCM comparison)"
    elif ! timeout --foreground --kill-after=5s 20s "$JNEXT" \
            "${common[@]}" --wav-record "$base_wav" &>/dev/null ||
         ! timeout --foreground --kill-after=5s 20s "$JNEXT" \
            "${common[@]}" --audio-gain-db 6.0206 \
            --wav-record "$gain_wav" &>/dev/null; then
        fail_row " (emulator run failed)"
    elif gain_out=$(python3 - "$base_wav" "$gain_wav" <<'PY'
import array
import struct
import sys

def peak(path):
    with open(path, "rb") as wav:
        data = wav.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"invalid WAV: {path}")
    payload = struct.unpack_from("<I", data, 40)[0]
    pcm = array.array("h", data[44:44 + payload])
    if sys.byteorder != "little":
        pcm.byteswap()
    pcm = pcm[44100 * 2:]
    if not pcm:
        raise SystemExit("capture is too short")
    return max(abs(sample) for sample in pcm)

base = peak(sys.argv[1])
gained = peak(sys.argv[2])
ratio = gained / base if base else 0.0
if not 1.98 <= ratio <= 2.02:
    raise SystemExit(f"peak ratio {ratio:.3f}, expected 2.000 ({base} -> {gained})")
print(f"PCM peak doubled ({base} -> {gained})")
PY
    ); then
        pass_row " ($gain_out)"
    else
        fail_row " ($gain_out)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
