#!/usr/bin/env python3
"""Promote a z88dk-generated NEX header to V1.3 with a CLI buffer (GH #172).

z88dk emits V1.2 and has no notion of the V1.3 CLI-buffer fields, so this
patches the four header bytes that matter, in place:

  4..7  version string      -> "V1.3"
  142   expansion bus       -> 1 ("leave NR 0x80 alone"; in a V1.2 header
                                  this offset is reserved space holding 0,
                                  which V1.3 reads as "disable it")
  148   CLI buffer address  -> 0xBF00 (top of bank 2, above the code and
                                  below the 0xFFFD stack)
  150   CLI buffer size     -> 32

Nothing else moves: V1.3 adds no data block unless the header asks for one
(no copper block, no "screen flags 2" bit, no checksum), so the file layout
is byte-identical to the V1.2 original and banks_offset may stay 0.
"""
import struct
import sys

CLI_ADDR = 0xBF00
CLI_SIZE = 32

path = sys.argv[1]
with open(path, "r+b") as f:
    data = bytearray(f.read())
    if bytes(data[0:4]) != b"Next":
        sys.exit(f"{path}: not a NEX file")
    data[4:8] = b"V1.3"
    data[142] = 1
    struct.pack_into("<HH", data, 148, CLI_ADDR, CLI_SIZE)
    f.seek(0)
    f.write(data)
print(f"{path}: header -> V1.3, CLI buffer {CLI_ADDR:#06x} ({CLI_SIZE} bytes)")
