#!/usr/bin/env python3
"""Task 77 Phase 1 — probe CSpect's joystick mode + port 0x1F/0x37 bits.

Question: with a USB pad connected, what joystick mode does CSpect run in,
and does it model the VHDL's port-bit gating (bits 7:6 driven ONLY in MD
mode, zeroed in Kempston — zxnext.vhd:3470-3494)?
"""
import os, sys, time

# Import cspect_dzrp from THIS script's directory, so the script works from
# any clone or worktree (GH #204).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP

MODES = {
    0b000: "Sinclair 2 (12345)",
    0b001: "Kempston 1 (port 0x1F)",
    0b010: "Cursor",
    0b011: "Sinclair 1 (67890)",
    0b100: "Kempston 2 (port 0x37)",
    0b101: "MD 1 (3 or 6 button joystick)",
    0b110: "MD 2 (3 or 6 button joystick)",
    0b111: "User defined keys joystick",
}

def decode(mode):
    return MODES.get(mode, "?")

with CSpectDZRP() as c:
    c.pause()
    nr05 = c.get_tbblue_reg(0x05)
    # zxnext.vhd:5157-5158
    #   nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7 downto 6);
    #   nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5 downto 4);
    joy0 = (((nr05 >> 3) & 1) << 2) | ((nr05 >> 6) & 0b11)
    joy1 = (((nr05 >> 1) & 1) << 2) | ((nr05 >> 4) & 0b11)
    print(f"NR 0x05 = 0x{nr05:02X} (0b{nr05:08b})")
    print(f"  joy0 (left)  = 0b{joy0:03b} -> {decode(joy0)}")
    print(f"  joy1 (right) = 0b{joy1:03b} -> {decode(joy1)}")
    print()

    print("Sampling ports 0x1F / 0x37 for 25 s (press pad buttons NOW)...")
    seen1f, seen37 = {}, {}
    t0 = time.time()
    while time.time() - t0 < 25:
        v1f = c.read_port(0x1F)
        v37 = c.read_port(0x37)
        seen1f[v1f] = seen1f.get(v1f, 0) + 1
        seen37[v37] = seen37.get(v37, 0) + 1
        time.sleep(0.02)

    for name, seen in (("0x1F", seen1f), ("0x37", seen37)):
        print(f"\nport {name} distinct values:")
        for v in sorted(seen):
            print(f"  0x{v:02X}  0b{v:08b}   n={seen[v]}")
        union = 0
        for v in seen:
            union |= v
        print(f"  union of all bits seen: 0x{union:02X} 0b{union:08b}")
        print(f"  bit6 (A) ever set: {bool(union & 0x40)}   bit7 (START) ever set: {bool(union & 0x80)}")
