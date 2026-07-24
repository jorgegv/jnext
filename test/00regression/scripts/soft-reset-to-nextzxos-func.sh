#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Soft reset (host F4 / GUI Machine > Soft Reset, issue #45) from a booted
# NextZXOS must return to NextZXOS — no fall to 48K BASIC, no hang. The env
# type "f4" goes through Emulator::on_hotkey_f4_soft_reset(), the exact GUI/SDL
# F4 path (config-mode gate + reset_type FSM strobe + soft_reset()).
#
# The pre-reset state is deliberately the MAIN MENU (SPACE at the welcome), not
# the welcome itself: the assertion compares against the welcome reference, so
# a soft reset that silently did NOTHING leaves the menu on screen and FAILS —
# comparing welcome-to-welcome could not tell "worked" from "no-op". A hang
# leaves no screenshot (px diff 999999); a 48K BASIC fall differs from the
# reference.
if want soft-reset-to-nextzxos-func; then
    begin_func soft-reset-to-nextzxos-func
    srst_png="$TMP_DIR/jnext_test_soft_reset_nextzxos.png"
    rm -f "$srst_png"
    JNEXT_DELAYED_RESET_FRAMES=700 JNEXT_DELAYED_RESET_TYPE=f4 \
    timeout --foreground --kill-after=5s 150s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --delayed-keypress-frames 450 space \
        --delayed-screenshot "$srst_png" --delayed-screenshot-frames 1200 \
        --delayed-automatic-exit-frames 1220 >/dev/null 2>&1 || true
    srst_diff=999999
    if [[ -f "$srst_png" ]]; then
        srst_diff=$(png_diff "$srst_png" "$WELCOME_REF")
    fi
    if [[ "$srst_diff" -le "$TOLERANCE" ]]; then
        pass_row " (soft reset returned to NextZXOS: ${srst_diff} px diff vs welcome)"
    else
        fail_row " (px_diff=$srst_diff — soft reset did not return to NextZXOS? [issue #45])"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
