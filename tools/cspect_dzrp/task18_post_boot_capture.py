#!/usr/bin/env python3
"""Task 18: Post-boot state capture from CSpect.

Connects to a running CSpect (no -debug), pauses CPU after NextZXOS has
finished booting, then dumps:
  - All Z80 regs + slot map.
  - All 256 NextRegs.
  - Full 64KB CPU memory view (current slot mapping).
  - Per-slot physical-bank sweep: for each slot 0..7, set bank 0..127
    and read 16KB — yields physical SRAM snapshot. This is constrained
    to "interesting" banks: $00..$1F (legacy 16K banks 0..15 + special),
    $80..$8F (alt ROM page space), and the actual mapped banks reported
    by NR $50..$57.

Output: markdown to stdout. Run from jnext repo root.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


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
        print(f"# CSpect post-boot capture")
        print()
        print(f"DZRP {'.'.join(map(str, info.dzrp_version))} / "
              f"machine={info.machine_type} / program={info.program_name!r}")
        print()

        # Pause CPU (CSpect was free-running).
        # CMD_PAUSE returns immediately; CSpect halts CPU but does not emit
        # a notification (per Server.cs the notification is only for BPs).
        c.pause()

        regs = c.get_registers()
        original_slots = list(regs.slots)
        print("## Z80 Registers (paused after boot)")
        print()
        print("```")
        print(regs.format())
        print("```")
        print()

        print("## Slot map (NR $50..$57) at pause")
        print()
        print("```")
        for i, b in enumerate(original_slots):
            start = i * 0x2000
            end = start + 0x1FFF
            print(f"slot {i} ({start:#06X}-{end:#06X}) = bank ${b:02X}")
        print("```")
        print()

        print("## All NextRegs (full sweep $00..$FF)")
        print()
        print("| Reg | Value |")
        print("|-----|-------|")
        all_nr = []
        for r in range(0x100):
            try:
                v = c.get_tbblue_reg(r)
                all_nr.append((r, v))
                print(f"| ${r:02X} | ${v:02X} |")
            except Exception:
                pass
        print()

        # Memory snapshot in the CURRENT (paused) mapping.
        print("## CPU-visible memory snapshot (current mapping)")
        print()
        for slot in range(8):
            start = slot * 0x2000
            print(f"### Slot {slot} (${start:04X}-${start + 0x1FFF:04X}), bank ${original_slots[slot]:02X} — first 256 bytes")
            print()
            print("```")
            try:
                data = c.read_mem(start, 256)
                print(hexdump(start, data))
            except Exception as e:
                print(f"ERR: {e}")
            print("```")
            print()

        # Physical bank sweep. For each slot, the bank value tells the
        # physical 8KB page. We collected those above. Beyond that, we want
        # to fingerprint NextZXOS-occupied SRAM. Sample 64 bytes of each
        # legacy 16KB bank's two 8KB sub-banks (bank-N sub-0 = phys-page 2N,
        # sub-1 = 2N+1) via SET_SLOT remap.
        print("## Physical SRAM bank fingerprint")
        print()
        print("(Sample: first 32 bytes of phys-bank N mapped into slot 4)")
        print()
        print("| Phys bank | First 32 bytes |")
        print("|-----------|----------------|")
        for bank in list(range(0x00, 0x20)) + list(range(0xA0, 0xC0)):
            try:
                c.set_slot(4, bank)
                d = c.read_mem(0x8000, 32)
                hexs = " ".join(f"{b:02X}" for b in d)
                print(f"| ${bank:02X} | {hexs} |")
            except Exception as e:
                print(f"| ${bank:02X} | ERR ({e}) |")
        # Restore slot 4
        try:
            c.set_slot(4, original_slots[4])
        except Exception:
            pass
        print()

        # Resume CPU (polite) — actually, leave paused so a follow-up
        # script can inspect more state without race.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
