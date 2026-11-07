#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #454 — what a NEX load leaves of bank 5 on the warm start, end to end.
# A `--load` on a Next applies the NEX on top of a recorded, resident
# NextZXOS, so bank 5 holds its system variables when apply() runs. The loader
# used to zero all 16 KB of it; neither nexload.asm (:319-320: border 0 and the
# attributes $5800-$5AFF only) nor nexload2.asm does. The probe NEX, generated
# below at run time (a V1.1 file, header border 5, no screen, only bank 2),
# dumps bank 5 to the magic port at entry and spins. Four assertions, each red
# when apply() zeroes bank 5 or sets the border from the header:
#
#   * CHARS ($5C36/7) = $3C00: a system variable the ROM and NextZXOS set;
#   * $5B00-$5BFF is not all zero (the NextZXOS paging stubs and sysvars);
#   * the attributes $5800-$5AFF are all zero (nexload.asm:320);
#   * the border pixel (2,2) of a screenshot is black (nexload.asm:319; the
#     header says 5 = cyan, written only when a screen is loaded, :424-425).
#
# Expectations were measured on the real chain: NextZXOS booted, Command Line,
# `.nexload` of the same probe. Deliberately no ROM printing: that still hangs
# from a warm start for a different reason (the recording is taken at the menu).
if want nex-bank5-warm-func; then
    begin_func nex-bank5-warm-func
    nb_nex="$TMP_DIR/bank5-probe.nex"
    nb_shot="$TMP_DIR/bank5-probe.png"
    python3 - "$nb_nex" <<'PY'
import struct
import sys

code = bytearray()
def e(*b):
    code.extend(b)

e(0xF3)                          # di
e(0x01, 0xFE, 0xCA)              # ld bc,$cafe
for m in (0xA5, 0x5A, 0xA5, 0x5A):
    e(0x3E, m, 0xED, 0x79)       # leading marker
e(0x21, 0x00, 0x40)              # ld hl,$4000
loop = 0x8000 + len(code)
e(0x7E)                          # ld a,(hl)
e(0xED, 0x79)                    # out (c),a
e(0x23)                          # inc hl
e(0x7C)                          # ld a,h
e(0xFE, 0x80)                    # cp $80
e(0x20, (loop - (0x8000 + len(code) + 2)) & 0xFF)   # jr nz,loop
for m in (0x5A, 0xA5, 0x5A, 0xA5):
    e(0x3E, m, 0xED, 0x79)       # trailing marker
e(0x18, 0xFE)                    # jr $

h = bytearray(512)
h[0:4] = b'Next'
h[4:8] = b'V1.1'
h[9] = 1                         # num_banks
h[10] = 0                        # no loading screen
h[11] = 5                        # header border: cyan
struct.pack_into('<H', h, 12, 0xBFF0)
struct.pack_into('<H', h, 14, 0x8000)
h[18 + 2] = 1                    # bank 2 only
open(sys.argv[1], 'wb').write(bytes(h) + bytes(code) + bytes(0x4000 - len(code)))
PY
    nb_faults=()
    nb_log=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --load "$nb_nex" \
        --magic-port 0xCAFE --magic-port-mode hex \
        --delayed-screenshot "$nb_shot" --delayed-screenshot-frames 40 \
        --delayed-automatic-exit-frames 50 2>&1 || true)
    nb_verdict=$(NB_LOG="$nb_log" python3 -I -c '
import os, sys
hx = [int(l, 16) for l in (x.strip() for x in os.environ["NB_LOG"].splitlines())
      if len(l) == 2 and all(c in "0123456789ABCDEFabcdef" for c in l)]
start = None
for i in range(len(hx) - 3):
    if hx[i:i + 4] == [0xA5, 0x5A, 0xA5, 0x5A]:
        start = i + 4
        break
if start is None or len(hx) < start + 16384:
    print("no bank-5 dump in the output")
    sys.exit(0)
d = bytes(hx[start:start + 16384])
faults = []
chars = d[0x5C36 - 0x4000] | d[0x5C37 - 0x4000] << 8
if chars != 0x3C00:
    faults.append("CHARS=%#06x, want 0x3c00 (the system variables were wiped)" % chars)
if not any(d[0x5B00 - 0x4000:0x5C00 - 0x4000]):
    faults.append("$5B00-$5BFF is all zero")
if any(d[0x5800 - 0x4000:0x5B00 - 0x4000]):
    faults.append("the attributes $5800-$5AFF are not all zero")
print("; ".join(faults))
')
    [[ -z "$nb_verdict" ]] || nb_faults+=("$nb_verdict")
    if [[ -f "$nb_shot" ]]; then
        if command -v magick &>/dev/null; then nb_img=(magick); else nb_img=(convert); fi
        nb_px=$("${nb_img[@]}" "$nb_shot" -format '%[hex:p{2,2}]' info: 2>/dev/null || echo "unreadable")
        [[ "$nb_px" == "000000" ]] \
            || nb_faults+=("border pixel (2,2) is $nb_px, want 000000 (header border 5 applied without a loading screen)")
    else
        nb_faults+=("no screenshot was written")
    fi
    if [[ ${#nb_faults[@]} -eq 0 ]]; then
        pass_row " (warm --load keeps the system variables, clears only the attributes, border stays black)"
    else
        fail_row " ($(IFS='; '; echo "${nb_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
