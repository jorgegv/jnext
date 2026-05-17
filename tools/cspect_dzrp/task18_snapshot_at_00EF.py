#!/usr/bin/env python3
"""Task 18 — capture CSpect's complete state at PC=$00EF (NextZXOS entry).

Brute-force snapshot for the bypass-firmware investigation: dump everything
needed to make jnext's `--bypass-tbblue-fw` start with CSpect-byte-identical
state, then jump to $00EF.

Output (in `/tmp/cspect_00EF_snapshot/`):
  - `regs.txt`            — Z80 register state at $00EF entry
  - `nextregs.bin`        — 256 bytes, NR $00..$FF as captured
  - `sram_pages.bin`      — 2 MiB, 256 × 8 KiB pages (page $XX at offset $XX*$2000)
  - `manifest.txt`        — human-readable summary

The bypass init in jnext can then memcpy this into Ram + NextReg + Z80 state
to start at $00EF in CSpect-identical state.
"""
from __future__ import annotations
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP

OUT_DIR = "/tmp/cspect_00EF_snapshot"
PAGE_SIZE = 0x2000
NUM_PAGES = 256  # full 2 MiB SRAM

def main() -> int:
    os.makedirs(OUT_DIR, exist_ok=True)

    with CSpectDZRP() as c:
        info = c.init()
        print(f"[snapshot] DZRP {'.'.join(map(str, info.dzrp_version))} "
              f"machine={info.machine_type} program={info.program_name!r}")

        # Ensure CSpect is halted (it will be at $0000 with -debug).
        try:
            c.pause()
        except Exception:
            pass

        # Continue until PC=$00EF (first NextZXOS post-IPL instruction).
        print("[snapshot] Adding temp BP at $00EF, continuing...")
        c.cont(tmp_bp1=0x00EF)
        pause = c.wait_for_pause(timeout=20.0)
        print(f"[snapshot] Paused: reason={pause.reason} addr=${pause.long_address:06X} "
              f"reason_str={pause.reason_string!r}")

        regs = c.get_registers()
        if regs.PC != 0x00EF:
            print(f"[snapshot] WARNING: PC=${regs.PC:04X}, expected $00EF — aborting")
            return 1

        # Capture Z80 registers.
        regs_path = os.path.join(OUT_DIR, "regs.txt")
        with open(regs_path, "w") as f:
            f.write(regs.format() + "\n")
            f.write(f"\nslots: {[f'${b:02X}' for b in regs.slots]}\n")
        print(f"[snapshot] regs -> {regs_path}")
        print("  " + regs.format().replace("\n", "\n  "))
        print(f"  slots: {[f'${b:02X}' for b in regs.slots]}")

        # Capture all 256 NextREGs via GET_TBBLUE_REG.
        nr_path = os.path.join(OUT_DIR, "nextregs.bin")
        nrs = bytearray(256)
        print("[snapshot] Reading all 256 NextREGs...")
        t0 = time.time()
        for r in range(256):
            try:
                nrs[r] = c.get_tbblue_reg(r) & 0xFF
            except Exception as e:
                print(f"  NR ${r:02X}: ERR {e}")
                nrs[r] = 0
        with open(nr_path, "wb") as f:
            f.write(bytes(nrs))
        print(f"[snapshot] nextregs -> {nr_path} ({time.time()-t0:.1f}s)")

        # Capture all 256 SRAM pages via slot-0 remap.
        # Strategy: save original slot-0 mapping, set slot 0 to each page in turn,
        # read_mem(0, 0x2000), restore at end.
        sram_path = os.path.join(OUT_DIR, "sram_pages.bin")
        original_slot0 = regs.slots[0]
        sram = bytearray(NUM_PAGES * PAGE_SIZE)
        print(f"[snapshot] Reading {NUM_PAGES} SRAM pages × 8 KiB via slot-0 remap...")
        t0 = time.time()
        for page in range(NUM_PAGES):
            try:
                c.set_slot(0, page)
                data = c.read_mem(0x0000, PAGE_SIZE)
                sram[page * PAGE_SIZE : (page + 1) * PAGE_SIZE] = data
            except Exception as e:
                print(f"  page ${page:02X}: ERR {e}")
            if page % 32 == 31:
                print(f"  ... {page+1}/{NUM_PAGES} pages ({time.time()-t0:.1f}s)")
        # Restore slot-0 to original.
        try:
            c.set_slot(0, original_slot0)
        except Exception as e:
            print(f"[snapshot] restore slot-0: {e}")
        with open(sram_path, "wb") as f:
            f.write(bytes(sram))
        print(f"[snapshot] sram_pages -> {sram_path} ({time.time()-t0:.1f}s, {len(sram)} bytes)")

        # Manifest.
        nonzero_pages = sum(1 for p in range(NUM_PAGES) if any(sram[p*PAGE_SIZE:(p+1)*PAGE_SIZE]))
        manifest_path = os.path.join(OUT_DIR, "manifest.txt")
        with open(manifest_path, "w") as f:
            f.write("CSpect $00EF snapshot — manifest\n")
            f.write("================================\n")
            f.write(f"PC          : ${regs.PC:04X}\n")
            f.write(f"AF/BC/DE/HL : ${regs.AF:04X} ${regs.BC:04X} ${regs.DE:04X} ${regs.HL:04X}\n")
            f.write(f"AF'/BC'/DE'/HL': ${regs.AFp:04X} ${regs.BCp:04X} ${regs.DEp:04X} ${regs.HLp:04X}\n")
            f.write(f"IX/IY/SP    : ${regs.IX:04X} ${regs.IY:04X} ${regs.SP:04X}\n")
            f.write(f"I/R         : ${regs.I:02X} ${regs.R:02X}\n")
            f.write(f"IM          : {regs.IM}\n")
            f.write(f"slots       : {[f'${b:02X}' for b in regs.slots]}\n")
            f.write(f"\nNextREG non-default summary (vs reset $00):\n")
            for r in range(256):
                if nrs[r] != 0:
                    f.write(f"  NR ${r:02X} = ${nrs[r]:02X}\n")
            f.write(f"\nSRAM non-zero pages: {nonzero_pages} / {NUM_PAGES}\n")
        print(f"[snapshot] manifest -> {manifest_path}")
        print(f"[snapshot] {nonzero_pages} non-zero SRAM pages out of {NUM_PAGES}")

    print("[snapshot] done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
