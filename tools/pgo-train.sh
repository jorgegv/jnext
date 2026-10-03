#!/usr/bin/env bash
# Train the PGO profile for `make gui-release` (GH #297).
#
# Usage: tools/pgo-train.sh <instrumented jnext> <profile dir>
#
# Runs a small, broad training set with the instrumented jnext, which writes
# its profile into <profile dir> (the directory was baked into the binary at
# compile time; the two must agree, which the callers guarantee). gcc writes
# one .gcda per translation unit; clang (macOS) writes .profraw files, merged
# here into <profile dir>/jnext.profdata with llvm-profdata. The optimised
# rebuild then reads that profile (cmake/JnextPgo.cmake).
#
# NOT the regression suite: profiles saturate quickly (GH #294 measured +17-20%
# on titles an 8-workload profile never saw). The set is the GH #294 one —
# the `make bench` workloads at half their frames plus parallax and
# trainyard-express — plus the paths those miss: a 128K machine driving the
# AY, a game with AY music, a real-time tape load, and a short GUI run on Qt's
# offscreen platform so the frontend's paint/scale path is profiled too.
# Only fixtures that are in the repository; every run pins --rtc.
#
# SKIPPED, exit 0, when nothing that decides the profile changed since the
# last training: the fingerprint below covers the instrumented binary (which
# changes with any source compiled into jnext and with any compile flag),
# the compiler and the configured flags, this script, every training input
# and the SD master's identity. On a skip <profile dir>/.trained keeps its mtime, which is how the
# Makefile knows not to rebuild the optimised tree.
#
# The SD image: training boots NextZXOS and every machine's ROMs come from
# it. The master is provisioned the way the regression suite does it (jnext's
# own download, into ~/.jnext/sdcard); each run then uses a private reflinked
# clone, deleted afterwards. If there is no image and none can be provisioned
# this FAILS, pointing at `make gui-release-non-pgo`, which needs none.
#
# Env:
#   JNEXT_PGO_RUNNER  command prefix for every jnext run (e.g. "wine" to train
#                     a MinGW jnext.exe); word-split on purpose.
#   JNEXT_PGO_NO_SD_HINT  what to tell the user when there is no SD image
#                     (default: the gui-release case, `make gui-release-non-pgo`);
#                     tools/pgo-build.sh sets it for the package trees.
#   JNEXT_PGO_NO_GUI  =1 skips the Qt-offscreen GUI run; the build is then
#                     byte-reproducible (see REPRODUCIBILITY below).
#
# Portable to macOS's bash 3.2 and BSD userland (no GNU timeout, flock,
# setarch, sha256sum or stat -c there): every such tool has a fallback.
set -euo pipefail
export LC_ALL=C LANG=C

die() { echo "pgo-train: ERROR: $*" >&2; exit 1; }

[[ $# -eq 2 ]] || die "usage: $0 <instrumented jnext> <profile dir>"
BIN=$1
PROFILE_DIR=$2
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
RUNNER=()
[[ -n "${JNEXT_PGO_RUNNER:-}" ]] && read -r -a RUNNER <<< "$JNEXT_PGO_RUNNER"

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi; }
# bounded <seconds> <command...>: a time limit that ends in SIGKILL.
bounded() {
    local t=$1; shift
    if command -v timeout >/dev/null 2>&1; then
        timeout --kill-after=5s "${t}s" "$@"
    else
        bash "$PROJECT_DIR/packaging/macos/run-bounded.sh" "$t" pgo-train "$@"
    fi
}
now() { local n; n=$(date +%s.%N); [[ "$n" == *N ]] && n=$(date +%s); echo "$n"; }
elapsed() { awk -v a="$1" -v b="$(now)" 'BEGIN { print b - a }'; }

[[ -x "$BIN" ]] || die "instrumented binary not found: $BIN"
# The build tree that made it (a macOS bundle puts the binary three levels
# down): its compiler goes into the fingerprint and picks the profile format.
TREE=$(dirname "$BIN")
while [[ ! -f "$TREE/CMakeCache.txt" && "$TREE" != / ]]; do TREE=$(dirname "$TREE"); done
COMPILER=$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$TREE/CMakeCache.txt" 2>/dev/null)
[[ -n "$COMPILER" ]] || die "no CMakeCache.txt with CMAKE_CXX_COMPILER above $BIN"
CLANG=0
[[ "$("$COMPILER" --version 2>&1)" == *[Cc]lang* ]] && CLANG=1
mkdir -p "$PROFILE_DIR"
PROFILE_DIR=$(cd "$PROFILE_DIR" && pwd)

SD_MASTER_DIR="$HOME/.jnext/sdcard"
SD_MASTER="$SD_MASTER_DIR/cspect-next-1gb-fixed.img"
NEX=test/00regression/nex
TAP=test/00regression/tap
RTC="2026-07-10T08:55:00"

# name|machine|frames|extra args (word-split)|fixture files (for the fingerprint)
WORKLOADS=(
    "boot-48k|48k|300||"
    "boot-nextzxos|next|200||"
    "copper-demo|next|200|--load $NEX/copper_demo.nex|$NEX/copper_demo.nex"
    "beast|next|200|--load $NEX/beast.nex|$NEX/beast.nex"
    "bifrost|48k|300|--load $TAP/bifrost.tap|$TAP/bifrost.tap"
    "parallax|next|200|--load $NEX/parallax.nex|$NEX/parallax.nex"
    "trainyard|next|200|--load $NEX/trainyard-express.nex --esxdos-stub|$NEX/trainyard-express.nex"
    "dma-48k|48k|150|--inject test/00regression/bin/rzx_dma_demo.bin --delayed-keypress-frames 20 space|test/00regression/bin/rzx_dma_demo.bin"
    "ay-128k|128k|340|--inject test/00regression/bin/ay_envelope_sweep.bin --inject-org 8000 --inject-pc 8000 --inject-delay 100|test/00regression/bin/ay_envelope_sweep.bin"
    "beanbros-ay|next|250|--load $NEX/beanbros.nex --esxdos-stub --delayed-keypress-frames 50 ENTER --delayed-keypress-frames 100 ENTER --delayed-keypress-frames 150 ENTER|$NEX/beanbros.nex"
    "tape-realtime|48k|1300|--load $TAP/beeper_demo.tap --tape-realtime|$TAP/beeper_demo.tap"
)
GUI_FRAMES=150
GUI_ARGS=(--machine next --load "$NEX/parallax.nex")

# --- Fingerprint: everything that decides what training would produce -------
fingerprint() {
    {
        echo "binary $(sha256 < "$BIN")"
        echo "compiler $("$COMPILER" --version 2>&1 | head -1)"
        # The instrumented binary already changes with any flag that changes
        # code; the configured flags are listed too, so ANY flag change
        # retrains, even one that happens to leave the binary identical.
        grep -E '^(CMAKE_BUILD_TYPE|CMAKE_[A-Z_]*FLAGS[A-Z_]*|CMAKE_C_COMPILER|CMAKE_CXX_COMPILER|JNEXT_[A-Z_]*|ENABLE_[A-Z_]*):' \
            "$TREE/CMakeCache.txt" | sort
        echo "script $(sha256 < "${BASH_SOURCE[0]}")"
        echo "runner ${RUNNER[*]:-native} gui=${JNEXT_PGO_NO_GUI:-0}"
        local spec files f
        for spec in "${WORKLOADS[@]}"; do
            IFS='|' read -r _ _ _ _ files <<< "$spec"
            for f in $files; do echo "input $f $(sha256 < "$PROJECT_DIR/$f")"; done
        done
        echo "input gui $(sha256 < "$PROJECT_DIR/$NEX/parallax.nex")"
        echo "sd $(ls -ln "$SD_MASTER" 2>/dev/null | awk '{print $5, $6, $7, $8}')"
    } | sha256 | cut -d' ' -f1
}

cd "$PROJECT_DIR"
if [[ -f "$SD_MASTER" && -f "$PROFILE_DIR/.trained" ]] &&
   [[ "$(cat "$PROFILE_DIR/.fingerprint" 2>/dev/null)" == "$(fingerprint)" ]]; then
    echo "pgo-train: profile up to date ($PROFILE_DIR) — not retraining"
    exit 0
fi

# --- The SD master: provision it exactly as the regression suite does -------
if [[ ! -f "$SD_MASTER" ]]; then
    echo "pgo-train: no SD image at $SD_MASTER — provisioning it (jnext's own download)"
    mkdir -p "$SD_MASTER_DIR"
    {
        command -v flock >/dev/null 2>&1 && flock 9
        [[ -f "$SD_MASTER" ]] || JNEXT_CONFIG_DIR="$HOME/.jnext" \
            bounded 1200 ${RUNNER[@]+"${RUNNER[@]}"} "$BIN" --headless \
                --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1 || true
    } 9>"$SD_MASTER_DIR/.provision.lock"
    if [[ ! -f "$SD_MASTER" ]]; then
        echo "pgo-train: ERROR: the PGO build needs the NextZXOS SD image to train its profile," >&2
        echo "pgo-train: ERROR: and it could not be provisioned at $SD_MASTER (network?)." >&2
        echo "pgo-train: ERROR: ${JNEXT_PGO_NO_SD_HINT:-Build without PGO instead:  make gui-release-non-pgo}" >&2
        exit 1
    fi
fi

# --- A private run directory: SD clone + clean GUI preferences ---------------
mkdir -p "$HOME/.jnext/runs"
RUN_DIR=$(mktemp -d "$HOME/.jnext/runs/pgo-XXXXXXXX")
cleanup() {
    rm -rf "$RUN_DIR"
    rmdir "$HOME/.jnext/runs" 2>/dev/null || true
    return 0
}
# INT/TERM exit explicitly: a handler that only cleans up would RESUME the
# script without the clone it just deleted (see test/bench/bench.sh).
trap 'cleanup' EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
mkdir -p "$RUN_DIR/sdcard" "$RUN_DIR/xdg"
cp --reflink=always "$SD_MASTER" "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" 2>/dev/null ||
    cp -c "$SD_MASTER" "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" 2>/dev/null ||   # APFS clone
    cp "$SD_MASTER" "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" ||
    die "cannot clone the SD master $SD_MASTER"
SD="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
# REPRODUCIBILITY. gcc's value profiling records runtime VALUES (pointer
# alignment, sizes), so stack and heap addresses end up in the .gcda files,
# and spdlog's formatter re-reads the clock once per wall-clock second, so its
# call count depends on when messages happen. The headless runs are therefore
# made deterministic: address-space randomisation off (setarch -R, where the
# host allows it — docker's default seccomp profile does not), one minimal
# environment for every run (env -i; the stack layout depends on its size), a
# fixed-length run directory (argv's size), and logging off. With those, two
# trainings WITHOUT the GUI run give byte-identical .gcda files and binaries.
# The GUI run is paced in real time and is NOT reproducible (its counts move
# by well under 1% between runs); JNEXT_PGO_NO_GUI=1 leaves it out.
RUN_ENV=(env -i "HOME=$HOME" "PATH=$PATH" LANG=C LC_ALL=C
         "JNEXT_CONFIG_DIR=$RUN_DIR" "XDG_CONFIG_HOME=$RUN_DIR/xdg")
for v in WINEPREFIX WINEDEBUG WINEDLLOVERRIDES XDG_RUNTIME_DIR DYLD_FRAMEWORK_PATH DYLD_LIBRARY_PATH; do
    [[ -n "${!v:-}" ]] && RUN_ENV+=("$v=${!v}")
done
NORAND=()
if command -v setarch >/dev/null 2>&1 && setarch -R true 2>/dev/null; then
    NORAND=(setarch -R)
fi

# A stale or partial profile must never survive into a new one: libgcov
# MERGES into existing .gcda files, so start from an empty directory.
find "$PROFILE_DIR" -mindepth 1 -delete
t0=$(now)

run() {   # run <name> <timeout s> <jnext args...>
    local name=$1 limit=$2 rc=0 t
    shift 2
    t=$(now)
    bounded "$limit" "${RUN_ENV[@]}" ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"} ${NORAND[@]+"${NORAND[@]}"} \
        ${RUNNER[@]+"${RUNNER[@]}"} "$BIN" --rtc "$RTC" --log-level off --sdcard "$SD" "$@" \
        >"$RUN_DIR/$name.log" 2>&1 || rc=$?
    if [[ $rc -ne 0 ]]; then
        tail -20 "$RUN_DIR/$name.log" >&2
        die "training run '$name' failed (exit $rc): ${RUNNER[*]:-} $BIN $*"
    fi
    printf 'pgo-train:   %-14s %6.1f s\n' "$name" "$(elapsed "$t")"
}

EXTRA_ENV=()
echo "pgo-train: training $BIN -> $PROFILE_DIR"
for spec in "${WORKLOADS[@]}"; do
    IFS='|' read -r name machine frames extra _ <<< "$spec"
    extra_args=()
    [[ -n "$extra" ]] && read -r -a extra_args <<< "$extra"
    run "$name" 300 --headless --machine "$machine" ${extra_args[@]+"${extra_args[@]}"} \
        --delayed-automatic-exit-frames "$frames"
done
if [[ "${JNEXT_PGO_NO_GUI:-0}" != 1 ]]; then
    # 100% speed, real pacing, sound on through SDL's dummy driver, so the
    # GUI's frame, paint and audio paths run as a user would run them.
    EXTRA_ENV=(QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy)
    run gui-offscreen 120 "${GUI_ARGS[@]}" --delayed-automatic-exit-frames "$GUI_FRAMES"
fi

if [[ $CLANG -eq 1 ]]; then
    n=$(find "$PROFILE_DIR" -name '*.profraw' | wc -l | tr -d ' ')
    [[ $n -gt 0 ]] || die "training wrote no .profraw files into $PROFILE_DIR — is $BIN instrumented for that directory?"
    # xcrun first: on macOS the llvm-profdata that matches AppleClang is
    # Xcode's; a newer Homebrew LLVM earlier in PATH can write a profile
    # format AppleClang cannot read.
    if command -v xcrun >/dev/null 2>&1 && xcrun -f llvm-profdata >/dev/null 2>&1; then PROFDATA=(xcrun llvm-profdata)
    elif command -v llvm-profdata >/dev/null 2>&1; then PROFDATA=(llvm-profdata)
    else die "llvm-profdata not found (clang PGO needs it to merge the profile)"; fi
    "${PROFDATA[@]}" merge -o "$PROFILE_DIR/jnext.profdata" "$PROFILE_DIR"/*.profraw ||
        die "llvm-profdata merge failed"
    rm -f "$PROFILE_DIR"/*.profraw
else
    n=$(find "$PROFILE_DIR" -name '*.gcda' | wc -l | tr -d ' ')
    [[ $n -gt 0 ]] || die "training wrote no .gcda files into $PROFILE_DIR — is $BIN instrumented for that directory?"
    # The instrumented link's map goes with the profile: cmake/JnextPgo.cmake
    # reads it to tell the members jnext links (profile REQUIRED) from those
    # it never links (no profile can exist).
    MAP="$TREE/jnext-pgo.map"
    [[ -s "$MAP" ]] || die "no link map at $MAP — the instrumented build did not write one"
    cp "$MAP" "$PROFILE_DIR/jnext.map"
fi
fingerprint > "$PROFILE_DIR/.fingerprint"
touch "$PROFILE_DIR/.trained"
printf 'pgo-train: %d profiles in %.1f s\n' "$n" "$(elapsed "$t0")"
