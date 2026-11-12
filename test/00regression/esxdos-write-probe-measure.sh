#!/usr/bin/env bash
# Measures what real NextZXOS answers to the esxDOS write calls: runs the
# esxdos-write-probe.py NEX (both modes) under NextZXOS booted in jnext, on a
# PRIVATE scratch copy of the SD master, and prints the raw transcripts.
# Run with bash (never sourced: it sets its own options).
#
#   esxdos-write-probe-measure.sh JNEXT WORKDIR [MASTER_IMAGE]
#
# Writes WORKDIR/raw-rw.txt and WORKDIR/raw-ro.txt (S.../END lines only) and
# WORKDIR/sha256-master.txt. The scratch card is WORKDIR/card/sdcard/*.img: it is
# a copy of a copyrighted image and must NEVER be committed, attached or kept;
# this script deletes it when it finishes. WORKDIR must be outside the repo.
set -euo pipefail

JNEXT=$1
WORK=$2
MASTER=${3:-$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RTC="2026-07-10T08:55:00"
FRAMES=${PROBE_FRAMES:-6000}

mkdir -p "$WORK/card/sdcard"
trap 'rm -rf "$WORK/card"' EXIT
sha256sum "$MASTER" | cut -d' ' -f1 > "$WORK/sha256-master.txt"
cp --reflink=auto "$MASTER" "$WORK/card/sdcard/cspect-next-1gb-fixed.img"

python3 -I "$HERE/esxdos-write-probe.py" nex --mode rw "$WORK/W457RW.NEX"
python3 -I "$HERE/esxdos-write-probe.py" nex --mode ro "$WORK/W457RO.NEX"
sha256sum "$WORK/W457RW.NEX" "$WORK/W457RO.NEX" | sed 's|  .*/| |' > "$WORK/sha256-nex.txt"

for m in RW RO; do
    JNEXT_CONFIG_DIR="$WORK/card" LANG=C timeout --foreground --kill-after=5s 120s \
        "$JNEXT" --sdcard-file-add "$WORK/W457$m.NEX" --sdcard-file-dest "/W457$m.NEX" \
        > "$WORK/add-$m.log" 2>&1
done

# DOWN+ENTER leave the NextZXOS tour for the command line (as sdcard-swap-func),
# then `.nexload w457rw.nex` is typed.
run() {       # run <rw|ro> <file> [extra jnext args]
    local mode=$1 nex=$2; shift 2
    local keys=(--delayed-keypress-frames 400 space --delayed-keypress-frames 470 down
                --delayed-keypress-frames 500 enter)
    local f=560 i c cmd=".nexload $nex"
    for ((i = 0; i < ${#cmd}; i++)); do
        c=${cmd:i:1}; [[ $c == " " ]] && c=space
        keys+=(--delayed-keypress-frames "$f" "$c"); f=$((f + 15))
    done
    keys+=(--delayed-keypress-frames "$f" enter)
    JNEXT_CONFIG_DIR="$WORK/card" LANG=C timeout --foreground --kill-after=5s 900s \
        "$JNEXT" --headless --machine next --rtc "$RTC" \
        --magic-port 0xCAFE --magic-port-mode line "${keys[@]}" "$@" \
        --delayed-automatic-exit-frames "$FRAMES" > "$WORK/run-$mode.log" 2>&1 || true
    grep -E '^(S[0-9A-F]{4} |END)' "$WORK/run-$mode.log" > "$WORK/raw-$mode.txt" || true
}
run rw w457rw.nex
run ro w457ro.nex --sdcard-readonly

# Assemble the recording: a header (what was measured, on which image, by which
# jnext), then the annotated [rw] and [ro] transcripts.
{
    echo "# NextZXOS answers to the esxDOS write calls, recorded by esxdos-write-probe-measure.sh"
    echo "# oracle: real NextZXOS booted in jnext, probe NEX run with .nexload; nothing here is computed"
    echo "# image: $(basename "$MASTER") sha256 $(cat "$WORK/sha256-master.txt")"
    echo "# M_DOSVERSION: $(grep -m1 '^S0000 ' "$WORK/raw-rw.txt") (B,C='N','X'; DE=0x0209 is NextZXOS 2.09)"
    sed 's|^|# probe NEX sha256 |' "$WORK/sha256-nex.txt"
    echo "# jnext $("$JNEXT" --version 2>/dev/null | tail -n 1 | sed 's/^jnext //'), RTC $RTC, measured $(date +%F)"
    echo "# line: S<step> F<carry> A<hex or **> [extra] ; <step description>. A** = A masked on success (it is whatever the"
    echo "# register held; NextZXOS leaves junk there), or a handle number"
    for m in rw ro; do
        echo "[$m]"
        python3 -I "$HERE/esxdos-write-probe.py" annotate --mode "$m" "$WORK/raw-$m.txt"
    done
} > "$WORK/recording.txt"
