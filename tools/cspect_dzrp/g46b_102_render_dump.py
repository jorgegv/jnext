#!/usr/bin/env python3
"""G46(b) #102: render a G46b102KeyInject screen_<frame>.bin dump (raw
0x4000-0x5AFF ULA screen bytes, 6912 bytes) to a PNG for visual inspection.
No DZRP connection involved (offline, post-run) — avoids the observed
CSpect free-run stall when DZRP clients connect/disconnect mid-run.

Usage: g46b_102_render_dump.py screen_1234.bin out.png
"""
import sys
import struct
import zlib

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
        addr = 0x4000 | (third << 11) | (r8 << 5) | (line << 8)
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
    if len(sys.argv) != 3:
        print("usage: g46b_102_render_dump.py screen_N.bin out.png", file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as f:
        mem = f.read()
    if len(mem) != 6912:
        print(f"warning: expected 6912 bytes, got {len(mem)}", file=sys.stderr)
    render_png(mem, sys.argv[2])
    print(f"wrote {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
