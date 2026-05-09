"""g46b_eod23_swap_capture.py — capture CSpect state at bank 0 $27A3
stack-swap entry to compare with jnext's slide-causing SWAP #4.

Per EOD-23 SWAPTRAP analysis, jnext's slide is triggered by the 4th
swap of each ~3s cycle:

  Pre-swap: SP=$5BEB, ($5B6A)=$FF55
  alt_stk[0..3] = [$3E00, $0000, $423C, $7E42]
                   ^^^^^   ^^^^^
                   wrapper $0000 = BUG (wrapper reads as inline-DW ptr)

In CSpect at the equivalent boot phase, alt_stk[1] should be a valid
return address. The diff is the upstream supervisor state-setup bug.

Usage:
  # Launch CSpect first:
  #   mono ../CSpect3_1_0_0/CSpect.exe -mmc roms/nextzxos-1gb-fat32fix.img -debug
  # Then run this script while CSpect is paused at $0000 (cold-boot).
  # It installs a BP at bank 0 $27A3, runs until first hit, captures
  # state, continues, captures the next 5 hits, then prints summary.

  python3 tools/cspect_dzrp/g46b_eod23_swap_capture.py [--host HOST] [--port PORT]
"""
from __future__ import annotations

import argparse
import sys
import time

import cspect_dzrp as dz


SWAP_BP = 0x27A3
HITS_TO_CAPTURE = 12  # capture more than 5 to span at least 2 cycles


def _hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def read_word(c: dz.CSpectDZRP, addr: int) -> int:
    b = c.read_mem(addr & 0xFFFF, 2)
    return b[0] | (b[1] << 8)


def capture_swap_state(c: dz.CSpectDZRP, hit_no: int) -> None:
    r = c.get_registers()
    sb6a = read_word(c, 0x5B6A)
    print(f"\n=== SWAP HIT #{hit_no} at PC=${r.PC:04X} ===")
    print(f"  SP=${r.SP:04X} ($5B6A)=${sb6a:04X}")
    print(f"  AF=${r.AF:04X} BC=${r.BC:04X} DE=${r.DE:04X} HL=${r.HL:04X}")
    print(f"  IX=${r.IX:04X} IY=${r.IY:04X}")
    print(f"  slots [NR $50..$57]: {_hex(bytes(r.slots))}")

    # Current stack 8 words
    print(f"  cur_stk[0..7]:")
    for i in range(8):
        a = (r.SP + i * 2) & 0xFFFF
        w = read_word(c, a)
        print(f"    [{a:04X}]=${w:04X}")

    # Alternate stack 8 words (= what RET will pop into PC after swap)
    print(f"  alt_stk[0..7] (= MEM[$5B6A..]):")
    for i in range(8):
        a = (sb6a + i * 2) & 0xFFFF
        w = read_word(c, a)
        marker = ""
        if i == 0:
            marker = "  ← will be popped to PC by RET"
        elif i == 1:
            marker = "  ← will be read by wrapper EX (SP),HL → inline-DW ptr"
        print(f"    [{a:04X}]=${w:04X}{marker}")

    # Sysvars $5B40..$5B7F
    sysvars = c.read_mem(0x5B40, 64)
    print(f"  sysvars $5B40..$5B7F:")
    print(f"    $5B40  {_hex(sysvars[0:16])}")
    print(f"    $5B50  {_hex(sysvars[16:32])}")
    print(f"    $5B60  {_hex(sysvars[32:48])}")
    print(f"    $5B70  {_hex(sysvars[48:64])}")
    print(f"  Key sysvars: ($5B52)=${(sysvars[0x12]|sysvars[0x13]<<8):04X} "
          f"($5B54)=${(sysvars[0x14]|sysvars[0x15]<<8):04X} "
          f"($5B58)=${(sysvars[0x18]|sysvars[0x19]<<8):04X} "
          f"($5B6A)=${(sysvars[0x2A]|sysvars[0x2B]<<8):04X}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=11000)
    ap.add_argument("--hits", type=int, default=HITS_TO_CAPTURE)
    ap.add_argument("--bp-bank", type=int, default=0,
                    help="Bank number for BP (0 = 64K address, default)")
    args = ap.parse_args()

    print(f"# Connecting to CSpect DZRP at {args.host}:{args.port}")
    c = dz.CSpectDZRP(host=args.host, port=args.port)
    c.connect()
    c.init()

    # Pause first (CSpect launched with -debug starts paused, but be safe).
    try:
        c.pause()
        c.wait_for_pause(timeout=3.0)
    except TimeoutError:
        pass
    except Exception:
        pass

    # Install BP at bank 0 $27A3.
    bp_id = c.add_breakpoint(SWAP_BP, args.bp_bank)
    print(f"# BP installed at $27A3 (id={bp_id})")

    # Initial state (idle / paused at $0000 cold-boot)
    r = c.get_registers()
    print(f"# Initial PC=${r.PC:04X} SP=${r.SP:04X}")

    captured = 0
    try:
        while captured < args.hits:
            # Continue execution until BP fires.
            print(f"\n# Continuing — waiting for hit #{captured+1}...")
            c.cont()
            try:
                c.wait_for_pause(timeout=15.0)
            except TimeoutError:
                print(f"!! Timeout waiting for BP hit #{captured+1}")
                break
            captured += 1
            capture_swap_state(c, captured)
    finally:
        try:
            c.remove_breakpoint(bp_id)
            print(f"\n# BP removed (id={bp_id})")
        except Exception:
            pass

    print(f"\n# Captured {captured} swap hits.")
    print("# Compare alt_stk[0..1] across hits.")
    print("# jnext SWAP #4 (slide trigger): alt_stk=[$3E00, $0000, ...]")
    print("# CSpect should show alt_stk[1] != $0000 — that's the missing value.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
