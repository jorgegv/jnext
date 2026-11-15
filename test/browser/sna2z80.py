#!/usr/bin/env python3
"""Convert a 48K .sna into a version 1 uncompressed .z80, written from the .z80 format spec.

    python3 sna2z80.py in.sna out.z80

.sna: 27-byte header, 48K of RAM, PC on the stack. .z80 v1: 30-byte header, PC in the header,
SP already popped, byte 12 bit 5 clear = the RAM that follows is not compressed.
"""
import struct
import sys

sna = open(sys.argv[1], 'rb').read()
if len(sna) != 27 + 49152:
    sys.exit('sna2z80: not a 48K .sna')
i, hl2, de2, bc2, af2, hl, de, bc, iy, ix = struct.unpack_from('<B9H', sna, 0)
iff2 = (sna[19] >> 2) & 1
r = sna[20]
f, a = sna[21], sna[22]
sp = struct.unpack_from('<H', sna, 23)[0]
im = sna[25]
border = sna[26]
ram = sna[27:]
if sp < 0x4000 or sp > 0xFFFE:
    sys.exit('sna2z80: SP outside RAM')
pc = ram[sp - 0x4000] | (ram[sp - 0x4000 + 1] << 8)
sp = (sp + 2) & 0xFFFF
hdr = struct.pack('<BBHHHHBBBHHHHBBHHBBB',
                  a, f, bc, hl, pc, sp, i, r & 0x7F,
                  ((r >> 7) & 1) | ((border & 7) << 1),
                  de, bc2, de2, hl2, af2 >> 8, af2 & 0xFF, iy, ix,
                  iff2, iff2, im & 3)
assert len(hdr) == 30
open(sys.argv[2], 'wb').write(hdr + ram)
