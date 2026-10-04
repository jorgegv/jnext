#!/usr/bin/env bash
#
# Contract tests for the version single source: version.yaml is the only file
# a bump edits by hand, and everything else either READS it at build time or is
# written by packaging/sync-version.sh (the part that must still be committed).
#
# Runs on throwaway fake roots, never touching the real tree. Asserts:
#   sync-version.sh (the bump-* targets' helper)
#   1. a PUBLIC version (listed in releases.yaml) gets its AppStream <release>
#      entry, and the previous entries are kept
#   2. running twice with the same version is idempotent
#   3. the PRIVATE-tag path: a version NOT in releases.yaml leaves <releases>
#      untouched, and the same version DOES get an entry once it is listed
#      (the gate, both ways)
#   4. the releases.yaml match is anchored and dot-escaped, so a version can
#      neither prefix-match nor regex-match a sibling entry
#   5. it edits NOTHING else: the rpm spec, both mkdocs configs, the Debian
#      packaging and the flatpak manifest stay byte-identical
#   6. a missing <releases> anchor makes it FAIL LOUD with no partial write
#   7. the Makefile bump-* recipes gate the commit/tag on it with `&&`
#   the single source itself
#   8. no version-bearing build input carries a copy of the version: neither
#      mkdocs config has a doc_release literal, the spec's Version: is the
#      jnext_version macro, packaging/debian has no committed `changelog`, and
#      the current version literal appears in no file under packaging/,
#      .github/, cmake/, CMakeLists.txt or mkdocs*.yml except the metainfo
#   packaging/gen-debian-changelog.sh
#   9. its top entry is version.yaml's version and the committed history
#      follows verbatim (and dpkg-parsechangelog reads it, where installed)
#  10. SOURCE_DATE_EPOCH pins the date, in English whatever the caller's locale
#  11. a missing or malformed version.yaml fails loud with nothing on stdout
#   packaging/rpm/jnext.spec (where rpmspec is installed)
#  12. without --define jnext_version the spec refuses to parse
#  13. with it, Version: and the top %changelog entry are that version, under a
#      non-English caller locale too
#   src/doc/version_hook.py (where mkdocs is importable)
#  14. it sets extra.doc_release to v<version.yaml>
#  15. a missing or malformed version.yaml raises, never a default
#
# Rows 12-15 need a tool that not every host has (CI's unit leg has no
# rpm-build); each prints SKIP there. CI's `package` job, which installs
# rpm-build and dpkg-dev, runs this same suite through `make package-test`.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root
repo=$(pwd)

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; BOLD='\033[1m'; RESET='\033[0m'
pass=0; fail=0; skip=0
ok()  { printf "  ${GREEN}PASS${RESET} %s\n" "$1"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %s\n" "$1"; fail=$((fail+1)); }
skp() { printf "  ${YELLOW}SKIP${RESET} %s\n" "$1"; skip=$((skip+1)); }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Fake root mirroring the layout sync-version.sh expects ($root =
# dirname(script)/..). Everything the script used to edit is copied in too, so
# row 5 can prove it no longer does.
mkdir -p "$tmp/packaging/assets" "$tmp/packaging/rpm" "$tmp/packaging/flatpak"
cp "$repo/packaging/sync-version.sh"                             "$tmp/packaging/"
cp "$repo/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"  "$tmp/packaging/assets/"
cp "$repo/packaging/rpm/jnext.spec"                              "$tmp/packaging/rpm/"
cp "$repo/packaging/flatpak/io.github.zxjogv.jnext.yml"          "$tmp/packaging/flatpak/"
cp -r "$repo/packaging/debian"                                   "$tmp/packaging/"
cp "$repo/mkdocs.yml" "$repo/mkdocs-devguide.yml"                "$tmp/"

# releases.yaml — the PUBLIC-release allowlist the <releases> edit is gated on.
# It MUST exist in the fake root: without it every version takes the private
# path and the public-path rows fail (that omission is GH #60). 9.9.1, 9.9.13
# and 9.9.14 are deliberately absent at first so they exercise the private path.
releases="$tmp/releases.yaml"
printf 'releases:\n  - v9.9.9\n  - v9.9.10\n' > "$releases"

metainfo="$tmp/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"
sync="$tmp/packaging/sync-version.sh"
others() { (cd "$tmp" && find packaging/rpm packaging/flatpak packaging/debian mkdocs.yml mkdocs-devguide.yml -type f -exec sha256sum {} + | sort); }
others_before=$(others)

printf "${BOLD}=== version single-source contract tests ===${RESET}\n\n"

# 1 — public version gets its entry; prior history kept
bash "$sync" 9.9.9  >/dev/null 2>&1
bash "$sync" 9.9.10 >/dev/null 2>&1
if grep -qE '<release version="9\.9\.10"' "$metainfo" \
   && grep -qE '<release version="9\.9\.9"' "$metainfo" \
   && grep -qE '<release version="1\.1\.0"' "$metainfo"; then
    ok "public version gets its <release> entry; previous entries kept"
else
    bad "public version: <release> entry missing, or prior history lost"
fi

# 2 — idempotency
before=$(cat "$metainfo")
bash "$sync" 9.9.10 >/dev/null 2>&1
if [ "$before" = "$(cat "$metainfo")" ]; then
    ok "second run with the same version is a no-op (idempotent)"
else
    bad "second run changed the metainfo (not idempotent)"
fi

# 3 — private tag leaves <releases> alone; listing it then adds the entry.
# The pair proves releases.yaml membership decides (a gate that always denied
# would pass the first half alone).
rel_before=$(sed -n '/<releases>/,/<\/releases>/p' "$metainfo")
bash "$sync" 9.9.13 >/dev/null 2>&1; rc=$?
rel_after=$(sed -n '/<releases>/,/<\/releases>/p' "$metainfo")
printf '  - v9.9.13\n' >> "$releases"
bash "$sync" 9.9.13 >/dev/null 2>&1
if [ "$rc" -eq 0 ] && [ "$rel_before" = "$rel_after" ] \
   && grep -qE '<release version="9\.9\.13"' "$metainfo"; then
    ok "private tag: <releases> untouched; the same version once listed gets its entry"
else
    bad "the releases.yaml gate did not decide the <releases> edit (private rc=$rc)"
fi

# 4 — anchored (9.9.1 must not prefix-match v9.9.10) and dot-escaped (9.9.14
# must not dot-match v9x9x14)
printf '  - v9x9x14\n' >> "$releases"
bash "$sync" 9.9.1  >/dev/null 2>&1
bash "$sync" 9.9.14 >/dev/null 2>&1
if ! grep -qE '<release version="9\.9\.1"'  "$metainfo" \
   && ! grep -qE '<release version="9\.9\.14"' "$metainfo"; then
    ok "releases.yaml match is anchored and dot-escaped (no sibling match)"
else
    bad "a version matched a SIBLING releases.yaml entry (unanchored/unescaped)"
fi

# 5 — after all of the above, nothing but the metainfo changed
if [ "$others_before" = "$(others)" ]; then
    ok "spec, mkdocs configs, debian/ and flatpak manifest untouched by every run"
else
    bad "sync-version.sh edited a file it no longer owns:"
    diff <(echo "$others_before") <(others) | sed 's/^/      /'
fi

# 6 — fail loud on a missing anchor, without writing
sed -i 's#<releases>#<releasez>#' "$metainfo"
before=$(cat "$metainfo")
if bash "$sync" 9.9.9 >/dev/null 2>&1; then
    bad "script succeeded despite a missing <releases> anchor"
elif [ "$before" = "$(cat "$metainfo")" ]; then
    ok "missing <releases> anchor -> fail loud, no partial write"
else
    bad "script failed but still rewrote the metainfo (partial write)"
fi

# 7 — Makefile gates the bump commit/tag on sync-version.sh with &&
# shellcheck disable=SC2016  # the $$newver literal is what we grep for
if [ "$(grep -cE 'bash packaging/sync-version\.sh "\$\$newver" &&' "$repo/Makefile")" -eq 3 ]; then
    ok "all three bump-* recipes chain sync-version.sh with && (abort on failure)"
else
    bad "a bump-* recipe does NOT gate on sync-version.sh success"
fi

# 8 — no copy of the version in a build input. The literal scan is word-bounded
# on both sides (a test's 999.1.1.1 must not read as 1.1.1) and scoped to the
# places a packaging or doc-config copy would be added, not to fixtures.
ver=$(sed -nE 's/^version: *([0-9]+\.[0-9]+\.[0-9]+) *$/\1/p' "$repo/version.yaml")
ver_re=$(printf '%s' "$ver" | sed 's/[.]/\\./g')
copies=$(cd "$repo" && git grep -nE "(^|[^0-9.])${ver_re}([^0-9.]|\$)" -- \
             packaging .github cmake CMakeLists.txt 'mkdocs*.yml' \
             ':!packaging/assets/io.github.zxjogv.jnext.metainfo.xml' 2>/dev/null)
if [ -n "$ver" ] \
   && ! grep -qE '^[[:space:]]*doc_release:' "$repo/mkdocs.yml" "$repo/mkdocs-devguide.yml" \
   && grep -qE '^Version:[[:space:]]+%\{jnext_version\}$' "$repo/packaging/rpm/jnext.spec" \
   && [ ! -e "$repo/packaging/debian/changelog" ] \
   && [ -z "$copies" ]; then
    ok "no build input carries a copy of version.yaml's version ($ver)"
else
    bad "a build input carries its own copy of the version (version.yaml says '$ver'):"
    [ -n "$copies" ] && printf '%s\n' "$copies" | sed 's/^/      /'
fi

# --- gen-debian-changelog.sh -------------------------------------------------
deb="$tmp/deb"
mkdir -p "$deb/packaging/debian"
cp "$repo/packaging/gen-debian-changelog.sh"         "$deb/packaging/"
cp "$repo/packaging/debian/changelog.history"        "$deb/packaging/debian/"
printf 'version: 9.9.9\n' > "$deb/version.yaml"
gen="$deb/packaging/gen-debian-changelog.sh"

# 9 — top entry from version.yaml, history verbatim after it
out=$(bash "$gen" 2>/dev/null); rc=$?
hist=$(cat "$deb/packaging/debian/changelog.history")
parsed=ok
if command -v dpkg-parsechangelog >/dev/null 2>&1; then
    printf '%s\n' "$out" > "$tmp/changelog"
    [ "$(dpkg-parsechangelog -l "$tmp/changelog" -S Version 2>/dev/null)" = "9.9.9-1" ] || parsed=no
fi
if [ "$rc" -eq 0 ] && head -n1 <<<"$out" | grep -qxF 'jnext (9.9.9-1) unstable; urgency=medium' \
   && [ "${out#*"$hist"}" = "" ] && [ "$parsed" = ok ]; then
    ok "debian changelog: top entry is version.yaml's 9.9.9-1, history follows verbatim"
else
    bad "debian changelog: wrong top entry, history not appended verbatim, or unparseable (rc=$rc parsed=$parsed)"
fi

# 10 — SOURCE_DATE_EPOCH pins the date; English names under a Spanish caller
a=$(LC_ALL=es_ES.UTF-8 LANG=es_ES.UTF-8 SOURCE_DATE_EPOCH=1700000000 bash "$gen" 2>/dev/null)
b=$(SOURCE_DATE_EPOCH=1700000000 bash "$gen" 2>/dev/null)
if [ "$a" = "$b" ] && grep -qxF ' -- ZXjogv <zx@jogv.es>  Tue, 14 Nov 2023 22:13:20 +0000' <<<"$a"; then
    ok "debian changelog: SOURCE_DATE_EPOCH pins the date, in the C locale"
else
    bad "debian changelog: date not reproducible or not in English"
fi

# 11 — missing / malformed version.yaml fails loud, prints nothing to build from
printf 'version: 9.9\n' > "$deb/version.yaml"
out1=$(bash "$gen" 2>/dev/null); rc1=$?
rm -f "$deb/version.yaml"
out2=$(bash "$gen" 2>/dev/null); rc2=$?
if [ "$rc1" -ne 0 ] && [ "$rc2" -ne 0 ] && [ -z "$out1$out2" ]; then
    ok "debian changelog: malformed or missing version.yaml -> fail loud, empty stdout"
else
    bad "debian changelog: a bad version.yaml produced output or rc 0 (rc=$rc1/$rc2)"
fi

# --- rpm spec ----------------------------------------------------------------
spec="$repo/packaging/rpm/jnext.spec"
if command -v rpmspec >/dev/null 2>&1; then
    # 12 — no define, no parse
    err=$(rpmspec -q --qf '%{VERSION}\n' "$spec" 2>&1); rc=$?
    if [ "$rc" -ne 0 ] && grep -q 'jnext_version is not defined' <<<"$err"; then
        ok "rpm spec: refuses to parse without --define jnext_version"
    else
        bad "rpm spec: parsed without jnext_version (rc=$rc): $(head -n1 <<<"$err")"
    fi
    # 13 — the define drives Version: and the top changelog entry
    v=$(LC_ALL=es_ES.UTF-8 LANG=es_ES.UTF-8 rpmspec -q --define 'jnext_version 9.9.9' --qf '%{VERSION}\n' "$spec" 2>/dev/null)
    top=$(LC_ALL=es_ES.UTF-8 LANG=es_ES.UTF-8 rpmspec -q --define 'jnext_version 9.9.9' --qf '[%{CHANGELOGNAME}\n]' "$spec" 2>/dev/null | head -n1)
    if [ "$v" = "9.9.9" ] && [ "$top" = "ZXjogv <zx@jogv.es> - 9.9.9-1" ]; then
        ok "rpm spec: Version: and top %changelog entry follow jnext_version (9.9.9)"
    else
        bad "rpm spec: Version='$v', top changelog='$top' (want 9.9.9 / ... - 9.9.9-1)"
    fi
else
    skp "rpm spec rows: rpmspec not installed"
    skp "rpm spec rows: rpmspec not installed"
fi

# --- mkdocs hook -------------------------------------------------------------
if python3 -c 'import mkdocs.exceptions' 2>/dev/null; then
    hk="$tmp/hook"; mkdir -p "$hk"
    hook_run() {   # prints doc_release, or the exception class name
        python3 - "$repo/src/doc/version_hook.py" "$hk/mkdocs.yml" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("version_hook", sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
class C:  # the two attributes on_config uses
    config_file_path = sys.argv[2]
    extra = {}
try:
    print(m.on_config(C()).extra["doc_release"])
except Exception as e:
    print(type(e).__name__)
PY
    }
    # 14
    printf 'version: 9.9.9\n' > "$hk/version.yaml"
    r=$(hook_run)
    if [ "$r" = "v9.9.9" ]; then
        ok "mkdocs hook: extra.doc_release = v<version.yaml>"
    else
        bad "mkdocs hook: got '$r', want v9.9.9"
    fi
    # 15
    printf 'version: 9.9\n' > "$hk/version.yaml"; r1=$(hook_run)
    rm -f "$hk/version.yaml";                    r2=$(hook_run)
    if [ "$r1" = PluginError ] && [ "$r2" = PluginError ]; then
        ok "mkdocs hook: malformed or missing version.yaml raises (no default)"
    else
        bad "mkdocs hook: malformed -> '$r1', missing -> '$r2' (want PluginError both)"
    fi
else
    skp "mkdocs hook rows: mkdocs not importable"
    skp "mkdocs hook rows: mkdocs not importable"
fi

printf "\n${BOLD}=== Results ===${RESET}\n"
printf "  ${GREEN}Pass: %d${RESET}  ${RED}Fail: %d${RESET}  ${YELLOW}Skip: %d${RESET}\n" "$pass" "$fail" "$skip"
[ "$fail" -eq 0 ]
