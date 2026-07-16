#!/usr/bin/env bash
#
# sync-version.sh <version>
#
# Propagate a version number from version.yaml into the hand-maintained
# packaging files that carry a hard-coded copy of it. Called by the Makefile
# `bump-*` targets so a bump updates EVERY version-bearing file at once —
# version.yaml is the single source of truth, this keeps the rest aligned.
#
# The CPack-generated packages (make package-rpm/deb/win/macos) already derive
# their version from version.yaml via CMake's PROJECT_VERSION, so they are NOT
# touched here. This script handles only the files a person wrote by hand:
#   - packaging/rpm/jnext.spec          Version: + a matching %changelog entry
#   - packaging/assets/*.metainfo.xml   the AppStream <releases> history
#   - packaging/debian/changelog        the Debian changelog
#
# The flatpak manifest is NOT listed: it builds from the local checkout
# (`type: git` + `path`), carrying no `tag: vX.Y.Z` to keep in sync.
#
# Idempotent: re-running with the same version is a no-op (each field is only
# updated/prepended when it does not already reflect <version>).
#
set -euo pipefail

ver=${1:?usage: sync-version.sh <version>}
root=$(cd "$(dirname "$0")/.." && pwd)

spec="$root/packaging/rpm/jnext.spec"
metainfo="$root/packaging/assets/io.github.zxjogv.jnext.metainfo.xml"
debchangelog="$root/packaging/debian/changelog"

# Fail loud up front if any target file or the anchor an edit depends on is
# missing. Without this, e.g. a spec with no `%changelog` line would get its
# `Version:` rewritten but not its changelog — a silently inconsistent file
# (rpmbuild warns/errors when the top %changelog version != Version:). Better
# to abort the whole bump than to commit a half-synced tree.
for f in "$spec" "$metainfo" "$debchangelog"; do
    [ -f "$f" ] || { echo "sync-version: missing file: $f" >&2; exit 1; }
done
grep -qE '^Version:'                              "$spec"     || { echo "sync-version: no 'Version:' line in $spec" >&2; exit 1; }
grep -qE '^%changelog$'                           "$spec"     || { echo "sync-version: no '%changelog' line in $spec" >&2; exit 1; }
grep -qE '<releases>'                             "$metainfo" || { echo "sync-version: no '<releases>' element in $metainfo" >&2; exit 1; }

maint="ZXjogv <zx@jogv.es>"
d_iso=$(date +%F)              # 2026-07-16
d_rpm=$(date '+%a %b %d %Y')   # Thu Jul 16 2026
d_deb=$(date -R)               # RFC 2822 (Debian changelog format)

# --- rpm spec: Version: field ------------------------------------------------
sed -i -E "s/^(Version:[[:space:]]*).*/\1$ver/" "$spec"

# --- rpm spec: %changelog — prepend a matching entry (rpmbuild expects the top
#     changelog version to match Version:, else it warns/errors) --------------
if ! grep -qE "^\* .* - ${ver}-1\$" "$spec"; then
    awk -v ver="$ver" -v d="$d_rpm" -v m="$maint" '
        /^%changelog$/ {
            print
            print "* " d " " m " - " ver "-1"
            print "- New release " ver "."
            print ""
            next
        }
        { print }
    ' "$spec" > "$spec.tmp" && mv "$spec.tmp" "$spec"
fi

# --- AppStream metainfo: prepend a <release> to the history ------------------
if ! grep -qE "<release version=\"$ver\"" "$metainfo"; then
    sed -i -E "s#([[:space:]]*)(<releases>)#\1\2\n\1  <release version=\"$ver\" date=\"$d_iso\"/>#" "$metainfo"
fi

# --- Debian changelog: prepend a new entry -----------------------------------
if ! head -n1 "$debchangelog" | grep -qE "^jnext \(${ver}-1\)"; then
    { printf 'jnext (%s-1) unstable; urgency=medium\n\n  * New release %s.\n\n -- %s  %s\n\n' \
        "$ver" "$ver" "$maint" "$d_deb"
      cat "$debchangelog"
    } > "$debchangelog.tmp"
    mv "$debchangelog.tmp" "$debchangelog"
fi

echo "sync-version: aligned $ver into spec, metainfo, debian changelog"
