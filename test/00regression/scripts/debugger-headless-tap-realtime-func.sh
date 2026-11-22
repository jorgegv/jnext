#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 stage 2: the same tape in real time (--tape-realtime): the stop is the same 0x8000.
if want debugger-headless-tap-realtime-func; then
    dbgstart_dzrp_row debugger-headless-tap-realtime-func 0x8000 \
        "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --tape-realtime --load "$DBGSTART_TAP"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
