#!/usr/bin/env python3
"""Drive CSpect to PC=$00EF via DZRP so the Task18Snapshot plugin fires.

The plugin (Task18Snapshot.dll) hooks Memory_EXE on $00EF and writes the
full state snapshot (NextREGs + Z80 incl IFF1/IFF2 + 2 MiB physical SRAM
+ eGlobals) to the file in JNEXT_SNAPSHOT_PATH.

DZRP role: kick CSpect out of -debug halt and let the CPU run until the
plugin captures the state.
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


def main():
    out = os.environ.get("JNEXT_SNAPSHOT_PATH", "/tmp/cspect_00EF_snapshot_v2.bin")

    with CSpectDZRP() as c:
        info = c.init()
        print(f"[capture] DZRP {'.'.join(map(str, info.dzrp_version))} program={info.program_name!r}")

        try: c.pause()
        except Exception: pass

        # DZRP BP fires BEFORE the Memory_EXE plugin hook, so set the BP
        # one instruction PAST the capture point. The plugin filters
        # internally for PC=$00EF, fires when CPU passes through, then
        # CPU continues until BP at $00F3 halts it.
        bp_pc = 0x00F3
        print(f"[capture] Continuing with temp BP at ${bp_pc:04X}"
              f" (plugin captures at $00EF before this)...")
        c.cont(tmp_bp1=bp_pc)
        pause = c.wait_for_pause(timeout=20.0)
        regs = c.get_registers()
        print(f"[capture] Paused at PC=${regs.PC:04X}  reason={pause.reason}")

        # Give the plugin a moment to finish writing the file.
        for _ in range(40):
            if os.path.exists(out):
                size = os.path.getsize(out)
                if size >= 2097436:
                    print(f"[capture] snapshot ready: {out} ({size} bytes)")
                    return 0
                else:
                    print(f"[capture] partial: {size} / 2097436, waiting...")
            time.sleep(0.25)
        print(f"[capture] WARNING: snapshot file did not reach expected size")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
