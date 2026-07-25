#!/usr/bin/env bash
# G46(b) #102 repro variant: same navigation, but the space-mashing blanket
# uses a caller-supplied START/STRIDE/END instead of the fixed 2000..14000
# step 100 in g46b_102_repro.sh. Used to test whether the freeze is tied to
# a specific ABSOLUTE frame number (script artifact) or a specific GAME
# STATE reachable via different keypress timings (genuine bug).
# Usage: g46b_102_repro_stride.sh <sdcard.img> <out-png> <sframe> <eframe> <spam_start> <spam_end> <spam_stride> [extra jnext args...]
set -euo pipefail
BIN=/home/jorgegv/src/spectrum/jnext-worktrees/fix-102/build/gui-release/jnext
SD="$1"; OUT="$2"; SFRAME="$3"; EFRAME="$4"; SPSTART="$5"; SPEND="$6"; SPSTRIDE="$7"; shift 7

args=(--headless --machine next --sdcard "$SD" --sdcard-readonly --rtc "2026-01-01 12:00:00")

kp() { args+=(--delayed-keypress-frames "$1" "$2"); }

kp 450 space
kp 700 enter

f=750
for i in 1 2 3 4 5 6; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

for i in 1 2 3 4; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

for i in $(seq 1 15); do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

for i in 1 2 3 4 5; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

f="$SPSTART"
while [ "$f" -le "$SPEND" ]; do
    kp $f space
    f=$((f+SPSTRIDE))
done

args+=(--delayed-screenshot "$OUT" --delayed-screenshot-frames "$SFRAME")
args+=("$@")
args+=(--delayed-automatic-exit-frames "$EFRAME")

"$BIN" "${args[@]}"
