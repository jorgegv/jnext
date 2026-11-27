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
# 2 also covers a patch run (--patch) whose patch was refused or whose application cannot be proven: NO verdict,
# never read as a kill or a survivor.
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
       remote-run.sh [options] --probe
       remote-run.sh --make-patch DIR

  <branch>   a branch already pushed to the project's repository
  <sha>      its full 40-hex tip; the run is refused if the remote sees another commit
  <target>   a make target (words after it are passed to make), or sh:<command>;
             targets run one by one, in order, stopping at the first failure

options:
  --probe                exit 0 if the host is usable and a lane is free right now, 75 if it is busy,
                         unreachable or refuses (preflight); starts nothing, one ssh round trip. A
                         caller choosing among several hosts uses it instead of paying LANE_WAIT per busy host
  --warm                 build the image, provision the tool caches and the SD image, and
                         build once to fill ccache (the project's WARM_TARGETS, on its default
                         branch); a normal run refuses until this has run for the current deps
  --gate                 run the batch gate (the project's GATE_STEPS, in order, failing fast) on
                         <branch> <sha>; the summary shows each step's own totals and the verdict
  --ref BRANCH           with --warm: the branch to warm (default: the project's default branch)
  --deps-ref BRANCH      take the dependency script (the image's package set) from BRANCH instead
                         of the commit under test; for --warm, --gate and plain runs. The log
                         names the ref that supplied it
  --make-patch DIR       print on stdout the canonical patch of DIR's work tree against its HEAD (header line
                         `# remote-run patch: base <sha> tree <tree>`, then a flag-fixed git diff); exit 2 with
                         the reason on stderr and nothing on stdout when it cannot be carried or there is no change
  --patch FILE           plain runs only: a patch made by --make-patch at <sha> (a mutant: an uncommitted edit of
                         the pushed commit). The remote applies it after proving its tree id, and the run counts
                         only when the log shows that; any doubt is exit 2 `PATCH REFUSED` (no verdict, no fallback),
                         no stamp is copied back, and a local fallback runs only in a tree equal to the patch's
  --timeout SECS         bound for the whole run (default from project.conf)
  --keep-going           run the remaining targets after a failure
  --stream               also print the log live (default: only the summary)
  --base-repo DIR        the repository whose LOCAL `main` gives test-affected its base on the remote:
                         the run tree's origin/main is set to `git merge-base <sha> main` there, so the
                         remote plan equals the local one while local main is ahead of origin (GH #562).
                         Default: $REMOTE_RUN_BASE_REPO, else the --fallback-local DIR, else the current directory; no usable base
                         means the remote keeps origin/main (the log says so)
  --fallback-local DIR   on exit 75, run the targets in DIR on this host (the log says so);
                         REMOTE_RUN_LOCAL_WRAP, if set, is the wrapper command put in front
                         (REMOTE_RUN_FALLBACK=1 is set in the wrapper's environment, so a wrapper that
                         itself sends heavy runs here, like slot.sh, never sends one back)
  --id ID                run id (default: a timestamp); unique among live runs
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
refuse_patch_local() { echo "remote-run: PATCH REFUSED: $*" >&2; exit 2; }

# ---- patches (mutants) -----------------------------------------------------------
# tree_of DIR: the id of the tree DIR's work tree would commit (tracked and untracked, ignore rules applied), made
# in a FRESH temporary index read from HEAD: never a copy of the real index (its skip-worktree and assume-unchanged
# bits would hide a mutated file), and the real index is never written.
tree_of() {
    local dir=$1 idx rc=0
    idx=$(mktemp -u "${TMPDIR:-/tmp}/rr-index.XXXXXX")
    {
        GIT_INDEX_FILE=$idx git -C "$dir" read-tree HEAD \
            && GIT_INDEX_FILE=$idx git -C "$dir" add -A . \
            && GIT_INDEX_FILE=$idx git -C "$dir" write-tree
    } 2>/dev/null || rc=$?
    rm -f "$idx"
    return "$rc"
}

# make_patch DIR: the canonical patch on stdout (exit 0), or a reason on stderr, nothing on stdout (exit 2).
# Every format choice is a FLAG: the caller's gitconfig (diff.noprefix, color.ui, a textconv driver) must not
# change the bytes.
make_patch() {
    local dir=$1 idx out top head tree raw rc=0
    [[ -d "$dir" ]] || { echo "remote-run: --make-patch: '$dir' is not a directory" >&2; return 2; }
    top=$(git -C "$dir" rev-parse --show-toplevel 2>/dev/null) || top=""
    [[ -n "$top" && "$(cd "$dir" && pwd -P)" == "$(cd "$top" && pwd -P)" ]] \
        || { echo "remote-run: --make-patch: '$dir' is not the top of a git work tree" >&2; return 2; }
    head=$(git -C "$dir" rev-parse --verify -q 'HEAD^{commit}' 2>/dev/null) \
        || { echo "remote-run: --make-patch: '$dir' has no commit (unborn HEAD)" >&2; return 2; }
    idx=$(mktemp -u "${TMPDIR:-/tmp}/rr-index.XXXXXX"); out=$idx.patch
    {
        GIT_INDEX_FILE=$idx git -C "$dir" read-tree HEAD && GIT_INDEX_FILE=$idx git -C "$dir" add -A .
    } >/dev/null 2>&1 || { rm -f "$idx" "$out"; echo "remote-run: --make-patch: cannot read the work tree of '$dir'" >&2; return 2; }
    tree=$(GIT_INDEX_FILE=$idx git -C "$dir" write-tree 2>/dev/null) || tree=""
    raw=$(GIT_INDEX_FILE=$idx git -C "$dir" diff --cached --raw --no-renames --no-ext-diff --no-textconv --no-color HEAD 2>/dev/null) || rc=1
    if [[ $rc -ne 0 || ! "$tree" =~ ^[0-9a-f]{40}$ ]]; then
        rc=2; echo "remote-run: --make-patch: git failed in '$dir'" >&2
    elif [[ "$tree" == "$(git -C "$dir" rev-parse 'HEAD^{tree}')" ]]; then
        rc=2; echo "remote-run: --make-patch: '$dir' carries no change against HEAD (a patch run would test the unmutated tree)" >&2
    elif awk '$1 == ":160000" || $2 == "160000" {f = 1} END {exit !f}' <<<"$raw"; then
        rc=2; echo "remote-run: --make-patch: a submodule or an embedded repository changed in '$dir': a patch cannot carry it" >&2
    elif [[ -n "$(git -C "$dir" submodule foreach --quiet --recursive 'git status --porcelain' 2>/dev/null)" ]]; then
        rc=2; echo "remote-run: --make-patch: a submodule of '$dir' has changes inside it: a patch cannot carry them" >&2
    else
        {
            echo "# remote-run patch: base $head tree $tree"
            GIT_INDEX_FILE=$idx git -C "$dir" diff --cached HEAD --binary --full-index --no-renames --no-ext-diff \
                --no-textconv --no-color --no-relative --src-prefix=a/ --dst-prefix=b/
        } > "$out" 2>/dev/null || { rc=2; echo "remote-run: --make-patch: git diff failed in '$dir'" >&2; }
        [[ $rc -ne 0 ]] || cat "$out"
    fi
    rm -f "$idx" "$out"
    return "$rc"
}

# patch_confirmed <log> <name> <sha256> <tree>: the pre-start part of the log holds exactly one
# `=== remote-run patch applied: <name> sha256=<sha256> tree=<tree> files=<n>` line (a step's own line, printed
# after `started`, never counts).
patch_confirmed() {
    local pre n
    pre=$(sed '/^=== remote-run started$/q' "$1")
    n=$(awk -v w="=== remote-run patch applied: $2 sha256=$3 tree=$4 files=" \
        'index($0, w) == 1 && substr($0, length(w) + 1) ~ /^[0-9]+$/ {c++} END {print c + 0}' <<<"$pre")
    [[ "$n" -eq 1 ]]
}

# ---- configuration ----------------------------------------------------------
load_conf() {
    [[ -f "$CONF" ]] || die "no project configuration: $CONF"
    # shellcheck source=/dev/null
    source "$CONF"
    local k v
    for k in PROJECT HOST_ENV_VAR DIR_ENV_VAR DIR_DEFAULT REPO_URL DEFAULT_BRANCH BASE_IMAGE DEPS_SCRIPT \
             DEPS_ARGS BOOTSTRAP_CMD CACHE_MOUNTS CONTAINER_ENV JOBS_DEFAULT JOBS_HEAVY HEAVY_TARGET_RE \
             TEST_JOBS CACHE_MIGRATE CACHE_SWEEP LANES LANE_CACHES CONTAINER_MEMORY CONTAINER_SHM TIMEOUT_DEFAULT LANE_WAIT GATE_LANE_WAIT MIN_MEM_MB MIN_DISK_GB \
             HEAVY_TARGETS WARM_TARGETS GATE_STEPS TIMEOUT_GATE STAMP_ENV STAMP_LOCAL_DIR \
             EXTRA_REPO_URL EXTRA_REF_FILE EXTRA_REF_SED EXTRA_SPARSE EXTRA_MOUNT EXTRA_ENV; do
        v="REMOTE_RUN_$k"
        if [[ -n "${!v:-}" ]]; then printf -v "$k" '%s' "${!v}"; fi
        [[ -n "${!k:-}" || "$k" == CACHE_MIGRATE || "$k" == CACHE_SWEEP || "$k" == LANE_CACHES || "$k" == BOOTSTRAP_CMD || "$k" == STAMP_ENV || "$k" == CONTAINER_ENV || "$k" == EXTRA_* ]] \
            || die "project.conf does not set $k"
    done
    [[ "$LANES" =~ ^[1-9]$ ]] || die "LANES must be one digit, 1 to 9: '$LANES'"
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
    if [[ -n "${PATCH_NAME:-}" ]]; then
        # the remote's own lines come from before the run started; the driver's own lines are written after it
        echo "--- patch"
        { sed '/^=== remote-run started$/q' <<<"$clean" | grep -aE '^=== remote-run patch (applied|refused): ' || true
          grep -aE '^=== remote-run (patch present locally|no stamp): ' <<<"$clean" || true; }
    fi
    if [[ -n "${PATCH_REFUSED_WHY:-}" ]]; then
        echo "--- verdict: PATCH REFUSED: $PATCH_REFUSED_WHY"
    else
        echo "--- verdict: $(verdict_of_log "$log" "${NSTEPS:-}")"
    fi
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
        printf 'RR_MODE=%q\nRR_ID=%q\nRR_PROJECT=%q\nRR_BRANCH=%q\nRR_SHA=%q\nRR_BASE=%q\n' "$mode" "$ID" "$PROJECT" "$BRANCH" "$SHA" "${BASE:-}"
        printf 'RR_REPO_URL=%q\nRR_DIR=%q\nRR_BASE_IMAGE=%q\nRR_DEPS_SCRIPT=%q\nRR_DEPS_ARGS=%q\n' \
            "$REPO_URL" "$DIR" "$BASE_IMAGE" "$DEPS_SCRIPT" "$DEPS_ARGS"
        printf 'RR_BOOTSTRAP=%q\nRR_TARGETS=%q\nRR_KEEP_GOING=%q\nRR_TIMEOUT=%q\nRR_LANE_WAIT=%q\nRR_LANES=%q\nRR_LANE_CACHES=%q\n' \
            "$BOOTSTRAP_CMD" "$targets" "$KEEP_GOING" "$TIMEOUT" "$LANE_WAIT" "$LANES" "${LANE_CACHES:-}"
        printf 'RR_MIN_MEM_MB=%q\nRR_MIN_DISK_GB=%q\nRR_MEMORY=%q\nRR_SHM=%q\nRR_TEST_JOBS=%q\n' \
            "$MIN_MEM_MB" "$MIN_DISK_GB" "$CONTAINER_MEMORY" "$CONTAINER_SHM" "$TEST_JOBS"
        printf 'RR_JOBS_DEFAULT=%q\nRR_JOBS_HEAVY=%q\nRR_HEAVY_RE=%q\nRR_CACHE_MOUNTS=%q\nRR_CACHE_MIGRATE=%q\nRR_CACHE_SWEEP=%q\nRR_CONTAINER_ENV=%q\nRR_STAMP_ENV=%q\n' \
            "$JOBS_DEFAULT" "$JOBS_HEAVY" "$HEAVY_TARGET_RE" "$CACHE_MOUNTS" "${CACHE_MIGRATE:-}" "${CACHE_SWEEP:-}" "$CONTAINER_ENV" "$STAMP_ENV"
        printf 'RR_DEPS_REF=%q\n' "$DEPS_REF"
        # always set (empty when unused): a value in the remote shell's environment can never act
        if [[ "$mode" == run && -n "${PATCH_NAME:-}" ]]; then
            printf 'RR_PATCH_NAME=%q\nRR_PATCH_B64=%s\nRR_PATCH_SHA256=%q\nRR_PATCH_TREE=%q\n' \
                "$PATCH_NAME" "$PATCH_B64" "$PATCH_SHA" "$PATCH_TREE"
        else
            printf 'RR_PATCH_NAME=\nRR_PATCH_B64=\nRR_PATCH_SHA256=\nRR_PATCH_TREE=\n'
        fi
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
            REMOTE_RUN_FALLBACK=1 "${wrap[@]}" env LANG=C bash -c "cd '$dir' && ${t#sh:}" </dev/null >> "$LOG" 2>&1 || rc=$?
        else
            IFS=' ' read -ra words <<<"$t"
            REMOTE_RUN_FALLBACK=1 "${wrap[@]}" env LANG=C make -C "$dir" "${words[@]}" </dev/null >> "$LOG" 2>&1 || rc=$?
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
    local warm=0 gate=0 probe=0 stream=0 warm_ref="" fallback_dir="" targets="" logdir="$HOME/tmp/remote-run"
    TIMEOUT="" DEPS_REF="" KEEP_GOING=0 LOCAL_SHELL=0 ID="" BRANCH="" SHA="" BASE=""
    local base_repo="" make_patch_dir="" patch_file="" patch_set=0
    PATCH_NAME="" PATCH_B64="" PATCH_SHA="" PATCH_TREE="" PATCH_REFUSED_WHY=""
    local -a pos=()
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) usage; exit 0 ;;
            --warm) warm=1 ;;
            --gate) gate=1 ;;
            --probe) probe=1 ;;
            --keep-going) KEEP_GOING=1 ;;
            --stream) stream=1 ;;
            --local-shell) LOCAL_SHELL=1 ;;
            --ref)      [[ $# -ge 2 ]] || { usage >&2; exit 2; }; warm_ref=$2; shift ;;
            --deps-ref) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; DEPS_REF=$2; shift ;;
            --timeout)  [[ $# -ge 2 ]] || { usage >&2; exit 2; }; TIMEOUT=$2; shift ;;
            --id)       [[ $# -ge 2 ]] || { usage >&2; exit 2; }; ID=$2; shift ;;
            --log-dir)  [[ $# -ge 2 ]] || { usage >&2; exit 2; }; logdir=$2; shift ;;
            --base-repo) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; base_repo=$2; shift ;;
            --fallback-local) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; fallback_dir=$2; shift ;;
            --conf)     [[ $# -ge 2 ]] || { usage >&2; exit 2; }; CONF=$2; shift ;;
            --make-patch) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; make_patch_dir=$2; shift ;;
            --patch)    [[ $# -ge 2 ]] || { usage >&2; exit 2; }; patch_file=$2; patch_set=1; shift ;;
            --) shift; pos+=("$@"); break ;;
            -*) echo "remote-run: unknown option '$1'" >&2; usage >&2; exit 2 ;;
            *)  pos+=("$1") ;;
        esac
        shift
    done
    # an empty value (an unset shell variable) must never fall through to a run of the unmutated tree
    [[ $patch_set -eq 0 || -n "$patch_file" ]] || refuse_patch_local "--patch needs a file name, got an empty value"
    if [[ -n "$make_patch_dir" ]]; then
        # generic: needs no project.conf, contacts nothing
        [[ $warm -eq 0 && $gate -eq 0 && $probe -eq 0 && -z "$patch_file" && ${#pos[@]} -eq 0 ]] \
            || die "--make-patch takes no other mode, branch, sha, target or --patch"
        make_patch "$make_patch_dir"
        exit $?
    fi
    load_conf
    [[ -z "$patch_file" || ( $warm -eq 0 && $gate -eq 0 && $probe -eq 0 ) ]] \
        || refuse_patch_local "--patch is for plain runs: not with --warm, --gate or --probe"
    [[ $warm -eq 0 || $gate -eq 0 ]] || die "--warm and --gate exclude each other"
    [[ $probe -eq 0 || ( $warm -eq 0 && $gate -eq 0 && ${#pos[@]} -eq 0 ) ]] || die "--probe takes no other mode, branch, sha or target"
    [[ -z "$warm_ref" || $warm -eq 1 ]] || die "--ref is for --warm only"
    [[ -z "$DEPS_REF" || "$DEPS_REF" =~ ^[A-Za-z0-9._][A-Za-z0-9._/-]*$ ]] || die "bad --deps-ref '$DEPS_REF'"

    if [[ $probe -eq 1 ]]; then
        # One ssh round trip, nothing started: 0 = usable with a free lane now, 75 = busy, unreachable or refused.
        local hostv=$HOST_ENV_VAR dirv=$DIR_ENV_VAR out rc=0
        HOST=${!hostv:-}; DIR=${!dirv:-$DIR_DEFAULT}
        ID=${ID:-probe-$$}; BRANCH=$DEFAULT_BRANCH; SHA=""; TIMEOUT=60; LANE_WAIT=0
        if [[ $LOCAL_SHELL -eq 0 && -z "$HOST" ]]; then log_info "probe: $hostv is not set"; exit 75; fi
        transport_cmd
        PAYDIR=$(mktemp -d)
        build_payload probe "" "$PAYDIR/payload"
        out=$(timeout --kill-after=5s 60s "${TCMD[@]}" < "$PAYDIR/payload" 2>&1) || rc=$?
        rm -rf "$PAYDIR"
        log_info "probe ${HOST:-local}: rc=$rc $(sed -e 's/\x1b\[[0-9;]*m//g' <<<"$out" | grep -aE 'lane|fallback' | tail -n 1)"
        [[ $rc -eq 0 ]] && exit 0
        exit 75
    fi
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
    if [[ -n "$patch_file" ]]; then
        # every refusal is before anything is contacted: no ssh, no podman, no remote run directory, no log
        local pbase=${patch_file##*/} pline=""
        [[ -f "$patch_file" && -r "$patch_file" && -s "$patch_file" ]] || refuse_patch_local "'$patch_file' is missing, unreadable or empty"
        [[ "$pbase" =~ ^[A-Za-z0-9._-]+$ && ${#pbase} -le 100 ]] \
            || refuse_patch_local "the patch file name must match [A-Za-z0-9._-]+ and be at most 100 characters (it is written into the log)"
        IFS= read -r pline < "$patch_file" || true
        [[ "$pline" =~ ^"# remote-run patch: base "([0-9a-f]{40})" tree "([0-9a-f]{40})$ ]] \
            || refuse_patch_local "'$pbase' has no '# remote-run patch: base <sha> tree <tree>' first line (make it with --make-patch)"
        [[ "${BASH_REMATCH[1]}" == "$SHA" ]] \
            || refuse_patch_local "the patch was made against ${BASH_REMATCH[1]}, not $SHA"
        PATCH_TREE=${BASH_REMATCH[2]}
        grep -q '^diff --git ' "$patch_file" || refuse_patch_local "'$pbase' holds no change (no 'diff --git' line)"
        PATCH_NAME=$pbase
        PATCH_B64=$(base64 -w0 < "$patch_file")
        PATCH_SHA=$(sha256sum < "$patch_file" | cut -d' ' -f1)
    fi
    # test-affected's base (GH #562): the merge base of the commit with LOCAL main, which the remote run tree
    # takes as its origin/main. Empty when there is no local main or no such commit: the remote falls back.
    # Not for --gate: a batch gate's TEST-AFFECTED MISS check compares against what was last pushed (origin/main);
    # against local main the gated commit would be its own base and every failing suite would read as a MISS.
    BASE_NOTE=""
    if [[ $gate -eq 1 ]]; then
        BASE_NOTE="test-affected base: none sent (--gate compares against origin/main)"
    elif [[ -n "$SHA" ]]; then
        local brepo=${base_repo:-${REMOTE_RUN_BASE_REPO:-${fallback_dir:-.}}}
        BASE=$(git -C "$brepo" merge-base "$SHA" main 2>/dev/null) || BASE=""
        if [[ "$BASE" =~ ^[0-9a-f]{40}$ ]]; then
            BASE_NOTE="test-affected base: $BASE (git -C $brepo merge-base ${SHA:0:12} main)"
        else
            BASE=""
            BASE_NOTE="test-affected base: none computed (no local main in $brepo, or ${SHA:0:12} is not in it); the remote uses origin/main"
        fi
    fi
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
    [[ -z "$BASE_NOTE" ]] || log_info "$BASE_NOTE"

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

    local was_remote=0 patch_no_verdict=""
    if [[ $state == remote ]]; then
        was_remote=1
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
        if [[ -n "$PATCH_NAME" ]] && grep -q '^=== remote-run patch refused: ' <<<"$pre"; then
            state=patchrefused
            PATCH_REFUSED_WHY=$(grep -am1 '^=== remote-run patch refused: ' <<<"$pre" | sed 's/^=== remote-run patch refused: //')
        elif grep -q '^=== remote-run fallback:' <<<"$pre"; then
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
            [[ -z "$PATCH_NAME" ]] || patch_no_verdict="the connection was lost after the start: no verdict (connection lost)"
        fi
        # For a mutant a red exit is the "good" answer (KILLED), so a status that says nothing about the mutant must never be one:
        # a run-level timeout is the lane, not the mutant (a mutant that hangs hits the suite's own bound first and shows as a FAIL
        # row), and a harness refusal (exit 2 from make, no Total line) is a refused run, not a failed row.
        # (harness refusal: its own banner LINE only; a FAIL listing quotes the words indented behind '| ')
        if [[ -n "$PATCH_NAME" && -z "$patch_no_verdict" ]]; then
            if [[ $rc -eq 124 ]]; then patch_no_verdict="the run hit the run-level timeout: no verdict (time limit)"
            elif [[ $rc -ne 0 ]] && grep -aEq '^[[:space:]]*TEST HARNESS REFUSES TO RUN[[:space:]]*$' <<<"$(sed -e 's/\x1b\[[0-9;]*m//g' "$LOG")"; then patch_no_verdict="the test harness refused to run: no verdict (REFUSES TO RUN)"
            fi
        fi
        if [[ $state == remote && -n "$patch_no_verdict" ]]; then
            PATCH_REFUSED_WHY=$patch_no_verdict
            echo "=== remote-run patch refused: $PATCH_REFUSED_WHY" >> "$LOG"
            state=patchrefused
        fi
        # A patch run counts only when the log proves the patch was applied, whatever the container said.
        if [[ $state == remote && -n "$PATCH_NAME" ]] && ! patch_confirmed "$LOG" "$PATCH_NAME" "$PATCH_SHA" "$PATCH_TREE"; then
            PATCH_REFUSED_WHY="the run cannot prove the patch was applied: no verdict"
            echo "=== remote-run patch refused: $PATCH_REFUSED_WHY" >> "$LOG"
            state=patchrefused
        fi
    fi

    if [[ $state == fallback ]]; then
        log_info "falling back: $(grep -h '^=== remote-run fallback:' "$LOG" | tail -n 1)"
        if [[ -n "$fallback_dir" && $warm -eq 0 ]]; then
            local ptree_here=""
            if [[ -n "$PATCH_NAME" ]]; then
                # the local run must test the mutant: its tree must be the patch's, never a clean or re-edited one
                ptree_here=$(tree_of "$fallback_dir") || ptree_here="unreadable"
                if [[ "$ptree_here" != "$PATCH_TREE" ]]; then
                    PATCH_REFUSED_WHY="$fallback_dir does not carry $PATCH_NAME (tree $ptree_here, the patch's $PATCH_TREE)"
                    echo "=== remote-run patch refused: $PATCH_REFUSED_WHY" >> "$LOG"
                    state=patchrefused
                else
                    echo "=== remote-run patch present locally: $PATCH_NAME sha256=$PATCH_SHA tree=$PATCH_TREE" | tee -a "$LOG" >&2
                fi
            fi
            if [[ $state != patchrefused ]]; then
                set +e; run_local "$fallback_dir" "$targets"; rc=$?; set -e
            fi
        else
            rc=75
        fi
    fi
    if [[ $state == patchrefused ]]; then
        log_info "PATCH REFUSED: $PATCH_REFUSED_WHY"
        rc=2
    elif [[ $state == refused ]]; then
        log_info "$(grep -h '^=== remote-run refused:' "$LOG" | tail -n 1)"
        rc=3
    elif [[ $state == remote && $rc -eq 0 && "$(verdict_of_log "$LOG" "$NSTEPS")" != CLEAN ]]; then
        echo "remote-run: exit 0 but the log is not clean: failing the run" >&2
        rc=1
    fi
    if [[ -n "$PATCH_NAME" && $state == remote ]]; then
        echo "=== remote-run no stamp: a patch run" >> "$LOG"   # a mutant's green run must never stand in for the commit
    elif [[ $state == remote && $warm -eq 0 && $rc -eq 0 ]]; then
        fetch_stamps
    fi
    if [[ $state == remote || $state == refused || ( $state == patchrefused && $was_remote -eq 1 ) ]]; then remote_cleanup; fi
    rm -rf "$PAYDIR"
    echo "remote-run summary: id=$ID branch=$BRANCH sha=${SHA:-tip}${PATCH_NAME:+ patch=$PATCH_NAME} exit=$rc log=$LOG"
    summarize_log "$LOG"
    exit "$rc"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi
