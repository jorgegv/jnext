#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# wav-stems-func: `--wav-record-stems DIR` writes seven valid stem WAVs that add up to the mix and
# that survive a cold boot (the selector chain-loading red.nex), next to `--wav-record`.
#   leg 1   --silent is refused with the message (the mixer does not run).
#   leg 2   menu.nex chain-loads red.nex with --wav-record W and --wav-record-stems DIR/new/sub (DIR is
#           created, nested). Then: seven 44.1 kHz stereo 16-bit files of one length, more than 150000
#           frames; mix.wav byte-identical to W (stems and --wav-record see the same samples across the
#           cold boot, none dropped or added); for every frame and channel |sum of the six source
#           stems - mix| <= 3 (the mixer's seven roundings, see the developer guide), the maximum
#           printed; pi.wav all zero (no Pi attached: the I2S term sits at its rest level); ay0, ay1 and ay2 all zero (the
#           fixture plays no AY: the plan's ay1/ay2-only check, widened to the three files after measuring ay0 silent too).
if want wav-stems-func; then
    begin_func wav-stems-func
    ws_dir="$TMP_DIR/wav-stems"
    rm -rf "$ws_dir"; mkdir -p "$ws_dir"
    ws_wav="$ws_dir/direct.wav"
    ws_stems="$ws_dir/new/sub"
    menu_nex="$PROJECT_DIR/test/00regression/nex/menu.nex"
    if reject_out=$("$JNEXT" --headless --silent --wav-record-stems "$ws_stems" 2>&1); then
        reject_status=0
    else
        reject_status=$?
    fi
    out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$menu_nex" \
        --wav-record "$ws_wav" --wav-record-stems "$ws_stems" --delayed-keypress-frames 100 1 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    chained=$(echo "$out" | grep -cE "NEX: loaded '.*red\.nex'" || true)
    if [[ "$reject_status" -eq 0 ]] ||
       ! grep -q -- "--wav-record-stems cannot be combined with --silent" <<<"$reject_out"; then
        fail_row " (--silent conflict was not rejected clearly)"
        show_output "--silent run" "$reject_out"
    elif [[ "$chained" -lt 1 ]]; then
        fail_row " (selector did not chain-load red.nex)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for WAV validation)"
    elif ws_out=$(python3 - "$ws_wav" "$ws_stems" <<'PY'
import array
import struct
import sys

wav_path, stems = sys.argv[1], sys.argv[2]
names = ["ay0", "ay1", "ay2", "dac", "beeper", "pi", "mix"]

def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE" or data[36:40] != b"data":
        raise SystemExit(f"{path}: invalid WAV container")
    rate, channels, bits, payload = (
        struct.unpack_from("<I", data, 24)[0],
        struct.unpack_from("<H", data, 22)[0],
        struct.unpack_from("<H", data, 34)[0],
        struct.unpack_from("<I", data, 40)[0],
    )
    if (rate, channels, bits) != (44100, 2, 16):
        raise SystemExit(f"{path}: unexpected format {rate} Hz/{channels} ch/{bits} bit")
    if payload != len(data) - 44:
        raise SystemExit(f"{path}: header says {payload} data bytes, file holds {len(data) - 44}")
    pcm = array.array("h")
    pcm.frombytes(data[44:])
    return data, pcm

stem = {}
raw = {}
for n in names:
    raw[n], stem[n] = load(f"{stems}/{n}.wav")
frames = len(stem["mix"]) // 2
if frames < 150000:
    raise SystemExit(f"stems stopped early ({frames} frames)")
for n in names:
    if len(stem[n]) != len(stem["mix"]):
        raise SystemExit(f"{n}.wav holds {len(stem[n]) // 2} frames, mix.wav {frames}")
with open(wav_path, "rb") as f:
    if f.read() != raw["mix"]:
        raise SystemExit("mix.wav differs from the --wav-record file")
worst = 0
for i in range(len(stem["mix"])):
    s = sum(stem[n][i] for n in names[:6])
    worst = max(worst, abs(s - stem["mix"][i]))
if worst > 3:
    raise SystemExit(f"the six source stems differ from the mix by {worst} (bound 3)")
if any(stem["pi"]):
    raise SystemExit("pi.wav is not silent without a Pi")
# The selector and red.nex play no AY: every AY chip is silent (measured on this fixture: all three files are 0 throughout).
for n in ("ay0", "ay1", "ay2"):
    if any(stem[n]):
        raise SystemExit(f"{n}.wav is not silent: the fixture programs no AY")
print(f"{frames} frames across the selector cold boot, mix.wav == --wav-record, max |sum - mix| = {worst}")
PY
    ); then
        pass_row " ($ws_out)"
    else
        fail_row " ($ws_out)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
