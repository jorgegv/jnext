#!/usr/bin/env bash
#
# gen-debian-changelog.sh — print the debian/changelog for the version in
# version.yaml, to be written into the debian/ directory a build uses:
#
#   cp -r packaging/debian debian
#   bash packaging/gen-debian-changelog.sh > debian/changelog
#   dpkg-buildpackage -us -uc -b
#
# dpkg-buildpackage takes the package version from the top changelog entry, and
# version.yaml is the single source of truth for it, so that entry is generated
# here at build time rather than committed. A bump therefore never edits the
# Debian packaging. The committed history (packaging/debian/changelog.history)
# follows it unchanged. It is deliberately NOT named `changelog`: a debian/
# directory copied without running this script has no changelog, so the build
# stops instead of quietly packaging the history's last version.
#
# The date is SOURCE_DATE_EPOCH when set (reproducible builds), else now. The
# changelog format requires English day and month names; `date -R` prints them
# in every locale, and LC_ALL=C is set anyway, per the project's child-process
# rule.
#
set -euo pipefail
export LC_ALL=C

root=$(cd "$(dirname "$0")/.." && pwd)
history="$root/packaging/debian/changelog.history"
[ -f "$history" ] || { echo "gen-debian-changelog: missing $history" >&2; exit 1; }

# Same field and X.Y.Z form cmake/GenerateVersion.cmake reads.
ver=$(sed -nE 's/^version: *([0-9]+\.[0-9]+\.[0-9]+) *$/\1/p' "$root/version.yaml" 2>/dev/null | head -n1)
[ -n "$ver" ] || { echo "gen-debian-changelog: no 'version: X.Y.Z' line in $root/version.yaml" >&2; exit 1; }

d=$(date -u -R -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}")
printf 'jnext (%s-1) unstable; urgency=medium\n\n  * Release %s.\n\n -- ZXjogv <zx@jogv.es>  %s\n\n' \
    "$ver" "$ver" "$d"
cat "$history"
