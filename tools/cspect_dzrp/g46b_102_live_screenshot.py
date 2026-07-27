#!/usr/bin/env python3
"""G46(b) #102: render a PNG from CSpect's live ULA screen memory (0x4000-
0x5AFF) via DZRP read_mem, WITHOUT pausing the CPU — used to visually verify
the G46b102KeyInject.dll browser-navigation schedule while CSpect runs
headless (no window in this sandbox's Xvfb). One-shot: connect, read, write
PNG, disconnect.

Usage: g46b_102_live_screenshot.py OUTFILE.png
"""
import sys
import struct
sys.path.insert(0, "/home/jorgegv/src/spectrum/jnext/tools/cspect_dzrp")
from cspect_dzrp import CSpectDZRP

# Standard 8-colour ZX Spectrum palette (normal / bright), RGB.
PALETTE = [
    (0, 0, 0), (0, 0, 215), (215, 0, 0), (215, 0, 215),
    (0, 215, 0), (0, 215, 215), (215, 215, 0), (215, 215, 215),
]
PALETTE_BRIGHT = [
    (0, 0, 0), (0, 0, 255), (255, 0, 0), (255, 0, 255),
    (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255),
]


def render_png(mem: bytes, path: str) -> None:
    w, h = 256, 192
    pix = bytearray(w * h * 3)
    for y in range(h):
        third = y // 64
        r8 = (y % 64) // 8
        line = y % 8
        addr = 0x4000 | (third << 11) | (r8 << 5) | (line << 8) | 0
        for cx in range(32):
            byte = mem[(addr + cx) - 0x4000]
            attr_addr = 0x5800 + (y // 8) * 32 + cx
            attr = mem[attr_addr - 0x4000]
            ink = attr & 0x07
            paper = (attr >> 3) & 0x07
            bright = (attr >> 6) & 0x01
            pal = PALETTE_BRIGHT if bright else PALETTE
            for bit in range(8):
                on = (byte >> (7 - bit)) & 1
                colour = pal[ink] if on else pal[paper]
                px = cx * 8 + bit
                o = (y * w + px) * 3
                pix[o:o + 3] = bytes(colour)

    # Minimal uncompressed PNG writer (zlib store-mode).
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += pix[y * w * 3:(y + 1) * w * 3]

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 6)
    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: g46b_102_live_screenshot.py OUTFILE.png", file=sys.stderr)
        return 2
    out = sys.argv[1]
    c = CSpectDZRP()
    c.connect()
    c.init()
    mem = c.read_mem(0x4000, 6912)
    c.close()
    render_png(mem, out)
    print(f"wrote {out} ({len(mem)} bytes read)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
