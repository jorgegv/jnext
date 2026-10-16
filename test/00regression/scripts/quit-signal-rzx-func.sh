#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# quit-signal-rzx-func: a Qt run whose --rzx-record write fails (/dev/full) must
# still end on SIGTERM without waiting on closeEvent's modal "RZX write failed"
# dialog: the signal path finishes the recording first (GH #364, plan D1).
# Status 143, "SIGTERM received: quitting" and "RZX: failed to write" in the
# log. Its own row, Linux only: the macOS stand-in for /dev/full (full-disk.sh)
# puts a wrapper process between the row and jnext, so the signal would not
# reach jnext's PID.
if want quit-signal-rzx-func; then
    begin_func quit-signal-rzx-func

    qs_dir="$TMP_DIR/quit-signal-rzx"
    rm -rf "$qs_dir"; mkdir -p "$qs_dir"
    qs_sdl="$JNEXT_SDL"
    qs_faults=()
    qs_rc=0
    source "$(dirname "${BASH_SOURCE[0]}")/quit-signal-functions.inc"

    qs_case qt qt-rzx TERM --rzx-record /dev/full
    grep -qF "RZX: failed to write" "$qs_dir/qt-rzx.log" \
        || qs_faults+=("qt-rzx: log has no 'RZX: failed to write'")

    if [[ ${#qs_faults[@]} -gt 0 ]]; then
        fail_row " ($(IFS=';'; echo "${qs_faults[*]}"))"
    else
        pass_row " (Qt with a failing --rzx-record exits on SIGTERM by the signal, 143, without waiting on a dialog)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
