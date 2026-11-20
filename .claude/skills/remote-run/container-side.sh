# shellcheck shell=bash
# container-side.sh: runs inside the container, in the checkout (/src). The remote side mounts
# it at /rr/inner.sh with /rr/env.sh (RR_TARGETS newline-separated, RR_BOOTSTRAP, caps).
# A target is a make target with optional extra words (`regression-rows`), or `sh:<command>`.
# Each target runs on its own, in order; the first failure stops the run unless RR_KEEP_GOING=1.
# Prints "=== remote-run result: <target> rc=N time=Ns peak=MMiB" per target.
set -uo pipefail
# shellcheck source=/dev/null
. /rr/env.sh
cd /src || exit 2

peak() {
    local p=/sys/fs/cgroup/memory.peak
    if [[ -r $p ]]; then echo $(( $(cat "$p") / 1048576 )); else echo "?"; fi
}

overall=0
if [[ -n "$RR_BOOTSTRAP" ]]; then
    echo "=== remote-run bootstrap: $RR_BOOTSTRAP"
    bash -c "$RR_BOOTSTRAP" || { echo "=== remote-run result: bootstrap rc=$? time=0s peak=$(peak)MiB"; exit 1; }
fi
while IFS= read -r t; do
    [[ -n "$t" ]] || continue
    echo "=== remote-run target: $t"
    start=$SECONDS
    if [[ "$t" == sh:* ]]; then
        bash -c "${t#sh:}"
        rc=$?
    else
        read -ra words <<<"$t"
        jobs=$RR_JOBS_DEFAULT
        [[ ! "${words[0]}" =~ $RR_HEAVY_RE ]] || jobs=$RR_JOBS_HEAVY
        make "JOBS=$jobs" "${words[@]}"
        rc=$?
    fi
    echo "=== remote-run result: $t rc=$rc time=$((SECONDS - start))s peak=$(peak)MiB"
    if [[ $rc -ne 0 ]]; then
        overall=1
        [[ "$RR_KEEP_GOING" == 1 ]] || break
    fi
done <<<"$RR_TARGETS"
exit "$overall"
