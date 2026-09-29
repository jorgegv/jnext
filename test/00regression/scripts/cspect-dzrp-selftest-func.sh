#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #12 (dzrp-frontend.md §7.2) — the self-test of tools/cspect_dzrp, the client
# the dzrp-*-func rows drive jnext with. A harness with a known race is not a
# harness: its REVIEW.md H1-H3 (a stale notification returned as a fresh stop,
# an unlocked wait_for_pause, an unlocked close) are fixed, and each fix has a
# test that fails with it reverted — as do the milestone-4 refinements: a wait
# that no longer blocks another thread's pause(), and a stray response that is
# an error rather than a silent drop. Nothing ran this suite before; this row
# does, and pins its size, so a test that stops being collected shows as a
# count, not as a quieter green.
SELFTEST_COUNT=22
if want cspect-dzrp-selftest-func; then
    begin_func cspect-dzrp-selftest-func
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available)"
    else
        rc=0
        out=$(cd "$PROJECT_DIR/tools/cspect_dzrp" && LANG=C timeout --foreground --kill-after=5s 120s \
            python3 -m unittest test_cspect_dzrp 2>&1) || rc=$?
        ran=$(grep -oE '^Ran [0-9]+ tests' <<<"$out" | grep -oE '[0-9]+' || true)
        if [[ $rc -eq 0 && "$ran" == "$SELFTEST_COUNT" ]] && grep -qx "OK" <<<"$out"; then
            pass_row " ($ran tests OK, H1-H3 included)"
        else
            fail_row " (exit $rc, ran '${ran}' of $SELFTEST_COUNT: $(grep -E '^(FAIL|ERROR):' <<<"$out" | tr '\n' ' ' | cut -c1-400))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
