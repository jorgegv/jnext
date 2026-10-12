#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Magic breakpoint test: verify ED FF is detected and logged
if want magic-bp-func; then
    begin_func magic-bp-func
    # 60 s is a crash guard, not a measurement. Under wine this command is a cold wine start plus the run:
    # 2.6-4.5 s solo, and it flaked at 10 s (GH #319) when it started beside ~11 other cold wine starts in the
    # parallel phase. The assertion (a 'Magic breakpoint hit' line) is unchanged.
    bp_output=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --magic-breakpoint \
        "${SD_CARD_ARGS[@]}" \
        --load "$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex" \
        --delayed-automatic-exit 3 2>&1) || true
    bp_count=$(echo "$bp_output" | grep -c "Magic breakpoint hit" || true)
    if [[ "$bp_count" -ge 1 ]]; then
        pass_row " ($bp_count magic breakpoint(s) detected)"
    else
        fail_row " (no magic breakpoint detected in output)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
