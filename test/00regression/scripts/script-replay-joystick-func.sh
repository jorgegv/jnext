#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #20 / GH #26 WP6 — the DAPR Kempston joystick test (test07joystick),
# parked as interactive because it needs a joystick, replayed from a RECORDING:
# a Qt GUI session with --joy1-source keys (the arrows and Space are joystick
# 1), driven by xdotool and recorded with --record-script
# (test/scripts/dsl/record-dapr.sh). Diagonals, fire and a direction held
# across another's release; the replay sets the same 12-bit connector state
# (`joystick 1 0x…`) at the same frames, and its four PNG captures must be
# pixel-identical to the GUI session's.
if want script-replay-joystick-func; then
    begin_func script-replay-joystick-func
    dir="$TMP_DIR/replay-joystick"
    mkdir -p "$dir"
    out=$(cd "$dir" && LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/test07joystick.nex" \
        --script "$PROJECT_DIR/test/scripts/dsl/dapr-joystick.jds" \
        --delayed-automatic-exit-frames 1100 2>&1) && rc=0 || rc=$?
    n=0; bad=""
    for ref in "$PROJECT_DIR"/test/scripts/dsl/dapr-joystick-0*.png; do
        n=$((n + 1))
        got="$dir/$(basename "$ref" .png)-replay.png"
        if [[ ! -s "$got" ]]; then
            bad+=" $(basename "$got") missing"
        else
            d=$(png_diff "$ref" "$got")
            [[ "$d" -eq 0 ]] || bad+=" $(basename "$ref"): $d px"
        fi
    done
    if [[ $rc -eq 0 && $n -eq 4 && -z "$bad" ]]; then
        pass_row " (exit 0; 4 replayed captures pixel-identical to the GUI recording's)"
    else
        fail_row " (exit $rc, $n refs:$bad; $(grep -E 'SCRIPT|ASSERT' <<<"$out" | tail -n 2 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
