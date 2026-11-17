#!/usr/bin/env bash
# docs-tree-same.sh <rendered-tree> <committed-tree>
#
# Exit 0 when a freshly rendered mkdocs site matches the committed one, 1 when it
# does not (the differing paths on stdout), 2 on a usage error. Used by
# docs-userguide-check and docs-devguide-check.
#
# Every file is compared byte for byte, except sitemap.xml.gz, which is compared
# DECOMPRESSED. mkdocs writes it with Python's gzip module, so its bytes depend on
# the zlib Python is linked against as well as on mkdocs: Fedora 45's Python uses
# zlib-ng, and its sitemap.xml.gz differs from Fedora 44's for identical content.
# The renderer fingerprint (mkdocs + material versions) does not see the zlib, so a
# byte diff reported a version gap as staleness. The uncompressed sitemap.xml next
# to it is still compared byte for byte, and the .gz must exist on both sides.
set -uo pipefail

[[ $# -eq 2 && -d "$1" && -d "$2" ]] || { echo "usage: $0 <rendered-tree> <committed-tree>" >&2; exit 2; }
a=$1 b=$2
rc=0

diff -r -q --exclude=sitemap.xml.gz "$a" "$b" || rc=1

gzlist() { (cd "$1" && find . -name sitemap.xml.gz | LC_ALL=C sort); }
la=$(gzlist "$a"); lb=$(gzlist "$b")
if [[ "$la" != "$lb" ]]; then
    echo "sitemap.xml.gz present in one tree only: rendered [$(echo $la)] / committed [$(echo $lb)]"
    rc=1
fi
while IFS= read -r g; do
    [[ -n "$g" && -f "$b/$g" ]] || continue
    if ! gzip -tq -- "$a/$g" 2>/dev/null || ! gzip -tq -- "$b/$g" 2>/dev/null; then
        echo "$g does not decompress"; rc=1
    elif ! cmp -s <(gzip -dc -- "$a/$g") <(gzip -dc -- "$b/$g"); then
        echo "$g differs (decompressed)"; rc=1
    fi
done <<<"$la"
exit $rc
