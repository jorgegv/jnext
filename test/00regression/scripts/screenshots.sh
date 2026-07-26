#!/usr/bin/env bash
# Group script: the whole screenshot suite — parallel launcher, throttle,
# evaluation loop and --update reference regeneration.
# Sourced by regression.sh (the driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Diff artifacts and --update references are written here.
mkdir -p "$IMG_DIR"

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

    # Append extra CLI arguments (e.g. --delayed-keypress 2 0).
    #
    # `@private-sd` is a HARNESS sentinel, not a jnext flag: it is stripped
    # here and gives this row its own SD-card clone instead of the run-wide
    # one. Needed by any row whose guest WRITES to the card — jnext opens the
    # image read-write and NextZXOS writes back, and every other row in the
    # run resolves the SAME $RUN_DIR/sdcard image (see the JNEXT_CONFIG_DIR
    # note in test-functions.inc). The run-wide clone protects the MASTER from
    # the run; this protects the other ROWS from this row.
    #
    # Proven, not theoretical (GH #113): boot-nextzxos-cpm drives the NextZXOS
    # CP/M loader, which imports 31 files onto the card. Without this, the
    # later-running tape-save-boot-func and sdcard-readonly-func rows booted
    # the mutated image and their frame-400 welcome comparison failed — a
    # reproducible 2-row FAIL that looks exactly like a rendering regression.
    #
    # Opt-in rather than always-on: the clone is a reflink on the dev host but
    # degrades to a real 1 GB copy where reflink is unavailable (CI containers),
    # so only rows that need it pay.
    row_env=()
    if [[ -n "$extra_args" ]]; then
        read -ra extra_array <<< "$extra_args"
        for extra_arg in "${extra_array[@]}"; do
            if [[ "$extra_arg" == "@private-sd" ]]; then
                priv_cfg="$RUN_DIR/private/$test_name"
                mkdir -p "$priv_cfg/sdcard"
                cp --reflink=auto \
                   "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                   "$priv_cfg/sdcard/cspect-next-1gb-fixed.img"
                row_env=(env "JNEXT_CONFIG_DIR=$priv_cfg")
            else
                cmd+=("$extra_arg")
            fi
        done
    fi

    # Launch in background
    "${row_env[@]}" "${cmd[@]}" &>/dev/null &

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

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
