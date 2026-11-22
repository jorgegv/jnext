# Sourced by the skill-script contract tests (remote-run-test.sh, slot-test.sh): unset every variable
# the remote-run driver and slot.sh read, so a test starts from the same environment whoever runs it.
# The driver's local fallback runs `make unit-test` with REMOTE_RUN_FALLBACK=1, REMOTE_RUN_LOCAL_WRAP,
# JNEXT_SLOT_NO_OVERFLOW=1 and the JNEXT_REMOTE_RUN_* pair in the environment; a contract test that
# inherited them ran its own nested drivers through the real wrapper (waiting on the real slots) and
# read the real remote's variables (GH #547 stage 4). By class: every name with these prefixes goes,
# so a variable added to either script later is covered too. A test sets what it needs AFTER this.
while IFS= read -r _scrub_v; do
    unset "$_scrub_v"
done < <(compgen -e | grep -E '^(REMOTE_RUN_|JNEXT_REMOTE_RUN_|JNEXT_SLOT_|RR_)' || true)
unset _scrub_v

# polluted_selfrun <test script> <scratch dir>: starts the test again in the background (output in
# <dir>/out, exit status in <dir>/rc) with the environment the driver's local fallback hands its
# wrapper, plus the remote's variables, and with an `ssh` and a wrapper on PATH that record any call
# in <dir>/touched and fail. The inner run must be green and must never reach either. The inner copy
# sees SKILLTEST_INNER=1 and does not start a third. (The status goes through a file because the
# callers use a bare `wait` elsewhere, which would reap this child and lose it.)
polluted_selfrun() {
    local script=$1 d=$2
    mkdir -p "$d/bin"
    printf '#!/bin/sh\necho "ssh $*" >> "%s/touched"\nexit 255\n' "$d" > "$d/bin/ssh"
    printf '#!/bin/sh\necho "wrap $*" >> "%s/touched"\nexit 97\n' "$d" > "$d/bin/wrap"
    chmod +x "$d/bin/ssh" "$d/bin/wrap"
    (
        env SKILLTEST_INNER=1 PATH="$d/bin:$PATH" REMOTE_RUN_FALLBACK=1 REMOTE_RUN_LOCAL_WRAP="$d/bin/wrap" \
            JNEXT_SLOT_NO_OVERFLOW=1 JNEXT_REMOTE_RUN_HOST=polluted-host JNEXT_REMOTE_RUN_HOSTS="polluted-host polluted-host2" JNEXT_REMOTE_RUN_DIR="$d/remote-dir" \
            JNEXT_SLOT_DIR="$d/slots" JNEXT_SLOT_LOAD_LIMIT=0 REMOTE_RUN_LANES=1 REMOTE_RUN_LANE_WAIT=1000 \
            REMOTE_RUN_REPO_URL=file:///nonexistent REMOTE_RUN_CONF=/nonexistent \
            RR_MEMINFO=/nonexistent RR_TARGETS=bogus RR_KEEP_GOING=1 RR_JOBS_DEFAULT=99 RR_JOBS_HEAVY=99 \
            RR_HEAVY_RE=bogus RR_BOOTSTRAP=false RR_TIMEOUT=1 RR_LANES=7 \
            timeout --kill-after=5s 900s bash "$script" > "$d/out" 2>&1
        echo $? > "$d/rc"
    ) &
}
# polluted_check <scratch dir>: waits for that run; sets POL to 1 when it was green and touched neither shim.
polluted_check() {
    local d=$1 n=0
    while [[ ! -s "$d/rc" && $n -lt 4000 ]]; do sleep 0.25; n=$((n + 1)); done
    POL=0
    if [[ "$(cat "$d/rc" 2>/dev/null)" == 0 && ! -e "$d/touched" ]] && grep -aq 'Failed:    0' "$d/out"; then POL=1; fi
}
