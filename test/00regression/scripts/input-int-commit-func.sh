#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #450 — a key reaches the guest at the frame interrupt, not before it.
#
# THE DEFECT THIS PINS. jnext applied every key (host or --delayed-keypress) at
# its frame boundary, 292 T before the 128K frame interrupt. A game that pages
# a second bank in with interrupts enabled the moment it sees a key (Ms. Nam-Pac
# does, for its music) then had the paged window straddle the interrupt, read
# the IM 2 vector from the wrong bank and jumped to $0000: a guest reset on
# every start of play. FUSE (spectrum.c:91-103) runs the interrupt first and
# host input after it, and so does jnext now.
#
# THE WORKLOAD is bin/gh450_bank_int.asm, our own code reproducing that
# mechanism with no third-party bytes: border GREEN when the paged window ended
# cleanly, RED when the vector was read from the wrong bank. The key arrives by
# --delayed-keypress-frames at three successive frames.
if want input-int-commit-func; then
    begin_func input-int-commit-func

    iic_fixture="$SCRIPT_DIR/bin/gh450_bank_int.bin"
    iic_bad=""
    iic_seen=""

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to read the screenshot back)"
    elif [[ ! -r "$iic_fixture" ]]; then
        fail_row " (fixture missing: $iic_fixture)"
    else
        for iic_frame in 150 151 152; do
            iic_png="$TMP_DIR/input_int_commit_$iic_frame.png"
            rm -f "$iic_png"
            if ! timeout --foreground --kill-after=5s 90s "$JNEXT" \
                    --headless --machine 128k "${SD_CARD_ARGS[@]}" \
                    --inject "$iic_fixture" --inject-org 8000 --inject-pc 8000 \
                    --inject-delay 100 \
                    --delayed-keypress-frames "$iic_frame" SPACE \
                    --delayed-screenshot "$iic_png" --delayed-screenshot-frames 300 \
                    --delayed-automatic-exit-frames 310 &>/dev/null; then
                iic_bad+=" frame $iic_frame: emulator run failed;"
                continue
            fi
            if ! iic_px=$(python3 - "$iic_png" <<'PY'
import struct
import sys
import zlib

def border_pixel(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG: %s" % path)
    pos, idat = 8, b""
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack_from(">IIBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if depth != 8 or colour not in (2, 6):
        raise SystemExit("unexpected PNG format")
    bpp = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    # Row 0 only: its filter has no row above it (prior row is all zero).
    filt = raw[0]
    line = bytearray(raw[1:1 + width * bpp])
    if filt == 1:
        for x in range(bpp, len(line)):
            line[x] = (line[x] + line[x - bpp]) & 0xFF
    elif filt in (3, 4):
        for x in range(bpp, len(line)):
            a = line[x - bpp]
            line[x] = (line[x] + (a >> 1 if filt == 3 else a)) & 0xFF
    elif filt not in (0, 2):
        raise SystemExit("unknown PNG filter %d" % filt)
    return tuple(line[5 * bpp:5 * bpp + 3])

print("%d,%d,%d" % border_pixel(sys.argv[1]))
PY
            ); then
                iic_bad+=" frame $iic_frame: screenshot unreadable;"
                continue
            fi
            iic_seen+=" $iic_frame=($iic_px)"
            # GREEN is (0,182,0); RED (182,0,0) means the vector came from bank 6.
            if [[ "$iic_px" != "0,182,0" ]]; then
                iic_bad+=" frame $iic_frame: border ($iic_px), not green;"
            fi
        done
        if [[ -n "$iic_bad" ]]; then
            fail_row " ($iic_bad)"
        else
            pass_row " (key at the interrupt: paged-bank window survives, border green:$iic_seen)"
        fi
    fi
fi
