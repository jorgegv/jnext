#!/usr/bin/env bash
# Self-test for packaging-test.sh's FAILURE path (GH #80).
#
# bad() prints a failing sub-test's log inline (fixed in v0.98.101 — before
# that it pointed at a $LOGDIR path its own EXIT trap had already deleted, so
# a CI-only failure was undiagnosable: the summary named the row and the
# evidence was gone). Nothing locked that behaviour in: the failing path is
# exactly what no green run exercises, so a future edit to bad() that dropped
# the dump would pass every existing test. Same class of guard as
# test/harness-selftest.sh: inject the fault, assert the report.
#
# Mechanism: packaging-test.sh self-locates (`cd "$(dirname "$0")/../.."`)
# and invokes its contract sub-tests by repo-relative path, so the REAL,
# committed script is copied byte-for-byte into a throwaway sandbox whose
# test/packaging/ holds stub sub-tests — all green except the one under
# test, which logs a distinctive marker and exits 1. The copy happens at run
# time from this repo checkout; there is NO hand-maintained copy to drift.
# --contracts-only keeps it hermetic and fast: that half never invokes make
# and builds no package.
#
# Wired as a prerequisite of `make package-test` — the exact target CI's
# package job runs — so the failure-reporting path is proven BEFORE the
# ~4 min of package builds, and CI picks it up with zero workflow changes.
#
# Usage: bash test/packaging/packaging-selftest.sh   (or `make packaging-selftest`)

set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
REAL="$REPO/test/packaging/packaging-test.sh"
export LC_ALL=C

[ -f "$REAL" ] || { echo "FATAL: $REAL not found — cannot self-test it"; exit 2; }

pass=0; fail=0; total=0

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

SANDBOX="$T/sandbox"

# The six contract sub-tests `packaging-test.sh --contracts-only` invokes, by
# script name. A new contract sub-test added there without a stub here fails
# the green-path row loudly (the sandbox lacks its script, so its row goes
# FAIL) — update this list when the roster changes; that edit is deliberate,
# the same way a manifest pin is.
SUBTESTS=(
    sync-version-test.sh
    add-release-test.sh
    verify-bundle-test.sh
    prune-plugins-test.sh
    complete-closure-test.sh
    bundle-dlopen-deps-test.sh
)

# reset_sandbox — fresh sandbox holding the REAL script + all-green stubs
reset_sandbox() {
    rm -rf "$SANDBOX"
    mkdir -p "$SANDBOX/test/packaging"
    cp "$REAL" "$SANDBOX/test/packaging/packaging-test.sh"
    local s
    for s in "${SUBTESTS[@]}"; do
        printf '#!/usr/bin/env bash\nexit 0\n' > "$SANDBOX/test/packaging/$s"
    done
}

# stub <script-name> <body-line...> — replace one stub's body
stub() {
    local name=$1; shift
    { echo '#!/usr/bin/env bash'; printf '%s\n' "$@"; } > "$SANDBOX/test/packaging/$name"
}

run_contracts() {
    bash "$SANDBOX/test/packaging/packaging-test.sh" --contracts-only 2>&1
}

# check <id> <desc> <want_rc> <got_rc> <output> <pattern...>
#   pattern    — fixed substring that must be PRESENT
#   !pattern   — fixed substring that must be ABSENT
#   ~line      — a whole line matching exactly must be present
# ('~', not '=': a first draft used '=' and it swallowed the leading '=' of
# the literal pattern '=== Results ==='. No pattern here starts with ~ or !.)
check() {
    local id=$1 desc=$2 want_rc=$3 got_rc=$4 out=$5; shift 5
    total=$((total + 1))
    local ok=1 why="" pat
    if [[ "$got_rc" -ne "$want_rc" ]]; then ok=0; why="exit $got_rc, wanted $want_rc"; fi
    for pat in "$@"; do
        case "$pat" in
            '!'*) if grep -qF -- "${pat#!}" <<<"$out"; then ok=0; why="${why:+$why; }present '${pat#!}'"; fi ;;
            '~'*) if ! grep -qxF -- "${pat#\~}" <<<"$out"; then ok=0; why="${why:+$why; }no exact line '${pat#\~}'"; fi ;;
            *)    if ! grep -qF -- "$pat" <<<"$out"; then ok=0; why="${why:+$why; }missing '$pat'"; fi ;;
        esac
    done
    if [[ "$ok" -eq 1 ]]; then
        pass=$((pass + 1))
        printf "  [PASS] %-6s %s\n" "$id" "$desc"
    else
        fail=$((fail + 1))
        printf "  FAIL %s: %s [%s]\n" "$id" "$desc" "$why"
        printf "%s\n" "$out" | sed -E 's/^/        | /' | head -30
    fi
}

echo "packaging-test.sh self-test (GH #80)"
echo "===================================="
echo ""

# ---------------------------------------------------------------- green path
# All six contract sub-tests pass: exit 0, six PASS rows counted, a summary,
# and NO failure machinery — no FAIL row, no inline log dump.
reset_sandbox
out=$(run_contracts); rc=$?
check "PS-01" "all sub-tests green: exit 0, Pass: 6, summary, no dump" 0 $rc "$out" \
    "Pass: 6" "Fail: 0" "Skip: 0" "=== Results ===" \
    '!FAIL' '!---- end ----'

# ------------------------------------------------ THE FAILING PATH (GH #80)
# One sub-test writes a distinctive root-cause line to its log and exits 1.
# The locked-in behaviour: the FAIL row names the row, the log content
# surfaces INLINE in the captured output (not as a pointer to a temp dir the
# EXIT trap deletes), the run CONTINUES past the failure (the five other
# rows still pass), the summary still prints, and the exit code is nonzero.
# The sub-test's stdout/stderr go to $LOGDIR/<row>.log, so bad()'s dump is
# the ONLY channel through which the marker can reach this captured output.
reset_sandbox
MARKER="PKGSELF_MARKER_c0ffee: the root-cause line that must surface inline"
stub sync-version-test.sh \
    "echo \"$MARKER\"" \
    "exit 1"
out=$(run_contracts); rc=$?
check "PS-02" "a failing sub-test: FAIL row + its log INLINE + run continues" 1 $rc "$out" \
    "FAIL" "sync-version" "contract test failed" \
    "$MARKER" \
    "Pass: 5" "Fail: 1" "=== Results ==="

# --------------------------------- final-newline normalisation in the dump
# bad() pipes the tail through `awk 1` so a log whose last line has NO
# trailing newline cannot glue the "---- end ----" footer onto it. Assert
# the footer still lands on a line of its own in exactly that case (the
# marker's own line carries a '| ' prefix, so only the footer can match the
# exact-line pattern).
reset_sandbox
stub add-release-test.sh \
    "printf 'PKGSELF_NONL_MARKER: last line, no trailing newline'" \
    "exit 1"
out=$(run_contracts); rc=$?
check "PS-03" "a log with NO final newline: footer stays on its own line" 1 $rc "$out" \
    "PKGSELF_NONL_MARKER" \
    "~    ---- end ----"

echo ""
echo "====================================="
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" "$total" "$pass" "$fail" 0
[[ "$fail" -eq 0 ]] || exit 1
exit 0
