"""g46b_eod24_nr8e03_compare.py — capture CSpect state at the NR $8E,$03
sites jnext executes per slide cycle.

jnext's SRAM_ROM trace shows three sites that flip sram_rom=3:
  $5B48 (RAM)  = NEXTREG $8E, $03; RET
  $3CFC (slot 1, bank context-dependent) = NEXTREG $8E, $03; RET
  $5B0E (OUT 7FFD-based toggle, sets sram_rom via two ports)

For each site, capture:
  - Whether BP fires at all in 10s of CSpect execution
  - SP, regs, slots, 7FFD/1FFD/DFFD/NR8C state when fired
  - Stack[0..3] (= return targets, who called this routine)

Goal: find which sites CSpect actually executes, and compare with jnext's
trace. If CSpect skips all three, jnext is misrouting supervisor execution
through bank-3-paging code; if CSpect hits them with different values,
the upstream call-target divergence is in the caller PC.

Usage:
  python3 tools/cspect_dzrp/g46b_eod24_nr8e03_compare.py [--hits 25]
"""
from __future__ import annotations

import argparse
import sys

import cspect_dzrp as dz


SITES = [
    (0x5B48, "RAM-resident NEXTREG $8E,$03; RET"),
    (0x5B4D, "RAM-resident NEXTREG $8E,$00; RET"),
    (0x5B0E, "RAM toggle OUT (C),A — about to write 7FFD"),
    (0x5B1B, "RAM toggle OUT (C),A — about to write 1FFD"),
    (0x3CFC, "Slot 1 NEXTREG $8E,$03 (bank-context dependent)"),
    (0x3E13, "Bank-flip wrapper at $3E13 (NEXTREG $8E,$xx)"),
    (0x3E93, "Bank-flip wrapper at $3E93 (NEXTREG $8E,$xx)"),
]


def _hex(d): return " ".join(f"{b:02X}" for b in d)


def read_word(c, addr):
    b = c.read_mem(addr & 0xFFFF, 2)
    return b[0] | (b[1] << 8)


def capture(c: dz.CSpectDZRP, label: str, hit: int):
    r = c.get_registers()
    A = (r.AF >> 8) & 0xFF
    bytes_pc = c.read_mem(r.PC & 0xFFFF, 6)
    sp_bytes = c.read_mem(r.SP & 0xFFFF, 8)
    print(f"=== HIT #{hit} PC=${r.PC:04X} — {label} ===")
    print(f"  A=${A:02X}  SP=${r.SP:04X}  HL=${r.HL:04X}  BC=${r.BC:04X}")
    print(f"  slots[0..7]: {_hex(bytes(r.slots))}")
    print(f"  bytes @ PC: {_hex(bytes_pc)}")
    print(f"  stack[0..3]: ${sp_bytes[0]|(sp_bytes[1]<<8):04X} "
          f"${sp_bytes[2]|(sp_bytes[3]<<8):04X} "
          f"${sp_bytes[4]|(sp_bytes[5]<<8):04X} "
          f"${sp_bytes[6]|(sp_bytes[7]<<8):04X}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11000)
    ap.add_argument("--hits", type=int, default=25)
    ap.add_argument("--timeout", type=float, default=10.0)
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
    fired = {a: 0 for _, a, _ in bps}
    try:
        while captured < args.hits:
            print(f"\n# Continuing — hit {captured+1}/{args.hits}...")
            c.cont()
            try:
                c.wait_for_pause(timeout=args.timeout)
            except TimeoutError:
                print(f"!! Timeout after {args.timeout}s — no more hits.")
                break
            captured += 1
            r = c.get_registers()
            label = next((d for _, a, d in bps if a == r.PC), f"unknown@${r.PC:04X}")
            fired[r.PC] = fired.get(r.PC, 0) + 1
            capture(c, label, captured)
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
