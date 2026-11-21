# spec-runner.inc - the browser specs' bounded parallel runner (GH #547).
#
# SOURCED by test/browser/browser-smoke.sh (which owns the trap) and by the harness self-test's stub
# driver (HS-192a..h), so the runner is reachable without the fixture preparation, emsdk, Chromium or
# the SD image. Defines functions only; nothing runs on source.
#
#   br_run_specs <node> <scratch> <here> <spec>...
#       spec = <file>:<expected rows>[:serial]; a relative file is resolved against <here>.
#       Needs <scratch>/out (the logs) and <scratch>/work (a directory per spec), both existing.
#       Prints each spec's log as ONE block when it ends, then the summary; returns 0 only when every
#       declared spec reported exactly once, with its pinned row count, no FAIL, no SKIP, exit 0.
#   br_freeze <scratch> / br_thaw <scratch>
#       the shared fixtures read-only (everything but out/ and work/): a spec that writes into the
#       shared area fails with EACCES instead of racing another spec. br_fingerprint is the second,
#       root-proof witness (CI runs as root, for whom a mode bit is no guard).
#
# JNEXT_BROWSER_JOBS (default min(4, max(1, nproc/2))): specs at once; 1 = one after another.
# JNEXT_BROWSER_SPEC_BOUND / JNEXT_BROWSER_SPEC_KILL_AFTER (default 1500 / 10 s): each spec's bound,
# `timeout --kill-after` so a spec that ignores TERM is still killed; overridable only for the self-test.
# A `:serial` spec runs alone after the parallel group; it is allowed only with evidence (a recorded
# parallel failure and a serial pass), the regression suite's `quiet` rule.

br_die() { echo "browser-smoke: $*" >&2; exit 2; }

br_jobs() {
    local j=${JNEXT_BROWSER_JOBS-} n
    if [[ -z "$j" ]]; then
        n=$(nproc 2>/dev/null || echo 2)
        j=$((n / 2)); (( j < 1 )) && j=1; (( j > 4 )) && j=4
    fi
    [[ "$j" =~ ^[1-9][0-9]*$ ]] || br_die "JNEXT_BROWSER_JOBS must be a positive integer, got [$j]"
    echo "$j"
}

br_freeze() {
    local s=$1 p
    for p in "$s"/*; do
        [[ "$p" == "$s/out" || "$p" == "$s/work" ]] && continue
        chmod -R a-w "$p"
    done
    chmod a-w "$s"
}
br_thaw() { chmod -R u+w "$1" 2>/dev/null || true; }

# Everything in <scratch> except out/ and work/, by path, type, size and mtime.
br_fingerprint() {
    find "$1" -mindepth 1 \( -path "$1/out" -o -path "$1/work" \) -prune -o -printf '%p %y %s %T@\n' | sort | sha256sum
}

# One spec, in the background: its log, its rc, its wall time, then its log as one block.
br_one_spec() {
    local node=$1 scratch=$2 file=$3 name rc=0 t0 t1
    name=$(basename "$file")
    mkdir -p "$scratch/work/$name"
    t0=$(date +%s.%N)
    echo "$t0" >"$scratch/out/$name.t0"
    # Not under ulimit -v: V8 and wasm reserve far more address space than 4 GiB.
    SMOKE_WORK="$scratch/work/$name" timeout --kill-after="${JNEXT_BROWSER_SPEC_KILL_AFTER:-10}s" "${JNEXT_BROWSER_SPEC_BOUND:-1500}s" \
        "$node" "$file" >"$scratch/out/$name.log" 2>&1 || rc=$?
    t1=$(date +%s.%N)
    echo "$t1" >"$scratch/out/$name.t1"
    echo "$rc" >"$scratch/out/$name.rc"
    {
        flock 9
        printf '\n==== %s (%.0f s) ====\n' "$name" "$(awk -v a="$t0" -v b="$t1" 'BEGIN{print b-a}')"
        cat "$scratch/out/$name.log"
    } 9>"$scratch/out/.print.lock"
}

br_run_specs() {
    local node=$1 scratch=$2 here=$3; shift 3
    local njobs spec file rest want tag name log line rc t p f s i
    local -a par=() ser=() files=() wants=()
    local TOTAL=0 PASS=0 FAIL=0 SKIP=0 FAULT=0 before after
    njobs=$(br_jobs) || return 2
    for spec in "$@"; do
        file=${spec%%:*}; rest=${spec#*:}; want=${rest%%:*}; tag=
        [[ "$rest" == *:* ]] && tag=${rest#*:}
        [[ "$file" == /* ]] || file="$here/$file"
        [[ -f "$file" && "$want" =~ ^[0-9]+$ && ( -z "$tag" || "$tag" == serial ) ]] || { echo "browser-smoke: bad spec [$spec] (file:count[:serial])" >&2; return 2; }
        files+=("$file"); wants+=("$want")
        if [[ "$tag" == serial ]]; then ser+=("$file"); else par+=("$file"); fi
    done
    before=$(br_fingerprint "$scratch")
    for file in "${par[@]+"${par[@]}"}"; do
        while (( $(jobs -rp | wc -l) >= njobs )); do sleep 0.1; done
        br_one_spec "$node" "$scratch" "$file" &
    done
    wait
    for file in "${ser[@]+"${ser[@]}"}"; do br_one_spec "$node" "$scratch" "$file"; done
    after=$(br_fingerprint "$scratch")
    # Accounting: every declared spec reported exactly once, in declared order.
    for i in "${!files[@]}"; do
        file=${files[$i]}; want=${wants[$i]}; name=$(basename "$file")
        log="$scratch/out/$name.log"
        if [[ ! -f "$scratch/out/$name.rc" ]]; then echo "browser-smoke: $file did not run" >&2; FAULT=1; continue; fi
        rc=$(cat "$scratch/out/$name.rc")
        line=$(grep '^Total:' "$log" | tail -1)
        if [[ -z "$line" ]]; then echo "browser-smoke: $file printed no Total line (rc=$rc)" >&2; FAULT=1; continue; fi
        t=$(awk '{print $2}' <<<"$line"); p=$(awk '{print $4}' <<<"$line"); f=$(awk '{print $6}' <<<"$line"); s=$(awk '{print $8}' <<<"$line")
        if [[ "$t" != "$want" ]]; then echo "browser-smoke: $file reported $t rows, expected $want" >&2; FAULT=1; fi
        TOTAL=$((TOTAL + t)); PASS=$((PASS + p)); FAIL=$((FAIL + f)); SKIP=$((SKIP + s))
        [[ $rc -eq 0 ]] || { echo "browser-smoke: $file exited $rc" >&2; FAULT=1; }
    done
    if [[ "$before" != "$after" ]]; then echo "browser-smoke: a spec wrote into the shared fixtures under $scratch (outside out/ and work/)" >&2; FAULT=1; fi
    echo
    echo "===================================================="
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$TOTAL" "$PASS" "$FAIL" "$SKIP"
    [[ $FAIL -eq 0 && $SKIP -eq 0 && $FAULT -eq 0 ]]
}
