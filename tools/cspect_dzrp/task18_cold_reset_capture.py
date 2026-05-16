#!/usr/bin/env python3
"""Task 18: Cold-reset state capture from CSpect.

Connects to CSpect launched with `-debug` (CPU halted at PC=0). Dumps:
  - All Z80 regs + slot map (NR $50..$57).
  - "Interesting" NextRegs: $00-$0A, $11, $12, $13, $40-$44, $4A-$4C,
    $50-$57, $80-$8F, $C4-$C8.
  - First 256 bytes of memory at $0000.

Output: markdown to stdout. Run from jnext repo root.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


INTERESTING_REGS = (
    list(range(0x00, 0x0B))    # $00..$0A — machine id / core / mem / reset / cpu speed / IO / per / sub / ...
    + [0x11, 0x12, 0x13]       # video timing, layer 2, layer 2 page
    + list(range(0x40, 0x45))  # palette index/value/format/etc.
    + list(range(0x4A, 0x4D))  # transparency, ULA fallback, sprite priorities
    + list(range(0x50, 0x58))  # MMU slots 0..7
    + list(range(0x80, 0x90))  # expansion bus / int control / pin
    + list(range(0xC0, 0xC9))  # interrupt control area
)


def hexdump(addr: int, data: bytes, width: int = 16) -> str:
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  {addr + i:04X}  {hexs:<{width * 3 - 1}}  {ascii_}")
    return "\n".join(lines)


def main() -> int:
    with CSpectDZRP() as c:
        info = c.init()
        print(f"# CSpect cold-reset capture")
        print()
        print(f"DZRP {'.'.join(map(str, info.dzrp_version))} / "
              f"machine={info.machine_type} / program={info.program_name!r}")
        print()

        # Make sure CPU is halted (it should be, with -debug).
        try:
            c.pause()
        except Exception as e:
            print(f"(pause: {e})")

        regs = c.get_registers()
        print("## Z80 Registers")
        print()
        print("```")
        print(regs.format())
        print("```")
        print()

        print("## Slot map (NR $50..$57)")
        print()
        print("```")
        print(f"slot 0 (0x0000-0x1FFF) = bank ${regs.slots[0]:02X}")
        print(f"slot 1 (0x2000-0x3FFF) = bank ${regs.slots[1]:02X}")
        print(f"slot 2 (0x4000-0x5FFF) = bank ${regs.slots[2]:02X}")
        print(f"slot 3 (0x6000-0x7FFF) = bank ${regs.slots[3]:02X}")
        print(f"slot 4 (0x8000-0x9FFF) = bank ${regs.slots[4]:02X}")
        print(f"slot 5 (0xA000-0xBFFF) = bank ${regs.slots[5]:02X}")
        print(f"slot 6 (0xC000-0xDFFF) = bank ${regs.slots[6]:02X}")
        print(f"slot 7 (0xE000-0xFFFF) = bank ${regs.slots[7]:02X}")
        print("```")
        print()

        print("## NextRegs (curated)")
        print()
        print("| Reg | Value | Notes |")
        print("|-----|-------|-------|")
        for r in INTERESTING_REGS:
            try:
                v = c.get_tbblue_reg(r)
                print(f"| ${r:02X} | ${v:02X} | |")
            except Exception as e:
                print(f"| ${r:02X} | ERR | {e} |")
        print()

        print("## Memory at $0000 (256 bytes)")
        print()
        print("```")
        try:
            data = c.read_mem(0x0000, 256)
            print(hexdump(0x0000, data))
        except Exception as e:
            print(f"ERR: {e}")
        print("```")
        print()

        # Bonus: also peek $4000 and $C000 since useful slot anchors.
        for addr in (0x4000, 0xC000):
            print(f"## Memory at ${addr:04X} (64 bytes)")
            print()
            print("```")
            try:
                data = c.read_mem(addr, 64)
                print(hexdump(addr, data))
            except Exception as e:
                print(f"ERR: {e}")
            print("```")
            print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
