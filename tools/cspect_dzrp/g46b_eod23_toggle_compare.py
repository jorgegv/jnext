"""g46b_eod23_toggle_compare.py — capture CSpect state across the
RAM-resident toggle wrapper at $5B00.

Sets BPs at:
  $5B0E (= OUT (C),A for 7FFD — captures pre-7FFD-write state)
  $5B10 (= LD BC,$1FFD just after the 7FFD write)
  $5B1B (= OUT (C),A for 1FFD — captures pre-1FFD-write state)
  $5B1D (= EI just after the 1FFD write)
  $5B20 (= RET — captures fully-toggled state)

For each hit captures:
  - A register (= the port value being written)
  - SP, regs
  - slots[0..7]
  - ($5B5C), ($5B67) sysvars

Goal: find what port transitions actually happen in CSpect at this
toggle, vs jnext's transition that ends in bank 3.
"""
from __future__ import annotations

import argparse
import sys

import cspect_dzrp as dz


SITES = [
    (0x5B0E, "OUT (C),A — about to write 7FFD"),
    (0x5B10, "LD BC,$1FFD — just after 7FFD write"),
    (0x5B1B, "OUT (C),A — about to write 1FFD"),
    (0x5B1D, "EI — just after 1FFD write"),
    (0x5B20, "RET — fully toggled"),
]


def _hex(d): return " ".join(f"{b:02X}" for b in d)


def read_word(c, addr):
    b = c.read_mem(addr & 0xFFFF, 2)
    return b[0] | (b[1] << 8)


def capture(c: dz.CSpectDZRP, label: str, hit: int):
    r = c.get_registers()
    A = (r.AF >> 8) & 0xFF
    sb5c = c.read_mem(0x5B5C, 1)[0]
    sb67 = c.read_mem(0x5B67, 1)[0]
    print(f"=== HIT #{hit} PC=${r.PC:04X} — {label} ===")
    print(f"  A=${A:02X}  SP=${r.SP:04X}  HL=${r.HL:04X}  BC=${r.BC:04X}")
    print(f"  slots[0..7]: {_hex(bytes(r.slots))}")
    print(f"  ($5B5C)=${sb5c:02X}  ($5B67)=${sb67:02X}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11000)
    ap.add_argument("--hits", type=int, default=15)
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
    for addr, desc in SITES:
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

    print(f"\n# Captured {captured} toggle-wrapper hits.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
