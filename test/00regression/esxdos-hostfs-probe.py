#!/usr/bin/env python3
# Writes the esxdos-hostfs-func probe NEX (usage: esxdos-hostfs-probe.py OUT.nex).
# Shared by esxdos-hostfs-func and esxdos-nexdir-func: one generator, so both
# rows run the byte-identical program. See esxdos-hostfs-func.sh for what it does.
import sys, struct

# ── A tiny two-pass assembler, so no offset is computed by hand ────────────
CODE_ORG = 0x8000
DATA = {                       # fixed data addresses, well clear of the code
    "fname":   0x8200,
    "escname": 0x8210,
    "m_ok":    0x8230,
    "m_bad":   0x8240,
    "m_err":   0x8250,
    "buf":     0x8260,
    "handle":  0x8280,
}
MAGIC = 0xCAFE

items = []                     # bytes | ("rel", label) | ("w16", label)
labels = {}

def b(*vals):      items.append(bytes(vals))
def w16(label):    items.append(("w16", label))
def rel(label):    items.append(("rel", label))
def label(name):   labels[name] = None; items.append(("label", name))

def ld_a_n(n):     b(0x3E, n)
def ld_ix(addr):   b(0xDD, 0x21, addr & 0xFF, addr >> 8)
def ld_b_n(n):     b(0x06, n)
def ld_bc(v):      b(0x01, v & 0xFF, v >> 8)
def ld_de(v):      b(0x11, v & 0xFF, v >> 8)
def ld_hl(v):      b(0x21, v & 0xFF, v >> 8)
def rst08(hook):   b(0xCF, hook)
def ld_a_mem(a):   b(0x3A, a & 0xFF, a >> 8)
def ld_mem_a(a):   b(0x32, a & 0xFF, a >> 8)
def jr_c(l):       b(0x38); rel(l)
def jr_nc(l):      b(0x30); rel(l)
def jr_nz(l):      b(0x20); rel(l)
def jr(l):         b(0x18); rel(l)
def call(l):       b(0xCD); w16(l)

F_OPEN, F_CLOSE, F_READ, F_SEEK = 0x9A, 0x9B, 0x9D, 0x9F

# F_OPEN "data.txt", esx_mode_read. A=drive, IX=filespec, B=mode.
ld_a_n(ord('*'))
ld_ix(DATA["fname"])
ld_b_n(0x01)
rst08(F_OPEN)
jr_c("err")
ld_mem_a(DATA["handle"])

# F_SEEK to 6. A=handle, BCDE=distance, IXL=esx_seek_set (0).
ld_a_mem(DATA["handle"])
ld_bc(0)
ld_de(6)
ld_ix(0x0000)
rst08(F_SEEK)
jr_c("err")

# F_READ 6 bytes. A=handle, IX=dest, BC=count.
ld_a_mem(DATA["handle"])
ld_ix(DATA["buf"])
ld_bc(6)
rst08(F_READ)
jr_c("err")

# Terminate what was read with CR,0 and print it.
ld_hl(DATA["buf"] + 6)
b(0x36, 0x0D)                  # ld (hl),13
b(0x23)                        # inc hl
b(0x36, 0x00)                  # ld (hl),0
ld_hl(DATA["buf"])
call("puts")

ld_a_mem(DATA["handle"])
rst08(F_CLOSE)

# Sandbox probe: "../escape.txt" exists one level above the root.
ld_a_n(ord('*'))
ld_ix(DATA["escname"])
ld_b_n(0x01)
rst08(F_OPEN)
jr_nc("escaped")               # Fc=0 means it OPENED — the sandbox failed
ld_hl(DATA["m_ok"])
call("puts")
jr("spin")

label("escaped")
ld_hl(DATA["m_bad"])
call("puts")
jr("spin")

label("err")
ld_hl(DATA["m_err"])
call("puts")

label("spin")
jr("spin")

# puts: HL -> asciiz, one byte per OUT to the magic port.
label("puts")
label("puts_loop")
b(0x7E)                        # ld a,(hl)
b(0xB7)                        # or a
b(0xC8)                        # ret z
ld_bc(MAGIC)
b(0xED, 0x79)                  # out (c),a
b(0x23)                        # inc hl
jr("puts_loop")

# ── Pass 1: addresses ─────────────────────────────────────────────────────
pc = CODE_ORG
for it in items:
    if isinstance(it, bytes):
        pc += len(it)
    elif it[0] == "label":
        labels[it[1]] = pc
    elif it[0] == "rel":
        pc += 1
    elif it[0] == "w16":
        pc += 2
# ── Pass 2: emit ──────────────────────────────────────────────────────────
out = bytearray()
pc = CODE_ORG
for it in items:
    if isinstance(it, bytes):
        out += it; pc += len(it)
    elif it[0] == "label":
        pass
    elif it[0] == "rel":
        target = labels[it[1]]; pc += 1
        delta = target - pc
        assert -128 <= delta <= 127, (it[1], delta)
        out.append(delta & 0xFF)
    elif it[0] == "w16":
        target = labels[it[1]]
        out += struct.pack("<H", target); pc += 2

bank2 = bytearray(16384)
bank2[0:len(out)] = out
def put(addr, raw):
    off = addr - 0x8000
    bank2[off:off + len(raw)] = raw
put(DATA["fname"],   b"c:/data.txt\0")   # DRIVE-QUALIFIED on purpose
put(DATA["escname"], b"../escape.txt\0")
put(DATA["m_ok"],    b"SANDBOX-OK\r\0")
put(DATA["m_bad"],   b"SANDBOX-ESCAPED\r\0")
put(DATA["m_err"],   b"ERR\r\0")

h = bytearray(512)
h[0:4] = b"Next"; h[4:8] = b"V1.2"
h[10] = 0                                  # screen_flags
h[12:14] = struct.pack("<H", 0xBFF0)       # SP
h[14:16] = struct.pack("<H", 0x8000)       # PC
h[18 + 2] = 1                              # bank 2 present ($8000)
open(sys.argv[1], "wb").write(bytes(h) + bytes(bank2))
