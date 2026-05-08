"""g46b_at_1661.py — capture CSpect state at PC=$1661 to resolve NR $8E routing.

Sets a 64K BP at $1661 (the bank-call target inside RST $28; DW $1661).
Whatever bank CSpect's bank-flip lands us in, slot 0/1 will be mapped
accordingly when the BP fires. Compare slot mapping with jnext's
post-bank-flip state to confirm whether real Next does 1-bit (bank 1)
or 4-way (bank 3) NR $8E routing.
"""
from __future__ import annotations

import sys

import cspect_dzrp as dz


def hexdump(data: bytes, addr: int, width: int = 16) -> None:
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        h = " ".join(f"{b:02X}" for b in chunk)
        a = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  {addr + i:04X}  {h:<{width * 3 - 1}}  {a}")


def main() -> int:
    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"DZRP {info.dzrp_version} {info.program_name!r}")

        regs0 = c.get_registers()
        print(f"current PC=${regs0.PC:04X} (will continue from here)")

        # Set a 64K BP at $1661 (bank=0).
        bp_id = c.add_breakpoint(0x1661)
        print(f"BP at $1661 set (id={bp_id})")

        c.cont()
        print("running...")
        ntf = c.wait_for_pause(timeout=120.0)
        print(f"stopped: reason={ntf.reason.name} long_addr=${ntf.long_address:06X}")

        regs = c.get_registers()
        print()
        print(f"At PC=${regs.PC:04X}:")
        print(f"  HL=${regs.HL:04X} BC=${regs.BC:04X} DE=${regs.DE:04X} "
              f"AF=${regs.AF:04X} SP=${regs.SP:04X}")
        print(f"  Slots:", " ".join(f"{b:02X}" for b in regs.slots))

        # Read 16 bytes at PC (= the actual instruction stream at $1661).
        pc_mem = c.read_mem(regs.PC, 16)
        print(f"  Memory @ PC=${regs.PC:04X}:")
        hexdump(pc_mem, regs.PC)

        # Read 64 bytes at HL ($3EAF expected) — the loop's input buffer.
        hl_mem = c.read_mem(regs.HL, 64)
        print(f"  Memory @ HL=${regs.HL:04X}:")
        hexdump(hl_mem, regs.HL)

        # NextReg state
        nr_03 = c.get_tbblue_reg(0x03)
        nr_8e = c.get_tbblue_reg(0x8E)
        nr_8c = c.get_tbblue_reg(0x8C)
        print(f"  NR $03=${nr_03:02X} NR $8C=${nr_8c:02X} NR $8E=${nr_8e:02X}")

        # Cleanup
        c.remove_breakpoint(bp_id)

    return 0


if __name__ == "__main__":
    sys.exit(main())
