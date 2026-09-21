#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# nex-extended-fh0-func: a NEX whose file is much larger than the banks its
# header declares, and whose header says file_handle=0 (close the file after
# loading). The real loaders load the declared banks, close the file and run
# the program — no size check, nothing reads the rest (nexload2.asm:390-395,
# nexload.asm:547-551). GH #250 (Spectron2084, 293120 such bytes) replaced
# issue #10's refusal with exactly that plus a warning. Discriminative both
# ways: the extended file MUST load, RUN (its code writes a line to the magic
# port) and warn about the ignored bytes, with no error; a plain NEX of the
# exact declared size and a stale-num_banks NEX MUST NOT draw the warning.
# Files are synthesised here (no binary in git).
if want nex-extended-fh0-func; then
    begin_func nex-extended-fh0-func
    ext_nex="$TMP_DIR/extended.nex"
    plain_nex="$TMP_DIR/plain.nex"
    stale_nex="$TMP_DIR/stale.nex"
    python3 - "$ext_nex" "$plain_nex" "$stale_nex" <<'PY'
import sys, struct
def header(num_banks_field, bitmap_banks):
    h = bytearray(512)
    h[0:4] = b"Next"; h[4:8] = b"V1.2"
    h[8] = 0                  # ram_required
    h[9] = num_banks_field    # decorative scalar (offset 9)
    h[10] = 0                 # screen_flags
    h[12:14] = struct.pack("<H", 0x8000)  # SP
    h[14:16] = struct.pack("<H", 0x8000)  # PC
    for b in bitmap_banks:
        h[18 + b] = 1         # presence bitmap = the authoritative count
    return bytes(h)
# extended: bank 2 (0x8000) holds code that prints GH250-RAN on magic port
# 0xCAFE, then 200 KB of trailing bytes; file_handle (offset 140) stays 0.
code = bytes([
    0x01, 0xFE, 0xCA,         # 8000 ld bc,$CAFE
    0x21, 0x20, 0x80,         # 8003 ld hl,msg ($8020)
    0x7E,                     # 8006 loop: ld a,(hl)
    0xB7,                     # 8007 or a
    0x28, 0x05,               # 8008 jr z,done ($800F)
    0xED, 0x79,               # 800A out (c),a
    0x23,                     # 800C inc hl
    0x18, 0xF7,               # 800D jr loop ($8006)
    0x18, 0xFE,               # 800F done: jr done
])
bank2 = bytearray(16384)
bank2[0:len(code)] = code
msg = b"GH250-RAN\n\x00"
bank2[0x20:0x20 + len(msg)] = msg
open(sys.argv[1], "wb").write(header(1, [2]) + bytes(bank2) + b"\xAA"*200000)
# plain: exactly header + 1 bank (bitmap), no trailing → no warning
open(sys.argv[2], "wb").write(header(1, [0]) + b"\x00"*16384)
# stale: bitmap says 3 banks, num_banks scalar wrongly says 1; file matches the
# BITMAP (3*16384). Must be taken as exact size — expected_size must come from
# the bitmap, not the scalar. Guards against the num_banks false-positive.
open(sys.argv[3], "wb").write(header(1, [0, 1, 2]) + b"\x00"*(3*16384))
PY
    ext_rc=0
    ext_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next \
        --magic-port 0xCAFE --magic-port-mode line --load "$ext_nex" \
        --delayed-automatic-exit-frames 50 2>&1) || ext_rc=$?
    plain_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$plain_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    stale_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$stale_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    ext_warn=$(echo "$ext_out" | grep -F "[warning]" | grep -F "extended NEX file" \
        | grep -cF "ignoring the trailing bytes" || true)
    ext_err=$(echo "$ext_out" | grep -F "[error]" | grep -cF "NEX" || true)
    ext_ran=$(echo "$ext_out" | grep -cxF "GH250-RAN" || true)
    plain_hit=$(echo "$plain_out" | grep -cF "extended NEX file" || true)
    stale_hit=$(echo "$stale_out" | grep -cF "extended NEX file" || true)
    # stale must NOT draw the extended warning AND must warn about the
    # num_banks/bitmap mismatch
    stale_warn=$(echo "$stale_out" | grep -cF "disagrees with the bank bitmap" || true)
    if [[ "$ext_rc" -eq 0 && "$ext_warn" -eq 1 && "$ext_err" -eq 0 && "$ext_ran" -ge 1 \
          && "$plain_hit" -eq 0 && "$stale_hit" -eq 0 && "$stale_warn" -ge 1 ]]; then
        pass_row " (extended fh=0 loaded, ran, warned; exact-size silent; stale num_banks warned)"
    else
        fail_row " (ext rc=$ext_rc want0, warn=$ext_warn want1, err=$ext_err want0, ran=$ext_ran want>=1, plain=$plain_hit want0, stale_ext=$stale_hit want0, stale_warn=$stale_warn want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
