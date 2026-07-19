#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Tape SAVE trap guard. An ordinary NextZXOS boot with --tape-save armed must
# boot pixel-identically to the boot-nextzxos-welcome reference and write ZERO
# blocks (file created empty by set_output, never grown). The SA-BYTES trap is
# gated on ROM identity (48K SA-BYTES prologue bytes at 0x04C2); an ungated
# trap fires repeatedly during this exact boot, appending garbage and breaking
# the boot — this row keeps the gate in place.
if want tape-save-boot-func; then
    begin_func tape-save-boot-func
    ts_tap="$TMP_DIR/jnext_test_tapesave_boot.tap"
    ts_png="$TMP_DIR/jnext_test_tapesave_boot.png"
    rm -f "$ts_tap" "$ts_png"
    ts_out=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --tape-save "$ts_tap" \
        --delayed-screenshot "$ts_png" --delayed-screenshot-frames 400 \
        --delayed-automatic-exit-frames 420 2>&1) || true
    ts_blocks=$(echo "$ts_out" | grep -c "TAP save: block" || true)
    ts_size=$(stat -c%s "$ts_tap" 2>/dev/null || echo missing)
    ts_diff=999999
    if [[ -f "$ts_png" ]]; then
        ts_diff=$(png_diff "$ts_png" "$WELCOME_REF")
    fi
    if [[ "$ts_blocks" -eq 0 && "$ts_size" == "0" && "$ts_diff" -le "$TOLERANCE" ]]; then
        pass_row " (NextZXOS boot clean with --tape-save armed: 0 blocks, empty file, ${ts_diff} px diff)"
    else
        fail_row " (blocks=$ts_blocks file_size=$ts_size px_diff=$ts_diff — SAVE trap fired during NextZXOS boot?)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
