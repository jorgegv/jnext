"""g46b_eod22_5b6a_capture.py — capture CSpect state of $5B52/$5B6A sysvars
plus optionally HL at the bank 1 $26B9 wrapper site.

Two modes (selected via --mode):

  idle       : read 16 bytes at $5B50..$5B6F at the current (idle) PC. Use
               this when CSpect has been left to free-run to its
               post-boot REPL (PC ~ $0C90). Verdict: are $5B52 and $5B6A
               non-zero? If yes, supervisor initialised them somewhere in
               boot — and jnext's $5B6A=0 hypothesis is corroborated.

  trace      : set persistent BPs at $26B6 (pre-CALL $32CC), $26B9
               (post-CALL $32CC) and $26C2 (final JP $3E93). Capture full
               state at each hit. Use this on a CSpect launched with
               -debug so it halts at $0000 — we install BPs before the
               supervisor reaches that wrapper site.

Both modes also dump $5B50..$5B6F memory so we can spot non-zero pointer
words for $5B52, $5B54, $5B6A.
"""
from __future__ import annotations

import argparse
import sys
import time
from typing import Optional

import cspect_dzrp as dz


WRAPPER_BPS = [
    (0x26B6, "pre-CALL $32CC (HL/$5B52/$5B6A)"),
    (0x26B9, "post-CALL $32CC (HL is the wrapper target)"),
    (0x26C2, "final JP $3E93 (about to push $007B and jp altrom-toggle)"),
]


def _hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def _word_le(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8)


def dump_sysvars(c: dz.CSpectDZRP, label: str) -> dict:
    """Read 16 bytes at $5B50..$5B5F + 16 bytes at $5B60..$5B6F = 32 bytes total.

    Specifically interpret:
      $5B52 (LE word)
      $5B54 (LE word)
      $5B6A (LE word)
    """
    data = c.read_mem(0x5B50, 32)
    snap = {
        "raw": data,
        "5B52": _word_le(data, 0x02),
        "5B54": _word_le(data, 0x04),
        "5B6A": _word_le(data, 0x1A),
    }
    print(f"\n[{label}]  mem @ $5B50..$5B6F:")
    print(f"  $5B50  {_hex(data[0x00:0x10])}")
    print(f"  $5B60  {_hex(data[0x10:0x20])}")
    print(f"  -> ($5B52)=${snap['5B52']:04X}  "
          f"($5B54)=${snap['5B54']:04X}  "
          f"($5B6A)=${snap['5B6A']:04X}")
    return snap


def dump_regs(c: dz.CSpectDZRP, label: str) -> dz.Registers:
    r = c.get_registers()
    print(f"\n[{label}]  PC=${r.PC:04X} SP=${r.SP:04X}")
    print(f"  AF=${r.AF:04X} BC=${r.BC:04X} DE=${r.DE:04X} HL=${r.HL:04X}")
    print(f"  IX=${r.IX:04X} IY=${r.IY:04X}  I=${r.I:02X} R=${r.R:02X} IM={r.IM}")
    print(f"  slots [NR $50..$57]: {_hex(bytes(r.slots))}")
    # Stack 8 words
    stk = []
    for i in range(8):
        a = (r.SP + i * 2) & 0xFFFF
        try:
            b = c.read_mem(a, 2)
            stk.append((a, b[0] | (b[1] << 8)))
        except Exception:
            break
    print(f"  stack@SP:")
    for a, w in stk:
        print(f"    [{a:04X}]=${w:04X}")
    return r


def mode_idle(c: dz.CSpectDZRP) -> int:
    print("# mode=idle — assume CSpect free-running at REPL")
    # Pause first to read state coherently.
    try:
        c.pause()
        c.wait_for_pause(timeout=3.0)
    except TimeoutError:
        pass
    except Exception:
        pass
    dump_regs(c, "idle (paused)")
    dump_sysvars(c, "idle (paused)")
    # Resume so CSpect doesn't sit halted.
    try:
        c.cont()
    except Exception:
        pass
    return 0


def mode_trace(c: dz.CSpectDZRP) -> int:
    print("# mode=trace — install BPs at $26B6/$26B9/$26C2 and capture each hit")

    # Pause (CSpect was launched with -debug so already paused at $0000).
    try:
        c.pause()
        c.wait_for_pause(timeout=3.0)
    except TimeoutError:
        pass
    except Exception:
        pass

    r0 = c.get_registers()
    print(f"# initial PC=${r0.PC:04X}")

    # Install the three persistent BPs.
    bp_ids: dict[int, int] = {}
    for addr, desc in WRAPPER_BPS:
        try:
            bid = c.add_breakpoint(addr)
            bp_ids[addr] = bid
            print(f"# BP at ${addr:04X} id={bid} — {desc}")
        except Exception as e:
            print(f"# BP at ${addr:04X} FAILED: {e}")

    # Run, capture each hit. We allow each BP to fire UP TO twice
    # (some wrappers re-fire) but cap total hits at 12.
    max_hits = 12
    hits = 0
    seen_count: dict[int, int] = {a: 0 for a, _ in WRAPPER_BPS}
    captures: list[dict] = []

    while hits < max_hits:
        t0 = time.monotonic()
        c.cont()
        try:
            ntf = c.wait_for_pause(timeout=60.0)
        except TimeoutError:
            el = time.monotonic() - t0
            print(f"\n# TIMEOUT after {el:.1f}s — no further BP fired")
            try:
                c.pause()
                c.wait_for_pause(timeout=3.0)
                rp = c.get_registers()
                print(f"# paused at PC=${rp.PC:04X}")
            except Exception:
                pass
            break

        el = time.monotonic() - t0
        pc = ntf.long_address & 0xFFFF
        desc = next((d for a, d in WRAPPER_BPS if a == pc), f"unknown ${pc:04X}")
        seen_count[pc] = seen_count.get(pc, 0) + 1
        print(f"\n# HIT #{hits + 1} after {el:.2f}s: PC=${pc:04X} "
              f"reason={ntf.reason.name} — {desc} "
              f"(seen {seen_count[pc]}x)")

        regs = dump_regs(c, f"BP ${pc:04X}")
        sys_ = dump_sysvars(c, f"BP ${pc:04X} sysvars")
        captures.append({
            "pc": pc, "desc": desc, "regs": regs, "sysvars": sys_,
            "n": seen_count[pc],
        })
        hits += 1

        # If we've now seen all three wrapper PCs at least once, stop.
        if all(seen_count.get(a, 0) >= 1 for a, _ in WRAPPER_BPS):
            print("\n# All three wrapper BPs hit at least once — stopping.")
            break

    # Cleanup BPs.
    for addr, bid in list(bp_ids.items()):
        try:
            c.remove_breakpoint(bid)
        except Exception:
            pass

    # Summary.
    print("\n" + "=" * 70)
    print("# SUMMARY")
    print("=" * 70)
    for addr, desc in WRAPPER_BPS:
        n = seen_count.get(addr, 0)
        print(f"  ${addr:04X}  {n}x — {desc}")

    print("\n# Captures (HL is the key value at $26B9):")
    for cap in captures:
        r = cap["regs"]
        sv = cap["sysvars"]
        print(f"  PC=${cap['pc']:04X} #{cap['n']}: HL=${r.HL:04X} "
              f"SP=${r.SP:04X} ($5B6A)=${sv['5B6A']:04X} "
              f"($5B52)=${sv['5B52']:04X}")

    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mode", choices=["idle", "trace"], default="idle",
                    help="idle: read sysvars at current PC; "
                         "trace: BP at wrapper PCs and capture state")
    args = ap.parse_args()

    print(f"# G46(b) EOD-22 — CSpect $5B6A/$5B52 capture")
    print(f"# {time.strftime('%Y-%m-%d %H:%M:%S %Z')}")
    print(f"# mode={args.mode}")

    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} "
              f"machine={info.machine_type} program={info.program_name!r}")

        if args.mode == "idle":
            return mode_idle(c)
        else:
            return mode_trace(c)


if __name__ == "__main__":
    sys.exit(main())
