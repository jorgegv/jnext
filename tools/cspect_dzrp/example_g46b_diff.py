"""example_g46b_diff.py — collect a CSpect ground-truth snapshot for G46(b).

The G46(b) bug: jnext's NextZXOS supervisor enters a tight loop at
bank-1 PC=$1661 reading memory at HL=$3EAF, looking for byte $22 or
$0D. Hypothesis: real-Next bank routing differs from jnext's, so the
bytes at HL+0..63 in CSpect would not match jnext's view.

This script:
  1. Connects to a running CSpect with DezogPlugin loaded.
  2. Sets a one-shot breakpoint at PC=$01ED (just before RST $28).
  3. Runs CSpect until that PC is hit.
  4. Dumps regs, NR $50..$57 slot mapping, and 64 bytes at HL.
  5. Steps the RST $28 (continues to PC+1) and dumps again.
  6. Prints everything as markdown for diffing against jnext's matching dump.

Pre-requisites:
  * CSpect running with DezogPlugin enabled (default port 11000).
  * Either CSpect already paused, or it will pause on hitting the BP.
  * The Z80 must reach PC=$01ED at some point during the boot sequence.

The script is read-only against CSpect state; it never writes memory,
registers, or NextRegs.

Usage:
    python3 example_g46b_diff.py                  # default 127.0.0.1:11000
    python3 example_g46b_diff.py --host 192.168.1.5 --port 11000
    python3 example_g46b_diff.py --pc 0x1661 --hl 0x3EAF --size 64
"""

from __future__ import annotations

import argparse
import sys
from typing import Optional

import cspect_dzrp as dz


def md_hex(data: bytes, addr: int, width: int = 16) -> str:
    out = ["```", "addr  +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F  ascii"]
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"{addr + i:04X}  {hexs:<{width * 3 - 1}}  {ascii_}")
    out.append("```")
    return "\n".join(out)


def snapshot(client: dz.CSpectDZRP, label: str,
             dump_addr: Optional[int], dump_size: int) -> str:
    regs = client.get_registers()
    out = [f"## {label}", "", "### Registers", "", "```", regs.format(), "```", ""]

    out.append("### NextReg slot mapping (NR $50..$57)")
    out.append("")
    out.append("| slot | addr range | bank |")
    out.append("|------|-----------|------|")
    for i, b in enumerate(regs.slots):
        lo = i * 0x2000
        hi = lo + 0x1FFF
        out.append(f"| {i} | ${lo:04X}-${hi:04X} | ${b:02X} |")
    out.append("")

    if dump_addr is not None:
        data = client.read_mem(dump_addr, dump_size)
        out.append(f"### Memory at ${dump_addr:04X} (size={dump_size})")
        out.append("")
        out.append(md_hex(data, dump_addr))
        out.append("")

    # A few NextRegs that matter for the G46(b) investigation.
    interesting_nr = [
        0x03,  # machine_type
        0x07,  # cpu_speed
        0x43,  # palette / ULAnext
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,  # MMU slots
        0x80,  # expansion bus
        0x8C,  # alt-rom
        0x8E,  # rom-bank / paging
    ]
    out.append("### Selected NextRegs")
    out.append("")
    out.append("| reg | value |")
    out.append("|-----|-------|")
    for r in interesting_nr:
        try:
            v = client.get_tbblue_reg(r)
            out.append(f"| ${r:02X} | ${v:02X} |")
        except Exception as e:  # noqa: BLE001
            out.append(f"| ${r:02X} | ERROR: {e} |")
    out.append("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=dz.DEFAULT_HOST)
    ap.add_argument("--port", type=int, default=dz.DEFAULT_PORT)
    ap.add_argument("--pc", type=lambda s: int(s, 0), default=0x01ED,
                    help="Run-to PC (default 0x01ED, just before RST $28).")
    ap.add_argument("--hl", type=lambda s: int(s, 0), default=None,
                    help="Address to dump (default: HL after stop).")
    ap.add_argument("--size", type=int, default=64,
                    help="Bytes to dump (default 64).")
    ap.add_argument("--no-step", action="store_true",
                    help="Skip the post-RST snapshot.")
    ap.add_argument("--timeout", type=float, default=60.0)
    args = ap.parse_args()

    print(f"# CSpect DZRP ground-truth snapshot")
    print(f"")
    print(f"- Host: `{args.host}:{args.port}`")
    print(f"- Run-to PC: `${args.pc:04X}`")
    print(f"")

    try:
        with dz.CSpectDZRP(args.host, args.port, timeout=args.timeout) as c:
            info = c.init()
            print(f"- DZRP {'.'.join(map(str, info.dzrp_version))}, "
                  f"machine={info.machine_type}, program={info.program_name!r}")
            print()

            print(f"# running CSpect to PC=${args.pc:04X} ...", file=sys.stderr)
            c.cont(tmp_bp1=args.pc)
            ntf = c.wait_for_pause(timeout=args.timeout)
            print(f"# stopped: {ntf.reason.name} @ ${ntf.long_address:06X}",
                  file=sys.stderr)

            regs = c.get_registers()
            dump_addr = args.hl if args.hl is not None else regs.HL
            print(snapshot(c, f"At PC=${regs.PC:04X} (pre-RST)",
                           dump_addr, args.size))

            if not args.no_step:
                # Step over (or really: continue past) the RST $28.
                # RST $28 is one byte; on entry CSpect pushed PC+1 and jumped
                # to $0028, so we set a tmp BP at $0028 to land *inside* the
                # ROM-call handler.
                print(f"# stepping into RST $28 (tmp BP at $0028) ...", file=sys.stderr)
                c.cont(tmp_bp1=0x0028)
                ntf = c.wait_for_pause(timeout=args.timeout)
                print(f"# stopped: {ntf.reason.name} @ ${ntf.long_address:06X}",
                      file=sys.stderr)
                regs2 = c.get_registers()
                # New slot mapping is read inside snapshot().
                print(snapshot(c, f"After RST $28 entry (PC=${regs2.PC:04X})",
                               regs2.HL, args.size))

    except (ConnectionRefusedError, OSError) as e:
        print(f"ERROR: could not connect to CSpect at {args.host}:{args.port}: {e}",
              file=sys.stderr)
        print(f"Hint: launch CSpect with DezogPlugin.dll in its plugins folder, "
              f"and verify Settings.Port == {args.port}.", file=sys.stderr)
        return 2
    except TimeoutError as e:
        print(f"ERROR: timed out waiting for CSpect: {e}", file=sys.stderr)
        return 3

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
