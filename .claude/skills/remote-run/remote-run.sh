#!/usr/bin/env bash
#
# remote-run.sh: run builds and tests on a remote host through rootless podman.
# Generic driver: it names no project; everything specific is in project.conf.
# See SKILL.md for when to use it and what it guarantees.
#
# Exit status: 0 green (only with "--- verdict: CLEAN" in the summary); 1 the run ran and failed, was not
# clean, or its connection was lost; 124 it timed out;
# 2 usage; 3 refused (the branch is not pushed, or is not at the expected sha);
# 75 the remote could not be used (alias unset, unreachable, preflight, no image): run locally.
# shellcheck disable=SC2016
set -euo pipefail
IFS=$'\n\t'

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CONF=${REMOTE_RUN_CONF:-$HERE/project.conf}

usage() {
    cat <<'USAGE'
usage: remote-run.sh [options] <branch> <sha> <target>...
       remote-run.sh [options] --gate <branch> <sha>
       remote-run.sh [options] --warm

  <branch>   a branch already pushed to the project's repository
  <sha>      its full 40-hex tip; the run is refused if the remote sees another commit
  <target>   a make target (words after it are passed to make), or sh:<command>;
             targets run one by one, in order, stopping at the first failure

options:
  --warm                 build the image, provision the tool caches and the SD image, and
                         build once to fill ccache (the project's WARM_TARGETS, on its default
                         branch); a normal run refuses until this has run for the current deps
  --gate                 run the batch gate (the project's GATE_STEPS, in order, failing fast) on
                         <branch> <sha>; the summary shows each step's own totals and the verdict
  --ref BRANCH           with --warm: the branch to warm (default: the project's default branch)
  --deps-ref BRANCH      take the dependency script (the image's package set) from BRANCH instead
                         of the commit under test; for --warm, --gate and plain runs. The log
                         names the ref that supplied it
  --timeout SECS         bound for the whole run (default from project.conf)
  --keep-going           run the remaining targets after a failure
  --stream               also print the log live (default: only the summary)
  --fallback-local DIR   on exit 75, run the targets in DIR on this host (the log says so);
                         REMOTE_RUN_LOCAL_WRAP, if set, is the wrapper command put in front
  --id ID                run id (default: a timestamp)
  --log-dir DIR          where logs go (default ~/tmp/remote-run); logs older than 7 days are deleted
  --local-shell          TEST SEAM: run the remote side as a local shell instead of over ssh
  --conf FILE            project configuration (default project.conf beside this script)
  -h, --help             this text

environment: the SSH alias and the remote work directory come from the variables named by
HOST_ENV_VAR and DIR_ENV_VAR in project.conf.
USAGE
}

log_info() { echo "[remote-run $(date '+%H:%M:%S')] $*" >&2; }
die()      { echo "remote-run: $*" >&2; exit "${2:-2}"; }

# ---- configuration ----------------------------------------------------------
load_conf() {
    [[ -f "$CONF" ]] || die "no project configuration: $CONF"
    # shellcheck source=/dev/null
    source "$CONF"
    local k v
    for k in PROJECT HOST_ENV_VAR DIR_ENV_VAR DIR_DEFAULT REPO_URL DEFAULT_BRANCH BASE_IMAGE DEPS_SCRIPT \
             DEPS_ARGS BOOTSTRAP_CMD CACHE_MOUNTS CONTAINER_ENV JOBS_DEFAULT JOBS_HEAVY HEAVY_TARGET_RE \
             TEST_JOBS CACHE_MIGRATE CACHE_SWEEP CONTAINER_MEMORY CONTAINER_SHM TIMEOUT_DEFAULT LANE_WAIT GATE_LANE_WAIT MIN_MEM_MB MIN_DISK_GB \
             HEAVY_TARGETS WARM_TARGETS GATE_STEPS TIMEOUT_GATE STAMP_ENV STAMP_LOCAL_DIR \
             EXTRA_REPO_URL EXTRA_REF_FILE EXTRA_REF_SED EXTRA_SPARSE EXTRA_MOUNT EXTRA_ENV; do
        v="REMOTE_RUN_$k"
        if [[ -n "${!v:-}" ]]; then printf -v "$k" '%s' "${!v}"; fi
        [[ -n "${!k:-}" || "$k" == CACHE_MIGRATE || "$k" == CACHE_SWEEP || "$k" == BOOTSTRAP_CMD || "$k" == STAMP_ENV || "$k" == CONTAINER_ENV || "$k" == EXTRA_* ]] \
            || die "project.conf does not set $k"
    done
}

# ---- summary ----------------------------------------------------------------
# summarize_log <log>: the per-target result lines, FAIL and SKIP lines, the last totals.
summarize_log() {
    local log=$1 clean
    clean=$(sed -e 's/\x1b\[[0-9;]*m//g' "$log")
    grep -aE 'dependency script' <<<"$clean" | sed -e 's/^\[remote-run [0-9:]*\] /deps: /' || true
    echo "--- targets"
    grep -E '^=== remote-run result:' <<<"$clean" || echo "(no target finished)"
    local bad nbad
    bad=$(grep -E '(^|[[:space:]])(FAIL|SKIP|SKIPPED)([[:space:]:]|$)' <<<"$clean" | grep -vE '^=== remote-run|^-- ' || true)
    nbad=0
    [[ -z "$bad" ]] || nbad=$(wc -l <<<"$bad")
    echo "--- FAIL / SKIP lines: $nbad"
    [[ -z "$bad" ]] || head -n 30 <<<"$bad"
    [[ "$nbad" -le 30 ]] || echo "... $((nbad - 30)) more in the log"
    grep -E '^=== remote-run oom: ' <<<"$clean" || true
    echo "--- totals per step"
    grep -E '^=== remote-run totals:' <<<"$clean" || echo "(no step printed a totals line)"
    echo "--- verdict: $(verdict_of_log "$log" "${NSTEPS:-}")"
}

# verdict_of_log <log> [steps]: CLEAN, or why not. Every step needs rc=0 and no totals line with a
# non-zero Failed/Fail or Skipped/Skip count; with <steps> (the number requested) exactly that many
# result lines must exist, so a step that never ran cannot pass unseen.
verdict_of_log() {
    local log=$1 want=${2:-} bad="" text results n tot
    text=$(cat "$log")
    results=$(grep -E '^=== remote-run result: ' <<<"$text" | grep -vE '^=== remote-run result: bootstrap ' || true)
    n=0
    [[ -z "$results" ]] || n=$(wc -l <<<"$results")
    if grep -qE '^=== remote-run result: .* rc=[1-9]' <<<"$text"; then bad+=" a step exited non-zero;"; fi
    tot=$(grep -E '^=== remote-run totals:' <<<"$text" || true)
    if grep -qE '(Failed|Fail):[[:space:]]*[1-9]|(Skipped|Skip):[[:space:]]*[1-9]' <<<"$tot"; then
        bad+=" a totals line shows failures or skips;"
    fi
    if grep -qE '^=== remote-run oom: ' <<<"$text"; then bad+=" the memory limit killed a process;"; fi
    [[ $n -gt 0 ]] || bad+=" no step finished;"
    if [[ -n "$want" && $n -gt 0 && $n -ne $want ]]; then bad+=" $want steps requested, $n reported;"; fi
    if [[ -z "$bad" ]]; then echo CLEAN; else echo "NOT CLEAN:${bad%;}"; fi
}

# ---- transport --------------------------------------------------------------
# build_payload <mode>: prelude of RR_* variables, then the remote side.
build_payload() {
    local mode=$1 targets=$2 f=$3
    {
        printf 'RR_MODE=%q\nRR_ID=%q\nRR_PROJECT=%q\nRR_BRANCH=%q\nRR_SHA=%q\n' "$mode" "$ID" "$PROJECT" "$BRANCH" "$SHA"
        printf 'RR_REPO_URL=%q\nRR_DIR=%q\nRR_BASE_IMAGE=%q\nRR_DEPS_SCRIPT=%q\nRR_DEPS_ARGS=%q\n' \
            "$REPO_URL" "$DIR" "$BASE_IMAGE" "$DEPS_SCRIPT" "$DEPS_ARGS"
        printf 'RR_BOOTSTRAP=%q\nRR_TARGETS=%q\nRR_KEEP_GOING=%q\nRR_TIMEOUT=%q\nRR_LANE_WAIT=%q\n' \
            "$BOOTSTRAP_CMD" "$targets" "$KEEP_GOING" "$TIMEOUT" "$LANE_WAIT"
        printf 'RR_MIN_MEM_MB=%q\nRR_MIN_DISK_GB=%q\nRR_MEMORY=%q\nRR_SHM=%q\nRR_TEST_JOBS=%q\n' \
            "$MIN_MEM_MB" "$MIN_DISK_GB" "$CONTAINER_MEMORY" "$CONTAINER_SHM" "$TEST_JOBS"
        printf 'RR_JOBS_DEFAULT=%q\nRR_JOBS_HEAVY=%q\nRR_HEAVY_RE=%q\nRR_CACHE_MOUNTS=%q\nRR_CACHE_MIGRATE=%q\nRR_CACHE_SWEEP=%q\nRR_CONTAINER_ENV=%q\nRR_STAMP_ENV=%q\n' \
            "$JOBS_DEFAULT" "$JOBS_HEAVY" "$HEAVY_TARGET_RE" "$CACHE_MOUNTS" "${CACHE_MIGRATE:-}" "${CACHE_SWEEP:-}" "$CONTAINER_ENV" "$STAMP_ENV"
        printf 'RR_DEPS_REF=%q\n' "$DEPS_REF"
        printf 'RR_EXTRA_URL=%q\nRR_EXTRA_REF_FILE=%q\nRR_EXTRA_REF_SED=%q\nRR_EXTRA_SPARSE=%q\nRR_EXTRA_MOUNT=%q\nRR_EXTRA_ENV=%q\n' \
            "${EXTRA_REPO_URL:-}" "${EXTRA_REF_FILE:-}" "${EXTRA_REF_SED:-}" "${EXTRA_SPARSE:-}" "${EXTRA_MOUNT:-}" "${EXTRA_ENV:-}"
        printf 'RR_INNER=%q\n' "$(cat "$HERE/container-side.sh")"
        cat "$HERE/remote-side.sh"
    } > "$f"
}

# transport_cmd: set TCMD to the command that runs a script read from stdin on the remote.
transport_cmd() {
    if [[ $LOCAL_SHELL -eq 1 ]]; then
        # shellcheck disable=SC2016
        TCMD=(bash -c 'exec bash -c "$(cat)"')
    else
        TCMD=(ssh -o BatchMode=yes -o ClearAllForwardings=yes -o ConnectTimeout=15 -o ServerAliveInterval=15 -o ServerAliveCountMax=4
              "$HOST" 'bash -c "$(cat)"')
    fi
}

# ---- local fallback ---------------------------------------------------------
run_local() {
    local dir=$1 targets=$2 t rc overall=0 wrap=() words
    [[ -z "${REMOTE_RUN_LOCAL_WRAP:-}" ]] || IFS=' ' read -ra wrap <<<"$REMOTE_RUN_LOCAL_WRAP"
    echo "=== remote-run RAN LOCALLY in $dir (the remote could not be used)" | tee -a "$LOG" >&2
    while IFS= read -r t <&3; do
        [[ -n "$t" ]] || continue
        echo "=== remote-run target: $t" >> "$LOG"
        rc=0
        if [[ "$t" == sh:* ]]; then
            "${wrap[@]}" env LANG=C bash -c "cd '$dir' && ${t#sh:}" </dev/null >> "$LOG" 2>&1 || rc=$?
        else
            IFS=' ' read -ra words <<<"$t"
            "${wrap[@]}" env LANG=C make -C "$dir" "${words[@]}" </dev/null >> "$LOG" 2>&1 || rc=$?
        fi
        echo "=== remote-run result: $t rc=$rc time=?s peak=?MiB" >> "$LOG"
        if [[ $rc -ne 0 ]]; then
            overall=1
            [[ $KEEP_GOING -eq 1 ]] || break
        fi
    done 3<<<"$targets"
    return "$overall"
}

# ---- stamps -----------------------------------------------------------------
fetch_stamps() {
    local dest tmp n
    dest=$(eval "echo \"$STAMP_LOCAL_DIR\"")
    tmp=$(mktemp -d)
    if "${TCMD[@]}" < "$PAYDIR/stamps" 2>/dev/null | tar -x -C "$tmp" 2>/dev/null; then
        n=$(find "$tmp" -maxdepth 1 -type f | wc -l)
        if [[ $n -gt 0 ]]; then
            mkdir -p "$dest"
            find "$tmp" -maxdepth 1 -type f -exec cp -f {} "$dest"/ \;
            log_info "copied $n stamp file(s) to $dest"
            echo "=== remote-run stamps copied: $n" >> "$LOG"
        fi
    fi
    rm -rf "$tmp"
}

remote_cleanup() {
    "${TCMD[@]}" < "$PAYDIR/cleanup" >/dev/null 2>&1 || true
}

# ---- main -------------------------------------------------------------------
main() {
    local warm=0 gate=0 stream=0 warm_ref="" fallback_dir="" targets="" logdir="$HOME/tmp/remote-run"
    TIMEOUT="" DEPS_REF="" KEEP_GOING=0 LOCAL_SHELL=0 ID="" BRANCH="" SHA=""
    local -a pos=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) usage; exit 0 ;;
            --warm) warm=1 ;;
            --gate) gate=1 ;;
            --keep-going) KEEP_GOING=1 ;;
            --stream) stream=1 ;;
            --local-shell) LOCAL_SHELL=1 ;;
            --ref)      [[ $# -ge 2 ]] || { usage >&2; exit 2; }; warm_ref=$2; shift ;;
            --deps-ref) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; DEPS_REF=$2; shift ;;
            --timeout)  [[ $# -ge 2 ]] || { usage >&2; exit 2; }; TIMEOUT=$2; shift ;;
            --id)       [[ $# -ge 2 ]] || { usage >&2; exit 2; }; ID=$2; shift ;;
            --log-dir)  [[ $# -ge 2 ]] || { usage >&2; exit 2; }; logdir=$2; shift ;;
            --fallback-local) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; fallback_dir=$2; shift ;;
            --conf)     [[ $# -ge 2 ]] || { usage >&2; exit 2; }; CONF=$2; shift ;;
            --) shift; pos+=("$@"); break ;;
            -*) echo "remote-run: unknown option '$1'" >&2; usage >&2; exit 2 ;;
            *)  pos+=("$1") ;;
        esac
        shift
    done
    load_conf
    [[ $warm -eq 0 || $gate -eq 0 ]] || die "--warm and --gate exclude each other"
    [[ -z "$warm_ref" || $warm -eq 1 ]] || die "--ref is for --warm only"
    [[ -z "$DEPS_REF" || "$DEPS_REF" =~ ^[A-Za-z0-9._][A-Za-z0-9._/-]*$ ]] || die "bad --deps-ref '$DEPS_REF'"

    if [[ $warm -eq 1 ]]; then
        [[ ${#pos[@]} -eq 0 ]] || die "--warm takes no branch, sha or target"
        BRANCH=${warm_ref:-$DEFAULT_BRANCH}
        targets=$(tr ' ' '\n' <<<"$WARM_TARGETS")
    else
        if [[ $gate -eq 1 ]]; then
            [[ ${#pos[@]} -eq 2 ]] || { usage >&2; exit 2; }
            targets=$(tr '|' '\n' <<<"$GATE_STEPS")
            TIMEOUT=${TIMEOUT:-$TIMEOUT_GATE}
            LANE_WAIT=$GATE_LANE_WAIT
        else
            [[ ${#pos[@]} -ge 3 ]] || { usage >&2; exit 2; }
            targets=$(printf '%s\n' "${pos[@]:2}")
        fi
        BRANCH=${pos[0]}; SHA=${pos[1]}
        [[ "$SHA" =~ ^[0-9a-f]{40}$ ]] || die "the sha must be 40 lowercase hex digits (git rev-parse <branch>): '$SHA'"
    fi
    [[ "$BRANCH" =~ ^[A-Za-z0-9._][A-Za-z0-9._/-]*$ ]] || die "bad branch name '$BRANCH'"
    TIMEOUT=${TIMEOUT:-$TIMEOUT_DEFAULT}
    [[ "$TIMEOUT" =~ ^[0-9]+$ && "$TIMEOUT" -gt 0 ]] || die "--timeout needs a positive number of seconds"
    [[ -n "$ID" ]] || ID="$(date +%Y%m%d-%H%M%S)-$$"
    [[ "$ID" =~ ^[A-Za-z0-9._-]+$ ]] || die "bad run id '$ID'"

    local hostv=$HOST_ENV_VAR dirv=$DIR_ENV_VAR
    HOST=${!hostv:-}
    DIR=${!dirv:-$DIR_DEFAULT}

    mkdir -p "$logdir"
    find "$logdir" -maxdepth 1 -name '*.log' -mtime +7 -delete 2>/dev/null || true
    LOG="$logdir/$ID.log"
    : > "$LOG"
    log_info "run $ID: $BRANCH ${SHA:+@ ${SHA:0:12}}; log $LOG"

    if [[ $warm -eq 0 && $gate -eq 0 ]]; then
        local t
        while IFS= read -r t; do
            [[ "$t" == sh:* ]] || [[ " $HEAVY_TARGETS " == *" ${t%% *} "* ]] \
                || log_info "notice: '${t%% *}' is not a heavy target; a local run is usually faster"
        done <<<"$targets"
    fi

    local rc=0 state=remote
    INTERRUPTED=0
    transport_cmd
    if [[ $LOCAL_SHELL -eq 0 && -z "$HOST" ]]; then
        echo "=== remote-run fallback: $HOST_ENV_VAR is not set, remote runs are off" | tee -a "$LOG" >&2
        state=fallback
    fi

    NSTEPS=$(grep -c . <<<"$targets" || true)
    # All three payloads are built now, from one snapshot of the skill's files: editing them
    # while a run is live changes nothing for it.
    PAYDIR=$(mktemp -d)
    build_payload "$([[ $warm -eq 1 ]] && echo warm || echo run)" "$targets" "$PAYDIR/payload"
    build_payload stamps "" "$PAYDIR/stamps"
    build_payload cleanup "" "$PAYDIR/cleanup"

    if [[ $state == remote ]]; then
        local tmp pidf rcf
        tmp=$PAYDIR
        pidf=$tmp/pid; rcf=$tmp/rc
        # The wrapper subshell carries the transport; a TERM/INT to this script reaches it, then
        # the remote side's own traps kill the container and remove the checkout.
        trap 'INTERRUPTED=1; kill -TERM "$(cat "$pidf" 2>/dev/null)" 2>/dev/null || true' TERM INT
        set +e
        (
            echo "$BASHPID" > "$pidf"
            "${TCMD[@]}" < "$tmp/payload" & cpid=$!
            trap 'kill -TERM $cpid 2>/dev/null' TERM INT
            wait "$cpid"; r=$?
            while kill -0 "$cpid" 2>/dev/null; do wait "$cpid"; r=$?; done
            echo "$r" > "$rcf"
        ) 2>&1 | sed -u -e '/^\x01HB$/d' -e 's/\x01HB//' | if [[ $stream -eq 1 ]]; then tee "$LOG"; else cat > "$LOG"; fi &
        # Waited for in the background: a trap only runs between foreground commands.
        local jpid=$!
        wait "$jpid"
        while kill -0 "$jpid" 2>/dev/null; do wait "$jpid"; done
        set -e
        trap - TERM INT
        local tr_rc
        tr_rc=$(cat "$rcf" 2>/dev/null || echo 255)
        [[ $INTERRUPTED -eq 0 ]] || echo "=== remote-run interrupted by a signal: the remote side was told to stop" >> "$LOG"

        # fallback / refused count only from the remote side's own lines BEFORE the run started:
        # a step that prints such a line must not turn a red run into a fallback.
        local pre
        pre=$(sed '/^=== remote-run started$/q' "$LOG")
        if grep -q '^=== remote-run fallback:' <<<"$pre"; then
            state=fallback
        elif grep -q '^=== remote-run refused:' <<<"$pre"; then
            state=refused
        elif grep -q '^=== remote-run exit: ' "$LOG"; then
            rc=$(sed -n 's/^=== remote-run exit: \([0-9]*\)$/\1/p' "$LOG" | tail -n 1)
            [[ "$rc" == 0 || "$rc" == 124 ]] || rc=1
        elif ! grep -q '^=== remote-run started' "$LOG"; then
            echo "=== remote-run fallback: the remote side did not start (transport status $tr_rc)" >> "$LOG"
            state=fallback
        else
            echo "=== remote-run connection lost (transport status $tr_rc); the remote watchdog stops the run" >> "$LOG"
            rc=1
        fi
    fi

    if [[ $state == fallback ]]; then
        log_info "falling back: $(grep -h '^=== remote-run fallback:' "$LOG" | tail -n 1)"
        if [[ -n "$fallback_dir" && $warm -eq 0 ]]; then
            set +e; run_local "$fallback_dir" "$targets"; rc=$?; set -e
        else
            rc=75
        fi
    elif [[ $state == refused ]]; then
        log_info "$(grep -h '^=== remote-run refused:' "$LOG" | tail -n 1)"
        rc=3
    elif [[ $rc -eq 0 && "$(verdict_of_log "$LOG" "$NSTEPS")" != CLEAN ]]; then
        echo "remote-run: exit 0 but the log is not clean: failing the run" >&2
        rc=1
    fi
    if [[ $state == remote && $warm -eq 0 && $rc -eq 0 ]]; then fetch_stamps; fi
    if [[ $state == remote || $state == refused ]]; then remote_cleanup; fi
    rm -rf "$PAYDIR"
    echo "remote-run summary: id=$ID branch=$BRANCH sha=${SHA:-tip} exit=$rc log=$LOG"
    summarize_log "$LOG"
    exit "$rc"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
