"""g46b_eod22_26b9_idle_sysvars.py — capture CSpect sysvars + memory at the
post-boot REPL idle state.

CSpect's supervisor never reaches bank 1 $26B9/$26C2 wrapper during boot
(verified via persistent BPs). To compare with jnext's broken wrapper-hit
state, we instead capture CSpect's IDLE-state sysvars and the memory at
$26B0..$26C2 and $3E93..$3E9F. This tells us:

  - What value ($5B6A) and ($5B54) hold in the steady-state CSpect.
  - Whether the wrapper bytes are even present (bank 1 mapped vs not).
  - What slot mapping CSpect uses idle (NR $50..$57).
"""
from __future__ import annotations

import sys
import time

import cspect_dzrp as dz


def _hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def _word_le(b: bytes, off: int) -> int:
    return b[off] | (b[off + 1] << 8)


def main() -> int:
    print("# G46(b) EOD-22 — CSpect IDLE-state sysvars + wrapper-bytes capture")
    print(f"# {time.strftime('%Y-%m-%d %H:%M:%S %Z')}")

    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} "
              f"machine={info.machine_type} program={info.program_name!r}")

        # Pause; show paused state.
        try:
            c.pause()
            c.wait_for_pause(timeout=3.0)
        except TimeoutError:
            pass
        except Exception:
            pass

        r = c.get_registers()
        print(f"\n# paused at PC=${r.PC:04X} SP=${r.SP:04X}")
        print(f"  AF=${r.AF:04X} BC=${r.BC:04X} DE=${r.DE:04X} HL=${r.HL:04X}")
        print(f"  IX=${r.IX:04X} IY=${r.IY:04X} I=${r.I:02X} IM={r.IM}")
        print(f"  slots [NR $50..$57]: {_hex(bytes(r.slots))}")

        # NR $03/$07/$8C/$8E
        nr_vals = {}
        for n in [0x03, 0x07, 0x8C, 0x8E]:
            try:
                nr_vals[n] = c.get_tbblue_reg(n)
            except Exception as e:
                nr_vals[n] = -1
        print(f"  NR $03=${nr_vals[0x03]:02X} $07=${nr_vals[0x07]:02X} "
              f"$8C=${nr_vals[0x8C]:02X} $8E=${nr_vals[0x8E]:02X}")

        # Memory: sysvars
        d50 = c.read_mem(0x5B50, 16)
        d60 = c.read_mem(0x5B60, 16)
        print(f"\n# Sysvars $5B50..$5B6F:")
        print(f"  $5B50: {_hex(d50)}")
        print(f"  $5B60: {_hex(d60)}")
        print(f"  -> ($5B52)=${_word_le(d50, 0x02):04X}  "
              f"($5B54)=${_word_le(d50, 0x04):04X}  "
              f"($5B5C)=${_word_le(d50, 0x0C):04X}  "
              f"($5B6A)=${_word_le(d60, 0x0A):04X}")

        # Memory at $26B0..$26C5 (the wrapper itself in slot 1)
        try:
            wrap = c.read_mem(0x26B0, 22)
            print(f"\n# Memory at $26B0..$26C5 (wrapper in slot 1):")
            print(f"  $26B0: {_hex(wrap)}")
            # Decode whether this looks like the documented wrapper.
            # Expected at $26B0: 33 E3 22 54 5B E1 CD CC 32 E5 2A 54 5B E3 ED 8A 7B 00 C3 93 3E
            expected = bytes([0x33, 0xE3, 0x22, 0x54, 0x5B, 0xE1, 0xCD, 0xCC,
                              0x32, 0xE5, 0x2A, 0x54, 0x5B, 0xE3, 0xED, 0x8A,
                              0x7B, 0x00, 0xC3, 0x93, 0x3E])
            if wrap[:21] == expected:
                print("  -> matches expected wrapper bytes (bank 1 mapped here)")
            else:
                print("  -> DOES NOT match expected wrapper")
        except Exception as e:
            print(f"# read $26B0 failed: {e}")

        # Memory at $3E93 (altrom-toggle)
        try:
            alt = c.read_mem(0x3E93, 8)
            print(f"\n# Memory at $3E93..$3E9A (altrom-toggle):")
            print(f"  $3E93: {_hex(alt)}")
            # Expected bank 1 $3E93: ED 91 8E 00 C9
            # Bank 0 $3E93        : ED 91 8E 01 C9
        except Exception as e:
            print(f"# read $3E93 failed: {e}")

        # Memory at $5B00..$5B1F (toggle wrapper)
        try:
            tog = c.read_mem(0x5B00, 32)
            print(f"\n# Memory at $5B00..$5B1F (toggle wrapper in RAM):")
            print(f"  $5B00: {_hex(tog[:16])}")
            print(f"  $5B10: {_hex(tog[16:])}")
        except Exception as e:
            print(f"# read $5B00 failed: {e}")

        # Don't resume (leave CSpect paused).

    return 0


if __name__ == "__main__":
    sys.exit(main())
