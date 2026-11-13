#!/usr/bin/env bash
#
# browser-smoke.sh - run the browser specs in headless Chromium (`make wasm-browser-test`).
#
#   bash test/browser/browser-smoke.sh <wasm-build-dir> [spec.mjs:EXPECTED ...]
#
# Specs default to `smoke.mjs:33` (rows BRW-01..33, this directory) and `image.mjs:8`
# (rows WBI-PW-01..08, the SD-card image and the warm start in the browser) and `api.mjs:31`
# (rows WEBAPI-01..31, the JavaScript API: jnext.mount and its controller) and `package.mjs:1`
# (row WEBPKG-BR-01: the extracted jnext-<ver>-web.zip of `make package-web`, served alone,
# boots the example page), `widget.mjs:17` (rows WIDGET-01..17: the ready-made widget and the
# guide's two embedding snippets, run verbatim), `touch.mjs:17` (rows TKB-01..17: the on-screen keyboard
# under touch-device emulation) and `writes.mjs:17` (rows WBW-01..17: the guest's writes
# kept across page loads, against a native journal oracle), `snapshots.mjs:11` (rows BSN-01..11: .sna, .z80,
# .szx and .jns loaded in the page and compared with native runs, and player.saveSnapshot()) and `../bench/web/bench-stats-test.mjs:12` (rows
# WBENCH-01..12: the arithmetic of the browser bench, synthetic data, no browser) and `crt.mjs:13` (rows
# BCRT-01..13: the CRT effect, crtemu on SDL's WebGL context, and its fallbacks). Further specs
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
#   beeper_demo.wav   the same TAP as a 44.1 kHz 8-bit mono cassette recording (tap2wav.py).
#   oracle/*.png  native runs of the same arguments (build/sdl-release on the dummy
#                 SDL drivers; keyed screens by the headless typist, which SdlApp lacks).
# The browser is the pinned headless shell of test/provision-playwright.sh, the node
# the emsdk's own (test/provision-emsdk.sh). Anything missing is a hard failure, never a skip.
set -uo pipefail

export LC_ALL=C LANG=C

BUILD=${1:?usage: browser-smoke.sh <wasm-build-dir> [spec.mjs:EXPECTED ...]}
shift
SPECS=("smoke.mjs:33" "image.mjs:8" "api.mjs:31" "package.mjs:1" "widget.mjs:17" "writes.mjs:17" "touch.mjs:17" "snapshots.mjs:11" "../bench/web/bench-stats-test.mjs:12" "crt.mjs:13" "$@")

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
python3 -I "$HERE/tap2wav.py" "$ROOT/test/00regression/tap/beeper_demo.tap" "$SCRATCH/beeper_demo.wav" || die "tap2wav failed"

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
oracle wav      sdl      --machine 48k --load "$SCRATCH/beeper_demo.wav" --delayed-screenshot "$O/wav.png" --delayed-screenshot-frames 1500 --delayed-automatic-exit-frames 1600
oracle tilemap  sdl      --machine next --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/tilemap_demo.nex" \
                         --delayed-screenshot "$O/tilemap.png" --delayed-screenshot-frames 150 --delayed-automatic-exit-frames 400
oracle keys     headless --machine 48k --delayed-keypress-frames 200 p --delayed-keypress-frames 215 1 --delayed-keypress-frames 230 2 \
                         --delayed-keypress-frames 245 enter --delayed-screenshot "$O/keys.png" --delayed-screenshot-frames 320 --delayed-automatic-exit-frames 500
oracle key1     headless --machine 48k --delayed-keypress-frames 200 1 --delayed-screenshot "$O/key1.png" --delayed-screenshot-frames 400 --delayed-automatic-exit-frames 600
oracle pad      headless --machine 48k --delayed-keypress-frames 200 p --delayed-keypress-frames 215 1 --delayed-keypress-frames 230 6 \
                         --delayed-keypress-frames 245 enter --delayed-screenshot "$O/pad.png" --delayed-screenshot-frames 420 --delayed-automatic-exit-frames 600
oracle kmx      headless --machine 48k --load "$ROOT/test/fixtures/tap/key_matrix_demo.tap" --magic-port 0xCAFE --magic-port-mode line \
                         --delayed-keypress-frames 400 p --delayed-screenshot "$O/kmx.png" --delayed-screenshot-frames 500 --delayed-automatic-exit-frames 600

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

# The write journal oracle (writes.mjs WBW-01..03): the save_counter program natively, twice, on a
# clone of the card with --sdcard-writes. The journal after each run (w1.jsdw, w2.jsdw) and the
# guest's magic-port lines are what the page's stored record must equal byte for byte. The clone
# is deleted; the journals hold only the sectors the guest wrote.
cp --reflink=auto "$SCRATCH/card.img" "$O/w.card"
for n in 1 2; do
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$O/w.cfg" \
        timeout --foreground --kill-after=5s 300s "$NATIVE" --headless --sdcard "$O/w.card" --silent --machine next \
        --sdcard-sha256 "$CARD_SHA256" --sdcard-writes "$O/w.jsdw" \
        --rtc 2026-07-10T08:55:00 --magic-port 0xCAFE --magic-port-mode line \
        --load "$ROOT/test/fixtures/nex/save_counter.nex" --delayed-automatic-exit-frames 400 \
        >"$O/w$n.log" 2>&1 || { cat "$O/w$n.log" >&2; die "oracle writes run $n failed"; }
    cp "$O/w.jsdw" "$O/w$n.jsdw"
    grep -a "^SAVE $n OK\$" "$O/w$n.log" >/dev/null || { cat "$O/w$n.log" >&2; die "oracle writes run $n did not save"; }
done
rm -rf "$O/w.card" "$O/w.cfg" "$O/w.jsdw"

# The esxDOS stand-in's kept file (writes.mjs WBW-13..17): esx_save_counter.nex natively, twice
# (e1.jsdw, e2.jsdw), then the same program built with another constant byte (e3.jsdw: it reads the
# old save, as a changed NEX does on a real Next), then the original again (e4.jsdw), always on one
# journal of one clone of the card. The clone is deleted; each journal holds only the kept file.
cp --reflink=auto "$SCRATCH/card.img" "$O/e.card"
n=0
for nex in esx_save_counter esx_save_counter esx_save_counter_b esx_save_counter; do
    n=$((n + 1))
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$O/e.cfg" \
        timeout --foreground --kill-after=5s 300s "$NATIVE" --headless --sdcard "$O/e.card" --silent --machine next \
        --sdcard-sha256 "$CARD_SHA256" --sdcard-writes "$O/e.jsdw" \
        --rtc 2026-07-10T08:55:00 --magic-port 0xCAFE --magic-port-mode line \
        --load "$ROOT/test/fixtures/nex/$nex.nex" --delayed-automatic-exit-frames 400 \
        >"$O/e$n.log" 2>&1 || { cat "$O/e$n.log" >&2; die "oracle esx run $n failed"; }
    cp "$O/e.jsdw" "$O/e$n.jsdw"
    grep -a "^ESX $n OK\$" "$O/e$n.log" >/dev/null || { cat "$O/e$n.log" >&2; die "oracle esx run $n did not save"; }
done
rm -rf "$O/e.card" "$O/e.cfg" "$O/e.jsdw"

# The snapshot fixtures (snapshots.mjs BSN-01..11), all made here from OUR programs on clones of the
# card, headless (--delayed-snapshot is headless-only), and their native oracle pictures: the
# snapshot loaded by the SDL jnext, a screenshot 50 frames after the restore. s48.z80 is the 48K
# .sna converted by a FOREIGN writer (sna2z80.py, from the .z80 format spec: jnext has no .z80
# saver); shadow.bin leaves the bank-7 screen (green paper) showing, so a restore that ignores the
# paging port shows the 128K menu instead; other48.jns is taken on a card one MiB larger (another
# card identity). Deleted with the scratch directory.
fixture() {
    local name=$1 grow=$2; shift 2
    local clone="$SCRATCH/$name.card" cfg="$SCRATCH/$name.cfg" rc=0
    cp --reflink=auto "$SCRATCH/card.img" "$clone"
    [[ "$grow" == grow ]] && truncate -s +1M "$clone"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy JNEXT_CONFIG_DIR="$cfg" \
        timeout --foreground --kill-after=5s 300s "$NATIVE" --headless --sdcard "$clone" --silent "$@" \
        >"$SCRATCH/out/$name.fixture.log" 2>&1 || rc=$?
    rm -rf "$clone" "$cfg"
    [[ $rc -eq 0 ]] || { cat "$SCRATCH/out/$name.fixture.log" >&2; die "fixture $name failed (rc=$rc)"; }
}
printf '\xF3\x3E\x0F\x01\xFD\x7F\xED\x79\x21\x00\xC0\x11\x01\xC0\x01\xFF\x17\x36\x00\xED\xB0\x21\x00\xD8\x11\x01\xD8\x01\xFF\x02\x36\x20\xED\xB0\x18\xFE' >"$SCRATCH/shadow.bin"
fixture s48      no   --machine 48k --load "$TAPF" --delayed-snapshot "$SCRATCH/s48.sna" --delayed-snapshot-frames 300 --delayed-automatic-exit-frames 320
python3 -I "$HERE/sna2z80.py" "$SCRATCH/s48.sna" "$SCRATCH/s48.z80" || die "sna2z80 failed"
# The state both loaders build, as the SAME writer sees it: the .sna and the converted .z80 each loaded,
# run one frame and saved again as .sna; the two files must be byte-identical (BSN-02). A picture
# cannot tell a wrong SP or PC from a right one, this can.
fixture rtsna    no   --load "$SCRATCH/s48.sna" --delayed-snapshot "$SCRATCH/rtsna.sna" --delayed-snapshot-frames 1 --delayed-automatic-exit-frames 10
fixture rtz80    no   --load "$SCRATCH/s48.z80" --delayed-snapshot "$SCRATCH/rtz80.sna" --delayed-snapshot-frames 1 --delayed-automatic-exit-frames 10
fixture s128     no   --machine 128k --inject "$SCRATCH/shadow.bin" --inject-org 8000 --inject-delay 250 \
                      --delayed-snapshot "$SCRATCH/s128.szx" --delayed-snapshot-frames 300 --delayed-automatic-exit-frames 320
fixture tilemap  no   --machine next --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/tilemap_demo.nex" \
                      --delayed-snapshot "$SCRATCH/tilemap.jns" --delayed-snapshot-frames 150 --delayed-automatic-exit-frames 170
fixture blue     no   --machine next --rtc 2026-07-10T08:55:00 --load "$ROOT/test/00regression/nex/blue.nex" \
                      --delayed-snapshot "$SCRATCH/blue.jns" --delayed-snapshot-frames 100 --delayed-automatic-exit-frames 120
fixture other48  grow --machine 48k --delayed-snapshot "$SCRATCH/other48.jns" --delayed-snapshot-frames 150 --delayed-automatic-exit-frames 170
oracle sna     sdl --load "$SCRATCH/s48.sna"  --delayed-screenshot "$O/sna.png"     --delayed-screenshot-frames 50 --delayed-automatic-exit-frames 120
oracle z80     sdl --load "$SCRATCH/s48.z80"  --delayed-screenshot "$O/z80.png"     --delayed-screenshot-frames 50 --delayed-automatic-exit-frames 120
oracle szx     sdl --load "$SCRATCH/s128.szx" --delayed-screenshot "$O/szx.png"     --delayed-screenshot-frames 50 --delayed-automatic-exit-frames 120
oracle jns     sdl --rtc 2026-07-10T08:55:00 --load "$SCRATCH/tilemap.jns" --delayed-screenshot "$O/jns.png" --delayed-screenshot-frames 50 --delayed-automatic-exit-frames 120
oracle bluejns sdl --rtc 2026-07-10T08:55:00 --load "$SCRATCH/blue.jns"    --delayed-screenshot "$O/bluejns.png" --delayed-screenshot-frames 50 --delayed-automatic-exit-frames 120

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
