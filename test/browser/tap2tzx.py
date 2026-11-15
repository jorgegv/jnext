#!/usr/bin/env python3
"""Wrap a TAP file in a TZX 1.20 file: one standard-speed block (ID 0x10) per TAP block.

    python3 tap2tzx.py in.tap out.tzx
"""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
out = bytearray(b'ZXTape!\x1a\x01\x14')
pos = 0
while pos < len(data):
    n = struct.unpack_from('<H', data, pos)[0]
    block = data[pos + 2:pos + 2 + n]
    if len(block) != n:
        sys.exit('tap2tzx: truncated TAP block')
    out += b'\x10' + struct.pack('<HH', 1000, n) + block
    pos += 2 + n
open(sys.argv[2], 'wb').write(out)
