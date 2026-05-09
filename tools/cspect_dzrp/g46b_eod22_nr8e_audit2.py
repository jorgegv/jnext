"""g46b_eod22_nr8e_audit2.py — followup probes after audit1.

Audit1 finding: read_port(0x7FFD/0x1FFD) returned $FF (write-only, useless).
Audit1 finding: NR $8E read returned $00 (CSpect plugin returns 0 for NR $8E?).
Audit1 finding: $5B20 BP did NOT fire in 30s.

This script:
  1. Reads ROM at $0000, $0066 (NMI), $0C90 (KEY-INPUT) to confirm rom_bank=1
     (= 48K BASIC) by content fingerprint.
  2. Reads NR $00, NR $01, NR $03 to confirm machine ID + machine type.
  3. Try to read NR $8F, NR $8C, NR $8E and a few more for a sanity sweep.
  4. Steps the CPU manually a few times to confirm BC counter movement (i.e.
     supervisor is genuinely in a tight wait loop).
  5. Sets the BP at $5B00 (wrapper start) AND $5B20 (RET site) AND for safety
     at $0080 (per EOD docs the bank-flip trampoline) — runs 30s.
  6. Reads $00EF, $00F3 (post-reset init from EOD-21) — see if those addresses
     contain code matching jnext's path.
"""
from __future__ import annotations
import sys
import time
import cspect_dzrp as dz


def hexd(d: bytes, addr: int, w: int = 16) -> str:
    out = []
    for i in range(0, len(d), w):
        c = d[i:i+w]
        h = " ".join(f"{b:02X}" for b in c)
        a = "".join(chr(b) if 32 <= b < 127 else "." for b in c)
        out.append(f"  {addr+i:04X}  {h:<{w*3-1}}  {a}")
    return "\n".join(out)


def main() -> int:
    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} program={info.program_name!r}")

        # Pause first
        try:
            c.pause()
            c.wait_for_pause(timeout=3.0)
        except Exception:
            pass

        regs = c.get_registers()
        print(f"\nbaseline: {regs.format()}")

        # NR sweep
        print("\nNextReg sweep:")
        for nr in [0x00, 0x01, 0x02, 0x03, 0x07, 0x8C, 0x8E, 0x8F, 0x40, 0x43,
                   0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57]:
            try:
                v = c.get_tbblue_reg(nr)
                print(f"  NR ${nr:02X} = ${v:02X}")
            except Exception as e:
                print(f"  NR ${nr:02X}: read failed: {e}")

        # ROM fingerprint reads
        print("\nROM fingerprints:")
        for addr, label in [(0x0000, "boot vec"), (0x0066, "NMI vec"),
                            (0x0038, "IM1 vec"), (0x00EF, "post-reset init"),
                            (0x00F3, "NEXTREG $03 site"),
                            (0x0C90, "KEY-INPUT (48K BASIC)"),
                            (0x386E, "L386E (48K BASIC main)"),
                            (0x1B7B, "STMT-RET (48K BASIC)")]:
            try:
                m = c.read_mem(addr, 8)
                hexs = " ".join(f"{b:02X}" for b in m)
                print(f"  ${addr:04X} ({label}): {hexs}")
            except Exception as e:
                print(f"  ${addr:04X}: read failed: {e}")

        # Bank-flip wrapper region
        print("\nBank-flip wrapper region in RAM:")
        for addr in [0x0080, 0x5B00, 0x5B20, 0x5B40, 0x5B48]:
            try:
                m = c.read_mem(addr, 16)
                print(hexd(m, addr))
            except Exception as e:
                print(f"  ${addr:04X}: read failed: {e}")

        # Step a few times, watch BC counter
        print("\nFree-run sampling (cont/pause every 250ms):")
        for i in range(6):
            c.cont()
            time.sleep(0.25)
            try:
                c.pause()
                c.wait_for_pause(timeout=2.0)
            except Exception:
                pass
            r = c.get_registers()
            print(f"  sample {i}: PC={r.PC:04X} SP={r.SP:04X} BC={r.BC:04X} HL={r.HL:04X} R={r.R:02X}")

        # Set BPs at wrapper start, RET, trampoline
        print("\nBP probes:")
        bps = []
        for addr in [0x0080, 0x5B00, 0x5B20, 0x5B48, 0x00EF, 0x00F3]:
            try:
                bid = c.add_breakpoint(addr)
                bps.append((bid, addr))
                print(f"  BP at ${addr:04X} id={bid}")
            except Exception as e:
                print(f"  BP at ${addr:04X} failed: {e}")

        print("  CONTINUE (run 30s)...")
        t0 = time.monotonic()
        c.cont()
        try:
            ntf = c.wait_for_pause(timeout=30.0)
            el = time.monotonic() - t0
            print(f"  STOP after {el:.2f}s; reason={ntf.reason.name} long_addr=${ntf.long_address:06X}")
            r = c.get_registers()
            print(f"  state: {r.format()}")
            # Show stack
            for i in range(8):
                a = (r.SP + i*2) & 0xFFFF
                try:
                    b = c.read_mem(a, 2)
                    w = b[0] | (b[1] << 8)
                    print(f"    [{a:04X}] = ${w:04X}")
                except Exception:
                    break
        except TimeoutError:
            el = time.monotonic() - t0
            print(f"  no BP fired within {el:.1f}s — pausing")
            try:
                c.pause()
                c.wait_for_pause(timeout=3.0)
                r = c.get_registers()
                print(f"  paused at: {r.format()}")
            except Exception as e:
                print(f"  pause failed: {e}")

        for bid, _ in bps:
            try:
                c.remove_breakpoint(bid)
            except Exception:
                pass

    print("\nDONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
