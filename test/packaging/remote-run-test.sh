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
# Rows RRUN-01 .. RRUN-30. A missing tool FAILS every row (no SKIP path).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SKILL=$ROOT/.claude/skills/remote-run
DRIVER=$SKILL/remote-run.sh
EXPECTED_ROWS=30

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

# ---- fakes ------------------------------------------------------------------
# Fake podman: records every call; any call without --root/--runroot other than --version FAILS (and
# leaves $FAKE_STATE/noroot), because a bare podman would write the default store outside the work dir.
# `image exists` is true after a `build`. `run` executes the real container-side script when
# FAKE_INNER=1 (the -v mounts mapped to RR_CTL / RR_SRC), else prints a canned result, writes a stamp
# when FAKE_STAMP=1, sleeps when FAKE_SLEEP=1 (until killed), kills its remote shell when
# FAKE_KILL_PARENT=1, and exits with FAKE_RC.
cat > "$TMP/bin/podman" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$FAKE_STATE/calls"
if [[ "${1:-}" == --version ]]; then echo "podman version 0.0-fake"; exit 0; fi
if [[ "${1:-}" != --root ]]; then : > "$FAKE_STATE/noroot"; exit 99; fi
echo "$2 $4" >> "$FAKE_STATE/roots"; shift 4
if [[ "${1:-}" == --storage-driver ]]; then echo "$2" >> "$FAKE_STATE/drivers"; shift 2; fi
case "${1:-}" in
    info)
        case "$*" in
            *GraphDriverName*) [[ "${FAKE_INFO_FAIL:-0}" != 1 ]] || { echo "boom: store broken" >&2; exit 1; }; echo overlay ;;
            *) echo "${FAKE_ROOTLESS:-true}" ;;
        esac ;;
    image)
        case "${2:-}" in
            exists) [[ -e "$FAKE_STATE/image-${3//\//_}" ]] ;;
            *) exit 0 ;;
        esac ;;
    build) while [[ $# -gt 0 ]]; do [[ "$1" == -t ]] && : > "$FAKE_STATE/image-${2//\//_}"; shift; done ;;
    kill) echo "$2" >> "$FAKE_STATE/killed" ;;
    run)
        ctl=""; src=""
        for a in "$@"; do
            [[ "$a" == *:/rr-stamps ]] && [[ "${FAKE_STAMP:-0}" == 1 ]] && echo stamp > "${a%%:*}/fakekey"
            [[ "$a" == *:/src ]] && { echo "${a%%:*}" > "$FAKE_STATE/srcdir"; src=${a%%:*}; }
            [[ "$a" == *:/rr:ro ]] && { cp "${a%%:*}/env.sh" "$FAKE_STATE/env.sh"; ctl=${a%%:*}; }
        done
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
# hold_lane <seconds>: a separate process takes the remote lane lock for that long (waits until it has it).
hold_lane() {
    mkdir -p "$WORK"
    flock "$WORK/lane.lock" sleep "$1" &
    until ! flock -n "$WORK/lane.lock" true; do sleep 0.1; done
}
calls() { cat "$FAKE_STATE/calls" 2>/dev/null; }
reset_state() { rm -f "$FAKE_STATE"/calls "$FAKE_STATE"/killed "$FAKE_STATE"/roots "$FAKE_STATE"/running; }

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

finish
