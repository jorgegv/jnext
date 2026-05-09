"""g46b_eod23_rst08_compare.py — capture post-RST $08 PCs in CSpect.

Sets BP at bank 0 $103B (= RST $08 handler entry, which is the second
instruction reached via $0008 JP $103B). For each hit, captures:
- SP, MEM[SP, SP+1] (= the post-RST-$08 PC that POP AF will read)
- regs, slot map
- 8 bytes at the post-RST-$08 PC (= what user code resumes with)

Goal: compare with jnext's post-RST-$08 PCs ($0302, $423C). If CSpect
diverges, the user code paths differ — that's the upstream divergence.

Usage:
  python3 tools/cspect_dzrp/g46b_eod23_rst08_compare.py [--hits 20]
"""
from __future__ import annotations

import argparse
import sys

import cspect_dzrp as dz


HANDLER_BP = 0x103B


def _hex(d): return " ".join(f"{b:02X}" for b in d)


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

    bp_id = c.add_breakpoint(HANDLER_BP, 0)
    print(f"# BP at $103B (id={bp_id}) — RST $08 handler entry")

    captured = 0
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
        # Read MEM[SP, SP+1] = post-RST-$08 PC
        sp_bytes = c.read_mem(r.SP, 2)
        post_rst = sp_bytes[0] | (sp_bytes[1] << 8)
        # Read 8 bytes at the post-RST PC and 8 bytes BEFORE (= caller code)
        before_bytes = c.read_mem((post_rst - 8) & 0xFFFF, 8)
        at_bytes = c.read_mem(post_rst, 8)
        print(f"=== HIT #{captured} sp=${r.SP:04X} post_rst_pc=${post_rst:04X} ===")
        print(f"  slots: {_hex(bytes(r.slots))}")
        print(f"  HL=${r.HL:04X} BC=${r.BC:04X} A=${(r.AF >> 8) & 0xFF:02X}")
        print(f"  bytes @ ${(post_rst - 8) & 0xFFFF:04X}..${(post_rst - 1) & 0xFFFF:04X}: {_hex(before_bytes)}  ← incl RST $08 caller (last byte should be CF)")
        print(f"  bytes @ ${post_rst:04X}..${(post_rst + 7) & 0xFFFF:04X}: {_hex(at_bytes)}  ← post-RST user code")

    try:
        c.remove_breakpoint(bp_id)
    except Exception:
        pass
    print(f"\n# Captured {captured} RST $08 hits.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
