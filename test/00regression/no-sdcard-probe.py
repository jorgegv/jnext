#!/usr/bin/env python3
"""Write the --no-sdcard ROM probe NEX (GH #540) to argv[1].

Invented bytes, no third-party code. The program prints "IM1-ARMED" to the
magic port 0xCAFE (line mode), then turns on IM 1 interrupts and halts: the
first frame interrupt sends the CPU to $0038, which with no SD card is an
empty ROM. jnext must log "executed ROM code at $0038 (IM 1); --no-sdcard has
no ROM" once.

    DI
    LD HL,msg ; puts
    IM 1 ; EI
    loop: HALT ; JR loop
"""
import struct
import sys

ORG = 0x8000
MSG = 0x8100
MAGIC = 0xCAFE

code = bytes([
    0xF3,                               # di
    0x21, MSG & 0xFF, MSG >> 8,         # ld hl,msg
    # puts:
    0x7E,                               # ld a,(hl)
    0xB7,                               # or a
    0x28, 0x08,                         # jr z,+8 (done)
    0x01, MAGIC & 0xFF, MAGIC >> 8,     # ld bc,MAGIC
    0xED, 0x79,                         # out (c),a
    0x23,                               # inc hl
    0x18, 0xF4,                         # jr puts
    # done:
    0xED, 0x56,                         # im 1
    0xFB,                               # ei
    0x76,                               # loop: halt
    0x18, 0xFD,                         # jr loop
])

bank2 = bytearray(16384)
bank2[0:len(code)] = code
msg = b"IM1-ARMED\r\0"
bank2[MSG - ORG:MSG - ORG + len(msg)] = msg

h = bytearray(512)
h[0:4] = b"Next"
h[4:8] = b"V1.2"
h[8] = 1                                   # num_banks
h[12:14] = struct.pack("<H", 0xBFF0)       # SP
h[14:16] = struct.pack("<H", ORG)          # PC
h[18 + 2] = 1                              # bank 2 present ($8000)
with open(sys.argv[1], "wb") as f:
    f.write(bytes(h) + bytes(bank2))
