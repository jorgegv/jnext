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
#   5. the PRIVATE-tag path: a version NOT in releases.yaml leaves the AppStream
#      <releases> history untouched while still syncing spec + debian, and the
#      same version DOES get an entry once it is listed (the gate, both ways)
#   6. the releases.yaml match is anchored and dot-escaped, so a version can
#      neither prefix-match nor regex-match a sibling entry
#   7. the flatpak manifest (local-source, no version tag) is left untouched —
#      the script neither requires a `tag:` nor rewrites the manifest
#   8. a missing anchor makes the script FAIL LOUD (non-zero) rather than write
#      a half-synced file
#   9. the Makefile bump-* recipes gate the commit/tag on sync-version.sh with
#      `&&`, so a sync failure aborts the bump (never commits a broken state)
#  10. mkdocs.yml's extra.doc_release — the "This version" shown in the user
#      guide header — is updated too, so the guide always states the version it
#      was built from. (The re-render that must accompany it is not exercised
#      here: this fake root deliberately has no src/doc/user-guide, which is the
#      state the script skips rendering in.)
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
# mkdocs.yml carries extra.doc_release, the user guide header's "This version".
# Copied (not synthesised) so a change to the real key's name or indentation
# fails here rather than silently skipping the field on the next bump.
cp "$repo/mkdocs.yml"                                        "$tmp/"

# releases.yaml — the PUBLIC-release allowlist sync-version.sh gates the
# AppStream <releases> edit on ($root/releases.yaml, sync-version.sh:77-88).
# It MUST exist in the fake root: without it the gate never matches, every
# version takes the private path, and the public-path assertions below fail.
# (That omission is exactly GH #60 — the suite went red the day the gate
# landed and stayed red for 46 tags.) Only the versions this test drives
# through the PUBLIC path are listed; 9.9.1/9.9.11/9.9.12/9.9.13/9.9.14 are
# deliberately absent so they exercise the private path.
releases="$tmp/releases.yaml"
printf 'releases:\n  - v9.9.9\n  - v9.9.10\n' > "$releases"

spec="$tmp/packaging/rpm/jnext.spec"
metainfo="$tmp/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"
deb="$tmp/packaging/debian/changelog"
mkdocs="$tmp/mkdocs.yml"
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
# 10 — the user guide header's "This version" tracks version.yaml. Asserted
# separately from the packaging fields above: it is the only synced field a
# reader sees at a glance, so a silent miss here misinforms every visitor.
if grep -qE '^  doc_release:[[:space:]]*v9\.9\.9$' "$mkdocs"; then
    ok "mkdocs.yml doc_release updated to v9.9.9 (guide header 'This version')"
else
    bad "mkdocs.yml doc_release was not updated to v9.9.9 (got: $(grep -m1 -E '^  doc_release:' "$mkdocs" || echo '<no doc_release line>'))"
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

# 5a — PRIVATE tag: a version absent from releases.yaml must leave the AppStream
# <releases> history untouched (that is the whole reason the gate exists — a
# per-merge bump-patch must not pollute the published release history), while
# STILL syncing spec + debian. The gate is metainfo-scoped, not a whole-file
# skip. Discriminative: deleting the releases.yaml gate flips this to FAIL.
rel_before=$(sed -n '/<releases>/,/<\/releases>/p' "$metainfo")
bash "$sync" 9.9.13 >/dev/null 2>&1; rc=$?
rel_after=$(sed -n '/<releases>/,/<\/releases>/p' "$metainfo")
if [ "$rc" -eq 0 ] \
   && [ "$rel_before" = "$rel_after" ] \
   && ! grep -qE '<release version="9\.9\.13"' "$metainfo" \
   && grep -qE '^Version:[[:space:]]*9\.9\.13$'  "$spec" \
   && head -n1 "$deb" | grep -qE '^jnext \(9\.9\.13-1\)'; then
    ok "private tag: metainfo <releases> untouched, spec+debian still synced"
else
    bad "private tag: metainfo history changed, or spec/debian were not synced"
fi

# 5b — the same version, now LISTED, does get its <release> entry. Pairs with
# 5a to prove releases.yaml membership is what decides, not something else
# (a gate that always denies would pass 5a alone). Prior public history kept.
printf '  - v9.9.13\n' >> "$releases"
bash "$sync" 9.9.13 >/dev/null 2>&1
if grep -qE '<release version="9\.9\.13"' "$metainfo" \
   && grep -qE '<release version="9\.9\.10"' "$metainfo"; then
    ok "same version once listed in releases.yaml gets its <release> entry"
else
    bad "listing the version in releases.yaml did not add its <release> entry"
fi

# 6 — the releases.yaml match must be ANCHORED and DOT-ESCAPED
# (sync-version.sh:78-82). Two distinct regex faults, one row each:
#   9.9.1  must not prefix-match the listed `- v9.9.10`
#   9.9.14 must not dot-match the listed `- v9x9x14`
# Both must therefore take the PRIVATE path and add no <release> entry.
# Discriminative: dropping the `([[:space:]]|$)` anchor fails the first;
# dropping the ver_esc dot-escaping fails the second.
printf '  - v9x9x14\n' >> "$releases"
bash "$sync" 9.9.1  >/dev/null 2>&1
bash "$sync" 9.9.14 >/dev/null 2>&1
if ! grep -qE '<release version="9\.9\.1"'  "$metainfo" \
   && ! grep -qE '<release version="9\.9\.14"' "$metainfo"; then
    ok "releases.yaml match is anchored and dot-escaped (no sibling match)"
else
    bad "a version matched a SIBLING releases.yaml entry (unanchored/unescaped)"
fi

# 7 — the real flatpak manifest carries no version tag and must be left alone:
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

# 8 — fail loud on a missing anchor (remove %changelog from the spec)
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

# 9 — Makefile gates the bump commit/tag on sync-version.sh with &&
# shellcheck disable=SC2016  # the $$newver literal is what we grep for, not a var to expand
if grep -qE 'bash packaging/sync-version\.sh "\$\$newver" &&' "$repo/Makefile"; then
    ok "bump-* recipes chain sync-version.sh with && (abort on failure)"
else
    bad "bump-* recipes do NOT gate on sync-version.sh success (found ';')"
fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}\n" "$pass" "$fail"
[ "$fail" -eq 0 ]
