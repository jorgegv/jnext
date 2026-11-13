#!/usr/bin/env python3
"""Render a TAP file as a mono 8-bit 44100 Hz WAV, as a cassette would carry it.

    python3 tap2wav.py in.tap out.wav

Written from the TAP format (blocks of u16 length + flag, data, checksum) and the 48K ROM's
SA-BYTES timings (TZX 1.20 block 0x10): pilot of 8063 pulses (flag < 0x80) or 3223 (flag >= 0x80)
of 2168 T, sync 667 T + 735 T, then two pulses per bit, MSB first, 855 T for 0 and 1710 T for 1,
then 1000 ms of silence. 0.5 s of silence first. Each pulse toggles the level (0x40 / 0xC0).
Edge k is placed at round(T_k * 44100 / 3500000) of the CUMULATIVE time, so rounding never drifts.
"""
import struct
import sys

CPU, RATE = 3500000, 44100
LOW, HIGH = 0x40, 0xC0

data = open(sys.argv[1], 'rb').read()
edges = []          # cumulative T of each level change
t = int(0.5 * CPU)  # lead-in silence
pos = 0
while pos < len(data):
    n = struct.unpack_from('<H', data, pos)[0]
    block = data[pos + 2:pos + 2 + n]
    if len(block) != n or n < 2:
        sys.exit('tap2wav: truncated TAP block')
    pulses = [2168] * (8063 if block[0] < 0x80 else 3223) + [667, 735]
    for byte in block:
        for bit in range(7, -1, -1):
            pulses += [1710 if (byte >> bit) & 1 else 855] * 2
    for p in pulses:
        t += p
        edges.append(t)
    t += CPU  # 1000 ms pause, level held
    pos += 2 + n
total = round(t * RATE / CPU)
pcm = bytearray()
level = LOW
last = 0
for e in edges:
    i = round(e * RATE / CPU)
    pcm += bytes([level]) * (i - last)
    last = i
    level = HIGH if level == LOW else LOW
pcm += bytes([level]) * (total - last)
hdr = (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' +
       struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8) + b'data' + struct.pack('<I', len(pcm)))
open(sys.argv[2], 'wb').write(hdr + pcm)
