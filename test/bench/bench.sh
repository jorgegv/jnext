#!/usr/bin/env bash
# JNEXT benchmark harness (Task 27 T1) — the measurement gate for every
# optimization task in doc/design/TASK27-OPTIMIZATION-PLAN.md.
#
# Runs the five canonical workloads through `jnext --benchmark N` (headless,
# uncapped), median-of-5, pinned to the fastest host core, serialised by a
# real flock. Results go to stdout AND test/bench/baseline-<git-sha>.txt so
# any later task can diff against the exact commit it branched from.
#
# Rules baked in (plan §0.4):
#   - Core selection is DERIVED at runtime from scaling_max_freq — never
#     hardcoded (hybrid P/E boxes differ ~40% between core classes).
#   - Median of 5 with min/max spread printed. Spread > 5% VOIDs the
#     workload (noted, run continues; final exit code is then non-zero).
#   - Only build/gui-release/jnext (Release) is measured. RelWithDebInfo
#     and Debug numbers are not comparable and the script refuses them.
#   - Primary metric is T-states/sec, not FPS.
#
# LIMITATION — spread does not catch CONSISTENT load. A background process
# that runs for the whole session depresses every run equally: spread stays
# under 5% while the medians are silently ~2x low (measured 2026-07-15: an
# orphaned jnext at 80% CPU gave boot-48k 175 fps at 1.4% spread, vs the
# ~329 fps reference). Always sanity-check medians against the last known
# baseline-<sha>.txt / the reference band in TASK27-OPTIMIZATION-PLAN.md;
# the loadavg warning below names the condition at run start.
#
# Usage: bash test/bench/bench.sh    (or: make bench)

set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$PROJECT_DIR/build/gui-release/jnext"
CACHE="$PROJECT_DIR/build/gui-release/CMakeCache.txt"
# The SD image jnext provisions and caches itself. `roms/` holds only the
# embedded boot ROM now — no SD image lives there (GH #75/#77), so a fixture
# path under the repo is no longer a thing to point at.
#
# NOTE: `make bench` is a manual target, not part of any test suite, so it
# reads the shared master rather than taking a private clone. The
# `boot-nextzxos` workload below DOES boot NextZXOS, and jnext opens the image
# read-write, so a benchmark run mutates the master (GH #77).
SD_IMAGE="${JNEXT_TEST_SD_IMAGE:-$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img}"
LOCK_FILE="$SCRIPT_DIR/.lock"
REPEATS=5
SPREAD_LIMIT_PCT=5.0

die() { echo "bench: ERROR: $*" >&2; exit 2; }

# --- Preconditions: the Release binary, and nothing but Release ---
[[ -x "$BIN" ]] || die "$BIN not found or not executable — build it with 'make gui-release' first"
[[ -f "$CACHE" ]] || die "$CACHE missing — build dir is not CMake-configured"
build_type=$(grep -oP '^CMAKE_BUILD_TYPE:\w+=\K.*' "$CACHE" || true)
[[ "$build_type" == "Release" ]] || \
    die "build/gui-release is configured as '${build_type:-<unset>}', not Release — refusing to benchmark it"
[[ -f "$SD_IMAGE" ]] || die "SD image missing: $SD_IMAGE (provision it with '$BIN --headless --sdcard-download-confirm', or point JNEXT_TEST_SD_IMAGE at an existing one)"

# --- Serialise: one benchmark at a time, machine-wide for this repo ---
# A real blocking flock, not a process scan: several agents may run overnight
# and a poisoned benchmark is the exact failure mode that corrupted TASK27A.
exec 200>"$LOCK_FILE"
if ! flock -n 200; then
    echo "bench: another benchmark holds $LOCK_FILE — waiting for it to finish..."
    flock 200
fi

# --- Core selection: fastest class, lowest-numbered core, DERIVED ---
# Pass 1: find the fastest core class. Pass 2: lowest-numbered core in it.
CORE=0
CORE_KHZ=0
CORE_NOTE=""
max_khz=0
for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_max_freq; do
    [[ -r "$f" ]] || continue
    khz=$(<"$f")
    (( khz > max_khz )) && max_khz=$khz
done
if (( max_khz == 0 )); then
    CORE_NOTE=" (no cpufreq sysfs — fell back to core 0, frequency unknown)"
else
    CORE=-1
    for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_max_freq; do
        [[ -r "$f" ]] || continue
        khz=$(<"$f")
        cpu=${f#/sys/devices/system/cpu/cpu}; cpu=${cpu%%/*}
        if (( khz == max_khz )) && { (( CORE < 0 )) || (( cpu < CORE )); }; then
            CORE=$cpu
        fi
    done
    CORE_KHZ=$max_khz
fi

# --- Load warning: a busy box biases medians without inflating spread ---
LOAD1=$(cut -d' ' -f1 /proc/loadavg 2>/dev/null || echo 0)
if awk -v l="$LOAD1" 'BEGIN { exit !(l > 1.5) }'; then
    echo "bench: WARNING: 1-min load average is $LOAD1 (> 1.5) — the box is not idle." >&2
    echo "bench: WARNING: consistent background load depresses ALL runs equally, so the" >&2
    echo "bench: WARNING: spread check will NOT catch it. Results may be uniformly low." >&2
fi

GIT_SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD)
OUT="$SCRIPT_DIR/baseline-$GIT_SHA.txt"
: > "$OUT"

emit() { echo "$@" | tee -a "$OUT"; }

emit "# jnext benchmark baseline"
emit "# date:   $(date '+%Y-%m-%d %H:%M:%S')"
emit "# host:   $(hostname)"
emit "# commit: $(git -C "$PROJECT_DIR" rev-parse HEAD)"
emit "# binary: $BIN (CMAKE_BUILD_TYPE=$build_type)"
emit "# core:   $CORE @ ${CORE_KHZ} kHz (fastest class, lowest-numbered)$CORE_NOTE"
emit "# load1:  $LOAD1 (1-min loadavg at run start; > 1.5 biases medians without inflating spread)"
emit "# runs:   median of $REPEATS per workload; spread = (max-min)/median of fps"
emit ""

# --- The five canonical workloads: name|machine|load-file|frames ---
# 48K workloads get 600 frames, Next ones 400 (plan T1, as amended).
# bifrost is the busy-48K control (NOT HALT-idle): the BIFROST* ENGINE
# multicolour demo, same asset the screenshot regression uses
# (test/00regression/regression_tests.conf: bifrost-screen1).
WORKLOADS=(
    "boot-48k|48k||600"
    "boot-nextzxos|next||400"
    "copper-demo|next|test/00regression/nex/copper_demo.nex|400"
    "beast|next|test/00regression/nex/beast.nex|400"
    "bifrost|48k|test/00regression/tap/bifrost.tap|600"
)

any_void=0

for spec in "${WORKLOADS[@]}"; do
    IFS='|' read -r name machine load frames <<< "$spec"

    # --benchmark-label: BENCH lines carry the same canonical workload name
    # used in this file's headers/MEDIAN rows, so baseline files are greppable
    # by workload (review MINOR, Task 27 T1).
    args=(--headless --machine "$machine" --sdcard "$SD_IMAGE"
          --benchmark "$frames" --benchmark-label "$name")
    if [[ -n "$load" ]]; then
        [[ -f "$PROJECT_DIR/$load" ]] || die "workload $name: asset missing: $load"
        args+=(--load "$PROJECT_DIR/$load")
    fi

    emit "## $name  (machine=$machine frames=$frames${load:+ load=$load})"

    bench_lines=()
    fps_list=()
    for (( run=1; run<=REPEATS; run++ )); do
        line=$(taskset -c "$CORE" "$BIN" "${args[@]}" 2>/dev/null | grep '^BENCH ' || true)
        if [[ -z "$line" ]]; then
            die "workload $name run $run produced no BENCH line — aborting (harness fault)"
        fi
        bench_lines+=("$line")
        fps=$(sed -n 's/.* fps=\([0-9.]*\).*/\1/p' <<< "$line")
        fps_list+=("$fps")
        emit "  run$run: $line"
    done

    # Median / min / max by fps (fps and tstates_per_sec are proportional for
    # a fixed workload — same tstates_per_frame — so one ordering serves both).
    sorted=$(printf '%s\n' "${fps_list[@]}" | sort -g)
    min_fps=$(head -1 <<< "$sorted")
    max_fps=$(tail -1 <<< "$sorted")
    med_fps=$(sed -n "$(( (REPEATS + 1) / 2 ))p" <<< "$sorted")
    med_line=""
    for l in "${bench_lines[@]}"; do
        [[ "$l" == *" fps=$med_fps "* ]] && med_line="$l" && break
    done
    med_tsps=$(sed -n 's/.* tstates_per_sec=\([0-9.]*\).*/\1/p' <<< "$med_line")

    spread_pct=$(awk -v mn="$min_fps" -v mx="$max_fps" -v md="$med_fps" \
                 'BEGIN { printf "%.2f", (md > 0) ? (mx - mn) * 100.0 / md : 999 }')
    verdict="OK"
    if awk -v s="$spread_pct" -v lim="$SPREAD_LIMIT_PCT" 'BEGIN { exit !(s > lim) }'; then
        verdict="VOID (spread ${spread_pct}% > ${SPREAD_LIMIT_PCT}% — rerun on an idle box)"
        any_void=1
    fi

    emit "  MEDIAN $name: fps=$med_fps tstates_per_sec=$med_tsps min_fps=$min_fps max_fps=$max_fps spread=${spread_pct}% $verdict"
    emit ""
done

emit "# result file: $OUT"
if (( any_void )); then
    emit "# NOTE: at least one workload was VOID (spread > ${SPREAD_LIMIT_PCT}%) — exit 1"
    exit 1
fi
