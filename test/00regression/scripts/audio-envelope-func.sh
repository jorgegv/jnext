#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# The suite's only audio-CONTENT row (GH #201).
#
# audio-gain-func and audio-underrun-func measure GAIN and PACING. Nothing
# measured what the emulator actually SYNTHESISES, so an AY envelope defect
# that made 10 of the 16 shapes wrong — shapes 4-7 ending at full volume
# instead of silence, 11 and 15 ending at the opposite rail, 9 and 13 one
# level short of it, and both triangles (10 and 14) locking into DC instead
# of turning round — passed 140/140 and shipped through every release to
# date. (Measured, not recalled: settling each shape on both builds and
# diffing gives exactly those ten; 0-3, 8 and 12 are unaffected.)
#
# Workload: bin/ay_envelope_sweep.bin walks ALL SIXTEEN envelope shapes on
# channel A with tone and noise disabled, so the DAC output is the envelope
# generator and nothing else (ym2149.vhd:469, :490-491). See that fixture's
# .asm for the VHDL basis and the regeneration bytes.
#
# Capture: --headless + --wav-record. Headless has no real-time pacing, so
# this row cannot join the set that lies under CPU contention; and the whole
# chain — injected binary, fixed frame count, no RTC, no tape — is
# deterministic to the sample. Verified: three consecutive runs produce a
# byte-identical fingerprint.
#
# Fingerprint: the capture cut into 32 equal slices, each reported as its
# mean (the DC level) and its AC RMS (the mean removed). Both halves matter
# and neither is redundant:
#   * a shape that ends at the WRONG RAIL is a pure DC change with no AC
#     change at all — slice 14 reads `0 0` correct and `1020 0` with the bug;
#   * a shape that LOCKS instead of ramping is a pure AC collapse —
#     slices 20-22 read `96 226 / 424 399 / 958 224` correct and
#     `13 91 / 13 91 / 21 107` with the bug — the AC RMS collapsing to
#     between a half and a quarter.
# The comparison is EXACT, not tolerance-based: the pipeline is deterministic,
# so any tolerance would only be a place for a real regression to hide.
#
# Every value quoted above is read off a capture, not off a memory of one.
if want audio-envelope-func; then
    begin_func audio-envelope-func

    fixture="$SCRIPT_DIR/bin/ay_envelope_sweep.bin"
    reference="$SCRIPT_DIR/ref/ay-envelope-profile.txt"
    capture="$TMP_DIR/ay_envelope.wav"
    measured="$TMP_DIR/ay_envelope_profile.txt"
    rm -f "$capture" "$measured"

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for the audio profile)"
    elif [[ ! -r "$fixture" || ! -r "$reference" ]]; then
        fail_row " (fixture or reference missing)"
    elif ! timeout --foreground --kill-after=5s 90s "$JNEXT" \
            --headless --machine 128k "${SD_CARD_ARGS[@]}" \
            --inject "$fixture" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 340 \
            --wav-record "$capture" &>/dev/null; then
        fail_row " (emulator run failed)"
    elif ! python3 - "$capture" >"$measured" <<'PY'
import array
import math
import struct
import sys

SLICES = 32

with open(sys.argv[1], "rb") as wav:
    data = wav.read()
if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
    raise SystemExit("invalid WAV: %s" % sys.argv[1])
payload = struct.unpack_from("<I", data, 40)[0]
pcm = array.array("h", data[44:44 + payload])
if sys.byteorder != "little":
    pcm.byteswap()
left = pcm[0::2]                      # both channels carry the same AY mix
if len(left) < SLICES * 64:
    raise SystemExit("capture is too short: %d samples" % len(left))

width = len(left) // SLICES
for i in range(SLICES):
    chunk = left[i * width:(i + 1) * width]
    mean = sum(chunk) / len(chunk)
    ac = math.sqrt(sum((s - mean) * (s - mean) for s in chunk) / len(chunk))
    print("%02d %6d %6d" % (i, round(mean), round(ac)))
PY
    then
        fail_row " (profile extraction failed)"
    elif ! diff -u "$reference" "$measured" >"$TMP_DIR/ay_envelope.diff" 2>&1; then
        changed=$(grep -c '^[-+][0-9]' "$TMP_DIR/ay_envelope.diff" || true)
        fail_row " (audio content changed: $changed profile lines differ vs \
$reference — see the diff for which slices moved)"
    else
        pass_row " (all 16 AY envelope shapes match the reference DC/AC \
profile across 32 slices)"
    fi
fi
