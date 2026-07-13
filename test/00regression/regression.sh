#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator.
# Runs screenshot tests + a few functional/integration tests.
# (FUSE Z80 + Z80N opcode coverage lives in `make unit-test`.)
#
# Usage: bash test/00regression/regression.sh [--update] [--preflight-only] [test_name...]
#   --update          Update reference screenshots instead of comparing
#   --preflight-only  Run only the harness preflight checks and exit (no tests).
#                     This is the seam test/harness-selftest.sh drives: the preflight
#                     is what proves the suite runs everything it declares, and an
#                     untested guard ships broken (it did — twice).
#   test_name         Run only specified tests (default: all)
#
# Env: JNEXT_REGRESSION_CONF / JNEXT_REGRESSION_FUNC_CONF override the manifests
#      (the self-test uses these to inject a truncated or unpinned manifest).

set -euo pipefail

# Pin the locale for the whole suite. Several assertions grep the emulator's
# error output for C-locale strerror() text ("No such file or directory"), and
# Qt's QApplication constructor calls setlocale(LC_ALL, "") — so under a non-English
# locale the Qt frontend localises strerror and screenshot-io-qt-func FAILed while
# its headless twin (no QApplication, so still in the C locale) PASSed. The suite's
# result must not depend on the user's LANG. Also keeps ImageMagick's `compare`
# emitting a C-locale decimal point for the pixel-diff parse.
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
# Wave 0.1 follow-up (2026-05-04): jnext now requires --sdcard at the CLI
# level (mandatory for every invocation, like real Next hardware). All
# regression invocations therefore include the canonical TBBlue/NextZXOS
# image as a shared shell array. When the boot-ROM auto-load gate is
# active (Next + sd_card non-empty + load_file empty), `BOOT` rows
# exercise the firmware path; rows with --load NEX skip the boot ROM via
# the cfg.load_file gate (Emulator::init).
SD_IMAGE="$PROJECT_DIR/roms/nextzxos-1gb-fat32fix.img"
SD_CARD_ARGS=(--sdcard "$SD_IMAGE")
# rewind_test is a unit-test binary (only built when ENABLE_TESTS=ON, i.e. via
# `make unit-test-build`, which `make regression` now depends on). If it is
# missing the rewind functional test FAILS LOUDLY — it used to print no row at
# all, silently shrinking the suite total (Task 35).
REWIND_TEST="$PROJECT_DIR/build/test/rewind_test"
CONF="${JNEXT_REGRESSION_CONF:-$SCRIPT_DIR/regression_tests.conf}"
FUNC_CONF="${JNEXT_REGRESSION_FUNC_CONF:-$SCRIPT_DIR/functional_tests.conf}"
# img/ lives next to this script under test/00regression/img.
IMG_DIR="$SCRIPT_DIR/img"
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

# Pixel difference tolerance (0 = exact match)
TOLERANCE=${JNEXT_TEST_TOLERANCE:-0}

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

# Colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'

pass=0
fail=0
skip=0

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

# Every screenshot test mounts this image, and so does sd_rom_extractor_test.
# In a worktree it is a git-ignored symlink that must be provisioned; without it
# all 46 screenshot rows would FAIL for a reason none of them would name.
if [[ ! -f "$SD_IMAGE" ]]; then
    harness_fault "SD-card fixture missing: ${BOLD}$SD_IMAGE${RESET}" \
                  "Every screenshot test mounts it, so the whole suite is meaningless without it." \
                  "In an agent worktree, provision it with: ${BOLD}make worktree-bootstrap${RESET}"
fi

# A manifest that is not there must say so, not be diagnosed as "missing its pin".
for conf in "$CONF" "$FUNC_CONF"; do
    [[ -f "$conf" ]] || harness_fault "Test manifest not found: ${BOLD}$conf${RESET}"
done
echo -e "  manifests: $(basename "$CONF") + $(basename "$FUNC_CONF")"

# rewind-func runs a unit-test binary that `make clean` deletes. Check it HERE, in the
# first second, not five minutes into the run: an incomplete build is a harness fault,
# not a code regression — and never, as it once was, an absent row (Task 35).
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] || printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx rewind-func; then
    if [[ ! -x "$REWIND_TEST" ]]; then
        harness_fault "rewind_test is not built: ${BOLD}$REWIND_TEST${RESET}" \
                      "The suite runs it, so it cannot report a rewind result without it." \
                      "Build it with: ${BOLD}make unit-test-build${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# --- The declared counts, pinned ---
# `# expect: N` in each manifest. Without it, deleting a test from a conf shrinks both
# sides of the completeness check below in lockstep — expected and actual both go down
# and the suite reports a smaller number as a clean pass. Review round 2 removed one
# screenshot row plus its reference image and got a green 59/0/0, which is this
# project's own previous published baseline. The pin makes the denominator a claim the
# file has to make out loud, exactly like test/unit-tests.conf's per-suite row counts.
declared_count() {   # declared_count <conf>  — non-comment, non-blank lines
    grep -cvE '^[[:space:]]*(#|$)' "$1" || true
}
pinned_count() {     # pinned_count <conf>  — the `# expect: N` line
    # `|| true` is load-bearing: grep exits 1 when there is no pin line, and under
    # `set -e` + pipefail that kills the script AT THE ASSIGNMENT — so the
    # "No '# expect: N' pin" fault below would never print. A guard that cannot
    # report is not a guard; this one was dead on arrival until review caught it.
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
# The completeness check at the end compares the rows reported against the rows
# declared — but for screenshots both sides come from regression_tests.conf, so on
# its own that term is a tautology: delete 44 of the 46 lines and the suite happily
# reports "15/15 declared tests reported" and exits green. (Found in review. It is
# the very bug this file exists to abolish, so it does not get to live here.)
#
# img/<name>-reference.png is that independent witness: it is checked in, one per
# screenshot test, and it does not disappear when the conf is truncated. A reference
# with no conf entry means a test was dropped from the manifest.
for ref in "$IMG_DIR"/*-reference.png; do
    [[ -e "$ref" ]] || continue           # no refs at all (fresh tree) — nothing to witness
    ref_name=$(basename "$ref" -reference.png)
    grep -qE "^[[:space:]]*${ref_name}[[:space:]]" "$CONF" \
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
REPORTED_FUNC=()

# Every preflight guard has now run. --preflight-only exists so the self-test can drive
# each of them in a second instead of a five-minute suite: these guards are what make
# the denominator trustworthy, and two of them shipped DEAD (a grep exiting 1 under
# `set -e` killed the script before the fault could print). Untested guards ship broken.
if $PREFLIGHT_ONLY; then
    echo -e "${GREEN}preflight OK${RESET}: $(declared_count "$CONF") screenshot + ${#DECLARED_FUNC[@]} functional tests declared, pins agree"
    exit 0
fi

# want <name> — should this test run? (no filter given, or explicitly named)
want() {
    [[ ${#FILTER_TESTS[@]} -eq 0 ]] && return 0
    printf '%s\n' "${FILTER_TESTS[@]}" | grep -qx "$1"
}

# begin_func <name> — register the row and print its label.
begin_func() {
    REPORTED_FUNC+=("$1")
    printf "  %-25s " "[$1]"
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

# The unit-test harness is load-bearing for every number this project quotes, so it
# is itself under test. It shipped once with a bug that only appeared when a suite
# FAILED — the very path nobody exercises when everything is green.
if want harness-selftest-func; then
    begin_func harness-selftest-func
    # Timeout-wrapped like every other invocation here. Its only other time bound would
    # be the per-suite timeout inside the very harness it is testing — circular, and the
    # exact shape of the Task 33 hang.
    if hs_out=$(timeout --foreground --kill-after=5s 120s bash "$PROJECT_DIR/test/harness-selftest.sh" 2>&1); then
        hs_line=$(echo "$hs_out" | grep -E '^Total:' | tail -1)
        echo -e "${GREEN}PASS${RESET} ($hs_line)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (the test harness itself is broken — see below)"
        echo "$hs_out" | grep -E '^\s*FAIL' | head -5 | sed -E 's/^/      /'
        fail=$((fail + 1))
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
        echo -e "${GREEN}PASS${RESET} ($bp_count magic breakpoint(s) detected)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (no magic breakpoint detected in output)"
        fail=$((fail + 1))
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
        echo -e "${GREEN}PASS${RESET} (magic port output verified)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (expected 'Hello from ZX Next!' in magic port output)"
        fail=$((fail + 1))
    fi
fi

# Video recording test: verify --record produces a valid MP4 file
if want video-record-func; then
    begin_func video-record-func
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
if want audio-underrun-func; then
    begin_func audio-underrun-func
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
        # This is the ONE test that cannot use --headless (it needs a real audio
        # path), so it runs the windowed emulator under xvfb-run. But xvfb-run
        # only sets DISPLAY; it leaves WAYLAND_DISPLAY alone, and on a Wayland
        # session Qt/SDL PREFER the Wayland backend -- so the emulator ignored
        # the virtual X display and opened a real window on the user's desktop,
        # once per regression run. Verified by strace: 2 connects to wayland-0.
        #
        # Fix: unset WAYLAND_DISPLAY and force the X11 backends, so Qt renders
        # into the Xvfb display instead of the real compositor.
        #
        # NOT done: pointing WAYLAND_DISPLAY at a dead socket. That does stop the
        # last libwayland probe (libwayland falls back to the default name
        # "wayland-0" when the var is simply unset), but it makes SDL fail to
        # bring up an audio backend, and the test SKIPs with "no audio captured"
        # -- trading a stray window for a silently-disabled test. Not a trade we
        # make: a residual probe connect is harmless; a skipped test is not.
        SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_file" \
        timeout --foreground --kill-after=5s 40s \
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
        xvfb-run -a "$JNEXT" \
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

# --silent test (Task 47): (1) no audio device is ever opened, and (2) the
# frames it renders are pixel-identical to a normal run — muting output must
# not skip, stall, or otherwise perturb CPU/video execution.
#
# Two proven techniques, not a new one:
#   - SDL's `disk` audio driver only ever creates its output file inside
#     SDL_OpenAudioDevice (audio-underrun-func). An ABSENT file is direct
#     proof no device was opened — stronger than grepping a log line, which
#     would still pass if the message were ever renamed while the device
#     kept opening underneath it.
#   - The pixel-compare snapshot-save-func uses to content-verify a reload,
#     used here to content-verify that --silent didn't perturb execution. A
#     test that only checked "the process exited around N seconds" would
#     NOT catch a bug that (wrongly) also stalls run_frame() under --silent:
#     --delayed-automatic-exit's countdown fires on its own schedule in
#     on_frame_tick() regardless of whether run_frame() itself ran — a stuck
#     CPU and a live one both exit "on time". Comparing actual rendered
#     content is what makes the frames-really-ran claim discriminative.
#
# QT_QPA_PLATFORM=offscreen (no xvfb needed — same technique as
# screenshot-paused-func) keeps this fast; SDL's disk driver needs no
# display of its own, unlike audio-underrun-func's real playback check.
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
        echo -e "${YELLOW}SKIP${RESET} (no SDL audio backend available; control run captured nothing)"
        skip=$((skip + 1))
    elif [[ ! -s "$png_normal" || ! -s "$png_silent" ]]; then
        echo -e "${RED}FAIL${RESET} (screenshot missing: normal=$([[ -s "$png_normal" ]] && echo y || echo n) silent=$([[ -s "$png_silent" ]] && echo y || echo n))"
        fail=$((fail + 1))
    elif [[ -e "$raw_silent" ]]; then
        echo -e "${RED}FAIL${RESET} (--silent still opened an audio device)"
        fail=$((fail + 1))
    elif $HAS_COMPARE; then
        diff_raw=$(compare -metric AE "$png_silent" "$png_normal" /dev/null 2>&1) || true
        diff_pixels=$(echo "$diff_raw" | awk '{printf "%d", $1+0}' 2>/dev/null || echo 999999)
        if [[ "$diff_pixels" -eq 0 ]]; then
            echo -e "${GREEN}PASS${RESET} (no audio device opened; frame 150 pixel-identical to a normal run)"
            pass=$((pass + 1))
        else
            echo -e "${RED}FAIL${RESET} (--silent changed rendered output: ${diff_pixels} pixels differ)"
            fail=$((fail + 1))
        fi
    else
        echo -e "${YELLOW}SKIP${RESET} (no ImageMagick — cannot content-verify frame identity)"
        skip=$((skip + 1))
    fi
fi

# --silent + --record test (Task 47 review round 2): reviewer reproduced a
# MAJOR bug — with audio synthesis skipped, audio_tmp_ is a 0-byte file, but
# VideoRecorder::stop() still invoked ffmpeg with that zero-duration raw-PCM
# input plus "-shortest", which clamps the WHOLE output to zero duration:
# ffmpeg exited 0 having written a structurally-valid but EMPTY MP4
# ("Output file is empty, nothing was encoded"), and jnext logged success.
# A test that only checks "the file exists and the process exited 0" is
# EXACTLY what missed this — it must assert on the artifact's content.
#
# Fix: VideoRecorder::stop() now detects the 0-byte audio temp file and
# encodes video-only (no audio input, no "-shortest" — nothing to be the
# shortest OF). Assert the output MP4 has a real video stream with a real
# (non-zero) duration, not just "a file appeared".
if want silent-record-func; then
    begin_func silent-record-func
    rec_file="$TMP_DIR/silent_recording.mp4"
    rm -f "$rec_file"
    if ! command -v ffprobe &>/dev/null; then
        echo -e "${YELLOW}SKIP${RESET} (ffprobe not available for validation)"
        skip=$((skip + 1))
    else
        timeout --foreground --kill-after=5s 20s "$JNEXT" --headless --silent \
            "${SD_CARD_ARGS[@]}" \
            --record "$rec_file" \
            --delayed-automatic-exit 3 &>/dev/null || true
        if [[ ! -s "$rec_file" ]]; then
            echo -e "${RED}FAIL${RESET} (no MP4 file produced)"
            fail=$((fail + 1))
        else
            has_video=$(ffprobe -show_streams "$rec_file" 2>/dev/null | grep -c "codec_type=video" || true)
            duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$rec_file" 2>/dev/null || echo 0)
            duration_ok=$(awk -v d="$duration" 'BEGIN{print (d+0 >= 1.0) ? 1 : 0}')
            if [[ "$has_video" -ge 1 && "$duration_ok" -eq 1 ]]; then
                echo -e "${GREEN}PASS${RESET} (video-only MP4, ${duration}s duration, ${has_video} video stream)"
                pass=$((pass + 1))
            else
                echo -e "${RED}FAIL${RESET} (has_video=$has_video duration=$duration — corrupt/empty recording reported as success)"
                fail=$((fail + 1))
            fi
        fi
    fi
fi

# Bare-filename CLI test (Task 25): `jnext <file>` must load the file exactly as
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
        echo -e "${GREEN}PASS${RESET} (pending capture: error + exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (pending_rc=$pend_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$ctrl_rc)"
        fail=$((fail + 1))
    fi
fi

# --delayed-automatic-exit-frames N (Task 49): the frames form of the exit
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
        echo -e "${GREEN}PASS${RESET} (exit at frame $N exactly; -frames overrides seconds)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (at_rc=$at_rc at_png=$([[ -s "$at_n" ]] && echo y || echo n) past_rc=$past_rc past_png=$([[ -f "$past_n" ]] && echo y || echo n) override_rc=$ovr_rc override_png=$([[ -f "$ovr" ]] && echo y || echo n))"
        fail=$((fail + 1))
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
        echo -e "${GREEN}PASS${RESET} (paused debugger: error + exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (paused_rc=$paused_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$pctrl_rc)"
        fail=$((fail + 1))
    fi
fi

# Third route by which a requested screenshot can fail to appear (Task 34): the
# capture IS taken, but the PNG cannot be WRITTEN (missing directory, no
# permission, disk full). save_screenshot_png() returned false and every caller
# threw the result away, so jnext exited 0 with no PNG. Same contract as the two
# tests above: error log + non-zero exit, each with a positive control.
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
        echo -e "${GREEN}PASS${RESET} (unwritable path: error+reason, exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (io_rc=$io_rc png_exists=$([[ -f "$bad_png" ]] && echo y || echo n) control_rc=$ioctrl_rc)"
        fail=$((fail + 1))
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
        echo -e "${GREEN}PASS${RESET} (Qt unwritable path: error+reason, exit!=0, no PNG; control writes one)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (qt_io_rc=$qio_rc png_exists=$([[ -f "$bad_png" ]] && echo y || echo n) control_rc=$qioctrl_rc)"
        fail=$((fail + 1))
    fi
fi

# --delayed-snapshot (Task 13b, headless-only): save/reload proof plus the
# same "requested but never written" loud-failure contract the
# --delayed-screenshot tests above use (screenshot-pending-func).
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
    # frame. This must be BYTE/PIXEL content-verified, not just "a PNG
    # came out" — Task 13b review round 2 caught a version of this test
    # that still PASSED when the reviewer mutated SzxSaver to write
    # all-zero RAM into every ZXSTRAMPAGE payload (structurally valid,
    # still loads, renders garbage). Compare against snap-orig.png with
    # the same ImageMagick `compare -metric AE` the screenshot tests
    # above use; 0 pixel diff is required (BASIC's copyright screen is
    # static, so the reload must reproduce it exactly).
    if [[ -s "$szx" ]] && timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$szx" \
            --delayed-screenshot "$reloaded_png" --delayed-screenshot-frames 1 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then reload_rc=0; else reload_rc=1; fi

    content_ok=0
    diff_pixels=-1
    if [[ -s "$orig_png" ]] && [[ -s "$reloaded_png" ]]; then
        if $HAS_COMPARE; then
            diff_raw=$(compare -metric AE "$reloaded_png" "$orig_png" /dev/null 2>&1) || true
            diff_pixels=$(echo "$diff_raw" | awk '{printf "%d", $1+0}' 2>/dev/null || echo 999999)
            [[ "$diff_pixels" -eq 0 ]] && content_ok=1
        else
            # No ImageMagick: cannot content-verify. Do NOT silently pass —
            # that is exactly the "advertises coverage that does not exist"
            # failure mode this fix exists to close.
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

    # Negative control (Task 13b redesign): .szx is scoped to 48K/128K/
    # +2A/+3 only — see SzxSaver class doc-comment SCOPE. jnext's DEFAULT
    # --machine is Next, so this is the common path in practice, not an
    # edge case: it must fail loudly (non-zero exit, clear reason logged,
    # no file written), never silently write a truncated/misrepresenting
    # snapshot.
    refused="$TMP_DIR/snap-refused.szx"
    rm -f "$refused"
    if out_next=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-snapshot "$refused" --delayed-snapshot-frames 5 \
                --delayed-automatic-exit 3 2>&1)
    then refuse_rc=0; else refuse_rc=1; fi

    if [[ "$content_ok" -eq -1 ]]; then
        echo -e "${YELLOW}SKIP${RESET} (no ImageMagick — cannot content-verify the reload)"
        skip=$((skip + 1))
    elif [[ "$save_rc" -eq 0 ]] && [[ -s "$szx" ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reloaded_png" ]] \
       && [[ "$content_ok" -eq 1 ]] \
       && [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$pending" ]] \
       && echo "$out" | grep -q "NO snapshot was written" \
       && [[ "$refuse_rc" -ne 0 ]] && [[ ! -f "$refused" ]] \
       && echo "$out_next" | grep -qi "cannot represent this machine"; then
        echo -e "${GREEN}PASS${RESET} (reload pixel-identical to pre-save screen; pending-never-written: error+exit!=0, no file; --machine next refused: error+exit!=0, no file)"
        pass=$((pass + 1))
    else
        echo -e "${RED}FAIL${RESET} (save_rc=$save_rc szx_exists=$([[ -s "$szx" ]] && echo y || echo n) reload_rc=$reload_rc png_exists=$([[ -s "$reloaded_png" ]] && echo y || echo n) content_ok=$content_ok diff_pixels=$diff_pixels pend_rc=$pend_rc pending_exists=$([[ -f "$pending" ]] && echo y || echo n) refuse_rc=$refuse_rc refused_exists=$([[ -f "$refused" ]] && echo y || echo n))"
        fail=$((fail + 1))
    fi
fi

# Rewind / backwards execution unit tests.
# This block used to be wrapped in `if [[ -x "$REWIND_TEST" ]]`, so after a `make clean`
# the test simply stopped existing — no PASS, no FAIL, no SKIP, and a suite total that
# quietly dropped by one (Task 35). The binary's absence is now caught in the preflight,
# so by the time we get here it is there and the only question is whether it passes.
if want rewind-func; then
    begin_func rewind-func
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

echo ""
echo -e "${BOLD}=== Results ===${RESET}"
echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"

# --- Completeness: prove the suite ran everything it declares ---
# A green result is only as trustworthy as its denominator. On a full run, every
# declared functional test must have reported exactly one row, no undeclared row
# may appear, and the grand total must equal 1 (lint) + screenshots + functional.
# Anything else means a test went missing, which is a harness fault, not a pass.
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] && ! $UPDATE_MODE; then
    faults=()
    for name in "${DECLARED_FUNC[@]}"; do
        n=$(printf '%s\n' "${REPORTED_FUNC[@]}" | grep -cx "$name" || true)
        [[ "$n" -eq 1 ]] || faults+=("declared in functional_tests.conf but reported $n rows: ${BOLD}$name${RESET}")
    done
    for name in "${REPORTED_FUNC[@]}"; do
        printf '%s\n' "${DECLARED_FUNC[@]}" | grep -qx "$name" \
            || faults+=("reported a row but is NOT declared in functional_tests.conf: ${BOLD}$name${RESET}")
    done
    expected=$(( 1 + ${#ORDERED_TESTS[@]} + ${#DECLARED_FUNC[@]} ))
    actual=$(( pass + fail + skip ))
    [[ "$actual" -eq "$expected" ]] \
        || faults+=("row count is ${BOLD}$actual${RESET}, but 1 lint + ${#ORDERED_TESTS[@]} screenshot + ${#DECLARED_FUNC[@]} functional = ${BOLD}$expected${RESET} were declared")
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
