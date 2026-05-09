"""g46b_eod24_01f0_compare.py — DZRP probe at bank 0 $01F0.

jnext's SRAM_ROM trace shows:
  PC=$5B48 (NEXTREG $8E,$03; RET) caller stack = [$1661, $5B4D, $01F0]
  → caller at bank 0 $01F0 sets up bank-3 trampoline to $1661 (LDDR).

This script:
  - BPs at $01F0, $01F3, $01F6, $01F9 (= $01F0 region in bank 0)
  - Captures regs/slots/bytes when fired
  - Also BPs at $1661 (= LDDR target in bank 3)
  - Reports hit counts after 10s

Goal: confirm whether CSpect ever reaches $01F0 or $1661, and if so what
state it's in. If CSpect skips them, the upstream divergence is in code
that calls $01F0; if CSpect reaches them but with different banks, the
bug is in jnext's bank-routing for that call path.
"""
from __future__ import annotations

import argparse
import sys

import cspect_dzrp as dz


SITES = [
    (0x01F0, "Bank 0 $01F0 — caller of bank-3 trampoline"),
    (0x01F3, "Bank 0 $01F3 — within trampoline setup"),
    (0x01F6, "Bank 0 $01F6"),
    (0x1661, "Bank 3 $1661 — LDDR target (per EOD-21)"),
    (0x5B48, "RAM $5B48 — NEXTREG $8E,$03; RET"),
]


def _hex(d): return " ".join(f"{b:02X}" for b in d)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11000)
    ap.add_argument("--hits", type=int, default=20)
    ap.add_argument("--timeout", type=float, default=8.0)
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

    fired = {a: 0 for _, a, _ in bps}
    captured = 0
    try:
        while captured < args.hits:
            print(f"\n# Continuing — hit {captured+1}/{args.hits}...")
            c.cont()
            try:
                c.wait_for_pause(timeout=args.timeout)
            except TimeoutError:
                print(f"!! Timeout after {args.timeout}s.")
                break
            captured += 1
            r = c.get_registers()
            label = next((d for _, a, d in bps if a == r.PC), f"unknown@${r.PC:04X}")
            fired[r.PC] = fired.get(r.PC, 0) + 1
            bytes_pc = c.read_mem(r.PC & 0xFFFF, 8)
            sp_bytes = c.read_mem(r.SP & 0xFFFF, 8)
            print(f"=== HIT #{captured} PC=${r.PC:04X} — {label} ===")
            print(f"  A=${(r.AF >> 8) & 0xFF:02X}  SP=${r.SP:04X}  HL=${r.HL:04X}  BC=${r.BC:04X}  DE=${r.DE:04X}")
            print(f"  slots[0..7]: {_hex(bytes(r.slots))}")
            print(f"  bytes @ PC: {_hex(bytes_pc)}")
            print(f"  stack[0..3]: ${sp_bytes[0]|(sp_bytes[1]<<8):04X} "
                  f"${sp_bytes[2]|(sp_bytes[3]<<8):04X} "
                  f"${sp_bytes[4]|(sp_bytes[5]<<8):04X} "
                  f"${sp_bytes[6]|(sp_bytes[7]<<8):04X}")
    finally:
        for bp_id, _, _ in bps:
            try:
                c.remove_breakpoint(bp_id)
            except Exception:
                pass

    print(f"\n# Captured {captured} hits. Fired-by-PC summary:")
    for _, addr, desc in bps:
        n = fired.get(addr, 0)
        print(f"#   ${addr:04X} hits={n}  — {desc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
