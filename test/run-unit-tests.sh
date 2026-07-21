#!/usr/bin/env bash
# Unit-test harness for jnext. Runs exactly the suites declared in
# test/unit-tests.conf, in parallel, and aggregates their results.
#
# The point of this script is that it CANNOT QUIETLY RUN FEWER TESTS THAN IT
# CLAIMS. Every way a suite could vanish from the denominator is loud:
#
#   refuse to run (exit 2), before a single test runs:
#     * declared here but not registered in CMake     -> stale entry
#     * registered in CMake but not declared here     -> Task 32's bug
#     * declared here but the binary is not built     -> Task 35's shape
#     * declared here twice                           -> inflated counts
#   FAIL (exit 1):
#     * ran, but printed no parseable summary line    -> used to score PASS with 0 rows
#     * ran, but reported a row count != the declared one -> Task 37's shape
#     * exited non-zero, crashed, or hit the timeout  -> reported, never swallowed
#
# It is itself under test: test/harness-selftest.sh injects each of those faults
# against stub suites and asserts the refusal. Run it via `make harness-selftest`
# (and the regression suite runs it as `harness-selftest-func`).
#
# Usage: bash test/run-unit-tests.sh [build_dir]      (default: build)
# Env:   JNEXT_UNIT_TEST_CONF   override the manifest path (the self-test uses this)

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
CONF="${JNEXT_UNIT_TEST_CONF:-test/unit-tests.conf}"
CTEST_FILE="$BUILD/test/CTestTestfile.cmake"
SUMMARY="$BUILD/test-summary.tsv"
LOG_DIR="$BUILD/test-logs"
SUITE_TIMEOUT="${JNEXT_SUITE_TIMEOUT:-300}"   # no suite may hang the run (cf. Task 33's hanging lint)

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
# <executable> <expected_rows> [args...]   — a leading '?' marks an optional suite
# (one that a legitimate build configuration may not register; see the manifest).
DECLARED=()
declare -A EXPECTED ARGS OPTIONAL
while read -r name rows rest; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    opt=0
    if [[ "$name" == \?* ]]; then opt=1; name="${name#\?}"; fi
    # A pin of 0 must not be expressible: a suite pinned at 0 that reports 0 rows would
    # PASS, while the same suite reporting no summary at all is a hard FAIL. Zeroing a
    # suite is exactly the silent-truncation move this manifest exists to forbid.
    [[ "$rows" =~ ^[1-9][0-9]*$ ]] \
        || die "Malformed line in $CONF: '${BOLD}$name $rows${RESET}'" \
               "Expected: <executable> <expected_rows> [args...], with expected_rows >= 1"
    for prev in ${DECLARED[@]+"${DECLARED[@]}"}; do
        [[ "$prev" == "$name" ]] && die "Declared twice in $CONF: ${BOLD}$name${RESET}" \
                                        "A duplicate runs the suite twice and inflates every count."
    done
    DECLARED+=("$name")
    EXPECTED["$name"]="$rows"
    OPTIONAL["$name"]="$opt"
    ARGS["$name"]="${rest//@BUILD@/$BUILD}"
done < <(sed 's/#.*//' "$CONF")

# --- What CMake actually registered (authoritative, machine-generated) ---
# add_test(<name> "<abs/path/to/binary>" [args...])  ->  basename of the binary.
#
# Read EVERY CTestTestfile.cmake in THIS build tree, not just test/'s: an add_test()
# issued from any other CMakeLists.txt would otherwise be invisible to both directions
# of the cross-check — never required in the manifest, never run, never faulted. That
# is Task 32 re-entering through a different door.
#
# But prune nested build trees. The project's other build dirs live INSIDE build/
# (build/sdl-debug, build/gui-debug, build/gui-release — see the Makefile), and the two
# debug ones configure with ENABLE_TESTS=ON. An unscoped find would swallow their
# CTestTestfile.cmake, see every binary twice, and refuse to run with a false
# "registered twice" diagnosis after a plain `make gui-debug`. A directory with its own
# CMakeCache.txt is an independent build root and is not ours to enumerate.
mapfile -t CTEST_FILES < <(
    find "$BUILD" -mindepth 1 -type d -exec test -e '{}/CMakeCache.txt' ';' -prune -o \
         -name CTestTestfile.cmake -print
)
REGISTERED=()
while read -r bin; do
    [[ -n "$bin" ]] && REGISTERED+=("$(basename "$bin")")
done < <(grep -hoP '^add_test\([^ ]+ "\K[^"]+' "${CTEST_FILES[@]}" 2>/dev/null || true)

# Assert we parsed every add_test line: a silent parse miss would be the very blindness
# this file exists to prevent. `|| true` is load-bearing — grep exits 1 when a file has
# no add_test lines, and under `set -e` + pipefail that would kill the script HERE,
# silently, making the "No suites registered" die below unreachable. (Exactly that bug
# was found in review, in this very guard.)
n_add_test=$(grep -hc '^add_test(' "${CTEST_FILES[@]}" 2>/dev/null | awk '{s+=$1} END {print s+0}' || true)
[[ "${#REGISTERED[@]}" -eq "${n_add_test:-0}" ]] \
    || die "Parsed ${BOLD}${#REGISTERED[@]}${RESET} of ${BOLD}${n_add_test:-0}${RESET} add_test() lines under $BUILD." \
           "The harness cannot see every registered suite, so it cannot vouch for the list."

# One binary registered under two add_test() names runs once, and the manifest cannot
# even express the second (duplicates are rejected). Set-membership would call that
# agreement; it is not. Reject it.
dupes=$(printf '%s\n' "${REGISTERED[@]}" | sort | uniq -d)
[[ -z "$dupes" ]] \
    || die "Registered under more than one add_test() name: ${BOLD}$(echo "$dupes" | tr '\n' ' ')${RESET}" \
           "The manifest names each binary once, so one of those registrations would never run."

(( ${#DECLARED[@]}   )) || die "No suites declared in $CONF."
(( ${#REGISTERED[@]} )) || die "No suites registered under $BUILD."

# The suite-count pin. Without it, "N declared == N registered" is a tautology against
# the one edit that matters most: remove a suite's add_test() AND its manifest row, and
# both sides shrink in lockstep — every count agrees, exit 0, and the suite is gone.
# `|| true`: grep exits 1 when there is no pin line, which under `set -e` would kill the
# script here and make the die below unreachable — a dead guard, found in review.
pin=$(grep -oP '^#\s*expect:\s*\K[0-9]+' "$CONF" | head -1 || true)
[[ -n "$pin" ]] || die "No '${BOLD}# expect: N${RESET}' pin in $CONF." \
                       "The manifest must state how many suites it declares."
[[ "$pin" -eq "${#DECLARED[@]}" ]] \
    || die "$CONF declares ${BOLD}${#DECLARED[@]}${RESET} suites but pins ${BOLD}# expect: $pin${RESET}." \
           "A suite was added or removed without updating the pin. If deliberate, update it."

# Membership is an in-shell hash lookup, NEVER `printf ... | grep -q`. That idiom is
# unsound under `set -o pipefail` (which this script sets): `grep -q` exits the instant
# it matches, `printf` then dies of SIGPIPE (141), and pipefail makes 141 the PIPELINE's
# status — so a suite that IS present reports as absent. It is a race, so it fires only
# sometimes and names a random suite in a random direction; measured at ~4% of runs on a
# loaded 12-core box and 0% idle, which is exactly the "fails right after a parallel
# build, passes on re-run" shape reported in Task 88. A guard that cries wolf trains
# everyone to re-run until green, which would let a REAL mismatch through.
declare -A IS_DECLARED IS_REGISTERED
for name in "${DECLARED[@]}";   do IS_DECLARED["$name"]=1;   done
for name in "${REGISTERED[@]}"; do IS_REGISTERED["$name"]=1; done

# --- Cross-check both directions, plus buildness. Any drift is fatal. ---
errors=(); notices=(); RUNNABLE=()
for name in "${DECLARED[@]}"; do
    if [[ -z "${IS_REGISTERED[$name]:-}" ]]; then
        if [[ "${OPTIONAL[$name]}" == "1" ]]; then
            # A legitimate build configuration may not register it (e.g.
            # -DENABLE_DEBUGGER=OFF). Skipped — but never silently.
            notices+=("optional suite not registered by this build, NOT RUN: ${BOLD}$name${RESET}")
            continue
        fi
        errors+=("declared in $CONF but NOT registered by CMake: ${BOLD}$name${RESET}")
        continue
    fi
    if [[ ! -x "$BUILD/test/$name" ]]; then
        errors+=("declared in $CONF but NOT built: ${BOLD}$name${RESET} (no $BUILD/test/$name)")
        continue
    fi
    RUNNABLE+=("$name")
done
for name in "${REGISTERED[@]}"; do
    [[ -n "${IS_DECLARED[$name]:-}" ]] \
        || errors+=("registered by CMake but MISSING from $CONF: ${BOLD}$name${RESET} — it would never run")
done
if (( ${#errors[@]} )); then
    die "${errors[@]}" "" \
        "The manifest and the build must agree exactly. Add the suite to $CONF," \
        "remove the stale entry, or run 'make unit-test-build' — whichever is true."
fi
for n in ${notices[@]+"${notices[@]}"}; do
    printf "${BADGE_SKIP} NOTICE ${RESET} %b\n" "$n"
done

# --- Run every runnable suite in parallel ---
TMPDIR_RUN=$(mktemp -d)
trap 'rm -rf "$TMPDIR_RUN"' EXIT
rm -f "$SUMMARY"
rm -rf "$LOG_DIR"; mkdir -p "$LOG_DIR"

for name in "${RUNNABLE[@]}"; do
    (
        # `|| rc=$?` is load-bearing: this subshell inherits `set -e`, so without it
        # a suite exiting non-zero (i.e. ANY failing test — the normal failure path)
        # would kill the subshell before the .rc file is written, and the aggregator
        # below would then abort the whole run, dropping every suite after it. That
        # bug shipped once; test/harness-selftest.sh now proves it cannot come back.
        rc=0
        timeout --kill-after=5s "${SUITE_TIMEOUT}s" \
            "$BUILD/test/$name" ${ARGS["$name"]} >"$TMPDIR_RUN/$name.out" 2>&1 || rc=$?
        echo "$rc" >"$TMPDIR_RUN/$name.rc"
    ) &
done
wait

# --- Aggregate ---
printf "\n${BOLD}Subsystem unit test results:${RESET}\n\n"
suites_pass=0; suites_fail=0
sum_total=0; sum_passed=0; sum_failed=0; sum_skipped=0

fail_row() {   # fail_row <name> <message>
    printf "  ${CYAN}%-34s${RESET} ${BADGE_FAIL} FAIL ${RESET}  %b\n" "$1" "$2"
    grep -E '^\s*(FAIL|FATAL|ERROR)' "$TMPDIR_RUN/$1.out" 2>/dev/null | head -5 | sed -E 's/^/      /' || true
    printf "      full log: %s\n" "$LOG_DIR/$1.log"
    suites_fail=$((suites_fail + 1))
}

for name in "${RUNNABLE[@]}"; do
    cp "$TMPDIR_RUN/$name.out" "$LOG_DIR/$name.log" 2>/dev/null || true
    rc=$(cat "$TMPDIR_RUN/$name.rc" 2>/dev/null || echo 255)
    line=$(grep -E '^Total:' "$TMPDIR_RUN/$name.out" 2>/dev/null | tail -1 || true)

    # A suite that ran but printed no parseable summary contributed 0 rows and still
    # scored PASS under the old aggregator. It is now a failure: we cannot tell "it
    # passed silently" from "it died before asserting anything". rc 124 = timeout,
    # 139/great = signal, 255 = the .rc file itself went missing.
    if [[ ! "$line" =~ ^Total:[[:space:]]+([0-9]+)[[:space:]]+Passed:[[:space:]]+([0-9]+)[[:space:]]+Failed:[[:space:]]+([0-9]+)[[:space:]]+Skipped:[[:space:]]+([0-9]+) ]]; then
        case "$rc" in
            124|137) fail_row "$name" "TIMED OUT after ${SUITE_TIMEOUT}s (no summary line)" ;;
            0)       fail_row "$name" "no parseable 'Total:' summary line (rc=0 — it asserted nothing)" ;;
            *)       fail_row "$name" "no parseable 'Total:' summary line (rc=$rc — crashed?)" ;;
        esac
        continue
    fi
    t_total=${BASH_REMATCH[1]}; t_passed=${BASH_REMATCH[2]}
    t_failed=${BASH_REMATCH[3]}; t_skipped=${BASH_REMATCH[4]}

    sum_total=$((sum_total + t_total));     sum_passed=$((sum_passed + t_passed))
    sum_failed=$((sum_failed + t_failed));  sum_skipped=$((sum_skipped + t_skipped))
    printf "%s\t%s\t%s\t%s\t%s\n" "$name" "$t_total" "$t_passed" "$t_failed" "$t_skipped" >>"$SUMMARY"

    # The row-count pin. A suite is not allowed to quietly stop emitting rows (that
    # is Task 37 exactly), nor to grow without the manifest saying so.
    exp=${EXPECTED[$name]}
    if [[ "$t_total" -ne "$exp" ]]; then
        if [[ "$t_total" -lt "$exp" ]]; then
            fail_row "$name" "$line\n      ${BOLD}reported $t_total rows, but $CONF pins $exp — $((exp - t_total)) row(s) VANISHED${RESET}"
        else
            fail_row "$name" "$line\n      ${BOLD}reported $t_total rows, but $CONF pins $exp — update the manifest to $t_total${RESET}"
        fi
        continue
    fi

    if [[ "$rc" -ne 0 || "$t_failed" -gt 0 ]]; then
        fail_row "$name" "$line"
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
printf "${BOLD}Suites: %d pass, %d fail  (%d run, %d declared, %d registered; manifest: %s)${RESET}\n" \
    "$suites_pass" "$suites_fail" "${#RUNNABLE[@]}" "${#DECLARED[@]}" "${#REGISTERED[@]}" "$CONF"
if (( ${#notices[@]} )); then
    printf "${BOLD}%d optional suite(s) not registered by this build — see NOTICE above.${RESET}\n" "${#notices[@]}"
fi

# The three counts in that footer are an invariant, not a decoration: every declared
# suite either ran or was a NOTICE'd optional, and the manifest and the build agree.
# If they ever disagree, the footer must not be allowed to read like agreement.
if (( ${#RUNNABLE[@]} + ${#notices[@]} != ${#DECLARED[@]} )); then
    die "Internal: ${#RUNNABLE[@]} run + ${#notices[@]} skipped != ${#DECLARED[@]} declared."
fi

# A pin violation or a failing suite must NOT leave a green-looking headline behind:
# "Total: ... Failed: 0" is the exact line that gets copied into status reports.
if [[ "$suites_fail" -gt 0 ]]; then
    printf "\n${BADGE_FAIL} UNIT TESTS FAILED ${RESET} ${BOLD}%d suite(s) failed — the totals above are NOT a passing result.${RESET}\n\n" \
        "$suites_fail"
    exit 1
fi
printf "\n"
exit 0
