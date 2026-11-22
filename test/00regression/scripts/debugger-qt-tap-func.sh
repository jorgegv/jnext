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

# GH #548 stage 2: the fixture tape on the Qt loop owner (QT_QPA_PLATFORM=offscreen, as dzrp-qt-func):
# 0x8000; the debugger window opens on the stop (PBPUI-15 holds the window itself).
if want debugger-qt-tap-func; then
    begin_func debugger-qt-tap-func
    log="$TMP_DIR/debugger-qt-tap-func.log"
    if ! dbgstart_have_python debugger-qt-tap-func; then
        :
    elif dbgstart_dzrp_attempt "$log" 0x8000 env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 --debugger --dzrp-port 0 \
            --load "$DBGSTART_TAP"; then
        pass_row " (QtApp: $DBGSTART_MSG)"
    else
        fail_row " (QtApp: $DBGSTART_MSG)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
