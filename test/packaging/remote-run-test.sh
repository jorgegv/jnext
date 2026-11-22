#!/usr/bin/env bash
#
# Contract test for .claude/skills/remote-run (the remote-run driver) and test/ci-deps.sh.
# Hermetic: the remote side runs as a local shell (--local-shell) against a throwaway git
# repository, with a fake `podman` and a fake `make` on PATH; no ssh, no container, no network,
# no SD-card image, no shellcheck (a lint the CI containers do not carry). HOME and every work directory are under one scratch directory, removed on exit.
#
# Oracles: the driver's documented exit codes (0 green, 1 failed, 2 usage, 3 refused, 75 run
# locally), the owner's rule that all remote work data lives under one directory, and
# .github/workflows/ci.yml as the consumer of test/ci-deps.sh. No VHDL: this is tooling.
#
# Rows RRUN-01 .. RRUN-46. A missing tool FAILS every row (no SKIP path).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=/dev/null
source "$ROOT/test/packaging/skill-env-scrub.sh"
SKILL=$ROOT/.claude/skills/remote-run
DRIVER=$SKILL/remote-run.sh
EXPECTED_ROWS=46

total=0; passed=0; failed=0
ids_seen=" "
row() {
    local id=$1 ok=$2 text=$3
    total=$((total + 1))
    if [[ "$ids_seen" == *" $id "* ]]; then
        echo "  FAIL $id duplicate row id"; failed=$((failed + 1)); return
    fi
    ids_seen+="$id "
    if [[ "$ok" == 1 ]]; then
        echo "  PASS $id $text"; passed=$((passed + 1))
    else
        echo "  FAIL $id $text"; failed=$((failed + 1))
    fi
}
finish() {
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
    if [[ $total -ne $EXPECTED_ROWS ]]; then
        echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1
    fi
    [[ $failed -eq 0 ]] || exit 1
    exit 0
}

missing=()
for t in git flock timeout tar sha256sum; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing tools: ${missing[*]} (dnf install git util-linux coreutils tar)"
    for i in $(seq 1 $EXPECTED_ROWS); do row "RRUN-$(printf '%02d' "$i")" 0 "not run: missing tools"; done
    total=$EXPECTED_ROWS; failed=$EXPECTED_ROWS; passed=0
    ids_seen=" "; finish
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/remote-run-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home
mkdir -p "$HOME" "$TMP/bin" "$TMP/state"
# RRUN-38's run: the whole test again, started now so it overlaps the rest, under a polluted environment
[[ -n "${SKILLTEST_INNER:-}" ]] || polluted_selfrun "${BASH_SOURCE[0]}" "$TMP/polluted"

# ---- fakes ------------------------------------------------------------------
# Fake podman: records every call; any call without --root/--runroot other than --version FAILS (and
# leaves $FAKE_STATE/noroot), because a bare podman would write the default store outside the work dir.
# `image exists` is true after a `build`. `run` executes the real container-side script when
# FAKE_INNER=1 (the -v mounts mapped to RR_CTL / RR_SRC), else prints a canned result, writes a stamp
# when FAKE_STAMP=1, sleeps when FAKE_SLEEP=1 (until killed), kills its remote shell when
# FAKE_KILL_PARENT=1, and exits with FAKE_RC. While a FAKE_SLEEP run sleeps it is listed in $FAKE_STATE/live
# as "<name> <rr-lane label>"; `ps` prints the live names matching --filter label=rr-lane=N or name=<regex>.
cat > "$TMP/bin/podman" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$FAKE_STATE/calls"
if [[ "${1:-}" == --version ]]; then echo "podman version 0.0-fake"; exit 0; fi
if [[ "${1:-}" != --root ]]; then : > "$FAKE_STATE/noroot"; exit 99; fi
echo "$2 $4" >> "$FAKE_STATE/roots"; wdir=${2%/containers/storage}; shift 4
if [[ "${1:-}" == --storage-driver ]]; then echo "$2" >> "$FAKE_STATE/drivers"; shift 2; fi
case "${1:-}" in
    info)
        case "$*" in
            *GraphDriverName*) flock -n "$wdir/lane-L2.lock" true && echo free >> "$FAKE_STATE/probe-lane2" || echo held >> "$FAKE_STATE/probe-lane2"
                [[ "${FAKE_INFO_FAIL:-0}" != 1 ]] || { echo "boom: store broken" >&2; exit 1; }; echo overlay ;;
            *) echo "${FAKE_ROOTLESS:-true}" ;;
        esac ;;
    image)
        case "${2:-}" in
            exists) flock -n "$wdir/setup.lock" true && echo free >> "$FAKE_STATE/setup-at-image" || echo held >> "$FAKE_STATE/setup-at-image"
                [[ -e "$FAKE_STATE/image-${3//\//_}" ]] ;;
            *) exit 0 ;;
        esac ;;
    build) while [[ $# -gt 0 ]]; do [[ "$1" == -t ]] && : > "$FAKE_STATE/image-${2//\//_}"; shift; done ;;
    kill) echo "$2" >> "$FAKE_STATE/killed" ;;
    ps)
        filt=""
        for a in "$@"; do case "$a" in label=rr-lane=*|name=*) filt=$a ;; esac; done
        [[ -e "$FAKE_STATE/live" ]] || exit 0
        while read -r n l; do
            case "$filt" in
                label=rr-lane=*) [[ "$l" != "${filt#label=rr-lane=}" ]] || echo "$n" ;;
                name=*) ! grep -qE "${filt#name=}" <<<"$n" || echo "$n" ;;
            esac
        done < "$FAKE_STATE/live"
        exit 0 ;;
    run)
        ctl=""; src=""; cname=""; clane=""
        prev=""
        for a in "$@"; do
            [[ "$prev" == --name ]] && cname=$a
            [[ "$prev" == --label && "$a" == rr-lane=* ]] && clane=${a#rr-lane=}
            prev=$a
            [[ "$a" == *:/rr-stamps ]] && [[ "${FAKE_STAMP:-0}" == 1 ]] && echo stamp > "${a%%:*}/fakekey"
            [[ "$a" == *:/src ]] && { echo "${a%%:*}" > "$FAKE_STATE/srcdir"; src=${a%%:*}; }
            [[ "$a" == *:/rr:ro ]] && { cp "${a%%:*}/env.sh" "$FAKE_STATE/env.sh"; ctl=${a%%:*}; }
        done
        [[ "${FAKE_REFS:-0}" != 1 ]] || git -C "$src" merge-base HEAD origin/main > "$FAKE_STATE/mergebase" 2>/dev/null
        if [[ "${FAKE_INNER:-0}" == 1 ]]; then
            RR_CTL=$ctl RR_SRC=$src bash "$ctl/inner.sh"
            exit $?
        fi
        # canned: one result line per requested step, the first one with rc=FAKE_RC (stop there when not 0)
        # shellcheck source=/dev/null
        . "$ctl/env.sh"
        first=1
        while IFS= read -r t <&3; do
            [[ -n "$t" ]] || continue
            rc=0
            [[ $first -eq 1 ]] && rc=${FAKE_RC:-0}
            echo "=== remote-run target: $t"
            echo "=== remote-run result: $t rc=$rc time=1s peak=1MiB"
            [[ -z "${FAKE_TOTALS:-}" || $first -eq 0 ]] || echo "=== remote-run totals: $t :: $FAKE_TOTALS"
            first=0
            [[ $rc -eq 0 ]] || break
        done 3<<<"$RR_TARGETS"
        if [[ "${FAKE_KILL_PARENT:-0}" == 1 ]]; then
            kill -KILL "$(awk '$1 == "PPid:" {print $2}' "/proc/$PPID/status")"
            sleep 1; exit 137
        fi
        if [[ "${FAKE_SLEEP:-0}" == 1 ]]; then
            trap 'exit 143' TERM
            echo "$cname $clane" >> "$FAKE_STATE/live"
            trap 'grep -vx "$cname $clane" "$FAKE_STATE/live" > "$FAKE_STATE/live.new"; mv -f "$FAKE_STATE/live.new" "$FAKE_STATE/live"' EXIT
            : > "$FAKE_STATE/running"; sleep 60 >/dev/null 2>&1 & wait
        fi
        exit "${FAKE_EXIT:-${FAKE_RC:-0}}" ;;
    *) exit 0 ;;
esac
EOF
# Fake make: logs its arguments; `bad` fails, `readstdin` reads stdin, `tot1` / `tot2` print a totals
# line in each harness format.
cat > "$TMP/bin/make" <<'EOF'
#!/usr/bin/env bash
echo "make $*" >> "$FAKE_STATE/make-calls"
[[ " $* " != *" readstdin "* ]] || cat > /dev/null
[[ " $* " != *" tot1 "* ]] || echo "Total:    4  Passed:    4  Failed:    0  Skipped:    0"
[[ " $* " != *" tot2 "* ]] || echo "  Pass: 7  Fail: 0  Skip: 0"
if [[ " $* " == *" oomy "* ]]; then
    echo "anon 3221225472" > "$RR_CGROUP/memory.stat"; sleep 2
    echo "anon 104857600" > "$RR_CGROUP/memory.stat"; echo "oom_kill 1" > "$RR_CGROUP/memory.events"
fi
[[ " $* " != *" bad "* ]]
EOF
chmod +x "$TMP/bin/podman" "$TMP/bin/make"
export FAKE_STATE=$TMP/state
# a fake cgroup for container-side.sh: memory.peak 2 GiB, anon 100 MiB, no oom kill yet
export RR_CGROUP=$TMP/cg RR_SAMPLE_INTERVAL=0.1
mkdir -p "$RR_CGROUP"
echo 2147483648 > "$RR_CGROUP/memory.peak"; echo "anon 104857600" > "$RR_CGROUP/memory.stat"; echo "oom_kill 0" > "$RR_CGROUP/memory.events"
export PATH=$TMP/bin:$PATH

# ---- a throwaway origin -----------------------------------------------------
REPO=$TMP/origin
git init -q -b main "$REPO"
mkdir -p "$REPO/test"
cp "$ROOT/test/ci-deps.sh" "$REPO/test/ci-deps.sh"
# the second repository (the FPGA core stand-in) and the ci.yml that pins it
FPGA=$TMP/fpga
git init -q -b master "$FPGA"
mkdir -p "$FPGA/cores/zxnext/src" "$FPGA/other"
echo "-- core" > "$FPGA/cores/zxnext/src/a.vhd"; echo x > "$FPGA/other/skipped.txt"
git -C "$FPGA" add -A
git -C "$FPGA" -c user.name=t -c user.email=t@t commit -qm core
FPGA_SHA=$(git -C "$FPGA" rev-parse HEAD)
mkdir -p "$REPO/.github/workflows"
printf 'env:\n      FPGA_CORE_REF: %s   # CORE VERSION x\n' "$FPGA_SHA" > "$REPO/.github/workflows/ci.yml"
git -C "$REPO" add -A
git -C "$REPO" -c user.name=t -c user.email=t@t commit -qm one
git -C "$REPO" branch feature
# a commit like main before the dependency script existed: ci.yml only
LTREE=$(GIT_INDEX_FILE="$TMP/legacy-index" git -C "$REPO" update-index --add --cacheinfo "100644,$(git -C "$REPO" rev-parse HEAD:.github/workflows/ci.yml),.github/workflows/ci.yml" \
    && GIT_INDEX_FILE="$TMP/legacy-index" git -C "$REPO" write-tree)
LEGACY=$(git -C "$REPO" -c user.name=t -c user.email=t@t commit-tree -m legacy "$LTREE")
git -C "$REPO" branch legacy "$LEGACY"
printf 'env:\n      FPGA_CORE_REF: %040d   # not in the core repo\n' 7 > "$REPO/.github/workflows/ci.yml"
git -C "$REPO" -c user.name=t -c user.email=t@t commit -qam badcore
git -C "$REPO" branch badcore
git -C "$REPO" reset -q --hard HEAD~1
SHA=$(git -C "$REPO" rev-parse HEAD)
OTHER=$(printf '%040d' 1)

WORK=$TMP/work
export REMOTE_RUN_REPO_URL="file://$REPO"
export REMOTE_RUN_EXTRA_REPO_URL="file://$FPGA"
export JNEXT_REMOTE_RUN_DIR=$WORK
export REMOTE_RUN_STAMP_LOCAL_DIR=$TMP/stamps
export REMOTE_RUN_MIN_MEM_MB=1 REMOTE_RUN_MIN_DISK_GB=1 RR_SKIP_CGROUP_CHECK=1
unset JNEXT_REMOTE_RUN_HOST

# drive <args...>: run the driver with the remote side as a local shell; sets OUT and RC.
drive() {
    OUT=$(timeout --kill-after=5s 120s bash "$DRIVER" --local-shell --log-dir "$TMP/logs" "$@" 2>&1); RC=$?
}
# drv_fn <function> <args>: call a function of the driver (sourced in a subshell).
# shellcheck disable=SC1090
drv_fn() { ( source "$DRIVER"; "$@" ); }
# hold_lock <seconds> <lock file>...: one separate process takes those locks for that long (waits until
# it has them); `kill $!` releases them. hold_lane holds EVERY lane, so "the remote is busy" keeps its
# meaning now that a free second lane would otherwise take the run.
hold_lock() {
    local secs=$1 f; shift
    mkdir -p "$WORK"
    ( n=7; for f in "$@"; do eval "exec $n>\"\$f\""; flock "$n"; n=$((n - 1)); done; exec sleep "$secs" ) &
    for f in "$@"; do until ! flock -n "$f" true; do sleep 0.1; done; done
}
hold_lane() { hold_lock "$1" "$WORK/lane.lock" "$WORK/lane-L2.lock"; }
calls() { cat "$FAKE_STATE/calls" 2>/dev/null; }
reset_state() { rm -f "$FAKE_STATE"/calls "$FAKE_STATE"/killed "$FAKE_STATE"/roots "$FAKE_STATE"/running "$FAKE_STATE"/live "$FAKE_STATE"/probe-lane2 "$FAKE_STATE"/setup-at-image; }

# RRUN-01: usage refusals exit 2, --help exits 0
out=""
bash "$DRIVER" >/dev/null 2>&1; out+="none=$? "
bash "$DRIVER" feature "$SHA" >/dev/null 2>&1; out+="notarget=$? "
bash "$DRIVER" feature abc unit-test >/dev/null 2>&1; out+="badsha=$? "
bash "$DRIVER" -x "$SHA" unit-test >/dev/null 2>&1; out+="badbranch=$? "
bash "$DRIVER" --bogus feature "$SHA" unit-test >/dev/null 2>&1; out+="badopt=$? "
bash "$DRIVER" --timeout 0 feature "$SHA" unit-test >/dev/null 2>&1; out+="badtimeout=$? "
bash "$DRIVER" --warm feature >/dev/null 2>&1; out+="warmargs=$? "
h=$(bash "$DRIVER" --help 2>&1); out+="help=$? "
[[ "$h" == usage:* ]] && out+="usage-text "
row RRUN-01 "$([[ "$out" == "none=2 notarget=2 badsha=2 badbranch=2 badopt=2 badtimeout=2 warmargs=2 help=0 usage-text " ]] && echo 1 || echo 0)" \
    "argument refusals exit 2, --help exits 0 with the usage text ($out)"

# RRUN-02: no alias set: exit 75 with the reason, nothing contacted
OUT=$(timeout --kill-after=5s 60s bash "$DRIVER" --log-dir "$TMP/logs" --id u1 feature "$SHA" unit-test 2>&1); RC=$?
row RRUN-02 "$([[ $RC -eq 75 && "$OUT" == *"JNEXT_REMOTE_RUN_HOST is not set"* && ! -e "$WORK" ]] && echo 1 || echo 0)" \
    "the alias unset exits 75 and creates nothing (rc=$RC)"

# RRUN-03: --fallback-local runs the targets here through the wrapper, logs it, propagates the status
rm -f "$FAKE_STATE/make-calls"; mkdir -p "$TMP/local"
cat > "$TMP/wrap" <<'EOF'
#!/usr/bin/env bash
echo "wrap $1" >> "$FAKE_STATE/make-calls"
exec "$@"
EOF
chmod +x "$TMP/wrap"
OUT=$(REMOTE_RUN_LOCAL_WRAP="$TMP/wrap" timeout --kill-after=5s 60s bash "$DRIVER" --log-dir "$TMP/logs" --id u2 \
    --fallback-local "$TMP/local" feature "$SHA" unit-test bad unit-test-sdl 2>&1); RC=$?
mk=$(cat "$FAKE_STATE/make-calls" 2>/dev/null)
row RRUN-03 "$([[ $RC -eq 1 && "$OUT" == *"RAN LOCALLY"* && "$mk" == *"make -C $TMP/local unit-test"* \
    && "$mk" == *"wrap env"* && "$mk" != *"unit-test-sdl"* ]] && echo 1 || echo 0)" \
    "the local fallback runs through the wrapper, says so, stops at the failing target (rc=$RC)"

# RRUN-04: the summary keeps result lines, FAIL/SKIP lines and the last totals
cat > "$TMP/sample.log" <<'EOF'
noise line
=== remote-run result: unit-test rc=1 time=12s peak=900MiB
  PASS A-01 fine
  FAIL B-02 broke
  SKIP C-03 not run
-- libspectrum NOT found: szx_probe/sna_probe not built, rows will SKIP
Total:  10  Passed:  8  Failed:  1  Skipped:  1
=== remote-run result: unit-test rc=1 time=12s peak=900MiB
=== remote-run totals: unit-test :: Total: 10 Passed: 8 Failed: 1 Skipped: 1
=== remote-run result: unit-test-sdl rc=0 time=3s peak=800MiB
=== remote-run totals: unit-test-sdl :: Total: 4 Passed: 4 Failed: 0 Skipped: 0
EOF
cat > "$TMP/clean.log" <<'EOF'
=== remote-run result: regression rc=0 time=9s peak=1MiB
=== remote-run totals: regression :: Pass: 300 Fail: 0 Skip: 0
EOF
cat > "$TMP/dirty.log" <<'EOF'
=== remote-run result: regression rc=0 time=9s peak=1MiB
=== remote-run totals: regression :: Pass: 299 Fail: 0 Skip: 1
EOF
sm=$(drv_fn summarize_log "$TMP/sample.log")
row RRUN-04 "$([[ "$sm" == *"unit-test rc=1"* && "$sm" == *"unit-test-sdl rc=0"* && "$sm" == *"FAIL B-02"* \
    && "$sm" == *"SKIP C-03"* && "$sm" == *"FAIL / SKIP lines: 2"* \
    && "$sm" == *"totals: unit-test :: Total: 10 Passed: 8 Failed: 1 Skipped: 1"* \
    && "$sm" == *"totals: unit-test-sdl :: Total: 4 Passed: 4"* && "$sm" == *"verdict: NOT CLEAN"* \
    && "$sm" != *"noise line"* && "$sm" != *"PASS A-01"* \
    && "$(drv_fn verdict_of_log "$TMP/clean.log")" == CLEAN \
    && "$(drv_fn verdict_of_log "$TMP/dirty.log")" == "NOT CLEAN"* ]] && echo 1 || echo 0)" \
    "the summary extracts results, FAIL/SKIP names, each step's totals and the verdict; a green step with a skip is not clean"

# RRUN-05: a sha that is not the branch tip is refused (exit 3) before anything is built or run
reset_state
drive --id s1 feature "$OTHER" unit-test
row RRUN-05 "$([[ $RC -eq 3 && "$OUT" == *"not at the expected $OTHER"* ]] && ! grep -qE '^(run|build)|(^| )(run|build) ' <<<"$(calls)" && echo 1 || echo 0)" \
    "a sha other than the pushed tip is refused with exit 3, no build, no run (rc=$RC)"

# RRUN-06: a branch that is not on the repository is refused (exit 3)
reset_state
drive --id s2 nosuchbranch "$SHA" unit-test
row RRUN-06 "$([[ $RC -eq 3 && "$OUT" == *"is not on the remote repository"* ]] && echo 1 || echo 0)" \
    "an unpushed branch is refused with exit 3 (rc=$RC)"

# RRUN-07: no image for the current deps: exit 75 telling to run --warm, nothing built
reset_state
drive --id i1 feature "$SHA" unit-test
row RRUN-07 "$([[ $RC -eq 75 && "$OUT" == *"remote-run --warm"* ]] && ! grep -qE '(^| )build ' <<<"$(calls)" && echo 1 || echo 0)" \
    "a normal run without the image exits 75 naming --warm, builds nothing (rc=$RC)"

# RRUN-08: --warm builds the image under the work dir; the next run reaches the container with
# volumes and podman storage under the work dir, copies the stamp back and removes its checkout
reset_state
drive --warm --id w1
warm_rc=$RC
built=$(calls | grep -c ' build ' || true)
reset_state
t0=$SECONDS
RR_HEARTBEAT=60 FAKE_STAMP=1 FAKE_RC=0 drive --id r1 feature "$SHA" unit-test sh:true
run_rc=$RC; run_took=$((SECONDS - t0))
# nothing the run started (the watchdog's sleep) may still hold the lane lock once the driver returned
# nor may it keep the session open: the driver returns long before the (60 s here) heartbeat sleep ends
lane_free=0; flock -n "$WORK/lane.lock" true && [[ $run_took -lt 45 ]] && lane_free=1
runline=$(calls | grep -E '(^| )run ' | head -n 1)
roots_ok=1
while read -r s r; do [[ "$s" == "$WORK/"* && "$r" == "$WORK/"* ]] || roots_ok=0; done < "$FAKE_STATE/roots"
vols_ok=1
while read -r v; do [[ "$v" == "$WORK/"* ]] || vols_ok=0; done < <(grep -oE -- '-v [^ ]+' <<<"$runline" | cut -d' ' -f2)
[[ "$runline" == *"/root/.cache/ccache"* && "$runline" == *":/root/.jnext "* ]] || vols_ok=0
mem_ok=0; [[ "$runline" == *"--memory "*"--memory-swap "* ]] && mem_ok=1
drv_ok=0; [[ "$(cat "$WORK/containers/driver" 2>/dev/null)" == overlay ]] && drv_ok=1
row RRUN-08 "$([[ $warm_rc -eq 0 && $built -eq 1 && $run_rc -eq 0 && $roots_ok -eq 1 && $vols_ok -eq 1 && $mem_ok -eq 1 && $drv_ok -eq 1 && $lane_free -eq 1 \
    && -f "$TMP/stamps/fakekey" && -z "$(find "$WORK/runs" "$WORK/ctl" "$WORK/stamps" -mindepth 1)" ]] && echo 1 || echo 0)" \
    "warm builds once; a run mounts only dirs under the work dir, stores podman there, records the driver, caps memory, returns the stamp, leaves no checkout (warm=$warm_rc run=$run_rc roots=$roots_ok vols=$vols_ok mem=$mem_ok drv=$drv_ok lane=$lane_free took=${run_took}s)"

# RRUN-09: a failing container exits the driver with 1, copies no stamp, and the log keeps the result
reset_state; rm -rf "$TMP/stamps"
FAKE_STAMP=1 FAKE_RC=1 drive --id f1 feature "$SHA" unit-test
row RRUN-09 "$([[ $RC -eq 1 && ! -e "$TMP/stamps" && "$OUT" == *"=== remote-run result: unit-test rc=1"* ]] && echo 1 || echo 0)" \
    "a failing run exits 1, writes no stamp, and the summary shows the result line (rc=$RC)"

# RRUN-10: TERM to the driver's own pid (its own session, so no process-group kill helps) stops the
# container and removes the checkout (the orphan guard); bounds are generous for a loaded host
reset_state
FAKE_SLEEP=1 setsid bash "$DRIVER" --local-shell --log-dir "$TMP/logs" --id k1 \
    feature "$SHA" unit-test >/dev/null 2>&1 &
dpid=$!
for _ in $(seq 1 900); do [[ -e "$FAKE_STATE/running" ]] && break; sleep 0.2; done
kill -TERM "$dpid" 2>/dev/null
for _ in $(seq 1 450); do
    kill -0 "$dpid" 2>/dev/null || break
    sleep 0.2
done
dead=1; kill -0 "$dpid" 2>/dev/null && { dead=0; kill -KILL "$dpid" 2>/dev/null; }
for _ in $(seq 1 450); do [[ -e "$FAKE_STATE/killed" && -z "$(find "$WORK/runs" -mindepth 1)" ]] && break; sleep 0.2; done
wait "$dpid" 2>/dev/null
row RRUN-10 "$([[ -e "$FAKE_STATE/running" && $dead -eq 1 && "$(cat "$TMP/logs/k1.log")" == *"remote-run interrupted by a signal"* ]] && grep -q '^rr-jnext-k1$' "$FAKE_STATE/killed" 2>/dev/null \
    && [[ -z "$(find "$WORK/runs" -mindepth 1)" ]] && echo 1 || echo 0)" \
    "TERM to the driver alone kills the named container and removes the run checkout, the driver exits"

# RRUN-11: nothing was written outside the scratch tree's work/log/stamp dirs: HOME is empty
row RRUN-11 "$([[ -z "$(find "$HOME" -mindepth 1 2>/dev/null)" && ! -e "$FAKE_STATE/noroot" ]] && echo 1 || echo 0)" \
    "no work data outside the work directory: the scratch HOME stays empty and podman is never called without --root"

# RRUN-12: ci-deps.sh: sets are unions, unknown set refused, ci.yml holds no package list, all
# scripts of the skill are shellcheck-clean
ci_ok=1
l1=$(bash "$ROOT/test/ci-deps.sh" --list test)
l2=$(bash "$ROOT/test/ci-deps.sh" --list test mingw)
[[ "$l1" == "$(sort -u <<<"$l1")" && $(wc -l <<<"$l2") -gt $(wc -l <<<"$l1") ]] || ci_ok=0
grep -qx 'qt6-qtbase-devel' <<<"$l1" || ci_ok=0
bash "$ROOT/test/ci-deps.sh" --list nonesuch >/dev/null 2>&1; [[ $? -eq 2 ]] || ci_ok=0
bash "$ROOT/test/ci-deps.sh" >/dev/null 2>&1; [[ $? -eq 2 ]] || ci_ok=0
inline=$(grep -nE 'dnf install -y +[a-z0-9]' "$ROOT/.github/workflows/ci.yml" || true)
grep -qvE 'dnf install -y git$' <<<"$inline" && ci_ok=0
grep -q 'bash test/ci-deps.sh test$' "$ROOT/.github/workflows/ci.yml" || ci_ok=0
for f in "$DRIVER" "$SKILL/remote-side.sh" "$SKILL/container-side.sh" "$ROOT/test/ci-deps.sh"; do bash -n "$f" 2>/dev/null || ci_ok=0; done
row RRUN-12 "$ci_ok" "ci-deps.sh sets/refusals, ci.yml calls it with no inline package list, the scripts parse"

# RRUN-13: --gate runs the batch gate's steps in CLAUDE.md's order with the gate timeout, and
# takes exactly a branch and a sha
reset_state
FAKE_RC=0 drive --gate --id g1 feature "$SHA"
gate_rc=$RC
steps=$(sed -n "s/^export RR_TARGETS=//p" "$FAKE_STATE/env.sh" | head -n 1)
expect='$'"'"'clean\ngui-release\nunit-test\nunit-test-sdl\nunit-test-win\nsh:./build/test/fuse_z80_test build/test/fuse\nregression\nregression-win\nwasm-core\nwasm-browser-test\nunit-test-wasm\nregression-wasm'"'"
bash "$DRIVER" --gate feature >/dev/null 2>&1; usage_rc=$?
bash "$DRIVER" --gate --warm >/dev/null 2>&1; excl_rc=$?
row RRUN-13 "$([[ $gate_rc -eq 0 && "$steps" == "$expect" && $usage_rc -eq 2 && $excl_rc -eq 2 ]] && echo 1 || echo 0)" \
    "--gate sends the twelve gate steps in order and refuses missing arguments and --warm (rc=$gate_rc usage=$usage_rc excl=$excl_rc)"

# RRUN-14: --deps-ref / --ref: a commit without the dependency script is refused (75) unless the
# script comes from another branch; --warm --ref warms that branch; the log names the supplier
reset_state
drive --id d1 legacy "$LEGACY" unit-test
d1=$RC; grep -q 'ci-deps.sh is not in' "$TMP/logs/d1.log" || d1=99
drive --id d2 --warm --ref legacy --deps-ref feature
d2=$RC
drive --id d3 legacy "$LEGACY" unit-test --deps-ref feature
d3=$RC
bash "$DRIVER" --ref feature feature "$SHA" unit-test >/dev/null 2>&1; d4=$?
bash "$DRIVER" --deps-ref 'bad branch' feature "$SHA" unit-test >/dev/null 2>&1; d5=$?
row RRUN-14 "$([[ $d1 -eq 75 && $d2 -eq 0 && $d3 -eq 0 && $d4 -eq 2 && $d5 -eq 2 \
    && "$OUT" == *"deps: dependency script test/ci-deps.sh taken from --deps-ref feature @ $SHA"* \
    && "$(grep -a 'source:' "$TMP/logs/d2.log")" == *"legacy @ $LEGACY"* ]] && echo 1 || echo 0)" \
    "--deps-ref supplies the dependency script, --warm --ref warms that branch, the log names the supplier (d1=$d1 d2=$d2 d3=$d3 d4=$d4 d5=$d5)"

# RRUN-15: the pinned second repository (read from ci.yml at the commit under test) is cloned
# sparse into the work dir, mounted read-only with its env var, reused on the next run, and a
# pin that cannot be fetched refuses the run (75)
reset_state; rm -rf "$WORK/cache/extra-repo"
drive --id e1 feature "$SHA" unit-test --deps-ref feature
e1=$RC
runline=$(calls | grep -E '(^| )run ' | head -n 1)
drive --id e2 feature "$SHA" unit-test --deps-ref feature
e2=$RC
files_ok=0
[[ -f "$WORK/cache/extra-repo/cores/zxnext/src/a.vhd" && ! -e "$WORK/cache/extra-repo/other/skipped.txt" ]] && files_ok=1
drive --id e3 badcore "$(git -C "$REPO" rev-parse badcore)" unit-test --deps-ref feature
e3=$RC
row RRUN-15 "$([[ $e1 -eq 0 && $e2 -eq 0 && $e3 -eq 75 \
    && "$runline" == *"-v $WORK/cache/extra-repo:/fpga-core:ro"* && "$runline" == *"-e JNEXT_FPGA_SRC=/fpga-core/cores/zxnext/src"* \
    && $files_ok -eq 1 \
    && "$(grep -a 'extra repository:' "$TMP/logs/e1.log")" == *"cloning"*"@ $FPGA_SHA"* \
    && "$(grep -a 'extra repository:' "$TMP/logs/e2.log")" == *"reusing"* \
    && "$(grep -a 'fallback:' "$TMP/logs/e3.log")" == *"cannot fetch"* ]] && echo 1 || echo 0)" \
    "the pinned second repository: read from ci.yml, sparse clone under the work dir, ro mount + env, reused, unfetchable pin refuses (e1=$e1 e2=$e2 e3=$e3)"

# RRUN-16: a busy lane costs a plain run no time: it waits LANE_WAIT, exits 75 and --fallback-local
# runs the targets here at once; the remote work is not touched
reset_state; rm -f "$FAKE_STATE/make-calls"; mkdir -p "$WORK"
hold_lane 20; holder=$!
# a live run's store must survive a probe that fails while the lane is busy
rm -f "$WORK/containers/driver"; mkdir -p "$WORK/containers/storage"; : > "$WORK/containers/storage/live"
start=$SECONDS
FAKE_INFO_FAIL=1 REMOTE_RUN_LANE_WAIT=2 drive --id l1 --fallback-local "$TMP/local" feature "$SHA" unit-test
l1=$RC; took=$((SECONDS - start))
live_ok=0; [[ -e "$WORK/containers/storage/live" ]] && live_ok=1; rm -f "$WORK/containers/storage/live"
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
row RRUN-16 "$([[ $l1 -eq 0 && $took -lt 60 && "$OUT" == *"RAN LOCALLY"* && $live_ok -eq 1 && "$(cat "$FAKE_STATE/make-calls")" == *"make -C $TMP/local unit-test"* \
    && "$(grep -a 'fallback:' "$TMP/logs/l1.log")" == *"lane stayed busy for 2s"* ]] && echo 1 || echo 0)" \
    "a busy lane: the plain run gives up after LANE_WAIT and runs locally at once, the store is untouched (rc=$l1, ${took}s, live=$live_ok)"

# RRUN-17: --gate waits for a busy lane (GATE_LANE_WAIT) and then runs on the remote
reset_state
hold_lane 6; holder=$!
REMOTE_RUN_LANE_WAIT=1 REMOTE_RUN_GATE_LANE_WAIT=120 drive --gate --id l2 feature "$SHA"
l2=$RC
wait "$holder" 2>/dev/null
row RRUN-17 "$([[ $l2 -eq 0 && "$(grep -a 'queued' "$TMP/logs/l2.log")" == *"waiting up to 120s"* \
    && "$(grep -ac 'remote-run exit: 0' "$TMP/logs/l2.log")" -eq 1 ]] && echo 1 || echo 0)" \
    "--gate queues behind a busy lane with its own long wait, then runs on the remote (rc=$l2)"

# The next rows run the REAL container-side.sh (FAKE_INNER=1: the fake podman executes it, with a fake
# make) so the verdict path of every step is covered, not just the driver's.
runin() { FAKE_INNER=1 drive "$@"; }
rlines() { grep -ac '^=== remote-run result: ' "$TMP/logs/$1.log"; }

# RRUN-18: a failing make step: the container exits 1, the result line carries rc=1, the verdict is NOT CLEAN
reset_state; rm -f "$FAKE_STATE/make-calls"
runin --id c1 feature "$SHA" bad
c1=$RC
row RRUN-18 "$([[ $c1 -eq 1 && "$(grep -a 'result: bad' "$TMP/logs/c1.log")" == *"rc=1"* && "$OUT" == *"verdict: NOT CLEAN"* \
    && "$(grep -a 'remote-run exit:' "$TMP/logs/c1.log")" == "=== remote-run exit: 1" ]] && echo 1 || echo 0)" \
    "a failing make step: rc=1 in its result line, the container itself exits 1, driver exit 1, verdict NOT CLEAN (rc=$c1)"

# RRUN-19: both harness totals formats are extracted per step, and the run is CLEAN
reset_state
runin --id c2 feature "$SHA" tot1 tot2
c2=$RC
row RRUN-19 "$([[ $c2 -eq 0 && "$OUT" == *"totals: tot1 :: Total: 4 Passed: 4 Failed: 0 Skipped: 0"* \
    && "$OUT" == *"totals: tot2 :: Pass: 7 Fail: 0 Skip: 0"* && "$OUT" == *"verdict: CLEAN"* ]] && echo 1 || echo 0)" \
    "each step's own totals line is extracted in both formats; the run is CLEAN (rc=$c2)"

# RRUN-20: fail-fast stops after the first failing step; --keep-going runs the rest and still exits 1
reset_state; rm -f "$FAKE_STATE/make-calls"
runin --id c3 feature "$SHA" bad ok1
c3=$RC; n3=$(rlines c3); ran3=$(grep -c 'ok1' "$FAKE_STATE/make-calls" || true)
rm -f "$FAKE_STATE/make-calls"
runin --keep-going --id c4 feature "$SHA" bad ok2
c4=$RC; n4=$(rlines c4); ran4=$(grep -c 'ok2' "$FAKE_STATE/make-calls" || true)
row RRUN-20 "$([[ $c3 -eq 1 && $n3 -eq 1 && $ran3 -eq 0 && $c4 -eq 1 && $n4 -eq 2 && $ran4 -eq 1 ]] && echo 1 || echo 0)" \
    "fail-fast stops at the first failure; --keep-going runs the rest and exits 1 (ff: rc=$c3 results=$n3 ran=$ran3; kg: rc=$c4 results=$n4 ran=$ran4)"

# RRUN-21: a step that reads stdin cannot swallow the steps after it, in the container or in the local fallback
reset_state
runin --id c5 feature "$SHA" 'sh:cat >/dev/null' 'sh:echo after-step' readstdin ok3
c5=$RC; n5=$(rlines c5)
mkdir -p "$TMP/local"
OUT=$(timeout --kill-after=5s 60s bash "$DRIVER" --log-dir "$TMP/logs" --id c6 --fallback-local "$TMP/local" \
    feature "$SHA" 'sh:cat >/dev/null' 'sh:echo after-local' </dev/null 2>&1); c6=$?
n6=$(rlines c6)
row RRUN-21 "$([[ $c5 -eq 0 && $n5 -eq 4 && $c6 -eq 0 && $n6 -eq 2 && "$(cat "$TMP/logs/c6.log")" == *"after-local"* \
    && "$(cat "$TMP/logs/c5.log")" == *"after-step"* ]] && echo 1 || echo 0)" \
    "a stdin-reading step does not swallow the later steps (container: rc=$c5 results=$n5; local: rc=$c6 results=$n6)"

# RRUN-22: the verdict of a log: rc=1 only, no result line, a missing step, a clean match, bootstrap ignored
cat > "$TMP/v1.log" <<'EOF'
=== remote-run result: a rc=1 time=1s peak=1MiB
EOF
: > "$TMP/v2.log"
cat > "$TMP/v3.log" <<'EOF'
=== remote-run result: bootstrap rc=0 time=0s peak=1MiB
=== remote-run result: a rc=0 time=1s peak=1MiB
EOF
v1=$(drv_fn verdict_of_log "$TMP/v1.log" 1); v2=$(drv_fn verdict_of_log "$TMP/v2.log" 1)
v3=$(drv_fn verdict_of_log "$TMP/v3.log" 2); v4=$(drv_fn verdict_of_log "$TMP/v3.log" 1)
row RRUN-22 "$([[ "$v1" == "NOT CLEAN"*"non-zero"* && "$v2" == "NOT CLEAN"*"no step finished"* \
    && "$v3" == "NOT CLEAN"*"2 steps requested, 1 reported"* && "$v4" == CLEAN ]] && echo 1 || echo 0)" \
    "the verdict needs rc=0 everywhere and exactly one result line per requested step (v1='$v1' v2='$v2' v3='$v3' v4='$v4')"

# RRUN-23: a container that exits 0 with a Failed:1 totals line is not green: the driver exits 1, no stamp
reset_state; rm -rf "$TMP/stamps"
FAKE_STAMP=1 FAKE_TOTALS='Total: 4 Passed: 3 Failed: 1 Skipped: 0' drive --id c7 feature "$SHA" unit-test
c7=$RC
row RRUN-23 "$([[ $c7 -eq 1 && "$OUT" == *"not clean"* && ! -e "$TMP/stamps" ]] && echo 1 || echo 0)" \
    "exit 0 with failures in the totals: the driver exits 1 and copies no stamp (rc=$c7)"

# RRUN-24: the transport dying after the run started is exit 1 "connection lost", never 0, no stamp
reset_state; rm -rf "$TMP/stamps"
FAKE_STAMP=1 FAKE_KILL_PARENT=1 drive --id c8 feature "$SHA" unit-test
c8=$RC
row RRUN-24 "$([[ $c8 -eq 1 && "$(cat "$TMP/logs/c8.log")" == *"connection lost"* && ! -e "$TMP/stamps" ]] && echo 1 || echo 0)" \
    "a session lost after the start exits 1 with 'connection lost' and no stamp (rc=$c8)"

# RRUN-25: a stamp waiting to be fetched survives a later run's sweep; only a day-old one goes
reset_state
mkdir -p "$WORK/stamps/recent" "$WORK/stamps/ancient"
touch -d '2 hours ago' "$WORK/stamps/recent"; touch -d '3 days ago' "$WORK/stamps/ancient"
mkdir -p "$WORK/cache/jnext-home/runs/recent" "$WORK/cache/jnext-home/runs/ancient"
touch -d '2 hours ago' "$WORK/cache/jnext-home/runs/recent"; touch -d '3 days ago' "$WORK/cache/jnext-home/runs/ancient"
drive --id c9 feature "$SHA" unit-test
c9=$RC
row RRUN-25 "$([[ $c9 -eq 0 && -d "$WORK/stamps/recent" && ! -e "$WORK/stamps/ancient" \
    && -d "$WORK/cache/jnext-home/runs/recent" && ! -e "$WORK/cache/jnext-home/runs/ancient" ]] && echo 1 || echo 0)" \
    "the sweeps keep a 2-hour-old stamp dir / run scratch and remove a 3-day-old one (rc=$c9)"

# RRUN-26: the SD master and every run's clone/scratch share ONE bind mount (a clone across mounts
# cannot be a reflink: sdcard-isolation-func D/E), and an old sdcard cache dir is migrated into it
reset_state
mkdir -p "$WORK/cache/sdcard"; echo master > "$WORK/cache/sdcard/cspect-next-1gb-fixed.img"
rm -rf "$WORK/cache/jnext-home"
drive --id m1 feature "$SHA" unit-test
m1=$RC
runline=$(calls | grep -E '(^| )run ' | head -n 1)
# the mount covering a container path: the longest mount point that is a prefix of it
covering() {
    local want=$1 best="" bestlen=0 v mp host
    while read -r v; do
        host=${v%%:*}; mp=${v#*:}; mp=${mp%%:*}
        if [[ "$want" == "$mp" || "$want" == "$mp"/* ]] && [[ ${#mp} -gt $bestlen ]]; then best=$host; bestlen=${#mp}; fi
    done < <(grep -oE -- '-v [^ ]+' <<<"$runline" | cut -d' ' -f2)
    echo "$best"
}
cov_master=$(covering /root/.jnext/sdcard/cspect-next-1gb-fixed.img)
cov_runs=$(covering /root/.jnext/runs/1-2-3/sdcard/cspect-next-1gb-fixed.img)
row RRUN-26 "$([[ $m1 -eq 0 && -n "$cov_master" && "$cov_master" == "$cov_runs" && "$cov_master" == "$WORK/cache/jnext-home" \
    && "$(cat "$WORK/cache/jnext-home/sdcard/cspect-next-1gb-fixed.img" 2>/dev/null)" == master && ! -e "$WORK/cache/sdcard" ]] && echo 1 || echo 0)" \
    "the SD master and a run's clone path are under one mount ($cov_master), the old cache dir was migrated (rc=$m1)"

# RRUN-27: the container exiting non-zero while every step printed rc=0 (a podman 125 at teardown, an
# OOM kill after the last result line) is not green: driver exit 1, no stamp
reset_state; rm -rf "$TMP/stamps"
FAKE_STAMP=1 FAKE_EXIT=3 drive --id x1 feature "$SHA" unit-test
x1=$RC
row RRUN-27 "$([[ $x1 -eq 1 && "$(grep -a 'remote-run exit:' "$TMP/logs/x1.log")" == "=== remote-run exit: 3" \
    && "$(grep -a 'result:' "$TMP/logs/x1.log")" == *"rc=0"* && ! -e "$TMP/stamps" ]] && echo 1 || echo 0)" \
    "a container that exits 3 after clean result lines is exit 1 with no stamp (rc=$x1)"

# RRUN-28: each step's own anonymous-memory peak is logged next to the cgroup's cumulative peak (a step
# that spikes to 3 GiB reads 3072, the next one 100 again), and an oom_kill during a step is logged
# and makes the run NOT CLEAN even when every step and the container exit 0
reset_state
runin --id o1 feature "$SHA" oomy tot1
o1=$RC
row RRUN-28 "$([[ $o1 -eq 1 && "$(grep -a 'result: oomy' "$TMP/logs/o1.log")" == *"peak=2048MiB anon=3072MiB"* \
    && "$(grep -a 'result: tot1' "$TMP/logs/o1.log")" == *"anon=100MiB"* \
    && "$(grep -a 'remote-run oom:' "$TMP/logs/o1.log")" == *"oomy killed 1 process"* \
    && "$OUT" == *"memory limit killed a process"* ]] && echo 1 || echo 0)" \
    "per-step anon peak logged; an oom kill is logged and the run is NOT CLEAN (rc=$o1)"

# RRUN-29: a step that prints a fallback marker of its own at column 0 and fails stays a red run: exit 1,
# not 75, no local re-run (the markers count only before the run started)
reset_state
runin --id n1 --fallback-local "$TMP/local" feature "$SHA" 'sh:echo "=== remote-run fallback: x"; false'
n1=$RC
row RRUN-29 "$([[ $n1 -eq 1 && "$OUT" != *"RAN LOCALLY"* && "$(cat "$TMP/logs/n1.log")" != *"RAN LOCALLY"* \
    && "$(grep -a 'remote-run exit:' "$TMP/logs/n1.log")" == "=== remote-run exit: 1" ]] && echo 1 || echo 0)" \
    "a step's own '=== remote-run fallback:' line does not turn a failing run into a fallback (rc=$n1)"

# RRUN-30: a podman that is not rootless is refused before anything is built or run (75)
reset_state
FAKE_ROOTLESS=false drive --id n2 feature "$SHA" unit-test
n2=$RC
row RRUN-30 "$([[ $n2 -eq 75 && "$(grep -a 'fallback:' "$TMP/logs/n2.log")" == *"not rootless"* ]] \
    && ! grep -qE '(^| )(run|build) ' <<<"$(calls)" && echo 1 || echo 0)" \
    "a podman that is not rootless: exit 75 'not rootless', nothing built or run (rc=$n2)"

# ---- two lanes (GH #547) ------------------------------------------------------
# The lane locks are released by their holders' exit; rows below start from "no lock held".
LOCK1=$WORK/lane.lock
ids_lane() { grep -aE "lane [0-9] of" "$TMP/logs/$1.log" | head -n 1; }

# RRUN-31: two runs at once: while A lives in lane 1 (and its checkout looks like an orphan of two hours),
# B takes lane 2 at once, succeeds, and neither kills A's container nor sweeps A's checkout
reset_state; rm -rf "$WORK/runs-L2" "$WORK/ctl-L2"
# orphans seeded before A starts: a lane-1 container (A must kill it) and a lane-2 one (A must not)
printf 'rr-jnext-orph 1\nrr-L2-jnext-other 2\n' > "$FAKE_STATE/live"
FAKE_SLEEP=1 setsid bash "$DRIVER" --local-shell --log-dir "$TMP/logs" --id twoA feature "$SHA" unit-test >/dev/null 2>&1 &
apid=$!
for _ in $(seq 1 900); do [[ -e "$FAKE_STATE/running" ]] && break; sleep 0.2; done
a_orph=0; grep -qx 'rr-jnext-orph' "$FAKE_STATE/killed" 2>/dev/null && ! grep -qx 'rr-L2-jnext-other' "$FAKE_STATE/killed" && a_orph=1
touch -d '2 hours ago' "$WORK/runs/twoA" "$WORK/ctl/twoA"
start=$SECONDS
REMOTE_RUN_LANE_WAIT=20 drive --id twoB feature "$SHA" unit-test
b_rc=$RC; b_took=$((SECONDS - start))
a_alive=0; kill -0 "$apid" 2>/dev/null && a_alive=1
a_dir=0; [[ -d "$WORK/runs/twoA" ]] && a_dir=1
a_killed=0; grep -q 'twoA' "$FAKE_STATE/killed" 2>/dev/null && a_killed=1
kill -TERM "$apid" 2>/dev/null
for _ in $(seq 1 450); do kill -0 "$apid" 2>/dev/null || break; sleep 0.2; done
kill -KILL "$apid" 2>/dev/null; wait "$apid" 2>/dev/null
row RRUN-31 "$([[ $b_rc -eq 0 && $b_took -lt 15 && "$(ids_lane twoB)" == *"lane 2 of 2"* && "$(ids_lane twoA)" == *"lane 1 of 2"* \
    && $a_alive -eq 1 && $a_dir -eq 1 && $a_killed -eq 0 && $a_orph -eq 1 && "$OUT" == *"verdict: CLEAN"* \
    && "$(grep -a 'RR_JOBS_DEFAULT' "$FAKE_STATE/env.sh")" == *"RR_JOBS_DEFAULT=6 RR_JOBS_HEAVY=3"* && "$(calls | grep -E '(^| )run ' | grep -c 'JNEXT_TEST_JOBS=6')" -eq 1 ]] && echo 1 || echo 0)" \
    "run B takes lane 2 beside live run A at halved CPU caps, and neither kills A's container nor sweeps A's checkout, while A kills its own lane's orphan only (rc=$b_rc ${b_took}s alive=$a_alive dir=$a_dir killed=$a_killed orph=$a_orph)"

# RRUN-32: a lane-2 run's names, mounts and label; the per-lane sweep; cleanup mode finds lane 2's dirs
reset_state
mkdir -p "$WORK/cache/jnext-home-L2/runs/x" "$WORK/cache/jnext-home/runs/y"
touch -d '3 days ago' "$WORK/cache/jnext-home-L2/runs/x" "$WORK/cache/jnext-home/runs/y"
hold_one() { hold_lock "$1" "$LOCK1"; }
hold_one 40; holder=$!
printf 'rr-L2-jnext-orph2 2\nrr-jnext-keep 1\n' > "$FAKE_STATE/live"
FAKE_KILL_PARENT=1 drive --id lane2 feature "$SHA" unit-test
l2_rc=$RC
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
runline=$(calls | grep -E '(^| )run ' | head -n 1)
orph_ok=0; grep -qx 'rr-L2-jnext-orph2' "$FAKE_STATE/killed" 2>/dev/null && ! grep -qx 'rr-jnext-keep' "$FAKE_STATE/killed" && orph_ok=1
setup_ok=0; [[ "$(tail -n 1 "$FAKE_STATE/setup-at-image" 2>/dev/null)" == held ]] && setup_ok=1
row RRUN-32 "$([[ $l2_rc -eq 1 && $orph_ok -eq 1 && $setup_ok -eq 1 && "$runline" == *"-v $WORK/runs-L2/lane2/src:/src"* && "$runline" == *"-v $WORK/ctl-L2/lane2:/rr:ro"* \
    && "$runline" == *"-v $WORK/cache/jnext-home-L2:/root/.jnext"* && "$runline" == *"-v $WORK/cache/extra-repo-L2:/fpga-core:ro"* \
    && "$runline" == *"-v $WORK/cache/ccache:/root/.cache/ccache"* && "$runline" == *"-v $WORK/cache/tools:/root/.cache/jnext"* \
    && "$runline" == *"--label rr-lane=2"* && "$runline" == *"--name rr-L2-jnext-lane2 "* \
    && ! -e "$WORK/cache/jnext-home-L2/runs/x" && -e "$WORK/cache/jnext-home/runs/y" \
    && ! -e "$WORK/runs-L2/lane2" && ! -e "$WORK/ctl-L2/lane2" && "$(grep -a RR_BOOTSTRAP "$FAKE_STATE/env.sh")" == *"sdcard-image"* ]] && echo 1 || echo 0)" \
    "lane 2: runs-L2/ctl-L2, jnext-home-L2 and extra-repo-L2 mounts, shared ccache and tools, label and name; its own sweep; cleanup mode removes its dirs; the bootstrap provisions an empty SD home; the lane kills its own orphan only; the setup lock is held at the image check (rc=$l2_rc orph=$orph_ok setup=$setup_ok)"

# RRUN-33: LANES=1 is the one-lane contract: a busy lane.lock gives 75 / local run, no second lock appears
reset_state; rm -f "$WORK/lane-L2.lock"; rm -f "$FAKE_STATE/make-calls"; mkdir -p "$TMP/local"
hold_one 20; holder=$!
REMOTE_RUN_LANES=1 REMOTE_RUN_LANE_WAIT=2 drive --id one1 --fallback-local "$TMP/local" feature "$SHA" unit-test
o_rc=$RC
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
row RRUN-33 "$([[ $o_rc -eq 0 && "$OUT" == *"RAN LOCALLY"* && "$(grep -a 'fallback:' "$TMP/logs/one1.log")" == *"every lane stayed busy for 2s"* \
    && ! -e "$WORK/lane-L2.lock" ]] && echo 1 || echo 0)" \
    "LANES=1: a busy lane.lock exits 75 (runs locally with --fallback-local) and no lane-L2.lock is created (rc=$o_rc)"

# RRUN-34: the setup lock: a run waits for it, and past LANE_WAIT gives up with 75
reset_state
hold_lock 4 "$WORK/setup.lock"; holder=$!
start=$SECONDS
REMOTE_RUN_LANE_WAIT=30 drive --id set1 feature "$SHA" unit-test
s1=$RC; s1_took=$((SECONDS - start))
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
hold_lock 10 "$WORK/setup.lock"; holder=$!
REMOTE_RUN_LANE_WAIT=1 drive --id set2 feature "$SHA" unit-test
s2=$RC
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
row RRUN-34 "$([[ $s1 -eq 0 && $s1_took -ge 3 && "$(cat "$TMP/logs/set1.log")" == *"waiting for the setup lock"* \
    && $s2 -eq 75 && "$(grep -a 'fallback:' "$TMP/logs/set2.log")" == *"setup lock stayed busy for 1s"* ]] && echo 1 || echo 0)" \
    "the setup lock: a run waits for it (${s1_took}s, rc=$s1), and gives up with 75 after LANE_WAIT (rc=$s2)"

# RRUN-35: no recorded storage driver and another lane live: the probe is refused (75) and the live
# store stays; with no lane held the probe runs and the driver is recorded
reset_state
hold_one 40; holder=$!
rm -f "$WORK/containers/driver"; mkdir -p "$WORK/containers/storage"; : > "$WORK/containers/storage/live"
FAKE_INFO_FAIL=1 drive --id pr1 feature "$SHA" unit-test
p1=$RC
live_ok=0; [[ -e "$WORK/containers/storage/live" ]] && live_ok=1
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
drive --id pr2 feature "$SHA" unit-test
p2=$RC
drec=$(cat "$WORK/containers/driver" 2>/dev/null)
probe_ok=0; [[ "$(tail -n 1 "$FAKE_STATE/probe-lane2" 2>/dev/null)" == held ]] && probe_ok=1
# a run that waits for a lane while another run records the driver must use it (vfs here) once it has its lane
rm -f "$WORK/containers/driver" "$FAKE_STATE/drivers"
hold_lane 4; holder=$!
( sleep 1.5; echo vfs > "$WORK/containers/driver" ) &
REMOTE_RUN_LANE_WAIT=30 drive --id pr3 feature "$SHA" unit-test
p3=$RC
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
drv_ok=0; [[ "$(tail -n 1 "$FAKE_STATE/drivers" 2>/dev/null)" == vfs ]] && drv_ok=1
echo overlay > "$WORK/containers/driver"
row RRUN-35 "$([[ $p1 -eq 75 && "$(grep -a 'fallback:' "$TMP/logs/pr1.log")" == *"another lane is live"* && $live_ok -eq 1 \
    && $p2 -eq 0 && "$drec" == overlay && $probe_ok -eq 1 && $p3 -eq 0 && $drv_ok -eq 1 ]] && echo 1 || echo 0)" \
    "the storage probe needs every lane: refused while another lane is live, store kept (rc=$p1, live=$live_ok); runs when idle with the other lane held during the probe (rc=$p2 probe=$probe_ok); a run that waited for its lane uses the driver recorded meanwhile (rc=$p3 drv=$drv_ok)"

# RRUN-36: the memory cap is the host's 80% divided by LANES; LANES must be one digit 1-9
reset_state
printf 'MemTotal:       16777216 kB\nMemAvailable:   16000000 kB\n' > "$TMP/meminfo"
RR_MEMINFO=$TMP/meminfo REMOTE_RUN_CONTAINER_MEMORY=20g REMOTE_RUN_LANES=2 drive --id mem1 feature "$SHA" unit-test
m_a=$RC; line2=$(calls | grep -E '(^| )run ' | head -n 1); reset_state
RR_MEMINFO=$TMP/meminfo REMOTE_RUN_CONTAINER_MEMORY=20g REMOTE_RUN_LANES=1 drive --id mem2 feature "$SHA" unit-test
m_b=$RC; line1=$(calls | grep -E '(^| )run ' | head -n 1); reset_state
bad=""
for v in 0 x 10; do
    REMOTE_RUN_LANES=$v bash "$DRIVER" --local-shell --log-dir "$TMP/logs" --id mem3 feature "$SHA" unit-test >"$TMP/lanes-$v.out" 2>&1; r=$?
    grep -q 'LANES' "$TMP/lanes-$v.out" && [[ $r -eq 2 ]] || bad+=" $v=$r"
done
row RRUN-36 "$([[ $m_a -eq 0 && $m_b -eq 0 && "$line2" == *"--memory 6553m --memory-swap 6553m"* && "$line2" == *"JNEXT_TEST_JOBS=12"* && "$line1" == *"--memory 13107m --memory-swap 13107m"* \
    && -z "$bad" ]] && echo 1 || echo 0)" \
    "per-lane memory: 16 GiB host gives 6553m for 2 lanes, 13107m for 1; LANES 0, x and 10 exit 2 naming LANES (a=$m_a b=$m_b bad:$bad)"

# RRUN-37: the local fallback marks its wrapper's environment REMOTE_RUN_FALLBACK=1, so a wrapper that itself
# sends heavy runs to the remote (the autonomous run's slot.sh, GH #547 stage 4) never sends one back
rm -f "$FAKE_STATE/make-calls"
cat > "$TMP/wrap2" <<'EOF'
#!/usr/bin/env bash
echo "fallback=${REMOTE_RUN_FALLBACK:-unset}" >> "$FAKE_STATE/make-calls"
exec "$@"
EOF
chmod +x "$TMP/wrap2"
env -u REMOTE_RUN_FALLBACK REMOTE_RUN_LOCAL_WRAP="$TMP/wrap2" timeout --kill-after=5s 60s bash "$DRIVER" --log-dir "$TMP/logs" --id u37 \
    --fallback-local "$TMP/local" feature "$SHA" unit-test 'sh:true' >/dev/null 2>&1
fb=$(grep fallback= "$FAKE_STATE/make-calls" | sort -u)
row RRUN-37 "$([[ "$fb" == "fallback=1" && $(grep -c fallback= "$FAKE_STATE/make-calls") -eq 2 ]] && echo 1 || echo 0)" \
    "both fallback forms (make target, sh:) put REMOTE_RUN_FALLBACK=1 in the wrapper's environment"

# RRUN-39: the run tree carries origin/main (the mirror holds only refs/rr/*, a local clone copies only
# refs/heads/*), so a tool in the run can take a merge base with main (test-affected --explain-miss, GH #547).
# The branch under test is one commit AHEAD of main, so main, the run's head and a wrongly set base differ.
MAIN_SHA=$(git -C "$REPO" rev-parse main)
AHEAD_SHA=$(git -C "$REPO" -c user.name=t -c user.email=t@t commit-tree -p "$MAIN_SHA" -m ahead "$(git -C "$REPO" rev-parse "$MAIN_SHA^{tree}")")
git -C "$REPO" branch ahead39 "$AHEAD_SHA"
reset_state; rm -f "$FAKE_STATE/mergebase"
FAKE_REFS=1 FAKE_RC=0 drive --id m39 ahead39 "$AHEAD_SHA" unit-test
mb39=$(cat "$FAKE_STATE/mergebase" 2>/dev/null)
row RRUN-39 "$([[ $RC -eq 0 && -n "$mb39" && "$mb39" == "$MAIN_SHA" && "$mb39" != "$AHEAD_SHA" ]] && echo 1 || echo 0)" \
    "the run tree's merge-base HEAD origin/main is main ($mb39), not the run's own head ($AHEAD_SHA) (rc $RC)"

# RRUN-40: the main fetch is best effort: against a remote with no main the run is green, not a fallback, not 75,
# and the run tree has no origin/main
NOMAIN=$TMP/origin-nomain.git
git clone -q --bare "$REPO" "$NOMAIN"; git -C "$NOMAIN" symbolic-ref HEAD refs/heads/feature; git -C "$NOMAIN" branch -q -D main
reset_state; rm -f "$FAKE_STATE/mergebase"
REMOTE_RUN_REPO_URL="file://$NOMAIN" FAKE_REFS=1 FAKE_RC=0 drive --id nm40 feature "$SHA" unit-test
row RRUN-40 "$([[ $RC -eq 0 && "$OUT" != *"fallback"* && -e "$FAKE_STATE/mergebase" && ! -s "$FAKE_STATE/mergebase" ]] && echo 1 || echo 0)" \
    "a remote with no main: the run is green (rc $RC), no fallback, and the run tree has no origin/main"

# RRUN-41 / RRUN-42: both fetches under the setup lock are time-bounded (RR_FETCH_TIMEOUT, default 120 s). A hanging
# main fetch is best effort (green run, no origin/main); a hanging branch fetch is the existing failure path (75).
mkdir -p "$TMP/hang"
REALGIT=$(command -v git)
cat > "$TMP/hang/git" <<EOF
#!/usr/bin/env bash
case "\$*" in *"refs/heads/\${HANG_REF:-none}:"*) exec sleep 300 ;; esac
exec $REALGIT "\$@"
EOF
chmod +x "$TMP/hang/git"
reset_state; rm -f "$FAKE_STATE/mergebase"
t0=$SECONDS
PATH="$TMP/hang:$PATH" HANG_REF=main RR_FETCH_TIMEOUT=2 FAKE_REFS=1 FAKE_RC=0 drive --id hm41 feature "$SHA" unit-test
took41=$((SECONDS - t0))
row RRUN-41 "$([[ $RC -eq 0 && $took41 -lt 60 && -e "$FAKE_STATE/mergebase" && ! -s "$FAKE_STATE/mergebase" ]] && echo 1 || echo 0)" \
    "a hanging main fetch is cut at RR_FETCH_TIMEOUT: green run (rc $RC) in ${took41}s with no origin/main"
reset_state
t0=$SECONDS
PATH="$TMP/hang:$PATH" HANG_REF=ahead39 RR_FETCH_TIMEOUT=2 FAKE_RC=0 drive --id hb42 ahead39 "$AHEAD_SHA" unit-test
took42=$((SECONDS - t0))
row RRUN-42 "$([[ $RC -eq 75 && $took42 -lt 60 && "$OUT" == *"timed out after 2s"* ]] && echo 1 || echo 0)" \
    "a hanging branch fetch is cut at RR_FETCH_TIMEOUT and takes the fallback path (rc $RC, ${took42}s)"

# RRUN-43: the --deps-ref fetch, also under the setup lock, is bounded too: a hang takes the existing 75 fallback
reset_state
t0=$SECONDS
PATH="$TMP/hang:$PATH" HANG_REF=feature RR_FETCH_TIMEOUT=2 FAKE_RC=0 drive --id hd43 ahead39 "$AHEAD_SHA" unit-test --deps-ref feature
took43=$((SECONDS - t0))
row RRUN-43 "$([[ $RC -eq 75 && $took43 -lt 60 && "$OUT" == *"cannot fetch --deps-ref feature"* ]] && echo 1 || echo 0)" \
    "a hanging --deps-ref fetch is cut at RR_FETCH_TIMEOUT and takes the fallback path (rc $RC, ${took43}s)"

# RRUN-44: --probe answers "a lane is free right now" in one round trip and starts nothing: 0 with a free lane
# (the second lane too), 75 with every lane held or no host, 2 with a branch or a mode beside it
reset_state
drive --probe; p_free=$RC; o_free=$OUT
hold_lock 20 "$WORK/lane.lock"; holder=$!
drive --probe; p_one=$RC; o_one=$OUT
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
hold_lane 20; holder=$!
reset_state
drive --probe; p_busy=$RC; o_busy=$OUT
started=0; [[ "$(calls)" == *"run "* || "$(calls)" == *"build "* ]] && started=1
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null
bash "$DRIVER" --probe >/dev/null 2>&1; p_nohost=$?
bash "$DRIVER" --probe feature "$SHA" unit-test >/dev/null 2>&1; p_use=$?
bash "$DRIVER" --probe --warm >/dev/null 2>&1; p_mode=$?
row RRUN-44 "$([[ $p_free -eq 0 && "$o_free" == *"lane 1 of 2 is free"* && $p_one -eq 0 && "$o_one" == *"lane 2 of 2 is free"* \
    && $p_busy -eq 75 && "$o_busy" == *"probe ${HOST:-local}: rc=75"* && $started -eq 0 && $p_nohost -eq 75 && $p_use -eq 2 && $p_mode -eq 2 ]] && echo 1 || echo 0)" \
    "--probe: free=$p_free, lane 1 held=$p_one (takes lane 2), every lane held=$p_busy (nothing started=$((1 - started))), no host=$p_nohost, with a target=$p_use, with --warm=$p_mode"

# RRUN-45: pick-host.sh prints the first host whose probe is free; 75 when none; --or-first prints the first
# host anyway (a gate waits there); one host in JNEXT_REMOTE_RUN_HOST is a one-host list; no host is 2
cat > "$TMP/pick-driver" <<'EOF'
#!/usr/bin/env bash
echo "$JNEXT_REMOTE_RUN_HOST" >> "$FAKE_STATE/picks"
[[ " ${PICK_BUSY:-} " != *" $JNEXT_REMOTE_RUN_HOST "* ]]
EOF
chmod +x "$TMP/pick-driver"
PICK=$SKILL/pick-host.sh
rm -f "$FAKE_STATE/picks"
export JNEXT_PICK_HOST_DRIVER=$TMP/pick-driver
k1=$(JNEXT_REMOTE_RUN_HOSTS="hA hB hC" bash "$PICK" 2>/dev/null); r1=$?
k2=$(PICK_BUSY="hA" JNEXT_REMOTE_RUN_HOSTS="hA hB hC" bash "$PICK" 2>/dev/null); r2=$?
k3=$(PICK_BUSY="hA hB hC" JNEXT_REMOTE_RUN_HOSTS="hA hB hC" bash "$PICK" 2>/dev/null); r3=$?
k4=$(PICK_BUSY="hA hB hC" JNEXT_REMOTE_RUN_HOSTS="hA hB hC" bash "$PICK" --or-first 2>/dev/null); r4=$?
k5=$(JNEXT_REMOTE_RUN_HOST="hOnly" bash "$PICK" 2>/dev/null); r5=$?
k6=$(bash "$PICK" 2>/dev/null); r6=$?
unset JNEXT_PICK_HOST_DRIVER
row RRUN-45 "$([[ "$k1" == hA && $r1 -eq 0 && "$k2" == hB && $r2 -eq 0 && -z "$k3" && $r3 -eq 75 && "$k4" == hA && $r4 -eq 0 \
    && "$k5" == hOnly && $r5 -eq 0 && -z "$k6" && $r6 -eq 2 ]] && echo 1 || echo 0)" \
    "pick-host.sh: first free host ($k1, $k2), none free 75 (rc=$r3), --or-first $k4, one-host list $k5, no host rc=$r6"

# RRUN-46: test-affected is a heavy target (the driver prints no "not a heavy target" notice for it); a target
# outside the list still gets the notice
reset_state
drive --id hv1 feature "$SHA" test-affected; hv_a=$OUT
reset_state
drive --id hv2 feature "$SHA" unit-test-build; hv_b=$OUT
row RRUN-46 "$([[ "$hv_a" != *"is not a heavy target"* && "$hv_b" == *"'unit-test-build' is not a heavy target"* ]] && echo 1 || echo 0)" \
    "test-affected raises no heavy-target notice, unit-test-build does"

# RRUN-38: the contract does not depend on the caller's environment: this whole test, run again with
# the driver-fallback variables, the remote's variables and a failing ssh and wrapper on PATH, is green
# and never reaches either (GH #547 stage 4: it timed out at RRUN-21 and RRUN-33 through the real wrapper)
if [[ -n "${SKILLTEST_INNER:-}" ]]; then pol=1; else polluted_check "$TMP/polluted"; pol=$POL; fi
row RRUN-38 "$([[ $pol -eq 1 ]] && echo 1 || echo 0)" \
    "a run under REMOTE_RUN_FALLBACK, REMOTE_RUN_LOCAL_WRAP, JNEXT_REMOTE_RUN_* in the environment is green and touches no real wrapper or ssh"

finish
