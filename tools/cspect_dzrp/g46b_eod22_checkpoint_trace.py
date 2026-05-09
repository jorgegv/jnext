"""g46b_eod22_checkpoint_trace.py — capture CSpect supervisor PC trajectory.

Sets BPs at supervisor checkpoints during NextZXOS post-soft-reset cascade
and init, captures full state at each, then removes the BP and continues
to the next.

Checkpoints (in expected execution order):
  $00EF — post-soft-reset init entry (NEXTREG $07,$03)
  $0124 — attribute LDIR start (LD HL,$5800)
  $016E — cascade outer loop top (JR $018E after DJNZ)
  $018E — second-phase RAM-test loop start
  $01D1 — post-test SP reset (LD SP,$5BFF)
  $01D4 — RST $20 dispatch
  $5B48 — bank-3 absolute wrapper
  $5B00 — toggle wrapper (this should NEVER fire on CSpect post-boot)

For each fired BP, capture:
  PC, SP, A/F/BC/DE/HL/IX/IY/IFF1/IM
  slot register values (NR $50..$57)
  stack[0..7] words at SP
  NR $03, NR $07, NR $8C, NR $8E
  port_7FFD (read_port 0x7FFD)
  port_1FFD (read_port 0x1FFD)
"""
from __future__ import annotations

import sys
import time
from typing import Optional

import cspect_dzrp as dz


CHECKPOINTS = [
    (0x00EF, "post-soft-reset init entry (NEXTREG $07,$03)"),
    (0x0124, "attribute LDIR start (LD HL,$5800)"),
    (0x016E, "cascade outer loop top (JR $018E after DJNZ)"),
    (0x018E, "second-phase RAM-test loop start"),
    (0x01D1, "post-test SP reset (LD SP,$5BFF)"),
    (0x01D4, "RST $20 dispatch"),
    (0x5B48, "bank-3 absolute wrapper (NEXTREG $8E,$03; RET)"),
    (0x5B00, "toggle wrapper — SHOULD NEVER fire on CSpect post-boot"),
]


def _hex16(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def capture_state(c: dz.CSpectDZRP, label: str) -> dict:
    """Capture full state snapshot at current PC."""
    r = c.get_registers()
    snap = {
        "PC": r.PC, "SP": r.SP,
        "AF": r.AF, "BC": r.BC, "DE": r.DE, "HL": r.HL,
        "IX": r.IX, "IY": r.IY,
        "AFp": r.AFp, "BCp": r.BCp, "DEp": r.DEp, "HLp": r.HLp,
        "I": r.I, "R": r.R, "IM": r.IM,
        "slots": list(r.slots),
    }

    # NextRegs
    nr_vals = {}
    for nr in [0x03, 0x07, 0x8C, 0x8E]:
        try:
            nr_vals[nr] = c.get_tbblue_reg(nr)
        except Exception as e:
            nr_vals[nr] = f"err:{e}"
    snap["nr"] = nr_vals

    # Ports (write-only on real Next; expect $FF)
    ports = {}
    for p in [0x7FFD, 0x1FFD]:
        try:
            ports[p] = c.read_port(p)
        except Exception as e:
            ports[p] = f"err:{e}"
    snap["ports"] = ports

    # Stack[0..7]
    stack = []
    for i in range(8):
        a = (r.SP + i * 2) & 0xFFFF
        try:
            b = c.read_mem(a, 2)
            w = b[0] | (b[1] << 8)
            stack.append((a, w))
        except Exception:
            break
    snap["stack"] = stack

    # 16 bytes at PC for sanity (instruction stream)
    try:
        snap["mem_at_pc"] = c.read_mem(r.PC, 16)
    except Exception as e:
        snap["mem_at_pc"] = b""

    return snap


def print_state(label: str, snap: dict) -> None:
    print(f"\n[{label}]")
    print(f"  PC={snap['PC']:04X} SP={snap['SP']:04X} "
          f"AF={snap['AF']:04X} BC={snap['BC']:04X} "
          f"DE={snap['DE']:04X} HL={snap['HL']:04X}")
    print(f"  IX={snap['IX']:04X} IY={snap['IY']:04X} "
          f"I={snap['I']:02X} R={snap['R']:02X} IM={snap['IM']}")
    print(f"  slots [NR $50..$57]: " + _hex16(bytes(snap['slots'])))

    nr = snap["nr"]
    fmt_nr = {}
    for k, v in nr.items():
        fmt_nr[k] = f"${v:02X}" if isinstance(v, int) else str(v)
    print(f"  NR $03={fmt_nr[0x03]} $07={fmt_nr[0x07]} "
          f"$8C={fmt_nr[0x8C]} $8E={fmt_nr[0x8E]}")

    ports = snap["ports"]
    fmt_p = {}
    for k, v in ports.items():
        fmt_p[k] = f"${v:02X}" if isinstance(v, int) else str(v)
    print(f"  port $7FFD={fmt_p[0x7FFD]} $1FFD={fmt_p[0x1FFD]}  "
          f"(write-only — $FF expected)")

    print(f"  stack[0..7] @ SP={snap['SP']:04X}:")
    for a, w in snap["stack"]:
        print(f"    [{a:04X}] = ${w:04X}")

    if snap.get("mem_at_pc"):
        print(f"  mem @ PC: {_hex16(snap['mem_at_pc'])}")


def main() -> int:
    print("# G46(b) EOD-22 — CSpect supervisor checkpoint trace")
    print(f"# {time.strftime('%Y-%m-%d %H:%M:%S %Z')}")

    captures: list[tuple[int, str, Optional[dict]]] = []

    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} "
              f"machine={info.machine_type} program={info.program_name!r}")

        # Pause to ensure deterministic state. Note: CSpect was launched with
        # -debug, so it should already be paused at PC=$0000.
        try:
            c.pause()
            c.wait_for_pause(timeout=3.0)
        except TimeoutError:
            pass
        except Exception:
            pass

        regs0 = c.get_registers()
        print(f"# initial state: {regs0.format()}")

        # Set ALL breakpoints up front. Order of execution by supervisor will
        # determine which one fires next.
        bp_ids: dict[int, int] = {}
        for addr, desc in CHECKPOINTS:
            try:
                bid = c.add_breakpoint(addr)
                bp_ids[addr] = bid
                print(f"# BP at ${addr:04X} id={bid} — {desc}")
            except Exception as e:
                print(f"# BP at ${addr:04X} FAILED: {e}")

        # Now iterate: continue, wait for stop, capture, remove BP, repeat.
        print(f"\n# Continuing — waiting for first BP hit...")

        max_hits = 12  # safety cap (allows revisits)
        hits_seen = 0
        addrs_remaining = set(bp_ids.keys())

        while hits_seen < max_hits and addrs_remaining:
            t0 = time.monotonic()
            c.cont()
            try:
                ntf = c.wait_for_pause(timeout=15.0)
            except TimeoutError:
                el = time.monotonic() - t0
                print(f"\n# TIMEOUT after {el:.1f}s — no remaining BP fired")
                # Pause to capture current PC.
                try:
                    c.pause()
                    c.wait_for_pause(timeout=3.0)
                    r = c.get_registers()
                    print(f"# paused at PC=${r.PC:04X} "
                          f"(remaining BPs: {[f'${a:04X}' for a in addrs_remaining]})")
                except Exception as e:
                    print(f"# pause failed: {e}")
                # Mark remaining BPs as MISS.
                for a in list(addrs_remaining):
                    desc = next((d for ad, d in CHECKPOINTS if ad == a), "")
                    captures.append((a, desc, None))
                    addrs_remaining.discard(a)
                break

            el = time.monotonic() - t0
            hit_pc = ntf.long_address & 0xFFFF
            desc = next((d for ad, d in CHECKPOINTS if ad == hit_pc),
                        f"unknown PC=${hit_pc:04X}")
            print(f"\n# HIT after {el:.2f}s: PC=${hit_pc:04X} "
                  f"reason={ntf.reason.name} — {desc}")

            snap = capture_state(c, desc)
            print_state(f"${hit_pc:04X} — {desc}", snap)
            captures.append((hit_pc, desc, snap))
            hits_seen += 1

            # Remove this BP so we proceed past.
            if hit_pc in bp_ids:
                try:
                    c.remove_breakpoint(bp_ids[hit_pc])
                    print(f"# BP at ${hit_pc:04X} removed")
                except Exception as e:
                    print(f"# remove BP at ${hit_pc:04X} failed: {e}")
                bp_ids.pop(hit_pc, None)
            addrs_remaining.discard(hit_pc)

        # Cleanup any leftover BPs.
        for addr, bid in list(bp_ids.items()):
            try:
                c.remove_breakpoint(bid)
            except Exception:
                pass

        # Mark any remaining (unfired) checkpoints as MISS.
        for addr, desc in CHECKPOINTS:
            seen = any(a == addr for a, _, _ in captures)
            if not seen:
                captures.append((addr, desc, None))

    # Print summary.
    print("\n" + "=" * 70)
    print("# SUMMARY")
    print("=" * 70)
    for addr, desc in CHECKPOINTS:
        hits = [s for a, _, s in captures if a == addr and s is not None]
        if hits:
            print(f"  ${addr:04X}  HIT ({len(hits)}x) — {desc}")
        else:
            print(f"  ${addr:04X}  MISS — {desc}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
