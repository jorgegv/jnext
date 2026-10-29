#!/usr/bin/env python3
"""Generate slow_gap.tap, the fixture of the tap-realtime-gap-48k regression row (GH #395).

A synthetic tape of our own bytes, no game data:

    10 LOAD "" CODE : PAUSE 500 : LOAD "" CODE 22528

followed by a 16-byte CODE block and a 768-byte block that is loaded into the
attribute area. BASIC is busy (PAUSE 500 = 10 s) while the tape's next blocks
go by, so a deck that cannot stop in the gap lets the attribute block pass
unseen and the screen stays blank; with automatic play/stop the deck holds in
the gap after the CODE block and the pattern appears.

Usage: make_slow_gap.py [OUT]     (default: slow_gap.tap next to this script)
"""
import os
import sys


def block(flag, payload):
    x = flag
    for b in payload:
        x ^= b
    body = bytes([flag]) + bytes(payload) + bytes([x])
    return len(body).to_bytes(2, "little") + body


def header(kind, name, length, p1, p2):
    payload = bytes([kind]) + name.ljust(10).encode("ascii") + \
        length.to_bytes(2, "little") + p1.to_bytes(2, "little") + p2.to_bytes(2, "little")
    return block(0x00, payload)


def num(text, value):
    """A BASIC number: its digits, then the hidden 5-byte integer form."""
    return text.encode("ascii") + bytes([0x0E, 0x00, 0x00, value & 0xFF, value >> 8, 0x00])


LOAD, CODE, PAUSE = 0xEF, 0xAF, 0xF2

line = bytes([LOAD, 0x22, 0x22, CODE, 0x3A, PAUSE]) + num("500", 500) + \
    bytes([0x3A, LOAD, 0x22, 0x22, CODE]) + num("22528", 22528) + bytes([0x0D])
program = (10).to_bytes(2, "big") + len(line).to_bytes(2, "little") + line

dummy = b"JNEXT GH395 TEST"
attrs = bytes(((((x // 4) + (y // 3)) & 7) | (((((x // 4) + (y // 3)) + 3) & 7) << 3))
              for y in range(24) for x in range(32))

tape = b"".join([
    header(0, "slowgap", len(program), 10, len(program)),
    block(0xFF, program),
    header(3, "dummy", len(dummy), 60000, 32768),
    block(0xFF, dummy),
    header(3, "attrs", len(attrs), 22528, 32768),
    block(0xFF, attrs),
])

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "slow_gap.tap")
with open(out, "wb") as f:
    f.write(tape)
print("wrote %s (%d bytes)" % (out, len(tape)))
