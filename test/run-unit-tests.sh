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
cd "$PROJECT_DIR"

# --- This run's PRIVATE SD-image clone (GH #75) ---------------------------
#
# sd_rom_extractor_test reads a real 1 GB SD image. Until GH #75 it read the
# machine-wide master at ~/.jnext/sdcard/ directly, which two sanctioned
# writers can mutate underneath it: any manual jnext run (the image is opened
# read-write and guest CMD24 writes are flushed — GH #77), and regression.sh's
# own sd_rederive repair, which takes a flock this harness does not observe.
# A torn read does not fail loudly as "the image is busy"; it fails as a
# handful of red rows that read like an emulator regression.
#
# So: clone the master into a per-run directory and point the suite at it via
# JNEXT_TEST_SD_IMAGE, which the test already honours. Same reflink-first
# approach as the regression harness (test/00regression/test-functions.inc):
# on a copy-on-write filesystem the clone is ~60 ms and costs no space until
# written, and $HOME is used rather than /tmp because reflink cannot cross a
# filesystem — and --reflink=auto would silently degrade to a real 1 GB copy
# into tmpfs.
#
# A missing master is NOT fatal here. The suite has its own honest refusal for
# that case and the rest of the run is unaffected; failing the whole harness
# because one fixture is unprovisioned would be a worse trade.
UNIT_RUN_DIR=""
unit_cleanup() {
    [[ -n "$UNIT_RUN_DIR" ]] && rm -rf "$UNIT_RUN_DIR"
    rmdir "$HOME/.jnext/runs" 2>/dev/null || true
    return 0
}
# Armed HERE, before the clone exists, because every preflight `die()` exits
# long before the run-phase trap further down is installed — and the harness
# self-test drives those refusal paths a dozen times per invocation. Relying on
# the later trap alone leaked a clone per refusal (measured: 12 in one
# regression run). The later trap REPLACES this one and therefore has to call
# unit_cleanup itself; see "ONE trap" there.
#
# INT and TERM are listed EXPLICITLY. An EXIT-only trap does not fire when the
# shell is killed by a signal it has not trapped, and `make unit-test` runs
# this script as a foreground recipe in the terminal's process group — so a
# Ctrl-C during the parallel `wait` leaked the whole 1 GB clone. Measured on
# the EXIT-only version: 20% of SIGINTs and 45% of SIGTERMs delivered mid-run.
# `kill` with no signal is TERM, which is the worse of the two.
#
# WHY THE SIGNAL HANDLERS EXIT EXPLICITLY. `trap 'cleanup' EXIT INT TERM` is
# NOT a general fix and was rejected in review after being tried: catching a
# signal you have an explicit trap for runs the handler and then RESUMES, so a
# handler that only cleans up leaves the script running without the thing it
# just deleted. In regression.sh that turned a Ctrl-C into 58 FAIL / 4 PASS
# against a deleted SD clone — output indistinguishable from a real emulator
# regression, far worse than the leaked directory it was fixing. Two of the
# three scripts happened to die anyway through incidental control flow (a
# killed process group tripping `set -e`; a synchronous foreground pipeline
# taking the signal itself); that is luck, not design, and a future edit would
# silently remove it. So INT/TERM terminate explicitly, 128+signo.
trap 'unit_cleanup' EXIT
trap 'unit_cleanup; exit 130' INT
trap 'unit_cleanup; exit 143' TERM

# reflink first and EXPLICITLY (`--reflink=always`), so the outcome is
# observable in $SD_CLONE_MODE rather than guessed — same rule as the
# regression harness. On btrfs the clone is instant and costs no space until
# written; anywhere else it degrades to a real copy, which still lands beside
# the master rather than in tmpfs.
#
# Called only once preflight has PASSED, never at parse time: a refusal never
# runs a suite, so cloning for one is pure waste — and the harness self-test
# drives ~30 refusals per invocation.
SD_CLONE_MODE=none
sd_clone_for_run() {
    if [[ -n "${JNEXT_TEST_SD_IMAGE:-}" ]]; then SD_CLONE_MODE=preset; return 0; fi
    local sd_master="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"
    [[ -f "$sd_master" ]] || { SD_CLONE_MODE=absent; return 0; }

    UNIT_RUN_DIR="$HOME/.jnext/runs/unit-$$-$RANDOM"
    local sd_clone="$UNIT_RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    if mkdir -p "$UNIT_RUN_DIR/sdcard" 2>/dev/null; then
        if   cp --reflink=always "$sd_master" "$sd_clone" 2>/dev/null; then SD_CLONE_MODE=reflink
        elif cp --reflink=auto   "$sd_master" "$sd_clone" 2>/dev/null; then SD_CLONE_MODE=copy
        else SD_CLONE_MODE=failed; fi
    else
        SD_CLONE_MODE=failed
    fi

    if [[ "$SD_CLONE_MODE" == failed ]]; then
        # Do not silently fall back to the shared master — say so, so the
        # suite reads it knowingly rather than by accident.
        rm -rf "$UNIT_RUN_DIR"; UNIT_RUN_DIR=""
        printf "  WARNING: could not clone the SD master; suites needing it will read %s directly\n" \
               "$sd_master" >&2
    else
        export JNEXT_TEST_SD_IMAGE="$sd_clone"
    fi
    return 0
}

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
# ONE trap for the whole script. `trap ... EXIT` REPLACES any previous EXIT
# handler, so this must clean up the SD clone too — installing a second trap
# here silently disabled the clone's cleanup and leaked a 1 GB directory per
# run (caught in testing: 34 of them, one per harness-selftest invocation).
# INT/TERM for the same reason as the earlier trap: this is the window the
# suites actually run in, so it is where a Ctrl-C most often lands. They exit
# explicitly — see the note on the earlier trap.
trap 'rm -rf "$TMPDIR_RUN"; unit_cleanup' EXIT
trap 'rm -rf "$TMPDIR_RUN"; unit_cleanup; exit 130' INT
trap 'rm -rf "$TMPDIR_RUN"; unit_cleanup; exit 143' TERM

# Preflight has passed and suites are about to run — now the clone is worth
# making.
sd_clone_for_run

# Say which SD image the suites that need one will read, and how it was
# obtained. Silence here would make "isolated clone" and "the shared master,
# because the clone failed" look identical.
case "$SD_CLONE_MODE" in
    reflink|copy) printf "  ${CYAN}sd-clone${RESET} private %s clone for this run\n" "$SD_CLONE_MODE" ;;
    preset)       printf "  ${CYAN}sd-clone${RESET} skipped — JNEXT_TEST_SD_IMAGE preset by the caller\n" ;;
    absent)       printf "  ${CYAN}sd-clone${RESET} skipped — no SD master to clone\n" ;;
    failed)       printf "  ${CYAN}sd-clone${RESET} FAILED — suites will read the shared master\n" ;;
esac

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
