#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"


# DAC trace: injected Z80N program writes Soundrive A-D; CSV rows verified.
if want dac-trace-func; then
    begin_func dac-trace-func
    dac_bin="$TMP_DIR/dac_trace.bin"
    dac_csv="$TMP_DIR/dac_trace.csv"
    printf '\xF3\xED\x91\x08\x08\x3E\x11\xD3\x1F\x3E\x22\xD3\x0F\x3E\x33\xD3\x4F\x3E\x44\xD3\x5F\x18\xFE' > "$dac_bin"
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --inject "$dac_bin" --inject-org 8000 \
        --inject-pc 8000 --dac-trace "$dac_csv" \
        --delayed-automatic-exit-frames 30 &>/dev/null &&
       [[ $(head -n 1 "$dac_csv" 2>/dev/null) == "segment,tstate,channel,value" ]] &&
       grep -qE '^[0-9]+,[0-9]+,0,17$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,1,34$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,2,51$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,3,68$' "$dac_csv"; then
        pass_row " (physical DAC channels A-D traced through CLI)"
    else
        fail_row " (missing or incorrect DAC CSV rows)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
