#!/usr/bin/env bash
#
# The CI batch gate (GH #547): a green GitHub CI run on the commit to be pushed is the gate.
#
#   test/ci-gate.sh dispatch <branch> <sha>    start CI on origin/<branch>, which must be at <sha>;
#                                              print the new run's id
#   test/ci-gate.sh wait [--max-min N] <id>    block until the run is completed (default bound 180 min)
#   test/ci-gate.sh verdict [--sha S] [--allow-missing JOB]... <id>
#                                              one line per job, then "verdict: CLEAN" or the reasons
#
# CLEAN means ALL of: the run is a push or workflow_dispatch run (a pull_request run tests a merge, not the
# commit); its conclusion is success; EVERY job concluded success (skipped, cancelled, neutral and timed_out
# are not clean: a skipped job is a row that was not tested); no job of the newest green push run on main is
# missing (a job that silently stopped running is caught the way the manifests catch a vanished suite);
# and, with --sha, the run is on that commit. --allow-missing names a job removed or renamed on purpose.
#
# Exit status: 0 ok / CLEAN; 1 not clean, or no run appeared; 2 usage; 3 dispatch refused (origin/<branch> is
# not <sha>); 75 GitHub unreachable; 124 wait ran out of time.
# Environment: CI_GATE_GH_SEC (bound on every gh / git call, default 120; a timeout is a failed poll in wait and exit 75 elsewhere), CI_GATE_POLL_SEC (wait poll interval, default 60), CI_GATE_APPEAR_SEC (dispatch wait for the
# run to appear, default 300), CI_GATE_SKEW_SEC (clock-skew allowance, default 120).
# Needs gh, git and python3 (JSON). Never retries a job; `gh run rerun` is for suspected flakes only.
set -euo pipefail
IFS=$'\n\t'

WORKFLOW=ci.yml
POLL=${CI_GATE_POLL_SEC:-60}
APPEAR=${CI_GATE_APPEAR_SEC:-300}
SKEW=${CI_GATE_SKEW_SEC:-120}
GH_SEC=${CI_GATE_GH_SEC:-120}

die() { echo "ci-gate: $*" >&2; exit "${2:-1}"; }
usage() { sed -n '3,/^# Needs gh/p' "$0" | sed -e 's/^# \{0,1\}//' -e '$d' >&2; exit 2; }

# gh_json <args...>: gh output, or exit 75 when GitHub cannot be reached.
gh_t() { timeout --kill-after=5s "${GH_SEC}s" gh "$@" 2>/dev/null; }
gh_json() { gh_t "$@" || die "GitHub unreachable, timed out or refused: gh $*" 75; }

# py <program> [args]: run python (isolated) on stdin; the program sees the parsed JSON as `d`.
py() { local prog=$1; shift; python3 -I -c "import json,sys
d=json.load(sys.stdin)
$prog" "$@"; }

now_iso() { date -u -d "@$(( $(date -u +%s) - ${1:-0} ))" +%Y-%m-%dT%H:%M:%SZ; }

cmd_dispatch() {
    [ "$#" -eq 2 ] || usage
    local branch=$1 sha=$2 remote start list id
    [[ "$sha" =~ ^[0-9a-f]{40}$ ]] || die "<sha> must be 40 hex digits" 2
    remote=$(timeout --kill-after=5s "${GH_SEC}s" git ls-remote origin "refs/heads/$branch" 2>/dev/null) || die "git ls-remote failed or timed out" 75
    remote=$(cut -f1 <<<"$remote")
    [ "$remote" = "$sha" ] || die "refused: origin/$branch is '${remote:-absent}', not $sha (push the branch first)" 3
    start=$(now_iso "$SKEW")
    gh_t workflow run "$WORKFLOW" --ref "$branch" >&2 || die "gh workflow run failed" 75
    local waited=0 step=$POLL
    [ "$step" -le 10 ] || step=10
    while :; do
        list=$(gh_json run list --workflow "$WORKFLOW" --branch "$branch" --event workflow_dispatch \
                 --json databaseId,headSha,createdAt --limit 30)
        # the newest run on this exact sha created after the dispatch (allowing for clock skew)
        id=$(py '
best=None
for r in d:
    if r["headSha"]==sys.argv[1] and r["createdAt"]>=sys.argv[2] and (best is None or r["createdAt"]>best["createdAt"]): best=r
print(best["databaseId"] if best else "")' "$sha" "$start" <<<"$list")
        if [ -n "$id" ]; then echo "$id"; return 0; fi
        [ "$waited" -lt "$APPEAR" ] || die "no workflow_dispatch run on $sha appeared within ${APPEAR}s" 1
        sleep "$step"
        waited=$((waited + step))
    done
}

cmd_wait() {
    local max=180
    if [ "${1:-}" = "--max-min" ]; then [ "$#" -ge 2 ] || usage; max=$2; shift 2; fi
    [ "$#" -eq 1 ] || usage
    local id=$1 t0 elapsed status fails=0 next_beat=600
    t0=$(date +%s)
    while :; do
        if status=$(gh_t run view "$id" --json status | py 'print(d["status"])' 2>/dev/null); then
            fails=0
            [ "$status" != completed ] || { echo "run $id completed"; return 0; }
        else
            fails=$((fails + 1))
            [ "$fails" -lt 5 ] || die "GitHub unreachable while waiting for run $id" 75
        fi
        elapsed=$(( $(date +%s) - t0 ))
        [ "$elapsed" -lt $((max * 60)) ] || die "run $id not completed after ${max} min" 124
        if [ "$elapsed" -ge "$next_beat" ]; then
            echo "run $id: ${status:-unknown} after $((elapsed / 60)) min"
            next_beat=$((next_beat + 600))
        fi
        sleep "$POLL"
    done
}

cmd_verdict() {
    local want_sha="" allow=() id
    while [ "$#" -gt 1 ]; do
        case $1 in
            --sha) want_sha=$2; shift 2 ;;
            --allow-missing) allow+=("$2"); shift 2 ;;
            *) usage ;;
        esac
    done
    [ "$#" -eq 1 ] || usage
    id=$1
    local run main_id main_run
    run=$(gh_json run view "$id" --json status,conclusion,headSha,headBranch,event,jobs)
    main_id=$(gh_json run list --workflow "$WORKFLOW" --branch main --event push --status success --limit 1 \
                --json databaseId)
    main_id=$(py 'print(d[0]["databaseId"] if d else "")' <<<"$main_id")
    [ -n "$main_id" ] || die "no green push run of $WORKFLOW on main to compare the job set with" 1
    main_run=$(gh_json run view "$main_id" --json jobs)
    # the report: table to stdout, reasons (one per line) after a blank marker line
    local out
    out=$(ID="$id" RUN="$run" MAIN="$main_run" WANT="$want_sha" ALLOW="$(printf '%s\n' "${allow[@]:-}")" python3 -I -c '
import json, os
from datetime import datetime
run = json.loads(os.environ["RUN"]); main = json.loads(os.environ["MAIN"])
allow = set(a for a in os.environ["ALLOW"].split("\n") if a)
def mins(j):
    try:
        f = lambda s: datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ")
        m = (f(j["completedAt"]) - f(j["startedAt"])).total_seconds() / 60
        return "%.1f" % m if m >= 0 else "?"
    except Exception:
        return "?"
bad = []
if run.get("status") != "completed": bad.append("run status is %s, not completed" % run.get("status"))
if run.get("event") not in ("push", "workflow_dispatch"): bad.append("run event is %s (only push and workflow_dispatch test the named commit)" % run.get("event"))
if run.get("conclusion") != "success": bad.append("run conclusion is %s" % run.get("conclusion"))
if os.environ["WANT"] and run.get("headSha") != os.environ["WANT"]: bad.append("run is on %s, not %s" % (run.get("headSha"), os.environ["WANT"]))
print("run %s on %s (%s, %s)" % (os.environ.get("ID", ""), run.get("headSha", "?")[:12], run.get("headBranch"), run.get("event")))
names = set()
for j in run.get("jobs", []):
    names.add(j["name"])
    print("  %-40s %-10s %s min" % (j["name"], j.get("conclusion"), mins(j)))
    if j.get("conclusion") != "success": bad.append("job %s concluded %s" % (j["name"], j.get("conclusion")))
for j in main.get("jobs", []):
    if j["name"] not in names and j["name"] not in allow: bad.append("job %s of the last green main run is missing" % j["name"])
if not run.get("jobs"): bad.append("the run has no jobs")
print("@@REASONS")
for b in bad: print(b)
') || die "cannot read the run's JSON" 1
    local table reasons
    table=${out%%$'\n'@@REASONS*}
    reasons=${out#*@@REASONS}
    reasons=${reasons#$'\n'}
    printf '%s\n' "$table"
    if [ -z "$reasons" ]; then
        echo "verdict: CLEAN"
        return 0
    fi
    printf 'not clean: %s\n' "$reasons"
    echo "verdict: NOT CLEAN"
    return 1
}

[ "$#" -ge 1 ] || usage
verb=$1; shift
case $verb in
    dispatch) cmd_dispatch "$@" ;;
    wait) cmd_wait "$@" ;;
    verdict) cmd_verdict "$@" ;;
    -h|--help) usage ;;
    *) usage ;;
esac
