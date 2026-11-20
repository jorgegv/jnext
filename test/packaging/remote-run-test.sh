#!/usr/bin/env bash
#
# Contract test for .claude/skills/remote-run (the remote-run driver) and test/ci-deps.sh.
# Hermetic: the remote side runs as a local shell (--local-shell) against a throwaway git
# repository, with a fake `podman` and a fake `make` on PATH; no ssh, no container, no network,
# no SD-card image. HOME and every work directory are under one scratch directory, removed on exit.
#
# Oracles: the driver's documented exit codes (0 green, 1 failed, 2 usage, 3 refused, 75 run
# locally), the owner's rule that all remote work data lives under one directory, and
# .github/workflows/ci.yml as the consumer of test/ci-deps.sh. No VHDL: this is tooling.
#
# Rows RRUN-01 .. RRUN-14. A missing tool FAILS every row (no SKIP path).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SKILL=$ROOT/.claude/skills/remote-run
DRIVER=$SKILL/remote-run.sh
EXPECTED_ROWS=14

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
for t in git flock timeout tar sha256sum shellcheck; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing tools: ${missing[*]} (dnf install git util-linux coreutils tar ShellCheck)"
    for i in $(seq 1 $EXPECTED_ROWS); do row "RRUN-$(printf '%02d' "$i")" 0 "not run: missing tools"; done
    total=$EXPECTED_ROWS; failed=$EXPECTED_ROWS; passed=0
    ids_seen=" "; finish
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/remote-run-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
export HOME=$TMP/home
mkdir -p "$HOME" "$TMP/bin" "$TMP/state"

# ---- fakes ------------------------------------------------------------------
# Fake podman: records every call; `image exists` is true after a `build`; `run` writes a stamp
# file when FAKE_STAMP=1, sleeps when FAKE_SLEEP=1 (until killed), else exits with FAKE_RC.
cat > "$TMP/bin/podman" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$FAKE_STATE/calls"
if [[ "${1:-}" == --root ]]; then echo "$2 $4" >> "$FAKE_STATE/roots"; shift 4; fi
case "${1:-}" in
    --version) echo "podman version 0.0-fake" ;;
    info) echo true ;;
    image)
        case "${2:-}" in
            exists) [[ -e "$FAKE_STATE/image-${3//\//_}" ]] ;;
            *) exit 0 ;;
        esac ;;
    build) while [[ $# -gt 0 ]]; do [[ "$1" == -t ]] && : > "$FAKE_STATE/image-${2//\//_}"; shift; done ;;
    kill) echo "$2" >> "$FAKE_STATE/killed" ;;
    run)
        for a in "$@"; do
            [[ "$a" == *:/rr-stamps ]] && [[ "${FAKE_STAMP:-0}" == 1 ]] && echo stamp > "${a%%:*}/fakekey"
            [[ "$a" == *:/src ]] && echo "${a%%:*}" > "$FAKE_STATE/srcdir"
            [[ "$a" == *:/rr:ro ]] && cp "${a%%:*}/env.sh" "$FAKE_STATE/env.sh"
        done
        echo "=== remote-run target: fake"
        echo "=== remote-run result: fake rc=${FAKE_RC:-0} time=1s peak=1MiB"
        if [[ "${FAKE_SLEEP:-0}" == 1 ]]; then
            trap 'exit 143' TERM
            : > "$FAKE_STATE/running"; sleep 60 >/dev/null 2>&1 & wait
        fi
        exit "${FAKE_RC:-0}" ;;
    *) exit 0 ;;
esac
EOF
# Fake make: logs its arguments; fails for the target `bad`.
cat > "$TMP/bin/make" <<'EOF'
#!/usr/bin/env bash
echo "make $*" >> "$FAKE_STATE/make-calls"
[[ " $* " != *" bad "* ]]
EOF
chmod +x "$TMP/bin/podman" "$TMP/bin/make"
export FAKE_STATE=$TMP/state
export PATH=$TMP/bin:$PATH

# ---- a throwaway origin -----------------------------------------------------
REPO=$TMP/origin
git init -q -b main "$REPO"
mkdir -p "$REPO/test"
cp "$ROOT/test/ci-deps.sh" "$REPO/test/ci-deps.sh"
git -C "$REPO" add -A
git -C "$REPO" -c user.name=t -c user.email=t@t commit -qm one
git -C "$REPO" branch feature
LEGACY=$(git -C "$REPO" -c user.name=t -c user.email=t@t commit-tree -m legacy "$(git -C "$REPO" hash-object -t tree /dev/null -w)")
git -C "$REPO" branch legacy "$LEGACY"
SHA=$(git -C "$REPO" rev-parse HEAD)
OTHER=$(printf '%040d' 1)

WORK=$TMP/work
export REMOTE_RUN_REPO_URL="file://$REPO"
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
row RRUN-05 "$([[ $RC -eq 3 && "$OUT" == *"not at the expected $OTHER"* ]] && ! calls | grep -qE '^(run|build)|(^| )(run|build) ' && echo 1 || echo 0)" \
    "a sha other than the pushed tip is refused with exit 3, no build, no run (rc=$RC)"

# RRUN-06: a branch that is not on the repository is refused (exit 3)
reset_state
drive --id s2 nosuchbranch "$SHA" unit-test
row RRUN-06 "$([[ $RC -eq 3 && "$OUT" == *"is not on the remote repository"* ]] && echo 1 || echo 0)" \
    "an unpushed branch is refused with exit 3 (rc=$RC)"

# RRUN-07: no image for the current deps: exit 75 telling to run --warm, nothing built
reset_state
drive --id i1 feature "$SHA" unit-test
row RRUN-07 "$([[ $RC -eq 75 && "$OUT" == *"remote-run --warm"* ]] && ! calls | grep -qE '(^| )build ' && echo 1 || echo 0)" \
    "a normal run without the image exits 75 naming --warm, builds nothing (rc=$RC)"

# RRUN-08: --warm builds the image under the work dir; the next run reaches the container with
# volumes and podman storage under the work dir, copies the stamp back and removes its checkout
reset_state
drive --warm --id w1
warm_rc=$RC
built=$(calls | grep -c ' build ' || true)
reset_state
FAKE_STAMP=1 FAKE_RC=0 drive --id r1 feature "$SHA" unit-test sh:true
run_rc=$RC
runline=$(calls | grep -E '(^| )run ' | head -n 1)
roots_ok=1
while read -r s r; do [[ "$s" == "$WORK/"* && "$r" == "$WORK/"* ]] || roots_ok=0; done < "$FAKE_STATE/roots"
vols_ok=1
while read -r v; do [[ "$v" == "$WORK/"* ]] || vols_ok=0; done < <(grep -oE -- '-v [^ ]+' <<<"$runline" | cut -d' ' -f2)
[[ "$runline" == *"/root/.cache/ccache"* && "$runline" == *"/root/.jnext/sdcard"* ]] || vols_ok=0
row RRUN-08 "$([[ $warm_rc -eq 0 && $built -eq 1 && $run_rc -eq 0 && $roots_ok -eq 1 && $vols_ok -eq 1 \
    && -f "$TMP/stamps/fakekey" && -z "$(find "$WORK/runs" "$WORK/ctl" "$WORK/stamps" -mindepth 1)" ]] && echo 1 || echo 0)" \
    "warm builds once; a run mounts only dirs under the work dir, stores podman there, returns the stamp, leaves no checkout (warm=$warm_rc run=$run_rc roots=$roots_ok vols=$vols_ok)"

# RRUN-09: a failing container exits the driver with 1, copies no stamp, and the log keeps the result
reset_state; rm -rf "$TMP/stamps"
FAKE_STAMP=1 FAKE_RC=1 drive --id f1 feature "$SHA" unit-test
row RRUN-09 "$([[ $RC -eq 1 && ! -e "$TMP/stamps" && "$OUT" == *"=== remote-run result: fake rc=1"* ]] && echo 1 || echo 0)" \
    "a failing run exits 1, writes no stamp, and the summary shows the result line (rc=$RC)"

# RRUN-10: TERM to the driver stops the container and removes the checkout (the orphan guard)
reset_state
FAKE_SLEEP=1 timeout --kill-after=5s 300s bash "$DRIVER" --local-shell --log-dir "$TMP/logs" --id k1 \
    feature "$SHA" unit-test >/dev/null 2>&1 &
dpid=$!
for _ in $(seq 1 600); do [[ -e "$FAKE_STATE/running" ]] && break; sleep 0.2; done
kill -TERM "$dpid" 2>/dev/null
wait "$dpid" 2>/dev/null
for _ in $(seq 1 150); do [[ -e "$FAKE_STATE/killed" && -z "$(find "$WORK/runs" -mindepth 1)" ]] && break; sleep 0.2; done
row RRUN-10 "$([[ -e "$FAKE_STATE/running" ]] && grep -q '^rr-jnext-k1$' "$FAKE_STATE/killed" 2>/dev/null \
    && [[ -z "$(ls -A "$WORK/runs")" ]] && echo 1 || echo 0)" \
    "TERM to the driver kills the named container and removes the run checkout"

# RRUN-11: nothing was written outside the scratch tree's work/log/stamp dirs: HOME is empty
row RRUN-11 "$([[ -z "$(find "$HOME" -mindepth 1 2>/dev/null)" ]] && echo 1 || echo 0)" \
    "no work data outside the work directory: the scratch HOME stays empty"

# RRUN-12: ci-deps.sh: sets are unions, unknown set refused, ci.yml holds no package list, all
# scripts of the skill are shellcheck-clean
ci_ok=1
l1=$(bash "$ROOT/test/ci-deps.sh" --list test)
l2=$(bash "$ROOT/test/ci-deps.sh" --list test mingw)
[[ "$l1" == "$(sort -u <<<"$l1")" && $(wc -l <<<"$l2") -gt $(wc -l <<<"$l1") ]] || ci_ok=0
grep -qx 'qt6-qtbase-devel' <<<"$l1" || ci_ok=0
bash "$ROOT/test/ci-deps.sh" --list nonesuch >/dev/null 2>&1; [[ $? -eq 2 ]] || ci_ok=0
bash "$ROOT/test/ci-deps.sh" >/dev/null 2>&1; [[ $? -eq 2 ]] || ci_ok=0
grep -nE 'dnf install -y +[a-z0-9]' "$ROOT/.github/workflows/ci.yml" | grep -qvE 'dnf install -y git$' && ci_ok=0
grep -q 'bash test/ci-deps.sh test$' "$ROOT/.github/workflows/ci.yml" || ci_ok=0
shellcheck -x "$DRIVER" "$SKILL/remote-side.sh" "$SKILL/container-side.sh" "$ROOT/test/ci-deps.sh" >/dev/null 2>&1 || ci_ok=0
row RRUN-12 "$ci_ok" "ci-deps.sh sets/refusals, ci.yml calls it with no inline package list, the scripts are shellcheck-clean"

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

finish
