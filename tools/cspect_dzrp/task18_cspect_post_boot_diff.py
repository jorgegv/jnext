#!/usr/bin/env python3
"""Task 18 — Post-boot state capture from CSpect (apples-to-apples diff).

Captures the exact same state we have for jnext bypass-mode after NextZXOS
boot, so the two can be diffed directly.

Captures:
  - Z80 registers + 8 MMU slot bytes
  - Curated NextRegs (the set the jnext bypass dump exposes)
  - 128 bytes at $5C00 (BASIC sysvars)
  - 128 bytes at $4000 (start of pixel area)
  - 64  bytes at $5800 (start of attribute area)
  - 32  bytes at $0000 (CPU view of slot 0 = current ROM)

Usage:
  Launch CSpect first (see tools/cspect_dzrp/README.md):
    timeout --kill-after=2s 30s mono \\
        /home/jorgegv/src/spectrum/CSpect3_1_0_0/CSpect.exe \\
        -mmc roms/nextzxos-1gb-fat32fix.img
  Wait ~12-15s for NextZXOS welcome screen. Then:
    python3 tools/cspect_dzrp/task18_cspect_post_boot_diff.py

Writes markdown to stdout — pipe to the diff doc.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


# NRs we need for the diff (matches the worktree's jnext bypass dump).
NR_LIST = (
    0x03, 0x07, 0x0A, 0x0E,
    0x12, 0x13, 0x14, 0x15,
    0x40, 0x41, 0x42, 0x43, 0x44,
    0x4A, 0x4B, 0x4C,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x68, 0x69, 0x6B,
    0x7F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
    0x8A, 0x8C, 0x8E, 0x8F,
    0xB8, 0xB9, 0xBA, 0xBB,
    0xC0, 0xD8,
)


def hexdump(addr: int, data: bytes, width: int = 16) -> str:
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        lines.append(f"  ${addr + i:04X}  {hexs:<{width * 3 - 1}}  {ascii_}")
    return "\n".join(lines)


def main() -> int:
    with CSpectDZRP() as c:
        info = c.init()
        print(f"# CSpect post-boot diff capture")
        print()
        print(f"DZRP {'.'.join(map(str, info.dzrp_version))} / "
              f"machine={info.machine_type} / program={info.program_name!r}")
        print()

        # Pause the free-running CSpect.
        c.pause()
        regs = c.get_registers()

        print("## Z80 Registers")
        print()
        print("```")
        print(regs.format())
        print("```")
        print()

        # IM / IFF aren't in the Registers struct's default format; show
        # what we have.
        print(f"IM={regs.IM}  I=${regs.I:02X}  R=${regs.R:02X}")
        print()

        print("## MMU slots (NR $50..$57)")
        print()
        print("```")
        print(" ".join(f"{b:02X}" for b in regs.slots))
        print("```")
        print()

        print("## NextRegs (curated set)")
        print()
        print("| Reg | Value |")
        print("|-----|-------|")
        for r in NR_LIST:
            try:
                v = c.get_tbblue_reg(r)
                print(f"| ${r:02X} | ${v:02X} |")
            except Exception as e:
                print(f"| ${r:02X} | ERR ({e}) |")
        print()

        print("## Memory dumps")
        print()

        sections = (
            ("BASIC sysvars at $5C00 (128 bytes)", 0x5C00, 128),
            ("Pixel area $4000 (128 bytes)", 0x4000, 128),
            ("Attribute area $5800 (64 bytes)", 0x5800, 64),
            ("Slot 0 ROM view at $0000 (32 bytes)", 0x0000, 32),
        )
        for title, addr, size in sections:
            print(f"### {title}")
            print()
            print("```")
            try:
                data = c.read_mem(addr, size)
                print(hexdump(addr, data))
            except Exception as e:
                print(f"ERR: {e}")
            print("```")
            print()

        # Larger attribute-area scan for banner detection — 768 bytes
        # covers the full attribute area. Useful to see if CSpect actually
        # drew anything beyond CLS.
        print("### Full attribute area $5800-$5AFF (768 bytes)")
        print()
        print("```")
        try:
            data = c.read_mem(0x5800, 768)
            print(hexdump(0x5800, data))
        except Exception as e:
            print(f"ERR: {e}")
        print("```")
        print()

        # Pixel rows 0-15 from the attribute area's first character row —
        # if CSpect drew text, this is where the (c) Spectrum / NextZXOS
        # banner would start. Sample 256 bytes for visibility.
        print("### Pixel area $4000-$40FF (256 bytes — first text rows)")
        print()
        print("```")
        try:
            data = c.read_mem(0x4000, 256)
            print(hexdump(0x4000, data))
        except Exception as e:
            print(f"ERR: {e}")
        print("```")
        print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
