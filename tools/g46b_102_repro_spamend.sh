#!/usr/bin/env bash
# G46(b) #102 repro variant: same as g46b_102_repro.sh but the space-mashing
# blanket stops at a configurable frame (SPAMEND) instead of always 14000.
# Used to find exactly which spam press (if any) triggers the transition
# into gameplay, and whether the freeze happens with or without further
# spam presses after that transition.
# Usage: g46b_102_repro_spamend.sh <sdcard.img> <out-png> <screenshot-frame> <exit-frame> <spamend> [extra jnext args...]
set -euo pipefail
BIN="${JNEXT_BIN:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)/build/gui-release/jnext}"
SD="$1"; OUT="$2"; SFRAME="$3"; EFRAME="$4"; SPAMEND="$5"; shift 5

args=(--headless --machine next --sdcard "$SD" --sdcard-readonly --rtc "2026-01-01 12:00:00")

kp() { args+=(--delayed-keypress-frames "$1" "$2"); }

kp 450 space
kp 700 enter

# GAMES (6 down + enter)
f=750
for i in 1 2 3 4 5 6; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

# Next (4 down + enter)
for i in 1 2 3 4; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

# TX-1696 (15 down + enter)
for i in $(seq 1 15); do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

# MAIN.NEX (5 down + enter)
for i in 1 2 3 4 5; do kp $f down; f=$((f+20)); done
kp $f enter; f=$((f+20))

f=2000
while [ "$f" -le "$SPAMEND" ]; do
    kp $f space
    f=$((f+100))
done

args+=(--delayed-screenshot "$OUT" --delayed-screenshot-frames "$SFRAME")
args+=("$@")
args+=(--delayed-automatic-exit-frames "$EFRAME")

"$BIN" "${args[@]}"
