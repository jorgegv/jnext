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

# GH #548 (--debugger on the Qt loop owner): starts paused at the NEX entry; the debugger
# window opens through the manager's own check for a pause it did not make (the window itself
# is PBPUI-15's). A DZRP client reads the PC, held, and CONTINUE runs it.
# QT_QPA_PLATFORM=offscreen, as dzrp-qt-func.
if want debugger-qt-func; then
    begin_func debugger-qt-func
    log="$TMP_DIR/debugger-qt-func.log"
    if ! dbgstart_have_python debugger-qt-func; then
        :
    elif ! dzrp_launch_cmd "$log" env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --rewind-buffer-size 0 --debugger --dzrp-port 0 \
            --load "$DBGSTART_NEX"; then
        dzrp_stop
        fail_row " (the Qt frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    elif dzrp_peer startup "$(dbgstart_pc_of nex "$DBGSTART_NEX")"; then
        dzrp_stop
        pass_row " (QtApp: ${DZRP_OUT#PASS })"
    else
        dzrp_stop
        fail_row " (QtApp: ${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|'))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
