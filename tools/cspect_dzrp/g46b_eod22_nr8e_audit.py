"""g46b_eod22_nr8e_audit.py — does CSpect treat NR $8E as VHDL-faithful?

Investigation question (EOD-22):
   In +3 mode, VHDL :3727 says NR $8E writes update both 7FFD bit 4 AND
   1FFD bit 2, so sram_rom = 1FFD(2):7FFD(4) (2-bit). NEXTREG $8E,$03
   should therefore land in sram_rom = 11 = bank 3.

   On jnext, supervisor reaches bank-3 (= +3 DOS area) and the bank-flip
   wrapper at $5B00..$5B20 RETs to $0000. CSpect's post-boot supervisor
   is in bank-1 (= 48K BASIC equivalent at $0C90).

   Does CSpect actually update 1FFD on a NR $8E write? Read 1FFD before
   and after.

Steps:
   1. Snapshot regs + 7FFD + 1FFD + NR $03/$8E/$8C/$50..$57 + mem@0x0C90.
   2. PAUSE the CPU (so we don't race a polling loop).
   3. Write NR $8E,$03 via TBBLUE_REG path. Re-read everything.
   4. Restore the original 7FFD/1FFD/NR $8E. Set BP at $5B20 and run 30s.
   5. Report whether $5B20 BP fires + whether 1FFD bit 2 changed.

Output: human-readable report on stdout for capture.
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


def decode_nr8e(v: int) -> str:
    """NR $8E read-back composition per VHDL :6158:
       bit7=dffd0, bit6=7ffd2, bit5=7ffd1, bit4=7ffd0, bit3=1, bit2=1ffd0,
       bit1=1ffd2, bit0=rom_bank_lsb (= 7ffd4)."""
    bits = {
        "dffd0":   (v >> 7) & 1,
        "7ffd2":   (v >> 6) & 1,
        "7ffd1":   (v >> 5) & 1,
        "7ffd0":   (v >> 4) & 1,
        "fixed_1": (v >> 3) & 1,
        "1ffd0":   (v >> 2) & 1,
        "1ffd2":   (v >> 1) & 1,    # the bit we care about for sram_rom MSB in +3 mode
        "7ffd4":   (v >> 0) & 1,    # = rom_bank_lsb
    }
    sram_rom = (bits["1ffd2"] << 1) | bits["7ffd4"]
    return (f"  NR $8E = ${v:02X} = {v:08b}b  -> "
            f"7ffd[4]={bits['7ffd4']}  1ffd[2]={bits['1ffd2']}  1ffd[0]={bits['1ffd0']}  "
            f"7ffd[2..0]={bits['7ffd2']}{bits['7ffd1']}{bits['7ffd0']}  "
            f"=> sram_rom (1ffd2:7ffd4) = {sram_rom:02b}")


def snapshot(c: dz.CSpectDZRP, label: str) -> dict:
    """Capture relevant regs + ports + nextregs + slot map + mem hint."""
    print(f"\n----- snapshot: {label} -----")
    regs = c.get_registers()
    print(f"  {regs.format()}")

    # Port reads (7FFD/1FFD/DFFD are write-only on real Next; CSpect's read_port
    # may return last-written or 0xFF — record what we get for transparency).
    p_7ffd = c.read_port(0x7FFD)
    p_1ffd = c.read_port(0x1FFD)
    p_dffd = c.read_port(0xDFFD)

    nr_03 = c.get_tbblue_reg(0x03)
    nr_07 = c.get_tbblue_reg(0x07)
    nr_8c = c.get_tbblue_reg(0x8C)
    nr_8e = c.get_tbblue_reg(0x8E)

    print(f"  port read 0x7FFD = ${p_7ffd:02X}    port read 0x1FFD = ${p_1ffd:02X}    "
          f"port read 0xDFFD = ${p_dffd:02X}")
    print(f"  NR $03 = ${nr_03:02X} (machine type)   NR $07 = ${nr_07:02X} (cpu spd)")
    print(f"  NR $8C = ${nr_8c:02X} (altrom)         NR $8E = ${nr_8e:02X}")
    print(decode_nr8e(nr_8e))

    # NR $50..$57 already in regs.slots; print explicitly
    slot_str = " ".join(f"NR${0x50+i:02X}=${b:02X}" for i, b in enumerate(regs.slots))
    print(f"  Slots: {slot_str}")

    # 1FFD bit 2 + 7FFD bit 4 reasoning
    bit_7ffd4 = (p_7ffd >> 4) & 1
    bit_1ffd2 = (p_1ffd >> 2) & 1
    composed = (bit_1ffd2 << 1) | bit_7ffd4
    print(f"  Composed sram_rom from port reads: 1FFD(2)={bit_1ffd2} 7FFD(4)={bit_7ffd4} "
          f"=> sram_rom = {composed:02b}")

    return {
        "regs": regs,
        "p_7ffd": p_7ffd,
        "p_1ffd": p_1ffd,
        "p_dffd": p_dffd,
        "nr_03": nr_03,
        "nr_07": nr_07,
        "nr_8c": nr_8c,
        "nr_8e": nr_8e,
    }


def main() -> int:
    with dz.CSpectDZRP() as c:
        info = c.init()
        print(f"# DZRP {'.'.join(map(str, info.dzrp_version))} program={info.program_name!r}"
              f" machine={info.machine_type}")

        # Step 0: pause first to avoid races during the audit.
        print("\n# step 0: PAUSE CPU")
        try:
            c.pause()
            ntf = c.wait_for_pause(timeout=5.0)
            print(f"  paused: reason={ntf.reason.name} long_addr=${ntf.long_address:06X}")
        except Exception as e:
            print(f"  pause failed (maybe already paused): {e}")

        # Step 1: baseline snapshot
        s0 = snapshot(c, "BASELINE (post-boot, before any writes)")

        # Read mem at 0x0C90 to confirm we're in 48K BASIC KEY-INPUT loop
        try:
            m_0c90 = c.read_mem(0x0C90, 16)
            print()
            print("# memory @ $0C90 (slot-0, expected 48K BASIC):")
            print(hexdump(m_0c90, 0x0C90))
        except Exception as e:
            print(f"# read_mem 0x0C90 failed: {e}")

        # Read 16 bytes at 0x5B20 — wrapper RET site (bank-flip)
        try:
            m_5b20 = c.read_mem(0x5B20, 16)
            print()
            print("# memory @ $5B20 (RAM, possible bank-flip wrapper RET site on jnext):")
            print(hexdump(m_5b20, 0x5B20))
        except Exception as e:
            print(f"# read_mem 0x5B20 failed: {e}")

        # Read 16 bytes at 0x5B00 — wrapper start
        try:
            m_5b00 = c.read_mem(0x5B00, 16)
            print()
            print("# memory @ $5B00 (RAM, wrapper start on jnext):")
            print(hexdump(m_5b00, 0x5B00))
        except Exception as e:
            print(f"# read_mem 0x5B00 failed: {e}")

        # Step 2: write NR $8E,$03 directly (TBBLUE register-write path)
        # Use port write 0x243B (NEXTREG-select) then 0x253B (NEXTREG-data).
        # CSpect's TBBLUE_REG read is via DZRP, but writing a NextReg requires
        # going through ports. The classic NEXTREG sequence is:
        #   OUT (0x243B), reg ; OUT (0x253B), value
        # Both must be done with 16-bit IO to hit the right NEXTREG_REG/DAT
        # selector. write_port lets us do that.
        print("\n# step 2: WRITE NR $8E <- $03 via NEXTREG ports (243B/253B)")
        c.write_port(0x243B, 0x8E)
        c.write_port(0x253B, 0x03)
        print("  done.")

        # Step 3: post-write snapshot
        s1 = snapshot(c, "POST NR $8E <- $03")

        # Compute deltas
        print("\n----- DELTAS -----")
        print(f"  port 0x7FFD: ${s0['p_7ffd']:02X} -> ${s1['p_7ffd']:02X}   "
              f"delta bit 4 = {((s0['p_7ffd']^s1['p_7ffd'])>>4)&1}")
        print(f"  port 0x1FFD: ${s0['p_1ffd']:02X} -> ${s1['p_1ffd']:02X}   "
              f"delta bit 2 = {((s0['p_1ffd']^s1['p_1ffd'])>>2)&1}   "
              f"delta bit 0 = {((s0['p_1ffd']^s1['p_1ffd'])>>0)&1}")
        print(f"  NR $8E:      ${s0['nr_8e']:02X} -> ${s1['nr_8e']:02X}")

        b0 = (s0['p_1ffd'] >> 2) & 1
        b1 = (s1['p_1ffd'] >> 2) & 1
        if b0 == b1:
            print(f"  NR $8E,$03 did NOT update 1FFD bit 2 (was {b0}, still {b0}).")
        else:
            print(f"  NR $8E,$03 DID update 1FFD bit 2 ({b0} -> {b1}).")

        # Step 4: try to restore. Use write_port for 7FFD/1FFD legacy.
        print("\n# step 4: restore original 7FFD and 1FFD via port writes")
        # NB: 7FFD is paging-locked when bit 5 set previously. Write back what
        # we read; if it's locked the write is a no-op.
        c.write_port(0x7FFD, s0['p_7ffd'])
        c.write_port(0x1FFD, s0['p_1ffd'])
        # Re-write NR $8E to its original value too (may not actually re-trigger
        # the legacy port writes — that's exactly the question we're auditing).
        c.write_port(0x243B, 0x8E)
        c.write_port(0x253B, s0['nr_8e'])
        snapshot(c, "POST RESTORE")

        # Step 5: BP at $5B20, then continue for 30s
        print("\n# step 5: BP at $5B20 (wrapper RET site on jnext)")
        try:
            bp_id = c.add_breakpoint(0x5B20)
            print(f"  BP id={bp_id}")
        except Exception as e:
            print(f"  add_breakpoint failed: {e}")
            bp_id = None

        if bp_id is not None:
            print("  CONTINUE (run 30s) ...")
            t0 = time.monotonic()
            c.cont()
            try:
                ntf = c.wait_for_pause(timeout=30.0)
                el = time.monotonic() - t0
                print(f"  $5B20 BP FIRED after {el:.2f}s; reason={ntf.reason.name} "
                      f"long_addr=${ntf.long_address:06X}")
                regs2 = c.get_registers()
                print(f"  state at BP:  {regs2.format()}")
                # Stack peek
                stack = []
                for i in range(8):
                    sp_a = (regs2.SP + i*2) & 0xFFFF
                    try:
                        b = c.read_mem(sp_a, 2)
                        w = b[0] | (b[1] << 8)
                        stack.append((sp_a, w))
                    except Exception:
                        stack.append((sp_a, None))
                        break
                print("  stack 8 words from SP:")
                for a, w in stack:
                    if w is None:
                        print(f"    [{a:04X}] = ??")
                    else:
                        print(f"    [{a:04X}] = ${w:04X}")
            except TimeoutError:
                el = time.monotonic() - t0
                print(f"  $5B20 BP DID NOT FIRE within {el:.1f}s — pausing CPU")
                try:
                    c.pause()
                    ntf = c.wait_for_pause(timeout=5.0)
                    regs2 = c.get_registers()
                    print(f"  paused at: {regs2.format()}")
                except Exception as e:
                    print(f"  pause after timeout failed: {e}")
            try:
                c.remove_breakpoint(bp_id)
            except Exception:
                pass

        # Final snapshot to confirm we end in a sane state
        snapshot(c, "FINAL")

    print("\n# DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
