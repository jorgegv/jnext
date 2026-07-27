#!/usr/bin/env bash
# G46(b) #102 repro (SPARSE variant): same browser navigation as
# g46b_102_repro.sh, but only the two keypresses from the original issue
# script (space@7200 PLAY, space@9700 weapon-screen) instead of the dense
# space-mashing blanket. Used to test whether the dense blanket in the main
# script is itself inducing the freeze (e.g. a pause-key artifact) rather
# than reproducing an organic bug.
# Usage: g46b_102_repro_sparse.sh <sdcard.img> <out-png> <screenshot-frame> <exit-frame> [extra jnext args...]
set -euo pipefail
BIN="${JNEXT_BIN:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)/build/gui-release/jnext}"
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

kp 7200 space
kp 9700 space

args+=(--delayed-screenshot "$OUT" --delayed-screenshot-frames "$SFRAME")
args+=("$@")
args+=(--delayed-automatic-exit-frames "$EFRAME")

"$BIN" "${args[@]}"
