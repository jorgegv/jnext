#!/usr/bin/env python3
"""
Task 18: poll CSpect screen RAM every ~250ms via DZRP for ~15s, saving each
snapshot to /tmp/banner-hunt/cspect/screen_NNNN.bin (6912 bytes raw).

CSpect must already be running with `-debug`. The script pauses CSpect briefly
each tick, reads $4000-$5AFF, resumes. The pauses are short (~1-5ms) and
shouldn't materially affect boot timing.

After this script finishes, convert dumps with:
  python3 tools/scr_to_png.py --batch /tmp/banner-hunt/cspect /tmp/banner-hunt/cspect_png
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cspect_dzrp import CSpectDZRP


def main():
    interval_s = 0.25
    duration_s = 16.0
    outdir = "/tmp/banner-hunt/cspect"
    os.makedirs(outdir, exist_ok=True)

    with CSpectDZRP() as c:
        c.init()
        # Start running. CSpect comes up halted with -debug; we cont() to
        # release it. Subsequent pause+resume cycles are how we sample.
        c.cont()
        # Give it a moment to start running.
        time.sleep(0.05)

        n = int(duration_s / interval_s)
        print(f"# Polling {n} snapshots over {duration_s}s ({interval_s}s interval)...")
        start = time.monotonic()
        for i in range(n):
            target = start + (i + 1) * interval_s
            now = time.monotonic()
            if now < target:
                time.sleep(target - now)
            try:
                c.pause()
                ntf = c.wait_for_pause(timeout=2.0)
                data = c.read_mem(0x4000, 0x1B00)  # 6912 bytes
                regs = c.get_registers()
                fn = os.path.join(outdir, f"screen_{i:04d}.bin")
                with open(fn, 'wb') as f:
                    f.write(data)
                print(f"  [{i:3d}] t={time.monotonic()-start:5.2f}s "
                      f"PC=${regs.PC:04X} slots={[f'${b:02X}' for b in regs.slots]} "
                      f"-> {fn}")
                c.cont()
            except Exception as e:
                print(f"  [{i:3d}] ERR: {e}")
                # Try to resume so further iterations work.
                try: c.cont()
                except Exception: pass
        print(f"# done. {n} dumps written.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
