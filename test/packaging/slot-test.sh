#!/usr/bin/env bash
#
# Contract test for .claude/skills/autonomous-run/slot.sh (GH #547 stage 4): local admission (slots,
# NSLOTS, load limit, PAUSED) and the overflow of heavy runs to the remote-run driver.
# Hermetic: a scratch slot directory, a load file in place of /proc/loadavg, a stub driver, a `make`
# stub, a real throwaway git repository with a local bare "origin" and a PATH shim that records every
# git call; no ssh, no network. Everything lives under one scratch directory, removed on exit.
#
# Oracle: the owner's rules of 2026-11-21/22 as written in slot.sh's header (4 local+remote slots, host
# picked by load, long heavy runs prefer the remote, never wait on it) and COMMON-RULES "REMOTE RUNS"
# (an agent pushes only its own issue branch, never force). No VHDL: this is tooling.
#
# Rows SLOT-01 .. SLOT-18. A missing tool FAILS every row (no SKIP path).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
SLOT=$ROOT/.claude/skills/autonomous-run/slot.sh
EXPECTED_ROWS=18

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
    rm -rf "$TMP"
}
trap cleanup EXIT
export HOME=$TMP/home
mkdir -p "$HOME" "$TMP/bin" "$TMP/state"
REALGIT=$(command -v git)

# ---- fakes ------------------------------------------------------------------
# git shim: records its arguments, then runs the real git.
cat > "$TMP/bin/git" <<EOF
#!/usr/bin/env bash
echo "\$*" >> "$TMP/state/git-calls"
exec "$REALGIT" "\$@"
EOF
# make stub: records the call and exits MAKE_RC (default 0).
cat > "$TMP/bin/make" <<'EOF'
#!/usr/bin/env bash
echo "make $*" >> "$STATE/make-calls"
exit "${MAKE_RC:-0}"
EOF
# driver stub: records its arguments and the two environment variables slot.sh must set, exits STUB_RC.
cat > "$TMP/rr-stub" <<'EOF'
#!/usr/bin/env bash
{ echo "ARGS $*"; echo "NO_OVERFLOW=${JNEXT_SLOT_NO_OVERFLOW:-unset}"; echo "WRAP=${REMOTE_RUN_LOCAL_WRAP:-unset}"; } >> "$STATE/rr-calls"
echo "stub driver ran"
exit "${STUB_RC:-0}"
EOF
chmod +x "$TMP/bin/git" "$TMP/bin/make" "$TMP/rr-stub"
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
    rm -rf "$CLEANSLOTS" "$STATE"/rr-calls "$STATE"/make-calls "$STATE"/git-calls "$TMP/rrlogs"
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
row SLOT-01 "$([[ $rc -eq 0 && "$out" == *"admitted to slot 1"* && "$(adm)" == *" slot 1 waited=0s load=1.00 true" ]] && echo 1 || echo 0)" \
    "a free slot runs the command here and admissions.log names the slot (rc=$rc)"

# SLOT-02: a LONG heavy command on a clean, pushed issue branch goes to the remote FIRST, even with slots free
fresh
out=$(slot env LANG=C make -C "$WT" unit-test 2>&1); rc=$?
rc_calls=$(rr_calls)
row SLOT-02 "$([[ $rc -eq 0 && "$rc_calls" == *"ARGS --stream --fallback-local $WT --keep-going --id gh900-demo-"* \
    && "$rc_calls" == *"--log-dir $TMP/rrlogs gh900-demo $SHA unit-test"* && "$rc_calls" == *"NO_OVERFLOW=1"* \
    && "$rc_calls" == *"WRAP=$SLOT --dir $CLEANSLOTS"* && "$(adm)" == *" remote "* && ! -e "$STATE/make-calls" ]] && echo 1 || echo 0)" \
    "long heavy run: the driver gets --fallback-local, branch, sha, target, NO_OVERFLOW=1 and this script as the local wrapper; nothing ran here (rc=$rc)"

# SLOT-03: a short heavy command (regression-rows) with every slot held overflows; with a free slot it stays local
fresh
hold 1; hold 2
out=$(slot make -C "$WT" regression-rows ROWS=a 2>&1); rc=$?
r3a=$(rr_calls)
release_all; fresh
out=$(slot make -C "$WT" regression-rows ROWS=a 2>&1); rc2=$?
row SLOT-03 "$([[ $rc -eq 0 && "$r3a" == *"gh900-demo $SHA regression-rows ROWS=a"* && $rc2 -eq 0 && -z "$(rr_calls)" \
    && "$(cat "$STATE/make-calls")" == "make -C $WT regression-rows ROWS=a" ]] && echo 1 || echo 0)" \
    "short heavy run: both slots held -> remote with its make variable; a free slot -> here, driver not called"

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
for i in 1 2 3 4; do ( slot sleep 4 >/dev/null 2>&1 ) & done
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

finish
