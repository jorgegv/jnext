#!/usr/bin/env bash
# The dispatch of `make test-affected` (GH #547): runs what the plan file written by
# test/test-affected.pl names, and exits non-zero if any step failed (every step still runs).
#
#   mode=full    make unit-test, then the make targets the plan names (a rule's `make:` beside `full`)
#   mode=docs    make docs-check
#   mode=subset  docs-check, cli-check, run-unit-tests.sh --only <suites>, the smoke boot row,
#                the make targets and the regression rows the plan names
#
# Usage: test/test-affected-run.sh <plan file>
# Env:   TA_MAKE (default make), TA_UNIT (default "bash test/run-unit-tests.sh"),
#        TA_REG (default "bash test/00regression/regression.sh"), TA_JNEXT (the jnext for the
#        smoke row), SMOKE_ROW (the smoke row). The test seams of test/harness-selftest.sh.
set -u
plan=${1:?usage: test-affected-run.sh <plan file>}
[[ -f "$plan" ]] || { echo "test-affected-run: no plan file $plan" >&2; exit 2; }
get() { sed -n "s/^$1=//p" "$plan"; }
mode=$(get mode); suites=$(get suites); rows=$(get rows); makes=$(get makes)
M=${TA_MAKE:-make}; UNIT=${TA_UNIT:-bash test/run-unit-tests.sh}; REG=${TA_REG:-bash test/00regression/regression.sh}
rc=0
if [ "$mode" = full ]; then
    printf 'test-affected: FULL, running make unit-test\n'
    $M --no-print-directory unit-test || rc=1
    for m in $makes; do $M --no-print-directory "$m" || rc=1; done
else
    $M --no-print-directory docs-check || rc=1
    if [ "$mode" = subset ]; then
        $M --no-print-directory cli-check || rc=1
        $UNIT --only "$suites" build || rc=1
        JNEXT=${TA_JNEXT:-build/jnext} $REG "${SMOKE_ROW:-boot-48k}" || rc=1
        for m in $makes; do $M --no-print-directory "$m" || rc=1; done
        if [ -n "$rows" ]; then $M --no-print-directory regression-rows ROWS="$rows" || rc=1; fi
    fi
fi
printf 'test-affected: NOT A GATE. Run the platform suites named above, and the full make unit-test once before landing.\n'
exit $rc
