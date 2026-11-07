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
    # testing — circular. 480 s, a hang bound: MEASURED, the self-test takes
    # 281 s run directly at load 4 and 443 s at load 10 on the 12-CPU dev host
    # (the old figure of ~85 s is long gone), and the row failed at 488 s and
    # 567 s at load 16-29. The row runs in the quiet
    # phase (functional_tests.conf); the bound is not raised.
    if hs_out=$(timeout --foreground --kill-after=5s 480s bash "$PROJECT_DIR/test/harness-selftest.sh" 2>&1); then
        hs_line=$(echo "$hs_out" | grep -E '^Total:' | tail -1)
        pass_row " ($hs_line)"
    else
        fail_row " (the test harness itself is broken — see below)"
        grep -E '^\s*FAIL' <<<"$hs_out" | head -5 | sed -E 's/^/      /' || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
