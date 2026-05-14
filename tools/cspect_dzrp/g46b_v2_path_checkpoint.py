"""G46(b)-v2 EOD-28 Step 2 — CSpect-vs-jnext PC-path checkpoint diff.

Install BPs at sparse checkpoints jnext visited during its first
dispatcher cycle (PC stream from JNEXT_G46B_PCTRACE=1). Resume CSpect
from $0000 (-debug mode). Capture order in which checkpoints fire.

Compare against jnext's PC trace order:
  $3CFC -> $3D00 -> ... -> $3D09 (DJNZ) -> $3D1B -> ... -> $3D54 -> ... -> $0448

If CSpect's hit-order matches jnext through $3D75 but then diverges
(e.g. CSpect reaches $0448 directly while jnext spirals further), the
divergence point is between $3D75 and $0448 in CSpect's path — but in
jnext's, the supervisor is still grinding through font data and never
RETs out cleanly.

If CSpect hits $5B00, jnext's hypothesis "wrapper is dead code in
CSpect" (EOD-27) breaks.

If CSpect HITS the same $3D series PCs as jnext, no divergence in
this window. Extend the trace forward.
"""
from __future__ import annotations
import sys
import time

sys.path.insert(0, '/home/jorgegv/src/spectrum/jnext/tools/cspect_dzrp')
import cspect_dzrp as dz


CHECKPOINTS = [
    0x3CFC,  # entry: NEXTREG $8E,$03
    # Fine-grain through the post-flip NOP sled to see CSpect's actual path.
    0x3D00, 0x3D01, 0x3D02, 0x3D03, 0x3D04,
    0x3D05, 0x3D06, 0x3D07, 0x3D08, 0x3D09,
    0x0448,  # CSpect's known dispatcher-return target (per EOD-27)
]


def main() -> int:
    timeout_s = 30.0
    with dz.CSpectDZRP() as c:
        info = c.init()
        regs0 = c.get_registers()
        print(f"# initial PC=${regs0.PC:04X}")

        bp_ids = {}
        for addr in CHECKPOINTS:
            bp_ids[addr] = c.add_breakpoint(addr)
        print(f"# {len(bp_ids)} BPs set: {', '.join(f'${a:04X}' for a in CHECKPOINTS)}")

        c.cont()
        t0 = time.monotonic()
        hits = []

        while time.monotonic() - t0 < timeout_s and len(hits) < 80:
            try:
                remaining = timeout_s - (time.monotonic() - t0)
                if remaining <= 0:
                    break
                ntf = c.wait_for_pause(timeout=remaining)
            except Exception as e:
                print(f"# wait timeout / error: {e}")
                break

            r = c.get_registers()
            hits.append((r.PC, r.AF, r.BC, r.DE, r.HL, r.SP))
            n = len(hits)
            print(
                f"hit #{n:2d} pc=${r.PC:04X} "
                f"AF={r.AF:04X} BC={r.BC:04X} DE={r.DE:04X} "
                f"HL={r.HL:04X} SP={r.SP:04X}"
            )

            # Continue past this checkpoint
            c.cont()

        # Cleanup
        try:
            c.pause()
            time.sleep(0.05)
        except Exception:
            pass
        for addr, bp in bp_ids.items():
            try:
                c.remove_breakpoint(bp)
            except Exception:
                pass
        try:
            c.cont()
        except Exception:
            pass

        # Summary
        print()
        print("=" * 60)
        print("# SUMMARY")
        from collections import Counter
        ctr = Counter(h[0] for h in hits)
        for addr in CHECKPOINTS:
            print(f"  ${addr:04X}: {ctr.get(addr, 0)} hits")
        print(
            f"  total hits: {len(hits)} in "
            f"{time.monotonic() - t0:.1f}s"
        )

        if hits:
            print()
            print("# ORDER (first 30 hits):")
            for i, h in enumerate(hits[:30]):
                print(f"  {i+1:2d}: ${h[0]:04X}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
