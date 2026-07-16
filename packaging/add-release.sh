#!/usr/bin/env bash
#
# add-release.sh <tag>
#
# Add a tag to the `releases:` list in releases.yaml so CI publishes a public
# GitHub Release for it (see doc/RELEASE-PROTOCOL.md). Called by the Makefile
# bump-minor / bump-major targets when the user opts in. Idempotent: a tag
# already listed is a no-op.
#
set -euo pipefail

tag=${1:?usage: add-release.sh <tag>}
f="$(cd "$(dirname "$0")/.." && pwd)/releases.yaml"

[ -f "$f" ] || { echo "add-release: $f not found" >&2; exit 1; }
grep -qE '^[[:space:]]*releases:' "$f" || { echo "add-release: no 'releases:' key in $f" >&2; exit 1; }

# Match the tag literally — escape regex metachars (dots) so v1.0.0 can't match
# a v1x0x0-shaped entry. Same discipline as the CI gate in release.yml.
esc=$(printf '%s' "$tag" | sed 's/[.]/\\./g')

# Already listed?
if grep -qE "^[[:space:]]*-[[:space:]]*${esc}[[:space:]]*$" "$f"; then
    echo "add-release: $tag already listed"
    exit 0
fi

if grep -qE '^[[:space:]]*releases:[[:space:]]*\[\][[:space:]]*$' "$f"; then
    # Empty inline list — turn `releases: []` into a block list with one entry.
    sed -i -E "s/^([[:space:]]*)releases:[[:space:]]*\[\][[:space:]]*$/\1releases:\n\1  - ${tag}/" "$f"
else
    # Block list already exists — insert the new tag right after `releases:`.
    sed -i -E "/^[[:space:]]*releases:[[:space:]]*$/a\\  - ${tag}" "$f"
fi

echo "add-release: added $tag to releases.yaml"
