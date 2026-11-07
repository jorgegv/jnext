#!/usr/bin/env bash
#
# provision-playwright.sh - provide the pinned Playwright (playwright-core) and
# its Chromium headless shell the browser smoke test (`make wasm-browser-test`)
# drives, on any machine, the same way locally and in CI.
#
# WHAT. playwright-core (the npm package: no dependencies, no test runner) and
# the Chromium headless shell that its browsers.json names for that version. Both
# are fetched once into the user cache, are used at test time only, are never
# committed and never shipped. The script runs on emsdk's pinned node, so there
# is no second node; the browser is launched by explicit executablePath, so the
# Playwright registry layout is not needed.
#
# PINS, verified 2026-11-06:
#   playwright-core 1.65.0   two independent downloads matched, and the sha512
#                            equals the registry's published `integrity`.
#   headless shell 158.0.8127.18 (browsers.json revision 1254, the URL pattern
#                            of coreBundle.js cftUrl); the vendor publishes no
#                            checksum, so two independent downloads matched.
#
# USAGE
#   bash test/provision-playwright.sh               resolve, else download + install
#   bash test/provision-playwright.sh --print-path  resolve only: print the root, or exit 1
#
# RESOLUTION: only ${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools/playwright-<V>/,
# and only when its stamp (written LAST, with the pins) exists.
#
# Env: JNEXT_PLAYWRIGHT_URL / _SHA256 and JNEXT_PLAYWRIGHT_BROWSER_URL / _SHA256
#      override the pins / sources (the harness proves a wrong checksum is
#      refused).
set -euo pipefail

export LC_ALL=C LANG=C

VERSION=1.65.0
BROWSER_VERSION=158.0.8127.18

URL="${JNEXT_PLAYWRIGHT_URL:-https://registry.npmjs.org/playwright-core/-/playwright-core-${VERSION}.tgz}"
SHA256="${JNEXT_PLAYWRIGHT_SHA256:-315247c55a793ca760b2f05bdcee0e6a53fd65b1f22d1d3de8323c8c9395add3}"
BROWSER_URL="${JNEXT_PLAYWRIGHT_BROWSER_URL:-https://cdn.playwright.dev/builds/cft/${BROWSER_VERSION}/linux64/chrome-headless-shell-linux64.zip}"
BROWSER_SHA256="${JNEXT_PLAYWRIGHT_BROWSER_SHA256:-18352d22cb0cdf520cbff2ad8c41391f0e02a72886cb53b0fde4bdfc4bd075d6}"

CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/jnext/tools"
CACHED="$CACHE_ROOT/playwright-$VERSION"
STAMP_NAME=.jnext-provisioned
STAMP_BODY="playwright $VERSION $SHA256 $BROWSER_VERSION $BROWSER_SHA256"

resolve() {
    if [[ -f "$CACHED/$STAMP_NAME" && "$(cat "$CACHED/$STAMP_NAME")" == "$STAMP_BODY" \
          && -x "$CACHED/chromium/chrome-headless-shell-linux64/chrome-headless-shell" ]]; then
        echo "$CACHED"; return 0
    fi
    return 1
}

if [[ "${1:-}" == "--print-path" ]]; then
    resolve
    exit
fi

if found=$(resolve); then
    echo "playwright: using $found"
    exit 0
fi

echo "playwright: not provisioned; installing $VERSION + Chromium headless shell $BROWSER_VERSION into $CACHED (a 135 MB download)"

missing=""
for tool in curl sha256sum tar python3 timeout flock; do
    command -v "$tool" >/dev/null 2>&1 || missing+=" $tool"
done
if [[ -n "$missing" ]]; then
    echo "playwright: required tool(s) missing:$missing" >&2
    exit 1
fi

mkdir -p "$CACHE_ROOT"

# One provisioner at a time per version; a run that waited re-resolves.
exec 9>"$CACHE_ROOT/playwright-$VERSION.lock"
flock 9
if found=$(resolve); then
    echo "playwright: using $found"
    exit 0
fi

WORK=$(mktemp -d "$CACHE_ROOT/playwright-build.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# Download one archive and verify it BEFORE anything is unpacked.
fetch_verified() {
    local url=$1 want=$2 dest=$3 actual
    curl -fsSL --retry 3 --max-time 1800 -o "$dest" "$url"
    actual=$(sha256sum "$dest" | cut -d' ' -f1)
    if [[ "$actual" != "$want" ]]; then
        echo "playwright: SHA-256 MISMATCH for $url" >&2
        echo "  expected $want" >&2
        echo "  got      $actual" >&2
        exit 2
    fi
}

mkdir "$WORK/dl" "$WORK/stage"
fetch_verified "$URL"         "$SHA256"         "$WORK/dl/playwright-core.tgz"
fetch_verified "$BROWSER_URL" "$BROWSER_SHA256" "$WORK/dl/browser.zip"
echo "playwright: sha256 verified (playwright-core, headless shell)"

mkdir -p "$WORK/stage/node_modules/playwright-core" "$WORK/stage/chromium"
tar -xzf "$WORK/dl/playwright-core.tgz" -C "$WORK/stage/node_modules/playwright-core" --strip-components=1
# python's zipfile drops the executable bits: restore them for the files that
# had them in the archive.
python3 -I - "$WORK/dl/browser.zip" "$WORK/stage/chromium" <<'PY'
import os, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
dest = os.path.realpath(sys.argv[2])
for i in z.infolist():
    target = os.path.realpath(os.path.join(dest, i.filename))
    if not target.startswith(dest + os.sep):
        sys.exit("playwright: unsafe path in the archive: " + i.filename)
    z.extract(i, dest)
    mode = i.external_attr >> 16
    if mode and not i.is_dir():
        os.chmod(target, mode & 0o777)
PY
[[ -f "$WORK/stage/node_modules/playwright-core/package.json" ]] \
    || { echo "playwright: the npm archive holds no package.json" >&2; exit 1; }
[[ -x "$WORK/stage/chromium/chrome-headless-shell-linux64/chrome-headless-shell" ]] \
    || { echo "playwright: the zip holds no chrome-headless-shell" >&2; exit 1; }

printf '%s\n' "$STAMP_BODY" > "$WORK/stage/$STAMP_NAME"
if [[ -e "$CACHED" ]]; then rm -rf "$CACHED"; fi
mv -T "$WORK/stage" "$CACHED"
echo "playwright: installed $CACHED"
