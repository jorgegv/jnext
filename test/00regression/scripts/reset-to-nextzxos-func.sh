#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Reset after boot must re-boot to NextZXOS, not fall to 48K BASIC. The Reset
# button (and F1 / a program's NR 0x02 hard reset) is a power-on cold boot the
# host performs by reconstructing the emulator and re-running init() (the
# proven startup path). Boot, hard-reset via the headless reset facility once
# the welcome is up, and assert the re-booted screen is PIXEL-IDENTICAL to the
# fresh boot-nextzxos-welcome reference (cold boot == startup).
if want reset-to-nextzxos-func; then
    begin_func reset-to-nextzxos-func
    rst_png="$TMP_DIR/jnext_test_reset_nextzxos.png"
    rm -f "$rst_png"
    JNEXT_DELAYED_RESET_FRAMES=450 JNEXT_DELAYED_RESET_TYPE=hard \
    timeout --foreground --kill-after=5s 150s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --delayed-screenshot "$rst_png" --delayed-screenshot-frames 900 \
        --delayed-automatic-exit-frames 920 >/dev/null 2>&1 || true
    rst_diff=999999
    if [[ -f "$rst_png" ]]; then
        rst_diff=$(png_diff "$rst_png" "$WELCOME_REF")
    fi
    if [[ "$rst_diff" -le "$TOLERANCE" ]]; then
        pass_row " (reset after boot re-booted to NextZXOS: ${rst_diff} px diff vs welcome)"
    else
        fail_row " (px_diff=$rst_diff — reset did not re-boot to NextZXOS? [Task 70])"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
