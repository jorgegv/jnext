#!/usr/bin/env bash
# Green-run stamps for the regression suite (GH #295).
#
# A full `make regression` that passes on a clean tree records a STAMP: the
# run's results, keyed on the content of the tree it tested. Any later tree
# with the same key — a docs-only commit on top, a reviewer's worktree of the
# same branch — is covered by that run and need not run it again.
#
# Usage: regression-stamp.sh [-C <repo>] <command>, where <repo> (default: the
# checkout this script lives in) is the repository or worktree to describe:
#   key [<commit>]          the content key of <commit> (default HEAD)
#   state                   "key=<k> commit=<sha> dirty=<n>" for the work tree
#   write <state> k=v...    (regression.sh, end of a green full run) stamp
#                           <state>'s key — refused unless fail=0 and skip=0,
#                           the tree was clean then and is clean and unchanged now
#   pending <state> fails=<rows> k=v...
#                           (regression.sh, end of a full run whose only FAILs
#                           are functional rows that failed on a loaded host)
#                           record those rows as awaiting a solo re-run
#   pending-rows            the rows pending for the work tree's key, or exit 1
#   confirm <state> rows=<rows> k=v...
#                           (regression.sh, end of `make regression-confirm`)
#                           every pending row passed solo on the same key: turn
#                           the pending record into a stamp that names them
#   check                   print the stamp matching the work tree, or exit 1
#   ci                      find a green CI run (gh, read-only) on a commit with
#                           the work tree's key
#
# THE KEY is a sha256 over `git ls-tree -r` of the commit — every path's mode,
# blob id and name, gitlinks included — restricted to the KEYED paths below. It
# is a function of committed content only, so it is the same in every worktree
# and on CI, and it needs no checkout of the commit to compute.
#
# THE KEYED PATHS are defined here and nowhere else (KEY_AWK_FN): every tracked
# path EXCEPT documentation — doc/, src/doc/, any *.md, ChangeLog — with the
# exceptions that are documentation by location but INPUTS to `make
# regression`, so a change to them can change its verdict. The rule that
# derives the list: a documentation file is keyed iff something `make
# regression` runs READS it, other than docs-check (a docs-only change runs
# docs-check — CLAUDE.md, the doc-only rule). Derived (GH #295 round 1) by
# tracing every file opened by the gates (lint-makefile-help,
# regression-doc-check, cli-check) and by a whole traced suite run, and
# cross-checked by a static grep of CMake, the Makefile, the row scripts and
# their helpers for documentation paths:
#   doc/formats/                          snapshot-schema-func validates jnext's
#                                         output against the schema there;
#   doc/man/jnext.1.md                    cli-check diffs the CLI table against it;
#   src/doc/user-guide/09-reference/01-command-line-options.md
#                                         cli-check (CLI-DOC-06) diffs it too;
#   doc/testing/CURRENT-REGRESSION-STATE.md  regression-doc-check checks it
#                                         against regression_tests.conf.
# HS-71h..j pin the classification on these real paths.
#
# CLEAN means no uncommitted change, staged or not, and no untracked file, in
# a KEYED path — at the start of the run AND at its end, with the key
# unchanged. A docs-only edit in flight does not stop a stamp; a source edit
# does, because then the tree that ran is not the commit the key names.
# "Untracked" is git's own answer, with ALL the user's ignore rules — the
# repository's, .git/info/exclude AND the global excludes file (see git_).
#
# Stamps live in ${XDG_CACHE_HOME:-~/.cache}/jnext/regression-stamps/<key>
# (JNEXT_REGRESSION_STAMP_DIR overrides), shared by every worktree of the user;
# a pending record is <key>.pending beside it.
set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == -C ]]; then
    [[ -n "${2:-}" ]] || { echo "regression-stamp: -C needs a directory" >&2; exit 2; }
    ROOT=$(git -C "$2" rev-parse --show-toplevel) || exit 2
    shift 2
fi
STAMP_DIR=${JNEXT_REGRESSION_STAMP_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/jnext/regression-stamps}

# The one definition of what the key covers: keyed(p) is true for a keyed path.
KEY_AWK_FN='function keyed(p) {
    if (p ~ /^doc\/formats\// || p == "doc/man/jnext.1.md" || p == "doc/testing/CURRENT-REGRESSION-STATE.md" \
        || p == "src/doc/user-guide/09-reference/01-command-line-options.md") return 1
    if (p ~ /^doc\// || p ~ /^src\/doc\// || p ~ /\.md$/ || p == "ChangeLog") return 0
    return 1
}'

# Run inside the regression harness, XDG_CONFIG_HOME points at a scratch dir
# (Qt isolation); git must still see the user's own global config and global
# excludes file, so it gets the value the harness captured before isolating
# (test-functions.inc). Run directly (`make regression-stamp-check`), the
# variable is unset and git sees the environment as it is. The KEY does not
# depend on either (ls-tree of a commit); the dirty check does.
git_() {
    if [[ -n "${JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME+x}" ]]; then
        XDG_CONFIG_HOME="$JNEXT_REGRESSION_GIT_XDG_CONFIG_HOME" command git -C "$ROOT" -c core.quotePath=false "$@"
    else
        command git -C "$ROOT" -c core.quotePath=false "$@"
    fi
}

die() { echo "regression-stamp: $*" >&2; exit 2; }

# key_of <commit> — the content key: sha256 of the keyed lines of ls-tree.
key_of() {
    local listing
    listing=$(git_ ls-tree -r --full-tree "$1") || return 1
    awk -F'\t' "$KEY_AWK_FN"' keyed($2)' <<<"$listing" | sha256sum | awk '{print $1}'
}

# dirty_paths — the keyed paths with uncommitted changes or untracked files.
dirty_paths() {
    { git_ diff --name-only --no-renames HEAD; git_ ls-files --others --exclude-standard; } \
        | awk "$KEY_AWK_FN"' keyed($0)' | sort -u
}

# state — one line describing the work tree.
state() {
    local commit key n
    commit=$(git_ rev-parse HEAD)
    key=$(key_of "$commit")
    n=$(dirty_paths | wc -l)
    echo "key=$key commit=$commit dirty=$n"
}

# field <state-or-args> <name> — the value of name=... in a space-separated list
field() { local f; for f in $1; do [[ "$f" == "$2="* ]] && { echo "${f#*=}"; return 0; }; done; return 1; }
# arg <name> <k=v...> — the value of name=... among separate arguments
arg() { local n=$1 a; shift; for a in "$@"; do [[ "$a" == "$n="* ]] && { echo "${a#*=}"; return 0; }; done; return 1; }

# clean_through <start-state> — the run's tree was clean at its start, is clean
# now, and its key did not move. Prints why not, and fails, otherwise.
clean_through() {
    local start=$1 now
    now=$(state)
    if [[ "$(field "$start" dirty)" != 0 || "$(field "$now" dirty)" != 0 ]]; then
        echo "  no regression stamp: the tree had uncommitted non-doc changes (at start: $(field "$start" dirty), at end: $(field "$now" dirty) paths)"
        dirty_paths | head -n 10 | sed 's/^/      /'
        return 1
    fi
    if [[ "$(field "$now" key)" != "$(field "$start" key)" ]]; then
        echo "  no regression stamp: the tree's non-doc content changed during the run ($(field "$start" commit) -> $(field "$now" commit))"
        return 1
    fi
    return 0
}

# header <key> <commit> — the identity lines of a stamp or pending record
header() {
    echo "key=$1"
    echo "commit=$2"
    echo "subject=$(git_ log -1 --format=%s "$2")"
    echo "stamped=$(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "host=$(hostname 2>/dev/null || uname -n)"
    echo "worktree=$ROOT"
}

# sorted_words <words> — the words, one space apart, sorted and deduplicated
sorted_words() { tr ' ' '\n' <<<"$1" | sed '/^$/d' | sort -u | tr '\n' ' ' | sed 's/ $//'; }

cmd=${1:-}
case $cmd in
    key)
        key_of "${2:-HEAD}"
        ;;
    state)
        state
        ;;
    write)
        [[ $# -ge 2 ]] || die "write needs the run's starting state"
        start=$2; shift 2
        skey=$(field "$start" key) || die "bad starting state '$start'"
        # A stamp stands in for a run (a reviewer or a release accepts it
        # instead of running), so it may only describe a run in which every
        # row PASSED: a skipped row was not tested, and the gate's baseline
        # declares no skips.
        f=$(arg fail "$@") || f=missing; s=$(arg skip "$@") || s=missing
        if [[ "$f" != 0 || "$s" != 0 ]]; then
            echo "  no regression stamp: only a run with fail=0 and skip=0 is stamped (this one: fail=$f skip=$s)"
            exit 0
        fi
        clean_through "$start" || exit 0
        commit=$(field "$start" commit)
        mkdir -p "$STAMP_DIR"
        tmp="$STAMP_DIR/.$skey.$$"
        { header "$skey" "$commit"; printf '%s\n' "$@"; } > "$tmp"
        mv -f "$tmp" "$STAMP_DIR/$skey"
        rm -f "$STAMP_DIR/$skey.pending"
        echo "  regression stamp written: $STAMP_DIR/$skey (commit ${commit:0:12})"
        ;;
    pending)
        [[ $# -ge 2 ]] || die "pending needs the run's starting state"
        start=$2; shift 2
        skey=$(field "$start" key) || die "bad starting state '$start'"
        fails=$(arg fails "$@") || die "pending needs fails=<rows>"
        [[ -n "$fails" ]] || die "pending with no failed rows"
        s=$(arg skip "$@") || s=missing
        [[ "$s" == 0 ]] || { echo "  no pending stamp: skip=$s — a skipped row was not tested"; exit 0; }
        clean_through "$start" || exit 0
        commit=$(field "$start" commit)
        mkdir -p "$STAMP_DIR"
        tmp="$STAMP_DIR/.$skey.pending.$$"
        { header "$skey" "$commit"; printf '%s\n' "$@"; } > "$tmp"
        mv -f "$tmp" "$STAMP_DIR/$skey.pending"
        echo "  NO STAMP YET: $(wc -w <<<"$fails") row(s) failed on a loaded host: $fails"
        echo "  They are recorded as pending ($STAMP_DIR/$skey.pending). Re-run them SOLO on this"
        echo "  tree with 'make regression-confirm': if every one passes, the run is stamped and"
        echo "  the stamp names them; if any fails again, the FAIL is real."
        ;;
    pending-rows)
        now=$(state)
        key=$(field "$now" key); n=$(field "$now" dirty)
        [[ "$n" == 0 ]] || { echo "regression-stamp: the work tree has $n uncommitted non-doc change(s)" >&2; exit 1; }
        [[ -f "$STAMP_DIR/$key.pending" ]] \
            || { echo "regression-stamp: no pending solo re-run for this tree's key $key" >&2; exit 1; }
        sed -n 's/^fails=//p' "$STAMP_DIR/$key.pending"
        ;;
    confirm)
        [[ $# -ge 2 ]] || die "confirm needs the run's starting state"
        start=$2; shift 2
        skey=$(field "$start" key) || die "bad starting state '$start'"
        pend="$STAMP_DIR/$skey.pending"
        [[ -f "$pend" ]] || { echo "  no regression stamp: nothing is pending for key $skey"; exit 0; }
        rows=$(arg rows "$@") || die "confirm needs rows=<rows>"
        want=$(sed -n 's/^fails=//p' "$pend")
        if [[ "$(sorted_words "$rows")" != "$(sorted_words "$want")" ]]; then
            echo "  no regression stamp: the solo run covered '$rows', the pending rows are '$want'"
            exit 0
        fi
        f=$(arg fail "$@") || f=missing; s=$(arg skip "$@") || s=missing
        if [[ "$f" != 0 || "$s" != 0 ]]; then
            echo "  no regression stamp: the solo re-run did not pass (fail=$f skip=$s) — the pending FAIL stands"
            exit 0
        fi
        clean_through "$start" || exit 0
        full_pass=$(sed -n 's/^pass=//p' "$pend"); full_fail=$(sed -n 's/^fail=//p' "$pend")
        mkdir -p "$STAMP_DIR"
        tmp="$STAMP_DIR/.$skey.$$"
        {
            grep -vE '^(stamped|host|worktree|pass|fail|fails)=' "$pend"
            echo "pass=$(( full_pass + full_fail ))"
            echo "fail=0"
            echo "stamped=$(date '+%Y-%m-%dT%H:%M:%S%z')"
            echo "host=$(hostname 2>/dev/null || uname -n)"
            echo "worktree=$ROOT"
            echo "full_run_failed_on_loaded_host=$want"
            echo "confirmed_solo=$rows"
            for a in "$@"; do [[ "$a" == rows=* ]] || echo "solo_$a"; done
        } > "$tmp"
        mv -f "$tmp" "$STAMP_DIR/$skey"
        rm -f "$pend"
        echo "  regression stamp written: $STAMP_DIR/$skey — the full run's loaded-host FAIL(s) $want passed solo"
        ;;
    check)
        now=$(state)
        key=$(field "$now" key); commit=$(field "$now" commit); n=$(field "$now" dirty)
        if [[ "$n" != 0 ]]; then
            echo "NO STAMP: the work tree has $n uncommitted non-doc change(s); no run can have tested it:"
            dirty_paths | head -n 10 | sed 's/^/    /'
            exit 1
        fi
        if [[ ! -f "$STAMP_DIR/$key" ]]; then
            echo "NO STAMP for this tree: no green full 'make regression' has been recorded for content key"
            echo "    $key (commit ${commit:0:12}, docs excluded)"
            if [[ -f "$STAMP_DIR/$key.pending" ]]; then
                echo "  A full run is PENDING solo re-runs of: $(sed -n 's/^fails=//p' "$STAMP_DIR/$key.pending")"
                echo "  — 'make regression-confirm' runs them."
            else
                echo "  Run 'make regression' on it, or check out the commit the author stamped."
            fi
            exit 1
        fi
        echo "STAMP OK: a green full regression run covers this tree (commit ${commit:0:12})"
        sed 's/^/    /' "$STAMP_DIR/$key"
        ;;
    ci)
        now=$(state)
        key=$(field "$now" key); commit=$(field "$now" commit); n=$(field "$now" dirty)
        [[ "$n" == 0 ]] || { echo "NO CI MATCH: the work tree has $n uncommitted non-doc change(s)"; exit 1; }
        command -v gh >/dev/null || die "gh (GitHub CLI) is needed to list CI runs"
        # push and workflow_dispatch runs test exactly the commit they name.
        # pull_request runs are NOT usable: they test a merge of the PR head
        # into main, whose content is neither commit.
        runs=$(cd "$ROOT" && gh run list --workflow ci.yml --status success --limit 200 \
                 --json headSha,event,url,createdAt \
                 --jq '.[] | select(.event == "push" or .event == "workflow_dispatch") | "\(.headSha) \(.url) \(.createdAt)"') \
            || die "gh run list failed"
        while read -r sha url created; do
            [[ -n "$sha" ]] || continue
            git_ cat-file -e "$sha^{commit}" 2>/dev/null || continue
            if [[ "$(key_of "$sha")" == "$key" ]]; then
                echo "CI MATCH: green CI run on commit ${sha:0:12} ($created) has this tree's content key"
                echo "    $url"
                echo "    key $key; this tree is commit ${commit:0:12}"
                exit 0
            fi
        done <<<"$runs"
        echo "NO CI MATCH: no green push/dispatch CI run among the last 200 is on a commit with content key"
        echo "    $key (commit ${commit:0:12}, docs excluded)"
        exit 1
        ;;
    *)
        die "usage: regression-stamp.sh [-C <repo>] key [<commit>] | state | write | pending | pending-rows | confirm | check | ci"
        ;;
esac
