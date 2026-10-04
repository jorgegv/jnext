#!/usr/bin/env bash
#
# sync-version.sh <version>
#
# Do the part of a version bump that still has to be COMMITTED, after the
# Makefile `bump-*` targets have written <version> into version.yaml — the
# single source of truth. Everything else READS version.yaml when it is built,
# so a bump does not edit it:
#   - CMake (PROJECT_VERSION -> the binary, every CPack package, the macOS
#     bundle plist); the package-* recipes read it for artifact names
#   - packaging/rpm/jnext.spec          `--define jnext_version` at rpmbuild
#                                       time; the top %changelog entry is
#                                       generated from it
#   - packaging/debian/changelog        written at build time by
#                                       packaging/gen-debian-changelog.sh
#   - mkdocs.yml, mkdocs-devguide.yml   extra.doc_release, set at render time
#                                       by src/doc/version_hook.py
#   - the flatpak manifest              builds from the local checkout, no tag
#
# What remains, and why it cannot be derived at build time:
#   - packaging/assets/*.metainfo.xml   the AppStream <releases> history, for
#                                       PUBLIC releases only (releases.yaml).
#                                       Each entry carries the release DATE,
#                                       which releases.yaml does not record and
#                                       a source tarball has no git to look up,
#                                       so it is written once, here, on the day.
#   - doc/user-guide, doc/developer-guide
#                                       the committed renders show "This
#                                       version" on every page (offline reading
#                                       of a clone), so they are re-rendered.
#
# Idempotent: re-running with the same version is a no-op.
#
set -euo pipefail

ver=${1:?usage: sync-version.sh <version>}
root=$(cd "$(dirname "$0")/.." && pwd)

metainfo="$root/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"

# Fail loud up front if the file or the anchor the edit depends on is missing,
# rather than letting a bump commit a tree that silently skipped it.
[ -f "$metainfo" ] || { echo "sync-version: missing file: $metainfo" >&2; exit 1; }
grep -qE '<releases>' "$metainfo" || { echo "sync-version: no '<releases>' element in $metainfo" >&2; exit 1; }

d_iso=$(date +%F)              # 2026-07-16

# --- AppStream metainfo: prepend a <release> for PUBLIC releases only ---------
# The metainfo <releases> is the AppStream release history shown in software
# centres / Flathub. It must list ONLY the public releases — the tags in
# releases.yaml — not the private per-merge patch tags, otherwise every
# bump-patch pollutes the history with a version that was never published.
# The bump-* targets add the tag to releases.yaml BEFORE calling this script
# (when the user opts in), so a public release is already listed there by the
# time we get here; a private bump never is.
releases_yaml="$root/releases.yaml"
# Escape dots so the version can't regex-match a sibling (same discipline as
# packaging/add-release.sh). The trailing anchor rejects prefix matches
# (v0.98.2 must not match v0.98.29).
ver_esc=$(printf '%s' "$ver" | sed 's/[.]/\\./g')
if grep -qE "^[[:space:]]*-[[:space:]]*v${ver_esc}([[:space:]]|\$)" "$releases_yaml" 2>/dev/null; then
    if ! grep -qE "<release version=\"$ver\"" "$metainfo"; then
        sed -i -E "s#([[:space:]]*)(<releases>)#\1\2\n\1  <release version=\"$ver\" date=\"$d_iso\"/>#" "$metainfo"
    fi
else
    echo "sync-version: v$ver not in releases.yaml (private tag) — metainfo <releases> left unchanged"
fi

# --- user guide: re-render ----------------------------------------------------
# "This version" is baked into every rendered page from version.yaml (by
# src/doc/version_hook.py), and docs-userguide-check byte-diffs a fresh render
# against the committed output. So a bump WITHOUT a re-render leaves a tree
# that fails its own docs gate: refuse the bump rather than commit that.
# Re-render only where there is a guide to render. The contract test drives this
# script against a synthetic root holding just the packaging files, so absent
# sources are a legitimate state — but a tree that HAS the sources and no mkdocs
# would commit a version.yaml saying v$ver beside a guide that does not, so that
# case fails loud instead.
if [ -d "$root/src/doc/user-guide" ]; then
    if command -v mkdocs >/dev/null 2>&1; then
        make -C "$root" --no-print-directory docs-userguide >/dev/null
    else
        echo "sync-version: mkdocs not installed — cannot re-render the user guide," >&2
        echo "  which would then still say the previous version. Install" >&2
        echo "  mkdocs-material before bumping." >&2
        exit 1
    fi
fi

# --- developer guide: the same, one document over (GH #44) -------------------
# It additionally needs graphviz, because make docs-devguide renders the .dot
# figures before mkdocs runs. Missing either tool is the same fault as above.
if [ -d "$root/src/doc/developer-guide" ]; then
    if command -v mkdocs >/dev/null 2>&1 && command -v dot >/dev/null 2>&1; then
        make -C "$root" --no-print-directory docs-devguide >/dev/null
    else
        echo "sync-version: mkdocs or graphviz not installed — cannot re-render the" >&2
        echo "  developer guide, which would then still say the previous version." >&2
        echo "  Install both before bumping." >&2
        exit 1
    fi
fi

echo "sync-version: $ver — metainfo checked, user guide and developer guide re-rendered"
