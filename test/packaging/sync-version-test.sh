#!/usr/bin/env bash
#
# Contract tests for packaging/sync-version.sh — the script the bump-* targets
# use to propagate version.yaml into the hand-written packaging files.
#
# Runs entirely on a throwaway COPY of packaging/ (a fake root), so it never
# touches the real tree. Asserts:
#   1. a run updates every version-bearing field to the new version
#   2. the rpm spec's Version: and %changelog top entry stay consistent
#   3. running twice with the same version is idempotent (no further change)
#   4. a different version prepends new entries and preserves the old ones
#   5. the flatpak manifest (local-source, no version tag) is left untouched —
#      the script neither requires a `tag:` nor rewrites the manifest
#   6. a missing anchor makes the script FAIL LOUD (non-zero) rather than write
#      a half-synced file
#   7. the Makefile bump-* recipes gate the commit/tag on sync-version.sh with
#      `&&`, so a sync failure aborts the bump (never commits a broken state)
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root
repo=$(pwd)

RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'
pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Build a fake root that mirrors the layout sync-version.sh expects
# ($root = dirname(script)/.. , then $root/packaging/...).
# The flatpak manifest is intentionally NOT copied: it builds from the local
# checkout and carries no version tag, so sync-version.sh must not touch it.
mkdir -p "$tmp/packaging/rpm" "$tmp/packaging/assets" "$tmp/packaging/debian"
cp "$repo/packaging/sync-version.sh"                          "$tmp/packaging/"
cp "$repo/packaging/rpm/jnext.spec"                           "$tmp/packaging/rpm/"
cp "$repo/packaging/assets/io.github.zxjogv.jnext.metainfo.xml" "$tmp/packaging/assets/"
cp "$repo/packaging/debian/changelog"                        "$tmp/packaging/debian/"

spec="$tmp/packaging/rpm/jnext.spec"
metainfo="$tmp/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"
deb="$tmp/packaging/debian/changelog"
sync="$tmp/packaging/sync-version.sh"

printf "${BOLD}=== sync-version.sh contract tests ===${RESET}\n\n"

# 1 + 2 — a run aligns every field, spec Version == %changelog top
bash "$sync" 9.9.9 >/dev/null 2>&1
if grep -qE '^Version:[[:space:]]*9\.9\.9$' "$spec" \
   && grep -qE '^\* .* - 9\.9\.9-1$'        "$spec" \
   && grep -qE '<release version="9\.9\.9"' "$metainfo" \
   && head -n1 "$deb" | grep -qE '^jnext \(9\.9\.9-1\)'; then
    ok "run aligns spec/metainfo/debian to the new version"
else
    bad "a field was not updated to 9.9.9"
fi
# spec Version: must equal the TOP %changelog version (rpmbuild consistency)
specver=$(grep -m1 -E '^Version:' "$spec" | awk '{print $2}')
cltop=$(grep -m1 -E '^\* ' "$spec" | sed -E 's/.* - ([0-9.]+)-1$/\1/')
if [ "$specver" = "$cltop" ]; then
    ok "spec Version: ($specver) matches %changelog top ($cltop)"
else
    bad "spec Version: ($specver) != %changelog top ($cltop)"
fi

# 3 — idempotency
before=$(cat "$spec" "$metainfo" "$deb")
bash "$sync" 9.9.9 >/dev/null 2>&1
after=$(cat "$spec" "$metainfo" "$deb")
if [ "$before" = "$after" ]; then
    ok "second run with the same version is a no-op (idempotent)"
else
    bad "second run changed the files (not idempotent)"
fi

# 4 — a new version prepends without destroying the old entry
bash "$sync" 9.9.10 >/dev/null 2>&1
if grep -qE '^Version:[[:space:]]*9\.9\.10$' "$spec" \
   && grep -qE '^\* .* - 9\.9\.10-1$'        "$spec" \
   && grep -qE '^\* .* - 9\.9\.9-1$'         "$spec" \
   && grep -qE '<release version="9\.9\.10"' "$metainfo" \
   && grep -qE '<release version="9\.9\.9"'  "$metainfo"; then
    ok "new version prepends its entry and keeps the previous one"
else
    bad "new version did not preserve prior history"
fi

# 5 — the real flatpak manifest carries no version tag and must be left alone:
# the script must SUCCEED with it present and leave it byte-identical (no tag
# requirement, no rewrite). Discriminative: re-adding a `tag:` require-check or
# a sed rewrite would flip this to FAIL.
mkdir -p "$tmp/packaging/flatpak"
realmanifest="$repo/packaging/flatpak/io.github.zxjogv.jnext.yml"
cp "$realmanifest" "$tmp/packaging/flatpak/"
fpk="$tmp/packaging/flatpak/io.github.zxjogv.jnext.yml"
fpk_before=$(cat "$fpk")
if bash "$sync" 9.9.12 >/dev/null 2>&1 && [ "$fpk_before" = "$(cat "$fpk")" ]; then
    ok "tag-less flatpak manifest untouched (no tag requirement, no rewrite)"
else
    bad "script failed with the local-source flatpak manifest, or rewrote it"
fi

# 6 — fail loud on a missing anchor (remove %changelog from the spec)
grep -v '^%changelog$' "$spec" > "$spec.tmp" && mv "$spec.tmp" "$spec"
verbefore=$(grep -m1 -E '^Version:' "$spec")
if bash "$sync" 9.9.11 >/dev/null 2>&1; then
    bad "script succeeded despite a missing %changelog anchor"
else
    verafter=$(grep -m1 -E '^Version:' "$spec")
    if [ "$verbefore" = "$verafter" ]; then
        ok "missing %changelog anchor -> fail loud, no partial write"
    else
        bad "script failed but still rewrote Version: (partial write)"
    fi
fi

# 7 — Makefile gates the bump commit/tag on sync-version.sh with &&
# shellcheck disable=SC2016  # the $$newver literal is what we grep for, not a var to expand
if grep -qE 'bash packaging/sync-version\.sh "\$\$newver" &&' "$repo/Makefile"; then
    ok "bump-* recipes chain sync-version.sh with && (abort on failure)"
else
    bad "bump-* recipes do NOT gate on sync-version.sh success (found ';')"
fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
