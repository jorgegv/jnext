#!/usr/bin/env python3
"""Minimal DZRP liveness check.

Connects to a running CSpect (with DezogPlugin loaded), dumps the
current CPU registers, slot mapping, machine ID and 16 bytes around
PC, then disconnects. No setup, no test program, no breakpoints —
just a sanity check that the plugin is reachable and the protocol
round-trips correctly.

Usage:
    python3 dzrp_check.py                  # default 127.0.0.1:11000
    python3 dzrp_check.py --host 10.0.0.5 --port 11000

Exit code: 0 on success, 1 on any protocol/connection failure.
"""
import argparse
import sys

from cspect_dzrp import CSpectDZRP


def main() -> int:
    p = argparse.ArgumentParser(description="DZRP liveness check")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=11000)
    p.add_argument("--mem-size", type=int, default=16,
                   help="bytes of memory to dump around PC (default 16)")
    args = p.parse_args()

    try:
        with CSpectDZRP(args.host, args.port) as c:
            v = c.init()
            print(f"Connected to {args.host}:{args.port} — DZRP v{v}")

            regs = c.get_registers()
            print()
            print("CPU registers:")
            print(f"  PC = {regs.PC:#06x}    SP = {regs.SP:#06x}")
            print(f"  AF = {regs.AF:#06x}    AF' = {regs.AFp:#06x}")
            print(f"  BC = {regs.BC:#06x}    BC' = {regs.BCp:#06x}")
            print(f"  DE = {regs.DE:#06x}    DE' = {regs.DEp:#06x}")
            print(f"  HL = {regs.HL:#06x}    HL' = {regs.HLp:#06x}")
            print(f"  IX = {regs.IX:#06x}    IY  = {regs.IY:#06x}")
            print(f"  I  = {regs.I:#04x}      R   = {regs.R:#04x}    IM = {regs.IM}")

            print()
            print("Slot mapping (NR $50–$57):")
            for slot, bank in enumerate(regs.slots):
                print(f"  slot {slot} ($%04x-$%04x) = bank ${bank:02x}"
                      % (slot * 0x2000, slot * 0x2000 + 0x1FFF))

            print()
            machine_id = c.get_tbblue_reg(0x00)
            core_major = c.get_tbblue_reg(0x01)
            core_minor = c.get_tbblue_reg(0x0E)
            print(f"NextReg $00 (machine ID)   = ${machine_id:02x}")
            print(f"NextReg $01 (core major)   = ${core_major:02x}")
            print(f"NextReg $0E (core minor)   = ${core_minor:02x}")

            print()
            n = max(1, args.mem_size)
            mem = c.read_mem(regs.PC, n)
            hex_bytes = " ".join(f"{b:02x}" for b in mem)
            print(f"Memory @ PC ({regs.PC:#06x}, {n} bytes):")
            print(f"  {hex_bytes}")

        print()
        print("OK — DZRP round-trip succeeded.")
        return 0
    except (OSError, ConnectionError, ValueError, TimeoutError) as e:
        print(f"FAIL: {type(e).__name__}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
