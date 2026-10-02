#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #20 / GH #26 WP6 — the DAPR keyboard test (test06keyb), parked as
# interactive because it needs typing, replayed from a RECORDING: a Qt GUI
# session driven by xdotool and recorded with --record-script
# (test/scripts/dsl/record-dapr.sh). The script holds W across CAPS SHIFT,
# presses EDIT (CAPS + 1) and holds E with R; its five captures are PNGs (the
# program draws on the tilemap, so the ULA screen alone says nothing). The
# replay, headless, must reach the script's `exit 0` and write five PNGs
# pixel-identical to the ones the GUI session captured.
if want script-replay-keyb-func; then
    begin_func script-replay-keyb-func
    dir="$TMP_DIR/replay-keyb"
    mkdir -p "$dir"
    out=$(cd "$dir" && LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/test06keyb.nex" \
        --script "$PROJECT_DIR/test/scripts/dsl/dapr-keyb.jds" \
        --delayed-automatic-exit-frames 1100 2>&1) && rc=0 || rc=$?
    n=0; bad=""
    for ref in "$PROJECT_DIR"/test/scripts/dsl/dapr-keyb-0*.png; do
        n=$((n + 1))
        got="$dir/$(basename "$ref" .png)-replay.png"
        if [[ ! -s "$got" ]]; then
            bad+=" $(basename "$got") missing"
        else
            d=$(png_diff "$ref" "$got")
            [[ "$d" -eq 0 ]] || bad+=" $(basename "$ref"): $d px"
        fi
    done
    if [[ $rc -eq 0 && $n -eq 5 && -z "$bad" ]]; then
        pass_row " (exit 0; 5 replayed captures pixel-identical to the GUI recording's)"
    else
        fail_row " (exit $rc, $n refs:$bad; $(grep -E 'SCRIPT|ASSERT' <<<"$out" | tail -n 2 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
