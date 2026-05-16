#!/usr/bin/env python3
"""Task 18: full 64KB memory dump + dump alternate ROMs by remapping slot 0."""
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
        print(f"# Full memory dump (PC=${regs.PC:04X})")
        print()
        print(f"slots at pause: {[f'${b:02X}' for b in regs.slots]}")
        print()

        # Save current slot mapping.
        original = list(regs.slots)

        # Probe what physical banks ARE candidate ROMs. Map several into slot 0
        # and dump first 64 bytes.
        print("## Slot-0 remapping probe (which bank is the *current* boot ROM contents?)")
        print()
        print("| Bank | First 16 bytes at $0000 | Decoded |")
        print("|------|--------------------------|---------|")
        # Reasonable candidate banks: $FF (current), $FE, $FD, $FC (special),
        # $00, $01, $02, $03 (legacy), and the canonical ROM IDs.
        probe_banks = [0xFF, 0xFE, 0xFD, 0xFC,
                       0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                       0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
                       0xA0, 0xA1, 0xA2, 0xA3]
        for bank in probe_banks:
            try:
                c.set_slot(0, bank)
                d = c.read_mem(0x0000, 16)
                hexs = " ".join(f"{b:02X}" for b in d)
                decoded = ""
                if d[0] == 0xF3:
                    decoded = "starts with DI"
                elif d[0:2] == bytes([0xF3, 0xC3]):
                    decoded = "DI JP ..."
                print(f"| ${bank:02X} | {hexs} | {decoded} |")
            except Exception as e:
                print(f"| ${bank:02X} | ERR | {e} |")
        # restore slot 0
        c.set_slot(0, original[0])
        print()

        # Now produce full 64KB dump (current mapping), one section per slot.
        print("## Full 64KB CPU view")
        print()
        for slot in range(8):
            start = slot * 0x2000
            print(f"### Slot {slot} ${start:04X}-${start+0x1FFF:04X} (bank ${original[slot]:02X})")
            print("```")
            try:
                # 8KB in 2 chunks (DZRP read_mem can handle 8KB but be conservative).
                d1 = c.read_mem(start, 0x1000)
                d2 = c.read_mem(start + 0x1000, 0x1000)
                # First 512 bytes + last 256 bytes to keep doc compact.
                print(f"-- first 512 bytes:")
                print(hexdump(start, d1[:512]))
                print(f"-- last 256 bytes of slot:")
                print(hexdump(start + 0x1F00, d2[-256:]))
            except Exception as e:
                print(f"ERR: {e}")
            print("```")
            print()


if __name__ == "__main__":
    raise SystemExit(main())
