#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Validate every subsystem gain CLI endpoint/error and prove +6.0206 dB on the
# beeper contribution doubles captured host PCM.
if want subsystem-gain-func; then
    begin_func subsystem-gain-func
    cli_ok=true
    gain_options=(
        --audio-gain-beeper-db
        --audio-gain-ay0-db
        --audio-gain-ay1-db
        --audio-gain-ay2-db
        --audio-gain-dac-db
    )
    for gain_option in "${gain_options[@]}"; do
        "$JNEXT" "$gain_option" -24 --help &>/dev/null || cli_ok=false
        "$JNEXT" "$gain_option" 24 --help &>/dev/null || cli_ok=false
        for bad_gain in -24.1 24.1 nan loud; do
            if bad_out=$("$JNEXT" "$gain_option" "$bad_gain" --headless 2>&1); then
                cli_ok=false
            elif ! grep -qF -- \
                    "$gain_option: expected a number from -24 to +24 dB" \
                    <<<"$bad_out"; then
                cli_ok=false
            fi
        done
    done

    tone_bin="$SCRIPT_DIR/bin/beeper_tone.bin"
    attenuated_wav="$TMP_DIR/subsystem_gain_beeper_minus24db.wav"
    base_wav="$TMP_DIR/subsystem_gain_0db.wav"
    gain_wav="$TMP_DIR/subsystem_gain_beeper_6db.wav"
    rm -f "$attenuated_wav" "$base_wav" "$gain_wav"
    common=(--headless --machine 48k
            "${SD_CARD_ARGS[@]}"
            --inject "$tone_bin" --inject-org 8000 --inject-pc 8000
            --inject-delay 100 --delayed-automatic-exit-frames 300)

    if ! $cli_ok; then
        fail_row " (CLI endpoint/range validation failed)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for PCM comparison)"
    elif ! timeout --foreground --kill-after=5s 20s "$JNEXT" \
            "${common[@]}" --audio-gain-beeper-db -24 \
            --wav-record "$attenuated_wav" &>/dev/null ||
         ! timeout --foreground --kill-after=5s 20s "$JNEXT" \
            "${common[@]}" --wav-record "$base_wav" &>/dev/null ||
         ! timeout --foreground --kill-after=5s 20s "$JNEXT" \
            "${common[@]}" --audio-gain-beeper-db 6.0206 \
            --wav-record "$gain_wav" &>/dev/null; then
        fail_row " (emulator run failed)"
    elif gain_out=$(python3 - "$attenuated_wav" "$base_wav" "$gain_wav" <<'PY'
import array
import math
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

attenuated = peak(sys.argv[1])
base = peak(sys.argv[2])
gained = peak(sys.argv[3])

# The fixture also contains the 0 dB I2S resting term, so total peaks do not
# themselves double. Difference the same capture at three beeper gains to
# cancel that constant term and measure only the beeper transfer.
actual = (gained - base) / (base - attenuated) if base != attenuated else 0.0
expected = (2.0 - 1.0) / (1.0 - math.pow(10.0, -24.0 / 20.0))
if abs(actual - expected) > 0.02:
    raise SystemExit(
        f"beeper delta ratio {actual:.4f}, expected {expected:.4f} "
        f"({attenuated} -> {base} -> {gained})")
print(
    f"beeper-only gain verified ({attenuated} -> {base} -> {gained}, "
    f"delta ratio {actual:.4f})")
PY
    ); then
        pass_row " ($gain_out)"
    else
        fail_row " ($gain_out)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
