# shellcheck shell=bash
# container-side.sh: runs inside the container, in the checkout (/src). The remote side mounts
# it at /rr/inner.sh with /rr/env.sh (RR_TARGETS newline-separated, RR_BOOTSTRAP, caps).
# A target is a make target with optional extra words (`regression-rows`), or `sh:<command>`.
# Each target runs on its own, in order; the first failure stops the run unless RR_KEEP_GOING=1.
# Prints "=== remote-run result: <target> rc=N time=Ns peak=MMiB" per target, and
# "=== remote-run totals: <target> :: <line>" with the target's own last totals line(s), in
# either format the harnesses print: "Total: N Passed: N Failed: N Skipped: N" or
# "Pass: N Fail: N Skip: N".
set -uo pipefail
# shellcheck source=/dev/null
. /rr/env.sh
cd /src || exit 2

peak() {
    local p=/sys/fs/cgroup/memory.peak
    if [[ -r $p ]]; then echo $(( $(cat "$p") / 1048576 )); else echo "?"; fi
}

overall=0
out=$(mktemp)
totals() {
    local t=$1 l
    for pat in 'Total:[[:space:]]+[0-9]+.*Skipped:[[:space:]]*[0-9]+' 'Pass:[[:space:]]*[0-9]+[[:space:]]+Fail:[[:space:]]*[0-9]+[[:space:]]+Skip:[[:space:]]*[0-9]+'; do
        l=$(sed -e 's/\x1b\[[0-9;]*m//g' "$out" | grep -E "$pat" | tail -n 1 || true)
        [[ -z "$l" ]] || echo "=== remote-run totals: $t :: $(echo "$l" | tr -s ' ')"
    done
}
if [[ -n "$RR_BOOTSTRAP" ]]; then
    echo "=== remote-run bootstrap: $RR_BOOTSTRAP"
    bash -c "$RR_BOOTSTRAP" || { echo "=== remote-run result: bootstrap rc=$? time=0s peak=$(peak)MiB"; exit 1; }
fi
while IFS= read -r t; do
    [[ -n "$t" ]] || continue
    echo "=== remote-run target: $t"
    start=$SECONDS
    if [[ "$t" == sh:* ]]; then
        bash -c "${t#sh:}" 2>&1 | tee "$out"
        rc=${PIPESTATUS[0]}
    else
        read -ra words <<<"$t"
        jobs=$RR_JOBS_DEFAULT
        [[ ! "${words[0]}" =~ $RR_HEAVY_RE ]] || jobs=$RR_JOBS_HEAVY
        make "JOBS=$jobs" "${words[@]}" 2>&1 | tee "$out"
        rc=${PIPESTATUS[0]}
    fi
    echo "=== remote-run result: $t rc=$rc time=$((SECONDS - start))s peak=$(peak)MiB"
    totals "$t"
    if [[ $rc -ne 0 ]]; then
        overall=1
        [[ "$RR_KEEP_GOING" == 1 ]] || break
    fi
done <<<"$RR_TARGETS"
exit "$overall"
