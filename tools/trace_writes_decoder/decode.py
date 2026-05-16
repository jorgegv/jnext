#!/usr/bin/env python3
"""
Trace-to-writes decoder for jnext / CSpect Z80 TraceLog dumps.

Parses lines of the form:
  CCCCCCCCCCCC  $PPPP  AF=XXXX BC=XXXX DE=XXXX HL=XXXX  AF'=XXXX BC'=XXXX DE'=XXXX HL'=XXXX  IX=XXXX IY=XXXX SP=XXXX  [SZ5H3PNC]  oo oo oo oo

For each instruction, emits zero or more write records:
  CCCCCCCCCCCC  PPPP  KIND  ARG  VALUE

KIND in {MEM, PORT, NEXTREG}. VALUE is hex; '?' when unknown (e.g. LDIR src
contents not in trace).

Modes:
  decode.py <trace_file> <output_file>
  decode.py --diff <jnext_writes> <cspect_writes>

Python 3, stdlib only.
"""

import argparse
import re
import sys
from dataclasses import dataclass
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

LINE_RE = re.compile(
    r'^\s*(?P<cycle>\d{12})\s+'
    r'\$(?P<pc>[0-9A-Fa-f]{4})\s+'
    r'AF=(?P<af>[0-9A-Fa-f]{4})\s+'
    r'BC=(?P<bc>[0-9A-Fa-f]{4})\s+'
    r'DE=(?P<de>[0-9A-Fa-f]{4})\s+'
    r'HL=(?P<hl>[0-9A-Fa-f]{4})\s+'
    r"AF'=(?P<af2>[0-9A-Fa-f]{4})\s+"
    r"BC'=(?P<bc2>[0-9A-Fa-f]{4})\s+"
    r"DE'=(?P<de2>[0-9A-Fa-f]{4})\s+"
    r"HL'=(?P<hl2>[0-9A-Fa-f]{4})\s+"
    r'IX=(?P<ix>[0-9A-Fa-f]{4})\s+'
    r'IY=(?P<iy>[0-9A-Fa-f]{4})\s+'
    r'SP=(?P<sp>[0-9A-Fa-f]{4})\s+'
    r'\[[^\]]*\]\s+'
    r'(?P<bytes>[0-9A-Fa-f ]+?)\s*$'
)


@dataclass
class TraceEntry:
    cycle: str        # 12-digit string preserved verbatim
    pc: int
    af: int
    bc: int
    de: int
    hl: int
    af2: int
    bc2: int
    de2: int
    hl2: int
    ix: int
    iy: int
    sp: int
    opbytes: List[int]

    @property
    def a(self) -> int: return (self.af >> 8) & 0xFF
    @property
    def f(self) -> int: return self.af & 0xFF
    @property
    def b(self) -> int: return (self.bc >> 8) & 0xFF
    @property
    def c(self) -> int: return self.bc & 0xFF
    @property
    def d(self) -> int: return (self.de >> 8) & 0xFF
    @property
    def e(self) -> int: return self.de & 0xFF
    @property
    def h(self) -> int: return (self.hl >> 8) & 0xFF
    @property
    def l(self) -> int: return self.hl & 0xFF


def parse_line(line: str) -> Optional[TraceEntry]:
    m = LINE_RE.match(line)
    if not m:
        return None
    try:
        opbytes = [int(b, 16) for b in m.group('bytes').split()]
    except ValueError:
        return None
    try:
        return TraceEntry(
            cycle=m.group('cycle'),
            pc=int(m.group('pc'), 16),
            af=int(m.group('af'), 16),
            bc=int(m.group('bc'), 16),
            de=int(m.group('de'), 16),
            hl=int(m.group('hl'), 16),
            af2=int(m.group('af2'), 16),
            bc2=int(m.group('bc2'), 16),
            de2=int(m.group('de2'), 16),
            hl2=int(m.group('hl2'), 16),
            ix=int(m.group('ix'), 16),
            iy=int(m.group('iy'), 16),
            sp=int(m.group('sp'), 16),
            opbytes=opbytes,
        )
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def fmt_mem(cycle: str, pc: int, addr: int, value) -> str:
    if isinstance(value, int):
        return f"{cycle}  {pc:04X}  MEM {addr & 0xFFFF:04X} {value & 0xFF:02X}"
    return f"{cycle}  {pc:04X}  MEM {addr & 0xFFFF:04X} ?"


def fmt_port(cycle: str, pc: int, port: int, value) -> str:
    if isinstance(value, int):
        return f"{cycle}  {pc:04X}  PORT {port & 0xFFFF:04X} {value & 0xFF:02X}"
    return f"{cycle}  {pc:04X}  PORT {port & 0xFFFF:04X} ?"


def fmt_nextreg(cycle: str, pc: int, nr: int, value) -> str:
    if isinstance(value, int):
        return f"{cycle}  {pc:04X}  NEXTREG {nr & 0xFF:02X} {value & 0xFF:02X}"
    return f"{cycle}  {pc:04X}  NEXTREG {nr & 0xFF:02X} ?"


def push_word(cycle: str, pc: int, sp: int, value: int) -> List[str]:
    """Z80 PUSH semantics: SP -= 2; (SP) = lo; (SP+1) = hi (actually
    high byte written first to SP-1, low to SP-2, but for write-set
    purposes we emit both)."""
    sp_new = (sp - 2) & 0xFFFF
    lo = value & 0xFF
    hi = (value >> 8) & 0xFF
    return [
        fmt_mem(cycle, pc, sp_new, lo),
        fmt_mem(cycle, pc, (sp_new + 1) & 0xFFFF, hi),
    ]


# ---------------------------------------------------------------------------
# Instruction decode
# ---------------------------------------------------------------------------

# Register selector for LD (HL),r / OUT (C),r etc. (bits 5:3 or 2:0 of opcode)
# Standard Z80 r encoding: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
def reg_value(e: TraceEntry, r: int) -> Optional[int]:
    if r == 0: return e.b
    if r == 1: return e.c
    if r == 2: return e.d
    if r == 3: return e.e
    if r == 4: return e.h
    if r == 5: return e.l
    if r == 7: return e.a
    return None  # r == 6 is (HL), not a register


# rp encoding for ED-prefixed LD (nn),rp / PUSH rp etc.
def pair_value(e: TraceEntry, rp: int) -> int:
    if rp == 0: return e.bc
    if rp == 1: return e.de
    if rp == 2: return e.hl
    if rp == 3: return e.sp
    return 0


# Main per-instruction decoder. Returns a list of formatted write-record strings.
def decode_writes(e: TraceEntry) -> List[str]:
    ob = e.opbytes
    if not ob:
        return []
    op = ob[0]
    cycle = e.cycle
    pc = e.pc

    # Single-byte primary opcodes -----------------------------------------
    if op == 0x02:  # LD (BC),A
        return [fmt_mem(cycle, pc, e.bc, e.a)]
    if op == 0x12:  # LD (DE),A
        return [fmt_mem(cycle, pc, e.de, e.a)]
    if op == 0x77:  # LD (HL),A
        return [fmt_mem(cycle, pc, e.hl, e.a)]

    # LD (HL),r  : 0x70..0x75  (r = op & 7); 0x76 = HALT excluded
    if 0x70 <= op <= 0x75:
        r = op & 0x07
        v = reg_value(e, r)
        if v is not None:
            return [fmt_mem(cycle, pc, e.hl, v)]
        return []

    if op == 0x36 and len(ob) >= 2:  # LD (HL),n
        return [fmt_mem(cycle, pc, e.hl, ob[1])]

    if op == 0x32 and len(ob) >= 3:  # LD (nn),A
        addr = ob[1] | (ob[2] << 8)
        return [fmt_mem(cycle, pc, addr, e.a)]

    if op == 0x22 and len(ob) >= 3:  # LD (nn),HL
        addr = ob[1] | (ob[2] << 8)
        return [
            fmt_mem(cycle, pc, addr, e.l),
            fmt_mem(cycle, pc, (addr + 1) & 0xFFFF, e.h),
        ]

    # PUSH BC/DE/HL/AF : 0xC5 0xD5 0xE5 0xF5
    if op == 0xC5:
        return push_word(cycle, pc, e.sp, e.bc)
    if op == 0xD5:
        return push_word(cycle, pc, e.sp, e.de)
    if op == 0xE5:
        return push_word(cycle, pc, e.sp, e.hl)
    if op == 0xF5:
        return push_word(cycle, pc, e.sp, e.af)

    # CALL nn : 0xCD nn nn  -- pushes PC+3 (return address)
    if op == 0xCD and len(ob) >= 3:
        ret = (pc + 3) & 0xFFFF
        return push_word(cycle, pc, e.sp, ret)

    # Conditional CALL cc,nn : 0b11ccc100 = 0xC4,0xCC,0xD4,0xDC,0xE4,0xEC,0xF4,0xFC
    # We cannot easily decide whether the condition was taken from the trace
    # alone (no post-state SP available). We use a heuristic: emit the push
    # only if the next instruction in the trace is at the target nn (cannot
    # know here without lookahead). Simpler: skip conditional CALLs — they
    # are rare in the path of interest and adding noise is worse than missing
    # them. To stay symmetric across both traces, we DO emit them, BUT use
    # SP change is unavailable. We'll emit unconditionally — that matches
    # both traces consistently if the same path runs (and if conditions
    # diverge, the divergence shows up earlier anyway).
    if op in (0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC) and len(ob) >= 3:
        # Heuristic: emit; if branch not taken, both emulators agree
        # (symmetric noise) — diff still meaningful.
        ret = (pc + 3) & 0xFFFF
        return push_word(cycle, pc, e.sp, ret)

    # RST n : 0xC7,0xCF,0xD7,0xDF,0xE7,0xEF,0xF7,0xFF -- push PC+1
    if op in (0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF):
        ret = (pc + 1) & 0xFFFF
        return push_word(cycle, pc, e.sp, ret)

    # EX (SP),HL : 0xE3 -- writes L to (SP), H to (SP+1)
    if op == 0xE3:
        return [
            fmt_mem(cycle, pc, e.sp, e.l),
            fmt_mem(cycle, pc, (e.sp + 1) & 0xFFFF, e.h),
        ]

    # OUT (n),A : 0xD3 nn
    if op == 0xD3 and len(ob) >= 2:
        port = ob[1] | (e.a << 8)  # Z80 OUT (n),A puts A on high half of address bus
        # For port-write reporting we want the 16-bit port address; but the
        # canonical interpretation users care about is the low byte. Emit
        # using a 16-bit port (low byte = nn, high byte = A) to keep
        # information; this matches how ZXNext ports often need full 16b.
        return [fmt_port(cycle, pc, port, e.a)]

    # IX/IY-prefixed: 0xDD or 0xFD ----------------------------------------
    if op in (0xDD, 0xFD) and len(ob) >= 2:
        is_iy = (op == 0xFD)
        idx = e.iy if is_iy else e.ix
        op2 = ob[1]

        # PUSH IX/IY : DD/FD E5
        if op2 == 0xE5:
            return push_word(cycle, pc, e.sp, idx)

        # EX (SP),IX/IY : DD/FD E3
        if op2 == 0xE3:
            return [
                fmt_mem(cycle, pc, e.sp, idx & 0xFF),
                fmt_mem(cycle, pc, (e.sp + 1) & 0xFFFF, (idx >> 8) & 0xFF),
            ]

        # LD (IX+d),r : DD 70..75/77 d   (NOT 0x76 which is LD (IX+d),(HL)? no, 76 is HALT in primary; with DD prefix bits 70-77 are LD (IX+d),r)
        # 0x70..0x77 with DD prefix means LD (IX+d),r; r = op2 & 7; 0x76 = LD (IX+d),(HL) — not actually; for DD-prefixed 0x76 is HALT. Skip.
        if 0x70 <= op2 <= 0x77 and op2 != 0x76 and len(ob) >= 3:
            r = op2 & 0x07
            d = ob[2]
            # Sign-extend d
            disp = d if d < 0x80 else d - 0x100
            addr = (idx + disp) & 0xFFFF
            v = reg_value(e, r)
            if v is not None:
                return [fmt_mem(cycle, pc, addr, v)]
            return []

        # LD (IX+d),n : DD 36 d n
        if op2 == 0x36 and len(ob) >= 4:
            d = ob[2]
            disp = d if d < 0x80 else d - 0x100
            addr = (idx + disp) & 0xFFFF
            return [fmt_mem(cycle, pc, addr, ob[3])]

        # DD/FD 22 nn nn : LD (nn),IX/IY
        if op2 == 0x22 and len(ob) >= 4:
            addr = ob[2] | (ob[3] << 8)
            return [
                fmt_mem(cycle, pc, addr, idx & 0xFF),
                fmt_mem(cycle, pc, (addr + 1) & 0xFFFF, (idx >> 8) & 0xFF),
            ]

        return []

    # ED-prefixed: 0xED -----------------------------------------------------
    if op == 0xED and len(ob) >= 2:
        op2 = ob[1]

        # LD (nn),rp  : ED 43/53/63/73 nn nn  (rp = (op2>>4)&3)
        if op2 in (0x43, 0x53, 0x63, 0x73) and len(ob) >= 4:
            rp = (op2 >> 4) & 0x03
            v = pair_value(e, rp)
            addr = ob[2] | (ob[3] << 8)
            return [
                fmt_mem(cycle, pc, addr, v & 0xFF),
                fmt_mem(cycle, pc, (addr + 1) & 0xFFFF, (v >> 8) & 0xFF),
            ]

        # OUT (C),r : ED 41/49/51/59/61/69/79 (r = (op2>>3)&7; 0x71 = OUT(C),0)
        if op2 in (0x41, 0x49, 0x51, 0x59, 0x61, 0x69, 0x79):
            r = (op2 >> 3) & 0x07
            v = reg_value(e, r)
            if v is not None:
                return [fmt_port(cycle, pc, e.bc, v)]
            return []
        if op2 == 0x71:  # OUT (C),0
            return [fmt_port(cycle, pc, e.bc, 0)]

        # OUTI / OUTD : ED A3 / ED AB -- writes [HL] to port (BC), unknown value
        if op2 in (0xA3, 0xAB):
            return [fmt_port(cycle, pc, e.bc, '?')]
        # OTIR / OTDR : ED B3 / ED BB -- iterated; one trace entry per iter
        if op2 in (0xB3, 0xBB):
            return [fmt_port(cycle, pc, e.bc, '?')]

        # LDI / LDD : ED A0 / ED A8 -- single-shot
        if op2 in (0xA0, 0xA8):
            return [fmt_mem(cycle, pc, e.de, '?')]
        # LDIR / LDDR : ED B0 / ED B8 -- iterated, one trace entry per iter
        if op2 in (0xB0, 0xB8):
            return [fmt_mem(cycle, pc, e.de, '?')]

        # Z80N PUSH nn : ED 8A hi lo  (note: spec varies; in the Spectrum Next
        # docs the PUSH is big-endian — hi byte FIRST). Check both ordering
        # against trace data. The header sample shows: ED 8A 00 01 at PC=$006A,
        # next PC is $006E (after a 4-byte instr), and SP becomes $FFFD per
        # jnext (or $FFFE per CSpect — CSpect starts SP=$0000). PUSH of $0100.
        # The header notes "PUSH $0100" so 00 01 -> $0100 means HI byte first.
        if op2 == 0x8A and len(ob) >= 4:
            hi = ob[2]
            lo = ob[3]
            v = (hi << 8) | lo
            return push_word(cycle, pc, e.sp, v)

        # Z80N NEXTREG nn,vv : ED 91 nn vv  (writes nn=>$243B, vv=>$253B)
        if op2 == 0x91 and len(ob) >= 4:
            nr = ob[2]
            vv = ob[3]
            return [
                fmt_port(cycle, pc, 0x243B, nr),
                fmt_port(cycle, pc, 0x253B, vv),
                fmt_nextreg(cycle, pc, nr, vv),
            ]

        # Z80N NEXTREG nn,A : ED 92 nn  (writes nn=>$243B, A=>$253B)
        if op2 == 0x92 and len(ob) >= 3:
            nr = ob[2]
            return [
                fmt_port(cycle, pc, 0x243B, nr),
                fmt_port(cycle, pc, 0x253B, e.a),
                fmt_nextreg(cycle, pc, nr, e.a),
            ]

        return []

    # No write
    return []


# ---------------------------------------------------------------------------
# Decode driver — also tracks NR-select state via raw OUT writes
# ---------------------------------------------------------------------------

def decode_trace(in_path: str, out_path: str) -> Tuple[int, int]:
    """Returns (lines_parsed, writes_emitted)."""
    parsed = 0
    written = 0
    selected_nr: Optional[int] = None

    with open(in_path, 'r', encoding='utf-8', errors='replace') as fin, \
         open(out_path, 'w', encoding='utf-8') as fout:
        for raw in fin:
            entry = parse_line(raw)
            if entry is None:
                continue
            parsed += 1

            recs = decode_writes(entry)

            # Track NR-select via raw OUT writes. NEXTREG (ED 91/ED 92) is
            # already handled inside decode_writes (emits a NEXTREG record);
            # here we only synthesize one when raw OUT($243B)/OUT($253B)
            # are used (Spectrum-Next NR access via plain OUT pairs).
            extra: List[str] = []
            for rec in recs:
                # rec format: "CCCCCCCCCCCC  PPPP  KIND ARG VAL"
                # Split deterministically.
                parts = rec.split()
                if len(parts) < 5:
                    continue
                kind = parts[2]
                if kind == 'PORT':
                    try:
                        port = int(parts[3], 16)
                        val_s = parts[4]
                        if val_s == '?':
                            val = None
                        else:
                            val = int(val_s, 16)
                    except ValueError:
                        continue
                    if port == 0x243B and val is not None:
                        selected_nr = val
                    elif port == 0x253B and selected_nr is not None and val is not None:
                        # Synthesize only if not already emitted (Z80N
                        # NEXTREG emits its own NEXTREG record). Detect
                        # by checking if this entry already has a
                        # NEXTREG line for the same NR/value.
                        already = any(
                            r.split()[2:5] == ['NEXTREG',
                                               f"{selected_nr:02X}",
                                               f"{val:02X}"]
                            for r in recs
                        )
                        if not already:
                            extra.append(
                                fmt_nextreg(entry.cycle, entry.pc,
                                            selected_nr, val)
                            )

            for rec in recs:
                fout.write(rec + '\n')
                written += 1
            for rec in extra:
                fout.write(rec + '\n')
                written += 1

    return parsed, written


# ---------------------------------------------------------------------------
# Diff mode
# ---------------------------------------------------------------------------

def diff_streams(a_path: str, b_path: str, limit: int = 40) -> int:
    """Print first `limit` divergent records (ignoring cycle column).

    Compare per-position (line-by-line after skipping cycle); print
    side-by-side context.
    """
    def key(line: str) -> str:
        # cycle  pc  kind  arg  value -> drop cycle
        parts = line.rstrip('\n').split(None, 1)
        return parts[1] if len(parts) > 1 else line.rstrip('\n')

    with open(a_path, 'r', encoding='utf-8') as fa, \
         open(b_path, 'r', encoding='utf-8') as fb:
        a_lines = fa.readlines()
        b_lines = fb.readlines()

    n = min(len(a_lines), len(b_lines))
    div = 0
    print(f"# jnext: {a_path}  ({len(a_lines)} writes)")
    print(f"# cspect: {b_path}  ({len(b_lines)} writes)")
    print(f"# first {limit} divergent records (cycle column ignored):")
    print(f"# {'idx':>8}  {'JNEXT':<46}  {'CSPECT':<46}")
    for i in range(n):
        ka = key(a_lines[i])
        kb = key(b_lines[i])
        if ka != kb:
            div += 1
            print(f"  {i:>8}  {a_lines[i].rstrip():<46}  {b_lines[i].rstrip():<46}")
            if div >= limit:
                break
    if div == 0 and len(a_lines) != len(b_lines):
        print(f"# streams match for first {n} records;"
              f" length differs: jnext={len(a_lines)} cspect={len(b_lines)}")
    return div


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Decode jnext/CSpect TraceLog to write stream (or diff two write streams)."
    )
    ap.add_argument('--diff', action='store_true',
                    help='Diff mode: takes two pre-decoded write-stream files.')
    ap.add_argument('--limit', type=int, default=40,
                    help='Diff mode: max divergent records to print.')
    ap.add_argument('args', nargs='+', help='Decode mode: <trace> <out>. Diff mode: <a> <b>.')
    ns = ap.parse_args(argv)

    if ns.diff:
        if len(ns.args) != 2:
            ap.error('--diff requires exactly two file arguments')
        diff_streams(ns.args[0], ns.args[1], limit=ns.limit)
        return 0

    if len(ns.args) != 2:
        ap.error('decode mode requires <trace_file> <output_file>')
    in_path, out_path = ns.args
    parsed, written = decode_trace(in_path, out_path)
    print(f"parsed={parsed} lines, emitted={written} write records -> {out_path}")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
