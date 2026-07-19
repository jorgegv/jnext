#!/usr/bin/env bash
#
# extract-sdcard-nex.sh — Build a test tree of every NEX file on an SD image.
#
# Finds all *.nex on the SD card image and copies them into an output
# directory, recreating just enough of the card's directory hierarchy to keep
# each NEX at its original path. Also emits a batched markdown checklist (10
# files per batch) for the tracking issue.
#
# Some Next programs load companion files (levels, music, graphics) from their
# own directory, so a NEX lifted out of its folder can fail for reasons that
# have nothing to do with the emulator. Keeping the paths keeps the audit
# honest — and `--with-siblings` copies each NEX's whole directory when a
# program turns out to need its data files.
#
# Requires: mtools (mdir, mcopy).
#
# Usage:
#   ./tools/extract-sdcard-nex.sh <output-dir> [options]
#
# Options:
#   -i, --image FILE      SD card image (default: $DEFAULT_IMAGE)
#   -b, --batch-size N    Files per batch in the checklist (default: 10)
#   -m, --markdown FILE   Write the checklist here (default: <output-dir>/NEX-AUDIT.md)
#   -s, --with-siblings   Copy each NEX's entire directory, not just the .nex
#   -h, --help            This text

set -euo pipefail

DEFAULT_IMAGE="$HOME/.jnext/sdcard/cspect-next-1gb.img"

# MBR partition 1 starts at LBA 63 on every ZX Next card image we ship.
PART_OFFSET=32256

OUT_DIR=""
IMAGE="$DEFAULT_IMAGE"
BATCH_SIZE=10
MARKDOWN=""
WITH_SIBLINGS=0

usage() { sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'; }

die() { echo "ERROR: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -i|--image)        IMAGE="${2:-}";      shift 2 ;;
        -b|--batch-size)   BATCH_SIZE="${2:-}"; shift 2 ;;
        -m|--markdown)     MARKDOWN="${2:-}";   shift 2 ;;
        -s|--with-siblings) WITH_SIBLINGS=1;    shift ;;
        -h|--help)         usage; exit 0 ;;
        -*)                die "unknown option '$1' (try --help)" ;;
        *)
            [[ -n "$OUT_DIR" ]] && die "output directory given twice ('$OUT_DIR', '$1')"
            OUT_DIR="$1"; shift ;;
    esac
done

[[ -n "$OUT_DIR" ]] || { usage; exit 1; }
[[ -f "$IMAGE" ]]   || die "SD image not found: $IMAGE"
[[ "$BATCH_SIZE" =~ ^[1-9][0-9]*$ ]] || die "batch size must be a positive integer, got '$BATCH_SIZE'"

for tool in mdir mcopy; do
    command -v "$tool" >/dev/null 2>&1 || die "'$tool' (from mtools) is required"
done

# mtools wants an absolute path in the drive spec.
abspath() { case "$1" in /*) printf '%s\n' "$1" ;; *) printf '%s\n' "$PWD/$1" ;; esac; }
DRIVE="$(abspath "$IMAGE")@@$PART_OFFSET"

[[ -n "$MARKDOWN" ]] || MARKDOWN="$OUT_DIR/NEX-AUDIT.md"

mkdir -p "$OUT_DIR"

# Recursive bare listing -> "::/path/to/file.nex". Case-insensitive match, as
# the card mixes NEXTEST.NEX with nextoid.nex.
mapfile -t NEX_FILES < <(mdir -i "$DRIVE" -s -b "::/" 2>/dev/null | grep -i '\.nex$' | sort -f)

(( ${#NEX_FILES[@]} > 0 )) || die "no NEX files found in $IMAGE"

echo "Image:  $IMAGE"
echo "Found:  ${#NEX_FILES[@]} NEX file(s)"
echo "Output: $OUT_DIR"
echo

copied=0
for src in "${NEX_FILES[@]}"; do
    rel="${src#::/}"                 # strip the mtools drive prefix
    dest_dir="$OUT_DIR/$(dirname "$rel")"
    mkdir -p "$dest_dir"
    if (( WITH_SIBLINGS )); then
        # -s recurses, so point it at the NEX's directory and take everything.
        mcopy -i "$DRIVE" -s -m -p -Q -n "::/$(dirname "$rel")" "$dest_dir/.." </dev/null 2>/dev/null
    else
        mcopy -i "$DRIVE" -m -p -Q -n "::/$rel" "$dest_dir/" </dev/null 2>/dev/null
    fi
    copied=$(( copied + 1 ))
    printf '  [%2d/%2d] %s\n' "$copied" "${#NEX_FILES[@]}" "$rel"
done

# --- checklist -------------------------------------------------------------
#
# Batches exist so testing can be handed out in fixed-size chunks; the boxes
# are the issue's completion criteria.
{
    echo "## NEX audit — $(basename "$IMAGE")"
    echo
    echo "${#NEX_FILES[@]} NEX files, $BATCH_SIZE per batch."
    echo "Regenerate this tree with \`tools/extract-sdcard-nex.sh <dir>\`."
    echo
    echo "Tick a box when the program has been run in jnext and behaves"
    echo "correctly. If it fails, tick nothing and link the bug report."
    echo

    idx=0
    batch=0
    for src in "${NEX_FILES[@]}"; do
        if (( idx % BATCH_SIZE == 0 )); then
            batch=$(( batch + 1 ))
            (( batch > 1 )) && echo
            echo "### Batch $batch"
            echo
        fi
        echo "- [ ] \`${src#::/}\`"
        idx=$(( idx + 1 ))
    done
} > "$MARKDOWN"

echo
echo "Checklist: $MARKDOWN  ($(( (${#NEX_FILES[@]} + BATCH_SIZE - 1) / BATCH_SIZE )) batch(es))"
echo
echo "Run one with:"
echo "  ./build/jnext --sdcard <image> --load '$OUT_DIR/<path>.nex'"
