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

# GH #548 stage 2: the same tape as a TZX the row builds (standard 0x10 blocks and a trailing text
# block, where at_end() stays false): 0x8000.
if want debugger-headless-tzx-func; then
    begin_func debugger-headless-tzx-func
    tzx="$TMP_DIR/debugger-tape.tzx"
    if ! dbgstart_have_python debugger-headless-tzx-func; then
        :
    else
        dbgstart_tape_from_tap tzx "$tzx"
        if dbgstart_dzrp_attempt "$TMP_DIR/debugger-headless-tzx-func.log" 0x8000 "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0  --load "$tzx"; then
            pass_row " ($DBGSTART_MSG)"
        else
            fail_row " ($DBGSTART_MSG)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
