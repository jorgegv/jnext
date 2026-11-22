#!/usr/bin/env bash
#
# Contract test for test/ci-gate.sh (GH #547): the CI batch gate's dispatch / wait / verdict.
# Hermetic: a stub `gh` and a stub `git` (only `ls-remote`) first on PATH serve fixture JSON; no
# network, no repository, nothing pushed. Everything lives under one scratch directory.
#
# Oracle: the gate's own definition (CLAUDE.md merge protocol step 2, plan GH #547 2C): CLEAN only if
# the run and EVERY job succeeded, no job of the last green main run is missing, and the run is the
# one dispatched on the named sha. No VHDL: this is tooling.
#
#   CIG-01  all jobs success, a superset of main's jobs -> CLEAN, exit 0
#   CIG-02  one job skipped -> not clean, exit 1, names it
#   CIG-03  one job failure -> not clean, exit 1, names it
#   CIG-04  run conclusion cancelled -> not clean
#   CIG-05  a job of main's last green run missing from the gate run -> not clean, names it
#   CIG-06  dispatch refuses when origin/<branch> is another sha (exit 3, nothing started)
#   CIG-07  dispatch picks the run on the given sha created after the dispatch, ignoring an older
#           run and a newer run of another sha; gives up (exit 1) when none appears
#   CIG-08  wait gives up at its bound (124, naming the run) even when a gh call hangs, and returns 0
#           once the run completes
#   CIG-09  a completed, successful pull_request run is not clean, naming the event
#   CIG-10  a run still in progress is not clean, naming the status
# The stub gh answers `run list` / `run view` / `workflow run` only for the exact flag sets the gate must
# use (`--branch main --event push --status success`, `--event workflow_dispatch --branch <b>`, ...) and
# exits 1 otherwise, so a dropped filter makes the gate fail instead of silently reading the wrong run.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
GATE=$ROOT/test/ci-gate.sh
EXPECTED_ROWS=10

total=0; passed=0; failed=0
ids_seen=" "
row() {
    local id=$1 ok=$2 text=$3
    total=$((total + 1))
    if [[ "$ids_seen" == *" $id "* ]]; then
        echo "  FAIL $id duplicate row id"; failed=$((failed + 1)); return
    fi
    ids_seen+="$id "
    if [[ "$ok" == 1 ]]; then echo "  PASS $id $text"; passed=$((passed + 1))
    else echo "  FAIL $id $text"; failed=$((failed + 1)); fi
}
finish() {
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
    if [[ $total -ne $EXPECTED_ROWS ]]; then echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; fi
    [[ $failed -eq 0 ]] || exit 1
    exit 0
}

missing=()
for t in python3 timeout date; do command -v "$t" >/dev/null 2>&1 || missing+=("$t"); done
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing tools: ${missing[*]}"
    for i in 1 2 3 4 5 6 7 8 9 10; do row "CIG-$(printf %02d "$i")" 0 "not run: missing tools"; done
    total=$EXPECTED_ROWS; failed=$EXPECTED_ROWS; passed=0; ids_seen=" "; finish
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/ci-gate-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
FX=$TMP/fx; mkdir -p "$TMP/bin" "$FX"
export CI_GATE_FX=$FX

# ---- stubs ------------------------------------------------------------------
cat > "$TMP/bin/gh" <<'STUB'
#!/usr/bin/env bash
fx=$CI_GATE_FX
echo "gh $*" >> "$fx/calls"
a=" $* "
case "$1 $2" in
    "workflow run")
        [[ "$a" == *" workflow run ci.yml --ref "* ]] || exit 1; exit 0 ;;
    "run list")
        [[ "$a" == *" --workflow ci.yml "* ]] || exit 1
        if [[ "$a" == *" --branch main "* ]]; then
            [[ "$a" == *" --event push "* && "$a" == *" --status success "* ]] || exit 1
            cat "$fx/main-list.json"
        else
            [[ "$a" == *" --event workflow_dispatch "* && "$a" == *" --branch "* ]] || exit 1
            cat "$fx/dispatch-list.json"
        fi ;;
    "run view")
        id=$3
        [ ! -f "$fx/hang" ] || sleep 40
        if [[ "$a" == *" --json status "* ]]; then
            n=$(cat "$fx/n-$id" 2>/dev/null || echo 0); echo $((n + 1)) > "$fx/n-$id"
            if [ "$n" -ge "$(cat "$fx/done-after-$id" 2>/dev/null || echo 999999)" ]; then s=completed; else s=in_progress; fi
            printf '{"status":"%s"}\n' "$s"
        elif [[ "$a" == *" --json status,conclusion,headSha,headBranch,event,jobs "* || "$a" == *" --json jobs "* ]]; then
            cat "$fx/run-$id.json"
        else exit 1; fi ;;
    *) exit 1 ;;
esac
STUB
cat > "$TMP/bin/git" <<'STUB'
#!/usr/bin/env bash
[ "$1" = ls-remote ] || exit 1
[ -f "$CI_GATE_FX/remote-sha" ] || exit 0
printf '%s\t%s\n' "$(cat "$CI_GATE_FX/remote-sha")" "$3"
STUB
chmod +x "$TMP/bin/gh" "$TMP/bin/git"
export PATH=$TMP/bin:$PATH

SHA=$(printf 'a%.0s' $(seq 40)); OTHER=$(printf 'b%.0s' $(seq 40))

# run_json <conclusion> <event> <sha> <name:conclusion>...
run_json() {
    local concl=$1 ev=$2 sha=$3; shift 3
    local jobs="" sep="" j
    for j in "$@"; do
        jobs+="$sep{\"name\":\"${j%%:*}\",\"conclusion\":\"${j#*:}\",\"startedAt\":\"2026-11-21T10:00:00Z\",\"completedAt\":\"2026-11-21T10:30:30Z\"}"
        sep=","
    done
    printf '{"status":"%s","conclusion":"%s","event":"%s","headSha":"%s","headBranch":"gate-v9","jobs":[%s]}\n' "${RUN_STATUS:-completed}" "$concl" "$ev" "$sha" "$jobs"
}
printf '[{"databaseId":500}]\n' > "$FX/main-list.json"
run_json success push "$OTHER" "test (unit):success" "test (regression):success" "package:success" > "$FX/run-500.json"

OUT=""; RC=0
vrun() { OUT=$(bash "$GATE" verdict "$@" 2>&1); RC=$?; }

# CIG-01
run_json success workflow_dispatch "$SHA" "test (unit):success" "test (regression):success" "package:success" "macos:success" > "$FX/run-1.json"
vrun --sha "$SHA" 1
ok1=0; [[ $RC -eq 0 && "$OUT" == *"verdict: CLEAN"* && "$OUT" == *"test (unit)"*"30.5 min"* ]] && ok1=1
vrun --sha "$OTHER" 1                       # the right run, the wrong commit
[[ $RC -eq 1 && "$OUT" == *"NOT CLEAN"* ]] || ok1=0
row CIG-01 "$ok1" "all jobs success and a superset of main's jobs: CLEAN, exit 0, table with minutes; the wrong --sha is refused"

# CIG-02
run_json success workflow_dispatch "$SHA" "test (unit):success" "test (regression):skipped" "package:success" > "$FX/run-2.json"
vrun 2
row CIG-02 "$([[ $RC -eq 1 && "$OUT" == *"NOT CLEAN"* && "$OUT" == *"job test (regression) concluded skipped"* && "$OUT" != *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "a skipped job under a success run: not clean, exit 1, the job named"

# CIG-03
run_json success workflow_dispatch "$SHA" "test (unit):failure" "test (regression):success" "package:success" > "$FX/run-3.json"
vrun 3
row CIG-03 "$([[ $RC -eq 1 && "$OUT" == *"job test (unit) concluded failure"* && "$OUT" != *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "a failed job: not clean, exit 1, the job named"

# CIG-04
run_json cancelled workflow_dispatch "$SHA" "test (unit):success" "test (regression):success" "package:success" > "$FX/run-4.json"
vrun 4
row CIG-04 "$([[ $RC -eq 1 && "$OUT" == *"run conclusion is cancelled"* && "$OUT" != *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "a cancelled run with every listed job success: not clean"

# CIG-05
run_json success workflow_dispatch "$SHA" "test (unit):success" "test (regression):success" > "$FX/run-5.json"
vrun 5
ok5=0; [[ $RC -eq 1 && "$OUT" == *"job package of the last green main run is missing"* ]] && ok5=1
vrun --allow-missing package 5              # a job removed on purpose is named by the caller
[[ $RC -eq 0 && "$OUT" == *"verdict: CLEAN"* ]] || ok5=0
row CIG-05 "$ok5" "a job of main's last green run missing from the gate run: not clean, named; --allow-missing accepts a deliberate removal"

# CIG-06
echo "$OTHER" > "$FX/remote-sha"; : > "$FX/calls"
OUT=$(bash "$GATE" dispatch gate-v9 "$SHA" 2>&1); RC=$?
row CIG-06 "$([[ $RC -eq 3 && "$OUT" == *"refused"* ]] && ! grep -q 'workflow run' "$FX/calls" && echo 1 || echo 0)" \
    "dispatch refuses (exit 3) when origin/<branch> is another sha, and starts nothing"

# CIG-07
now=$(date -u +%Y-%m-%dT%H:%M:%SZ); old=$(date -u -d '-1 hour' +%Y-%m-%dT%H:%M:%SZ)
echo "$SHA" > "$FX/remote-sha"; : > "$FX/calls"
printf '[{"databaseId":10,"headSha":"%s","createdAt":"%s"},{"databaseId":11,"headSha":"%s","createdAt":"%s"},{"databaseId":12,"headSha":"%s","createdAt":"%s"}]\n' \
    "$SHA" "$old" "$OTHER" "$now" "$SHA" "$now" > "$FX/dispatch-list.json"
OUT=$(CI_GATE_POLL_SEC=1 CI_GATE_APPEAR_SEC=3 bash "$GATE" dispatch gate-v9 "$SHA" 2>/dev/null); RC=$?
ok7=0; [[ $RC -eq 0 && "$OUT" == 12 ]] && grep -q 'workflow run ci.yml --ref gate-v9' "$FX/calls" && ok7=1
printf '[{"databaseId":10,"headSha":"%s","createdAt":"%s"},{"databaseId":11,"headSha":"%s","createdAt":"%s"}]\n' \
    "$SHA" "$old" "$OTHER" "$now" > "$FX/dispatch-list.json"
OUT=$(CI_GATE_POLL_SEC=1 CI_GATE_APPEAR_SEC=2 timeout --foreground --kill-after=5s 30s bash "$GATE" dispatch gate-v9 "$SHA" 2>&1); RC=$?
[[ $RC -eq 1 && "$OUT" == *"no workflow_dispatch run on $SHA appeared"* ]] || ok7=0
row CIG-07 "$ok7" "dispatch prints the run on the given sha created after the dispatch (not an older run, not another sha); none appearing exits 1"

# CIG-08
echo 999999 > "$FX/done-after-20"; rm -f "$FX/n-20"
OUT=$(CI_GATE_POLL_SEC=1 timeout --foreground --kill-after=5s 30s bash "$GATE" wait --max-min 0 20 2>&1); RC=$?
ok8=0; [[ $RC -eq 124 && "$OUT" == *"run 20 not completed after 0 min"* ]] && ok8=1
echo 2 > "$FX/done-after-21"; rm -f "$FX/n-21"
OUT=$(CI_GATE_POLL_SEC=1 timeout --foreground --kill-after=5s 30s bash "$GATE" wait 21 2>&1); RC=$?
[[ $RC -eq 0 && "$OUT" == *"run 21 completed"* ]] || ok8=0
# a gh call that hangs (stub sleeps 40 s) must not stretch the bound: the call is cut at CI_GATE_GH_SEC
touch "$FX/hang"; echo 999999 > "$FX/done-after-22"; rm -f "$FX/n-22"
t0=$SECONDS
OUT=$(CI_GATE_POLL_SEC=1 CI_GATE_GH_SEC=2 timeout --foreground --kill-after=5s 30s bash "$GATE" wait --max-min 0 22 2>&1); RC=$?
[[ $RC -eq 124 && "$OUT" == *"run 22 not completed after 0 min"* && $((SECONDS - t0)) -lt 15 ]] || ok8=0
t0=$SECONDS
OUT=$(CI_GATE_GH_SEC=2 timeout --foreground --kill-after=5s 30s bash "$GATE" verdict 22 2>&1); RC=$?
[[ $RC -eq 75 && $((SECONDS - t0)) -lt 15 && "$OUT" != *"verdict: CLEAN"* ]] || ok8=0
rm -f "$FX/hang"
row CIG-08 "$ok8" "wait gives up at its bound (exit 124, the run named), also while a gh call hangs, and returns 0 once the run is completed"

# CIG-09
RUN_STATUS=completed run_json success pull_request "$SHA" "test (unit):success" "test (regression):success" "package:success" > "$FX/run-9.json"
vrun --sha "$SHA" 9
row CIG-09 "$([[ $RC -eq 1 && "$OUT" == *"run event is pull_request"* && "$OUT" != *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "a completed, successful pull_request run on the right sha: not clean, the event named"

# CIG-10
RUN_STATUS=in_progress run_json success workflow_dispatch "$SHA" "test (unit):success" "test (regression):success" "package:success" > "$FX/run-10.json"
vrun --sha "$SHA" 10
row CIG-10 "$([[ $RC -eq 1 && "$OUT" == *"run status is in_progress"* && "$OUT" != *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "a run still in progress whose jobs all read success: not clean, the status named"

finish
