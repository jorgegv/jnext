#!/usr/bin/env bash
#
# fix-sdcard-image.sh — Make an SD card image bootable under jnext.
#
# Two fixes can be applied to make a shipped NextZXOS / CSpect image
# boot under jnext's strict (firmware-faithful) SD/FAT32 emulation:
#
#  1. **FAT32 cluster-count fix.** The shipped 1 GB image uses 32 KB
#     clusters (SecPerClus=64), yielding only ~32 758 data clusters —
#     below the FAT32 spec minimum of 65 525. tbblue.fw's strict FatFs
#     (correctly per spec) categorises it as FAT16, then rejects it
#     because the FAT32 BPB has n_rootdir=0. CSpect's own SD driver
#     tolerates the under-clustered variant; jnext does not, by design
#     (see CLAUDE.md / project_nextzxos_task9_stagec.md memory). The
#     fix reformats the partition with 8 KB clusters (16 sectors per
#     cluster, ~131 000 clusters) and copies the original tree back.
#
#  2. **Default config.ini.** Inject /MACHINES/NEXT/config.ini so
#     NextZXOS picks up sensible default hardware settings on first
#     boot (DivMMC off, AY+turbo on, ULA+ on, etc.).
#
#  3. **ZX81 ROM (GH #284).** The 24.11 distribution ships
#     /MACHINES/NEXT/zx81.rom as ONE 16 KB page, but its menu.def loads the
#     ZX81 entry as two, so the firmware boot menu stops on
#     "zx81.rom...error reading!". When the file is exactly that 16 KB file
#     (full SHA-256), it is replaced by itself twice (32 KB), as upstream
#     tbblue did in commit e2df8e15. Any other zx81.rom is left alone. This
#     is the same change jnext makes to the image it downloads.
#
# Requires: mtools (mcopy, mdir, mformat, minfo, mmd), sha256sum.
#
# Usage:
#   ./tools/fix-sdcard-image.sh <source.img> [dest.img]
#
# If dest.img is omitted (or equal to source.img), the script modifies
# source.img IN PLACE — it asks for confirmation first since source is
# overwritten. When a reformat is needed, the in-place mode writes to
# a temporary file first and renames it on top of source only after
# all steps succeed (so a failure mid-fix leaves source untouched).
#
# jnext's tests do NOT use a fixture under `roms/` any more (GH #75): they
# resolve `~/.jnext/sdcard/cspect-next-1gb-fixed.img`, which jnext itself
# downloads and FAT32-patches via `--sdcard-download-confirm`. This script
# stays for re-clustering an image you supply yourself — an under-clustered
# 1 GB FAT32 (32 KB clusters -> 32758 clusters, below the spec minimum of
# 65525) is rejected by tbblue.fw's FatFs, correctly.

set -euo pipefail

SRC="${1:-}"
DST="${2:-}"

if [ -z "$SRC" ]; then
    cat >&2 <<EOF
Usage: $0 <source.img> [dest.img]

If dest.img is omitted, source.img is modified in place (after a
confirmation prompt).
EOF
    exit 1
fi

if [ ! -f "$SRC" ]; then
    echo "ERROR: source image not found: $SRC" >&2
    exit 1
fi

# --- Decide in-place vs copy mode ---
if [ -z "$DST" ] || [ "$DST" = "$SRC" ]; then
    IN_PLACE=1
    DST="$SRC"
    echo ""
    echo "WARNING: no separate destination provided —"
    echo "         '$SRC' will be MODIFIED IN PLACE."
    echo ""
    read -r -p "Continue? [y/N] " CONFIRM
    case "$CONFIRM" in
        [Yy]|[Yy][Ee][Ss]) ;;
        *) echo "Aborted." >&2; exit 1 ;;
    esac
else
    IN_PLACE=0
fi

# --- Required tools ---
for tool in mcopy mdir mformat minfo mmd sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: '$tool' (from mtools / coreutils) is required" >&2
        exit 1
    fi
done

# --- mtools setup ---
# Partition 1 starts at sector 63 on every shipped CSpect / NextZXOS image
# (verified by fdisk). MBR offset = 63 * 512 = 32256 bytes.
# MTOOLS_SKIP_CHECK suppresses geometry warnings on raw image files.
export MTOOLS_SKIP_CHECK=1
PART_OFFSET=32256

# Helper: produce an absolute path (string-only — does NOT require the
# file to exist; used because mtools' @@offset syntax stops working
# after we cd into the extraction temp dir).
abspath() {
    case "$1" in
        /*) printf '%s' "$1" ;;
        *)  printf '%s' "$PWD/$1" ;;
    esac
}

# --- Detect FAT32 under-cluster bug (read SOURCE BPB) ---
SRC_DRIVE_SPEC="$(abspath "$SRC")@@$PART_OFFSET"
CSIZE=$(minfo -i "$SRC_DRIVE_SPEC" :: 2>/dev/null \
         | sed -n 's/.*-c \([0-9][0-9]*\).*/\1/p' | head -1)

if [ -z "$CSIZE" ]; then
    echo "WARNING: could not parse cluster size from BPB of $SRC; assuming valid" >&2
    NEEDS_FAT32_FIX=0
elif [ "$CSIZE" -ge 32 ]; then
    NEEDS_FAT32_FIX=1
else
    NEEDS_FAT32_FIX=0
fi

# --- Determine WORK file ---
# Three cases:
#   A) IN_PLACE + reformat needed → write to temp, rename onto SRC at end.
#   B) IN_PLACE + no reformat     → modify SRC directly.
#   C) Copy mode                  → cp SRC → DST, modify DST.
WORK_TMP=""
if [ "$IN_PLACE" = "1" ] && [ "$NEEDS_FAT32_FIX" = "1" ]; then
    WORK_TMP=$(mktemp -p "$(dirname "$SRC")" "$(basename "$SRC").fix.XXXXXX")
    WORK="$WORK_TMP"
    echo "Reformat needed; writing to temporary file: $WORK"
    cp "$SRC" "$WORK"
elif [ "$IN_PLACE" = "1" ]; then
    WORK="$SRC"
    echo "No reformat needed; modifying $SRC in place."
else
    WORK="$DST"
    echo "Copying $SRC → $DST ..."
    cp "$SRC" "$WORK"
fi

DRIVE_SPEC="$(abspath "$WORK")@@$PART_OFFSET"

# --- Cleanup trap ---
EXTRACT_DIR=""
CONFIGFILE=""
ZX81DIR=""
cleanup() {
    [ -n "$EXTRACT_DIR" ] && [ -d "$EXTRACT_DIR" ] && rm -rf "$EXTRACT_DIR"
    [ -n "$CONFIGFILE" ]  && [ -f "$CONFIGFILE"  ] && rm -f  "$CONFIGFILE"
    [ -n "$ZX81DIR" ]     && [ -d "$ZX81DIR"     ] && rm -rf "$ZX81DIR"
    # Only remove WORK_TMP if we still own it (i.e., the final rename
    # didn't happen — the script aborted mid-way). After a successful
    # rename we clear WORK_TMP so this becomes a no-op.
    [ -n "$WORK_TMP" ] && [ -f "$WORK_TMP" ] && rm -f "$WORK_TMP"
    # Succeed explicitly. Under `set -e` the trap's status is the script's, and
    # the false test above made every SUCCESSFUL run exit 1. A failing run
    # still exits with its own status: `exit N` is kept across the trap.
    return 0
}
trap cleanup EXIT

# --- FAT32 fix (if needed) ---
if [ "$NEEDS_FAT32_FIX" = "1" ]; then
    KB_PER_CLUSTER=$(( CSIZE * 512 / 1024 ))
    echo ""
    echo "FAT32 fix: source image uses ${KB_PER_CLUSTER} KB clusters"
    echo "           (FatFs requires ≥ 65 525 data clusters; this"
    echo "            image has ~32 758, below the spec minimum)."

    # Read partition geometry from BPB so the reformat preserves it.
    PART_INFO=$(minfo -i "$DRIVE_SPEC" :: 2>/dev/null)
    BIG_SIZE=$(echo "$PART_INFO" | sed -n 's/.*big size: \([0-9][0-9]*\).*/\1/p' | head -1)
    HEADS=$(echo "$PART_INFO"    | sed -n 's/.*-h \([0-9][0-9]*\).*/\1/p' | head -1)
    SECTORS=$(echo "$PART_INFO"  | sed -n 's/.*-s \([0-9][0-9]*\).*/\1/p' | head -1)
    BIG_SIZE="${BIG_SIZE:-2097089}"
    HEADS="${HEADS:-255}"
    SECTORS="${SECTORS:-63}"

    EXTRACT_DIR=$(mktemp -d /tmp/sdcard-fix-XXXXXX)
    echo ""
    echo "Extracting filesystem tree to $EXTRACT_DIR ..."
    mcopy -i "$DRIVE_SPEC" -s -m -p -Q "::" "$EXTRACT_DIR/" 2>&1

    EXTRACTED_FILES=$(find "$EXTRACT_DIR" -type f | wc -l)
    EXTRACTED_BYTES=$(du -sb "$EXTRACT_DIR" | awk '{print $1}')
    echo "Extracted ${EXTRACTED_FILES} files, ${EXTRACTED_BYTES} bytes."

    # -c 16 = 16 sectors/cluster = 8 KB clusters. -F = force FAT32.
    # -T preserves total sectors; -h, -s preserve CHS geometry.
    echo ""
    echo "Reformatting partition: -c 16 -F -T ${BIG_SIZE} -h ${HEADS} -s ${SECTORS} ..."
    mformat -c 16 -F -T "$BIG_SIZE" -h "$HEADS" -s "$SECTORS" \
            -i "$DRIVE_SPEC" ::

    NEW_INFO=$(minfo -i "$DRIVE_SPEC" :: 2>/dev/null)
    NEW_CSIZE=$(echo "$NEW_INFO" | sed -n 's/.*-c \([0-9][0-9]*\).*/\1/p' | head -1)
    NEW_FREE=$(echo "$NEW_INFO" | sed -n 's/.*free clusters=\([0-9][0-9]*\).*/\1/p' | head -1)
    NEW_KB=$(( NEW_CSIZE * 512 / 1024 ))
    echo "  → new cluster size: ${NEW_KB} KB (${NEW_CSIZE} sectors/cluster)"
    echo "  → free clusters: ${NEW_FREE} (FAT32 requires ≥ 65 525)"

    if [ -n "$NEW_FREE" ] && [ "$NEW_FREE" -lt 65525 ]; then
        echo "ERROR: cluster count ${NEW_FREE} still below FAT32 minimum 65525" >&2
        exit 1
    fi

    echo ""
    echo "Copying tree back to image ..."
    (
        cd "$EXTRACT_DIR"
        shopt -s dotglob nullglob
        ENTRIES=( * )
        if [ ${#ENTRIES[@]} -eq 0 ]; then
            echo "  (extract dir is empty — nothing to copy back)"
        else
            mcopy -i "$DRIVE_SPEC" -s -m -p -Q -o "${ENTRIES[@]}" "::/"
        fi
    )

    NEW_FILE_COUNT=$(mdir -i "$DRIVE_SPEC" -s -b "::/" 2>&1 | wc -l)
    echo "  → image now contains ${NEW_FILE_COUNT} entries"
fi

# --- Inject default config.ini ---
echo ""
CONFIGFILE=$(mktemp /tmp/config.ini.XXXXXX)
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

echo "Injecting config.ini → /MACHINES/NEXT/config.ini ..."
# `mmd -Ds` = "skip on clash". Without it, mtools 4.0+ prompts on
# stdin when the target directory already exists, hanging the script.
# mmd returns 1 when it skips, hence the `|| true`.
echo "  ensuring /machines/ exists ..."
mmd -Ds -i "$DRIVE_SPEC" "::/machines"      </dev/null 2>/dev/null || true
echo "  ensuring /machines/next/ exists ..."
mmd -Ds -i "$DRIVE_SPEC" "::/machines/next" </dev/null 2>/dev/null || true
echo "  writing config.ini ..."
mcopy -i "$DRIVE_SPEC" -o "$CONFIGFILE" "::/machines/next/config.ini" </dev/null

echo ""
echo "Verifying ..."
mdir -i "$DRIVE_SPEC" "::/machines/next/config.ini" </dev/null

# --- Double the 24.11 distribution's 16 KB zx81.rom (GH #284) ---
# Only the exact file, by FULL SHA-256: this literal is the same as
# sdcard::kZx81Rom16kSha256 in src/core/sdcard_provisioner.cpp, and the
# fix-sdcard-image-func regression row fails if the two ever differ. The new
# file is built from the image's own bytes, so no ROM content lives here.
# Idempotent: a re-run finds 32 KB, which is not the known file.
ZX81_16K_SHA256=c294e4b60a0eba85c02a3d8b37f77de6c4516d873682dcf333813bd392d82aac
echo ""
ZX81DIR=$(mktemp -d /tmp/zx81-rom.XXXXXX)
if mcopy -n -i "$DRIVE_SPEC" "::/machines/next/zx81.rom" "$ZX81DIR/zx81.rom" \
        </dev/null 2>/dev/null \
   && [ "$(wc -c < "$ZX81DIR/zx81.rom")" -eq 16384 ] \
   && [ "$(LC_ALL=C sha256sum "$ZX81DIR/zx81.rom" | cut -d' ' -f1)" = "$ZX81_16K_SHA256" ]; then
    echo "Doubling /MACHINES/NEXT/zx81.rom to 32 KB (the 24.11 16 KB file; GH #284) ..."
    cat "$ZX81DIR/zx81.rom" "$ZX81DIR/zx81.rom" > "$ZX81DIR/zx81-32k.rom"
    mcopy -i "$DRIVE_SPEC" -o "$ZX81DIR/zx81-32k.rom" "::/machines/next/zx81.rom" </dev/null
    mdir -i "$DRIVE_SPEC" "::/machines/next/zx81.rom" </dev/null
else
    echo "/MACHINES/NEXT/zx81.rom is absent or not the 24.11 16 KB file; left as it is."
fi

# --- If we used a temp file for in-place reformat, swap it onto SRC ---
if [ -n "$WORK_TMP" ]; then
    echo ""
    echo "Replacing $SRC with fixed version ..."
    # Preserve the original file's mode (mktemp creates 0600; we want
    # to match whatever the user had on the source, typically 0644).
    SRC_MODE=$(stat -c "%a" "$SRC" 2>/dev/null || echo "")
    [ -n "$SRC_MODE" ] && chmod "$SRC_MODE" "$WORK_TMP"
    mv -f "$WORK_TMP" "$SRC"
    WORK_TMP=""   # disarm cleanup trap; the file is now $SRC
fi

echo ""
echo "Done. Fixed image: $DST"
