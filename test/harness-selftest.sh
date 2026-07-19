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
# build/debug and build/gui-debug live INSIDE build/ and configure with ENABLE_TESTS=ON.
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
# the real tree is never touched.
mkdir -p "$T/scripts"
cp "$PROJECT_DIR"/test/00regression/scripts/*.sh "$T/scripts/"
echo '#!/usr/bin/env bash' > "$T/scripts/bogus-func.sh"
out=$(JNEXT_REGRESSION_CONF="$REG_CONF" JNEXT_REGRESSION_FUNC_CONF="$REG_FUNC" \
      JNEXT_REGRESSION_SCRIPTS_DIR="$T/scripts" bash "$REG" --preflight-only 2>&1); rc=$?
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

echo ""
echo "====================================="
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" "$total" "$pass" "$fail" 0
[[ "$fail" -eq 0 ]] || exit 1
exit 0
