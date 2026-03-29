#!/usr/bin/env python3
"""Extract the ZX Spectrum Next boot ROM from VHDL source to a binary file.

Parses the ROM_ARRAY constant from bootrom.vhd, extracting all x"HH" hex
byte literals, and writes them as a raw 8K binary.

Usage:
    python3 extract_bootrom.py <input.vhd> <output.rom>
"""

import re
import sys

def extract(vhd_path: str, rom_path: str) -> None:
    with open(vhd_path, "r") as f:
        text = f.read()

    # Find all x"HH" hex byte literals in the ROM_ARRAY constant
    hex_bytes = re.findall(r'x"([0-9A-Fa-f]{2})"', text)

    if not hex_bytes:
        print(f"ERROR: no hex bytes found in {vhd_path}", file=sys.stderr)
        sys.exit(1)

    data = bytes(int(h, 16) for h in hex_bytes)
    print(f"Extracted {len(data)} bytes ({len(data) // 1024}K) from {vhd_path}")

    if len(data) != 8192:
        print(f"WARNING: expected 8192 bytes, got {len(data)}", file=sys.stderr)

    with open(rom_path, "wb") as f:
        f.write(data)

    print(f"Written to {rom_path}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.vhd> <output.rom>", file=sys.stderr)
        sys.exit(1)
    extract(sys.argv[1], sys.argv[2])
