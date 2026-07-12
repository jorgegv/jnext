#!/usr/bin/env bash
# Unit-test harness for jnext. Runs exactly the suites declared in
# test/unit-tests.conf, in parallel, and aggregates their results.
#
# The point of this script is that it CANNOT QUIETLY RUN FEWER TESTS THAN IT
# CLAIMS. Every way a suite could previously vanish from the denominator is now
# a loud, non-zero-exit failure before a single test runs:
#
#   * declared here but not registered in CMake  -> stale entry, refuse to run
#   * registered in CMake but not declared here  -> Task 32's bug, refuse to run
#   * declared here but the binary is not built  -> refuse to run
#   * ran, but printed no parseable summary line -> FAIL (it used to score 0/0/0
#                                                   rows and still print PASS)
#
# and, unlike the Makefile recipe it replaces, a failing suite makes the whole
# run exit non-zero.
#
# Usage: bash test/run-unit-tests.sh [build_dir]      (default: build)

set -euo pipefail

# Pin the locale for every suite. Test assertions that grep strerror() text (or any
# other libc/Qt message) must not depend on the user's LANG: Qt's QApplication calls
# setlocale(LC_ALL, "") on construction, which localises strerror — that is exactly
# how screenshot-io-qt-func FAILed on a Spanish desktop while its headless twin passed.
# A test's verdict is about the code, never about who ran it.
export LC_ALL=C

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"   # sd_rom_extractor_test resolves roms/ relative to the root

BUILD="${1:-build}"
CONF="test/unit-tests.conf"
CTEST_FILE="$BUILD/test/CTestTestfile.cmake"
SUMMARY="$BUILD/test-summary.tsv"
LOG_DIR="$BUILD/test-logs"

RESET='\033[0m'; BOLD='\033[1m'; CYAN='\033[36m'
BADGE_PASS='\033[38;5;0m\033[48;5;42m'
BADGE_SKIP='\033[38;5;0m\033[48;5;220m'
BADGE_FAIL='\033[38;5;15m\033[48;5;161m'

die() {
    printf "\n${BADGE_FAIL} TEST HARNESS REFUSES TO RUN ${RESET}\n\n" >&2
    printf "  %b\n" "$@" >&2
    printf "\n" >&2
    exit 2
}

[[ -f "$CONF" ]]       || die "Missing test manifest: $CONF"
[[ -f "$CTEST_FILE" ]] || die "Build directory '$BUILD' is not configured." \
                              "Run: make unit-test-build"

# --- The declared contract (test/unit-tests.conf) ---
DECLARED=()
declare -A ARGS
while read -r name rest; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    DECLARED+=("$name")
    ARGS["$name"]="${rest//@BUILD@/$BUILD}"
done < <(sed 's/#.*//' "$CONF")

# --- What CMake actually registered (authoritative, machine-generated) ---
# add_test(<name> "<abs/path/to/binary>" [args...])  ->  basename of the binary.
REGISTERED=()
while read -r bin; do
    [[ -n "$bin" ]] && REGISTERED+=("$(basename "$bin")")
done < <(grep -oP '^add_test\([^ ]+ "\K[^"]+' "$CTEST_FILE")

(( ${#DECLARED[@]}   )) || die "No suites declared in $CONF."
(( ${#REGISTERED[@]} )) || die "No suites registered in $CTEST_FILE."

# --- Cross-check both directions, plus buildness. Any drift is fatal. ---
errors=()
for name in "${DECLARED[@]}"; do
    if ! printf '%s\n' "${REGISTERED[@]}" | grep -qx "$name"; then
        errors+=("declared in $CONF but NOT registered by CMake: ${BOLD}$name${RESET}")
    elif [[ ! -x "$BUILD/test/$name" ]]; then
        errors+=("declared in $CONF but NOT built: ${BOLD}$name${RESET} (no $BUILD/test/$name)")
    fi
done
for name in "${REGISTERED[@]}"; do
    if ! printf '%s\n' "${DECLARED[@]}" | grep -qx "$name"; then
        errors+=("registered by CMake but MISSING from $CONF: ${BOLD}$name${RESET} — it would never run")
    fi
done
if (( ${#errors[@]} )); then
    die "${errors[@]}" "" \
        "The manifest and the build must agree exactly. Add the suite to $CONF," \
        "remove the stale entry, or run 'make unit-test-build' — whichever is true."
fi

# --- Run every declared suite in parallel ---
TMPDIR_RUN=$(mktemp -d)
trap 'rm -rf "$TMPDIR_RUN"' EXIT
rm -f "$SUMMARY"
rm -rf "$LOG_DIR"; mkdir -p "$LOG_DIR"

for name in "${DECLARED[@]}"; do
    (
        # shellcheck disable=SC2086  # args are intentionally word-split
        "$BUILD/test/$name" ${ARGS["$name"]} >"$TMPDIR_RUN/$name.out" 2>&1
        echo $? >"$TMPDIR_RUN/$name.rc"
    ) &
done
wait

# --- Aggregate ---
printf "\n${BOLD}Subsystem unit test results:${RESET}\n\n"
suites_pass=0; suites_fail=0
sum_total=0; sum_passed=0; sum_failed=0; sum_skipped=0

for name in "${DECLARED[@]}"; do
    cp "$TMPDIR_RUN/$name.out" "$LOG_DIR/$name.log"
    rc=$(cat "$TMPDIR_RUN/$name.rc")
    line=$(grep -E '^Total:' "$TMPDIR_RUN/$name.out" | tail -1 || true)

    # A suite that ran but printed no parseable summary contributed 0 rows and
    # still scored PASS under the old aggregator. It is now a failure: we cannot
    # tell "it passed silently" from "it died before asserting anything".
    if [[ ! "$line" =~ ^Total:[[:space:]]+([0-9]+)[[:space:]]+Passed:[[:space:]]+([0-9]+)[[:space:]]+Failed:[[:space:]]+([0-9]+)[[:space:]]+Skipped:[[:space:]]+([0-9]+) ]]; then
        printf "  ${CYAN}%-34s${RESET} ${BADGE_FAIL} FAIL ${RESET}  no parseable 'Total:' summary line (rc=%s)\n" "$name" "$rc"
        sed -E 's/^/      /' <(grep -E '^\s*(FAIL|FATAL|ERROR)' "$TMPDIR_RUN/$name.out" | head -5 || true)
        printf "      full log: %s\n" "$LOG_DIR/$name.log"
        suites_fail=$((suites_fail + 1))
        continue
    fi
    t_total=${BASH_REMATCH[1]}; t_passed=${BASH_REMATCH[2]}
    t_failed=${BASH_REMATCH[3]}; t_skipped=${BASH_REMATCH[4]}

    sum_total=$((sum_total + t_total));     sum_passed=$((sum_passed + t_passed))
    sum_failed=$((sum_failed + t_failed));  sum_skipped=$((sum_skipped + t_skipped))
    printf "%s\t%s\t%s\t%s\t%s\n" "$name" "$t_total" "$t_passed" "$t_failed" "$t_skipped" >>"$SUMMARY"

    if [[ "$rc" -ne 0 || "$t_failed" -gt 0 ]]; then
        printf "  ${CYAN}%-34s${RESET} ${BADGE_FAIL} FAIL ${RESET}  %s\n" "$name" "$line"
        sed -E 's/^/      /' <(grep -E '^\s*(FAIL|FATAL|ERROR)' "$TMPDIR_RUN/$name.out" | head -5 || true)
        printf "      full log: %s\n" "$LOG_DIR/$name.log"
        suites_fail=$((suites_fail + 1))
    elif [[ "$t_skipped" -gt 0 ]]; then
        printf "  ${CYAN}%-34s${RESET} ${BADGE_SKIP} SKIP ${RESET}  %s\n" "$name" "$line"
        suites_pass=$((suites_pass + 1))
    else
        printf "  ${CYAN}%-34s${RESET} ${BADGE_PASS} PASS ${RESET}  %s\n" "$name" "$line"
        suites_pass=$((suites_pass + 1))
    fi
done

printf "\n${BOLD}Total: %d  Passed: %d  Failed: %d  Skipped: %d${RESET}\n" \
    "$sum_total" "$sum_passed" "$sum_failed" "$sum_skipped"
printf "${BOLD}Suites: %d pass, %d fail  (%d declared, %d registered — in agreement)${RESET}\n\n" \
    "$suites_pass" "$suites_fail" "${#DECLARED[@]}" "${#REGISTERED[@]}"

[[ "$suites_fail" -eq 0 ]] || exit 1
exit 0
