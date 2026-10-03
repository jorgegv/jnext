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
# on titles an 8-workload profile never saw), so the set is BROAD, not big:
# the `make bench` workloads, the games parallax, trainyard-express and
# beanbros (AY music), a 128K machine driving the AY, a real-time tape load,
# one program per video mode (Layer 2 256/320/640, LoRes, tilemaps, sprites
# and scaling, the 512-colour palette, stencil), demo/dma_all (every zxnDMA
# feature, self-checking), the Next's MOD player (NXModPlayer: Paula emulation
# on the DACs, one CTC interrupt per sample), and a short GUI run on Qt's
# offscreen platform so the frontend's paint/scale path is profiled too. Every
# run pins --rtc.
#
# BALANCED: a run's weight in the profile is its total execution count, and in
# LTO mode gcc calls a block hot only if it is part of the most-executed 99% of
# ALL counts (--param hot-bb-count-ws-permille=990). One heavy run would push
# the others' hot code out of that set, so every run's length is chosen for
# about the same total count (JNEXT_PGO_REPORT_WEIGHTS=1 below measures them;
# re-measure after adding or changing a run). The MOD player's navigation
# through NextZXOS is therefore NOT profiled — it would outweigh five other
# runs — only 150 frames of play from a snapshot taken at frame 900.
#
# HELD OUT, never trained, so they measure how well the profile generalises:
# santaspressie, celeste, celeste2, odemo, test02layer2, shift, nirvana. A new
# training run is accepted only if every held-out title stays within retrain
# noise of the previous PGO build or gets faster (developer guide 5.5).
#
# Fixtures come from the repository, except the MOD player and its MOD, which
# come from the SD image the training already needs (the distribution's
# /apps/audio/NXModPlayer): nothing of the distribution is copied into the
# repository, and nothing beyond jnext itself is needed to reach them.
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
#   JNEXT_PGO_REPORT_WEIGHTS  =1 (gcc only; needs gcov-dump next to the
#                     compiler): profile every run into its own directory,
#                     print each run's total count, and stop WITHOUT a usable
#                     profile — the tool for keeping the set balanced.
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
    "boot-48k|48k|500||"
    "boot-nextzxos|next|250||"
    "copper-demo|next|115|--load $NEX/copper_demo.nex|$NEX/copper_demo.nex"
    "beast|next|130|--load $NEX/beast.nex|$NEX/beast.nex"
    "bifrost|48k|500|--load $TAP/bifrost.tap|$TAP/bifrost.tap"
    "parallax|next|170|--load $NEX/parallax.nex|$NEX/parallax.nex"
    "trainyard|next|170|--load $NEX/trainyard-express.nex --esxdos-stub|$NEX/trainyard-express.nex"
    "ay-128k|128k|485|--inject test/00regression/bin/ay_envelope_sweep.bin --inject-org 8000 --inject-pc 8000 --inject-delay 100|test/00regression/bin/ay_envelope_sweep.bin"
    "beanbros-ay|next|185|--load $NEX/beanbros.nex --esxdos-stub --delayed-keypress-frames 50 ENTER --delayed-keypress-frames 100 ENTER --delayed-keypress-frames 150 ENTER|$NEX/beanbros.nex"
    "tape-realtime|48k|460|--load $TAP/beeper_demo.tap --tape-realtime|$TAP/beeper_demo.tap"
    "layer2-320|next|120|--load $NEX/layer2_320x256_test.nex|$NEX/layer2_320x256_test.nex"
    "layer2-640|next|125|--load $NEX/layer2_640x256_test.nex|$NEX/layer2_640x256_test.nex"
    "lores|next|120|--load $NEX/lores_demo.nex|$NEX/lores_demo.nex"
    "tilemap|next|115|--load $NEX/tilemap_demo.nex|$NEX/tilemap_demo.nex"
    "sprite-scale|next|120|--load $NEX/sprite_scaling_test.nex|$NEX/sprite_scaling_test.nex"
    "show512|next|175|--load $NEX/show512.nex|$NEX/show512.nex"
    "stencil|next|120|--load $NEX/stencil_test.nex|$NEX/stencil_test.nex"
    "palette|next|125|--load $NEX/palette_demo.nex|$NEX/palette_demo.nex"
    "tilemapper|next|190|--load $NEX/test10tilemapper.nex --delayed-keypress-frames 100 0|$NEX/test10tilemapper.nex"
    "dapr-tilemap|next|190|--load $NEX/test04tilemap.nex --delayed-keypress-frames 100 1|$NEX/test04tilemap.nex"
    "dapr-sprite|next|200|--load $NEX/test03sprite.nex|$NEX/test03sprite.nex"
)
# demo/dma_all: every zxnDMA feature, each pass checked by the program itself;
# its signature is in RAM only after a fully checked pass (the proof below).
DMA_ALL=test/00regression/bin/dma_all.bin
DMA_FRAMES=165
DMA_PROOF="DMA-ALL-OK!!"
GUI_FRAMES=150
GUI_ARGS=(--machine next --silent --load "$NEX/parallax.nex")

# The MOD player, launched as a user launches it: the NextZXOS Browser down to
# /apps/audio/NXModPlayer/nxmodplayer.nex, then the player's own Mod Browser to
# mods/Jarresque.mod. Every key is pressed at a fixed frame (frame, key pairs
# below; the cursor positions are those of the 24.11 image), so the run is as
# deterministic as the others. That navigation runs UNPROFILED (see BALANCED)
# and saves a .jns at frame 900, with the MOD playing; the profiled run then
# plays MOD_PLAY frames from it. A key pressed at a fixed frame cannot tell
# whether the guest acted on it, so the profiled run also saves a snapshot one
# frame before its exit, and the training FAILS unless the MOD's own text is
# in that RAM: a changed image or a slower boot must not quietly leave the
# player out of the profile.
MOD_KEYS=(
    400 space  430 enter                             # welcome, menu: Browser at C:/ on APPS
    470 enter  495 down  510 down  525 enter         # APPS: . .. AUDIO
    550 down  565 down  580 down  595 down  610 enter    # . .. NextDAW-demo NextSID NXModPlayer
    635 down  650 down  665 down  680 enter          # . .. MODS nxmodplayer.nex: run it
    760 down  775 enter                              # Mod Browser: .. MODS
    800 down  815 down  830 down  845 enter          # .. AWESOME5 DUNE_ECOLOVE JARRESQUE: play
)
MOD_SNAP_FRAME=900
MOD_PLAY=150
MOD_PROOF="by hollywood/lunatics"   # the MOD's second sample name (Jarresque.mod offset 20)

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
        echo "runner ${RUNNER[*]:-native} gui=${JNEXT_PGO_NO_GUI:-0} weights=${JNEXT_PGO_REPORT_WEIGHTS:-0}"
        local spec files f
        for spec in "${WORKLOADS[@]}"; do
            IFS='|' read -r _ _ _ _ files <<< "$spec"
            for f in $files; do echo "input $f $(sha256 < "$PROJECT_DIR/$f")"; done
        done
        echo "input gui $(sha256 < "$PROJECT_DIR/$NEX/parallax.nex")"
        echo "input dma-all $(sha256 < "$PROJECT_DIR/$DMA_ALL")"
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
# The GUI run is paced in real time and is NOT reproducible: its emulation is
# (see --silent below), but its GUI-only counters (status timer, paints, event
# loop polls) follow the wall clock, and through LTO that is enough to change
# the binary's cold code. JNEXT_PGO_NO_GUI=1 leaves it out.
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

# JNEXT_PGO_REPORT_WEIGHTS: each run's profile goes to its own directory
# (GCOV_PREFIX + GCOV_PREFIX_STRIP replace the baked profile directory).
WEIGHTS=()
if [[ "${JNEXT_PGO_REPORT_WEIGHTS:-0}" == 1 ]]; then
    [[ $CLANG -eq 0 ]] || die "JNEXT_PGO_REPORT_WEIGHTS is gcc-only"
    GCOV_DUMP="${COMPILER%[gc]++}gcov-dump"
    command -v "$GCOV_DUMP" >/dev/null 2>&1 || die "no $GCOV_DUMP for JNEXT_PGO_REPORT_WEIGHTS"
    STRIP=$(awk -F/ '{print NF-1}' <<< "$PROFILE_DIR")
fi
run() {   # run <name> <timeout s> <jnext args...>
    local name=$1 limit=$2 rc=0 t
    shift 2
    t=$(now)
    WEIGHTS=()
    [[ "${JNEXT_PGO_REPORT_WEIGHTS:-0}" == 1 && " ${EXTRA_ENV[*]:-} " != *" GCOV_PREFIX="* ]] &&
        WEIGHTS=("GCOV_PREFIX=$RUN_DIR/w/$name" "GCOV_PREFIX_STRIP=$STRIP")
    bounded "$limit" "${RUN_ENV[@]}" ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"} ${WEIGHTS[@]+"${WEIGHTS[@]}"} ${NORAND[@]+"${NORAND[@]}"} \
        ${RUNNER[@]+"${RUNNER[@]}"} "$BIN" --rtc "$RTC" --log-level off --sdcard "$SD" "$@" \
        >"$RUN_DIR/$name.log" 2>&1 || rc=$?
    if [[ $rc -ne 0 ]]; then
        tail -20 "$RUN_DIR/$name.log" >&2
        die "training run '$name' failed (exit $rc): ${RUNNER[*]:-} $BIN $*"
    fi
    printf 'pgo-train:   %-14s %6.1f s\n' "$name" "$(elapsed "$t")"
}

echo "pgo-train: training $BIN -> $PROFILE_DIR"
# Unprofiled warm-up: the first NEX loaded on a Next records jnext's warm-start
# cache (a 500-frame NextZXOS cold boot, once per SD image) into this run's
# config directory. Profiled, that one-off boot would land on whichever run
# came first and outweigh it several times over.
EXTRA_ENV=("GCOV_PREFIX=$RUN_DIR/unprofiled" "LLVM_PROFILE_FILE=$RUN_DIR/unprofiled/%p.profraw")
run warm-start 300 --headless --machine next --load "$NEX/copper_demo.nex" --delayed-automatic-exit-frames 1
EXTRA_ENV=()
for spec in "${WORKLOADS[@]}"; do
    IFS='|' read -r name machine frames extra _ <<< "$spec"
    extra_args=()
    [[ -n "$extra" ]] && read -r -a extra_args <<< "$extra"
    run "$name" 300 --headless --machine "$machine" ${extra_args[@]+"${extra_args[@]}"} \
        --delayed-automatic-exit-frames "$frames"
done
# dma_all, then its proof: the signature only a fully checked pass writes.
run dma-all 300 --headless --machine 48k --inject "$DMA_ALL" \
    --delayed-snapshot "$RUN_DIR/dma-all.sna" --delayed-snapshot-frames $((DMA_FRAMES - 1)) \
    --delayed-automatic-exit-frames "$DMA_FRAMES"
grep -a -q "$DMA_PROOF" "$RUN_DIR/dma-all.sna" ||
    die "training run 'dma-all' did not complete a checked pass (no \"$DMA_PROOF\" in its RAM at frame $((DMA_FRAMES - 1))): a zxnDMA result no longer matches what the program expects (demo/dma_all/dma_all.asm; FAILID at \$9F02 names the step)"

# The MOD player: navigation unprofiled (its counts go to a scratch directory),
# then MOD_PLAY profiled frames of play from the snapshot it leaves.
mod_args=()
for (( i = 0; i < ${#MOD_KEYS[@]}; i += 2 )); do
    mod_args+=(--delayed-keypress-frames "${MOD_KEYS[i]}" "${MOD_KEYS[i+1]}")
done
EXTRA_ENV=("GCOV_PREFIX=$RUN_DIR/unprofiled" "LLVM_PROFILE_FILE=$RUN_DIR/unprofiled/%p.profraw")
# The snapshot is stored UNCOMPRESSED: its manifest carries the time and this
# run's directory, and deflated, those bytes change the archive's member sizes
# and so the counts of the profiled run that loads it. Stored, every training
# loads the same layout and the headless profile stays byte-reproducible.
run modplayer-nav 300 --headless --machine next "${mod_args[@]}" --snapshot-compression off \
    --delayed-snapshot "$RUN_DIR/modplayer.jns" --delayed-snapshot-frames "$MOD_SNAP_FRAME" \
    --delayed-automatic-exit-frames $((MOD_SNAP_FRAME + 1))
EXTRA_ENV=()
run modplayer 300 --headless --load "$RUN_DIR/modplayer.jns" \
    --delayed-snapshot "$RUN_DIR/modplayer.nex" --delayed-snapshot-frames $((MOD_PLAY - 1)) \
    --delayed-automatic-exit-frames "$MOD_PLAY"
grep -a -q "$MOD_PROOF" "$RUN_DIR/modplayer.nex" ||
    die "training run 'modplayer' never loaded the MOD (no \"$MOD_PROOF\" in its RAM): the key sequence no longer reaches NXModPlayer's Jarresque.mod in this SD image"
if [[ "${JNEXT_PGO_NO_GUI:-0}" != 1 ]]; then
    # 100% speed, frame-bounded, every frame composited and presented, so the
    # GUI's frame and paint paths run as a user would run them. --silent: with
    # an audio device the frame pacer runs one OR two frames per tick depending
    # on how full the device queue is, so how many frames skipped the
    # compositor depended on the host's load; without one it runs exactly one
    # composited frame per tick, and the emulation the GUI run profiles is the
    # same at any load. (Speed above 100% would not help: the compositor is
    # then throttled by the wall clock.) What still depends on the wall clock is
    # GUI-only: the 1 s status-bar timer, paint coalescing, the event-loop polls.
    EXTRA_ENV=(QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy)
    # Under wine these two never reach the exe: wine drops QT_* and
    # SDL_AUDIODRIVER from the Unix environment (is_ignored_env_var(),
    # dlls/ntdll/unix/env.c) and imports WINEQT_* / WINESDL_AUDIODRIVER as
    # them instead (GH #299). Without the offscreen platform the Qt exe uses
    # "windows", whose window wine cannot create without a display; the window
    # is then visible with no platform window, and quit() spins forever in
    # QApplicationPrivate::tryCloseAllWidgetWindows().
    [[ "${RUNNER[0]:-}" == wine* ]] &&
        EXTRA_ENV+=(WINEQT_QPA_PLATFORM=offscreen WINESDL_AUDIODRIVER=dummy)
    run gui-offscreen 120 "${GUI_ARGS[@]}" --delayed-automatic-exit-frames "$GUI_FRAMES"
fi

if [[ "${JNEXT_PGO_REPORT_WEIGHTS:-0}" == 1 ]]; then
    # Each run's total count: the sum of all its arc counters.
    echo "pgo-train: total count per run (the weight it has in the profile):"
    for d in "$RUN_DIR"/w/*/; do
        printf '  %-15s %s\n' "$(basename "$d")" "$(for f in "$d"*.gcda; do "$GCOV_DUMP" -l "$f"; done |
            awk '/COUNTERS /{arcs = ($0 ~ / arcs /); next}
                 arcs && /:[ ]+[0-9]+: / {sub(/.*:[ ]+[0-9]+: /, ""); for (i = 1; i <= NF; i++) t += $i}
                 END {printf "%.0f", t}')"
    done
    die "JNEXT_PGO_REPORT_WEIGHTS=1: report only, no profile was written"
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
