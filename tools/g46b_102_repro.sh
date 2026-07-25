#!/usr/bin/env bash
# G46(b) #102 repro: TX-1696 browser launch -> PLAY -> weapon screen -> freeze check.
# Usage: g46b_102_repro.sh <sdcard.img> <out-png> <screenshot-frame> <exit-frame> [extra jnext args...]
set -euo pipefail
BIN=/home/jorgegv/src/spectrum/jnext-worktrees/fix-102/build/gui-release/jnext
SD="$1"; OUT="$2"; SFRAME="$3"; EFRAME="$4"; shift 4

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

# Blanket space presses to reliably clear the loading splash / weapon-select
# confirm regardless of small navigation-timing drift versus the original
# repro (this script's own down/enter cadence differs from the session that
# authored the issue). Purely a test-driver robustness measure — real SPACE
# keypresses, nothing injected into game state.
f=2000
while [ "$f" -le 14000 ]; do
    kp $f space
    f=$((f+100))
done

args+=(--delayed-screenshot "$OUT" --delayed-screenshot-frames "$SFRAME")
args+=("$@")
args+=(--delayed-automatic-exit-frames "$EFRAME")

"$BIN" "${args[@]}"
