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

# The pinned denominator (GH #79): without it, deleting a check here shrinks
# the declared and the reported side in lockstep — the exact silent-truncation
# move the harnesses this file guards were built to forbid. Adding or removing
# a check MUST update this number, deliberately.
EXPECTED_TOTAL=39

# Per-invocation bound on every end-to-end run of a REAL script (GH #81).
# run_harness and run_preflight each execute a real harness end to end, and a
# hanging fault anywhere in one — a slow cleanup body, a slow injected fault —
# used to hang this whole self-test at its first row, long before the bounded
# HS-43 probes were even reached (GH #79's verifiers had to shrink that
# issue's literal `sleep 300` to keep mutation testing tractable). Bounded per
# invocation, a hang is one loud rc=124 FAIL at the affected row (check()
# prints the rc) and the run continues. 30 s is ~14x the slowest legitimate
# invocation measured on the dev box (HS-04 at ~2.1 s, floor-bound by its own
# 2 s suite timeout; every other invocation is < 0.3 s) — wide on purpose,
# this project has been burned by tight wall-clock budgets on loaded boxes.
# HS-44 proves the bound fires; INVOKE_TIMEOUT_OVERRIDE is its hook (same
# pattern as HS-04's TIMEOUT_OVERRIDE) so proving it costs ~3 s, not 30.
INVOKE_TIMEOUT=30

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

# manifest <line...> — write the manifest, with a matching `# expect: N` pin
manifest() {
    { echo "# expect: $#"; printf '%s\n' "$@"; } > "$T/manifest.conf"
}
# manifest_pinned <pin> <line...> — same, but with a deliberately chosen pin
manifest_pinned() {
    local pin=$1; shift
    { echo "# expect: $pin"; printf '%s\n' "$@"; } > "$T/manifest.conf"
}

run_harness() {
    JNEXT_UNIT_TEST_CONF="$T/manifest.conf" JNEXT_SUITE_TIMEOUT="${TIMEOUT_OVERRIDE:-300}" \
        timeout --kill-after=5s "${INVOKE_TIMEOUT_OVERRIDE:-$INVOKE_TIMEOUT}s" \
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

# --------------------------------------------------------- the suite-count pin
# Without it, "N declared == N registered" is a tautology against the edit that matters
# most: drop a suite's add_test() AND its manifest row and both sides shrink together.
stub good_test 10 0; stub other_test 5 0
register good_test other_test
manifest_pinned 3 "good_test 10" "other_test 5"      # pin says 3, only 2 declared
out=$(run_harness); rc=$?
check "HS-18" "the manifest's own suite-count pin is enforced" 2 $rc "$out" \
    "REFUSES TO RUN" "pins" "expect: 3"

register good_test other_test
printf 'good_test 10\nother_test 5\n' > "$T/manifest.conf"    # no pin line at all
out=$(run_harness); rc=$?
check "HS-19" "a manifest with NO suite-count pin is rejected (guard must not be dead)" 2 $rc "$out" \
    "REFUSES TO RUN" "expect: N"

# ------------------------------------------- nested build trees must NOT be swallowed
# build/sdl-debug and build/gui-debug live INSIDE build/ and configure with ENABLE_TESTS=ON.
# An unscoped find swallowed their CTestTestfile.cmake, saw every binary twice, and
# refused to run with a FALSE "registered twice" diagnosis after a plain `make gui-debug`.
# A directory with its own CMakeCache.txt is an independent build root.
stub good_test 10 0; stub other_test 5 0
register good_test other_test
touch "$T/build/CMakeCache.txt"          # the root IS a build root: -mindepth 1 must not prune it
mkdir -p "$T/build/gui-debug/test"
touch "$T/build/gui-debug/CMakeCache.txt"
{ echo "add_test(good \"$T/build/gui-debug/test/good_test\")"
  echo "add_test(other \"$T/build/gui-debug/test/other_test\")"
} > "$T/build/gui-debug/test/CTestTestfile.cmake"
manifest "good_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-20" "a nested build tree (own CMakeCache.txt) is NOT enumerated" 0 $rc "$out" \
    "Suites: 2 pass, 0 fail" "2 registered"
rm -rf "$T/build/gui-debug"

# ------------------------------------------- a build tree that registers NOTHING
# The other half of the dead-guard pair. `grep -hc` exits 1 when no file has an
# add_test line; under `set -e` + pipefail that killed the script at the assignment —
# bare exit 1, not one character of output, and the "No suites registered" die below it
# was unreachable. Revived with `|| true`, and it stayed 24/24 green when the reviewer
# re-killed it, because nothing tested it. This is that test.
stub good_test 10 0
: > "$T/build/test/CTestTestfile.cmake"      # configured, but zero add_test()
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-25" "a build tree that registers NO suites is rejected, loudly" 2 $rc "$out" \
    "REFUSES TO RUN" "No suites registered"

# The parse-completeness guard: an add_test() line the regex cannot read must not be
# silently dropped from the registered set — that is the blindness this file prevents.
stub good_test 10 0
{ echo "add_test(good \"$T/build/test/good_test\")"
  echo "add_test(unquoted $T/build/test/other_test)"     # no quotes: counted, not parsed
} > "$T/build/test/CTestTestfile.cmake"
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-26" "an add_test() line the parser cannot read is a refusal, not a silent drop" 2 $rc "$out" \
    "REFUSES TO RUN" "Parsed"

# ------------------------------------- agreement must NEVER be reported as drift (Task 88)
# The guard's contract has two sides, and this is the side nobody tested: when the
# manifest and the build DO agree, it must say so — every time, on every machine, under
# any load. It did not. Membership was `printf '%s\n' "${LIST[@]}" | grep -qx "$name"`,
# and this script sets `set -o pipefail`: `grep -q` exits the moment it matches, `printf`
# then dies of SIGPIPE (141), and pipefail promotes 141 to the pipeline's status — so a
# suite that IS declared reported as MISSING. Whether printf had finished writing before
# grep exited is a scheduling race, which is why it fired ~4% of the time on a loaded box,
# named a random suite, and pointed in a random direction — and why "just re-run it"
# looked like a fix. That reflex is the real damage: it is exactly how a REAL mismatch
# would get waved through.
#
# Provoking the race on purpose is a matter of MARGIN, and the margin has to clear two
# things, not one: the pipe capacity (64 KB on Linux) AND however much grep's first read()
# drains before it matches the FIRST entry and exits. Only if the writer is still blocked
# after that does SIGPIPE fire. Sizing to just past the pipe buffer is NOT enough — two
# 40 KB fillers were measured here at 25/50 false passes against the broken guard, i.e. a
# coin flip, which would have let the bug back in half the time it was reintroduced.
# 200 KB per filler clears both and was measured at 0/50 false passes (and 0/50 again post-
# fix, correctly passing). Treat that figure as the empirical floor, not as a proof: it is
# a race, so the honest claim is a large measured margin, not a guarantee. If this row ever
# flaps, the fix is MORE padding, never fewer iterations.
stub good_test 10 0
register good_test
filler_a="pad_a_$(head -c 200000 /dev/zero | tr '\0' 'a')"
filler_b="pad_b_$(head -c 200000 /dev/zero | tr '\0' 'b')"
manifest "good_test 10" "?$filler_a 1" "?$filler_b 1"
out=$(run_harness); rc=$?
check "HS-27" "manifest/build AGREEMENT is never reported as drift (no SIGPIPE race)" 0 $rc "$out" \
    "Total: 10  Passed: 10  Failed: 0  Skipped: 0" "Suites: 1 pass, 0 fail"

# ------------------------------------- the per-invocation bound itself (GH #81)
# Every row above and every preflight row below runs a REAL script end to end,
# and the `timeout` inside run_harness/run_preflight is what keeps a hanging
# fault — a slow cleanup body, a slow injected fault — down to one loud rc=124
# FAIL instead of hanging this whole self-test at that row. A guard that
# cannot be shown to fire is not a guard, so: drive run_harness ITSELF (the
# real wrapper, not a copy of its invocation — a copy would stay green with
# the wrapper deleted) against a stub that sleeps far longer than every bound
# in play, and assert the row comes back 124, fast, and the self-test
# continues — HS-21 and everything after it still running IS the
# continuation claim.
#
# INVOKE_TIMEOUT_OVERRIDE=3 (HS-04's TIMEOUT_OVERRIDE pattern) keeps this row
# at ~3 s instead of 30. The OUTER `timeout 15s` is what lets the row report
# the very defect it guards against: with the inner wrapper removed,
# run_harness blocks until the harness's suite timeout — the outer bound
# kills it at 15 s and the row FAILs loudly, bounded, instead of this
# self-test hanging. rc alone cannot tell the two apart (either timeout
# yields 124), so the discriminator is WALL TIME: inner path ~3 s, outer path
# ~15 s, threshold 8 s — the same generous-headroom reasoning as HS-42's.
# The exports live inside the $( ) subshell and die with it; run_harness is
# exported as a function so the outer `timeout` (which cannot run a shell
# function) can reach it through `bash -c`.
#
# The bound ordering is deliberate, all four ways:
#   inner 3 s  <  threshold 8 s  <  outer 15 s  <  suite TIMEOUT_OVERRIDE 20 s
# so only the inner bound can produce fast=y, and with the inner wrapper
# removed the outer fires before the suite timeout can dress the hang up as
# an ordinary rc=1 "TIMED OUT" report. The suite timeout is overridden (and
# the stub's sleep finite) for hygiene, not semantics: each GNU `timeout`
# setpgid()s its command into a NEW process group, so the harness's
# suite-level `timeout` ESCAPES the invoke-level kill — the killed group is
# the harness's, and the {suite-timeout, stub, sleep} trio survives it,
# reparented to init, holding no fd of ours (the suite's stdout is a file,
# not this row's pipe — the capture returns promptly). It self-terminates
# when the suite timeout fires, so 20 s bounds the litter at ~17 s where the
# 300 s default would leave a sleeping trio behind for 5 minutes.
stub hang44_test -1 0 'sleep 60'
register hang44_test
manifest "hang44_test 10"
t0=$(date +%s%N)
out=$( export -f run_harness; export T HARNESS INVOKE_TIMEOUT
       timeout --kill-after=5s 15s \
           bash -c 'TIMEOUT_OVERRIDE=20 INVOKE_TIMEOUT_OVERRIDE=3 run_harness' ); rc=$?
t1=$(date +%s%N)
elapsed=$(( (t1 - t0) / 1000000 ))          # ms
fast=y; [[ "$elapsed" -gt 8000 ]] && fast=n
check "HS-44" "a hanging REAL-script invocation is bounded: rc=124, fast, run continues (GH #81)" 0 0 \
    "rc=$rc fast=$fast elapsed=${elapsed}ms" "rc=124 fast=y"

# =====================================================================================
# The regression harness's preflight (test/00regression/regression.sh --preflight-only).
# It had ZERO test coverage, and that is precisely where two guards shipped DEAD: a grep
# exiting 1 under `set -e` killed the script before the fault could print.
# =====================================================================================
REG="$PROJECT_DIR/test/00regression/regression.sh"
REG_CONF="$PROJECT_DIR/test/00regression/regression_tests.conf"
REG_FUNC="$PROJECT_DIR/test/00regression/functional_tests.conf"

run_preflight() {   # run_preflight <conf> <func_conf>
    JNEXT_REGRESSION_CONF="$1" JNEXT_REGRESSION_FUNC_CONF="$2" \
        timeout --kill-after=5s "${INVOKE_TIMEOUT_OVERRIDE:-$INVOKE_TIMEOUT}s" \
        bash "$REG" --preflight-only 2>&1
}

out=$(run_preflight "$REG_CONF" "$REG_FUNC"); rc=$?
check "HS-21" "regression preflight: the real manifests pass" 0 $rc "$out" "preflight OK"

# a screenshot test quietly dropped from the conf (with its reference image deleted too,
# which is what defeated the reference-image witness)
grep -v '^odemo ' "$REG_CONF" > "$T/trunc.conf"
out=$(run_preflight "$T/trunc.conf" "$REG_FUNC"); rc=$?
check "HS-22" "regression preflight: a dropped screenshot test faults on the pin" 2 $rc "$out" \
    "HARNESS FAULT" "declares" "pins"

# a functional test quietly dropped from its conf
grep -v '^rzx-record-func' "$REG_FUNC" > "$T/trunc-func.conf"
out=$(run_preflight "$REG_CONF" "$T/trunc-func.conf"); rc=$?
check "HS-23" "regression preflight: a dropped functional test faults on the pin" 2 $rc "$out" \
    "HARNESS FAULT" "declares" "pins"

# no pin line at all — the guard that was dead on arrival
grep -v '^# expect:' "$REG_CONF" > "$T/nopin.conf"
out=$(run_preflight "$T/nopin.conf" "$REG_FUNC"); rc=$?
check "HS-24" "regression preflight: a manifest with NO pin is rejected, loudly" 2 $rc "$out" \
    "HARNESS FAULT" "expect: N"

# a functional test declared in the conf with NO scripts/<name>.sh to run it — that
# test could never report its row, so the driver must refuse before running anything.
# The doctored conf keeps its pin consistent, so only the script check can fire.
{ grep -v '^# expect:' "$REG_FUNC"; echo "ghost-func"
  echo "# expect: $(( $(grep -oP '^#\s*expect:\s*\K[0-9]+' "$REG_FUNC") + 1 ))"
} > "$T/ghost-func.conf"
out=$(run_preflight "$REG_CONF" "$T/ghost-func.conf"); rc=$?
check "HS-31" "regression preflight: a declared functional test with NO script is refused" 2 $rc "$out" \
    "HARNESS FAULT" "ghost-func" "NO test script"

# a stray scripts/*.sh not declared in the conf — a test dropped from the manifest
# while its script lives on. Injected via a copy of the real scripts directory so
# the real tree is never touched. Goes through run_preflight (the env prefix is
# exported to its children for the call), so it gets the same invocation bound
# as every other preflight row.
mkdir -p "$T/scripts"
cp "$PROJECT_DIR"/test/00regression/scripts/*.sh "$T/scripts/"
echo '#!/usr/bin/env bash' > "$T/scripts/bogus-func.sh"
out=$(JNEXT_REGRESSION_SCRIPTS_DIR="$T/scripts" run_preflight "$REG_CONF" "$REG_FUNC"); rc=$?
check "HS-32" "regression preflight: a stray scripts/*.sh not declared in the conf is refused" 2 $rc "$out" \
    "HARNESS FAULT" "bogus-func" "NOT declared"

# ---------------- membership checks must never lose to SIGPIPE (Task 88b) ----------------
# The suite asks "is this name in that list?" in three places: want() in
# test-functions.inc, plus the rewind-func build guard and the end-of-run completeness
# cross-check in regression.sh. All three used
# `printf '%s\n' "${LIST[@]}" | grep -qx "$name"`, which is unsound under the
# `set -o pipefail` those files run under: grep -q exits the instant it matches, the
# printf subshell can then die of SIGPIPE (141), and pipefail promotes 141 to the
# PIPELINE's status — so a name that IS present reports as absent.
#
# Why these rows exist at all, given the real completeness check cannot be driven here:
# that check only runs on a FULL suite (no filter, not update mode), and this self-test is
# itself invoked BY the suite as harness-selftest-func — driving a second full run from
# here would recurse. So HS-28/29 pin the SEMANTICS of the membership pattern in isolation
# (bash-only, no jnext binary, sub-second), and HS-30 is what BINDS every suite source —
# the driver, test-functions.inc and each scripts/*.sh — to them by refusing the unsound
# idiom textually. The pair matters: without HS-30 these rows test a copy of the pattern
# and would happily stay green while the suite regressed.
#
# Determinism comes from SIZE, not from repetition. The writer must be BLOCKED in write()
# for SIGPIPE to land, so the payload has to exceed the pipe buffer (65536 on Linux):
# measured on this host at 0/60 false absents with a 32 KB payload, 13/60 at 64 KB and
# 60/60 at 130 KB. 200 KB is used here and was measured at 20/20 discrimination. If this
# row ever flaps, the fix is MORE padding, never fewer iterations.
membership_probe() {   # membership_probe <hash|pipe> <target> -> FOUND | ABSENT
    ( set -euo pipefail
      local pad; pad=$(head -c 200000 /dev/zero | tr '\0' 'z')
      local LIST=(alpha-func beta-func "pad_$pad")
      if [[ "$1" == hash ]]; then
          declare -A H; local n; for n in "${LIST[@]}"; do H["$n"]=1; done
          if [[ -n "${H[$2]:-}" ]]; then echo FOUND; else echo ABSENT; fi
      else
          if printf '%s\n' "${LIST[@]}" | grep -qx "$2"; then echo FOUND; else echo ABSENT; fi
      fi )
}
probe_count() {        # probe_count <impl> <target> <n> -> "<found>/<n>"
    local f=0 i
    for ((i = 0; i < $3; i++)); do [[ "$(membership_probe "$1" "$2")" == FOUND ]] && f=$((f + 1)); done
    echo "$f/$3"
}

got=$(probe_count hash alpha-func 20)
check "HS-28" "membership: a PRESENT name is found every time, at any list size" 0 0 "$got" "20/20"

# Both directions, so a membership check that simply always says "yes" cannot pass.
present=$(probe_count hash beta-func 5)
absent=$(probe_count hash not-a-test-func 5)
check "HS-29" "membership: present name found, absent name NOT found (no blanket yes)" 0 0 \
    "present=$present absent=$absent" "present=5/5" "absent=0/5"

# The binding guard: no suite source may reintroduce the unsound idiom — the scan covers
# the driver, the shared lib and every scripts/*.sh. Comments are stripped first so the
# explanatory text above each fixed site does not self-trigger.
offenders=""
for src in "$REG" \
           "$PROJECT_DIR/test/00regression/test-functions.inc" \
           "$PROJECT_DIR"/test/00regression/scripts/*.sh; do
    hits=$(sed -E 's/^[[:space:]]*#.*$//' "$src" | grep -nE "printf.*\|[[:space:]]*grep[[:space:]]+-q" || true)
    [[ -z "$hits" ]] || offenders+="${src##*/}: ${hits}"$'\n'
done
check "HS-30" "no suite source reintroduces 'printf ... | grep -q' membership" 0 0 \
    "offenders=[${offenders}]" "offenders=[]"

# ------------------------------------------- SD clone cleanup on a signal
# GH #75: the harness clones the ~1 GB SD master into $HOME/.jnext/runs for
# the duration of a run. Cleaning that up on the NORMAL exit paths is the easy
# half and was already green while the script leaked a gigabyte per run twice
# over: first because a second `trap ... EXIT` REPLACES the first rather than
# adding to it, then because an EXIT-only trap does not fire at all when the
# shell is killed by an untrapped signal. Measured on that version, 20% of
# SIGINTs and 45% of SIGTERMs delivered mid-run leaked the clone — and `make
# unit-test` runs the harness as a foreground recipe in the terminal's process
# group, so Ctrl-C is the ordinary way a user ends a slow run.
#
# Neither leak was visible to any test: a green run cleans up correctly, and
# the whole triplet passed throughout. So it is pinned here.
#
# Hermetic and fast: a fake $HOME with a TINY file standing in for the master,
# so the clone is instant and the real ~/.jnext is never touched. The stub
# suite sleeps, so the signal lands in the parallel `wait` — the window that
# actually leaked. JNEXT_TEST_SD_IMAGE must be unset: inherited from an outer
# run it would make the harness report `preset` and skip cloning entirely,
# which would make this test pass without proving anything.
# This is a SOURCE-SCAN guard, like HS-30 above, and that is a deliberate
# choice over a functional one. Read this before "improving" it into a test
# that actually sends a signal.
#
# The leak is real: with EXIT-only traps, `timeout --signal=TERM <d>s` against
# this harness leaks the clone — measured here at 1/8 across d = 0.2 .. 1.6s,
# and independently at 20% (INT) / 45% (TERM) by review. With INT and TERM
# trapped it is 0/10 over the same sweep, including three repeats of the exact
# duration that leaked.
#
# But a functional row is a bad guard for it, and that was proven rather than
# assumed. A first attempt built a hermetic fixture (fake $HOME, tiny stand-in
# master, stub suite sleeping, signal sent once the clone appeared) and it
# PASSED WITH THE FIX REVERTED — bash does run an EXIT trap for most
# signal-death paths, so the vulnerable window is narrow and depends on where
# in the run the signal lands. A probabilistic row that passes against the bug
# ~7 times in 8 is worse than no row: it launders the bug as covered.
#
# So the guard asserts the property that closes the window, deterministically:
# every EXIT trap in the harness also lists INT and TERM. It cannot regress
# silently, and it costs nothing to run.
# Scans EVERY script that cleans up a ~1 GB SD clone, not just the unit
# harness. The bug appeared three times in one branch — run-unit-tests.sh
# twice, then bench.sh again 18 minutes after the second fix — and a guard
# covering only the file that happened to be fixed first would not have
# caught the third. test-functions.inc carried it from GH #65 and was found
# by the same review.
CLEANUP_SCRIPTS=("$HARNESS"
                 "$PROJECT_DIR/test/bench/bench.sh"
                 "$PROJECT_DIR/test/00regression/test-functions.inc")

# (a) each script handles INT and TERM at all.
missing=""
for src in "${CLEANUP_SCRIPTS[@]}"; do
    grep -qE "^[[:space:]]*trap[[:space:]].*[[:space:]]INT([[:space:]]|$)" "$src" || missing+="${src##*/}:INT "
    grep -qE "^[[:space:]]*trap[[:space:]].*[[:space:]]TERM([[:space:]]|$)" "$src" || missing+="${src##*/}:TERM "
done
check "HS-40" "every SD-clone cleanup script handles INT and TERM (GH #75)" 0 0 \
    "missing=[${missing}]" "missing=[]"

# (b) and each INT/TERM handler EXITS. This is the half that matters and the
# half a syntax check nearly missed: `trap 'cleanup' EXIT INT TERM` satisfies
# (a) but runs the handler and RESUMES, so regression.sh deleted its own SD
# clone and carried on for another four minutes producing 58 FAIL / 4 PASS
# against a missing image — failures indistinguishable from a real regression.
# The property that matters is that the process STOPS, and the nearest thing
# to it a source scan can assert is that the handler body calls exit.
#
# A trap listing EXIT alongside INT/TERM is also rejected outright: the same
# body cannot both be the normal-exit handler (which must NOT exit, or it
# recurses) and the signal handler (which must).
noexit=""
for src in "${CLEANUP_SCRIPTS[@]}"; do
    while IFS= read -r line; do
        [[ -z "$line" ]] && continue
        if grep -qE "[[:space:]]EXIT([[:space:]]|$)" <<<"$line"; then
            noexit+="${src##*/}: shares handler with EXIT: ${line}"$'\n'
        elif ! grep -qE "exit[[:space:]]+[0-9]+" <<<"$line"; then
            noexit+="${src##*/}: handler does not exit: ${line}"$'\n'
        fi
    done < <(grep -nE "^[[:space:]]*trap[[:space:]].*[[:space:]](INT|TERM)([[:space:]]|$)" "$src" || true)
done
check "HS-41" "every INT/TERM handler exits instead of resuming (GH #75)" 0 0 \
    "bad=[${noexit}]" "bad=[]"

# (c) and — the one that actually matters — the handler must TERMINATE the
# shell, which no source scan can establish. HS-41 asserts the handler body
# contains `exit <n>`; review broke that in one move with `(exit 130)`, which
# passes the regex and cleans up. Under the real scripts' `set -e` an
# UNGUARDED failing handler command self-terminates the shell via errexit
# (measured for GH #79: the probe dies ~500 ms after the signal, and the real
# harness prints nothing past it), so since the probe was aligned to that
# strict mode it correctly accepts the bare `(exit 130)` shape. The escapes
# that REALLY resume are handlers whose last status is success — a
# cleanup-only handler (the GH #75 incident shape), `(exit 130) || true`
# (still passes HS-41's regex), `; true`, a function call returning 0, a
# trailing `&` — and refining the regex against those is whack-a-mole
# against an unbounded pattern space.
#
# So run the REAL trap statements, lifted verbatim from each script, in a
# minimal shell with stub cleanup functions, signal it, and measure. This is
# viable where the leak test was not: termination is not a narrow race — every
# correct shape dies within a second of the signal and every broken one hangs
# indefinitely, measured across 35+ trials in review. Hermetic and ~1s total.
sig_terminates() {   # sig_terminates <trap-statement> <signal> -> "died=<y|n> cleaned=<y|n>"
    local line=$1 sig=$2
    local marker="$T/sigmarker-$$-$RANDOM" probe="$T/sigprobe-$$-$RANDOM.sh"
    [[ -n "$line" ]] || { echo "died=n cleaned=n"; return; }
    {   echo '#!/usr/bin/env bash'
        echo 'set -euo pipefail'
        echo "TMPDIR_RUN=$(printf %q "$T/sigtmp")"
        echo 'mkdir -p "$TMPDIR_RUN"'
        # One stub for whichever cleanup function this script's trap names.
        echo "unit_cleanup()       { touch $(printf %q "$marker"); return 0; }"
        echo "bench_cleanup()      { touch $(printf %q "$marker"); return 0; }"
        echo "regression_cleanup() { touch $(printf %q "$marker"); return 0; }"
        echo "$line"
        # A LOOP, not a single `sleep`. With one sleep the probe ends the moment
        # the signal kills that sleep, so even a handler that "cleans up and
        # resumes" terminates — the first version of this row did that and
        # passed against all three known-broken shapes. The loop reproduces
        # regression.sh's actual structure: a per-item loop that tolerates its
        # own errors (`|| true`), so a resuming handler keeps grinding. That
        # explicit tolerance is what lets the probe run under the real
        # scripts' full `set -euo pipefail` (GH #79 — it used to drop `-e`
        # instead, diverging from the environment it stands in for): without
        # it, the interrupted sleep's status 130 would trip errexit and even a
        # broken cleanup-and-resume handler would appear to terminate.
        echo 'for _ in $(seq 1 120); do sleep 0.5 || true; done'
    } > "$probe"
    # Measured by WALL TIME, not exit status: `timeout` returns 124 whenever it
    # had to signal at all, so the status says nothing about whether the process
    # then died — that mistake made the first version of this row fail against
    # correct code. --kill-after bounds a handler that never terminates.
    local t0 t1 elapsed
    t0=$(date +%s%N)
    timeout --signal="$sig" --kill-after=3s 0.5s bash "$probe" >/dev/null 2>&1
    t1=$(date +%s%N)
    elapsed=$(( (t1 - t0) / 1000000 ))          # ms
    # A correct handler exits at the signal (~500 ms). A broken one survives it
    # and is SIGKILLed 3s later (~3500 ms). The 2000 ms threshold leaves 1.5 s
    # of headroom either side — deliberately wide, because this project has
    # been burned by tight wall-clock budgets on a loaded box before
    # (audio-underrun-func / screenshot-paused-func, the reason JNEXT_TEST_JOBS
    # is capped). The extra second of runtime is worth not learning that again
    # on a CI runner.
    local died=y; [[ "$elapsed" -gt 2000 ]] && died=n
    local cleaned=n; [[ -e "$marker" ]] && cleaned=y
    rm -f "$marker" "$probe"
    echo "died=$died cleaned=$cleaned"
}

# EVERY registration is probed, not the first one found. run-unit-tests.sh
# arms its INT/TERM traps TWICE by design — once early to cover the preflight
# `die()` paths, then again before the suite loop, which replaces it and is the
# only one live during the parallel `wait` (the window that caused the original
# bug). An earlier version of this row took `grep ... | head -1` and therefore
# probed the early trap and never looked at the live one: review mutated only
# the second registration and got a 16-second hang past HS-40, HS-41 AND HS-42
# with all three green. `tail -1` would fix today's two-layer shape and break
# again silently on a third layer, so the loop probes them all.
sig_results=""
for src in "${CLEANUP_SCRIPTS[@]}"; do
    for sig in INT TERM; do
        n=0
        while IFS= read -r tline; do
            [[ -z "$tline" ]] && continue
            n=$((n + 1))
            r=$(sig_terminates "$tline" "$sig")
            [[ "$r" == "died=y cleaned=y" ]] || sig_results+="${src##*/}/${sig}#${n}: ${r} "
        done < <(grep -E "^[[:space:]]*trap[[:space:]].*[[:space:]]${sig}([[:space:]]|$)" "$src" || true)
        # A script with no registration at all for this signal is itself a
        # failure — HS-40 covers that, but say so here too rather than
        # silently probing nothing and reporting clean.
        [[ "$n" -gt 0 ]] || sig_results+="${src##*/}/${sig}: no registration "
    done
done
check "HS-42" "real INT/TERM trap statements clean up AND terminate (GH #75)" 0 0 \
    "bad=[${sig_results}]" "bad=[]"

# ------------------------------------------- SD clone cleanup BODIES (GH #79)
# HS-42 proves the trap WIRING terminates — with the three cleanup functions
# STUBBED. A defect inside a real cleanup BODY is therefore invisible to it:
# `sleep 300` at the top of the real unit_cleanup() left this whole self-test
# green while every real harness run hung on exit — same observable symptom as
# GH #75, different root cause. So each REAL body is lifted verbatim from its
# script and executed once in a minimal shell:
#   - under the real scripts' `set -euo pipefail`,
#   - bounded by `timeout 10s`, so a hanging body is a FAIL in seconds, never
#     a hang of this self-test,
#   - wall-time asserted well under that bound (2 s threshold vs ~50 ms
#     nominal — the same generous-headroom reasoning as HS-42's),
#   - every directory the body owns must be GONE afterwards (its one job),
#   - a sibling run directory must SURVIVE: concurrent runs are live by
#     design, so cleanup may remove only ITS OWN state, and the shared runs/
#     parent is pruned only when empty.
# Everything the body touches points into a purpose-built fixture (fake $HOME
# included), so the real ~/.jnext is never involved.
#
# Extraction is LOUD: a function that cannot be found, or whose extraction
# looks truncated (no close brace back at column 0, or more than one function
# definition swallowed), yields a tuple no pattern matches — the row FAILS
# rather than quietly probing the wrong text.
cleanup_body_probe() {   # cleanup_body_probe <script> <fn> <var>...
    # -> "rc=<n> fast=<y|n> gone=<y|n> sibling=<y|n>"  |  "extract=FAILED(...)"
    local script=$1 fn=$2; shift 2
    local body first last ndefs
    body=$(sed -n "/^${fn}() {/,/^}/p" "$script")
    first="${body%%$'\n'*}"; last="${body##*$'\n'}"
    ndefs=$(grep -cE '^[A-Za-z_][A-Za-z0-9_]*\(\) \{' <<<"$body" || true)
    if [[ -z "$body" || "$first" != "${fn}() {" || "$last" != "}" || "$ndefs" -ne 1 ]]; then
        echo "extract=FAILED($fn: defs=${ndefs:-0})"
        return 0
    fi
    # Fixture: a fake $HOME holding one directory per variable the body
    # removes (each with a payload file standing in for the ~1 GB clone) plus
    # the sibling. TMP_DIR is the one variable that does NOT live under
    # $HOME/.jnext/runs in reality (it is a mktemp scratch), so its fixture
    # lives outside the fake home too.
    local fh="$T/bodyfix-$fn" probe="$T/bodyprobe-$fn.sh"
    rm -rf "$fh" "$fh-scratch"
    mkdir -p "$fh/.jnext/runs/other-live-run"
    touch "$fh/.jnext/runs/other-live-run/payload.img"
    {   echo '#!/usr/bin/env bash'
        echo 'set -euo pipefail'
        echo "HOME=$(printf %q "$fh")"
    } > "$probe"
    local var d fixtures=()
    for var in "$@"; do
        if [[ "$var" == TMP_DIR ]]; then d="$fh-scratch"; else d="$fh/.jnext/runs/fx-$var"; fi
        mkdir -p "$d"; touch "$d/payload.img"
        fixtures+=("$d")
        echo "$var=$(printf %q "$d")" >> "$probe"
    done
    printf '%s\n' "$body" >> "$probe"
    echo "$fn" >> "$probe"
    local t0 t1 elapsed rc
    t0=$(date +%s%N)
    timeout --kill-after=3s 10s bash "$probe" >/dev/null 2>&1; rc=$?
    t1=$(date +%s%N)
    elapsed=$(( (t1 - t0) / 1000000 ))          # ms
    local fast=y; [[ "$elapsed" -gt 2000 ]] && fast=n
    local gone=y
    for d in "${fixtures[@]}"; do [[ -e "$d" ]] && gone=n; done
    local sibling=n; [[ -e "$fh/.jnext/runs/other-live-run/payload.img" ]] && sibling=y
    rm -rf "$fh" "$fh-scratch" "$probe"
    echo "rc=$rc fast=$fast gone=$gone sibling=$sibling"
}

out=$(cleanup_body_probe "$HARNESS" unit_cleanup UNIT_RUN_DIR)
check "HS-43a" "REAL unit_cleanup body: bounded, removes only its run dir (GH #79)" 0 0 \
    "$out" "rc=0 fast=y gone=y sibling=y"

out=$(cleanup_body_probe "$PROJECT_DIR/test/bench/bench.sh" bench_cleanup BENCH_RUN_DIR)
check "HS-43b" "REAL bench_cleanup body: bounded, removes only its run dir (GH #79)" 0 0 \
    "$out" "rc=0 fast=y gone=y sibling=y"

out=$(cleanup_body_probe "$PROJECT_DIR/test/00regression/test-functions.inc" regression_cleanup RUN_DIR TMP_DIR)
check "HS-43c" "REAL regression_cleanup body: bounded, removes its dirs (GH #79)" 0 0 \
    "$out" "rc=0 fast=y gone=y sibling=y"

echo ""
echo "====================================="
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" "$total" "$pass" "$fail" 0
if [[ "$total" -ne "$EXPECTED_TOTAL" ]]; then
    printf "  FAIL selftest-pin: ran %d checks but EXPECTED_TOTAL pins %d — a check was added or removed without updating the pin\n" \
           "$total" "$EXPECTED_TOTAL"
    exit 2
fi
[[ "$fail" -eq 0 ]] || exit 1
exit 0
