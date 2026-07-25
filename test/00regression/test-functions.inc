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

# regression_cleanup — the one EXIT handler. Removes the scratch dir and this
# run's private state directory (which holds the ~1 GB SD-image clone). Guarded
# against an empty variable so it can never become `rm -rf /` or `rm -rf ""`.
regression_cleanup() {
    [[ -n "${TMP_DIR:-}" ]] && rm -rf "$TMP_DIR"
    [[ -n "${RUN_DIR:-}" ]] && rm -rf "$RUN_DIR"
    # Prune the (now likely empty) runs/ parent; harmless if other runs are live.
    rmdir "$HOME/.jnext/runs" 2>/dev/null || true
    return 0
}

# sd_clone_for_run — give THIS run a private copy of the master SD image at
# $RUN_DIR/sdcard/, the location jnext's provisioner resolves from
# $JNEXT_CONFIG_DIR. Idempotent: safe to call again after the master has been
# re-derived.
#
# reflink first, explicitly (`--reflink=always`), so the outcome is OBSERVABLE
# in $SD_CLONE_MODE rather than guessed: on a copy-on-write filesystem the
# clone is instant and free, and anywhere else it degrades to a plain copy that
# still lands on the same filesystem as the master (never in RAM).
# Sets SD_CLONE_MODE to reflink | copy | absent | failed.
sd_clone_for_run() {
    SD_CLONE_MODE=absent
    [[ -f "$SD_MASTER_IMAGE" ]] || return 1
    local dst="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    mkdir -p "$RUN_DIR/sdcard" || { SD_CLONE_MODE=failed; return 1; }
    rm -f "$dst"
    if cp --reflink=always "$SD_MASTER_IMAGE" "$dst" 2>/dev/null; then
        SD_CLONE_MODE=reflink
    elif cp --reflink=auto "$SD_MASTER_IMAGE" "$dst" 2>/dev/null; then
        SD_CLONE_MODE=copy
    else
        SD_CLONE_MODE=failed
        return 1
    fi
    return 0
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

# standalone_summary — totals line for a directly-executed test script; exits
# non-zero if any row failed. (The driver prints its own results block instead.)
standalone_summary() {
    echo ""
    echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"
    [[ "$fail" -eq 0 ]] || exit 1
}

# png_diff <a> <b> [sentinel] — count pixels whose channels differ exactly.
#
# Do not use `compare -metric AE` here. ImageMagick 7.1.2 Q16-HDRI reports
# a quantum-scaled absolute-error sum for that metric (one black -> white
# pixel is 65535), not a pixel count. Build an any-channel binary difference
# mask instead, then its mean times its area is the literal number of changed
# pixels. A command or parse failure yields <sentinel> (default 999999, i.e.
# "treat as different"), as does a pixel-dimension mismatch between the two
# images (GH #85): -composite silently crops to the overlap, so two images of
# different sizes could otherwise compare as identical — and a dimension
# mismatch can only mean harness/renderer breakage.
png_diff() {
    local raw dims a_dim b_dim sentinel=${3:-999999}
    local -a image_cmd
    if command -v magick &>/dev/null; then
        image_cmd=(magick)
    elif command -v convert &>/dev/null; then
        image_cmd=(convert)
    else
        echo "$sentinel"
        return
    fi

    # IHDR dimension check first: "<w>x<h>;<w>x<h>;" for the two images.
    dims=$("${image_cmd[@]}" "$1" "$2" -format '%wx%h;' info: 2>/dev/null) || {
        echo "$sentinel"
        return
    }
    a_dim=${dims%%;*}
    b_dim=${dims#*;}; b_dim=${b_dim%%;*}
    if [[ ! "$a_dim" =~ ^[0-9]+x[0-9]+$ || "$a_dim" != "$b_dim" ]]; then
        echo "$sentinel"
        return
    fi

    raw=$("${image_cmd[@]}" "$1" "$2" \
        -compose difference -composite -threshold 0 \
        -separate -evaluate-sequence max \
        -format '%[fx:mean*w*h]' info: 2>/dev/null) || {
        echo "$sentinel"
        return
    }
    if [[ "$raw" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        printf '%.0f\n' "$raw"
    else
        echo "$sentinel"
    fi
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
    # stays empty, a deliberate seam. jnext falls back to the self-provisioned
    # image under its state directory (sdcard::provision_sd_card,
    # src/core/sdcard_provisioner.*), the same FAT32-patched canonical distro
    # image an end user gets. That state directory is $JNEXT_CONFIG_DIR, which
    # sd_clone_for_run below points at a PRIVATE per-run clone (see there).
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

    # The MASTER SD image — machine-wide, shared, and the only copy the suite
    # ever writes. 01-sdcard-provision.sh gates it on a hash and re-derives it
    # if a stray interactive session drifted it.
    SD_MASTER_DIR="$HOME/.jnext/sdcard"
    SD_MASTER_IMAGE="$SD_MASTER_DIR/cspect-next-1gb-fixed.img"
    SD_MASTER_WITNESS="$SD_MASTER_IMAGE.sha256"

    # This run's PRIVATE state directory (GH #65). Two jobs at once:
    #
    #  1. GUI preferences. Every NON-headless $JNEXT invocation constructs the
    #     real MainWindow, which loads AppConfig unconditionally for fields
    #     with no CLI flag (CPU speed, window scale, CRT filter, tape
    #     fast-load); a stray real config could silently change CPU speed and
    #     perturb timing-sensitive pixel comparisons. Pointing every load() at
    #     a fresh directory yields clean AppConfigData defaults.
    #
    #  2. The SD-card image. src/core/sdcard_provisioner.cpp resolves the
    #     fallback image under this same directory, so the run boots
    #     $RUN_DIR/sdcard/cspect-next-1gb-fixed.img — a private clone — and
    #     never the master. NextZXOS WRITES BACK to the card it boots, so
    #     without this the suite mutates the very artefact its reference
    #     screenshots were generated against. That is not hypothetical: a drift
    #     turned a green branch into 91 pass / 5 FAIL, indistinguishable from a
    #     rendering regression.
    #
    # WHY UNDER $HOME AND NOT $TMP_DIR. The clone is made with reflink, which
    # is a filesystem-internal operation: it works only WITHIN one filesystem.
    # On the dev host $HOME is btrfs and /tmp is tmpfs, and `cp --reflink` from
    # one to the other fails with "Invalid cross-device link" — `--reflink=auto`
    # then silently degrades to a real 1 GB copy INTO RAM, per concurrent run.
    # Same-filesystem, the clone takes ~60 ms and costs no space until written.
    RUN_DIR="$HOME/.jnext/runs/$$-$(date +%s)-$RANDOM"
    export JNEXT_CONFIG_DIR="$RUN_DIR"
    # (XDG_CONFIG_HOME is kept for any other Qt state.)
    export XDG_CONFIG_HOME="$TMP_DIR/xdg-config-home"

    # ONE trap, installed before either directory has anything in it. Cleanup
    # must be a trap and never a final step: a killed or timed-out run would
    # otherwise leak a gigabyte, and `set -e` has already killed three scripts
    # in this suite.
    # INT/TERM as well as EXIT: an EXIT-only trap does not fire when the
    # shell dies from an untrapped signal, so a Ctrl-C during a multi-minute
    # regression run leaked the ~1 GB clone. This dates from GH #65 and was
    # found by the GH #75 review; same one-line shape as the fix in
    # test/run-unit-tests.sh and test/bench/bench.sh, and HS-40 now scans all
    # three so it cannot come back.
    #
    # THIS script is why the INT/TERM handlers exit explicitly. Registered as
    # `EXIT INT TERM` sharing one cleanup-only handler, a Ctrl-C here removed
    # the SD clone and then RESUMED the suite: measured 58 FAIL / 4 PASS
    # against an image that no longer existed, every row reading like a real
    # emulator regression. The per-test loop tolerates its own errors, so
    # nothing downstream trips `set -e` to stop it. A wrong result that is
    # believed is worse than no result.
    trap 'regression_cleanup' EXIT
    trap 'regression_cleanup; exit 130' INT
    trap 'regression_cleanup; exit 143' TERM

    # Clone now, so a DIRECTLY-EXECUTED test script (every scripts/*.sh is also
    # standalone-runnable) has an image before it launches jnext. A full run
    # re-clones in the "[sdcard-provision]" row after the master's hash gate,
    # which may have re-derived it; the re-clone costs ~60 ms. A failure here is
    # not fatal on its own — the row is what reports and provisions.
    sd_clone_for_run || true

    # The master's identity at the start of the run. sdcard-isolation-func
    # compares against it at the END, after every screenshot row has booted
    # NextZXOS — so the assertion covers the whole suite, not one contrived
    # boot. 01-sdcard-provision.sh re-stamps if its hash gate re-derived it.
    SD_MASTER_STAMP=$(stat -c '%i:%Y:%s' "$SD_MASTER_IMAGE" 2>/dev/null || echo "absent")

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
    # want() succeeds), compare mode (not --update), nothing reported yet. The
    # driver overwrites these from its command line and its functional-tests
    # manifest.
    FILTER_TESTS=()
    declare -gA IS_FILTERED=()
    REPORTED_FUNC=()
    UPDATE_MODE=false
}

regression_lib_init
