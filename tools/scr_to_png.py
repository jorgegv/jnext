#!/usr/bin/env python3
"""
Convert ZX Spectrum screen RAM (6912-byte raw .scr / .bin) to PNG.

Layout:
  $4000-$57FF (6144 bytes): pixel data, Spectrum's non-linear scan-row layout
  $5800-$5AFF (768 bytes):  attributes (32×24 character cells, FLASH/BRIGHT/PAPER[3]/INK[3])

Usage:
  scr_to_png.py <input.bin> <output.png>
  scr_to_png.py --batch <indir> <outdir>   (converts every .bin in indir)
"""
import sys
from pathlib import Path
import struct
import os

# Standard ZX Spectrum palette (bright=0 / bright=1), in RGB.
PALETTE = [
    # Normal brightness (bright=0)
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xCD), (0xCD, 0x00, 0x00), (0xCD, 0x00, 0xCD),
    (0x00, 0xCD, 0x00), (0x00, 0xCD, 0xCD), (0xCD, 0xCD, 0x00), (0xCD, 0xCD, 0xCD),
    # Bright (bright=1)
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xFF), (0xFF, 0x00, 0x00), (0xFF, 0x00, 0xFF),
    (0x00, 0xFF, 0x00), (0x00, 0xFF, 0xFF), (0xFF, 0xFF, 0x00), (0xFF, 0xFF, 0xFF),
]


def pixel_addr(x, y):
    """Z80 logical pixel address offset (within $4000) for (x, y) — Spectrum layout."""
    # y is 0..191; x is 0..255.
    # Spectrum scan-row layout:
    # offset bits: 010 Y[7] Y[6] Y[2:0] Y[5:3] X[7:3]
    third = y >> 6
    char_row_in_third = (y >> 3) & 0x07
    pixel_row = y & 0x07
    char_col = x >> 3
    offset = (third << 11) | (pixel_row << 8) | (char_row_in_third << 5) | char_col
    return offset


def attr_addr(x, y):
    """Offset within $5800 for attribute byte at (x, y)."""
    char_col = x >> 3
    char_row = y >> 3
    return char_row * 32 + char_col


def render(scr):
    """Render 6912-byte screen RAM to 256×192 RGB list-of-rows."""
    assert len(scr) == 6912, f"expected 6912 bytes, got {len(scr)}"
    pixels = scr[:6144]
    attrs = scr[6144:]
    rgba = bytearray(256 * 192 * 3)
    for y in range(192):
        for x in range(256):
            poff = pixel_addr(x, y)
            byte = pixels[poff]
            bit = 7 - (x & 7)
            on = (byte >> bit) & 1
            aoff = attr_addr(x, y)
            attr = attrs[aoff]
            bright = (attr >> 6) & 1
            paper = (attr >> 3) & 7
            ink = attr & 7
            color_idx = ink if on else paper
            color_idx += bright * 8
            r, g, b = PALETTE[color_idx]
            i = (y * 256 + x) * 3
            rgba[i] = r
            rgba[i + 1] = g
            rgba[i + 2] = b
    return bytes(rgba), 256, 192


def write_png(rgb_bytes, width, height, path):
    """Minimal PNG writer (stdlib only) — uncompressed IDAT for simplicity."""
    import zlib
    sig = b'\x89PNG\r\n\x1a\n'

    def chunk(name, data):
        length = struct.pack('>I', len(data))
        block = name + data
        crc = struct.pack('>I', zlib.crc32(block) & 0xFFFFFFFF)
        return length + block + crc

    # IHDR: width, height, bit depth=8, color type=2 (RGB), compression=0,
    # filter=0, interlace=0
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)

    # Scanlines: each row prefixed by filter type 0 (None).
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgb_bytes[y * width * 3:(y + 1) * width * 3])
    idat = zlib.compress(bytes(raw), level=6)

    with open(path, 'wb') as f:
        f.write(sig)
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', idat))
        f.write(chunk(b'IEND', b''))


def convert_one(in_path, out_path):
    with open(in_path, 'rb') as f:
        data = f.read()
    if len(data) < 6912:
        print(f"  WARN: {in_path} is only {len(data)} bytes; padding with zeros")
        data = data + bytes(6912 - len(data))
    elif len(data) > 6912:
        data = data[:6912]
    rgb, w, h = render(data)
    write_png(rgb, w, h, out_path)


def main():
    args = sys.argv[1:]
    if len(args) == 2:
        convert_one(args[0], args[1])
        return 0
    if len(args) == 3 and args[0] == '--batch':
        indir = Path(args[1])
        outdir = Path(args[2])
        outdir.mkdir(parents=True, exist_ok=True)
        for f in sorted(indir.iterdir()):
            if f.suffix in ('.bin', '.scr'):
                out = outdir / (f.stem + '.png')
                try:
                    convert_one(f, out)
                    print(f"  {f.name} -> {out.name}")
                except Exception as e:
                    print(f"  {f.name}: ERR {e}")
        return 0
    print(__doc__, file=sys.stderr)
    return 1


if __name__ == '__main__':
    raise SystemExit(main())
