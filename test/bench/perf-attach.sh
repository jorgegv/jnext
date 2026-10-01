#!/usr/bin/env bash
# GH #278 Q WP2 — the hot-path cost of an ATTACHED CLIENT, in retired user-space
# instructions (perf stat), interleaved.
#
# WHY INSTRUCTIONS AND NOT T/s. The question is what a process-lifetime attach
# of the Qt adapter would cost the GUI with its window closed: the machine armed
# through `clients_attached_` with nothing subscribed. ab-hotlatch.sh
# (AB_SET=attach) answers it in wall-clock T/s, which is the number a user
# feels, but on a host shared with other agents its spread runs to 10-80 % and
# the answer drowns. Retired instructions are deterministic to ~0.01 % run to
# run whatever else the machine is doing, so the DELTA is exact; it is the cost
# in work, and on this code (well-predicted branches) it tracks wall time: B2's
# quiet-host wall-clock P − U (−2.2 %, hotlatch-49d5025e3.txt, boot-nextzxos)
# matches P's instruction delta here (+2.18 %). cycles:u is recorded as well and
# moves with the load.
#
# Usage:
#   bash test/bench/perf-attach.sh <baseline-jnext> [<new-jnext>] [reps]
#
# VARIANTS (per workload, interleaved, `reps` rounds)
#   B    the baseline binary, nothing armed
#   U    this tree, nothing armed                    (the attach-on-enable GUI, window closed)
#   P    this tree, --persistent-breakpoints only    (JNEXT_BENCH_WATCH=p)
#   C    this tree, one client ATTACHED, nothing subscribed (JNEXT_BENCH_WATCH=c)
#
# P IS VOID ON THE TWO NEX WORKLOADS: the load reconstructs the machine and
# init() re-latches --persistent-breakpoints from the config, so the fixture's
# direct set is lost. C survives it (the backend re-applies its client).
set -uo pipefail
export LC_ALL=C LANG=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

die() { echo "perf-attach: ERROR: $*" >&2; exit 2; }

BASE_BIN="${1:-}"
NEW_BIN="${2:-$PROJECT_DIR/build/gui-release/jnext}"
REPS="${3:-3}"
[[ -n "$BASE_BIN" ]] || die "usage: $0 <baseline-jnext> [<new-jnext>] [reps]"
[[ -x "$BASE_BIN" ]] || die "baseline binary not executable: $BASE_BIN"
[[ -x "$NEW_BIN"  ]] || die "new binary not executable: $NEW_BIN (make gui-release)"
command -v perf >/dev/null || die "perf is not installed"

SD_MASTER="${JNEXT_TEST_SD_IMAGE:-$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img}"
[[ -f "$SD_MASTER" ]] || die "SD image missing: $SD_MASTER"
# A private clone, as in ab-hotlatch.sh and for the same reasons.
CLONE="$HOME/tmp/jnext-perf-attach-$$.img"
mkdir -p "$HOME/tmp"
# INT/TERM exit explicitly, as in bench.sh: an EXIT-only trap misses a signal death.
trap 'rm -f "$CLONE"' EXIT
trap 'rm -f "$CLONE"; exit 130' INT
trap 'rm -f "$CLONE"; exit 143' TERM
cp --reflink=auto "$SD_MASTER" "$CLONE" || die "cannot clone $SD_MASTER"

declare -A VBIN VENV
VBIN[B]="$BASE_BIN"; VENV[B]=""
VBIN[U]="$NEW_BIN";  VENV[U]=""
VBIN[P]="$NEW_BIN";  VENV[P]="JNEXT_BENCH_WATCH=p"
VBIN[C]="$NEW_BIN";  VENV[C]="JNEXT_BENCH_WATCH=c"

one() {   # $1 variant  $2 machine  $3 frames  $4 load-or-empty
    local v=$1 m=$2 f=$3 l=$4
    local args=(--headless --machine "$m" --sdcard "$CLONE" --benchmark "$f"
                --benchmark-label "$v")
    [[ -n "$l" ]] && args+=(--load "$l")
    local out ins cyc
    # --kill-after is mandatory (test/lint-timeouts.sh).
    out=$(env ${VENV[$v]} timeout --kill-after=5s 300s \
          perf stat -x, -e instructions:u,cycles:u "${VBIN[$v]}" "${args[@]}" 2>&1) || true
    ins=$(awk -F, '/instructions:u/{print $1}' <<< "$out")
    cyc=$(awk -F, '/cycles:u/{print $1}' <<< "$out")
    [[ "$ins" =~ ^[0-9]+$ ]] || die "$v on $m produced no instruction count"
    printf '%s %s %s\n' "$v" "$ins" "$cyc"
}

median() { printf '%s\n' "$@" | sort -g | sed -n "$(( ($# + 1) / 2 ))p"; }

echo "# perf-attach: sha=$(git -C "$PROJECT_DIR" rev-parse --short HEAD 2>/dev/null) reps=$REPS load1_start=$(cut -d' ' -f1 /proc/loadavg)"
echo "# baseline=$BASE_BIN"
echo "# new=$NEW_BIN"

workload() {   # $1 name  $2 machine  $3 frames  $4 load
    local name=$1 m=$2 f=$3 l=$4
    declare -A ins
    local r v line
    for v in B U P C; do ins[$v]=""; done
    for (( r=1; r<=REPS; r++ )); do
        for v in B U P C; do
            line=$(one "$v" "$m" "$f" "$l")
            echo "  $name r$r $line"
            ins[$v]+="$(cut -d' ' -f2 <<< "$line") "
        done
    done
    local -a a; read -ra a <<< "${ins[U]}"
    local u; u=$(median "${a[@]}")
    echo "## $name (machine=$m frames=$f${l:+ load=$(basename "$l")})"
    for v in B U P C; do
        read -ra a <<< "${ins[$v]}"
        local md; md=$(median "${a[@]}")
        awk -v v="$v" -v md="$md" -v u="$u" \
            'BEGIN{printf "  %s median instructions:u=%15.0f  %+.2f%% vs U\n", v, md, (md-u)*100/u}'
    done
}

NEX="$PROJECT_DIR/test/00regression/nex"
workload boot-48k      48k  600 ""
workload boot-nextzxos next 400 ""
workload beast         next 400 "$NEX/beast.nex"
workload copper-demo   next 400 "$NEX/copper_demo.nex"
echo "# load1_end=$(cut -d' ' -f1 /proc/loadavg)"
