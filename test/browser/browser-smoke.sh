#!/usr/bin/env bash
#
# browser-smoke.sh - run the browser specs in headless Chromium (`make wasm-browser-test`).
#
#   bash test/browser/browser-smoke.sh <wasm-build-dir> [spec.mjs:EXPECTED ...]
#
# Specs default to `smoke.mjs:33` (rows BRW-01..33, this directory) and `image.mjs:8`
# (rows WBI-PW-01..08, the SD-card image and the warm start in the browser) and `api.mjs:29`
# (rows WEBAPI-01..29, the JavaScript API: jnext.mount and its controller) and `package.mjs:1`
# (row WEBPKG-BR-01: the extracted jnext-<ver>-web.zip of `make package-web`, served alone,
# boots the example page), `widget.mjs:16` (rows WIDGET-01..16: the ready-made widget and the
# guide's two embedding snippets, run verbatim) and `../bench/web/bench-stats-test.mjs:12` (rows
# WBENCH-01..12: the arithmetic of the browser bench, synthetic data, no browser). Further specs
# (the JavaScript API rows) are passed as extra `file:count` arguments or added to
# SPECS below: each runs on the SDK's node, prints PASS|FAIL rows and a `Total:` line,
# and its row count is pinned here, so a spec that loses rows fails the run.
#
# Standalone script, run with bash (not sourced by a row runner), so it owns its trap.
#
# What it provides to the specs, all in ONE scratch directory under ~/tmp that is
# deleted at the end:
#   card.img      the 40 MiB minimal NextZXOS card, built from the cached distribution
#                 image by tools/make-minimal-sdcard-image.sh. It is copyrighted
#                 content: it lives only here, the page gets it through the harness's
#                 request handler, and it is never committed, uploaded or traced
#                 (Playwright tracing, HAR and video stay off).
#   beeper_demo.tzx   the TAP of the regression suite re-wrapped as TZX.
#   oracle/*.png  native runs of the same arguments (build/sdl-release on the dummy
#                 SDL drivers; keyed screens by the headless typist, which SdlApp lacks).
# The browser is the pinned headless shell of test/provision-playwright.sh, the node
# the emsdk's own (test/provision-emsdk.sh). Anything missing is a hard failure, never a skip.
set -uo pipefail

export LC_ALL=C LANG=C

BUILD=${1:?usage: browser-smoke.sh <wasm-build-dir> [spec.mjs:EXPECTED ...]}
shift
SPECS=("smoke.mjs:33" "image.mjs:8" "api.mjs:29" "package.mjs:1" "widget.mjs:16" "../bench/web/bench-stats-test.mjs:12" "$@")

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD=$(cd "$BUILD" && pwd) || exit 2

die() { echo "browser-smoke: $*" >&2; exit 2; }

EMSDK=$(bash "$ROOT/test/provision-emsdk.sh" --print-path) || die "no provisioned emsdk (make emsdk)"
NODE=$(ls -d "$EMSDK"/node/*/bin/node)
PW=$(bash "$ROOT/test/provision-playwright.sh" --print-path) || die "no provisioned Playwright (make playwright)"
NATIVE="$ROOT/build/sdl-release/jnext"
[[ -x "$NATIVE" ]] || die "$NATIVE is not built (make sdl-release): it is the oracle"
DIST="${JNEXT_BROWSER_DIST_IMAGE:-$HOME/.jnext/sdcard/cspect-next-1gb.img}"
[[ -f "$DIST" ]] || die "no distribution SD image at $DIST (make sdcard-image)"
for t in mcopy sfdisk python3 timeout; do command -v "$t" >/dev/null 2>&1 || die "required tool missing: $t"; done

BUILD_VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")
VERSION=$(sed -n 's/^version: *//p' "$ROOT/version.yaml")
[[ -n "$BUILD_VERSION" && "$BUILD_VERSION" == "$VERSION" ]] || die "build version [$BUILD_VERSION] != version.yaml [$VERSION]: rebuild"
for f in "jnext-$VERSION.js" "jnext-$VERSION.wasm"; do [[ -f "$BUILD/$f" ]] || die "$BUILD/$f is not built (make wasm-web)"; done
PKGZIP="$BUILD/jnext-$VERSION-web.zip"
[[ -f "$PKGZIP" ]] || die "$PKGZIP is not built (make package-web)"

mkdir -p "$HOME/tmp"
SCRATCH=$(mktemp -d "$HOME/tmp/jnext-browser-smoke.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
mkdir "$SCRATCH/oracle" "$SCRATCH/out"

bash "$ROOT/tools/make-minimal-sdcard-image.sh" "$DIST" "$SCRATCH/card.img" >"$SCRATCH/out/card.log" 2>&1 \
    || { cat "$SCRATCH/out/card.log" >&2; die "cannot build the minimal card"; }
# For image.mjs: the card's sha256 and a ZIP of it made by a FOREIGN writer (CPython zipfile,
# deflate), both computed here at run time (the card is reproducible for one mtools version
# only, so no digest is ever pinned). Same scratch directory, same deletion.
CARD_SHA256=$(sha256sum "$SCRATCH/card.img" | cut -d' ' -f1)
python3 -I -c 'import sys, zipfile
with zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED) as z:
    z.write(sys.argv[1], "minimal.img")' "$SCRATCH/card.img" "$SCRATCH/card.img.zip" || die "cannot zip the card"
ZIP_SHA256=$(sha256sum "$SCRATCH/card.img.zip" | cut -d' ' -f1)
# The shipped zip, extracted by a FOREIGN reader: package.mjs serves only this directory.
mkdir "$SCRATCH/pkg" && python3 -I -m zipfile -e "$PKGZIP" "$SCRATCH/pkg" || die "cannot extract $PKGZIP"
python3 -I "$HERE/tap2tzx.py" "$ROOT/test/00regression/tap/beeper_demo.tap" "$SCRATCH/beeper_demo.tzx" || die "tap2tzx failed"

# Native oracle: oracle <name> <sdl|headless> <jnext args...>; the card is a throwaway clone.
oracle() {
    local name=$1 mode=$2; shift 2
    local clone="$SCRATCH/oracle/$name.card" cfg="$SCRATCH/oracle/$name.cfg" rc=0 front=()
    cp --reflink=auto "$SCRATCH/card.img" "$clone"
    [[ "$mode" == headless ]] && front=(--headless)
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$cfg" \
        timeout --foreground --kill-after=5s 300s "$NATIVE" "${front[@]}" --sdcard "$clone" --silent "$@" \
        >"$SCRATCH/oracle/$name.log" 2>&1 || rc=$?
    rm -rf "$clone" "$cfg"
    [[ $rc -eq 0 && -f "$SCRATCH/oracle/$name.png" ]] || { cat "$SCRATCH/oracle/$name.log" >&2; die "oracle $name failed (rc=$rc)"; }
}
O="$SCRATCH/oracle"
TAPF="$ROOT/test/00regression/tap/beeper_demo.tap"
oracle boot     sdl      --machine 48k --delayed-screenshot "$O/boot.png"  --delayed-screenshot-frames 150 --delayed-automatic-exit-frames 400
oracle idle     sdl      --machine 48k --delayed-screenshot "$O/idle.png"  --delayed-screenshot-frames 320 --delayed-automatic-exit-frames 500
oracle tap      sdl      --machine 48k --load "$TAPF" --delayed-screenshot "$O/tap.png" --delayed-screenshot-frames 300 --delayed-automatic-exit-frames 600
oracle tzx      sdl      --machine 48k --load "$SCRATCH/beeper_demo.tzx" --delayed-screenshot "$O/tzx.png" --delayed-screenshot-frames 400 --delayed-automatic-exit-frames 700
oracle tilemap  sdl      --machine next --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/tilemap_demo.nex" \
                         --delayed-screenshot "$O/tilemap.png" --delayed-screenshot-frames 150 --delayed-automatic-exit-frames 400
oracle keys     headless --machine 48k --delayed-keypress-frames 200 p --delayed-keypress-frames 215 1 --delayed-keypress-frames 230 2 \
                         --delayed-keypress-frames 245 enter --delayed-screenshot "$O/keys.png" --delayed-screenshot-frames 320 --delayed-automatic-exit-frames 500
oracle key1     headless --machine 48k --delayed-keypress-frames 200 1 --delayed-screenshot "$O/key1.png" --delayed-screenshot-frames 400 --delayed-automatic-exit-frames 600
oracle pad      headless --machine 48k --delayed-keypress-frames 200 p --delayed-keypress-frames 215 1 --delayed-keypress-frames 230 6 \
                         --delayed-keypress-frames 245 enter --delayed-screenshot "$O/pad.png" --delayed-screenshot-frames 420 --delayed-automatic-exit-frames 600

# The per-subsystem log level oracle (BRW-27): the same arguments natively print the line the page must print.
cp --reflink=auto "$SCRATCH/card.img" "$O/copper.card"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$O/copper.cfg" \
    timeout --foreground --kill-after=5s 300s "$NATIVE" --sdcard "$O/copper.card" --silent --machine next \
    --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/copper_demo.nex" --log-level copper=debug \
    --delayed-automatic-exit-frames 300 >"$O/copper.log" 2>&1 || die "oracle copper failed"
rm -rf "$O/copper.card" "$O/copper.cfg"

# The frame period / CPU speed oracle (BRW-31): the BENCH line of the same arguments natively.
cp --reflink=auto "$SCRATCH/card.img" "$O/beast.card"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$O/beast.cfg" \
    timeout --foreground --kill-after=5s 300s "$NATIVE" --headless --sdcard "$O/beast.card" --silent --machine next \
    --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/beast.nex" --benchmark 450 >"$O/beast.bench" 2>"$O/beast.err" \
    || die "oracle beast failed"
rm -rf "$O/beast.card" "$O/beast.cfg"

export JNEXT_PLAYWRIGHT_DIR="$PW" SMOKE_BUILD="$BUILD" SMOKE_ROOT="$ROOT" SMOKE_SCRATCH="$SCRATCH" \
       SMOKE_ORACLE="$O" SMOKE_CARD_SHA256="$CARD_SHA256" SMOKE_ZIP_SHA256="$ZIP_SHA256" SMOKE_OUT="$SCRATCH/out" SMOKE_VERSION="$VERSION" SMOKE_BUILD_VERSION="$BUILD_VERSION" SMOKE_PKG="$SCRATCH/pkg"

TOTAL=0; PASS=0; FAIL=0; SKIP=0; FAULT=0
for spec in "${SPECS[@]}"; do
    file=${spec%%:*}; want=${spec##*:}
    [[ "$file" == /* ]] || file="$HERE/$file"
    [[ -f "$file" && "$want" =~ ^[0-9]+$ ]] || die "bad spec [$spec] (file:count)"
    log="$SCRATCH/out/$(basename "$file").log"
    rc=0
    # Not under ulimit -v: V8 and wasm reserve far more address space than 4 GiB.
    timeout --kill-after=10s 1500s "$NODE" "$file" >"$log" 2>&1 || rc=$?
    cat "$log"
    line=$(grep '^Total:' "$log" | tail -1)
    if [[ -z "$line" ]]; then echo "browser-smoke: $file printed no Total line (rc=$rc)" >&2; FAULT=1; continue; fi
    t=$(awk '{print $2}' <<<"$line"); p=$(awk '{print $4}' <<<"$line"); f=$(awk '{print $6}' <<<"$line"); s=$(awk '{print $8}' <<<"$line")
    if [[ "$t" != "$want" ]]; then echo "browser-smoke: $file reported $t rows, expected $want" >&2; FAULT=1; fi
    TOTAL=$((TOTAL + t)); PASS=$((PASS + p)); FAIL=$((FAIL + f)); SKIP=$((SKIP + s))
    [[ $rc -eq 0 ]] || FAULT=1
done

echo
echo "===================================================="
printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$TOTAL" "$PASS" "$FAIL" "$SKIP"
[[ $FAIL -eq 0 && $SKIP -eq 0 && $FAULT -eq 0 ]]
