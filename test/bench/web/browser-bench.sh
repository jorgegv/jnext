#!/usr/bin/env bash
#
# browser-bench.sh - the browser bench behind `make bench-web`.
#
#   bash test/bench/web/browser-bench.sh <wasm-build-dir>
#
# The regression NEX set (plus the bench NEX, see browser-bench.mjs) in the host's Chrome, Chrome at
# 2x CPU throttle and the host's Firefox, through the shipped jnext.mount() page. Writes
# test/bench/web-baseline-<sha>.txt. No gate depends on it and CI never runs it: a speed bound
# measures the host, so the numbers live in that committed file and on the issue.
#
# Variables (all optional): CONFIGS=chrome,chrome-x2,firefox  SET=<program,...> (default: the whole
# set)  W=250 M=600 (warm-up and window frames)  PROFILE=<program,...> (Chrome CPU profile of those
# cells, with a profiling build in WEB_BUILD)  WEB_BUILD=<dir> (default: the argument)
# PROFILE_DIR=<dir> (where .cpuprofile files are copied)  RESULTS=<file> (instead of the
# committed test/bench file)  FIREFOX_MODE=headless (default xvfb: headed Firefox on a
# private Xvfb; headless Firefox 159 gives the canvas no WebGL context)  RAW_DIR=<dir> (every cell's raw JSON, for analysis)  NO_REPEAT=1 (no repeat runs)  QUIET_WAIT=0 (do not wait for a quiet host: the pass is marked LOADED)  JNEXT_BENCH_CHROME=<chrome binary>.
#
# Standalone script, run with bash (not sourced by a row runner), so it owns its trap.
#
# Everything it creates lives in ONE scratch directory under ~/tmp that is deleted at the end,
# the minimal NextZXOS card included (copyrighted: built from the cached distribution image at
# run time by tools/make-minimal-sdcard-image.sh, served from here only, never committed,
# uploaded or traced). The browser runs pinned to the fastest physical cores, as bench.sh derives
# them, and nothing is audible (Chrome --mute-audio, Firefox volume 0).
set -uo pipefail

export LC_ALL=C LANG=C

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
BUILD=${WEB_BUILD:-${1:?usage: browser-bench.sh <wasm-build-dir>}}

die() { echo "browser-bench: $*" >&2; exit 2; }

BUILD=$(cd "$BUILD" && pwd) || die "no build directory $BUILD"
EMSDK=$(bash "$ROOT/test/provision-emsdk.sh" --print-path) || die "no provisioned emsdk (make emsdk)"
NODE=$(ls -d "$EMSDK"/node/*/bin/node)
PW=$(bash "$ROOT/test/provision-playwright.sh" --print-path) || die "no provisioned Playwright (make playwright)"
DIST="${JNEXT_BROWSER_DIST_IMAGE:-$HOME/.jnext/sdcard/cspect-next-1gb.img}"
[[ -f "$DIST" ]] || die "no distribution SD image at $DIST (make sdcard-image)"
for t in mcopy sfdisk python3 timeout taskset flock git; do command -v "$t" >/dev/null 2>&1 || die "required tool missing: $t"; done
CONFIGS=${CONFIGS:-chrome,chrome-x2,firefox}
case ",$CONFIGS," in *,firefox,*) command -v firefox >/dev/null 2>&1 || die "no firefox on PATH";; esac
CHROME=${JNEXT_BENCH_CHROME:-/usr/bin/google-chrome}
case ",$CONFIGS," in *,chrome*) [[ -x "$CHROME" ]] || die "no Chrome at $CHROME (JNEXT_BENCH_CHROME)";; esac

BUILD_VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")
VERSION=$(sed -n 's/^version: *//p' "$ROOT/version.yaml")
[[ -n "$BUILD_VERSION" && "$BUILD_VERSION" == "$VERSION" ]] || die "build version [$BUILD_VERSION] != version.yaml [$VERSION]: rebuild"
for f in "jnext-$VERSION.js" "jnext-$VERSION.wasm"; do [[ -f "$BUILD/$f" ]] || die "$BUILD/$f is not built (make wasm-web)"; done

# One bench at a time, machine-wide for this repo (the same lock as bench.sh).
exec 200>"$ROOT/test/bench/.lock"
flock -n 200 || { echo "browser-bench: another benchmark holds the lock, waiting..."; flock 200; }

mkdir -p "$HOME/tmp"
SCRATCH=$(mktemp -d "$HOME/tmp/jnext-browser-bench.XXXXXX")
bench_cleanup() {
    [[ -n "${SCRATCH:-}" ]] && rm -rf "$SCRATCH"
    return 0
}
# INT and TERM exit explicitly (128+signo): a handler that only cleans up would let the script
# resume without the scratch directory (HS-40..42 scan this file for exactly that).
trap 'bench_cleanup' EXIT
trap 'bench_cleanup; exit 130' INT
trap 'bench_cleanup; exit 143' TERM

bash "$ROOT/tools/make-minimal-sdcard-image.sh" "$DIST" "$SCRATCH/card.img" >"$SCRATCH/card.log" 2>&1 \
    || { cat "$SCRATCH/card.log" >&2; die "cannot build the minimal card"; }
CARD_SHA256=$(sha256sum "$SCRATCH/card.img" | cut -d' ' -f1)

# Core set: the fastest scaling_max_freq class, one logical CPU per physical core (the first of
# thread_siblings_list), DERIVED as bench.sh derives its core, never hard-coded.
CORES=""
max_khz=0
for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_max_freq; do
    [[ -r "$f" ]] || continue
    k=$(<"$f"); (( k > max_khz )) && max_khz=$k
done
if (( max_khz > 0 )); then
    declare -A seen=()
    for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_max_freq; do
        [[ -r "$f" ]] || continue
        k=$(<"$f")
        (( k == max_khz )) || continue
        d=${f%/cpufreq/scaling_max_freq}
        sib=$(<"$d/topology/thread_siblings_list")
        first=${sib%%[,-]*}
        [[ -n "${seen[$first]:-}" ]] && continue
        seen[$first]=1
        CORES+="${CORES:+,}$first"
    done
    CORES=$(tr ',' '\n' <<<"$CORES" | sort -n | paste -sd, -)
fi

SHA=$(git -C "$ROOT" rev-parse --short HEAD)
OUT="$ROOT/test/bench/web-baseline-$SHA.txt"
[[ -n "${PROFILE:-}" ]] && OUT="$SCRATCH/web-profile.txt"
[[ -n "${RESULTS:-}" ]] && OUT="$RESULTS"

export BENCH_ROOT="$ROOT" BENCH_BUILD="$BUILD" BENCH_VERSION="$VERSION" BENCH_SCRATCH="$SCRATCH" \
       BENCH_SHA256="$CARD_SHA256" BENCH_OUT="$OUT" BENCH_CONFIGS="$CONFIGS" BENCH_SET="${SET:-}" \
       BENCH_W="${W:-250}" BENCH_M="${M:-600}" BENCH_PROFILE="${PROFILE:-}" BENCH_CORES="$CORES" \
       BENCH_EMSDK=6.0.13 BENCH_FIREFOX="${FIREFOX_MODE:-xvfb}" BENCH_RAW_DIR="${RAW_DIR:-}" BENCH_NO_REPEAT="${NO_REPEAT:-0}" BENCH_QUIET_WAIT="${QUIET_WAIT:-1}" JNEXT_PLAYWRIGHT_DIR="$PW" JNEXT_BENCH_CHROME="$CHROME"

pin=()
[[ -n "$CORES" ]] && pin=(taskset -c "$CORES")
rc=0
# Not under ulimit -v: V8 and wasm reserve far more address space than 4 GiB.
timeout --kill-after=30s 21600s "${pin[@]}" "$NODE" "$HERE/browser-bench.mjs" || rc=$?
if [[ -n "${PROFILE:-}" && -n "${PROFILE_DIR:-}" ]]; then
    mkdir -p "$PROFILE_DIR" && cp "$SCRATCH"/*.cpuprofile "$PROFILE_DIR"/ 2>/dev/null
    cp "$OUT" "$PROFILE_DIR"/ 2>/dev/null
fi
[[ $rc -eq 0 ]] && echo "browser-bench: results in $OUT"
exit $rc
