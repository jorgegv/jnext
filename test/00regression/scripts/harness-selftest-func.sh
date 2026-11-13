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
    # testing — circular. 900 s, a hang bound: MEASURED, the self-test takes
    # 281 s run directly at load 4, 324 s, 443 s at load 10 and 607 s at
    # moderate load (other agents running); as a row 353 s, 455 s and 457 s
    # solo (load 17 -> 6). The old 480 s bound failed it at 484 s (load 24-28),
    # 488 s and 567 s (load 16-29). 900 s leaves headroom over the worst pass
    # and stays under the driver's per-row bound for THIS row, the `bound=1200` tag
    # in functional_tests.conf (the default ROW_BOUND is 600 s), which must
    # remain the larger of the two. The row runs in the
    # quiet phase (functional_tests.conf).
    if hs_out=$(timeout --foreground --kill-after=5s 900s bash "$PROJECT_DIR/test/harness-selftest.sh" 2>&1); then
        hs_line=$(echo "$hs_out" | grep -E '^Total:' | tail -1)
        pass_row " ($hs_line)"
    else
        fail_row " (the test harness itself is broken — see below)"
        grep -E '^\s*FAIL' <<<"$hs_out" | head -5 | sed -E 's/^/      /' || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
