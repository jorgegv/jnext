#!/usr/bin/env bash
#
# Contract test for .claude/skills/autonomous-run/slot.sh (GH #547 stage 4): local admission (slots,
# NSLOTS, load limit, PAUSED) and the overflow of heavy runs to the remote-run driver.
# Hermetic: a scratch slot directory, a load file in place of /proc/loadavg, a stub driver, a `make`
# stub, a real throwaway git repository with a local bare "origin" and a PATH shim that records every
# git call; no ssh, no network. Everything lives under one scratch directory, removed on exit.
#
# Oracle: the owner's rules of 2026-11-21/22 as written in slot.sh's header (4 local+remote slots, host
# picked by load, never wait on it) and of 2026-11-23 (remote first: every eligible heavy run goes to a free
# remote lane, long or short, and runs here only when every lane is busy) and COMMON-RULES "REMOTE RUNS"
# (an agent pushes only its own issue branch, never force). No VHDL: this is tooling.
#
# Rows SLOT-01 .. SLOT-70. A missing tool FAILS every row (no SKIP path).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=/dev/null
source "$ROOT/test/packaging/skill-env-scrub.sh"
SLOT=$ROOT/.claude/skills/autonomous-run/slot.sh
EXPECTED_ROWS=70

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
for t in git flock timeout awk nice; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
[[ -x "$SLOT" ]] || missing+=("slot.sh")
if [[ ${#missing[@]} -gt 0 ]]; then
    echo "  missing: ${missing[*]}"
    for i in $(seq 1 $EXPECTED_ROWS); do row "SLOT-$(printf '%02d' "$i")" 0 "not run: missing ${missing[*]}"; done
    total=$EXPECTED_ROWS; failed=$EXPECTED_ROWS; passed=0
    ids_seen=" "; finish
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/slot-test.XXXXXX")
HOLDERS=()
cleanup() {
    local p
    for p in "${HOLDERS[@]:-}"; do [[ -z "$p" ]] || kill "$p" 2>/dev/null; done
    # helpers a broken TERM forwarding would leave running (they also end by themselves after 30 s)
    [[ ! -f "$TMP/state/helper-pids" ]] || while read -r p; do grep -aq -- "$TMP" "/proc/$p/cmdline" 2>/dev/null && kill "$p" 2>/dev/null; done < "$TMP/state/helper-pids"
    rm -rf "$TMP"
}
trap cleanup EXIT
export HOME=$TMP/home
mkdir -p "$HOME" "$TMP/bin" "$TMP/state"
REALGIT=$(command -v git)
# SLOT-32's run: the whole test again, started now so it overlaps the rest, under a polluted environment
[[ -n "${SKILLTEST_INNER:-}" ]] || polluted_selfrun "${BASH_SOURCE[0]}" "$TMP/polluted"

# ---- fakes ------------------------------------------------------------------
# git shim: records its arguments, then runs the real git.
cat > "$TMP/bin/git" <<EOF
#!/usr/bin/env bash
echo "\$*" >> "$TMP/state/git-calls"
# STUB_PUSH_RACE=1: the push lands, then reports failure (the loser of a concurrent push of the same sha)
# STUB_GIT_HANG=1: push and ls-remote hang (exec: the sleep is the killed process, no orphan holds the pipes)
if [[ -n "\${STUB_GIT_HANG:-}" && ( " \$* " == *" push "* || " \$* " == *" ls-remote "* ) ]]; then exec sleep 30; fi
if [[ -n "\${STUB_PUSH_RACE:-}" && " \$* " == *" push "* ]]; then "$REALGIT" "\$@"; exit 1; fi
exec "$REALGIT" "\$@"
EOF
# make stub: records the call and exits MAKE_RC (default 0).
cat > "$TMP/bin/make" <<'EOF'
#!/usr/bin/env bash
echo "make $*" >> "$STATE/make-calls"
echo "MF=[${MAKEFLAGS-}] U=${JNEXT_UNIT_TEST_JOBS-unset} T=${JNEXT_TEST_JOBS-unset}" >> "$STATE/make-env"
exit "${MAKE_RC:-0}"
EOF
# driver stub: records its arguments and the two environment variables slot.sh must set, exits STUB_RC.
cat > "$TMP/rr-stub" <<'EOF'
#!/usr/bin/env bash
# --probe: record the host asked, say busy (75) for a host listed in STUB_BUSY_HOSTS, else free (0). Not an rr-calls entry.
if [[ "${1:-}" == --probe ]]; then
    echo "${JNEXT_REMOTE_RUN_HOST:-unset}" >> "$STATE/probe-calls"
    # STUB_BUSY_FIRST=N: busy for the first N probes of the whole run, free after (a lane that frees up later)
    [[ -z "${STUB_BUSY_FIRST:-}" || $(wc -l < "$STATE/probe-calls") -gt "$STUB_BUSY_FIRST" ]] || exit 75
    [[ " ${STUB_BUSY_HOSTS:-} " != *" ${JNEXT_REMOTE_RUN_HOST:-} "* ]] || exit 75
    exit 0
fi
{ echo "ARGS $*"; echo "HOST=${JNEXT_REMOTE_RUN_HOST:-unset}"; echo "NO_OVERFLOW=${JNEXT_SLOT_NO_OVERFLOW:-unset}"; echo "WRAP=${REMOTE_RUN_LOCAL_WRAP:-unset}"; echo "BASE_REPO=${REMOTE_RUN_BASE_REPO:-unset}"; } >> "$STATE/rr-calls"
echo "MF=[${MAKEFLAGS-}] U=${JNEXT_UNIT_TEST_JOBS-unset} T=${JNEXT_TEST_JOBS-unset}" >> "$STATE/rr-env"
echo "stub driver ran"
[[ -z "${STUB_PAUSE:-}" ]] || : > "$STUB_PAUSE"
[[ " ${STUB_RC75_HOSTS:-} " != *" ${JNEXT_REMOTE_RUN_HOST:-} "* ]] || exit 75
if [[ "${STUB_WAIT:-0}" == 1 ]]; then
    trap 'echo term > "$STATE/rr-term"; sleep 1; exit 5' TERM
    echo $$ >> "$STATE/helper-pids"
    for _ in $(seq 1 150); do sleep 0.2; done
fi
exit "${STUB_RC:-0}"
EOF
# nice shim: records its arguments, then runs the real nice (so the test does not depend on its own niceness).
cat > "$TMP/bin/nice" <<EOF
#!/usr/bin/env bash
echo "\$*" >> "$TMP/state/nice-calls"
exec "$(command -v nice)" "\$@"
EOF
chmod +x "$TMP/bin/git" "$TMP/bin/make" "$TMP/bin/nice" "$TMP/rr-stub"
export STATE=$TMP/state PATH="$TMP/bin:$PATH"
export JNEXT_SLOT_REMOTE_RUN=$TMP/rr-stub JNEXT_SLOT_LOADAVG=$TMP/loadavg JNEXT_SLOT_POLL=0.2
export JNEXT_SLOT_LOAD_LIMIT=12 JNEXT_REMOTE_RUN_HOST=fakehost JNEXT_SLOT_REMOTE_LOG_DIR=$TMP/rrlogs
unset JNEXT_SLOT_NO_OVERFLOW REMOTE_RUN_FALLBACK JNEXT_SLOT_DIR

# A real repository with a bare local origin; worktree WT is on an issue branch, clean, and pushed.
git init -q --bare "$TMP/origin.git"
git init -q -b main "$TMP/wt"
git -C "$TMP/wt" -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
git -C "$TMP/wt" remote add origin "$TMP/origin.git"
git -C "$TMP/wt" checkout -q -b gh900-demo
git -C "$TMP/wt" push -q origin gh900-demo main
WT=$TMP/wt
SHA=$(git -C "$WT" rev-parse HEAD)
CLEANSLOTS=$TMP/slots

set_load() { echo "$1 1.00 1.00 1/100 1" > "$TMP/loadavg"; }
fresh() {   # fresh slot dir, low load, empty records
    rm -rf "$CLEANSLOTS" "$STATE"/rr-calls "$STATE"/make-calls "$STATE"/git-calls "$STATE"/probe-calls "$STATE"/make-env "$STATE"/rr-env "$TMP/rrlogs"
    mkdir -p "$CLEANSLOTS"; set_load 1.00
}
slot() { timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" "$@"; }
rr_calls() { cat "$STATE/rr-calls" 2>/dev/null; }
hold() {    # hold slot <n> with a background flock; waits until the lock is really taken
    local n=$1 k
    ( exec {k}>"$CLEANSLOTS/slot$n"; flock "$k"; exec sleep 120 ) &   # exec: the holder IS the sleep, so a kill frees the lock
    HOLDERS+=($!)
    for k in $(seq 1 50); do flock -n "$CLEANSLOTS/slot$n" true 2>/dev/null || return 0; sleep 0.1; done
    return 1
}
release_all() { local p; for p in "${HOLDERS[@]:-}"; do [[ -z "$p" ]] || { kill "$p" 2>/dev/null; wait "$p" 2>/dev/null; }; done; HOLDERS=(); }
adm() { cat "$CLEANSLOTS/admissions.log" 2>/dev/null; }

# SLOT-01: a free slot runs the command locally at once and logs the admission
fresh
out=$(slot true 2>&1); rc=$?
row SLOT-01 "$([[ $rc -eq 0 && "$out" == *"admitted to slot 1"* && "$(adm)" =~ \ slot\ 1\ waited=[0-9]+s\ load=1.00\ true$ ]] && echo 1 || echo 0)" \
    "a free slot runs the command here and admissions.log names the slot (rc=$rc)"

# SLOT-02: a LONG heavy command on a clean, pushed issue branch goes to the remote FIRST, even with slots free
fresh
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
rc_calls=$(rr_calls)
row SLOT-02 "$([[ $rc -eq 0 && "$rc_calls" == *"ARGS --stream --fallback-local $WT --keep-going --id gh900-demo-"* \
    && "$rc_calls" == *"--log-dir $TMP/rrlogs gh900-demo $SHA unit-test"* && "$rc_calls" == *"NO_OVERFLOW=1"* \
    && "$rc_calls" == *"WRAP=$SLOT --dir $CLEANSLOTS"* && "$(adm)" == *" remote "* && ! -e "$STATE/make-calls" ]] && ! grep -qE '(^| )push( |$)' "$STATE/git-calls" && echo 1 || echo 0)" \
    "long heavy run: the driver gets --fallback-local, branch, sha, target, NO_OVERFLOW=1 and this script as the local wrapper; nothing ran here, no push when origin is at HEAD (rc=$rc)"

# SLOT-03: a short heavy command (regression-rows) also goes to a free lane FIRST, slots free or held, with its make
# variable (owner, 2026-11-23); with every host busy it runs here and the driver is not called
fresh
out=$(slot make -C "$WT" regression-rows ROWS=a 2>&1); rc=$?
r3a=$(rr_calls); m3a=$(cat "$STATE/make-calls" 2>/dev/null)
fresh
out=$(STUB_BUSY_HOSTS=fakehost slot make -C "$WT" regression-rows ROWS=a 2>&1); rc2=$?
row SLOT-03 "$([[ $rc -eq 0 && "$r3a" == *"gh900-demo $SHA regression-rows ROWS=a"* && -z "$m3a" && $rc2 -eq 0 && -z "$(rr_calls)" \
    && "$(cat "$STATE/make-calls")" == "make -C $WT regression-rows ROWS=a" ]] && echo 1 || echo 0)" \
    "short heavy run: a free lane takes it with a slot free, with its make variable; every host busy -> here, driver not called"

# SLOT-04: a dirty tree (an untracked file) is never sent: runs here and says why
fresh
: > "$WT/scratch.txt"
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
rm -f "$WT/scratch.txt"
row SLOT-04 "$([[ $rc -eq 0 && -z "$(rr_calls)" && "$out" == *"not sent to the remote: the worktree has uncommitted or untracked files"* \
    && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "an untracked file keeps a long heavy run local, with the reason printed (rc=$rc)"

# SLOT-05: main, an epic branch and a detached HEAD are never sent
fresh
git -C "$WT" checkout -q main
slot env LANG=C make -C "$WT" unit-test >"$TMP/o5a" 2>&1
git -C "$WT" checkout -q -b epic9-x
slot env LANG=C make -C "$WT" unit-test >"$TMP/o5b" 2>&1
git -C "$WT" checkout -q --detach
slot env LANG=C make -C "$WT" unit-test >"$TMP/o5c" 2>&1
git -C "$WT" checkout -q gh900-demo; git -C "$WT" branch -q -D epic9-x
row SLOT-05 "$([[ -z "$(rr_calls)" && "$(grep -c '^make ' "$STATE/make-calls")" -eq 3 && "$(cat "$TMP/o5a")" == *"branch 'main' is not an issue branch"* \
    && "$(cat "$TMP/o5b")" == *"branch 'epic9-x' is not an issue branch"* && "$(cat "$TMP/o5c")" == *"detached HEAD"* ]] && echo 1 || echo 0)" \
    "main, epic9-x and a detached HEAD all run here and name the reason"

# SLOT-06: a branch that is not on origin at HEAD is pushed first, exactly 'push origin <branch>', never forced
fresh
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m second
NEWSHA=$(git -C "$WT" rev-parse HEAD)
: > "$STATE/git-calls"
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
pushes=$(grep -E '(^| )push( |$)' "$STATE/git-calls")
row SLOT-06 "$([[ $rc -eq 0 && "$pushes" == "-C $WT push origin gh900-demo" && "$pushes" != *force* && "$pushes" != *" -f"* \
    && "$(git -C "$TMP/origin.git" rev-parse gh900-demo)" == "$NEWSHA" && "$(rr_calls)" == *"gh900-demo $NEWSHA unit-test"* ]] && echo 1 || echo 0)" \
    "an unpushed tip is pushed with exactly 'push origin gh900-demo' (no force) and the driver gets the new sha"

# SLOT-07: PAUSED refuses with 75 and runs nothing
fresh
: > "$CLEANSLOTS/PAUSED"
slot make -C "$WT" unit-test-build >/dev/null 2>"$TMP/o7"; rc=$?
row SLOT-07 "$([[ $rc -eq 75 && ! -e "$STATE/make-calls" && -z "$(rr_calls)" && "$(cat "$TMP/o7")" == *REFUSED* ]] && echo 1 || echo 0)" \
    "slots/PAUSED exits 75 and starts nothing (rc=$rc)"

# SLOT-08: NSLOTS=4 admits four commands at once (the default 2 admits two)
fresh
echo 4 > "$CLEANSLOTS/NSLOTS"
for i in 1 2 3 4; do ( slot sleep 6 >/dev/null 2>&1 ) & done
n4=0; for k in $(seq 1 40); do n4=$(grep -c ' slot ' "$CLEANSLOTS/admissions.log" 2>/dev/null || true); [[ "$n4" -ge 4 ]] && break; sleep 0.1; done
wait
fresh
for i in 1 2 3; do ( slot sleep 3 >/dev/null 2>&1 ) & done
sleep 1.5; n2=$(grep -c ' slot ' "$CLEANSLOTS/admissions.log" 2>/dev/null || true)
wait
row SLOT-08 "$([[ "$n4" -eq 4 && "$n2" -eq 2 ]] && echo 1 || echo 0)" \
    "NSLOTS=4 admits four at once ($n4); without the file two ($n2 of 3 started)"

# SLOT-09: NSLOTS that is not 1..8 is refused with 2
fresh
bad=""
for v in x 0 9 "" 2.5; do
    echo "$v" > "$CLEANSLOTS/NSLOTS"
    slot true >/dev/null 2>&1; r=$?
    [[ $r -eq 2 ]] || bad+=" '$v'=$r"
done
row SLOT-09 "$([[ -z "$bad" && ! -e "$CLEANSLOTS/admissions.log" ]] && echo 1 || echo 0)" \
    "NSLOTS x, 0, 9, empty and 2.5 exit 2 and admit nothing (bad:$bad)"

# SLOT-10: load at the limit waits until it drops (the command is not started while it is high)
fresh
set_load 12.00
( slot sh -c ": > $TMP/ran10" >/dev/null 2>&1 ) & w10=$!
sleep 1.2; before=0; [[ -e "$TMP/ran10" ]] && before=1
set_load 3.00
for k in $(seq 1 50); do [[ -e "$TMP/ran10" ]] && break; sleep 0.1; done
after=0; [[ -e "$TMP/ran10" ]] && after=1
wait "$w10"
row SLOT-10 "$([[ $before -eq 0 && $after -eq 1 ]] && echo 1 || echo 0)" \
    "load 12.00 at limit 12: not started (started=$before); load 3.00: started (started=$after)"

# SLOT-11: a command that is not a heavy make never overflows, even with every slot held; it waits and then runs
fresh
hold 1; hold 2
( slot env LANG=C make -C "$WT" unit-test-build >"$TMP/o11" 2>&1; echo $? > "$TMP/rc11" ) & w11=$!
sleep 1.2
early=0; [[ -e "$STATE/make-calls" ]] && early=1
release_all
wait "$w11"
row SLOT-11 "$([[ $early -eq 0 && -z "$(rr_calls)" && "$(cat "$TMP/rc11")" == 0 && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test-build" \
    && "$(cat "$TMP/o11")" == *"not sent to the remote: 'unit-test-build' is not a heavy target"* ]] && echo 1 || echo 0)" \
    "unit-test-build with both slots held waits (ran early=$early), never calls the driver, then runs here"

# SLOT-12: the driver's exit status is slot.sh's (1 and 124)
fresh
STUB_RC=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; r1=$?
STUB_RC=124 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; r124=$?
row SLOT-12 "$([[ $r1 -eq 1 && $r124 -eq 124 && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "the remote verdict is the exit status: 1 stays 1, 124 stays 124, nothing re-run here ($r1, $r124)"

# SLOT-13: exit 3 from the driver (the push did not land) ran nothing, so the command runs here
fresh
out=$(STUB_RC=3 slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-13 "$([[ $rc -eq 0 && "$out" == *"the remote refused"* && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "driver exit 3 falls back to a local run, rc=$rc"

# SLOT-14: no overflow inside a fallback: JNEXT_SLOT_NO_OVERFLOW and REMOTE_RUN_FALLBACK each keep a long run local
fresh
JNEXT_SLOT_NO_OVERFLOW=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; a=$?
REMOTE_RUN_FALLBACK=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; b=$?
row SLOT-14 "$([[ $a -eq 0 && $b -eq 0 && -z "$(rr_calls)" && "$(grep -c '^make ' "$STATE/make-calls")" -eq 2 ]] && echo 1 || echo 0)" \
    "NO_OVERFLOW and REMOTE_RUN_FALLBACK both keep an eligible run here (no driver call, no loop)"

# SLOT-15: JNEXT_REMOTE_RUN_HOST unset keeps everything local
fresh
out=$(env -u JNEXT_REMOTE_RUN_HOST "$SLOT" --dir "$CLEANSLOTS" env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-15 "$([[ $rc -eq 0 && -z "$(rr_calls)" && "$out" == *"JNEXT_REMOTE_RUN_HOST is not set"* ]] && echo 1 || echo 0)" \
    "no alias: the heavy run stays here and says so (rc=$rc)"

# SLOT-16: a make variable with spaces becomes one sh: target, quoted; other env words and flags are not sent
fresh; hold 1; hold 2
slot make -C "$WT" regression-rows 'ROWS=a b' >/dev/null 2>&1
r16=$(rr_calls)
release_all; fresh
slot env FOO=1 make -C "$WT" unit-test >"$TMP/o16" 2>&1
slot make -C "$WT" -j3 unit-test >"$TMP/o16b" 2>&1
c1=0; [[ "$r16" == *"gh900-demo $NEWSHA sh:make regression-rows ROWS=a\\ b"* ]] && c1=1
row SLOT-16 "$([[ $c1 -eq 1 && ! -e "$STATE/rr-calls" \
    && "$(cat "$TMP/o16")" == *"environment word 'FOO=1'"* && "$(cat "$TMP/o16b")" == *"'-j3' is not a heavy target"* ]] && echo 1 || echo 0)" \
    "spaced make variable -> one quoted sh: target; FOO=1 and -j3 keep the run local with the reason"

# SLOT-17: a command's own exit status passes through a local run
fresh
slot sh -c 'exit 7' >/dev/null 2>&1; rc=$?
row SLOT-17 "$([[ $rc -eq 7 ]] && echo 1 || echo 0)" "a local command's exit status is slot.sh's (rc=$rc)"

# SLOT-18: the lock is held for the whole command: with NSLOTS=1 a second command waits for the first
fresh
echo 1 > "$CLEANSLOTS/NSLOTS"
( slot sh -c "sleep 2; : > $TMP/first18" >/dev/null 2>&1 ) &
sleep 0.5
( slot sh -c "[ -e $TMP/first18 ] && : > $TMP/second-after18" >/dev/null 2>&1 ) &
wait
row SLOT-18 "$([[ -e "$TMP/second-after18" ]] && echo 1 || echo 0)" \
    "the slot stays locked until its command ends: the second command started only after the first finished"

# SLOT-19: PAUSED stops an eligible long run BEFORE it is sent: exit 75, the driver is not called
fresh
: > "$CLEANSLOTS/PAUSED"
slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-19 "$([[ $rc -eq 75 && ! -e "$STATE/rr-calls" && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "PAUSED with an eligible long run exits 75 and never calls the driver (rc=$rc)"

# SLOT-20: PAUSED created while a command waits for a held slot: the retry refuses with 75
fresh
hold 1; hold 2
( slot sh -c ": > $TMP/ran20" >/dev/null 2>&1; echo $? > "$TMP/rc20" ) & w20=$!
sleep 1; : > "$CLEANSLOTS/PAUSED"
release_all
wait "$w20"
row SLOT-20 "$([[ "$(cat "$TMP/rc20")" == 75 && ! -e "$TMP/ran20" ]] && echo 1 || echo 0)" \
    "PAUSED appearing during the wait exits 75 and the command never starts"

# SLOT-21: PAUSED set while the (fell-back) driver runs: its folded status 1 becomes 75
fresh
STUB_PAUSE=$CLEANSLOTS/PAUSED STUB_RC=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-21 "$([[ $rc -eq 75 ]] && echo 1 || echo 0)" "a driver that exits 1 with PAUSED set makes slot.sh exit 75 (rc=$rc)"

# SLOT-22: every heavy target (the 8 long and the 2 short ones) goes remote with a free lane and a free local slot, and
# runs here with every host busy
fresh
bad=""
for t in unit-test unit-test-sdl unit-test-win unit-test-wasm wasm-browser-test regression regression-win regression-wasm regression-rows test-affected; do
    rm -f "$STATE/rr-calls" "$STATE/make-calls"
    slot env LANG=C make -C "$WT" "$t" >/dev/null 2>&1
    [[ "$(rr_calls)" == *"gh900-demo $NEWSHA $t"* && ! -e "$STATE/make-calls" ]] || bad+=" free-lane:$t"
    rm -f "$STATE/rr-calls" "$STATE/make-calls"
    STUB_BUSY_HOSTS=fakehost slot env LANG=C make -C "$WT" "$t" >/dev/null 2>&1
    [[ -z "$(rr_calls)" && "$(cat "$STATE/make-calls" 2>/dev/null)" == "make -C $WT $t" ]] || bad+=" busy:$t"
done
row SLOT-22 "$([[ -z "$bad" ]] && echo 1 || echo 0)" \
    "all 10 heavy targets (long and short) go remote with a free lane and a free slot, and run here with every host busy (bad:$bad)"

# SLOT-23: a gate-* branch, a modified tracked file and a staged change are never sent
fresh
git -C "$WT" checkout -q -b gate-v1.3.49
slot env LANG=C make -C "$WT" unit-test >"$TMP/o23a" 2>&1
git -C "$WT" checkout -q gh900-demo; git -C "$WT" branch -q -D gate-v1.3.49
echo one > "$WT/t.txt"; git -C "$WT" add t.txt
git -C "$WT" -c user.name=t -c user.email=t@t commit -q -m tracked
git -C "$WT" push -q origin gh900-demo
NEWSHA=$(git -C "$WT" rev-parse HEAD)
echo two > "$WT/t.txt"
slot env LANG=C make -C "$WT" unit-test >"$TMP/o23b" 2>&1
git -C "$WT" checkout -q -- t.txt
echo three > "$WT/t.txt"; git -C "$WT" add t.txt
slot env LANG=C make -C "$WT" unit-test >"$TMP/o23c" 2>&1
git -C "$WT" reset -q --hard
row SLOT-23 "$([[ -z "$(rr_calls)" && "$(grep -c '^make ' "$STATE/make-calls")" -eq 3 && "$(cat "$TMP/o23a")" == *"branch 'gate-v1.3.49' is not an issue branch"* \
    && "$(cat "$TMP/o23b")" == *"uncommitted"* && "$(cat "$TMP/o23c")" == *"uncommitted"* ]] && echo 1 || echo 0)" \
    "gate-v1.3.49, a modified tracked file and a staged change all run here with the reason"

# SLOT-24: nice 19 for a local command
fresh
rm -f "$STATE/nice-calls"
slot true >/dev/null 2>&1
row SLOT-24 "$([[ "$(cat "$TMP/state/nice-calls" 2>/dev/null)" == "-n 19 true" ]] && echo 1 || echo 0)" \
    "slot.sh runs a local command as 'nice -n 19 <command>' (recorded: $(cat "$TMP/state/nice-calls" 2>/dev/null))"

# SLOT-25: TERM to slot.sh reaches a local command, and the slot stays locked until that command is gone
fresh
cat > "$TMP/term-local.sh" <<'EOF'
#!/usr/bin/env bash
trap 'echo term > "$1"; sleep 1.5; echo done > "$2"; exit 0' TERM
echo $$ >> "$4"
: > "$3"
for _ in $(seq 1 150); do sleep 0.2; done
EOF
chmod +x "$TMP/term-local.sh"
rm -f "$TMP/t25a" "$TMP/t25b" "$TMP/t25c"
timeout --foreground --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" "$TMP/term-local.sh" "$TMP/t25a" "$TMP/t25b" "$TMP/t25c" "$STATE/helper-pids" >/dev/null 2>&1 & p25=$!
for k in $(seq 1 50); do [[ -e "$TMP/t25c" ]] && break; sleep 0.1; done
kill -TERM "$p25"; sleep 0.5
held=0; flock -n "$CLEANSLOTS/slot1" true 2>/dev/null || held=1
wait "$p25" 2>/dev/null
row SLOT-25 "$([[ "$(cat "$TMP/t25a" 2>/dev/null)" == term && $held -eq 1 && "$(cat "$TMP/t25b" 2>/dev/null)" == done ]] && echo 1 || echo 0)" \
    "TERM reaches the local command; the slot stays locked while it shuts down (held=$held)"

# SLOT-26: TERM to slot.sh reaches the driver, and slot.sh returns the driver's own status, not 143
fresh
rm -f "$STATE/rr-term"
STUB_WAIT=1 timeout --foreground --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" env LANG=C make -C "$WT" unit-test >/dev/null 2>&1 & p26=$!
for k in $(seq 1 50); do [[ -e "$STATE/rr-calls" ]] && break; sleep 0.1; done
sleep 0.3; kill -TERM "$p26"
wait "$p26"; rc=$?
row SLOT-26 "$([[ -e "$STATE/rr-term" && $rc -eq 5 ]] && echo 1 || echo 0)" \
    "TERM reaches the driver and slot.sh waits for it, returning its status 5 (rc=$rc)"

# SLOT-27: the admissions.log command is cut at 120 CHARACTERS, not 120 words
fresh
long=$(printf 'x%.0s' $(seq 1 200))
slot true "$long" >/dev/null 2>&1
line=$(cat "$CLEANSLOTS/admissions.log")
cmdpart=${line#*load=* }
row SLOT-27 "$([[ ${#cmdpart} -eq 120 && "$cmdpart" == "true x"* ]] && echo 1 || echo 0)" \
    "a 205-character command is logged as exactly 120 characters (${#cmdpart})"

# SLOT-28: the local half of a fallback is tagged in admissions.log
fresh
REMOTE_RUN_FALLBACK=1 slot true >/dev/null 2>&1
row SLOT-28 "$([[ "$(adm)" =~ \ slot\ 1\ fallback\ waited= ]] && echo 1 || echo 0)" "a run inside a fallback logs 'slot 1 fallback'"

# SLOT-29: a push that fails (origin ahead, not fast-forward) runs the command here, never forces, never calls the driver
fresh
git clone -q "$TMP/origin.git" "$TMP/other"
git -C "$TMP/other" -c user.name=t -c user.email=t@t checkout -q gh900-demo
git -C "$TMP/other" -c user.name=t -c user.email=t@t commit -q --allow-empty -m ahead
git -C "$TMP/other" push -q origin gh900-demo
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m diverged
: > "$STATE/git-calls"
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
pushes=$(grep -E '(^| )push( |$)' "$STATE/git-calls")
git -C "$WT" fetch -q origin; git -C "$WT" reset -q --hard origin/gh900-demo
row SLOT-29 "$([[ $rc -eq 0 && -z "$(rr_calls)" && "$pushes" == "-C $WT push origin gh900-demo" && "$pushes" != *force* \
    && "$out" == *"push failed; running here"* && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "a rejected push runs the command here, once, without force and without calling the driver (rc=$rc)"

# SLOT-46: a push reported as failed while origin is at HEAD (the loser of a concurrent slot.sh push of the same
# sha, git: "incorrect old value provided") still counts: the driver is called with the sha, nothing runs here
fresh
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m raced
RACESHA=$(git -C "$WT" rev-parse HEAD)
out=$(STUB_PUSH_RACE=1 slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-46 "$([[ $rc -eq 0 && "$out" != *"push failed"* && "$(rr_calls)" == *"gh900-demo $RACESHA unit-test"* && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "a push that fails while origin is at HEAD goes on to the driver, once (rc=$rc)"

# SLOT-47: a hanging push and ls-remote are cut at JNEXT_SLOT_GIT_SEC: the command runs here within seconds,
# says "push failed", and the driver is never called
fresh
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m hung
t0=$SECONDS
out=$(STUB_GIT_HANG=1 JNEXT_SLOT_GIT_SEC=2 slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-47 "$([[ $rc -eq 0 && $((SECONDS - t0)) -lt 25 && "$out" == *"push failed; running here"* && -z "$(rr_calls)" && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "a hanging push and ls-remote are cut at JNEXT_SLOT_GIT_SEC (rc=$rc, $((SECONDS - t0)) s): runs here, no driver call"

# SLOT-30: a directory that is not a repository is never sent
fresh
mkdir -p "$TMP/not-a-repo"
out=$(slot env LANG=C make -C "$TMP/not-a-repo" unit-test 2>&1); rc=$?
row SLOT-30 "$([[ -z "$(rr_calls)" && "$out" == *"not sent to the remote"* ]] && echo 1 || echo 0)" \
    "a directory that is not a repository is never sent (the reason names it)"

# SLOT-31: a repository whose `git status` fails (corrupt index; symbolic-ref still works) is not read as clean
fresh
printf garbage > "$WT/.git/index"
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
rm -f "$WT/.git/index"; git -C "$WT" reset -q
row SLOT-31 "$([[ $rc -eq 0 && -z "$(rr_calls)" && "$out" == *"git status failed"* && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "a failing git status keeps the run here and says so, the driver is not called (rc=$rc)"

# SLOT-33: two hosts, both with a free lane: the FIRST in the list gets the run, one probe, no fallback flag
fresh
JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-33 "$([[ $rc -eq 0 && "$(cat "$STATE/probe-calls")" == hostA && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 \
    && "$(rr_calls)" == *"HOST=hostA"* && "$(rr_calls)" != *"--fallback-local"* && "$(adm)" == *" remote hostA waited="* ]] && echo 1 || echo 0)" \
    "two hosts, both free: the first gets the run (probed once, no --fallback-local), the log names it (rc=$rc)"

# SLOT-34: the first host is busy: the second gets the run, and being the last it carries --fallback-local
fresh
STUB_BUSY_HOSTS=hostA JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-34 "$([[ $rc -eq 0 && "$(tr '\n' ' ' < "$STATE/probe-calls")" == "hostA hostB " && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 \
    && "$(rr_calls)" == *"HOST=hostB"* && "$(rr_calls)" == *"--fallback-local $WT"* ]] && echo 1 || echo 0)" \
    "first host busy: probed A then B, the run goes to B with --fallback-local (rc=$rc)"

# SLOT-35: every host busy: no driver call at all, the command runs here, and no remote admission is logged
fresh
STUB_BUSY_HOSTS="hostA hostB" JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-35 "$([[ $rc -eq 0 && ! -e "$STATE/rr-calls" && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" && "$(adm)" != *remote* && "$(adm)" == *" slot 1 "* ]] && echo 1 || echo 0)" \
    "every host busy: both probed, no run sent, the command runs in a local slot (rc=$rc)"

# SLOT-36: a host that answers 75 after its probe (a lane lost, no image): the next host is tried, nothing ran twice
fresh
STUB_RC75_HOSTS=hostA JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-36 "$([[ $rc -eq 0 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 2 && "$(grep '^HOST=' "$STATE/rr-calls" | tr '\n' ' ')" == "HOST=hostA HOST=hostB " \
    && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "driver status 75 from host A moves the run to host B (rc=$rc)"

# SLOT-37: a run that STARTED on host A and failed is never retried on host B: its status is slot.sh's
fresh
STUB_RC=1 JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-37 "$([[ $rc -eq 1 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(rr_calls)" == *"HOST=hostA"* && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "a failed run on host A exits 1 and is not tried on B or here (rc=$rc)"

# SLOT-38: JNEXT_REMOTE_RUN_HOST alone is a one-host list (and loses to JNEXT_REMOTE_RUN_HOSTS when both are set);
# a one-host list whose host is busy runs here with no driver call
fresh
slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1
one=$(rr_calls)
fresh
JNEXT_REMOTE_RUN_HOSTS=hostC slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1
both=$(rr_calls)
fresh
STUB_BUSY_HOSTS=fakehost slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-38 "$([[ "$one" == *"HOST=fakehost"* && "$one" == *"--fallback-local $WT"* && "$both" == *"HOST=hostC"* && "$both" != *"HOST=fakehost"* \
    && $rc -eq 0 && ! -e "$STATE/rr-calls" && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "JNEXT_REMOTE_RUN_HOST is a one-host list, JNEXT_REMOTE_RUN_HOSTS wins, a busy single host runs the command here"

# SLOT-39: every host busy at the first probe, one free on a LATER probe while both local slots are held: the run goes
# to that host (not run here), without waiting for a local slot
fresh
hold 1; hold 2
STUB_BUSY_FIRST=2 JNEXT_REMOTE_RUN_HOSTS="hostA hostB" JNEXT_SLOT_REPROBE=2 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
release_all
row SLOT-39 "$([[ $rc -eq 0 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(rr_calls)" == *"HOST=hostA"* && ! -e "$STATE/make-calls" \
    && "$(wc -l < "$STATE/probe-calls")" -ge 3 ]] && echo 1 || echo 0)" \
    "all hosts busy at the first probe, a lane free on a later one: the run goes to the remote while both local slots are held (rc=$rc)"

# SLOT-40: PAUSED set while host A answers 75: exit 75, host B is never probed for a run or called
fresh
STUB_RC75_HOSTS=hostA STUB_PAUSE=$CLEANSLOTS/PAUSED JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
row SLOT-40 "$([[ $rc -eq 75 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(rr_calls)" == *"HOST=hostA"* && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "PAUSED appearing as a host answers 75 stops the search: exit 75, no run on the next host (rc=$rc)"

# SLOT-41: every host busy with an UNPUSHED commit: nothing is pushed (a push is only for a host that will take the run)
fresh
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m unpushed
: > "$STATE/git-calls"
STUB_BUSY_HOSTS="hostA hostB" JNEXT_REMOTE_RUN_HOSTS="hostA hostB" JNEXT_SLOT_REPROBE=1000 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; rc=$?
pushes=$(grep -cE '(^| )push( |$)' "$STATE/git-calls")
git -C "$WT" reset -q --hard origin/gh900-demo
row SLOT-41 "$([[ $rc -eq 0 && "$pushes" -eq 0 && ! -e "$STATE/rr-calls" && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "every host busy, unpushed commit: no push, the run goes to a local slot (pushes=$pushes, rc=$rc)"

# SLOT-42: PAUSED created while a long run waits with every host busy and every slot held (re-probing): exit 75
fresh
hold 1; hold 2
( STUB_BUSY_HOSTS="hostA hostB" JNEXT_REMOTE_RUN_HOSTS="hostA hostB" JNEXT_SLOT_REPROBE=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; echo $? > "$TMP/rc42" ) & w42=$!
sleep 1; : > "$CLEANSLOTS/PAUSED"
wait "$w42"
release_all
p_after=$(wc -l < "$STATE/probe-calls"); sleep 0.6; p_later=$(wc -l < "$STATE/probe-calls")
row SLOT-42 "$([[ "$(cat "$TMP/rc42")" == 75 && ! -e "$STATE/rr-calls" && ! -e "$STATE/make-calls" && "$p_after" -eq "$p_later" ]] && echo 1 || echo 0)" \
    "PAUSED during the wait exits 75 with re-probing running, nothing is sent or started, probing stops"

# SLOT-43: a rejected push ends the remote attempt for good: with both slots held and a re-probe on every poll the
# push is tried exactly ONCE (not again every poll), the driver is never called, and the command then runs here
fresh
git clone -q "$TMP/origin.git" "$TMP/other43"
git -C "$TMP/other43" -c user.name=t -c user.email=t@t checkout -q gh900-demo
git -C "$TMP/other43" -c user.name=t -c user.email=t@t commit -q --allow-empty -m ahead43
git -C "$TMP/other43" push -q origin gh900-demo
git -C "$WT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m diverged43
: > "$STATE/git-calls"
hold 1; hold 2
( JNEXT_SLOT_REPROBE=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; echo $? > "$TMP/rc43" ) & w43=$!
sleep 1.5
release_all
wait "$w43"
pushes=$(grep -cE '(^| )push( |$)' "$STATE/git-calls")
git -C "$WT" fetch -q origin; git -C "$WT" reset -q --hard origin/gh900-demo
row SLOT-43 "$([[ "$(cat "$TMP/rc43")" == 0 && "$pushes" -eq 1 && ! -e "$STATE/rr-calls" && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "a rejected push is tried once while the command waits (pushes=$pushes), the driver is not called, the command runs here"

# SLOT-44: a refusal (driver exit 3) is final too: exactly one driver call over several re-probes, then the command runs here
fresh
hold 1; hold 2
( STUB_RC=3 JNEXT_SLOT_REPROBE=1 slot env LANG=C make -C "$WT" unit-test >/dev/null 2>&1; echo $? > "$TMP/rc44" ) & w44=$!
sleep 1.5
release_all
wait "$w44"
row SLOT-44 "$([[ "$(cat "$TMP/rc44")" == 0 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "a driver refusal (exit 3) is not re-sent on later polls: one driver call, then the command runs here"

# SLOT-45: a SHORT target, every host busy, whose local slot frees on a later poll runs here with no extra probe (the
# order is: probe the hosts once, try the local slots, THEN re-probe every REPROBE-th failed poll; REPROBE is set
# high so the 0.5 s wait cannot reach a re-probe)
fresh
hold 1; hold 2
( STUB_BUSY_HOSTS=fakehost JNEXT_SLOT_REPROBE=1000 slot make -C "$WT" regression-rows ROWS=a >/dev/null 2>&1; echo $? > "$TMP/rc45" ) & w45=$!
# release only once slot.sh is known to be waiting: its first probe has run (bounded wait), the command was not admitted
# (make stub never called), and a margin of several polls has passed; a command admitted before the release fails the row
for _ in $(seq 1 300); do [[ -e "$STATE/probe-calls" ]] && break; sleep 0.1; done
early45=0; [[ -e "$STATE/probe-calls" && ! -e "$STATE/make-calls" ]] || early45=1
sleep 1
release_all
wait "$w45"
row SLOT-45 "$([[ $early45 -eq 0 && "$(cat "$TMP/rc45")" == 0 && "$(wc -l < "$STATE/probe-calls")" -eq 1 && ! -e "$STATE/rr-calls" && "$(cat "$STATE/make-calls")" == "make -C $WT regression-rows ROWS=a" ]] && echo 1 || echo 0)" \
    "a short target, hosts busy, whose slot frees after failed polls runs here with exactly one probe (the first)"

# SLOT-63: a SHORT target (test-affected) from a dirty tree stays local with the lane free: no probe, no driver call
# (the mutation-loop case)
fresh
: > "$WT/scratch.txt"
out=$(slot make -C "$WT" test-affected 2>&1); rc=$?
rm -f "$WT/scratch.txt"
row SLOT-63 "$([[ $rc -eq 0 && -z "$(rr_calls)" && ! -e "$STATE/probe-calls" && "$out" == *"not sent to the remote: the worktree has uncommitted"* \
    && "$(cat "$STATE/make-calls")" == "make -C $WT test-affected" ]] && echo 1 || echo 0)" \
    "a dirty tree keeps a short heavy run local with a free lane: no probe, no driver call, reason printed (rc=$rc)"

# SLOT-64: a queued short run keeps the queue behaviour (owner, 2026-11-23): a short target with both slots held and every host busy at first leaves for the lane that frees on a later probe
fresh
hold 1; hold 2
STUB_BUSY_FIRST=2 JNEXT_SLOT_REPROBE=2 slot make -C "$WT" regression-rows ROWS=a >/dev/null 2>&1; rc=$?
release_all
row SLOT-64 "$([[ $rc -eq 0 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(rr_calls)" == *"regression-rows ROWS=a"* && ! -e "$STATE/make-calls" \
    && "$(wc -l < "$STATE/probe-calls")" -ge 3 ]] && echo 1 || echo 0)" \
    "a short run queued with every host busy goes to the remote when a lane frees on a later probe (rc=$rc)"

# SLOT-65: the first probe of an eligible short run is loud: with every host busy the line naming the host and the
# missing lane is printed before the run goes local
fresh
out=$(STUB_BUSY_HOSTS=fakehost slot make -C "$WT" test-affected 2>&1); rc=$?
row SLOT-65 "$([[ $rc -eq 0 && "$out" == *"[slot.sh] remote host fakehost: no free lane (or unusable)"* && -z "$(rr_calls)" \
    && "$(cat "$STATE/make-calls")" == "make -C $WT test-affected" ]] && echo 1 || echo 0)" \
    "an eligible short run with every host busy prints the 'no free lane' line for the host, then runs here (rc=$rc)"

# SLOT-66 (GH #562): the driver is told whose LOCAL main supplies test-affected's base: this branch's worktree, on every
# host tried (REMOTE_RUN_BASE_REPO), not only the last one that also gets --fallback-local
fresh
out=$(slot env LANG=C make -C "$WT" test-affected 2>&1); rc=$?
row SLOT-66 "$([[ $rc -eq 0 && "$(rr_calls)" == *"BASE_REPO=$WT"* && "$(rr_calls)" != *"BASE_REPO=unset"* ]] && echo 1 || echo 0)" \
    "a remote run is given REMOTE_RUN_BASE_REPO=<the branch's worktree> (rc=$rc)"

# SLOT-67 (GH #562 review): two hosts, both free: the first takes the run WITHOUT --fallback-local and still gets
# the base repository (the variable is not tied to the fallback flag, which only the last host carries)
fresh
JNEXT_REMOTE_RUN_HOSTS="hostA hostB" slot env LANG=C make -C "$WT" test-affected >/dev/null 2>&1; rc=$?
row SLOT-67 "$([[ $rc -eq 0 && "$(grep -c '^ARGS' "$STATE/rr-calls")" -eq 1 && "$(rr_calls)" == *"HOST=hostA"* \
    && "$(rr_calls)" != *"--fallback-local"* && "$(rr_calls)" == *"BASE_REPO=$WT"* ]] && echo 1 || echo 0)" \
    "first of two free hosts: no --fallback-local, BASE_REPO=<worktree> (rc=$rc)"

# SLOT-68 (GH #564 review): heavy BY CLASS. Every Makefile target whose prerequisites reach the PGO build gui-release
# (harness-selftest, regression-confirm, fuse-pgo, ...) is in slot.sh's and project.conf's HEAVY_TARGETS, derived
# from the Makefile's own rule database (make -qp), so a new target that pulls the PGO build in cannot stay local
pgo_targets() {   # pgo_targets <dir holding Makefile>: every target that reaches gui-release, by prerequisite or recipe-level $(MAKE)
    LANG=C /usr/bin/make -C "$1" --no-print-directory -f Makefile -qp 2>/dev/null | awk '
    /^# Not a target/ {skip=1}
    /^[A-Za-z0-9_][A-Za-z0-9_.-]*:([^=]|$)/ { if (!skip) { t=$0; sub(/:.*/,"",t); cur=t; d=$0; sub(/^[^:]*:/,"",d); dep[t]=dep[t] " " d } }
    /^\t/ { if (!skip && cur != "" && index($0,"$(MAKE)")) { r=$0; sub(/.*\$\(MAKE\)/,"",r); dep[cur]=dep[cur] " " r } }   # recipe-level $(MAKE) <target>: an edge too
    /^$/ {skip=0; cur=""}
    function reach(t, n, i, a) { if (t in seen) return; seen[t]=1; n=split(dep[t],a," "); for (i=1;i<=n;i++) reach(a[i]) }
    END { for (t in dep) { delete seen; reach(t); if (t != "gui-release" && ("gui-release" in seen)) print t } }' | sort
}
pgo=$(pgo_targets "$ROOT")
pc=$(sed -n 's/^HEAVY_TARGETS="\(.*\)"/\1/p' "$ROOT/.claude/skills/remote-run/project.conf")
sl=$(sed -n 's/^HEAVY_TARGETS="\(.*\)"/\1/p' "$SLOT")
miss=""
for t in $pgo gui-release; do
    [[ " $sl " == *" $t "* ]] || miss+=" slot.sh:$t"
    [[ " $pc " == *" $t "* ]] || miss+=" project.conf:$t"
done
row SLOT-68 "$([[ -n "$pgo" && "$pgo" == *harness-selftest* && -z "$miss" ]] && echo 1 || echo 0)" \
    "every Makefile target that depends on gui-release is a heavy target in slot.sh and project.conf (derived from make -qp: $(tr '\n' ' ' <<<"$pgo")missing:$miss)"

# SLOT-69: harness-selftest (PGO build) goes remote with a free lane and runs here with every host busy, like the other heavy targets
fresh
slot env LANG=C make -C "$WT" harness-selftest >/dev/null 2>&1
ok_remote=$([[ "$(rr_calls)" == *" gh900-demo "*" harness-selftest"* && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)
rm -f "$STATE/rr-calls" "$STATE/make-calls"
STUB_BUSY_HOSTS=fakehost slot env LANG=C make -C "$WT" harness-selftest >/dev/null 2>&1
ok_busy=$([[ -z "$(rr_calls)" && "$(cat "$STATE/make-calls" 2>/dev/null)" == "make -C $WT harness-selftest" ]] && echo 1 || echo 0)
row SLOT-69 "$([[ $ok_remote -eq 1 && $ok_busy -eq 1 ]] && echo 1 || echo 0)" \
    "make harness-selftest goes to a free remote lane and runs here with every host busy (remote:$ok_remote busy:$ok_busy)"

# SLOT-70 (GH #564 review 2): the derivation sees a recipe-level $(MAKE) call: a fixture Makefile whose target builds
# gui-release only from its recipe, one by prerequisite and one by order-only prerequisite are all derived; an unrelated target is not
FX="$TMP/pgo-fixture"; mkdir -p "$FX"
printf 'gui-release:\n\t@true\nzz-recipe:\n\t@$(MAKE) --no-print-directory gui-release\nzz-prereq: gui-release\nzz-oo: | gui-release\nzz-other:\n\t@$(MAKE) zz-unrelated\nzz-unrelated:\n\t@true\n' >"$FX/Makefile"
fx=$(pgo_targets "$FX" | tr '\n' ' ')
row SLOT-70 "$([[ "$fx" == "zz-oo zz-prereq zz-recipe " ]] && echo 1 || echo 0)" \
    "a target reaching gui-release by recipe \$(MAKE), prerequisite or order-only prerequisite is derived, an unrelated one is not (got: $fx)"

# ---- thread caps (owner, 2026-11-23): a locally admitted run gets nproc/NSLOTS threads ------------------------
# Oracle: the measurement in slot.sh's apply_thread_caps comment (6 threads each beat 12 each for two concurrent
# runs). JNEXT_SLOT_CPUS stands for nproc; the make stub records MAKEFLAGS and the two JNEXT_*_JOBS variables.
capslot() {   # <NSLOTS> <cpus> <command...>: fresh slot dir with that NSLOTS, caller's cap variables removed
    local n=$1 c=$2; shift 2
    echo "$n" > "$CLEANSLOTS/NSLOTS"
    env -u JOBS -u MAKEFLAGS -u JNEXT_UNIT_TEST_JOBS -u JNEXT_TEST_JOBS JNEXT_SLOT_CPUS=$c ${CAPS_REMOTE:-JNEXT_SLOT_NO_OVERFLOW=1} \
        timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" "$@"
}
makeenv() { cat "$STATE/make-env" 2>/dev/null; }

# SLOT-48: NSLOTS 1 -> every CPU
fresh
out=$(capslot 1 12 env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-48 "$([[ $rc -eq 0 && "$(makeenv)" == "MF=[ JOBS=12] U=12 T=12" && "$out" == *"thread caps (nproc 12 / NSLOTS 1 = 12): JOBS=12 JNEXT_UNIT_TEST_JOBS=12 JNEXT_TEST_JOBS=12"* \
    && "$(cat "$STATE/make-calls")" == "make -C $WT unit-test" ]] && echo 1 || echo 0)" \
    "NSLOTS 1: JOBS (MAKEFLAGS), JNEXT_UNIT_TEST_JOBS and JNEXT_TEST_JOBS are 12, one log line names them, the make argv is untouched"

# SLOT-49: NSLOTS 2 -> half
fresh
out=$(capslot 2 12 env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-49 "$([[ $rc -eq 0 && "$(makeenv)" == "MF=[ JOBS=6] U=6 T=6" && "$out" == *"NSLOTS 2 = 6): JOBS=6"* ]] && echo 1 || echo 0)" \
    "NSLOTS 2: the three caps are 6, whether or not the other slot is busy (the other slot is idle here)"

# SLOT-50: NSLOTS 3 -> a third, with the other slots HELD (busy or not makes no difference)
fresh
hold 1; hold 2
out=$(capslot 3 12 env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
release_all
row SLOT-50 "$([[ $rc -eq 0 && "$(makeenv)" == "MF=[ JOBS=4] U=4 T=4" && "$out" == *"admitted to slot 3"* ]] && echo 1 || echo 0)" \
    "NSLOTS 3 with slots 1 and 2 busy: the caps are 4 (slot 3 admitted)"

# SLOT-51: a value the caller set wins, per variable, and the log line says so
fresh
echo 2 > "$CLEANSLOTS/NSLOTS"
out=$(env -u JOBS -u MAKEFLAGS -u JNEXT_UNIT_TEST_JOBS JNEXT_TEST_JOBS=2 JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" make -C "$WT" unit-test JOBS=3 2>&1); rc=$?
row SLOT-51 "$([[ $rc -eq 0 && "$(makeenv)" == "MF=[] U=6 T=2" && "$out" == *"JOBS=caller's command line JNEXT_UNIT_TEST_JOBS=6 JNEXT_TEST_JOBS=2 (caller's)"* ]] && echo 1 || echo 0)" \
    "JOBS= on the command line and an exported JNEXT_TEST_JOBS are kept (MAKEFLAGS untouched), JNEXT_UNIT_TEST_JOBS is still capped, the line says so"

# SLOT-52: a caller's JOBS in MAKEFLAGS is kept; a caller's JOBS in the environment is passed to make as its value
fresh
echo 2 > "$CLEANSLOTS/NSLOTS"
out=$(env -u JOBS -u JNEXT_UNIT_TEST_JOBS -u JNEXT_TEST_JOBS MAKEFLAGS="-k JOBS=7" JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" make -C "$WT" unit-test 2>&1); rc=$?
a=$(makeenv); rm -f "$STATE/make-env"
out2=$(env -u MAKEFLAGS -u JNEXT_UNIT_TEST_JOBS -u JNEXT_TEST_JOBS JOBS=5 JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" make -C "$WT" unit-test 2>&1); rc2=$?
row SLOT-52 "$([[ $rc -eq 0 && $rc2 -eq 0 && "$a" == "MF=[-k JOBS=7] U=6 T=6" && "$out" == *"JOBS=caller's MAKEFLAGS"* \
    && "$(makeenv)" == "MF=[ JOBS=5] U=6 T=6" && "$out2" == *"JOBS=5 (caller's environment)"* ]] && echo 1 || echo 0)" \
    "a caller's JOBS in MAKEFLAGS is kept; one in the environment is handed to make (the Makefile ignores an environment JOBS), both named in the log"

# SLOT-53: a run sent to the remote is untouched: the driver sees none of the caps and no line is logged
fresh
out=$(CAPS_REMOTE=JNEXT_SLOT_X=1 capslot 2 12 env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-53 "$([[ $rc -eq 0 && "$(cat "$STATE/rr-env" 2>/dev/null)" == "MF=[] U=unset T=unset" && ! -e "$STATE/make-calls" && "$out" != *"thread caps"* ]] && echo 1 || echo 0)" \
    "a long run sent to the driver gets no MAKEFLAGS, JNEXT_UNIT_TEST_JOBS or JNEXT_TEST_JOBS and no cap line (the remote driver keeps its own rule)"

# SLOT-54: the cap never drops below 1
fresh
out=$(capslot 4 2 env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
row SLOT-54 "$([[ $rc -eq 0 && "$(makeenv)" == "MF=[ JOBS=1] U=1 T=1" ]] && echo 1 || echo 0)" \
    "2 CPUs over 4 slots: the caps are 1, not 0"

# SLOT-55: a command that is not make gets the two environment caps AND the JOBS cap in MAKEFLAGS (a make it
# starts reads it; a non-make program ignores it)
fresh
out=$(capslot 2 12 bash -c 'echo "got MF=[${MAKEFLAGS-}] U=$JNEXT_UNIT_TEST_JOBS T=$JNEXT_TEST_JOBS"' 2>&1); rc=$?
row SLOT-55 "$([[ $rc -eq 0 && "$out" == *"got MF=[ JOBS=6] U=6 T=6"* && "$out" == *"JOBS=6 JNEXT_UNIT_TEST_JOBS=6"* ]] && echo 1 || echo 0)" \
    "a non-make command gets JNEXT_UNIT_TEST_JOBS, JNEXT_TEST_JOBS and the JOBS cap in MAKEFLAGS"

# SLOT-56: the mechanism itself: a REAL make on a Makefile that assigns JOBS with := (as ours does) sees the cap,
# and a JOBS= word on its command line still beats it
fresh
RM=$TMP/realmake; mkdir -p "$RM"; printf 'JOBS := 12\nall:\n\t@echo seen-JOBS=$(JOBS)\n' > "$RM/Makefile"
REALPATH=${PATH#"$TMP/bin:"}
o56a=$(capslot 2 12 env PATH="$REALPATH" make -C "$RM" 2>&1); o56b=$(capslot 2 12 env PATH="$REALPATH" make -C "$RM" JOBS=3 2>&1)
row SLOT-56 "$([[ "$o56a" == *"seen-JOBS=6"* && "$o56b" == *"seen-JOBS=3"* ]] && echo 1 || echo 0)" \
    "real make: a := JOBS in the Makefile is overridden by the cap (6); the caller's command-line JOBS=3 wins over it"

# SLOT-57: `env [LANG=C] JOBS=N make`: the env word is the caller's value, handed to make (the Makefile ignores an env JOBS)
fresh
o57a=$(capslot 2 12 env PATH="$REALPATH" JOBS=3 make -C "$RM" 2>&1); o57b=$(capslot 2 12 env LANG=C PATH="$REALPATH" JOBS=3 make -C "$RM" 2>&1)
row SLOT-57 "$([[ "$o57a" == *"seen-JOBS=3"* && "$o57b" == *"seen-JOBS=3"* && "$o57b" == *"JOBS=3 (caller's env word)"* ]] && echo 1 || echo 0)" \
    "real make behind env JOBS=3 (with and without LANG=C) sees 3, not 12 or the cap, and the log says the caller's env word"

# SLOT-58: make behind wrappers is capped: timeout, an absolute path, env -u, and the driver's fallback sh form (bash -c)
fresh
o58a=$(capslot 2 12 timeout --kill-after=5s 20s env PATH="$REALPATH" make -C "$RM" 2>&1)
o58b=$(capslot 2 12 env PATH="$REALPATH" /usr/bin/make -C "$RM" 2>&1)
o58c=$(capslot 2 12 env -u FOO PATH="$REALPATH" make -C "$RM" 2>&1)
o58d=$(capslot 2 12 env LANG=C PATH="$REALPATH" bash -c "cd '$RM' && make" 2>&1)
o58e=$(capslot 2 12 env PATH="$REALPATH" /usr/bin/make -C "$RM" JOBS=3 2>&1)
row SLOT-58 "$([[ "$o58e" == *"seen-JOBS=3"* && "$o58e" == *"JOBS=caller's command line"* && "$o58a" == *"seen-JOBS=6"* && "$o58b" == *"seen-JOBS=6"* && "$o58c" == *"seen-JOBS=6"* && "$o58d" == *"seen-JOBS=6"* ]] && echo 1 || echo 0)" \
    "real make behind timeout, /usr/bin/make, env -u and bash -c (the fallback sh: form) sees the cap 6; a JOBS= after /usr/bin/make is the command line's"

# SLOT-59: the caller's other MAKEFLAGS survive: a real make still sees -k next to the cap
fresh
echo 2 > "$CLEANSLOTS/NSLOTS"
printf 'JOBS := 12\nall:\n\t@echo seen-JOBS=$(JOBS) flags=[$(MAKEFLAGS)]\n' > "$RM/Makefile"
o59=$(env -u JOBS -u JNEXT_UNIT_TEST_JOBS -u JNEXT_TEST_JOBS MAKEFLAGS=-k JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 PATH="$REALPATH" timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" make -C "$RM" 2>&1)
row SLOT-59 "$([[ "$o59" == *"seen-JOBS=6"* && "$o59" == *"flags=[k "* ]] && echo 1 || echo 0)" \
    "a caller's MAKEFLAGS=-k is kept: real make sees the k flag and JOBS=6"

# SLOT-60: an exported EMPTY JNEXT_TEST_JOBS is not a caller's value: it is capped
fresh
out=$(env -u JOBS -u MAKEFLAGS -u JNEXT_UNIT_TEST_JOBS JNEXT_TEST_JOBS= JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" env LANG=C make -C "$WT" unit-test 2>&1)
row SLOT-60 "$([[ "$(makeenv)" == *" T=6" && "$out" == *"JNEXT_TEST_JOBS=6"* && "$out" != *"JNEXT_TEST_JOBS= (caller"* ]] && echo 1 || echo 0)" \
    "an exported empty JNEXT_TEST_JOBS counts as unset and gets the cap"

# SLOT-61: the caller's MAKEFLAGS=-k survives on the two other branches that write MAKEFLAGS: an env JOBS word and an
# environment JOBS (a real make sees k and the caller's value)
fresh
echo 2 > "$CLEANSLOTS/NSLOTS"
mkf() {   # <MAKEFLAGS value> <command...>: slot.sh with that caller MAKEFLAGS (and $MKF_ENV, e.g. JOBS=5), real make's PATH
    local mf=$1; shift
    env -u JOBS -u JNEXT_UNIT_TEST_JOBS -u JNEXT_TEST_JOBS MAKEFLAGS="$mf" JNEXT_SLOT_CPUS=12 JNEXT_SLOT_NO_OVERFLOW=1 PATH="$REALPATH" ${MKF_ENV:-X_UNUSED=1} \
        timeout --kill-after=5s 60s "$SLOT" --dir "$CLEANSLOTS" "$@" 2>&1
}
o61a=$(mkf -k env JOBS=3 make -C "$RM")
o61b=$(MKF_ENV=JOBS=5 mkf -k make -C "$RM")
row SLOT-61 "$([[ "$o61a" == *"seen-JOBS=3"* && "$o61a" == *"flags=[k "* && "$o61b" == *"seen-JOBS=5"* && "$o61b" == *"flags=[k "* ]] && echo 1 || echo 0)" \
    "with MAKEFLAGS=-k, an env JOBS=3 word and an environment JOBS=5 each reach a real make together with the k flag"

# SLOT-62: two caller values: MAKEFLAGS JOBS=7 beats an env word JOBS=3 (as make itself would), and the log says so
fresh
echo 2 > "$CLEANSLOTS/NSLOTS"
o62=$(mkf "JOBS=7" env JOBS=3 make -C "$RM")
row SLOT-62 "$([[ "$o62" == *"seen-JOBS=7"* && "$o62" == *"JOBS=caller's MAKEFLAGS"* ]] && echo 1 || echo 0)" \
    "MAKEFLAGS JOBS=7 plus env JOBS=3 make: make sees 7 and the log says the caller's MAKEFLAGS"

# SLOT-32: the contract does not depend on the caller's environment: this whole test, run again with the
# driver-fallback variables, the remote's variables and a failing ssh and wrapper on PATH, is green and
# never reaches either (GH #547 stage 4)
if [[ -n "${SKILLTEST_INNER:-}" ]]; then pol=1; else polluted_check "$TMP/polluted"; pol=$POL; fi
row SLOT-32 "$([[ $pol -eq 1 ]] && echo 1 || echo 0)" \
    "a run under REMOTE_RUN_FALLBACK, REMOTE_RUN_LOCAL_WRAP, JNEXT_SLOT_*, JNEXT_REMOTE_RUN_* in the environment is green and touches no real wrapper or ssh"

finish
