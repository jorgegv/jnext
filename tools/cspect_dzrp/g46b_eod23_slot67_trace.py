"""g46b_eod23_slot67_trace.py — trace slot 6/7 evolution in CSpect.

Sets BPs at multiple sites where slot 6/7 mapping changes:
  - bank 2 $0A61 (NEXTREG $56,$0E)
  - bank 2 $0A65 (NEXTREG $57,$0F)
  - bank 0 $01D7 (NEXTREG $8E,$08 — reverts to bank 0)
  - bank 2 $1F01 (NEXTREG $8E,$7A — sets bank 7)
  - bank 0 $0008 (RST $08 entry → JP $103B = bank-flip handler)
  - bank 0 $5B0E (RAM-resident toggle wrapper OUT (C),A to 7FFD)

For each hit, capture: PC, regs, slot map, key sysvars.

Usage:
  python3 tools/cspect_dzrp/g46b_eod23_slot67_trace.py [--hits 30]
"""
from __future__ import annotations

import argparse
import sys

import cspect_dzrp as dz


CHECKPOINTS = [
    (0x0A61, "NEXTREG $56,$0E (bank 2 — set bank 7 lo)"),
    (0x0A65, "NEXTREG $57,$0F (bank 2 — set bank 7 hi)"),
    (0x01D7, "NEXTREG $8E,$08 (bank 0 — revert to bank 0)"),
    (0x1F01, "NEXTREG $8E,$7A (bank 2 — set bank 7)"),
    (0x103B, "RST $08 handler entry"),
    (0x5B0E, "RAM toggle wrapper OUT (C),A"),
    (0x27A3, "Stack-swap routine entry"),
]


def _hex(d): return " ".join(f"{b:02X}" for b in d)


def read_word(c, addr):
    b = c.read_mem(addr & 0xFFFF, 2)
    return b[0] | (b[1] << 8)


def capture(c: dz.CSpectDZRP, label: str, hit_no: int):
    r = c.get_registers()
    print(f"\n=== HIT #{hit_no} PC=${r.PC:04X} — {label} ===")
    print(f"  SP=${r.SP:04X}")
    print(f"  AF=${r.AF:04X} BC=${r.BC:04X} DE=${r.DE:04X} HL=${r.HL:04X}")
    print(f"  slots[0..7]: {_hex(bytes(r.slots))}")
    sb6a = read_word(c, 0x5B6A)
    sb5c = read_word(c, 0x5B5C)
    sb67 = read_word(c, 0x5B67)
    print(f"  ($5B5C)=${sb5c:04X} ($5B67)=${sb67:04X} ($5B6A)=${sb6a:04X}")
    # Stack 4 words
    stk = []
    for i in range(4):
        a = (r.SP + i * 2) & 0xFFFF
        stk.append(read_word(c, a))
    print(f"  stack[0..3] = ${stk[0]:04X} ${stk[1]:04X} ${stk[2]:04X} ${stk[3]:04X}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11000)
    ap.add_argument("--hits", type=int, default=30)
    args = ap.parse_args()

    c = dz.CSpectDZRP(host=args.host, port=args.port)
    c.connect()
    c.init()
    try:
        c.pause()
        c.wait_for_pause(timeout=3.0)
    except Exception:
        pass

    bps = []
    for addr, desc in CHECKPOINTS:
        bp_id = c.add_breakpoint(addr, 0)
        bps.append((bp_id, addr, desc))
        print(f"# BP at ${addr:04X} (id={bp_id}) — {desc}")

    captured = 0
    try:
        while captured < args.hits:
            print(f"\n# Continuing — hit {captured+1}/{args.hits}...")
            c.cont()
            try:
                c.wait_for_pause(timeout=15.0)
            except TimeoutError:
                print(f"!! Timeout")
                break
            captured += 1
            r = c.get_registers()
            label = next((d for _, a, d in bps if a == r.PC), f"unknown@${r.PC:04X}")
            capture(c, label, captured)
    finally:
        for bp_id, _, _ in bps:
            try:
                c.remove_breakpoint(bp_id)
            except Exception:
                pass

    print(f"\n# Captured {captured} checkpoint hits.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
