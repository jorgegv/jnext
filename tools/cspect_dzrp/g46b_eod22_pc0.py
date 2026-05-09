"""g46b_eod22_pc0.py — does CSpect's NextZXOS land at PC=$0000 after boot?

Set a breakpoint at PC=$0000 and let CSpect run. If the breakpoint
fires within the timeout, capture full state (registers, stack words,
slot map, NextRegs). If it doesn't fire, that's the ground-truth
answer: real-Next does NOT land at $0000 in normal post-boot operation.

Used to validate jnext's EOD-21 finding that PC lands at $0000 ~640
times in 6 minutes after the NR $03 fix landed.
"""
from __future__ import annotations

import sys
import time

import cspect_dzrp as dz


def hexdump(data: bytes, addr: int, width: int = 16) -> str:
    out = []
    for i in range(0, len(data), width):
        chunk = data[i:i + width]
        h = " ".join(f"{b:02X}" for b in chunk)
        a = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"  {addr + i:04X}  {h:<{width * 3 - 1}}  {a}")
    return "\n".join(out)


def main() -> int:
    timeout_s = 90.0  # how long to wait for BP at $0000 to fire

    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} program={info.program_name!r}")

        regs0 = c.get_registers()
        print(f"# pre-BP state: {regs0.format()}")
        nr03_0 = c.get_tbblue_reg(0x03)
        nr07_0 = c.get_tbblue_reg(0x07)
        print(f"# NR $03=${nr03_0:02X} NR $07=${nr07_0:02X}")

        bp_id = c.add_breakpoint(0x0000)
        print(f"# BP at $0000 set (id={bp_id}); waiting up to {timeout_s:.0f}s ...")

        t0 = time.monotonic()
        c.cont()

        try:
            ntf = c.wait_for_pause(timeout=timeout_s)
        except TimeoutError:
            elapsed = time.monotonic() - t0
            print(f"# TIMEOUT after {elapsed:.1f}s — BP at $0000 did NOT fire")
            print("# pausing CPU to inspect current state")
            try:
                c.pause()
                ntf2 = c.wait_for_pause(timeout=5.0)
                regs = c.get_registers()
                print(f"# post-pause state: {regs.format()}")
                print(f"# pause reason={ntf2.reason.name} long_addr=${ntf2.long_address:06X}")
            except Exception as e:
                print(f"# pause failed: {e}")
            try:
                c.remove_breakpoint(bp_id)
            except Exception:
                pass
            print("VERDICT: PC=$0000 NOT REACHED in normal CSpect operation")
            return 0

        elapsed = time.monotonic() - t0
        print(f"# BP fired after {elapsed:.2f}s")
        print(f"# stop reason={ntf.reason.name} long_addr=${ntf.long_address:06X}")

        regs = c.get_registers()
        print()
        print("## At PC=$0000:")
        print(f"  {regs.format()}")

        # Read stack: top 16 words from SP. Cap at 0x10000.
        sp_top_words = []
        for i in range(16):
            sp_addr = (regs.SP + i * 2) & 0xFFFF
            try:
                lo_hi = c.read_mem(sp_addr, 2)
                w = lo_hi[0] | (lo_hi[1] << 8)
                sp_top_words.append((sp_addr, w))
            except Exception as e:
                sp_top_words.append((sp_addr, None))
                print(f"# stack read failed at ${sp_addr:04X}: {e}")
                break

        print()
        print("## Stack (16 words from SP):")
        for sp_addr, w in sp_top_words:
            if w is None:
                print(f"  [{sp_addr:04X}] = ??")
            else:
                # If `w` looks like a code address (within 64K), suggest it as a candidate prev_pc
                print(f"  [{sp_addr:04X}] = ${w:04X}")

        # Read 16 bytes at PC ($0000)
        pc_mem = c.read_mem(0x0000, 16)
        print()
        print("## Memory @ PC=$0000 (16 bytes — boot vector):")
        print(hexdump(pc_mem, 0x0000))

        # Slot map
        print()
        print(f"## Slot map (NR $50..$57): {' '.join(f'${b:02X}' for b in regs.slots)}")

        # NextReg state
        nr_03 = c.get_tbblue_reg(0x03)
        nr_07 = c.get_tbblue_reg(0x07)
        nr_8c = c.get_tbblue_reg(0x8C)
        nr_8e = c.get_tbblue_reg(0x8E)
        nr_43 = c.get_tbblue_reg(0x43)
        print()
        print("## NextRegs:")
        print(f"  NR $03 (machine type)  = ${nr_03:02X}")
        print(f"  NR $07 (cpu speed)     = ${nr_07:02X}")
        print(f"  NR $43 (palette ctrl)  = ${nr_43:02X}")
        print(f"  NR $8C (altrom)        = ${nr_8c:02X}")
        print(f"  NR $8E (paging legacy) = ${nr_8e:02X}")

        # IM/IFF: not directly exposed via DZRP — log IM (in regs) and a port read sentinel
        print()
        print(f"## CPU flags: IM={regs.IM} (IFF1/IFF2 not exposed via DZRP)")

        # Read 256 bytes around the bytes prev_pc candidates point to
        candidates = [w for _, w in sp_top_words[:4] if w is not None and w != 0]
        print()
        print("## Candidate prev_pc disassembly hints (top-4 stack words, peek 8 bytes each):")
        for cand in candidates:
            try:
                m = c.read_mem(cand & 0xFFFF, 8)
                hex_s = " ".join(f"{b:02X}" for b in m)
                print(f"  ${cand:04X}: {hex_s}")
            except Exception as e:
                print(f"  ${cand:04X}: read failed ({e})")

        c.remove_breakpoint(bp_id)
        print()
        print("VERDICT: PC=$0000 REACHED in CSpect normal operation")

    return 0


if __name__ == "__main__":
    sys.exit(main())
