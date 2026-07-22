#!/usr/bin/env python3
"""Independently derive the expected lores-demo frame and diff it against a PNG.

Run by hand; NOT wired into the regression harness.

    python3 demo/lores_demo/derive-lores-reference.py \
            test/00regression/img/lores-demo-reference.png

Why this exists.  A screenshot row's expected value IS its reference PNG, so
generating one by running jnext and committing whatever appears blesses the
implementation instead of testing it.  This script computes the frame from the
LoRes VHDL formulas and the fixture's known bank-5 content ALONE -- it never
looks at src/video/lores.* -- so the committed reference can be justified
rather than assumed.

The chain, all of it citable:

  lores.vhd:82        x = phc + scroll_x            (scroll_x = 0 here)
  lores.vhd:84-87     y = (vc + scroll_y) mod 192   (scroll_y = 0 here)
  lores.vhd:91,93-94  addr = y(7:1):x(7:1), +0x800 on bits 13:11 when y >= 96
  lores.vhd:102,111   pixel = ((data(7:4) + offset) & 0xF) : data(3:0),
                      offset = 0  =>  pixel = data
  zxnext.vhd:6981     the pixel is an 8-bit index into the ULA palette
  lores.vhd:115       pixel_en only inside phc in [0,255] and vc in [0,191],
                      so LoRes never touches the border

and the fixture writes byte(cx,cy) = ((cy>>3)<<4) | (cx>>3) with the ULA
palette loaded as the identity, palette[i] = RRRGGGBB i.  Composing those:

  colour(framebuffer display column dx, raster row vc)
      = expand8( (((vc>>1)>>3) << 4) | ((dx>>2)>>3) )

(dx>>2 because one phc covers two of the 640-wide framebuffer's cells, and
one LoRes pixel covers two phc.)

The addr formula is therefore NOT re-implemented here: for this fixture it
cancels out.  Whatever address the renderer computes, it must land on the byte
the derivation names, and only a correct address generator does.  That is the
whole assertion, and it holds without this script knowing how the address is
built -- which is exactly what makes it independent.

The ONE thing borrowed from the emulator rather than derived is the
RRRGGGBB -> RGB333 -> RGB888 expansion (PaletteManager::rrrgggbb_to_rgb333 /
rgb333_to_argb8888).  That is the palette subsystem's contract, not LoRes's,
and it is covered by the palette suites; it is spelled out below so the
borrowing is visible rather than implicit.

Framebuffer geometry (src/video/renderer.h, src/platform/screenshot.cpp):
640x256 ARGB, display area x in [64,575] / y in [32,223]; the PNG is 640x512
because each framebuffer row is written twice.
"""

import sys
import zlib
import struct

# --- framebuffer / PNG geometry ---
FB_W, FB_H = 640, 256
DISP_X, DISP_Y, DISP_W, DISP_H = 64, 32, 512, 192
PNG_W, PNG_H = FB_W, FB_H * 2


def expand8(idx):
    """RRRGGGBB palette byte -> (r, g, b) 8-bit, per PaletteManager."""
    r3 = (idx >> 5) & 7
    g3 = (idx >> 2) & 7
    b2 = idx & 3
    b3 = ((b2 << 1) | ((b2 >> 1) | (b2 & 1))) & 7          # B0 = B1 or B0
    exp = lambda c: ((c << 5) | (c << 2) | (c >> 1)) & 0xFF  # noqa: E731
    return exp(r3), exp(g3), exp(b3)


def expected_display_colour(fb_col, vc):
    """The colour the display pixel at framebuffer column `fb_col` (0-based
    within the 512-cell display area) and raster row `vc` must carry.

    jnext's canonical framebuffer is 640 wide with a 512-cell display area, so
    ONE phc covers TWO adjacent cells (both get the same LoRes colour --
    Renderer::apply_lores writes dst[0] and dst[1], the Timex-hi-res behaviour
    of zxnext.vhd:6843 vs 6858, plan row LR-28).
    """
    phc = fb_col >> 1      # 2 framebuffer cells per phc
    cx = phc >> 1          # lores.vhd:91 -- x(7:1): 2 display px per LoRes px
    cy = vc >> 1           # lores.vhd:91 -- y(7:1)
    byte = ((cy >> 3) << 4) | (cx >> 3)   # the fixture's bank-5 content
    return expand8(byte)                  # offset 0 => index == byte


def read_png_rgb(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat, hdr = 8, b'', None
    while pos < len(data):
        ln, = struct.unpack('>I', data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            hdr = struct.unpack('>IIBBBBB', body)
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    w, h, depth, ctype = hdr[0], hdr[1], hdr[2], hdr[3]
    assert (depth, ctype) == (8, 2), f'expected 8-bit RGB, got depth={depth} ctype={ctype}'
    raw = zlib.decompress(idat)
    stride = w * 3
    out, prev = [], bytearray(stride)
    p = 0
    for _ in range(h):
        filt = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if filt == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filt == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filt == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif filt == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out.append(bytes(line))
        prev = line
    return w, h, out


def main(path):
    w, h, rows = read_png_rgb(path)
    if (w, h) != (PNG_W, PNG_H):
        print(f'FAIL: PNG is {w}x{h}, expected {PNG_W}x{PNG_H}')
        return 1

    px = lambda x, y: tuple(rows[y][x * 3:x * 3 + 3])  # noqa: E731

    bad_display = 0
    first_bad = None
    for fby in range(FB_H):
        for fbx in range(FB_W):
            inside = (DISP_X <= fbx < DISP_X + DISP_W
                      and DISP_Y <= fby < DISP_Y + DISP_H)
            if not inside:
                continue
            want = expected_display_colour(fbx - DISP_X, fby - DISP_Y)
            for sub in (0, 1):                 # the PNG doubles every fb row
                got = px(fbx, fby * 2 + sub)
                if got != want:
                    bad_display += 1
                    if first_bad is None:
                        first_bad = (fbx, fby * 2 + sub, got, want)

    # The border must be one uniform colour: LoRes is clipped to the 256x192
    # display area by lores.vhd:115 alone, so not one LoRes pixel may reach it
    # (plan row LR-22).
    border = set()
    for fby in range(FB_H):
        for fbx in range(FB_W):
            if (DISP_X <= fbx < DISP_X + DISP_W
                    and DISP_Y <= fby < DISP_Y + DISP_H):
                continue
            border.add(px(fbx, fby * 2))
            border.add(px(fbx, fby * 2 + 1))

    n_display = DISP_W * DISP_H * 2
    print(f'display area : {n_display - bad_display}/{n_display} pixels match the derivation')
    if first_bad:
        x, y, got, want = first_bad
        print(f'  first mismatch at PNG ({x},{y}): got {got}, derived {want}')
    print(f'border       : {len(border)} distinct colour(s) {sorted(border)}')

    ok = (bad_display == 0 and len(border) == 1)
    print('RESULT: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1
                  else 'test/00regression/img/lores-demo-reference.png'))
