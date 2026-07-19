#!/usr/bin/env bash
#
# build-matrix.sh — build every ENABLE_QT_UI x ENABLE_DEBUGGER combination.
#
# Guards against link rot. The libraries declare their dependencies through
# target_link_libraries, and CMake derives the static-link ORDER from that
# graph — so a target that under-declares still links whenever some other
# library happens to pull its dependencies in at a usable position. Turning
# that other library off removes the accident and the link breaks.
#
# That is exactly how this got shipped twice: jnext_core never declared
# cpu/memory/video/audio/port, and jnext_platform never declared jnext_core.
# The default combo (QT_UI=ON, DEBUGGER=ON) hid both, so nothing noticed until
# someone built without the debugger. Only building every combination catches
# it, which is what this does.
#
# Builds EVERY combination even after one fails — knowing whether three combos
# are broken or one is the difference between a missing edge and a wrong graph
# — then prints the undefined references for each and exits non-zero. A broken
# combo is a hard failure, never a warning.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="${JNEXT_BUILD_MATRIX_DIR:-build/matrix}"
JOBS="${JNEXT_BUILD_JOBS:-$(nproc)}"

# Every combination of the two frontend options. Both are independently
# meaningful: QT_UI=OFF is the SDL-only build, DEBUGGER=OFF the lean one, and
# the pair is what an ARM64 / Raspberry-Pi target wants.
COMBOS=(
    "OFF OFF"
    "OFF ON"
    "ON  OFF"
    "ON  ON"
)

failed=0
declare -a RESULTS=()

for combo in "${COMBOS[@]}"; do
    read -r qt dbg <<<"$combo"
    tag="qt${qt}_dbg${dbg}"
    dir="$OUT_DIR/$tag"
    log="$OUT_DIR/$tag.log"
    mkdir -p "$OUT_DIR"

    printf '=== ENABLE_QT_UI=%-3s ENABLE_DEBUGGER=%-3s ' "$qt" "$dbg"

    if ! cmake -S . -B "$dir" -DENABLE_QT_UI="$qt" -DENABLE_DEBUGGER="$dbg" \
            > "$log" 2>&1; then
        printf 'CONFIGURE FAILED\n'
        tail -20 "$log" | sed 's/^/    /'
        RESULTS+=("FAIL(configure) qt=$qt dbg=$dbg")
        failed=1
        continue
    fi

    if ! cmake --build "$dir" -j"$JOBS" >> "$log" 2>&1; then
        printf 'BUILD FAILED\n'
        # Undefined references are the signature of this failure class; show
        # them first, then whatever else the tail holds.
        grep -E "undefined reference|error:" "$log" | head -15 | sed 's/^/    /'
        RESULTS+=("FAIL(build) qt=$qt dbg=$dbg  (log: $log)")
        failed=1
        continue
    fi

    printf 'OK\n'
    RESULTS+=("PASS qt=$qt dbg=$dbg")
done

echo
echo "=== Build matrix ==="
for r in "${RESULTS[@]}"; do echo "  $r"; done

if (( failed )); then
    echo
    echo "BUILD MATRIX FAILED — at least one option combination does not link."
    echo "This is link rot: a target is not declaring a dependency it uses."
    exit 1
fi

echo
echo "All ${#COMBOS[@]} combinations built."
