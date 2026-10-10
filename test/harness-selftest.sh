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
EXPECTED_TOTAL=190  # 160 + HS-90a..e, HS-91a..g, HS-92a..b, HS-93a..d, HS-94a..f (GH #319: --platform, os= tags, per-OS pins, the wine runner; 30 rows) -> see below; 75 + HS-82..88d (19 rows: the target OS, `# os:`, .exe/runner, userland preflight, job cap; GH #214) + HS-81a..b + HS-78, HS-79, HS-80a..h (tool-missing / version-gap FAIL pins) + HS-76a..b + HS-77a..d (a SKIP fails the run, 2026-10-06) + HS-68a..e (the sourced-row counter guard) + HS-69a..o, HS-70a..e, HS-71a..p, HS-72, HS-73, HS-74a..c, HS-75a..b (GH #295)

# Per-invocation bound on every end-to-end run of a REAL script (GH #81).
# run_harness and run_preflight each execute a real harness end to end, and a
# hanging fault anywhere in one — a slow cleanup body, a slow injected fault —
# used to hang this whole self-test at its first row, long before the bounded
# HS-43 probes were even reached (GH #79's verifiers had to shrink that
# issue's literal `sleep 300` to keep mutation testing tractable). Bounded per
# invocation, a hang is one loud timeout FAIL at the affected row — check()
# prints the rc: 124, or 137 when only the follow-up SIGKILL could end it
# (e.g. a TERM-trapped cleanup that just hangs again) — and the run
# continues. 120 s, because the slowest legitimate invocation is no longer
# HS-04 (~2.1 s): the HS-49/56/57/67 rows run the whole six-lint preflight,
# ~4.7 s solo, and since GH #295 this self-test runs inside the regression's
# parallel phase — where HS-57b's preflight (a C++ preprocessing pass per
# published header) outlived the old 30 s bound at load 13 on 12 CPUs (exit
# 124). It is a hang bound, not a budget: wide on purpose, this project has
# been burned by tight wall-clock budgets on loaded boxes.
# HS-44 proves the bound fires; INVOKE_TIMEOUT_OVERRIDE is its hook (same
# pattern as HS-04's TIMEOUT_OVERRIDE) so proving it costs ~3 s, not 120.
INVOKE_TIMEOUT=120

# EVERY external tool a check's evidence depends on, verified before any check
# runs. A missing tool must be a loud refusal, never evidence: HS-70e once
# asked `pgrep`/`ps` (procps-ng, absent from the fedora:44 CI container) for
# the process it was about to signal, both failed silently behind `|| true`,
# and the row reported a broken harness that was not broken (GH #295 CI). With
# a tool missing from an INCOMPLETE list, the shape is the same, only louder:
# 45 rows "exit 127, wanted 0" (review of the first version of this list).
#
# HOW THIS LIST WAS DERIVED — and how to re-derive it after changing any code
# a check drives (this file, run-unit-tests.sh, regression.sh and its .inc
# files, regression-stamp.sh, the lints): trace every program the self-test
# actually executes, with the binaries built, and take the basenames:
#
#   strace -f -qq -e trace=execve -e status=successful -o hs.trace \
#       bash test/harness-selftest.sh
#   grep -oE 'execve\("[^"]+"' hs.trace | sed 's/execve("//; s/"$//; s|.*/||' | sort -u
#
# then drop the stub suites the self-test writes itself (*_test). Two
# substitutions: `cc1plus` is gcc's own (c++ stays), and `hostname` — absent
# from fedora:44 — is only a fallback-guarded call in regression-stamp.sh,
# whose fallback is `uname`. Process lookups read /proc (proc_* below), so
# procps is deliberately NOT here. No row re-derives the list on every run:
# strace is not installed in CI and roughly triples the self-test's time, and
# a static scan for command names in the scripts both misses tools (built
# argv, `env`/`timeout`/`xargs` wrappers) and flags prose — re-run the trace.
#
# A tool counts as present only if an EXECUTABLE of that name is on PATH
# (`type -P`, so a shell builtin such as `true`/`test` does not stand in for
# the program `flock ... true` runs) and running it does not end in 126/127,
# the shell's "cannot execute" / "not found". That also refuses a stub or a
# dangling wrapper that only pretends to be the tool.
tool_runnable() {   # tool_runnable <name>
    local p rc
    p=$(type -P "$1") || return 1
    "$p" --version </dev/null >/dev/null 2>&1; rc=$?
    [[ $rc -ne 126 && $rc -ne 127 ]]
}
missing_tools=""
for tool in awk basename bash c++ cat chmod comm cp cut date dirname env find flock git grep \
            head ln ls make mkdir mktemp mv nproc perl rg rm rmdir sed seq sh sha256sum sleep \
            sort stat tail test timeout touch tr true uname uniq wc xargs; do
    tool_runnable "$tool" || missing_tools+=" $tool"
done
[[ -r /proc/self/stat && -r /proc/self/cmdline ]] || missing_tools+=" /proc"
if [[ -n "$missing_tools" ]]; then
    printf "  REFUSE harness-selftest: required tool(s) missing:%s — install them; a check cannot report evidence it has no tool to collect\n" "$missing_tools"
    exit 2
fi

# /proc readers, so no check depends on procps-ng (ps/pgrep).
proc_args() {   # proc_args <pid> — argv joined by single spaces; "" if gone
    local a
    a=$(tr '\0' ' ' 2>/dev/null < "/proc/$1/cmdline") || return 0
    echo "${a% }"
}
proc_ppid() {   # proc_ppid <pid> — the parent pid; "" if gone
    local s
    s=$(cat "/proc/$1/stat" 2>/dev/null) || return 0
    s=${s##*) }               # past "(comm) ": comm may itself contain spaces
    set -- $s                 # $1 = state, $2 = ppid
    echo "${2:-}"
}
proc_comm() { cat "/proc/$1/comm" 2>/dev/null || true; }   # proc_comm <pid>
proc_with_args() {   # proc_with_args <exact argv string> — the pids running it
    local d pid
    for d in /proc/[0-9]*; do
        pid=${d#/proc/}
        [[ "$(proc_args "$pid")" == "$1" ]] && echo "$pid"
    done
    return 0
}

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# The harness reads the build's configuration out of its CMakeCache.txt to decide
# which `# gate:`d suites this build owes it (GH #273), so every fake build tree
# needs one. Default: the canonical configuration, both options ON, under which
# every gate is satisfied and the pre-#273 rows below behave exactly as they did.
ensure_cache() {
    [[ -f "$T/build/CMakeCache.txt" ]] || cache ON ON
}
# cache <qt> <dbg> [os] — (re)write the fake build tree's configuration. The
# target OS defaults to linux, under which every `# os:` is satisfied (GH #214).
cache() {
    mkdir -p "$T/build"
    { echo "ENABLE_QT_UI:BOOL=$1"; echo "ENABLE_DEBUGGER:BOOL=$2"
      echo "JNEXT_TARGET_OS:INTERNAL=${3:-linux}"; } > "$T/build/CMakeCache.txt"
}

# lit_ids <n> — n `report "ROW-i"` lines, each ID spelled out as a literal in the
# stub's text, the way a real suite spells its IDs in its source (the harness
# checks that every reported ID is a literal of the suite's sources, and a stub's
# source is the stub itself — see register).
lit_ids() { local i; for ((i = 1; i <= $1; i++)); do printf 'report "ROW-%d"\n' "$i"; done; }

# stub <name> <rows> <exit_code> [body]  — a fake suite binary. Like every real
# suite (test/row_id.h), it reports one distinct row ID per row it counts; a body
# that prints its own `Total:` line reports its own IDs (`report "ID"`, lit_ids).
stub() {
    local name=$1 rows=$2 rc=$3 body=${4:-}
    mkdir -p "$T/build/test"
    ensure_cache
    { echo '#!/usr/bin/env bash'
      echo 'report() { [[ -z "${JNEXT_TEST_ROW_IDS:-}" ]] || echo "$1" >>"$JNEXT_TEST_ROW_IDS"; }'
      [[ -n "$body" ]] && echo "$body"
      if [[ "$rows" -ge 0 ]]; then
          lit_ids "$rows"
          printf 'echo "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d"\n' \
                 "$rows" "$rows" 0 0
      fi
      echo "exit $rc"
    } > "$T/build/test/$name"
    chmod +x "$T/build/test/$name"
}

# register <name...> — write a CTestTestfile.cmake naming exactly these binaries,
# and the suite -> sources map the harness checks row IDs against
# (JNEXT_UNIT_TEST_SOURCES): each stub is its own source.
register() {
    ensure_cache
    : > "$T/build/test/CTestTestfile.cmake"
    : > "$T/sources.tsv"
    for n in "$@"; do
        echo "add_test(${n}s \"$T/build/test/$n\")" >> "$T/build/test/CTestTestfile.cmake"
        printf '%s\t%s\n' "$n" "$T/build/test/$n" >> "$T/sources.tsv"
    done
}

# manifest <line...> — write the manifest, with a matching `# expect: N` pin.
# `# gate:` directives may be passed as lines too; they are not suites, so they
# do not count towards the pin.
manifest() {
    local n=0 l
    for l in "$@"; do [[ "$l" == \#* ]] || n=$((n + 1)); done
    { echo "# expect: $n"; printf '%s\n' "$@"; } > "$T/manifest.conf"
}
# manifest_pinned <pin> <line...> — same, but with a deliberately chosen pin
manifest_pinned() {
    local pin=$1; shift
    { echo "# expect: $pin"; printf '%s\n' "$@"; } > "$T/manifest.conf"
}

run_harness() {
    JNEXT_UNIT_TEST_CONF="$T/manifest.conf" JNEXT_UNIT_TEST_SOURCES="$T/sources.tsv" \
    JNEXT_SUITE_TIMEOUT="${TIMEOUT_OVERRIDE:-300}" \
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

# ---------------------------------------------------- a SKIP fails the run
# Owner decision 2026-10-06: a row that was not tested is not a pass. A suite
# with Skipped > 0 keeps its SKIP badge (it says what to install) and prints its
# SKIP lines, but the run exits 1 and is not counted as a passing suite.
stub skipping_test -1 0 "$(lit_ids 10)"$'\n''echo "  [SKIP] ROW-3: stub"'$'\n''echo "Total:   10  Passed:    9  Failed:    0  Skipped:    1"'
register skipping_test
manifest "skipping_test 10"
out=$(run_harness); rc=$?
check "HS-76a" "a suite reporting a SKIP fails the run, names the suite and the SKIP row, and is not a passing suite" 1 $rc "$out" \
    "SKIP" "skipping_test" "ROW-3" "a SKIP is not a pass" "Suites: 0 pass, 0 fail, 1 skipped"

stub failing_test -1 1 "$(lit_ids 10)"$'\n''echo "Total:   10  Passed:    9  Failed:    1  Skipped:    0"'
register skipping_test failing_test
manifest "skipping_test 10" "failing_test 10"
out=$(run_harness); rc=$?
check "HS-76b" "a SKIPping suite beside a FAILING one: both are named, both counted" 1 $rc "$out" \
    "skipping_test" "failing_test" "ROW-3" "Suites: 0 pass, 1 fail, 1 skipped"

# ------------------------------------------------- THE BUG THAT SHIPPED (a)
# A suite that FAILS (valid summary, non-zero exit). The failing row must be
# reported, every later suite must still be reported, and the grand total must
# still print. Before the fix: the runner subshell died under `set -e` without
# writing its .rc, the aggregator's `cat` failed, and the run aborted here.
stub failing_test -1 1 "$(lit_ids 10)"$'\n''echo "Total:   10  Passed:    9  Failed:    1  Skipped:    0"'
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

# ------------------------------------------------ row IDs (test/row_id.h)
# An ID is a global name: a suite that reports one ID for two rows puts ONE row in
# the traceability matrix and two in its count — GH #190's manufactured coverage,
# inside one suite, which traceability-dup-ids.pl (suite vs suite) cannot see. The
# source cannot answer it either (the same literal legitimately appears in a table
# and at its check()), so the harness reads what the run REPORTED. The duplicate
# carries a space on purpose: an anonymous row (rewind_test's CHECK) reports its
# message, and the whole line is the ID.
row_ids_body() {   # row_ids_body <rows> <id>... — count <rows> rows, report exactly these IDs
    local rows=$1 b='' id; shift
    for id in "$@"; do b+="report \"$id\"; "; done
    printf '%secho "Total: %4d  Passed: %4d  Failed:    0  Skipped:    0"' "$b" "$rows" "$rows"
}
stub dup_test -1 0 "$(row_ids_body 5 DUP-01 'a CHECK message' OTHER-01 DUP-01 'a CHECK message')"
stub other_test 5 0
register dup_test other_test
manifest "dup_test 5" "other_test 5"
out=$(run_harness); rc=$?
check "HS-58" "a suite reporting one row ID for two rows FAILS, naming every such ID" 1 $rc "$out" \
    "dup_test" "FAIL" "the same row ID more than once" "DUP-01  (x2)" "a CHECK message  (x2)" \
    "Suites: 1 pass, 1 fail"

# The control, from the same fixture with the IDs made distinct: without it HS-58
# would also pass on a harness that refused every suite reporting IDs at all.
stub dup_test -1 0 "$(row_ids_body 5 DUP-01 'a CHECK message' OTHER-01 DUP-01b 'a CHECK message b')"
out=$(run_harness); rc=$?
check "HS-59" "the control: the same suite with distinct row IDs passes" 0 $rc "$out" \
    "Total: 10  Passed: 10  Failed: 0  Skipped: 0" "Suites: 2 pass, 0 fail"

# The count, both directions. FEWER is the unwired row helper: it still counts its
# row but reports nothing, and a duplicate check alone would call that clean.
stub unwired_test -1 0 "$(row_ids_body 4 ROW-1 ROW-2 ROW-3)"
register unwired_test other_test
manifest "unwired_test 4" "other_test 5"
out=$(run_harness); rc=$?
check "HS-60" "a suite reporting FEWER row IDs than rows FAILS (an unwired row helper)" 1 $rc "$out" \
    "unwired_test" "FAIL" "reported 3 row ID(s) for 4 rows" "Suites: 1 pass, 1 fail"

# MORE: a report from something that is not a row, which would let a real row's
# ID be missing while the file still looked full.
stub unwired_test -1 0 "$(row_ids_body 4 ROW-1 ROW-2 ROW-3 ROW-4 EXTRA-01)"
out=$(run_harness); rc=$?
check "HS-61" "a suite reporting MORE row IDs than rows FAILS" 1 $rc "$out" \
    "unwired_test" "FAIL" "reported 5 row ID(s) for 4 rows" "Suites: 1 pass, 1 fail"

# A suite that fork()s before its first report, run through the REAL test/row_id.h
# (build/test/row_id_fork_probe — a stub cannot stand in for it, the property is
# the header's). Parent and child each open the ID file themselves; a truncating
# open ("w") would drop the child's row and this clean suite would FAIL on the
# count. A missing probe is a FAIL here, never a skip: `make harness-selftest`
# builds the test tree first.
if [[ -x "$PROJECT_DIR/build/test/row_id_fork_probe" ]]; then
    cp "$PROJECT_DIR/build/test/row_id_fork_probe" "$T/build/test/fork_test"
    register fork_test other_test
    printf 'fork_test\t%s\n' "$PROJECT_DIR/test/row_id_fork_probe.cpp" >> "$T/sources.tsv"
    manifest "fork_test 3" "other_test 5"
    out=$(run_harness); rc=$?
else
    out="build/test/row_id_fork_probe is not built"; rc=99
fi
check "HS-62" "a fork()ing suite's child and parent rows are all counted (row_id.h appends)" 0 $rc "$out" \
    "Total: 8  Passed: 8  Failed: 0  Skipped: 0" "Suites: 2 pass, 0 fail"

# Every reported row ID must be a LITERAL of the suite's sources (CLAUDE.md "A row
# ID must be a LITERAL"): an ID built at run time is a row no source reader can
# see. The stub's source holds the literal "RUN-0$n"; the ID it reports is RUN-02.
stub rt_test -1 0 'report "LIT-01"; n=2; report "RUN-0$n"
echo "Total:    2  Passed:    2  Failed:    0  Skipped:    0"'
stub other_test 5 0
register rt_test other_test
manifest "rt_test 2" "other_test 5"
out=$(run_harness); rc=$?
check "HS-63" "a suite reporting a row ID that is not a literal in its sources FAILS, naming it" 1 $rc "$out" \
    "rt_test" "FAIL" "not a literal in its sources" "RUN-02" "Suites: 1 pass, 1 fail"

# The declared exemption: a suite whose IDs are DATA (fuse_z80_test, z80n_test —
# case names from a checked-in fixture) passes the literal check, and is NAMED.
stub fuse_z80_test -1 0 'n=1; report "CASE-0$n"; report "CASE-0$((n + 1))"
echo "Total:    2  Passed:    2  Failed:    0  Skipped:    0"'
register fuse_z80_test other_test
manifest "fuse_z80_test 2" "other_test 5"
out=$(run_harness); rc=$?
check "HS-64" "an exempt data-ID suite passes the literal check, and the exemption is named" 0 $rc "$out" \
    "exempt from the row-ID literal check" "fuse_z80_test (case names from test/fuse/tests.in)" \
    "Suites: 2 pass, 0 fail"

# ...and an exemption that is no longer needed is refused, not carried forever.
stub fuse_z80_test -1 0 'report "CASE-01"; report "CASE-02"
echo "Total:    2  Passed:    2  Failed:    0  Skipped:    0"'
out=$(run_harness); rc=$?
check "HS-65" "an exempt suite whose IDs are all literals FAILS: drop the exemption" 1 $rc "$out" \
    "fuse_z80_test" "FAIL" "drop the exemption"

# A runnable suite with no sources to check its IDs against is a refusal, never a
# silently skipped check.
register good_test other_test
printf 'good_test\t%s\n' "$T/build/test/good_test" > "$T/sources.tsv"
manifest "good_test 10" "other_test 5"
out=$(run_harness); rc=$?
check "HS-66" "a runnable suite with no known sources is a refusal (the literal check cannot be skipped)" 2 $rc "$out" \
    "REFUSES TO RUN" "No sources known for" "other_test"

# --------------------------------------------------- build-gated suites (GH #273)
# '# gate: qt|dbg|qt+dbg' + '?name' says WHICH configurations own a suite, and the
# harness reads the configuration from the build tree's own CMakeCache.txt. So
# absence is CHECKED, not excused: expected where the gate is off, a refusal where
# it is on. Before #273 the '?' alone meant "skip it quietly if CMake did not
# register it", which is the same silent shrinking the manifest exists to forbid —
# HS-13a is the hole that closed.
stub good_test 10 0
register good_test                       # gated_test NOT registered
cache OFF OFF                            # ...and this configuration does not own it
manifest "good_test 10" "# gate: qt" "?gated_test 7"
out=$(run_harness); rc=$?
check "HS-12" "a suite gated out by this configuration: NOTICE, not silence" 0 $rc "$out" \
    "NOTICE" "gated out" "gated_test" "Suites: 1 pass, 0 fail" "ENABLE_QT_UI=OFF"

stub gated_test 7 0
register good_test gated_test            # owned by this configuration AND registered
cache ON ON
manifest "good_test 10" "# gate: qt" "?gated_test 7"
out=$(run_harness); rc=$?
check "HS-13" "a gated suite whose configuration owns it is run like any other" 0 $rc "$out" \
    "gated_test" "Total: 17"

register good_test                       # owned by this configuration but GONE from CMake
cache ON ON
manifest "good_test 10" "# gate: qt" "?gated_test 7"
out=$(run_harness); rc=$?
check "HS-13a" "a suite MISSING from the configuration that owns it is a refusal" 2 $rc "$out" \
    "REFUSES TO RUN" "gated_test" "NOT registered by CMake"

stub gated_test 7 0
register good_test gated_test            # present although the gate excludes it
cache OFF OFF
manifest "good_test 10" "# gate: qt" "?gated_test 7"
out=$(run_harness); rc=$?
check "HS-13b" "a suite PRESENT although its gate excludes it is a refusal" 2 $rc "$out" \
    "REFUSES TO RUN" "gated_test" "gates it to"

register good_test
cache ON ON
manifest "good_test 10" "# gate: qt" "gated_test 7"   # under a gate, but no '?'
out=$(run_harness); rc=$?
check "HS-13c" "a gated suite without the '?' marker is a refusal (the two must agree)" 2 $rc "$out" \
    "REFUSES TO RUN" "not marked"

register good_test
cache ON ON
manifest "?good_test 10"                              # '?' with no gate in force
out=$(run_harness); rc=$?
check "HS-13d" "a '?' suite under 'gate: none' is a refusal (the two must agree)" 2 $rc "$out" \
    "REFUSES TO RUN" "gate: none"

register good_test
cache ON ON
manifest "good_test 10" "# gate: gui" "?gated_test 7" # not one of the four gates
out=$(run_harness); rc=$?
check "HS-13e" "an unknown gate name is a refusal, not a gate that never applies" 2 $rc "$out" \
    "REFUSES TO RUN" "Unknown gate"

stub good_test 10 0
register good_test
rm -f "$T/build/CMakeCache.txt"          # configured tree, unreadable configuration
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-13f" "a build tree whose configuration cannot be read is a refusal, not a guess" 2 $rc "$out" \
    "REFUSES TO RUN" "CMakeCache.txt"
cache ON ON

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
# The fillers exist to make the DECLARED list huge; they are not registered, so
# they go under a gate this configuration does NOT satisfy (GH #273) — which is
# the only way a declared suite may legitimately be absent.
cache ON OFF
filler_a="pad_a_$(head -c 200000 /dev/zero | tr '\0' 'a')"
filler_b="pad_b_$(head -c 200000 /dev/zero | tr '\0' 'b')"
manifest "good_test 10" "# gate: dbg" "?$filler_a 1" "?$filler_b 1"
out=$(run_harness); rc=$?
check "HS-27" "manifest/build AGREEMENT is never reported as drift (no SIGPIPE race)" 0 $rc "$out" \
    "Total: 10  Passed: 10  Failed: 0  Skipped: 0" "Suites: 1 pass, 0 fail"
cache ON ON

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

# ------------------------- the lint gate must stay WIRED to `make unit-test` (GH #129)
# test/lint-assertions.sh self-tests its own patterns on every invocation, so it can
# prove it still DETECTS a tautology. Nothing proved it still RUNS — and that is the
# defect: reachable only through the regression preflight, the lint never saw an
# author's `make unit-test` loop, so a check(..., true) row passed its author, passed
# an independent reviewer who mutation-tested everything around it, and turned CI red
# only after landing on main, from where three more branches inherited it. Deleting
# one word from the Makefile prerequisite restores exactly that state with every suite
# still green, which is the same "guard silently un-wired" shape as HS-30/HS-40.
#
# Probed BEHAVIOURALLY with `make -n` (a dry run: builds nothing, ~10 ms) rather than
# by grepping the Makefile text, so the row survives the wiring being expressed
# differently and fails only when the lint genuinely stops being reached. The pattern
# is anchored to the command form: recipe `@#` comment lines ARE printed under -n, so
# an unanchored match on the name could be satisfied by a comment alone.
#
# TWO facts, because reachability alone is not the property this gate sells. Moving
# the prerequisite to LAST keeps wired=y — the lint still runs, but only after the
# compile it exists to spare you, which is the whole headline ("fails in ~40 ms
# before any build"). So `first` pins that NOTHING precedes it.
#
# `first` rather than "precedes the `cmake --build` line", deliberately. It is the
# strictly stronger claim (first implies before-everything, so a demotion to any
# later slot is caught, not just a demotion past the build), and — the reason that
# matters here — it needs NO second text anchor: it is positional on the lint line
# itself. Anchoring on the build line would re-couple this row to recipe text
# (`$(CMAKE) --build build -j$(JOBS)`, which renders host-dependently as
# `cmake --build build -j12`), so a harmless rewording would turn the row red for a
# reason unrelated to the gate. That is the same argument that put this probe on
# `make -n` instead of a Makefile grep, applied once more.
#
# A deliberate future gate placed ahead of the lint therefore fails this row and must
# update it — the pin philosophy this file already runs on (EXPECTED_TOTAL, the
# manifest row counts, the conf `# expect:` lines): the claim is made on purpose.
#
# `first` is the first COMMAND, not the first printed line: `make -n` echoes recipe
# `@#` comments (they are suppressed only in a real run), and a comment is not a gate
# that can precede anything. Counting them made the row red the moment GH #140 moved
# lint-assertions' rationale into its recipe — a harmless rewording turning the row red
# for a reason unrelated to the gate, the exact failure the paragraph above rejects.
# A real gate placed ahead is still a non-comment line, so nothing is weakened.
#
# MAKEFLAGS is cleared because this self-test itself runs FROM a make recipe (`make
# harness-selftest`, and harness-selftest-func inside the regression suite): an
# inherited jobserver flag makes the nested make warn, and inherited goals would
# change what is being probed. Bounded like every other real invocation here (GH #81).
LINT_CMD_RE='^[[:space:]]*bash[[:space:]]+test/lint-assertions\.sh'
dry=$(MAKEFLAGS= timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
          make -C "$PROJECT_DIR" --no-print-directory -n unit-test 2>&1 || true)
dry_head=$(grep -vE '^[[:space:]]*(#|$)' <<<"$dry" | head -1)
wired=n; grep -qE "$LINT_CMD_RE" <<<"$dry"      && wired=y
first=n; grep -qE "$LINT_CMD_RE" <<<"$dry_head" && first=y
# The offending first line rides along in the tuple: on a demotion the row must say
# WHAT ran first, or the diagnosis costs a manual dry run. Truncated and newline-free
# so one pathological recipe line cannot swamp the report.
check "HS-45" "'make unit-test' reaches the lint, and reaches it FIRST (GH #129)" 0 0 \
    "wired=$wired first=$first head=[$(cut -c1-60 <<<"${dry_head//$'\n'/ }")]" \
    "wired=y" "first=y"

# ------------------ the help-listing lint must stay wired to BOTH entry points (GH #140)
# Same exposure as HS-45, one gate later: `make` (no target) is the project's index of
# targets, and eight of them advertised a sentence fragment because a rationale comment
# below the description silently replaced it. test/lint-makefile-help.sh self-tests its
# own scanner on every run, so it can prove it still DETECTS the shape; nothing but this
# row proves it still RUNS. Deleting one word from either prerequisite list restores the
# un-gated state with every suite green — the GH #129 shape exactly.
#
# Both entry points, because they are wired independently: `make unit-test` is the
# author's inner loop and `make regression` is what CI and the merge protocol run.
# `head2` (first two COMMAND lines of the unit-test dry run) pins that it is not
# demoted behind the build, without anchoring on any build-command text — the two
# cheap linters may swap with each other, and nothing else may precede them.
MKHELP_CMD_RE='^[[:space:]]*bash[[:space:]]+test/lint-makefile-help\.sh'
dry_reg=$(MAKEFLAGS= timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
              make -C "$PROJECT_DIR" --no-print-directory -n regression 2>&1 || true)
dry_head2=$(grep -vE '^[[:space:]]*(#|$)' <<<"$dry" | head -2)
u_wired=n; grep -qE "$MKHELP_CMD_RE" <<<"$dry"       && u_wired=y
r_wired=n; grep -qE "$MKHELP_CMD_RE" <<<"$dry_reg"   && r_wired=y
early=n;   grep -qE "$MKHELP_CMD_RE" <<<"$dry_head2" && early=y
check "HS-46" "the help-listing lint is wired to unit-test AND regression, ahead of the build (GH #140)" 0 0 \
    "unit=$u_wired regression=$r_wired early=$early" \
    "unit=y" "regression=y" "early=y"

# ------------- worktree-bootstrap must not call an unprovisioned tree "ready" (GH #149)
# `git worktree add` does not populate submodules, so a fresh worktree has an EMPTY
# third_party/spdlog — and the target CLAUDE.md sends every agent through first printed
# "worktree-bootstrap: ready." over it. The build usually self-heals (CMakeLists.txt
# inits submodules at configure time), which is exactly why nothing else catches this:
# it fails only when that fetch cannot run — offline, sandboxed, or a transient GitHub
# failure — and CMake's init failure is a message(WARNING), so configure then dies at
# add_subdirectory(third_party/spdlog). The claim under test is the readiness verdict,
# not the build.
#
# The REAL Makefile runs against a throwaway fixture tree (`make -C <fixture> -f <real
# Makefile>`), the way the packaging sub-tests sandbox the real scripts: there is no
# hand-maintained copy of the recipe to drift. HOME is redirected at a fixture SD master
# so the SD branch stays "ok" and the verdict line reflects the submodule alone.
# MAKEFLAGS is cleared and the invocation bounded for the same reasons as HS-45/46.
WTB="$T/wtb"
wtb_fixture() {   # wtb_fixture <populated:y|n>
    rm -rf "$WTB"
    mkdir -p "$WTB/roms" "$WTB/third_party/spdlog" "$WTB/home/.jnext/sdcard"
    : > "$WTB/roms/nextboot.rom"
    : > "$WTB/home/.jnext/sdcard/cspect-next-1gb-fixed.img"
    printf '[submodule "third_party/spdlog"]\n\tpath = third_party/spdlog\n\turl = https://example.invalid/spdlog.git\n' \
        > "$WTB/.gitmodules"
    [[ "$1" == y ]] && : > "$WTB/third_party/spdlog/CMakeLists.txt"
    return 0
}
run_wtb() {
    MAKEFLAGS= HOME="$WTB/home" timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
        make -C "$WTB" -f "$PROJECT_DIR/Makefile" --no-print-directory worktree-bootstrap 2>&1
}

# Reported, not failed: the condition self-heals wherever the network is up, so exit 0
# is the contract (the SD master below behaves the same way). "not ready yet" and
# "ready." are the two arms of one if/else, so asserting the first proves the second
# did not print.
wtb_fixture n
out=$(run_wtb); rc=$?
check "HS-47" "worktree-bootstrap: an EMPTY submodule is reported, and the tree is not called ready (GH #149)" 0 $rc "$out" \
    "missing" "git submodule(s):" "third_party/spdlog" \
    "git submodule update --init --recursive" \
    "worktree-bootstrap: not ready yet"

# The other arm, from the same fixture: populating the submodule must flip the verdict.
# Without this row HS-47 would also pass on a target that printed "not ready" always.
wtb_fixture y
out=$(run_wtb); rc=$?
check "HS-48" "worktree-bootstrap: a POPULATED submodule reports ok and reaches ready (GH #149)" 0 $rc "$out" \
    "1 git submodule(s) populated" \
    "worktree-bootstrap: ready."

# ---------------- the trap lint must stay wired to the regression preflight (GH #153)
# Third instance of the HS-45/HS-46 shape, one entry point over. lint-traps.sh
# self-tests its own 86-case table on every invocation, so it can prove it still
# DETECTS a stray trap; nothing but this row proves it is still REACHED, and that its
# verdict still turns the preflight row red. Deleting the four-line `if bash
# .../lint-traps.sh` block from scripts/00-preflight-lint.sh restores the un-gated state
# with a leak that only shows up as ~/.jnext/runs/ filling a disk — the exact silence
# GH #153 was filed about.
#
# The row-count witness in regression.sh (`3 lint + 1 sdcard-provision + ...`) is a
# SECOND, independent check that catches the deletion too, but only via the total: it
# says a row went missing, not which, and it cannot see the call surviving with its exit
# status ignored. Both halves below close that.
#
# Driven through the REAL scripts/00-preflight-lint.sh (executed standalone — every row
# script is standalone-runnable by design), so the property proven is the wiring and not
# a restatement of the lint. JNEXT_LINT_TRAPS_DIR aims the lint at a fixture directory;
# it exists for this row alone and regression.sh never sets it.
LT_FIX="$T/lint-traps"
rm -rf "$LT_FIX"; mkdir -p "$LT_FIX/clean" "$LT_FIX/dirty"
: > "$LT_FIX/clean/row-func.sh"
printf '#!/usr/bin/env bash\ntrap %s EXIT\n' "'rm -rf \"\$W\"'" > "$LT_FIX/dirty/row-func.sh"
run_preflight_lint() {   # run_preflight_lint <clean|dirty>
    JNEXT_LINT_TRAPS_DIR="$LT_FIX/$1" timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
        bash "$PROJECT_DIR/test/00regression/scripts/00-preflight-lint.sh" 2>&1
}

# A clean fixture must reach the lint (its own "[lint-traps] scanned:" line is printed
# by lint-traps.sh, so seeing it proves the script actually ran) and pass, giving the
# preflight its documented 6 rows (assertions, traps, timeouts, hardcoded paths,
# debug-headers, pipe-grepq — GH #204 added the third, the unescalated-timeout lint the
# fourth, GH #276 B0's published-header include-graph lint the fifth and the quiet-grep
# pipe lint the sixth; this count is deliberately pinned, so adding a seventh fails here
# first).
out=$(run_preflight_lint clean); rc=$?
check "HS-49a" "the trap lint is reached from the regression preflight, as row 2 (GH #153)" 0 $rc \
    "$out" "[lint-traps] scanned:" "no row script installs its own trap" "Pass: 6"

# The other arm: an offending fixture must turn that row red and fail the preflight.
# Without it HS-49a would also pass on a call whose exit status was discarded.
out=$(run_preflight_lint dirty); rc=$?
check "HS-49b" "an offending row script FAILS the preflight, not just the lint (GH #153)" 1 $rc \
    "$out" "a row script installs its own trap" "Fail: 1"

# ------------- the unescalated-timeout lint must stay wired to the same preflight
# Fourth instance of the HS-45/HS-46/HS-49 shape. test/lint-timeouts.sh self-tests its
# own 64-case table on every invocation, so it can prove it still DETECTS a bare
# `timeout`; nothing but these two rows proves it is still REACHED and that its verdict
# still turns the preflight row red. The failure it guards leaves no trace in any
# count: a `timeout N` that cannot escalate lets a wedged process outlive its row and
# load the box underneath the pacing-bound rows, which then report a FAIL nobody can
# reproduce. Deleting the four-line `if bash .../lint-timeouts.sh` block restores that
# silence, and the row-count witness in regression.sh only says a row went missing —
# not which, and not that the call survived with its status ignored.
#
# JNEXT_LINT_TIMEOUTS_DIR aims the lint at a fixture directory; it exists for these two
# rows alone and regression.sh never sets it.
LTO_FIX="$T/lint-timeouts"
rm -rf "$LTO_FIX"; mkdir -p "$LTO_FIX/clean" "$LTO_FIX/dirty"
printf '#!/usr/bin/env bash\ntimeout --foreground --kill-after=5s 60s jnext\n' > "$LTO_FIX/clean/row-func.sh"
printf '#!/usr/bin/env bash\ntimeout 60 jnext\n'                              > "$LTO_FIX/dirty/row-func.sh"
run_preflight_timeouts() {   # run_preflight_timeouts <clean|dirty>
    JNEXT_LINT_TIMEOUTS_DIR="$LTO_FIX/$1" timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
        bash "$PROJECT_DIR/test/00regression/scripts/00-preflight-lint.sh" 2>&1
}

out=$(run_preflight_timeouts clean); rc=$?
check "HS-56a" "the unescalated-timeout lint is reached from the regression preflight, as row 3" 0 $rc \
    "$out" "[lint-timeouts] scanned:" "every 'timeout' escalates to SIGKILL" "Pass: 6"

# The other arm: a bare `timeout` must turn that row red and fail the whole preflight.
# Without it HS-56a would also pass on a call whose exit status was discarded.
out=$(run_preflight_timeouts dirty); rc=$?
check "HS-56b" "a bare 'timeout' FAILS the preflight, not just the lint" 1 $rc \
    "$out" "runs 'timeout' with no escalation" "Fail: 1"

# ------------- the published-header include-graph lint must stay wired to the preflight
# Fifth instance of the HS-45/HS-46/HS-49/HS-56 shape, and it exists for the reason the
# lint itself does. Epic #276's defining property — a debugger frontend never holds an
# `Emulator*` — had NO gate at all: the four published headers cited design §9's
# `grep -l 'core/emulator.h' src/debugger/*.cpp`, which greps the Qt panels and says
# nothing about them, and `#include "core/emulator.h"` in inspect.h compiled clean.
# test/lint-debug-headers.sh closes that, and these two rows are the only thing proving
# it is still REACHED and that its verdict still turns the preflight row red: deleting
# the four-line `if bash .../lint-debug-headers.sh` block from
# scripts/00-preflight-lint.sh restores the un-gated state, and the row-count witness in
# regression.sh only says a row went missing, not which.
#
# The fixture is a SYMLINK FARM of the real src/ (`cp -as`) with one header overlaid, so
# the dirty arm poisons `debug/inspect.h` alone while every other header — including the
# `debug/result.h` the lint's own positive control looks for — stays the real file.
# JNEXT_LINT_DEBUG_HEADERS_SRC aims the lint at it and regression.sh never sets it.
DBH_FIX="$T/lint-debug-headers"
rm -rf "$DBH_FIX"; mkdir -p "$DBH_FIX"
cp -as "$PROJECT_DIR/src" "$DBH_FIX/clean"
cp -as "$PROJECT_DIR/src" "$DBH_FIX/dirty"
# Write to a temp file and MOVE it into place, rather than `rm -f` then redirect.
# The positional form works only because the `rm` runs first: a redirect into a
# path that is still a symlink FOLLOWS it and would truncate the real
# src/debug/inspect.h in the working tree. `mv` replaces the link itself, so the
# safety is structural rather than a property of statement order.
{ printf '#include "core/emulator.h"\n'; cat "$PROJECT_DIR/src/debug/inspect.h"; } \
    > "$DBH_FIX/poisoned-inspect.h"
mv -f "$DBH_FIX/poisoned-inspect.h" "$DBH_FIX/dirty/debug/inspect.h"
run_preflight_debug_headers() {   # run_preflight_debug_headers <clean|dirty>
    JNEXT_LINT_DEBUG_HEADERS_SRC="$DBH_FIX/$1" timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
        bash "$PROJECT_DIR/test/00regression/scripts/00-preflight-lint.sh" 2>&1
}

out=$(run_preflight_debug_headers clean); rc=$?
check "HS-57a" "the published-header include-graph lint is reached from the regression preflight, as row 5 (GH #276)" 0 $rc \
    "$out" "published header(s) checked" "no published debug header reaches a forbidden dependency" "Pass: 6"

# The other arm: a forbidden include must turn that row red and fail the whole preflight.
# Without it HS-57a would also pass on a call whose exit status was discarded — which is
# exactly how the two guards below shipped dead.
out=$(run_preflight_debug_headers dirty); rc=$?
check "HS-57b" "a forbidden include in a published header FAILS the preflight, not just the lint (GH #276)" 1 $rc \
    "$out" "reaches a FORBIDDEN dependency" "core/emulator.h" "Fail: 1"

# ------------- the quiet-grep pipe lint must stay wired to the same preflight
# Sixth instance of the shape. test/lint-pipe-grepq.sh self-tests its own case table on
# every invocation, so it can prove it still DETECTS `producer | grep -q`; only these two
# rows prove it is still REACHED and that its verdict still turns the preflight row red.
# What it guards fails nothing on its own: a row whose `echo "$out" | grep -q` loses to
# SIGPIPE under load flips its verdict, and the next solo run passes.
# JNEXT_LINT_PIPE_GREPQ_DIR aims the lint at a fixture directory; it exists for these
# two rows alone and regression.sh never sets it.
LPG_FIX="$T/lint-pipe-grepq"
rm -rf "$LPG_FIX"; mkdir -p "$LPG_FIX/clean" "$LPG_FIX/dirty"
printf '#!/usr/bin/env bash\ngrep -q "x" <<<"$out"\n'    > "$LPG_FIX/clean/row-func.sh"
printf '#!/usr/bin/env bash\necho "$out" | grep -q "x"\n' > "$LPG_FIX/dirty/row-func.sh"
run_preflight_pipe_grepq() {   # run_preflight_pipe_grepq <clean|dirty>
    JNEXT_LINT_PIPE_GREPQ_DIR="$LPG_FIX/$1" timeout --kill-after=5s "${INVOKE_TIMEOUT}s" \
        bash "$PROJECT_DIR/test/00regression/scripts/00-preflight-lint.sh" 2>&1
}

out=$(run_preflight_pipe_grepq clean); rc=$?
check "HS-67a" "the quiet-grep pipe lint is reached from the regression preflight, as row 6" 0 $rc \
    "$out" "[lint-pipe-grepq] scanned:" "no pipeline ends in a quiet grep" "Pass: 6"

# The other arm: an offending pipe must turn that row red and fail the whole preflight.
out=$(run_preflight_pipe_grepq dirty); rc=$?
check "HS-67b" "a pipe into grep -q FAILS the preflight, not just the lint" 1 $rc \
    "$out" "a pipeline ends in a quiet grep under pipefail" "Fail: 1"

# Where the run LIVES must not decide the verdict. The lint's patterns once saw
# absolute paths, so its own `mktemp -d` translation unit, or a source root under a
# directory ending in `qt`, matched the Qt pattern: HS-49a failed once on
# `/tmp/tmp.ZPHf7lfGqt/tu.cpp`. Both are forced here — TMPDIR and the (clean)
# source root sit under a `…qt` directory — and the clean tree must still pass.
mkdir -p "$DBH_FIX/xqt"
cp -as "$PROJECT_DIR/src" "$DBH_FIX/xqt/src"
out=$(TMPDIR="$DBH_FIX/xqt" JNEXT_LINT_DEBUG_HEADERS_SRC="$DBH_FIX/xqt/src" \
          timeout --kill-after=5s "${INVOKE_TIMEOUT}s" bash "$PROJECT_DIR/test/lint-debug-headers.sh" 2>&1); rc=$?
check "HS-57c" "the include-graph lint's verdict does not depend on its temp or source path (a '…qt/' component)" 0 $rc \
    "$out" "none reaches a forbidden dependency"

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
          if printf '%s\n' "${LIST[@]}" | grep -qx "$2"; then echo FOUND; else echo ABSENT; fi  # lint-pipe-grepq: allow (builds the hazard on purpose)
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
                 "$PROJECT_DIR/test/bench/ab-hotlatch.sh"
                 "$PROJECT_DIR/test/bench/perf-attach.sh"
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
    # A <var> written `file:NAME` names a single FILE the body removes (the
    # bench scripts' ~/tmp clone image) rather than a directory; its fixture
    # sits in the fake ~/tmp beside a sibling image that must survive.
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
    local var d fixtures=() file_sib=""
    for var in "$@"; do
        if [[ "$var" == file:* ]]; then
            var=${var#file:}; d="$fh/tmp/fx-$var.img"
            file_sib="$fh/tmp/other-live-run.img"
            mkdir -p "$fh/tmp"; touch "$d" "$file_sib"
            fixtures+=("$d")
            echo "$var=$(printf %q "$d")" >> "$probe"
            continue
        fi
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
    [[ -z "$file_sib" || -e "$file_sib" ]] || sibling=n
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

# The two bench scripts that clone the SD image to a per-run ~/tmp FILE.
# HS-42 covers their trap wiring against a STUB; these run the real bodies.
out=$(cleanup_body_probe "$PROJECT_DIR/test/bench/ab-hotlatch.sh" bench_cleanup file:CLONE)
check "HS-43d" "REAL ab-hotlatch bench_cleanup body: bounded, removes only its clone file" 0 0 \
    "$out" "rc=0 fast=y gone=y sibling=y"

out=$(cleanup_body_probe "$PROJECT_DIR/test/bench/perf-attach.sh" bench_cleanup file:CLONE)
check "HS-43e" "REAL perf-attach bench_cleanup body: bounded, removes only its clone file" 0 0 \
    "$out" "rc=0 fast=y gone=y sibling=y"

# ------------------------------------------------ host load (GH #245)
# The regression suite flags a FAIL that happened on a loaded host and names
# every failed row at the end, so whether a red row might be contention is
# answered by the harness rather than by a reader's memory of which rows are
# sensitive. These rows drive the REAL suite library (sourced whole, in a child
# shell with a fake $HOME so no SD clone is made) against a fake /proc/loadavg,
# and pin both halves: the warning appears on a loaded host and NOT on an idle
# one, and in neither case does the verdict change.
load_probe() {   # load_probe <1-min load> <cpus> <bash snippet>
    local fh="$T/loadfix"
    rm -rf "$fh"; mkdir -p "$fh"
    printf '%s 0.00 0.00 1/100 12345\n' "$1" > "$T/loadavg"
    HOME="$fh" JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg" JNEXT_REGRESSION_NPROC="$2" \
        timeout --kill-after=3s 10s bash -c \
        "set -euo pipefail; source '$PROJECT_DIR/test/00regression/test-functions.inc'; $3" 2>&1
}
# How many lines of <text> match <fixed string> — for the ABSENCE halves, which
# check() (patterns must be present) cannot express on its own.
count_of() { grep -cF -- "$2" <<<"$1" || true; }

out=$(load_probe 12.5 12 'CURRENT_ROW=fake-func; fail_row " (boom)"; echo "fail=$fail"'); rc=$?
check "HS-50" "a FAIL on a loaded host is flagged on the spot, and still counts as a FAIL (GH #245)" 0 $rc "$out" \
    "FAIL" " (boom)" "^ host load 12.5 on 12 CPUs when this row failed" "re-run it SOLO" "fail=1"

out=$(load_probe 3.0 12 'CURRENT_ROW=fake-func; fail_row " (boom)"; echo "fail=$fail"'); rc=$?
out+=$'\n'"warned=$(count_of "$out" "when this row failed")"
check "HS-51" "the control: the same FAIL on an idle host is NOT flagged, and still counts (GH #245)" 0 $rc "$out" \
    "FAIL" " (boom)" "fail=1" "warned=0"

loaded_run='load_report start; begin_func a-func; fail_row; begin_func b-func; pass_row; load_report end; load_summary'
out=$(load_probe 12.5 12 "$loaded_run"); rc=$?
out+=$'\n'"passed-row-listed=$(count_of "$out" "b-func (")"
check "HS-52" "a loaded run records the load at start and end, names every failed row, and warns (GH #245)" 0 $rc "$out" \
    "host load at start: 12.5 (1-min) on 12 CPUs" "host load at end: 12.5" "Failed rows:" \
    "a-func (1-min load 12.5)" "WARNING: this run was on a loaded host" \
    "The FAIL verdict above stands" "passed-row-listed=0"

out=$(load_probe 3.0 12 "$loaded_run"); rc=$?
out+=$'\n'"warned=$(count_of "$out" "loaded host")"
check "HS-53" "the control: an idle run still names its failed rows, but does NOT warn (GH #245)" 0 $rc "$out" \
    "Failed rows:" "a-func (1-min load 3.0)" "warned=0"

# No load average to read (not Linux, or /proc not mounted): say "unknown",
# never flag, never print the shell's own error, and still count the FAIL.
out=$(load_probe 12.5 12 "LOADAVG_FILE=$T/no-such-loadavg; $loaded_run; echo \"fail=\$fail\""); rc=$?
out+=$'\n'"warned=$(count_of "$out" "loaded host") noise=$(count_of "$out" "No such file")"
check "HS-55" "an unreadable load average reads 'unknown', is never flagged, and the FAIL still counts (GH #245)" 0 $rc "$out" \
    "host load at start: unknown" "a-func (1-min load unknown)" "fail=1" "warned=0 noise=0"

# ---------------- a sourced row must not clobber the counters (CI, v1.0.66) ----------------
# Rows are SOURCED into the harness shell. script-replay-edge-func did `skip=""`,
# which zeroed an earlier SKIP: CI (where gdb-z88dk-func skips) reported 214 of
# 215 rows and an empty `Skip:`, while every local run stayed green. The driver
# now wraps each row in row_counters_snapshot/row_counters_check; these rows
# drive that guard around REAL sourced stub rows, through the same library.
mkdir -p "$T/rows"
printf '%s\n' 'begin_func clobber-func' 'skip=""' 'pass_row' > "$T/rows/clobber-func.sh"
printf '%s\n' 'begin_func good-func' 'skip_row " (stub)"' > "$T/rows/good-func.sh"
printf '%s\n' 'begin_func twice-func' 'pass_row' 'pass_row' > "$T/rows/twice-func.sh"
printf '%s\n' 'if want quiet-func; then begin_func quiet-func; pass_row; fi' > "$T/rows/quiet-func.sh"
guarded() {   # guarded <row> — an earlier SKIP, then <row> sourced inside the guard
    echo "begin_func early-func; skip_row; row_counters_snapshot $1; source '$T/rows/$1.sh'; row_counters_check; echo \"after: \$pass/\$fail/\$skip\""
}

out=$(load_probe 0.5 12 "$(guarded clobber-func)"); rc=$?
out+=$'\n'"reached=$(count_of "$out" "after:")"
check "HS-68a" "a sourced row that clobbers \$skip after an earlier SKIP is a harness fault NAMING the row" 2 $rc "$out" \
    "HARNESS FAULT" "clobber-func" "left the harness counter" "\$skip" "0/0/1 pass/fail/skip before the row" "reached=0"

out=$(load_probe 0.5 12 "$(guarded good-func)"); rc=$?
out+=$'\n'"faults=$(count_of "$out" "HARNESS FAULT")"
check "HS-68b" "the NULL control: a row that leaves the counters alone and reports one result passes the guard" 0 $rc "$out" \
    "after: 0/0/2" "faults=0"

out=$(load_probe 0.5 12 "$(guarded twice-func)"); rc=$?
check "HS-68c" "a row that reports two results is a harness fault naming it" 2 $rc "$out" \
    "HARNESS FAULT" "twice-func" "reported" "a row reports exactly 1"

out=$(load_probe 0.5 12 "FILTER_TESTS=(other-func); IS_FILTERED=([other-func]=1); $(guarded quiet-func)"); rc=$?
out+=$'\n'"faults=$(count_of "$out" "HARNESS FAULT")"
check "HS-68d" "a row a name filter leaves out reports nothing, and the guard expects nothing of it" 0 $rc "$out" \
    "after: 0/0/1" "faults=0"

runner="$PROJECT_DIR/test/00regression/row-runner.sh"
out="snap=$(grep -cE '^[[:space:]]*row_counters_snapshot "\$row"$' "$runner") check=$(grep -cE '^[[:space:]]*row_counters_check$' "$runner")"
out+=" sourced=$(grep -cE '^[[:space:]]*source "\$row_script"$' "$runner")"
check "HS-68e" "the row runner wraps every sourced functional row in the counter guard (GH #295)" 0 0 "$out" "snap=1 check=1 sourced=1"

# The library is only half of it: the driver must call it, and the screenshot
# rows must name themselves (they report through fail_row without begin_func).
# Source-scan, like HS-30/HS-40 — a harness that quietly stopped calling the
# summary would leave HS-50..53 green.
reg="$PROJECT_DIR/test/00regression/regression.sh"
out="start=$(grep -cE '^load_report start$' "$reg") end=$(grep -cE '^load_report end$' "$reg")"
out+=" summary=$(grep -cE '^load_summary$' "$reg")"
out+=" shots=$(grep -cE '^[[:space:]]*CURRENT_ROW=\$test_name$' "$PROJECT_DIR/test/00regression/scripts/screenshots.sh")"
check "HS-54" "regression.sh reports the load at start and end and prints the summary; screenshot rows name themselves (GH #245)" 0 0 \
    "$out" "start=1 end=1 summary=1 shots=1"

# ---------------- parallel functional rows (GH #295) ----------------
# Every functional row now runs in a process of its own, several at once, and
# the driver merges the results. These rows drive the REAL scheduler
# (parallel-rows.inc) and the REAL row runner against stub rows, through the
# real suite library in a child shell with a fake $HOME, and inject each way a
# row process can go wrong. Each must be a loud, NAMED outcome — a harness
# fault where the accounting cannot be trusted, a FAIL where the row itself
# misbehaved — never a quietly shorter or longer total.
mkdir -p "$T/prows"
prow() { printf '%s\n' "${@:2}" > "$T/prows/$1.sh"; }   # prow <name> <line...>
prow ok-func     'begin_func ok-func' 'pass_row " (stub)"'
prow ok2-func    'begin_func ok2-func' 'pass_row " (stub)"'
prow skip-func   'begin_func skip-func' 'skip_row " (stub)"'
prow fail-func   'begin_func fail-func' 'fail_row " (stub)"'
prow slow1-func  'begin_func slow1-func' 'sleep 2' 'pass_row " (slow)"'
prow slow2-func  'begin_func slow2-func' 'sleep 2' 'pass_row " (slow)"'
prow twice-func  'begin_func twice-func' 'pass_row' 'pass_row'
prow silent-func ':'
prow crash-func  'begin_func crash-func' 'exit 3'
prow hang-func   'begin_func hang-func' 'sleep 60' 'pass_row'
prow intruder-func 'mkdir -p "$TMP_DIR/../victim-func"' 'begin_func intruder-func' 'pass_row'
prow victim-func 'begin_func victim-func' 'pass_row'
prow lateintruder-func 'mkdir -p "$TMP_DIR/../victim-func"; touch "$TMP_DIR/../victim-func/x"' 'begin_func lateintruder-func' 'pass_row'
prow trapper-func 'trap ":" EXIT' 'begin_func trapper-func' 'pass_row'
prow writer-func 'printf x >> "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"' 'begin_func writer-func' 'pass_row'

par_probe() {   # par_probe <bash snippet> — the real scheduler, stub rows, fake $HOME
    local fh="$T/parfix"
    rm -rf "$fh"; mkdir -p "$fh"
    printf '0.50 0.00 0.00 1/100 12345\n' > "$T/loadavg-idle"
    HOME="$fh" JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-idle" JNEXT_REGRESSION_NPROC=12 \
        timeout --kill-after=3s 40s bash -c \
        "set -euo pipefail
         source '$PROJECT_DIR/test/00regression/test-functions.inc'
         source '$PROJECT_DIR/test/00regression/parallel-rows.inc'
         SCRIPTS_DIR='$T/prows'; JNEXT=/bin/false; declare -A IS_PRIVATE_SD=()
         $1" 2>&1
}
phase() {   # phase <lanes> <row...> — one phase, then the end-of-run checks and the tally
    echo "t0=\$SECONDS; func_phases_begin; run_func_phase stub $1 ${*:2}; func_phases_end; echo \"after: \$pass/\$fail/\$skip [\${REPORTED_FUNC[*]}] elapsed=\$(( SECONDS - t0 ))\""
}
# line_of <text> <fixed> — first line number holding <fixed>, 0 if none
line_of() { local n; n=$(grep -nF -- "$2" <<<"$1" | head -n 1 | cut -d: -f1); echo "${n:-0}"; }

out=$(par_probe "$(phase 3 slow1-func slow2-func ok-func)"); rc=$?
el=$(grep -oE 'elapsed=[0-9]+' <<<"$out" | cut -d= -f2)
a=$(line_of "$out" "[slow1-func]"); b=$(line_of "$out" "[slow2-func]"); c=$(line_of "$out" "[ok-func]")
out+=$'\n'"parallel=$(( ${el:-99} < 4 ? 1 : 0 )) ordered=$(( a > 0 && a < b && b < c ? 1 : 0 ))"
check "HS-69a" "the control: rows run at once, and are printed and merged in declared order (GH #295)" 0 $rc "$out" \
    "after: 3/0/0 [slow1-func slow2-func ok-func]" "parallel=1 ordered=1"

out=$(par_probe "$(phase 2 ok-func twice-func ok2-func)"); rc=$?
check "HS-69b" "a row that reports twice is a harness fault naming it (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "twice-func" "raised a harness fault in its row process" "a row reports exactly 1"

out=$(par_probe "$(phase 2 ok-func silent-func)"); rc=$?
check "HS-69c" "a row that reports nothing is a harness fault naming it (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "silent-func" "raised a harness fault in its row process" "a row reports exactly 1 (pass/fail/skip 0/0/0 -> 0/0/0)"

out=$(par_probe "$(phase 1 crash-func ok-func)"); rc=$?
check "HS-69d" "a row that dies before reporting is a FAIL naming it, and the run goes on (GH #295)" 0 $rc "$out" \
    "[crash-func]" "died with exit 3 before reporting" "after: 1/1/0 [crash-func ok-func]"

out=$(par_probe "ROW_BOUND=2; $(phase 2 hang-func ok-func)"); rc=$?
el=$(grep -oE 'elapsed=[0-9]+' <<<"$out" | cut -d= -f2)
out+=$'\n'"bounded=$(( ${el:-99} < 15 ? 1 : 0 ))"
check "HS-69e" "a row that hangs is killed at its bound and FAILs naming it; the others still run (GH #295)" 0 $rc "$out" \
    "outlived its 2 s bound" "after: 1/1/0 [hang-func ok-func]" "bounded=1"

out=$(par_probe "$(phase 1 intruder-func victim-func)"); rc=$?
check "HS-69f" "a row that writes into a later row's directory is refused when that row starts (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "victim-func" "already exists before the row started"

out=$(par_probe "$(phase 1 victim-func lateintruder-func)"); rc=$?
check "HS-69g" "a row that writes into a finished row's directory is a harness fault at the end (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "entries left under the per-row directories" "victim-func"

out=$(par_probe "$(phase 1 trapper-func)"); rc=$?
check "HS-69h" "a row whose own trap replaces the library's cleanup is caught by its leftovers (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "trapper-func" "exited but its directories are still there"

# The shared clone: an untagged row WRITING to it is a fault; tagged
# private-sd, the same row writes only its own copy. A one-line stand-in
# image keeps it to a few bytes.
shared='mkdir -p "$RUN_DIR/sdcard"; printf card > "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"'
out=$(par_probe "$shared; $(phase 2 ok-func writer-func)"); rc=$?
check "HS-69i" "an untagged row that writes to the shared SD clone is a harness fault (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "shared SD clone CHANGED" "private-sd"

out=$(par_probe "$shared; IS_PRIVATE_SD[writer-func]=1; $(phase 2 ok-func writer-func); echo \"card=\$(cat \"\$RUN_DIR/sdcard/cspect-next-1gb-fixed.img\")\""); rc=$?
check "HS-69j" "the control: tagged private-sd, the same row writes its own copy and the run is clean (GH #295)" 0 $rc "$out" \
    "after: 2/0/0 [ok-func writer-func]" "card=card"

out=$(par_probe "$(phase 2 fail-func skip-func ok-func); load_summary"); rc=$?
check "HS-69k" "a row process's FAIL, its failed-row record and the tally all reach the driver (GH #295)" 0 $rc "$out" \
    "after: 1/1/1 [fail-func skip-func ok-func]" "Failed rows:" "fail-func (1-min load 0.50)"

out=$(par_probe "ROW_BOUND=4; JNEXT_REGRESSION_HEARTBEAT=1; $(phase 2 hang-func ok-func)"); rc=$?
check "HS-69l" "a heartbeat names the rows still running while the phase waits on them (GH #295)" 0 $rc "$out" \
    "[heartbeat] stub:" "still running: hang-func"

out=$(par_probe "$(phase 2 skip-func ok-func); echo \"skipped=[\${SKIPPED_ROWS[*]}]\""); rc=$?
check "HS-69m" "a row process's SKIP reaches the driver BY NAME, for the stamp's refusal (GH #295)" 0 $rc "$out" \
    "skipped=[skip-func]"

printf '20.00 0.00 0.00 1/100 12345\n' > "$T/loadavg-busy20"
solo='declare -A IS_DECLARED_FUNC=([a-func]=1 [b-func]=1); LOADAVG_FILE=$1;'
solo_case() {   # solo_case <loadavg> <fail-snippet>
    local fh="$T/solofix"; rm -rf "$fh"; mkdir -p "$fh"
    HOME="$fh" JNEXT_REGRESSION_NPROC=12 timeout --kill-after=3s 20s bash -c \
        "set -euo pipefail
         source '$PROJECT_DIR/test/00regression/test-functions.inc'
         source '$PROJECT_DIR/test/00regression/parallel-rows.inc'
         $solo $2
         if r=\$(solo_confirmable_fails); then echo \"eligible=[\$r]\"; else echo \"refused=[\$r]\"; fi" _ "$1" 2>&1
}
out=$(solo_case "$T/loadavg-busy20" 'CURRENT_ROW=a-func; fail_row; CURRENT_ROW=b-func; fail_row')
out+=$'\n'$(solo_case "$T/loadavg-idle" 'CURRENT_ROW=a-func; fail_row')
out+=$'\n'$(solo_case "$T/loadavg-busy20" 'CURRENT_ROW=a-func; fail_row; CURRENT_ROW=boot-48k; fail_row')
check "HS-69n" "only functional rows that FAILED ON A LOADED HOST may go to a solo re-run (GH #295)" 0 0 "$out" \
    "eligible=[a-func b-func]" "refused=[FAIL(s) that a solo re-run cannot confirm: a-func (failed on an idle host)]" \
    "boot-48k (not a functional row)"

# The cleanup path: a TERM to the driver while rows run must stop every row
# process and remove every directory (the review checked it by hand).
prow sleeper1-func 'echo $BASHPID >> "'"$T"'/sleeper.pids"' 'echo "$TMP_DIR" >> "'"$T"'/sleeper.dirs"' 'sleep 30 & echo $! >> "'"$T"'/sleeper.pids"; wait' 'begin_func sleeper1-func' 'pass_row'
prow sleeper2-func 'echo $BASHPID >> "'"$T"'/sleeper.pids"' 'sleep 30 & echo $! >> "'"$T"'/sleeper.pids"; wait' 'begin_func sleeper2-func' 'pass_row'
rm -f "$T/sleeper.pids" "$T/sleeper.dirs"
rm -f "$T/driver.pid"
par_probe "echo \$\$ > '$T/driver.pid'; $(phase 2 sleeper1-func sleeper2-func)" > "$T/sleeper.out" 2>&1 &
sp=$!
for _ in $(seq 1 50); do [[ $(wc -l < "$T/sleeper.pids" 2>/dev/null || echo 0) -ge 4 ]] && break; sleep 0.2; done
dp=$(cat "$T/driver.pid" 2>/dev/null || true)
t0=$SECONDS
kill -TERM "$dp" 2>/dev/null; wait "$sp" 2>/dev/null
sleep 1
alive=0; for p in $(cat "$T/sleeper.pids" 2>/dev/null); do kill -0 "$p" 2>/dev/null && alive=$(( alive + 1 )); done
rdir=$(head -n1 "$T/sleeper.dirs" 2>/dev/null); tdir=$(dirname "$(dirname "${rdir:-/nonexistent/x/y}")")
out="pids=$(wc -l < "$T/sleeper.pids" 2>/dev/null) alive=$alive tmp-gone=$([[ -n "$rdir" && ! -e "$tdir" ]] && echo y || echo n) runs-gone=$([[ -z "$(ls -A "$T/parfix/.jnext/runs" 2>/dev/null)" ]] && echo y || echo n) fast=$(( SECONDS - t0 < 15 ? 1 : 0 ))"
check "HS-69o" "a TERM to the driver stops every running row process and removes every directory (GH #295)" 0 0 "$out" \
    "pids=4 alive=0 tmp-gone=y runs-gone=y fast=1"

# ---------------- one full run per host, on a quiet host (GH #295) ----------------
# Driven through regression.sh itself: JNEXT_REGRESSION_LOCK=force makes the
# fast --preflight-only run take the same lock and load-wait a full run takes.
lockf="$T/reg.lock"
# The load is FAKED idle unless a row says otherwise: a forced-slot run also
# waits for a quiet host, so on a really loaded box (this self-test runs inside
# the regression's own parallel phase) HS-70a..c would otherwise measure the
# host instead of the lock — which is exactly how they first failed.
printf '0.50 0.00 0.00 1/100 12345\n' > "$T/loadavg-idle"
slot_run() {   # slot_run <env...> — a forced-slot preflight run, bounded
    env JNEXT_REGRESSION_LOCK_FILE="$lockf" JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-idle" \
        JNEXT_REGRESSION_NPROC=12 "$@" \
        timeout --kill-after=5s 60s bash "$REG" --preflight-only 2>&1
}
flock "$lockf" sleep 7 & holder=$!
sleep 1
t0=$SECONDS
out=$(slot_run JNEXT_REGRESSION_LOCK=force); rc=$?
out+=$'\n'"waited=$(( SECONDS - t0 >= 4 ? 1 : 0 ))"
wait "$holder" 2>/dev/null
check "HS-70a" "a second full run waits for the host lock, says so, then runs (GH #295)" 0 $rc "$out" \
    "another FULL regression run holds the host lock" "preflight OK" "waited=1"

flock "$lockf" sleep 20 & holder=$!
sleep 1
out=$(slot_run JNEXT_REGRESSION_LOCK=force JNEXT_REGRESSION_LOCK_WAIT=5); rc=$?
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
check "HS-70b" "the lock wait is bounded: a lock held too long is a harness fault (GH #295)" 2 $rc "$out" \
    "HARNESS FAULT" "for the full-run lock"

flock "$lockf" sleep 8 & holder=$!
sleep 1
t0=$SECONDS
out=$(slot_run); rc=$?
out+=$'\n'"quick=$(( SECONDS - t0 < 4 ? 1 : 0 )) waits=$(count_of "$out" "holds the host lock")"
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
check "HS-70c" "the control: a targeted run never takes the lock, even while it is held (GH #295)" 0 $rc "$out" \
    "preflight OK" "quick=1 waits=0"

printf '50.00 0.00 0.00 1/100 12345\n' > "$T/loadavg-busy"
out=$(slot_run JNEXT_REGRESSION_LOCK=force JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-busy" \
               JNEXT_REGRESSION_NPROC=12 JNEXT_REGRESSION_LOAD_WAIT=5); rc=$?
check "HS-70d" "a full run on a loaded host waits (bounded), says why, then starts with a loud note (GH #295)" 0 $rc "$out" \
    "waiting for the host to quieten: 1-min load 50.00 > 12 CPUs" "starting anyway" "preflight OK"

# A TERM to the OUTER regression.sh only (not its process group) must stop the
# locked run and free the lock; it used to leave flock + the run holding it.
# The busy fake load keeps the locked run waiting in its quiet-host wait.
slot_run JNEXT_REGRESSION_LOCK=force JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-busy" \
         JNEXT_REGRESSION_NPROC=12 JNEXT_REGRESSION_LOAD_WAIT=60 > "$T/outer.out" 2>&1 &
op=$!
for _ in $(seq 1 50); do grep -q 'waiting for the host' "$T/outer.out" 2>/dev/null && break; sleep 0.2; done
outer=""   # the regression.sh whose parent is timeout (the locked run's parent is flock)
for p in $(proc_with_args "bash $REG --preflight-only"); do
    [[ "$(proc_comm "$(proc_ppid "$p")")" == timeout ]] && outer=$p
done
held=$(flock -n "$lockf" true && echo free || echo held)
t0=$SECONDS
kill -TERM "$outer" 2>/dev/null; wait "$op" 2>/dev/null
sleep 1
out="outer=${outer:+found} before=$held after=$(flock -n "$lockf" true && echo free || echo held) fast=$(( SECONDS - t0 < 10 ? 1 : 0 )) runs=$(proc_with_args "bash $REG --preflight-only" | wc -l)"
check "HS-70e" "a TERM to the outer regression.sh alone stops the locked run and frees the lock (GH #295)" 0 0 "$out" \
    "outer=found before=held after=free" "fast=1" "runs=0"

# ---------------- regression stamps (GH #295) ----------------
# The REAL stamp script, run inside a throwaway git repository (it keys the
# repository it lives in), with its own stamp directory.
SR="$T/stamprepo"; SD="$T/stamps"
mkdir -p "$SR/test" "$SR/src" "$SR/doc/formats" "$SR/doc/man"
cp "$PROJECT_DIR/test/regression-stamp.sh" "$SR/test/"
echo 'int main(){}' > "$SR/src/main.cpp"; echo '# readme' > "$SR/README.md"
echo '{}' > "$SR/doc/formats/s.json"; echo 'notes' > "$SR/doc/notes.txt"
sg() { git -C "$SR" -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }
stamp() { JNEXT_REGRESSION_STAMP_DIR="$SD" timeout --kill-after=5s 30s bash "$SR/test/regression-stamp.sh" "$@" 2>&1; }
sg init -q; sg add -A; sg commit -qm base
st=$(stamp state)
out=$(stamp write "$st" pass=3 fail=0 skip=0 rows=3); rc=$?
out+=$'\n'"files=$(find "$SD" -type f 2>/dev/null | wc -l)"
check "HS-71a" "a green run on a clean tree writes a stamp keyed on its content (GH #295)" 0 $rc "$out" \
    "regression stamp written" "files=1"

echo 'more' >> "$SR/README.md"; echo 'x' >> "$SR/doc/notes.txt"; sg commit -qam docs
out=$(stamp check); rc=$?
check "HS-71b" "a docs-only commit after the run is still covered by its stamp (GH #295)" 0 $rc "$out" \
    "STAMP OK" "pass=3"

echo 'int x;' >> "$SR/src/main.cpp"
st=$(stamp state)
out=$(stamp write "$st" pass=3 fail=0 skip=0 rows=3; stamp check); rc=$?
check "HS-71c" "an uncommitted source change: no stamp is written, and the check says why (GH #295)" 1 $rc "$out" \
    "no regression stamp: the tree had uncommitted non-doc changes" "src/main.cpp" "NO STAMP: the work tree has 1"

sg commit -qam code
out=$(stamp check); rc=$?
check "HS-71d" "after a committed non-doc change the old stamp no longer matches (GH #295)" 1 $rc "$out" \
    "NO STAMP for this tree"

k1=$(stamp key); echo '{"a":1}' > "$SR/doc/formats/s.json"; sg commit -qam schema; k2=$(stamp key)
out="formats-keyed=$([[ "$k1" != "$k2" ]] && echo 1 || echo 0)"
k1=$(stamp key); echo 'y' >> "$SR/doc/notes.txt"; sg commit -qam notes; k2=$(stamp key)
out+=" notes-keyed=$([[ "$k1" != "$k2" ]] && echo 1 || echo 0)"
check "HS-71e" "doc/formats/ is a test input and changes the key; a plain doc does not (GH #295)" 0 0 "$out" \
    "formats-keyed=1 notes-keyed=0"

echo 'dirty doc' >> "$SR/README.md"
st=$(stamp state)
out=$(stamp write "$st" pass=3 fail=0 skip=0 rows=3; stamp check); rc=$?
check "HS-71f" "an uncommitted DOC edit does not stop the stamp, nor the check (GH #295)" 0 $rc "$out" \
    "regression stamp written" "STAMP OK"

st=$(stamp state); echo 'int y;' >> "$SR/src/main.cpp"; sg commit -qam during
out=$(stamp write "$st" pass=3 fail=0 skip=0 rows=3); rc=$?
check "HS-71g" "a non-doc commit DURING the run: no stamp for either tree (GH #295)" 0 $rc "$out" \
    "no regression stamp: the tree's non-doc content changed during the run"

# --- the keyed-path matrix, on the REAL paths (review round 1) ---
# Each path is committed once, then changed and committed again; the row reads
# whether the key moved. Non-doc classes MUST move it, documentation must NOT,
# and the documentation files a regression gate reads as input MUST.
keymove() {   # keymove <path> — 1 if a committed change to <path> moves the key
    local k1 k2
    mkdir -p "$SR/$(dirname "$1")"; echo a > "$SR/$1"; sg add -A; sg commit -qm "add $1"
    k1=$(stamp key); echo b >> "$SR/$1"; sg add -A; sg commit -qm "change $1"; k2=$(stamp key)
    [[ "$k1" != "$k2" ]] && echo 1 || echo 0
}
out=""
for kp in Makefile CMakeLists.txt test/00regression/functional_tests.conf tools/pgo-train.sh \
          .github/workflows/ci.yml src/core/emulator.cpp test/00regression/scripts/x-func.sh; do
    out+="$kp=$(keymove "$kp") "
done
# a gitlink (a submodule pointer) moving to another commit
k1=$(stamp key)
sg update-index --add --cacheinfo "160000,$(git -C "$SR" rev-parse HEAD),third_party/sub"; sg commit -qm gitlink
k2=$(stamp key)
sg update-index --add --cacheinfo "160000,$(git -C "$SR" rev-parse HEAD~2),third_party/sub"; sg commit -qm gitlink2
k3=$(stamp key)
out+="gitlink=$([[ "$k1" != "$k2" && "$k2" != "$k3" ]] && echo 1 || echo 0)"
check "HS-71h" "every non-doc class of path moves the key — Makefile, CMake, a .conf, tools/, .github/, a gitlink (GH #295)" 0 0 "$out" \
    "Makefile=1 CMakeLists.txt=1 test/00regression/functional_tests.conf=1 tools/pgo-train.sh=1 .github/workflows/ci.yml=1 src/core/emulator.cpp=1 test/00regression/scripts/x-func.sh=1 gitlink=1"

out=""
for kp in doc/testing/X-TEST-PLAN-DESIGN.md doc/notes2.txt src/doc/user-guide/01-introduction/x.md \
          src/doc/developer-guide/diagrams/x.dot doc/user-guide/search/search_index.json \
          test/00regression/nextsync/README.md ChangeLog README.md; do
    out+="$kp=$(keymove "$kp") "
done
check "HS-71i" "documentation does not move the key — doc/, src/doc/, any *.md, ChangeLog, the rendered guides (GH #295)" 0 0 "$out" \
    "doc/testing/X-TEST-PLAN-DESIGN.md=0 doc/notes2.txt=0 src/doc/user-guide/01-introduction/x.md=0 src/doc/developer-guide/diagrams/x.dot=0 doc/user-guide/search/search_index.json=0 test/00regression/nextsync/README.md=0 ChangeLog=0 README.md=0"

out=""
for kp in doc/formats/jns-snapshot.schema.json doc/man/jnext.1.md doc/testing/CURRENT-REGRESSION-STATE.md \
          src/doc/user-guide/09-reference/01-command-line-options.md; do
    out+="$kp=$(keymove "$kp") "
done
check "HS-71j" "the documentation a regression gate READS moves the key: schema, man page, CLI guide page, regression state (GH #295)" 0 0 "$out" \
    "doc/formats/jns-snapshot.schema.json=1 doc/man/jnext.1.md=1 doc/testing/CURRENT-REGRESSION-STATE.md=1 src/doc/user-guide/09-reference/01-command-line-options.md=1"

# --- cleanliness at each end separately, and the counts (review round 1) ---
st=$(stamp state); echo 'int z;' >> "$SR/src/main.cpp"; st_dirty=$(stamp state); sg checkout -- src/main.cpp
out=$(stamp write "$st_dirty" pass=3 fail=0 skip=0; echo "files=$(find "$SD" -name "$(stamp key)" | wc -l)"); rc=$?
check "HS-71k" "dirty at the START only (clean again at the end): no stamp (GH #295)" 0 $rc "$out" \
    "no regression stamp: the tree had uncommitted non-doc changes (at start: 1, at end: 0" "files=0"

echo 'int z;' >> "$SR/src/main.cpp"
out=$(stamp write "$st" pass=3 fail=0 skip=0; echo "files=$(find "$SD" -name "$(git -C "$SR" rev-parse HEAD >/dev/null; stamp key)" | wc -l)"); rc=$?
sg checkout -- src/main.cpp
check "HS-71l" "clean at the start, dirty at the END only: no stamp (GH #295)" 0 $rc "$out" \
    "no regression stamp: the tree had uncommitted non-doc changes (at start: 0, at end: 1" "files=0"

st=$(stamp state)
out=$(stamp write "$st" pass=3 fail=0 skip=1; stamp write "$st" pass=3 fail=1 skip=0; stamp write "$st" pass=3)
out+=$'\n'"files=$(find "$SD" -name "$(stamp key)" | wc -l)"
check "HS-71m" "a SKIP, a FAIL, or no counts at all: never stamped (GH #295)" 0 0 "$out" \
    "this one: fail=0 skip=1" "this one: fail=1 skip=0" "this one: fail=missing skip=missing" "files=0"

# --- a loaded-host FAIL, pending, then confirmed solo (review round 1) ---
st=$(stamp state)
out=$(stamp pending "$st" "fails=b-func a-func" pass=3 fail=2 skip=0; stamp check; stamp pending-rows); rc=$?
check "HS-71n" "loaded-host FAILs are recorded as PENDING: not a stamp, and the check says what to re-run (GH #295)" 0 $rc "$out" \
    "NO STAMP YET: 2 row(s) failed on a loaded host" "NO STAMP for this tree" "PENDING solo re-runs of: b-func a-func" "b-func a-func"
out=$(stamp confirm "$st" "rows=a-func" fail=0 skip=0; stamp confirm "$st" "rows=a-func b-func" fail=1 skip=0; stamp check); rc=$?
check "HS-71o" "a solo re-run of the WRONG rows, or one that fails again, confirms nothing (GH #295)" 1 $rc "$out" \
    "the pending rows are 'b-func a-func'" "the pending FAIL stands" "NO STAMP for this tree"
out=$(stamp confirm "$st" "rows=a-func b-func" pass=8 fail=0 skip=0; stamp check; echo "pending-left=$(find "$SD" -name '*.pending' | wc -l)"); rc=$?
check "HS-71p" "every pending row passing solo stamps the run, naming them; the pending record is consumed (GH #295)" 0 $rc "$out" \
    "passed solo" "STAMP OK" "pass=5" "fail=0" "full_run_failed_on_loaded_host=b-func a-func" "confirmed_solo=a-func b-func" "pending-left=0"

# ---------------- display allocation must be race-free (review round 1) ----------------
# `xvfb-run -a` scans /tmp/.X<n>-lock for a free number, which is not atomic:
# with rows in parallel, two of them picked :101, one lost its X server and the
# row SKIPPED (no screenshot) — and the run stayed green. `-d` lets Xvfb choose
# the display itself (-displayfd): 64/64 concurrent starts clean under 16 busy
# loops, against 1/64 failing with -a. Banned in every tracked test script, and
# so is a fixed -n/--server-num, which two parallel rows would share.
XVFB_RACY_RE='xvfb-run([[:space:]]+(-[a-zA-Z]|--[a-z-]+(=("[^"]*"|'"'"'[^'"'"']*'"'"'|[^[:space:]]+))?))*[[:space:]]+(-a|--auto-servernum|-n|--server-num)([[:space:]=]|$)'
ctl=""
for t in 'xvfb-run -a cmd' 'xvfb-run --server-args="-screen 0 1x1x24" -a cmd' 'xvfb-run -n 99 cmd' 'xvfb-run --auto-servernum cmd'; do
    grep -qE "$XVFB_RACY_RE" <<<"$t" && ctl+=1 || ctl+=0
done
for t in 'xvfb-run -d cmd' 'xvfb-run -d --server-args="-screen 0 1x1x24" bash -c '"'"'head -n 5'"'"''; do
    grep -qE "$XVFB_RACY_RE" <<<"$t" && ctl+=1 || ctl+=0
done
# (this file is excluded: the control strings above are deliberate instances)
racy=$(cd "$PROJECT_DIR" && git ls-files 'test/*.sh' 'test/*.inc' 'tools/*.sh' | grep -vx 'test/harness-selftest.sh' \
       | xargs grep -nE "$XVFB_RACY_RE" 2>/dev/null || true)
check "HS-72" "no test script starts Xvfb on a non-atomically chosen display (xvfb-run -a/-n); the matcher's control (GH #295)" 0 0 \
    "control=$ctl racy=[${racy}]" "control=111100 racy=[]"

# ---------------- a nested harness never waits on its ancestor's lock (GH #295) ----------------
# `make regression-confirm` could not pass harness-selftest-func: the confirm
# run holds the host lock, JNEXT_REGRESSION_STAMP=confirm leaked into the
# self-test's own `regression.sh --preflight-only` children, each of them
# therefore took the slot and waited for the lock its ancestor held, until its
# bound (HS-21..31 exit 124 at load 0.5). This row is that shape, end to end,
# in a throwaway repository holding the REAL driver, library, scheduler, row
# runner and stamp script: a pending record for one row; a confirm run of it,
# under its own lock file; and the row itself invokes the harness again,
# exactly as harness-selftest-func does. The confirm must complete and stamp.
CR="$T/confirmrepo"; CRT="$CR/test/00regression"
mkdir -p "$CRT/scripts" "$CRT/img" "$T/cfix" "$T/cstamps"
cp "$PROJECT_DIR/test/00regression/regression.sh" "$PROJECT_DIR/test/00regression/test-functions.inc" \
   "$PROJECT_DIR/test/00regression/parallel-rows.inc" "$PROJECT_DIR/test/00regression/row-runner.sh" "$CRT/"
cp "$PROJECT_DIR/test/regression-stamp.sh" "$CR/test/"
printf '# expect: 0\n' > "$CRT/regression_tests.conf"
printf '# expect: 1\nnested-func\n' > "$CRT/functional_tests.conf"
: > "$CRT/scripts/00-preflight-lint.sh"; : > "$CRT/scripts/01-sdcard-provision.sh"
printf 'ORDERED_TESTS=()\n' > "$CRT/scripts/screenshots.sh"
# Two nested runs: the self-test's own shape (a plain --preflight-only, which
# only took the slot because STAMP=confirm leaked into it), and one that asks
# for the slot outright (LOCK=force, as a nested full run would) — that one
# must recognise its ancestor's lock instead of waiting for it.
cat > "$CRT/scripts/nested-func.sh" <<'NESTED'
if want nested-func; then
    begin_func nested-func
    nested_a=$(timeout --kill-after=5s 60s bash "$SCRIPT_DIR/regression.sh" --preflight-only 2>&1); nested_ra=$?
    nested_b=$(JNEXT_REGRESSION_LOCK=force timeout --kill-after=5s 60s bash "$SCRIPT_DIR/regression.sh" --preflight-only 2>&1); nested_rb=$?
    if [[ $nested_ra -eq 0 && $nested_rb -eq 0 ]]; then
        pass_row " (the nested harness ran: plain $(grep -c 'preflight OK' <<<"$nested_a"), forced $(grep -c 'preflight OK' <<<"$nested_b"); $(grep -o 'nested inside a run that holds' <<<"$nested_b"))"
    else
        fail_row " (a nested harness did not complete: rc $nested_ra/$nested_rb: $(tail -n 2 <<<"$nested_a$nested_b" | tr '\n' ' '))"
    fi
fi
NESTED
# the binaries a no-filter preflight insists on, as stand-ins (ignored by git)
mkdir -p "$CR/build/test" "$CR/build/sdl-release"
printf '#!/bin/sh\nexit 0\n' > "$CR/build/test/rewind_test"; cp "$CR/build/test/rewind_test" "$CR/build/sdl-release/jnext"
chmod +x "$CR/build/test/rewind_test" "$CR/build/sdl-release/jnext"
printf 'build/\n' > "$CR/.gitignore"
sg_cr() { git -C "$CR" -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }
sg_cr init -q; sg_cr add -A; sg_cr commit -qm fixture
cst=$(JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" bash "$CR/test/regression-stamp.sh" state)
JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" bash "$CR/test/regression-stamp.sh" pending "$cst" \
    "fails=nested-func" pass=0 fail=1 skip=0 rows=1 >/dev/null 2>&1
t0=$SECONDS
out=$(cd "$CR" && HOME="$T/cfix" JNEXT=/bin/true JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" \
      JNEXT_REGRESSION_LOCK_FILE="$T/confirm.lock" JNEXT_REGRESSION_LOCK_WAIT=6 \
      JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-idle" JNEXT_REGRESSION_NPROC=12 JNEXT_REGRESSION_HEARTBEAT=0 \
      JNEXT_REGRESSION_STAMP=confirm JNEXT_TEST_JOBS=1 \
      timeout --kill-after=5s 120s bash "$CRT/regression.sh" nested-func 2>&1); rc=$?
out+=$'\n'"$(JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" bash "$CR/test/regression-stamp.sh" check 2>&1)"
out+=$'\n'"fast=$(( SECONDS - t0 < 60 ? 1 : 0 ))"
check "HS-73" "a confirm run whose row runs the harness again completes and stamps — no wait on its own ancestor's lock (GH #295)" 0 $rc "$out" \
    "[nested-func]" "the nested harness ran: plain 1, forced 1; nested inside a run that holds" \
    "passed solo" "STAMP OK" "confirmed_solo=nested-func" "fast=1"

# ...and the marker is EVIDENCE, never a password (review of HS-73's fix). An
# entry counts only for a live ancestor, with its /proc start time, that holds
# the lock right now. A hand-exported marker, an entry naming a live ancestor
# that does NOT hold the lock, a stale entry of a run that has exited, and a
# live holder that is NOT an ancestor must each leave this run a normal second
# full run: it waits for the lock, bounded, and is a harness fault at the bound.
proc_start() { local st; st=$(cat "/proc/$1/stat" 2>/dev/null) || return 0; st=${st##*) }; set -- $st; echo "${20:-}"; }
flock "$lockf" sleep 40 & holder=$!
sleep 1
gone=$(bash -c 'echo $$'); gone_start=unknown   # a pid that has exited
out=""
for marker in "$lockf" "$lockf|$$|$(proc_start $$)" "$lockf|$gone|$gone_start"; do
    o=$(slot_run JNEXT_REGRESSION_LOCK=force JNEXT_REGRESSION_LOCK_WAIT=5 JNEXT_REGRESSION_ANCESTOR_LOCKS="$marker"); r=$?
    out+="rc=$r nested=$(count_of "$o" "nested inside") waited=$(count_of "$o" "holds the host lock") "
done
check "HS-74a" "a hand-exported, non-holding or stale ancestor-lock marker does NOT bypass the host lock (GH #295)" 0 0 "$out" \
    "rc=2 nested=0 waited=1 rc=2 nested=0 waited=1 rc=2 nested=0 waited=1"
o=$(slot_run JNEXT_REGRESSION_LOCK=force JNEXT_REGRESSION_LOCK_WAIT=5 \
             JNEXT_REGRESSION_ANCESTOR_LOCKS="$lockf|$holder|$(proc_start "$holder")"); r=$?
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
out="rc=$r nested=$(count_of "$o" "nested inside") waited=$(count_of "$o" "holds the host lock")"
check "HS-74b" "a marker naming the LIVE lock holder that is not an ancestor does not bypass it either (GH #295)" 0 0 "$out" \
    "rc=2 nested=0 waited=1"

# A SYMLINKED lock path: flock(1) locks the link's TARGET, so the holder check
# must compare the resolved file — comparing the link's own inode judged a
# genuine nested child "not held" and it waited on its ancestor (review). The
# same confirm path as HS-73, its lock file now a symlink.
ln -sfn "$T/confirm-real.lock" "$T/confirm-sym.lock"; : > "$T/confirm-real.lock"
JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" bash "$CR/test/regression-stamp.sh" pending "$cst" \
    "fails=nested-func" pass=0 fail=1 skip=0 rows=1 >/dev/null 2>&1
t0=$SECONDS
out=$(cd "$CR" && HOME="$T/cfix" JNEXT=/bin/true JNEXT_REGRESSION_STAMP_DIR="$T/cstamps" \
      JNEXT_REGRESSION_LOCK_FILE="$T/confirm-sym.lock" JNEXT_REGRESSION_LOCK_WAIT=6 \
      JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-idle" JNEXT_REGRESSION_NPROC=12 JNEXT_REGRESSION_HEARTBEAT=0 \
      JNEXT_REGRESSION_STAMP=confirm JNEXT_TEST_JOBS=1 \
      timeout --kill-after=5s 120s bash "$CRT/regression.sh" nested-func 2>&1); rc=$?
out+=$'\n'"fast=$(( SECONDS - t0 < 60 ? 1 : 0 ))"
check "HS-74c" "with a SYMLINKED lock file a genuine nested child still recognises its ancestor's lock (GH #295)" 0 $rc "$out" \
    "the nested harness ran: plain 1, forced 1; nested inside a run that holds)" "passed solo" "fast=1"

# ---------------- a SKIPPED row fails the regression driver (owner, 2026-10-06) ----------------
# A SKIP is a row that was not tested; the driver used to exit 0 on it (only the
# STAMP refused it), so `make regression` read green while a row had not run.
# Here: a throwaway repository holding the REAL driver, library, scheduler, row
# runner and stamp script, two rows (one passes, one calls skip_row), run as a
# full run (HS-77a), as a named-row run (HS-77b), the control with only the
# passing row (HS-77c), and a standalone row script (HS-77d).
SR="$T/skiprepo"; SRT="$SR/test/00regression"
mkdir -p "$SRT/scripts" "$SRT/img" "$T/sfix" "$T/sstamps"
cp "$PROJECT_DIR/test/00regression/regression.sh" "$PROJECT_DIR/test/00regression/test-functions.inc" \
   "$PROJECT_DIR/test/00regression/parallel-rows.inc" "$PROJECT_DIR/test/00regression/row-runner.sh" "$SRT/"
cp "$PROJECT_DIR/test/regression-stamp.sh" "$SR/test/"
printf '# expect: 0\n' > "$SRT/regression_tests.conf"
# the seven group rows a full run counts (6 lints + sdcard-provision), as stand-ins
cat > "$SRT/scripts/00-preflight-lint.sh" <<'GROUP'
for lint_n in 1 2 3 4 5 6; do CURRENT_ROW="lint-$lint_n"; pass_row; done
GROUP
printf 'CURRENT_ROW=sdcard-provision; pass_row\n' > "$SRT/scripts/01-sdcard-provision.sh"
printf 'ORDERED_TESTS=()\n' > "$SRT/scripts/screenshots.sh"
printf 'if want ok-func; then begin_func ok-func; pass_row; fi\n' > "$SRT/scripts/ok-func.sh"
printf 'if want skip-func; then begin_func skip-func; skip_row " (stub: tool missing)"; fi\n' > "$SRT/scripts/skip-func.sh"
mkdir -p "$SR/build/test" "$SR/build/sdl-release"
printf '#!/bin/sh\nexit 0\n' > "$SR/build/test/rewind_test"; cp "$SR/build/test/rewind_test" "$SR/build/sdl-release/jnext"
chmod +x "$SR/build/test/rewind_test" "$SR/build/sdl-release/jnext"
printf 'build/\n' > "$SR/.gitignore"
sg_sr() { git -C "$SR" -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }
skip_run() {   # skip_run <functional manifest rows, space separated> <regression.sh args...>
    local rows=$1; shift
    { echo "# expect: $(wc -w <<<"$rows")"; tr ' ' '\n' <<<"$rows"; } > "$SRT/functional_tests.conf"
    sg_sr init -q; sg_sr add -A; sg_sr commit -qm fixture --allow-empty
    ( cd "$SR" && HOME="$T/sfix" JNEXT=/bin/true JNEXT_REGRESSION_STAMP_DIR="$T/sstamps" \
      JNEXT_REGRESSION_LOCK_FILE="$T/skip.lock" JNEXT_REGRESSION_LOCK_WAIT=6 \
      JNEXT_REGRESSION_LOADAVG_FILE="$T/loadavg-idle" JNEXT_REGRESSION_NPROC=12 JNEXT_REGRESSION_HEARTBEAT=0 \
      JNEXT_REGRESSION_STAMP=1 JNEXT_TEST_JOBS=1 \
      timeout --kill-after=5s 120s bash "$SRT/regression.sh" "$@" 2>&1 )
}
out=$(skip_run "ok-func skip-func"); rc=$?
out+=$'\n'"$(JNEXT_REGRESSION_STAMP_DIR="$T/sstamps" bash "$SR/test/regression-stamp.sh" check 2>&1)"
check "HS-77a" "a full regression run with a SKIPPED row exits 1, names the row, says a SKIP is not a pass, and writes no stamp" 1 $rc "$out" \
    "SKIPPED, i.e. not tested: skip-func" "a SKIP is not a pass" "no regression stamp" "NO STAMP"
out=$(skip_run "ok-func skip-func" skip-func); rc=$?
check "HS-77b" "a named-row regression run whose row SKIPS exits 1 and names it" 1 $rc "$out" \
    "SKIPPED, i.e. not tested: skip-func" "a SKIP is not a pass"
rm -f "$SRT/scripts/skip-func.sh"   # a declared-less script is a harness fault, not the control
out=$(skip_run "ok-func"); rc=$?
out+=$'\n'"$(JNEXT_REGRESSION_STAMP_DIR="$T/sstamps" bash "$SR/test/regression-stamp.sh" check 2>&1)"
check "HS-77c" "the control: the same fixture with no SKIPPED row exits 0 and stamps" 0 $rc "$out" \
    "regression stamp written" "STAMP OK"
out=$(cd "$SR" && JNEXT=/bin/true timeout --kill-after=3s 30s bash -c \
      "set -euo pipefail; source '$SRT/test-functions.inc'; CURRENT_ROW=lone-row; skip_row ' (stub)'; standalone_summary" 2>&1); rc=$?
check "HS-77d" "a standalone row script that SKIPS exits 1 from standalone_summary" 1 $rc "$out" "a SKIP is not a pass"

# ---------------- a missing TOOL or renderer-version gap FAILS, never skips (owner, 2026-10-06) ----------------
# Every check below runs a REAL script of the project with a tool hidden from
# PATH (a directory of symlinks to everything on PATH except the named tools),
# or with a fake committed-renderer fingerprint, and expects a non-zero exit.
# Reverting any one of those branches to "SKIP, exit 0" turns exactly its row red.
shim_without() {   # shim_without <tool...> — echo a PATH that lacks them
    local d dir f b skip x
    d=$(mktemp -d "$T/shim.XXXXXX")
    local IFS=:
    for dir in $PATH; do
        [[ -d "$dir" ]] || continue
        for f in "$dir"/*; do
            [[ -x "$f" && ! -d "$f" ]] || continue
            b=${f##*/}; skip=0
            for x in "$@"; do [[ "$b" == "$x" ]] && skip=1; done
            [[ $skip -eq 1 || -e "$d/$b" ]] || ln -s "$f" "$d/$b"
        done
    done
    echo "$d"
}
mk_fake_fp() { printf 'a fingerprint no real renderer prints\n' > "$T/fakefp.txt"; echo "$T/fakefp.txt"; }
run_in_repo() {   # run_in_repo <PATH> <cmd...>
    local p=$1; shift
    ( cd "$PROJECT_DIR" && PATH="$p" timeout --kill-after=5s 300s "$@" 2>&1 )
}
# the ffmpeg row, standalone, with a stand-in jnext that always prints the warning
NOFF=$(shim_without ffmpeg ffprobe)
printf '#!/bin/sh\necho "ffmpeg not found in PATH"\n' > "$T/fakejnext.sh"; chmod +x "$T/fakejnext.sh"
out=$(cd "$PROJECT_DIR" && PATH="$NOFF" JNEXT="$T/fakejnext.sh" timeout --kill-after=5s 120s \
      bash test/00regression/scripts/ffmpeg-missing-warn-func.sh 2>&1); rc=$?
check "HS-78" "ffmpeg-missing-warn-func with no ffmpeg on the host SKIPS (exit 1), it does not PASS with its control half unrun" 1 $rc "$out" \
    "SKIP" "control not run" "a SKIP is not a pass"
# the real sync-version-test with rpmspec hidden: its rpm rows SKIP and the suite must exit non-zero
out=$(run_in_repo "$(shim_without rpmspec)" bash test/packaging/sync-version-test.sh); rc=$?
check "HS-79" "sync-version-test.sh with rpmspec hidden exits non-zero and says a SKIP is not a pass" 1 $rc "$out" \
    "SKIP" "rpmspec not installed" "a SKIP is not a pass"
out=$(run_in_repo "$(shim_without pandoc)" make docs-man-check); rc=$?
check "HS-80a" "docs-man-check without pandoc FAILS" 2 $rc "$out" "pandoc not installed"
out=$(run_in_repo "$PATH" make docs-man-check MAN_RENDERER="$(mk_fake_fp)"); rc=$?
check "HS-80b" "docs-man-check with a different pandoc than the committed renders FAILS" 2 $rc "$out" "different pandoc"
out=$(run_in_repo "$(shim_without mkdocs)" make docs-userguide-check); rc=$?
check "HS-80c" "docs-userguide-check without mkdocs FAILS" 2 $rc "$out" "mkdocs not installed"
out=$(run_in_repo "$PATH" make docs-userguide-check GUIDE_RENDERER="$(mk_fake_fp)"); rc=$?
check "HS-80d" "docs-userguide-check with a different mkdocs than the committed render FAILS" 2 $rc "$out" "different mkdocs/material"
out=$(run_in_repo "$(shim_without dot)" make docs-devguide-check); rc=$?
check "HS-80e" "docs-devguide-check without graphviz FAILS" 2 $rc "$out" "not installed"
out=$(run_in_repo "$PATH" make docs-devguide-check DEVGUIDE_RENDERER="$(mk_fake_fp)"); rc=$?
check "HS-80f" "docs-devguide-check with a different renderer than the committed render FAILS" 2 $rc "$out" "different mkdocs/material/graphviz"
out=$(run_in_repo "$(shim_without python3)" bash test/snapshot/verify-schema.sh); rc=$?
check "HS-80g" "verify-schema.sh without python3/jsonschema FAILS" 1 $rc "$out" "FAIL" "jsonschema is not available"
out=$(run_in_repo "$(shim_without unzip)" bash test/snapshot/verify-external-zip.sh); rc=$?
check "HS-80h" "verify-external-zip.sh without unzip FAILS" 1 $rc "$out" "FAIL" "unzip is not installed"

# A set-but-unusable Z88DK_GDB is an error, never a quiet fall-through to another client
out=$(Z88DK_GDB="$T/no-such-client" bash "$PROJECT_DIR/test/provision-z88dk-gdb.sh" --print-path 2>&1); rc=$?
check "HS-81a" "provision-z88dk-gdb.sh --print-path with Z88DK_GDB set to a non-executable exits 2, naming it" 2 $rc "$out" "Z88DK_GDB=" "not an executable file"
out=$(Z88DK_GDB="$T/no-such-client" bash "$PROJECT_DIR/test/provision-z88dk-gdb.sh" 2>&1); rc=$?
check "HS-81b" "...and so does the provisioning run itself (it does not go and build another one)" 2 $rc "$out" "not an executable file"

# ---------------- the harness's git keeps the user's own config (GH #295) ----------------
# The suite points XDG_CONFIG_HOME at a scratch dir for Qt, and git resolves
# its global excludes file through it: on the main checkout a file ignored only
# by ~/.config/git/ignore read as untracked and a 216/216 run got no stamp.
# Here: a throwaway repo, a fake ORIGINAL XDG_CONFIG_HOME whose git/ignore
# ignores one planted untracked file, and the stamp asked for from inside the
# REAL suite library (which isolates XDG_CONFIG_HOME). The globally-ignored
# file must not make the tree dirty; an untracked file nobody ignores must.
XR="$T/xdgrepo"; XO="$T/xdg-original"; XH="$T/xdg-home"
mkdir -p "$XR/test" "$XO/git" "$XH"
cp "$PROJECT_DIR/test/regression-stamp.sh" "$XR/test/"
echo 'int main(){}' > "$XR/main.cpp"
git -C "$XR" -c user.name=t -c user.email=t@t init -q && git -C "$XR" add -A \
    && git -C "$XR" -c user.name=t -c user.email=t@t commit -qm base
printf 'globally-ignored.txt\n' > "$XO/git/ignore"
echo local > "$XR/globally-ignored.txt"
# The child is an OUTERMOST run: when this self-test itself runs inside the
# suite (harness-selftest-func) it inherits the suite's captured original,
# which belongs to that run, not to this fake one — so it is dropped here.
xdg_stamp() {   # xdg_stamp — state + write, from inside the real suite library
    env -u JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME \
    HOME="$XH" XDG_CONFIG_HOME="$XO" JNEXT_REGRESSION_STAMP_DIR="$T/xdgstamps" \
        timeout --kill-after=3s 30s bash -c \
        "set -euo pipefail
         source '$PROJECT_DIR/test/00regression/test-functions.inc'
         echo \"isolated=\$([[ \$XDG_CONFIG_HOME != '$XO' ]] && echo 1 || echo 0)\"
         st=\$(bash '$XR/test/regression-stamp.sh' state); echo \"\$st\"
         bash '$XR/test/regression-stamp.sh' write \"\$st\" pass=1 fail=0 skip=0" 2>&1
}
out=$(xdg_stamp); rc=$?
check "HS-75a" "an untracked file ignored only by the user's GLOBAL git ignore does not stop the stamp (GH #295)" 0 $rc "$out" \
    "isolated=1" "dirty=0" "regression stamp written"
echo other > "$XR/not-ignored.txt"
out=$(xdg_stamp); rc=$?
check "HS-75b" "the control: an untracked file nobody ignores still makes the tree dirty — no stamp (GH #295)" 0 $rc "$out" \
    "isolated=1" "dirty=1" "no regression stamp: the tree had uncommitted non-doc changes" "not-ignored.txt"


# ---------------- the target OS and `# os:` (GH #214) ----------------
# '# os: all|posix|linux' says which TARGET OSes own a suite, and the harness reads
# the OS from the build tree's own CMakeCache.txt (JNEXT_TARGET_OS), exactly as it
# reads the gates. Absence is CHECKED both ways, like the gates: HS-82/83.
stub good_test 10 0; stub posix_only_test 7 0
register good_test posix_only_test        # posix_only_test registered...
cache ON ON windows                       # ...on an OS its `# os: posix` excludes
manifest "good_test 10" "# os: posix" "?posix_only_test 7"
out=$(run_harness); rc=$?
check "HS-82" "a suite registered on an OS its '# os:' excludes is a refusal" 2 $rc "$out" \
    "REFUSES TO RUN" "posix_only_test" "gates it to"

register good_test                        # posix_only_test GONE from CMake...
cache ON ON macos                         # ...on an OS that owns it (posix = linux + macos)
manifest "good_test 10" "# os: posix" "?posix_only_test 7"
out=$(run_harness); rc=$?
check "HS-83" "a suite MISSING from an OS that owns it is a refusal" 2 $rc "$out" \
    "REFUSES TO RUN" "posix_only_test" "NOT registered by CMake"

register good_test                        # absent, and excluded: expected, NOTICE
cache ON ON macos                         # (`# os: linux` does not include macOS)
manifest "good_test 10" "# os: linux" "?posix_only_test 7"
out=$(run_harness); rc=$?
check "HS-83a" "a suite os-gated out by this target: NOTICE naming it and the OS, not silence" 0 $rc "$out" \
    "NOTICE" "gated out" "posix_only_test" "os=macos" "Suites: 1 pass, 0 fail"

register good_test
cache ON ON linux
manifest "good_test 10" "# os: bsd" "?posix_only_test 7"
out=$(run_harness); rc=$?
check "HS-84" "an unknown '# os:' value is a refusal, not an os that never applies" 2 $rc "$out" \
    "REFUSES TO RUN" "Unknown os"

register good_test
cache ON ON linux
manifest "good_test 10" "# os: posix" "posix_only_test 7"   # os-restricted, but no '?'
out=$(run_harness); rc=$?
check "HS-85a" "an os-restricted suite without the '?' marker is a refusal (the two must agree)" 2 $rc "$out" \
    "REFUSES TO RUN" "not marked"

register good_test
cache ON ON plan9
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-85b" "a build tree naming an unknown target OS is a refusal, not a guess" 2 $rc "$out" \
    "REFUSES TO RUN" "Unknown target OS"

# ...and a cache that does not say WHICH OS is a refusal too, not a default of linux
# (the harness cannot know what the suites owe it from a guess).
register good_test
cache ON ON linux
sed -i '/^JNEXT_TARGET_OS/d' "$T/build/CMakeCache.txt"
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-85c" "a CMakeCache without JNEXT_TARGET_OS is a refusal naming it, not an assumed linux" 2 $rc "$out" \
    "REFUSES TO RUN" "JNEXT_TARGET_OS"

# A Windows build registers NAME.exe and runs under a runner (wine). The manifest names
# the suite; the harness maps the .exe, checks the .exe is built, and prefixes the runner.
stub good_test 10 0
mv "$T/build/test/good_test" "$T/build/test/good_test.exe"
: > "$T/build/test/CTestTestfile.cmake"
echo "add_test(good_tests \"$T/build/test/good_test.exe\")" >> "$T/build/test/CTestTestfile.cmake"
printf 'good_test\t%s\n' "$T/build/test/good_test.exe" > "$T/sources.tsv"
cache ON ON windows
manifest "good_test 10"
out=$(run_harness); rc=$?
check "HS-86a" "a Windows build's NAME.exe registration matches the manifest and the suite runs" 0 $rc "$out" \
    "good_test" "Total: 10" "os=windows"
printf '#!/usr/bin/env bash\necho "runner ran $*" >"%s/runner.log"\nexec "$@"\n' "$T" > "$T/runner.sh"
chmod +x "$T/runner.sh"
out=$(JNEXT_TEST_RUNNER="$T/runner.sh" run_harness); rc=$?
runner_log=$(cat "$T/runner.log" 2>/dev/null || true)
check "HS-86b" "JNEXT_TEST_RUNNER prefixes every suite (the .exe is what it is handed)" 0 $rc "$out" "Total: 10"
check "HS-86c" "...and the runner really received the suite's .exe path" 0 $([[ "$runner_log" == "runner ran $T/build/test/good_test.exe" ]] && echo 0 || echo 1) "$runner_log" "runner ran"
rm -f "$T/build/test/good_test.exe"
out=$(run_harness); rc=$?
check "HS-86d" "a Windows build whose NAME.exe is not built is a refusal naming the .exe" 2 $rc "$out" \
    "REFUSES TO RUN" "NOT built" "good_test.exe"

# The userland preflight: bash >= 4, GNU timeout/grep -P, perl. A refusal naming what
# to install, never a quiet degradation (macOS ships bash 3.2 and BSD tools).
stub good_test 10 0
register good_test
cache ON ON linux
manifest "good_test 10"
out=$(JNEXT_PREFLIGHT_BASH_MAJOR=3 run_harness); rc=$?
check "HS-87a" "a bash older than 4 is a refusal that names the install line" 2 $rc "$out" \
    "REFUSES TO RUN" "bash >= 4" "brew install bash coreutils grep"
NOTIMEOUT=$(shim_without timeout)
out=$(JNEXT_UNIT_TEST_CONF="$T/manifest.conf" JNEXT_UNIT_TEST_SOURCES="$T/sources.tsv" \
      "$(type -P timeout)" --kill-after=5s 120s env PATH="$NOTIMEOUT" bash "$HARNESS" "$T/build" 2>&1); rc=$?
check "HS-87b" "a host without GNU timeout is a refusal, not a run that cannot bound its suites" 2 $rc "$out" \
    "REFUSES TO RUN" "GNU timeout"
# A grep that cannot do -P (BSD/macOS grep): a shim directory whose grep refuses the
# flag and defers to the real one otherwise, so only the preflight's own probe sees it.
NOGREPP=$(shim_without grep)
real_grep=$(type -P grep)
printf '#!/usr/bin/env bash\nfor a in "$@"; do case "$a" in -*P*) echo "grep: invalid option -- P" >&2; exit 2 ;; esac; done\nexec "%s" "$@"\n' "$real_grep" > "$NOGREPP/grep"
chmod +x "$NOGREPP/grep"
out=$(JNEXT_UNIT_TEST_CONF="$T/manifest.conf" JNEXT_UNIT_TEST_SOURCES="$T/sources.tsv" \
      "$(type -P timeout)" --kill-after=5s 120s env PATH="$NOGREPP" bash "$HARNESS" "$T/build" 2>&1); rc=$?
check "HS-87d" "a host whose grep cannot do -P is a refusal naming GNU grep" 2 $rc "$out" \
    "REFUSES TO RUN" "GNU grep"
NOPERL=$(shim_without perl)
out=$(PATH="$NOPERL" run_harness); rc=$?
check "HS-87c" "a host without perl is a refusal (the row-ID literal check needs it)" 2 $rc "$out" \
    "REFUSES TO RUN" "perl"

# JNEXT_UNIT_TEST_JOBS caps the suites running at once: with 1, two suites that each
# hold a lock for 0.3 s never overlap; with no cap they do (the control, so the row
# cannot pass because the stubs are too quick to collide).
overlap_body='if ! mkdir "'"$T"'/lock" 2>/dev/null; then echo overlap >>"'"$T"'/overlap"; else sleep 0.3; rmdir "'"$T"'/lock"; fi'
stub job_a_test 3 0 "$overlap_body"; stub job_b_test 4 0 "$overlap_body"
register job_a_test job_b_test
cache ON ON linux
manifest "job_a_test 3" "job_b_test 4"
rm -f "$T/overlap"; rmdir "$T/lock" 2>/dev/null || true
out=$(run_harness); rc=$?
uncapped_overlap=$([[ -f "$T/overlap" ]] && echo 1 || echo 0)
rm -f "$T/overlap"; rmdir "$T/lock" 2>/dev/null || true
out2=$(JNEXT_UNIT_TEST_JOBS=1 run_harness); rc2=$?
capped_overlap=$([[ -f "$T/overlap" ]] && echo 1 || echo 0)
check "HS-88a" "the control: with no cap the two lock-holding suites DO overlap" 0 $((uncapped_overlap == 1 ? 0 : 1)) "overlap=$uncapped_overlap" "overlap=1"
check "HS-88b" "JNEXT_UNIT_TEST_JOBS=1 runs them one at a time (no overlap) and both still count" 0 $rc2 "$out2" "Total: 7"
check "HS-88c" "...and the overlap flag stays unset under the cap" 0 $((capped_overlap == 0 ? 0 : 1)) "overlap=$capped_overlap" "overlap=0"
out=$(JNEXT_UNIT_TEST_JOBS=lots run_harness); rc=$?
check "HS-88d" "a non-numeric JNEXT_UNIT_TEST_JOBS is a refusal" 2 $rc "$out" "REFUSES TO RUN" "JNEXT_UNIT_TEST_JOBS"

# ---------------------------------------------------------------------------
# GH #319: regression.sh --platform (macOS / wine runs), the os= tag grammar and
# the per-OS pins, and the wine runner's --jnext mode. The oracle is the harness
# contract itself (no VHDL involved).
# ---------------------------------------------------------------------------
plat_tree() {   # plat_tree <dir> <qt-os> <sdl-os> — two configured build trees' caches
    mkdir -p "$1/q" "$1/s"
    printf 'JNEXT_TARGET_OS:INTERNAL=%s\n' "$2" > "$1/q/CMakeCache.txt"
    printf 'JNEXT_TARGET_OS:INTERNAL=%s\n' "$3" > "$1/s/CMakeCache.txt"
}
run_plat() {   # run_plat <dir> <func_conf> [extra args] — a platform preflight
    local d=$1 fc=$2; shift 2
    JNEXT_TARGET_BUILD="$d/q" JNEXT_TARGET_SDL_BUILD="$d/s" \
    JNEXT_REGRESSION_CONF="$REG_CONF" JNEXT_REGRESSION_FUNC_CONF="$fc" \
        timeout --kill-after=5s "${INVOKE_TIMEOUT_OVERRIDE:-$INVOKE_TIMEOUT}s" \
        bash "$REG" --preflight-only --platform "$@" 2>&1
}
retag() {   # retag <out> <row> <tag-list> — the real conf with <row>'s os= replaced (or added)
    awk -v r="$2" -v t="$3" '$1 == r { print $1 " " t; next } { print }' "$REG_FUNC" > "$1"
}

retag "$T/os-bad.conf" magic-bp-func "os=linux,beos"
out=$(run_preflight "$REG_CONF" "$T/os-bad.conf"); rc=$?
check "HS-90a" "an os= tag naming an unknown OS is a refusal naming it" 2 $rc "$out" "HARNESS FAULT" "unknown OS" "beos"
retag "$T/os-empty.conf" magic-bp-func "os="
out=$(run_preflight "$REG_CONF" "$T/os-empty.conf"); rc=$?
check "HS-90b" "an empty os= tag is a refusal" 2 $rc "$out" "HARNESS FAULT" "empty"
retag "$T/os-nolinux.conf" magic-bp-func "os=macos,windows"
out=$(run_preflight "$REG_CONF" "$T/os-nolinux.conf"); rc=$?
check "HS-90c" "an os= list without linux is a refusal (a Linux run runs every row)" 2 $rc "$out" "HARNESS FAULT" "does not list linux"
retag "$T/os-malformed.conf" magic-bp-func "os=linux,,macos"
out=$(run_preflight "$REG_CONF" "$T/os-malformed.conf"); rc=$?
check "HS-90d" "a malformed os= list (empty element) is a refusal" 2 $rc "$out" "HARNESS FAULT" "malformed"
# retagged rows change the per-OS counts, so the pins refuse them: the tags themselves are valid.
retag "$T/os-ok.conf" magic-bp-func "os=linux,macos,windows"
out=$(run_preflight "$REG_CONF" "$T/os-ok.conf"); rc=$?
check "HS-90e" "the control: an explicit full os= list on one row is accepted (counts unchanged)" 0 $rc "$out" "preflight OK"

plat_tree "$T/pt-ok" windows windows
out=$(run_plat "$T/pt-ok" "$REG_FUNC"); rc=$?
check "HS-91a" "a platform preflight names the target and the admitted / absent row counts" 0 $rc "$out" \
    "platform windows: 127 functional rows run, 20 declared absent"
plat_tree "$T/pt-mac" macos macos
out=$(run_plat "$T/pt-mac" "$REG_FUNC"); rc=$?
check "HS-91b" "...macOS runs 133 and declares 14 absent" 0 $rc "$out" "platform macos: 133 functional rows run, 14 declared absent"
plat_tree "$T/pt-none" windows windows; sed -i '/^JNEXT_TARGET_OS/d' "$T/pt-none/s/CMakeCache.txt"
out=$(run_plat "$T/pt-none" "$REG_FUNC"); rc=$?
check "HS-91c" "a build tree without JNEXT_TARGET_OS is a refusal naming it, not an assumed OS" 2 $rc "$out" "HARNESS FAULT" "JNEXT_TARGET_OS"
plat_tree "$T/pt-unk" windows haiku
out=$(run_plat "$T/pt-unk" "$REG_FUNC"); rc=$?
check "HS-91d" "an unknown target OS is a refusal" 2 $rc "$out" "HARNESS FAULT" "unknown target OS" "haiku"
plat_tree "$T/pt-dis" windows macos
out=$(run_plat "$T/pt-dis" "$REG_FUNC"); rc=$?
check "HS-91e" "Qt and SDL-only trees that disagree about the OS are a refusal" 2 $rc "$out" "HARNESS FAULT" "disagree"
plat_tree "$T/pt-lin" linux linux
out=$(run_plat "$T/pt-lin" "$REG_FUNC"); rc=$?
check "HS-91f" "--platform against Linux trees is a refusal (a Linux run is plain make regression)" 2 $rc "$out" "HARNESS FAULT" "targets linux"
out=$(JNEXT_REGRESSION_CONF="$REG_CONF" JNEXT_REGRESSION_FUNC_CONF="$REG_FUNC" timeout --kill-after=5s 120s \
      bash "$REG" --preflight-only --platform 2>&1); rc=$?
check "HS-91g" "--platform without JNEXT_TARGET_BUILD is a refusal naming it" 2 $rc "$out" "HARNESS FAULT" "JNEXT_TARGET_BUILD"

out=$(run_plat "$T/pt-ok" "$REG_FUNC" video-record-func); rc=$?
check "HS-92a" "a named row this OS declares absent is refused in a platform run" 2 $rc "$out" "HARNESS FAULT" "declared absent on windows"
out=$(run_plat "$T/pt-ok" "$REG_FUNC" magic-bp-func); rc=$?
check "HS-92b" "...and a named admitted row is accepted" 0 $rc "$out" "preflight OK"

# per-OS pins, both directions
sed 's/^# expect-windows: 127$/# expect-windows: 126/' "$REG_FUNC" > "$T/pin-lo.conf"
out=$(run_preflight "$REG_CONF" "$T/pin-lo.conf"); rc=$?
check "HS-93a" "a Windows pin one too low is a refusal naming the OS" 2 $rc "$out" "HARNESS FAULT" "windows" "expect-windows: 126"
sed 's/^# expect-macos: 133$/# expect-macos: 134/' "$REG_FUNC" > "$T/pin-hi.conf"
out=$(run_preflight "$REG_CONF" "$T/pin-hi.conf"); rc=$?
check "HS-93b" "a macOS pin one too high is a refusal" 2 $rc "$out" "HARNESS FAULT" "macos" "expect-macos: 134"
grep -v '^# expect-macos:' "$REG_FUNC" > "$T/pin-none.conf"
out=$(run_preflight "$REG_CONF" "$T/pin-none.conf"); rc=$?
check "HS-93c" "os= tags without the per-OS pin are a refusal" 2 $rc "$out" "HARNESS FAULT" "No '# expect-macos: N' pin"
sed 's/^\(video-record-func\) .*/\1/' "$REG_FUNC" > "$T/untag.conf"
out=$(run_preflight "$REG_CONF" "$T/untag.conf"); rc=$?
check "HS-93d" "removing a row's os= tag without updating the pin is a refusal (the Windows count moved)" 2 $rc "$out" "HARNESS FAULT" "expect-windows"

# the wine runner's --jnext mode, against a stub wine
wstub="$T/wine-stub"
cat > "$wstub" <<'WEOF'
#!/bin/bash
# stub wine: report what a Windows process would see, per the first argument
case "${2:-}" in
    env)  echo "QPA=${QT_QPA_PLATFORM:-unset} WQPA=${WINEQT_QPA_PLATFORM:-unset} SDLV=${WINESDL_VIDEODRIVER:-unset}" ;;
    crlf) printf 'out line\r\n'; printf 'err line\r\n' >&2 ;;
    exit) exit 7 ;;
    hang) trap 'echo got-term; exit 9' TERM; echo ready; while :; do sleep 0.1; done ;;
esac
WEOF
chmod +x "$wstub"
wr() { JNEXT_WINE_BIN="$wstub" WINEPREFIX="$T/wp" bash "$PROJECT_DIR/test/wine-run.sh" --jnext "$T/fake.exe" "$@"; }
touch "$T/fake.exe"
wrun() { JNEXT_WINE_BIN="$wstub" WINEPREFIX="$T/wp" bash "$PROJECT_DIR/test/wine-run.sh" --jnext "$T/fake.exe" "$@"; }
out=$(QT_QPA_PLATFORM=offscreen SDL_VIDEODRIVER=dummy wrun env 2>&1); rc=$?
check "HS-94a" "the runner passes QT_* / SDL_* to a Windows process as WINEQT_* / WINESDL_*" 0 $rc "$out" "WQPA=offscreen" "SDLV=dummy"
n=$(wrun crlf 2>/dev/null | tr -cd '\r' | wc -c)
check "HS-94b" "the runner folds CRLF to LF on stdout" 0 $((n == 0 ? 0 : 1)) "cr=$n" "cr=0"
n=$(wrun crlf 2>&1 >/dev/null | tr -cd '\r' | wc -c)
check "HS-94c" "...and on stderr" 0 $((n == 0 ? 0 : 1)) "cr=$n" "cr=0"
n=$(wrun crlf 2>&1 | tr -cd '\n' | wc -l)
check "HS-94d" "...and the text survives (both lines arrive)" 0 $((n == 2 ? 0 : 1)) "lines=$n" "lines=2"
out=$(wrun exit 2>&1); rc=$?
check "HS-94e" "wine's exit status is the runner's exit status" 7 $rc "$out"
JNEXT_WINE_BIN="$wstub" WINEPREFIX="$T/wp" bash "$PROJECT_DIR/test/wine-run.sh" --jnext "$T/fake.exe" hang > "$T/hang.out" 2>&1 &
hang_pid=$!
for _ in $(seq 1 50); do grep -q ready "$T/hang.out" 2>/dev/null && break; sleep 0.1; done
kill -TERM "$hang_pid" 2>/dev/null
hang_rc=0; wait "$hang_pid" || hang_rc=$?
out=$(cat "$T/hang.out")
check "HS-94f" "a TERM sent to the runner reaches wine (the stub saw it; its status 9 comes back)" 9 $hang_rc "$out" "got-term"

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
