#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Rewind / backwards execution unit tests. The binary's absence is caught in
# the preflight, so by the time we get here it is there and the only question
# is whether it passes.
if want rewind-func; then
    begin_func rewind-func
    rewind_out=$(timeout --foreground --kill-after=5s 30s "$REWIND_TEST" 2>/dev/null || true)
    rewind_summary=$(echo "$rewind_out" | grep -oP "Passed:\s+\d+(?=.*Failed:\s+0)" || true)
    if [[ -n "$rewind_summary" ]]; then
        rewind_passed=$(echo "$rewind_summary" | grep -oP "\d+")
        pass_row " (${rewind_passed}/${rewind_passed} rewind unit tests)"
    else
        fail_line=$(echo "$rewind_out" | grep -E "^Total:" || echo "unknown")
        fail_row " ($fail_line)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
