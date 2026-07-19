#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator.
# Runs screenshot tests + a few functional/integration tests.
# (FUSE Z80 + Z80N opcode coverage lives in `make unit-test`.)
#
# Usage: bash test/00regression/regression.sh [--update] [--preflight-only] [test_name...]
#   --update          Update reference screenshots instead of comparing
#   --preflight-only  Run only the harness preflight checks and exit (no tests).
#                     This is the seam test/harness-selftest.sh drives.
#   test_name         Run only specified tests (default: all)
#
# Env: JNEXT_REGRESSION_CONF / JNEXT_REGRESSION_FUNC_CONF override the manifests
#      (the self-test uses these to inject a truncated or unpinned manifest).

set -euo pipefail

# Pin the locale for the whole suite: assertions grep C-locale strerror() text
# ("No such file or directory"), Qt's QApplication constructor calls
# setlocale(LC_ALL, "") which would localise it, and ImageMagick's `compare`
# must emit a C-locale decimal point for the pixel-diff parse. The suite's
# result must not depend on the user's LANG.
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Locate the jnext executable. Prefer gui-release (fastest), then
# gui-debug, then the canonical build/. The first existing executable
# wins. Override with JNEXT=... in the environment to bypass the search.
if [[ -z "${JNEXT:-}" ]]; then
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
# SD-card image: the suite does NOT pass --sdcard at all — SD_CARD_ARGS stays
# empty, a deliberate seam. jnext falls back to the pristine, self-provisioned
# image at $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img (sdcard::
# provision_sd_card, src/core/sdcard_provisioner.*), the same FAT32-patched
# canonical distro image an end user gets; the "[sdcard-provision]" row below
# ensures it exists before any test row runs. When the boot-ROM auto-load gate
# is active (Next + sd_card non-empty + load_file empty), `BOOT` rows exercise
# the firmware path; rows with --load NEX skip the boot ROM via the
# cfg.load_file gate (Emulator::init).
SD_CARD_ARGS=()
# rewind_test is a unit-test binary (only built when ENABLE_TESTS=ON, i.e. via
# `make unit-test-build`, which `make regression` depends on). If it is
# missing, the rewind functional test FAILS LOUDLY in the preflight — never a
# silently absent row.
REWIND_TEST="$PROJECT_DIR/build/test/rewind_test"
CONF="${JNEXT_REGRESSION_CONF:-$SCRIPT_DIR/regression_tests.conf}"
FUNC_CONF="${JNEXT_REGRESSION_FUNC_CONF:-$SCRIPT_DIR/functional_tests.conf}"
# img/ lives next to this script under test/00regression/img.
IMG_DIR="$SCRIPT_DIR/img"
# NextZXOS welcome-screen reference, diffed against by several boot rows.
WELCOME_REF="$IMG_DIR/boot-nextzxos-welcome-reference.png"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

# Isolate GUI preferences from the developer's real config. Every NON-headless
# $JNEXT invocation below constructs the real MainWindow, which loads AppConfig
# unconditionally for fields with no CLI flag (CPU speed, window scale, CRT
# filter, tape fast-load) — a stray real config file could silently change CPU
# speed and perturb timing-sensitive pixel comparisons. JNEXT_CONFIG_DIR
# points every load() at a fresh, never-created directory, i.e. clean
# AppConfigData defaults. (XDG_CONFIG_HOME is kept for any other Qt state.)
export JNEXT_CONFIG_DIR="$TMP_DIR/jnext-config-home"
export XDG_CONFIG_HOME="$TMP_DIR/xdg-config-home"

# Pixel difference tolerance (0 = exact match)
TOLERANCE=${JNEXT_TEST_TOLERANCE:-0}

# Pinned RTC for deterministic NextZXOS boot screenshots (must match the
# checked-in boot-nextzxos-* references).
NEXTZXOS_RTC="2026-07-10T08:55:00"

# Parse arguments
UPDATE_MODE=false
PREFLIGHT_ONLY=false
FILTER_TESTS=()
for arg in "$@"; do
    if [[ "$arg" == "--update" ]]; then
        UPDATE_MODE=true
    elif [[ "$arg" == "--preflight-only" ]]; then
        PREFLIGHT_ONLY=true
    else
        FILTER_TESTS+=("$arg")
    fi
done
# Membership is an in-shell hash lookup, NEVER `printf ... | grep -qx`: under
# `set -o pipefail` grep -q exits the instant it matches, the printf subshell
# can then die of SIGPIPE (141), and pipefail promotes 141 to the pipeline's
# status — a name that IS present reports as absent.
declare -A IS_FILTERED
for arg in "${FILTER_TESTS[@]+"${FILTER_TESTS[@]}"}"; do IS_FILTERED["$arg"]=1; done

# Colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

pass=0
fail=0
skip=0

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

# Check prerequisites
if [[ ! -x "$JNEXT" ]]; then
    echo -e "${RED}ERROR: jnext binary not found at $JNEXT — build first${RESET}"
    exit 1
fi

# No SD-card fixture existence check here: the suite self-provisions the
# fallback image (see the "[sdcard-provision]" row below) after the lint row
# and before the screenshot launch, once the manifest/preflight checks below
# have passed. --preflight-only never needs an SD image at all.

# A manifest that is not there must say so, not be diagnosed as "missing its pin".
for conf in "$CONF" "$FUNC_CONF"; do
    [[ -f "$conf" ]] || harness_fault "Test manifest not found: ${BOLD}$conf${RESET}"
done
echo -e "  manifests: $(basename "$CONF") + $(basename "$FUNC_CONF")"

# rewind-func runs a unit-test binary that `make clean` deletes. Check it HERE,
# in the first second, not five minutes into the run: an incomplete build is a
# harness fault, not a code regression — and never a silently absent row.
if [[ ${#FILTER_TESTS[@]} -eq 0 || -n "${IS_FILTERED[rewind-func]:-}" ]]; then
    if [[ ! -x "$REWIND_TEST" ]]; then
        harness_fault "rewind_test is not built: ${BOLD}$REWIND_TEST${RESET}" \
                      "The suite runs it, so it cannot report a rewind result without it." \
                      "Build it with: ${BOLD}make unit-test-build${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# --- The declared counts, pinned ---
# `# expect: N` in each manifest. Without it, deleting a test from a conf
# shrinks both sides of the completeness check below in lockstep and the suite
# reports a smaller number as a clean pass. The pin makes the denominator a
# claim the file has to make out loud, exactly like test/unit-tests.conf's
# per-suite row counts.
declared_count() {   # declared_count <conf>  — non-comment, non-blank lines
    grep -cvE '^[[:space:]]*(#|$)' "$1" || true
}
pinned_count() {     # pinned_count <conf>  — the `# expect: N` line
    # `|| true` is load-bearing: grep exits 1 when there is no pin line, and
    # under `set -e` + pipefail that would kill the script AT THE ASSIGNMENT,
    # before the "No '# expect: N' pin" fault below could print.
    grep -oP '^#\s*expect:\s*\K[0-9]+' "$1" 2>/dev/null | head -1 || true
}
for conf in "$CONF" "$FUNC_CONF"; do
    pin=$(pinned_count "$conf")
    have=$(declared_count "$conf")
    [[ -n "$pin" ]] || harness_fault "No '# expect: N' pin in ${BOLD}$conf${RESET}" \
                                     "The manifest must state how many tests it declares."
    [[ "$pin" -eq "$have" ]] || harness_fault \
        "${BOLD}$conf${RESET} declares ${BOLD}$have${RESET} tests but pins ${BOLD}# expect: $pin${RESET}" \
        "A test was added or removed without updating the pin. If deliberate, update it."
done

# --- The screenshot manifest needs an INDEPENDENT witness ---
# The completeness check at the end compares the rows reported against the
# rows declared — but for screenshots both sides come from
# regression_tests.conf, so on its own that term is a tautology.
# img/<name>-reference.png is the independent witness: it is checked in, one
# per screenshot test, and it does not disappear when the conf is truncated.
# A reference with no conf entry means a test was dropped from the manifest.
declare -A IS_DECLARED_SCREENSHOT
while read -r name _; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    IS_DECLARED_SCREENSHOT["$name"]=1
done < "$CONF"
for ref in "$IMG_DIR"/*-reference.png; do
    [[ -e "$ref" ]] || continue           # no refs at all (fresh tree) — nothing to witness
    ref_name=${ref##*/}
    ref_name=${ref_name%-reference.png}
    [[ -n "${IS_DECLARED_SCREENSHOT[$ref_name]:-}" ]] \
        || harness_fault "reference image ${BOLD}img/${ref_name}-reference.png${RESET} exists, but ${BOLD}$ref_name${RESET} is NOT declared in regression_tests.conf" \
                         "A screenshot test was dropped from the manifest. If that was deliberate, delete its reference image too."
done

# --- The declared functional tests (test/00regression/functional_tests.conf) ---
# Each block below calls `begin_func <name>`, which records that the row was
# actually reported. The completeness check at the end of the run proves that
# every declared test reported exactly one row and no undeclared row appeared.
DECLARED_FUNC=()
while read -r name _; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    DECLARED_FUNC+=("$name")
done < "$FUNC_CONF"
[[ ${#DECLARED_FUNC[@]} -gt 0 ]] || harness_fault "No functional tests declared in $FUNC_CONF"
declare -A IS_DECLARED_FUNC
for name in "${DECLARED_FUNC[@]}"; do IS_DECLARED_FUNC["$name"]=1; done
REPORTED_FUNC=()

# Every preflight guard has now run. --preflight-only exists so the self-test
# can drive each of them in a second instead of a five-minute suite: these
# guards are what make the denominator trustworthy.
if $PREFLIGHT_ONLY; then
    echo -e "${GREEN}preflight OK${RESET}: $(declared_count "$CONF") screenshot + ${#DECLARED_FUNC[@]} functional tests declared, pins agree"
    exit 0
fi

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

if ! command -v compare &>/dev/null; then
    echo -e "${YELLOW}WARNING: ImageMagick 'compare' not found — pixel comparison disabled${RESET}"
    HAS_COMPARE=false
else
    HAS_COMPARE=true
fi

mkdir -p "$IMG_DIR"

echo -e "${BOLD}=== JNEXT Regression Test Suite ===${RESET}"
echo ""

# --- Tautological-assertion lint (fast fail on new offenders) ---
echo -e "${BOLD}[lint-assertions] Scanning test/ for tautological assertions...${RESET}"
if bash "$PROJECT_DIR/test/lint-assertions.sh"; then
    printf "  "; pass_row ": no new tautological assertions"
else
    printf "  "; fail_row ": new tautological assertions detected (see above)"
fi
echo ""

# --- SD-card image self-provisioning (must run before ANY row that needs it) ---
# Every screenshot test and several functional tests need a NextZXOS SD image
# (the pristine fallback, see SD_CARD_ARGS above). Ensure it is present,
# exactly once, right here — before the screenshot tests launch in parallel
# below (all of them would race to provision it otherwise). If it is already
# present (the common case — provisioning is a one-time, machine-wide cost),
# this is instant.
echo -e "${BOLD}[sdcard-provision] Ensuring NextZXOS SD image is provisioned...${RESET}"
FALLBACK_SD_IMAGE="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"
if [[ -f "$FALLBACK_SD_IMAGE" ]]; then
    printf "  "; pass_row ": already present ($FALLBACK_SD_IMAGE)"
else
    echo "  not present — provisioning via jnext's own download+patch path (one-time)..."
    "$JNEXT" --headless --sdcard-download-confirm --delayed-automatic-exit 2 >/dev/null 2>&1 || true
    if [[ -f "$FALLBACK_SD_IMAGE" ]]; then
        printf "  "; pass_row ": provisioned $FALLBACK_SD_IMAGE"
    else
        printf "  "; fail_row ": provisioning failed — $FALLBACK_SD_IMAGE still missing"
    fi
fi
echo ""

# FUSE Z80 + Z80N opcode tests run in `make unit-test` (fuse_z80_test +
# z80n_test), not here.

# --- Screenshot tests ---
echo -e "${BOLD}Running screenshot tests...${RESET}"
echo ""

# Maximum parallel jobs (default: number of CPUs). The JNEXT_TEST_JOBS=4 cap
# used on every full run is deliberate: pacing-bounded tests lie under load.
MAX_JOBS=${JNEXT_TEST_JOBS:-$(nproc 2>/dev/null || echo 4)}

# Phase 1: Launch all emulator instances in parallel to generate screenshots
ORDERED_TESTS=()

while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    read -r test_name machine_type nex_file delay_frames extra_args <<< "$line"

    want "$test_name" || continue

    ORDERED_TESTS+=("$test_name")

    out_img="$TMP_DIR/${test_name}.png"
    # Wall-clock safety: assume the emulator clears at least 25 fps
    # headless (real-world is much higher; this is a worst-case bound
    # so the auto-exit + timeout don't fire before the screenshot).
    # +5 s buffer lets the screenshot+quit cleanup finish.
    exit_delay=$(( delay_frames / 25 + 5 ))
    [[ $exit_delay -lt 15 ]] && exit_delay=15
    wall_timeout=$(( (exit_delay + 5) * 4 ))

    cmd=("timeout" "--kill-after=5s" "${wall_timeout}s"
         "$JNEXT" "--headless"
         "${SD_CARD_ARGS[@]}"
         "--machine" "$machine_type"
         "--delayed-screenshot" "$out_img"
         "--delayed-screenshot-frames" "$delay_frames"
         "--delayed-automatic-exit" "$exit_delay")

    if [[ "$nex_file" != "BOOT" ]]; then
        cmd+=("--load" "$PROJECT_DIR/$nex_file")
    fi

    # Append extra CLI arguments (e.g. --delayed-keypress 2 0)
    if [[ -n "$extra_args" ]]; then
        read -ra extra_array <<< "$extra_args"
        cmd+=("${extra_array[@]}")
    fi

    # Launch in background
    "${cmd[@]}" &>/dev/null &

    # Throttle: wait if we've reached MAX_JOBS
    while [[ $(jobs -rp | wc -l) -ge $MAX_JOBS ]]; do
        wait -n 2>/dev/null || true
    done
done < "$CONF"

# Wait for all background jobs to finish
wait 2>/dev/null || true

# Phase 2: Evaluate results (sequential, for ordered output)
for test_name in "${ORDERED_TESTS[@]}"; do
    ref_img="$IMG_DIR/${test_name}-reference.png"
    out_img="$TMP_DIR/${test_name}.png"

    printf "  %-25s " "[$test_name]"

    # Check if emulator produced output
    if [[ ! -f "$out_img" ]]; then
        fail_row " (emulator crashed or timed out)"
        continue
    fi

    if $UPDATE_MODE; then
        cp "$out_img" "$ref_img"
        echo -e "${YELLOW}UPDATED${RESET} reference"
        pass=$((pass + 1))
        continue
    fi

    if [[ ! -f "$ref_img" ]]; then
        skip_row " (no reference image — run with --update first)"
        continue
    fi

    if $HAS_COMPARE; then
        diff_pixels=$(png_diff "$out_img" "$ref_img")
        if [[ "$diff_pixels" -le "$TOLERANCE" ]]; then
            pass_row " (${diff_pixels} pixel diff)"
        else
            fail_row " (${diff_pixels} pixels differ)"
            compare "$out_img" "$ref_img" "$IMG_DIR/${test_name}-diff.png" 2>/dev/null || true
            continue
        fi
    else
        skip_row " (no ImageMagick)"
        continue
    fi
done

# --- Functional tests ---
echo ""
echo -e "${BOLD}Running functional tests...${RESET}"
echo ""

# The unit-test harness is load-bearing for every number this project quotes,
# so it is itself under test (test/harness-selftest.sh injects each harness
# fault and asserts the refusal).
if want harness-selftest-func; then
    begin_func harness-selftest-func
    # Timeout-wrapped like every other invocation here. Its only other time
    # bound would be the per-suite timeout inside the very harness it is
    # testing — circular.
    if hs_out=$(timeout --foreground --kill-after=5s 120s bash "$PROJECT_DIR/test/harness-selftest.sh" 2>&1); then
        hs_line=$(echo "$hs_out" | grep -E '^Total:' | tail -1)
        pass_row " ($hs_line)"
    else
        fail_row " (the test harness itself is broken — see below)"
        echo "$hs_out" | grep -E '^\s*FAIL' | head -5 | sed -E 's/^/      /'
    fi
fi

# Magic breakpoint test: verify ED FF is detected and logged
if want magic-bp-func; then
    begin_func magic-bp-func
    bp_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless --magic-breakpoint \
        "${SD_CARD_ARGS[@]}" \
        --load "$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex" \
        --delayed-automatic-exit 3 2>&1) || true
    bp_count=$(echo "$bp_output" | grep -c "Magic breakpoint hit" || true)
    if [[ "$bp_count" -ge 1 ]]; then
        pass_row " ($bp_count magic breakpoint(s) detected)"
    else
        fail_row " (no magic breakpoint detected in output)"
    fi
fi

# Magic port test: verify port output appears on stderr in line mode
if want magic-port-func; then
    begin_func magic-port-func
    port_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --magic-port 0xCAFE --magic-port-mode line \
        --load "$PROJECT_DIR/test/00regression/nex/magic_port_demo.nex" \
        --delayed-automatic-exit 3 2>&1) || true
    if echo "$port_output" | grep -q "Hello from ZX Next!"; then
        pass_row " (magic port output verified)"
    else
        fail_row " (expected 'Hello from ZX Next!' in magic port output)"
    fi
fi

# Video recording test: verify --record produces a valid MP4 file
if want video-record-func; then
    begin_func video-record-func
    rec_file="$TMP_DIR/jnext_test_recording.mp4"
    rm -f "$rec_file"
    timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --record "$rec_file" \
        --delayed-automatic-exit 3 2>/dev/null || true
    if [[ -f "$rec_file" ]] && command -v ffprobe &>/dev/null; then
        rec_probe=$(ffprobe -show_streams "$rec_file" 2>/dev/null || true)
        has_video=$(grep -c "codec_type=video" <<< "$rec_probe" || true)
        has_audio=$(grep -c "codec_type=audio" <<< "$rec_probe" || true)
        if [[ "$has_video" -ge 1 && "$has_audio" -ge 1 ]]; then
            pass_row " (MP4 with video+audio streams)"
        else
            fail_row " (MP4 missing video or audio stream)"
        fi
    elif [[ -f "$rec_file" ]]; then
        skip_row " (ffprobe not available for validation)"
    else
        fail_row " (no MP4 file produced)"
    fi
fi

# Direct WAV capture must use the mixed-audio path in headless mode and remain
# attached when sibling-NEX chaining reconstructs the Emulator. The selector
# chains at frame 100; a recording substantially longer than that first leg
# proves capture resumed after the cold boot.
if want wav-record-func; then
    begin_func wav-record-func
    wav_file="$TMP_DIR/direct_audio.wav"
    menu_nex="$PROJECT_DIR/test/00regression/nex/menu.nex"
    if reject_out=$("$JNEXT" --headless --silent --wav-record "$wav_file" 2>&1); then
        reject_status=0
    else
        reject_status=$?
    fi
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$menu_nex" \
        --wav-record "$wav_file" --delayed-keypress-frames 100 1 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    chained=$(echo "$out" | grep -cE "NEX: loaded '.*red\.nex'" || true)
    if [[ "$reject_status" -eq 0 ]] ||
       ! echo "$reject_out" | grep -q -- "--wav-record cannot be combined with --silent"; then
        fail_row " (--silent conflict was not rejected clearly)"
    elif [[ "$chained" -lt 1 ]]; then
        fail_row " (selector did not chain-load red.nex)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for WAV validation)"
    elif wav_out=$(python3 - "$wav_file" <<'PY'
import struct
import sys

path = sys.argv[1]
with open(path, "rb") as wav:
    data = wav.read()
if len(data) < 44:
    raise SystemExit("WAV is shorter than its header")
if data[:4] != b"RIFF" or data[8:12] != b"WAVE" or data[36:40] != b"data":
    raise SystemExit("invalid WAV container")
rate, channels, bits, payload = (
    struct.unpack_from("<I", data, 24)[0],
    struct.unpack_from("<H", data, 22)[0],
    struct.unpack_from("<H", data, 34)[0],
    struct.unpack_from("<I", data, 40)[0],
)
if (rate, channels, bits) != (44100, 2, 16):
    raise SystemExit(f"unexpected format {rate} Hz/{channels} ch/{bits} bit")
if payload != len(data) - 44 or payload < 600000:
    raise SystemExit(f"capture stopped early ({payload} payload bytes)")
print(f"{payload} payload bytes across selector cold boot")
PY
    ); then
        pass_row " ($wav_out)"
    else
        fail_row " ($wav_out)"
    fi
fi

# Exercise --dac-trace through the real port-dispatch path. The injected Z80N
# program runs DI; NEXTREG $08,$08; writes $11/$22/$33/$44
# to Soundrive A/B/C/D, then idles.
if want dac-trace-func; then
    begin_func dac-trace-func
    dac_bin="$TMP_DIR/dac_trace.bin"
    dac_csv="$TMP_DIR/dac_trace.csv"
    printf '\xF3\xED\x91\x08\x08\x3E\x11\xD3\x1F\x3E\x22\xD3\x0F\x3E\x33\xD3\x4F\x3E\x44\xD3\x5F\x18\xFE' > "$dac_bin"
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --inject "$dac_bin" --inject-org 8000 \
        --inject-pc 8000 --dac-trace "$dac_csv" \
        --delayed-automatic-exit-frames 30 &>/dev/null &&
       [[ $(head -n 1 "$dac_csv" 2>/dev/null) == "segment,tstate,channel,value" ]] &&
       grep -qE '^[0-9]+,[0-9]+,0,17$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,1,34$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,2,51$' "$dac_csv" &&
       grep -qE '^[0-9]+,[0-9]+,3,68$' "$dac_csv"; then
        pass_row " (physical DAC channels A-D traced through CLI)"
    else
        fail_row " (missing or incorrect DAC CSV rows)"
    fi
fi

# Render-skip at turbo speed (Qt frontend only). At --speed 400 the Qt tick
# throttles Emulator::render_frame() to ~50 Hz wall-clock for frames nobody
# displays. This row proves the three load-bearing guarantees:
#   (1) capture-frame correctness: a --delayed-screenshot taken at --speed 400
#       is pixel-identical to the headless (never-skipping) capture of the
#       SAME emulated frame — i.e. the skip can never hit the capture frame.
#       beast.nex is used because its parallax animates every frame, so a
#       stale framebuffer (frame ~97-99 instead of 100) differs visibly.
#   (2) recorder guard: with --record active the core NEVER skips (the debug
#       witness line "render skipped for undisplayed frame" must be absent),
#       and the MP4 contains genuinely fresh frames — adjacent decoded frames
#       in the animated region differ. Without the guard the recorder would
#       capture the same stale composite 3-4x in a row.
#   (3) engagement: the skip actually fires at --speed 400 (witness line
#       present in the no-record run). NOTE: (3) is wall-clock-pacing bounded
#       like audio-underrun-func — skips only happen when ticks arrive faster
#       than 20 ms, so a severely starved host could render every frame and
#       report zero skips. The 48K boot workload (~2.5 ms/frame) + 600 frames
#       makes that require a sustained ~8x slowdown.
if want render-skip-turbo-func; then
    begin_func render-skip-turbo-func
    beast_nex="$PROJECT_DIR/test/00regression/nex/beast.nex"
    ctrl_png="$TMP_DIR/rsk-ctrl.png"
    turbo_png="$TMP_DIR/rsk-turbo.png"
    turbo_mp4="$TMP_DIR/rsk-turbo.mp4"
    turbo_log="$TMP_DIR/rsk-turbo.log"
    rec_log="$TMP_DIR/rsk-rec.log"
    engage_log="$TMP_DIR/rsk-engage.log"
    rm -f "$ctrl_png" "$turbo_png" "$turbo_mp4" "$turbo_log" "$rec_log" "$engage_log"
    SKIP_WITNESS="render skipped for undisplayed frame"

    if ! $HAS_COMPARE || ! command -v ffmpeg &>/dev/null || ! command -v ffprobe &>/dev/null; then
        skip_row " (needs ImageMagick compare + ffmpeg/ffprobe)"
    else
        rsk_fail=""

        # (1a) headless control: beast frame 100, never-skipping path.
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$beast_nex" \
            --delayed-screenshot "$ctrl_png" --delayed-screenshot-frames 100 \
            --delayed-automatic-exit-frames 130 >/dev/null 2>&1 || rsk_fail="ctrl-run"

        # (1b) Qt offscreen at --speed 400: same emulated frame.
        QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --silent --speed 400 \
            --log-level video=debug --load "$beast_nex" \
            --delayed-screenshot "$turbo_png" --delayed-screenshot-frames 100 \
            --delayed-automatic-exit-frames 130 >/dev/null 2>"$turbo_log" || rsk_fail="${rsk_fail:+$rsk_fail,}turbo-run"

        if [[ -z "$rsk_fail" ]]; then
            diff_pixels=$(png_diff "$turbo_png" "$ctrl_png")
            [[ "$diff_pixels" -eq 0 ]] || rsk_fail="capture-diff=$diff_pixels"
        fi

        # (2) --record at --speed 400: guard forces every frame to render.
        if [[ -z "$rsk_fail" ]]; then
            QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --silent --speed 400 \
                --log-level video=debug --load "$beast_nex" \
                --record "$turbo_mp4" \
                --delayed-automatic-exit-frames 220 >/dev/null 2>"$rec_log" || rsk_fail="rec-run"
        fi
        if [[ -z "$rsk_fail" ]]; then
            if grep -q "$SKIP_WITNESS" "$rec_log"; then
                rsk_fail="skip-while-recording"
            fi
            rec_streams=$(count_streams "$turbo_mp4" video)
            [[ "$rec_streams" -ge 1 ]] || rsk_fail="${rsk_fail:-mp4-no-video}"
        fi
        if [[ -z "$rsk_fail" ]]; then
            # Adjacent decoded frames in the animated region must differ.
            # A missing recorder guard duplicates the stale composite across
            # each ~20 ms window (3-4 frames), so at least one adjacent pair
            # among 5 consecutive frames decodes (near-)identical.
            rm -f "$TMP_DIR"/rsk-f*.png
            ffmpeg -v error -i "$turbo_mp4" -vf "select='between(n,150,154)'" -fps_mode passthrough \
                "$TMP_DIR/rsk-f%d.png" </dev/null 2>/dev/null || true
            if [[ -f "$TMP_DIR/rsk-f5.png" ]]; then
                for i in 1 2 3 4; do
                    # Sentinel 0 is deliberate: a parse failure must read as
                    # "stale pair" and fail the check, not slip past it.
                    pair_diff=$(png_diff "$TMP_DIR/rsk-f$i.png" "$TMP_DIR/rsk-f$((i+1)).png" 0)
                    if [[ "$pair_diff" -le 100 ]]; then
                        rsk_fail="stale-recorded-frame-pair-$i-diff=$pair_diff"
                        break
                    fi
                done
            else
                rsk_fail="mp4-frame-extract"
            fi
        fi

        # (3) engagement: skips actually fire at turbo speed (48K boot, cheap).
        if [[ -z "$rsk_fail" ]]; then
            QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 --silent --speed 400 \
                --log-level video=debug \
                --delayed-automatic-exit-frames 600 >/dev/null 2>"$engage_log" || rsk_fail="engage-run"
        fi
        if [[ -z "$rsk_fail" ]]; then
            engage_count=$(grep -c "$SKIP_WITNESS" "$engage_log" || true)
            # Lower bound: the throttle engages at all. Upper bound: it must
            # also RENDER at ~50 Hz — a broken throttle that skips every
            # single frame produces skips == frames. 590 leaves room for the
            # fastest plausible tick rate while rejecting skip-everything.
            [[ "$engage_count" -gt 0 ]] || rsk_fail="no-skip-at-turbo"
            [[ -n "$rsk_fail" ]] || [[ "$engage_count" -le 590 ]] || rsk_fail="skipped-every-frame=$engage_count"
        fi

        if [[ -z "$rsk_fail" ]]; then
            pass_row " (turbo capture identical; recorder never skipped, frames fresh; ${engage_count} skips engaged)"
        else
            fail_row " ($rsk_fail)"
        fi
    fi
fi

# Sprite collision/overtime (port 0x303B) under render-skip.
# Collision (bit 0) and line-budget overtime (bit 1) are computed
# inside the sprite render path (sprites.vhd:971-995); a skipped frame must
# still produce them (Emulator::run_frame calls
# Renderer::run_sprite_side_effects on skipped frames). This row proves it:
# the SAME poller program runs headless (never skips) and in the Qt GUI at
# --speed 400 (skips most composites), and the two magic-port status
# sequences must be IDENTICAL, with the control actually observing
# collisions (else the row proves nothing).
#
# Poller: test/00regression/bin/sprite_collision_poll.bin (64 bytes,
# org/pc 0x8000, hand-assembled):
#   di ; nextreg 0x15,0x01                      ; sprites visible
#   ld bc,0x303B ; xor a ; out (c),a            ; select slot 0
#   ld bc,0x0057                                ; attribute upload port
#   2x [ x=100, y=100, byte2=0, byte3=0x80 ]    ; two overlapping sprites,
#                                               ; pattern 0 (all-zero bytes,
#                                               ; != transparent 0xE3)
#   loop: ld hl,0x2000 ; dly: dec hl; ld a,h; or l; jr nz,dly   (~213k T)
#         ld bc,0x303B ; in a,(c)               ; read status (clears flags)
#         ld bc,0x1234 ; out (c),a              ; magic port
#         jr loop
if want sprite-collision-turbo-func; then
    begin_func sprite-collision-turbo-func
    poll_bin="$SCRIPT_DIR/bin/sprite_collision_poll.bin"
    scp_ctrl_log="$TMP_DIR/scp-ctrl.log"
    scp_turbo_log="$TMP_DIR/scp-turbo.log"
    rm -f "$scp_ctrl_log" "$scp_turbo_log"
    SCP_ARGS=(--rewind-buffer-size 0
              --inject "$poll_bin" --inject-org 0x8000 --inject-pc 0x8000
              --inject-delay 20
              --magic-port 0x1234 --magic-port-mode hex
              --delayed-automatic-exit-frames 400)

    scp_fail=""
    # Control: headless never skips a render.
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" "${SCP_ARGS[@]}" \
        >/dev/null 2>"$scp_ctrl_log" || scp_fail="ctrl-run"
    # Qt GUI at --speed 400: most frames skip the compositor.
    if [[ -z "$scp_fail" ]]; then
        QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --silent --speed 400 "${SCP_ARGS[@]}" \
            >/dev/null 2>"$scp_turbo_log" || scp_fail="turbo-run"
    fi
    if [[ -z "$scp_fail" ]]; then
        scp_ctrl_seq=$(grep -E '^[0-9A-F]{2}$' "$scp_ctrl_log")
        scp_turbo_seq=$(grep -E '^[0-9A-F]{2}$' "$scp_turbo_log")
        scp_polls=$(echo "$scp_ctrl_seq" | grep -c . || true)
        scp_hits=$(echo "$scp_ctrl_seq" | grep -cE '^(01|03)$' || true)
        if [[ "$scp_polls" -eq 0 ]]; then
            scp_fail="no-polls"
        elif [[ "$scp_hits" -eq 0 ]]; then
            scp_fail="control-saw-no-collisions"
        elif [[ "$scp_ctrl_seq" != "$scp_turbo_seq" ]]; then
            scp_fail="turbo-status-sequence-differs (ctrl $scp_polls polls/$scp_hits hits, turbo $(echo "$scp_turbo_seq" | grep -c . || true)/$(echo "$scp_turbo_seq" | grep -cE '^(01|03)$' || true))"
        fi
    fi
    if [[ -z "$scp_fail" ]]; then
        pass_row " (0x303B identical headless vs GUI@400%: $scp_polls polls, $scp_hits collision reads)"
    else
        fail_row " ($scp_fail)"
    fi
fi

# Audio underrun test: verify the emulator's audio clock tracks the sound
# card's, so the device queue never runs dry.
#
# Audio is synthesised on the EMULATED clock (44100 samples per emulated
# second). A 48K frame is 69888 T @3.5 MHz = 1/50.08 s, so pacing emulation on a
# 20 ms wall-clock timer (50.00 fps) synthesises only ~44030 samples per REAL
# second while the sound card consumes 44100. The device queue drains to empty
# and SDL pads it with ZEROS — a hole punched into a live waveform, i.e. an
# audible click, a few times a second, for as long as the emulator runs.
#
# Captured via SDL's `disk` audio driver (raw S16LE stereo 44100), which writes
# exactly what jnext hands the sound card. --headless has no audio at all, so
# this must run the GUI binary under a virtual X server.
if want audio-underrun-func; then
    begin_func audio-underrun-func
    tone_bin="$SCRIPT_DIR/bin/beeper_tone.bin"
    checker="$SCRIPT_DIR/check-audio-underruns.py"
    raw_file="$TMP_DIR/audio_underrun.raw"
    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; audio needs a display)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for capture analysis)"
    else
        rm -f "$raw_file"
        # A bare 18-byte square-wave loop: sound starts immediately, with none
        # of the tape-fastload burst that would pre-fill the queue and mask the
        # leak. This is the ONE test that cannot use --headless (it needs a
        # real audio path), so it runs the windowed emulator under xvfb-run.
        # xvfb-run only sets DISPLAY, and on a Wayland session Qt/SDL prefer
        # the Wayland backend — so WAYLAND_DISPLAY is unset and the X11
        # backends forced, making Qt render into the Xvfb display instead of
        # the real compositor. Deliberately NOT done: pointing WAYLAND_DISPLAY
        # at a dead socket — that makes SDL fail to bring up an audio backend
        # and the test SKIPs, trading a harmless residual probe connect for a
        # silently-disabled test.
        SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_file" \
        timeout --foreground --kill-after=5s 40s \
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
        xvfb-run -a "$JNEXT" \
            "${SD_CARD_ARGS[@]}" \
            --machine 48k \
            --inject "$tone_bin" --inject-org 8000 --inject-pc 8000 --inject-delay 100 \
            --delayed-automatic-exit 12 &>/dev/null || true
        if [[ ! -s "$raw_file" ]]; then
            skip_row " (no audio captured; no SDL audio backend?)"
        elif underrun_out=$(python3 "$checker" "$raw_file" --skip-secs 3 2>&1); then
            pass_row " (no audio underruns)"
        else
            fail_row " ($(echo "$underrun_out" | head -1))"
        fi
    fi
fi

# --silent test: (1) no audio device is ever opened, and (2) the frames it
# renders are pixel-identical to a normal run — muting output must not skip,
# stall, or otherwise perturb CPU/video execution.
#
#   - SDL's `disk` audio driver only ever creates its output file inside
#     SDL_OpenAudioDevice. An ABSENT file is direct proof no device was opened
#     — stronger than grepping a log line, which would still pass if the
#     message were renamed while the device kept opening underneath it.
#   - The pixel-compare content-verifies that --silent didn't perturb
#     execution. "The process exited around N seconds" would NOT catch a bug
#     that also stalls run_frame(): --delayed-automatic-exit's countdown fires
#     in on_frame_tick() regardless of whether run_frame() itself ran, so a
#     stuck CPU and a live one both exit "on time".
#
# QT_QPA_PLATFORM=offscreen (no xvfb needed) keeps this fast; SDL's disk
# driver needs no display of its own, unlike audio-underrun-func's real
# playback check.
if want silent-func; then
    begin_func silent-func
    raw_normal="$TMP_DIR/silent_normal.raw"
    raw_silent="$TMP_DIR/silent_silent.raw"
    png_normal="$TMP_DIR/silent_normal.png"
    png_silent="$TMP_DIR/silent_silent.png"
    rm -f "$raw_normal" "$raw_silent" "$png_normal" "$png_silent"

    SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_normal" QT_QPA_PLATFORM=offscreen \
    timeout --foreground --kill-after=5s 30s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 \
        --delayed-screenshot "$png_normal" --delayed-screenshot-frames 150 \
        --delayed-automatic-exit 5 &>/dev/null || true

    SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_silent" QT_QPA_PLATFORM=offscreen \
    timeout --foreground --kill-after=5s 30s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 --silent \
        --delayed-screenshot "$png_silent" --delayed-screenshot-frames 150 \
        --delayed-automatic-exit 5 &>/dev/null || true

    if [[ ! -s "$raw_normal" ]]; then
        skip_row " (no SDL audio backend available; control run captured nothing)"
    elif [[ ! -s "$png_normal" || ! -s "$png_silent" ]]; then
        fail_row " (screenshot missing: normal=$([[ -s "$png_normal" ]] && echo y || echo n) silent=$([[ -s "$png_silent" ]] && echo y || echo n))"
    elif [[ -e "$raw_silent" ]]; then
        fail_row " (--silent still opened an audio device)"
    elif $HAS_COMPARE; then
        diff_pixels=$(png_diff "$png_silent" "$png_normal")
        if [[ "$diff_pixels" -eq 0 ]]; then
            pass_row " (no audio device opened; frame 150 pixel-identical to a normal run)"
        else
            fail_row " (--silent changed rendered output: ${diff_pixels} pixels differ)"
        fi
    else
        skip_row " (no ImageMagick — cannot content-verify frame identity)"
    fi
fi

# --silent + --record test: with audio synthesis skipped the audio temp file
# is 0 bytes; VideoRecorder::stop() must detect that and encode video-only (no
# audio input, no "-shortest") — ffmpeg fed a zero-duration raw-PCM input plus
# "-shortest" clamps the WHOLE output to zero duration and still exits 0. So
# assert the output MP4 has a real video stream with a real (non-zero)
# duration, not just "a file appeared".
if want silent-record-func; then
    begin_func silent-record-func
    rec_file="$TMP_DIR/silent_recording.mp4"
    rm -f "$rec_file"
    if ! command -v ffprobe &>/dev/null; then
        skip_row " (ffprobe not available for validation)"
    else
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --silent \
            "${SD_CARD_ARGS[@]}" \
            --record "$rec_file" \
            --delayed-automatic-exit 3 &>/dev/null || true
        if [[ ! -s "$rec_file" ]]; then
            fail_row " (no MP4 file produced)"
        else
            has_video=$(count_streams "$rec_file" video)
            duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rec_file" 2>/dev/null || echo 0)
            duration_ok=$(awk -v d="$duration" 'BEGIN{print (d+0 >= 1.0) ? 1 : 0}')
            if [[ "$has_video" -ge 1 && "$duration_ok" -eq 1 ]]; then
                pass_row " (video-only MP4, ${duration}s duration, ${has_video} video stream)"
            else
                fail_row " (has_video=$has_video duration=$duration — corrupt/empty recording reported as success)"
            fi
        fi
    fi
fi

# Bare-filename CLI test: `jnext <file>` must load the file exactly as
# `--load <file>` does, while a mistyped flag must still be an error rather than
# being silently swallowed as a filename.
if want cli-bare-file-func; then
    begin_func cli-bare-file-func
    bare_nex="$SCRIPT_DIR/nex/celeste.nex"
    bare_out=$(timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" "$bare_nex" --delayed-automatic-exit 2 2>&1) || true
    # A typo'd flag must NOT be taken as a filename. These runs are EXPECTED to
    # exit non-zero, so capture the status in an if — a bare command would trip
    # the script's `set -e`.
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --hedless --delayed-automatic-exit 1 >/dev/null 2>&1
    then typo_rc=0; else typo_rc=1; fi
    # --load plus a bare file is ambiguous and must be rejected.
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --load "$bare_nex" "$bare_nex" --delayed-automatic-exit 1 >/dev/null 2>&1
    then both_rc=0; else both_rc=1; fi
    if ! echo "$bare_out" | grep -q "NEX: loaded"; then
        fail_row " (bare filename did not load the NEX)"
    elif [[ $typo_rc -eq 0 ]]; then
        fail_row " (a mistyped flag was accepted as a filename)"
    elif [[ $both_rc -eq 0 ]]; then
        fail_row " (--load plus a bare file was not rejected)"
    else
        pass_row " (bare filename loads; typo and --load+file rejected)"
    fi
fi

# RZX recording test: verify --rzx-record produces a valid RZX file
if want rzx-record-func; then
    begin_func rzx-record-func
    rzx_file="$TMP_DIR/test_recording.rzx"
    rzx_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --rzx-record "$rzx_file" \
        --delayed-automatic-exit 3 2>&1) || true
    if [[ -f "$rzx_file" ]]; then
        # Check file starts with RZX! magic signature
        magic=$(xxd -l 4 -p "$rzx_file" 2>/dev/null)
        frame_count=$(echo "$rzx_output" | grep -oP 'RZX:.*?\K\d+(?= frames)' || true)
        if [[ "$magic" == "525a5821" ]]; then
            pass_row " (valid RZX file, ${frame_count:-?} frames)"
        else
            fail_row " (file exists but invalid RZX signature)"
        fi
    else
        fail_row " (no RZX file produced)"
    fi
fi

# RZX roundtrip test: record then play back, verify playback starts
if want rzx-playback-func; then
    begin_func rzx-playback-func
    rzx_rt="$TMP_DIR/roundtrip.rzx"
    # Record 2 seconds
    timeout --foreground --kill-after=5s 8s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --rzx-record "$rzx_rt" \
        --delayed-automatic-exit 2 &>/dev/null || true
    if [[ -f "$rzx_rt" ]]; then
        # Play back and check for playback log message
        play_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" \
            --rzx-play "$rzx_rt" \
            --delayed-automatic-exit 3 2>&1) || true
        if echo "$play_output" | grep -qi "rzx.*play\|rzx.*load\|rzx.*snapshot"; then
            pass_row " (RZX playback started successfully)"
        else
            fail_row " (no RZX playback confirmation in log)"
        fi
    else
        fail_row " (RZX recording failed, cannot test playback)"
    fi
fi

# A --delayed-screenshot that is requested and never taken must FAIL LOUDLY:
# an `error` log line and a non-zero exit, never a silent "no PNG, status 0"
# that a CI script would read as success.
#
# Two cases, each with its own positive control so the test cannot pass by
# simply never producing a PNG:
#
#   screenshot-pending-func  headless — --delayed-automatic-exit fires before
#                            the capture comes due.
#   screenshot-paused-func   GUI (Qt, offscreen) — --magic-breakpoint pauses the
#                            debugger, so run_frame() stops and the capture is
#                            deferred; auto-exit then arrives with it still
#                            outstanding. This is the case that has no headless
#                            equivalent (headless has no debugger pause), so it
#                            is driven through the real Qt frontend.
if want screenshot-pending-func; then
    begin_func screenshot-pending-func
    png="$TMP_DIR/pending.png"
    rm -f "$png"
    # Capture due at frame 5000; auto-exit at 1 s (~50 frames). Never taken.
    if out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$png" --delayed-screenshot-frames 5000 \
                --delayed-automatic-exit 1 2>&1)
    then pend_rc=0; else pend_rc=1; fi

    # Positive control: same flags, capture comfortably before the exit.
    png_ok="$TMP_DIR/pending-ok.png"
    rm -f "$png_ok"
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then ctrl_rc=0; else ctrl_rc=1; fi

    if [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$png" ]] \
       && echo "$out" | grep -q "NO screenshot was written" \
       && [[ "$ctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (pending capture: error + exit!=0, no PNG; control writes one)"
    else
        fail_row " (pending_rc=$pend_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$ctrl_rc)"
    fi
fi

# --delayed-automatic-exit-frames N: the frames form of the exit
# bound. Two things must hold, and both are proved against the exit's own hard
# contract — a --delayed-screenshot left outstanding at exit errors and exits
# non-zero (see screenshot-pending-func above), which makes the exit frame
# OBSERVABLE to the frame.
#
#   exactness  Capture at frame N is taken (exit 0); capture at frame N+1 is
#              NOT (error + exit!=0). One frame either way and one of the two
#              runs flips: an off-by-one in the frame count cannot hide.
#   override   --delayed-automatic-exit 100 (=5000 frames) + -frames 10: the
#              capture at frame 11 must NOT be taken. If seconds won, the exit
#              would be 5000 frames away and the PNG would exist.
if want exit-frames-func; then
    begin_func exit-frames-func
    at_n="$TMP_DIR/exit-frames-at-n.png"       # capture at the exit frame     -> taken
    past_n="$TMP_DIR/exit-frames-past-n.png"   # capture one frame later       -> never taken
    ovr="$TMP_DIR/exit-frames-override.png"    # frames must beat seconds      -> never taken
    rm -f "$at_n" "$past_n" "$ovr"
    N=30

    # 1. Capture due at exactly frame N, exit at frame N: taken, exit 0.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$at_n" --delayed-screenshot-frames "$N" \
            --delayed-automatic-exit-frames "$N" >/dev/null 2>&1
    then at_rc=0; else at_rc=1; fi

    # 2. Capture due at frame N+1, exit at frame N: never taken, error, exit!=0.
    if past_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$past_n" --delayed-screenshot-frames "$((N + 1))" \
            --delayed-automatic-exit-frames "$N" 2>&1)
    then past_rc=0; else past_rc=1; fi

    # 3. Both forms given: frames (10) wins over seconds (100 = 5000 frames),
    #    so the capture at frame 11 is never taken.
    if ovr_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$ovr" --delayed-screenshot-frames 11 \
            --delayed-automatic-exit 100 --delayed-automatic-exit-frames 10 2>&1)
    then ovr_rc=0; else ovr_rc=1; fi

    if [[ "$at_rc" -eq 0 ]] && [[ -s "$at_n" ]] \
       && [[ "$past_rc" -ne 0 ]] && [[ ! -f "$past_n" ]] \
       && echo "$past_out" | grep -q "NO screenshot was written" \
       && [[ "$ovr_rc" -ne 0 ]] && [[ ! -f "$ovr" ]] \
       && echo "$ovr_out" | grep -q "NO screenshot was written"; then
        pass_row " (exit at frame $N exactly; -frames overrides seconds)"
    else
        fail_row " (at_rc=$at_rc at_png=$([[ -s "$at_n" ]] && echo y || echo n) past_rc=$past_rc past_png=$([[ -f "$past_n" ]] && echo y || echo n) override_rc=$ovr_rc override_png=$([[ -f "$ovr" ]] && echo y || echo n))"
    fi
fi

if want screenshot-paused-func; then
    begin_func screenshot-paused-func
    png="$TMP_DIR/paused.png"
    png_ok="$TMP_DIR/paused-ok.png"
    rm -f "$png" "$png_ok"
    bp_nex="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex"

    # --magic-breakpoint: the demo's ED FF traps into the debugger and PAUSES it.
    # run_frame() then stops, so the capture (frame 200) is deferred forever and
    # --delayed-automatic-exit (6 s) arrives with it still pending.
    if out=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --magic-breakpoint --load "$bp_nex" \
                --delayed-screenshot "$png" --delayed-screenshot-frames 200 \
                --delayed-automatic-exit 6 2>&1)
    then paused_rc=0; else paused_rc=1; fi

    # Positive control: identical, minus --magic-breakpoint, so it never pauses.
    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$bp_nex" \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 200 \
            --delayed-automatic-exit 6 >/dev/null 2>&1
    then pctrl_rc=0; else pctrl_rc=1; fi

    if [[ "$paused_rc" -ne 0 ]] && [[ ! -f "$png" ]] \
       && echo "$out" | grep -q "NO screenshot was written" \
       && [[ "$pctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (paused debugger: error + exit!=0, no PNG; control writes one)"
    else
        fail_row " (paused_rc=$paused_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$pctrl_rc)"
    fi
fi

# Third route by which a requested screenshot can fail to appear: the capture
# IS taken, but the PNG cannot be WRITTEN (missing directory, no permission,
# disk full). Same contract as the two tests above: error log + non-zero exit,
# each with a positive control.
#
#   screenshot-io-func     headless — unwritable path (directory does not exist)
#   screenshot-io-qt-func  GUI (Qt, offscreen) — same, through the Qt frontend
if want screenshot-io-func; then
    begin_func screenshot-io-func
    bad_png="$TMP_DIR/no-such-dir/io.png"     # parent directory does not exist
    png_ok="$TMP_DIR/io-ok.png"
    rm -rf "$TMP_DIR/no-such-dir"; rm -f "$png_ok"

    if out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$bad_png" --delayed-screenshot-frames 60 \
                --delayed-automatic-exit 5 2>&1)
    then io_rc=0; else io_rc=1; fi

    # Positive control: identical run, writable path — must write a PNG, exit 0.
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then ioctrl_rc=0; else ioctrl_rc=1; fi

    if [[ "$io_rc" -ne 0 ]] && [[ ! -f "$bad_png" ]] \
       && echo "$out" | grep -q "FAILED to write" \
       && echo "$out" | grep -q "No such file or directory" \
       && [[ "$ioctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (unwritable path: error+reason, exit!=0, no PNG; control writes one)"
    else
        fail_row " (io_rc=$io_rc png_exists=$([[ -f "$bad_png" ]] && echo y || echo n) control_rc=$ioctrl_rc)"
    fi
fi

if want screenshot-io-qt-func; then
    begin_func screenshot-io-qt-func
    bad_png="$TMP_DIR/no-such-dir-qt/io.png"  # parent directory does not exist
    png_ok="$TMP_DIR/io-qt-ok.png"
    rm -rf "$TMP_DIR/no-such-dir-qt"; rm -f "$png_ok"

    # 120 s timeout, not the 60 s used elsewhere: a Qt-offscreen boot to frame 60
    # takes ~28 s on an idle box, and the whole point of this test is that a
    # non-zero exit means "the PNG failed to write" — a timeout kill would be
    # indistinguishable from the very failure being asserted. Give it headroom.
    if out=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$bad_png" --delayed-screenshot-frames 60 \
                --delayed-automatic-exit 5 2>&1)
    then qio_rc=0; else qio_rc=1; fi

    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then qioctrl_rc=0; else qioctrl_rc=1; fi

    if [[ "$qio_rc" -ne 0 ]] && [[ ! -f "$bad_png" ]] \
       && echo "$out" | grep -q "FAILED to write" \
       && echo "$out" | grep -q "No such file or directory" \
       && [[ "$qioctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (Qt unwritable path: error+reason, exit!=0, no PNG; control writes one)"
    else
        fail_row " (qt_io_rc=$qio_rc png_exists=$([[ -f "$bad_png" ]] && echo y || echo n) control_rc=$qioctrl_rc)"
    fi
fi

# --delayed-snapshot (headless-only): save/reload proof plus the same
# "requested but never written" loud-failure contract the --delayed-screenshot
# tests above use (screenshot-pending-func).
if want snapshot-save-func; then
    begin_func snapshot-save-func
    szx="$TMP_DIR/snap.szx"
    orig_png="$TMP_DIR/snap-orig.png"
    reloaded_png="$TMP_DIR/snap-reloaded.png"
    rm -f "$szx" "$orig_png" "$reloaded_png"

    # Positive control: boot 48K to the BASIC copyright screen, capture it
    # AND save a .szx at the same frame (150) in the same run, so
    # snap-orig.png is a screenshot of the exact state snap.szx captured.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$orig_png" --delayed-screenshot-frames 150 \
            --delayed-snapshot "$szx" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then save_rc=0; else save_rc=1; fi

    # Reload proof: a FRESH process loads the saved file and renders a
    # frame. This must be PIXEL content-verified, not just "a PNG came
    # out" — a structurally-valid .szx with garbage RAM payloads still
    # loads and renders. 0 pixel diff vs snap-orig.png is required
    # (BASIC's copyright screen is static, so the reload must reproduce
    # it exactly).
    if [[ -s "$szx" ]] && timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$szx" \
            --delayed-screenshot "$reloaded_png" --delayed-screenshot-frames 1 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then reload_rc=0; else reload_rc=1; fi

    content_ok=0
    diff_pixels=-1
    if [[ -s "$orig_png" ]] && [[ -s "$reloaded_png" ]]; then
        if $HAS_COMPARE; then
            diff_pixels=$(png_diff "$reloaded_png" "$orig_png")
            [[ "$diff_pixels" -eq 0 ]] && content_ok=1
        else
            # No ImageMagick: cannot content-verify. Do NOT silently pass —
            # that would advertise coverage that does not exist.
            content_ok=-1
        fi
    fi

    # Negative control: a snapshot requested but never due before auto-exit
    # fires must be a loud non-zero-exit failure, never a silent no-op.
    pending="$TMP_DIR/snap-pending.szx"
    rm -f "$pending"
    if out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$pending" --delayed-snapshot-frames 5000 \
                --delayed-automatic-exit 1 2>&1)
    then pend_rc=0; else pend_rc=1; fi

    # Negative control: .szx is scoped to 48K/128K/+2A/+3 only — see the
    # SzxSaver class doc-comment SCOPE. jnext's DEFAULT --machine is Next,
    # so this is the common path in practice, not an edge case: it must
    # fail loudly (non-zero exit, clear reason logged, no file written),
    # never silently write a truncated/misrepresenting snapshot.
    refused="$TMP_DIR/snap-refused.szx"
    rm -f "$refused"
    if out_next=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$refused" --delayed-snapshot-frames 5 \
                --delayed-automatic-exit 3 2>&1)
    then refuse_rc=0; else refuse_rc=1; fi

    if [[ "$content_ok" -eq -1 ]]; then
        skip_row " (no ImageMagick — cannot content-verify the reload)"
    elif [[ "$save_rc" -eq 0 ]] && [[ -s "$szx" ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reloaded_png" ]] \
       && [[ "$content_ok" -eq 1 ]] \
       && [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$pending" ]] \
       && echo "$out" | grep -q "NO snapshot was written" \
       && [[ "$refuse_rc" -ne 0 ]] && [[ ! -f "$refused" ]] \
       && echo "$out_next" | grep -qi "cannot represent this machine"; then
        pass_row " (reload pixel-identical to pre-save screen; pending-never-written: error+exit!=0, no file; --machine next refused: error+exit!=0, no file)"
    else
        fail_row " (save_rc=$save_rc szx_exists=$([[ -s "$szx" ]] && echo y || echo n) reload_rc=$reload_rc png_exists=$([[ -s "$reloaded_png" ]] && echo y || echo n) content_ok=$content_ok diff_pixels=$diff_pixels pend_rc=$pend_rc pending_exists=$([[ -f "$pending" ]] && echo y || echo n) refuse_rc=$refuse_rc refused_exists=$([[ -f "$refused" ]] && echo y || echo n))"
    fi
fi

# Tape SAVE trap guard. An ordinary NextZXOS boot with --tape-save armed must
# boot pixel-identically to the boot-nextzxos-welcome reference and write ZERO
# blocks (file created empty by set_output, never grown). The SA-BYTES trap is
# gated on ROM identity (48K SA-BYTES prologue bytes at 0x04C2); an ungated
# trap fires repeatedly during this exact boot, appending garbage and breaking
# the boot — this row keeps the gate in place.
if want tape-save-boot-func; then
    begin_func tape-save-boot-func
    ts_tap="$TMP_DIR/jnext_test_tapesave_boot.tap"
    ts_png="$TMP_DIR/jnext_test_tapesave_boot.png"
    rm -f "$ts_tap" "$ts_png"
    ts_out=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --tape-save "$ts_tap" \
        --delayed-screenshot "$ts_png" --delayed-screenshot-frames 400 \
        --delayed-automatic-exit-frames 420 2>&1) || true
    ts_blocks=$(echo "$ts_out" | grep -c "TAP save: block" || true)
    ts_size=$(stat -c%s "$ts_tap" 2>/dev/null || echo missing)
    ts_diff=999999
    if [[ -f "$ts_png" ]]; then
        ts_diff=$(png_diff "$ts_png" "$WELCOME_REF")
    fi
    if [[ "$ts_blocks" -eq 0 && "$ts_size" == "0" && "$ts_diff" -le "$TOLERANCE" ]]; then
        pass_row " (NextZXOS boot clean with --tape-save armed: 0 blocks, empty file, ${ts_diff} px diff)"
    else
        fail_row " (blocks=$ts_blocks file_size=$ts_size px_diff=$ts_diff — SAVE trap fired during NextZXOS boot?)"
    fi
fi

# Reset after boot must re-boot to NextZXOS, not fall to 48K BASIC. The Reset
# button (and F1 / a program's NR 0x02 hard reset) is a power-on cold boot the
# host performs by reconstructing the emulator and re-running init() (the
# proven startup path). Boot, hard-reset via the headless reset facility once
# the welcome is up, and assert the re-booted screen is PIXEL-IDENTICAL to the
# fresh boot-nextzxos-welcome reference (cold boot == startup).
if want reset-to-nextzxos-func; then
    begin_func reset-to-nextzxos-func
    rst_png="$TMP_DIR/jnext_test_reset_nextzxos.png"
    rm -f "$rst_png"
    JNEXT_DELAYED_RESET_FRAMES=450 JNEXT_DELAYED_RESET_TYPE=hard \
    timeout --foreground --kill-after=5s 150s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --delayed-screenshot "$rst_png" --delayed-screenshot-frames 900 \
        --delayed-automatic-exit-frames 920 >/dev/null 2>&1 || true
    rst_diff=999999
    if [[ -f "$rst_png" ]]; then
        rst_diff=$(png_diff "$rst_png" "$WELCOME_REF")
    fi
    if [[ "$rst_diff" -le "$TOLERANCE" ]]; then
        pass_row " (reset after boot re-booted to NextZXOS: ${rst_diff} px diff vs welcome)"
    else
        fail_row " (px_diff=$rst_diff — reset did not re-boot to NextZXOS? [Task 70])"
    fi
fi

# A menu / cold-boot file-load must route by extension through the SHARED
# dispatch (platform/emulator_boot.h :: emulator_apply_load), so .rzx reaches
# load_rzx, not load_nex. Record a short RZX, then cold-boot-LOAD it via the
# headless reset facility (the SAME shared dispatch the Qt menu uses) and
# assert RZX playback started — misrouting to load_nex leaves no "RZX:
# playback started" line.
if want cold-boot-load-rzx-func; then
    begin_func cold-boot-load-rzx-func
    cb_rzx="$TMP_DIR/cold_boot_test.rzx"
    rm -f "$cb_rzx"
    timeout --foreground --kill-after=5s 40s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --rzx-record "$cb_rzx" --delayed-automatic-exit-frames 120 >/dev/null 2>&1 || true
    cb_out=""
    if [[ -f "$cb_rzx" ]]; then
        cb_out=$(JNEXT_DELAYED_RESET_FRAMES=420 JNEXT_DELAYED_RESET_TYPE="loadnex:$cb_rzx" \
            timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
            --delayed-automatic-exit-frames 520 2>&1 || true)
    fi
    if echo "$cb_out" | grep -q "RZX: playback started"; then
        pass_row " (cold-boot .rzx load routed to RZX playback via shared dispatch)"
    else
        fail_row " (cold-boot .rzx misrouted — shared load dispatch dropped .rzx? [Task 70 review])"
    fi
fi

# Rewind / backwards execution unit tests. The binary's absence is caught in
# the preflight, so by the time we get here it is there and the only question
# is whether it passes.
if want rewind-func; then
    begin_func rewind-func
    rewind_out=$(timeout --foreground --kill-after=5s 30s "$REWIND_TEST" 2>/dev/null || true)
    rewind_summary=$(echo "$rewind_out" | grep -oP "Passed:\s+\d+(?=.*Failed:\s+0)" || true)
    if [[ -n "$rewind_summary" ]]; then
        rewind_passed=$(echo "$rewind_summary" | grep -oP "\d+")
        pass_row " (${rewind_passed}/${rewind_passed} rewind unit tests)"
    else
        fail_line=$(echo "$rewind_out" | grep -E "^Total:" || echo "unknown")
        fail_row " ($fail_line)"
    fi
fi

# Benchmark harness line-format test: --benchmark N must print
# exactly one machine-parseable BENCH line to stdout, with every field present
# and the deterministic ones exact: a 48K machine at 3.5 MHz has
# (447+1)x(311+1)x4 / 8 = 69888 T-states/frame (timing.h HC/VC_MAX + Clock
# divisor 8). This validates the line contract, not performance — it runs on
# whatever binary/build the regression uses.
if want benchmark-func; then
    begin_func benchmark-func
    bench_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --benchmark 20 2>/dev/null) || true
    bench_count=$(echo "$bench_out" | grep -c '^BENCH ' || true)
    if [[ "$bench_count" -eq 1 ]] && echo "$bench_out" | grep -qE \
        '^BENCH workload=boot-48k frames=20 wall=[0-9]+\.[0-9]+ fps=[0-9]+\.[0-9]+ tstates_per_sec=[0-9]+ tstates_per_frame=69888 cpu=3\.5MHz core=[0-9]+@[0-9]+kHz build=.+$'; then
        pass_row " (one well-formed BENCH line, 69888 T-states/frame @ 3.5MHz)"
    else
        fail_row " (BENCH line missing or malformed; got: $(echo "$bench_out" | grep '^BENCH ' || echo '<none>'))"
    fi
fi

# --trace CLI flag. The instruction trace log is decoupled from
# rewind: --trace must enable it (the "Instruction trace log enabled (--trace)"
# info line is the observable), and — the discriminating half — a default run
# with no trace/rewind flags must NOT emit that line (trace off by default).
if want trace-func; then
    begin_func trace-func
    tr_line="Instruction trace log enabled (--trace)"
    tr_on=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --trace --delayed-automatic-exit-frames 20 2>&1) || true
    tr_off=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 20 2>&1) || true
    tr_on_count=$(echo "$tr_on" | grep -cF "$tr_line" || true)
    tr_off_count=$(echo "$tr_off" | grep -cF "$tr_line" || true)
    if [[ "$tr_on_count" -ge 1 && "$tr_off_count" -eq 0 ]]; then
        pass_row " (--trace enables the trace log; default run leaves it off)"
    else
        fail_row " (enable-line count: with --trace=$tr_on_count (want >=1), default=$tr_off_count (want 0))"
    fi
fi

# frame-pacing-func: a 60 Hz demo must pace the frontend timer to the emulated
# video refresh, not a hardcoded 50 Hz. Beast.nex switches the machine to 60 Hz
# (NR 0x05 bit 2); the Qt frontend logs the 50->60 Hz pacing transition and
# drives its frame timer at the exact 17.198 ms Next-60 period via the deadline
# scheduler instead of the 19.968 ms 50 Hz period. This is the ONLY suite row
# that exercises the frontend pacing path (headless is frame-counted and never
# touches it), so it is what stops a revert to a hardcoded 50 Hz period from
# sailing through green. Discriminative both ways: the 60 Hz demo MUST emit the
# "17.198 ms deadline scheduler" line, and a plain 50 Hz machine (48k) MUST NOT.
# QT_QPA_PLATFORM=offscreen runs the real GUI binary with no display; --silent
# keeps it audio-free.
if want frame-pacing-func; then
    begin_func frame-pacing-func
    beast_nex="$SCRIPT_DIR/nex/beast.nex"
    fp_60=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine next --load "$beast_nex" --silent \
        --delayed-automatic-exit-frames 150 2>&1) || true
    fp_50=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --silent \
        --delayed-automatic-exit-frames 150 2>&1) || true
    fp_60_hit=$(echo "$fp_60" | grep -cF "17.198 ms deadline scheduler" || true)
    fp_50_hit=$(echo "$fp_50" | grep -cF "17.198 ms deadline scheduler" || true)
    if [[ "$fp_60_hit" -ge 1 && "$fp_50_hit" -eq 0 ]]; then
        pass_row " (60 Hz demo paces to the 17.198 ms deadline; 50 Hz machine does not)"
    else
        fail_row " (60Hz->17.198ms line count=$fp_60_hit (want >=1), 48k count=$fp_50_hit (want 0))"
    fi
fi

# nex-extended-reject-func: NEXTEST.NEX and similar NextZXOS apps are a small
# NEX with a large payload appended that they stream from their own file at
# runtime. Loaded with --load there is no NextZXOS, so they black-screen. The
# loader rejects any NEX whose file is substantially larger than its header
# (banks + screens) describes. Discriminative both ways: an extended NEX MUST
# be rejected with the "extended NEX file" message, and a plain NEX of the
# exact declared size MUST NOT be. Files are synthesised here (no 26 MB binary
# in git).
if want nex-extended-reject-func; then
    begin_func nex-extended-reject-func
    ext_nex="$TMP_DIR/extended.nex"
    plain_nex="$TMP_DIR/plain.nex"
    stale_nex="$TMP_DIR/stale.nex"
    python3 - "$ext_nex" "$plain_nex" "$stale_nex" <<'PY'
import sys, struct
def header(num_banks_field, bitmap_banks):
    h = bytearray(512)
    h[0:4] = b"Next"; h[4:8] = b"V1.2"
    h[8] = 0                  # ram_required
    h[9] = num_banks_field    # decorative scalar (offset 9)
    h[10] = 0                 # screen_flags
    h[12:14] = struct.pack("<H", 0x8000)  # SP
    h[14:16] = struct.pack("<H", 0x8000)  # PC
    for i in range(bitmap_banks):
        h[18 + i] = 1         # presence bitmap = the authoritative count
    return bytes(h)
# extended: bitmap says 1 bank (16384) + 200 KB of appended payload → REJECT
open(sys.argv[1], "wb").write(header(1, 1) + b"\x00"*16384 + b"\xAA"*200000)
# plain: exactly header + 1 bank (bitmap), no trailing → accept
open(sys.argv[2], "wb").write(header(1, 1) + b"\x00"*16384)
# stale: bitmap says 3 banks, num_banks scalar wrongly says 1; file matches the
# BITMAP (3*16384). Must be accepted — expected_size must come from the bitmap,
# not the scalar. Guards against the num_banks false-positive.
open(sys.argv[3], "wb").write(header(1, 3) + b"\x00"*(3*16384))
PY
    ext_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$ext_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    plain_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$plain_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    stale_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next --load "$stale_nex" \
        --delayed-automatic-exit-frames 5 2>&1) || true
    ext_hit=$(echo "$ext_out" | grep -cF "extended NEX file" || true)
    plain_hit=$(echo "$plain_out" | grep -cF "extended NEX file" || true)
    stale_hit=$(echo "$stale_out" | grep -cF "extended NEX file" || true)
    # stale must NOT be rejected AND must warn about the num_banks/bitmap mismatch
    stale_warn=$(echo "$stale_out" | grep -cF "disagrees with the bank bitmap" || true)
    if [[ "$ext_hit" -ge 1 && "$plain_hit" -eq 0 && "$stale_hit" -eq 0 && "$stale_warn" -ge 1 ]]; then
        pass_row " (extended rejected; exact-size accepted; stale num_banks warned + loaded)"
    else
        fail_row " (ext=$ext_hit want>=1, plain=$plain_hit want0, stale_rej=$stale_hit want0, stale_warn=$stale_warn want>=1)"
    fi
fi

# esxdos-chain-{red,blue,return}-func: the --esxdos-stub M_EXECCMD
# ".RUN <name>.nex" path lets a selector NEX chain-load a SIBLING NEX (same
# directory) without booting NextZXOS. menu.nex shows white and, on key 1/2,
# runs red.nex/blue.nex; red/blue on key M run menu.nex back. Driven headlessly
# with --delayed-keypress-frames and asserted on the stub's .RUN request + the
# resulting NEX load. The keypress schedule and frame counter live in the
# frontend and survive the cold boot the chain triggers, so a second hop (the
# return) is observable in the same run.
MENU_NEX="$PROJECT_DIR/test/00regression/nex/menu.nex"

if want esxdos-chain-red-func; then
    begin_func esxdos-chain-red-func
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 100 1 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    req=$(echo "$out" | grep -cE "requested \.RUN '.*red\.nex'" || true)
    got=$(echo "$out" | grep -cE "NEX: loaded '.*red\.nex'" || true)
    if [[ "$req" -ge 1 && "$got" -ge 1 ]]; then
        pass_row " (menu -> key 1 -> chain-loaded red.nex)"
    else
        fail_row " (red .RUN=$req want>=1, red load=$got want>=1)"
    fi
fi

if want esxdos-chain-blue-func; then
    begin_func esxdos-chain-blue-func
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 100 2 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    req=$(echo "$out" | grep -cE "requested \.RUN '.*blue\.nex'" || true)
    got=$(echo "$out" | grep -cE "NEX: loaded '.*blue\.nex'" || true)
    if [[ "$req" -ge 1 && "$got" -ge 1 ]]; then
        pass_row " (menu -> key 2 -> chain-loaded blue.nex)"
    else
        fail_row " (blue .RUN=$req want>=1, blue load=$got want>=1)"
    fi
fi

if want esxdos-chain-return-func; then
    begin_func esxdos-chain-return-func
    out=$(timeout --foreground --kill-after=5s 40s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 80 2 --delayed-keypress-frames 200 m \
        --delayed-automatic-exit-frames 320 2>&1) || true
    to_blue=$(echo "$out" | grep -cE "requested \.RUN '.*blue\.nex'" || true)
    back=$(echo "$out" | grep -cE "requested \.RUN '.*menu\.nex'" || true)
    # menu.nex loads once at the initial --load and again on the return hop, so
    # a count of >=2 is what proves the round-trip actually chained back.
    menu_loads=$(echo "$out" | grep -cE "NEX: loaded '.*menu\.nex'" || true)
    if [[ "$to_blue" -ge 1 && "$back" -ge 1 && "$menu_loads" -ge 2 ]]; then
        pass_row " (menu -> blue -> menu round-trip via .RUN)"
    else
        fail_row " (blue .RUN=$to_blue want>=1, menu .RUN=$back want>=1, menu loads=$menu_loads want>=2)"
    fi
fi

# ffmpeg-missing-warn-func: jnext shells out to ffmpeg for --record /
# File > Record MPEG4 Video. At startup it probes for
# ffmpeg and, if absent, warns once (in EVERY mode, headless included) so the
# user is not surprised only when a recording silently fails. Discriminative
# both ways: with ffmpeg masked from PATH the warning MUST fire; with ffmpeg
# present it MUST NOT. The masked run points PATH at a directory with no
# executables (so `ffmpeg -version` cannot resolve); the control run uses the
# real PATH. Both use --headless so no display is needed.
if want ffmpeg-missing-warn-func; then
    begin_func ffmpeg-missing-warn-func
    warn_line="ffmpeg not found in PATH"
    # jnext is addressed by absolute path, so a stripped PATH does not stop it
    # launching — only its own ffmpeg probe fails. `env` sets PATH for the jnext
    # child ONLY; it must come AFTER timeout, or `env PATH=... timeout` would
    # leave `timeout` itself unresolvable on the stripped PATH.
    ff_masked=$(timeout --foreground --kill-after=5s 20s \
        env PATH=/nonexistent-jnext-ffmpeg-test "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 5 2>&1) || true
    ff_present=$(timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 5 2>&1) || true
    masked_hit=$(echo "$ff_masked" | grep -cF "$warn_line" || true)
    present_hit=$(echo "$ff_present" | grep -cF "$warn_line" || true)
    if ! command -v ffmpeg &>/dev/null; then
        # ffmpeg genuinely absent on this host — the control run cannot prove the
        # negative, so only assert the masked run warns. Loud, not silent.
        if [[ "$masked_hit" -ge 1 ]]; then
            pass_row " (warns when ffmpeg absent; control skipped — no ffmpeg on host)"
        else
            fail_row " (masked run did not warn: masked_hit=$masked_hit want>=1)"
        fi
    elif [[ "$masked_hit" -ge 1 && "$present_hit" -eq 0 ]]; then
        pass_row " (warns when ffmpeg masked from PATH; silent when present)"
    else
        fail_row " (masked_hit=$masked_hit want>=1, present_hit=$present_hit want0)"
    fi
fi

echo ""
echo -e "${BOLD}=== Results ===${RESET}"
echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"

# --- Completeness: prove the suite ran everything it declares ---
# A green result is only as trustworthy as its denominator. On a full run, every
# declared functional test must have reported exactly one row, no undeclared row
# may appear, and the grand total must equal 1 (lint) + 1 (sdcard-provision) +
# screenshots + functional. Anything else means a test went missing, which is a
# harness fault, not a pass.
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] && ! $UPDATE_MODE; then
    faults=()
    # Per-name row counts and membership come from pure-bash hashes over the
    # arrays — never a pipe/subshell (`printf ... | grep` can misreport under
    # `set -o pipefail`; see the note above IS_FILTERED).
    declare -A REPORTED_COUNT
    for name in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do
        REPORTED_COUNT["$name"]=$(( ${REPORTED_COUNT["$name"]:-0} + 1 ))
    done
    for name in "${DECLARED_FUNC[@]}"; do
        n=${REPORTED_COUNT["$name"]:-0}
        [[ "$n" -eq 1 ]] || faults+=("declared in functional_tests.conf but reported $n rows: ${BOLD}$name${RESET}")
    done
    for name in "${REPORTED_FUNC[@]}"; do
        [[ -n "${IS_DECLARED_FUNC[$name]:-}" ]] \
            || faults+=("reported a row but is NOT declared in functional_tests.conf: ${BOLD}$name${RESET}")
    done
    expected=$(( 1 + 1 + ${#ORDERED_TESTS[@]} + ${#DECLARED_FUNC[@]} ))
    actual=$(( pass + fail + skip ))
    [[ "$actual" -eq "$expected" ]] \
        || faults+=("row count is ${BOLD}$actual${RESET}, but 1 lint + 1 sdcard-provision + ${#ORDERED_TESTS[@]} screenshot + ${#DECLARED_FUNC[@]} functional = ${BOLD}$expected${RESET} were declared")
    if [[ ${#faults[@]} -gt 0 ]]; then
        harness_fault "${faults[@]}" "" \
            "The suite did not run what it says it ran. Treat this as RED, not as a pass."
    fi
    echo -e "  ${BOLD}$actual/$expected declared tests reported${RESET}"
fi

if [[ $fail -gt 0 ]]; then
    exit 1
fi
exit 0
