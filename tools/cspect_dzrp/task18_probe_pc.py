#!/usr/bin/env python3
"""Task 18: probe code around current PC + a few key NextZXOS landmarks."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


def hexdump(addr, data, w=16):
    out = []
    for i in range(0, len(data), w):
        chunk = data[i:i+w]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {addr+i:04X}  {hexs:<{w*3-1}}  {asc}")
    return "\n".join(out)


def main():
    with CSpectDZRP() as c:
        c.init()
        c.pause()
        regs = c.get_registers()
        print(f"# code probe at PC=${regs.PC:04X}")
        print()
        print(f"slots: {[f'${b:02X}' for b in regs.slots]}")
        print()
        # 64 bytes BEFORE PC, 192 bytes AT/AFTER PC.
        start = max(0, regs.PC - 32)
        d = c.read_mem(start, 256)
        print("```")
        print(hexdump(start, d))
        print("```")
        print()
        print(f"## peek SP={regs.SP:04X} (top of stack)")
        try:
            d = c.read_mem(regs.SP, 32)
            print("```")
            print(hexdump(regs.SP, d))
            print("```")
        except Exception as e:
            print(f"ERR: {e}")


if __name__ == "__main__":
    raise SystemExit(main())
