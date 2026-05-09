"""g46b_eod22_26b9_hl_capture.py — capture CSpect state at bank 1 $26B9.

Per EOD-22 Wave 7 finding, jnext at $26B9 has:
  - HL = $0000 (post-$32CC)
  - ($5B54) = $3E93 (= zero-padded altrom region -> NOP slide -> PC=$0000)
  - ($5B6A) likely $0000 (cascade-cleared, never re-init'd)
  - sysvars $5B50..$5B6F otherwise zero

This script captures the SAME state on real-Next-faithful CSpect for direct
comparison.

Procedure:
  1. Connect to CSpect (must be launched with `-debug` so it halts at PC=$0000)
  2. Install persistent BPs at $26B9 (primary) and $26C2 (fallback if $26B9
     fires in bank 0 with different bytes)
  3. CONTINUE; wait for first hit
  4. Capture full state: PC, all registers, IFF1, IM, slots, NR $03/$07/$8C/$8E,
     NR $50..$57, ports $7FFD/$1FFD, mem at $5B50..$5B6F, mem at SP..SP+15
  5. Continue once more to capture a second sample (some wrappers re-enter)
  6. Detach cleanly
"""
from __future__ import annotations

import sys
import time
from typing import Optional

import cspect_dzrp as dz


# Primary capture site: $26B9 = post-CALL $32CC, HL holds the wrapper target.
# Secondary fallback: $26C2 = JP $3E93 itself.
TARGETS = [
    (0x26B9, "post-CALL $32CC (HL is the wrapper target)"),
    (0x26C2, "JP $3E93 (final wrapper jump)"),
]


def _hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def _word_le(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8)


def capture_full(c: dz.CSpectDZRP, label: str) -> dict:
    """Capture every datum requested by the task at the current paused state."""
    r = c.get_registers()
    snap: dict = {
        "label": label,
        "PC": r.PC, "SP": r.SP,
        "AF": r.AF, "BC": r.BC, "DE": r.DE, "HL": r.HL,
        "IX": r.IX, "IY": r.IY,
        "AFp": r.AFp, "BCp": r.BCp, "DEp": r.DEp, "HLp": r.HLp,
        "I": r.I, "R": r.R, "IM": r.IM,
        "slots": list(r.slots),  # NR $50..$57
    }
    # IFF1 isn't surfaced directly; IM is. Note in summary.

    # NextRegs
    nr = {}
    for n in [0x03, 0x07, 0x8C, 0x8E]:
        try:
            nr[n] = c.get_tbblue_reg(n)
        except Exception as e:
            nr[n] = f"err:{e}"
    snap["nr"] = nr

    # Legacy ports (write-only on real Next; expect $FF). Read for completeness.
    ports = {}
    for p in [0x7FFD, 0x1FFD]:
        try:
            ports[p] = c.read_port(p)
        except Exception as e:
            ports[p] = f"err:{e}"
    snap["ports"] = ports

    # Memory: $5B50..$5B5F (16 bytes) and $5B60..$5B6F (16 bytes)
    try:
        snap["mem_5B50"] = c.read_mem(0x5B50, 16)
        snap["mem_5B60"] = c.read_mem(0x5B60, 16)
    except Exception as e:
        snap["mem_5B50"] = b""
        snap["mem_5B60"] = b""
        snap["mem_err"] = str(e)

    # Top of stack: 8 bytes at SP..SP+7
    try:
        snap["mem_sp"] = c.read_mem(r.SP, 8)
    except Exception:
        snap["mem_sp"] = b""

    # 16 bytes at PC for instruction stream
    try:
        snap["mem_pc"] = c.read_mem(r.PC, 16)
    except Exception:
        snap["mem_pc"] = b""

    return snap


def print_snapshot(snap: dict) -> None:
    print(f"\n[{snap['label']}]")
    print(f"  PC=${snap['PC']:04X}   SP=${snap['SP']:04X}")
    print(f"  AF=${snap['AF']:04X}   BC=${snap['BC']:04X}   "
          f"DE=${snap['DE']:04X}   HL=${snap['HL']:04X}")
    print(f"  IX=${snap['IX']:04X}   IY=${snap['IY']:04X}")
    print(f"  AF'=${snap['AFp']:04X}  BC'=${snap['BCp']:04X}  "
          f"DE'=${snap['DEp']:04X}  HL'=${snap['HLp']:04X}")
    print(f"  I=${snap['I']:02X} R=${snap['R']:02X} IM={snap['IM']}")

    a = (snap['AF'] >> 8) & 0xFF
    f = snap['AF'] & 0xFF
    print(f"  A=${a:02X} F=${f:02X}")

    print(f"  slots [NR $50..$57]: {_hex(bytes(snap['slots']))}")

    nr = snap["nr"]
    fmt = lambda v: f"${v:02X}" if isinstance(v, int) else str(v)
    print(f"  NR $03={fmt(nr[0x03])}  $07={fmt(nr[0x07])}  "
          f"$8C={fmt(nr[0x8C])}  $8E={fmt(nr[0x8E])}")

    ports = snap["ports"]
    fmt_p = lambda v: f"${v:02X}" if isinstance(v, int) else str(v)
    print(f"  port $7FFD={fmt_p(ports[0x7FFD])}  $1FFD={fmt_p(ports[0x1FFD])}  "
          f"(write-only)")

    if snap.get("mem_5B50"):
        d50 = snap["mem_5B50"]
        d60 = snap["mem_5B60"]
        print(f"  mem $5B50: {_hex(d50)}")
        print(f"  mem $5B60: {_hex(d60)}")
        v_5B52 = _word_le(d50, 0x02)
        v_5B54 = _word_le(d50, 0x04)
        v_5B5C = _word_le(d50, 0x0C)
        v_5B6A = _word_le(d60, 0x0A)
        print(f"  -> ($5B52)=${v_5B52:04X}  "
              f"($5B54)=${v_5B54:04X}  "
              f"($5B5C)=${v_5B5C:04X}  "
              f"($5B6A)=${v_5B6A:04X}")

    if snap.get("mem_sp"):
        ms = snap["mem_sp"]
        print(f"  stack@SP (8 bytes): {_hex(ms)}")
        # As 4 LE words:
        words = [ms[i] | (ms[i + 1] << 8) for i in range(0, 8, 2)]
        wstr = " ".join(f"${w:04X}" for w in words)
        print(f"    as 4 LE words: {wstr}")

    if snap.get("mem_pc"):
        print(f"  mem @ PC: {_hex(snap['mem_pc'])}")


def main() -> int:
    print("# G46(b) EOD-22 — CSpect $26B9 / $26C2 HL + sysvars capture")
    print(f"# {time.strftime('%Y-%m-%d %H:%M:%S %Z')}")

    captures: list[dict] = []

    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} "
              f"machine={info.machine_type} program={info.program_name!r}")

        # CSpect launched with -debug halts at $0000. Pause defensively.
        try:
            c.pause()
            c.wait_for_pause(timeout=3.0)
        except TimeoutError:
            pass
        except Exception:
            pass

        r0 = c.get_registers()
        print(f"# initial state: {r0.format()}")

        # Install both BPs.
        bp_ids: dict[int, int] = {}
        for addr, desc in TARGETS:
            try:
                bid = c.add_breakpoint(addr)
                bp_ids[addr] = bid
                print(f"# BP at ${addr:04X} id={bid} — {desc}")
            except Exception as e:
                print(f"# BP at ${addr:04X} FAILED: {e}")

        # Continue; wait for first hit. We're after the FIRST passage through
        # the wrapper, so cap at first 4 hits per BP for safety.
        max_hits = 8
        hit_count = 0
        seen_per_addr: dict[int, int] = {a: 0 for a, _ in TARGETS}
        first_hit_per_addr: dict[int, dict] = {}

        while hit_count < max_hits:
            t0 = time.monotonic()
            try:
                c.cont()
            except Exception as e:
                print(f"# CONT failed: {e}")
                break
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
            desc = next((d for a, d in TARGETS if a == pc), f"unknown ${pc:04X}")
            seen_per_addr[pc] = seen_per_addr.get(pc, 0) + 1
            n = seen_per_addr[pc]
            print(f"\n# HIT #{hit_count + 1} after {el:.2f}s: "
                  f"PC=${pc:04X} reason={ntf.reason.name} "
                  f"(seen {n}x) — {desc}")

            label = f"BP ${pc:04X} hit#{n}"
            snap = capture_full(c, label)
            print_snapshot(snap)
            captures.append(snap)
            if pc not in first_hit_per_addr:
                first_hit_per_addr[pc] = snap
            hit_count += 1

            # Stop after we've captured BOTH targets at least once (or 4 of any).
            if all(seen_per_addr.get(a, 0) >= 1 for a, _ in TARGETS):
                print("\n# Both target BPs captured — stopping capture loop.")
                break

        # Cleanup BPs.
        for addr, bid in list(bp_ids.items()):
            try:
                c.remove_breakpoint(bid)
                print(f"# BP at ${addr:04X} (id={bid}) removed")
            except Exception:
                pass

    # Summary.
    print("\n" + "=" * 72)
    print("# SUMMARY")
    print("=" * 72)
    for addr, desc in TARGETS:
        n = seen_per_addr.get(addr, 0)
        print(f"  ${addr:04X}  {n} hit(s) — {desc}")

    print("\n# Captures (chronological, each hit):")
    for snap in captures:
        d50 = snap.get("mem_5B50") or b""
        d60 = snap.get("mem_5B60") or b""
        v_5B54 = _word_le(d50, 0x04) if len(d50) >= 6 else None
        v_5B6A = _word_le(d60, 0x0A) if len(d60) >= 12 else None
        s_54 = f"${v_5B54:04X}" if v_5B54 is not None else "?"
        s_6A = f"${v_5B6A:04X}" if v_5B6A is not None else "?"
        print(f"  PC=${snap['PC']:04X} HL=${snap['HL']:04X} "
              f"SP=${snap['SP']:04X} ($5B54)={s_54} ($5B6A)={s_6A}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
