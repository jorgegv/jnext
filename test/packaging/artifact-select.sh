# shellcheck shell=bash
# Artifact selection for test/packaging/packaging-test.sh (sourced, not run).
#
# The package rows used to take `ls -1 <dir>/<glob> | head -1`. A build
# directory keeps the artifacts of every earlier version (CPack and the
# package-* recipes only replace their own file name), so after a bump the
# row could pick the PREVIOUS version's leftover, assert on it, and pass:
# observed on 2026-10-04, when a run on v1.1.3 reported `v1.1.2.tar.gz` and
# `jnext-1.1.2-1.x86_64.rpm`. These helpers make a row check the artifact
# the build it just ran produced, or fail and say why.
#
# The version comes from version.yaml, the single source of truth, read with
# the same field and X.Y.Z form cmake/GenerateVersion.cmake uses.

# artifact_version [<repo-root>] — print version.yaml's X.Y.Z, or fail
artifact_version() {
    local v
    v=$(sed -nE 's/^version: *([0-9]+\.[0-9]+\.[0-9]+) *$/\1/p' "${1:-.}/version.yaml" 2>/dev/null | head -n1)
    [ -n "$v" ] || { echo "artifact_version: no 'version: X.Y.Z' line in ${1:-.}/version.yaml" >&2; return 1; }
    printf '%s\n' "$v"
}

# clear_artifacts <dir> <glob> — delete earlier artifacts matching <glob> in
# <dir> before a build, so a leftover can never stand in for the new one.
# Only regular files directly in <dir> are touched.
clear_artifacts() {
    local f
    for f in "$1"/$2; do
        [ -f "$f" ] && rm -f -- "$f"
    done
    return 0
}

# pick_artifact <dir> <glob> <expected-basename> — print <dir>/<expected> when
# it is the ONLY file matching <glob>. Otherwise explain on stderr and fail:
#   no match                    the build produced nothing
#   more than one match         stale or extra artifacts; listed
#   one match, wrong name       the build produced a different version
pick_artifact() {
    local dir=$1 glob=$2 want=$3 f
    local -a found=()
    for f in "$dir"/$glob; do
        [ -f "$f" ] && found+=("$f")
    done
    if [ "${#found[@]}" -eq 0 ]; then
        echo "pick_artifact: no $glob in $dir (expected $want)" >&2
        return 1
    fi
    if [ "${#found[@]}" -gt 1 ]; then
        echo "pick_artifact: ${#found[@]} candidates for $glob in $dir, expected exactly one ($want):" >&2
        printf '  %s\n' "${found[@]}" >&2
        return 1
    fi
    if [ "$(basename "${found[0]}")" != "$want" ]; then
        echo "pick_artifact: $dir holds $(basename "${found[0]}"), but version.yaml says $want" >&2
        return 1
    fi
    printf '%s\n' "${found[0]}"
}
