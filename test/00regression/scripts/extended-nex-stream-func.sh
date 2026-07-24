#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #29/#84: a runtime-generated NEX uses both supported paths against its
# own host-backed file. F_SEEK/F_READ fetch "PAY!" from the appended payload;
# DISK_FILEMAP/DISK_STRMSTART plus port-$EB reads fetch "Next" from the file
# header; then a raw SD CMD18 against the same synthetic file-map address
# fetches "Next" again. The guest reports all three through the magic port.
if want extended-nex-stream-func; then
    begin_func extended-nex-stream-func
    nex="$TMP_DIR/extended-stream.nex"
    python3 - "$nex" <<'PY'
import struct
import sys

code = bytearray()
def e(*values):
    code.extend(values)

HANDLE = 0xC200
FLAGS  = 0xC201
BUF    = 0x9000
MAP    = 0x9200
MAGIC  = 0xCAFE
PAYLOAD_OFFSET = 512 + 16384

# Handle delivered by nexload at $BFFE -> private variable.
e(0x3A, 0xFE, 0xBF)                   # ld a,($bffe)
e(0x32, HANDLE & 0xff, HANDLE >> 8)   # ld (handle),a

# F_SEEK(handle, PAYLOAD_OFFSET, ESX_SEEK_SET in IXL).
e(0x01, 0x00, 0x00)                   # ld bc,0
e(0x11, PAYLOAD_OFFSET & 0xff, PAYLOAD_OFFSET >> 8)
e(0xDD, 0x21, 0x00, 0x00)             # ld ix,0
e(0xCF, 0x9F)                         # rst $08 : db F_SEEK

# F_READ four appended bytes into BUF.
e(0x3A, HANDLE & 0xff, HANDLE >> 8)
e(0xDD, 0x21, BUF & 0xff, BUF >> 8)
e(0x01, 0x04, 0x00)
e(0xCF, 0x9D)
e(0x01, MAGIC & 0xff, MAGIC >> 8)     # ld bc,$cafe
for address in range(BUF, BUF + 4):
    e(0x3A, address & 0xff, address >> 8, 0xED, 0x79)
e(0x3E, ord('|'), 0xED, 0x79)

# Streaming preamble: rewind, read one byte, then obtain the file map.
e(0x3A, HANDLE & 0xff, HANDLE >> 8)
e(0x01, 0x00, 0x00, 0x11, 0x00, 0x00)
e(0xDD, 0x21, 0x00, 0x00)
e(0xCF, 0x9F)
e(0x3A, HANDLE & 0xff, HANDLE >> 8)
e(0xDD, 0x21, 0x00, 0x91)
e(0x01, 0x01, 0x00)
e(0xCF, 0x9D)
e(0x3A, HANDLE & 0xff, HANDLE >> 8)
e(0xDD, 0x21, MAP & 0xff, MAP >> 8)
e(0x11, 0x02, 0x00)                   # two-entry capacity
e(0xCF, 0x85)                         # DISK_FILEMAP
e(0x32, FLAGS & 0xff, FLAGS >> 8)

# Start at the first returned IXDE card address, using the v2.01 no-wait
# variant. Port $EB therefore yields FE, then the file's "Next" magic.
e(0xED, 0x5B, MAP & 0xff, MAP >> 8)   # ld de,(map)
e(0xDD, 0x2A, (MAP + 2) & 0xff, (MAP + 2) >> 8)
e(0x01, 0x00, 0x00)
e(0x3A, FLAGS & 0xff, FLAGS >> 8)
e(0xF6, 0x80)
e(0xCF, 0x86)                         # DISK_STRMSTART
e(0xDB, 0xEB)                         # consume ready token
e(0x01, MAGIC & 0xff, MAGIC >> 8)
for _ in range(4):
    e(0xDB, 0xEB, 0xED, 0x79)
e(0x3A, FLAGS & 0xff, FLAGS >> 8)
e(0xCF, 0x87)                         # DISK_STRMEND

# Atic Atac's route: consume the same DISK_FILEMAP address directly through
# SDHC CMD18 rather than calling DISK_STRMSTART.
e(0x01, MAGIC & 0xff, MAGIC >> 8)
e(0x3E, ord('|'), 0xED, 0x79)
e(0x3E, 0xFE, 0xD3, 0xE7)             # select SD0
e(0xDB, 0xEB)                         # consume old SPI receive latch
e(0x3E, 0x52, 0xD3, 0xEB)             # CMD18
for address in (MAP + 3, MAP + 2, MAP + 1, MAP):
    e(0x3A, address & 0xff, address >> 8, 0xD3, 0xEB)
e(0x3E, 0x80, 0xD3, 0xEB)             # dummy CRC
e(0xDB, 0xEB, 0xFE, 0xFF, 0x28, 0xFA) # poll past NCR to R1
e(0xDB, 0xEB, 0xFE, 0xFE, 0x20, 0xFA) # poll to data token
e(0x01, MAGIC & 0xff, MAGIC >> 8)
for _ in range(4):
    e(0xDB, 0xEB, 0xED, 0x79)

e(0x01, MAGIC & 0xff, MAGIC >> 8)
e(0x3E, 0x0A, 0xED, 0x79)             # newline flushes line mode
e(0x18, 0xFE)                         # jr $

header = bytearray(512)
header[0:8] = b"NextV1.2"
header[8] = 0
header[9] = 1
header[12:14] = struct.pack("<H", 0xFF00)
header[14:16] = struct.pack("<H", 0xC000)
header[18] = 1                         # bank 0 present
header[139] = 0                        # bank 0 at $C000
header[140:142] = struct.pack("<H", 0xBFFE)

bank = bytearray(16384)
bank[:len(code)] = code
open(sys.argv[1], "wb").write(header + bank + b"PAY!")
PY

    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --silent \
        "${SD_CARD_ARGS[@]}" --machine next --load "$nex" \
        --magic-port 0xCAFE --magic-port-mode line \
        --delayed-automatic-exit-frames 20 2>&1) || true
    if echo "$out" | grep -qF "PAY!|Next|Next"; then
        pass_row " (file API + API/raw block streams returned host bytes)"
    else
        fail_row " (expected magic-port line 'PAY!|Next|Next')"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
