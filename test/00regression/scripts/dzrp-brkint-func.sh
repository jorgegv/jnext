#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; row 39) — CMD_ENABLE_BREAK_ON_INTERRUPT 1 and a
# CONTINUE stop at the IM 1 handler's entry (0x0038) with NTF_PAUSE 255 "Break
# on interrupt."; with 0, frames run (FRAMES advances) without a stop until the
# client's own CMD_PAUSE (reason 1).
if want dzrp-brkint-func; then
    dzrp_row dzrp-brkint-func brkint
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
