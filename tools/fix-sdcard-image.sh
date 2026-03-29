#!/usr/bin/env bash
#
# fix-sdcard-image.sh — Create a bootable SD card image from the CSpect image.
#
# Copies the original image and injects a default config.ini into
# machines/next/ so NextZXOS can boot.  Does NOT modify the original.
#
# Requires: mtools (mcopy, mdir)
#
# Usage:
#   ./tools/fix-sdcard-image.sh [source.img] [dest.img]
#
# Defaults:
#   source = roms/cspect-next-1gb.img
#   dest   = roms/nextzxos-1gb.img

set -euo pipefail

SRC="${1:-roms/cspect-next-1gb.img}"
DST="${2:-roms/nextzxos-1gb.img}"

if [ ! -f "$SRC" ]; then
    echo "ERROR: source image not found: $SRC" >&2
    exit 1
fi

# --- Copy image ---
if [ "$SRC" != "$DST" ]; then
    echo "Copying $SRC → $DST ..."
    cp "$SRC" "$DST"
else
    echo "ERROR: source and dest must be different files" >&2
    exit 1
fi

# --- Create config.ini ---
CONFIGFILE=$(mktemp /tmp/config.ini.XXXXXX)
trap 'rm -f "$CONFIGFILE"' EXIT

cat > "$CONFIGFILE" <<'CONFIGEOF'
scandoubler=1
50_60hz=0
timex=1
psgmode=0
stereomode=1
intsnd=1
turbosound=1
dac=1
divmmc=0
divports=1
mf=0
joystick1=2
joystick2=0
ps2=0
scanlines=0
turbokey=1
timing=0
default=0
dma=0
keyb_issue=0
ay48=0
uart_i2c=1
kmouse=1
ulaplus=1
hdmisound=1
beepmode=0
buttonswap=0
mousedpi=1
CONFIGEOF

# --- mtools config: partition 1 starts at sector 63 ---
# mtools needs MTOOLS_SKIP_CHECK to avoid geometry complaints on raw images.
export MTOOLS_SKIP_CHECK=1

# The partition offset: sector 63 * 512 = 32256 bytes.
# mtools can address a partition inside a disk image with the @@offset syntax.
MTOOLSRC=$(mktemp /tmp/mtoolsrc.XXXXXX)
trap 'rm -f "$CONFIGFILE" "$MTOOLSRC"' EXIT

cat > "$MTOOLSRC" <<EOF
drive x:
    file="$DST"
    offset=32256
EOF

export MTOOLSRC

echo "Checking existing files..."
mdir x:/machines/next/ 2>/dev/null || true

echo ""
echo "Injecting config.ini → machines/next/config.ini ..."
mcopy -o "$CONFIGFILE" x:/machines/next/config.ini

echo ""
echo "Verifying..."
mdir x:/machines/next/config.ini

echo ""
echo "Done. Fixed image: $DST"
echo "Run with:"
echo "  ./build/jnext --boot-rom roms/nextboot.rom --divmmc-rom roms/enNxtmmc.rom --sd-card $DST"
