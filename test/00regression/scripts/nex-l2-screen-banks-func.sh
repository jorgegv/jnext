#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #573 - where a classic Layer 2 loading screen lands under `--load`.
# nexload.asm (:100-103, :441-443) reads the 48 KB screen into banks 9,10,11
# and shows bank 9 (NR 0x12, :367); jnext used to put it in banks 8,9,10, so
# the displayed Layer 2 was shifted by a third. The probe NEX, generated below
# at run time (V1.2, LOADSCR 0x81 = Layer 2 without palette, thirds 0xE0 /
# 0x1C / 0x03 = red / green / blue in the default palette, bank 2 = `di ; jr $`),
# is shown in that order. The expectation was measured on the real chain:
# NextZXOS booted, Command Line, `.nexload` of the same probe (2026-11-27):
# pixels (100,70) (100,200) (100,330) of the 640x512 screenshot are FF0000,
# 00FF00, 0000FF. Before the fix they were 00FF00, 0000FF, 000000.
if want nex-l2-screen-banks-func; then
    begin_func nex-l2-screen-banks-func
    l2_nex="$TMP_DIR/l2-probe.nex"
    l2_shot="$TMP_DIR/l2-probe.png"
    python3 -I - "$l2_nex" <<'PY'
import struct
import sys

h = bytearray(512)
h[0:4] = b'Next'
h[4:8] = b'V1.2'
h[9] = 1                         # num_banks
h[10] = 0x81                     # Layer 2 screen, no palette
struct.pack_into('<H', h, 12, 0xBFF0)
struct.pack_into('<H', h, 14, 0x8000)
h[18 + 2] = 1                    # bank 2 only
screen = bytes([0xE0]) * 16384 + bytes([0x1C]) * 16384 + bytes([0x03]) * 16384
bank2 = bytes([0xF3, 0x18, 0xFE]) + bytes(0x4000 - 3)   # di ; jr $
open(sys.argv[1], 'wb').write(bytes(h) + screen + bank2)
PY
    l2_faults=()
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --load "$l2_nex" \
        --delayed-screenshot "$l2_shot" --delayed-screenshot-frames 40 \
        --delayed-automatic-exit-frames 50 >/dev/null 2>&1 || true
    if [[ -f "$l2_shot" ]]; then
        if command -v magick &>/dev/null; then l2_img=(magick); else l2_img=(convert); fi
        for l2_case in "100,70:FF0000" "100,200:00FF00" "100,330:0000FF"; do
            l2_xy=${l2_case%%:*}
            l2_want=${l2_case##*:}
            l2_px=$("${l2_img[@]}" "$l2_shot" -format "%[hex:p{${l2_xy}}]" info: 2>/dev/null || echo "unreadable")
            [[ "$l2_px" == "$l2_want" ]] \
                || l2_faults+=("pixel ($l2_xy) is $l2_px, want $l2_want")
        done
    else
        l2_faults+=("no screenshot was written")
    fi
    if [[ ${#l2_faults[@]} -eq 0 ]]; then
        pass_row " (the Layer 2 loading screen shows red, green, blue thirds in order)"
    else
        fail_row " ($(IFS='; '; echo "${l2_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
