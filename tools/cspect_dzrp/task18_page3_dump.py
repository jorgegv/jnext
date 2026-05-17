#!/usr/bin/env python3
"""
Task 18: dump CSpect SRAM page 3 at the moment slot 7 = page 3.

We know jnext reads $00 from $FFFF (= page 3 offset $1FFF) at $0146 in the
init loop, while CSpect reads $DB. This script:

1. Connects to CSpect (running with `-debug`, halted at $0000).
2. Sets a BP at $0148 (one instruction after the divergent read).
3. Lets CSpect run until that BP fires twice (visit 1 then visit 2 — visit 2
   is when slot 7 has just been set to page 3 and the divergent value shows up).
4. At visit 2, dumps slot 7 ($E000-$FFFF) = physical page 3 (8KB).
5. Remaps slot 0 -> physical pages 0..15 and dumps the last 32 bytes of each.

For comparison, jnext's bypass loads enNextZX.rom into pages 0..7 contiguously:
   page 3 offset $1FE0-$1FFF in enNextZX.rom = $7FE0-$7FFF =
     `78 5C AF 32 7A 5C C9 3A 7F 5C E6 0F C8 FE 03 D8 CB 3F CB 3F 3D C9 00 00 00 00 00 00 00 00 00 00`
"""
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
        regs0 = c.get_registers()
        print(f"# Initial CSpect state (just after -debug halt)")
        print(f"#   PC=${regs0.PC:04X}  slots: {[f'${b:02X}' for b in regs0.slots]}")
        print(f"#   $FFFF = ${c.read_mem(0xFFFF, 1)[0]:02X}")
        print()

        # Add a persistent BP at $0148. Run, wait for first hit (visit 1).
        bp_id = c.add_breakpoint(0x0148)
        c.cont()
        ntf = c.wait_for_pause(timeout=40)
        regs1 = c.get_registers()
        print(f"# Visit 1 to $0148 (PC=${regs1.PC:04X}, A=${regs1.A:02X})")
        print(f"#   slots: {[f'${b:02X}' for b in regs1.slots]}")
        print()
        if regs1.PC != 0x0148:
            print(f"# UNEXPECTED: did not reach $0148 on cont(); got PC=${regs1.PC:04X}")
            return 1

        # Continue past visit 1 to visit 2. The BP at $0148 stays armed; we
        # need to step over the current instruction first or temp-BP at $0149.
        c.cont(tmp_bp1=0x0149)
        c.wait_for_pause(timeout=10)
        # Now at $0149, continue and wait for BP at $0148 to fire again.
        c.cont()
        ntf = c.wait_for_pause(timeout=40)
        regs2 = c.get_registers()
        print(f"# Visit 2 to $0148 (PC=${regs2.PC:04X}, A=${regs2.A:02X})")
        print(f"#   slots: {[f'${b:02X}' for b in regs2.slots]}")
        print(f"#   BC=${regs2.BC:04X} DE=${regs2.DE:04X} HL=${regs2.HL:04X}")
        print()
        if regs2.PC != 0x0148:
            print(f"# UNEXPECTED: did not reach $0148 visit 2; got PC=${regs2.PC:04X}")
            return 1

        # Sanity-check: what's at $FFFF right now?
        last_byte = c.read_mem(0xFFFF, 1)
        print(f"# Memory at $FFFF (slot 7 last byte, expected page 3 last byte): ${last_byte[0]:02X}")
        print(f"# (jnext bypass reads $00 here; trace shows CSpect reads $DB)")
        print()

        original = list(regs2.slots)
        print(f"# At visit 2, slot 7 should be physical page 3 (NR $57=3). Actual bank ID: ${original[7]:02X}")
        print()

        # Dump entire slot 7 ($E000-$FFFF) = current page 3 (8KB).
        print(f"## Slot 7 ($E000-$FFFF) — currently mapped to bank ${original[7]:02X}")
        print()
        print("```")
        d = c.read_mem(0xE000, 0x2000)
        print("-- first 256 bytes (page offset $0000-$00FF):")
        print(hexdump(0xE000, d[:256]))
        print()
        print("-- last 256 bytes (page offset $1F00-$1FFF, includes $DB at $FFFF):")
        print(hexdump(0xFF00, d[-256:]))
        print("```")
        print()

        # Systematically remap slot 0 to pages 0..15 and dump 1st + last 32
        # bytes of each. The 16K-paging legacy mappings ($FF*) are not 8K
        # physical pages, so skip them. Set NR $50 directly via tbblue reg.
        print("## Slot-0 remapping probe: first + last 32 bytes of pages 0..15")
        print()
        print("Pages 0..7 SHOULD match enNextZX.rom byte ranges N*8192..N*8192+8191")
        print("if tbblue.fw's load_roms() simply copied the file. They probably don't.")
        print()
        print("| Page | First 32 bytes (offset $0000-$001F) | Last 32 bytes (offset $1FE0-$1FFF) |")
        print("|------|--------------------------------------|-------------------------------------|")
        for page in range(16):
            try:
                c.set_slot(0, page)
                first = c.read_mem(0x0000, 0x20)
                last = c.read_mem(0x1FE0, 0x20)
                fh = " ".join(f"{b:02X}" for b in first)
                lh = " ".join(f"{b:02X}" for b in last)
                print(f"| {page:2d} | `{fh}` | `{lh}` |")
            except Exception as e:
                print(f"| {page:2d} | ERR: {e} | |")
        c.set_slot(0, original[0])
        print()
        print("## Comparison reference — enNextZX.rom byte ranges:")
        print()
        print("| Page (jnext bypass layout) | enNextZX.rom offset | First 32 bytes | Last 32 bytes |")
        print("|-----------------------------|---------------------|----------------|---------------|")
        try:
            with open("/home/jorgegv/src/spectrum/jnext/.claude/worktrees/enNextZX-shared.rom", "rb") as f:
                rom = f.read()
            for page in range(8):
                base = page * 0x2000
                first = rom[base:base+0x20]
                last = rom[base+0x1FE0:base+0x2000]
                fh = " ".join(f"{b:02X}" for b in first)
                lh = " ".join(f"{b:02X}" for b in last)
                print(f"| {page} | ${base:04X}-${base+0x1FFF:04X} | `{fh}` | `{lh}` |")
        except Exception as e:
            print(f"could not read enNextZX.rom: {e}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
