#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# frame-pacing-func: a 60 Hz demo must pace the frontend timer to the emulated
# video refresh, not a hardcoded 50 Hz. Beast.nex switches the machine to 60 Hz
# (NR 0x05 bit 2); the Qt frontend logs the 50->60 Hz pacing transition and
# drives its frame timer at the exact 17.198 ms Next-60 period via the deadline
# scheduler instead of the 19.968 ms 50 Hz period. This is the ONLY suite row
# that exercises the frontend pacing path (headless is frame-counted and never
# touches it), so it is what stops a revert to a hardcoded 50 Hz period from
# sailing through green. Discriminative both ways: the 60 Hz demo MUST emit the
# "17.198 ms deadline scheduler" line, and a plain 50 Hz machine (48k) MUST NOT.
# QT_QPA_PLATFORM=offscreen runs the real GUI binary with no display; --silent
# keeps it audio-free.
if want frame-pacing-func; then
    begin_func frame-pacing-func
    beast_nex="$SCRIPT_DIR/nex/beast.nex"
    fp_60=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine next --load "$beast_nex" --silent \
        --delayed-automatic-exit-frames 150 2>&1) || true
    fp_50=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --silent \
        --delayed-automatic-exit-frames 150 2>&1) || true
    fp_60_hit=$(echo "$fp_60" | grep -cF "17.198 ms deadline scheduler" || true)
    fp_50_hit=$(echo "$fp_50" | grep -cF "17.198 ms deadline scheduler" || true)
    if [[ "$fp_60_hit" -ge 1 && "$fp_50_hit" -eq 0 ]]; then
        pass_row " (60 Hz demo paces to the 17.198 ms deadline; 50 Hz machine does not)"
    else
        fail_row " (60Hz->17.198ms line count=$fp_60_hit (want >=1), 48k count=$fp_50_hit (want 0))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
