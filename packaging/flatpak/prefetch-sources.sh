#!/usr/bin/env bash
#
# prefetch-sources.sh [<manifest> [<state-dir>]]
#
# Fetch the manifest's `type: file` sources (url + sha256) into flatpak-builder's
# download cache, retrying a bad fetch, so flatpak-builder finds them and does not
# download them itself (GH #497).
#
# Why: specnext.com sometimes answers a runner with a ~12 KB page instead of the
# 70 MB NextZXOS zip, and flatpak-builder (1.4.x has no retry option) fails the
# whole build with "Wrong sha256 checksum". Here every attempt is checked against
# the manifest's sha256; a wrong body is discarded and fetched again, up to 3
# retries, after 2, 4 and 8 seconds. The cache layout is flatpak-builder's own
# (flatpak-builder(1), --extra-sources): <state-dir>/downloads/<sha256>/<basename>.
#
# Best effort by design: if the fetch still fails after the retries, or no
# downloader is installed, it says so and exits 0 with nothing cached, and
# flatpak-builder then makes its own attempt exactly as it did before this
# script existed. A source already in the cache with the right sha256 is kept.
#
# Test seams (never needed in production): PREFETCH_FETCH = command run as
# `$PREFETCH_FETCH <url> <dest>` instead of curl; PREFETCH_SLEEP = command run as
# `$PREFETCH_SLEEP <seconds>` instead of sleep.
set -u
export LC_ALL=C LANG=C

manifest=${1:-packaging/flatpak/io.github.zxjogv.jnext.yml}
state=${2:-.flatpak-builder}
fetch=${PREFETCH_FETCH:-}
nap=${PREFETCH_SLEEP:-sleep}
retries=3

if [ -z "$fetch" ]; then
    if command -v curl >/dev/null 2>&1; then
        fetch=default_curl
        default_curl() { curl --fail --silent --show-error --location --output "$2" "$1"; }
    else
        echo "prefetch-sources: curl not found; flatpak-builder will download the sources itself" >&2
        exit 0
    fi
fi

# url/sha256 pairs: a `url:` line followed by its `sha256:` line.
pairs=$(awk '
    /^[[:space:]]*(- )?url:/    { u=$NF }
    /^[[:space:]]*sha256:/      { if (u != "") print u, $NF; u="" }
' "$manifest")
[ -n "$pairs" ] || { echo "prefetch-sources: no url/sha256 source in $manifest" >&2; exit 2; }

while read -r url sum; do
    dir="$state/downloads/$sum"
    dest="$dir/$(basename "$url")"
    if [ -f "$dest" ] && [ "$(sha256sum "$dest" | cut -d' ' -f1)" = "$sum" ]; then
        echo "prefetch-sources: $(basename "$url") already cached"
        continue
    fi
    mkdir -p "$dir"
    attempt=0; wait_s=2; got=0
    while :; do
        rm -f "$dest.part"
        if $fetch "$url" "$dest.part" && [ "$(sha256sum "$dest.part" | cut -d' ' -f1)" = "$sum" ]; then
            mv "$dest.part" "$dest"; got=1; break
        fi
        rm -f "$dest.part"
        [ "$attempt" -lt "$retries" ] || break
        attempt=$((attempt + 1))
        echo "prefetch-sources: $url: bad or failed fetch; retry $attempt of $retries in ${wait_s} s" >&2
        $nap "$wait_s"
        wait_s=$((wait_s * 2))
    done
    if [ "$got" -eq 1 ]; then
        echo "prefetch-sources: $(basename "$url") cached"
    else
        echo "prefetch-sources: $url: giving up after $((retries + 1)) attempts; flatpak-builder will try itself" >&2
    fi
done <<<"$pairs"
exit 0
