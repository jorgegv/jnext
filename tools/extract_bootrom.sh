#!/usr/bin/env bash
# Extract the ZX Spectrum Next boot ROM from VHDL source to a binary file.
#
# Parses the ROM_ARRAY constant from bootrom.vhd, extracting all x"HH" hex
# byte literals, and writes them as a raw 8K binary.
#
# Usage:
#     ./extract_bootrom.sh <input.vhd> <output.rom>

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <input.vhd> <output.rom>" >&2
    exit 1
fi

vhd_path="$1"
rom_path="$2"

if [[ ! -f "$vhd_path" ]]; then
    echo "ERROR: file not found: $vhd_path" >&2
    exit 1
fi

# Extract all x"HH" hex byte literals and convert to binary
hex_bytes=$(grep -oP 'x"\K[0-9A-Fa-f]{2}(?=")' "$vhd_path")

if [[ -z "$hex_bytes" ]]; then
    echo "ERROR: no hex bytes found in $vhd_path" >&2
    exit 1
fi

count=$(echo "$hex_bytes" | wc -l)
echo "Extracted $count bytes ($((count / 1024))K) from $vhd_path"

if [[ $count -ne 8192 ]]; then
    echo "WARNING: expected 8192 bytes, got $count" >&2
fi

# Convert hex strings to binary
printf '%s\n' "$hex_bytes" | while read -r h; do
    printf "\\x$h"
done > "$rom_path"

echo "Written to $rom_path"
