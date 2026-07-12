#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator.
# Runs screenshot tests + a few functional/integration tests.
# (FUSE Z80 + Z80N opcode coverage lives in `make unit-test`.)
#
# Usage: bash test/00regression/regression.sh [--update] [test_name...]
#   --update    Update reference screenshots instead of comparing
#   test_name   Run only specified tests (default: all)

set -euo pipefail

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
# Wave 0.1 follow-up (2026-05-04): jnext now requires --sdcard at the CLI
# level (mandatory for every invocation, like real Next hardware). All
# regression invocations therefore include the canonical TBBlue/NextZXOS
# image as a shared shell array. When the boot-ROM auto-load gate is
# active (Next + sd_card non-empty + load_file empty), `BOOT` rows
# exercise the firmware path; rows with --load NEX skip the boot ROM via
# the cfg.load_file gate (Emulator::init).
SD_CARD_ARGS=(--sdcard "$PROJECT_DIR/roms/nextzxos-1gb-fat32fix.img")
# rewind_test is a unit-test binary (only built when ENABLE_TESTS=ON,
# i.e. via `make unit-test-build`); the rewind functional test below
# SKIPs gracefully if it is missing.
CONF="$SCRIPT_DIR/regression_tests.conf"
# img/ lives next to this script under test/00regression/img.
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
if bash "$PROJECT_DIR/test/lint-assertions.sh"; then
    echo -e "  ${GREEN}PASS${RESET}: no new tautological assertions"
    pass=$((pass + 1))
else
    echo -e "  ${RED}FAIL${RESET}: new tautological assertions detected (see above)"
    fail=$((fail + 1))
fi
echo ""

# FUSE Z80 + Z80N opcode tests are run by `make unit-test`
# (fuse_z80_test + z80n_test). They were duplicated here historically
# but added no signal that wasn't already in unit-test, so they have
# been removed from the regression run.

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
    read -r test_name machine_type nex_file delay_frames extra_args <<< "$line"

    # Filter if specific tests requested
    if [[ ${#FILTER_TESTS[@]} -gt 0 ]]; then
        match=false
        for ft in "${FILTER_TESTS[@]}"; do
            [[ "$test_name" == "$ft" ]] && match=true
        done
        $match || continue
    fi

    ORDERED_TESTS+=("$test_name")
    TEST_INFO["$test_name"]="$machine_type $nex_file $delay_frames"

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
        "${SD_CARD_ARGS[@]}" \
        --load "$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex" \
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
        "${SD_CARD_ARGS[@]}" \
        --magic-port 0xCAFE --magic-port-mode line \
        --load "$PROJECT_DIR/test/00regression/nex/magic_port_demo.nex" \
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
        "${SD_CARD_ARGS[@]}" \
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

# Audio underrun test (GitHub issue #7 / Task 23): verify the emulator's audio
# clock tracks the sound card's, so the device queue never runs dry.
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
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'audio-underrun-func'; then
    printf "  %-25s " "[audio-underrun-func]"
    tone_bin="$SCRIPT_DIR/bin/beeper_tone.bin"
    checker="$SCRIPT_DIR/check-audio-underruns.py"
    raw_file="$TMP_DIR/audio_underrun.raw"
    if ! command -v xvfb-run &>/dev/null; then
        echo -e "${YELLOW}SKIP${RESET} (xvfb-run not available; audio needs a display)"
        skip=$((skip + 1))
    elif ! command -v python3 &>/dev/null; then
        echo -e "${YELLOW}SKIP${RESET} (python3 not available for capture analysis)"
        skip=$((skip + 1))
    else
        rm -f "$raw_file"
        # A bare 18-byte square-wave loop: sound starts immediately, with none of
        # the tape-fastload burst that would pre-fill the queue and mask the leak.
        SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_file" \
        timeout --foreground --kill-after=5s 40s xvfb-run -a "$JNEXT" \
            "${SD_CARD_ARGS[@]}" \
            --machine 48k \
            --inject "$tone_bin" --inject-org 8000 --inject-pc 8000 --inject-delay 100 \
            --delayed-automatic-exit 12 &>/dev/null || true
        if [[ ! -s "$raw_file" ]]; then
            echo -e "${YELLOW}SKIP${RESET} (no audio captured; no SDL audio backend?)"
            skip=$((skip + 1))
        elif underrun_out=$(python3 "$checker" "$raw_file" --skip-secs 3 2>&1); then
            echo -e "${GREEN}PASS${RESET} (no audio underruns)"
            pass=$((pass + 1))
        else
            echo -e "${RED}FAIL${RESET} ($(echo "$underrun_out" | head -1))"
            fail=$((fail + 1))
        fi
    fi
fi

# Bare-filename CLI test (Task 25): `jnext <file>` must load the file exactly as
# `--load <file>` does, while a mistyped flag must still be an error rather than
# being silently swallowed as a filename.
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'cli-bare-file-func'; then
    printf "  %-25s " "[cli-bare-file-func]"
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
        echo -e "${RED}FAIL${RESET} (bare filename did not load the NEX)"
        fail=$((fail + 1))
    elif [[ $typo_rc -eq 0 ]]; then
        echo -e "${RED}FAIL${RESET} (a mistyped flag was accepted as a filename)"
        fail=$((fail + 1))
    elif [[ $both_rc -eq 0 ]]; then
        echo -e "${RED}FAIL${RESET} (--load plus a bare file was not rejected)"
        fail=$((fail + 1))
    else
        echo -e "${GREEN}PASS${RESET} (bare filename loads; typo and --load+file rejected)"
        pass=$((pass + 1))
    fi
fi

# RZX recording test: verify --rzx-record produces a valid RZX file
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'rzx-record-func'; then
    printf "  %-25s " "[rzx-record-func]"
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

# A --delayed-screenshot that is requested and never taken must FAIL LOUDLY:
# an `error` log line and a non-zero exit, never a silent "no PNG, status 0"
# that a CI script would read as success (Task 22b round-2 review).
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
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'screenshot-pending-func'; then
    printf "  %-25s " "[screenshot-pending-func]"
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
        echo -e "${GREEN}PASS${RESET} (pending capture: error + exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (pending_rc=$pend_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$ctrl_rc)"
        fail=$((fail + 1))
    fi
fi

if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'screenshot-paused-func'; then
    printf "  %-25s " "[screenshot-paused-func]"
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
        echo -e "${GREEN}PASS${RESET} (paused debugger: error + exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (paused_rc=$paused_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$pctrl_rc)"
        fail=$((fail + 1))
    fi
fi

# Rewind / backwards execution unit tests
REWIND_TEST="$PROJECT_DIR/build/test/rewind_test"
if [[ -x "$REWIND_TEST" ]]; then
    if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx 'rewind-func'; then
        printf "  %-25s " "[rewind-func]"
        rewind_out=$(timeout --foreground --kill-after=5s 30s "$REWIND_TEST" 2>/dev/null || true)
        rewind_summary=$(echo "$rewind_out" | grep -oP "Passed:\s+\d+(?=.*Failed:\s+0)" || true)
        if [[ -n "$rewind_summary" ]]; then
            rewind_passed=$(echo "$rewind_summary" | grep -oP "\d+")
            echo -e "${GREEN}PASS${RESET} (${rewind_passed}/${rewind_passed} rewind unit tests)"
            pass=$((pass + 1))
        else
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
