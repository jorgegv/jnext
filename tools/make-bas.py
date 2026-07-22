#!/usr/bin/env python3
"""
Minimal ZX Spectrum BASIC tokenizer + +3DOS header wrapper.
Produces a NextZXOS-loadable BAS file from a plain-text BASIC source.

Only handles the subset of keywords used by cspect-g46b-dump.bas:
  REM, OUT, LET, IN, SAVE, CODE, FOR, TO, NEXT, POKE, PRINT
plus integers, identifiers, strings, and basic punctuation.

Usage:
    tools/make-bas.py cspect-g46b-dump.bas /tmp/dump.bas
    mcopy -i $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img@@32256 /tmp/dump.bas ::/dump.bas
"""
import sys
import re
import struct

# ZX Spectrum 48K BASIC keyword tokens (subset).
# Full table at https://www.worldofspectrum.org/ZXBasicManual/zxmanchap25.html
TOKENS = {
    'CODE':   0xAF,
    'PEEK':   0xBE,
    'IN':     0xBF,
    'TO':     0xCC,
    'OUT':    0xDF,
    'BORDER': 0xE7,
    'REM':    0xEA,
    'FOR':    0xEB,
    'NEXT':   0xF3,
    'POKE':   0xF4,
    'PRINT':  0xF5,
    'SAVE':   0xF8,
    'LET':    0xF1,
    'RUN':    0xF7,
}

# Greedy keyword match: longest first.
KEYWORDS_SORTED = sorted(TOKENS.keys(), key=len, reverse=True)


def tokenize_line(text):
    """Tokenize a single BASIC line (without line number / length prefix).

    Returns bytes ending with 0x0D.
    """
    out = bytearray()
    i = 0
    n = len(text)
    in_string = False
    after_rem = False  # everything after REM is literal text (not tokenized)

    while i < n:
        c = text[i]

        # String literal: pass through verbatim until closing quote.
        if c == '"':
            in_string = not in_string
            out.append(ord(c))
            i += 1
            continue
        if in_string:
            out.append(ord(c))
            i += 1
            continue

        # After REM: keep raw (still encode digits/letters as ASCII).
        if after_rem:
            out.append(ord(c))
            i += 1
            continue

        # Try keyword match (case-insensitive on the source).
        matched = False
        for kw in KEYWORDS_SORTED:
            if text[i:i + len(kw)].upper() == kw:
                # Word-boundary check: previous + next char must not be alnum.
                # (avoids tokenizing "INPUT" as IN...PUT, etc.)
                prev_ok = i == 0 or not text[i - 1].isalnum()
                next_idx = i + len(kw)
                next_ok = next_idx >= n or not text[next_idx].isalnum()
                if prev_ok and next_ok:
                    out.append(TOKENS[kw])
                    i += len(kw)
                    if kw == 'REM':
                        after_rem = True
                    matched = True
                    break

        if matched:
            continue

        # Numeric literal: ASCII digits + ZX 5-byte FP representation.
        if c.isdigit():
            j = i
            while j < n and (text[j].isdigit() or text[j] == '.'):
                j += 1
            num_text = text[i:j]
            for ch in num_text:
                out.append(ord(ch))
            # Embed 5-byte FP marker: 0x0E + 5 bytes
            # For integers in [0, 65535]: format is 00 00 LSB MSB 00.
            try:
                v = int(num_text)
            except ValueError:
                v = int(float(num_text))
            if 0 <= v <= 65535:
                out.append(0x0E)
                out.append(0x00)
                out.append(0x00)
                out.append(v & 0xFF)
                out.append((v >> 8) & 0xFF)
                out.append(0x00)
            else:
                # Negative or large numbers: BASIC parser handles them at runtime
                # if we omit the FP rep (some interpreters require it; this BAS
                # uses only small positive ints).
                pass
            i = j
            continue

        # Plain ASCII pass-through (identifiers, operators, whitespace).
        out.append(ord(c))
        i += 1

    out.append(0x0D)  # line terminator
    return bytes(out)


def tokenize_program(source_text):
    """Tokenize whole BASIC program text → tokenized body bytes."""
    body = bytearray()
    for line in source_text.splitlines():
        line = line.rstrip('\r\n')
        line = line.strip()
        if not line:
            continue
        # Extract leading line number.
        m = re.match(r'^(\d+)\s+(.*)$', line)
        if not m:
            raise SystemExit(f'line missing line number: {line!r}')
        lineno = int(m.group(1))
        rest = m.group(2)
        line_body = tokenize_line(rest)
        # Line header: line_no big-endian, length little-endian (including 0x0D).
        body += struct.pack('>H', lineno)
        body += struct.pack('<H', len(line_body))
        body += line_body
    return bytes(body)


def make_p3dos_basic(body, autostart_line=None):
    """Wrap tokenized BASIC body with a 128-byte +3DOS header."""
    header = bytearray(128)
    header[0:8] = b'PLUS3DOS'
    header[8] = 0x1A
    header[9] = 0x01    # issue (NextZXOS expects 1; rejects 0 as "wrong file type")
    header[10] = 0      # version
    file_length = 128 + len(body)
    header[11:15] = struct.pack('<I', file_length)
    header[15] = 0      # file type 0 = BASIC
    header[16:18] = struct.pack('<H', len(body))   # BASIC program length
    if autostart_line is not None:
        header[18:20] = struct.pack('<H', autostart_line)
    else:
        header[18:20] = struct.pack('<H', 0x8000)  # = no autostart
    header[20:22] = struct.pack('<H', len(body))   # variable offset = body length (no vars)
    # bytes 22..126 stay zero
    # checksum byte 127 = sum of bytes 0..126 mod 256
    header[127] = sum(header[0:127]) & 0xFF
    return bytes(header) + body


def main():
    if len(sys.argv) < 3:
        print('usage: make-bas.py <input.txt> <output.bas> [--autostart LINE]', file=sys.stderr)
        sys.exit(2)

    src_path = sys.argv[1]
    dst_path = sys.argv[2]
    autostart = None
    if '--autostart' in sys.argv:
        idx = sys.argv.index('--autostart')
        autostart = int(sys.argv[idx + 1])

    with open(src_path, 'r') as f:
        text = f.read()

    body = tokenize_program(text)
    output = make_p3dos_basic(body, autostart_line=autostart)

    with open(dst_path, 'wb') as f:
        f.write(output)

    print(f'wrote {dst_path}: header=128 + body={len(body)} = {len(output)} bytes')
    if autostart:
        print(f'autostart line: {autostart}')


if __name__ == '__main__':
    main()
