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
#   regression-stamp.sh key [<commit>]   the content key of <commit> (default HEAD)
#   regression-stamp.sh state            "key=<k> commit=<sha> dirty=<n>" for the work tree
#   regression-stamp.sh write <state> k=v...
#                                        (regression.sh, at the end of a green full
#                                        run) stamp <state>'s key, if the tree was
#                                        clean then and is clean and unchanged now
#   regression-stamp.sh check            print the stamp matching the work tree, or
#                                        exit 1 saying why there is none
#   regression-stamp.sh ci               find a green CI run (gh, read-only) on a
#                                        commit with the work tree's key
#
# THE KEY is a sha256 over `git ls-tree -r` of the commit — every path's mode,
# blob id and name — restricted to the KEYED paths below. It is a function of
# committed content only, so it is the same in every worktree and on CI, and it
# needs no checkout of the commit to compute.
#
# THE KEYED PATHS are defined here and nowhere else (KEY_FILTER_AWK): every
# tracked path EXCEPT documentation — doc/, src/doc/, any *.md, ChangeLog —
# with three exceptions that live in doc/ but are INPUTS to a gate that
# `make regression` runs, so a change to them can change its verdict:
#   doc/formats/                          snapshot-schema-func validates jnext's
#                                         output against the schema there;
#   doc/man/jnext.1.md                    cli-check diffs the CLI table against it;
#   doc/testing/CURRENT-REGRESSION-STATE.md  regression-doc-check checks it
#                                         against regression_tests.conf.
# A file that only docs-check reads is NOT an input in that sense: a docs-only
# change runs docs-check (CLAUDE.md, the doc-only rule), not the regression.
#
# CLEAN means no uncommitted change, staged or not, and no untracked file, in
# a KEYED path. A docs-only edit in flight does not stop a stamp; a source
# edit does, because then the tree that ran is not the commit the key names.
#
# Stamps live in ${XDG_CACHE_HOME:-~/.cache}/jnext/regression-stamps/<key>
# (JNEXT_REGRESSION_STAMP_DIR overrides), shared by every worktree of the user.
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
    if (p ~ /^doc\/formats\// || p == "doc/man/jnext.1.md" || p == "doc/testing/CURRENT-REGRESSION-STATE.md") return 1
    if (p ~ /^doc\// || p ~ /^src\/doc\// || p ~ /\.md$/ || p == "ChangeLog") return 0
    return 1
}'

git_() { git -C "$ROOT" -c core.quotePath=false "$@"; }

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

# field <state> <name>
field() { local f; for f in $1; do [[ "$f" == "$2="* ]] && { echo "${f#*=}"; return 0; }; done; return 1; }

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
        now=$(state)
        skey=$(field "$start" key) || die "bad starting state '$start'"
        if [[ "$(field "$start" dirty)" != 0 || "$(field "$now" dirty)" != 0 ]]; then
            echo "  no regression stamp: the tree had uncommitted non-doc changes (at start: $(field "$start" dirty), at end: $(field "$now" dirty) paths)"
            dirty_paths | head -n 10 | sed 's/^/      /'
            exit 0
        fi
        if [[ "$(field "$now" key)" != "$skey" ]]; then
            echo "  no regression stamp: the tree's non-doc content changed during the run ($(field "$start" commit) -> $(field "$now" commit))"
            exit 0
        fi
        commit=$(field "$start" commit)
        mkdir -p "$STAMP_DIR"
        tmp="$STAMP_DIR/.$skey.$$"
        {
            echo "key=$skey"
            echo "commit=$commit"
            echo "subject=$(git_ log -1 --format=%s "$commit")"
            echo "stamped=$(date '+%Y-%m-%dT%H:%M:%S%z')"
            echo "host=$(hostname 2>/dev/null || uname -n)"
            echo "worktree=$ROOT"
            printf '%s\n' "$@"
        } > "$tmp"
        mv -f "$tmp" "$STAMP_DIR/$skey"
        echo "  regression stamp written: $STAMP_DIR/$skey (commit ${commit:0:12})"
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
            echo "  Run 'make regression' on it, or check out the commit the author stamped."
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
        die "usage: regression-stamp.sh [-C <repo>] key [<commit>] | state | write <state> k=v... | check | ci"
        ;;
esac
