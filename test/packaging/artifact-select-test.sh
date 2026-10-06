#!/usr/bin/env bash
#
# Contract tests for test/packaging/artifact-select.sh: the helpers the package
# rows of packaging-test.sh use to pick the artifact a build produced. Before
# them a row took `ls | head -1`, and a previous version's leftover in the
# build directory could satisfy it (a v1.1.3 run reported v1.1.2.tar.gz).
# Runs on throwaway directories with planted files; builds nothing. Asserts:
#   1. the current-version artifact, alone, is picked
#   2. a planted OLD-version leftover beside it fails loudly, naming both
#   3. an old-version leftover ALONE (the build produced nothing new) fails,
#      naming what is there and what version.yaml expects
#   4. no artifact at all fails
#   5. clear_artifacts removes the leftovers matching the glob and nothing else
#   6. artifact_version reads version.yaml, and fails on a malformed one
#   7. packaging-test.sh has no `| head -1` artifact pick left, and every
#      package row clears its artifacts before building and picks after
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root
repo=$(pwd)
# shellcheck source=test/packaging/artifact-select.sh
. "$repo/test/packaging/artifact-select.sh"

RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
d="$tmp/rpm-release"; mkdir -p "$d"
glob='jnext-*.x86_64.rpm'
new='jnext-9.9.10-1.x86_64.rpm'
old='jnext-9.9.9-1.x86_64.rpm'

printf "${BOLD}=== artifact-select contract tests ===${RESET}\n\n"

# 1
: > "$d/$new"
got=$(pick_artifact "$d" "$glob" "$new" 2>/dev/null); rc=$?
if [ "$rc" -eq 0 ] && [ "$got" = "$d/$new" ]; then
    ok "the current-version artifact, alone, is picked"
else
    bad "current-version artifact not picked (rc=$rc got='$got')"
fi

# 2 — the observed failure: an older leftover sorts FIRST, which is exactly
# what `ls | head -1` returned
: > "$d/$old"
err=$(pick_artifact "$d" "$glob" "$new" 2>&1 >/dev/null); rc=$?
if [ "$rc" -ne 0 ] && grep -qF "2 candidates" <<<"$err" && grep -qF "$old" <<<"$err"; then
    ok "an old-version leftover beside the new artifact fails loudly, naming it"
else
    bad "a leftover beside the new artifact was accepted (rc=$rc): $err"
fi

# 3
rm -f "$d/$new"
err=$(pick_artifact "$d" "$glob" "$new" 2>&1 >/dev/null); rc=$?
if [ "$rc" -ne 0 ] && grep -qF "holds $old" <<<"$err" && grep -qF "$new" <<<"$err"; then
    ok "an old-version leftover alone fails, naming it and the expected file"
else
    bad "a lone old-version artifact was accepted (rc=$rc): $err"
fi

# 4
rm -f "$d/$old"
pick_artifact "$d" "$glob" "$new" >/dev/null 2>&1; rc=$?
if [ "$rc" -ne 0 ]; then
    ok "no artifact at all fails"
else
    bad "an empty directory yielded an artifact"
fi

# 5
: > "$d/$old"; : > "$d/$new"; : > "$d/jnext-9.9.9-1.src.rpm"; mkdir -p "$d/jnext-0.0.0-1.x86_64.rpm.d"
clear_artifacts "$d" "$glob"
if [ ! -e "$d/$old" ] && [ ! -e "$d/$new" ] && [ -e "$d/jnext-9.9.9-1.src.rpm" ] \
   && [ -d "$d/jnext-0.0.0-1.x86_64.rpm.d" ]; then
    ok "clear_artifacts removes the matching files and nothing else"
else
    bad "clear_artifacts removed too little or too much"
fi

# 6
printf 'version: 9.9.10\n' > "$tmp/version.yaml"
v1=$(artifact_version "$tmp" 2>/dev/null)
printf 'version: 9.9\n' > "$tmp/version.yaml"
artifact_version "$tmp" >/dev/null 2>&1; rc=$?
if [ "$v1" = 9.9.10 ] && [ "$rc" -ne 0 ]; then
    ok "artifact_version reads version.yaml, fails on a malformed one"
else
    bad "artifact_version: got '$v1' / malformed rc=$rc"
fi

# 7 — the real harness uses the helpers on every package row
pt="$repo/test/packaging/packaging-test.sh"
picks=$(grep -cE 'pick_artifact ' "$pt")
clears=$(grep -cE 'clear_artifacts ' "$pt")
leftover=$(grep -nE 'ls -1 build/.*\| *head -1' "$pt")
# (the floor was 10 before the Flatpak BUILD row left package-test: the `flatpak`
# CI job builds it, so one fewer row picks an artifact — 2026-10-06)
if [ -z "$leftover" ] && [ "$picks" -ge 9 ] && [ "$picks" -eq "$clears" ]; then
    ok "packaging-test.sh: no ls|head artifact pick; $picks rows pick, each clears first"
else
    bad "packaging-test.sh: picks=$picks clears=$clears; leftover ls|head: ${leftover:-none}"
fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
