#!/usr/bin/env bash
# slot.sh: build/test admission for the agents of an autonomous run (GH #547 stage 4), with
# automatic overflow of heavy runs to the remote-run host.
#
#   slot.sh --dir <run>/slots [--limit N] <command> [args...]
#
# The run directory's own slot.sh is a two-line wrapper the orchestrator writes at run start:
#   #!/bin/bash
#   exec <main checkout>/.claude/skills/autonomous-run/slot.sh --dir <run>/slots "$@"
# (the MAIN checkout's copy: it also finds remote-run.sh next to it, and the driver must be main's).
#
# LOCAL ADMISSION (unchanged from the hand-written script this replaces)
#   - <dir>/PAUSED exists: exit 75 at once, and again before every retry ("checkpoint and stop").
#   - A slot is an flock on <dir>/slot<i>, held for the whole command, which runs at nice 19; INT/TERM
#     are forwarded; the exit status is the command's.
#   - Admitted only while the 1-minute load is below the limit (default: nproc; --limit or
#     JNEXT_SLOT_LOAD_LIMIT). The check is made AFTER the slot's lock is taken. The lock is per slot, so
#     two waiters on two different slots can both pass on the same load reading (as before).
#   - Slot count: <dir>/NSLOTS, an integer 1..8, read at every attempt; absent = 2; anything else is
#     refused with exit 2. The autonomous-run skill writes 4 at the start of an unattended run (owner,
#     2026-11-21) and 2 when the owner is at the machine.
#
# OVERFLOW TO THE REMOTE (owner, 2026-11-21: host picked by load; long heavy runs go to the remote by
# default; never wait on it)
#   A command is ELIGIBLE when ALL of these hold (the first that fails is printed, once, and the
#   command waits for a local slot as before):
#     1. JNEXT_SLOT_NO_OVERFLOW and REMOTE_RUN_FALLBACK are unset (a run the driver fell back from is
#        never sent back: this is also how the driver's own fallback through this script ends);
#     2. it is  [env LANG=C] make -C <worktree> <word>...  where every word is a heavy target or a
#        make VAR=value; at least one target; no other make option, no other environment word;
#     3. JNEXT_REMOTE_RUN_HOST is set;
#     4. the worktree's branch is an issue branch gh<N>-<slug> (never main, epic*, gate-*, detached);
#     5. `git status --porcelain` there is empty, untracked files included: the remote runs only
#        committed state, so an edit-and-test cycle (mutations!) always stays local.
#   An eligible command is sent to the remote FIRST when it is LONG (the full unit-test targets,
#   wasm-browser-test, the full regression, regression-win/-wasm), because the remote host is as fast as
#   this one and does not load it; a heavy but short command (regression-rows, test-affected) is sent
#   only when no local slot is admissible right now (all held, or the load is at the limit). Sending =
#   push origin/<branch> if it is not at HEAD (`git push origin <branch>`, never forced; COMMON-RULES
#   "REMOTE RUNS" authorizes an agent to push its own issue branch), then remote-run.sh --stream
#   --fallback-local, with this script as REMOTE_RUN_LOCAL_WRAP. Both remote lanes busy past the
#   driver's LANE_WAIT (30 s), the alias unreachable, ...: the driver runs the targets here through this
#   script, with overflow switched off (no live output reaches the caller meanwhile: the driver writes a
#   fallback run to its log only, so a long wait looks silent until the summary), so the run waits for a local slot and never loops. The exit
#   status is the driver's, except 75 when <dir>/PAUSED appears meanwhile (0 green, 1 not clean or failed, 124 timeout, 2 usage). Exit 3 (the push did
#   not land) ran nothing, so it is run locally instead; 75 never reaches here (the fallback took it).
#
# The remote host comes from the environment this script runs in: the run's wrapper must export
# JNEXT_REMOTE_RUN_HOST=<ssh alias> and JNEXT_REMOTE_RUN_DIR=<remote work directory> (see the
# autonomous-run skill), or every heavy command stays local and says why.
#
# Every admission appends one line to <dir>/admissions.log:
#   <ISO time> <slot N | remote> waited=<s>s load=<1-min> <command, first 120 characters>
#
# Test hooks (the contract, test/packaging/slot-test.sh): JNEXT_SLOT_LOADAVG (a file in /proc/loadavg
# format), JNEXT_SLOT_REMOTE_RUN (the driver), JNEXT_SLOT_POLL (seconds between local attempts, default 20),
# JNEXT_SLOT_REMOTE_LOG_DIR (default ~/tmp/remote-run).
set -u

SELF=$(readlink -f "${BASH_SOURCE[0]}")
HERE=$(dirname "$SELF")
RR=${JNEXT_SLOT_REMOTE_RUN:-$HERE/../remote-run/remote-run.sh}
LOADAVG=${JNEXT_SLOT_LOADAVG:-/proc/loadavg}
POLL=${JNEXT_SLOT_POLL:-20}
LOGDIR=${JNEXT_SLOT_REMOTE_LOG_DIR:-$HOME/tmp/remote-run}
LIMIT=${JNEXT_SLOT_LOAD_LIMIT:-$(nproc 2>/dev/null || echo 12)}
DIR=${JNEXT_SLOT_DIR:-}

# Targets worth sending (the remote-run skill's D14 list, plus the full regression and test-affected).
LONG_TARGETS=" unit-test unit-test-sdl unit-test-win unit-test-wasm wasm-browser-test regression regression-win regression-wasm "
SHORT_TARGETS=" regression-rows test-affected "

usage() { echo "usage: slot.sh --dir <slots dir> [--limit N] <command> [args...]" >&2; exit 2; }
while [[ $# -gt 0 ]]; do
    case $1 in
        --dir)   [[ $# -ge 2 ]] || usage; DIR=$2; shift 2 ;;
        --limit) [[ $# -ge 2 ]] || usage; LIMIT=$2; shift 2 ;;
        --)      shift; break ;;
        -*)      usage ;;
        *)       break ;;
    esac
done
[[ -n "$DIR" && $# -gt 0 ]] || usage
[[ "$LIMIT" =~ ^[0-9]+$ ]] || { echo "[slot.sh] bad load limit '$LIMIT'" >&2; exit 2; }
mkdir -p "$DIR"
CMD=("$@")
T0=$SECONDS

paused() {
    [[ ! -e "$DIR/PAUSED" ]] && return 1
    echo "[slot.sh] REFUSED: the owner paused all builds and tests (slots/PAUSED). Do not retry; checkpoint and stop." >&2
    exit 75
}

# read_nslots: sets NSLOTS from <dir>/NSLOTS (absent = 2); anything but 1..8 is refused.
read_nslots() {
    local v=2
    if [[ -e "$DIR/NSLOTS" ]]; then v=$(tr -d ' \t\r\n' < "$DIR/NSLOTS"); fi
    [[ "$v" =~ ^[1-8]$ ]] || { echo "[slot.sh] REFUSED: $DIR/NSLOTS must hold an integer 1..8, not '$v'" >&2; exit 2; }
    NSLOTS=$v
}

load1() { cut -d' ' -f1 "$LOADAVG"; }
at_limit() { awk -v l="$LIMIT" '{exit !($1>=l)}' "$LOADAVG"; }

admit_log() {   # <slot N | remote>
    local c="${CMD[*]}" tag=""
    [[ -z "${JNEXT_SLOT_NO_OVERFLOW:-}${REMOTE_RUN_FALLBACK:-}" ]] || tag=" fallback"   # the local half of a remote run that fell back
    printf '%s %s%s waited=%ss load=%s %s\n' "$(date +%Y-%m-%dT%H:%M:%S)" "$1" "$tag" "$((SECONDS - T0))" "$(load1)" \
        "${c:0:120}" >> "$DIR/admissions.log"
}

# try_local: one pass over the slots; on success sets GOT (slot number) and the open lock descriptor LOCKFD.
try_local() {
    local i
    GOT=
    read_nslots
    for i in $(seq 1 "$NSLOTS"); do
        exec {LOCKFD}>"$DIR/slot$i"
        if flock -n "$LOCKFD"; then
            if at_limit; then flock -u "$LOCKFD"; exec {LOCKFD}>&-; return 1; fi
            GOT=$i; return 0
        fi
        exec {LOCKFD}>&-
    done
    return 1
}

# parse_overflow: sets OV_REASON ("" = eligible), OV_SHAPE (1 = the command names a heavy target), OV_LONG, OV_WT,
# OV_BRANCH and the array OV_TARGETS (the remote-run targets).
parse_overflow() {
    OV_REASON=""; OV_SHAPE=0; OV_LONG=0; OV_WT=""; OV_BRANCH=""; OV_TARGETS=()
    if [[ -n "${JNEXT_SLOT_NO_OVERFLOW:-}" || -n "${REMOTE_RUN_FALLBACK:-}" ]]; then OV_REASON=SILENT; return; fi
    local a=("${CMD[@]}") i=0 w words=() vars=() targets=() spaced=0
    for w in "${a[@]}"; do [[ "$LONG_TARGETS$SHORT_TARGETS" != *" $w "* ]] || OV_SHAPE=1; done   # names a heavy target: the reason is worth printing
    if [[ "${a[0]}" == env ]]; then
        i=1
        while [[ "${a[i]:-}" == *=* ]]; do
            [[ "${a[i]}" == LANG=C ]] || { OV_REASON="environment word '${a[i]}' cannot be sent to the remote"; return; }
            i=$((i + 1))
        done
    fi
    [[ "${a[i]:-}" == make && "${a[i + 1]:-}" == -C && -n "${a[i + 2]:-}" ]] \
        || { OV_REASON="not of the form: make -C <worktree> <heavy targets>"; return; }
    OV_WT=${a[i + 2]}
    words=("${a[@]:i + 3}")
    for w in "${words[@]}"; do
        if [[ "$LONG_TARGETS$SHORT_TARGETS" == *" $w "* ]]; then
            targets+=("$w")
            [[ "$LONG_TARGETS" != *" $w "* ]] || OV_LONG=1
        elif [[ "$w" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
            vars+=("$w")
            [[ "$w" != *[[:space:]]* ]] || spaced=1
        else
            OV_REASON="'$w' is not a heavy target or a make variable"; return
        fi
    done
    [[ ${#targets[@]} -gt 0 ]] || { OV_REASON="no heavy target in the command"; return; }
    [[ -n "${JNEXT_REMOTE_RUN_HOST:-}" ]] || { OV_REASON="JNEXT_REMOTE_RUN_HOST is not set"; return; }
    [[ -d "$OV_WT" ]] || { OV_REASON="'$OV_WT' is not a directory"; return; }
    OV_BRANCH=$(git -C "$OV_WT" symbolic-ref --short -q HEAD 2>/dev/null) \
        || { OV_REASON="detached HEAD (only an issue branch gh<N>-<slug> is sent)"; return; }
    [[ "$OV_BRANCH" =~ ^gh[0-9]+-[A-Za-z0-9._-]+$ ]] \
        || { OV_REASON="branch '$OV_BRANCH' is not an issue branch gh<N>-<slug>"; return; }
    local st
    st=$(git -C "$OV_WT" status --porcelain 2>/dev/null) || { OV_REASON="git status failed in '$OV_WT'"; return; }
    [[ -z "$st" ]] || { OV_REASON="the worktree has uncommitted or untracked files (the remote runs committed state)"; return; }
    if [[ $spaced -eq 1 ]]; then
        local q="make" x
        for x in "${words[@]}"; do q+=" $(printf '%q' "$x")"; done
        OV_TARGETS=("sh:$q")
    else
        for w in "${targets[@]}"; do OV_TARGETS+=("$w${vars[*]:+ ${vars[*]}}"); done
    fi
}

# go_remote: push if needed, run the driver. Returns its status; 99 = nothing ran, run it here.
go_remote() {
    local sha id rc pid
    sha=$(git -C "$OV_WT" rev-parse HEAD) || return 99
    if [[ "$(git -C "$OV_WT" rev-parse --verify -q "refs/remotes/origin/$OV_BRANCH" 2>/dev/null)" != "$sha" ]]; then
        echo "[slot.sh] pushing $OV_BRANCH to origin for the remote run" >&2
        JNEXT_ALLOW_PUSH=1 git -C "$OV_WT" push origin "$OV_BRANCH" >&2 \
            || { echo "[slot.sh] push failed; running here" >&2; return 99; }
    fi
    id="${OV_BRANCH//\//-}-$(date +%H%M)-$$"
    admit_log remote
    echo "[slot.sh] sending to the remote host ($JNEXT_REMOTE_RUN_HOST), run id $id, log $LOGDIR/$id.log: ${OV_TARGETS[*]}" >&2
    env JNEXT_SLOT_NO_OVERFLOW=1 REMOTE_RUN_LOCAL_WRAP="$SELF --dir $DIR" \
        "$RR" --stream --fallback-local "$OV_WT" --keep-going --id "$id" --log-dir "$LOGDIR" \
        "$OV_BRANCH" "$sha" "${OV_TARGETS[@]}" &
    pid=$!
    trap 'kill -TERM "$pid" 2>/dev/null' INT TERM
    wait "$pid"; rc=$?
    while kill -0 "$pid" 2>/dev/null; do wait "$pid"; rc=$?; done
    trap - INT TERM
    [[ $rc -eq 0 ]] || paused     # PAUSED set while the run waited: say 75, not the driver's folded 1
    if [[ $rc -eq 3 ]]; then echo "[slot.sh] the remote refused (the push did not land); running here" >&2; return 99; fi
    return "$rc"
}

parse_overflow
announced=0
announce() {
    [[ $announced -eq 0 && -n "$OV_REASON" && "$OV_REASON" != SILENT ]] || return 0
    announced=1
    echo "[slot.sh] not sent to the remote: $OV_REASON" >&2
}

paused
if [[ -z "$OV_REASON" && $OV_LONG -eq 1 ]]; then
    go_remote; rc=$?
    [[ $rc -eq 99 ]] || exit "$rc"
    OV_REASON=SILENT
fi
[[ $OV_SHAPE -eq 0 || -z "$OV_REASON" ]] || announce

first=1
while :; do
    paused
    if try_local; then break; fi
    if [[ $first -eq 1 ]]; then
        first=0
        announce
        if [[ -z "$OV_REASON" ]]; then
            go_remote; rc=$?
            [[ $rc -eq 99 ]] || exit "$rc"
            OV_REASON=SILENT
        fi
    fi
    sleep "$POLL"
done

admit_log "slot $GOT"
echo "[slot.sh] admitted to slot $GOT at load $(load1): ${CMD[*]}" >&2
nice -n 19 "${CMD[@]}" {LOCKFD}>&- &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null' INT TERM
wait "$pid"; rc=$?
while kill -0 "$pid" 2>/dev/null; do wait "$pid"; rc=$?; done   # a TERM returns wait at once: keep the slot until the command is gone
flock -u "$LOCKFD"; exec {LOCKFD}>&-
exit "$rc"
