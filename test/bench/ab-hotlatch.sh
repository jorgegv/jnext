#!/usr/bin/env bash
# GH #276 §11 item 3 — the HOT-LATCH measurement, as a committed harness.
#
# PROVENANCE, because a commit message on this branch gets it wrong: this file,
# `make bench-hotlatch`, the `JNEXT_BENCH_WATCH` fixture and the `jnext_debug`
# link were all added in commit 0f20b4868 ("close the nine blocking items of the
# B2 review"). The NEXT commit, e2f66ec96, is titled "land the §11-item-3
# hot-latch harness" and touched no bench file at all — it is the doc-correction
# commit. The messages are not rewritten because two review rounds cite these
# SHAs; the correction lives here instead, where a reader of the harness finds
# it. `test/bench/hotlatch-e2f66ec96.txt`'s name and its `sha=` line are
# CORRECT and mean something else: the tree the measurement was taken on.
#
# `bench.sh` measures ONE binary at a time and cannot arm a watch, so it cannot
# express the question §11 item 3 asks: what does a range `Mem` watch that HITS
# cost? This script answers it the way Task 27's `c1-ab-e596fa6a.txt` did and
# §6.2 of the design prescribes — INTERLEAVED A/B, every variant run
# back-to-back within each pair on the same core, medians over pairs, spread per
# variant — because interleaving makes a slow drift hit every variant equally
# where a sequential run attributes it to the code.
#
# The watch itself comes from the `JNEXT_BENCH_WATCH` fixture in
# `src/platform/headless_app.cpp` (env-gated, zero cost unset). The first round
# of B2 measured this with an UNCOMMITTED hook, and the number then survived only
# as prose — which is how an independent re-derivation came back with a ~35%
# different magnitude. Hence this file.
#
# Usage:
#   bash test/bench/ab-hotlatch.sh <baseline-jnext> [<new-jnext>] [pairs]
#
# <baseline-jnext> is a Release binary from the commit being compared against
# (build it in its own worktree); <new-jnext> defaults to this tree's
# build/gui-release/jnext. Output goes to stdout and to
# test/bench/hotlatch-<git-sha>.txt.
#
# VARIANTS
#   B    the baseline binary, nothing armed
#   U    this tree, nothing armed              -> the NO-SUBSCRIBER cost
#   P    this tree, --persistent-breakpoints only, no subscription
#   A1   this tree, Mem[0x0000,0x3FFF] Write   -> §11 item 3's named case
#   A2   this tree, Mem[0x0000,0xFFFF] Write   -> EVERY write; the upper bound
#
# P exists because §6.3's armed rows conflated the latch cost with the
# pre-existing `--persistent-breakpoints` per-instruction `should_break()`
# lookup. A − P is what isolates the latch; A − B is what a user would feel.
set -euo pipefail
export LC_ALL=C LANG=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

die() { echo "ab-hotlatch: ERROR: $*" >&2; exit 2; }

BASE_BIN="${1:-}"
NEW_BIN="${2:-$PROJECT_DIR/build/gui-release/jnext}"
PAIRS="${3:-5}"
[[ -n "$BASE_BIN" ]] || die "usage: $0 <baseline-jnext> [<new-jnext>] [pairs]"
[[ -x "$BASE_BIN" ]] || die "baseline binary not executable: $BASE_BIN"
[[ -x "$NEW_BIN"  ]] || die "new binary not executable: $NEW_BIN (make gui-release)"

# Both binaries must be Release, for the reason bench.sh refuses otherwise.
for b in "$BASE_BIN" "$NEW_BIN"; do
    "$b" --version >/dev/null 2>&1 || die "$b does not run"
done

SD_MASTER="${JNEXT_TEST_SD_IMAGE:-$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img}"
[[ -f "$SD_MASTER" ]] || die "SD image missing: $SD_MASTER"
# A PRIVATE clone: jnext opens the image read-write and boot-nextzxos mutates it,
# and a benchmark must not alter the input the next run measures. $HOME, not
# /tmp: reflink cannot cross a filesystem and /tmp is a RAM tmpfs here.
CLONE="$HOME/tmp/jnext-ab-hotlatch.img"
mkdir -p "$HOME/tmp"
[[ -f "$CLONE" ]] || cp --reflink=auto "$SD_MASTER" "$CLONE"

# The fastest core, derived at runtime exactly as bench.sh derives it — never
# hardcoded, because hybrid P/E boxes differ ~40% between core classes.
CORE=$(for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq; do
           n=${c#/sys/devices/system/cpu/cpu}; n=${n%%/*}
           echo "$(cat "$c") $n"
       done | sort -rn | head -1 | awk '{print $2}')

SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null || echo unknown)
OUT="$SCRIPT_DIR/hotlatch-$SHA.txt"
: > "$OUT"
emit() { printf '%s\n' "$*" | tee -a "$OUT"; }

emit "# GH #276 §11 item 3 — hot-latch interleaved A/B"
emit "# sha=$SHA core=$CORE pairs=$PAIRS load1_start=$(cut -d' ' -f1 /proc/loadavg)"
emit "# baseline=$BASE_BIN"
emit "# new=$NEW_BIN"
emit ""

declare -A VBIN VENV
VBIN[B]="$BASE_BIN";  VENV[B]=""
VBIN[U]="$NEW_BIN";   VENV[U]=""
VBIN[P]="$NEW_BIN";   VENV[P]="JNEXT_BENCH_WATCH=p"
VBIN[A1]="$NEW_BIN";  VENV[A1]="JNEXT_BENCH_WATCH=0000-3fff"
VBIN[A2]="$NEW_BIN";  VENV[A2]="JNEXT_BENCH_WATCH=0000-ffff"

run_one() {   # $1 variant  $2 machine  $3 frames  $4 load-or-empty
    local v=$1 machine=$2 frames=$3 load=$4
    local args=(--headless --machine "$machine" --sdcard "$CLONE"
                --benchmark "$frames" --benchmark-label "$v")
    [[ -n "$load" ]] && args+=(--load "$load")
    local out
    # --kill-after is mandatory (test/lint-timeouts.sh): plain SIGTERM is a
    # bound a program may ignore, and the 124 status would then lie.
    if [[ -n "${VENV[$v]}" ]]; then
        out=$(env ${VENV[$v]} timeout --kill-after=5s 300s \
              taskset -c "$CORE" "${VBIN[$v]}" "${args[@]}" 2>&1) || true
    else
        out=$(timeout --kill-after=5s 300s \
              taskset -c "$CORE" "${VBIN[$v]}" "${args[@]}" 2>&1) || true
    fi
    # BOTH values on one line: `run_one` is called in a command substitution, so
    # a variable it sets is set in a SUBSHELL and never reaches the caller. The
    # first version assigned LAST_HITS and every row printed `hits=-`.
    local tsps hits
    tsps=$(sed -n 's/.* tstates_per_sec=\([0-9.]*\).*/\1/p' <<< "$out" | tail -1)
    hits=$(sed -n 's/^BENCHWATCH hits=\([0-9]*\).*/\1/p' <<< "$out" | tail -1)
    printf '%s %s\n' "${tsps:-}" "${hits:--}"
}

median() { printf '%s\n' "$@" | sort -g | sed -n "$(( ($# + 1) / 2 ))p"; }

workload() {   # $1 name  $2 machine  $3 frames  $4 load  $5.. variants
    local name=$1 machine=$2 frames=$3 load=$4; shift 4
    local -a vs=("$@")
    declare -A acc hits
    local v
    for v in "${vs[@]}"; do acc[$v]=""; hits[$v]="-"; done
    local p
    for (( p=1; p<=PAIRS; p++ )); do
        for v in "${vs[@]}"; do
            local line val h
            line=$(run_one "$v" "$machine" "$frames" "$load")
            read -r val h <<< "$line"
            [[ -n "$val" ]] || die "$name pair$p $v produced no BENCH line"
            acc[$v]+="$val "
            [[ "$h" != "-" ]] && hits[$v]="$h"
        done
    done
    emit "## $name (machine=$machine frames=$frames${load:+ load=$(basename "$load")})"
    local base=""
    for v in "${vs[@]}"; do
        local -a a; read -ra a <<< "${acc[$v]}"
        local m mn mx sp d
        m=$(median "${a[@]}")
        mn=$(printf '%s\n' "${a[@]}" | sort -g | head -1)
        mx=$(printf '%s\n' "${a[@]}" | sort -g | tail -1)
        sp=$(awk -v mn="$mn" -v mx="$mx" -v md="$m" 'BEGIN{printf "%.1f",(mx-mn)*100/md}')
        if [[ -z "$base" ]]; then base=$m; d="(baseline)"
        else d=$(awk -v x="$m" -v b="$base" 'BEGIN{printf "%+.1f%% vs B",(x-b)*100/b}'); fi
        emit "$(printf '  %-3s median=%12.0f T/s  spread=%5s%%  hits=%-10s %s' \
                "$v" "$m" "$sp" "${hits[$v]}" "$d")"
    done
    emit ""
}

NEX="$PROJECT_DIR/test/00regression/nex"
workload boot-nextzxos next 400 ""                  B U P A1 A2
workload copper-demo   next 400 "$NEX/copper_demo.nex" B U
workload beast         next 400 "$NEX/beast.nex"       B U
workload boot-48k      48k  600 ""                  B U

emit "# load1_end=$(cut -d' ' -f1 /proc/loadavg)"
emit "# result file: $OUT"
