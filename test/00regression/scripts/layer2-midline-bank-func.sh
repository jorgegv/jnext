#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #270 — a Copper MOVE to the Layer 2 bank register that lands PART-WAY
# along a scanline must take effect where it lands, not repaint the line.
#
# THE DEFECT THIS PINS. Layer2's per-scanline change log recorded only WHICH
# line each NR 0x12 write belonged to. The renderer replayed every entry for
# a line before drawing it, so when a line carried two writes the second won
# and the whole line came from one bank. Two MOVEs on one line is the normal
# way to put a strip of a different Layer 2 screen inside a line — next-point
# does it on each of the twelve lines of its advertising hoardings, which
# under the old code were drawn entirely from the crowd bank and never
# appeared at all.
#
# THE WORKLOAD is the reporter's own repro
# (https://github.com/vmorilla/jnext-copper-bug, cases/layer2-bank-midline)
# rebuilt as an injected binary: see bin/l2_midline_bank.asm for what it
# paints and the VHDL it is derived from. Bank 8 is solid red, bank 11 solid
# green, the ULA is off, and a Copper list switches NR 0x12 twice on each of
# lines 48..191 while lines 0..47 stay a one-write-per-frame control band.
#
# THE ORACLE IS NOT THIS EMULATOR. MAME 0.289 (`tbblue`) renders the same
# program with the band boundary at display pixel 67 — measured by the
# reporter at x = 460 of the 193..1215 display area of their capture. 67 is
# also what the VHDL says: WAIT hpos=8 has threshold (8<<3)+12 = 76
# (copper.vhd:94) in the hc_ula domain, the Copper->NextREG path adds three
# 28 MHz cycles (zxnext.vhd:4709-4731), layer2.vhd:110-122 resamples the bank
# one CLK_7 later still and layer2.vhd:145-148 runs the address one column
# ahead. The arithmetic is spelled out at seg_first_col() in
# src/video/layer2.cpp; this row is the end-to-end check on it.
#
# WHAT IS ASSERTED, in framebuffer columns of the 640-wide screenshot (the
# 256-mode display occupies 64..575, two cells per source pixel, so source
# column 67 starts at 64 + 2*67 = 198):
#   * control-band rows are ONE run of red across the whole display;
#   * banded rows are EXACTLY two runs, green then red, splitting at 198.
# Before the fix every banded row was a single run of green — not one pixel
# of bank 8 on any of the 144 lines — so both halves flip together and
# neither alone could pass on the broken build.
#
# 128K, not Next: the fixture programs every register it needs and wants no
# firmware underneath it, so this row costs a fast boot instead of a NextZXOS
# one. The Copper and Layer 2 are core hardware, present in every machine
# mode. Injected binary + fixed frame counts + no RTC = deterministic.
if want layer2-midline-bank-func; then
    begin_func layer2-midline-bank-func

    l2mb_fixture="$SCRIPT_DIR/bin/l2_midline_bank.bin"
    l2mb_png="$TMP_DIR/l2_midline_bank.png"
    rm -f "$l2mb_png"

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to read the screenshot back)"
    elif [[ ! -r "$l2mb_fixture" ]]; then
        fail_row " (fixture missing: $l2mb_fixture)"
    elif ! timeout --foreground --kill-after=5s 90s "$JNEXT" \
            --headless --machine 128k "${SD_CARD_ARGS[@]}" \
            --inject "$l2mb_fixture" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 \
            --delayed-screenshot "$l2mb_png" --delayed-screenshot-frames 200 \
            --delayed-automatic-exit-frames 220 &>/dev/null; then
        fail_row " (emulator run failed)"
    elif ! l2mb_out=$(python3 - "$l2mb_png" <<'PY'
import struct
import sys
import zlib

RED   = (255, 0, 0)     # Layer 2 pixel 0xE0 through the default palette
GREEN = (0, 255, 0)     # Layer 2 pixel 0x1C
DISP_L, DISP_R = 64, 575          # the 256-mode display strip, inclusive
SPLIT = 198                       # 64 + 2*67 — MAME's boundary column

# Framebuffer rows to read. Screenshots are 640x512: framebuffer row R is PNG
# rows 2R and 2R+1, and the 192-line display starts at framebuffer row 32.
# Copper line L is framebuffer row 32+L.
CONTROL = [32 + l for l in (8, 24, 46)]         # one-write-per-frame band
BANDED  = [32 + l for l in (60, 100, 143, 191)] # two writes per line

def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG: %s" % path)
    pos, idat = 8, b""
    width = height = depth = colour = None
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
        raise SystemExit("unexpected PNG format: depth=%s colour=%s" % (depth, colour))
    bpp = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        filt = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        if filt == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 0xFF
        elif filt == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif filt == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif filt == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif filt != 0:
            raise SystemExit("unknown PNG filter %d" % filt)
        rows.append(bytes(line)); prev = line
    return width, height, bpp, rows

width, height, bpp, rows = read_png(sys.argv[1])
if (width, height) != (640, 512):
    raise SystemExit("unexpected screenshot size %dx%d" % (width, height))

def runs(fb_row):
    """Colour runs across the display strip of one framebuffer row."""
    line = rows[fb_row * 2]
    out, last = [], None
    for x in range(DISP_L, DISP_R + 1):
        c = tuple(line[x * bpp:x * bpp + 3])
        if c != last:
            out.append((x, c)); last = c
    return out

bad = []
for r in CONTROL:
    got = runs(r)
    if got != [(DISP_L, RED)]:
        bad.append("control fb row %d: expected one red run, got %s" % (r, got))
for r in BANDED:
    got = runs(r)
    if got != [(DISP_L, GREEN), (SPLIT, RED)]:
        bad.append("banded fb row %d: expected green then red at %d, got %s"
                   % (r, SPLIT, got))

if bad:
    print("FAIL " + " | ".join(bad))
else:
    print("OK control band solid, %d banded rows split green/red at column %d"
          % (len(BANDED), SPLIT))
PY
    ); then
        fail_row " (screenshot analysis failed)"
    elif [[ "$l2mb_out" == FAIL* ]]; then
        fail_row " (${l2mb_out#FAIL })"
    else
        pass_row " (mid-line Copper NR 0x12: ${l2mb_out#OK })"
    fi
fi
