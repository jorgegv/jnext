#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# The unit-test harness is load-bearing for every number this project quotes,
# so it is itself under test (test/harness-selftest.sh injects each harness
# fault and asserts the refusal).
if want harness-selftest-func; then
    begin_func harness-selftest-func
    # Timeout-wrapped like every other invocation here. Its only other time
    # bound would be the per-suite timeout inside the very harness it is
    # testing — circular.
    if hs_out=$(timeout --foreground --kill-after=5s 120s bash "$PROJECT_DIR/test/harness-selftest.sh" 2>&1); then
        hs_line=$(echo "$hs_out" | grep -E '^Total:' | tail -1)
        pass_row " ($hs_line)"
    else
        fail_row " (the test harness itself is broken — see below)"
        echo "$hs_out" | grep -E '^\s*FAIL' | head -5 | sed -E 's/^/      /'
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
