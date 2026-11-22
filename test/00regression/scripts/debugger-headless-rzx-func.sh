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

# GH #548 (--debugger with an RZX replay): the row RECORDS an RZX itself (a 48K .sna made by a
# headless run, loaded with --rzx-record, so the recording's embedded snapshot is the machine
# right after that load), then starts `--debugger --load x.rzx`: the stop is the PC the embedded
# snapshot restores, which is the PC the .sna holds (read from the .sna file per the SNA format).
if want debugger-headless-rzx-func; then
    row=debugger-headless-rzx-func
    begin_func "$row"
    sna="$TMP_DIR/$row.sna"
    rzx="$TMP_DIR/$row.rzx"
    if ! dbgstart_have_python "$row"; then
        :
    elif ! dbgstart_snapshot "$sna"; then
        fail_row " (the headless run that saves the .sna failed: $(tail -n 2 "$TMP_DIR/dbgstart-snapshot.log" | tr '\n' ' '))"
    elif ! LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --load "$sna" --rzx-record "$rzx" --delayed-automatic-exit-frames 60 \
            >"$TMP_DIR/$row.record.log" 2>&1 || [[ ! -s "$rzx" ]]; then
        fail_row " (the recording run failed or wrote no RZX: $(tail -n 3 "$TMP_DIR/$row.record.log" | tr '\n' ' '))"
    else
        pc=$(dbgstart_pc_of sna "$sna")
        if dbgstart_dzrp_attempt "$TMP_DIR/$row.log" "$pc" "$JNEXT" --headless --machine 48k \
                "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 --load "$rzx"; then
            pass_row " ($DBGSTART_MSG)"
        else
            fail_row " ($DBGSTART_MSG)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
