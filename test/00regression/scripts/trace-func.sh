#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --trace CLI flag. The instruction trace log is decoupled from
# rewind: --trace must enable it (the "Instruction trace log enabled (--trace)"
# info line is the observable), and — the discriminating half — a default run
# with no trace/rewind flags must NOT emit that line (trace off by default).
if want trace-func; then
    begin_func trace-func
    tr_line="Instruction trace log enabled (--trace)"
    tr_on=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --trace --delayed-automatic-exit-frames 20 2>&1) || true
    tr_off=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 20 2>&1) || true
    tr_on_count=$(echo "$tr_on" | grep -cF "$tr_line" || true)
    tr_off_count=$(echo "$tr_off" | grep -cF "$tr_line" || true)
    if [[ "$tr_on_count" -ge 1 && "$tr_off_count" -eq 0 ]]; then
        pass_row " (--trace enables the trace log; default run leaves it off)"
    else
        fail_row " (enable-line count: with --trace=$tr_on_count (want >=1), default=$tr_off_count (want 0))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
