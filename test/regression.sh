#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator
# Runs screenshot tests and FUSE Z80 opcode tests.
#
# Usage: bash test/regression.sh [--update] [test_name...]
#   --update    Update reference screenshots instead of comparing
#   test_name   Run only specified tests (default: all)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
JNEXT="$PROJECT_DIR/build/jnext"
FUSE_TEST="$PROJECT_DIR/build/test/fuse_z80_test"
FUSE_DATA="$PROJECT_DIR/build/test/fuse"
CONF="$SCRIPT_DIR/regression_tests.conf"
IMG_DIR="$SCRIPT_DIR/img"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

# Pixel difference tolerance (0 = exact match)
TOLERANCE=${JNEXT_TEST_TOLERANCE:-0}

# Parse arguments
UPDATE_MODE=false
FILTER_TESTS=()
for arg in "$@"; do
    if [[ "$arg" == "--update" ]]; then
        UPDATE_MODE=true
    else
        FILTER_TESTS+=("$arg")
    fi
done

# Colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

pass=0
fail=0
skip=0

# Check prerequisites
if [[ ! -x "$JNEXT" ]]; then
    echo -e "${RED}ERROR: jnext binary not found at $JNEXT — build first${RESET}"
    exit 1
fi

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
if bash "$SCRIPT_DIR/lint-assertions.sh"; then
    echo -e "  ${GREEN}PASS${RESET}: no new tautological assertions"
    pass=$((pass + 1))
else
    echo -e "  ${RED}FAIL${RESET}: new tautological assertions detected (see above)"
    fail=$((fail + 1))
fi
echo ""

# --- FUSE Z80 opcode tests ---
echo -e "${BOLD}[fuse-z80] Running FUSE Z80 opcode tests...${RESET}"
if [[ -x "$FUSE_TEST" && -d "$FUSE_DATA" ]]; then
    fuse_output=$("$FUSE_TEST" "$FUSE_DATA" 2>&1) || true
    # Extract from format: "Total: 1356  Passed: 1340  Failed: 16  Skipped: 0"
    if echo "$fuse_output" | grep -qE "Total:.*Passed:"; then
        fuse_total=$(echo "$fuse_output" | grep -oP "Total:\s*\K[0-9]+")
        fuse_pass=$(echo "$fuse_output" | grep -oP "Passed:\s*\K[0-9]+")
        if [[ "$fuse_pass" -ge 1340 ]]; then
            echo -e "  ${GREEN}PASS${RESET}: $fuse_pass/$fuse_total opcodes passed"
            pass=$((pass + 1))
        else
            echo -e "  ${RED}FAIL${RESET}: only $fuse_pass/$fuse_total opcodes passed (expected >= 1340)"
            fail=$((fail + 1))
        fi
    else
        echo -e "  ${RED}FAIL${RESET}: unexpected output format"
        echo "$fuse_output" | tail -5
        fail=$((fail + 1))
    fi
else
    echo -e "  ${YELLOW}SKIP${RESET}: fuse_z80_test not built"
    skip=$((skip + 1))
fi

echo ""

# --- Z80N extended opcode tests ---
Z80N_TEST="$PROJECT_DIR/build/test/z80n_test"
Z80N_DATA="$PROJECT_DIR/build/test/z80n"
echo -e "${BOLD}[z80n] Running Z80N extended opcode tests...${RESET}"
if [[ -x "$Z80N_TEST" && -d "$Z80N_DATA" ]]; then
    z80n_output=$("$Z80N_TEST" "$Z80N_DATA" 2>&1) || true
    if echo "$z80n_output" | grep -qE "Total:.*Passed:"; then
        z80n_total=$(echo "$z80n_output" | grep -oP "Total:\s*\K[0-9]+")
        z80n_pass=$(echo "$z80n_output" | grep -oP "Passed:\s*\K[0-9]+")
        z80n_fail=$(echo "$z80n_output" | grep -oP "Failed:\s*\K[0-9]+")
        if [[ "$z80n_fail" -eq 0 ]]; then
            echo -e "  ${GREEN}PASS${RESET}: $z80n_pass/$z80n_total Z80N opcodes passed"
            pass=$((pass + 1))
        else
            echo -e "  ${RED}FAIL${RESET}: $z80n_pass/$z80n_total passed ($z80n_fail failures)"
            echo "$z80n_output" | grep "FAIL" | head -10
            fail=$((fail + 1))
        fi
    else
        echo -e "  ${RED}FAIL${RESET}: unexpected output format"
        echo "$z80n_output" | tail -5
        fail=$((fail + 1))
    fi
else
    echo -e "  ${YELLOW}SKIP${RESET}: z80n_test not built"
    skip=$((skip + 1))
fi

echo ""

# --- Screenshot tests ---
echo -e "${BOLD}Running screenshot tests...${RESET}"
echo ""

# Maximum parallel jobs (default: number of CPUs)
MAX_JOBS=${JNEXT_TEST_JOBS:-$(nproc 2>/dev/null || echo 4)}

# Phase 1: Launch all emulator instances in parallel to generate screenshots
declare -A TEST_PIDS  # test_name -> PID
declare -A TEST_INFO  # test_name -> "machine_type nex_file delay_secs"
ORDERED_TESTS=()

while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    read -r test_name machine_type nex_file delay_secs extra_args <<< "$line"

    # Filter if specific tests requested
    if [[ ${#FILTER_TESTS[@]} -gt 0 ]]; then
        match=false
        for ft in "${FILTER_TESTS[@]}"; do
            [[ "$test_name" == "$ft" ]] && match=true
        done
        $match || continue
    fi

    ORDERED_TESTS+=("$test_name")
    TEST_INFO["$test_name"]="$machine_type $nex_file $delay_secs"

    out_img="$TMP_DIR/${test_name}.png"
    exit_delay=$((delay_secs + 2))
    wall_timeout=$(( (exit_delay + 5) * 4 ))

    cmd=("timeout" "--kill-after=5s" "${wall_timeout}s"
         "$JNEXT" "--headless"
         "--machine-type" "$machine_type"
         "--delayed-screenshot" "$out_img"
         "--delayed-screenshot-time" "$delay_secs"
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
    TEST_PIDS["$test_name"]=$!

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
        echo -e "${RED}FAIL${RESET} (emulator crashed or timed out)"
        fail=$((fail + 1))
        continue
    fi

    if $UPDATE_MODE; then
        cp "$out_img" "$ref_img"
        echo -e "${YELLOW}UPDATED${RESET} reference"
        pass=$((pass + 1))
        continue
    fi

    if [[ ! -f "$ref_img" ]]; then
        echo -e "${YELLOW}SKIP${RESET} (no reference image — run with --update first)"
        skip=$((skip + 1))
        continue
    fi

    if $HAS_COMPARE; then
        diff_raw=$(compare -metric AE "$out_img" "$ref_img" /dev/null 2>&1) || true
        diff_pixels=$(echo "$diff_raw" | awk '{printf "%d", $1+0}' 2>/dev/null || echo 999999)
        if [[ "$diff_pixels" -le "$TOLERANCE" ]]; then
            echo -e "${GREEN}PASS${RESET} (${diff_pixels} pixel diff)"
        else
            echo -e "${RED}FAIL${RESET} (${diff_pixels} pixels differ)"
            compare "$out_img" "$ref_img" "$IMG_DIR/${test_name}-diff.png" 2>/dev/null || true
            fail=$((fail + 1))
            continue
        fi
    else
        echo -e "${YELLOW}SKIP${RESET} (no ImageMagick)"
        skip=$((skip + 1))
        continue
    fi

    pass=$((pass + 1))
done

# --- Functional tests ---
echo ""
echo -e "${BOLD}Running functional tests...${RESET}"
echo ""

# Magic breakpoint test: verify ED FF is detected and logged
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'magic-bp-func'; then
    printf "  %-25s " "[magic-bp-func]"
    bp_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless --magic-breakpoint \
        --load "$PROJECT_DIR/demo/magic_bp_demo/magic_bp_demo.nex" \
        --delayed-automatic-exit 3 2>&1) || true
    bp_count=$(echo "$bp_output" | grep -c "Magic breakpoint hit" || true)
    if [[ "$bp_count" -ge 1 ]]; then
        echo -e "${GREEN}PASS${RESET} ($bp_count magic breakpoint(s) detected)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (no magic breakpoint detected in output)"
        fail=$((fail + 1))
    fi
fi

# Magic port test: verify port output appears on stderr in line mode
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'magic-port-func'; then
    printf "  %-25s " "[magic-port-func]"
    port_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
        --magic-port 0xCAFE --magic-port-mode line \
        --load "$PROJECT_DIR/demo/magic_port_demo/magic_port_demo.nex" \
        --delayed-automatic-exit 3 2>&1) || true
    if echo "$port_output" | grep -q "Hello from ZX Next!"; then
        echo -e "${GREEN}PASS${RESET} (magic port output verified)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (expected 'Hello from ZX Next!' in magic port output)"
        fail=$((fail + 1))
    fi
fi

# Video recording test: verify --record produces a valid MP4 file
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'video-record-func'; then
    printf "  %-25s " "[video-record-func]"
    rec_file="/tmp/jnext_test_recording.mp4"
    rm -f "$rec_file"
    timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        --record "$rec_file" \
        --delayed-automatic-exit 3 2>/dev/null || true
    if [[ -f "$rec_file" ]] && command -v ffprobe &>/dev/null; then
        has_video=$(ffprobe -show_streams "$rec_file" 2>/dev/null | grep -c "codec_type=video" || true)
        has_audio=$(ffprobe -show_streams "$rec_file" 2>/dev/null | grep -c "codec_type=audio" || true)
        if [[ "$has_video" -ge 1 && "$has_audio" -ge 1 ]]; then
            echo -e "${GREEN}PASS${RESET} (MP4 with video+audio streams)"
            pass=$((pass + 1))
        else
            echo -e "${RED}FAIL${RESET} (MP4 missing video or audio stream)"
            fail=$((fail + 1))
        fi
    elif [[ -f "$rec_file" ]]; then
        echo -e "${YELLOW}SKIP${RESET} (ffprobe not available for validation)"
        skip=$((skip + 1))
    else
        echo -e "${RED}FAIL${RESET} (no MP4 file produced)"
        fail=$((fail + 1))
    fi
fi

# RZX recording test: verify --rzx-record produces a valid RZX file
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'rzx-record-func'; then
    printf "  %-25s " "[rzx-record-func]"
    rzx_file="$TMP_DIR/test_recording.rzx"
    rzx_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
        --rzx-record "$rzx_file" \
        --delayed-automatic-exit 3 2>&1) || true
    if [[ -f "$rzx_file" ]]; then
        # Check file starts with RZX! magic signature
        magic=$(xxd -l 4 -p "$rzx_file" 2>/dev/null)
        frame_count=$(echo "$rzx_output" | grep -oP 'RZX:.*?\K\d+(?= frames)' || true)
        if [[ "$magic" == "525a5821" ]]; then
            echo -e "${GREEN}PASS${RESET} (valid RZX file, ${frame_count:-?} frames)"
            pass=$((pass + 1))
        else
            echo -e "${RED}FAIL${RESET} (file exists but invalid RZX signature)"
            fail=$((fail + 1))
        fi
    else
        echo -e "${RED}FAIL${RESET} (no RZX file produced)"
        fail=$((fail + 1))
    fi
fi

# RZX roundtrip test: record then play back, verify playback starts
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'rzx-playback-func'; then
    printf "  %-25s " "[rzx-playback-func]"
    rzx_rt="$TMP_DIR/roundtrip.rzx"
    # Record 2 seconds
    timeout --foreground --kill-after=5s 8s "$JNEXT" --headless \
        --rzx-record "$rzx_rt" \
        --delayed-automatic-exit 2 &>/dev/null || true
    if [[ -f "$rzx_rt" ]]; then
        # Play back and check for playback log message
        play_output=$(timeout --foreground --kill-after=5s 10s "$JNEXT" --headless \
            --rzx-play "$rzx_rt" \
            --delayed-automatic-exit 3 2>&1) || true
        if echo "$play_output" | grep -qi "rzx.*play\|rzx.*load\|rzx.*snapshot"; then
            echo -e "${GREEN}PASS${RESET} (RZX playback started successfully)"
            pass=$((pass + 1))
        else
            echo -e "${RED}FAIL${RESET} (no RZX playback confirmation in log)"
            fail=$((fail + 1))
        fi
    else
        echo -e "${RED}FAIL${RESET} (RZX recording failed, cannot test playback)"
        fail=$((fail + 1))
    fi
fi

# Rewind / backwards execution unit tests
REWIND_TEST="$PROJECT_DIR/build/test/rewind_test"
if [[ -x "$REWIND_TEST" ]]; then
    if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'rewind-func'; then
        printf "  %-25s " "[rewind-func]"
        if timeout --foreground --kill-after=5s 30s "$REWIND_TEST" 2>/dev/null | grep -qP "Passed:\s+18.*Failed:\s+0"; then
            echo -e "${GREEN}PASS${RESET} (18/18 rewind unit tests)"
            pass=$((pass + 1))
        else
            rewind_out=$(timeout --foreground --kill-after=5s 30s "$REWIND_TEST" 2>/dev/null || true)
            fail_line=$(echo "$rewind_out" | grep -E "^Total:" || echo "unknown")
            echo -e "${RED}FAIL${RESET} ($fail_line)"
            fail=$((fail + 1))
        fi
    fi
fi

echo ""
echo -e "${BOLD}=== Results ===${RESET}"
echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"

if [[ $fail -gt 0 ]]; then
    exit 1
fi
exit 0
