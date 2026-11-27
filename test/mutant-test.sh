#!/usr/bin/env bash
# `make mutant-test` (GH #585): a mutation run builds and runs ONLY the suites and rows the author
# named as the mutant's killers.
#
#   make mutant-test SUITES=a,b [ROWS="row-x row-y"] [JNEXT_SUITE_VMEM_KB=3145728]
#
# Exit status, with a last line that says it:
#   0  `mutant-test: SURVIVED`   every named suite and row passed
#   1  `mutant-test: KILLED`     a named suite or row failed (FAIL row, Failed > 0, crash, no Total:)
#   2  `mutant-test: NO VERDICT: <reason>`, after the banner line `TEST HARNESS REFUSES TO RUN` alone on
#      its line (the line test/remote-run.sh reads to report PATCH REFUSED instead of a kill): empty
#      SUITES and ROWS, an empty item, an unknown or gated-out suite, an unknown row, a configure or
#      build failure (a mutant that does not compile proves nothing), a harness refusal or fault.
# Suites run first; a kill there ends the run. Names are checked before anything is built. Runs no
# docs-check, cli-check, lint or smoke set: none of them is a killer the author named.
#
# Env (the test seams of test/harness-selftest.sh): MT_MAKE (make), MT_UNIT ("bash test/run-unit-tests.sh"),
#   MT_REG ("bash test/00regression/regression.sh"), MT_CMAKE (cmake), MT_JOBS, MT_SUITES, MT_ROWS,
#   MT_REG_CONF, MT_FUNC_CONF (the two regression manifests).
set -u
M=${MT_MAKE:-make}; UNIT=${MT_UNIT:-bash test/run-unit-tests.sh}; REG=${MT_REG:-bash test/00regression/regression.sh}
CM=${MT_CMAKE:-cmake}; JOBS=${MT_JOBS:-1}
SUITES_RAW=${MT_SUITES:-}; ROWS_RAW=${MT_ROWS:-}
REG_CONF=${MT_REG_CONF:-test/00regression/regression_tests.conf}
FUNC_CONF=${MT_FUNC_CONF:-test/00regression/functional_tests.conf}

refuse() {
    printf '\n TEST HARNESS REFUSES TO RUN \n\n' >&2
    printf '  %s\n\n' "$1" >&2
    printf 'mutant-test: NO VERDICT: %s\n' "$1"
    exit 2
}

# Names are split with read -a / IFS and passed as separate argv words: never eval'd, never `bash -c`.
SUITES=(); ROWS=()
IFS=, read -r -a SUITES <<<"$SUITES_RAW"
read -r -a ROWS <<<"$ROWS_RAW"
(( ${#SUITES[@]} + ${#ROWS[@]} )) || refuse "name at least one suite (SUITES=a,b) or row (ROWS=\"r1 r2\")"
if [[ -n "$SUITES_RAW" ]]; then
    [[ "$SUITES_RAW" != ,* && "$SUITES_RAW" != *, && "$SUITES_RAW" != *,,* ]] \
        || refuse "empty item in SUITES='$SUITES_RAW'"
    for s in "${SUITES[@]}"; do
        [[ "$s" == *[![:space:]]* && "$s" != *[[:space:]]* ]] || refuse "bad suite name '$s' in SUITES='$SUITES_RAW'"
    done
fi

# Row names: the first word of a non-blank, non-# line of either manifest (regression.sh's own rule).
declare -A KNOWN_ROW=()
for conf in "$REG_CONF" "$FUNC_CONF"; do
    [[ -r "$conf" ]] || refuse "cannot read the regression manifest $conf"
    while read -r name _; do
        [[ -z $name || $name == \#* ]] && continue
        KNOWN_ROW["$name"]=1
    done <"$conf"
done
if [[ "${MT_COUNT_ROWS:-}" == 1 ]]; then printf '%d\n' "${#KNOWN_ROW[@]}"; exit 0; fi
for r in ${ROWS[@]+"${ROWS[@]}"}; do
    [[ -n "${KNOWN_ROW[$r]:-}" ]] || refuse "unknown regression row '$r' (not in $REG_CONF or $FUNC_CONF)"
done

$M --no-print-directory unit-test-configure || refuse "configuring build/ failed"

if (( ${#SUITES[@]} )); then
    list=$(IFS=,; printf '%s' "${SUITES[*]}")
    $UNIT --only "$list" --check build; rc=$?
    (( rc == 0 )) || refuse "run-unit-tests.sh --check exited $rc (a name it refuses, see above)"
    $CM --build build -j"$JOBS" --target "${SUITES[@]}" || refuse "the build failed"
    $UNIT --only "$list" build; rc=$?
    if (( rc == 2 )); then refuse "run-unit-tests.sh refused to run (see above)"; fi
    if (( rc != 0 )); then printf 'mutant-test: KILLED by the suites; rows not run\n'; exit 1; fi
fi

if (( ${#ROWS[@]} )); then
    # The rows execute an open set of helpers (jnext, sdfile_tool, rewind_test, the SDL binary, the
    # provisioned tools): build all of them rather than keep a list that goes stale into a false kill.
    # Built only now: a suite that killed the mutant has saved this build.
    $M --no-print-directory unit-test-build sdl-release z88dk-gdb skoolkit ghidra || refuse "the build failed"
    JNEXT=$PWD/build/jnext $REG "${ROWS[@]}"; rc=$?
    if (( rc == 2 )); then refuse "the regression harness faulted (exit 2)"; fi
    if (( rc != 0 )); then printf 'mutant-test: KILLED by the rows\n'; exit 1; fi
fi

printf 'mutant-test: SURVIVED\n'
exit 0
