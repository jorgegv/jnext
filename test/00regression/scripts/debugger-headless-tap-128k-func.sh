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

# GH #548 stage 2 (D2): on a 128K the editor ROM reaches BASIC through paging routines it copied to
# RAM at 0x5B00, which run between every statement: the stop is the program (0x8000), never one of
# those stubs, because the tape did not load them.
if want debugger-headless-tap-128k-func; then
    dbgstart_dzrp_row debugger-headless-tap-128k-func 0x8000 \
        "$JNEXT" --headless --machine 128k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0  --load "$DBGSTART_TAP"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
