#!/usr/bin/env bash
# slot.sh: build/test admission for the agents of an autonomous run (GH #547 stage 4), with
# automatic overflow of heavy runs to the remote-run host.
#
#   slot.sh --dir <run>/slots [--limit N] [--patch] <command> [args...]
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
# THREAD CAPS (owner, 2026-11-23, measured: see apply_thread_caps)
#   A command admitted to a local slot runs with nproc/NSLOTS threads (at least 1): JOBS (as a make variable
#   through MAKEFLAGS), JNEXT_UNIT_TEST_JOBS and JNEXT_TEST_JOBS, unless the caller set that one itself. One
#   line at admission names the caps. Runs sent to the remote are untouched (the driver caps them there).
#
# REMOTE FIRST (owner, 2026-11-23: "Remote work rule: try to fill remote workers before running builds
# locally, unless needed"; before it only LONG runs went first and short ones went when no local slot was free)
#   A command is ELIGIBLE when ALL of these hold (the first that fails is printed, once, and the
#   command waits for a local slot as before):
#     1. JNEXT_SLOT_NO_OVERFLOW and REMOTE_RUN_FALLBACK are unset (a run the driver fell back from is
#        never sent back: this is also how the driver's own fallback through this script ends);
#     2. it is  [env LANG=C] make -C <worktree> <word>...  where every word is a heavy target or a
#        make VAR=value; at least one target; no other make option, no other environment word;
#     3. JNEXT_REMOTE_RUN_HOSTS (a space-separated list of ssh aliases, in preference order) or
#        JNEXT_REMOTE_RUN_HOST (one alias) is set;
#     4. the worktree's branch is an issue branch gh<N>-<slug> (never main, epic*, gate-*, detached);
#     5. `git status --porcelain` there is empty, untracked files included: the remote runs only
#        committed state, so an edit-and-test cycle stays local (a mutant is a --patch run: see PATCH RUNS).
#   An eligible command (any heavy target: the full unit-test targets, wasm-browser-test, the full
#   regression, regression-win/-wasm, regression-rows, test-affected) is sent to a remote lane FIRST, whenever
#   one is free right now, and runs here only when every lane of every host is busy or unusable (then it
#   waits for a local slot, as below). A command that is not eligible stays local exactly as before.
#   "Unless needed" (the owner's words): a run that needs THIS host sets JNEXT_SLOT_NO_OVERFLOW=1: uncommitted state
#   other than a --patch run, bench or xvfb probes, a measurement, or material that must never leave this host (a
#   reporter's game shared for testing, the user's own card; slot.sh cannot detect it). Never set it for a gate run.
#   Sending =
#   push origin/<branch> if it is not at HEAD (`git push origin <branch>`, never forced; COMMON-RULES
#   "REMOTE RUNS" authorizes an agent to push its own issue branch), then remote-run.sh --stream.
#   HOSTS (GH #547, owner 2026-11-22: several remote hosts): the aliases are tried in list order; a host
#   is tried only when `remote-run.sh --probe` finds a free lane on it right now (one ssh round trip: a busy
#   or unreachable host costs a moment, never the driver's 30 s LANE_WAIT), so a run goes to whichever
#   host has a free lane and never queues while a lane anywhere is free. The driver is called with that
#   host's alias in its environment (one host per call; the work directory name is the same on every host,
#   each expands its own `~`). A driver status 75 (nothing ran: the host refused after the probe) moves to
#   the next host; a run that started and failed is never retried elsewhere. Every host busy or unusable:
#   the run waits for a local slot (live output), and WHILE it waits the hosts are probed again, quietly, every
#   JNEXT_SLOT_REPROBE-th poll (default 3, so about a minute): a lane that frees up takes the run, and a lane
#   anywhere is never ignored for the length of the local queue. PAUSED is checked before every poll and re-probe. The LAST host of the list is called with
#   --fallback-local and this script as REMOTE_RUN_LOCAL_WRAP, so a lane lost between its probe and the
#   run is taken over here as with one host: the driver runs the targets here through this
#   script, with overflow switched off (no live output reaches the caller meanwhile: the driver writes a
#   fallback run to its log only, so a long wait looks silent until the summary), so the run waits for a local slot and never loops. The exit
#   status is the driver's, except 75 when <dir>/PAUSED appears meanwhile (0 green, 1 not clean or failed, 124 timeout, 2 usage). Exit 3 (the push did
#   not land) ran nothing, so it is run locally instead; 75 never reaches here (the fallback took it).
#
# PATCH RUNS (GH #565: mutants on the remote hosts). `--patch` says the command runs a MUTANT: an uncommitted edit in a
#   worktree whose HEAD is a pushed issue-branch commit (detached or not). It must be  [env LANG=C] make -C <worktree>
#   <words>  and the worktree must carry a change (else exit 2 `PATCH REFUSED`, whatever else applies: no host,
#   JNEXT_SLOT_NO_OVERFLOW, a busy host: a run that says it is a mutant runs a mutant or nothing; the unmutated tree's
#   pass would read as a survivor). Eligibility then replaces conditions 4 and 5: exactly one `gh<N>-<slug>` branch of
#   origin (refs/remotes/origin) points at HEAD, and `remote-run.sh --make-patch <worktree>` succeeds (it refuses a
#   changed submodule, an embedded repository, no change). The patch is made when the first host is free, kept under
#   <dir>/patches/ and removed when the driver returns. Patch mode NEVER pushes (a reviewer may not push, a detached HEAD
#   has no branch). The driver gets --patch <file>: it proves the patch applied on the remote or exits 2 (`PATCH
#   REFUSED`: no verdict, nothing runs here either), 3 (a stale tracking ref) runs the command here in the worktree
#   (which IS the mutant), 75 tries the next host, the last host's --fallback-local runs here after a tree check. Not
#   eligible: waits for a local slot and runs in the worktree, as before. --patch is a flag, not an environment
#   variable: it never reaches the driver's nested fallback call through REMOTE_RUN_LOCAL_WRAP.
#
# The remote host comes from the environment this script runs in: the run's wrapper must export
# JNEXT_REMOTE_RUN_HOSTS="<alias> <alias>" (or JNEXT_REMOTE_RUN_HOST=<alias>) and
# JNEXT_REMOTE_RUN_DIR=<remote work directory> (see the
# autonomous-run skill), or every heavy command stays local and says why.
#
# Every admission appends one line to <dir>/admissions.log:
#   <ISO time> <slot N | remote <alias>> waited=<s>s load=<1-min> <command, first 120 characters>
#
# Test hooks (the contract, test/packaging/slot-test.sh): JNEXT_SLOT_LOADAVG (a file in /proc/loadavg
# format), JNEXT_SLOT_REMOTE_RUN (the driver), JNEXT_SLOT_POLL (seconds between local attempts, default 20),
# JNEXT_SLOT_REMOTE_LOG_DIR (default ~/tmp/remote-run), JNEXT_SLOT_GIT_SEC (bound on the branch push and the
# ls-remote check after it, default 60), JNEXT_SLOT_CPUS (the CPU count the thread caps divide, default nproc).
set -u

SELF=$(readlink -f "${BASH_SOURCE[0]}")
HERE=$(dirname "$SELF")
RR=${JNEXT_SLOT_REMOTE_RUN:-$HERE/../remote-run/remote-run.sh}
LOADAVG=${JNEXT_SLOT_LOADAVG:-/proc/loadavg}
POLL=${JNEXT_SLOT_POLL:-20}
GIT_SEC=${JNEXT_SLOT_GIT_SEC:-60}
LOGDIR=${JNEXT_SLOT_REMOTE_LOG_DIR:-$HOME/tmp/remote-run}
LIMIT=${JNEXT_SLOT_LOAD_LIMIT:-$(nproc 2>/dev/null || echo 12)}
DIR=${JNEXT_SLOT_DIR:-}

# Targets worth sending (the remote-run skill's D14 list, plus the full regression, regression-rows and test-affected,
# plus every target whose prerequisites include the PGO build gui-release: SLOT-68 derives that set from the Makefile).
HEAVY_TARGETS=" unit-test unit-test-sdl unit-test-win unit-test-wasm wasm-browser-test regression regression-win regression-wasm regression-rows test-affected harness-selftest regression-confirm fuse-pgo gui-release gui-release-run "

usage() { echo "usage: slot.sh --dir <slots dir> [--limit N] [--patch] <command> [args...]" >&2; exit 2; }
PATCH=0
PATCH_FILE=""
trap '[[ -z "$PATCH_FILE" ]] || rm -f "$PATCH_FILE"' EXIT
while [[ $# -gt 0 ]]; do
    case $1 in
        --patch) PATCH=1; shift ;;
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

# parse_overflow: sets OV_REASON ("" = eligible), OV_SHAPE (1 = the command names a heavy target), OV_WT,
# OV_BRANCH and the array OV_TARGETS (the remote-run targets).
parse_overflow() {
    OV_REASON=""; OV_SHAPE=0; OV_WT=""; OV_BRANCH=""; OV_TARGETS=()
    if [[ -n "${JNEXT_SLOT_NO_OVERFLOW:-}" || -n "${REMOTE_RUN_FALLBACK:-}" ]]; then OV_REASON=SILENT; return; fi
    local a=("${CMD[@]}") i=0 w words=() vars=() targets=() spaced=0
    for w in "${a[@]}"; do [[ "$HEAVY_TARGETS" != *" $w "* ]] || OV_SHAPE=1; done   # names a heavy target: the reason is worth printing
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
        if [[ "$HEAVY_TARGETS" == *" $w "* ]]; then
            targets+=("$w")
        elif [[ "$w" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
            vars+=("$w")
            [[ "$w" != *[[:space:]]* ]] || spaced=1
        else
            OV_REASON="'$w' is not a heavy target or a make variable"; return
        fi
    done
    [[ ${#targets[@]} -gt 0 ]] || { OV_REASON="no heavy target in the command"; return; }
    HOSTS=(${JNEXT_REMOTE_RUN_HOSTS:-${JNEXT_REMOTE_RUN_HOST:-}})
    [[ ${#HOSTS[@]} -gt 0 ]] || { OV_REASON="JNEXT_REMOTE_RUN_HOST is not set (nor JNEXT_REMOTE_RUN_HOSTS)"; return; }
    [[ -d "$OV_WT" ]] || { OV_REASON="'$OV_WT' is not a directory"; return; }
    if [[ $PATCH -eq 1 ]]; then
        # 4p: the base is the ONE issue branch of origin at HEAD (attached or detached alike); the change travels as a patch
        local refs b n=0 names=""
        refs=$(git -C "$OV_WT" for-each-ref --points-at HEAD --format='%(refname:strip=3)' refs/remotes/origin 2>/dev/null) || refs=""
        while IFS= read -r b; do
            [[ "$b" =~ ^gh[0-9]+-[A-Za-z0-9._-]+$ ]] || continue
            n=$((n + 1)); names+="${names:+ }$b"; OV_BRANCH=$b
        done <<<"$refs"
        [[ $n -ne 0 ]] || { OV_BRANCH=""; OV_REASON="no issue branch on origin is at HEAD (push it first)"; return; }
        [[ $n -eq 1 ]] || { OV_BRANCH=""; OV_REASON="several issue branches on origin are at HEAD ($names): the base is ambiguous"; return; }
    else
        OV_BRANCH=$(git -C "$OV_WT" symbolic-ref --short -q HEAD 2>/dev/null) \
            || { OV_REASON="detached HEAD (only an issue branch gh<N>-<slug> is sent)"; return; }
        [[ "$OV_BRANCH" =~ ^gh[0-9]+-[A-Za-z0-9._-]+$ ]] \
            || { OV_REASON="branch '$OV_BRANCH' is not an issue branch gh<N>-<slug>"; return; }
        local st
        st=$(git -C "$OV_WT" status --porcelain 2>/dev/null) || { OV_REASON="git status failed in '$OV_WT'"; return; }
        [[ -z "$st" ]] || { OV_REASON="the worktree has uncommitted or untracked files (the remote runs committed state)"; return; }
    fi
    if [[ $spaced -eq 1 ]]; then
        local q="make" x
        for x in "${words[@]}"; do q+=" $(printf '%q' "$x")"; done
        OV_TARGETS=("sh:$q")
    else
        for w in "${targets[@]}"; do OV_TARGETS+=("$w${vars[*]:+ ${vars[*]}}"); done
    fi
}

# go_remote: try the remote hosts in order. Returns the driver's status; 99 = nothing ran, run it here.
# A host is tried only when `remote-run.sh --probe` finds a free lane on it (one ssh round trip, so a busy or
# unreachable host costs a moment, not the driver's 30 s LANE_WAIT). The LAST host of the list gets
# --fallback-local (a lane taken between the probe and the run, or any other 75, runs the targets here through
# this script, as with one host); an earlier host does not: its 75 (nothing ran) moves on to the next host.
# Exit 3 (the push did not land: the same on every host) and any status of a run that started end the search.
# go_remote [1]: with 1 (a re-probe from the wait loop) the probes print nothing. Returns 99 when no host took the
# run (all busy or unusable: the caller keeps waiting and probes again later) and 98 when remote is out for good
# (the push failed, the remote refused, the commit cannot be read).
go_remote() {
    local quiet=${1:-0} sha id rc pid h i n=${#HOSTS[@]} pushed=0 fb pa=()
    [[ $PATCH -eq 0 ]] || pushed=1    # patch mode never pushes
    sha=$(git -C "$OV_WT" rev-parse HEAD) || return 98
    id="${OV_BRANCH//\//-}-$(date +%H%M)-$$"
    for ((i = 0; i < n; i++)); do
        h=${HOSTS[i]}
        if [[ $quiet -eq 1 ]]; then
            env JNEXT_REMOTE_RUN_HOST="$h" "$RR" --probe 2>/dev/null || continue
        else
            env JNEXT_REMOTE_RUN_HOST="$h" "$RR" --probe >&2 || { echo "[slot.sh] remote host $h: no free lane (or unusable)" >&2; continue; }
        fi
        if [[ $pushed -eq 0 ]]; then
            if [[ "$(git -C "$OV_WT" rev-parse --verify -q "refs/remotes/origin/$OV_BRANCH" 2>/dev/null)" != "$sha" ]]; then
                echo "[slot.sh] pushing $OV_BRANCH to origin for the remote run" >&2
                # A concurrent slot.sh on the same branch pushes the same sha: the loser is rejected
                # ("incorrect old value") although origin is now at $sha, so a failed push counts when it is.
                # Both network calls are bounded: a stalled one would hang this script and its waiting agent.
                JNEXT_ALLOW_PUSH=1 timeout --kill-after=5s "${GIT_SEC}s" git -C "$OV_WT" push origin "$OV_BRANCH" >&2 \
                    || [[ "$(timeout --kill-after=5s "${GIT_SEC}s" git -C "$OV_WT" ls-remote origin "refs/heads/$OV_BRANCH" 2>/dev/null | cut -f1)" == "$sha" ]] \
                    || { echo "[slot.sh] push failed; running here" >&2; return 98; }
            fi
            pushed=1
        fi
        fb=(); [[ $i -ne $((n - 1)) ]] || fb=(--fallback-local "$OV_WT")
        if [[ $PATCH -eq 1 && -z "$PATCH_FILE" ]]; then
            # made once, when a host is free (the tree is quiet now); a worktree that cannot be carried runs here
            local pn perr
            pn=${OV_WT%/}; pn=${pn##*/}; pn=${pn:0:60}; pn=${pn//[^A-Za-z0-9._-]/_}
            mkdir -p "$DIR/patches"
            PATCH_FILE="$DIR/patches/$pn-$$.patch"
            if ! perr=$("$RR" --make-patch "$OV_WT" 2>&1 >"$PATCH_FILE"); then
                rm -f "$PATCH_FILE"; PATCH_FILE=""
                echo "[slot.sh] not sent to the remote: ${perr#remote-run: }" >&2
                return 98
            fi
        fi
        pa=(); [[ $PATCH -eq 0 ]] || pa=(--patch "$PATCH_FILE")
        admit_log "remote $h"
        echo "[slot.sh] sending to the remote host ($h), run id $id, log $LOGDIR/$id.log: ${OV_TARGETS[*]}" >&2
        env JNEXT_REMOTE_RUN_HOST="$h" JNEXT_SLOT_NO_OVERFLOW=1 REMOTE_RUN_LOCAL_WRAP="$SELF --dir $DIR" REMOTE_RUN_BASE_REPO="$OV_WT" \
            "$RR" --stream "${fb[@]}" "${pa[@]}" --keep-going --id "$id" --log-dir "$LOGDIR" \
            "$OV_BRANCH" "$sha" "${OV_TARGETS[@]}" &
        pid=$!
        trap 'kill -TERM "$pid" 2>/dev/null' INT TERM
        wait "$pid"; rc=$?
        while kill -0 "$pid" 2>/dev/null; do wait "$pid"; rc=$?; done
        trap - INT TERM

        [[ $rc -eq 0 ]] || paused     # PAUSED set while the run waited: say 75, not the driver's folded 1
        if [[ $rc -eq 3 ]]; then echo "[slot.sh] the remote refused (the push did not land); running here" >&2; return 98; fi
        if [[ $rc -eq 75 ]]; then echo "[slot.sh] remote host $h could not be used (nothing ran)" >&2; continue; fi
        return "$rc"
    done
    return 99
}

# apply_thread_caps: a run admitted HERE gets nproc/NSLOTS threads (at least 1), always, whether or not the
# other slots are busy (owner, 2026-11-23; measured on the 12-CPU host, `make unit-test` from an empty build/ with
# a warm ccache: two concurrent runs took 617 s and 519 s at 12 threads each and 496 s and 491 s at 6 each; one
# run alone 347 s at 12 and 343 s at 6, so the cap costs a lone run nothing; NSLOTS above 2 is an extrapolation, measured only at 1 and 2 slots: nproc/NSLOTS
# keeps the total at nproc, and oversubscription is what cost the 12-thread pair). Three knobs, each one the variable
# its consumer really reads:
#   JOBS                  the Makefile assigns it with `:=`, so an ENVIRONMENT JOBS is ignored; it goes in as a
#                         make command-line variable through MAKEFLAGS, whatever the command (make behind timeout, bash -c, env: a
#                         non-make program ignores it; a `JOBS=` word after the make word wins over it, and the
#                         argv stays as the caller wrote it);
#   JNEXT_UNIT_TEST_JOBS  suites running at once in test/run-unit-tests.sh (the wasm and win targets set it
#                         from JOBS themselves);
#   JNEXT_TEST_JOBS       lanes of test/00regression/regression.sh.
# A value the caller already set wins (JOBS: on the make command line, in MAKEFLAGS, or in the environment, where
# its value is passed on to make); the log line says which. NOT capped, on purpose: JNEXT_TEST_QUIET_JOBS (the
# quiet rows are real-time paced; more lanes is the trade CLAUDE.md rejects), UNIT_TEST_CHECK_JOBS (6, the cap
# of light checks that was in force in the measurement above) and the remote driver's lower LTO/PGO link cap
# (a memory cap of its 10 GB lanes: with 2+ slots nproc/NSLOTS is already that low here, and the host kept 39 GB free; with 1 slot a heavy target gets every CPU, as the lone run measured).
# Remote runs never reach this: the driver sets its own caps on the remote host.
apply_thread_caps() {
    local cpus=${JNEXT_SLOT_CPUS:-$(nproc 2>/dev/null || echo 12)} cap w note="" jobs mf=${MAKEFLAGS:-}
    [[ "$cpus" =~ ^[0-9]+$ ]] || cpus=12
    cap=$((cpus / NSLOTS)); [[ $cap -ge 1 ]] || cap=1
    # Where is make? The first word that is make or ends in /make (so `timeout .. make`, `/usr/bin/make`, `env -u X
    # make` count; `bash -c 'make ..'` has no such word and is treated as "no make word"). A JOBS= word AFTER it is
    # on make's command line (it beats MAKEFLAGS); one BEFORE it, or anywhere when there is no make word, is an
    # environment word (`env JOBS=3 make`), which the Makefile ignores, so its value is handed on like an env JOBS.
    local mi=-1 i=0 cmdjobs=0 prejobs=""
    for w in "${CMD[@]}"; do
        if [[ $mi -lt 0 && ( "$w" == make || "$w" == */make ) ]]; then mi=$i
        elif [[ "$w" == JOBS=* ]]; then
            if [[ $mi -ge 0 ]]; then cmdjobs=1; else prejobs=${w#JOBS=}; fi
        fi
        i=$((i + 1))
    done
    if [[ $cmdjobs -eq 1 ]]; then jobs="caller's command line"
    elif [[ " $mf" == *" JOBS="* ]]; then jobs="caller's MAKEFLAGS"
    elif [[ -n "$prejobs" ]]; then export MAKEFLAGS="$mf JOBS=$prejobs"; jobs="$prejobs (caller's env word)"
    elif [[ -n "${JOBS:-}" ]]; then export MAKEFLAGS="$mf JOBS=$JOBS"; jobs="$JOBS (caller's environment)"
    else export MAKEFLAGS="$mf JOBS=$cap"; jobs=$cap
    fi
    for w in JNEXT_UNIT_TEST_JOBS JNEXT_TEST_JOBS; do
        if [[ -n "${!w:+x}" ]]; then note+=" $w=${!w} (caller's)"
        else export "$w=$cap"; note+=" $w=$cap"
        fi
    done
    echo "[slot.sh] thread caps (nproc $cpus / NSLOTS $NSLOTS = $cap): JOBS=$jobs$note" >&2
}

# patch_guard: --patch only ever runs a mutant or nothing. The command must be [env LANG=C] make -C <worktree> ...
# and the worktree must carry a change: with none, the run would test the unmutated tree and its pass would read as
# a survivor. This holds whatever else applies (no host, JNEXT_SLOT_NO_OVERFLOW, busy hosts).
patch_refused() { echo "[slot.sh] PATCH REFUSED: $*" >&2; exit 2; }
patch_guard() {
    local a=("${CMD[@]}") i=0 st
    if [[ "${a[0]}" == env ]]; then
        i=1
        while [[ "${a[i]:-}" == *=* ]]; do
            [[ "${a[i]}" == LANG=C ]] || patch_refused "--patch needs make -C <worktree> ...: the environment word '${a[i]}' cannot be sent"
            i=$((i + 1))
        done
    fi
    [[ "${a[i]:-}" == make && "${a[i + 1]:-}" == -C && -n "${a[i + 2]:-}" ]] \
        || patch_refused "--patch needs make -C <worktree> <words>, not '${CMD[*]:0:80}'"
    [[ -d "${a[i + 2]}" ]] || patch_refused "'${a[i + 2]}' is not a directory"
    st=$(git -C "${a[i + 2]}" status --porcelain 2>/dev/null) || patch_refused "git status failed in '${a[i + 2]}'"
    [[ -n "$st" ]] || patch_refused "'${a[i + 2]}' carries no change: a patch run would test the unmutated tree"
}

parse_overflow
announced=0
announce() {
    [[ $announced -eq 0 && -n "$OV_REASON" && "$OV_REASON" != SILENT ]] || return 0
    announced=1
    echo "[slot.sh] not sent to the remote: $OV_REASON" >&2
}

# remote_try [quiet]: one pass over the hosts. A run that happened ends this script with its status; 99 = no host
# had a lane (stay eligible, probe again), 98 = remote is out for good.
can_remote=0; [[ -z "$OV_REASON" ]] && can_remote=1
remote_try() {
    local rc
    go_remote "${1:-0}"; rc=$?
    case $rc in
        99) ;;
        98) can_remote=0 ;;
        *)  exit "$rc" ;;
    esac
}

paused
[[ $PATCH -eq 0 ]] || patch_guard
if [[ $can_remote -eq 1 ]]; then remote_try; fi   # remote first: every eligible command, long or short
[[ $OV_SHAPE -eq 0 || $can_remote -eq 1 || "$OV_REASON" == SILENT ]] || announce

# The wait for a local slot also watches the remote hosts (owner, 2026-11-22: never queue here while a remote lane
# could take the run): every REPROBE-th poll the hosts are probed again, quietly; a lane that freed up takes the run.
REPROBE=${JNEXT_SLOT_REPROBE:-3}
polls=0
while :; do
    paused
    if try_local; then break; fi
    announce
    if [[ $can_remote -eq 1 ]]; then
        polls=$((polls + 1))
        if (( polls % REPROBE == 0 )); then remote_try 1; fi
    fi
    sleep "$POLL"
done

admit_log "slot $GOT"
echo "[slot.sh] admitted to slot $GOT at load $(load1): ${CMD[*]}" >&2
apply_thread_caps
nice -n 19 "${CMD[@]}" {LOCKFD}>&- &
pid=$!
trap 'kill -TERM "$pid" 2>/dev/null' INT TERM
wait "$pid"; rc=$?
while kill -0 "$pid" 2>/dev/null; do wait "$pid"; rc=$?; done   # a TERM returns wait at once: keep the slot until the command is gone
flock -u "$LOCKFD"; exec {LOCKFD}>&-
exit "$rc"
