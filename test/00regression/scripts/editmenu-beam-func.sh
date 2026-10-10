#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #305 — the NextZXOS EDIT menu appeared "interleaved" (alternating grey and
# white rows, then new glyphs on the old background) while it was drawn.
#
# TWO DEFECTS, both in how a screen write is timed against the beam:
#   A. a CPU write's beam position was computed as if the CPU ran at 3.5 MHz;
#      NextZXOS runs at 28 MHz (NR 0x07 = 0x33), so every attribute write was
#      tagged on a scattered row;
#   B. the ULA's pixel bytes were read at the END of the frame while its
#      attribute bytes were replayed at beam time, so a frame could show the
#      new glyphs on the old attributes. The VHDL fetches both at beam time
#      (zxula.vhd:226-303).
#
# THE ORACLE IS NOT THIS EMULATOR'S RENDERER. The row runs the real NextZXOS
# to the NextBASIC editor, presses CAPS+1 (EDIT) and logs, through the debugger
# script engine, EVERY write to the bank-5 pages with its raster position
# (VC_ULA, HC_ULA) and its frame. It then rebuilds each captured frame's paper
# area in Python from that write log alone, with the VHDL fetch rule:
#
#   zxula.vhd:270-303 — the byte of display column C is latched, in hc_ula
#   ticks of line L, at   attribute: 8C+11 (C even) / 8C+7 (C odd)
#                         pixel:     the attribute instant - 2
#   (abyte00 @B / abyte10 @F; pbyte00 @9 / pbyte10 @D.) A write is seen by a
#   fetch when it happened at or before it. The paper area has no scroll and
#   no Timex mode here, so the primary fetches are all that matter.
#
# and requires the emulator's screenshot of every frame to equal that model.
# The write log is cross-checked against the .scr VRAM dumps (start-of-frame
# VRAM + the frame's writes == end-of-frame VRAM) so a log that missed a write
# cannot make a wrong model look right.
#
# NON-VACUITY. The row asserts that (a) the CPU really runs at 28 MHz in the
# captured frames (NR 0x07 bits 1:0 = 3), and (b) at least one captured frame
# is one where the beam model differs from "pixels read at the end of the
# frame" — i.e. the frame genuinely contains a write that raced the beam. On
# the broken emulator the model comparison fails thousands of pixels in the
# stripe frames (defect A) and hundreds in the frames with new glyphs on old
# attributes (defect B).
#
# Deterministic: injected keys on fixed frame numbers, --rtc pinned, headless.
if want editmenu-beam-func; then
    begin_func editmenu-beam-func

    emb_dir="$TMP_DIR/editmenu-beam"
    emb_log="$emb_dir/run.log"
    mkdir -p "$emb_dir"

    # First/last frame whose paper area is checked; the script captures from
    # EMB_LO-3 so each checked frame has its start-of-frame VRAM dump.
    emb_lo=641
    emb_hi=646
    {
        echo 'on frame 400 do press "space" for 3 end'
        echo 'on frame 470 do press "down" for 3 end'
        echo 'on frame 490 do press "down" for 3 end'
        echo 'on frame 510 do press "enter" for 3 end'
        echo 'on frame 640 do press "caps+1" for 3 end'
        for ((emb_f = emb_lo - 3; emb_f <= emb_hi - 1; emb_f++)); do
            echo "on frame $emb_f do"
            echo " screenshot \"$emb_dir/p$emb_f.png\""
            echo " screenshot \"$emb_dir/s$emb_f.scr\""
            echo " log \"NR07 \${nextreg[0x07]:x2}\""
            echo "end"
        done
        echo "on write page 10..11 when FRAME >= $((emb_lo - 3)) and FRAME <= $((emb_hi)) do"
        echo ' log "W ${FRAME:d} ${VC_ULA:d} ${HC_ULA:d} ${PAGE:d} ${ADDR:x4} ${VALUE:x2}"'
        echo "end"
        echo "on frame $((emb_hi + 3)) do exit 0 end"
    } > "$emb_dir/capture.jds"

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to model the frames)"
    elif ! timeout --foreground --kill-after=5s 240s "$JNEXT" \
            --headless --machine next "${SD_CARD_ARGS[@]}" \
            --rtc 2026-07-10T08:55:00 --script "$emb_dir/capture.jds" \
            > "$emb_log" 2>&1; then
        fail_row " (emulator run failed)"
    elif ! emb_out=$(python3 -I - "$emb_dir" "$emb_lo" "$emb_hi" <<'PY'
import re
import struct
import sys
import zlib
from collections import defaultdict

run, LO, HI = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])

# Raster geometry of the 50 Hz machine in the debugger's (VC_ULA, HC_ULA)
# coordinates: a frame's writes are ordered by (vc - VC0) mod LPF lines, VC0
# being the raster line the FRAME counter ticks over on.
VC0, LPF, LW = 247, 311, 456

def tkey(vc, hc):
    return ((vc - VC0) % LPF) * LW + hc

log = open(run + "/run.log", errors="replace").read().splitlines()

nr07 = []
writes = defaultdict(list)
for line in log:
    m = re.search(r"\] NR07 ([0-9a-f]{2})", line)
    if m:
        nr07.append(int(m.group(1), 16))
    m = re.search(r"\] W (\d+) (\d+) (\d+) \d+ ([0-9A-F]{4}) ([0-9A-F]{2})", line)
    if m:
        fr, vc, hc, ad, val = m.groups()
        a = int(ad, 16)
        if 0x4000 <= a <= 0x5AFF:
            writes[int(fr)].append((tkey(int(vc), int(hc)), a - 0x4000, int(val, 16)))

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
        raise SystemExit("unexpected PNG format")
    bpp = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        filt = raw[pos]; pos += 1
        ln = bytearray(raw[pos:pos + stride]); pos += stride
        if filt == 1:
            for x in range(bpp, stride):
                ln[x] = (ln[x] + ln[x - bpp]) & 0xFF
        elif filt == 2:
            for x in range(stride):
                ln[x] = (ln[x] + prev[x]) & 0xFF
        elif filt == 3:
            for x in range(stride):
                a = ln[x - bpp] if x >= bpp else 0
                ln[x] = (ln[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif filt == 4:
            for x in range(stride):
                a = ln[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                ln[x] = (ln[x] + pr) & 0xFF
        elif filt != 0:
            raise SystemExit("unknown PNG filter %d" % filt)
        rows.append(bytes(ln)); prev = ln
    return width, height, bpp, rows

def scr(n):
    return bytearray(open("%s/s%d.scr" % (run, n), "rb").read())

def level(c):
    # ZX colour channel: 0 = off, ~0xD7 = normal, 0xFF = bright. Only the
    # on/off structure and brightness class matter, not the exact palette.
    return 0 if c < 0x40 else (1 if c < 0xE8 else 2)

def zx_rgb(attr_colour, bright):
    r = (attr_colour >> 1) & 1; g = (attr_colour >> 2) & 1; b = attr_colour & 1
    k = 2 if bright else 1
    return (level_of(r, k), level_of(g, k), level_of(b, k))

def level_of(on, k):
    return k if on else 0

def paper_cell(png_rows, bpp, x, y):
    # Framebuffer pixels are doubled; the display starts at fb (64/2, 32).
    px = 64 + 2 * x + 1
    py = 64 + 2 * y + 1
    r = png_rows[py]
    return tuple(level(r[px * bpp + i]) for i in range(3))

bad = []
raced = 0
for F in range(LO, HI + 1):
    # Script frame N's screenshot is the frame whose writes are logged as
    # FRAME N+1; sN.scr is the VRAM at its end.
    start, end = scr(F - 2), scr(F - 1)
    w = sorted(writes[F])
    chk = bytearray(start)
    for _t, o, v in w:
        chk[o] = v
    if chk != end:
        bad.append("frame %d: write log does not reproduce the VRAM dump (%d bytes)"
                   % (F, sum(1 for i in range(6912) if chk[i] != end[i])))
        continue
    per = defaultdict(list)
    for t, o, v in w:
        per[o].append((t, v))

    def at(off, t):
        v = start[off]
        for tw, vv in per.get(off, ()):
            if tw > t:
                break
            v = vv
        return v

    width, height, bpp, rows = read_png("%s/p%d.png" % (run, F - 1))
    if (width, height) != (640, 512):
        bad.append("frame %d: unexpected screenshot size %dx%d" % (F, width, height))
        continue
    diff_beam = diff_end = 0
    for y in range(192):
        pa = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2)
        aa = 0x1800 + (y >> 3) * 32
        for c in range(32):
            ta = tkey(y, 0) + 8 * c + (7 if c & 1 else 11)
            a = at(aa + c, ta)
            p_beam = at(pa + c, ta - 2)
            p_end = end[pa + c]
            ink, paper, bright = a & 7, (a >> 3) & 7, bool(a & 0x40)
            for b in range(8):
                x = c * 8 + b
                got = paper_cell(rows, bpp, x, y)
                for p, which in ((p_beam, "beam"), (p_end, "end")):
                    col = ink if p & (0x80 >> b) else paper
                    exp = zx_rgb(col, bright)
                    if exp != got:
                        if which == "beam":
                            diff_beam += 1
                        else:
                            diff_end += 1
    if diff_beam:
        bad.append("frame %d: %d pixels differ from the beam model" % (F, diff_beam))
    if diff_end:
        raced += 1

if not nr07 or any((v & 3) != 3 for v in nr07):
    bad.append("NR 0x07 was not 28 MHz in the captured frames: %s"
               % sorted(set("%02x" % v for v in nr07)))
if raced == 0:
    bad.append("vacuous: no captured frame differs between the beam model and "
               "end-of-frame pixels, so no write raced the beam")

if bad:
    print("FAIL " + " | ".join(bad))
else:
    print("OK frames %d-%d equal the VHDL beam model; %d of them differ from "
          "end-of-frame pixels; CPU at 28 MHz" % (LO, HI, raced))
PY
    ); then
        fail_row " (analysis failed)"
    elif [[ "$emb_out" == FAIL* ]]; then
        fail_row " (${emb_out#FAIL })"
    else
        pass_row " (EDIT menu: ${emb_out#OK })"
    fi
fi
