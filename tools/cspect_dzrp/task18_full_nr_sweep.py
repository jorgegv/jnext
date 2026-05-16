#!/usr/bin/env python3
"""Task 18: Full NextReg sweep ($00..$FF).

Iterates all 256 NextReg addresses and reports their values.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


def main() -> int:
    with CSpectDZRP() as c:
        c.init()
        try:
            c.pause()
        except Exception:
            pass

        print("| Reg | Value |")
        print("|-----|-------|")
        nonzero = []
        for r in range(0x100):
            try:
                v = c.get_tbblue_reg(r)
                print(f"| ${r:02X} | ${v:02X} |")
                if v != 0:
                    nonzero.append((r, v))
            except Exception as e:
                print(f"| ${r:02X} | ERR ({e}) |")
        print()
        print(f"Non-zero count: {len(nonzero)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
