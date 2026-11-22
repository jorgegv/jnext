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

# GH #548 stage 2: the same tape as a WAV the row makes (a WAV plays in real time): 0x8000, the stop
# at the last signal edge and not after the silence that ends the recording.
if want debugger-headless-wav-func; then
    begin_func debugger-headless-wav-func
    wav="$TMP_DIR/debugger-tape.wav"
    if ! dbgstart_have_python debugger-headless-wav-func; then
        :
    else
        dbgstart_tape_from_tap wav "$wav"
        if dbgstart_dzrp_attempt "$TMP_DIR/debugger-headless-wav-func.log" 0x8000 "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0  --load "$wav"; then
            pass_row " ($DBGSTART_MSG)"
        else
            fail_row " ($DBGSTART_MSG)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
