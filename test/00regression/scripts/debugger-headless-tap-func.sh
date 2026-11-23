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

# GH #548 stage 2 (--debugger on a tape): the z88dk fixture tape (BASIC loader, then CODE at 32768)
# loads in fast mode and the machine stops at the first instruction of the code the tape loaded,
# after its last block: 0x8000, the CODE header's start (read from the file by the row). The client
# attaches once the stop is logged.
if want debugger-headless-tap-func; then
    dbgstart_dzrp_row debugger-headless-tap-func 0x8000 \
        "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0  --load "$DBGSTART_TAP"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
