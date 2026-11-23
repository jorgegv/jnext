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

# GH #548 (--debugger): a NEX starts paused at its entry point, read from its header
# (bytes 14-15) by the row, not from jnext: the machine has not run an instruction of the
# program. Same client and held-PC check as debugger-headless-nofile-func.
if want debugger-headless-nex-func; then
    pc=$(command -v python3 &>/dev/null && dbgstart_pc_of nex "$DBGSTART_NEX" || echo 0x8000)
    dbgstart_dzrp_row debugger-headless-nex-func "$pc" \
        "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$DBGSTART_NEX"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
