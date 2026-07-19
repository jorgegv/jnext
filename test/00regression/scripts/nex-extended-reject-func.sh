#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# nex-extended-reject-func: NEXTEST.NEX and similar NextZXOS apps are a small
# NEX with a large payload appended that they stream from their own file at
# runtime. Loaded with --load there is no NextZXOS, so they black-screen. The
# loader rejects any NEX whose file is substantially larger than its header
# (banks + screens) describes. Discriminative both ways: an extended NEX MUST
# be rejected with the "extended NEX file" message, and a plain NEX of the
# exact declared size MUST NOT be. Files are synthesised here (no 26 MB binary
# in git).
if want nex-extended-reject-func; then
    begin_func nex-extended-reject-func
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
    for i in range(bitmap_banks):
        h[18 + i] = 1         # presence bitmap = the authoritative count
    return bytes(h)
# extended: bitmap says 1 bank (16384) + 200 KB of appended payload → REJECT
open(sys.argv[1], "wb").write(header(1, 1) + b"\x00"*16384 + b"\xAA"*200000)
# plain: exactly header + 1 bank (bitmap), no trailing → accept
open(sys.argv[2], "wb").write(header(1, 1) + b"\x00"*16384)
# stale: bitmap says 3 banks, num_banks scalar wrongly says 1; file matches the
# BITMAP (3*16384). Must be accepted — expected_size must come from the bitmap,
# not the scalar. Guards against the num_banks false-positive.
open(sys.argv[3], "wb").write(header(1, 3) + b"\x00"*(3*16384))
PY
    ext_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$ext_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    plain_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$plain_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    stale_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$stale_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    ext_hit=$(echo "$ext_out" | grep -cF "extended NEX file" || true)
    plain_hit=$(echo "$plain_out" | grep -cF "extended NEX file" || true)
    stale_hit=$(echo "$stale_out" | grep -cF "extended NEX file" || true)
    # stale must NOT be rejected AND must warn about the num_banks/bitmap mismatch
    stale_warn=$(echo "$stale_out" | grep -cF "disagrees with the bank bitmap" || true)
    if [[ "$ext_hit" -ge 1 && "$plain_hit" -eq 0 && "$stale_hit" -eq 0 && "$stale_warn" -ge 1 ]]; then
        pass_row " (extended rejected; exact-size accepted; stale num_banks warned + loaded)"
    else
        fail_row " (ext=$ext_hit want>=1, plain=$plain_hit want0, stale_rej=$stale_hit want0, stale_warn=$stale_warn want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
