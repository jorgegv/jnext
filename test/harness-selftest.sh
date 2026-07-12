#!/usr/bin/env bash
# Self-test for the unit-test harness (test/run-unit-tests.sh).
#
# The harness is load-bearing for every number this project quotes. It shipped
# once with a bug that only appeared when a suite FAILED — the normal failure
# path — because it had been verified only against suites that pass. `set -e`
# killed the runner subshell before it recorded the exit code, and the aggregator
# then aborted, dropping every suite after the failing one. A harness that
# under-reports precisely when something is wrong is worse than no harness.
#
# So: each fault is injected against stub suites in a throwaway build dir, and
# the refusal is asserted. A guard that cannot be shown to fire is not a guard.
#
# Usage: bash test/harness-selftest.sh

set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
HARNESS="$PROJECT_DIR/test/run-unit-tests.sh"
export LC_ALL=C

pass=0; fail=0; total=0

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# stub <name> <rows> <exit_code> [body]  — a fake suite binary
stub() {
    local name=$1 rows=$2 rc=$3 body=${4:-}
    mkdir -p "$T/build/test"
    { echo '#!/usr/bin/env bash'
      [[ -n "$body" ]] && echo "$body"
      [[ "$rows" -ge 0 ]] && printf 'echo "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d"\n' \
                                    "$rows" "$rows" 0 0
      echo "exit $rc"
    } > "$T/build/test/$name"
    chmod +x "$T/build/test/$name"
}

# register <name...> — write a CTestTestfile.cmake naming exactly these binaries
register() {
    : > "$T/build/test/CTestTestfile.cmake"
    for n in "$@"; do
        echo "add_test(${n}s \"$T/build/test/$n\")" >> "$T/build/test/CTestTestfile.cmake"
    done
}

# manifest <line...> — write the manifest
manifest() { printf '%s\n' "$@" > "$T/manifest.conf"; }

run_harness() {
    JNEXT_UNIT_TEST_CONF="$T/manifest.conf" JNEXT_SUITE_TIMEOUT="${TIMEOUT_OVERRIDE:-300}" \
        bash "$HARNESS" "$T/build" 2>&1
}

# check <id> <condition-description> <expected_rc> <actual_rc> <output> <pattern...>
check() {
    local id=$1 desc=$2 want_rc=$3 got_rc=$4 out=$5; shift 5
    total=$((total + 1))
    local ok=1 why=""
    if [[ "$got_rc" -ne "$want_rc" ]]; then ok=0; why="exit $got_rc, wanted $want_rc"; fi
    for pat in "$@"; do
        if ! grep -qF -- "$pat" <<<"$out"; then ok=0; why="${why:+$why; }missing '$pat'"; fi
    done
    if [[ "$ok" -eq 1 ]]; then
        pass=$((pass + 1))
        printf "  [PASS] %-12s %s\n" "$id" "$desc"
    else
        fail=$((fail + 1))
        printf "  FAIL %s: %s [%s]\n" "$id" "$desc" "$why"
        printf "%s\n" "$out" | sed -E 's/^/        | /' | head -20
    fi
}

echo "Unit-test harness self-test"
echo "==========================="
echo ""

# ---------------------------------------------------------------- happy path
stub good_test 10 0; stub other_test 5 0
register good_test other_test
manifest "good_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-01" "clean run: both suites reported, grand total, exit 0" 0 $rc "$out" \
    "Total: 15  Passed: 15  Failed: 0  Skipped: 0" "Suites: 2 pass, 0 fail"

# ------------------------------------------------- THE BUG THAT SHIPPED (a)
# A suite that FAILS (valid summary, non-zero exit). The failing row must be
# reported, every later suite must still be reported, and the grand total must
# still print. Before the fix: the runner subshell died under `set -e` without
# writing its .rc, the aggregator's `cat` failed, and the run aborted here.
stub failing_test -1 1 'echo "Total:   10  Passed:    9  Failed:    1  Skipped:    0"'
register failing_test other_test
manifest "failing_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-02" "a FAILING suite is reported and does not abort the run" 1 $rc "$out" \
    "failing_test" "FAIL" "other_test" "Failed: 1" "Suites: 1 pass, 1 fail"

# ------------------------------------------------- THE BUG THAT SHIPPED (b)
# Same, via a signal: a segfaulting suite must not eat the suites after it.
stub crashing_test -1 0 'kill -SEGV $$'
register crashing_test other_test
manifest "crashing_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-03" "a CRASHING suite is reported and does not abort the run" 1 $rc "$out" \
    "crashing_test" "FAIL" "other_test" "Suites: 1 pass, 1 fail"

# ------------------------------------------------------------------ timeout
stub hanging_test -1 0 'sleep 30'
register hanging_test other_test
manifest "hanging_test 10" "other_test 5"
out=$(TIMEOUT_OVERRIDE=2 run_harness); rc=$?   # NOT `X=1 out=$(...)`: that is an
# assignment list, not a command prefix, so the outer X would PERSIST into every later
# case and silently run them all with a 2 s suite timeout. Found in review.
check "HS-04" "a HANGING suite times out, is reported, run continues" 1 $rc "$out" \
    "hanging_test" "TIMED OUT" "other_test"

# --------------------------------------------------------- manifest drift
stub good_test 10 0; stub other_test 5 0
register good_test other_test
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-05" "registered by CMake but MISSING from the manifest (Task 32)" 2 $rc "$out" \
    "REFUSES TO RUN" "MISSING from" "other_test"

register good_test other_test
manifest "good_test 10" "other_test 5" "ghost_test 3"
out=$(run_harness); rc=$?
check "HS-06" "declared in the manifest but NOT registered by CMake" 2 $rc "$out" \
    "REFUSES TO RUN" "NOT registered by CMake" "ghost_test"

manifest "good_test 10" "other_test 5" "unbuilt_test 3"
register good_test other_test unbuilt_test    # registered, but no binary on disk
out=$(run_harness); rc=$?
check "HS-07" "declared and registered but NOT built (Task 35)" 2 $rc "$out" \
    "REFUSES TO RUN" "NOT built" "unbuilt_test"

register good_test other_test
manifest "good_test 10" "other_test 5" "good_test 10"
out=$(run_harness); rc=$?
check "HS-08" "declared TWICE (would run twice and inflate the count)" 2 $rc "$out" \
    "REFUSES TO RUN" "Declared twice"

# --------------------------------------------------------- the row-count pin
# Task 37's shape: the suite runs, exits 0, and quietly reports fewer rows.
stub shrunk_test 2 0
register shrunk_test other_test
manifest "shrunk_test 66" "other_test 5"
out=$(run_harness); rc=$?
check "HS-09" "a suite that SHRINKS its own row count (Task 37)" 1 $rc "$out" \
    "shrunk_test" "FAIL" "VANISHED"

stub grown_test 70 0
register grown_test other_test
manifest "grown_test 66" "other_test 5"
out=$(run_harness); rc=$?
check "HS-10" "a suite that GROWS its row count without a manifest update" 1 $rc "$out" \
    "grown_test" "FAIL" "update the manifest"

# ------------------------------------------------- a suite that asserts nothing
stub silent_test -1 0    # exits 0, prints no Total: line
register silent_test other_test
manifest "silent_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-11" "a suite that runs, exits 0 and asserts NOTHING" 1 $rc "$out" \
    "silent_test" "FAIL" "asserted nothing"

# --------------------------------------------------------- optional suites
# '?name': mandatory if CMake registered it, loudly skipped if it did not — so a
# legitimate config (-DENABLE_DEBUGGER=OFF) can still run the other 45 suites.
stub good_test 10 0
register good_test                       # optional_test NOT registered
manifest "good_test 10" "?optional_test 7"
out=$(run_harness); rc=$?
check "HS-12" "an optional suite the build did not register: NOTICE, not silence" 0 $rc "$out" \
    "NOTICE" "optional_test" "Suites: 1 pass, 0 fail"

stub optional_test 7 0
register good_test optional_test         # now it IS registered -> mandatory
manifest "good_test 10" "?optional_test 7"
out=$(run_harness); rc=$?
check "HS-13" "an optional suite the build DID register is run like any other" 0 $rc "$out" \
    "optional_test" "Total: 17"

# ------------------------------------------------------------ malformed input
register good_test
manifest "good_test"                     # no row count
out=$(run_harness); rc=$?
check "HS-14" "a manifest line with no row count is rejected" 2 $rc "$out" \
    "REFUSES TO RUN" "Malformed line"

register good_test
manifest "good_test 0"                   # a pin of zero would make an empty suite "pass"
out=$(run_harness); rc=$?
check "HS-15" "a row-count pin of 0 is rejected (an empty suite must not pass)" 2 $rc "$out" \
    "REFUSES TO RUN" "Malformed line"

# ------------------------------ the CMake side must be checked by count, not membership
# One binary registered under two add_test() names: the manifest can only name it once
# (duplicates are rejected), so one registration would never run — while a plain
# set-membership check calls that "in agreement" and exits 0.
stub good_test 10 0; stub other_test 5 0
{ echo "add_test(other_a \"$T/build/test/other_test\" modeA)"
  echo "add_test(other_b \"$T/build/test/other_test\" modeB)"
  echo "add_test(good \"$T/build/test/good_test\")"
} > "$T/build/test/CTestTestfile.cmake"
manifest "good_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-16" "one binary registered under TWO add_test() names is rejected" 2 $rc "$out" \
    "REFUSES TO RUN" "more than one add_test" "other_test"

# ---------------------- add_test() from a CMakeLists other than test/ must still be seen
# Otherwise it is invisible to BOTH directions of the cross-check: not registered as far
# as the harness can see, so not required in the manifest, so never run, never faulted —
# Task 32 re-entering through a different door.
stub good_test 10 0; stub other_test 5 0
register good_test                                   # build/test/CTestTestfile.cmake
mkdir -p "$T/build/elsewhere"
echo "add_test(sneaky \"$T/build/test/other_test\")" > "$T/build/elsewhere/CTestTestfile.cmake"
manifest "good_test 10"                              # other_test NOT declared
out=$(run_harness); rc=$?
check "HS-17" "an add_test() outside build/test/ is still cross-checked" 2 $rc "$out" \
    "REFUSES TO RUN" "MISSING from" "other_test"
rm -rf "$T/build/elsewhere"

echo ""
echo "====================================="
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" "$total" "$pass" "$fail" 0
[[ "$fail" -eq 0 ]] || exit 1
exit 0
