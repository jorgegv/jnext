#!/usr/bin/env python3
"""Minimal NextBASIC (+3DOS/PLUS3DOS tokenised BASIC) detokeniser.

Written for GH #165 (esxdos automap investigation) to decode
nextzxos/snapload.bas -- the real NextZXOS Snapshot Loader -- byte-for-byte
rather than guessing from a hexdump.

Token table verified against TWO independent sources, cross-checked and
found IN AGREEMENT:
  (1) tbblue repo src/asm/dot_commands/bastoken.def ("token_* equ $XX").
  (2) The official NextZXOS manual "NextBASIC New Commands and Features"
      (docs/nextzxos/NextBASIC_New_Commands_and_Features.pdf in the tbblue
      repo checkout), "New keyword tokens" section, page 45-46 of 54.

Usage:
    python3 tools/nextbasic_detok.py <file.bas> [line_number ...]

With no line numbers, prints every line as "LINE\tfile_off(hdr)=.. body_off=..
len=..\tDETOKENISED TEXT" -- the file offsets let a reviewer verify any
citation directly against a hex dump (`xxd -s <body_off> -l <len> file.bas`).
Passing one or more line numbers additionally prints the RAW HEX bytes of
that line's body, for byte-level citation.

Known limitations (fine for this investigation, not a general-purpose tool):
  - Assumes a 128-byte PLUS3DOS header (true for a BASIC-program PLUS3DOS
    file; verified via the file's own header type byte would be a nice-to-
    have, not done here since every file this tool was run against was
    independently `file`-verified as "Spectrum +3 data - BASIC program").
  - Does not decode embedded numeric constants beyond stripping the 0x0E
    marker + 5-byte binary mantissa (the ASCII digits before the marker are
    printed as-is, which is what the BASIC editor itself displays).
  - Control codes INK/PAPER/FLASH/BRIGHT (0x10-0x14) are shown as
    "<ctrlXX N>" rather than decoded further -- none appear in the lines
    this investigation needed.
"""
import sys

# See module docstring for the two independent sources this table was
# cross-checked against.
NAMES = {
0x81:"TIME",0x82:"PRIVATE",0x83:"IFELSE",0x84:"ENDIF",0x85:"EXIT",0x86:"REF",
0x87:"PEEK$",0x88:"REG",0x89:"DPOKE",0x8a:"DPEEK",0x8b:"MOD",0x8c:"<<",
0x8d:">>",0x8e:"UNTIL",0x8f:"ERROR",0x90:"ON",0x91:"DEFPROC",0x92:"ENDPROC",
0x93:"PROC",0x94:"LOCAL",0x95:"DRIVER",0x96:"WHILE",0x97:"REPEAT",0x98:"ELSE",
0x99:"REMOUNT",0x9a:"BANK",0x9b:"TILE",0x9c:"LAYER",0x9d:"PALETTE",0x9e:"SPRITE",
0x9f:"PWD",0xa0:"CD",0xa1:"MKDIR",0xa2:"RMDIR",0xa3:"SPECTRUM",0xa4:"PLAY",
0xa5:"RND",0xa6:"INKEY$",0xa7:"PI",0xa8:"FN",0xa9:"POINT",0xaa:"SCREEN$",
0xab:"ATTR",0xac:"AT",0xad:"TAB",0xae:"VAL$",0xaf:"CODE",0xb0:"VAL",0xb1:"LEN",
0xb2:"SIN",0xb3:"COS",0xb4:"TAN",0xb5:"ASN",0xb6:"ACS",0xb7:"ATN",0xb8:"LN",
0xb9:"EXP",0xba:"INT",0xbb:"SQR",0xbc:"SGN",0xbd:"ABS",0xbe:"PEEK",0xbf:"IN",
0xc0:"USR",0xc1:"STR$",0xc2:"CHR$",0xc3:"NOT",0xc4:"BIN",0xc5:"OR",0xc6:"AND",
0xc7:"<=",0xc8:">=",0xc9:"<>",0xca:"LINE",0xcb:"THEN",0xcc:"TO",0xcd:"STEP",
0xce:"DEF FN",0xcf:"CAT",0xd0:"FORMAT",0xd1:"MOVE",0xd2:"ERASE",0xd3:"OPEN #",
0xd4:"CLOSE #",0xd5:"MERGE",0xd6:"VERIFY",0xd7:"BEEP",0xd8:"CIRCLE",0xd9:"INK",
0xda:"PAPER",0xdb:"FLASH",0xdc:"BRIGHT",0xdd:"INVERSE",0xde:"OVER",0xdf:"OUT",
0xe0:"LPRINT",0xe1:"LLIST",0xe2:"STOP",0xe3:"READ",0xe4:"DATA",0xe5:"RESTORE",
0xe6:"NEW",0xe7:"BORDER",0xe8:"CONTINUE",0xe9:"DIM",0xea:"REM",0xeb:"FOR",
0xec:"GOTO",0xed:"GOSUB",0xee:"INPUT",0xef:"LOAD",0xf0:"LIST",0xf1:"LET",
0xf2:"PAUSE",0xf3:"NEXT",0xf4:"POKE",0xf5:"PRINT",0xf6:"PLOT",0xf7:"RUN",
0xf8:"SAVE",0xf9:"RANDOMIZE",0xfa:"IF",0xfb:"CLS",0xfc:"DRAW",0xfd:"CLEAR",
0xfe:"RETURN",0xff:"COPY",
}

PLUS3DOS_HEADER_LEN = 128


def detokenise(path: str):
    """Yields (line_number, header_file_offset, body_file_offset, body_len,
    body_hex, decoded_text) for every line in the tokenised BASIC file."""
    data = open(path, "rb").read()
    assert data[:8] == b"PLUS3DOS", f"{path}: not a PLUS3DOS file"
    i = PLUS3DOS_HEADER_LEN
    while i < len(data) - 4:
        ln = (data[i] << 8) | data[i + 1]
        length = data[i + 2] | (data[i + 3] << 8)
        hdr_off = i
        i += 4
        if ln == 0 or ln > 9999 or length <= 0 or length > 2000 or i + length > len(data):
            break
        body = data[i:i + length]
        body_off = i
        i += length

        j = 0
        out = []
        while j < len(body):
            b = body[j]
            if b == 0x0D:
                j += 1
                continue
            if b == 0x0E:
                j += 6  # marker + 5-byte binary mantissa of the preceding number
                continue
            if b in (0x10, 0x11, 0x12, 0x13, 0x14, 0x16, 0x17):
                if b == 0x16:
                    out.append(f"AT {body[j+1]},{body[j+2]}")
                    j += 3
                elif b == 0x17:
                    out.append(f"TAB {body[j+1]}")
                    j += 2
                else:
                    out.append(f"<ctrl{b:02x} {body[j+1]}>")
                    j += 2
                continue
            if 0x81 <= b <= 0xFF:
                out.append(NAMES.get(b, f"<${b:02x}>"))
                j += 1
                continue
            if b == 0x22:  # string literal
                k = j + 1
                while k < len(body) and body[k] != 0x22:
                    k += 1
                out.append('"' + body[j + 1:k].decode("latin1") + '"')
                j = k + 1
                continue
            out.append(chr(b) if 32 <= b < 127 else f"<{b:02x}>")
            j += 1

        yield ln, hdr_off, body_off, length, body.hex(), "".join(out)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    want_lines = {int(x) for x in sys.argv[2:]}
    for ln, hdr_off, body_off, length, hx, text in detokenise(path):
        print(f"{ln}\tfile_off(hdr)={hdr_off} body_off={body_off} len={length}\t{text}")
        if ln in want_lines:
            print(f"      RAW: {hx}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
