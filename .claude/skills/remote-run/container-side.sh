# shellcheck shell=bash
# container-side.sh: runs inside the container, in the checkout (/src). The remote side mounts
# it at /rr/inner.sh with /rr/env.sh (RR_CTL and RR_SRC move /rr and /src; the contract test does) (RR_TARGETS newline-separated, RR_BOOTSTRAP, caps).
# The target list is read from fd 3 and every step runs with stdin from /dev/null, so a step
# that reads stdin cannot swallow the steps after it.
# A target is a make target with optional extra words (`regression-rows`), or `sh:<command>`.
# Each target runs on its own, in order; the first failure stops the run unless RR_KEEP_GOING=1.
# Prints "=== remote-run result: <target> rc=N time=Ns peak=MMiB" per target, and
# "=== remote-run totals: <target> :: <line>" with the target's own last totals line(s), in
# either format the harnesses print: "Total: N Passed: N Failed: N Skipped: N" or
# "Pass: N Fail: N Skip: N".
set -uo pipefail
# shellcheck source=/dev/null
. "${RR_CTL:-/rr}/env.sh"
cd "${RR_SRC:-/src}" || exit 2

# Memory, from the container's cgroup (RR_CGROUP moves it; the contract test does). `peak` is
# memory.peak: the cgroup's high-water mark since the container started, page cache included, so it
# only ever grows and says little. `anon` is the step's own peak of anonymous memory (real use), found
# by a sampler that reads memory.stat once per RR_SAMPLE_INTERVAL seconds while the step runs; that
# is what the caps are tuned on. An oom_kill event during a step is logged as `oom`, which makes the
# verdict NOT CLEAN.
CG=${RR_CGROUP:-/sys/fs/cgroup}
mib() { echo $(( $1 / 1048576 )); }
peak() {
    if [[ -r $CG/memory.peak ]]; then mib "$(cat "$CG/memory.peak")"; else echo "?"; fi
}
anon_now() { awk '$1 == "anon" {print $2}' "$CG/memory.stat" 2>/dev/null; }
oom_count() { awk '$1 == "oom_kill" {print $2}' "$CG/memory.events" 2>/dev/null; }
sampler_pid=""
sampler_start() {
    rm -f "$anon_file"; echo 0 > "$anon_file"
    (
        max=0
        while :; do
            a=$(anon_now)
            if [[ -n "$a" && "$a" -gt "$max" ]]; then max=$a; echo "$max" > "$anon_file"; fi
            sleep "${RR_SAMPLE_INTERVAL:-1}"
        done
    ) &
    sampler_pid=$!
}
sampler_stop() {
    [[ -z "$sampler_pid" ]] || { kill "$sampler_pid" 2>/dev/null; wait "$sampler_pid" 2>/dev/null; sampler_pid=""; }
    a=$(anon_now); m=$(cat "$anon_file" 2>/dev/null || echo 0)
    [[ -z "$a" || "$a" -le "$m" ]] || echo "$a" > "$anon_file"
}
anon_peak() { if [[ -r $CG/memory.stat ]]; then mib "$(cat "$anon_file")"; else echo "?"; fi; }

overall=0
out=$(mktemp)
anon_file=$(mktemp)
totals() {
    local t=$1 l
    for pat in 'Total:[[:space:]]+[0-9]+.*Skipped:[[:space:]]*[0-9]+' 'Pass:[[:space:]]*[0-9]+[[:space:]]+Fail:[[:space:]]*[0-9]+[[:space:]]+Skip:[[:space:]]*[0-9]+'; do
        l=$(sed -e 's/\x1b\[[0-9;]*m//g' "$out" | grep -E "$pat" | tail -n 1 || true)
        [[ -z "$l" ]] || echo "=== remote-run totals: $t :: $(echo "$l" | tr -s ' ' | sed -e 's/^ //')"
    done
}
if [[ -n "$RR_BOOTSTRAP" ]]; then
    echo "=== remote-run bootstrap: $RR_BOOTSTRAP"
    bash -c "$RR_BOOTSTRAP" </dev/null || { echo "=== remote-run result: bootstrap rc=$? time=0s peak=$(peak)MiB anon=?MiB"; exit 1; }
fi
while IFS= read -r t <&3; do
    [[ -n "$t" ]] || continue
    echo "=== remote-run target: $t"
    start=$SECONDS
    oom0=$(oom_count); sampler_start
    if [[ "$t" == sh:* ]]; then
        bash -c "${t#sh:}" </dev/null 2>&1 | tee "$out"
        rc=${PIPESTATUS[0]}
    else
        read -ra words <<<"$t"
        jobs=$RR_JOBS_DEFAULT
        [[ ! "${words[0]}" =~ $RR_HEAVY_RE ]] || jobs=$RR_JOBS_HEAVY
        make "JOBS=$jobs" "${words[@]}" </dev/null 2>&1 | tee "$out"
        rc=${PIPESTATUS[0]}
    fi
    sampler_stop; oom1=$(oom_count)
    echo "=== remote-run result: $t rc=$rc time=$((SECONDS - start))s peak=$(peak)MiB anon=$(anon_peak)MiB"
    [[ -z "$oom1" || "${oom1:-0}" -le "${oom0:-0}" ]] || echo "=== remote-run oom: $t killed $((oom1 - oom0)) process(es) by the container's memory limit"
    totals "$t"
    if [[ $rc -ne 0 ]]; then
        overall=1
        [[ "$RR_KEEP_GOING" == 1 ]] || break
    fi
done 3<<<"$RR_TARGETS"
exit "$overall"
