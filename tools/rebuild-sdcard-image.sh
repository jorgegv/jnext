#!/usr/bin/env bash
#
# rebuild-sdcard-image.sh — Rebuild SD card image with correct FAT32 cluster size.
#
# The CSpect 1GB image uses 64 sectors/cluster, giving only 32758 clusters.
# FatFs (in the Next firmware) requires >65525 clusters to recognize FAT32.
# This script reformats the partition with 16 sectors/cluster (131K+ clusters)
# and copies all files from the original image.
#
# Usage: ./tools/rebuild-sdcard-image.sh <source.img> <output.img>

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <source.img> <output.img>"
    echo "  Rebuilds the SD card image with FAT32-compatible cluster size."
    exit 1
fi

SRC="$1"
DST="$2"

if [ ! -f "$SRC" ]; then
    echo "Error: source image '$SRC' not found"
    exit 1
fi

if [ "$SRC" = "$DST" ]; then
    echo "Error: source and destination must be different files"
    exit 1
fi

echo "=== Rebuild SD card image for NextZXOS boot ==="
echo "Source: $SRC"
echo "Output: $DST"

# Get image size
IMG_SIZE=$(stat -c%s "$SRC")
echo "Image size: $IMG_SIZE bytes ($(( IMG_SIZE / 1048576 )) MB)"

# Copy the image (we'll reformat the partition in-place)
echo "Copying image..."
cp "$SRC" "$DST"

# The partition starts at sector 63 (LBA), spanning 2097089 sectors
# We need to reformat just the partition area with smaller cluster size.
# Partition offset = 63 * 512 = 32256 bytes
PART_OFFSET=32256
PART_SECTORS=2097089

echo "Partition: offset=$PART_OFFSET, sectors=$PART_SECTORS"

# Extract files from the original partition using mtools
TMPDIR=$(mktemp -d)
echo "Extracting files from original image to $TMPDIR..."

# Set up mtools config for the source image
export MTOOLS_SKIP_CHECK=1
MTOOLSRC=$(mktemp)
echo "drive x: file=\"$SRC\" partition=1" > "$MTOOLSRC"
export MTOOLSRC

# Copy all files recursively
mcopy -s -n -o x:/ "$TMPDIR/" 2>/dev/null || true

echo "Files extracted. Contents:"
find "$TMPDIR" -maxdepth 2 -type f | head -30
echo "..."

# Reformat the partition with smaller cluster size (16 sectors = 8K clusters)
echo "Reformatting partition with cluster size 16 sectors (8K)..."
mkfs.fat -F 32 -s 16 -n "NEXT1.4.1" --offset 63 "$DST" $PART_SECTORS

# Update mtools config for the destination image
echo "drive x: file=\"$DST\" partition=1" > "$MTOOLSRC"

# Copy files back
echo "Copying files back to reformatted image..."
for item in "$TMPDIR"/*; do
    if [ -e "$item" ]; then
        name=$(basename "$item")
        if [ -d "$item" ]; then
            echo "  Copying directory: $name/"
            mcopy -s -n -o "$item" x:/ 2>/dev/null || echo "  Warning: some files in $name/ may have failed"
        else
            echo "  Copying file: $name"
            mcopy -n -o "$item" x:/ 2>/dev/null || echo "  Warning: failed to copy $name"
        fi
    fi
done

# Verify the result
echo ""
echo "=== Verification ==="
python3 -c "
import struct
with open('$DST', 'rb') as f:
    f.seek(63 * 512)
    bpb = f.read(512)
    spc = bpb[13]
    reserved = struct.unpack_from('<H', bpb, 14)[0]
    num_fats = bpb[16]
    fat_sz = struct.unpack_from('<I', bpb, 36)[0]
    tot_sec = struct.unpack_from('<I', bpb, 32)[0]
    sysect = reserved + (fat_sz * num_fats)
    nclst = (tot_sec - sysect) // spc
    fat_type = 'FAT32' if nclst > 65525 else 'FAT16 (WRONG!)'
    print(f'Cluster size: {spc} sectors ({spc * 512} bytes)')
    print(f'Total sectors: {tot_sec}')
    print(f'Cluster count: {nclst}')
    print(f'FAT type: {fat_type}')
"

# Cleanup
rm -rf "$TMPDIR" "$MTOOLSRC"
echo ""
echo "Done! Output image: $DST"
