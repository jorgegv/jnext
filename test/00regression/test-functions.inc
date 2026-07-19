#!/usr/bin/env bash
# shellcheck disable=SC2034  # variables set here are consumed by the sourcing scripts
# Shared helpers, constants and one-time environment setup for the regression
# suite. Sourced by test/00regression/regression.sh (the driver) and by
# standalone test scripts.
#
# Include guard: sourcing this file twice in one shell is a no-op. All
# initialization lives in regression_lib_init() — never in top-level side
# effects — and the function itself is idempotent, so an already-initialized
# shell keeps its state (same TMP_DIR, one EXIT trap, counters untouched).
# The guard variables are NOT exported: a child bash (e.g. the self-test
# re-invoking regression.sh) must initialize itself from scratch.

if [[ -n "${JNEXT_REGRESSION_LIB_SOURCED:-}" ]]; then
    return 0
fi
JNEXT_REGRESSION_LIB_SOURCED=1

# Colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

# pass_row/fail_row/skip_row <text> — print the coloured verdict followed by
# <text> and bump the one matching counter. Keeps verdict text and row
# accounting in lockstep at every reporting site.
pass_row() { echo -e "${GREEN}PASS${RESET}${1-}"; pass=$((pass + 1)); }
fail_row() { echo -e "${RED}FAIL${RESET}${1-}"; fail=$((fail + 1)); }
skip_row() { echo -e "${YELLOW}SKIP${RESET}${1-}"; skip=$((skip + 1)); }

# A harness fault is not a test failure: it means the suite cannot be trusted to
# have run what it claims. Exit 2, loudly, and run nothing further.
harness_fault() {
    echo ""
    echo -e "${RED}${BOLD}=== REGRESSION HARNESS FAULT ===${RESET}"
    for msg in "$@"; do echo -e "  $msg"; done
    echo ""
    exit 2
}

declared_count() {   # declared_count <conf>  — non-comment, non-blank lines
    grep -cvE '^[[:space:]]*(#|$)' "$1" || true
}
pinned_count() {     # pinned_count <conf>  — the `# expect: N` line
    # `|| true` is load-bearing: grep exits 1 when there is no pin line, and
    # under `set -e` + pipefail that would kill the script AT THE ASSIGNMENT,
    # before the driver's "No '# expect: N' pin" fault could print.
    grep -oP '^#\s*expect:\s*\K[0-9]+' "$1" 2>/dev/null | head -1 || true
}

# want <name> — should this test run? (no filter given, or explicitly named)
want() {
    [[ ${#FILTER_TESTS[@]} -eq 0 ]] && return 0
    [[ -n "${IS_FILTERED[$1]:-}" ]]
}

# begin_func <name> — register the row and print its label.
begin_func() {
    REPORTED_FUNC+=("$1")
    printf "  %-25s " "[$1]"
}

# png_diff <a> <b> [sentinel] — differing-pixel count from `compare -metric AE`;
# a parse failure yields <sentinel> (default 999999, i.e. "treat as different").
png_diff() {
    local raw
    raw=$(compare -metric AE "$1" "$2" /dev/null 2>&1) || true
    awk '{printf "%d", $1+0}' <<< "$raw" 2>/dev/null || echo "${3:-999999}"
}

# count_streams <file> <codec_type> — number of streams of that type in the
# container (one ffprobe execution per call).
count_streams() {
    ffprobe -show_streams "$1" 2>/dev/null | grep -c "codec_type=$2" || true
}

# regression_lib_init — one-time environment setup shared by the driver and by
# standalone test scripts. Idempotent: a second call returns immediately.
regression_lib_init() {
    [[ -n "${JNEXT_REGRESSION_LIB_INITIALIZED:-}" ]] && return 0
    JNEXT_REGRESSION_LIB_INITIALIZED=1

    # Pin the locale for the whole suite: assertions grep C-locale strerror()
    # text ("No such file or directory"), Qt's QApplication constructor calls
    # setlocale(LC_ALL, "") which would localise it, and ImageMagick's
    # `compare` must emit a C-locale decimal point for the pixel-diff parse.
    # The suite's result must not depend on the user's LANG.
    export LC_ALL=C

    # Repo root is resolved from THIS file's own location (test/00regression),
    # so a standalone test script sourcing the lib needs no path knowledge.
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

    # Locate the jnext executable. Prefer gui-release (fastest), then
    # gui-debug, then the canonical build/. The first existing executable
    # wins. Override with JNEXT=... in the environment to bypass the search.
    if [[ -z "${JNEXT:-}" ]]; then
        local candidate
        for candidate in "$PROJECT_DIR/build/gui-release/jnext" \
                         "$PROJECT_DIR/build/gui-debug/jnext" \
                         "$PROJECT_DIR/build/jnext"; do
            if [[ -x "$candidate" ]]; then
                JNEXT="$candidate"
                break
            fi
        done
        JNEXT="${JNEXT:-$PROJECT_DIR/build/jnext}"
    fi

    # SD-card image: the suite does NOT pass --sdcard at all — SD_CARD_ARGS
    # stays empty, a deliberate seam. jnext falls back to the pristine,
    # self-provisioned image at $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img
    # (sdcard::provision_sd_card, src/core/sdcard_provisioner.*), the same
    # FAT32-patched canonical distro image an end user gets; the
    # "[sdcard-provision]" row ensures it exists before any test row runs.
    # When the boot-ROM auto-load gate is active (Next + sd_card non-empty +
    # load_file empty), `BOOT` rows exercise the firmware path; rows with
    # --load NEX skip the boot ROM via the cfg.load_file gate (Emulator::init).
    SD_CARD_ARGS=()

    # rewind_test is a unit-test binary (only built when ENABLE_TESTS=ON, i.e.
    # via `make unit-test-build`, which `make regression` depends on). If it is
    # missing, the rewind functional test FAILS LOUDLY in the preflight —
    # never a silently absent row.
    REWIND_TEST="$PROJECT_DIR/build/test/rewind_test"
    CONF="${JNEXT_REGRESSION_CONF:-$SCRIPT_DIR/regression_tests.conf}"
    FUNC_CONF="${JNEXT_REGRESSION_FUNC_CONF:-$SCRIPT_DIR/functional_tests.conf}"
    # img/ lives next to this file under test/00regression/img.
    IMG_DIR="$SCRIPT_DIR/img"
    # NextZXOS welcome-screen reference, diffed against by several boot rows.
    WELCOME_REF="$IMG_DIR/boot-nextzxos-welcome-reference.png"
    TMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TMP_DIR"' EXIT

    # Isolate GUI preferences from the developer's real config. Every
    # NON-headless $JNEXT invocation in the suite constructs the real
    # MainWindow, which loads AppConfig unconditionally for fields with no CLI
    # flag (CPU speed, window scale, CRT filter, tape fast-load) — a stray
    # real config file could silently change CPU speed and perturb
    # timing-sensitive pixel comparisons. JNEXT_CONFIG_DIR points every load()
    # at a fresh, never-created directory, i.e. clean AppConfigData defaults.
    # (XDG_CONFIG_HOME is kept for any other Qt state.)
    export JNEXT_CONFIG_DIR="$TMP_DIR/jnext-config-home"
    export XDG_CONFIG_HOME="$TMP_DIR/xdg-config-home"

    # Pixel difference tolerance (0 = exact match)
    TOLERANCE=${JNEXT_TEST_TOLERANCE:-0}

    # Pinned RTC for deterministic NextZXOS boot screenshots (must match the
    # checked-in boot-nextzxos-* references).
    NEXTZXOS_RTC="2026-07-10T08:55:00"

    pass=0
    fail=0
    skip=0

    # ImageMagick availability. Detected here (silently) so every consumer can
    # test $HAS_COMPARE; the driver prints the user-facing warning when false.
    if command -v compare &>/dev/null; then
        HAS_COMPARE=true
    else
        HAS_COMPARE=false
    fi

    # Filter and row-reporting state. Standalone defaults: no filter (every
    # want() succeeds), nothing reported yet. The driver overwrites these from
    # its command line and its functional-tests manifest.
    FILTER_TESTS=()
    declare -gA IS_FILTERED=()
    REPORTED_FUNC=()
}

regression_lib_init
